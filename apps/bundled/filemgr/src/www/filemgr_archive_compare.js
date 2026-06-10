(() => {
  "use strict";

  window.PQNAS_FILEMGR = window.PQNAS_FILEMGR || {};

  function t(key, params, fallback) {
    try {
      const api = window.PQNAS_I18N;
      if (api && typeof api.t === "function") return api.t(key, params || null, fallback || key);
    } catch (_) {}

    let out = String(fallback || key || "");
    const p = params || {};
    for (const k of Object.keys(p)) out = out.split(`{${k}}`).join(String(p[k]));
    return out;
  }

  function esc(v) {
    return String(v ?? "")
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;")
      .replace(/'/g, "&#39;");
  }

  function fmtBytes(n) {
    n = Number(n || 0);
    if (!Number.isFinite(n) || n <= 0) return "0 B";
    const u = ["B", "KiB", "MiB", "GiB", "TiB"];
    let i = 0;
    while (n >= 1024 && i < u.length - 1) {
      n /= 1024;
      i++;
    }
    return `${n.toFixed(i === 0 ? 0 : 1)} ${u[i]}`;
  }

  function hex32(n) {
    return (Number(n) >>> 0).toString(16).padStart(8, "0");
  }

  function baseName(path) {
    const s = String(path || "");
    const i = Math.max(s.lastIndexOf("/"), s.lastIndexOf("\\"));
    return i >= 0 ? s.slice(i + 1) : s;
  }

  function isZipPath(path) {
    return /\.zip$/i.test(String(path || "").split("?")[0]);
  }

  const MAX_ZIP_COMPARE_BYTES = 64 * 1024 * 1024;
  const MAX_ZIP_ENTRIES = 6000;

  let modal = null;
  let titleEl = null;
  let statusEl = null;
  let summaryEl = null;
  let bodyEl = null;

  function ensureCss() {
    if (document.getElementById("fmArchiveCompareCss")) return;

    const style = document.createElement("style");
    style.id = "fmArchiveCompareCss";
    style.textContent = `
      .fmArchiveCompareModal{
        position:fixed;
        inset:0;
        z-index:99999;
        display:none;
        align-items:center;
        justify-content:center;
        padding:24px;
        background:rgba(0,0,0,.62);
        backdrop-filter:blur(8px);
      }
      .fmArchiveCompareModal.show{display:flex;}
      .fmArchiveCompareCard{
        width:min(1300px,96vw);
        height:min(850px,92vh);
        display:flex;
        flex-direction:column;
        overflow:hidden;
        border:1px solid var(--border, rgba(255,255,255,.16));
        border-radius:22px;
        background:var(--fm_surface, var(--panel, #101522));
        color:var(--fg, #e8f3ff);
        box-shadow:0 24px 90px rgba(0,0,0,.55);
      }
      .fmArchiveCompareHead{
        display:flex;
        align-items:flex-start;
        justify-content:space-between;
        gap:12px;
        padding:12px 14px;
        border-bottom:1px solid var(--border2, rgba(255,255,255,.10));
        background:rgba(0,0,0,.18);
      }
      .fmArchiveCompareTitle{
        font-weight:950;
        font-size:15px;
      }
      .fmArchiveComparePath{
        margin-top:4px;
        font-family:var(--mono, monospace);
        font-size:12px;
        opacity:.75;
        word-break:break-all;
      }
      .fmArchiveCompareBtn{
        border:1px solid rgba(255,255,255,.16);
        border-radius:12px;
        background:rgba(255,255,255,.06);
        color:inherit;
        padding:8px 11px;
        font-weight:850;
        cursor:pointer;
      }
      .fmArchiveCompareBtn:hover{background:rgba(255,255,255,.10);}
      .fmArchiveCompareStatus{
        padding:9px 14px;
        border-bottom:1px solid var(--border2, rgba(255,255,255,.10));
        font-size:13px;
        opacity:.9;
      }
      .fmArchiveCompareStatus.warn{color:rgba(var(--warn-rgb,255,190,0),.98);font-weight:850;}
      .fmArchiveCompareStatus.err{color:rgba(var(--fail-rgb,255,90,90),.98);font-weight:850;}
      .fmArchiveCompareSummary{
        display:flex;
        flex-wrap:wrap;
        gap:8px;
        padding:10px 14px;
        border-bottom:1px solid var(--border2, rgba(255,255,255,.10));
      }
      .fmArchiveChip{
        border:1px solid var(--border2, rgba(255,255,255,.12));
        border-radius:999px;
        padding:5px 10px;
        font-weight:900;
        font-size:12px;
        background:rgba(255,255,255,.05);
      }
      .fmArchiveChip.add{background:rgba(40,150,80,.20);}
      .fmArchiveChip.remove{background:rgba(180,40,40,.20);}
      .fmArchiveChip.change{background:rgba(255,190,0,.20);}
      .fmArchiveCompareBody{
        flex:1 1 auto;
        min-height:0;
        overflow:auto;
        padding:14px;
        display:grid;
        gap:14px;
        align-content:start;
      }
      .fmArchiveCompareColumns{
        display:grid;
        grid-template-columns:minmax(0,1fr) minmax(0,1fr);
        gap:14px;
        min-height:0;
      }
      .fmArchiveSection{
        border:1px solid var(--border2, rgba(255,255,255,.10));
        border-radius:16px;
        overflow:hidden;
        background:rgba(0,0,0,.16);
      }
      .fmArchiveSectionHead{
        padding:10px 12px;
        font-weight:950;
        border-bottom:1px solid var(--border2, rgba(255,255,255,.10));
        background:rgba(0,0,0,.18);
      }
      .fmArchiveRows{
        display:grid;
        max-height:min(44vh, 390px);
        overflow:auto;
      }
      .fmArchiveRow{
        display:grid;
        grid-template-columns:minmax(0,1fr) auto auto;
        gap:12px;
        padding:8px 12px;
        border-bottom:1px solid rgba(255,255,255,.06);
        font-family:var(--mono, monospace);
        font-size:12px;
        align-items:center;
      }
      .fmArchiveMoveRow{
        display:grid;
        grid-template-columns:minmax(0,1fr) 28px minmax(0,1fr) auto;
        gap:10px;
        padding:8px 12px;
        border-bottom:1px solid rgba(255,255,255,.06);
        font-family:var(--mono, monospace);
        font-size:12px;
        align-items:center;
      }
      .fmArchiveMoveArrow{
        text-align:center;
        font-weight:950;
        color:#74c7ff;
      }
      .fmArchiveMovePath{
        overflow:hidden;
        text-overflow:ellipsis;
        white-space:nowrap;
      }
      .fmArchiveRow:last-child{border-bottom:0;}
      .fmArchivePath{
        overflow:hidden;
        text-overflow:ellipsis;
        white-space:nowrap;
      }
      .fmArchiveMeta{
        opacity:.75;
        white-space:nowrap;
      }
      .fmArchiveKind{
        font-weight:950;
        white-space:nowrap;
      }
      .fmArchiveKind.add{color:#5ee27a;}
      .fmArchiveKind.remove{color:#ff7070;}
      .fmArchiveKind.change{color:#ffd15e;}
      .fmArchiveKind.move{color:#74c7ff;}
      .fmArchiveChip.move{background:rgba(90,170,255,.20);}
      .fmArchiveEmpty{
        padding:14px;
        opacity:.72;
      }
      @media (max-width:760px){
        .fmArchiveCompareCard{height:94vh;}
        .fmArchiveCompareColumns{grid-template-columns:1fr;}
        .fmArchiveRow{grid-template-columns:minmax(0,1fr);}
        .fmArchiveMoveRow{grid-template-columns:minmax(0,1fr);}
        .fmArchiveMoveArrow{text-align:left;}
        .fmArchiveMeta,.fmArchiveKind{white-space:normal;}
      }
    `;
    document.head.appendChild(style);
  }

  function ensureModal() {
    ensureCss();

    if (modal) return modal;

    modal = document.createElement("div");
    modal.className = "fmArchiveCompareModal";
    modal.setAttribute("aria-hidden", "true");

    modal.innerHTML = `
      <div class="fmArchiveCompareCard" role="dialog" aria-modal="true" aria-label="${esc(t("filemgr.archive_compare.title", null, "Archive version compare"))}">
        <div class="fmArchiveCompareHead">
          <div>
            <div class="fmArchiveCompareTitle">${esc(t("filemgr.archive_compare.title", null, "Archive version compare"))}</div>
            <div class="fmArchiveComparePath" data-fmac-title></div>
          </div>
          <button class="fmArchiveCompareBtn" type="button" data-fmac-close>${esc(t("common.close", null, "Close"))}</button>
        </div>
        <div class="fmArchiveCompareStatus warn" data-fmac-status>${esc(t("filemgr.archive_compare.loading", null, "Loading archive manifests…"))}</div>
        <div class="fmArchiveCompareSummary" data-fmac-summary></div>
        <div class="fmArchiveCompareBody" data-fmac-body></div>
      </div>
    `;

    document.body.appendChild(modal);

    titleEl = modal.querySelector("[data-fmac-title]");
    statusEl = modal.querySelector("[data-fmac-status]");
    summaryEl = modal.querySelector("[data-fmac-summary]");
    bodyEl = modal.querySelector("[data-fmac-body]");

    modal.querySelector("[data-fmac-close]")?.addEventListener("click", close);
    modal.addEventListener("click", (ev) => {
      if (ev.target === modal) close();
    });

    document.addEventListener("keydown", (ev) => {
      if (ev.key === "Escape" && modal.classList.contains("show")) {
        ev.preventDefault();
        close();
      }
    });

    return modal;
  }

  function setStatus(text, kind = "") {
    if (!statusEl) return;
    statusEl.className = `fmArchiveCompareStatus${kind ? " " + kind : ""}`;
    statusEl.textContent = text || "";
  }

  async function fetchManifest(url, label) {
    const r = await fetch(url, {
      method: "GET",
      credentials: "include",
      cache: "no-store",
      headers: {
        "Accept": "application/json",
        "Cache-Control": "no-cache",
        "Pragma": "no-cache"
      }
    });

    let j = null;
    try {
      j = await r.json();
    } catch (_) {}

    if (!r.ok || !j || j.ok === false) {
      const msg = j && j.error ? j.error : `HTTP ${r.status}`;
      throw new Error(`${label}: ${msg}`);
    }

    const map = new Map();
    for (const e of Array.isArray(j.entries) ? j.entries : []) {
      if (!e || !e.path) continue;
      map.set(String(e.path), {
        path: String(e.path),
        size: Number(e.size || 0),
        compSize: Number(e.compressed_size || 0),
        crc32: String(e.crc32 || "")
      });
    }

    return map;
  }

  async function fetchArrayBuffer(url, label) {
    const r = await fetch(url, {
      method: "GET",
      credentials: "include",
      cache: "no-store",
      headers: {
        "Accept": "application/zip,application/octet-stream,*/*",
        "Cache-Control": "no-cache",
        "Pragma": "no-cache"
      }
    });

    if (!r.ok) {
      throw new Error(`${label}: HTTP ${r.status}`);
    }

    const blob = await r.blob();
    if (blob.size > MAX_ZIP_COMPARE_BYTES) {
      throw new Error(`${label}: ${t("filemgr.archive_compare.too_large", null, "ZIP is too large for browser-side compare")} (${fmtBytes(blob.size)})`);
    }

    return await blob.arrayBuffer();
  }

  function readZipManifest(buf, label) {
    const dv = new DataView(buf);
    const len = dv.byteLength;

    if (len < 22) throw new Error(`${label}: not a ZIP file`);

    const min = Math.max(0, len - 22 - 65535);
    let eocd = -1;

    for (let i = len - 22; i >= min; i--) {
      if (dv.getUint32(i, true) === 0x06054b50) {
        eocd = i;
        break;
      }
    }

    if (eocd < 0) throw new Error(`${label}: ZIP central directory not found`);

    const totalEntries = dv.getUint16(eocd + 10, true);
    const cdSize = dv.getUint32(eocd + 12, true);
    const cdOffset = dv.getUint32(eocd + 16, true);

    if (totalEntries === 0xffff || cdSize === 0xffffffff || cdOffset === 0xffffffff) {
      throw new Error(`${label}: ZIP64 is not supported in this MVP`);
    }

    if (totalEntries > MAX_ZIP_ENTRIES) {
      throw new Error(`${label}: too many ZIP entries (${totalEntries})`);
    }

    if (cdOffset + cdSize > len) {
      throw new Error(`${label}: invalid ZIP central directory`);
    }

    const utf8 = new TextDecoder("utf-8", { fatal:false });
    const entries = new Map();

    let off = cdOffset;

    for (let i = 0; i < totalEntries; i++) {
      if (off + 46 > len || dv.getUint32(off, true) !== 0x02014b50) {
        throw new Error(`${label}: invalid central directory entry`);
      }

      const flags = dv.getUint16(off + 8, true);
      const crc32 = dv.getUint32(off + 16, true);
      const compSize = dv.getUint32(off + 20, true);
      const size = dv.getUint32(off + 24, true);
      const nameLen = dv.getUint16(off + 28, true);
      const extraLen = dv.getUint16(off + 30, true);
      const commentLen = dv.getUint16(off + 32, true);

      const nameStart = off + 46;
      const nameEnd = nameStart + nameLen;
      if (nameEnd > len) throw new Error(`${label}: invalid filename length`);

      let path = utf8.decode(new Uint8Array(buf, nameStart, nameLen));
      path = path.replace(/\\/g, "/").replace(/^\/+/, "");

      const dangerous = path.split("/").some((p) => p === "..");
      const isDir = path.endsWith("/");

      if (path && !isDir) {
        entries.set(path, {
          path,
          size,
          compSize,
          crc32: hex32(crc32),
          flags,
          dangerous
        });
      }

      off = nameEnd + extraLen + commentLen;
    }

    return entries;
  }

  function compareManifests(oldMap, curMap) {
    const added = [];
    const removed = [];
    const changed = [];
    const moved = [];
    let unchanged = 0;

    for (const [path, cur] of curMap.entries()) {
      const old = oldMap.get(path);
      if (!old) {
        added.push(cur);
        continue;
      }

      if (old.size !== cur.size || old.crc32 !== cur.crc32) {
        changed.push({
          path,
          old,
          cur
        });
      } else {
        unchanged++;
      }
    }

    for (const [path, old] of oldMap.entries()) {
      if (!curMap.has(path)) removed.push(old);
    }

    // Detect moved/renamed files:
    // if removed and added have same size + CRC32, content is probably same
    // and only the ZIP path changed.
    const addedBySig = new Map();
    for (const a of added) {
      const sig = `${a.size}:${a.crc32}`;
      if (!addedBySig.has(sig)) addedBySig.set(sig, []);
      addedBySig.get(sig).push(a);
    }

    const removedKeep = [];
    const addedUsed = new Set();

    for (const r of removed) {
      const sig = `${r.size}:${r.crc32}`;
      const candidates = addedBySig.get(sig) || [];
      const match = candidates.find((a) => !addedUsed.has(a.path));

      if (match) {
        addedUsed.add(match.path);
        moved.push({
          oldPath: r.path,
          newPath: match.path,
          size: match.size,
          crc32: match.crc32
        });
      } else {
        removedKeep.push(r);
      }
    }

    const addedKeep = added.filter((a) => !addedUsed.has(a.path));

    const byPath = (a, b) => String(a.path || "").localeCompare(String(b.path || ""), undefined, { numeric:true, sensitivity:"base" });
    const byMove = (a, b) => String(a.newPath || "").localeCompare(String(b.newPath || ""), undefined, { numeric:true, sensitivity:"base" });

    addedKeep.sort(byPath);
    removedKeep.sort(byPath);
    changed.sort((a, b) => String(a.path || "").localeCompare(String(b.path || ""), undefined, { numeric:true, sensitivity:"base" }));
    moved.sort(byMove);

    return { added: addedKeep, removed: removedKeep, changed, moved, unchanged };
  }

  function renderSection(title, kind, rows, renderRow) {
    const section = document.createElement("section");
    section.className = "fmArchiveSection";

    const head = document.createElement("div");
    head.className = "fmArchiveSectionHead";
    head.textContent = `${title} (${rows.length})`;
    section.appendChild(head);

    const list = document.createElement("div");
    list.className = "fmArchiveRows";

    if (!rows.length) {
      const empty = document.createElement("div");
      empty.className = "fmArchiveEmpty";
      empty.textContent = t("filemgr.archive_compare.none", null, "None");
      list.appendChild(empty);
    } else {
      for (const rowData of rows.slice(0, 500)) {
        list.appendChild(renderRow(rowData, kind));
      }

      if (rows.length > 500) {
        const more = document.createElement("div");
        more.className = "fmArchiveEmpty";
        more.textContent = t("filemgr.archive_compare.more_hidden", { count: rows.length - 500 }, `${rows.length - 500} more hidden`);
        list.appendChild(more);
      }
    }

    section.appendChild(list);
    return section;
  }

  function basicRow(entry, kind, metaText = "") {
    const row = document.createElement("div");
    row.className = "fmArchiveRow";

    const path = document.createElement("div");
    path.className = "fmArchivePath";
    path.title = entry.path || "";
    path.textContent = entry.path || "";

    const meta = document.createElement("div");
    meta.className = "fmArchiveMeta";
    meta.textContent = metaText || `${fmtBytes(entry.size)} • CRC ${entry.crc32}`;

    const k = document.createElement("div");
    k.className = `fmArchiveKind ${kind}`;
    k.textContent = kind === "add" ? "+" : kind === "remove" ? "−" : kind === "move" ? "↔" : "~";

    row.appendChild(path);
    row.appendChild(meta);
    row.appendChild(k);
    return row;
  }

  function movedRow(entry) {
    const row = document.createElement("div");
    row.className = "fmArchiveMoveRow";

    const oldPath = document.createElement("div");
    oldPath.className = "fmArchiveMovePath";
    oldPath.title = entry.oldPath || "";
    oldPath.textContent = entry.oldPath || "";

    const arrow = document.createElement("div");
    arrow.className = "fmArchiveMoveArrow";
    arrow.textContent = "→";

    const newPath = document.createElement("div");
    newPath.className = "fmArchiveMovePath";
    newPath.title = entry.newPath || "";
    newPath.textContent = entry.newPath || "";

    const meta = document.createElement("div");
    meta.className = "fmArchiveMeta";
    meta.textContent = `${fmtBytes(entry.size)} • CRC ${entry.crc32}`;

    row.appendChild(oldPath);
    row.appendChild(arrow);
    row.appendChild(newPath);
    row.appendChild(meta);
    return row;
  }

  function renderDiff(diff) {
    summaryEl.innerHTML = `
      <span class="fmArchiveChip add">+ ${diff.added.length} ${esc(t("filemgr.archive_compare.added", null, "added"))}</span>
      <span class="fmArchiveChip remove">− ${diff.removed.length} ${esc(t("filemgr.archive_compare.removed", null, "removed"))}</span>
      <span class="fmArchiveChip move">↔ ${diff.moved.length} ${esc(t("filemgr.archive_compare.moved", null, "moved/renamed"))}</span>
      <span class="fmArchiveChip change">~ ${diff.changed.length} ${esc(t("filemgr.archive_compare.changed", null, "changed"))}</span>
      <span class="fmArchiveChip">= ${diff.unchanged} ${esc(t("filemgr.archive_compare.unchanged", null, "unchanged"))}</span>
    `;

    bodyEl.replaceChildren();

    if (diff.moved.length) {
      const note = document.createElement("div");
      note.className = "fmArchiveEmpty";
      note.textContent = t(
        "filemgr.archive_compare.moved_note",
        null,
        "Moved / renamed detection is based on matching file size and CRC."
      );
      bodyEl.appendChild(note);
    }

    const columns = document.createElement("div");
    columns.className = "fmArchiveCompareColumns";

    columns.appendChild(renderSection(
      t("filemgr.archive_compare.added_files", null, "Added files"),
      "add",
      diff.added,
      (e) => basicRow(e, "add")
    ));

    columns.appendChild(renderSection(
      t("filemgr.archive_compare.removed_files", null, "Removed files"),
      "remove",
      diff.removed,
      (e) => basicRow(e, "remove")
    ));

    bodyEl.appendChild(columns);

    bodyEl.appendChild(renderSection(
      t("filemgr.archive_compare.moved_files", null, "Moved / renamed files"),
      "move",
      diff.moved,
      (e) => movedRow(e)
    ));

    bodyEl.appendChild(renderSection(
      t("filemgr.archive_compare.changed_files", null, "Changed files"),
      "change",
      diff.changed,
      (e) => basicRow(
        { path:e.path, size:e.cur.size, crc32:e.cur.crc32 },
        "change",
        `${fmtBytes(e.old.size)} / ${e.old.crc32} → ${fmtBytes(e.cur.size)} / ${e.cur.crc32}`
      )
    ));
  }

  async function open(opts) {
    const o = opts || {};
    const path = String(o.path || "").trim();
    const leftUrl = String(o.leftUrl || "").trim();
    const rightUrl = String(o.rightUrl || "").trim();
    const leftManifestUrl = String(o.leftManifestUrl || "").trim();
    const rightManifestUrl = String(o.rightManifestUrl || "").trim();

    if (!path || ((!leftUrl || !rightUrl) && (!leftManifestUrl || !rightManifestUrl))) return false;

    ensureModal();

    if (titleEl) titleEl.textContent = "/" + path;
    summaryEl.replaceChildren();
    bodyEl.replaceChildren();

    modal.classList.add("show");
    modal.setAttribute("aria-hidden", "false");
    setStatus(t("filemgr.archive_compare.loading", null, "Loading archive manifests…"), "warn");

    try {
      const leftManifestUrl = String(o.leftManifestUrl || "").trim();
      const rightManifestUrl = String(o.rightManifestUrl || "").trim();

      let cur;
      let old;

      if (leftManifestUrl && rightManifestUrl) {
        [cur, old] = await Promise.all([
          fetchManifest(leftManifestUrl, t("filemgr.archive_compare.current", null, "Current file")),
          fetchManifest(rightManifestUrl, t("filemgr.archive_compare.version", null, "Selected version"))
        ]);
      } else {
        const [curBuf, oldBuf] = await Promise.all([
          fetchArrayBuffer(leftUrl, t("filemgr.archive_compare.current", null, "Current file")),
          fetchArrayBuffer(rightUrl, t("filemgr.archive_compare.version", null, "Selected version"))
        ]);

        cur = readZipManifest(curBuf, t("filemgr.archive_compare.current", null, "Current file"));
        old = readZipManifest(oldBuf, t("filemgr.archive_compare.version", null, "Selected version"));
      }

      const diff = compareManifests(old, cur);

      renderDiff(diff);
      setStatus(
        t(
          "filemgr.archive_compare.loaded",
          {
            added: diff.added.length,
            removed: diff.removed.length,
            changed: diff.changed.length,
            unchanged: diff.unchanged
          },
          `Loaded. +${diff.added.length} / -${diff.removed.length} / ↔${diff.moved.length} / ~${diff.changed.length} / =${diff.unchanged}`
        ),
        "ok"
      );
    } catch (e) {
      summaryEl.replaceChildren();
      bodyEl.innerHTML = `<div class="fmArchiveEmpty">${esc(String(e && e.message ? e.message : e))}</div>`;
      setStatus(String(e && e.message ? e.message : e), "err");
    }

    return true;
  }

  function close() {
    if (!modal) return;
    modal.classList.remove("show");
    modal.setAttribute("aria-hidden", "true");
    summaryEl?.replaceChildren();
    bodyEl?.replaceChildren();
  }

  window.PQNAS_FILEMGR.versionArchiveCompare = {
    open,
    close,
    isZipPath
  };
})();
