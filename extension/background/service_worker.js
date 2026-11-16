// ===========================================================================
//  S-DOWNLOAD MANAGER (SDM)
//  extension/background/service_worker.js
//
//  Service worker Manifest V3 (spec cap. 7 y 10):
//   - Canal primario: Native Messaging (STDIN/STDOUT) con com.sdm.bridge.
//   - Canal secundario: WebSocket a 127.0.0.1:puerto con token (?token=).
//   - Intercepcion de descargas del navegador -> addDownload / startStream.
//   - Refresco de enlaces caducados (spec cap. 10): pestaña oculta + sniff.
//   - Deteccion de transmisiones HLS/DASH -> startStream.
//
//  Protocolo JSON: { "type": "...", "ref": "...", "payload": { ... } }
//  (documentado en docs/PROTOCOL.md; servidor en src/bridge/ProtocolHandler.cpp)
// ===========================================================================

"use strict";

const NATIVE_HOST_NAME = "com.sdm.bridge";
const STORAGE_KEY = "sdmConfig";
const WS_RECONNECT_MIN_MS = 1500;
const WS_RECONNECT_MAX_MS = 30000;
const PING_INTERVAL_MS = 20000;
const REFRESH_TIMEOUT_MS = 15000;

const DEFAULT_CONFIG = {
  enabled: true,          // captura activa on/off
  autoStart: true,        // arrancar tareas automaticamente
  threads: 0,             // 0 = automatico (decide el motor)
  connectionKey: "",      // "puerto:token" del puente WS (fallback)
  captureMedia: true,     // interceptar .m3u8/.mpd como stream
  autoRefreshUrls: true,  // refresco de enlaces caducados (spec cap. 10)
  blockedSites: [],       // dominios donde NO capturar
  captureExtensions: [
    "zip", "rar", "7z", "tar", "gz", "bz2", "xz", "iso", "exe", "msi", "msix",
    "apk", "deb", "rpm", "dmg", "pkg", "pdf", "epub", "doc", "docx", "xls",
    "xlsx", "ppt", "pptx", "mp3", "flac", "wav", "m4a", "aac", "ogg", "opus",
    "mp4", "mkv", "avi", "mov", "wmv", "flv", "webm", "m4v", "ts", "img",
    "torrent", "csv", "sql", "bak", "jar", "wasm", "crx"
  ]
};

// ---------------------------------------------------------------------------
// Estado del service worker
// ---------------------------------------------------------------------------

let config = { ...DEFAULT_CONFIG };
let nativePort = null;
let nativeFailed = false;
let ws = null;
let wsRetryDelay = WS_RECONNECT_MIN_MS;
let wsRetryTimer = null;
let pingTimer = null;
let refCounter = 0;
let lastStatus = { active: 0, queued: 0, total: 0, speed: 0 };
let connectionState = "idle"; // idle | native | ws | disconnected
let lastToast = null;

// ---------------------------------------------------------------------------
// Utilidades
// ---------------------------------------------------------------------------

function newRef(prefix) {
  refCounter = (refCounter + 1) % 1000000;
  return `${prefix}${Date.now().toString(36)}_${refCounter}`;
}

function log(...args) {
  console.debug("[SDM]", ...args);
}

async function loadConfig() {
  const stored = await chrome.storage.local.get(STORAGE_KEY);
  config = { ...DEFAULT_CONFIG, ...(stored[STORAGE_KEY] || {}) };
}

async function saveConfig(patch) {
  config = { ...config, ...patch };
  await chrome.storage.local.set({ [STORAGE_KEY]: config });
}

function parseConnectionKey(key) {
  if (typeof key !== "string") return null;
  const idx = key.indexOf(":");
  if (idx <= 0) return null;
  const port = parseInt(key.slice(0, idx), 10);
  const token = key.slice(idx + 1).trim();
  if (!Number.isFinite(port) || port <= 0 || port > 65535) return null;
  if (token.length < 16) return null;
  return { port, token };
}

