(() => {
  "use strict";

  const MEMORY_ITEM_REACTIONS = ["👍", "❤️", "😂", "😮", "👏", "🔥"];

  const API = "/api/v4/circlestack";

  function memT(key, vars = null, fallback = undefined) {
    if (typeof window.csT === "function") {
      return window.csT(key, vars, fallback);
    }
    return String(fallback ?? key);
  }

  function el(tag, className, text) {
    const node = document.createElement(tag);
    if (className) node.className = className;
    if (text !== undefined && text !== null) node.textContent = text;
    return node;
  }

  function memoryNodeIcon(className = "cs-memory-icon") {
    const wrap = el("span", className);
    const img = document.createElement("img");
    img.src = "memory_node_icon.svg?v=20260523-simpleicon1";
    img.alt = "";
    img.decoding = "async";
    img.loading = "lazy";
    wrap.appendChild(img);
    return wrap;
  }

  function isVideo(item) {
    return String(item?.media_kind || "").toLowerCase() === "video";
  }

  function mediaName(path) {
    const s = String(path || "");
    const parts = s.split("/").filter(Boolean);
    return parts[parts.length - 1] || s || "media";
  }

  async function pickMedia(input, previewBox) {
    if (typeof window.csOpenMediaPicker !== "function") return;

    const picked = await window.csOpenMediaPicker();
    if (!picked) return;

    input.value = picked;
    renderPickedPreview(previewBox, picked);
  }

  function renderPickedPreview(box, path) {
    if (!box) return;

    box.textContent = "";

    if (!path) {
      box.hidden = true;
      return;
    }

    const label = el("div", "cs-memory-picked-label", mediaName(path));
    const kind = /\.(mp4|webm|mov|m4v)$/i.test(path) ? memT("memory.kind.video", "Video") : memT("memory.kind.image", "Image");

    const badge = el("span", "cs-memory-kind", kind);
    label.prepend(badge);

    box.appendChild(label);
    box.hidden = false;
  }

  function modalShell(titleText, subtitleText = "") {
    const backdrop = el("div", "cs-modal-backdrop");
    const modal = el("div", "cs-modal cs-memory-modal");

    const title = el("div", "cs-modal-title", titleText);
    modal.appendChild(title);

    if (subtitleText) {
      modal.appendChild(el("div", "cs-modal-text", subtitleText));
    }

    const close = () => backdrop.remove();

    backdrop.addEventListener("click", (ev) => {
      if (ev.target === backdrop) close();
    });

    backdrop.appendChild(modal);
    document.body.appendChild(backdrop);

    return { backdrop, modal, close };
  }

  async function createMemoryNode(payload) {
    const res = await fetch(`${API}/memory-nodes/create`, {
      method: "POST",
      credentials: "same-origin",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload)
    });

    const data = await res.json().catch(() => ({}));
    if (!res.ok || !data.ok) {
      throw new Error(data.error || `HTTP ${res.status}`);
    }

    return data;
  }

  async function addMemoryItem(nodeId, payload) {
    const res = await fetch(`${API}/memory-nodes/items/add`, {
      method: "POST",
      credentials: "same-origin",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ node_id: nodeId, ...payload })
    });

    const data = await res.json().catch(() => ({}));
    if (!res.ok || !data.ok) {
      throw new Error(data.error || `HTTP ${res.status}`);
    }

    return data.item || null;
  }

  async function deleteMemoryItem(itemId) {
    const res = await fetch(`${API}/memory-nodes/items?id=${encodeURIComponent(itemId)}`, {
      method: "DELETE",
      credentials: "same-origin"
    });

    const data = await res.json().catch(() => ({}));
    if (!res.ok || !data.ok) {
      throw new Error(data.error || `HTTP ${res.status}`);
    }

    return true;
  }

  function openCreateNodeModal() {
    const { modal, close } = modalShell(
      "Open Memory Node",
      "Create a shared memory room. Friends can add their own media from their own NAS files."
    );

    const title = document.createElement("input");
    title.className = "cs-memory-input";
    title.placeholder = memT("memory.titlePlaceholder", "Title, e.g. Saturday match");

    const body = document.createElement("textarea");
    body.className = "cs-memory-textarea";
    body.placeholder = memT("memory.bodyPlaceholder", "Tell what this Memory Node is about...");

    const visibility = document.createElement("select");
    visibility.className = "cs-memory-input";
    visibility.innerHTML = `
      <option value="circle">${memT("visibility.circle", "👥 Circle").replace(/^\S+\s+/, "")}</option>
      <option value="public">${memT("visibility.public", "🌍 Public").replace(/^\S+\s+/, "")}</option>
      <option value="private">${memT("visibility.private", "🔒 Private").replace(/^\S+\s+/, "")}</option>
    `;

    const mediaRow = el("div", "cs-memory-media-row");
    const mediaInput = document.createElement("input");
    mediaInput.className = "cs-memory-input";
    mediaInput.placeholder = memT("memory.optionalFirstMedia", "Optional first image/video path");

    const browse = el("button", "cs-memory-secondary", memT("common.browse", "Browse"));
    browse.type = "button";

    const pickedPreview = el("div", "cs-memory-picked");
    pickedPreview.hidden = true;

    browse.addEventListener("click", () => pickMedia(mediaInput, pickedPreview));

    const caption = document.createElement("input");
    caption.className = "cs-memory-input";
    caption.placeholder = memT("memory.optionalFirstCaption", "Optional caption for first media");

    mediaRow.appendChild(mediaInput);
    mediaRow.appendChild(browse);

    const actions = el("div", "cs-modal-actions");
    const cancel = el("button", "cs-modal-cancel", memT("common.cancel", "Cancel"));
    cancel.type = "button";

    const create = el("button", "cs-modal-primary", "Open Memory Node");
    create.type = "button";

    cancel.addEventListener("click", close);

    create.addEventListener("click", async () => {
      const payload = {
        title: title.value.trim(),
        body: body.value.trim(),
        visibility: visibility.value,
        media_path: mediaInput.value.trim(),
        caption: caption.value.trim()
      };

      if (!payload.title && !payload.body && !payload.media_path) {
        title.focus();
        return;
      }

      create.disabled = true;
      create.textContent = memT("memory.opening", "Opening…");

      try {
        await createMemoryNode(payload);
        close();

        if (typeof window.csLoadFeed === "function") {
          await window.csLoadFeed();
        }
      } catch (e) {
        alert(memT("memory.nodeFailed", { error: e.message || e }, `Memory Node failed: ${e.message || e}`));
      } finally {
        create.disabled = false;
        create.textContent = memT("memory.openNode", "Open Memory Node");
      }
    });

    actions.appendChild(cancel);
    actions.appendChild(create);

    modal.appendChild(title);
    modal.appendChild(body);
    modal.appendChild(visibility);
    modal.appendChild(mediaRow);
    modal.appendChild(pickedPreview);
    modal.appendChild(caption);
    modal.appendChild(actions);

    title.focus();
  }

  function openAddItemModal(node, onAdded) {
    const { modal, close } = modalShell(
      memT("memory.addToNode", "Add to Memory Node"),
      memT("memory.addSubtitle", "Pick an image or video from your own NAS files. The media stays in your storage.")
    );

    const mediaRow = el("div", "cs-memory-media-row");

    const mediaInput = document.createElement("input");
    mediaInput.className = "cs-memory-input";
    mediaInput.placeholder = memT("memory.imageVideoPath", "Image/video path");

    const browse = el("button", "cs-memory-secondary", memT("common.browse", "Browse"));
    browse.type = "button";

    const pickedPreview = el("div", "cs-memory-picked");
    pickedPreview.hidden = true;

    browse.addEventListener("click", () => pickMedia(mediaInput, pickedPreview));

    const caption = document.createElement("input");
    caption.className = "cs-memory-input";
    caption.placeholder = memT("memory.optionalCaption", "Optional caption");

    mediaRow.appendChild(mediaInput);
    mediaRow.appendChild(browse);

    const actions = el("div", "cs-modal-actions");
    const cancel = el("button", "cs-modal-cancel", memT("common.cancel", "Cancel"));
    cancel.type = "button";

    const add = el("button", "cs-modal-delete", memT("memory.addMedia", "Add media"));
    add.type = "button";

    cancel.addEventListener("click", close);

    add.addEventListener("click", async () => {
      const media_path = mediaInput.value.trim();
      if (!media_path) {
        mediaInput.focus();
        return;
      }

      add.disabled = true;
      add.textContent = memT("memory.adding", "Adding…");

      try {
        const item = await addMemoryItem(node.id, {
          media_path,
          caption: caption.value.trim()
        });

        close();

        if (typeof onAdded === "function" && item) {
          onAdded(item);
        } else if (typeof window.csLoadFeed === "function") {
          await window.csLoadFeed();
        }
      } catch (e) {
        alert(memT("memory.addFailed", { error: e.message || e }, `Add failed: ${e.message || e}`));
      } finally {
        add.disabled = false;
        add.textContent = memT("memory.addMedia", "Add media");
      }
    });

    actions.appendChild(cancel);
    actions.appendChild(add);

    modal.appendChild(mediaRow);
    modal.appendChild(pickedPreview);
    modal.appendChild(caption);
    modal.appendChild(actions);

    mediaInput.focus();
  }

  function openMemoryImageLightbox(item) {
    if (!item || !item.media_url) return;

    const backdrop = el("div", "cs-memory-lightbox");
    const closeBtn = el("button", "cs-memory-lightbox-close", "×");
    closeBtn.type = "button";
    closeBtn.setAttribute("aria-label", memT("memory.closeImage", "Close image"));

    const figure = document.createElement("figure");
    figure.className = "cs-memory-lightbox-figure";

    const img = document.createElement("img");
    img.src = item.media_url;
    img.alt = item.caption || "";

    const caption = document.createElement("figcaption");
    caption.className = "cs-memory-lightbox-caption";

    const owner = el(
      "div",
      "cs-memory-lightbox-owner",
      item.owner_display_name || item.owner_fp_short || "unknown"
    );
    caption.appendChild(owner);

    if (item.caption) {
      caption.appendChild(el("div", "cs-memory-lightbox-text", item.caption));
    }

    figure.appendChild(img);
    figure.appendChild(caption);

    const close = () => {
      document.removeEventListener("keydown", onKeyDown);
      backdrop.remove();
    };

    function onKeyDown(ev) {
      if (ev.key === "Escape") close();
    }

    closeBtn.addEventListener("click", close);

    img.addEventListener("click", (ev) => {
      ev.stopPropagation();
      close();
    });

    backdrop.addEventListener("click", (ev) => {
      if (ev.target === backdrop) close();
    });

    document.addEventListener("keydown", onKeyDown);

    backdrop.appendChild(closeBtn);
    backdrop.appendChild(figure);
    document.body.appendChild(backdrop);

    closeBtn.focus();
  }

  function confirmRemoveMemoryItem(item) {
    return new Promise((resolve) => {
      const { modal, close } = modalShell(
        memT("memory.removeMediaTitle", "Remove media?"),
        memT("memory.removeMediaText", "This removes the media from this Memory Node. The original file stays in the owner's NAS storage.")
      );

      const detail = el("div", "cs-memory-remove-detail");

      const owner = el(
        "div",
        "cs-memory-remove-owner",
        item.owner_display_name || item.owner_fp_short || "unknown"
      );

      detail.appendChild(owner);

      if (item.caption) {
        detail.appendChild(el("div", "cs-memory-remove-caption", item.caption));
      }

      modal.appendChild(detail);

      const actions = el("div", "cs-modal-actions");

      const cancel = el("button", "cs-modal-cancel", memT("common.cancel", "Cancel"));
      cancel.type = "button";

      const remove = el("button", "cs-modal-delete cs-memory-remove-confirm", memT("common.remove", "Remove"));
      remove.type = "button";

      const done = (value) => {
        close();
        resolve(value);
      };

      cancel.addEventListener("click", () => done(false));
      remove.addEventListener("click", () => done(true));

      actions.appendChild(cancel);
      actions.appendChild(remove);
      modal.appendChild(actions);

      cancel.focus();
    });
  }

  function memoryItemReactionNames(summary) {
    const people = Array.isArray(summary?.people) ? summary.people : [];
    return people
      .map(p => p.display_name || p.fp_short || p.fp || "")
      .filter(Boolean)
      .join(", ");
  }

  async function reactMemoryItem(itemId, reaction) {
    const res = await fetch(`${API}/memory-nodes/items/react`, {
      method: "POST",
      credentials: "same-origin",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ item_id: itemId, reaction })
    });

    const data = await res.json().catch(() => ({}));
    if (!res.ok || !data.ok) {
      throw new Error(data.error || data.detail || `HTTP ${res.status}`);
    }

    return data.item || null;
  }

  function renderMemoryItemReactionBar(item) {
    const wrap = el("div", "cs-memory-item-reactions");

    const summaries = new Map(
      (Array.isArray(item.reactions) ? item.reactions : [])
        .map(r => [r.reaction, r])
    );

    const summaryRow = el("div", "cs-memory-item-reaction-summary");

    for (const reaction of MEMORY_ITEM_REACTIONS) {
      const summary = summaries.get(reaction);
      const count = Number(summary?.count || 0);
      if (!summary || count <= 0) continue;

      const isMine = item.my_reaction === reaction || summary.reacted_by_me === true;

      const chip = el("button", "cs-memory-item-reaction-chip", `${reaction} ${count}`);
      chip.type = "button";
      if (isMine) chip.classList.add("is-active");

      const names = memoryItemReactionNames(summary);
      chip.setAttribute("aria-label", names ? `${reaction} ${names}` : reaction);

      chip.addEventListener("click", async (ev) => {
        ev.stopPropagation();

        try {
          chip.disabled = true;
          const updated = await reactMemoryItem(item.id, isMine ? "" : reaction);
          if (updated) Object.assign(item, updated);

          const next = renderMemoryItemReactionBar(item);
          wrap.replaceWith(next);
        } catch (e) {
          alert(memT("reaction.failed", { error: e.message || e }, `Reaction failed: ${e.message || e}`));
        }
      });

      summaryRow.appendChild(chip);
    }

    const actionRow = el("div", "cs-memory-item-reaction-actions");

    for (const reaction of MEMORY_ITEM_REACTIONS) {
      const isMine = item.my_reaction === reaction;

      const btn = el("button", "cs-memory-item-reaction-button", reaction);
      btn.type = "button";
      btn.classList.toggle("is-active", isMine);
      btn.setAttribute("aria-label", isMine ? memT("memory.removeReactionEmoji", { reaction }, `Remove ${reaction}`) : memT("reaction.reactEmoji", { reaction }, `React ${reaction}`));

      btn.addEventListener("click", async (ev) => {
        ev.stopPropagation();

        try {
          btn.disabled = true;
          const updated = await reactMemoryItem(item.id, isMine ? "" : reaction);
          if (updated) Object.assign(item, updated);

          const next = renderMemoryItemReactionBar(item);
          wrap.replaceWith(next);
        } catch (e) {
          alert(memT("reaction.failed", { error: e.message || e }, `Reaction failed: ${e.message || e}`));
        }
      });

      actionRow.appendChild(btn);
    }

    if (summaryRow.children.length) {
      wrap.appendChild(summaryRow);
    }

    wrap.appendChild(actionRow);
    return wrap;
  }

  function renderItem(item, onDeleted) {
    const tile = el("div", `cs-memory-item is-${item.media_kind || "image"}`);

    const frame = el("div", "cs-memory-frame");

    if (isVideo(item)) {
      const video = document.createElement("video");
      video.src = item.media_url;
      video.controls = true;
      video.preload = "metadata";
      frame.appendChild(video);
    } else {
      const img = document.createElement("img");
      img.src = item.media_url;
      img.loading = "lazy";
      img.decoding = "async";
      img.alt = item.caption || "";

      frame.classList.add("is-clickable");
      frame.tabIndex = 0;
      frame.setAttribute("role", "button");
      frame.setAttribute("aria-label", memT("memory.openImage", "Open image"));
      frame.title = memT("memory.openImage", "Open image");

      frame.addEventListener("click", () => openMemoryImageLightbox(item));
      frame.addEventListener("keydown", (ev) => {
        if (ev.key === "Enter" || ev.key === " ") {
          ev.preventDefault();
          openMemoryImageLightbox(item);
        }
      });

      frame.appendChild(img);
    }

    tile.appendChild(frame);

    const meta = el("div", "cs-memory-item-meta");

    const owner = el("div", "cs-memory-item-owner", item.owner_display_name || item.owner_fp_short || "unknown");
    meta.appendChild(owner);

    if (item.caption) {
      meta.appendChild(el("div", "cs-memory-caption", item.caption));
    }

    meta.appendChild(renderMemoryItemReactionBar(item));

    if (item.can_delete) {
      const del = el("button", "cs-memory-delete", memT("common.remove", "Remove"));
      del.type = "button";
      del.addEventListener("click", async (ev) => {
        ev.stopPropagation();

        const ok = await confirmRemoveMemoryItem(item);
        if (!ok) return;

        try {
          del.disabled = true;
          del.textContent = memT("common.removing", "Removing…");

          await deleteMemoryItem(item.id);
          tile.remove();
          if (typeof onDeleted === "function") onDeleted(item);
        } catch (e) {
          del.disabled = false;
          del.textContent = memT("common.remove", "Remove");
          alert(memT("common.removeFailed", { error: e.message || e }, `Remove failed: ${e.message || e}`));
        }
      });
      meta.appendChild(del);
    }

    tile.appendChild(meta);
    return tile;
  }

  function memoryNodeContributors(node) {
    const items = Array.isArray(node?.items) ? node.items : [];
    const seen = new Map();

    for (const item of items) {
      const fp = String(item.owner_fp || item.owner_fp_short || "").trim();
      const key = fp || String(item.owner_display_name || "").trim();
      if (!key || seen.has(key)) continue;

      seen.set(key, {
        fp,
        name: item.owner_display_name || item.owner_fp_short || "unknown",
        avatar_url: item.owner_avatar_url || ""
      });
    }

    return Array.from(seen.values())
      .sort((a, b) => String(a.name || "").localeCompare(String(b.name || "")));
  }

  function updateMemoryStats(statsEl, node) {
    if (!statsEl || !node) return;

    const items = Array.isArray(node.items) ? node.items : [];
    const mediaCount = items.length;
    const contributors = memoryNodeContributors(node);
    const contributorCount = contributors.length || Number(node.contributors_count || 0);

    statsEl.textContent = "";
    statsEl.tabIndex = 0;
    statsEl.removeAttribute("title");

    const label = el(
      "span",
      "cs-memory-stats-label",
      memT("memory.stats", { media: mediaCount, contributors: contributorCount }, `${mediaCount} media · ${contributorCount} contributors`)
    );
    statsEl.appendChild(label);

    const pop = el("div", "cs-memory-contributors-popover");

    const title = el("div", "cs-memory-contributors-title", memT("memory.contributors", "Contributors"));
    pop.appendChild(title);

    if (!contributors.length) {
      pop.appendChild(el("div", "cs-memory-contributor-empty", memT("memory.noContributors", "No contributors yet")));
    } else {
      for (const person of contributors) {
        const row = el("div", "cs-memory-contributor-row");

        const avatar = el("span", "cs-memory-contributor-avatar");
        if (person.avatar_url) {
          const img = document.createElement("img");
          img.src = person.avatar_url;
          img.alt = "";
          avatar.appendChild(img);
        } else {
          avatar.textContent = String(person.name || "?").slice(0, 1).toUpperCase();
        }

        const name = el("span", "cs-memory-contributor-name", person.name || "unknown");

        row.appendChild(avatar);
        row.appendChild(name);
        pop.appendChild(row);
      }
    }

    statsEl.removeAttribute("title");
    statsEl.appendChild(pop);
  }

  function formatMemoryBytes(bytes) {
    const n = Number(bytes || 0);
    if (!Number.isFinite(n) || n <= 0) return "0 B";

    const units = ["B", "KiB", "MiB", "GiB", "TiB"];
    let v = n;
    let i = 0;

    while (v >= 1024 && i < units.length - 1) {
      v /= 1024;
      i++;
    }

    if (i === 0) return `${Math.round(v)} ${units[i]}`;

    const digits = v >= 100 ? 1 : 2;
    return `${v.toFixed(digits)} ${units[i]}`;
  }

  function memoryNodeOwnershipRows(node) {
    const items = Array.isArray(node?.items) ? node.items : [];
    const byOwner = new Map();

    for (const item of items) {
      const fp = String(item.owner_fp || item.owner_fp_short || "").trim();
      const key = fp || String(item.owner_display_name || "unknown").trim();
      const name = item.owner_display_name || item.owner_fp_short || "unknown";
      const bytes = Number(item.media_bytes || 0);

      if (!byOwner.has(key)) {
        byOwner.set(key, {
          fp,
          name,
          count: 0,
          bytes: 0
        });
      }

      const row = byOwner.get(key);
      row.count += 1;
      if (Number.isFinite(bytes) && bytes > 0) {
        row.bytes += bytes;
      }
    }

    return Array.from(byOwner.values())
      .sort((a, b) => String(a.name || "").localeCompare(String(b.name || "")));
  }

  function renderMemoryOwnershipPill(node) {
    const pill = el("div", "cs-memory-ownership");
    pill.tabIndex = 0;

    const label = el("span", "cs-memory-ownership-label", memT("memory.noCopies", "No copies"));
    pill.appendChild(label);

    const pop = el("div", "cs-memory-ownership-popover");
    pop.appendChild(el("div", "cs-memory-ownership-title", memT("memory.dataOwnership", "Data ownership")));

    const rows = memoryNodeOwnershipRows(node);
    const totalBytes = rows.reduce((sum, row) => sum + Number(row.bytes || 0), 0);

    if (!rows.length) {
      pop.appendChild(el("div", "cs-memory-ownership-empty", memT("memory.noMediaYet", "No media yet")));
    } else {
      for (const row of rows) {
        const item = el("div", "cs-memory-ownership-row");

        const name = el("span", "cs-memory-ownership-name", row.name || "unknown");
        const value = el(
          "span",
          "cs-memory-ownership-value",
          memT("memory.fileCountBytes", { count: row.count, size: formatMemoryBytes(row.bytes) }, `${row.count} file${row.count === 1 ? "" : "s"} · ${formatMemoryBytes(row.bytes)}`)
        );

        item.appendChild(name);
        item.appendChild(value);
        pop.appendChild(item);
      }
    }

    const total = el("div", "cs-memory-ownership-total");
    total.textContent = memT("memory.referencedTotal", { size: formatMemoryBytes(totalBytes) }, `Referenced media total: ${formatMemoryBytes(totalBytes)}`);
    pop.appendChild(total);

    const note = el("div", "cs-memory-ownership-note");
    note.textContent = memT("memory.noDuplicateCopies", "No duplicate copies created. Circle Stack stores references to owners' NAS files.");
    pop.appendChild(note);

    pill.appendChild(pop);
    return pill;
  }

  function renderMemoryNodeCard(node) {
    const card = el("section", "cs-memory-node");

    const head = el("div", "cs-memory-head");

    const titleWrap = el("div", "cs-memory-title-wrap");
    titleWrap.appendChild(el("div", "cs-memory-eyebrow", memT("memory.nodeLabel", "Memory Node")));
    titleWrap.appendChild(el("div", "cs-memory-title", node.title || memT("memory.nodeLabel", "Memory Node")));

    const stats = el("div", "cs-memory-stats");
    updateMemoryStats(stats, node);

    const headRight = el("div", "cs-memory-head-right");
    headRight.appendChild(stats);
    headRight.appendChild(renderMemoryOwnershipPill(node));

    head.appendChild(titleWrap);
    head.appendChild(headRight);
    card.appendChild(head);

    if (node.body) {
      card.appendChild(el("div", "cs-memory-body", node.body));
    }

    const grid = el("div", "cs-memory-grid");

    const items = Array.isArray(node.items) ? node.items : [];
    if (!items.length) {
      grid.appendChild(el("div", "cs-memory-empty", memT("memory.emptyGrid", "No media yet. Be the first to add something.")));
    } else {
      for (const item of items) {
        grid.appendChild(renderItem(item, () => {
          node.items = (Array.isArray(node.items) ? node.items : [])
            .filter(existing => existing.id !== item.id);
          updateMemoryStats(stats, node);
        }));
      }
    }

    card.appendChild(grid);

    const actions = el("div", "cs-memory-actions");
    const add = el("button", "cs-memory-add", memT("memory.addYourMedia", "Add your media"));
    add.type = "button";

    add.addEventListener("click", () => {
      openAddItemModal(node, async (item) => {
        const empty = grid.querySelector(".cs-memory-empty");
        if (empty) empty.remove();

        node.items = Array.isArray(node.items) ? node.items : [];
        node.items.push(item);
        grid.appendChild(renderItem(item, () => {
          node.items = node.items.filter(existing => existing.id !== item.id);
          updateMemoryStats(stats, node);
        }));
        updateMemoryStats(stats, node);
      });
    });

    actions.appendChild(add);
    card.appendChild(actions);

    return card;
  }

  function decoratePost(postEl, post) {
    if (!postEl || !post || !post.memory_node) return;

    postEl.classList.add("cs-post-memory-node");

    if (!postEl.querySelector(".cs-memory-spotlight")) {
      const spotlight = el("div", "cs-memory-spotlight");

      const left = el("div", "cs-memory-spotlight-left");

      const icon = memoryNodeIcon("cs-memory-spotlight-icon");
      const text = el("div", "cs-memory-spotlight-text");

      text.appendChild(el("div", "cs-memory-spotlight-title", memT("memory.collaborativeTitle", "Collaborative Memory Node")));
      text.appendChild(el("div", "cs-memory-spotlight-sub", memT("memory.collaborativeSub", "Friends can add media from their own NAS storage")));

      left.appendChild(icon);
      left.appendChild(text);

      const pill = el(
        "div",
        "cs-memory-spotlight-pill",
        memT("memory.mediaCount", { count: Number(post.memory_node.item_count || 0) }, `${Number(post.memory_node.item_count || 0)} media`)
      );

      spotlight.appendChild(left);
      spotlight.appendChild(pill);

      const header = postEl.querySelector(".cs-post-header");
      if (header && header.nextSibling) {
        postEl.insertBefore(spotlight, header.nextSibling);
      } else if (header) {
        postEl.appendChild(spotlight);
      } else {
        postEl.prepend(spotlight);
      }
    }

    const nodeCard = renderMemoryNodeCard(post.memory_node);
    const before = postEl.querySelector(".cs-reactions") || postEl.querySelector(".cs-replies");
    if (before) {
      postEl.insertBefore(nodeCard, before);
    } else {
      postEl.appendChild(nodeCard);
    }
  }

  function installComposerButton() {
    const postBtn = document.getElementById("csPostButton");
    if (!postBtn || document.getElementById("csOpenMemoryNodeBtn")) return;

    const btn = el("button", "cs-memory-open-btn");
    btn.appendChild(memoryNodeIcon("cs-memory-button-icon"));
    btn.appendChild(el("span", "", memT("memory.nodeLabel", "Memory Node")));
    btn.id = "csOpenMemoryNodeBtn";
    btn.type = "button";
    btn.title = memT("memory.openSharedTitle", "Open a shared Memory Node");

    btn.addEventListener("click", () => {
      if (typeof window.csSetComposeExpanded === "function") {
        window.csSetComposeExpanded(true);
      }
      openCreateNodeModal();
    });

    postBtn.parentNode.insertBefore(btn, postBtn);
  }

  window.CircleStackMemoryNodes = {
    decoratePost,
    openCreateNodeModal,
    openAddItemModal
  };

  document.addEventListener("DOMContentLoaded", installComposerButton);
})();
