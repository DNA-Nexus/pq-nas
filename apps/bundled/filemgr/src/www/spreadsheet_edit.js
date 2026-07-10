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
  const DEFAULT_COL_WIDTH = 120;
  const MIN_COL_WIDTH = 72;
  const MAX_COL_WIDTH = 520;
  const STYLE_SHEET_NAME = "_pqnas_styles";
  const STYLE_META_VERSION = "pqnas-spreadsheet-style-v1";
  const STYLE_META_CHUNK_SIZE = 30000;

  // Document content colors: these are spreadsheet cell fill values, not
  // DNA-Nexus UI theme colors. Fixed values are intentional for XLSX output.
  const CELL_FILL_COLORS = Object.freeze({
    yellow: { css: "rgb(255, 242, 204)", rgb: "FFFFF2CC" },
    green: { css: "rgb(217, 234, 211)", rgb: "FFD9EAD3" },
    blue: { css: "rgb(207, 226, 243)", rgb: "FFCFE2F3" },
    red: { css: "rgb(244, 204, 204)", rgb: "FFF4CCCC" },
    gray: { css: "rgb(217, 217, 217)", rgb: "FFD9D9D9" }
  });

  // Document content colors for spreadsheet text/font color.
  const TEXT_COLOR_COLORS = Object.freeze({
    black: { css: "rgb(0, 0, 0)", rgb: "FF000000" },
    red: { css: "rgb(204, 0, 0)", rgb: "FFCC0000" },
    green: { css: "rgb(56, 118, 29)", rgb: "FF38761D" },
    blue: { css: "rgb(17, 85, 204)", rgb: "FF1155CC" },
    gray: { css: "rgb(102, 102, 102)", rgb: "FF666666" },
    white: { css: "rgb(255, 255, 255)", rgb: "FFFFFFFF" }
  });

  const FONT_SIZE_OPTIONS = Object.freeze([10, 12, 14, 16, 18, 24, 32]);
  const BORDER_STYLE_KEYS = Object.freeze(["thin", "thick"]);
  const BORDER_SIDES = Object.freeze(["top", "right", "bottom", "left"]);
  const BORDER_XLSX_COLOR = "FF000000";

  let modal = null;
  let titleEl = null;
  let pathEl = null;
  let infoEl = null;
  let tabsEl = null;
  let bodyEl = null;
  let saveBtn = null;
  let addRowBtn = null;
  let addColBtn = null;
  let boldBtn = null;
  let italicBtn = null;
  let underlineBtn = null;
  let fontSizeSelect = null;
  let alignLeftBtn = null;
  let alignCenterBtn = null;
  let textColorBtn = null;
  let fillBtn = null;
  let closeBtn = null;
  let confirmModal = null;
  let axisMenu = null;
  let fillMenu = null;
  let textColorMenu = null;
  let borderMenu = null;
  let xlsxLoadPromise = null;
  let formulaFocus = null;

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
    tooLarge: false,
    selection: null,
    activeCell: null,
    rangeSelection: null,
    editorGeometry: null
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

  function isValidTextColorKey(value) {
    return Object.prototype.hasOwnProperty.call(TEXT_COLOR_COLORS, String(value || ""));
  }

  function normalizeTextColorKey(value) {
    const key = String(value || "").trim();
    return isValidTextColorKey(key) ? key : "";
  }

  function cellTextColorKeyFromRgb(value) {
    const raw = String(value || "").replace(/^#/, "").toUpperCase();
    const normalized = raw.length === 6 ? "FF" + raw : raw;

    for (const [key, def] of Object.entries(TEXT_COLOR_COLORS)) {
      if (String(def.rgb || "").toUpperCase() === normalized) return key;
    }

    return "";
  }

  function isValidFillColorKey(value) {
    return Object.prototype.hasOwnProperty.call(CELL_FILL_COLORS, String(value || ""));
  }

  function normalizeFillColorKey(value) {
    const key = String(value || "").trim();
    return isValidFillColorKey(key) ? key : "";
  }

  function cellFillKeyFromRgb(value) {
    const raw = String(value || "").replace(/^#/, "").toUpperCase();
    const normalized = raw.length === 6 ? "FF" + raw : raw;

    for (const [key, def] of Object.entries(CELL_FILL_COLORS)) {
      if (String(def.rgb || "").toUpperCase() === normalized) return key;
    }

    return "";
  }

  function normalizeFontSize(value) {
    const n = Number(value);
    if (!Number.isFinite(n)) return 0;
    const rounded = Math.round(n);
    return FONT_SIZE_OPTIONS.includes(rounded) ? rounded : 0;
  }

  function normalizeBorderSide(value) {
    const key = String(value || "").trim();
    return BORDER_STYLE_KEYS.includes(key) ? key : "";
  }

  function normalizeBorderFormat(border) {
    const src = border && typeof border === "object" ? border : {};
    return {
      top: normalizeBorderSide(src.top),
      right: normalizeBorderSide(src.right),
      bottom: normalizeBorderSide(src.bottom),
      left: normalizeBorderSide(src.left)
    };
  }

  function isEmptyBorderFormat(border) {
    const b = normalizeBorderFormat(border);
    return !b.top && !b.right && !b.bottom && !b.left;
  }

  function borderCssWidth(style) {
    return style === "thick" ? "3px" : style === "thin" ? "1px" : "";
  }

  function borderXlsxStyle(style) {
    if (style === "thick") return "medium";
    if (style === "thin") return "thin";
    return "";
  }

  function cellBorderSideFromXlsx(side) {
    const style = String(side && side.style || "").toLowerCase();
    if (!style || style === "none") return "";
    return style === "medium" || style === "thick" ? "thick" : "thin";
  }

  function cellBorderFromXlsxStyle(border) {
    const src = border && typeof border === "object" ? border : {};
    return normalizeBorderFormat({
      top: cellBorderSideFromXlsx(src.top),
      right: cellBorderSideFromXlsx(src.right),
      bottom: cellBorderSideFromXlsx(src.bottom),
      left: cellBorderSideFromXlsx(src.left)
    });
  }

  function applyBorderFormatToCell(cell, border) {
    if (!cell) return;

    const b = normalizeBorderFormat(border);
    const sideMap = { top: "Top", right: "Right", bottom: "Bottom", left: "Left" };

    let hasBorder = false;
    for (const side of BORDER_SIDES) {
      const cssSide = sideMap[side];
      const width = borderCssWidth(b[side]);
      const prop = `border${cssSide}`;

      if (width) {
        hasBorder = true;
        cell.style[prop] = `${width} solid var(--spreadsheet-cell-border-color, currentColor)`;
      } else {
        cell.style.removeProperty(`border-${side}`);
      }
    }

    if (hasBorder) {
      cell.dataset.spreadsheetCellBorder = "1";
    } else {
      cell.removeAttribute("data-spreadsheet-cell-border");
    }
  }

  function outputBorderStyle(border) {
    const b = normalizeBorderFormat(border);
    if (isEmptyBorderFormat(b)) return null;

    const out = {};
    for (const side of BORDER_SIDES) {
      const style = borderXlsxStyle(b[side]);
      if (!style) continue;
      out[side] = {
        style,
        color: { rgb: BORDER_XLSX_COLOR }
      };
    }

    return Object.keys(out).length ? out : null;
  }

  function normalizeCellFormat(fmt) {
    const src = fmt && typeof fmt === "object" ? fmt : {};
    const align = src.align === "center" || src.align === "left" ? src.align : "";
    return {
      bold: !!src.bold,
      italic: !!src.italic,
      underline: !!src.underline,
      fontSize: normalizeFontSize(src.fontSize || src.sz),
      align,
      bg: normalizeFillColorKey(src.bg),
      fg: normalizeTextColorKey(src.fg),
      border: normalizeBorderFormat(src.border)
    };
  }

  function isEmptyCellFormat(fmt) {
    const f = normalizeCellFormat(fmt);
    return !f.bold && !f.italic && !f.underline && !f.fontSize && !f.align && !f.bg && !f.fg && isEmptyBorderFormat(f.border);
  }

  function ensureSheetCellFormats(sheet, rowCount = null, colCount = null) {
    if (!sheet) return [];

    const rows = Array.isArray(sheet.rows) ? sheet.rows : [];
    const effectiveRows = Number.isInteger(rowCount) ? rowCount : rows.length;
    const effectiveCols = Number.isInteger(colCount)
      ? colCount
      : rows.reduce((m, row) => Math.max(m, Array.isArray(row) ? row.length : 0), 0);

    if (!Array.isArray(sheet.cellFormats)) {
      sheet.cellFormats = [];
    }

    while (sheet.cellFormats.length < effectiveRows) {
      sheet.cellFormats.push([]);
    }

    for (let r = 0; r < sheet.cellFormats.length; r++) {
      if (!Array.isArray(sheet.cellFormats[r])) {
        sheet.cellFormats[r] = [];
      }

      while (sheet.cellFormats[r].length < effectiveCols) {
        sheet.cellFormats[r].push(null);
      }

      for (let c = 0; c < sheet.cellFormats[r].length; c++) {
        const normalized = normalizeCellFormat(sheet.cellFormats[r][c]);
        sheet.cellFormats[r][c] = isEmptyCellFormat(normalized) ? null : normalized;
      }
    }

    return sheet.cellFormats;
  }

  function getCellFormat(sheet, row, col) {
    if (!sheet || !Number.isInteger(row) || !Number.isInteger(col) || row < 0 || col < 0) {
      return normalizeCellFormat(null);
    }

    ensureSheetCellFormats(sheet, row + 1, col + 1);
    return normalizeCellFormat(sheet.cellFormats[row] && sheet.cellFormats[row][col]);
  }

  function setCellFormat(sheet, row, col, fmt) {
    if (!sheet || !Number.isInteger(row) || !Number.isInteger(col) || row < 0 || col < 0) {
      return;
    }

    ensureSheetCellFormats(sheet, row + 1, col + 1);
    const normalized = normalizeCellFormat(fmt);
    sheet.cellFormats[row][col] = isEmptyCellFormat(normalized) ? null : normalized;
  }

  function applyCellFormatToInput(input, fmt) {
    if (!input) return;

    const cell = input.closest ? input.closest("td[data-row][data-col]") : null;
    const f = normalizeCellFormat(fmt);

    input.style.fontWeight = f.bold ? "700" : "";
    input.style.fontStyle = f.italic ? "italic" : "";
    input.style.textDecoration = f.underline ? "underline" : "";
    input.style.fontSize = f.fontSize ? `${f.fontSize}px` : "";
    input.style.textAlign = f.align || "";

    // The td owns spreadsheet cell fill and borders. This prevents visual
    // formatting from covering only the input-sized part of a wider table cell.
    input.removeAttribute("data-spreadsheet-cell-bg");
    input.style.removeProperty("--spreadsheet-cell-bg");

    if (cell) {
      if (f.bg && CELL_FILL_COLORS[f.bg]) {
        cell.dataset.spreadsheetCellBg = "1";
        cell.style.setProperty("--spreadsheet-cell-bg", CELL_FILL_COLORS[f.bg].css);
      } else {
        cell.removeAttribute("data-spreadsheet-cell-bg");
        cell.style.removeProperty("--spreadsheet-cell-bg");
      }

      applyBorderFormatToCell(cell, f.border);
    }

    if (f.fg && TEXT_COLOR_COLORS[f.fg]) {
      input.dataset.spreadsheetCellFg = "1";
      input.style.setProperty("--spreadsheet-cell-fg", TEXT_COLOR_COLORS[f.fg].css);
      input.style.color = TEXT_COLOR_COLORS[f.fg].css;
    } else {
      input.removeAttribute("data-spreadsheet-cell-fg");
      input.style.removeProperty("--spreadsheet-cell-fg");
      input.style.color = "";
    }
  }

  function formatTargetCells() {
    const sheet = state.sheets[state.active];
    if (!sheet || !Array.isArray(sheet.rows)) return [];

    const rowCount = sheet.rows.length;
    const colCount = sheet.rows.reduce((m, row) => Math.max(m, Array.isArray(row) ? row.length : 0), 0);

    const range = normalizedRangeSelection();
    if (range) {
      const cells = [];
      for (let row = range.row1; row <= range.row2; row++) {
        for (let col = range.col1; col <= range.col2; col++) {
          if (row >= 0 && row < rowCount && col >= 0 && col < colCount) {
            cells.push({ row, col });
          }
        }
      }
      return cells;
    }

    if (state.selection && state.selection.type === "row") {
      const row = Number(state.selection.index);
      if (!Number.isInteger(row) || row < 0 || row >= rowCount) return [];
      return Array.from({ length: colCount }, (_v, col) => ({ row, col }));
    }

    if (state.selection && state.selection.type === "column") {
      const col = Number(state.selection.index);
      if (!Number.isInteger(col) || col < 0 || col >= colCount) return [];
      return Array.from({ length: rowCount }, (_v, row) => ({ row, col }));
    }

    if (state.activeCell) {
      const row = Number(state.activeCell.row);
      const col = Number(state.activeCell.col);
      if (Number.isInteger(row) && Number.isInteger(col) && row >= 0 && col >= 0 && row < rowCount && col < colCount) {
        return [{ row, col }];
      }
    }

    return [];
  }

  function paintVisibleCellFormat(row, col) {
    if (!bodyEl) return;

    const input = bodyEl.querySelector(`input[data-row="${row}"][data-col="${col}"]`);
    const sheet = state.sheets[state.active];
    if (!input || !sheet) return;

    applyCellFormatToInput(input, getCellFormat(sheet, row, col));
  }

  function formatTargetsContainCell(row, col) {
    return formatTargetCells().some((cell) => cell.row === row && cell.col === col);
  }

  function applyBorderCommand(action, style = "thin") {
    if (state.readOnly || state.tooLarge) return;

    const sheet = state.sheets[state.active];
    const cells = formatTargetCells();
    if (!sheet || !cells.length) {
      setStatus(tr("filemgr.spreadsheet_editor.select_cell_first", null, "Select a cell, row or column first."), "warn");
      return;
    }

    const borderStyle = normalizeBorderSide(style) || "thin";
    const rows = cells.map((cell) => cell.row);
    const cols = cells.map((cell) => cell.col);
    const row1 = Math.min(...rows);
    const row2 = Math.max(...rows);
    const col1 = Math.min(...cols);
    const col2 = Math.max(...cols);

    for (const { row, col } of cells) {
      const fmt = getCellFormat(sheet, row, col);
      const border = normalizeBorderFormat(fmt.border);

      // Border menu actions replace the custom borders for the target cells.
      for (const side of BORDER_SIDES) {
        border[side] = "";
      }

      if (action === "all") {
        // Draw all grid lines without doubling internal left/top borders.
        border.top = row === row1 ? borderStyle : "";
        border.left = col === col1 ? borderStyle : "";
        border.right = borderStyle;
        border.bottom = borderStyle;
      } else if (action === "outside") {
        if (row === row1) border.top = borderStyle;
        if (row === row2) border.bottom = borderStyle;
        if (col === col1) border.left = borderStyle;
        if (col === col2) border.right = borderStyle;
      } else if (action === "bottom") {
        border.bottom = borderStyle;
      }

      fmt.border = border;
      setCellFormat(sheet, row, col, fmt);
      paintVisibleCellFormat(row, col);
    }

    setDirty(true);
    updateFormatToolbar();
  }

  function openCellBorderMenu(ev, input) {
    if (!input || state.readOnly || state.tooLarge) return;

    const row = Number(input.dataset.row);
    const col = Number(input.dataset.col);
    if (!Number.isInteger(row) || !Number.isInteger(col)) return;

    ev.preventDefault();
    ev.stopPropagation();

    if (!formatTargetsContainCell(row, col)) {
      state.selection = null;
      state.rangeSelection = null;
      state.activeCell = { row, col };
      repaintSpreadsheetSelection();
      updateFormatToolbar();
    }

    hideTextColorMenu();
    hideFillMenu();
    hideAxisMenu();
    openBorderMenu(ev.clientX, ev.clientY);
  }

  function applyFormatCommand(kind, value = null) {
    if (state.readOnly || state.tooLarge) return;

    const sheet = state.sheets[state.active];
    const cells = formatTargetCells();
    if (!sheet || !cells.length) {
      setStatus(tr("filemgr.spreadsheet_editor.select_cell_first", null, "Select a cell, row or column first."), "warn");
      return;
    }

    let enable = true;
    if (kind === "bold" || kind === "italic" || kind === "underline") {
      enable = !cells.every(({ row, col }) => !!getCellFormat(sheet, row, col)[kind]);
    }

    for (const { row, col } of cells) {
      const fmt = getCellFormat(sheet, row, col);

      if (kind === "bold" || kind === "italic" || kind === "underline") {
        fmt[kind] = enable;
      } else if (kind === "fontSize") {
        fmt.fontSize = normalizeFontSize(value);
      } else if (kind === "align") {
        fmt.align = value === "center" ? "center" : "left";
      } else if (kind === "bg") {
        fmt.bg = normalizeFillColorKey(value);
      } else if (kind === "fg") {
        fmt.fg = normalizeTextColorKey(value);
      }

      setCellFormat(sheet, row, col, fmt);
      paintVisibleCellFormat(row, col);
    }

    setDirty(true);
    updateFormatToolbar();
  }

  function firstFormatTargetCell() {
    const cells = formatTargetCells();
    return cells.length ? cells[0] : null;
  }

  function setToolButtonActive(btn, on) {
    if (!btn) return;
    btn.classList.toggle("active", !!on);
    btn.setAttribute("aria-pressed", on ? "true" : "false");
  }

  function updateFormatToolbar() {
    const disabled = state.saving || state.readOnly || state.tooLarge;
    const first = firstFormatTargetCell();
    const sheet = state.sheets[state.active];
    const fmt = first && sheet ? getCellFormat(sheet, first.row, first.col) : normalizeCellFormat(null);

    for (const btn of [boldBtn, italicBtn, underlineBtn, alignLeftBtn, alignCenterBtn, textColorBtn, fillBtn]) {
      if (btn) btn.disabled = disabled;
    }
    if (fontSizeSelect) fontSizeSelect.disabled = disabled;

    setToolButtonActive(boldBtn, !!fmt.bold);
    setToolButtonActive(italicBtn, !!fmt.italic);
    setToolButtonActive(underlineBtn, !!fmt.underline);
    if (fontSizeSelect) {
      fontSizeSelect.value = fmt.fontSize ? String(fmt.fontSize) : "";
    }
    setToolButtonActive(alignLeftBtn, fmt.align === "left" || !fmt.align);
    setToolButtonActive(alignCenterBtn, fmt.align === "center");
    setToolButtonActive(textColorBtn, !!fmt.fg);
    if (textColorBtn) {
      textColorBtn.dataset.fg = fmt.fg || "";
      if (fmt.fg && TEXT_COLOR_COLORS[fmt.fg]) {
        textColorBtn.style.setProperty("--spreadsheet-text-preview", TEXT_COLOR_COLORS[fmt.fg].css);
      } else {
        textColorBtn.style.removeProperty("--spreadsheet-text-preview");
      }
    }
    setToolButtonActive(fillBtn, !!fmt.bg);
    if (fillBtn) {
      fillBtn.dataset.fill = fmt.bg || "";
      if (fmt.bg && CELL_FILL_COLORS[fmt.bg]) {
        fillBtn.style.setProperty("--spreadsheet-fill-preview", CELL_FILL_COLORS[fmt.bg].css);
      } else {
        fillBtn.style.removeProperty("--spreadsheet-fill-preview");
      }
    }
  }

  function compactCellFormats(sheet) {
    const rows = Array.isArray(sheet && sheet.cellFormats) ? sheet.cellFormats : [];
    const out = [];

    for (let r = 0; r < rows.length; r++) {
      const srcRow = Array.isArray(rows[r]) ? rows[r] : [];
      const dstRow = [];

      for (let c = 0; c < srcRow.length; c++) {
        const fmt = normalizeCellFormat(srcRow[c]);
        dstRow[c] = isEmptyCellFormat(fmt) ? null : fmt;
      }

      while (dstRow.length && dstRow[dstRow.length - 1] == null) {
        dstRow.pop();
      }

      out[r] = dstRow;
    }

    while (out.length && (!Array.isArray(out[out.length - 1]) || out[out.length - 1].length === 0)) {
      out.pop();
    }

    return out;
  }

  function readStyleMetadataJsonFromSheet(ws) {
    if (!ws) return "";

    const chunks = [];
    for (let row = 2; row < 100000; row++) {
      const cell = ws[`A${row}`];
      if (!cell) break;

      const value = cell.v != null ? cell.v : cell.w;
      if (value == null) break;
      chunks.push(String(value));
    }

    return chunks.join("");
  }

  function readStoredCellFormats(XLSX, wb) {
    const ws = wb && wb.Sheets ? wb.Sheets[STYLE_SHEET_NAME] : null;
    if (!ws) return {};

    const raw = readStyleMetadataJsonFromSheet(ws);
    if (!raw) return {};

    try {
      const parsed = JSON.parse(String(raw));
      if (!parsed || parsed.version !== STYLE_META_VERSION || !parsed.sheets || typeof parsed.sheets !== "object") {
        return {};
      }
      return parsed.sheets;
    } catch (_) {
      return {};
    }
  }

  function appendStyleMetadataSheet(XLSX, wb, stylePayload) {
    const json = JSON.stringify({ version: STYLE_META_VERSION, sheets: stylePayload || {} });
    const rows = [[STYLE_META_VERSION]];

    // XLSX cells have a hard text length limit of 32767 characters. Store the
    // style metadata JSON as safe chunks so heavily formatted sheets still save.
    for (let i = 0; i < json.length; i += STYLE_META_CHUNK_SIZE) {
      rows.push([json.slice(i, i + STYLE_META_CHUNK_SIZE)]);
    }

    const ws = XLSX.utils.aoa_to_sheet(rows);

    XLSX.utils.book_append_sheet(wb, ws, STYLE_SHEET_NAME);

    wb.Workbook = wb.Workbook || {};
    wb.Workbook.Sheets = Array.isArray(wb.Workbook.Sheets) ? wb.Workbook.Sheets : [];

    while (wb.Workbook.Sheets.length < wb.SheetNames.length) {
      wb.Workbook.Sheets.push({});
    }

    wb.Workbook.Sheets[wb.SheetNames.length - 1].Hidden = 1;
  }

  function outputCellStyle(fmt) {
    const f = normalizeCellFormat(fmt);
    if (isEmptyCellFormat(f)) return null;

    const style = {};
    if (f.bold || f.italic || f.underline || f.fontSize || (f.fg && TEXT_COLOR_COLORS[f.fg])) {
      style.font = {};
      if (f.bold) style.font.bold = true;
      if (f.italic) style.font.italic = true;
      if (f.underline) style.font.underline = true;
      if (f.fontSize) style.font.sz = f.fontSize;
      if (f.fg && TEXT_COLOR_COLORS[f.fg]) {
        style.font.color = { rgb: TEXT_COLOR_COLORS[f.fg].rgb };
      }
    }

    if (f.align) {
      style.alignment = { horizontal: f.align };
    }

    if (f.bg && CELL_FILL_COLORS[f.bg]) {
      style.fill = {
        patternType: "solid",
        fgColor: { rgb: CELL_FILL_COLORS[f.bg].rgb }
      };
    }

    const border = outputBorderStyle(f.border);
    if (border) {
      style.border = border;
    }

    return style;
  }

  function applyOutputCellFormats(XLSX, ws, sheet, rowCount, colCount) {
    ensureSheetCellFormats(sheet, rowCount, colCount);

    for (let r = 0; r < rowCount; r++) {
      for (let c = 0; c < colCount; c++) {
        const style = outputCellStyle(getCellFormat(sheet, r, c));
        if (!style) continue;

        const addr = XLSX.utils.encode_cell({ r, c });
        ws[addr] = ws[addr] || { t: "s", v: "" };
        ws[addr].s = style;
      }
    }
  }

  function clampColumnWidth(width) {
    const n = Number(width);
    if (!Number.isFinite(n)) return DEFAULT_COL_WIDTH;
    return Math.max(MIN_COL_WIDTH, Math.min(MAX_COL_WIDTH, Math.round(n)));
  }

  function xlsxColumnToPixelWidth(col) {
    if (!col || typeof col !== "object") return DEFAULT_COL_WIDTH;
    if (Number.isFinite(col.wpx)) return clampColumnWidth(col.wpx);
    if (Number.isFinite(col.width)) return clampColumnWidth((col.width * 8) + 16);
    if (Number.isFinite(col.wch)) return clampColumnWidth((col.wch * 8) + 16);
    return DEFAULT_COL_WIDTH;
  }

  function ensureSheetColWidths(sheet, colCount) {
    if (!sheet) return [];
    const count = Math.max(0, Number.isInteger(colCount) ? colCount : 0);

    if (!Array.isArray(sheet.colWidths)) {
      sheet.colWidths = [];
    }

    while (sheet.colWidths.length < count) {
      sheet.colWidths.push(DEFAULT_COL_WIDTH);
    }

    if (sheet.colWidths.length > count) {
      sheet.colWidths.length = count;
    }

    for (let i = 0; i < sheet.colWidths.length; i++) {
      sheet.colWidths[i] = clampColumnWidth(sheet.colWidths[i]);
    }

    return sheet.colWidths;
  }

  function sheetColumnWidth(sheet, col) {
    const widths = ensureSheetColWidths(sheet, col + 1);
    return clampColumnWidth(widths[col]);
  }

  function applyColumnWidth(el, width) {
    if (!el) return;

    // Keep spreadsheet inputs as full-cell editors. The td owns the actual
    // cell geometry; pinning input width/maxWidth can create a visible split
    // when browser table layout expands the cell.
    if (String(el.tagName || "").toUpperCase() === "INPUT") {
      el.style.width = "100%";
      el.style.minWidth = "0";
      el.style.maxWidth = "none";
      return;
    }

    const px = `${clampColumnWidth(width)}px`;
    el.style.width = px;
    el.style.minWidth = px;
    el.style.maxWidth = px;
  }

  function paintVisibleColumnWidth(col, width) {
    if (!bodyEl || !Number.isInteger(col) || col < 0) return;

    const table = bodyEl.querySelector(".spreadsheetEditorTable");
    if (!table) return;

    const pxWidth = clampColumnWidth(width);

    const colEl = table.querySelector(`col[data-col="${col}"]`);
    if (colEl) {
      colEl.style.width = `${pxWidth}px`;
    }

    const header = table.querySelector(`th[data-col="${col}"]`);
    applyColumnWidth(header, pxWidth);

    for (const cell of table.querySelectorAll(`td[data-col="${col}"]`)) {
      applyColumnWidth(cell, pxWidth);
      const input = cell.querySelector("input");
      applyColumnWidth(input, pxWidth);
    }
  }

  function startColumnResize(ev, col) {
    if (state.readOnly || state.tooLarge) return;

    const sheet = state.sheets[state.active];
    if (!sheet || !Number.isInteger(col) || col < 0) return;

    ev.preventDefault();
    ev.stopPropagation();

    if (typeof ev.stopImmediatePropagation === "function") {
      ev.stopImmediatePropagation();
    }

    hideAxisMenu();

    const startX = Number(ev.clientX);
    const startWidth = sheetColumnWidth(sheet, col);
    let changed = false;

    const onMove = (moveEv) => {
      const dx = Number(moveEv.clientX) - startX;
      const nextWidth = clampColumnWidth(startWidth + dx);

      ensureSheetColWidths(sheet, col + 1);
      if (sheet.colWidths[col] === nextWidth) return;

      sheet.colWidths[col] = nextWidth;
      paintVisibleColumnWidth(col, nextWidth);

      if (!changed) {
        changed = true;
        setDirty(true);
      }
    };

    const onUp = () => {
      document.removeEventListener("pointermove", onMove);
      document.removeEventListener("pointerup", onUp);
      document.body.classList.remove("spreadsheetColumnResizing");
      repaintSpreadsheetSelection();
    };

    document.body.classList.add("spreadsheetColumnResizing");
    document.addEventListener("pointermove", onMove);
    document.addEventListener("pointerup", onUp, { once: true });
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


  function isFormulaValue(value) {
    return String(value == null ? "" : value).startsWith("=");
  }

  function cellRaw(sheet, row, col) {
    const rows = sheet && Array.isArray(sheet.rows) ? sheet.rows : [];
    const r = rows[row];
    if (!Array.isArray(r)) return "";
    return r[col] == null ? "" : String(r[col]);
  }

  function setCellRaw(sheet, row, col, value) {
    if (!sheet || !Array.isArray(sheet.rows)) return;
    while (sheet.rows.length <= row) sheet.rows.push([]);
    while (sheet.rows[row].length <= col) sheet.rows[row].push("");
    sheet.rows[row][col] = String(value == null ? "" : value);
  }

  function colLettersToIndex(letters) {
    let n = 0;
    const s = String(letters || "").toUpperCase();
    for (let i = 0; i < s.length; i++) {
      const code = s.charCodeAt(i);
      if (code < 65 || code > 90) return -1;
      n = n * 26 + (code - 64);
    }
    return n - 1;
  }

  function parseCellRef(ref) {
    const m = String(ref || "").trim().match(/^([A-Z]+)([1-9][0-9]*)$/i);
    if (!m) return null;
    const col = colLettersToIndex(m[1]);
    const row = Number(m[2]) - 1;
    if (!Number.isInteger(row) || row < 0 || col < 0) return null;
    return { row, col };
  }

  function coordToRef(row, col) {
    return `${columnName(col)}${row + 1}`;
  }

  function parsePlainNumber(raw) {
    const s = String(raw == null ? "" : raw).trim();
    if (!s) return { blank: true, number: 0 };
    if (/^-?\d+(?:[.,]\d+)?$/.test(s)) {
      const n = Number(s.replace(",", "."));
      if (Number.isFinite(n)) return { blank: false, number: n };
    }
    return { blank: false, text: s };
  }

  function formatFormulaNumber(n) {
    if (!Number.isFinite(n)) return "#VALUE!";
    const rounded = Math.round(n * 1000000000000) / 1000000000000;
    return String(Object.is(rounded, -0) ? 0 : rounded);
  }

  function makeFormulaParser(source, sheet, cache, visiting) {
    const src = String(source || "");
    let pos = 0;

    function skipWs() {
      while (/\s/.test(src[pos] || "")) pos++;
    }

    function peek() {
      skipWs();
      return src[pos] || "";
    }

    function match(ch) {
      skipWs();
      if (src[pos] === ch) {
        pos++;
        return true;
      }
      return false;
    }

    function parseNumberLiteral() {
      skipWs();
      const start = pos;
      while (/[0-9.]/.test(src[pos] || "")) pos++;
      const raw = src.slice(start, pos);
      if (!raw || raw === ".") throw new Error("#VALUE!");
      const n = Number(raw);
      if (!Number.isFinite(n)) throw new Error("#VALUE!");
      return n;
    }

    function readLetters() {
      skipWs();
      const start = pos;
      while (/[A-Za-z]/.test(src[pos] || "")) pos++;
      return src.slice(start, pos);
    }

    function readDigits() {
      const start = pos;
      while (/[0-9]/.test(src[pos] || "")) pos++;
      return src.slice(start, pos);
    }

    function parseCellTokenAtCurrent() {
      skipWs();
      const save = pos;
      const letters = readLetters();
      if (!letters) {
        pos = save;
        return null;
      }
      const digits = readDigits();
      if (!digits) {
        pos = save;
        return null;
      }
      const ref = parseCellRef(letters + digits);
      if (!ref) {
        pos = save;
        return null;
      }
      return ref;
    }

    function cellNumericValue(ref) {
      const result = evaluateCell(sheet, ref.row, ref.col, cache, visiting);
      if (result.error) throw new Error(result.error);
      if (result.blank) return 0;
      if (typeof result.value === "number" && Number.isFinite(result.value)) return result.value;

      const parsed = parsePlainNumber(result.raw);
      if (parsed.blank) return 0;
      if (typeof parsed.number === "number") return parsed.number;
      throw new Error("#VALUE!");
    }

    function rangeValues(start, end) {
      const r1 = Math.min(start.row, end.row);
      const r2 = Math.max(start.row, end.row);
      const c1 = Math.min(start.col, end.col);
      const c2 = Math.max(start.col, end.col);
      const out = [];

      if ((r2 - r1 + 1) * (c2 - c1 + 1) > MAX_EDIT_ROWS * MAX_EDIT_COLS) {
        throw new Error("#REF!");
      }

      for (let r = r1; r <= r2; r++) {
        for (let c = c1; c <= c2; c++) {
          const result = evaluateCell(sheet, r, c, cache, visiting);
          if (result.error) throw new Error(result.error);
          if (result.blank) {
            out.push({ blank: true, value: 0 });
            continue;
          }
          if (typeof result.value === "number" && Number.isFinite(result.value)) {
            out.push({ blank: false, value: result.value });
            continue;
          }

          const parsed = parsePlainNumber(result.raw);
          if (parsed.blank) out.push({ blank: true, value: 0 });
          else if (typeof parsed.number === "number") out.push({ blank: false, value: parsed.number });
          else out.push({ blank: false, text: parsed.text, value: 0 });
        }
      }

      return out;
    }

    function parseFunctionArg() {
      skipWs();
      const save = pos;
      const firstCell = parseCellTokenAtCurrent();
      if (firstCell && match(":")) {
        const secondCell = parseCellTokenAtCurrent();
        if (!secondCell) throw new Error("#REF!");
        return { type: "range", values: rangeValues(firstCell, secondCell) };
      }
      pos = save;
      return { type: "number", value: parseExpression() };
    }

    function parseFunctionCall(name) {
      if (!match("(")) throw new Error("#VALUE!");

      const args = [];
      if (!match(")")) {
        while (true) {
          args.push(parseFunctionArg());
          if (match(")")) break;
          if (!match(",")) throw new Error("#VALUE!");
        }
      }

      const values = [];
      for (const arg of args) {
        if (arg.type === "range") values.push(...arg.values);
        else values.push({ blank: false, value: arg.value });
      }

      const nums = values
        .filter((v) => !v.text)
        .filter((v) => !v.blank || name.toUpperCase() !== "COUNT")
        .map((v) => Number(v.value))
        .filter((v) => Number.isFinite(v));

      switch (name.toUpperCase()) {
        case "SUM":
          return nums.reduce((a, b) => a + b, 0);
        case "AVERAGE":
          if (!nums.length) throw new Error("#DIV/0!");
          return nums.reduce((a, b) => a + b, 0) / nums.length;
        case "MIN":
          if (!nums.length) throw new Error("#VALUE!");
          return Math.min(...nums);
        case "MAX":
          if (!nums.length) throw new Error("#VALUE!");
          return Math.max(...nums);
        case "COUNT":
          return nums.length;
        default:
          throw new Error("#NAME?");
      }
    }

    function parsePrimary() {
      skipWs();

      if (match("(")) {
        const v = parseExpression();
        if (!match(")")) throw new Error("#VALUE!");
        return v;
      }

      if (/[0-9.]/.test(peek())) {
        return parseNumberLiteral();
      }

      const save = pos;
      const letters = readLetters();
      if (letters) {
        const digits = readDigits();
        if (digits) {
          const ref = parseCellRef(letters + digits);
          if (!ref) throw new Error("#REF!");
          if (match(":")) throw new Error("#VALUE!");
          return cellNumericValue(ref);
        }

        pos = save + letters.length;
        return parseFunctionCall(letters);
      }

      throw new Error("#VALUE!");
    }

    function parseUnary() {
      if (match("+")) return parseUnary();
      if (match("-")) return -parseUnary();
      return parsePrimary();
    }

    function parseTerm() {
      let v = parseUnary();
      while (true) {
        if (match("*")) {
          v *= parseUnary();
        } else if (match("/")) {
          const d = parseUnary();
          if (d === 0) throw new Error("#DIV/0!");
          v /= d;
        } else {
          return v;
        }
      }
    }

    function parseExpression() {
      let v = parseTerm();
      while (true) {
        if (match("+")) {
          v += parseTerm();
        } else if (match("-")) {
          v -= parseTerm();
        } else {
          return v;
        }
      }
    }

    function parseAll() {
      const v = parseExpression();
      skipWs();
      if (pos !== src.length) throw new Error("#VALUE!");
      return v;
    }

    return { parseAll };
  }

  function evaluateCell(sheet, row, col, cache, visiting) {
    const key = `${row}:${col}`;
    if (cache.has(key)) return cache.get(key);

    const raw = cellRaw(sheet, row, col);
    const parsed = parsePlainNumber(raw);

    if (!isFormulaValue(raw)) {
      const result = typeof parsed.number === "number"
        ? { raw, value: parsed.number, blank: parsed.blank, error: "" }
        : { raw, value: parsed.text || "", blank: parsed.blank, error: "" };
      cache.set(key, result);
      return result;
    }

    if (visiting.has(key)) {
      const cycle = { raw, value: "", blank: false, error: "#CYCLE!" };
      cache.set(key, cycle);
      return cycle;
    }

    visiting.add(key);

    try {
      const body = raw.slice(1);
      if (body.includes("#REF!")) throw new Error("#REF!");
      const parser = makeFormulaParser(body, sheet, cache, visiting);
      const value = parser.parseAll();
      const result = { raw, value, blank: false, error: "" };
      cache.set(key, result);
      return result;
    } catch (e) {
      const result = { raw, value: "", blank: false, error: String(e && e.message ? e.message : e || "#VALUE!") };
      cache.set(key, result);
      return result;
    } finally {
      visiting.delete(key);
    }
  }

  function computeSheetCache(sheet) {
    const cache = new Map();
    const visiting = new Set();
    const rows = sheet && Array.isArray(sheet.rows) ? sheet.rows : [];
    for (let r = 0; r < rows.length; r++) {
      const row = Array.isArray(rows[r]) ? rows[r] : [];
      for (let c = 0; c < row.length; c++) {
        evaluateCell(sheet, r, c, cache, visiting);
      }
    }
    return cache;
  }

  function displayCellValue(sheet, row, col, cache = null) {
    const raw = cellRaw(sheet, row, col);
    if (!isFormulaValue(raw)) return raw;
    const effectiveCache = cache || computeSheetCache(sheet);
    const result = evaluateCell(sheet, row, col, effectiveCache, new Set());
    if (result.error) return result.error;
    return formatFormulaNumber(result.value);
  }

  function refreshFormulaDisplays(skipInput = null) {
    const sheet = state.sheets[state.active];
    if (!sheet || !bodyEl) return;

    const cache = computeSheetCache(sheet);
    for (const input of bodyEl.querySelectorAll("input[data-row][data-col]")) {
      if (input === skipInput || input === document.activeElement) continue;
      const r = Number(input.dataset.row);
      const c = Number(input.dataset.col);
      if (!Number.isInteger(r) || !Number.isInteger(c)) continue;
      input.value = displayCellValue(sheet, r, c, cache);
    }
  }

  function clearSelectionClasses() {
    const table = bodyEl ? bodyEl.querySelector(".spreadsheetEditorTable") : null;
    if (!table) return;

    for (const el of table.querySelectorAll(
      ".spreadsheetAxisSelectedHeader, .spreadsheetAxisSelectedCell, .spreadsheetAxisSelectedInput, " +
      ".spreadsheetEditorSelected, .spreadsheetEditorSelectedInput"
    )) {
      el.classList.remove(
        "spreadsheetAxisSelectedHeader",
        "spreadsheetAxisSelectedCell",
        "spreadsheetAxisSelectedInput",
        "spreadsheetEditorSelected",
        "spreadsheetEditorSelectedInput"
      );
    }

    for (const el of table.querySelectorAll("[data-spreadsheet-axis-style='1']")) {
      el.style.removeProperty("background");
      el.style.removeProperty("color");
      el.style.removeProperty("box-shadow");
      el.style.removeProperty("border-color");
      el.style.removeProperty("outline");
      el.style.removeProperty("outline-offset");
      el.removeAttribute("data-spreadsheet-axis-style");
    }

    for (const el of table.querySelectorAll("[aria-selected='true']")) {
      el.removeAttribute("aria-selected");
    }
  }

  function markAxisHeader(el) {
    if (!el) return;
    el.dataset.spreadsheetAxisStyle = "1";
    el.style.setProperty("background", "Highlight");
    el.style.setProperty("color", "HighlightText");
    el.style.setProperty("box-shadow", "inset 0 0 0 3px Highlight, inset 0 -4px 0 0 Highlight");
  }

  function markAxisCell(cell) {
    if (!cell) return;
    cell.dataset.spreadsheetAxisStyle = "1";
    cell.style.setProperty("background", "Highlight");
    cell.style.setProperty("box-shadow", "inset 0 0 0 2px Highlight");

    const input = cell.querySelector("input");
    if (input) {
      input.dataset.spreadsheetAxisStyle = "1";
      input.style.setProperty("background", "transparent");
      input.style.setProperty("color", "HighlightText");
      input.style.removeProperty("border-color");
      input.style.removeProperty("box-shadow");
    }
  }

  function paintAxisSelection() {
    const table = bodyEl ? bodyEl.querySelector(".spreadsheetEditorTable") : null;
    if (!table) return;

    clearSelectionClasses();

    if (!state.selection) return;

    const type = state.selection.type;
    const index = Number(state.selection.index);
    if (!Number.isInteger(index) || index < 0) return;

    const headerSelector = type === "column"
      ? `th[data-col="${index}"]`
      : `th[data-row="${index}"]`;

    const cellMatcher = type === "column"
      ? (cell) => Number(cell.dataset.col) === index
      : (cell) => Number(cell.dataset.row) === index;

    const header = table.querySelector(headerSelector);
    if (header) {
      header.classList.add("spreadsheetAxisSelectedHeader");
      header.setAttribute("aria-selected", "true");
      markAxisHeader(header);
    }

    for (const cell of table.querySelectorAll("td[data-row][data-col]")) {
      if (!cellMatcher(cell)) continue;

      cell.classList.add("spreadsheetAxisSelectedCell");
      cell.setAttribute("aria-selected", "true");

      const input = cell.querySelector("input");
      if (input) {
        input.classList.add("spreadsheetAxisSelectedInput");
      }
      markAxisCell(cell);
    }
  }

  function normalizedRangeSelection(range = state.rangeSelection) {
    if (!range) return null;

    const sr = Number(range.startRow);
    const sc = Number(range.startCol);
    const er = Number(range.endRow);
    const ec = Number(range.endCol);

    if (![sr, sc, er, ec].every(Number.isInteger)) return null;

    return {
      row1: Math.min(sr, er),
      row2: Math.max(sr, er),
      col1: Math.min(sc, ec),
      col2: Math.max(sc, ec)
    };
  }

  function clearRangeSelectionClasses() {
    if (!bodyEl) return;

    for (const el of bodyEl.querySelectorAll(".spreadsheetRangeSelectedCell, .spreadsheetRangeSelectedInput")) {
      el.classList.remove("spreadsheetRangeSelectedCell", "spreadsheetRangeSelectedInput");
      el.removeAttribute("aria-selected");
    }
  }

  function paintRangeSelection() {
    clearRangeSelectionClasses();

    const range = normalizedRangeSelection();
    if (!range || !bodyEl) return;

    for (let r = range.row1; r <= range.row2; r++) {
      for (let c = range.col1; c <= range.col2; c++) {
        const input = bodyEl.querySelector(`input[data-row="${r}"][data-col="${c}"]`);
        if (!input) continue;

        const cell = input.closest("td[data-row][data-col]");
        if (cell) {
          cell.classList.add("spreadsheetRangeSelectedCell");
          cell.setAttribute("aria-selected", "true");
        }

        input.classList.add("spreadsheetRangeSelectedInput");
        input.setAttribute("aria-selected", "true");
      }
    }
  }

  function setRangeSelection(startRow, startCol, endRow, endCol) {
    if (![startRow, startCol, endRow, endCol].every(Number.isInteger)) return;

    state.selection = null;
    state.activeCell = null;
    state.rangeSelection = null;
    state.rangeSelection = { startRow, startCol, endRow, endCol };

    repaintSpreadsheetSelection();
    updateFormatToolbar();
  }

  function beginCellRangePointer(ev, row, col) {
    if (state.readOnly || state.tooLarge) return;
    if (!ev || ev.button !== 0) return;
    if (!Number.isInteger(row) || !Number.isInteger(col)) return;

    const activeFormula = formulaFocus && formulaFocus.input;
    if (activeFormula && activeFormula !== ev.currentTarget && String(activeFormula.value || "").startsWith("=")) {
      return;
    }

    const startX = Number(ev.clientX);
    const startY = Number(ev.clientY);
    let dragging = false;

    const onMove = (moveEv) => {
      const dx = Math.abs(Number(moveEv.clientX) - startX);
      const dy = Math.abs(Number(moveEv.clientY) - startY);

      if (!dragging && dx < 4 && dy < 4) return;

      const target = document.elementFromPoint(moveEv.clientX, moveEv.clientY);
      const input = target && target.closest
        ? target.closest("input[data-row][data-col]")
        : null;

      if (!input || !bodyEl || !bodyEl.contains(input)) return;

      const endRow = Number(input.dataset.row);
      const endCol = Number(input.dataset.col);
      if (!Number.isInteger(endRow) || !Number.isInteger(endCol)) return;

      dragging = true;
      moveEv.preventDefault();
      document.body.classList.add("spreadsheetRangeSelecting");
      setRangeSelection(row, col, endRow, endCol);
    };

    const onUp = () => {
      document.removeEventListener("pointermove", onMove);
      document.removeEventListener("pointerup", onUp);
      document.body.classList.remove("spreadsheetRangeSelecting");
    };

    document.addEventListener("pointermove", onMove);
    document.addEventListener("pointerup", onUp, { once: true });
  }

  function clearActiveCellHighlight() {
    if (!bodyEl) return;

    for (const el of bodyEl.querySelectorAll(".spreadsheetActiveCell, .spreadsheetActiveCellInput")) {
      el.classList.remove("spreadsheetActiveCell", "spreadsheetActiveCellInput");
      el.removeAttribute("aria-current");
    }
  }

  function paintActiveCellSelection() {
    clearActiveCellHighlight();

    if (state.selection || state.rangeSelection || !state.activeCell || !bodyEl) return;

    const row = Number(state.activeCell.row);
    const col = Number(state.activeCell.col);
    if (!Number.isInteger(row) || !Number.isInteger(col) || row < 0 || col < 0) return;

    const input = bodyEl.querySelector(`input[data-row="${row}"][data-col="${col}"]`);
    if (!input) return;

    const cell = input.closest("td[data-row][data-col]");
    if (cell) {
      cell.classList.add("spreadsheetActiveCell");
      cell.setAttribute("aria-current", "true");
    }

    input.classList.add("spreadsheetActiveCellInput");
    input.setAttribute("aria-current", "true");
  }

  function repaintSpreadsheetSelection() {
    paintAxisSelection();
    paintRangeSelection();
    paintActiveCellSelection();
  }

  function setSpreadsheetAxisSelection(type, index) {
    if ((type !== "row" && type !== "column") || !Number.isInteger(index) || index < 0) {
      return;
    }

    state.selection = { type, index };
    state.activeCell = null;
    state.rangeSelection = null;
    repaintSpreadsheetSelection();
    updateFormatToolbar();
  }

  function selectSpreadsheetAxis(type, index) {
    if ((type !== "row" && type !== "column") || !Number.isInteger(index) || index < 0) {
      return;
    }

    const same =
      state.selection &&
      state.selection.type === type &&
      state.selection.index === index;

    state.selection = same ? null : { type, index };
    state.activeCell = null;
    state.rangeSelection = null;
    repaintSpreadsheetSelection();
    updateFormatToolbar();
  }

  function hideTextColorMenu() {
    if (!textColorMenu || textColorMenu.hidden) return false;
    textColorMenu.hidden = true;
    textColorMenu.replaceChildren();
    return true;
  }

  function textColorLabel(key) {
    switch (key) {
      case "black": return tr("filemgr.spreadsheet_editor.text_black", null, "Black");
      case "red": return tr("filemgr.spreadsheet_editor.text_red", null, "Red");
      case "green": return tr("filemgr.spreadsheet_editor.text_green", null, "Green");
      case "blue": return tr("filemgr.spreadsheet_editor.text_blue", null, "Blue");
      case "gray": return tr("filemgr.spreadsheet_editor.text_gray", null, "Gray");
      case "white": return tr("filemgr.spreadsheet_editor.text_white", null, "White");
      default: return tr("filemgr.spreadsheet_editor.text_none", null, "Default text");
    }
  }

  function ensureTextColorMenu() {
    if (textColorMenu) return textColorMenu;

    textColorMenu = document.createElement("div");
    textColorMenu.className = "spreadsheetTextColorMenu";
    textColorMenu.hidden = true;
    textColorMenu.setAttribute("role", "menu");

    textColorMenu.addEventListener("click", (ev) => {
      ev.stopPropagation();
    });

    document.body.appendChild(textColorMenu);
    return textColorMenu;
  }

  function makeTextColorMenuButton(key) {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.setAttribute("role", "menuitem");
    btn.dataset.fg = key || "";

    const swatch = document.createElement("span");
    swatch.className = "spreadsheetTextColorSwatch";
    if (key && TEXT_COLOR_COLORS[key]) {
      swatch.style.setProperty("--spreadsheet-text-preview", TEXT_COLOR_COLORS[key].css);
    }

    const label = document.createElement("span");
    label.textContent = textColorLabel(key);

    btn.appendChild(swatch);
    btn.appendChild(label);

    btn.addEventListener("click", (ev) => {
      ev.preventDefault();
      ev.stopPropagation();
      hideTextColorMenu();
      applyFormatCommand("fg", key || "");
    });

    return btn;
  }

  function openTextColorMenu() {
    if (!textColorBtn) return;

    const menu = ensureTextColorMenu();
    menu.replaceChildren();

    menu.appendChild(makeTextColorMenuButton(""));
    for (const key of Object.keys(TEXT_COLOR_COLORS)) {
      menu.appendChild(makeTextColorMenuButton(key));
    }

    const rect = textColorBtn.getBoundingClientRect();
    menu.hidden = false;

    const left = Math.max(8, Math.min(rect.left, window.innerWidth - menu.offsetWidth - 8));
    const top = Math.max(8, Math.min(rect.bottom + 6, window.innerHeight - menu.offsetHeight - 8));

    menu.style.left = `${left}px`;
    menu.style.top = `${top}px`;
  }

  function hideFillMenu() {
    if (!fillMenu || fillMenu.hidden) return false;
    fillMenu.hidden = true;
    fillMenu.replaceChildren();
    return true;
  }

  function fillColorLabel(key) {
    switch (key) {
      case "yellow": return tr("filemgr.spreadsheet_editor.fill_yellow", null, "Yellow");
      case "green": return tr("filemgr.spreadsheet_editor.fill_green", null, "Green");
      case "blue": return tr("filemgr.spreadsheet_editor.fill_blue", null, "Blue");
      case "red": return tr("filemgr.spreadsheet_editor.fill_red", null, "Red");
      case "gray": return tr("filemgr.spreadsheet_editor.fill_gray", null, "Gray");
      default: return tr("filemgr.spreadsheet_editor.fill_none", null, "No fill");
    }
  }

  function ensureFillMenu() {
    if (fillMenu) return fillMenu;

    fillMenu = document.createElement("div");
    fillMenu.className = "spreadsheetFillMenu";
    fillMenu.hidden = true;
    fillMenu.setAttribute("role", "menu");

    fillMenu.addEventListener("click", (ev) => {
      ev.stopPropagation();
    });

    document.body.appendChild(fillMenu);
    return fillMenu;
  }

  function makeFillMenuButton(key) {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.setAttribute("role", "menuitem");
    btn.dataset.fill = key || "";

    const swatch = document.createElement("span");
    swatch.className = "spreadsheetFillSwatch";
    if (key && CELL_FILL_COLORS[key]) {
      swatch.style.setProperty("--spreadsheet-fill-preview", CELL_FILL_COLORS[key].css);
    }

    const label = document.createElement("span");
    label.textContent = fillColorLabel(key);

    btn.appendChild(swatch);
    btn.appendChild(label);

    btn.addEventListener("click", (ev) => {
      ev.preventDefault();
      ev.stopPropagation();
      hideFillMenu();
      applyFormatCommand("bg", key || "");
    });

    return btn;
  }

  function openFillMenu() {
    if (!fillBtn) return;

    const menu = ensureFillMenu();
    menu.replaceChildren();

    menu.appendChild(makeFillMenuButton(""));
    for (const key of Object.keys(CELL_FILL_COLORS)) {
      menu.appendChild(makeFillMenuButton(key));
    }

    const rect = fillBtn.getBoundingClientRect();
    menu.hidden = false;

    const left = Math.max(8, Math.min(rect.left, window.innerWidth - menu.offsetWidth - 8));
    const top = Math.max(8, Math.min(rect.bottom + 6, window.innerHeight - menu.offsetHeight - 8));

    menu.style.left = `${left}px`;
    menu.style.top = `${top}px`;
  }

  function hideBorderMenu() {
    if (!borderMenu || borderMenu.hidden) return false;
    borderMenu.hidden = true;
    borderMenu.replaceChildren();
    return true;
  }

  function borderMenuLabel(action, style = "") {
    if (action === "clear") return tr("filemgr.spreadsheet_editor.border_none", null, "No custom borders");
    if (action === "all" && style === "thin") return tr("filemgr.spreadsheet_editor.border_all_thin", null, "All thin borders");
    if (action === "outside" && style === "thin") return tr("filemgr.spreadsheet_editor.border_outside_thin", null, "Outside thin border");
    if (action === "outside" && style === "thick") return tr("filemgr.spreadsheet_editor.border_outside_thick", null, "Outside thick border");
    if (action === "bottom" && style === "thin") return tr("filemgr.spreadsheet_editor.border_bottom_thin", null, "Bottom thin border");
    if (action === "bottom" && style === "thick") return tr("filemgr.spreadsheet_editor.border_bottom_thick", null, "Bottom thick border");
    return tr("filemgr.spreadsheet_editor.borders", null, "Borders");
  }

  function ensureBorderMenu() {
    if (borderMenu) return borderMenu;

    borderMenu = document.createElement("div");
    borderMenu.className = "spreadsheetBorderMenu";
    borderMenu.hidden = true;
    borderMenu.setAttribute("role", "menu");

    borderMenu.addEventListener("click", (ev) => {
      ev.stopPropagation();
    });

    document.addEventListener("click", () => hideBorderMenu());
    document.addEventListener("keydown", (ev) => {
      if (ev.key === "Escape") hideBorderMenu();
    });

    document.body.appendChild(borderMenu);
    return borderMenu;
  }

  function makeBorderMenuButton(action, style = "") {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.setAttribute("role", "menuitem");
    btn.textContent = borderMenuLabel(action, style);
    btn.addEventListener("click", (ev) => {
      ev.preventDefault();
      ev.stopPropagation();
      hideBorderMenu();
      applyBorderCommand(action, style);
    });
    return btn;
  }

  function openBorderMenu(x, y) {
    const menu = ensureBorderMenu();
    menu.replaceChildren();

    const title = document.createElement("div");
    title.className = "spreadsheetBorderMenuTitle";
    title.textContent = tr("filemgr.spreadsheet_editor.borders", null, "Borders");
    menu.appendChild(title);

    menu.appendChild(makeBorderMenuButton("clear"));
    menu.appendChild(makeBorderMenuButton("all", "thin"));
    menu.appendChild(makeBorderMenuButton("outside", "thin"));
    menu.appendChild(makeBorderMenuButton("outside", "thick"));
    menu.appendChild(makeBorderMenuButton("bottom", "thin"));
    menu.appendChild(makeBorderMenuButton("bottom", "thick"));

    menu.hidden = false;
    menu.style.left = `${Math.max(8, Number(x) || 8)}px`;
    menu.style.top = `${Math.max(8, Number(y) || 8)}px`;

    const rect = menu.getBoundingClientRect();
    const maxLeft = Math.max(8, window.innerWidth - rect.width - 8);
    const maxTop = Math.max(8, window.innerHeight - rect.height - 8);

    menu.style.left = `${Math.min(Math.max(8, Number(x) || 8), maxLeft)}px`;
    menu.style.top = `${Math.min(Math.max(8, Number(y) || 8), maxTop)}px`;
  }

  function hideAxisMenu() {
    if (!axisMenu || axisMenu.hidden) return false;
    axisMenu.hidden = true;
    axisMenu.replaceChildren();
    return true;
  }

  function makeAxisMenuButton(label, onClick) {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.textContent = label;
    btn.addEventListener("click", (ev) => {
      ev.preventDefault();
      ev.stopPropagation();
      hideAxisMenu();
      onClick();
    });
    return btn;
  }

  function ensureAxisMenu() {
    if (axisMenu) return axisMenu;

    axisMenu = document.createElement("div");
    axisMenu.className = "spreadsheetAxisMenu";
    axisMenu.hidden = true;
    axisMenu.setAttribute("role", "menu");

    axisMenu.addEventListener("click", (ev) => {
      ev.stopPropagation();
    });

    document.body.appendChild(axisMenu);
    return axisMenu;
  }

  function positionAxisMenu(menu, x, y) {
    menu.style.left = `${Math.max(8, x)}px`;
    menu.style.top = `${Math.max(8, y)}px`;

    const rect = menu.getBoundingClientRect();
    const maxLeft = Math.max(8, window.innerWidth - rect.width - 8);
    const maxTop = Math.max(8, window.innerHeight - rect.height - 8);

    menu.style.left = `${Math.min(Math.max(8, x), maxLeft)}px`;
    menu.style.top = `${Math.min(Math.max(8, y), maxTop)}px`;
  }

  function openAxisMenu(type, index, x, y) {
    if ((type !== "row" && type !== "column") || !Number.isInteger(index) || index < 0) return;
    if (state.readOnly || state.tooLarge) return;

    setSpreadsheetAxisSelection(type, index);

    const menu = ensureAxisMenu();
    menu.replaceChildren();

    if (type === "column") {
      menu.appendChild(makeAxisMenuButton(
        tr("filemgr.spreadsheet_editor.insert_column_here", null, "Insert column here"),
        () => addColumn()
      ));
      menu.appendChild(makeAxisMenuButton(
        tr("filemgr.spreadsheet_editor.delete_column", null, "Delete column"),
        () => deleteSelectedAxis("column", index)
      ));
    } else {
      menu.appendChild(makeAxisMenuButton(
        tr("filemgr.spreadsheet_editor.insert_row_here", null, "Insert row here"),
        () => addRow()
      ));
      menu.appendChild(makeAxisMenuButton(
        tr("filemgr.spreadsheet_editor.delete_row", null, "Delete row"),
        () => deleteSelectedAxis("row", index)
      ));
    }

    menu.hidden = false;
    positionAxisMenu(menu, x, y);
  }

  function attachSpreadsheetSelectionHandlers(table) {
    if (!table || table.dataset.axisSelectionCleanAttached === "1") return;
    table.dataset.axisSelectionCleanAttached = "1";

    const activate = (ev) => {
      const header = ev.target && ev.target.closest
        ? ev.target.closest("th[data-col], th[data-row]")
        : null;

      if (!header || !table.contains(header)) return;

      ev.preventDefault();
      ev.stopPropagation();

      if (typeof ev.stopImmediatePropagation === "function") {
        ev.stopImmediatePropagation();
      }

      if (header.dataset.col != null) {
        const col = Number(header.dataset.col);
        if (Number.isInteger(col)) selectSpreadsheetAxis("column", col);
        return;
      }

      if (header.dataset.row != null) {
        const row = Number(header.dataset.row);
        if (Number.isInteger(row)) selectSpreadsheetAxis("row", row);
      }
    };

    table.addEventListener("click", activate, true);
    table.addEventListener("keydown", (ev) => {
      if (ev.key === "Enter" || ev.key === " ") activate(ev);
    }, true);

    table.addEventListener("contextmenu", (ev) => {
      const header = ev.target && ev.target.closest
        ? ev.target.closest("th[data-col], th[data-row]")
        : null;

      if (!header || !table.contains(header)) return;

      ev.preventDefault();
      ev.stopPropagation();

      if (header.dataset.col != null) {
        const col = Number(header.dataset.col);
        if (Number.isInteger(col)) openAxisMenu("column", col, ev.clientX, ev.clientY);
        return;
      }

      if (header.dataset.row != null) {
        const row = Number(header.dataset.row);
        if (Number.isInteger(row)) openAxisMenu("row", row, ev.clientX, ev.clientY);
      }
    }, true);
  }

  function attachHeaderSelectionHandlers(table) {
    attachSpreadsheetSelectionHandlers(table);
  }

  function insertFormulaReference(input, row, col) {
    if (!input || !formulaFocus || formulaFocus.input !== input) return;

    const ref = coordToRef(row, col);
    const start = Number.isInteger(input.selectionStart) ? input.selectionStart : String(input.value || "").length;
    const end = Number.isInteger(input.selectionEnd) ? input.selectionEnd : start;
    const value = String(input.value || "");
    const next = value.slice(0, start) + ref + value.slice(end);

    input.value = next;
    input.selectionStart = input.selectionEnd = start + ref.length;

    const fr = Number(input.dataset.row);
    const fc = Number(input.dataset.col);
    const sheet = state.sheets[state.active];
    if (sheet && Number.isInteger(fr) && Number.isInteger(fc)) {
      setCellRaw(sheet, fr, fc, next);
      setDirty(true);
      refreshFormulaDisplays(input);
    }
  }

  function cancelFormulaEdit(input) {
    const focus = formulaFocus && formulaFocus.input === input ? formulaFocus : null;
    const row = focus ? focus.row : Number(input && input.dataset ? input.dataset.row : NaN);
    const col = focus ? focus.col : Number(input && input.dataset ? input.dataset.col : NaN);
    const sheet = state.sheets[state.active];

    if (!input || !sheet || !Number.isInteger(row) || !Number.isInteger(col)) {
      return false;
    }

    const originalRaw = focus && Object.prototype.hasOwnProperty.call(focus, "originalRaw")
      ? String(focus.originalRaw == null ? "" : focus.originalRaw)
      : cellRaw(sheet, row, col);

    setCellRaw(sheet, row, col, originalRaw);
    formulaFocus = null;

    input.value = displayCellValue(sheet, row, col);
    input.title = isFormulaValue(originalRaw) ? originalRaw : "";

    // Escape cancels only the formula edit. Preserve earlier dirty state when
    // the workbook already had unsaved changes before this formula edit began.
    setDirty(!!(focus && focus.wasDirty));
    refreshFormulaDisplays(input);

    return true;
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

  function worksheetToEditableRows(XLSX, ws) {
    if (!ws || !ws["!ref"]) {
      return {
        rows: Array.from({ length: DEFAULT_ROWS }, () => Array.from({ length: DEFAULT_COLS }, () => "")),
        colWidths: Array.from({ length: DEFAULT_COLS }, () => DEFAULT_COL_WIDTH),
        tooLarge: false
      };
    }

    const range = XLSX.utils.decode_range(ws["!ref"]);
    const sourceRows = Math.max(0, range.e.r - range.s.r + 1);
    const sourceCols = Math.max(0, range.e.c - range.s.c + 1);
    const tooLarge = sourceRows > MAX_EDIT_ROWS || sourceCols > MAX_EDIT_COLS;

    const rowCount = Math.max(Math.min(sourceRows, MAX_EDIT_ROWS), DEFAULT_ROWS);
    const colCount = Math.max(Math.min(sourceCols, MAX_EDIT_COLS), DEFAULT_COLS);
    const rows = [];

    for (let r = 0; r < rowCount; r++) {
      const row = [];
      for (let c = 0; c < colCount; c++) {
        const addr = XLSX.utils.encode_cell({ r: range.s.r + r, c: range.s.c + c });
        const cell = ws[addr];

        if (!cell) {
          row.push("");
        } else if (cell.f) {
          row.push("=" + String(cell.f));
        } else if (cell.w != null) {
          row.push(String(cell.w));
        } else if (cell.v != null) {
          row.push(String(cell.v));
        } else {
          row.push("");
        }
      }
      rows.push(row);
    }

    const colWidths = Array.from({ length: colCount }, (_v, c) => {
      const meta = ws["!cols"] && ws["!cols"][c];
      return xlsxColumnToPixelWidth(meta);
    });

    const cellFormats = Array.from({ length: rowCount }, () => Array.from({ length: colCount }, () => null));

    for (let r = 0; r < rowCount; r++) {
      for (let c = 0; c < colCount; c++) {
        const addr = XLSX.utils.encode_cell({ r: range.s.r + r, c: range.s.c + c });
        const style = ws[addr] && ws[addr].s;
        const fmt = normalizeCellFormat({
          bold: !!(style && style.font && style.font.bold),
          italic: !!(style && style.font && style.font.italic),
          underline: !!(style && style.font && style.font.underline),
          fontSize: normalizeFontSize(style && style.font && style.font.sz),
          align: style && style.alignment && style.alignment.horizontal === "center" ? "center" : "",
          bg: cellFillKeyFromRgb(style && style.fill && style.fill.fgColor && style.fill.fgColor.rgb),
          fg: cellTextColorKeyFromRgb(style && style.font && style.font.color && style.font.color.rgb),
          border: cellBorderFromXlsxStyle(style && style.border)
        });
        cellFormats[r][c] = isEmptyCellFormat(fmt) ? null : fmt;
      }
    }

    return { rows, colWidths, cellFormats, tooLarge };
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

    // Security: formulas are preserved as inert strings and evaluated only by
    // our small allowlisted parser. No eval(), macros, links or active content.
    const wb = XLSX.read(buf, {
      type: "array",
      cellFormula: true,
      cellHTML: false,
      cellNF: false,
      cellStyles: true
    });

    const storedCellFormats = readStoredCellFormats(XLSX, wb);
    const names = Array.isArray(wb.SheetNames)
      ? wb.SheetNames.filter((name) => name !== STYLE_SHEET_NAME)
      : [];

    if (!names.length) {
      return [{
        name: tr("filemgr.spreadsheet_create.sheet.sheet1", null, "Sheet1"),
        rows: Array.from({ length: DEFAULT_ROWS }, () => Array.from({ length: DEFAULT_COLS }, () => "")),
        colWidths: Array.from({ length: DEFAULT_COLS }, () => DEFAULT_COL_WIDTH),
        cellFormats: Array.from({ length: DEFAULT_ROWS }, () => Array.from({ length: DEFAULT_COLS }, () => null))
      }];
    }

    let anyTooLarge = false;
    const sheets = names.map((name, idx) => {
      const converted = worksheetToEditableRows(XLSX, wb.Sheets[name]);
      anyTooLarge = anyTooLarge || converted.tooLarge;
      const safeName = safeSheetName(name, idx);
      const storedFormats = storedCellFormats[safeName] || storedCellFormats[name] || null;
      return {
        name: safeName,
        rows: converted.rows,
        colWidths: converted.colWidths,
        cellFormats: Array.isArray(storedFormats) ? storedFormats : converted.cellFormats
      };
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

    const stylePayload = {};

    for (let i = 0; i < state.sheets.length; i++) {
      const sheet = state.sheets[i];
      const rows = Array.isArray(sheet.rows) ? sheet.rows : [];
      const cache = computeSheetCache(sheet);

      const aoa = rows.map((row, r) => (Array.isArray(row) ? row : []).map((raw, c) => {
        if (!isFormulaValue(raw)) return coerceCellValue(raw);
        const result = evaluateCell(sheet, r, c, cache, new Set());
        return result.error ? "" : result.value;
      }));

      const ws = XLSX.utils.aoa_to_sheet(aoa);

      const colCount = rows.reduce((m, row) => Math.max(m, Array.isArray(row) ? row.length : 0), 0);
      const colWidths = ensureSheetColWidths(sheet, colCount);
      applyOutputCellFormats(XLSX, ws, sheet, rows.length, colCount);
      ws["!cols"] = colWidths.map((w) => {
        const px = clampColumnWidth(w);
        return {
          wpx: px,
          wch: Math.max(6, Math.round((px - 16) / 8))
        };
      });

      rows.forEach((row, r) => {
        if (!Array.isArray(row)) return;
        row.forEach((raw, c) => {
          if (!isFormulaValue(raw)) return;

          const addr = XLSX.utils.encode_cell({ r, c });
          const result = evaluateCell(sheet, r, c, cache, new Set());
          ws[addr] = ws[addr] || {};
          ws[addr].f = String(raw).slice(1);

          if (!result.error && typeof result.value === "number" && Number.isFinite(result.value)) {
            ws[addr].t = "n";
            ws[addr].v = result.value;
          }
        });
      });

      const outputName = safeSheetName(sheet.name, i);
      stylePayload[outputName] = compactCellFormats(sheet);
      XLSX.utils.book_append_sheet(wb, ws, outputName);
    }

    appendStyleMetadataSheet(XLSX, wb, stylePayload);

    wb.Workbook = wb.Workbook || {};
    wb.Workbook.CalcPr = Object.assign({}, wb.Workbook.CalcPr || {}, { fullCalcOnLoad: true });

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
    updateFormatToolbar();
  }

  function clampEditorGeometryNumber(value, min, max) {
    const n = Number(value);
    if (!Number.isFinite(n)) return min;
    return Math.max(min, Math.min(max, Math.round(n)));
  }

  function normalizeEditorGeometry(geometry) {
    if (!geometry || typeof geometry !== "object") return null;

    const pad = 16;
    const viewportW = Math.max(320, window.innerWidth || 0);
    const viewportH = Math.max(260, window.innerHeight || 0);

    const maxW = Math.max(320, viewportW - pad);
    const maxH = Math.max(260, viewportH - pad);

    return {
      width: clampEditorGeometryNumber(geometry.width, 320, maxW),
      height: clampEditorGeometryNumber(geometry.height, 260, maxH)
    };
  }

  function applyEditorGeometry() {
    const box = modal ? modal.querySelector(".spreadsheetEditorBox") : null;
    if (!box) return;

    box.style.removeProperty("position");
    box.style.removeProperty("left");
    box.style.removeProperty("top");
    box.style.removeProperty("margin");

    const geometry = normalizeEditorGeometry(state.editorGeometry);
    if (!geometry) {
      box.style.removeProperty("width");
      box.style.removeProperty("height");
      box.style.removeProperty("max-width");
      box.style.removeProperty("max-height");
      return;
    }

    // Keep the editor centered by CSS, but inherit the preview size.
    box.style.width = `${geometry.width}px`;
    box.style.height = `${geometry.height}px`;
    box.style.maxWidth = "calc(100vw - 16px)";
    box.style.maxHeight = "calc(100vh - 16px)";
  }

  let editorDragState = null;
  let editorResizeState = null;

  function clampEditorWindowNumber(value, min, max) {
    const n = Number(value);
    if (!Number.isFinite(n)) return min;
    return Math.max(min, Math.min(max, Math.round(n)));
  }

  function editorWindowBounds() {
    const pad = 8;
    const viewportW = Math.max(320, window.innerWidth || 0);
    const viewportH = Math.max(260, window.innerHeight || 0);
    const maxW = Math.max(320, viewportW - pad * 2);
    const maxH = Math.max(260, viewportH - pad * 2);

    return {
      pad,
      viewportW,
      viewportH,
      minW: Math.min(720, maxW),
      minH: Math.min(420, maxH),
      maxW,
      maxH
    };
  }

  function makeEditorWindowDetached(box) {
    if (!box) return null;

    const rect = box.getBoundingClientRect();
    const b = editorWindowBounds();

    const width = clampEditorWindowNumber(rect.width, b.minW, b.maxW);
    const height = clampEditorWindowNumber(rect.height, b.minH, b.maxH);
    const left = clampEditorWindowNumber(rect.left, b.pad, Math.max(b.pad, b.viewportW - width - b.pad));
    const top = clampEditorWindowNumber(rect.top, b.pad, Math.max(b.pad, b.viewportH - height - b.pad));

    box.style.position = "fixed";
    box.style.left = `${left}px`;
    box.style.top = `${top}px`;
    box.style.width = `${width}px`;
    box.style.height = `${height}px`;
    box.style.maxWidth = "calc(100vw - 16px)";
    box.style.maxHeight = "calc(100vh - 16px)";
    box.style.margin = "0";

    return { left, top, width, height };
  }

  function beginEditorDrag(ev, box) {
    if (!box || !ev || ev.button !== 0) return;

    const target = ev.target;
    if (target && target.closest && target.closest("button, a, input, textarea, select, [data-spreadsheet-editor-resize]")) {
      return;
    }

    const detached = makeEditorWindowDetached(box);
    if (!detached) return;

    ev.preventDefault();

    editorDragState = {
      startX: ev.clientX,
      startY: ev.clientY,
      left: detached.left,
      top: detached.top,
      width: detached.width,
      height: detached.height
    };

    document.body.classList.add("spreadsheetEditorDragging");

    const onMove = (moveEv) => {
      if (!editorDragState) return;

      const b = editorWindowBounds();
      const nextLeft = clampEditorWindowNumber(
        editorDragState.left + moveEv.clientX - editorDragState.startX,
        b.pad,
        Math.max(b.pad, b.viewportW - 80)
      );
      const nextTop = clampEditorWindowNumber(
        editorDragState.top + moveEv.clientY - editorDragState.startY,
        b.pad,
        Math.max(b.pad, b.viewportH - 60)
      );

      box.style.left = `${nextLeft}px`;
      box.style.top = `${nextTop}px`;
    };

    const onUp = () => {
      editorDragState = null;
      document.body.classList.remove("spreadsheetEditorDragging");
      document.removeEventListener("pointermove", onMove);
      document.removeEventListener("pointerup", onUp);
    };

    document.addEventListener("pointermove", onMove);
    document.addEventListener("pointerup", onUp, { once: true });
  }

  function beginEditorResize(ev, box, mode) {
    if (!box || !ev || ev.button !== 0) return;
    if (mode !== "right" && mode !== "bottom" && mode !== "corner") return;

    const detached = makeEditorWindowDetached(box);
    if (!detached) return;

    ev.preventDefault();
    ev.stopPropagation();

    editorResizeState = {
      mode,
      startX: ev.clientX,
      startY: ev.clientY,
      width: detached.width,
      height: detached.height
    };

    document.body.classList.add("spreadsheetEditorResizing");

    const onMove = (moveEv) => {
      if (!editorResizeState) return;

      const b = editorWindowBounds();

      if (editorResizeState.mode === "right" || editorResizeState.mode === "corner") {
        box.style.width = `${clampEditorWindowNumber(
          editorResizeState.width + moveEv.clientX - editorResizeState.startX,
          b.minW,
          b.maxW
        )}px`;
      }

      if (editorResizeState.mode === "bottom" || editorResizeState.mode === "corner") {
        box.style.height = `${clampEditorWindowNumber(
          editorResizeState.height + moveEv.clientY - editorResizeState.startY,
          b.minH,
          b.maxH
        )}px`;
      }
    };

    const onUp = () => {
      editorResizeState = null;
      document.body.classList.remove("spreadsheetEditorResizing");
      document.removeEventListener("pointermove", onMove);
      document.removeEventListener("pointerup", onUp);
    };

    document.addEventListener("pointermove", onMove);
    document.addEventListener("pointerup", onUp, { once: true });
  }

  function appendEditorResizeHandle(box, className, mode) {
    if (!box || box.querySelector(`[data-spreadsheet-editor-resize="${mode}"]`)) return;

    const handle = document.createElement("div");
    handle.className = `spreadsheetEditorResizeHandle ${className}`;
    handle.dataset.spreadsheetEditorResize = mode;
    handle.setAttribute("aria-hidden", "true");
    handle.addEventListener("pointerdown", (ev) => beginEditorResize(ev, box, mode));

    box.appendChild(handle);
  }

  function attachEditorWindowControls() {
    const box = modal ? modal.querySelector(".spreadsheetEditorBox") : null;
    const head = modal ? modal.querySelector(".spreadsheetEditorHead") : null;
    if (!box || !head) return;

    if (head.dataset.editorDragAttached !== "1") {
      head.dataset.editorDragAttached = "1";
      head.addEventListener("pointerdown", (ev) => beginEditorDrag(ev, box));
    }

    appendEditorResizeHandle(box, "spreadsheetEditorResizeRight", "right");
    appendEditorResizeHandle(box, "spreadsheetEditorResizeBottom", "bottom");
    appendEditorResizeHandle(box, "spreadsheetEditorResizeCorner", "corner");
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
            <button id="spreadsheetEditorBold" type="button" class="btn secondary spreadsheetToolBtn" aria-pressed="false" aria-label="${tr("filemgr.spreadsheet_editor.bold", null, "Bold")}" title="${tr("filemgr.spreadsheet_editor.bold", null, "Bold")}">
              <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M8 5h5a3.5 3.5 0 0 1 0 7H8z"></path><path d="M8 12h6a3.5 3.5 0 0 1 0 7H8z"></path></svg>
            </button>
            <button id="spreadsheetEditorItalic" type="button" class="btn secondary spreadsheetToolBtn" aria-pressed="false" aria-label="${tr("filemgr.spreadsheet_editor.italic", null, "Italic")}" title="${tr("filemgr.spreadsheet_editor.italic", null, "Italic")}">
              <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M10 5h8"></path><path d="M6 19h8"></path><path d="M15 5 9 19"></path></svg>
            </button>
            <button id="spreadsheetEditorUnderline" type="button" class="btn secondary spreadsheetToolBtn" aria-pressed="false" aria-label="${tr("filemgr.spreadsheet_editor.underline", null, "Underline")}" title="${tr("filemgr.spreadsheet_editor.underline", null, "Underline")}">
              <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M7 5v6a5 5 0 0 0 10 0V5"></path><path d="M5 21h14"></path></svg>
            </button>
            <select id="spreadsheetEditorFontSize" class="spreadsheetFontSizeSelect" aria-label="${tr("filemgr.spreadsheet_editor.font_size", null, "Font size")}" title="${tr("filemgr.spreadsheet_editor.font_size", null, "Font size")}">
              <option value="">${tr("filemgr.spreadsheet_editor.font_size_default", null, "Size")}</option>
              ${FONT_SIZE_OPTIONS.map((size) => `<option value="${size}">${size}</option>`).join("")}
            </select>
            <button id="spreadsheetEditorAlignLeft" type="button" class="btn secondary spreadsheetToolBtn" aria-pressed="false" aria-label="${tr("filemgr.spreadsheet_editor.align_left", null, "Align left")}" title="${tr("filemgr.spreadsheet_editor.align_left", null, "Align left")}">
              <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M5 6h14"></path><path d="M5 10h10"></path><path d="M5 14h14"></path><path d="M5 18h8"></path></svg>
            </button>
            <button id="spreadsheetEditorAlignCenter" type="button" class="btn secondary spreadsheetToolBtn" aria-pressed="false" aria-label="${tr("filemgr.spreadsheet_editor.align_center", null, "Align center")}" title="${tr("filemgr.spreadsheet_editor.align_center", null, "Align center")}">
              <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M5 6h14"></path><path d="M7 10h10"></path><path d="M5 14h14"></path><path d="M8 18h8"></path></svg>
            </button>
            <button id="spreadsheetEditorTextColor" type="button" class="btn secondary spreadsheetToolBtn spreadsheetTextToolBtn" aria-pressed="false" aria-label="${tr("filemgr.spreadsheet_editor.text_color", null, "Text color")}" title="${tr("filemgr.spreadsheet_editor.text_color", null, "Text color")}">
              <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 4 6 20"></path><path d="M12 4 18 20"></path><path d="M8 14h8"></path><path d="M5 22h14"></path></svg>
            </button>
            <button id="spreadsheetEditorFill" type="button" class="btn secondary spreadsheetToolBtn spreadsheetFillToolBtn" aria-pressed="false" aria-label="${tr("filemgr.spreadsheet_editor.fill_color", null, "Fill color")}" title="${tr("filemgr.spreadsheet_editor.fill_color", null, "Fill color")}">
              <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M4 14 12 6l6 6-8 8z"></path><path d="M14 4 20 10"></path><path d="M4 14h16"></path></svg>
            </button>
            <span class="spreadsheetToolSep" aria-hidden="true"></span>
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
    boldBtn = modal.querySelector("#spreadsheetEditorBold");
    italicBtn = modal.querySelector("#spreadsheetEditorItalic");
    underlineBtn = modal.querySelector("#spreadsheetEditorUnderline");
    fontSizeSelect = modal.querySelector("#spreadsheetEditorFontSize");
    alignLeftBtn = modal.querySelector("#spreadsheetEditorAlignLeft");
    alignCenterBtn = modal.querySelector("#spreadsheetEditorAlignCenter");
    textColorBtn = modal.querySelector("#spreadsheetEditorTextColor");
    fillBtn = modal.querySelector("#spreadsheetEditorFill");
    closeBtn = modal.querySelector("#spreadsheetEditorClose");

    boldBtn?.addEventListener("click", () => applyFormatCommand("bold"));
    italicBtn?.addEventListener("click", () => applyFormatCommand("italic"));
    underlineBtn?.addEventListener("click", () => applyFormatCommand("underline"));
    fontSizeSelect?.addEventListener("change", () => applyFormatCommand("fontSize", fontSizeSelect.value));
    alignLeftBtn?.addEventListener("click", () => applyFormatCommand("align", "left"));
    alignCenterBtn?.addEventListener("click", () => applyFormatCommand("align", "center"));
    textColorBtn?.addEventListener("click", (ev) => {
      ev.preventDefault();
      ev.stopPropagation();
      hideAxisMenu();
      hideFillMenu();
      openTextColorMenu();
    });
    fillBtn?.addEventListener("click", (ev) => {
      ev.preventDefault();
      ev.stopPropagation();
      hideAxisMenu();
      hideTextColorMenu();
      openFillMenu();
    });
    saveBtn?.addEventListener("click", saveCurrent);
    addRowBtn?.addEventListener("click", addRow);
    addColBtn?.addEventListener("click", addColumn);
    closeBtn?.addEventListener("click", close);

    modal.addEventListener("click", (ev) => {
      hideTextColorMenu();
      hideFillMenu();
      hideAxisMenu();
      if (ev.target === modal) close();
    });

    document.addEventListener("keydown", (ev) => {
      if (!modal.classList.contains("show")) return;
      if ((ev.ctrlKey || ev.metaKey) && ev.key.toLowerCase() === "s") {
        ev.preventDefault();
        saveCurrent();
      }
      if (ev.key === "Escape") {
        if (hideTextColorMenu() || hideFillMenu() || hideAxisMenu()) {
          ev.preventDefault();
          return;
        }
        close();
      }
    });
  }

  function show() {
    ensureModal();

    attachEditorWindowControls();
    const box = modal ? modal.querySelector(".spreadsheetEditorBox") : null;
    if (box) {
      box.style.removeProperty("position");
      box.style.removeProperty("left");
      box.style.removeProperty("top");
      box.style.removeProperty("width");
      box.style.removeProperty("height");
      box.style.removeProperty("max-width");
      box.style.removeProperty("max-height");
      box.style.removeProperty("margin");
    }

    modal.classList.add("show");
    modal.setAttribute("aria-hidden", "false");
  }

  function confirmDiscardChanges() {
    if (!state.dirty) return Promise.resolve(true);

    if (confirmModal) {
      confirmModal.remove();
      confirmModal = null;
    }

    return new Promise((resolve) => {
      let done = false;

      const finish = (ok) => {
        if (done) return;
        done = true;

        document.removeEventListener("keydown", onKeyDown);
        if (confirmModal) {
          confirmModal.remove();
          confirmModal = null;
        }

        resolve(!!ok);
      };

      const onKeyDown = (ev) => {
        if (ev.key === "Escape") {
          ev.preventDefault();
          finish(false);
        }
      };

      confirmModal = document.createElement("div");
      confirmModal.className = "spreadsheetEditorConfirmModal";
      confirmModal.setAttribute("role", "presentation");

      const box = document.createElement("div");
      box.className = "spreadsheetEditorConfirmBox";
      box.setAttribute("role", "dialog");
      box.setAttribute("aria-modal", "true");
      box.setAttribute("aria-labelledby", "spreadsheetEditorConfirmTitle");
      box.setAttribute("aria-describedby", "spreadsheetEditorConfirmText");

      const title = document.createElement("div");
      title.id = "spreadsheetEditorConfirmTitle";
      title.className = "spreadsheetEditorConfirmTitle";
      title.textContent = tr("filemgr.spreadsheet_editor.discard_title", null, "Unsaved changes");

      const msg = document.createElement("div");
      msg.id = "spreadsheetEditorConfirmText";
      msg.className = "spreadsheetEditorConfirmText";
      msg.textContent = tr("filemgr.spreadsheet_editor.discard_close", null, "Discard unsaved spreadsheet changes?");

      const actions = document.createElement("div");
      actions.className = "spreadsheetEditorConfirmActions";

      const keepBtn = document.createElement("button");
      keepBtn.type = "button";
      keepBtn.className = "btn secondary";
      keepBtn.textContent = tr("filemgr.spreadsheet_editor.keep_editing", null, "Keep editing");

      const discardBtn = document.createElement("button");
      discardBtn.type = "button";
      discardBtn.className = "btn";
      discardBtn.textContent = tr("filemgr.spreadsheet_editor.discard_changes", null, "Discard changes");

      actions.appendChild(keepBtn);
      actions.appendChild(discardBtn);
      box.appendChild(title);
      box.appendChild(msg);
      box.appendChild(actions);
      confirmModal.appendChild(box);

      confirmModal.addEventListener("click", (ev) => {
        if (ev.target === confirmModal) finish(false);
      });
      keepBtn.addEventListener("click", () => finish(false));
      discardBtn.addEventListener("click", () => finish(true));

      document.addEventListener("keydown", onKeyDown);
      document.body.appendChild(confirmModal);

      window.setTimeout(() => keepBtn.focus(), 0);
    });
  }

  async function close() {
    if (!modal || !modal.classList.contains("show")) return;

    const discard = await confirmDiscardChanges();
    if (!discard) return;

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
    formulaFocus = null;

    const sheet = state.sheets[state.active] || { rows: [] };
    const rows = Array.isArray(sheet.rows) ? sheet.rows : [];
    const colCount = rows.reduce((m, row) => Math.max(m, Array.isArray(row) ? row.length : 0), 0);
    const colWidths = ensureSheetColWidths(sheet, colCount);
    const cache = computeSheetCache(sheet);

    const table = document.createElement("table");
    table.className = "spreadsheetEditorTable";
    table.setAttribute("aria-label", tr("filemgr.spreadsheet_editor.cell_grid", null, "Editable spreadsheet cells"));
    attachSpreadsheetSelectionHandlers(table);
    attachHeaderSelectionHandlers(table);

    const colgroup = document.createElement("colgroup");
    const rowHeadCol = document.createElement("col");
    rowHeadCol.style.width = "44px";
    colgroup.appendChild(rowHeadCol);

    for (let c = 0; c < colCount; c++) {
      const colEl = document.createElement("col");
      colEl.dataset.col = String(c);
      colEl.style.width = `${clampColumnWidth(colWidths[c])}px`;
      colgroup.appendChild(colEl);
    }

    table.appendChild(colgroup);

    const thead = document.createElement("thead");
    const headRow = document.createElement("tr");
    const corner = document.createElement("th");
    corner.className = "rowHead corner";
    headRow.appendChild(corner);

    for (let c = 0; c < colCount; c++) {
      const th = document.createElement("th");
      th.className = "colHead";
      th.dataset.col = String(c);
      th.tabIndex = 0;
      applyColumnWidth(th, colWidths[c]);

      const colLabel = document.createElement("span");
      colLabel.className = "spreadsheetColumnLabel";
      colLabel.textContent = columnName(c);

      const resizeHandle = document.createElement("span");
      resizeHandle.className = "spreadsheetColumnResize";
      resizeHandle.setAttribute("role", "separator");
      resizeHandle.setAttribute("aria-orientation", "vertical");
      resizeHandle.title = tr("filemgr.spreadsheet_editor.resize_column", null, "Drag to resize column");
      resizeHandle.addEventListener("pointerdown", (ev) => startColumnResize(ev, c));
      resizeHandle.addEventListener("click", (ev) => {
        ev.preventDefault();
        ev.stopPropagation();
      });

      th.appendChild(colLabel);
      th.appendChild(resizeHandle);
      th.title = tr("filemgr.spreadsheet_editor.select_column", { col: columnName(c) }, `Select column ${columnName(c)}`);
      th.addEventListener("click", () => selectSpreadsheetAxis("column", c));
      th.addEventListener("keydown", (ev) => {
        if (ev.key === "Enter" || ev.key === " ") {
          ev.preventDefault();
          selectSpreadsheetAxis("column", c);
        }
      });
      headRow.appendChild(th);
    }

    thead.appendChild(headRow);
    table.appendChild(thead);

    const tbody = document.createElement("tbody");

    rows.forEach((row, rIdx) => {
      const trEl = document.createElement("tr");
      const rh = document.createElement("th");
      rh.className = "rowHead";
      rh.dataset.row = String(rIdx);
      rh.tabIndex = 0;
      rh.textContent = String(rIdx + 1);
      rh.title = tr("filemgr.spreadsheet_editor.select_row", { row: String(rIdx + 1) }, `Select row ${rIdx + 1}`);
      rh.addEventListener("click", () => selectSpreadsheetAxis("row", rIdx));
      rh.addEventListener("keydown", (ev) => {
        if (ev.key === "Enter" || ev.key === " ") {
          ev.preventDefault();
          selectSpreadsheetAxis("row", rIdx);
        }
      });
      trEl.appendChild(rh);

      for (let c = 0; c < colCount; c++) {
        const td = document.createElement("td");
        td.dataset.row = String(rIdx);
        td.dataset.col = String(c);
        applyColumnWidth(td, colWidths[c]);

        const input = document.createElement("input");

        input.type = "text";
        input.value = displayCellValue(sheet, rIdx, c, cache);
        input.dataset.row = String(rIdx);
        input.dataset.col = String(c);
        applyColumnWidth(input, colWidths[c]);
        input.title = isFormulaValue(cellRaw(sheet, rIdx, c)) ? cellRaw(sheet, rIdx, c) : "";
        input.disabled = state.readOnly || state.tooLarge;

        input.addEventListener("pointerdown", (ev) => {
          if (!formulaFocus || formulaFocus.input === input) return;
          const active = formulaFocus.input;
          if (!active || !String(active.value || "").startsWith("=")) return;
          ev.preventDefault();
          insertFormulaReference(active, rIdx, c);
        });

        input.addEventListener("pointerdown", (ev) => {
          beginCellRangePointer(ev, rIdx, c);
        });

        input.addEventListener("focus", () => {
          const r = Number(input.dataset.row);
          const col = Number(input.dataset.col);
          state.activeCell = Number.isInteger(r) && Number.isInteger(col) ? { row: r, col } : null;

          if (state.selection || state.rangeSelection) {
            state.selection = null;
            state.rangeSelection = null;
          }

          hideAxisMenu();
          hideFillMenu();
          hideTextColorMenu();
          repaintSpreadsheetSelection();
          updateFormatToolbar();

          const raw = cellRaw(sheet, r, col);

          input.value = raw;

          if (String(raw || "").startsWith("=")) {
            formulaFocus = {
              input,
              row: r,
              col,
              originalRaw: raw,
              wasDirty: state.dirty
            };
          } else if (formulaFocus && formulaFocus.input === input) {
            formulaFocus = null;
          }
        });

        input.addEventListener("blur", () => {
          const r = Number(input.dataset.row);
          const col = Number(input.dataset.col);
          if (!Number.isInteger(r) || !Number.isInteger(col)) return;
          if (formulaFocus && formulaFocus.input === input) formulaFocus = null;
          refreshFormulaDisplays(null);
          input.value = displayCellValue(sheet, r, col);
          input.title = isFormulaValue(cellRaw(sheet, r, col)) ? cellRaw(sheet, r, col) : "";
        });

        input.addEventListener("keydown", (ev) => {
          if (ev.key === "Escape") {
            const r = Number(input.dataset.row);
            const col = Number(input.dataset.col);
            const raw = Number.isInteger(r) && Number.isInteger(col) ? cellRaw(sheet, r, col) : "";
            const editingFormula =
              (formulaFocus && formulaFocus.input === input) ||
              String(input.value || "").startsWith("=") ||
              isFormulaValue(raw);

            if (editingFormula) {
              ev.preventDefault();
              ev.stopPropagation();
              cancelFormulaEdit(input);
              input.blur();
              return;
            }
          }

          if (ev.key === "Enter") {
            ev.preventDefault();
            input.blur();
          }
        });

        input.addEventListener("input", () => {
          const r = Number(input.dataset.row);
          const col = Number(input.dataset.col);
          if (!Number.isInteger(r) || !Number.isInteger(col)) return;
          if (!state.sheets[state.active] || !state.sheets[state.active].rows[r]) return;

          const previousRaw = cellRaw(state.sheets[state.active], r, col);
          const previousFormulaFocus = formulaFocus && formulaFocus.input === input ? formulaFocus : null;

          setCellRaw(state.sheets[state.active], r, col, input.value);

          if (String(input.value || "").startsWith("=")) {
            formulaFocus = {
              input,
              row: r,
              col,
              originalRaw: previousFormulaFocus ? previousFormulaFocus.originalRaw : previousRaw,
              wasDirty: previousFormulaFocus ? previousFormulaFocus.wasDirty : state.dirty
            };
          } else if (formulaFocus && formulaFocus.input === input) {
            formulaFocus = null;
          }

          setDirty(true);
          refreshFormulaDisplays(input);
        });

        // Security: cell content is edited through input.value and never
        // injected as HTML, so workbook text cannot become executable markup.
        input.addEventListener("contextmenu", (ev) => openCellBorderMenu(ev, input));

        td.appendChild(input);
        applyCellFormatToInput(input, getCellFormat(sheet, rIdx, c));

        trEl.appendChild(td);
      }

      tbody.appendChild(trEl);
    });

    table.appendChild(tbody);
    bodyEl.appendChild(table);
    repaintSpreadsheetSelection();
    updateButtons();
    updateFormatToolbar();

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

  function adjustSingleFormulaRef(refText, axis, index, delta) {
    const ref = parseCellRef(refText);
    if (!ref) return refText;

    if (axis === "row") {
      if (delta > 0) {
        if (ref.row >= index) ref.row += delta;
      } else if (delta < 0) {
        if (ref.row === index) return "#REF!";
        if (ref.row > index) ref.row += delta;
      }
    } else if (axis === "column") {
      if (delta > 0) {
        if (ref.col >= index) ref.col += delta;
      } else if (delta < 0) {
        if (ref.col === index) return "#REF!";
        if (ref.col > index) ref.col += delta;
      }
    }

    if (ref.row < 0 || ref.col < 0) return "#REF!";
    return coordToRef(ref.row, ref.col);
  }

  function adjustRangeFormulaRef(startText, endText, axis, index, delta) {
    const start = parseCellRef(startText);
    const end = parseCellRef(endText);

    if (!start || !end) return `${startText}:${endText}`;

    const adjustRangeAxis = (prop) => {
      const a = start[prop];
      const b = end[prop];

      if (delta > 0) {
        if (start[prop] >= index) start[prop] += delta;
        if (end[prop] >= index) end[prop] += delta;
        return true;
      }

      if (delta < 0) {
        const min = Math.min(a, b);
        const max = Math.max(a, b);

        if (index < min) {
          start[prop] += delta;
          end[prop] += delta;
          return true;
        }

        if (index > max) {
          return true;
        }

        if (min === max) {
          return false;
        }

        // Delete inside range: shrink the range. If the deleted row/column is
        // the upper endpoint, keep that endpoint because following cells shift
        // into its place. If it is the lower endpoint, move it one step back.
        if (start[prop] > index || (start[prop] === index && start[prop] === max)) {
          start[prop] += delta;
        }
        if (end[prop] > index || (end[prop] === index && end[prop] === max)) {
          end[prop] += delta;
        }

        return start[prop] >= 0 && end[prop] >= 0;
      }

      return true;
    };

    const ok = axis === "row"
      ? adjustRangeAxis("row")
      : adjustRangeAxis("col");

    if (!ok || start.row < 0 || start.col < 0 || end.row < 0 || end.col < 0) {
      return "#REF!";
    }

    return `${coordToRef(start.row, start.col)}:${coordToRef(end.row, end.col)}`;
  }

  function adjustFormulaReferences(formulaBody, axis, index, delta) {
    const placeholders = [];

    // Security: formula text is transformed as inert text only. It is never
    // executed, and only same-sheet A1/range references are adjusted.
    let out = String(formulaBody || "").replace(
      /\b([A-Z]{1,3}[1-9][0-9]*):([A-Z]{1,3}[1-9][0-9]*)\b/g,
      (_match, startRef, endRef) => {
        const token = `__PQNAS_RANGE_${placeholders.length}__`;
        placeholders.push(adjustRangeFormulaRef(startRef, endRef, axis, index, delta));
        return token;
      }
    );

    out = out.replace(/\b([A-Z]{1,3}[1-9][0-9]*)\b/g, (_match, refText) => {
      return adjustSingleFormulaRef(refText, axis, index, delta);
    });

    placeholders.forEach((value, idx) => {
      out = out.replace(`__PQNAS_RANGE_${idx}__`, value);
    });

    return out;
  }

  function adjustSheetFormulasForAxisChange(sheet, axis, index, delta) {
    if (!sheet || !Array.isArray(sheet.rows)) return;
    if ((axis !== "row" && axis !== "column") || !Number.isInteger(index) || index < 0) return;
    if (delta !== 1 && delta !== -1) return;

    for (const row of sheet.rows) {
      if (!Array.isArray(row)) continue;

      for (let c = 0; c < row.length; c++) {
        const raw = row[c] == null ? "" : String(row[c]);
        if (!isFormulaValue(raw)) continue;

        row[c] = "=" + adjustFormulaReferences(raw.slice(1), axis, index, delta);
      }
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
    const insertAt = state.selection && state.selection.type === "row" && Number.isInteger(state.selection.index)
      ? Math.max(0, Math.min(state.selection.index, sheet.rows.length))
      : sheet.rows.length;

    adjustSheetFormulasForAxisChange(sheet, "row", insertAt, 1);
    ensureSheetCellFormats(sheet, sheet.rows.length, cols);
    sheet.cellFormats.splice(insertAt, 0, Array.from({ length: cols }, () => null));
    sheet.rows.splice(insertAt, 0, Array.from({ length: cols }, () => ""));

    state.selection = { type: "row", index: insertAt };
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

    const insertAt = state.selection && state.selection.type === "column" && Number.isInteger(state.selection.index)
      ? Math.max(0, Math.min(state.selection.index, cols))
      : cols;

    adjustSheetFormulasForAxisChange(sheet, "column", insertAt, 1);
    ensureSheetColWidths(sheet, cols);
    ensureSheetCellFormats(sheet, sheet.rows.length, cols);
    sheet.colWidths.splice(insertAt, 0, DEFAULT_COL_WIDTH);
    for (const fmtRow of sheet.cellFormats) {
      if (Array.isArray(fmtRow)) fmtRow.splice(insertAt, 0, null);
    }

    if (!sheet.rows.length) {
      sheet.rows.push(Array.from({ length: Math.max(1, insertAt + 1) }, () => ""));
    }

    for (const row of sheet.rows) {
      while (row.length < cols) row.push("");
      row.splice(insertAt, 0, "");
    }

    state.selection = { type: "column", index: insertAt };
    setDirty(true);
    render();
  }

  function deleteSelectedAxis(type, index) {
    if (state.readOnly || state.tooLarge) return;

    const sheet = state.sheets[state.active];
    if (!sheet || !Array.isArray(sheet.rows)) return;

    if (type === "row") {
      if (!Number.isInteger(index) || index < 0 || index >= sheet.rows.length) return;

      const cols = Math.max(
        DEFAULT_COLS,
        sheet.rows.reduce((m, row) => Math.max(m, Array.isArray(row) ? row.length : 0), 0)
      );

      adjustSheetFormulasForAxisChange(sheet, "row", index, -1);
      ensureSheetCellFormats(sheet);
      sheet.cellFormats.splice(index, 1);
      sheet.rows.splice(index, 1);

      if (!sheet.rows.length) {
        sheet.rows.push(Array.from({ length: cols }, () => ""));
      }

      state.selection = {
        type: "row",
        index: Math.min(index, sheet.rows.length - 1)
      };

      setDirty(true);
      render();
      return;
    }

    if (type === "column") {
      const colCount = sheet.rows.reduce((m, row) => Math.max(m, Array.isArray(row) ? row.length : 0), 0);
      if (!Number.isInteger(index) || index < 0 || index >= colCount) return;

      if (!sheet.rows.length) {
        sheet.rows.push(Array.from({ length: DEFAULT_COLS }, () => ""));
      }

      adjustSheetFormulasForAxisChange(sheet, "column", index, -1);
      ensureSheetColWidths(sheet, colCount);
      ensureSheetCellFormats(sheet, sheet.rows.length, colCount);

      if (colCount <= 1) {
        sheet.colWidths = [DEFAULT_COL_WIDTH];
        sheet.cellFormats = sheet.rows.map(() => [null]);
        for (const row of sheet.rows) {
          if (!Array.isArray(row)) continue;
          row.splice(0, row.length, "");
        }

        state.selection = { type: "column", index: 0 };
      } else {
        sheet.colWidths.splice(index, 1);
        for (const fmtRow of sheet.cellFormats) {
          if (Array.isArray(fmtRow)) fmtRow.splice(index, 1);
        }

        for (const row of sheet.rows) {
          if (!Array.isArray(row)) continue;
          while (row.length < colCount) row.push("");
          row.splice(index, 1);
        }

        state.selection = {
          type: "column",
          index: Math.min(index, colCount - 2)
        };
      }

      setDirty(true);
      render();
    }
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
    state.selection = null;
    state.activeCell = null;
    state.rangeSelection = null;
    state.editorGeometry = null;

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
