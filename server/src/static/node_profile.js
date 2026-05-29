(function () {
  "use strict";

  function shortNodeId(value) {
    value = String(value || "");
    return value.length >= 12 ? value.slice(0, 12) + "…" : value;
  }

  function text(value) {
    return value == null ? "" : String(value);
  }

  function formatNumber(value) {
    const n = Number(value || 0);
    if (!Number.isFinite(n)) return "0";
    return n.toLocaleString();
  }

  function removeExisting() {
    const old = document.querySelector(".nodeProfileOverlay");
    if (old) old.remove();
  }

  function badgeRow(badge) {
    const row = document.createElement("div");
    row.className = "nodeProfileBadge " + (badge.unlocked ? "unlocked" : "locked");

    const img = document.createElement("img");
    img.className = "nodeProfileBadgeIcon";
    img.src = badge.icon_asset || "";
    img.alt = "";
    img.loading = "eager";
    img.decoding = "sync";

    const body = document.createElement("div");

    const title = document.createElement("div");
    title.className = "nodeProfileBadgeTitle";
    title.textContent = badge.title || "Node badge";

    const desc = document.createElement("div");
    desc.className = "nodeProfileBadgeDesc";
    desc.textContent = badge.description || "";

    body.appendChild(title);
    body.appendChild(desc);

    const tier = document.createElement("div");
    tier.className = "nodeProfileBadgeTier";
    tier.textContent = badge.unlocked ? (badge.tier || "node") : "locked";

    row.appendChild(img);
    row.appendChild(body);
    row.appendChild(tier);

    return row;
  }

  function makeStat(label, value) {
    const box = document.createElement("div");
    box.className = "nodeProfileStat";

    const strong = document.createElement("strong");
    strong.textContent = formatNumber(value);

    const span = document.createElement("span");
    span.textContent = label;

    box.appendChild(strong);
    box.appendChild(span);

    return box;
  }

  function makeWindow() {
    removeExisting();

    const overlay = document.createElement("div");
    overlay.className = "nodeProfileOverlay";

    const win = document.createElement("div");
    win.className = "nodeProfileWindow";
    win.setAttribute("role", "dialog");
    win.setAttribute("aria-modal", "true");
    win.setAttribute("aria-label", "DNA-Nexus node profile");

    const titlebar = document.createElement("div");
    titlebar.className = "nodeProfileTitlebar";

    const title = document.createElement("div");
    const kicker = document.createElement("div");
    kicker.className = "nodeProfileKicker";
    kicker.textContent = "Node Profile";

    const sub = document.createElement("div");
    sub.className = "nodeProfileSub";
    sub.textContent = "Detached window";

    title.appendChild(kicker);
    title.appendChild(sub);

    const close = document.createElement("button");
    close.className = "nodeProfileClose";
    close.type = "button";
    close.textContent = "×";
    close.setAttribute("aria-label", "Close");

    titlebar.appendChild(title);
    titlebar.appendChild(close);

    const body = document.createElement("div");
    body.className = "nodeProfileBody";
    body.textContent = "Loading node profile…";

    win.appendChild(titlebar);
    win.appendChild(body);
    overlay.appendChild(win);
    document.body.appendChild(overlay);

    close.addEventListener("click", () => overlay.remove());
    overlay.addEventListener("click", (ev) => {
      if (ev.target === overlay) overlay.remove();
    });
    document.addEventListener("keydown", function onKey(ev) {
      if (!document.body.contains(overlay)) {
        document.removeEventListener("keydown", onKey);
        return;
      }
      if (ev.key === "Escape") overlay.remove();
    });

    let drag = null;
    titlebar.addEventListener("pointerdown", (ev) => {
      if (ev.target === close) return;
      const rect = win.getBoundingClientRect();
      drag = {
        x: ev.clientX,
        y: ev.clientY,
        left: rect.left,
        top: rect.top
      };
      win.style.position = "fixed";
      win.style.margin = "0";
      win.style.left = rect.left + "px";
      win.style.top = rect.top + "px";
      win.style.width = rect.width + "px";
      titlebar.setPointerCapture(ev.pointerId);
    });

    titlebar.addEventListener("pointermove", (ev) => {
      if (!drag) return;
      const nextLeft = Math.max(8, Math.min(window.innerWidth - 80, drag.left + ev.clientX - drag.x));
      const nextTop = Math.max(8, Math.min(window.innerHeight - 60, drag.top + ev.clientY - drag.y));
      win.style.left = nextLeft + "px";
      win.style.top = nextTop + "px";
    });

    titlebar.addEventListener("pointerup", () => {
      drag = null;
    });

    return { overlay, body };
  }

  function renderNodeProfile(body, data) {
    body.textContent = "";

    const node = data && data.node ? data.node : {};
    const stats = data && data.stats ? data.stats : {};
    const badges = Array.isArray(data && data.badges) ? data.badges : [];

    const hero = document.createElement("div");
    hero.className = "nodeProfileHero";

    const orb = document.createElement("div");
    orb.className = "nodeProfileOrb";

    const hbody = document.createElement("div");
    const h2 = document.createElement("h2");
    h2.textContent = node.name || "DNA-Nexus Node";

    const nodeId = document.createElement("div");
    nodeId.className = "nodeProfileNodeId";
    nodeId.textContent = node.node_id
      ? "Node ID: " + text(node.node_id)
      : "Node ID: not configured";

    hbody.appendChild(h2);
    hbody.appendChild(nodeId);

    hero.appendChild(orb);
    hero.appendChild(hbody);
    body.appendChild(hero);

    const statsGrid = document.createElement("div");
    statsGrid.className = "nodeProfileStats";
    statsGrid.appendChild(makeStat("Remote events", stats.remote_events_total));
    statsGrid.appendChild(makeStat("Remote posts", stats.remote_posts_total));
    statsGrid.appendChild(makeStat("Remote interactions", stats.remote_interactions_total));
    statsGrid.appendChild(makeStat("Known origins", stats.known_origins_total));
    statsGrid.appendChild(makeStat("Sent events", stats.federation_outbox_done));
    statsGrid.appendChild(makeStat("Failed sends", stats.federation_outbox_failed));
    body.appendChild(statsGrid);

    const earnedTitle = document.createElement("div");
    earnedTitle.className = "nodeProfileSectionTitle";
    earnedTitle.textContent = "Node badges";
    body.appendChild(earnedTitle);

    const list = document.createElement("div");
    list.className = "nodeProfileBadges";

    for (const badge of badges) {
      list.appendChild(badgeRow(badge));
    }

    body.appendChild(list);
  }

  async function openNodeProfile() {
    const ui = makeWindow();

    try {
      const res = await fetch("/api/v4/circlestack/node-profile", {
        credentials: "same-origin",
        cache: "no-store"
      });

      const data = await res.json().catch(() => null);

      if (!res.ok || !data || data.ok === false) {
        throw new Error((data && (data.message || data.error)) || ("HTTP " + res.status));
      }

      renderNodeProfile(ui.body, data);
    } catch (err) {
      ui.body.textContent = "";
      const box = document.createElement("div");
      box.className = "nodeProfileError";
      box.textContent = "Could not load node profile: " + (err && err.message ? err.message : String(err));
      ui.body.appendChild(box);
    }
  }

  function init() {
    const btn = document.getElementById("nodeProfileButton");
    if (!btn) return;

    btn.addEventListener("click", openNodeProfile);
  }

  document.addEventListener("DOMContentLoaded", init);
})();