// Guarda el par puerto:token que entrega SDM (por el host nativo) para que el
// canal WebSocket de respaldo quede configurado solo, sin intervencion humana.
async function adoptConnectionKey(port, token) {
  if (!Number.isFinite(port) || port <= 0 || port > 65535) return;
  if (typeof token !== "string" || token.length < 16) return;
  const key = `${port}:${token}`;
  if (key === config.connectionKey) return;
  await saveConfig({ connectionKey: key });
  log(`Clave del puente aprendida automaticamente (ws://127.0.0.1:${port})`);
}

function isBlockedUrl(url) {
  try {
    const host = new URL(url).hostname.toLowerCase();
    return (config.blockedSites || []).some((entry) => {
      const site = String(entry || "").trim().toLowerCase();
      if (!site) return false;
      return host === site || host.endsWith("." + site);
    });
  } catch {
    return false;
  }
}

function fileExtensionOf(url) {
  try {
    const path = new URL(url).pathname;
    const last = path.substring(path.lastIndexOf("/") + 1);
    const dot = last.lastIndexOf(".");
    if (dot <= 0) return "";
    return last.substring(dot + 1).toLowerCase();
  } catch {
    return "";
  }
}

function looksLikeMediaManifest(url) {
  const ext = fileExtensionOf(url);
  return ext === "m3u8" || ext === "mpd" || ext === "m3u";
}

function shouldCaptureExtension(url) {
  const ext = fileExtensionOf(url);
  if (!ext) return false;
  return (config.captureExtensions || []).includes(ext);
}

async function cookiesForUrl(url) {
  try {
    const cookies = await chrome.cookies.getAll({ url });
    return cookies.map((c) => `${c.name}=${c.value}`).join("; ");
  } catch {
    return "";
  }
}

function currentTabInfo() {
  return new Promise((resolve) => {
    chrome.tabs.query({ active: true, currentWindow: true }, (tabs) => {
      resolve(tabs && tabs[0] ? tabs[0] : null);
    });
  });
}

// ---------------------------------------------------------------------------
// Canal con SDM: Native Messaging (primario)
// ---------------------------------------------------------------------------

function sendHello() {
  // El host nativo ya es un canal de confianza (lo lanzo el navegador para
  // esta extension), asi que el saludo solo aporta el token si lo conocemos.
  const parsed = parseConnectionKey(config.connectionKey);
  if (parsed) {
    postRaw({ type: "hello", ref: newRef("h"), payload: { token: parsed.token } });
  }
  // Pide a SDM puerto + token para dejar listo el respaldo WebSocket.
  postRaw({ type: "getBridgeInfo", ref: newRef("g") });
}

function connectNative() {
  if (nativePort || nativeFailed) return;
  try {
    nativePort = chrome.runtime.connectNative(NATIVE_HOST_NAME);
    nativePort.onMessage.addListener(onSdmMessage);
    nativePort.onDisconnect.addListener(() => {
      const err = chrome.runtime.lastError;
      if (err) log("Native host desconectado:", err.message);
      nativePort = null;
      // Se marca el canal nativo como no disponible para que ensureChannel()
      // abra YA el WebSocket (que SDM autoconfigura) en vez de quedarse en
      // bucle reintentando un host que no responde.
      nativeFailed = true;
      if (connectionState === "native") connectionState = "disconnected";
      updateBadge();
      ensureChannel();
      setTimeout(() => {
        nativeFailed = false; // reintento suave del canal nativo
        ensureChannel();
      }, 30000);
    });
    connectionState = "native";
    updateBadge();
    log("Native messaging conectado");
    sendHello();
    flushQueue();
  } catch (e) {
    log("connectNative fallo:", e && e.message);
    nativePort = null;
    nativeFailed = true;
  }
}

// ---------------------------------------------------------------------------
// Canal con SDM: WebSocket local (secundario / fallback)
// ---------------------------------------------------------------------------

