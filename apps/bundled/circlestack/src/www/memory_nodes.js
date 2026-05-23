(() => {
  "use strict";

  const API = "/api/v4/circlestack";

  function el(tag, className, text) {
    const node = document.createElement(tag);
    if (className) node.className = className;
    if (text !== undefined && text !== null) node.textContent = text;
    return node;
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
    const kind = /\.(mp4|webm|mov|m4v)$/i.test(path) ? "Video" : "Image";

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
    title.placeholder = "Title, e.g. Saturday match";

    const body = document.createElement("textarea");
    body.className = "cs-memory-textarea";
    body.placeholder = "Tell what this Memory Node is about...";

    const visibility = document.createElement("select");
    visibility.className = "cs-memory-input";
    visibility.innerHTML = `
      <option value="circle">Circle</option>
      <option value="public">Public</option>
      <option value="private">Private</option>
    `;

    const mediaRow = el("div", "cs-memory-media-row");
    const mediaInput = document.createElement("input");
    mediaInput.className = "cs-memory-input";
    mediaInput.placeholder = "Optional first image/video path";

    const browse = el("button", "cs-memory-secondary", "Browse");
    browse.type = "button";

    const pickedPreview = el("div", "cs-memory-picked");
    pickedPreview.hidden = true;

    browse.addEventListener("click", () => pickMedia(mediaInput, pickedPreview));

    const caption = document.createElement("input");
    caption.className = "cs-memory-input";
    caption.placeholder = "Optional caption for first media";

    mediaRow.appendChild(mediaInput);
    mediaRow.appendChild(browse);

    const actions = el("div", "cs-modal-actions");
    const cancel = el("button", "cs-modal-cancel", "Cancel");
    cancel.type = "button";

    const create = el("button", "cs-modal-delete", "Open Memory Node");
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
      create.textContent = "Opening…";

      try {
        await createMemoryNode(payload);
        close();

        if (typeof window.csLoadFeed === "function") {
          await window.csLoadFeed();
        }
      } catch (e) {
        alert(`Memory Node failed: ${e.message || e}`);
      } finally {
        create.disabled = false;
        create.textContent = "Open Memory Node";
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
      "Add to Memory Node",
      "Pick an image or video from your own NAS files. The media stays in your storage."
    );

    const mediaRow = el("div", "cs-memory-media-row");

    const mediaInput = document.createElement("input");
    mediaInput.className = "cs-memory-input";
    mediaInput.placeholder = "Image/video path";

    const browse = el("button", "cs-memory-secondary", "Browse");
    browse.type = "button";

    const pickedPreview = el("div", "cs-memory-picked");
    pickedPreview.hidden = true;

    browse.addEventListener("click", () => pickMedia(mediaInput, pickedPreview));

    const caption = document.createElement("input");
    caption.className = "cs-memory-input";
    caption.placeholder = "Optional caption";

    mediaRow.appendChild(mediaInput);
    mediaRow.appendChild(browse);

    const actions = el("div", "cs-modal-actions");
    const cancel = el("button", "cs-modal-cancel", "Cancel");
    cancel.type = "button";

    const add = el("button", "cs-modal-delete", "Add media");
    add.type = "button";

    cancel.addEventListener("click", close);

    add.addEventListener("click", async () => {
      const media_path = mediaInput.value.trim();
      if (!media_path) {
        mediaInput.focus();
        return;
      }

      add.disabled = true;
      add.textContent = "Adding…";

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
        alert(`Add failed: ${e.message || e}`);
      } finally {
        add.disabled = false;
        add.textContent = "Add media";
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
      frame.appendChild(img);
    }

    tile.appendChild(frame);

    const meta = el("div", "cs-memory-item-meta");

    const owner = el("div", "cs-memory-item-owner", item.owner_display_name || item.owner_fp_short || "unknown");
    meta.appendChild(owner);

    if (item.caption) {
      meta.appendChild(el("div", "cs-memory-caption", item.caption));
    }

    if (item.can_delete) {
      const del = el("button", "cs-memory-delete", "Remove");
      del.type = "button";
      del.addEventListener("click", async () => {
        if (!confirm("Remove this media from Memory Node?")) return;

        try {
          await deleteMemoryItem(item.id);
          tile.remove();
          if (typeof onDeleted === "function") onDeleted(item);
        } catch (e) {
          alert(`Remove failed: ${e.message || e}`);
        }
      });
      meta.appendChild(del);
    }

    tile.appendChild(meta);
    return tile;
  }

  function renderMemoryNodeCard(node) {
    const card = el("section", "cs-memory-node");

    const head = el("div", "cs-memory-head");

    const titleWrap = el("div", "cs-memory-title-wrap");
    titleWrap.appendChild(el("div", "cs-memory-eyebrow", "Memory Node"));
    titleWrap.appendChild(el("div", "cs-memory-title", node.title || "Memory Node"));

    const stats = el(
      "div",
      "cs-memory-stats",
      `${Number(node.item_count || 0)} media · ${Number(node.contributors_count || 0)} contributors`
    );

    head.appendChild(titleWrap);
    head.appendChild(stats);
    card.appendChild(head);

    if (node.body) {
      card.appendChild(el("div", "cs-memory-body", node.body));
    }

    const grid = el("div", "cs-memory-grid");

    const items = Array.isArray(node.items) ? node.items : [];
    if (!items.length) {
      grid.appendChild(el("div", "cs-memory-empty", "No media yet. Be the first to add something."));
    } else {
      for (const item of items) {
        grid.appendChild(renderItem(item, () => {
          const left = Math.max(0, grid.querySelectorAll(".cs-memory-item").length);
          stats.textContent = `${left} media · ${Number(node.contributors_count || 0)} contributors`;
        }));
      }
    }

    card.appendChild(grid);

    const actions = el("div", "cs-memory-actions");
    const add = el("button", "cs-memory-add", "Add your media");
    add.type = "button";

    add.addEventListener("click", () => {
      openAddItemModal(node, async (item) => {
        const empty = grid.querySelector(".cs-memory-empty");
        if (empty) empty.remove();

        grid.appendChild(renderItem(item));
        const count = grid.querySelectorAll(".cs-memory-item").length;
        stats.textContent = `${count} media · ${Number(node.contributors_count || 0)} contributors`;
      });
    });

    actions.appendChild(add);
    card.appendChild(actions);

    return card;
  }

  function decoratePost(postEl, post) {
    if (!postEl || !post || !post.memory_node) return;

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

    const btn = el("button", "cs-memory-open-btn", "🧠 Memory Node");
    btn.id = "csOpenMemoryNodeBtn";
    btn.type = "button";
    btn.title = "Open a shared Memory Node";

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
