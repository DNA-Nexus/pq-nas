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
  let originInfoByNas = new Map();

  function normalizeMode(mode) {
    if (mode === "federated") return "federated";
    if (mode === "my_circle") return "my_circle";
    if (mode === "discover") return "discover";
    return "local";
  }

  function isFederatedSurface(mode) {
    const m = normalizeMode(mode);
    return m === "federated" || m === "my_circle" || m === "discover";
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
    try {
      const origins = await fetchKnownOrigins();

      originInfoByNas = new Map(
        origins
          .map((origin) => [
            String(origin && origin.origin_nas ? origin.origin_nas : "").trim(),
            origin
          ])
          .filter(([originNas]) => !!originNas)
      );

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
    } catch (_) {
      knownOrigins = new Set();
      mutedOrigins = new Set();
      originInfoByNas = new Map();
      return [];
    }
  }

  async function filterFederatedEvents(rawEvents, mode) {
    await refreshOriginSets();

    const m = normalizeMode(mode);
    const myCircleOnly = m === "my_circle";

    const filtered = (Array.isArray(rawEvents) ? rawEvents : []).filter((ev) => {
      const origin = String(ev && ev.origin_nas ? ev.origin_nas : "").trim();

      if (origin && mutedOrigins.has(origin)) {
        return false;
      }

      if (myCircleOnly) {
        return origin && knownOrigins.has(origin);
      }

      return true;
    });

    if (m !== "discover") {
      return filtered;
    }

    return filtered
      .map((ev, index) => ({
        ev,
        index,
        classification: classifyFederatedEvent(ev, m)
      }))
      .sort((a, b) => {
        const pa = Number(a.classification && a.classification.priority || 0);
        const pb = Number(b.classification && b.classification.priority || 0);

        if (pb !== pa) return pb - pa;

        // Keep the existing feed order inside the same bucket.
        return a.index - b.index;
      })
      .map((item) => item.ev);
  }

  function emptyMessage(mode) {
    const m = normalizeMode(mode);

    if (m === "my_circle") {
      return "No My Circle federated posts yet. Add or follow a remote person to include their NAS origin here.";
    }

    if (m === "discover") {
      return "No Discover posts yet. Discover currently uses the public federated feed until Extended Circle ranking is enabled.";
    }

    return "No federated events yet.";
  }

  // CIRCLESTACK_FEED_MODE_CYCLE_BUTTON_V1
  function modeLabel(mode) {
    const m = normalizeMode(mode);
    if (m === "federated") return "Mode: Federated";
    if (m === "my_circle") return "Mode: My Circle";
    if (m === "discover") return "Mode: Discover";
    return "Mode: Feed";
  }

  function nextMode(mode) {
    const m = normalizeMode(mode);
    if (m === "local") return "federated";
    if (m === "federated") return "my_circle";
    if (m === "my_circle") return "discover";
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


  // CIRCLESTACK_FEDERATED_REASON_LABELS_V1
  function shortOrigin(originNas) {
    const raw = String(originNas || "").trim();
    if (!raw) return "";
    return raw.length > 8 ? raw.slice(0, 8) : raw;
  }

  // EXTENDED_CIRCLE_BUCKET_CLASSIFIER_V1
  function classifyFederatedEvent(ev, mode) {
    const originNas = String(ev && ev.origin_nas ? ev.origin_nas : "").trim();
    const m = normalizeMode(mode);
    const info = originNas ? originInfoByNas.get(originNas) : null;

    if (originNas && mutedOrigins.has(originNas)) {
      return {
        bucket: "hidden_muted",
        label: "Muted for me",
        priority: 0,
        reason: "This origin is muted for your user."
      };
    }

    if (info && info.enabled === false) {
      return {
        bucket: "hidden_disabled",
        label: "Globally disabled",
        priority: 0,
        reason: "This origin is disabled globally for the server."
      };
    }

    if (info) {
      return {
        bucket: "my_circle",
        label: "My Circle",
        priority: 100,
        reason: "This origin is already known by your server."
      };
    }

    if (m === "discover") {
      return {
        bucket: "wider_public",
        label: "Wider public",
        priority: 10,
        reason: "This is public federated content from an origin outside your known circle."
      };
    }

    return {
      bucket: "wider_public",
      label: "Public federated",
      priority: 10,
      reason: "This is public federated content."
    };
  }

  function originReason(ev, mode) {
    const originNas = String(ev && ev.origin_nas ? ev.origin_nas : "").trim();
    const m = normalizeMode(mode);
    const info = originNas ? originInfoByNas.get(originNas) : null;

    if (!originNas) {
      if (m === "discover") {
        return {
          label: "Wider public",
          detail:
            "This Discover item did not include a remote NAS origin id. " +
            "Discover currently uses the public federated feed until Extended Circle ranking is enabled.",
          tone: "neutral"
        };
      }

      return {
        label: "Public federated",
        detail: "This event did not include a remote NAS origin id, so it is shown as a generic federated event.",
        tone: "neutral"
      };
    }

    if (mutedOrigins.has(originNas)) {
      return {
        label: "Muted for me",
        detail: "This origin is muted for your user. Normally it should be hidden from your federated feed.",
        tone: "muted"
      };
    }

    if (info && info.enabled === false) {
      return {
        label: "Globally disabled",
        detail: "This origin is disabled globally for the server. Existing cached events may still be visible.",
        tone: "warning"
      };
    }

    if (m === "my_circle") {
      return {
        label: "My Circle",
        detail:
          `Shown because ${info && info.display_name ? info.display_name : "this NAS"} is in Known origins. ` +
          `Origin: ${shortOrigin(originNas)}. Source: ${info && info.source ? info.source : "known origin"}.`,
        tone: "known"
      };
    }

    if (m === "discover" && info) {
      return {
        label: "Discover",
        detail:
          `Shown in Discover because ${info && info.display_name ? info.display_name : "this NAS"} is already a known origin. ` +
          "Discover currently uses the public federated feed until Extended Circle ranking is enabled. " +
          `Origin: ${shortOrigin(originNas)}. Source: ${info.source || "known origin"}.` +
          (info.public_base_url ? ` URL: ${info.public_base_url}.` : ""),
        tone: "known"
      };
    }

    if (info) {
      return {
        label: "Known origin",
        detail:
          `This came from a NAS origin your server knows. ` +
          `Origin: ${shortOrigin(originNas)}. Source: ${info.source || "known origin"}.` +
          (info.public_base_url ? ` URL: ${info.public_base_url}.` : ""),
        tone: "known"
      };
    }

    if (m === "discover") {
      return {
        label: "Wider public",
        detail:
          "Shown in Discover as wider public federated content. " +
          "Discover currently uses the public federated feed until Extended Circle ranking is enabled. " +
          `Origin: ${shortOrigin(originNas)}.`,
        tone: "neutral"
      };
    }

    return {
      label: "Public federated",
      detail:
        `This came from a federated NAS origin that is not currently in your Known origins list. ` +
        `Origin: ${shortOrigin(originNas)}.`,
      tone: "neutral"
    };
  }

  function decorateFederatedEvent(card, ev, mode) {
    if (!card || !ev) return;

    const old = card.querySelector(".cs-federated-reason");
    if (old) old.remove();

    const classification = classifyFederatedEvent(ev, mode);
    const reason = originReason(ev, mode);

    card.dataset.federationBucket = classification.bucket || "";
    card.dataset.federationPriority = String(classification.priority || 0);

    const row = document.createElement("div");
    row.className = `cs-federated-reason cs-federated-reason-${reason.tone || "neutral"}`;

    const pill = document.createElement("span");
    pill.className = "cs-federated-reason-pill";
    pill.textContent = reason.label;

    const why = document.createElement("button");
    why.className = "cs-federated-reason-why";
    why.type = "button";
    why.textContent = "Why?";

    const detail = document.createElement("div");
    detail.className = "cs-federated-reason-detail";

    // FEDERATED_REASON_EVENT_ID_DETAIL_V1
    const eventId = String(ev && ev.event_id ? ev.event_id : "").trim();
    detail.textContent = eventId
      ? `${reason.detail} Event: ${eventId}.`
      : reason.detail;

    detail.hidden = true;

    why.addEventListener("click", () => {
      detail.hidden = !detail.hidden;
      why.textContent = detail.hidden ? "Why?" : "Hide";
    });

    row.appendChild(pill);
    row.appendChild(why);
    row.appendChild(detail);

    const header = card.querySelector(".cs-post-header");
    if (header && header.nextSibling) {
      card.insertBefore(row, header.nextSibling);
    } else if (header) {
      card.appendChild(row);
    } else {
      card.prepend(row);
    }
  }

  window.CircleStackFeedModes = {
    normalizeMode,
    isFederatedSurface,
    filterFederatedEvents,
    emptyMessage,
    setActiveButtons,
    initButtons,
    refreshOriginSets,
    classifyFederatedEvent,
    decorateFederatedEvent
  };
})();
