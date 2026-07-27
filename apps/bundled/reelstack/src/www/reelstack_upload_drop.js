(() => {
  "use strict";

  let installed = false;
  let dragDepth = 0;
  let overlay = null;

  function reelT(key, params, fallback) {
    try {
      const i18n = window.PQNAS_I18N;

      if (i18n && typeof i18n.t === "function") {
        return i18n.t(key, params || null, fallback);
      }
    } catch (_) {}

    let output = String(fallback || key || "");

    for (const [name, value] of Object.entries(params || {})) {
      output = output
        .split(`{${name}}`)
        .join(String(value));
    }

    return output;
  }

  function setStatus(text) {
    const app = window.PQNAS_REELSTACK_APP;

    if (app && typeof app.setStatus === "function") {
      app.setStatus(text);
      return;
    }

    const status = document.getElementById("statusText");

    if (status) {
      status.textContent = String(text || "");
    }
  }

  function ensureOverlay() {
    if (overlay) {
      return overlay;
    }

    overlay = document.createElement("div");
    overlay.className = "rsUploadDropOverlay";
    overlay.hidden = true;
    overlay.setAttribute("aria-hidden", "true");

    const card = document.createElement("div");
    card.className = "rsUploadDropCard";
    card.setAttribute("role", "status");
    card.setAttribute("aria-live", "polite");

    const title = document.createElement("div");
    title.className = "rsUploadDropTitle";
    title.textContent = reelT(
      "reelstack.drop.title",
      null,
      "Drop videos here to upload"
    );

    const subtitle = document.createElement("div");
    subtitle.className = "rsUploadDropSubtitle";
    subtitle.textContent = reelT(
      "reelstack.drop.subtitle",
      null,
      "Choose the destination folder next"
    );

    card.appendChild(title);
    card.appendChild(subtitle);
    overlay.appendChild(card);
    document.body.appendChild(overlay);

    return overlay;
  }

  function showOverlay(show) {
    const node = ensureOverlay();
    const visible = !!show;

    node.hidden = !visible;
    node.setAttribute(
      "aria-hidden",
      visible ? "false" : "true"
    );
  }

  function resetDragState() {
    dragDepth = 0;
    showOverlay(false);
  }

  function hasFiles(dataTransfer) {
    if (!dataTransfer) {
      return false;
    }

    try {
      if (
        dataTransfer.files &&
        dataTransfer.files.length > 0
      ) {
        return true;
      }
    } catch (_) {}

    try {
      const types = Array.from(dataTransfer.types || []);

      return (
        types.includes("Files") ||
        types.includes("application/x-moz-file")
      );
    } catch (_) {
      return false;
    }
  }

  function uploadApi() {
    return window.PQNAS_REELSTACK_UPLOAD || null;
  }

  function uploadIsActive() {
    const api = uploadApi();

    return !!(
      api &&
      typeof api.isActive === "function" &&
      api.isActive()
    );
  }

  function onDragEnter(event) {
    if (!hasFiles(event.dataTransfer)) {
      return;
    }

    event.preventDefault();

    if (uploadIsActive()) {
      if (event.dataTransfer) {
        event.dataTransfer.dropEffect = "none";
      }

      resetDragState();
      return;
    }

    dragDepth++;
    showOverlay(true);
  }

  function onDragOver(event) {
    if (!hasFiles(event.dataTransfer)) {
      return;
    }

    event.preventDefault();

    if (event.dataTransfer) {
      event.dataTransfer.dropEffect = uploadIsActive()
        ? "none"
        : "copy";
    }

    showOverlay(!uploadIsActive());
  }

  function onDragLeave(event) {
    if (dragDepth > 0) {
      dragDepth--;
    }

    if (
      dragDepth === 0 ||
      !event.relatedTarget
    ) {
      showOverlay(false);
    }
  }

  async function onDrop(event) {
    if (!hasFiles(event.dataTransfer)) {
      return;
    }

    event.preventDefault();
    event.stopPropagation();
    resetDragState();

    const transfer = event.dataTransfer;
    const files = Array.from(
      transfer && transfer.files || []
    );

    if (!files.length) {
      setStatus(reelT(
        "reelstack.drop.no_files",
        null,
        "The drop contained no readable video files."
      ));

      return;
    }

    const api = uploadApi();

    if (
      !api ||
      typeof api.startFiles !== "function"
    ) {
      return;
    }

    await api.startFiles(files);
  }

  function install() {
    if (installed) {
      return;
    }

    installed = true;
    ensureOverlay();

    document.addEventListener(
      "dragenter",
      onDragEnter,
      true
    );
    document.addEventListener(
      "dragover",
      onDragOver,
      true
    );
    document.addEventListener(
      "dragleave",
      onDragLeave,
      true
    );
    document.addEventListener(
      "drop",
      onDrop,
      true
    );
    document.addEventListener(
      "dragend",
      resetDragState,
      true
    );
  }

  window.addEventListener(
    "pqnas-reelstack-upload-ready",
    install,
    { once: true }
  );

  if (window.PQNAS_REELSTACK_UPLOAD) {
    install();
  }
})();
