/* apps/bundled/circlestack/src/www/feed_modes.js
 * Circle Stack feed mode helpers.
 *
 * This file intentionally owns the new feed-mode logic so app.js does not
 * keep growing for every new federation/feed feature.
 */
(() => {
  "use strict";

  const CS_API_LOCAL = "/api/v4/circlestack";

  function t(key, vars = null, fallback = undefined) {
    if (typeof vars === "string" && fallback === undefined) {
      fallback = vars;
      vars = null;
    }

    if (window.csT && typeof window.csT === "function") {
      return window.csT(key, vars, fallback);
    }

    return String(fallback ?? key);
  }

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

    const maxWiderPublic = 5;
    let widerPublicShown = 0;

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
      .filter((item) => {
        const bucket = item.classification && item.classification.bucket
          ? item.classification.bucket
          : "";

        if (bucket !== "wider_public") {
          return true;
        }

        if (widerPublicShown >= maxWiderPublic) {
          return false;
        }

        widerPublicShown += 1;
        return true;
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
    if (m === "federated") return t("feedMode.federated", "Mode: Federated");
    if (m === "my_circle") return t("feedMode.myCircle", "Mode: My Circle");
    if (m === "discover") return t("feedMode.discover", "Mode: Discover");
    return t("feedMode.feed", "Mode: Feed");
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
      cycleBtn.title = t("feedMode.switchTitle", "Click to switch feed mode");
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
    const originShort = shortOrigin(originNas);
    const knownOriginSource = t("feedReason.source.knownOrigin", "known origin");

    function urlText() {
      return info && info.public_base_url
        ? t("feedReason.urlSuffix", { url: info.public_base_url }, ` URL: ${info.public_base_url}.`)
        : "";
    }

    if (!originNas) {
      if (m === "discover") {
        return {
          label: t("feedReason.label.widerPublic", "Wider public"),
          detail: t(
            "feedReason.detail.discoverMissingOrigin",
            "This Discover item did not include a remote NAS origin id. Discover currently uses the public federated feed until Extended Circle ranking is enabled."
          ),
          tone: "neutral"
        };
      }

      return {
        label: t("feedReason.label.publicFederated", "Public federated"),
        detail: t(
          "feedReason.detail.missingOrigin",
          "This event did not include a remote NAS origin id, so it is shown as a generic federated event."
        ),
        tone: "neutral"
      };
    }

    if (mutedOrigins.has(originNas)) {
      return {
        label: t("feedReason.label.mutedForMe", "Muted for me"),
        detail: t(
          "feedReason.detail.mutedForMe",
          "This origin is muted for your user. Normally it should be hidden from your federated feed."
        ),
        tone: "muted"
      };
    }

    if (info && info.enabled === false) {
      return {
        label: t("feedReason.label.globallyDisabled", "Globally disabled"),
        detail: t(
          "feedReason.detail.globallyDisabled",
          "This origin is disabled globally for the server. Existing cached events may still be visible."
        ),
        tone: "warning"
      };
    }

    if (m === "my_circle") {
      const displayName = info && info.display_name
        ? info.display_name
        : t("feedReason.thisNas", "this NAS");
      const source = info && info.source ? info.source : knownOriginSource;

      return {
        label: t("feedReason.label.myCircle", "My Circle"),
        detail: t(
          "feedReason.detail.myCircle",
          { displayName, origin: originShort, source },
          `Shown because ${displayName} is in Known origins. Origin: ${originShort}. Source: ${source}.`
        ),
        tone: "known"
      };
    }

    if (m === "discover" && info) {
      const displayName = info && info.display_name
        ? info.display_name
        : t("feedReason.thisNas", "this NAS");
      const source = info.source || knownOriginSource;

      return {
        label: t("feedReason.label.discover", "Discover"),
        detail:
          t(
            "feedReason.detail.discoverKnown",
            { displayName, origin: originShort, source },
            `Shown in Discover because ${displayName} is already a known origin. Discover currently uses the public federated feed until Extended Circle ranking is enabled. Origin: ${originShort}. Source: ${source}.`
          ) + urlText(),
        tone: "known"
      };
    }

    if (info) {
      const source = info.source || knownOriginSource;

      return {
        label: t("feedReason.label.knownOrigin", "Known origin"),
        detail:
          t(
            "feedReason.detail.knownOrigin",
            { origin: originShort, source },
            `This came from a NAS origin your server knows. Origin: ${originShort}. Source: ${source}.`
          ) + urlText(),
        tone: "known"
      };
    }

    if (m === "discover") {
      return {
        label: t("feedReason.label.widerPublic", "Wider public"),
        detail: t(
          "feedReason.detail.discoverWiderPublic",
          { origin: originShort },
          `Shown in Discover as wider public federated content. Discover currently uses the public federated feed until Extended Circle ranking is enabled. Origin: ${originShort}.`
        ),
        tone: "neutral"
      };
    }

    return {
      label: t("feedReason.label.publicFederated", "Public federated"),
      detail: t(
        "feedReason.detail.publicFederated",
        { origin: originShort },
        `This came from a federated NAS origin that is not currently in your Known origins list. Origin: ${originShort}.`
      ),
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
    why.textContent = t("feedMode.why", "Why?");

    const detail = document.createElement("div");
    detail.className = "cs-federated-reason-detail";

    // FEDERATED_REASON_EVENT_ID_DETAIL_V1
    const eventId = String(ev && ev.event_id ? ev.event_id : "").trim();
    detail.textContent = eventId
      ? `${reason.detail}${t("feedReason.eventSuffix", { event_id: eventId }, ` Event: ${eventId}.`)}`
      : reason.detail;

    detail.hidden = true;

    why.addEventListener("click", () => {
      detail.hidden = !detail.hidden;
      why.textContent = detail.hidden ? t("feedMode.why", "Why?") : t("feedMode.hide", "Hide");
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
