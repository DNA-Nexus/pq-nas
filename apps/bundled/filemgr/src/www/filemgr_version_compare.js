(() => {
  "use strict";

  window.PQNAS_FILEMGR = window.PQNAS_FILEMGR || {};

  function t(key, params, fallback) {
    try {
      const api = window.PQNAS_I18N;
      if (api && typeof api.t === "function") {
        return api.t(key, params || null, fallback || key);
      }
    } catch (_) {}

    let out = String(fallback || key || "");
    const p = params || {};
    for (const k of Object.keys(p)) {
      out = out.split(`{${k}}`).join(String(p[k]));
    }
    return out;
  }

  function esc(v) {
    return String(v ?? "")
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;")
      .replace(/'/g, "&#39;");
  }

  function isImagePath(path) {
    return /\.(png|jpe?g|webp|gif|bmp|svg)$/i.test(String(path || "").split("?")[0]);
  }

  function currentFileUrl(path) {
    return `/api/v4/files/get?path=${encodeURIComponent(String(path || ""))}`;
  }

  function versionBlobUrl(path, versionId) {
    return `/api/v4/files/versions/blob?path=${encodeURIComponent(String(path || ""))}&version_id=${encodeURIComponent(String(versionId || ""))}&inline=1`;
  }

  let modal = null;
  let imgCurrent = null;
  let imgVersion = null;
  let titleCurrent = null;
  let titleVersion = null;
  let zoomLabel = null;

  const state = {
    zoom: 1,
    panX: 0,
    panY: 0,
    dragging: false,
    sx: 0,
    sy: 0,
    startPanX: 0,
    startPanY: 0
  };

  function clamp(n, lo, hi) {
    return Math.max(lo, Math.min(hi, n));
  }

  function applyTransform() {
    const tx = `translate(${state.panX}px, ${state.panY}px) scale(${state.zoom})`;
    if (imgCurrent) imgCurrent.style.transform = tx;
    if (imgVersion) imgVersion.style.transform = tx;
    if (zoomLabel) zoomLabel.textContent = `${Math.round(state.zoom * 100)}%`;
  }

  function resetView() {
    state.zoom = 1;
    state.panX = 0;
    state.panY = 0;
    applyTransform();
  }

  function zoomBy(mult, cx, cy) {
    const oldZoom = state.zoom;
    const nextZoom = clamp(oldZoom * mult, 0.1, 8);
    if (Math.abs(nextZoom - oldZoom) < 0.001) return;

    const rect = modal ? modal.getBoundingClientRect() : null;
    const mx = rect ? cx - rect.left - rect.width / 2 : 0;
    const my = rect ? cy - rect.top - rect.height / 2 : 0;

    const scale = nextZoom / oldZoom;
    state.panX = mx - (mx - state.panX) * scale;
    state.panY = my - (my - state.panY) * scale;
    state.zoom = nextZoom;

    applyTransform();
  }

  function ensureCss() {
    if (document.getElementById("fmVersionCompareCss")) return;

    const style = document.createElement("style");
    style.id = "fmVersionCompareCss";
    style.textContent = `
      .fmVersionCompareModal{
        position:fixed;
        inset:0;
        z-index:99999;
        display:none;
        align-items:center;
        justify-content:center;
        padding:24px;
        background:rgba(0,0,0,.62);
        backdrop-filter:blur(8px);
      }
      .fmVersionCompareModal.show{display:flex;}
      .fmVersionCompareCard{
        width:min(1500px,96vw);
        height:min(900px,92vh);
        display:flex;
        flex-direction:column;
        border:1px solid var(--border, rgba(255,255,255,.16));
        border-radius:22px;
        overflow:hidden;
        background:var(--fm_surface, var(--panel, #101522));
        color:var(--fg, #e8f3ff);
        box-shadow:0 24px 90px rgba(0,0,0,.55);
      }
      .fmVersionCompareHead{
        display:flex;
        align-items:center;
        justify-content:space-between;
        gap:12px;
        padding:12px 14px;
        border-bottom:1px solid var(--border2, rgba(255,255,255,.10));
        background:rgba(0,0,0,.18);
      }
      .fmVersionCompareTitle{
        display:flex;
        align-items:center;
        gap:10px;
        min-width:0;
        font-weight:900;
      }
      .fmVersionCompareZoom{
        font-family:var(--mono, monospace);
        opacity:.72;
        font-size:12px;
        padding:3px 8px;
        border-radius:999px;
        border:1px solid var(--border2, rgba(255,255,255,.12));
      }
      .fmVersionCompareActions{
        display:flex;
        align-items:center;
        gap:8px;
        flex-wrap:wrap;
      }
      .fmVersionCompareBtn{
        border:1px solid rgba(255,255,255,.16);
        border-radius:12px;
        background:rgba(255,255,255,.06);
        color:inherit;
        padding:8px 11px;
        font-weight:850;
        cursor:pointer;
      }
      .fmVersionCompareBtn:hover{
        background:rgba(255,255,255,.10);
      }
      .fmVersionCompareBody{
        flex:1 1 auto;
        min-height:0;
        display:grid;
        grid-template-columns:1fr 1fr;
        gap:1px;
        background:var(--border2, rgba(255,255,255,.10));
        overflow:hidden;
      }
      .fmVersionComparePane{
        min-width:0;
        min-height:0;
        display:flex;
        flex-direction:column;
        background:rgba(0,0,0,.22);
      }
      .fmVersionComparePaneTitle{
        padding:9px 12px;
        font-family:var(--mono, monospace);
        font-size:12px;
        color:var(--fg-dim, rgba(255,255,255,.72));
        overflow:hidden;
        text-overflow:ellipsis;
        white-space:nowrap;
        border-bottom:1px solid var(--border2, rgba(255,255,255,.10));
      }
      .fmVersionCompareImageWrap{
        flex:1 1 auto;
        min-height:0;
        display:flex;
        align-items:center;
        justify-content:center;
        overflow:hidden;
        cursor:grab;
        background:
          radial-gradient(circle at center, rgba(255,255,255,.05), transparent 60%),
          rgba(0,0,0,.25);
      }
      .fmVersionCompareModal.dragging .fmVersionCompareImageWrap{cursor:grabbing;}
      .fmVersionCompareImage{
        max-width:94%;
        max-height:94%;
        object-fit:contain;
        transform-origin:center center;
        will-change:transform;
        user-select:none;
        -webkit-user-drag:none;
      }
      .fmVersionCompareHint{
        padding:8px 12px;
        font-size:12px;
        opacity:.72;
        border-top:1px solid var(--border2, rgba(255,255,255,.10));
        background:rgba(0,0,0,.14);
      }
      @media (max-width: 800px){
        .fmVersionCompareBody{grid-template-columns:1fr;}
        .fmVersionCompareCard{height:94vh;}
      }
    `;
    document.head.appendChild(style);
  }

  function ensureModal() {
    ensureCss();

    if (modal) return modal;

    modal = document.createElement("div");
    modal.className = "fmVersionCompareModal";
    modal.setAttribute("aria-hidden", "true");

    modal.innerHTML = `
      <div class="fmVersionCompareCard" role="dialog" aria-modal="true" aria-label="${esc(t("filemgr.version_compare.image", null, "Image version compare"))}">
        <div class="fmVersionCompareHead">
          <div class="fmVersionCompareTitle">
            <span>${esc(t("filemgr.version_compare.image", null, "Image version compare"))}</span>
            <span class="fmVersionCompareZoom">100%</span>
          </div>
          <div class="fmVersionCompareActions">
            <button class="fmVersionCompareBtn" type="button" data-fmvc-reset>${esc(t("filemgr.version_compare.reset", null, "Reset"))}</button>
            <button class="fmVersionCompareBtn" type="button" data-fmvc-close>${esc(t("common.close", null, "Close"))}</button>
          </div>
        </div>

        <div class="fmVersionCompareBody">
          <div class="fmVersionComparePane">
            <div class="fmVersionComparePaneTitle" data-fmvc-current-title></div>
            <div class="fmVersionCompareImageWrap">
              <img class="fmVersionCompareImage" data-fmvc-current-img alt="">
            </div>
          </div>
          <div class="fmVersionComparePane">
            <div class="fmVersionComparePaneTitle" data-fmvc-version-title></div>
            <div class="fmVersionCompareImageWrap">
              <img class="fmVersionCompareImage" data-fmvc-version-img alt="">
            </div>
          </div>
        </div>

        <div class="fmVersionCompareHint">
          ${esc(t("filemgr.version_compare.hint", null, "Mouse wheel zooms both images. Drag pans both images together."))}
        </div>
      </div>
    `;

    document.body.appendChild(modal);

    imgCurrent = modal.querySelector("[data-fmvc-current-img]");
    imgVersion = modal.querySelector("[data-fmvc-version-img]");
    titleCurrent = modal.querySelector("[data-fmvc-current-title]");
    titleVersion = modal.querySelector("[data-fmvc-version-title]");
    zoomLabel = modal.querySelector(".fmVersionCompareZoom");

    modal.querySelector("[data-fmvc-close]")?.addEventListener("click", close);
    modal.querySelector("[data-fmvc-reset]")?.addEventListener("click", resetView);

    const body = modal.querySelector(".fmVersionCompareBody");
    body?.addEventListener("wheel", (ev) => {
      ev.preventDefault();
      zoomBy(ev.deltaY < 0 ? 1.12 : 1 / 1.12, ev.clientX, ev.clientY);
    }, { passive:false });

    body?.addEventListener("pointerdown", (ev) => {
      if (ev.button !== 0) return;
      state.dragging = true;
      state.sx = ev.clientX;
      state.sy = ev.clientY;
      state.startPanX = state.panX;
      state.startPanY = state.panY;
      modal.classList.add("dragging");
      try { body.setPointerCapture(ev.pointerId); } catch (_) {}
      ev.preventDefault();
    });

    body?.addEventListener("pointermove", (ev) => {
      if (!state.dragging) return;
      state.panX = state.startPanX + (ev.clientX - state.sx);
      state.panY = state.startPanY + (ev.clientY - state.sy);
      applyTransform();
    });

    const stop = () => {
      state.dragging = false;
      modal.classList.remove("dragging");
    };
    body?.addEventListener("pointerup", stop);
    body?.addEventListener("pointercancel", stop);

    modal.addEventListener("click", (ev) => {
      if (ev.target === modal) close();
    });

    document.addEventListener("keydown", (ev) => {
      if (ev.key === "Escape" && modal.classList.contains("show")) {
        ev.preventDefault();
        close();
      }
    });

    return modal;
  }

  function open(opts) {
    const o = opts || {};
    const path = String(o.path || "").trim();
    const versionId = String(o.versionId || "").trim();

    const leftUrl = String(o.leftUrl || (path ? currentFileUrl(path) : "")).trim();
    const rightUrl = String(o.rightUrl || (path && versionId ? versionBlobUrl(path, versionId) : "")).trim();

    if (!leftUrl || !rightUrl) return false;

    ensureModal();
    resetView();

    if (titleCurrent) titleCurrent.textContent = o.leftTitle || t("filemgr.version_compare.current", null, "Current file");
    if (titleVersion) titleVersion.textContent = o.rightTitle || t("filemgr.version_compare.version", null, "Selected version");

    if (imgCurrent) {
      imgCurrent.removeAttribute("src");
      imgCurrent.alt = o.leftTitle || "";
      imgCurrent.src = leftUrl;
    }

    if (imgVersion) {
      imgVersion.removeAttribute("src");
      imgVersion.alt = o.rightTitle || "";
      imgVersion.src = rightUrl;
    }

    modal.classList.add("show");
    modal.setAttribute("aria-hidden", "false");
    return true;
  }

  function close() {
    if (!modal) return;

    modal.classList.remove("show");
    modal.setAttribute("aria-hidden", "true");

    if (imgCurrent) imgCurrent.removeAttribute("src");
    if (imgVersion) imgVersion.removeAttribute("src");

    resetView();
  }

  window.PQNAS_FILEMGR.versionImageCompare = {
    open,
    close,
    isImagePath,
    currentFileUrl,
    versionBlobUrl
  };
})();
