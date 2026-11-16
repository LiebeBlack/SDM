// ============================================================================
//  S-DOWNLOAD MANAGER (SDM)
//  extension/content/content_script.js
//
//  Boton flotante estilo "descargar este video" (spec cap. 4 y 9):
//   - Se ancla al reproductor visible (<video>) y se recoloca al hacer scroll,
//     redimensionar o mover el reproductor; si la deteccion vino de la red y no
//     hay reproductor, cae a la esquina inferior derecha.
//   - Vive en un Shadow DOM: el CSS de la web no puede romperlo ni ocultarlo.
//   - Al pulsarlo envia "sdm:startStream" al service worker, que crea la tarea
//     en SDM (canal nativo y, si falla, WebSocket ya autoconfigurado).
// ============================================================================

"use strict";

(function () {
  if (window.__sdmContentLoaded) return;
  window.__sdmContentLoaded = true;

  const KIND_LABEL = { hls: "HLS", dash: "DASH", file: "Vídeo" };
  const DIM_AFTER_MS = 6000;

  let host = null;
  let shadow = null;
  let button = null;
  let label = null;
  let hideTimer = null;
  let rafPending = false;
  let resizeObserver = null;
  let observedElement = null;
  let anchorState = { detail: null, el: null, anchored: false };

  // -------------------------------------------------------------------------
  // Construccion de la UI (Shadow DOM aislado del CSS de la pagina)
  // -------------------------------------------------------------------------
  function ensureUi() {
    if (host && document.contains(host)) return;
    host = document.createElement("div");
    host.id = "sdm-floating-root";
    // `all: initial` evita que los `* { }` de la web afecten al contenedor.
    host.style.cssText =
      "all:initial;position:fixed;z-index:2147483647;top:12px;left:12px;" +
      "display:flex;pointer-events:auto;";
    shadow = host.attachShadow({ mode: "open" });

    const style = document.createElement("style");
    style.textContent = `
      :host { all: initial; }
      .btn {
        display: flex; align-items: center; gap: 8px; cursor: pointer;
        border: none; border-radius: 8px; padding: 9px 14px;
        background: #107C10; color: #fff; font-size: 13px; font-weight: 600;
        font-family: 'Segoe UI Variable', 'Segoe UI', system-ui, sans-serif;
        box-shadow: 0 4px 14px rgba(0,0,0,.35); transition: background 120ms ease, transform 120ms ease;
        white-space: nowrap;
      }
      .btn:hover { background: #0f6d0f; transform: translateY(-1px); }
      .btn:active { transform: translateY(0); }
      .btn[disabled] { opacity: .75; cursor: default; }
      .icon { font-size: 15px; line-height: 1; }
      .kind { opacity: .82; font-weight: 500; }
    `;

    button = document.createElement("button");
    button.type = "button";
    button.className = "btn";
    button.setAttribute("aria-label", "Descargar este vídeo con S-Download Manager");
    button.title = "Descargar con S-Download Manager";

    const icon = document.createElement("span");
    icon.className = "icon";
    icon.textContent = "⬇";

    label = document.createElement("span");
    label.className = "kind";
    label.textContent = "SDM";

    button.appendChild(icon);
    button.appendChild(label);
    button.addEventListener("click", onCapture);
    button.addEventListener("mouseenter", () => setOpacity("1"));
    button.addEventListener("mouseleave", scheduleDim);

    shadow.appendChild(style);
    shadow.appendChild(button);
    (document.body || document.documentElement).appendChild(host);
    setOpacity("0");
  }

  function setOpacity(value) {
    if (!host) return;
    host.style.opacity = value;
    host.style.pointerEvents = value === "0" ? "none" : "auto";
  }

  // -------------------------------------------------------------------------
  // Anclaje: junto al video (esquina superior derecha del reproductor)
  // -------------------------------------------------------------------------
  function anchor() {
    if (!host) return;
    const element = anchorState.el && anchorState.el.isConnected ? anchorState.el : null;
    if (!element) {
      anchorToCorner();
      return;
    }
    const rect = element.getBoundingClientRect();
    const onScreen = rect.bottom > 8 && rect.top < window.innerHeight - 8 &&
                     rect.right > 8 && rect.left < window.innerWidth - 8;
    if (!onScreen || rect.width < 140 || rect.height < 100) {
      anchorToCorner();
      return;
    }
    const width = host.offsetWidth || 120;
    const height = host.offsetHeight || 36;
    const left = Math.min(Math.max(8, rect.right - width - 12), window.innerWidth - width - 8);
    const top = Math.min(Math.max(8, rect.top + 12), window.innerHeight - height - 8);
    host.style.left = `${Math.round(left)}px`;
    host.style.top = `${Math.round(top)}px`;
    anchorState.anchored = true;
  }

  function anchorToCorner() {
    if (!host) return;
    host.style.left = "auto";
    host.style.top = "auto";
    host.style.right = "18px";
    host.style.bottom = "18px";
    anchorState.anchored = false;
  }

  function requestAnchor() {
    if (rafPending) return;
    rafPending = true;
    requestAnimationFrame(() => {
      rafPending = false;
      anchor();
    });
  }

  function observe(element) {
    if (observedElement === element) return;
    observedElement = element;
    if (resizeObserver) {
      resizeObserver.disconnect();
    }
    if (element && typeof ResizeObserver === "function") {
      resizeObserver = new ResizeObserver(requestAnchor);
      resizeObserver.observe(element);
    }
  }

  // -------------------------------------------------------------------------
  // Visibilidad
  // -------------------------------------------------------------------------
  function show(detail) {
    anchorState.detail = detail;
    anchorState.el = detail.el || null;
    ensureUi();
    label.textContent = `SDM · ${KIND_LABEL[detail.kind] || "Media"}`;
    observe(anchorState.el);
    requestAnchor();
    setOpacity("1");
    scheduleDim();
  }

  function scheduleDim() {
    clearTimeout(hideTimer);
    hideTimer = setTimeout(() => {
      // Se atenua pero sigue disponible junto al video.
      setOpacity("0.35");
    }, DIM_AFTER_MS);
  }

  // -------------------------------------------------------------------------
  // Captura
  // -------------------------------------------------------------------------
  function onCapture() {
    const detail = anchorState.detail;
    if (!detail) return;
    button.disabled = true;
    const original = label.textContent;
    label.textContent = "Enviando…";
    chrome.runtime.sendMessage(
      {
        type: "sdm:startStream",
        payload: {
          url: detail.url,
          referer: location.href,
          title: detail.title || document.title || ""
        }
      },
      (response) => {
        button.disabled = false;
        label.textContent = response && response.ok ? "¡En SDM!" : "SDM no conectado";
        setTimeout(() => {
          label.textContent = original;
        }, 2500);
      }
    );
  }

  // -------------------------------------------------------------------------
  // Eventos
  // -------------------------------------------------------------------------
  window.addEventListener("sdm-media-detected", (event) => {
    show(event.detail || {});
  });

  window.addEventListener("scroll", requestAnchor, { passive: true, capture: true });
  window.addEventListener("resize", requestAnchor, { passive: true });

  // Al pasar el raton por el reproductor el boton aparece y se recoloca.
  document.addEventListener(
    "mouseover",
    (event) => {
      if (!anchorState.detail) return;
      const video = event.target && event.target.closest ? event.target.closest("video") : null;
      if (!video) return;
      if (video !== anchorState.el) {
        anchorState.el = video;
        observe(video);
      }
      requestAnchor();
      setOpacity("1");
      scheduleDim();
    },
    { passive: true, capture: true }
  );

  // Mensaje directo desde el popup ("capturar medios de esta pagina").
  chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
    if (message && message.type === "sdm:capturePageMedia") {
      const media = window.__sdmMedia ? window.__sdmMedia.current() : null;
      if (media) {
        onCapture();
        sendResponse({ ok: true, url: media.url });
      } else {
        sendResponse({ ok: false, error: "Sin transmision detectada" });
      }
      return false;
    }
    if (message && message.type === "sdm:collectMedia") {
      const items = window.__sdmMedia && window.__sdmMedia.items
        ? window.__sdmMedia.items()
        : [];
      sendResponse({ ok: true, items });
      return false;
    }
    if (message && message.type === "sdm:collectLinks") {
      // Recolector para "descargar todos los enlaces" (menu contextual).
      try {
        const urls = new Set();
        document.querySelectorAll("a[href]").forEach((a) => {
          try {
            const abs = new URL(a.getAttribute("href"), location.href).href;
            if (abs.startsWith("http://") || abs.startsWith("https://") || abs.startsWith("ftp://")) {
              urls.add(abs);
            }
          } catch { /* href invalido */ }
        });
        sendResponse({ ok: true, links: Array.from(urls) });
      } catch (e) {
        sendResponse({ ok: false, error: String((e && e.message) || e) });
      }
      return false;
    }
    return false;
  });
})();