function connectWebSocket() {
  if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) {
    return;
  }
  const parsed = parseConnectionKey(config.connectionKey);
  if (!parsed) return;

  try {
    ws = new WebSocket(`ws://127.0.0.1:${parsed.port}/?token=${encodeURIComponent(parsed.token)}`);
  } catch (e) {
    log("WebSocket fallo:", e && e.message);
    ws = null;
    scheduleWsRetry();
    return;
  }

  ws.onopen = () => {
    connectionState = "ws";
    wsRetryDelay = WS_RECONNECT_MIN_MS;
    updateBadge();
    log("WebSocket del puente conectado");
    // El handshake del host espera el formato comun {type, ref, payload}.
    postRaw({ type: "hello", ref: newRef("h"), payload: { token: parsed.token } });
    flushQueue();
    startPing();
  };
  ws.onmessage = (event) => {
    let parsed2 = null;
    try {
      parsed2 = JSON.parse(event.data);
    } catch {
      return;
    }
    onSdmMessage(parsed2);
  };
  ws.onclose = () => {
    ws = null;
    stopPing();
    if (connectionState === "ws") connectionState = "disconnected";
    updateBadge();
    scheduleWsRetry();
  };
  ws.onerror = () => {
    // onclose se encarga del reintento.
  };
}

function scheduleWsRetry() {
  if (wsRetryTimer) return;
  if (!parseConnectionKey(config.connectionKey)) return;
  wsRetryTimer = setTimeout(() => {
    wsRetryTimer = null;
    connectWebSocket();
  }, wsRetryDelay);
  wsRetryDelay = Math.min(wsRetryDelay * 2, WS_RECONNECT_MAX_MS);
}

function startPing() {
  stopPing();
  pingTimer = setInterval(() => {
    postRaw({ type: "ping", ref: newRef("p") });
  }, PING_INTERVAL_MS);
}

function stopPing() {
  if (pingTimer) {
    clearInterval(pingTimer);
    pingTimer = null;
  }
}

// ---------------------------------------------------------------------------
// Envio unificado de mensajes
// ---------------------------------------------------------------------------

function isConnected() {
  return (nativePort && !nativeFailed) || (ws && ws.readyState === WebSocket.OPEN);
}

function postRaw(msg) {
  try {
    if (nativePort) {
      nativePort.postMessage(msg);
      return true;
    }
    if (ws && ws.readyState === WebSocket.OPEN) {
      ws.send(JSON.stringify(msg));
      return true;
    }
  } catch (e) {
    log("postMessage fallo:", e && e.message);
  }
  return false;
}

const outbox = [];

function sendToSdm(msg) {
  if (!msg.ref) msg.ref = newRef("e");
  if (!postRaw(msg)) {
    outbox.push(msg);
    if (outbox.length > 200) outbox.shift();
    ensureChannel();
    return false;
  }
  return true;
}

function flushQueue() {
  while (outbox.length > 0 && isConnected()) {
    const msg = outbox.shift();
    postRaw(msg);
  }
}

function ensureChannel() {
  if (isConnected()) return;
  connectNative();
  if (!isConnected()) connectWebSocket();
  updateBadge();
}

function disconnectAll() {
  if (nativePort) {
    try {
      nativePort.disconnect();
    } catch { /* ya cerrado */ }
    nativePort = null;
  }
  if (ws) {
    try {
      ws.close();
    } catch { /* ya cerrado */ }
    ws = null;
  }
  stopPing();
  connectionState = "disconnected";
  updateBadge();
}

// ---------------------------------------------------------------------------
// Badge de estado (spec: indicador de conexion)
// ---------------------------------------------------------------------------

function updateBadge() {
  const connected = isConnected();
  if (connected) {
    chrome.action.setBadgeBackgroundColor({ color: "#107C10" });
    chrome.action.setBadgeText({ text: "ON" });
  } else {
    chrome.action.setBadgeBackgroundColor({ color: "#C42B1C" });
    chrome.action.setBadgeText({ text: "OFF" });
  }
}

// ---------------------------------------------------------------------------
// Mensajes entrantes desde SDM
// ---------------------------------------------------------------------------

