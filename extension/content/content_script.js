// ============================================================================
//  S-DOWNLOAD MANAGER (SDM)
//  extension/content/content_script.js
//
//  Boton flotante Fluent (spec cap. 4 y 9): aparece cuando la pagina
//  contiene una transmision HLS/DASH o un vídeo directo; al pulsarlo se
//  envia "sdm:startStream" al service worker, que crea la tarea en SDM.
// ============================================================================

"use strict";

(function () {
  if (window.__sdmContentLoaded) return;
  window.__sdmContentLoaded = true;

  let host = null;
  let button = null;
  let label = null;
  let hideTimer = null;
  let currentDetail = null;

  const KIND_LABEL = { hls: "HLS", dash: "DASH", file: "Vídeo" };

  function ensureUi() {
    if (host && document.contains(host)) return;
    host = document.createElement("div");
    host.id = "sdm-floating-root";
    host.style.cssText =
      "position:fixed;z-index:2147483647;right:18px;bottom:18px;" +
      "display:flex;flex-direction:column;gap:8px;align-items:flex-end;" +
      "font-family:'Segoe UI Variable','Segoe UI',system-ui,sans-serif;";

    button = document.createElement("button");
    button.type = "button";
    button.setAttribute("aria-label", "Descargar con S-Download Manager");
    button.style.cssText =
      "display:flex;align-items:center;gap:8px;cursor:pointer;" +
      "border:none;border-radius:8px;padding:9px 14px;" +
      "background:#107C10;color:#ffffff;font-size:13px;font-weight:600;" +
      "box-shadow:0 4px 14px rgba(0,0,0,0.35);transition:background 120ms ease,transform 120ms ease;";
    button.addEventListener("mouseenter", () => {
      button.style.background = "#0f6d0f";
      button.style.transform = "translateY(-1px)";
    });
    button.addEventListener("mouseleave", () => {
      button.style.background = "#107C10";
      button.style.transform = "translateY(0)";
    });

    const icon = document.createElement("span");
    icon.textContent = "⬇";
    icon.style.cssText = "font-size:15px;line-height:1;";

    label = document.createElement("span");
    label.textContent = "SDM";

    button.appendChild(icon);
    button.appendChild(label);
    host.appendChild(button);
    (document.body || document.documentElement).appendChild(host);

    button.addEventListener("click", onCapture);
  }

  function show(detail) {
    currentDetail = detail;
    ensureUi();
    label.textContent = `SDM · ${KIND_LABEL[detail.kind] || "Media"}`;
    host.style.display = "flex";
    host.style.opacity = "1";
    scheduleHide();
  }

  function scheduleHide() {
    clearTimeout(hideTimer);
    hideTimer = setTimeout(() => {
      if (host) {
        host.style.transition = "opacity 400ms ease";
        host.style.opacity = "0.25";
      }
    }, 6000);
  }

  function onCapture() {
    if (!currentDetail) return;
    button.disabled = true;
    const original = label.textContent;
    label.textContent = "Enviando…";
    chrome.runtime.sendMessage(
      {
        type: "sdm:startStream",
        payload: {
          url: currentDetail.url,
          referer: location.href,
          title: currentDetail.title || document.title || ""
        }
      },
      (response) => {
        button.disabled = false;
        if (response && response.ok) {
          label.textContent = "¡En SDM!";
        } else {
          label.textContent = "SDM no conectado";
        }
        setTimeout(() => {
          label.textContent = original;
        }, 2500);
      }
    );
  }

  window.addEventListener("sdm-media-detected", (event) => {
    show(event.detail);
  });

  // El boton tambien reaparece al pasar el raton cerca de la esquina.
  document.addEventListener("mousemove", (event) => {
    if (!host || !currentDetail) return;
    const near =
      event.clientY > window.innerHeight - 140 && event.clientX > window.innerWidth - 260;
    if (near) {
      host.style.opacity = "1";
      scheduleHide();
    }
  });

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
        sendResponse({ ok: false, error: String(e && e.message || e) });
      }
      return false;
    }
    return false;
  });
})();
