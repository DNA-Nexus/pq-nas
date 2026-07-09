window.PQNAS_FILEMGR = window.PQNAS_FILEMGR || {};

(() => {
  "use strict";

  const FM = window.PQNAS_FILEMGR;
  const XLSX_VENDOR_URL = "./vendor/xlsx.full.min.js";
  const XLSX_MIME = "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";

  const MAX_EDIT_ROWS = 200;
  const MAX_EDIT_COLS = 50;
  const DEFAULT_ROWS = 10;
  const DEFAULT_COLS = 5;

  let modal = null;
  let titleEl = null;
  let pathEl = null;
  let infoEl = null;
  let tabsEl = null;
  let bodyEl = null;
  let saveBtn = null;
  let addRowBtn = null;
  let addColBtn = null;
  let closeBtn = null;
  let xlsxLoadPromise = null;

  const state = {
    rel: "",
    name: "",
    url: "",
    ext: "",
    sheets: [],
    active: 0,
    dirty: false,
    saving: false,
    readOnly: false,
    tooLarge: false
  };

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

  function safeSheetName(name, idx) {
    const raw = String(name || `Sheet${idx + 1}`).trim() || `Sheet${idx + 1}`;
    return raw.replace(/[:\\/?*\[\]]/g, "-").slice(0, 31) || `Sheet${idx + 1}`;
  }

  function ensureXlsxLibrary() {
    if (window.XLSX && typeof window.XLSX.read === "function" && typeof window.XLSX.write === "function") {
      return Promise.resolve(window.XLSX);
    }

    if (xlsxLoadPromise) return xlsxLoadPromise;

    xlsxLoadPromise = new Promise((resolve, reject) => {
      const existing = document.querySelector('script[data-pqnas-xlsx-lib="1"]');
      if (existing) {
        existing.addEventListener("load", () => resolve(window.XLSX), { once: true });
        existing.addEventListener("error", () => reject(new Error("XLSX library failed to load.")), { once: true });
        window.setTimeout(() => {
          if (window.XLSX && typeof window.XLSX.read === "function" && typeof window.XLSX.write === "function") {
            resolve(window.XLSX);
          }
        }, 0);
        return;
      }

      const script = document.createElement("script");
      script.src = XLSX_VENDOR_URL;
      script.defer = true;
      script.dataset.pqnasXlsxLib = "1";
      script.onload = () => {
        if (window.XLSX && typeof window.XLSX.read === "function" && typeof window.XLSX.write === "function") {
          resolve(window.XLSX);
        } else {
          reject(new Error("XLSX library loaded, but required APIs are missing."));
        }
      };
      script.onerror = () => reject(new Error("XLSX library is not installed."));
      document.head.appendChild(script);
    });

    return xlsxLoadPromise;
  }

  function normalizeEditableRows(rows) {
    const src = Array.isArray(rows) ? rows : [];
    let maxCols = 0;

    for (const row of src) {
      if (Array.isArray(row)) maxCols = Math.max(maxCols, row.length);
    }

    const tooLarge = src.length > MAX_EDIT_ROWS || maxCols > MAX_EDIT_COLS;
    const rowCount = Math.max(Math.min(src.length, MAX_EDIT_ROWS), DEFAULT_ROWS);
    const colCount = Math.max(Math.min(maxCols, MAX_EDIT_COLS), DEFAULT_COLS);

    const out = [];
    for (let r = 0; r < rowCount; r++) {
      const sourceRow = Array.isArray(src[r]) ? src[r] : [];
      const nextRow = [];
      for (let c = 0; c < colCount; c++) {
        const v = sourceRow[c];
        nextRow.push(v == null ? "" : String(v));
      }
      out.push(nextRow);
    }

    return { rows: out, tooLarge };
  }

  async function readWorkbook(ctx) {
    if (fileExtLower(ctx.name || ctx.rel || "") !== "xlsx") {
      throw new Error(tr("filemgr.spreadsheet_editor.xlsx_only", null, "Only .xlsx editing is supported in this first version."));
    }

    const r = await fetch(ctx.url, { credentials: "include", cache: "no-store" });
    if (!r.ok) {
      const msg = await r.text().catch(() => "");
      throw new Error(msg || `HTTP ${r.status}`);
    }

    const XLSX = await ensureXlsxLibrary();
    const buf = await r.arrayBuffer();

    // Security: parse workbook content as inert cell data only. Do not execute
    // formulas, macros, links or embedded active content.
    const wb = XLSX.read(buf, {
      type: "array",
      cellFormula: false,
      cellHTML: false,
      cellNF: false,
      cellStyles: false
    });

    const names = Array.isArray(wb.SheetNames) ? wb.SheetNames : [];
    if (!names.length) {
      return [{
        name: tr("filemgr.spreadsheet_create.sheet.sheet1", null, "Sheet1"),
        rows: Array.from({ length: DEFAULT_ROWS }, () => Array.from({ length: DEFAULT_COLS }, () => ""))
      }];
    }

    let anyTooLarge = false;
    const sheets = names.map((name, idx) => {
      const ws = wb.Sheets[name];
      const rawRows = XLSX.utils.sheet_to_json(ws, {
        header: 1,
        raw: false,
        defval: "",
        blankrows: true
      });

      const normalized = normalizeEditableRows(rawRows);
      anyTooLarge = anyTooLarge || normalized.tooLarge;
      return { name: safeSheetName(name, idx), rows: normalized.rows };
    });

    state.tooLarge = anyTooLarge;
    return sheets;
  }

  function coerceCellValue(value) {
    const s = String(value == null ? "" : value);
    const trimmed = s.trim();
    if (!trimmed) return "";

    // Convenience only: keep ordinary text as text, but preserve simple numeric
    // spreadsheet values when users type numbers into simple DNA-Nexus sheets.
    if (/^-?\d+(?:[.,]\d+)?$/.test(trimmed)) {
      const n = Number(trimmed.replace(",", "."));
      if (Number.isFinite(n)) return n;
    }

    return s;
  }

  function makeOutputFile(XLSX) {
    const wb = XLSX.utils.book_new();

    for (let i = 0; i < state.sheets.length; i++) {
      const sheet = state.sheets[i];
      const rows = Array.isArray(sheet.rows) ? sheet.rows : [];
      const aoa = rows.map((row) => (Array.isArray(row) ? row : []).map(coerceCellValue));
      const ws = XLSX.utils.aoa_to_sheet(aoa);
      XLSX.utils.book_append_sheet(wb, ws, safeSheetName(sheet.name, i));
    }

    const out = XLSX.write(wb, { bookType: "xlsx", type: "array" });
    const blob = new Blob([out], { type: XLSX_MIME });

    try {
      return new File([blob], state.name || "spreadsheet.xlsx", { type: XLSX_MIME, lastModified: Date.now() });
    } catch (_) {
      blob.name = state.name || "spreadsheet.xlsx";
      blob.lastModified = Date.now();
      return blob;
    }
  }

  function setStatus(text, kind = "") {
    if (!infoEl) return;
    infoEl.textContent = String(text || "");
    infoEl.dataset.kind = kind || "";
  }

  function setDirty(on) {
    state.dirty = !!on;
    updateButtons();

    if (state.dirty) {
      setStatus(tr("filemgr.spreadsheet_editor.dirty_status", null, "Unsaved changes."), "warn");
    } else if (!state.tooLarge) {
      setStatus(tr("filemgr.spreadsheet_editor.ready_status", null, "Ready to edit."), "");
    }
  }

  function updateButtons() {
    const disabled = state.saving || state.readOnly || state.tooLarge;

    if (saveBtn) {
      saveBtn.disabled = disabled || !state.dirty;
      saveBtn.textContent = state.saving
        ? tr("filemgr.spreadsheet_editor.saving", null, "Saving…")
        : tr("filemgr.spreadsheet_editor.save", null, "Save");
    }

    if (addRowBtn) addRowBtn.disabled = disabled;
    if (addColBtn) addColBtn.disabled = disabled;
  }

  function ensureModal() {
    if (modal) return;

    modal = document.createElement("div");
    modal.className = "spreadsheetEditorModal";
    modal.setAttribute("aria-hidden", "true");

    modal.innerHTML = `
      <div class="spreadsheetEditorBox" role="dialog" aria-modal="true" aria-label="${tr("filemgr.spreadsheet_editor.title", null, "Edit spreadsheet")}">
        <div class="spreadsheetEditorHead">
          <div>
            <div id="spreadsheetEditorTitle" class="spreadsheetEditorTitle">${tr("filemgr.spreadsheet_editor.title", null, "Edit spreadsheet")}</div>
            <div id="spreadsheetEditorPath" class="spreadsheetEditorPath mono"></div>
          </div>
          <div class="spreadsheetEditorActions">
            <button id="spreadsheetEditorAddRow" type="button" class="btn secondary">${tr("filemgr.spreadsheet_editor.add_row", null, "Add row")}</button>
            <button id="spreadsheetEditorAddCol" type="button" class="btn secondary">${tr("filemgr.spreadsheet_editor.add_column", null, "Add column")}</button>
            <button id="spreadsheetEditorSave" type="button" class="btn">${tr("filemgr.spreadsheet_editor.save", null, "Save")}</button>
            <button id="spreadsheetEditorClose" type="button" class="btn secondary">${tr("filemgr.close", null, "Close")}</button>
          </div>
        </div>
        <div id="spreadsheetEditorInfo" class="spreadsheetEditorInfo">${tr("common.loading", null, "Loading…")}</div>
        <div id="spreadsheetEditorTabs" class="spreadsheetEditorTabs"></div>
        <div id="spreadsheetEditorBody" class="spreadsheetEditorBody"></div>
      </div>
    `;

    document.body.appendChild(modal);

    titleEl = modal.querySelector("#spreadsheetEditorTitle");
    pathEl = modal.querySelector("#spreadsheetEditorPath");
    infoEl = modal.querySelector("#spreadsheetEditorInfo");
    tabsEl = modal.querySelector("#spreadsheetEditorTabs");
    bodyEl = modal.querySelector("#spreadsheetEditorBody");
    saveBtn = modal.querySelector("#spreadsheetEditorSave");
    addRowBtn = modal.querySelector("#spreadsheetEditorAddRow");
    addColBtn = modal.querySelector("#spreadsheetEditorAddCol");
    closeBtn = modal.querySelector("#spreadsheetEditorClose");

    saveBtn?.addEventListener("click", saveCurrent);
    addRowBtn?.addEventListener("click", addRow);
    addColBtn?.addEventListener("click", addColumn);
    closeBtn?.addEventListener("click", close);

    modal.addEventListener("click", (ev) => {
      if (ev.target === modal) close();
    });

    document.addEventListener("keydown", (ev) => {
      if (!modal.classList.contains("show")) return;
      if ((ev.ctrlKey || ev.metaKey) && ev.key.toLowerCase() === "s") {
        ev.preventDefault();
        saveCurrent();
      }
      if (ev.key === "Escape") close();
    });
  }

  function show() {
    ensureModal();
    modal.classList.add("show");
    modal.setAttribute("aria-hidden", "false");
  }

  function close() {
    if (!modal || !modal.classList.contains("show")) return;

    if (state.dirty && !window.confirm(tr("filemgr.spreadsheet_editor.discard_close", null, "Discard unsaved spreadsheet changes?"))) {
      return;
    }

    modal.classList.remove("show");
    modal.setAttribute("aria-hidden", "true");
  }

  function renderTabs() {
    if (!tabsEl) return;
    tabsEl.replaceChildren();

    state.sheets.forEach((sheet, idx) => {
      const btn = document.createElement("button");
      btn.type = "button";
      btn.className = "spreadsheetEditorTab" + (idx === state.active ? " active" : "");
      btn.textContent = sheet.name || `Sheet ${idx + 1}`;
      btn.addEventListener("click", () => {
        state.active = idx;
        render();
      });
      tabsEl.appendChild(btn);
    });
  }

  function render() {
    if (!bodyEl) return;
    renderTabs();
    bodyEl.replaceChildren();

    const sheet = state.sheets[state.active] || { rows: [] };
    const rows = Array.isArray(sheet.rows) ? sheet.rows : [];
    const colCount = rows.reduce((m, row) => Math.max(m, Array.isArray(row) ? row.length : 0), 0);

    const table = document.createElement("table");
    table.className = "spreadsheetEditorTable";
    table.setAttribute("aria-label", tr("filemgr.spreadsheet_editor.cell_grid", null, "Editable spreadsheet cells"));

    const thead = document.createElement("thead");
    const headRow = document.createElement("tr");
    const corner = document.createElement("th");
    corner.className = "rowHead corner";
    headRow.appendChild(corner);

    for (let c = 0; c < colCount; c++) {
      const th = document.createElement("th");
      th.textContent = columnName(c);
      headRow.appendChild(th);
    }

    thead.appendChild(headRow);
    table.appendChild(thead);

    const tbody = document.createElement("tbody");

    rows.forEach((row, rIdx) => {
      const trEl = document.createElement("tr");
      const rh = document.createElement("th");
      rh.className = "rowHead";
      rh.textContent = String(rIdx + 1);
      trEl.appendChild(rh);

      for (let c = 0; c < colCount; c++) {
        const td = document.createElement("td");
        const input = document.createElement("input");

        input.type = "text";
        input.value = row[c] == null ? "" : String(row[c]);
        input.dataset.row = String(rIdx);
        input.dataset.col = String(c);
        input.disabled = state.readOnly || state.tooLarge;

        input.addEventListener("input", () => {
          const r = Number(input.dataset.row);
          const col = Number(input.dataset.col);
          if (!Number.isInteger(r) || !Number.isInteger(col)) return;
          if (!state.sheets[state.active] || !state.sheets[state.active].rows[r]) return;
          state.sheets[state.active].rows[r][col] = input.value;
          setDirty(true);
        });

        // Security: cell content is edited through input.value and never
        // injected as HTML, so workbook text cannot become executable markup.
        td.appendChild(input);
        trEl.appendChild(td);
      }

      tbody.appendChild(trEl);
    });

    table.appendChild(tbody);
    bodyEl.appendChild(table);
    updateButtons();

    if (state.tooLarge) {
      setStatus(tr(
        "filemgr.spreadsheet_editor.too_large",
        { rows: MAX_EDIT_ROWS, cols: MAX_EDIT_COLS },
        `This spreadsheet is larger than the safe editor limit (${MAX_EDIT_ROWS} rows × ${MAX_EDIT_COLS} columns). Preview only.`
      ), "warn");
    } else if (state.readOnly) {
      setStatus(tr("filemgr.spreadsheet_editor.read_only", null, "This location is read-only."), "warn");
    } else if (!state.dirty) {
      setStatus(tr("filemgr.spreadsheet_editor.ready_status", null, "Ready to edit."), "");
    }
  }

  function addRow() {
    if (state.readOnly || state.tooLarge) return;
    const sheet = state.sheets[state.active];
    if (!sheet) return;

    if (sheet.rows.length >= MAX_EDIT_ROWS) {
      setStatus(tr("filemgr.spreadsheet_editor.row_limit", null, "Row limit reached."), "warn");
      return;
    }

    const cols = sheet.rows.reduce((m, row) => Math.max(m, Array.isArray(row) ? row.length : 0), DEFAULT_COLS);
    sheet.rows.push(Array.from({ length: cols }, () => ""));
    setDirty(true);
    render();
  }

  function addColumn() {
    if (state.readOnly || state.tooLarge) return;
    const sheet = state.sheets[state.active];
    if (!sheet) return;

    const cols = sheet.rows.reduce((m, row) => Math.max(m, Array.isArray(row) ? row.length : 0), 0);
    if (cols >= MAX_EDIT_COLS) {
      setStatus(tr("filemgr.spreadsheet_editor.col_limit", null, "Column limit reached."), "warn");
      return;
    }

    for (const row of sheet.rows) row.push("");
    if (!sheet.rows.length) {
      sheet.rows.push(Array.from({ length: 1 }, () => ""));
    }

    setDirty(true);
    render();
  }

  async function saveCurrent() {
    if (state.saving || state.readOnly || state.tooLarge || !state.dirty) return;

    if (!FM || typeof FM.saveGeneratedFileOverwrite !== "function") {
      setStatus(tr("filemgr.spreadsheet_editor.save_helper_missing", null, "Spreadsheet save helper is not ready."), "err");
      return;
    }

    state.saving = true;
    updateButtons();
    setStatus(tr("filemgr.spreadsheet_editor.saving", null, "Saving…"), "warn");

    try {
      const XLSX = await ensureXlsxLibrary();
      const file = makeOutputFile(XLSX);
      await FM.saveGeneratedFileOverwrite(file, state.rel);

      state.dirty = false;
      setStatus(tr("filemgr.spreadsheet_editor.saved", null, "Saved."), "ok");

      const globalStatus = FM && typeof FM.getStatusEl === "function" ? FM.getStatusEl() : null;
      if (globalStatus) {
        globalStatus.textContent = tr("filemgr.spreadsheet_editor.saved_file", { path: state.rel }, `Saved spreadsheet: ${state.rel}`);
      }
    } catch (e) {
      const msg = String(e && e.message ? e.message : e);
      setStatus(tr("filemgr.spreadsheet_editor.save_failed", { error: msg }, `Save failed: ${msg}`), "err");
    } finally {
      state.saving = false;
      updateButtons();
    }
  }

  async function open(ctx) {
    ensureModal();

    const rel = String(ctx && ctx.rel ? ctx.rel : "");
    const name = String(ctx && ctx.name ? ctx.name : rel.split("/").pop() || "spreadsheet.xlsx");
    const url = String(ctx && ctx.url ? ctx.url : "");

    state.rel = rel;
    state.name = name;
    state.url = url;
    state.ext = fileExtLower(name || rel);
    state.sheets = [];
    state.active = 0;
    state.dirty = false;
    state.saving = false;
    state.readOnly = !!(FM && typeof FM.canWriteCurrentScope === "function" && !FM.canWriteCurrentScope());
    state.tooLarge = false;

    if (titleEl) titleEl.textContent = tr("filemgr.spreadsheet_editor.title", null, "Edit spreadsheet");
    if (pathEl) pathEl.textContent = "/" + rel;
    if (bodyEl) bodyEl.replaceChildren();
    if (tabsEl) tabsEl.replaceChildren();

    show();
    setStatus(tr("filemgr.spreadsheet_editor.loading", null, "Loading spreadsheet editor…"), "warn");
    updateButtons();

    try {
      state.sheets = await readWorkbook({ rel, name, url });
      render();
    } catch (e) {
      const msg = String(e && e.message ? e.message : e);
      if (bodyEl) {
        bodyEl.replaceChildren();
        const err = document.createElement("div");
        err.className = "spreadsheetEditorError";
        err.textContent = msg;
        bodyEl.appendChild(err);
      }
      setStatus(tr("filemgr.spreadsheet_editor.failed", { error: msg }, `Spreadsheet editor failed: ${msg}`), "err");
    }
  }

  FM.spreadsheetEdit = {
    open
  };
})();
