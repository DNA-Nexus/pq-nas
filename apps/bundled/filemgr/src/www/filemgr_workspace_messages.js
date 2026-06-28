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
      #workspaceMessagesFab,
      #workspaceMessagesDrawer,
      .wsMsgConfirmBackdrop{
        --wsmsg-fg-rgb: var(--fg-rgb);

        /* Opaque component surfaces derived from solid theme tokens.
           Do not use --panel / --panel2 here: some themes intentionally
           define them as translucent glass layers. */
        --wsmsg-surface: color-mix(in srgb, var(--bg) 92%, var(--fg) 8%);
        --wsmsg-surface-2: color-mix(in srgb, var(--bg) 86%, var(--fg) 14%);
        --wsmsg-surface-3: color-mix(in srgb, var(--bg) 80%, var(--fg) 20%);

        --wsmsg-border: var(--border2);
        --wsmsg-shadow: color-mix(in srgb, var(--bg) 76%, transparent);
        --wsmsg-overlay: color-mix(in srgb, var(--bg) 72%, transparent);

        --wsmsg-soft-layer: color-mix(in srgb, var(--wsmsg-surface) 90%, var(--fg) 10%);
        --wsmsg-strong-layer: color-mix(in srgb, var(--wsmsg-surface-2) 88%, var(--fg) 12%);
        --wsmsg-input-bg: color-mix(in srgb, var(--wsmsg-surface) 94%, var(--bg) 6%);
        --wsmsg-card-bg: color-mix(in srgb, var(--wsmsg-surface) 88%, var(--fg) 12%);
        --wsmsg-card-bg-hover: color-mix(in srgb, var(--wsmsg-surface) 82%, var(--fg) 18%);
        --wsmsg-danger-border: rgba(var(--fail-rgb),0.45);
      }

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
        background:linear-gradient(180deg, rgba(var(--fg-rgb),0.14), var(--wsmsg-overlay));
        color:var(--fg);
        box-shadow:0 18px 55px var(--wsmsg-overlay), 0 0 20px rgba(var(--fg-rgb),.12);
        cursor:pointer;
        font-weight:950;
      }

      #workspaceMessagesFab:hover{
        border-color:rgba(var(--fg-rgb),0.58);
        background:linear-gradient(180deg, rgba(var(--fg-rgb),0.20), var(--wsmsg-overlay));
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
        color:var(--bg);
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
        border:1px solid rgba(var(--fg-rgb),0.30);
        border-radius:22px;
        background:var(--wsmsg-surface);
        background:linear-gradient(180deg, var(--wsmsg-surface-2), var(--wsmsg-surface));
        background-clip:padding-box;
        isolation:isolate;
        box-shadow:0 28px 110px var(--wsmsg-overlay), 0 0 28px rgba(var(--fg-rgb),.16);
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
        background:var(--wsmsg-strong-layer);
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
        background:var(--wsmsg-surface);
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
        background:rgba(var(--wsmsg-fg-rgb),.035);
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

      .wsMsgMetaRight{
        display:flex;
        align-items:center;
        justify-content:flex-end;
        gap:6px;
        flex:0 0 auto;
      }

      .wsMsgMiniBtn{
        border:1px solid rgba(var(--fg-rgb),0.18);
        border-radius:999px;
        background:var(--wsmsg-soft-layer);
        color:var(--fg);
        cursor:pointer;
        min-width:26px;
        height:24px;
        padding:0 8px;
        font-size:11px;
        line-height:1;
        font-weight:950;
      }

      .wsMsgMiniBtn:hover{
        border-color:rgba(var(--fg-rgb),0.36);
        background:rgba(var(--fg-rgb),0.10);
      }

      .wsMsgConfirmBackdrop{
        position:fixed;
        inset:0;
        z-index:10050;
        display:none;
        align-items:center;
        justify-content:center;
        padding:18px;
        background:var(--wsmsg-overlay);
        backdrop-filter:blur(4px);
      }

      .wsMsgConfirmBackdrop.show{
        display:flex;
      }

      .wsMsgConfirmBox{
        width:min(460px, calc(100vw - 32px));
        border:1px solid rgba(var(--fg-rgb),0.30);
        border-radius:22px;
        background:var(--wsmsg-surface);
        background:linear-gradient(180deg, var(--wsmsg-surface-2), var(--wsmsg-surface));
        color:var(--fg);
        box-shadow:0 30px 110px var(--wsmsg-overlay), 0 0 28px rgba(var(--fg-rgb),.16);
        overflow:hidden;
      }

      .wsMsgConfirmHead{
        padding:16px 18px 10px;
        border-bottom:1px solid rgba(var(--fg-rgb),0.12);
      }

      .wsMsgConfirmTitle{
        font-weight:950;
        font-size:17px;
        letter-spacing:.2px;
      }

      .wsMsgConfirmBody{
        display:grid;
        gap:12px;
        padding:14px 18px 16px;
      }

      .wsMsgConfirmMessage{
        color:var(--fg);
        line-height:1.42;
      }

      .wsMsgConfirmTarget{
        display:grid;
        gap:4px;
        border:1px solid rgba(var(--fg-rgb),0.14);
        border-radius:14px;
        padding:10px 12px;
        background:rgba(var(--fg-rgb),0.06);
      }

      .wsMsgConfirmTargetLabel{
        font-size:11px;
        text-transform:uppercase;
        letter-spacing:.06em;
        color:var(--fg-dim);
        font-weight:850;
      }

      .wsMsgConfirmTargetValue{
        font-weight:950;
        overflow-wrap:anywhere;
      }

      .wsMsgConfirmActions{
        display:flex;
        justify-content:flex-end;
        gap:10px;
        padding:12px 18px 16px;
        border-top:1px solid rgba(var(--fg-rgb),0.12);
      }

      .wsMsgConfirmOk.wsMsgConfirmOk.wsMsgConfirmDanger{
        border-color:var(--wsmsg-danger-border);
      }

      .wsMsgDeleteBtn{
        color:rgba(var(--fg-rgb),0.92);
      }

      .wsMsgMuteBtn.isMuted{
        border-color:rgba(var(--warn-rgb),0.58);
        background:rgba(var(--warn-rgb),0.16);
      }

      .wsMsgHeadActions{
        display:flex;
        align-items:center;
        gap:8px;
        flex:0 0 auto;
      }

      .wsMsgMuteAll{
        display:none;
      }

      #workspaceMessagesDrawer.canModerate .wsMsgMuteAll{
        display:inline-flex;
      }

      .wsMsgMutedBanner{
        border:1px solid rgba(var(--warn-rgb),0.36);
        border-radius:14px;
        padding:9px 11px;
        background:rgba(var(--warn-rgb),0.12);
        color:var(--fg);
        font-size:12px;
        font-weight:850;
      }

      .wsMsgMutedBanner.hidden{
        display:none;
      }

      .wsMsgBody{
        white-space:pre-wrap;
        overflow-wrap:anywhere;
        line-height:1.42;
        color:rgba(var(--fg-rgb),.96);
        font-size:14px;
      }

      .wsMsgContactWrap{
        display:grid;
        gap:8px;
      }

      .wsMsgContactText{
        white-space:pre-wrap;
        overflow-wrap:anywhere;
        line-height:1.42;
        color:rgba(var(--fg-rgb),.96);
        font-size:14px;
      }

      .wsMsgContactCard{
        display:grid;
        gap:9px;
        border:1px solid rgba(var(--fg-rgb),0.22);
        border-radius:16px;
        background:
          linear-gradient(180deg, rgba(var(--fg-rgb),0.10), transparent),
          var(--wsmsg-card-bg);
        padding:11px 12px;
      }

      .wsMsgContactTop{
        display:flex;
        align-items:flex-start;
        justify-content:space-between;
        gap:10px;
      }

      .wsMsgContactName{
        min-width:0;
        font-weight:950;
        overflow-wrap:anywhere;
      }

      .wsMsgContactBadge{
        flex:0 0 auto;
        border:1px solid rgba(var(--fg-rgb),0.22);
        border-radius:999px;
        padding:3px 8px;
        background:rgba(var(--fg-rgb),0.08);
        font-size:11px;
        font-weight:950;
      }

      .wsMsgContactMeta,
      .wsMsgContactLine{
        color:var(--fg-dim);
        font-size:12px;
        line-height:1.42;
        overflow-wrap:anywhere;
      }

      .wsMsgContactLine strong{
        color:var(--fg);
      }

      .wsMsgContactActions{
        display:flex;
        flex-wrap:wrap;
        gap:8px;
        margin-top:2px;
      }

      .wsMsgContactAction{
        border:1px solid rgba(var(--fg-rgb),0.18);
        border-radius:999px;
        background:var(--wsmsg-soft-layer);
        color:var(--fg);
        cursor:pointer;
        min-height:26px;
        padding:0 9px;
        font-size:11px;
        font-weight:950;
      }

      .wsMsgContactAction:hover{
        border-color:rgba(var(--fg-rgb),0.36);
        background:rgba(var(--fg-rgb),0.10);
      }

      .wsMsgFoot{
        flex:0 0 auto;
        display:grid;
        gap:10px;
        padding:12px 14px 14px;
        border-top:1px solid var(--border2);
        background:var(--wsmsg-strong-layer);
      }

      .wsMsgInput{
        width:100%;
        min-height:74px;
        max-height:160px;
        resize:vertical;
        padding:10px 12px;
        border-radius:14px;
        border:1px solid var(--border2);
        background:var(--wsmsg-input-bg);
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
        background:var(--wsmsg-surface);
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
        background:var(--wsmsg-surface);
        background:linear-gradient(180deg, var(--wsmsg-surface-2), var(--wsmsg-surface));
      }

      html[data-theme="bright"] .wsMsgHead,
      html[data-theme="win_classic"] .wsMsgHead{
        background:var(--wsmsg-surface);
      }

      html[data-theme="bright"] .wsMsgList,
      html[data-theme="win_classic"] .wsMsgList{
        background:rgba(248,250,252,0.96);
      }

      html[data-theme="bright"] .wsMsgFoot,
      html[data-theme="win_classic"] .wsMsgFoot{
        background:var(--wsmsg-surface);
      }

      html[data-theme="bright"] .wsMsgConfirmBox,
      html[data-theme="win_classic"] .wsMsgConfirmBox{
        background:var(--wsmsg-surface);
        background:linear-gradient(180deg, var(--wsmsg-surface-2), var(--wsmsg-surface));
        color:var(--fg);
      }

      html[data-theme="bright"] .wsMsgInput,
      html[data-theme="win_classic"] .wsMsgInput{
        background:var(--wsmsg-surface);
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

  function ensureConfirmModal() {
    installStyle();

    let backdrop = document.getElementById("wsMsgConfirmBackdrop");
    if (backdrop) return backdrop;

    backdrop = document.createElement("div");
    backdrop.id = "wsMsgConfirmBackdrop";
    backdrop.className = "wsMsgConfirmBackdrop";
    backdrop.setAttribute("aria-hidden", "true");
    backdrop.innerHTML = `
      <div class="wsMsgConfirmBox" role="dialog" aria-modal="true">
        <div class="wsMsgConfirmHead">
          <div class="wsMsgConfirmTitle"></div>
        </div>
        <div class="wsMsgConfirmBody">
          <div class="wsMsgConfirmMessage"></div>
          <div class="wsMsgConfirmTarget">
            <div class="wsMsgConfirmTargetLabel"></div>
            <div class="wsMsgConfirmTargetValue"></div>
          </div>
        </div>
        <div class="wsMsgConfirmActions">
          <button type="button" class="btn secondary wsMsgConfirmCancel"></button>
          <button type="button" class="btn wsMsgConfirmOk"></button>
        </div>
      </div>
    `;

    document.body.appendChild(backdrop);
    return backdrop;
  }

  function wsMsgConfirm(opts = {}) {
    const backdrop = ensureConfirmModal();
    const box = backdrop.querySelector(".wsMsgConfirmBox");
    const title = backdrop.querySelector(".wsMsgConfirmTitle");
    const msg = backdrop.querySelector(".wsMsgConfirmMessage");
    const target = backdrop.querySelector(".wsMsgConfirmTarget");
    const targetLabel = backdrop.querySelector(".wsMsgConfirmTargetLabel");
    const targetValue = backdrop.querySelector(".wsMsgConfirmTargetValue");
    const cancel = backdrop.querySelector(".wsMsgConfirmCancel");
    const ok = backdrop.querySelector(".wsMsgConfirmOk");

    title.textContent = opts.title || tr("filemgr.ws.messages.confirm_title", null, "Confirm action");
    msg.textContent = opts.message || "";
    targetLabel.textContent = opts.targetLabel || tr("filemgr.ws.messages.target", null, "Target");
    targetValue.textContent = opts.target || "";
    target.style.display = opts.target ? "grid" : "none";
    cancel.textContent = opts.cancelText || tr("filemgr.cancel", null, "Cancel");
    ok.textContent = opts.confirmText || tr("filemgr.ok", null, "OK");
    ok.classList.toggle("wsMsgConfirmDanger", !!opts.danger);

    backdrop.classList.add("show");
    backdrop.setAttribute("aria-hidden", "false");

    return new Promise((resolve) => {
      let done = false;

      const finish = (value) => {
        if (done) return;
        done = true;

        backdrop.classList.remove("show");
        backdrop.setAttribute("aria-hidden", "true");

        ok.removeEventListener("click", onOk);
        cancel.removeEventListener("click", onCancel);
        backdrop.removeEventListener("click", onBackdrop);
        document.removeEventListener("keydown", onKey);

        resolve(value);
      };

      const onOk = () => finish(true);
      const onCancel = () => finish(false);
      const onBackdrop = (ev) => {
        if (ev.target === backdrop) finish(false);
      };
      const onKey = (ev) => {
        if (ev.key === "Escape") {
          ev.preventDefault();
          finish(false);
        }
        if (ev.key === "Enter") {
          ev.preventDefault();
          finish(true);
        }
      };

      ok.addEventListener("click", onOk);
      cancel.addEventListener("click", onCancel);
      backdrop.addEventListener("click", onBackdrop);
      document.addEventListener("keydown", onKey);

      setTimeout(() => ok.focus(), 20);
    });
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

  function parseContactCardText(text) {
    const raw = String(text || "");
    const start = raw.indexOf("[DNA-NEXUS-CONTACT]");
    const end = raw.indexOf("[/DNA-NEXUS-CONTACT]");
    if (start < 0 || end < 0 || end <= start) return null;

    const before = raw.slice(0, start).trim();
    const body = raw.slice(start + "[DNA-NEXUS-CONTACT]".length, end).trim();
    const after = raw.slice(end + "[/DNA-NEXUS-CONTACT]".length).trim();

    const card = {};
    for (const line of body.split(/\r?\n/)) {
      const idx = line.indexOf(":");
      if (idx <= 0) continue;

      const key = line.slice(0, idx).trim().toLowerCase();
      const value = line.slice(idx + 1).trim();
      if (!key || !value) continue;

      if (key === "name") card.name = value;
      else if (key === "company") card.company = value;
      else if (key === "title") card.title = value;
      else if (key === "email") card.email = value;
      else if (key === "phone") card.phone = value;
      else if (key === "mobile") card.mobile = value;
      else if (key === "website") card.website = value;
      else if (key === "address") card.address = value;
      else if (key === "tags") card.tags = value;
      else if (key === "identity") card.identity = value;
    }

    if (!card.name && !card.company && !card.email && !card.phone && !card.mobile) return null;

    return { before, after, card };
  }

  function formatContactCardForClipboard(card) {
    const c = card || {};
    const lines = [
      "[DNA-NEXUS-CONTACT]",
      `Name: ${c.name || ""}`,
      `Company: ${c.company || ""}`,
      `Title: ${c.title || ""}`,
      `Email: ${c.email || ""}`,
      `Phone: ${c.phone || ""}`,
      `Mobile: ${c.mobile || ""}`,
      `Website: ${c.website || ""}`,
      `Address: ${c.address || ""}`,
      `Tags: ${c.tags || ""}`,
      `Identity: ${c.identity || ""}`,
      "[/DNA-NEXUS-CONTACT]"
    ];
    return lines.filter((line) => !line.endsWith(": ")).join("\n");
  }

  function copyWorkspaceText(value, okText) {
    const text = String(value || "").trim();
    if (!text) {
      setStatus(tr("filemgr.ws.messages.nothing_to_copy", null, "Nothing to copy."));
      return;
    }

    const done = () => {
      setStatus(okText || tr("filemgr.ws.messages.copied", null, "Copied."));
      setTimeout(() => {
        if (els && els.status.textContent === (okText || tr("filemgr.ws.messages.copied", null, "Copied."))) {
          setStatus("");
        }
      }, 1200);
    };

    if (navigator.clipboard && typeof navigator.clipboard.writeText === "function") {
      navigator.clipboard.writeText(text).then(done).catch(() => {
        fallbackCopyWorkspaceText(text, done);
      });
      return;
    }

    fallbackCopyWorkspaceText(text, done);
  }

  function fallbackCopyWorkspaceText(text, done) {
    const area = document.createElement("textarea");
    area.value = text;
    area.setAttribute("readonly", "");
    area.style.position = "fixed";
    area.style.left = "-1000px";
    area.style.top = "-1000px";
    document.body.appendChild(area);
    area.select();

    try {
      document.execCommand("copy");
      done();
    } catch (_) {
      setStatus(tr("filemgr.ws.messages.copy_failed", null, "Copy failed."), true);
    } finally {
      area.remove();
    }
  }

  function appendContactLine(parent, label, value) {
    const v = String(value || "").trim();
    if (!v) return;

    const line = document.createElement("div");
    line.className = "wsMsgContactLine";

    const strong = document.createElement("strong");
    strong.textContent = label + ": ";

    const span = document.createElement("span");
    span.textContent = v;

    line.appendChild(strong);
    line.appendChild(span);
    parent.appendChild(line);
  }

  function contactCardNode(parsed) {
    const wrap = document.createElement("div");
    wrap.className = "wsMsgContactWrap";

    if (parsed.before) {
      const before = document.createElement("div");
      before.className = "wsMsgContactText";
      before.textContent = parsed.before;
      wrap.appendChild(before);
    }

    const card = parsed.card || {};
    const box = document.createElement("div");
    box.className = "wsMsgContactCard";

    const top = document.createElement("div");
    top.className = "wsMsgContactTop";

    const name = document.createElement("div");
    name.className = "wsMsgContactName";
    name.textContent = card.name || card.company || card.email || tr("filemgr.ws.messages.contact_card", null, "Contact card");

    const badge = document.createElement("div");
    badge.className = "wsMsgContactBadge";
    badge.textContent = tr("filemgr.ws.messages.contact", null, "Contact");

    top.appendChild(name);
    top.appendChild(badge);
    box.appendChild(top);

    const metaParts = [card.company, card.title].filter(Boolean);
    if (metaParts.length) {
      const meta = document.createElement("div");
      meta.className = "wsMsgContactMeta";
      meta.textContent = metaParts.join(" • ");
      box.appendChild(meta);
    }

    appendContactLine(box, tr("filemgr.ws.messages.email", null, "Email"), card.email);
    appendContactLine(box, tr("filemgr.ws.messages.phone", null, "Phone"), card.phone);
    appendContactLine(box, tr("filemgr.ws.messages.mobile", null, "Mobile"), card.mobile);
    appendContactLine(box, tr("filemgr.ws.messages.website", null, "Website"), card.website);
    appendContactLine(box, tr("filemgr.ws.messages.address", null, "Address"), card.address);
    appendContactLine(box, tr("filemgr.ws.messages.tags", null, "Tags"), card.tags);

    const actions = document.createElement("div");
    actions.className = "wsMsgContactActions";

    const copyCard = document.createElement("button");
    copyCard.type = "button";
    copyCard.className = "wsMsgContactAction";
    copyCard.textContent = tr("filemgr.ws.messages.copy_contact", null, "Copy contact");
    copyCard.addEventListener("click", (ev) => {
      ev.preventDefault();
      ev.stopPropagation();
      copyWorkspaceText(formatContactCardForClipboard(card), tr("filemgr.ws.messages.contact_copied", null, "Contact copied."));
    });
    actions.appendChild(copyCard);

    if (card.address) {
      const copyAddress = document.createElement("button");
      copyAddress.type = "button";
      copyAddress.className = "wsMsgContactAction";
      copyAddress.textContent = tr("filemgr.ws.messages.copy_address", null, "Copy address");
      copyAddress.addEventListener("click", (ev) => {
        ev.preventDefault();
        ev.stopPropagation();
        copyWorkspaceText(card.address, tr("filemgr.ws.messages.address_copied", null, "Address copied."));
      });
      actions.appendChild(copyAddress);
    }

    if (card.email) {
      const copyEmail = document.createElement("button");
      copyEmail.type = "button";
      copyEmail.className = "wsMsgContactAction";
      copyEmail.textContent = tr("filemgr.ws.messages.copy_email", null, "Copy email");
      copyEmail.addEventListener("click", (ev) => {
        ev.preventDefault();
        ev.stopPropagation();
        copyWorkspaceText(card.email, tr("filemgr.ws.messages.email_copied", null, "Email copied."));
      });
      actions.appendChild(copyEmail);
    }

    if (card.phone || card.mobile) {
      const copyPhone = document.createElement("button");
      copyPhone.type = "button";
      copyPhone.className = "wsMsgContactAction";
      copyPhone.textContent = tr("filemgr.ws.messages.copy_phone", null, "Copy phone");
      copyPhone.addEventListener("click", (ev) => {
        ev.preventDefault();
        ev.stopPropagation();
        copyWorkspaceText(card.phone || card.mobile, tr("filemgr.ws.messages.phone_copied", null, "Phone copied."));
      });
      actions.appendChild(copyPhone);
    }

    if (card.website) {
      const openWebsite = document.createElement("button");
      openWebsite.type = "button";
      openWebsite.className = "wsMsgContactAction";
      openWebsite.textContent = tr("filemgr.ws.messages.open_website", null, "Open website");
      openWebsite.addEventListener("click", (ev) => {
        ev.preventDefault();
        ev.stopPropagation();
        let url = String(card.website || "").trim();
        if (!/^https?:\/\//i.test(url)) url = "https://" + url;
        window.open(url, "_blank", "noopener,noreferrer");
      });
      actions.appendChild(openWebsite);
    }

    box.appendChild(actions);
    wrap.appendChild(box);

    if (parsed.after) {
      const after = document.createElement("div");
      after.className = "wsMsgContactText";
      after.textContent = parsed.after;
      wrap.appendChild(after);
    }

    return wrap;
  }

  function messageBodyNode(text) {
    const parsed = parseContactCardText(text);
    if (parsed) return contactCardNode(parsed);

    const body = document.createElement("div");
    body.className = "wsMsgBody";
    body.textContent = String(text || "");
    return body;
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
      if (FM && typeof FM.openAndHighlightRelPath === "function") {
        FM.openAndHighlightRelPath(path, kind);
        setDrawerOpen(false).catch(() => {});
        return;
      }

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
  let canModerateMessages = false;
  let actorMuted = false;
  let allMuted = false;
  let muteCount = 0;
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
        <div class="wsMsgHeadActions">
          <button type="button" class="btn secondary wsMsgMuteAll">${tr("filemgr.ws.messages.mute_all", null, "Mute all")}</button>
          <button type="button" class="btn secondary wsMsgClose">${tr("filemgr.close", null, "Close")}</button>
        </div>
      </div>
      <div class="wsMsgList" aria-live="polite"></div>
      <div class="wsMsgFoot">
        <div class="wsMsgMutedBanner hidden">${tr("filemgr.ws.messages.you_are_muted", null, "You are muted in this workspace message board.")}</div>
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
      muteAll: drawer.querySelector(".wsMsgMuteAll"),
      mutedBanner: drawer.querySelector(".wsMsgMutedBanner"),
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

    els.muteAll.addEventListener("click", async () => {
      const hasAnyMute = isTargetMuted("*") || muteCount > 0;
      await setMute("*", !hasAnyMute, tr("filemgr.ws.messages.everyone_except_owner", null, "Everyone except owners"));
    });

    els.send.addEventListener("click", sendMessage);

    els.input.addEventListener("keydown", (ev) => {
      if (ev.key === "Enter" && (ev.ctrlKey || ev.metaKey)) {
        ev.preventDefault();
        sendMessage();
      }
    });

    els.input.addEventListener("paste", () => {
      setTimeout(() => {
        if (parseContactCardText(els.input.value)) {
          setStatus(tr("filemgr.ws.messages.contact_card_detected", null, "Contact card detected. Send to share it with workspace members."));
        }
      }, 0);
    });

    installAttachmentDropHandlers();

    return els;
  }

  function setStatus(text, isError = false) {
    ensureUi();
    els.status.textContent = text || "";
    els.status.style.color = isError ? "rgb(var(--fail-rgb))" : "";
  }

  function isTargetMuted(targetKey) {
    return String(targetKey || "") === "*" ? !!allMuted : false;
  }

  function applyModerationUi() {
    ensureUi();

    els.drawer.classList.toggle("canModerate", !!canModerateMessages);

    const hasAnyMute = isTargetMuted("*") || muteCount > 0;
    if (els.muteAll) {
      els.muteAll.textContent = hasAnyMute
        ? tr("filemgr.ws.messages.unmute_all", null, "Unmute all")
        : tr("filemgr.ws.messages.mute_all", null, "Mute all");
    }

    if (els.mutedBanner) {
      els.mutedBanner.classList.toggle("hidden", !actorMuted);
    }

    const inputDisabled = !!actorMuted;
    els.input.disabled = inputDisabled;
    els.send.disabled = inputDisabled || sendBusy;

    if (inputDisabled) {
      els.input.placeholder = tr("filemgr.ws.messages.you_are_muted", null, "You are muted in this workspace message board.");
    } else {
      els.input.placeholder = tr("filemgr.ws.messages.placeholder", null, "Write a message for workspace members…");
    }
  }

  async function deleteMessage(messageId) {
    const ws = currentWorkspaceId();
    const id = Number(messageId || 0);
    if (!ws || !id) return;

    const ok = await wsMsgConfirm({
      title: tr("filemgr.ws.messages.delete_title", null, "Delete message?"),
      message: tr("filemgr.ws.messages.delete_confirm", null, "This message will be removed from the workspace message board."),
      confirmText: tr("filemgr.ws.messages.delete", null, "Delete"),
      cancelText: tr("filemgr.cancel", null, "Cancel"),
      danger: true
    });
    if (!ok) return;

    setStatus(tr("filemgr.ws.messages.deleting", null, "Deleting…"));

    try {
      await fetchJson("/api/v4/workspaces/messages/delete", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          workspace_id: ws,
          message_id: id
        })
      });

      await refreshMessages({ markRead: true });
      setStatus(tr("filemgr.ws.messages.deleted", null, "Deleted."));
      setTimeout(() => {
        if (els && els.status.textContent === tr("filemgr.ws.messages.deleted", null, "Deleted.")) setStatus("");
      }, 1200);
    } catch (e) {
      setStatus(
        tr("filemgr.ws.messages.delete_failed", { error: String(e && e.message ? e.message : e) }, `Delete failed: ${String(e && e.message ? e.message : e)}`),
        true
      );
    }
  }

  async function setMute(targetKey, muted, targetLabel = "") {
    const ws = currentWorkspaceId();
    const target = String(targetKey || "").trim();
    if (!ws || !target) return;

    const targetAll = target === "*";
    const messageId = targetAll ? 0 : Number(target || 0);
    if (!targetAll && (!Number.isFinite(messageId) || messageId <= 0)) return;

    const label = targetAll
      ? tr("filemgr.ws.messages.everyone", null, "everyone")
      : String(targetLabel || "").trim() || tr("filemgr.ws.messages.member", null, "Member");

    const ok = await wsMsgConfirm({
      title: muted
        ? tr("filemgr.ws.messages.mute_title", null, "Mute member?")
        : tr("filemgr.ws.messages.unmute_title", null, "Unmute member?"),
      message: muted
        ? tr("filemgr.ws.messages.mute_confirm", { target: label }, `${label} will not be able to send messages to this workspace board.`)
        : tr("filemgr.ws.messages.unmute_confirm", { target: label }, `${label} will be able to send messages again.`),
      targetLabel: tr("filemgr.ws.messages.member_label", null, "Member"),
      target: label,
      confirmText: muted
        ? tr("filemgr.ws.messages.mute", null, "Mute")
        : tr("filemgr.ws.messages.unmute", null, "Unmute"),
      cancelText: tr("filemgr.cancel", null, "Cancel"),
      danger: muted
    });
    if (!ok) return;

    setStatus(muted
      ? tr("filemgr.ws.messages.muting", null, "Muting…")
      : tr("filemgr.ws.messages.unmuting", null, "Unmuting…")
    );

    try {
      const j = await fetchJson("/api/v4/workspaces/messages/mute", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          workspace_id: ws,
          target_all: targetAll,
          message_id: messageId,
          muted: !!muted
        })
      });

      await refreshMessages({ markRead: true });
      applyModerationUi();
      setStatus(muted
        ? tr("filemgr.ws.messages.muted", null, "Muted.")
        : tr("filemgr.ws.messages.unmuted", null, "Unmuted.")
      );
      setTimeout(() => {
        const mutedText = tr("filemgr.ws.messages.muted", null, "Muted.");
        const unmutedText = tr("filemgr.ws.messages.unmuted", null, "Unmuted.");
        if (els && (els.status.textContent === mutedText || els.status.textContent === unmutedText)) setStatus("");
      }, 1200);
    } catch (e) {
      setStatus(
        tr("filemgr.ws.messages.mute_failed", { error: String(e && e.message ? e.message : e) }, `Mute update failed: ${String(e && e.message ? e.message : e)}`),
        true
      );
    }
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
      canModerateMessages = false;
      actorMuted = false;
      allMuted = false;
      muteCount = 0;
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
      const own = !!msg.is_own;
      row.className = own ? "wsMsgRow wsMsgOwn" : "wsMsgRow";

      const meta = document.createElement("div");
      meta.className = "wsMsgMeta";

      const author = document.createElement("div");
      author.className = "wsMsgAuthor";
      author.textContent = String(msg.author_name || tr("filemgr.ws.member", null, "Member"));

      const time = document.createElement("div");
      time.className = "wsMsgTime";
      time.textContent = formatTime(msg.created_at);

      const metaRight = document.createElement("div");
      metaRight.className = "wsMsgMetaRight";
      metaRight.appendChild(time);

      const canDelete = !!msg.can_delete;
      if (canDelete) {
        const del = document.createElement("button");
        del.type = "button";
        del.className = "wsMsgMiniBtn wsMsgDeleteBtn";
        del.title = tr("filemgr.ws.messages.delete", null, "Delete message");
        del.textContent = "×";
        del.addEventListener("click", (ev) => {
          ev.preventDefault();
          ev.stopPropagation();
          deleteMessage(msg.id);
        });
        metaRight.appendChild(del);
      }

      if (msg.can_mute_author) {
        const target = String(msg.id || "");
        const muted = !!msg.author_muted;
        const mute = document.createElement("button");
        mute.type = "button";
        mute.className = "wsMsgMiniBtn wsMsgMuteBtn" + (muted ? " isMuted" : "");
        mute.title = muted
          ? tr("filemgr.ws.messages.unmute_member", null, "Unmute member")
          : tr("filemgr.ws.messages.mute_member", null, "Mute member");
        mute.textContent = muted
          ? tr("filemgr.ws.messages.unmute_short", null, "Unmute")
          : tr("filemgr.ws.messages.mute_short", null, "Mute");
        mute.addEventListener("click", (ev) => {
          ev.preventDefault();
          ev.stopPropagation();
          setMute(target, !muted, String(msg.author_name || ""));
        });
        metaRight.appendChild(mute);
      }

      meta.appendChild(author);
      meta.appendChild(metaRight);

      const body = messageBodyNode(msg.body || "");

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
      canModerateMessages = !!j.can_moderate_messages;
      actorMuted = !!j.actor_muted;
      allMuted = !!j.message_board_muted_all;
      muteCount = Number(j.workspace_message_mute_count || 0);
      messages = Array.isArray(j.messages) ? j.messages : [];
      latestId = Number(j.latest_id || 0);
      unreadCount = Number(j.unread_count || 0);

      applyModerationUi();
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
    if (actorMuted) {
      setStatus(tr("filemgr.ws.messages.you_are_muted", null, "You are muted in this workspace message board."), true);
      sendBusy = false;
      applyModerationUi();
      return;
    }
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
      applyModerationUi();
      if (!actorMuted) els.input.focus();
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
      if (els.muteAll) {
        els.muteAll.textContent = isTargetMuted("*")
          ? tr("filemgr.ws.messages.unmute_all", null, "Unmute all")
          : tr("filemgr.ws.messages.mute_all", null, "Mute all");
      }
      els.input.placeholder = actorMuted
        ? tr("filemgr.ws.messages.you_are_muted", null, "You are muted in this workspace message board.")
        : tr("filemgr.ws.messages.placeholder", null, "Write a message for workspace members…");
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
