// ============================================================================
//  S-DOWNLOAD MANAGER (SDM)
//  extension/content/media_detector.js
//
//  Deteccion de transmisiones (HLS/DASH) y vídeos <video> en la pagina
//  (spec cap. 9).  Emite CustomEvent("sdm-media-detected") que consume
//  content_script.js para mostrar el boton flotante de captura.
// ============================================================================

"use strict";

(function () {
  if (window.__sdmDetectorLoaded) return;
  window.__sdmDetectorLoaded = true;

  const seen = new Set();
  let current = null; // { url, kind, title, pageUrl }

  function emit(detail) {
    current = detail;
    window.dispatchEvent(new CustomEvent("sdm-media-detected", { detail }));
  }

  function classify(url) {
    const lower = String(url || "").toLowerCase();
    const clean = lower.split("?")[0].split("#")[0];
    if (clean.endsWith(".m3u8") || clean.endsWith(".m3u")) return "hls";
    if (clean.endsWith(".mpd")) return "dash";
    if (clean.endsWith(".ts") || clean.endsWith(".m4s")) return "segment";
    if (/\.(mp4|mkv|webm|mov|avi|flv|wmv|m4v|mp3|flac|wav|m4a|aac|ogg)(\?|$)/.test(clean)) {
      return "file";
    }
    return null;
  }

  function consider(url, kindHint) {
    if (!url || url.startsWith("blob:") || url.startsWith("data:")) return;
    const kind = kindHint || classify(url);
    if (!kind || kind === "segment") return;
    if (seen.has(url)) return;
    seen.add(url);
    // Prioridad: manifiestos HLS/DASH por encima de ficheros sueltos.
    if (current && current.kind !== "file" && kind === "file") return;
    emit({
      url,
      kind,
      title: document.title || "",
      pageUrl: location.href
    });
  }

  // 1) Resource Timing: peticiones XHR/fetch del reproductor (hls.js, dash.js...).
  try {
    const observer = new PerformanceObserver((list) => {
      for (const entry of list.getEntries()) {
        consider(entry.name);
      }
    });
    observer.observe({ type: "resource", buffered: true });
  } catch {
    // PerformanceObserver sin soporte de "resource": seguimos con el DOM.
  }

  // 2) Elementos <video>/<source> y atributos data-src de reproductores comunes.
  function scanDom() {
    document.querySelectorAll("video, audio").forEach((el) => {
      if (el.src) consider(el.src);
      el.querySelectorAll("source").forEach((src) => {
        if (src.src) consider(src.src);
      });
      // Reproductores que cargan el manifiesto en data-* attributes.
      const attrs = el.attributes;
      for (let i = 0; i < attrs.length; i++) {
        const a = attrs[i];
        if (a.name.startsWith("data-") && /https?:\/\//.test(a.value) && classify(a.value)) {
          consider(a.value);
        }
      }
    });
    // Iframes embebidos: no podemos inspeccionarlos cross-origin, pero los
    // <source> del mismo origen si.
    document.querySelectorAll("source").forEach((src) => {
      if (src.src) consider(src.src);
    });
  }

  scanDom();
  try {
    const domObserver = new MutationObserver(() => {
      clearTimeout(scanDom._t);
      scanDom._t = setTimeout(scanDom, 800);
    });
    domObserver.observe(document.documentElement, {
      childList: true,
      subtree: true,
      attributes: true,
      attributeFilter: ["src", "data-src", "data-video", "data-hls", "data-dash"]
    });
  } catch {
    // Sin MutationObserver: el escaneo inicial es suficiente.
  }

  // API interna para content_script.js / depuracion.
  window.__sdmMedia = {
    current: () => current,
    list: () => Array.from(seen)
  };
})();
