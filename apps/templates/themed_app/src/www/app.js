(function () {
  "use strict";

  const SUPPORTED_THEMES = new Set(["dark", "bright", "cpunk_orange", "win_classic"]);

  function detectTheme() {
    const params = new URLSearchParams(window.location.search);
    const fromUrl = params.get("theme");
    if (SUPPORTED_THEMES.has(fromUrl)) return fromUrl;

    const fromDataset = document.documentElement.dataset.theme;
    if (SUPPORTED_THEMES.has(fromDataset)) return fromDataset;

    try {
      const fromStorage =
        window.localStorage.getItem("pqnas_theme") ||
        window.localStorage.getItem("theme") ||
        window.localStorage.getItem("pq_theme");
      if (SUPPORTED_THEMES.has(fromStorage)) return fromStorage;
    } catch (_) {
      /* localStorage can be unavailable in hardened browser modes. */
    }

    return "dark";
  }

  function applyTheme(theme) {
    const safeTheme = SUPPORTED_THEMES.has(theme) ? theme : "dark";
    document.documentElement.dataset.theme = safeTheme;

    const badge = document.getElementById("themeBadge");
    if (badge) badge.textContent = `theme: ${safeTheme}`;
  }

  function setStatusBadge(value) {
    const badge = document.getElementById("themeBadge");
    if (!badge) return;

    badge.classList.remove("ok", "warn", "err", "info", "muted");
    if (value === "ready") badge.classList.add("ok");
    else if (value === "working") badge.classList.add("warn");
    else if (value === "error") badge.classList.add("err");
    else badge.classList.add("info");
  }

  document.addEventListener("DOMContentLoaded", () => {
    applyTheme(detectTheme());

    const refreshBtn = document.getElementById("refreshBtn");
    if (refreshBtn) refreshBtn.addEventListener("click", () => applyTheme(detectTheme()));

    const primaryBtn = document.getElementById("primaryBtn");
    if (primaryBtn) {
      primaryBtn.addEventListener("click", () => {
        const emptyState = document.getElementById("emptyState");
        if (emptyState) {
          emptyState.textContent = "Primary action clicked. Replace this with real app behaviour.";
        }
      });
    }

    const statusSelect = document.getElementById("statusSelect");
    if (statusSelect) {
      statusSelect.addEventListener("change", () => setStatusBadge(statusSelect.value));
    }
  });
})();
