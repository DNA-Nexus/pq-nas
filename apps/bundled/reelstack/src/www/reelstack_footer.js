(() => {
  "use strict";

  const summaryEl = document.getElementById("rsLibrarySummary");

  let currentVideos = [];
  let selectedPaths = new Set();

  function reelT(key, params, fallback) {
    try {
      const api = window.PQNAS_I18N;

      if (api && typeof api.t === "function") {
        return api.t(key, params || null, fallback);
      }
    } catch (_) {}

    let output = String(fallback || key || "");
    const values = params || {};

    for (const name of Object.keys(values)) {
      output = output.split(`{${name}}`).join(String(values[name]));
    }

    return output;
  }

  function localizedNumber(value, fractionDigits) {
    const numericValue = Number(value || 0);
    const digits = Math.max(0, Number(fractionDigits || 0));

    try {
      const locale = document.documentElement.lang || undefined;

      return new Intl.NumberFormat(locale, {
        minimumFractionDigits: digits,
        maximumFractionDigits: digits
      }).format(numericValue);
    } catch (_) {
      return numericValue.toFixed(digits);
    }
  }

  function fmtBytes(bytes) {
    let value = Number(bytes || 0);

    if (!Number.isFinite(value) || value <= 0) {
      return "0 B";
    }

    const units = ["B", "KB", "MB", "GB", "TB", "PB"];
    let unitIndex = 0;

    while (value >= 1024 && unitIndex < units.length - 1) {
      value /= 1024;
      unitIndex += 1;
    }

    const fractionDigits = unitIndex === 0 ? 0 : 1;

    return `${localizedNumber(value, fractionDigits)} ${units[unitIndex]}`;
  }

  function videoPath(video) {
    return String(video && video.path || "");
  }

  function videoSize(video) {
    const size = Number(video && video.size_bytes);
    return Number.isFinite(size) && size > 0 ? size : 0;
  }

  function renderSummary() {
    if (!summaryEl) {
      return;
    }

    let libraryBytes = 0;
    let selectedBytes = 0;
    let selectedCount = 0;

    for (const video of currentVideos) {
      const size = videoSize(video);
      const path = videoPath(video);

      libraryBytes += size;

      if (path && selectedPaths.has(path)) {
        selectedCount += 1;
        selectedBytes += size;
      }
    }

    if (selectedCount > 0) {
      summaryEl.textContent = reelT(
        "reelstack.footer.selected_summary",
        {
          count: selectedCount,
          size: fmtBytes(selectedBytes)
        },
        "Selected: {count} · {size}"
      );

      return;
    }

    const libraryCount = currentVideos.length;
    const countLabel = reelT(
      "reelstack.video_count",
      { count: libraryCount },
      "{count} video(s)"
    );

    summaryEl.textContent = reelT(
      "reelstack.footer.library_summary",
      {
        count: countLabel,
        size: fmtBytes(libraryBytes)
      },
      "{count} · {size} total"
    );
  }

  function update(videos) {
    currentVideos = Array.isArray(videos) ? videos : [];

    const availablePaths = new Set(
      currentVideos
        .map(videoPath)
        .filter(Boolean)
    );

    selectedPaths = new Set(
      Array.from(selectedPaths)
        .filter(path => availablePaths.has(path))
    );

    renderSummary();
  }

  function setSelection(paths) {
    const values = Array.isArray(paths)
      ? paths
      : Array.from(paths || []);

    selectedPaths = new Set(
      values
        .map(path => String(path || ""))
        .filter(Boolean)
    );

    renderSummary();
  }

  window.PQNAS_REELSTACK_FOOTER = Object.freeze({
    update,
    setSelection
  });

  update([]);
})();
