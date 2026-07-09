window.PQNAS_FILEMGR = window.PQNAS_FILEMGR || {};

(() => {
  "use strict";

  const FM = window.PQNAS_FILEMGR;
  const previousOfficePreview = FM.officePreview || null;

  const SPREADSHEET_EXTS = new Set(["csv", "tsv", "xls", "xlsx", "ods"]);
  const MAX_RENDER_ROWS = 1000;
  const MAX_RENDER_COLS = 80;
  const XLSX_VENDOR_URL = "./vendor/xlsx.full.min.js";

  let modal = null;
  let titleEl = null;
  let pathEl = null;
  let infoEl = null;
  let tabsEl = null;
  let bodyEl = null;
  let downloadBtn = null;
  let editBtn = null;
  let currentEditContext = null;
  let spreadsheetEditLoadPromise = null;
  let openSeq = 0;
  let dragState = null;
  let resizeState = null;
  let positionedOnce = false;
  let xlsxLoadPromise = null;

  function tr(key, vars = null, fallback = "") {
    try {
      if (window.PQNAS_I18N && typeof window.PQNAS_I18N.t === "function") {
        return window.PQNAS_I18N.t(key, vars, fallback || key);
      }
    } catch (_) {}
    return fallback || key;
  }

  function fileExtLower(name) {
    const n = String(name || "").toLowerCase().trim();
    const clean = n.split("?")[0].split("#")[0];
    const slash = Math.max(clean.lastIndexOf("/"), clean.lastIndexOf("\\"));
    const base = slash >= 0 ? clean.slice(slash + 1) : clean;
    const dot = base.lastIndexOf(".");
    if (dot <= 0 || dot === base.length - 1) return "";
    return base.slice(dot + 1);
  }

  function canOpenFor(item) {
    if (!item || item.type !== "file") return false;
    return SPREADSHEET_EXTS.has(fileExtLower(item.name || item.rel || ""));
  }

  function isSpreadsheetName(name) {
    return SPREADSHEET_EXTS.has(fileExtLower(name));
  }

  function canEditFor(item) {
    if (!item || item.type !== "file") return false;
    if (fileExtLower(item.name || item.rel || "") !== "xlsx") return false;
    try {
      if (FM && typeof FM.canWriteCurrentScope === "function") return !!FM.canWriteCurrentScope();
    } catch (_) {}
    return true;
  }

  function safeName(item) {
    return String(item && item.name ? item.name : tr("filemgr.spreadsheet.title", null, "Spreadsheet preview"));
  }

  function relPathFor(item) {
    if (FM && typeof FM.currentRelPathFor === "function") {
      return FM.currentRelPathFor(item);
    }
    const cur = FM && typeof FM.getCurPath === "function" ? FM.getCurPath() : "";
    const name = safeName(item);
    return cur ? `${cur}/${name}` : name;
  }

  function getDownloadUrl(rel) {
    if (FM && typeof FM.apiGetUrl === "function") {
      return FM.apiGetUrl(rel);
    }
    return `/api/v4/files/get?path=${encodeURIComponent(rel || "")}`;
  }

  function loadScriptOnce(src, attrName) {
    return new Promise((resolve, reject) => {
      const existing = document.querySelector(`script[${attrName}="1"]`);
      if (existing) {
        existing.addEventListener("load", resolve, { once: true });
        existing.addEventListener("error", () => reject(new Error(`failed to load ${src}`)), { once: true });
        window.setTimeout(resolve, 0);
        return;
      }
      const script = document.createElement("script");
      script.src = src;
      script.defer = true;
      script.setAttribute(attrName, "1");
      script.onload = resolve;
      script.onerror = () => reject(new Error(`failed to load ${src}`));
      document.head.appendChild(script);
    });
  }

  function loadStyleOnce(href, attrName) {
    return new Promise((resolve, reject) => {
      const existing = document.querySelector(`link[${attrName}="1"]`);
      if (existing) {
        window.setTimeout(resolve, 0);
        return;
      }
      const link = document.createElement("link");
      link.rel = "stylesheet";
      link.href = href;
      link.setAttribute(attrName, "1");
      link.onload = resolve;
      link.onerror = () => reject(new Error(`failed to load ${href}`));
      document.head.appendChild(link);
    });
  }

  function ensureSpreadsheetEditor() {
    if (FM && FM.spreadsheetEdit && typeof FM.spreadsheetEdit.open === "function") {
      return Promise.resolve(FM.spreadsheetEdit);
    }
    if (spreadsheetEditLoadPromise) return spreadsheetEditLoadPromise;

    spreadsheetEditLoadPromise = Promise.all([
      loadStyleOnce("./spreadsheet_edit.css", "data-pqnas-spreadsheet-edit-css"),
      loadScriptOnce("./spreadsheet_edit.js?v=spreadsheet-edit-1", "data-pqnas-spreadsheet-edit-js")
    ]).then(() => {
      if (FM && FM.spreadsheetEdit && typeof FM.spreadsheetEdit.open === "function") return FM.spreadsheetEdit;
      throw new Error("spreadsheet editor did not register");
    });

    return spreadsheetEditLoadPromise;
  }

  async function openSpreadsheetEditor() {
    if (!currentEditContext) return;
    try {
      setInfo(tr("filemgr.spreadsheet.editor_loading", null, "Loading spreadsheet editor…"), "warn");
      const editor = await ensureSpreadsheetEditor();
      close();
      editor.open(currentEditContext);
    } catch (e) {
      const msg = String(e && e.message ? e.message : e);
      setInfo(tr("filemgr.spreadsheet_editor.failed", { error: msg }, `Spreadsheet editor failed: ${msg}`), "err");
    }
  }

  function columnName(idx) {
    let n = idx + 1;
    let out = "";
    while (n > 0) {
      const r = (n - 1) % 26;
      out = String.fromCharCode(65 + r) + out;
      n = Math.floor((n - 1) / 26);
    }
    return out;
  }

  function normalizeRows(rows) {
    const out = Array.isArray(rows) ? rows : [];
    let maxCols = 0;
    for (const row of out) {
      if (Array.isArray(row)) maxCols = Math.max(maxCols, row.length);
    }
    maxCols = Math.min(maxCols, MAX_RENDER_COLS);
    return {
      rows: out.slice(0, MAX_RENDER_ROWS).map((row) => {
        const arr = Array.isArray(row) ? row : [];
        return arr.slice(0, maxCols).map((v) => v == null ? "" : String(v));
      }),
      cols: maxCols,
      truncatedRows: out.length > MAX_RENDER_ROWS,
      truncatedCols: out.some((row) => Array.isArray(row) && row.length > MAX_RENDER_COLS)
    };
  }

  function parseDelimited(text, delimiter) {
    const rows = [];
    let row = [];
    let field = "";
    let quoted = false;
    const s = String(text || "");

    for (let i = 0; i < s.length; i++) {
      const ch = s[i];

      if (quoted) {
        if (ch === '"') {
          if (s[i + 1] === '"') {
            field += '"';
            i++;
          } else {
            quoted = false;
          }
        } else {
          field += ch;
        }
        continue;
      }

      if (ch === '"') {
        quoted = true;
      } else if (ch === delimiter) {
        row.push(field);
        field = "";
      } else if (ch === "\n") {
        row.push(field);
        rows.push(row);
        row = [];
        field = "";
      } else if (ch !== "\r") {
        field += ch;
      }
    }

    row.push(field);
    if (row.length > 1 || row[0] !== "" || rows.length === 0) rows.push(row);
    return rows;
  }

  function ensureXlsxLibrary() {
    if (window.XLSX && typeof window.XLSX.read === "function") {
      return Promise.resolve(window.XLSX);
    }

    if (xlsxLoadPromise) return xlsxLoadPromise;

    xlsxLoadPromise = new Promise((resolve, reject) => {
      const existing = document.querySelector('script[data-pqnas-xlsx-lib="1"]');
      if (existing) {
        existing.addEventListener("load", () => resolve(window.XLSX), { once: true });
        existing.addEventListener("error", () => reject(new Error("XLSX parser failed to load.")), { once: true });
        return;
      }

      const script = document.createElement("script");
      script.src = XLSX_VENDOR_URL;
      script.defer = true;
      script.dataset.pqnasXlsxLib = "1";
      script.onload = () => {
        if (window.XLSX && typeof window.XLSX.read === "function") resolve(window.XLSX);
        else reject(new Error("XLSX parser loaded, but window.XLSX is missing."));
      };
      script.onerror = () => reject(new Error("XLSX parser is not installed. Vendor xlsx.full.min.js under apps/bundled/filemgr/src/www/vendor/."));
      document.head.appendChild(script);
    });

    return xlsxLoadPromise;
  }

  async function readWorkbookRows(url, ext) {
    const r = await fetch(url, { credentials: "include", cache: "no-store" });
    if (!r.ok) {
      const msg = await r.text().catch(() => "");
      throw new Error(msg || `HTTP ${r.status}`);
    }

    if (ext === "csv" || ext === "tsv") {
      const text = await r.text();
      return [{ name: ext.toUpperCase(), rows: parseDelimited(text, ext === "tsv" ? "\t" : ",") }];
    }

    const XLSX = await ensureXlsxLibrary();
    const buf = await r.arrayBuffer();

    // Security: parse the workbook as data only; do not execute macros, formulas,
    // external links or embedded active content in the browser.
    const wb = XLSX.read(buf, {
      type: "array",
      cellFormula: false,
      cellHTML: false,
      cellNF: false,
      cellStyles: false
    });

    const names = Array.isArray(wb.SheetNames) ? wb.SheetNames : [];
    return names.map((name) => {
      const ws = wb.Sheets[name];
      const rows = XLSX.utils.sheet_to_json(ws, {
        header: 1,
        raw: false,
        defval: "",
        blankrows: false
      });
      return { name, rows };
    });
  }

  function clampNumber(v, min, max) {
    return Math.max(min, Math.min(max, v));
  }

  function beginResize(ev, mode) {
    const box = modal ? modal.querySelector(".spreadsheetPreviewBox") : null;
    if (!box) return;

    const r = box.getBoundingClientRect();
    resizeState = {
      mode,
      startX: ev.clientX,
      startY: ev.clientY,
      startWidth: r.width,
      startHeight: r.height
    };

    box.style.width = `${r.width}px`;
    box.style.height = `${r.height}px`;

    ev.currentTarget.classList.add("active");
    try { ev.currentTarget.setPointerCapture(ev.pointerId); } catch (_) {}
    ev.preventDefault();
  }

  function moveResize(ev) {
    if (!resizeState || !modal) return;

    const box = modal.querySelector(".spreadsheetPreviewBox");
    if (!box) return;

    const maxW = Math.max(320, window.innerWidth * 0.96);
    const maxH = Math.max(260, window.innerHeight * 0.92);
    const minW = Math.min(720, maxW);
    const minH = Math.min(420, maxH);

    if (resizeState.mode === "right") {
      box.style.width = `${clampNumber(resizeState.startWidth + ev.clientX - resizeState.startX, minW, maxW)}px`;
    } else if (resizeState.mode === "bottom") {
      box.style.height = `${clampNumber(resizeState.startHeight + ev.clientY - resizeState.startY, minH, maxH)}px`;
    } else if (resizeState.mode === "corner") {
      box.style.width = `${clampNumber(resizeState.startWidth + ev.clientX - resizeState.startX, minW, maxW)}px`;
      box.style.height = `${clampNumber(resizeState.startHeight + ev.clientY - resizeState.startY, minH, maxH)}px`;
    }
  }

  function endResize() {
    if (!resizeState || !modal) return;
    for (const h of modal.querySelectorAll(".spreadsheetPreviewResizeHandle.active")) {
      h.classList.remove("active");
    }
    resizeState = null;
  }

  function ensureModal() {
    if (modal) return;

    modal = document.createElement("div");
    modal.id = "spreadsheetPreviewModal";
    modal.className = "spreadsheetPreviewModal";
    modal.setAttribute("aria-hidden", "true");
    modal.innerHTML = `
      <div class="spreadsheetPreviewBox" role="dialog" aria-modal="false" aria-label="${tr("filemgr.spreadsheet.title", null, "Spreadsheet preview")}">
        <div class="spreadsheetPreviewHead">
          <div class="spreadsheetPreviewTitleWrap">
            <div id="spreadsheetPreviewTitle" class="spreadsheetPreviewTitle">${tr("filemgr.spreadsheet.title", null, "Spreadsheet preview")}</div>
            <div id="spreadsheetPreviewPath" class="spreadsheetPreviewPath mono"></div>
          </div>
          <div class="spreadsheetPreviewActions">
            <button id="spreadsheetPreviewEdit" type="button" class="btn secondary" hidden>${tr("filemgr.spreadsheet.edit", null, "Edit")}</button>
            <button id="spreadsheetPreviewDownload" type="button" class="btn secondary">${tr("filemgr.preview.download", null, "Download")}</button>
            <button id="spreadsheetPreviewClose" type="button" class="btn secondary">${tr("filemgr.preview.close", null, "Close")}</button>
          </div>
        </div>
        <div id="spreadsheetPreviewInfo" class="spreadsheetPreviewInfo">${tr("common.loading", null, "Loading…")}</div>
        <div id="spreadsheetPreviewTabs" class="spreadsheetPreviewTabs"></div>
        <div id="spreadsheetPreviewBody" class="spreadsheetPreviewBody"></div>
        <div class="spreadsheetPreviewResizeHandle spreadsheetPreviewResizeRight" data-spreadsheet-resize="right" aria-hidden="true"></div>
        <div class="spreadsheetPreviewResizeHandle spreadsheetPreviewResizeBottom" data-spreadsheet-resize="bottom" aria-hidden="true"></div>
        <div class="spreadsheetPreviewResizeHandle spreadsheetPreviewResizeCorner" data-spreadsheet-resize="corner" aria-hidden="true"></div>
      </div>
    `;
    document.body.appendChild(modal);

    titleEl = modal.querySelector("#spreadsheetPreviewTitle");
    pathEl = modal.querySelector("#spreadsheetPreviewPath");
    infoEl = modal.querySelector("#spreadsheetPreviewInfo");
    tabsEl = modal.querySelector("#spreadsheetPreviewTabs");
    bodyEl = modal.querySelector("#spreadsheetPreviewBody");
    downloadBtn = modal.querySelector("#spreadsheetPreviewDownload");
    editBtn = modal.querySelector("#spreadsheetPreviewEdit");

    editBtn?.addEventListener("click", openSpreadsheetEditor);
    modal.querySelector("#spreadsheetPreviewClose")?.addEventListener("click", close);
    modal.addEventListener("click", (ev) => {
      if (ev.target === modal) close();
    });

    document.addEventListener("keydown", (ev) => {
      if (ev.key === "Escape" && modal.classList.contains("show")) close();
    });

    const head = modal.querySelector(".spreadsheetPreviewHead");
    const box = modal.querySelector(".spreadsheetPreviewBox");
    head?.addEventListener("pointerdown", (ev) => {
      if (!box || ev.target.closest("button")) return;
      const r = box.getBoundingClientRect();
      dragState = { x: ev.clientX, y: ev.clientY, left: r.left, top: r.top };
      box.style.position = "absolute";
      box.style.left = `${r.left}px`;
      box.style.top = `${r.top}px`;
      box.style.transform = "none";
      try { head.setPointerCapture(ev.pointerId); } catch (_) {}
    });
    head?.addEventListener("pointermove", (ev) => {
      if (!dragState || !box) return;
      const pad = 8;
      const nextLeft = Math.max(pad, Math.min(window.innerWidth - 80, dragState.left + ev.clientX - dragState.x));
      const nextTop = Math.max(pad, Math.min(window.innerHeight - 60, dragState.top + ev.clientY - dragState.y));
      box.style.left = `${nextLeft}px`;
      box.style.top = `${nextTop}px`;
    });
    const endDrag = () => { dragState = null; };
    head?.addEventListener("pointerup", endDrag);
    head?.addEventListener("pointercancel", endDrag);

    for (const handle of modal.querySelectorAll("[data-spreadsheet-resize]")) {
      handle.addEventListener("pointerdown", (ev) => beginResize(ev, handle.dataset.spreadsheetResize || ""));
    }
    document.addEventListener("pointermove", moveResize);
    document.addEventListener("pointerup", endResize);
    document.addEventListener("pointercancel", endResize);
  }

  function show() {
    ensureModal();
    const box = modal.querySelector(".spreadsheetPreviewBox");
    if (box && !positionedOnce) {
      box.style.position = "";
      box.style.left = "";
      box.style.top = "";
      box.style.transform = "";
      positionedOnce = true;
    }
    modal.classList.add("show");
    modal.setAttribute("aria-hidden", "false");
  }

  function close() {
    if (!modal) return;
    openSeq++;
    modal.classList.remove("show");
    modal.setAttribute("aria-hidden", "true");
  }

  function setInfo(text, kind = "") {
    if (!infoEl) return;
    infoEl.className = `spreadsheetPreviewInfo${kind ? " " + kind : ""}`;
    infoEl.textContent = text || "";
  }

  function renderSheet(sheets, index) {
    if (!bodyEl || !tabsEl) return;

    const sheet = sheets[index] || { name: "Sheet", rows: [] };
    const normalized = normalizeRows(sheet.rows);

    tabsEl.innerHTML = "";
    sheets.forEach((s, i) => {
      const btn = document.createElement("button");
      btn.type = "button";
      btn.className = "spreadsheetPreviewTab" + (i === index ? " active" : "");
      btn.textContent = s.name || `Sheet ${i + 1}`;
      btn.addEventListener("click", () => renderSheet(sheets, i));
      tabsEl.appendChild(btn);
    });

    if (!normalized.rows.length || normalized.cols <= 0) {
      bodyEl.innerHTML = "";
      const empty = document.createElement("div");
      empty.className = "spreadsheetPreviewEmpty";
      empty.textContent = tr("filemgr.spreadsheet.empty", null, "This sheet is empty.");
      bodyEl.appendChild(empty);
      return;
    }

    bodyEl.innerHTML = "";
    const table = document.createElement("table");
    table.className = "spreadsheetPreviewTable";

    const thead = document.createElement("thead");
    const hr = document.createElement("tr");
    const corner = document.createElement("th");
    corner.className = "rowHead corner";
    corner.textContent = "";
    hr.appendChild(corner);

    for (let c = 0; c < normalized.cols; c++) {
      const th = document.createElement("th");
      th.textContent = columnName(c);
      hr.appendChild(th);
    }

    thead.appendChild(hr);
    table.appendChild(thead);

    const tbody = document.createElement("tbody");
    normalized.rows.forEach((row, rIdx) => {
      const trEl = document.createElement("tr");
      const rh = document.createElement("th");
      rh.className = "rowHead";
      rh.textContent = String(rIdx + 1);
      trEl.appendChild(rh);

      for (let c = 0; c < normalized.cols; c++) {
        const td = document.createElement("td");
        // Security: always render cell values as text, never HTML.
        td.textContent = row[c] == null ? "" : String(row[c]);
        trEl.appendChild(td);
      }

      tbody.appendChild(trEl);
    });

    table.appendChild(tbody);
    bodyEl.appendChild(table);

    const notes = [];
    notes.push(`${normalized.rows.length} × ${normalized.cols}`);
    if (normalized.truncatedRows || normalized.truncatedCols) {
      notes.push(tr("filemgr.spreadsheet.truncated", null, "large sheet truncated for preview"));
    }
    setInfo(notes.join(" · "));
  }

  async function open(item) {
    if (!canOpenFor(item)) {
      if (previousOfficePreview && typeof previousOfficePreview.open === "function") {
        return previousOfficePreview.open(item);
      }
      return;
    }

    ensureModal();

    const seq = ++openSeq;
    const rel = relPathFor(item);
    const ext = fileExtLower(item.name || rel);
    const downloadUrl = getDownloadUrl(rel);

    currentEditContext = {
      item,
      rel,
      name: safeName(item),
      url: downloadUrl,
      ext
    };

    if (titleEl) titleEl.textContent = tr("filemgr.spreadsheet.title", null, "Spreadsheet preview");
    if (pathEl) pathEl.textContent = "/" + rel;
    if (tabsEl) tabsEl.innerHTML = "";
    if (bodyEl) bodyEl.innerHTML = "";
    if (downloadBtn) downloadBtn.onclick = () => { window.location.href = downloadUrl; };
    if (editBtn) {
      const editable = canEditFor(item);
      editBtn.hidden = !editable;
      editBtn.disabled = !editable;
      editBtn.title = tr("filemgr.spreadsheet.edit_title", null, "Edit spreadsheet");
    }

    show();
    setInfo(tr("common.loading", null, "Loading…"));

    const status = FM && typeof FM.getStatusEl === "function" ? FM.getStatusEl() : null;
    if (FM && typeof FM.setBadge === "function") FM.setBadge("warn", "preview");
    if (status) status.textContent = tr("filemgr.spreadsheet.loading", { name: safeName(item) }, `Loading spreadsheet preview: ${safeName(item)}…`);

    try {
      const sheets = await readWorkbookRows(downloadUrl, ext);
      if (seq !== openSeq) return;

      if (!sheets.length) {
        if (bodyEl) {
          bodyEl.innerHTML = "";
          const empty = document.createElement("div");
          empty.className = "spreadsheetPreviewEmpty";
          empty.textContent = tr("filemgr.spreadsheet.empty_workbook", null, "No sheets found.");
          bodyEl.appendChild(empty);
        }
        setInfo(tr("filemgr.spreadsheet.empty_workbook", null, "No sheets found."));
        return;
      }

      renderSheet(sheets, 0);
      if (FM && typeof FM.setBadge === "function") FM.setBadge("ok", "preview");
      if (status) status.textContent = tr("filemgr.spreadsheet.previewing", { name: safeName(item) }, `Previewing spreadsheet: ${safeName(item)}`);
    } catch (e) {
      if (seq !== openSeq) return;
      const msg = String(e && e.message ? e.message : e);
      if (bodyEl) {
        bodyEl.innerHTML = "";
        const err = document.createElement("div");
        err.className = "spreadsheetPreviewError";
        err.textContent = msg;
        bodyEl.appendChild(err);
      }
      setInfo(tr("filemgr.spreadsheet.failed", { error: msg }, `Spreadsheet preview failed: ${msg}`));
      if (FM && typeof FM.setBadge === "function") FM.setBadge("err", "preview");
      if (status) status.textContent = tr("filemgr.spreadsheet.failed", { error: msg }, `Spreadsheet preview failed: ${msg}`);
    }
  }

  FM.spreadsheetPreview = {
    open,
    canOpenFor,
    isSpreadsheetName
  };

  // Keep app.js small: existing app.js already routes Office files through
  // FM.officePreview. This wrapper intercepts spreadsheet files and leaves
  // documents/presentations to the original Office-to-PDF preview.
  FM.officePreview = {
    open(item) {
      if (canOpenFor(item)) return open(item);
      if (previousOfficePreview && typeof previousOfficePreview.open === "function") {
        return previousOfficePreview.open(item);
      }
    },
    canOpenFor(item) {
      return canOpenFor(item) ||
        !!(previousOfficePreview &&
           typeof previousOfficePreview.canOpenFor === "function" &&
           previousOfficePreview.canOpenFor(item));
    }
  };
})();