function onSdmMessage(msg) {
  if (!msg || typeof msg !== "object") return;
  switch (msg.type) {
    case "helloAck":
      log("SDM version", msg.payload && msg.payload.version);
      connectionState = nativePort ? "native" : "ws";
      updateBadge();
      break;

    case "bridgeInfo":
      // SDM publica aqui el puerto y el token del WebSocket local: asi el
      // canal de respaldo queda configurado sin que el usuario pegue nada.
      if (msg.payload) {
        adoptConnectionKey(Number(msg.payload.port), msg.payload.token);
      }
      break;

    case "pong":
      break;

    case "status":
      lastStatus = msg.payload || lastStatus;
      break;

    case "taskProgress":
    case "taskAdded":
    case "taskRemoved":
    case "tasks":
      // El popup consulta el estado via getState; aqui solo registramos.
      break;

    case "toast":
      lastToast = {
        message: (msg.payload && msg.payload.message) || "",
        level: (msg.payload && msg.payload.level) || 0,
        ts: Date.now()
      };
      break;

    case "refreshUrl":
      // Spec cap. 10: enlace caducado -> abrir origen y capturar el nuevo.
      handleRefreshUrl(msg.payload || {});
      break;

    case "streamVariants":
      // El selector de calidad lo muestra la UI nativa de SDM.
      break;

    case "error":
      log("SDM error:", msg.payload && msg.payload.message);
      break;

    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// Refresco de enlaces caducados (spec cap. 10)
// ---------------------------------------------------------------------------

async function handleRefreshUrl(payload) {
  const taskId = payload.taskId;
  const originUrl = payload.originUrl || "";
  if (!taskId || !originUrl) return;

  const captured = await sniffDownloadUrl(originUrl, payload.fileName || "");
  let url = "";
  let cookies = "";
  if (captured) {
    url = captured;
    cookies = await cookiesForUrl(captured);
  }
  sendToSdm({
    type: "urlUpdated",
    ref: newRef("r"),
    payload: { taskId, url, cookies, referer: originUrl }
  });
}

function sniffDownloadUrl(originUrl, fileName) {
  return new Promise((resolve) => {
    let done = false;
    let tabId = null;
    let listener = null;
    const targetExt = (fileName.split(".").pop() || "").toLowerCase();

    const finish = (result) => {
      if (done) return;
      done = true;
      clearTimeout(timer);
      if (listener) {
        try {
          chrome.webRequest.onBeforeRequest.removeListener(listener);
        } catch { /* sin listener */ }
      }
      if (tabId !== null) {
        try {
          chrome.tabs.remove(tabId);
        } catch { /* pestana ya cerrada */ }
      }
      resolve(result);
    };

    listener = (details) => {
      if (done) return;
      if (details.tabId !== tabId) return;
      const url = details.url || "";
      if (!url || url.startsWith("data:") || url.startsWith("blob:")) return;
      const ext = fileExtensionOf(url);
      if (targetExt && targetExt !== "html" && ext === targetExt) {
        finish(url);
        return;
      }
      if (!targetExt && (details.type === "xmlhttprequest" || details.type === "other") &&
          !looksLikeMediaManifest(url)) {
        // Sin nombre conocido: nos quedamos con el primer recurso descargable.
        finish(url);
      }
    };

    chrome.webRequest.onBeforeRequest.addListener(
      listener,
      { urls: ["<all_urls>"], types: ["xmlhttprequest", "other", "media", "sub_frame"] },
      []
    );

    chrome.tabs.create({ url: originUrl, active: false }, (tab) => {
      tabId = tab && tab.id != null ? tab.id : null;
    });

    const timer = setTimeout(() => finish(null), REFRESH_TIMEOUT_MS);
  });
}

// ---------------------------------------------------------------------------
// Menu contextual "Descargar con SDM" (estilo IDM)
// ---------------------------------------------------------------------------

const MENU_IDS = {
  link: "sdm-dl-link",
  media: "sdm-dl-media",
  all: "sdm-dl-all"
};

function setupContextMenus() {
  try {
    chrome.contextMenus.removeAll(() => {
      chrome.contextMenus.create({
        id: MENU_IDS.link,
        title: "Descargar este enlace con SDM",
        contexts: ["link"]
      });
      chrome.contextMenus.create({
        id: MENU_IDS.media,
        title: "Descargar este vídeo/imagen con SDM",
        contexts: ["video", "audio", "image"]
      });
      chrome.contextMenus.create({
        id: MENU_IDS.all,
        title: "Descargar todos los enlaces de la página con SDM",
        contexts: ["page"]
      });
    });
  } catch (e) {
    log("contextMenus no disponible:", e && e.message);
  }
}

// Encola una descarga en SDM con cookies + referer (compartido por menú,
// popup y captura de descargas del navegador).
async function queueDownload(url, referer, fileName) {
  if (!url || isBlockedUrl(url)) return false;
  ensureChannel();
  const cookies = await cookiesForUrl(url);
  const name = fileName || decodeURIComponent(String(url).split("?")[0].split("/").pop() || "");
  if (looksLikeMediaManifest(url)) {
    return sendToSdm({
      type: "startStream",
      ref: newRef("s"),
      payload: { url, destDir: "", referer, cookies, fileName: name }
    });
  }
  return sendToSdm({
    type: "addDownload",
    ref: newRef("a"),
    payload: {
      url,
      referer,
      cookies,
      userAgent: (typeof navigator !== "undefined" && navigator.userAgent) || "",
      fileName: name,
      destDir: "",
      pageUrl: referer,
      httpUser: "",
      httpPass: "",
      threads: config.threads || 0,
      autoStart: config.autoStart !== false
    }
  });
}

chrome.contextMenus.onClicked.addListener(async (info, tab) => {
  if (!config.enabled) return;
  const pageUrl = info.pageUrl || (tab && tab.url) || "";

  if (info.menuItemId === MENU_IDS.link && info.linkUrl) {
    await queueDownload(info.linkUrl, pageUrl);
    return;
  }
  if (info.menuItemId === MENU_IDS.media && info.srcUrl) {
    await queueDownload(info.srcUrl, pageUrl);
    return;
  }
  if (info.menuItemId === MENU_IDS.all && tab && tab.id != null) {
    chrome.tabs.sendMessage(tab.id, { type: "sdm:collectLinks" }, async (response) => {
      if (chrome.runtime.lastError || !response || !response.ok) {
        lastToast = { message: "No se pudieron recoger los enlaces de la página", level: 2, ts: Date.now() };
        return;
      }
      const links = (response.links || []).filter((u) => shouldCaptureExtension(u) || looksLikeMediaManifest(u));
      for (const url of links.slice(0, 100)) {
        await queueDownload(url, pageUrl);
      }
      lastToast = {
        message: links.length > 0 ? `${Math.min(links.length, 100)} enlaces enviados a SDM` : "Sin enlaces descargables en la página",
        level: links.length > 0 ? 0 : 1,
        ts: Date.now()
      };
    });
  }
});

// ---------------------------------------------------------------------------
// Intercepcion de descargas del navegador
// ---------------------------------------------------------------------------

chrome.downloads.onCreated.addListener(async (item) => {
  if (!config.enabled) return;
  if (!item || !item.url) return;
  if (item.byExtensionId === chrome.runtime.id) return; // descargas propias
  const url = item.finalUrl || item.url;
  if (isBlockedUrl(url)) return;

  const isManifest = looksLikeMediaManifest(url);
  if (isManifest && !config.captureMedia) return;
  if (!isManifest && !shouldCaptureExtension(url)) return;

  // Seguridad: nunca cancelar si SDM no esta accesible (evita perder la
  // descarga del usuario); en ese caso la dejamos al navegador.
  ensureChannel();
  if (!isConnected()) {
    log("SDM no conectado; se ignora la captura de", url);
    return;
  }

  try {
    await chrome.downloads.cancel(item.id);
    await chrome.downloads.erase({ id: item.id });
  } catch (e) {
    log("cancel/erase fallo:", e && e.message);
    return;
  }

  const cookies = await cookiesForUrl(url);
  const referer = item.referrer || "";
  const userAgent = (typeof navigator !== "undefined" && navigator.userAgent) || "";

  if (isManifest) {
    sendToSdm({
      type: "startStream",
      ref: newRef("s"),
      payload: {
        url,
        destDir: "",
        referer,
        cookies,
        fileName: (item.filename || "").split(/[\\/]/).pop() || ""
      }
    });
  } else {
    sendToSdm({
      type: "addDownload",
      ref: newRef("a"),
      payload: {
        url,
        referer,
        cookies,
        userAgent,
        fileName: (item.filename || "").split(/[\\/]/).pop() || "",
        destDir: "",
        pageUrl: referer,
        httpUser: "",
        httpPass: "",
        threads: config.threads || 0,
        autoStart: config.autoStart !== false
      }
    });
  }
});

// ---------------------------------------------------------------------------
// Mensajes desde popup / content scripts
// ---------------------------------------------------------------------------

chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
  if (!message || typeof message !== "object") return;

  switch (message.type) {
    case "sdm:getState":
      ensureChannel();
      sendResponse({
        connected: isConnected(),
        connectionState,
        enabled: config.enabled,
        autoStart: config.autoStart,
        threads: config.threads,
        captureMedia: config.captureMedia,
        autoRefreshUrls: config.autoRefreshUrls,
        blockedSites: config.blockedSites,
        captureExtensions: config.captureExtensions,
        connectionKey: config.connectionKey,
        status: lastStatus,
        lastToast
      });
      return false;

    case "sdm:setConfig":
      saveConfig(message.payload || {}).then(() => {
        updateBadge();
        sendResponse({ ok: true, config });
      });
      return true; // respuesta asincrona

    case "sdm:connect":
      saveConfig({ connectionKey: (message.payload && message.payload.connectionKey) || config.connectionKey })
        .then(() => {
          disconnectAll();
          nativeFailed = false;
          ensureChannel();
          setTimeout(() => sendResponse({ ok: true, connected: isConnected() }), 600);
        });
      return true;

    case "sdm:disconnect":
      disconnectAll();
      sendResponse({ ok: true });
      return false;

    case "sdm:addDownload": {
      const p = (message.payload && message.payload.url) ? message.payload : { url: message.payload };
      if (!p.url) {
        sendResponse({ ok: false, error: "Falta la URL" });
        return false;
      }
      const cookiesPromise = cookiesForUrl(p.url);
      cookiesPromise.then((cookies) => {
        const ok = sendToSdm({
          type: "addDownload",
          ref: newRef("m"),
          payload: {
            url: p.url,
            referer: p.referer || "",
            cookies,
            userAgent: (typeof navigator !== "undefined" && navigator.userAgent) || "",
            fileName: p.fileName || "",
            destDir: p.destDir || "",
            pageUrl: p.referer || "",
            httpUser: p.httpUser || "",
            httpPass: p.httpPass || "",
            threads: config.threads || 0,
            autoStart: config.autoStart !== false
          }
        });
        sendResponse({ ok });
      });
      return true;
    }

    case "sdm:startStream": {
      const p = message.payload || {};
      if (!p.url) {
        sendResponse({ ok: false, error: "Falta la URL" });
        return false;
      }
      cookiesForUrl(p.url).then((cookies) => {
        const ok = sendToSdm({
          type: "startStream",
          ref: newRef("st"),
          payload: {
            url: p.url,
            destDir: "",
            referer: p.referer || "",
            cookies,
            fileName: p.title || ""
          }
        });
        sendResponse({ ok });
      });
      return true;
    }

    case "sdm:clearCompleted":
      sendToSdm({ type: "clearCompleted", ref: newRef("c") });
      sendResponse({ ok: true });
      return false;

    case "sdm:downloadUrl": {
      const p = message.payload || {};
      if (!p.url) {
        sendResponse({ ok: false, error: "Falta la URL" });
        return false;
      }
      queueDownload(p.url, p.referer || "", p.fileName || "").then((ok) => {
        sendResponse({ ok });
      });
      return true;
    }

    default:
      return false;
  }
});

// ---------------------------------------------------------------------------
// Arranque
// ---------------------------------------------------------------------------

chrome.runtime.onInstalled.addListener(() => {
  loadConfig().then(() => {
    updateBadge();
    ensureChannel();
    setupContextMenus();
  });
});

chrome.runtime.onStartup.addListener(() => {
  loadConfig().then(() => {
    updateBadge();
    ensureChannel();
  });
});

chrome.storage.onChanged.addListener((changes, area) => {
  if (area === "local" && changes[STORAGE_KEY]) {
    config = { ...DEFAULT_CONFIG, ...(changes[STORAGE_KEY].newValue || {}) };
    updateBadge();
  }
});

// Alarmas: reintentar conexion si el SW revive sin socket (MV3 duerme el SW).
chrome.alarms.onAlarm.addListener((alarm) => {
  if (alarm && alarm.name === "sdm:keepalive") {
    if (!isConnected()) ensureChannel();
  }
});

chrome.alarms.create("sdm:keepalive", { periodInMinutes: 0.5 });

loadConfig().then(() => {
  updateBadge();
  ensureChannel();
});
