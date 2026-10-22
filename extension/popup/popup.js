// ============================================================================
//  S-DOWNLOAD MANAGER (SDM)
//  extension/popup/popup.js
//
//  Panel rapido: estado del puente, estadisticas globales, ajustes de
//  captura y exclusiones por sitio.  Toda la logica de red vive en el
//  service worker; aqui solo se habla via chrome.runtime.sendMessage.
// ============================================================================

"use strict";

const $ = (id) => document.getElementById(id);

function fmtSpeed(bytes) {
  const units = ["B/s", "KB/s", "MB/s", "GB/s"];
  let value = Number(bytes) || 0;
  let unit = 0;
  while (value >= 1024 && unit < units.length - 1) {
    value /= 1024;
    unit++;
  }
  return `${value.toFixed(unit === 0 ? 0 : 1)} ${units[unit]}`;
}

function send(message) {
  return new Promise((resolve) => {
    chrome.runtime.sendMessage(message, (response) => {
      if (chrome.runtime.lastError) {
        resolve({ ok: false, error: chrome.runtime.lastError.message });
      } else {
        resolve(response || { ok: false });
      }
    });
  });
}

function renderState(state) {
  if (!state) return;

  const label = $("connection-label");
  if (state.connected) {
    label.textContent = state.connectionState === "native" ? "Conectado (nativo)" : "Conectado (WebSocket)";
    label.className = "connection ok";
  } else {
    label.textContent = "SDM no conectado";
    label.className = "connection bad";
  }

  $("enabled-toggle").checked = !!state.enabled;
  $("autostart-toggle").checked = !!state.autoStart;
  $("media-toggle").checked = !!state.captureMedia;
  $("refresh-toggle").checked = !!state.autoRefreshUrls;
  $("threads-select").value = String(state.threads || 0);
  $("key-input").value = state.connectionKey || "";

  if (state.status) {
    $("stat-active").textContent = String(state.status.active ?? 0);
    $("stat-queued").textContent = String(state.status.queued ?? 0);
    $("stat-total").textContent = String(state.status.total ?? 0);
    $("stat-speed").textContent = fmtSpeed(state.status.speed);
  }

  if (state.lastToast && Date.now() - state.lastToast.ts < 60000) {
    $("toast-line").textContent = state.lastToast.message;
  }

  renderBlocked(state.blockedSites || []);
}

function renderBlocked(sites) {
  const list = $("blocked-list");
  list.innerHTML = "";
  if (sites.length === 0) {
    const li = document.createElement("li");
    li.innerHTML = "<span style='color:#c5c5c5'>Ningún sitio excluido</span>";
    list.appendChild(li);
    return;
  }
  for (const site of sites) {
    const li = document.createElement("li");
    const span = document.createElement("span");
    span.textContent = site;
    const remove = document.createElement("button");
    remove.textContent = "Quitar";
    remove.addEventListener("click", async () => {
      const current = await send({ type: "sdm:getState" });
      const blocked = (current.blockedSites || []).filter((s) => s !== site);
      await send({ type: "sdm:setConfig", payload: { blockedSites: blocked } });
      renderBlocked(blocked);
    });
    li.appendChild(span);
    li.appendChild(remove);
    list.appendChild(li);
  }
}

async function refresh() {
  const state = await send({ type: "sdm:getState" });
  renderState(state);
}

// --- Acciones ---------------------------------------------------------------

function bindControls() {
  $("enabled-toggle").addEventListener("change", async (e) => {
    await send({ type: "sdm:setConfig", payload: { enabled: e.target.checked } });
  });

  $("autostart-toggle").addEventListener("change", async (e) => {
    await send({ type: "sdm:setConfig", payload: { autoStart: e.target.checked } });
  });

  $("media-toggle").addEventListener("change", async (e) => {
    await send({ type: "sdm:setConfig", payload: { captureMedia: e.target.checked } });
  });

  $("refresh-toggle").addEventListener("change", async (e) => {
    await send({ type: "sdm:setConfig", payload: { autoRefreshUrls: e.target.checked } });
  });

  $("threads-select").addEventListener("change", async (e) => {
    await send({ type: "sdm:setConfig", payload: { threads: parseInt(e.target.value, 10) || 0 } });
  });

  $("quick-add").addEventListener("click", async () => {
    const url = $("quick-url").value.trim();
    const feedback = $("quick-feedback");
    if (!url) {
      feedback.textContent = "Introduce una URL.";
      feedback.className = "feedback err";
      return;
    }
    const response = await send({ type: "sdm:addDownload", payload: { url } });
    if (response.ok) {
      feedback.textContent = "Tarea enviada a SDM.";
      feedback.className = "feedback ok";
      $("quick-url").value = "";
    } else {
      feedback.textContent = response.error || "No se pudo enviar la tarea.";
      feedback.className = "feedback err";
    }
  });

  $("blocked-add").addEventListener("click", async () => {
    const input = $("blocked-input");
    const site = input.value.trim().toLowerCase().replace(/^https?:\/\//, "").split("/")[0];
    if (!site) return;
    const current = await send({ type: "sdm:getState" });
    const blocked = current.blockedSites || [];
    if (!blocked.includes(site)) {
      blocked.push(site);
      await send({ type: "sdm:setConfig", payload: { blockedSites: blocked } });
    }
    input.value = "";
    renderBlocked(blocked);
  });

  $("key-save").addEventListener("click", async () => {
    const feedback = $("bridge-feedback");
    const key = $("key-input").value.trim();
    const response = await send({ type: "sdm:connect", payload: { connectionKey: key } });
    feedback.textContent = response.connected
      ? "Conectado al puente de SDM."
      : "Clave guardada; sin conexión todavía (¿SDM abierto?).";
    feedback.className = response.connected ? "feedback ok" : "feedback err";
    setTimeout(refresh, 800);
  });

  $("key-test").addEventListener("click", async () => {
    const feedback = $("bridge-feedback");
    feedback.textContent = "Probando…";
    feedback.className = "feedback";
    const response = await send({ type: "sdm:connect", payload: { connectionKey: $("key-input").value.trim() } });
    feedback.textContent = response.connected ? "Conexión correcta." : "Fallo de conexión.";
    feedback.className = response.connected ? "feedback ok" : "feedback err";
    setTimeout(refresh, 800);
  });

  // Enter en el campo de URL = añadir.
  $("quick-url").addEventListener("keydown", (e) => {
    if (e.key === "Enter") $("quick-add").click();
  });
}

document.addEventListener("DOMContentLoaded", () => {
  bindControls();
  refresh();
  setInterval(refresh, 2000);
});
