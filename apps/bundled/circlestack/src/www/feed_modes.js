/* apps/bundled/circlestack/src/www/feed_modes.js
 * Circle Stack feed mode helpers.
 *
 * This file intentionally owns the new feed-mode logic so app.js does not
 * keep growing for every new federation/feed feature.
 */
(() => {
  "use strict";

  const CS_API_LOCAL = "/api/v4/circlestack";

  let knownOrigins = new Set();
  let mutedOrigins = new Set();

  function normalizeMode(mode) {
    if (mode === "federated") return "federated";
    if (mode === "my_circle") return "my_circle";
    return "local";
  }

  function isFederatedSurface(mode) {
    const m = normalizeMode(mode);
    return m === "federated" || m === "my_circle";
  }

  async function fetchKnownOrigins() {
    const res = await fetch(`${CS_API_LOCAL}/federated/origins`, {
      credentials: "same-origin",
      cache: "no-store"
    });

    const data = await res.json().catch(() => ({ ok: false }));
    if (!res.ok || !data.ok) return [];

    return Array.isArray(data.items) ? data.items : [];
  }

  async function refreshOriginSets() {
    const origins = await fetchKnownOrigins();

    knownOrigins = new Set(
      origins
        .filter((origin) => origin && origin.enabled !== false)
        .map((origin) => String(origin.origin_nas || "").trim())
        .filter(Boolean)
    );

    mutedOrigins = new Set(
      origins
        .filter((origin) => origin && (origin.my_muted || origin.my_hidden))
        .map((origin) => String(origin.origin_nas || "").trim())
        .filter(Boolean)
    );

    return origins;
  }

  async function filterFederatedEvents(rawEvents, mode) {
    await refreshOriginSets();

    const m = normalizeMode(mode);
    const myCircleOnly = m === "my_circle";

    return (Array.isArray(rawEvents) ? rawEvents : []).filter((ev) => {
      const origin = String(ev && ev.origin_nas ? ev.origin_nas : "").trim();

      if (origin && mutedOrigins.has(origin)) {
        return false;
      }

      if (myCircleOnly) {
        return origin && knownOrigins.has(origin);
      }

      return true;
    });
  }

  function emptyMessage(mode) {
    if (normalizeMode(mode) === "my_circle") {
      return "No My Circle federated posts yet. Add or follow a remote person to include their NAS origin here.";
    }

    return "No federated events yet.";
  }

  // CIRCLESTACK_FEED_MODE_CYCLE_BUTTON_V1
  function modeLabel(mode) {
    const m = normalizeMode(mode);
    if (m === "federated") return "Mode: Federated";
    if (m === "my_circle") return "Mode: My Circle";
    return "Mode: Feed";
  }

  function nextMode(mode) {
    const m = normalizeMode(mode);
    if (m === "local") return "federated";
    if (m === "federated") return "my_circle";
    return "local";
  }

  function setActiveButtons(mode) {
    const m = normalizeMode(mode);

    const cycleBtn = document.getElementById("csFeedModeBtn");
    const localBtn = document.getElementById("csLocalFeedBtn");
    const fedBtn = document.getElementById("csFederatedBtn");

    if (cycleBtn) {
      cycleBtn.classList.add("is-active");
      cycleBtn.textContent = modeLabel(m);
      cycleBtn.dataset.mode = m;
      cycleBtn.title = "Click to switch feed mode";
    }

    // Backward compatibility if old buttons exist in cached/runtime HTML.
    if (localBtn) localBtn.classList.toggle("is-active", m === "local");
    if (fedBtn) fedBtn.classList.toggle("is-active", m === "federated");

    // Important: do not touch csMyCircleBtn here.
    // That button belongs to the existing My Circle people modal.
  }

  function initButtons(setFeedMode) {
    if (typeof setFeedMode !== "function") return false;

    const cycleBtn = document.getElementById("csFeedModeBtn");

    if (cycleBtn) {
      cycleBtn.addEventListener("click", () => {
        setFeedMode(nextMode(cycleBtn.dataset.mode || "local"));
      });
      setActiveButtons("local");
      return true;
    }

    // Backward compatibility if old runtime HTML still has separate buttons.
    const localBtn = document.getElementById("csLocalFeedBtn");
    const fedBtn = document.getElementById("csFederatedBtn");

    if (localBtn) {
      localBtn.addEventListener("click", () => setFeedMode("local"));
    }

    if (fedBtn) {
      fedBtn.addEventListener("click", () => setFeedMode("federated"));
    }

    return !!(localBtn || fedBtn);
  }

  window.CircleStackFeedModes = {
    normalizeMode,
    isFederatedSurface,
    filterFederatedEvents,
    emptyMessage,
    setActiveButtons,
    initButtons,
    refreshOriginSets
  };
})();
