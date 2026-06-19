window.PQNAS_FILEMGR = window.PQNAS_FILEMGR || {};

(() => {
  "use strict";

  const FM = window.PQNAS_FILEMGR;

  function tr(key, vars = null, fallback = "") {
    try {
      if (window.PQNAS_I18N && typeof window.PQNAS_I18N.t === "function") {
        return window.PQNAS_I18N.t(key, vars, fallback || key);
      }
    } catch (_) {}
    return fallback || key;
  }

  function installStyle() {
    if (document.getElementById("workspaceMessagesStyle")) return;

    const st = document.createElement("style");
    st.id = "workspaceMessagesStyle";
    st.textContent = `
      #workspaceMessagesFab{
        position:fixed;
        left:18px;
        bottom:52px;
        z-index:9997;
        display:none;
        align-items:center;
        gap:8px;
        min-height:42px;
        padding:10px 14px;
        border-radius:999px;
        border:1px solid rgba(var(--fg-rgb),0.32);
        background:linear-gradient(180deg, rgba(var(--fg-rgb),0.14), rgba(0,0,0,0.34));
        color:var(--fg);
        box-shadow:0 18px 55px rgba(0,0,0,.34), 0 0 20px rgba(var(--fg-rgb),.12);
        cursor:pointer;
        font-weight:950;
      }

      #workspaceMessagesFab:hover{
        border-color:rgba(var(--fg-rgb),0.58);
        background:linear-gradient(180deg, rgba(var(--fg-rgb),0.20), rgba(0,0,0,0.32));
      }

      body.scope-workspace #workspaceMessagesFab{
        display:inline-flex;
      }

      #workspaceMessagesFab .wsMsgIcon{
        font-size:18px;
        line-height:1;
      }

      #workspaceMessagesFab .wsMsgBadge{
        display:none;
        min-width:20px;
        height:20px;
        align-items:center;
        justify-content:center;
        padding:0 6px;
        border-radius:999px;
        background:rgba(var(--warn-rgb),0.95);
        color:#161000;
        font-size:12px;
        line-height:20px;
        font-weight:950;
      }

      #workspaceMessagesFab.hasUnread .wsMsgBadge{
        display:inline-flex;
      }

      #workspaceMessagesDrawer{
        position:fixed;
        left:50%;
        bottom:16px;
        transform:translateX(-50%) translateY(16px);
        z-index:9998;
        width:min(720px, calc(100vw - 24px));
        max-height:min(72vh, 640px);
        display:none;
        flex-direction:column;
        overflow:hidden;
        border:1px solid rgba(var(--fg-rgb),0.24);
        border-radius:22px;
        background:linear-gradient(180deg, var(--fm_surface2, var(--panel2)), var(--fm_surface, var(--panel)));
        box-shadow:0 28px 110px rgba(0,0,0,.62), 0 0 28px rgba(var(--fg-rgb),.10);
      }

      #workspaceMessagesDrawer.show{
        display:flex;
        transform:translateX(-50%) translateY(0);
      }

      .wsMsgHead{
        display:flex;
        align-items:flex-start;
        justify-content:space-between;
        gap:12px;
        padding:14px 16px;
        border-bottom:1px solid var(--border2);
        background:rgba(0,0,0,.16);
      }

      .wsMsgTitle{
        font-weight:950;
        letter-spacing:.2px;
      }

      .wsMsgSub{
        margin-top:3px;
        font-size:12px;
        color:var(--fg-dim);
      }

      .wsMsgList{
        flex:1 1 auto;
        min-height:120px;
        overflow:auto;
        padding:14px;
        display:grid;
        gap:10px;
      }

      .wsMsgEmpty{
        border:1px dashed var(--border2);
        border-radius:16px;
        padding:18px;
        text-align:center;
        color:var(--fg-dim);
      }

      .wsMsgRow{
        display:grid;
        gap:5px;
        border:1px solid rgba(var(--fg-rgb),0.13);
        border-radius:16px;
        background:rgba(255,255,255,.035);
        padding:10px 12px;
        justify-self:start;
        width:min(82%, 620px);
        max-width:100%;
      }

      .wsMsgRow.wsMsgOwn{
        justify-self:end;
        background:rgba(var(--fg-rgb),0.10);
        border-color:rgba(var(--fg-rgb),0.26);
        box-shadow:0 0 0 1px rgba(var(--fg-rgb),0.05) inset;
      }

      .wsMsgRow.wsMsgOwn .wsMsgAuthor{
        color:rgba(var(--fg-rgb),1);
      }

      .wsMsgMeta{
        display:flex;
        align-items:center;
        justify-content:space-between;
        gap:10px;
        font-size:12px;
        color:var(--fg-dim);
      }

      .wsMsgAuthor{
        color:rgba(var(--fg-rgb),.94);
        font-weight:950;
        overflow:hidden;
        text-overflow:ellipsis;
        white-space:nowrap;
      }

      .wsMsgTime{
        flex:0 0 auto;
        font-family:var(--mono);
        opacity:.76;
      }

      .wsMsgBody{
        white-space:pre-wrap;
        overflow-wrap:anywhere;
        line-height:1.42;
        color:rgba(var(--fg-rgb),.96);
        font-size:14px;
      }

      .wsMsgFoot{
        flex:0 0 auto;
        display:grid;
        gap:10px;
        padding:12px 14px 14px;
        border-top:1px solid var(--border2);
        background:rgba(0,0,0,.12);
      }

      .wsMsgInput{
        width:100%;
        min-height:74px;
        max-height:160px;
        resize:vertical;
        padding:10px 12px;
        border-radius:14px;
        border:1px solid var(--border2);
        background:rgba(0,0,0,.22);
        color:var(--fg);
        font:inherit;
        line-height:1.35;
      }

      .wsMsgInput:focus{
        outline:none;
        border-color:rgba(var(--fg-rgb),0.45);
        box-shadow:0 0 0 3px rgba(var(--fg-rgb),0.08);
      }

      .wsMsgDropHint{
        display:none;
        border:1px dashed rgba(var(--fg-rgb),0.32);
        border-radius:14px;
        padding:10px 12px;
        background:rgba(var(--fg-rgb),0.07);
        color:var(--fg-dim);
        font-size:12px;
        font-weight:850;
      }

      #workspaceMessagesDrawer.wsMsgDragOver .wsMsgDropHint{
        display:block;
        border-color:rgba(var(--fg-rgb),0.55);
        background:rgba(var(--fg-rgb),0.12);
        color:rgba(var(--fg-rgb),0.96);
      }

      .wsMsgPendingAttachments,
      .wsMsgAttachments{
        display:grid;
        gap:8px;
      }

      .wsMsgPendingAttachments:empty,
      .wsMsgAttachments:empty{
        display:none;
      }

      .wsMsgAttachmentCard{
        display:grid;
        grid-template-columns:auto minmax(0,1fr) auto;
        gap:10px;
        align-items:center;
        border:1px solid rgba(var(--fg-rgb),0.18);
        border-radius:14px;
        background:rgba(var(--fg-rgb),0.055);
        padding:9px 10px;
      }

      .wsMsgAttachmentCard.clickable{
        cursor:pointer;
      }

      .wsMsgAttachmentCard.clickable:hover{
        border-color:rgba(var(--fg-rgb),0.36);
        background:rgba(var(--fg-rgb),0.09);
      }

      .wsMsgAttachmentIcon{
        font-size:18px;
        line-height:1;
      }

      .wsMsgAttachmentMain{
        min-width:0;
      }

      .wsMsgAttachmentName{
        font-weight:950;
        overflow:hidden;
        text-overflow:ellipsis;
        white-space:nowrap;
      }

      .wsMsgAttachmentPath{
        margin-top:2px;
        font-family:var(--mono);
        font-size:11px;
        color:var(--fg-dim);
        overflow:hidden;
        text-overflow:ellipsis;
        white-space:nowrap;
      }

      .wsMsgAttachmentRemove{
        border:1px solid rgba(var(--fg-rgb),0.16);
        border-radius:999px;
        background:rgba(0,0,0,0.10);
        color:var(--fg);
        cursor:pointer;
        width:26px;
        height:26px;
        line-height:1;
        font-weight:950;
      }

      .tile.wsMsgDragSource{
        cursor:grab;
      }

      .wsMsgActions{
        display:flex;
        align-items:flex-end;
        justify-content:space-between;
        gap:10px;
        flex-wrap:nowrap;
      }

      .wsMsgStatus{
        min-height:18px;
        flex:1 1 auto;
        font-size:12px;
        color:var(--fg-dim);
        overflow-wrap:anywhere;
      }

      .wsMsgSend{
        flex:0 0 auto;
        align-self:flex-end;
      }

      html[data-theme="bright"] #workspaceMessagesDrawer,
      html[data-theme="win_classic"] #workspaceMessagesDrawer{
        background:#f8fafc;
      }

      html[data-theme="bright"] .wsMsgInput,
      html[data-theme="win_classic"] .wsMsgInput{
        background:#fff;
        color:var(--fg);
        border-color:rgba(20,24,32,.18);
      }

      @media (max-width: 560px){
        #workspaceMessagesFab{
          left:12px;
          bottom:40px;
        }

        #workspaceMessagesDrawer{
          width:calc(100vw - 16px);
          bottom:8px;
          max-height:78vh;
        }

        .wsMsgHead,
        .wsMsgFoot{
          padding-left:12px;
          padding-right:12px;
        }

        .wsMsgList{
          padding:12px;
        }

        .wsMsgActions{
          align-items:stretch;
          flex-wrap:wrap;
        }

        .wsMsgSend{
          margin-left:auto;
        }
      }
    `;
    document.head.appendChild(st);
  }

  function currentWorkspaceId() {
    try {
      if (!FM || typeof FM.isWorkspaceScope !== "function" || !FM.isWorkspaceScope()) return "";
      if (typeof FM.getWorkspaceId !== "function") return "";
      return String(FM.getWorkspaceId() || "").trim();
    } catch (_) {
      return "";
    }
  }

  function currentWorkspaceName() {
    try {
      return String(FM.scope && (FM.scope.workspaceName || FM.scope.workspaceId) || "").trim();
    } catch (_) {
      return "";
    }
  }

  async function fetchJson(url, opts = {}) {
    const headers = Object.assign({ "Accept": "application/json" }, opts.headers || {});
    const r = await fetch(url, Object.assign({
      credentials: "include",
      cache: "no-store",
      headers
    }, opts));

    const j = await r.json().catch(() => null);
    if (!r.ok || !j || !j.ok) {
      const msg = (j && (j.message || j.error || j.detail)) || `HTTP ${r.status}`;
      throw new Error(msg);
    }

    return j;
  }

  function formatTime(value) {
    const raw = String(value || "");
    if (!raw) return "";
    try {
      const d = new Date(raw);
      if (!Number.isFinite(d.getTime())) return raw;
      return d.toLocaleString();
    } catch (_) {
      return raw;
    }
  }

  function normalizeAttachmentRef(raw) {
    if (!raw || typeof raw !== "object") return null;

    const path = String(raw.path || "").replace(/^\/+/, "").trim();
    if (!path || path.includes("\\") || path.split("/").some((p) => !p || p === "." || p === "..")) return null;

    const kind = String(raw.kind || raw.itemType || "file") === "dir" ? "dir" : "file";
    const name = String(raw.name || path.split("/").pop() || path).trim();

    const ref = {
      type: "workspace_file",
      workspace_id: currentWorkspaceId(),
      path,
      name,
      kind
    };

    const size = Number(raw.size_bytes || raw.sizeBytes || 0);
    if (Number.isFinite(size) && size >= 0) ref.size_bytes = Math.floor(size);

    return ref;
  }

  function attachmentKey(ref) {
    return `${String(ref.kind || "file")}:${String(ref.path || "")}`;
  }

  function iconForAttachment(ref) {
    return String(ref.kind || "file") === "dir" ? "📁" : "📎";
  }

  function attachmentCard(ref, opts = {}) {
    const card = document.createElement("div");
    card.className = "wsMsgAttachmentCard" + (opts.clickable ? " clickable" : "");

    const icon = document.createElement("div");
    icon.className = "wsMsgAttachmentIcon";
    icon.textContent = iconForAttachment(ref);

    const main = document.createElement("div");
    main.className = "wsMsgAttachmentMain";

    const name = document.createElement("div");
    name.className = "wsMsgAttachmentName";
    name.textContent = String(ref.name || ref.path || "");

    const path = document.createElement("div");
    path.className = "wsMsgAttachmentPath";
    path.textContent = "/" + String(ref.path || "");

    main.appendChild(name);
    main.appendChild(path);

    card.appendChild(icon);
    card.appendChild(main);

    if (opts.remove) {
      const remove = document.createElement("button");
      remove.type = "button";
      remove.className = "wsMsgAttachmentRemove";
      remove.title = tr("filemgr.ws.messages.remove_attachment", null, "Remove attachment");
      remove.textContent = "×";
      remove.addEventListener("click", (ev) => {
        ev.preventDefault();
        ev.stopPropagation();
        opts.remove(ref);
      });
      card.appendChild(remove);
    } else {
      const open = document.createElement("div");
      open.className = "mono";
      open.style.opacity = ".72";
      open.style.fontSize = "11px";
      open.textContent = tr("filemgr.ws.messages.open", null, "Open");
      card.appendChild(open);
    }

    if (opts.clickable) {
      card.addEventListener("click", () => openAttachmentRef(ref));
    }

    return card;
  }

  function renderPendingAttachments() {
    ensureUi();
    els.pending.innerHTML = "";

    for (const ref of pendingAttachments) {
      els.pending.appendChild(attachmentCard(ref, {
        remove(removeRef) {
          const key = attachmentKey(removeRef);
          pendingAttachments = pendingAttachments.filter((x) => attachmentKey(x) !== key);
          renderPendingAttachments();
        }
      }));
    }
  }

  function addPendingAttachment(raw) {
    const ref = normalizeAttachmentRef(raw);
    if (!ref) return false;

    const key = attachmentKey(ref);
    if (!pendingAttachments.some((x) => attachmentKey(x) === key)) {
      pendingAttachments.push(ref);
      renderPendingAttachments();
    }

    return true;
  }

  function fileRefFromTile(tile) {
    if (!tile) return null;

    const path = String(tile.dataset.relPath || "").trim();
    if (!path) return null;

    return normalizeAttachmentRef({
      path,
      name: tile.dataset.name || "",
      kind: tile.dataset.itemType || "file"
    });
  }

  function enhanceFileTilesForMessageDrag() {
    if (!FM || typeof FM.isWorkspaceScope !== "function" || !FM.isWorkspaceScope()) return;

    for (const tile of Array.from(document.querySelectorAll(".tile[data-rel-path]"))) {
      if (tile.dataset.wsMsgDragReady === "1") continue;
      if (!fileRefFromTile(tile)) continue;

      tile.draggable = true;
      tile.dataset.wsMsgDragReady = "1";
      tile.classList.add("wsMsgDragSource");

      tile.addEventListener("dragstart", (ev) => {
        const ref = fileRefFromTile(tile);
        if (!ref || !ev.dataTransfer) return;

        ev.dataTransfer.effectAllowed = "copy";
        const payload = JSON.stringify(ref);
        ev.dataTransfer.setData(WS_MSG_FILE_REF_MIME, payload);
        ev.dataTransfer.setData("text/plain", `📎 ${ref.name}\n/${ref.path}`);
      });
    }
  }

  function fileRefFromDataTransfer(dt) {
    if (!dt) return null;

    try {
      const raw = dt.getData(WS_MSG_FILE_REF_MIME);
      if (raw) return normalizeAttachmentRef(JSON.parse(raw));
    } catch (_) {}

    return null;
  }

  function isWorkspaceFileRefDrag(ev) {
    try {
      const types = ev.dataTransfer && ev.dataTransfer.types
        ? Array.from(ev.dataTransfer.types)
        : [];
      return types.includes(WS_MSG_FILE_REF_MIME);
    } catch (_) {
      return false;
    }
  }

  function installAttachmentDropHandlers() {
    ensureUi();

    const onDragOver = (ev) => {
      if (!isWorkspaceFileRefDrag(ev)) return;
      ev.preventDefault();
      ev.dataTransfer.dropEffect = "copy";
      els.drawer.classList.add("wsMsgDragOver");
    };

    const onDragLeave = (ev) => {
      if (els.drawer.contains(ev.relatedTarget)) return;
      els.drawer.classList.remove("wsMsgDragOver");
    };

    const onDrop = (ev) => {
      if (!isWorkspaceFileRefDrag(ev)) return;
      ev.preventDefault();
      els.drawer.classList.remove("wsMsgDragOver");

      const ref = fileRefFromDataTransfer(ev.dataTransfer);
      if (addPendingAttachment(ref)) {
        setDrawerOpen(true).catch(() => {});
        setStatus(tr("filemgr.ws.messages.file_attached", null, "File reference attached."));
        setTimeout(() => {
          if (els && els.status.textContent === tr("filemgr.ws.messages.file_attached", null, "File reference attached.")) {
            setStatus("");
          }
        }, 1200);
      }
    };

    els.drawer.addEventListener("dragover", onDragOver);
    els.drawer.addEventListener("dragleave", onDragLeave);
    els.drawer.addEventListener("drop", onDrop);
  }

  function openAttachmentRef(ref) {
    const path = String(ref && ref.path || "").replace(/^\/+/, "");
    if (!path) return;

    const kind = String(ref.kind || "file");
    let targetPath = path;

    if (kind !== "dir") {
      const slash = path.lastIndexOf("/");
      targetPath = slash >= 0 ? path.slice(0, slash) : "";
    }

    try {
      if (FM && typeof FM.setPathAndLoad === "function") {
        FM.setPathAndLoad(targetPath);
        setDrawerOpen(false).catch(() => {});
        return;
      }
    } catch (_) {}

    setStatus("/" + path);
  }

  let els = null;
  let activeWorkspaceId = "";
  let drawerOpen = false;
  let messages = [];
  let latestId = 0;
  let unreadCount = 0;
  let selfFp = "";
  let pendingAttachments = [];
  const WS_MSG_FILE_REF_MIME = "application/x-pqnas-workspace-file-ref";
  let refreshBusy = false;
  let sendBusy = false;

  function ensureUi() {
    if (els) return els;

    installStyle();

    const fab = document.createElement("button");
    fab.id = "workspaceMessagesFab";
    fab.type = "button";
    fab.title = tr("filemgr.ws.messages.open", null, "Workspace messages");
    fab.innerHTML = `
      <span class="wsMsgIcon" aria-hidden="true">💬</span>
      <span class="wsMsgText">${tr("filemgr.ws.messages.button", null, "Messages")}</span>
      <span class="wsMsgBadge" aria-label="${tr("filemgr.ws.messages.unread", null, "Unread messages")}">!</span>
    `;

    const drawer = document.createElement("div");
    drawer.id = "workspaceMessagesDrawer";
    drawer.setAttribute("role", "dialog");
    drawer.setAttribute("aria-modal", "false");
    drawer.setAttribute("aria-hidden", "true");

    drawer.innerHTML = `
      <div class="wsMsgHead">
        <div>
          <div class="wsMsgTitle">${tr("filemgr.ws.messages.title", null, "Workspace messages")}</div>
          <div class="wsMsgSub"></div>
        </div>
        <button type="button" class="btn secondary wsMsgClose">${tr("filemgr.close", null, "Close")}</button>
      </div>
      <div class="wsMsgList" aria-live="polite"></div>
      <div class="wsMsgFoot">
        <div class="wsMsgDropHint">${tr("filemgr.ws.messages.drop_file_hint", null, "Drop a workspace file here to attach a reference.")}</div>
        <textarea class="wsMsgInput"
                  maxlength="4000"
                  placeholder="${tr("filemgr.ws.messages.placeholder", null, "Write a message for workspace members…")}"></textarea>
        <div class="wsMsgPendingAttachments"></div>
        <div class="wsMsgActions">
          <div class="wsMsgStatus mono"></div>
          <button type="button" class="btn wsMsgSend">${tr("filemgr.ws.messages.send", null, "Send")}</button>
        </div>
      </div>
    `;

    document.body.appendChild(fab);
    document.body.appendChild(drawer);

    els = {
      fab,
      badge: fab.querySelector(".wsMsgBadge"),
      drawer,
      sub: drawer.querySelector(".wsMsgSub"),
      close: drawer.querySelector(".wsMsgClose"),
      list: drawer.querySelector(".wsMsgList"),
      input: drawer.querySelector(".wsMsgInput"),
      pending: drawer.querySelector(".wsMsgPendingAttachments"),
      send: drawer.querySelector(".wsMsgSend"),
      status: drawer.querySelector(".wsMsgStatus"),
    };

    fab.addEventListener("click", async () => {
      await setDrawerOpen(!drawerOpen);
    });

    els.close.addEventListener("click", async () => {
      await setDrawerOpen(false);
    });

    els.send.addEventListener("click", sendMessage);

    els.input.addEventListener("keydown", (ev) => {
      if (ev.key === "Enter" && (ev.ctrlKey || ev.metaKey)) {
        ev.preventDefault();
        sendMessage();
      }
    });

    installAttachmentDropHandlers();

    return els;
  }

  function setStatus(text, isError = false) {
    ensureUi();
    els.status.textContent = text || "";
    els.status.style.color = isError ? "rgb(var(--fail-rgb))" : "";
  }

  function updateVisibility() {
    ensureUi();

    const ws = currentWorkspaceId();
    const show = !!ws;

    els.fab.style.display = show ? "inline-flex" : "none";

    if (!show) {
      drawerOpen = false;
      els.drawer.classList.remove("show");
      els.drawer.setAttribute("aria-hidden", "true");
      els.fab.classList.remove("hasUnread");
      els.badge.textContent = "!";
      activeWorkspaceId = "";
      messages = [];
      latestId = 0;
      unreadCount = 0;
      selfFp = "";
      pendingAttachments = [];
      if (els) renderPendingAttachments();
    }
  }

  function updateBadge() {
    ensureUi();

    const hasUnread = unreadCount > 0;
    els.fab.classList.toggle("hasUnread", hasUnread);
    els.badge.textContent = unreadCount > 9 ? "9+" : (unreadCount > 0 ? String(unreadCount) : "!");
  }

  function renderMessages() {
    ensureUi();

    const name = currentWorkspaceName();
    els.sub.textContent = name
      ? tr("filemgr.ws.messages.sub_named", { name }, `Visible to members of: ${name}`)
      : tr("filemgr.ws.messages.sub", null, "Visible to all workspace members");

    els.list.innerHTML = "";

    if (!messages.length) {
      const empty = document.createElement("div");
      empty.className = "wsMsgEmpty";
      empty.textContent = tr("filemgr.ws.messages.empty", null, "No workspace messages yet.");
      els.list.appendChild(empty);
      return;
    }

    for (const msg of messages) {
      const row = document.createElement("div");
      const own = !!selfFp && String(msg.author_fp || "") === selfFp;
      row.className = own ? "wsMsgRow wsMsgOwn" : "wsMsgRow";

      const meta = document.createElement("div");
      meta.className = "wsMsgMeta";

      const author = document.createElement("div");
      author.className = "wsMsgAuthor";
      author.textContent = String(msg.author_name || msg.author_fp || tr("filemgr.ws.member", null, "Member"));

      const time = document.createElement("div");
      time.className = "wsMsgTime";
      time.textContent = formatTime(msg.created_at);

      meta.appendChild(author);
      meta.appendChild(time);

      const body = document.createElement("div");
      body.className = "wsMsgBody";
      body.textContent = String(msg.body || "");

      row.appendChild(meta);
      row.appendChild(body);
      els.list.appendChild(row);
      const atts = Array.isArray(msg.attachments) ? msg.attachments : [];
      if (atts.length) {
        const wrap = document.createElement("div");
        wrap.className = "wsMsgAttachments";
        for (const rawRef of atts) {
          const ref = normalizeAttachmentRef(rawRef);
          if (ref) {
            wrap.appendChild(attachmentCard(ref, { clickable: true }));
          }
        }
        if (wrap.childNodes.length) row.appendChild(wrap);
      }

    }

    els.list.scrollTop = els.list.scrollHeight;
  }

  async function markRead() {
    const ws = currentWorkspaceId();
    if (!ws || !latestId) return;

    await fetchJson("/api/v4/workspaces/messages/read", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        workspace_id: ws,
        last_seen_id: latestId
      })
    });

    unreadCount = 0;
    updateBadge();
  }

  async function refreshMessages(opts = {}) {
    if (refreshBusy) return;
    const ws = currentWorkspaceId();
    if (!ws) {
      updateVisibility();
      return;
    }

    refreshBusy = true;
    try {
      const qs = new URLSearchParams();
      qs.set("workspace_id", ws);
      qs.set("limit", "100");

      const j = await fetchJson(`/api/v4/workspaces/messages?${qs.toString()}`);
      activeWorkspaceId = ws;
      selfFp = String(j.actor_fp || "");
      messages = Array.isArray(j.messages) ? j.messages : [];
      latestId = Number(j.latest_id || 0);
      unreadCount = Number(j.unread_count || 0);

      renderMessages();

      if (drawerOpen || opts.markRead) {
        await markRead();
      } else {
        updateBadge();
      }

      setStatus("");
    } catch (e) {
      setStatus(
        tr("filemgr.ws.messages.load_failed", { error: String(e && e.message ? e.message : e) }, `Load failed: ${String(e && e.message ? e.message : e)}`),
        true
      );
    } finally {
      refreshBusy = false;
    }
  }

  async function setDrawerOpen(open) {
    ensureUi();

    const ws = currentWorkspaceId();
    if (!ws) return;

    drawerOpen = !!open;
    els.drawer.classList.toggle("show", drawerOpen);
    els.drawer.setAttribute("aria-hidden", drawerOpen ? "false" : "true");

    if (drawerOpen) {
      await refreshMessages({ markRead: true });
      setTimeout(() => els.input.focus(), 30);
    }
  }

  async function sendMessage() {
    ensureUi();
    if (sendBusy) return;

    const ws = currentWorkspaceId();
    if (!ws) return;

    const text = String(els.input.value || "").trim();
    if (!text && !pendingAttachments.length) {
      els.input.focus();
      return;
    }

    sendBusy = true;
    els.send.disabled = true;
    setStatus(tr("filemgr.ws.messages.sending", null, "Sending…"));

    try {
      await fetchJson("/api/v4/workspaces/messages/post", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          workspace_id: ws,
          body: text,
          attachments: pendingAttachments
        })
      });

      els.input.value = "";
      pendingAttachments = [];
      renderPendingAttachments();
      await refreshMessages({ markRead: true });
      setStatus(tr("filemgr.ws.messages.sent", null, "Sent."));
      setTimeout(() => {
        if (els && els.status.textContent === tr("filemgr.ws.messages.sent", null, "Sent.")) {
          setStatus("");
        }
      }, 1200);
    } catch (e) {
      setStatus(
        tr("filemgr.ws.messages.send_failed", { error: String(e && e.message ? e.message : e) }, `Send failed: ${String(e && e.message ? e.message : e)}`),
        true
      );
    } finally {
      sendBusy = false;
      els.send.disabled = false;
      els.input.focus();
    }
  }

  async function tick() {
    ensureUi();

    const ws = currentWorkspaceId();
    updateVisibility();

    if (!ws) return;

    if (ws !== activeWorkspaceId) {
      drawerOpen = false;
      els.drawer.classList.remove("show");
      els.drawer.setAttribute("aria-hidden", "true");
      messages = [];
      latestId = 0;
      unreadCount = 0;
      renderMessages();
      updateBadge();
      await refreshMessages();
      return;
    }

    await refreshMessages({ markRead: drawerOpen });
  }

  function init() {
    ensureUi();
    updateVisibility();

    setTimeout(() => tick(), 300);
    setInterval(() => tick(), 12000);

    setTimeout(enhanceFileTilesForMessageDrag, 700);
    setInterval(enhanceFileTilesForMessageDrag, 1500);

    try {
      const grid = document.getElementById("grid");
      if (grid && window.MutationObserver) {
        const mo = new MutationObserver(() => enhanceFileTilesForMessageDrag());
        mo.observe(grid, { childList: true, subtree: true });
      }
    } catch (_) {}

    window.addEventListener("pqnas-language-changed", () => {
      if (!els) return;
      els.fab.querySelector(".wsMsgText").textContent = tr("filemgr.ws.messages.button", null, "Messages");
      els.drawer.querySelector(".wsMsgTitle").textContent = tr("filemgr.ws.messages.title", null, "Workspace messages");
      els.close.textContent = tr("filemgr.close", null, "Close");
      els.send.textContent = tr("filemgr.ws.messages.send", null, "Send");
      els.input.placeholder = tr("filemgr.ws.messages.placeholder", null, "Write a message for workspace members…");
      const hint = els.drawer.querySelector(".wsMsgDropHint");
      if (hint) hint.textContent = tr("filemgr.ws.messages.drop_file_hint", null, "Drop a workspace file here to attach a reference.");
      renderPendingAttachments();
      renderMessages();
    });
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init, { once: true });
  } else {
    init();
  }
})();
