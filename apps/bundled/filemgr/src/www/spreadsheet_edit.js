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
  const DEFAULT_ROW_HEIGHT = 34;
  const MIN_ROW_HEIGHT = 24;
  const MAX_ROW_HEIGHT = 220;
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
  const MIN_DECIMAL_PLACES = 0;
  const MAX_DECIMAL_PLACES = 10;

  let modal = null;
  let titleEl = null;
  let pathEl = null;
  let infoEl = null;
  let formulaBarNameEl = null;
  let formulaBarInput = null;
  let tabsEl = null;
  let bodyEl = null;
  let saveBtn = null;
  let addRowBtn = null;
  let addColBtn = null;
  let boldBtn = null;
  let italicBtn = null;
  let underlineBtn = null;
  let fontSizeSelect = null;
  let decreaseDecimalsBtn = null;
  let increaseDecimalsBtn = null;
  let alignLeftBtn = null;
  let alignCenterBtn = null;
  let valignTopBtn = null;
  let valignMiddleBtn = null;
  let valignBottomBtn = null;
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
  let spreadsheetHistory = null;
  let historyKeyboardAttached = false;
  let pendingCellEditHistory = null;

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

  function normalizeVerticalAlign(value) {
    const key = String(value || "").trim().toLowerCase();
    if (key === "center") return "middle";
    return key === "top" || key === "middle" || key === "bottom" ? key : "";
  }

  function cellVerticalAlignCss(value) {
    const key = normalizeVerticalAlign(value);
    return key === "top" ? "top" : key === "bottom" ? "bottom" : "middle";
  }

  function applyInputVerticalAlign(input, fmt) {
    if (!input) return;

    const f = normalizeCellFormat(fmt);
    const rowHeight = clampRowHeight(
      parseFloat(input.style.height) ||
      (typeof input.getBoundingClientRect === "function" ? input.getBoundingClientRect().height : 0) ||
      DEFAULT_ROW_HEIGHT
    );

    const fontPx = f.fontSize || 13;
    const lineHeight = Math.max(14, Math.ceil(fontPx * 1.35));
    const spare = Math.max(0, rowHeight - lineHeight);
    const edgePad = Math.min(8, Math.max(2, Math.floor(spare * 0.18)));

    let topPad = Math.floor(spare / 2);
    let bottomPad = spare - topPad;

    if (f.valign === "top") {
      topPad = spare > 0 ? Math.min(edgePad, spare) : 0;
      bottomPad = Math.max(0, spare - topPad);
    } else if (f.valign === "bottom") {
      bottomPad = spare > 0 ? Math.min(edgePad, spare) : 0;
      topPad = Math.max(0, spare - bottomPad);
    }

    // Native text inputs do not obey td vertical-align for their internal text.
    // Use padding/line-height on the input itself so editor view matches preview
    // and exported XLSX vertical alignment.
    input.style.lineHeight = `${lineHeight}px`;
    input.style.paddingTop = `${topPad}px`;
    input.style.paddingBottom = `${bottomPad}px`;
  }

  function cellVerticalAlignXlsx(value) {
    const key = normalizeVerticalAlign(value);
    if (key === "middle") return "center";
    return key === "top" || key === "bottom" ? key : "";
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

  function normalizeDecimalPlaces(value) {
    if (value == null || value === "") return null;

    const n = Number(value);
    if (!Number.isFinite(n)) return null;

    const rounded = Math.round(n);
    return Math.max(MIN_DECIMAL_PLACES, Math.min(MAX_DECIMAL_PLACES, rounded));
  }

  function decimalPlacesFromText(value) {
    const s = String(value == null ? "" : value).trim();
    const m = s.match(/^-?\d+(?:[.,](\d+))?$/);
    return m && m[1] ? Math.min(MAX_DECIMAL_PLACES, m[1].length) : 0;
  }

  function decimalPlacesFromNumber(value) {
    if (!Number.isFinite(Number(value))) return 0;

    const text = formatFormulaNumber(Number(value));
    const dot = text.indexOf(".");
    return dot >= 0 ? Math.min(MAX_DECIMAL_PLACES, text.length - dot - 1) : 0;
  }

  function formatDecimalNumber(value, decimals) {
    const places = normalizeDecimalPlaces(decimals);
    const n = Number(value);

    if (places == null || !Number.isFinite(n)) {
      return String(value == null ? "" : value);
    }

    return n.toFixed(places);
  }

  function inferCellDecimalPlaces(sheet, row, col, cache = null) {
    const raw = cellRaw(sheet, row, col);

    if (isFormulaValue(raw)) {
      const effectiveCache = cache || computeSheetCache(sheet);
      const result = evaluateCell(sheet, row, col, effectiveCache, new Set());
      return typeof result.value === "number" && Number.isFinite(result.value)
        ? decimalPlacesFromNumber(result.value)
        : 0;
    }

    const parsed = parsePlainNumber(raw);
    if (parsed.blank || typeof parsed.number !== "number") return 0;

    return decimalPlacesFromText(raw);
  }

  function normalizeCellFormat(fmt) {
    const src = fmt && typeof fmt === "object" ? fmt : {};
    const align = src.align === "center" || src.align === "left" ? src.align : "";
    const valign = normalizeVerticalAlign(src.valign || src.verticalAlign || src.vertical);
    const decimals = normalizeDecimalPlaces(src.decimals);
    return {
      bold: !!src.bold,
      italic: !!src.italic,
      underline: !!src.underline,
      fontSize: normalizeFontSize(src.fontSize || src.sz),
      decimals,
      align,
      valign,
      bg: normalizeFillColorKey(src.bg),
      fg: normalizeTextColorKey(src.fg),
      border: normalizeBorderFormat(src.border)
    };
  }

  function isEmptyCellFormat(fmt) {
    const f = normalizeCellFormat(fmt);
    return !f.bold && !f.italic && !f.underline && !f.fontSize && f.decimals == null && !f.align && !f.valign && !f.bg && !f.fg && isEmptyBorderFormat(f.border);
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
    applyInputVerticalAlign(input, f);

    // The td owns vertical alignment metadata for table layout/preview parity.
    // The input gets matching padding/line-height above for visible edit mode.
    if (cell) {
      cell.style.verticalAlign = cellVerticalAlignCss(f.valign);
    }

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

    if (state.selection) {
      const api = FM && FM.spreadsheetAxis;
      if (api && typeof api.targetCells === "function") {
        return api.targetCells(state.selection, rowCount, colCount);
      }
      return [];
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

  function focusSpreadsheetCell(row, col, options = {}) {
    if (!bodyEl || !Number.isInteger(row) || !Number.isInteger(col) || row < 0 || col < 0) {
      return false;
    }

    const input = bodyEl.querySelector(`input[data-row="${row}"][data-col="${col}"]`);
    if (!input || input.disabled) return false;

    try {
      input.focus({ preventScroll: true });
    } catch (_) {
      input.focus();
    }

    if (options.select) {
      try {
        input.select();
      } catch (_) {}
    } else if (options.end) {
      const len = String(input.value || "").length;
      try {
        input.setSelectionRange(len, len);
      } catch (_) {}
    }

    try {
      input.scrollIntoView({ block: "nearest", inline: "nearest" });
    } catch (_) {}

    return true;
  }

  function navigateSpreadsheetCell(input, rowDelta, colDelta) {
    if (!input || !input.dataset) return false;

    const row = Number(input.dataset.row);
    const col = Number(input.dataset.col);

    if (!Number.isInteger(row) || !Number.isInteger(col)) return false;

    const nextRow = row + rowDelta;
    const nextCol = col + colDelta;

    if (nextRow < 0 || nextCol < 0) return false;

    return focusSpreadsheetCell(nextRow, nextCol, { select: true });
  }

  function shouldSpreadsheetArrowNavigate(input, key) {
    if (!input) return false;

    if (key === "ArrowUp" || key === "ArrowDown") {
      return true;
    }

    const value = String(input.value || "");
    const start = Number.isInteger(input.selectionStart) ? input.selectionStart : 0;
    const end = Number.isInteger(input.selectionEnd) ? input.selectionEnd : start;

    if (start !== end) return true;
    if (key === "ArrowLeft") return start <= 0;
    if (key === "ArrowRight") return end >= value.length;

    return false;
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

    const historyBefore = captureHistorySnapshot();
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

    commitHistorySnapshot(historyBefore);
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

    const historyBefore = captureHistorySnapshot();

    let enable = true;
    if (kind === "bold" || kind === "italic" || kind === "underline") {
      enable = !cells.every(({ row, col }) => !!getCellFormat(sheet, row, col)[kind]);
    }

    const decimalCache = kind === "decimals" ? computeSheetCache(sheet) : null;
    const decimalDelta = value === "increase" ? 1 : value === "decrease" ? -1 : 0;

    for (const { row, col } of cells) {
      const fmt = getCellFormat(sheet, row, col);

      if (kind === "bold" || kind === "italic" || kind === "underline") {
        fmt[kind] = enable;
      } else if (kind === "fontSize") {
        fmt.fontSize = normalizeFontSize(value);
      } else if (kind === "align") {
        fmt.align = value === "center" ? "center" : "left";
      } else if (kind === "valign") {
        fmt.valign = normalizeVerticalAlign(value);
      } else if (kind === "decimals") {
        const current = fmt.decimals == null
          ? inferCellDecimalPlaces(sheet, row, col, decimalCache)
          : fmt.decimals;
        fmt.decimals = normalizeDecimalPlaces(current + decimalDelta);
      } else if (kind === "bg") {
        fmt.bg = normalizeFillColorKey(value);
      } else if (kind === "fg") {
        fmt.fg = normalizeTextColorKey(value);
      }

      setCellFormat(sheet, row, col, fmt);
      paintVisibleCellFormat(row, col);

      if (kind === "decimals") {
        const refreshDecimalCell = () => syncVisibleInputForCell(row, col, { forceValue: true });

        refreshDecimalCell();

        // Toolbar clicks can interleave with input focus/blur. Refresh once more
        // after the browser has settled focus so decimal formatting is visible
        // immediately instead of only after another click.
        window.requestAnimationFrame(refreshDecimalCell);
      }
    }

    commitHistorySnapshot(historyBefore);
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

    for (const btn of [boldBtn, italicBtn, underlineBtn, decreaseDecimalsBtn, increaseDecimalsBtn, alignLeftBtn, alignCenterBtn, valignTopBtn, valignMiddleBtn, valignBottomBtn, textColorBtn, fillBtn]) {
      if (btn) btn.disabled = disabled;
    }
    if (fontSizeSelect) fontSizeSelect.disabled = disabled;

    setToolButtonActive(boldBtn, !!fmt.bold);
    setToolButtonActive(italicBtn, !!fmt.italic);
    setToolButtonActive(underlineBtn, !!fmt.underline);
    if (fontSizeSelect) {
      fontSizeSelect.value = fmt.fontSize ? String(fmt.fontSize) : "";
    }
    setToolButtonActive(decreaseDecimalsBtn, false);
    setToolButtonActive(increaseDecimalsBtn, false);
    setToolButtonActive(alignLeftBtn, fmt.align === "left" || !fmt.align);
    setToolButtonActive(alignCenterBtn, fmt.align === "center");
    setToolButtonActive(valignTopBtn, fmt.valign === "top");
    setToolButtonActive(valignMiddleBtn, fmt.valign === "middle" || !fmt.valign);
    setToolButtonActive(valignBottomBtn, fmt.valign === "bottom");
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

    updateFormulaBar();
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

    const outputValign = cellVerticalAlignXlsx(f.valign);
    if (f.align || outputValign) {
      style.alignment = {};
      if (f.align) style.alignment.horizontal = f.align;
      if (outputValign) style.alignment.vertical = outputValign;
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

  function xlsxCleanXmlText(value) {
    return String(value == null ? "" : value)
      .replace(/[\x00-\x08\x0B\x0C\x0E-\x1F]/g, "");
  }

  function xlsxXmlEscape(value) {
    return xlsxCleanXmlText(value)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;");
  }

  function xlsxAttrEscape(value) {
    return xlsxXmlEscape(value).replace(/"/g, "&quot;");
  }

  function xlsxCellRef(row, col) {
    return `${columnName(col)}${row + 1}`;
  }

  function xlsxDimensionRef(rowCount, colCount) {
    const rows = Math.max(1, Number(rowCount) || 1);
    const cols = Math.max(1, Number(colCount) || 1);
    return `A1:${xlsxCellRef(rows - 1, cols - 1)}`;
  }

  function xlsxColumnPixelWidthToExcelWidth(px) {
    const widthPx = clampColumnWidth(px);

    // Excel stores column widths in character units, not pixels. This mirrors
    // the common Calibri 11 approximation more closely than a simple /8.
    if (widthPx <= 12) return 1;
    return Math.max(1, Math.round(((widthPx - 5) / 7) * 100) / 100);
  }

  function xlsxRowPixelHeightToPointHeight(px) {
    const heightPx = clampRowHeight(px);

    // Excel row heights are points. Browser CSS pixels are approximately
    // 0.75 pt at 96 DPI, so this is intentionally an approximation.
    return Math.max(1, Math.round((heightPx * 0.75) * 100) / 100);
  }

  function xlsxRowHeightForSheetRow(sheet, rowIndex, colCount) {
    const explicitHeights = ensureSheetRowHeights(sheet, Math.max(rowIndex + 1, 0));
    const explicitPx = explicitHeights[rowIndex];

    if (Number.isFinite(explicitPx) && clampRowHeight(explicitPx) !== DEFAULT_ROW_HEIGHT) {
      return xlsxRowPixelHeightToPointHeight(explicitPx);
    }

    let maxFontSize = 11;

    for (let c = 0; c < colCount; c++) {
      const fmt = getCellFormat(sheet, rowIndex, c);
      if (fmt.fontSize && fmt.fontSize > maxFontSize) {
        maxFontSize = fmt.fontSize;
      }
    }

    // Excel row heights are points. Add a little breathing room for borders and
    // underline so large font rows do not look clipped.
    return maxFontSize > 11 ? Math.ceil(maxFontSize * 1.35) : 0;
  }

  function xlsxNumberValue(value) {
    const n = Number(value);
    return Number.isFinite(n) ? String(n) : "";
  }

  function xlsxFormulaValueXml(value) {
    const formula = String(value == null ? "" : value).replace(/^=/, "");
    return xlsxXmlEscape(formula);
  }

  function xlsxInlineStringXml(value) {
    return `<is><t xml:space="preserve">${xlsxXmlEscape(value)}</t></is>`;
  }

  function xlsxUniqueSheetName(name, idx, used) {
    const base = safeSheetName(name, idx);
    let candidate = base;
    let n = 2;

    while (used.has(candidate)) {
      const suffix = ` ${n++}`;
      candidate = `${base.slice(0, Math.max(1, 31 - suffix.length))}${suffix}`;
    }

    used.add(candidate);
    return candidate;
  }

  function xlsxStyleFontKey(fmt) {
    const f = normalizeCellFormat(fmt);
    return JSON.stringify({
      bold: !!f.bold,
      italic: !!f.italic,
      underline: !!f.underline,
      fontSize: f.fontSize || 0,
      fg: f.fg || ""
    });
  }

  function xlsxStyleFillKey(fmt) {
    const f = normalizeCellFormat(fmt);
    return f.bg || "";
  }

  function xlsxStyleBorderKey(fmt) {
    const f = normalizeCellFormat(fmt);
    return JSON.stringify(normalizeBorderFormat(f.border));
  }

  function xlsxDecimalNumFmtCode(decimals) {
    const places = normalizeDecimalPlaces(decimals);
    if (places == null) return "";

    return places === 0 ? "0" : `0.${"0".repeat(places)}`;
  }

  function xlsxStyleNumberFormatKey(fmt) {
    const f = normalizeCellFormat(fmt);
    return xlsxDecimalNumFmtCode(f.decimals);
  }

  function buildXlsxStyleCatalog() {
    const fonts = [{
      xml: '<font><sz val="11"/><color theme="1"/><name val="Calibri"/><family val="2"/><scheme val="minor"/></font>'
    }];
    const fills = [
      { xml: '<fill><patternFill patternType="none"/></fill>' },
      { xml: '<fill><patternFill patternType="gray125"/></fill>' }
    ];
    const borders = [{
      xml: '<border><left/><right/><top/><bottom/><diagonal/></border>'
    }];
    const xfs = [{
      xml: '<xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0"/>'
    }];
    const numFmts = [];

    const fontIds = new Map([["", 0]]);
    const fillIds = new Map([["", 0]]);
    const borderIds = new Map([["", 0]]);
    const numFmtIds = new Map();
    const xfIds = new Map([["", 0]]);
    let nextCustomNumFmtId = 164;

    function ensureNumFmt(fmt) {
      const code = xlsxStyleNumberFormatKey(fmt);
      if (!code) return 0;
      if (numFmtIds.has(code)) return numFmtIds.get(code);

      const id = nextCustomNumFmtId++;
      numFmtIds.set(code, id);
      numFmts.push({
        id,
        xml: `<numFmt numFmtId="${id}" formatCode="${xlsxAttrEscape(code)}"/>`
      });
      return id;
    }

    function ensureFont(fmt) {
      const f = normalizeCellFormat(fmt);
      const key = xlsxStyleFontKey(f);
      const defaultKey = JSON.stringify({ bold: false, italic: false, underline: false, fontSize: 0, fg: "" });

      if (key === defaultKey) return 0;
      if (fontIds.has(key)) return fontIds.get(key);

      const parts = ["<font>"];
      if (f.bold) parts.push("<b/>");
      if (f.italic) parts.push("<i/>");
      if (f.underline) parts.push("<u/>");
      parts.push(`<sz val="${f.fontSize || 11}"/>`);

      if (f.fg && TEXT_COLOR_COLORS[f.fg]) {
        parts.push(`<color rgb="${xlsxAttrEscape(TEXT_COLOR_COLORS[f.fg].rgb)}"/>`);
      } else {
        parts.push('<color theme="1"/>');
      }

      parts.push('<name val="Calibri"/>');
      parts.push('<family val="2"/>');
      parts.push('<scheme val="minor"/>');
      parts.push("</font>");

      const id = fonts.length;
      fontIds.set(key, id);
      fonts.push({ xml: parts.join("") });
      return id;
    }

    function ensureFill(fmt) {
      const f = normalizeCellFormat(fmt);
      const key = xlsxStyleFillKey(f);
      if (!key || !CELL_FILL_COLORS[key]) return 0;
      if (fillIds.has(key)) return fillIds.get(key);

      const rgb = CELL_FILL_COLORS[key].rgb;
      const id = fills.length;
      fillIds.set(key, id);
      fills.push({
        xml: `<fill><patternFill patternType="solid"><fgColor rgb="${xlsxAttrEscape(rgb)}"/><bgColor indexed="64"/></patternFill></fill>`
      });
      return id;
    }

    function borderSideXml(name, style) {
      const xlsxStyle = borderXlsxStyle(style);
      if (!xlsxStyle) return `<${name}/>`;
      return `<${name} style="${xlsxAttrEscape(xlsxStyle)}"><color rgb="${xlsxAttrEscape(BORDER_XLSX_COLOR)}"/></${name}>`;
    }

    function ensureBorder(fmt) {
      const f = normalizeCellFormat(fmt);
      const border = normalizeBorderFormat(f.border);
      const key = xlsxStyleBorderKey(f);

      if (isEmptyBorderFormat(border)) return 0;
      if (borderIds.has(key)) return borderIds.get(key);

      const id = borders.length;
      borderIds.set(key, id);
      borders.push({
        xml: [
          "<border>",
          borderSideXml("left", border.left),
          borderSideXml("right", border.right),
          borderSideXml("top", border.top),
          borderSideXml("bottom", border.bottom),
          "<diagonal/>",
          "</border>"
        ].join("")
      });
      return id;
    }

    function styleIndexForFormat(fmt) {
      const f = normalizeCellFormat(fmt);
      if (isEmptyCellFormat(f)) return 0;

      const numFmtId = ensureNumFmt(f);
      const font = ensureFont(f);
      const fill = ensureFill(f);
      const border = ensureBorder(f);
      const align = f.align || "";
      const valign = cellVerticalAlignXlsx(f.valign);
      const key = JSON.stringify({ numFmtId, font, fill, border, align, valign });

      if (xfIds.has(key)) return xfIds.get(key);

      const attrs = [
        `numFmtId="${numFmtId}"`,
        `fontId="${font}"`,
        `fillId="${fill}"`,
        `borderId="${border}"`,
        'xfId="0"'
      ];

      if (numFmtId) attrs.push('applyNumberFormat="1"');
      if (font) attrs.push('applyFont="1"');
      if (fill) attrs.push('applyFill="1"');
      if (border) attrs.push('applyBorder="1"');
      if (align || valign) attrs.push('applyAlignment="1"');

      const alignmentAttrs = [];
      if (align) alignmentAttrs.push(`horizontal="${xlsxAttrEscape(align)}"`);
      if (valign) alignmentAttrs.push(`vertical="${xlsxAttrEscape(valign)}"`);
      const alignment = alignmentAttrs.length ? `<alignment ${alignmentAttrs.join(" ")}/>` : "";
      const xml = alignment
        ? `<xf ${attrs.join(" ")}>${alignment}</xf>`
        : `<xf ${attrs.join(" ")}/>`;

      const id = xfs.length;
      xfIds.set(key, id);
      xfs.push({ xml });
      return id;
    }

    function stylesXml() {
      const numFmtsXml = numFmts.length
        ? `<numFmts count="${numFmts.length}">${numFmts.map((f) => f.xml).join("")}</numFmts>`
        : "";

      return [
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>',
        '<styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">',
        numFmtsXml,
        `<fonts count="${fonts.length}" x14ac:knownFonts="1" xmlns:x14ac="http://schemas.microsoft.com/office/spreadsheetml/2009/9/ac">${fonts.map((f) => f.xml).join("")}</fonts>`,
        `<fills count="${fills.length}">${fills.map((f) => f.xml).join("")}</fills>`,
        `<borders count="${borders.length}">${borders.map((b) => b.xml).join("")}</borders>`,
        '<cellStyleXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0"/></cellStyleXfs>',
        `<cellXfs count="${xfs.length}">${xfs.map((xf) => xf.xml).join("")}</cellXfs>`,
        '<cellStyles count="1"><cellStyle name="Normal" xfId="0" builtinId="0"/></cellStyles>',
        '<dxfs count="0"/>',
        '<tableStyles count="0" defaultTableStyle="TableStyleMedium2" defaultPivotStyle="PivotStyleLight16"/>',
        '</styleSheet>'
      ].join("");
    }

    return { styleIndexForFormat, stylesXml };
  }

  function buildWorksheetXml(sheet, styleCatalog) {
    const rows = Array.isArray(sheet.rows) ? sheet.rows : [];
    const rowCount = rows.length;
    const colCount = rows.reduce((m, row) => Math.max(m, Array.isArray(row) ? row.length : 0), 0);
    const colWidths = ensureSheetColWidths(sheet, colCount);
    const rowHeights = ensureSheetRowHeights(sheet, rowCount);
    const cache = computeSheetCache(sheet);

    const colsXml = colWidths.length
      ? `<cols>${colWidths.map((w, i) => {
          const width = xlsxColumnPixelWidthToExcelWidth(w);
          return `<col min="${i + 1}" max="${i + 1}" width="${width}" customWidth="1"/>`;
        }).join("")}</cols>`
      : "";

    const rowXml = [];

    for (let r = 0; r < rowCount; r++) {
      const row = Array.isArray(rows[r]) ? rows[r] : [];
      const cells = [];

      for (let c = 0; c < colCount; c++) {
        const raw = row[c] == null ? "" : String(row[c]);
        const fmt = getCellFormat(sheet, r, c);
        const styleIndex = styleCatalog.styleIndexForFormat(fmt);
        const styleAttr = styleIndex ? ` s="${styleIndex}"` : "";
        const ref = xlsxCellRef(r, c);

        if (isFormulaValue(raw)) {
          const result = evaluateCell(sheet, r, c, cache, new Set());
          const fxml = `<f>${xlsxFormulaValueXml(raw)}</f>`;

          if (!result.error && typeof result.value === "number" && Number.isFinite(result.value)) {
            cells.push(`<c r="${ref}"${styleAttr}>${fxml}<v>${xlsxNumberValue(result.value)}</v></c>`);
          } else {
            cells.push(`<c r="${ref}"${styleAttr}>${fxml}</c>`);
          }
          continue;
        }

        const value = coerceCellValue(raw);
        if (value === "" && !styleIndex) continue;

        if (typeof value === "number" && Number.isFinite(value)) {
          cells.push(`<c r="${ref}"${styleAttr}><v>${xlsxNumberValue(value)}</v></c>`);
        } else if (value === "") {
          cells.push(`<c r="${ref}"${styleAttr}/>`);
        } else {
          cells.push(`<c r="${ref}"${styleAttr} t="inlineStr">${xlsxInlineStringXml(value)}</c>`);
        }
      }

      const rowHeight = xlsxRowHeightForSheetRow(sheet, r, colCount);
      const explicitCustomHeight = clampRowHeight(rowHeights[r]) !== DEFAULT_ROW_HEIGHT;

      if (cells.length || explicitCustomHeight) {
        const rowAttrs = rowHeight ? ` r="${r + 1}" ht="${rowHeight}" customHeight="1"` : ` r="${r + 1}"`;
        rowXml.push(`<row${rowAttrs}>${cells.join("")}</row>`);
      }
    }

    return [
      '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>',
      '<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">',
      `<dimension ref="${xlsxDimensionRef(rowCount || 1, colCount || 1)}"/>`,
      '<sheetViews><sheetView workbookViewId="0"/></sheetViews>',
      '<sheetFormatPr defaultRowHeight="15"/>',
      colsXml,
      `<sheetData>${rowXml.join("")}</sheetData>`,
      '<pageMargins left="0.7" right="0.7" top="0.75" bottom="0.75" header="0.3" footer="0.3"/>',
      '</worksheet>'
    ].join("");
  }

  function buildStyleMetadataWorksheetXml(stylePayload) {
    const json = JSON.stringify({ version: STYLE_META_VERSION, sheets: stylePayload || {} });
    const rows = [STYLE_META_VERSION];

    // Avoid Excel's 32767-character per-cell text limit by storing metadata as
    // multiple safe inline-string cells in the hidden _pqnas_styles worksheet.
    for (let i = 0; i < json.length; i += STYLE_META_CHUNK_SIZE) {
      rows.push(json.slice(i, i + STYLE_META_CHUNK_SIZE));
    }

    const rowXml = rows.map((value, idx) => {
      const r = idx + 1;
      return `<row r="${r}"><c r="A${r}" t="inlineStr">${xlsxInlineStringXml(value)}</c></row>`;
    }).join("");

    return [
      '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>',
      '<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">',
      `<dimension ref="A1:A${rows.length}"/>`,
      `<sheetData>${rowXml}</sheetData>`,
      '</worksheet>'
    ].join("");
  }

  function buildWorkbookXml(sheetNames) {
    const sheetsXml = sheetNames.map((name, idx) => {
      const hidden = name === STYLE_SHEET_NAME ? ' state="hidden"' : "";
      return `<sheet name="${xlsxAttrEscape(name)}" sheetId="${idx + 1}" r:id="rId${idx + 1}"${hidden}/>`;
    }).join("");

    return [
      '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>',
      '<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">',
      '<workbookPr/>',
      `<sheets>${sheetsXml}</sheets>`,
      '<calcPr fullCalcOnLoad="1"/>',
      '</workbook>'
    ].join("");
  }

  function buildWorkbookRelsXml(sheetCount) {
    const rels = [];
    for (let i = 0; i < sheetCount; i++) {
      rels.push(`<Relationship Id="rId${i + 1}" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet${i + 1}.xml"/>`);
    }
    rels.push(`<Relationship Id="rId${sheetCount + 1}" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>`);

    return [
      '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>',
      '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">',
      rels.join(""),
      '</Relationships>'
    ].join("");
  }

  function buildRootRelsXml() {
    return [
      '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>',
      '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">',
      '<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>',
      '<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties" Target="docProps/core.xml"/>',
      '<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties" Target="docProps/app.xml"/>',
      '</Relationships>'
    ].join("");
  }

  function buildContentTypesXml(sheetCount) {
    const sheetOverrides = Array.from({ length: sheetCount }, (_v, i) =>
      `<Override PartName="/xl/worksheets/sheet${i + 1}.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>`
    ).join("");

    return [
      '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>',
      '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">',
      '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>',
      '<Default Extension="xml" ContentType="application/xml"/>',
      '<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>',
      sheetOverrides,
      '<Override PartName="/xl/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"/>',
      '<Override PartName="/docProps/core.xml" ContentType="application/vnd.openxmlformats-package.core-properties+xml"/>',
      '<Override PartName="/docProps/app.xml" ContentType="application/vnd.openxmlformats-officedocument.extended-properties+xml"/>',
      '</Types>'
    ].join("");
  }

  function buildCorePropsXml() {
    const now = new Date().toISOString();
    return [
      '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>',
      '<cp:coreProperties xmlns:cp="http://schemas.openxmlformats.org/package/2006/metadata/core-properties" xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:dcterms="http://purl.org/dc/terms/" xmlns:dcmitype="http://purl.org/dc/dcmitype/" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">',
      '<dc:creator>DNA-Nexus</dc:creator>',
      '<cp:lastModifiedBy>DNA-Nexus</cp:lastModifiedBy>',
      `<dcterms:created xsi:type="dcterms:W3CDTF">${xlsxXmlEscape(now)}</dcterms:created>`,
      `<dcterms:modified xsi:type="dcterms:W3CDTF">${xlsxXmlEscape(now)}</dcterms:modified>`,
      '</cp:coreProperties>'
    ].join("");
  }

  function buildAppPropsXml(sheetNames) {
    const visibleNames = sheetNames.filter((name) => name !== STYLE_SHEET_NAME);
    return [
      '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>',
      '<Properties xmlns="http://schemas.openxmlformats.org/officeDocument/2006/extended-properties" xmlns:vt="http://schemas.openxmlformats.org/officeDocument/2006/docPropsVTypes">',
      '<Application>DNA-Nexus</Application>',
      `<TitlesOfParts><vt:vector size="${visibleNames.length}" baseType="lpstr">${visibleNames.map((name) => `<vt:lpstr>${xlsxXmlEscape(name)}</vt:lpstr>`).join("")}</vt:vector></TitlesOfParts>`,
      '</Properties>'
    ].join("");
  }

  function textToUtf8Bytes(text) {
    return new TextEncoder().encode(String(text == null ? "" : text));
  }

  function zipCrc32(bytes) {
    let crc = 0xFFFFFFFF;

    if (!zipCrc32.table) {
      zipCrc32.table = Array.from({ length: 256 }, (_v, n) => {
        let c = n;
        for (let k = 0; k < 8; k++) {
          c = (c & 1) ? (0xEDB88320 ^ (c >>> 1)) : (c >>> 1);
        }
        return c >>> 0;
      });
    }

    for (let i = 0; i < bytes.length; i++) {
      crc = zipCrc32.table[(crc ^ bytes[i]) & 0xFF] ^ (crc >>> 8);
    }

    return (crc ^ 0xFFFFFFFF) >>> 0;
  }

  function zipDosTimeDate(date = new Date()) {
    const year = Math.max(1980, date.getFullYear());
    const dosTime = (date.getHours() << 11) | (date.getMinutes() << 5) | Math.floor(date.getSeconds() / 2);
    const dosDate = ((year - 1980) << 9) | ((date.getMonth() + 1) << 5) | date.getDate();
    return { dosTime, dosDate };
  }

  function writeU16(out, offset, value) {
    out[offset] = value & 0xFF;
    out[offset + 1] = (value >>> 8) & 0xFF;
  }

  function writeU32(out, offset, value) {
    out[offset] = value & 0xFF;
    out[offset + 1] = (value >>> 8) & 0xFF;
    out[offset + 2] = (value >>> 16) & 0xFF;
    out[offset + 3] = (value >>> 24) & 0xFF;
  }

  function zipEntryBytes(data) {
    if (data instanceof Uint8Array) return data;
    if (data instanceof ArrayBuffer) return new Uint8Array(data);
    return textToUtf8Bytes(data);
  }

  function buildZipStore(entries) {
    const prepared = entries.map((entry) => {
      const nameBytes = textToUtf8Bytes(entry.name);
      const dataBytes = zipEntryBytes(entry.data);
      return {
        name: entry.name,
        nameBytes,
        dataBytes,
        crc: zipCrc32(dataBytes)
      };
    });

    const { dosTime, dosDate } = zipDosTimeDate();
    let localSize = 0;
    let centralSize = 0;

    for (const entry of prepared) {
      localSize += 30 + entry.nameBytes.length + entry.dataBytes.length;
      centralSize += 46 + entry.nameBytes.length;
    }

    const endSize = 22;
    const out = new Uint8Array(localSize + centralSize + endSize);
    let offset = 0;
    const central = [];

    for (const entry of prepared) {
      const localOffset = offset;

      writeU32(out, offset, 0x04034B50); offset += 4;
      writeU16(out, offset, 20); offset += 2;
      writeU16(out, offset, 0x0800); offset += 2; // UTF-8 names
      writeU16(out, offset, 0); offset += 2;      // stored, no compression
      writeU16(out, offset, dosTime); offset += 2;
      writeU16(out, offset, dosDate); offset += 2;
      writeU32(out, offset, entry.crc); offset += 4;
      writeU32(out, offset, entry.dataBytes.length); offset += 4;
      writeU32(out, offset, entry.dataBytes.length); offset += 4;
      writeU16(out, offset, entry.nameBytes.length); offset += 2;
      writeU16(out, offset, 0); offset += 2;
      out.set(entry.nameBytes, offset); offset += entry.nameBytes.length;
      out.set(entry.dataBytes, offset); offset += entry.dataBytes.length;

      central.push({ ...entry, localOffset });
    }

    const centralOffset = offset;

    for (const entry of central) {
      writeU32(out, offset, 0x02014B50); offset += 4;
      writeU16(out, offset, 20); offset += 2;
      writeU16(out, offset, 20); offset += 2;
      writeU16(out, offset, 0x0800); offset += 2;
      writeU16(out, offset, 0); offset += 2;
      writeU16(out, offset, dosTime); offset += 2;
      writeU16(out, offset, dosDate); offset += 2;
      writeU32(out, offset, entry.crc); offset += 4;
      writeU32(out, offset, entry.dataBytes.length); offset += 4;
      writeU32(out, offset, entry.dataBytes.length); offset += 4;
      writeU16(out, offset, entry.nameBytes.length); offset += 2;
      writeU16(out, offset, 0); offset += 2;
      writeU16(out, offset, 0); offset += 2;
      writeU16(out, offset, 0); offset += 2;
      writeU16(out, offset, 0); offset += 2;
      writeU32(out, offset, 0); offset += 4;
      writeU32(out, offset, entry.localOffset); offset += 4;
      out.set(entry.nameBytes, offset); offset += entry.nameBytes.length;
    }

    const centralDirectorySize = offset - centralOffset;

    writeU32(out, offset, 0x06054B50); offset += 4;
    writeU16(out, offset, 0); offset += 2;
    writeU16(out, offset, 0); offset += 2;
    writeU16(out, offset, prepared.length); offset += 2;
    writeU16(out, offset, prepared.length); offset += 2;
    writeU32(out, offset, centralDirectorySize); offset += 4;
    writeU32(out, offset, centralOffset); offset += 4;
    writeU16(out, offset, 0); offset += 2;

    return out.buffer;
  }

  function buildStyledXlsxArrayBuffer() {
    const styleCatalog = buildXlsxStyleCatalog();
    const usedNames = new Set();
    const visibleSheets = [];
    const stylePayload = {};

    for (let i = 0; i < state.sheets.length; i++) {
      const sheet = state.sheets[i];
      const outputName = xlsxUniqueSheetName(sheet && sheet.name, i, usedNames);
      visibleSheets.push({ sheet, outputName });
      stylePayload[outputName] = compactCellFormats(sheet);
    }

    const allSheetNames = visibleSheets.map((item) => item.outputName).concat([STYLE_SHEET_NAME]);
    const entries = [];

    for (let i = 0; i < visibleSheets.length; i++) {
      const item = visibleSheets[i];
      entries.push({
        name: `xl/worksheets/sheet${i + 1}.xml`,
        data: buildWorksheetXml(item.sheet, styleCatalog)
      });
    }

    entries.push({
      name: `xl/worksheets/sheet${visibleSheets.length + 1}.xml`,
      data: buildStyleMetadataWorksheetXml(stylePayload)
    });

    entries.push({ name: "[Content_Types].xml", data: buildContentTypesXml(allSheetNames.length) });
    entries.push({ name: "_rels/.rels", data: buildRootRelsXml() });
    entries.push({ name: "docProps/core.xml", data: buildCorePropsXml() });
    entries.push({ name: "docProps/app.xml", data: buildAppPropsXml(allSheetNames) });
    entries.push({ name: "xl/workbook.xml", data: buildWorkbookXml(allSheetNames) });
    entries.push({ name: "xl/_rels/workbook.xml.rels", data: buildWorkbookRelsXml(allSheetNames.length) });
    entries.push({ name: "xl/styles.xml", data: styleCatalog.stylesXml() });

    return buildZipStore(entries);
  }

  function clampColumnWidth(width) {
    const n = Number(width);
    if (!Number.isFinite(n)) return DEFAULT_COL_WIDTH;
    return Math.max(MIN_COL_WIDTH, Math.min(MAX_COL_WIDTH, Math.round(n)));
  }

  function clampRowHeight(height) {
    const n = Number(height);
    if (!Number.isFinite(n)) return DEFAULT_ROW_HEIGHT;
    return Math.max(MIN_ROW_HEIGHT, Math.min(MAX_ROW_HEIGHT, Math.round(n)));
  }

  function xlsxColumnToPixelWidth(col) {
    if (!col || typeof col !== "object") return DEFAULT_COL_WIDTH;
    if (Number.isFinite(col.wpx)) return clampColumnWidth(col.wpx);
    if (Number.isFinite(col.width)) return clampColumnWidth((col.width * 8) + 16);
    if (Number.isFinite(col.wch)) return clampColumnWidth((col.wch * 8) + 16);
    return DEFAULT_COL_WIDTH;
  }

  function xlsxRowToPixelHeight(row) {
    if (!row || typeof row !== "object") return DEFAULT_ROW_HEIGHT;
    if (Number.isFinite(row.hpx)) return clampRowHeight(row.hpx);
    if (Number.isFinite(row.ht)) return clampRowHeight(row.ht / 0.75);
    if (Number.isFinite(row.hpt)) return clampRowHeight(row.hpt / 0.75);
    return DEFAULT_ROW_HEIGHT;
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

  function ensureSheetRowHeights(sheet, rowCount) {
    if (!sheet) return [];
    const count = Math.max(0, Number.isInteger(rowCount) ? rowCount : 0);

    if (!Array.isArray(sheet.rowHeights)) {
      sheet.rowHeights = [];
    }

    while (sheet.rowHeights.length < count) {
      sheet.rowHeights.push(DEFAULT_ROW_HEIGHT);
    }

    if (sheet.rowHeights.length > count) {
      sheet.rowHeights.length = count;
    }

    for (let i = 0; i < sheet.rowHeights.length; i++) {
      sheet.rowHeights[i] = clampRowHeight(sheet.rowHeights[i]);
    }

    return sheet.rowHeights;
  }

  function sheetRowHeight(sheet, row) {
    const heights = ensureSheetRowHeights(sheet, row + 1);
    return clampRowHeight(heights[row]);
  }

  function applyRowHeight(el, height) {
    if (!el) return;

    const px = `${clampRowHeight(height)}px`;
    el.style.height = px;
    el.style.minHeight = px;
  }

  function paintVisibleRowHeight(row, height) {
    if (!bodyEl || !Number.isInteger(row) || row < 0) return;

    const table = bodyEl.querySelector(".spreadsheetEditorTable");
    if (!table) return;

    const pxHeight = clampRowHeight(height);
    const header = table.querySelector(`th[data-row="${row}"]`);
    applyRowHeight(header, pxHeight);

    const sheet = state.sheets[state.active];

    for (const cell of table.querySelectorAll(`td[data-row="${row}"]`)) {
      applyRowHeight(cell, pxHeight);
      const input = cell.querySelector("input");
      applyRowHeight(input, pxHeight);

      const col = Number(cell.dataset.col);
      if (sheet && input && Number.isInteger(col)) {
        applyCellFormatToInput(input, getCellFormat(sheet, row, col));
      }
    }
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
    const historyBefore = captureHistorySnapshot();
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
      if (changed) commitHistorySnapshot(historyBefore);
      repaintSpreadsheetSelection();
    };

    document.body.classList.add("spreadsheetColumnResizing");
    document.addEventListener("pointermove", onMove);
    document.addEventListener("pointerup", onUp, { once: true });
  }

  function startRowResize(ev, row) {
    if (state.readOnly || state.tooLarge) return;

    const sheet = state.sheets[state.active];
    if (!sheet || !Number.isInteger(row) || row < 0) return;

    ev.preventDefault();
    ev.stopPropagation();

    if (typeof ev.stopImmediatePropagation === "function") {
      ev.stopImmediatePropagation();
    }

    hideAxisMenu();

    const startY = Number(ev.clientY);
    const startHeight = sheetRowHeight(sheet, row);
    const historyBefore = captureHistorySnapshot();
    let changed = false;

    const onMove = (moveEv) => {
      const dy = Number(moveEv.clientY) - startY;
      const nextHeight = clampRowHeight(startHeight + dy);

      ensureSheetRowHeights(sheet, row + 1);
      if (sheet.rowHeights[row] === nextHeight) return;

      sheet.rowHeights[row] = nextHeight;
      paintVisibleRowHeight(row, nextHeight);

      if (!changed) {
        changed = true;
        setDirty(true);
      }
    };

    const onUp = () => {
      document.removeEventListener("pointermove", onMove);
      document.removeEventListener("pointerup", onUp);
      document.body.classList.remove("spreadsheetRowResizing");
      if (changed) commitHistorySnapshot(historyBefore);
      repaintSpreadsheetSelection();
    };

    document.body.classList.add("spreadsheetRowResizing");
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

  function cloneHistoryValue(value) {
    try {
      return JSON.parse(JSON.stringify(value == null ? null : value));
    } catch (_) {
      return null;
    }
  }

  function resetSpreadsheetHistory() {
    const api = FM && FM.spreadsheetHistory;
    spreadsheetHistory = api && typeof api.create === "function"
      ? api.create({ maxDepth: 100 })
      : null;

    if (spreadsheetHistory && typeof spreadsheetHistory.markClean === "function") {
      spreadsheetHistory.markClean();
    }

    pendingCellEditHistory = null;
  }

  function captureHistorySnapshot() {
    // Security: undo snapshots contain plain workbook state only. DOM nodes,
    // generated HTML, credentials, and file paths outside the editor state are
    // never stored in the history stack.
    return {
      sheets: cloneHistoryValue(state.sheets),
      active: state.active,
      selection: cloneHistoryValue(state.selection),
      activeCell: cloneHistoryValue(state.activeCell),
      rangeSelection: cloneHistoryValue(state.rangeSelection)
    };
  }

  function historyDirtyState() {
    if (spreadsheetHistory && typeof spreadsheetHistory.isDirty === "function") {
      return spreadsheetHistory.isDirty();
    }
    return !!state.dirty;
  }

  function commitHistorySnapshot(beforeSnapshot) {
    if (spreadsheetHistory && beforeSnapshot && typeof spreadsheetHistory.record === "function") {
      spreadsheetHistory.record(beforeSnapshot);
      setDirty(historyDirtyState());
      return;
    }

    setDirty(true);
  }

  function restoreHistorySnapshot(snapshot, label) {
    if (!snapshot || !Array.isArray(snapshot.sheets)) return false;

    const restoredSheets = cloneHistoryValue(snapshot.sheets);
    if (!Array.isArray(restoredSheets)) return false;

    state.sheets = restoredSheets;
    state.active = Math.max(0, Math.min(
      Number.isInteger(snapshot.active) ? snapshot.active : 0,
      Math.max(0, state.sheets.length - 1)
    ));
    state.selection = cloneHistoryValue(snapshot.selection);
    state.activeCell = cloneHistoryValue(snapshot.activeCell);
    state.rangeSelection = cloneHistoryValue(snapshot.rangeSelection);
    state.dirty = historyDirtyState();

    formulaFocus = null;
    pendingCellEditHistory = null;

    render();
    setStatus(label, state.dirty ? "warn" : "");
    return true;
  }

  function undoSpreadsheetHistory() {
    if (!spreadsheetHistory || state.saving || state.readOnly || state.tooLarge) return false;
    if (typeof spreadsheetHistory.undo !== "function") return false;

    const snapshot = spreadsheetHistory.undo(captureHistorySnapshot());
    if (!snapshot) return false;

    return restoreHistorySnapshot(snapshot, tr("filemgr.spreadsheet_editor.undo_status", null, "Undone."));
  }

  function redoSpreadsheetHistory() {
    if (!spreadsheetHistory || state.saving || state.readOnly || state.tooLarge) return false;
    if (typeof spreadsheetHistory.redo !== "function") return false;

    const snapshot = spreadsheetHistory.redo(captureHistorySnapshot());
    if (!snapshot) return false;

    return restoreHistorySnapshot(snapshot, tr("filemgr.spreadsheet_editor.redo_status", null, "Redone."));
  }

  function shouldLetNativeTextUndoHandle(ev) {
    const target = ev && ev.target;
    if (!target) return false;

    if (target === formulaBarInput) {
      const cell = activeFormulaBarCell();
      if (!cell) return false;
      return String(formulaBarInput.value == null ? "" : formulaBarInput.value) !== cellRaw(cell.sheet, cell.row, cell.col);
    }

    const tag = String(target.tagName || "").toUpperCase();
    if (tag === "TEXTAREA") return true;

    if (tag === "INPUT" && target.closest && target.closest(".spreadsheetEditorTable")) {
      const row = Number(target.dataset ? target.dataset.row : NaN);
      const col = Number(target.dataset ? target.dataset.col : NaN);
      const pending = pendingCellEditHistory;

      // Let the browser handle Ctrl+Z only while the user is actively editing
      // text inside the focused cell. Once focus has moved to another unchanged
      // cell, Ctrl+Z should be workbook-level undo instead.
      return !!(
        pending &&
        pending.input === target &&
        pending.row === row &&
        pending.col === col &&
        String(target.value == null ? "" : target.value) !== pending.originalRaw
      );
    }

    return !!target.isContentEditable;
  }

  function shouldLetTextDeleteHandle(ev) {
    const target = ev && ev.target;
    if (!target) return false;

    if (target === formulaBarInput) return true;

    const tag = String(target.tagName || "").toUpperCase();
    if (tag === "TEXTAREA") return true;

    if (tag === "INPUT" && target.closest && target.closest(".spreadsheetEditorTable")) {
      return true;
    }

    return !!target.isContentEditable;
  }

  function handleHistoryKeyboard(ev) {
    if (!ev || ev.defaultPrevented) return;
    if (!modal || !modal.classList.contains("show")) return;

    if (
      (ev.key === "Delete" || ev.key === "Backspace") &&
      !ev.altKey &&
      !ev.ctrlKey &&
      !ev.metaKey
    ) {
      if (shouldLetTextDeleteHandle(ev)) return;

      if (state.selection || state.rangeSelection || state.activeCell) {
        ev.preventDefault();
        ev.stopPropagation();

        // Keep spreadsheet Delete/Backspace local to the editor. Without this,
        // row/column header focus can bubble to File Manager's global delete
        // shortcut and open the trash confirmation for the selected file.
        clearSpreadsheetTargets(null);
        return;
      }
    }

    if (!(ev.ctrlKey || ev.metaKey) || ev.altKey) return;

    const key = String(ev.key || "").toLowerCase();
    const wantsUndo = key === "z" && !ev.shiftKey;
    const wantsRedo = key === "y" || (key === "z" && ev.shiftKey);
    if (!wantsUndo && !wantsRedo) return;

    if (shouldLetNativeTextUndoHandle(ev)) return;

    const handled = wantsRedo ? redoSpreadsheetHistory() : undoSpreadsheetHistory();
    if (!handled) return;

    ev.preventDefault();
    ev.stopPropagation();
  }

  function attachHistoryKeyboard() {
    if (historyKeyboardAttached) return;
    historyKeyboardAttached = true;
    document.addEventListener("keydown", handleHistoryKeyboard, true);
  }

  function beginPendingCellEditHistory(input, row, col, previousRaw) {
    if (!input) return;

    const pending = pendingCellEditHistory;
    if (pending && pending.input === input && pending.row === row && pending.col === col) {
      if (pending.recorded) return;
      commitHistorySnapshot(pending.before);
      pending.recorded = true;
      return;
    }

    // Capture workbook undo before the first text mutation. Waiting until blur
    // can record only focus/selection changes after the cell value has already
    // been written into state.rows.
    const before = captureHistorySnapshot();

    pendingCellEditHistory = {
      input,
      row,
      col,
      originalRaw: String(previousRaw == null ? "" : previousRaw),
      before,
      recorded: false
    };

    commitHistorySnapshot(before);
    pendingCellEditHistory.recorded = true;
  }

  function commitPendingCellEditHistory(input, row, col) {
    const pending = pendingCellEditHistory;
    if (!pending || pending.input !== input || pending.row !== row || pending.col !== col) return;

    pendingCellEditHistory = null;

    if (!pending.recorded) {
      const sheet = state.sheets[state.active];
      const currentRaw = cellRaw(sheet, row, col);
      if (currentRaw !== pending.originalRaw) {
        commitHistorySnapshot(captureHistorySnapshot());
        return;
      }
    }

    setDirty(historyDirtyState());
  }

  function activeFormulaBarCell() {
    const sheet = state.sheets[state.active];
    const row = Number(state.activeCell && state.activeCell.row);
    const col = Number(state.activeCell && state.activeCell.col);

    if (!sheet || !Number.isInteger(row) || !Number.isInteger(col) || row < 0 || col < 0) {
      return null;
    }

    return { sheet, row, col };
  }

  function updateFormulaBar(forceValue = false) {
    if (!formulaBarNameEl || !formulaBarInput) return;

    const cell = activeFormulaBarCell();
    const disabled = state.saving || state.readOnly || state.tooLarge || !cell;

    formulaBarInput.disabled = disabled;

    if (!cell) {
      formulaBarNameEl.textContent = "";
      formulaBarInput.removeAttribute("data-row");
      formulaBarInput.removeAttribute("data-col");
      if (forceValue || document.activeElement !== formulaBarInput) {
        formulaBarInput.value = "";
      }
      return;
    }

    formulaBarNameEl.textContent = coordToRef(cell.row, cell.col);
    formulaBarInput.dataset.row = String(cell.row);
    formulaBarInput.dataset.col = String(cell.col);

    if (forceValue || document.activeElement !== formulaBarInput) {
      // Security: spreadsheet cell content is assigned as input.value, never as
      // HTML, so formulas/text cannot become executable markup.
      formulaBarInput.value = cellRaw(cell.sheet, cell.row, cell.col);
    }
  }

  function syncVisibleInputForCell(row, col, options = {}) {
    if (!bodyEl) return;

    const sheet = state.sheets[state.active];
    const input = bodyEl.querySelector(`input[data-row="${row}"][data-col="${col}"]`);
    if (!sheet || !input) return;

    const forceValue = !!(options && options.forceValue);

    if (forceValue || document.activeElement !== input) {
      input.value = displayCellValue(sheet, row, col);
    }

    input.title = isFormulaValue(cellRaw(sheet, row, col)) ? cellRaw(sheet, row, col) : "";
  }

  function beginFormulaBarEdit() {
    const cell = activeFormulaBarCell();
    if (!formulaBarInput || !cell) return;

    const raw = cellRaw(cell.sheet, cell.row, cell.col);
    formulaBarInput.value = raw;

    if (String(raw || "").startsWith("=")) {
      formulaFocus = {
        input: formulaBarInput,
        row: cell.row,
        col: cell.col,
        originalRaw: raw,
        wasDirty: state.dirty
      };
    } else if (formulaFocus && formulaFocus.input === formulaBarInput) {
      formulaFocus = null;
    }
  }

  function updateActiveCellFromFormulaBar() {
    if (state.readOnly || state.tooLarge || !formulaBarInput) return false;

    const cell = activeFormulaBarCell();
    if (!cell) return false;

    const previousRaw = cellRaw(cell.sheet, cell.row, cell.col);
    const previousFormulaFocus = formulaFocus && formulaFocus.input === formulaBarInput ? formulaFocus : null;
    const nextRaw = String(formulaBarInput.value == null ? "" : formulaBarInput.value);
    const historyBefore = previousRaw !== nextRaw ? captureHistorySnapshot() : null;

    setCellRaw(cell.sheet, cell.row, cell.col, nextRaw);

    if (String(nextRaw || "").startsWith("=")) {
      formulaFocus = {
        input: formulaBarInput,
        row: cell.row,
        col: cell.col,
        originalRaw: previousFormulaFocus ? previousFormulaFocus.originalRaw : previousRaw,
        wasDirty: previousFormulaFocus ? previousFormulaFocus.wasDirty : state.dirty
      };
    } else if (formulaFocus && formulaFocus.input === formulaBarInput) {
      formulaFocus = null;
    }

    if (previousRaw !== nextRaw) {
      commitHistorySnapshot(historyBefore);
    }

    refreshFormulaDisplays(null);
    syncVisibleInputForCell(cell.row, cell.col);
    updateFormatToolbar();
    return true;
  }

  function cancelFormulaBarEdit() {
    const focus = formulaFocus && formulaFocus.input === formulaBarInput ? formulaFocus : null;
    const cell = activeFormulaBarCell();

    if (!focus || !cell || !formulaBarInput) return false;

    const originalRaw = Object.prototype.hasOwnProperty.call(focus, "originalRaw")
      ? String(focus.originalRaw == null ? "" : focus.originalRaw)
      : cellRaw(cell.sheet, cell.row, cell.col);

    setCellRaw(cell.sheet, cell.row, cell.col, originalRaw);
    formulaFocus = null;

    formulaBarInput.value = originalRaw;
    setDirty(!!focus.wasDirty);
    refreshFormulaDisplays(null);
    syncVisibleInputForCell(cell.row, cell.col);
    updateFormulaBar(true);

    return true;
  }

  function focusActiveCellFromFormulaBar() {
    const cell = activeFormulaBarCell();
    if (!cell) return false;
    return focusSpreadsheetCell(cell.row, cell.col, { end: true });
  }

  function spreadsheetInputHasPartialTextSelection(input) {
    if (!input) return false;

    const value = String(input.value || "");
    const start = Number.isInteger(input.selectionStart) ? input.selectionStart : 0;
    const end = Number.isInteger(input.selectionEnd) ? input.selectionEnd : start;

    return start !== end && !(start === 0 && end === value.length);
  }

  function spreadsheetTargetCells(input) {
    const cells = formatTargetCells();
    if (cells.length) return cells;

    const row = Number(input && input.dataset ? input.dataset.row : NaN);
    const col = Number(input && input.dataset ? input.dataset.col : NaN);
    if (Number.isInteger(row) && Number.isInteger(col) && row >= 0 && col >= 0) {
      return [{ row, col }];
    }

    return [];
  }

  function spreadsheetTargetRange(input) {
    const cells = spreadsheetTargetCells(input);
    if (!cells.length) return null;

    const rows = cells.map((cell) => cell.row);
    const cols = cells.map((cell) => cell.col);

    return {
      row1: Math.min(...rows),
      row2: Math.max(...rows),
      col1: Math.min(...cols),
      col2: Math.max(...cols)
    };
  }

  function spreadsheetClipboardCellText(value) {
    // Security: clipboard export is plain text TSV only, not HTML.
    // Tabs/newlines inside a cell are flattened so copied data cannot reshape
    // the TSV in surprising ways.
    return String(value == null ? "" : value)
      .replace(/\r\n/g, "\n")
      .replace(/[\r\n\t]/g, " ");
  }

  function buildSpreadsheetClipboardText(input) {
    const sheet = state.sheets[state.active];
    const range = spreadsheetTargetRange(input);
    if (!sheet || !range) return null;

    const lines = [];
    for (let row = range.row1; row <= range.row2; row++) {
      const values = [];
      for (let col = range.col1; col <= range.col2; col++) {
        values.push(spreadsheetClipboardCellText(cellRaw(sheet, row, col)));
      }
      lines.push(values.join("\t"));
    }

    return lines.join("\r\n");
  }

  function handleSpreadsheetCopy(ev, input) {
    if (!ev || !ev.clipboardData || typeof ev.clipboardData.setData !== "function") return false;
    if (!state.rangeSelection && !state.selection && spreadsheetInputHasPartialTextSelection(input)) return false;

    const text = buildSpreadsheetClipboardText(input);
    if (text == null) return false;

    // Security: only text/plain is written to the clipboard. We do not generate
    // HTML clipboard content, so workbook data cannot become executable markup.
    ev.preventDefault();
    ev.stopPropagation();
    ev.clipboardData.setData("text/plain", text);
    return true;
  }

  function parseSpreadsheetClipboardText(text) {
    const normalized = String(text == null ? "" : text)
      .replace(/\r\n/g, "\n")
      .replace(/\r/g, "\n");

    if (!normalized.length) return [];

    const withoutFinalExcelNewline = normalized.endsWith("\n")
      ? normalized.slice(0, -1)
      : normalized;

    return withoutFinalExcelNewline.split("\n").map((line) => line.split("\t"));
  }

  function spreadsheetPasteStartCell(input) {
    const row = Number(input && input.dataset ? input.dataset.row : NaN);
    const col = Number(input && input.dataset ? input.dataset.col : NaN);
    if (Number.isInteger(row) && Number.isInteger(col) && row >= 0 && col >= 0) {
      return { row, col };
    }

    if (state.activeCell && Number.isInteger(state.activeCell.row) && Number.isInteger(state.activeCell.col)) {
      return { row: state.activeCell.row, col: state.activeCell.col };
    }

    const range = normalizedRangeSelection();
    if (range) return { row: range.row1, col: range.col1 };

    const first = firstFormatTargetCell();
    return first ? { row: first.row, col: first.col } : null;
  }

  function pasteSpreadsheetClipboardText(input, text) {
    if (state.readOnly || state.tooLarge) return false;

    const sheet = state.sheets[state.active];
    const start = spreadsheetPasteStartCell(input);
    const rows = parseSpreadsheetClipboardText(text);

    if (!sheet || !start || !rows.length) return false;

    const historyBefore = captureHistorySnapshot();
    let changed = false;
    for (let r = 0; r < rows.length; r++) {
      const targetRow = start.row + r;
      if (targetRow < 0 || targetRow >= MAX_EDIT_ROWS) continue;

      const cols = rows[r];
      for (let c = 0; c < cols.length; c++) {
        const targetCol = start.col + c;
        if (targetCol < 0 || targetCol >= MAX_EDIT_COLS) continue;

        const next = String(cols[c] == null ? "" : cols[c]);
        if (cellRaw(sheet, targetRow, targetCol) !== next) changed = true;
        setCellRaw(sheet, targetRow, targetCol, next);
      }
    }

    formulaFocus = null;
    state.selection = null;
    state.rangeSelection = null;
    state.activeCell = { row: start.row, col: start.col };

    if (changed) commitHistorySnapshot(historyBefore);
    render();

    window.requestAnimationFrame(() => {
      focusSpreadsheetCell(start.row, start.col, { select: true });
    });

    return true;
  }

  function handleSpreadsheetPaste(ev, input) {
    if (!ev || !ev.clipboardData || typeof ev.clipboardData.getData !== "function") return false;

    const text = ev.clipboardData.getData("text/plain");
    if (!text) return false;

    if (
      !state.rangeSelection &&
      !state.selection &&
      spreadsheetInputHasPartialTextSelection(input) &&
      !/[\t\r\n]/.test(text)
    ) {
      return false;
    }

    // Security: paste reads text/plain only and writes through state/input
    // values. Pasted clipboard data is never injected as HTML.
    const applied = pasteSpreadsheetClipboardText(input, text);
    if (!applied) return false;

    ev.preventDefault();
    ev.stopPropagation();
    return true;
  }

  function shouldSpreadsheetClearKey(input, ev) {
    if (!ev || (ev.key !== "Delete" && ev.key !== "Backspace")) return false;
    if (ev.altKey || ev.ctrlKey || ev.metaKey) return false;
    if (formulaFocus && formulaFocus.input === input) return false;

    const value = String(input && input.value || "");
    const start = Number.isInteger(input && input.selectionStart) ? input.selectionStart : 0;
    const end = Number.isInteger(input && input.selectionEnd) ? input.selectionEnd : start;

    // Text editing must win over range clearing: when the user selects only
    // part of a cell's text, Backspace/Delete should remove those characters,
    // not clear the whole spreadsheet cell/range.
    if (value.length && start !== end && !(start === 0 && end === value.length)) {
      return false;
    }

    if (state.rangeSelection || state.selection) return true;
    if (!value.length) return true;

    return start === 0 && end === value.length;
  }

  function clearSpreadsheetTargets(input) {
    if (state.readOnly || state.tooLarge) return false;

    const sheet = state.sheets[state.active];
    const cells = spreadsheetTargetCells(input);
    if (!sheet || !cells.length) return false;

    const historyBefore = captureHistorySnapshot();
    let changed = false;
    for (const { row, col } of cells) {
      if (cellRaw(sheet, row, col) !== "") changed = true;
      setCellRaw(sheet, row, col, "");
    }

    const anchorCell = cells[0];
    formulaFocus = null;
    state.selection = null;
    state.rangeSelection = null;
    state.activeCell = { row: anchorCell.row, col: anchorCell.col };

    if (changed) commitHistorySnapshot(historyBefore);
    render();

    window.requestAnimationFrame(() => {
      focusSpreadsheetCell(anchorCell.row, anchorCell.col, { select: true });
    });

    return true;
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
    const fmt = getCellFormat(sheet, row, col);

    if (!isFormulaValue(raw)) {
      const parsed = parsePlainNumber(raw);
      if (fmt.decimals != null && !parsed.blank && typeof parsed.number === "number") {
        return formatDecimalNumber(parsed.number, fmt.decimals);
      }
      return raw;
    }

    const effectiveCache = cache || computeSheetCache(sheet);
    const result = evaluateCell(sheet, row, col, effectiveCache, new Set());
    if (result.error) return result.error;

    if (fmt.decimals != null && typeof result.value === "number") {
      return formatDecimalNumber(result.value, fmt.decimals);
    }

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

    const api = FM && FM.spreadsheetAxis;
    if (!api || typeof api.paintSelection !== "function") return;

    api.paintSelection(table, state.selection, {
      markHeader: markAxisHeader,
      markCell: markAxisCell
    });
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

  function axisApi() {
    return FM && FM.spreadsheetAxis ? FM.spreadsheetAxis : null;
  }

  function axisSelectionRange(selection = state.selection) {
    const api = axisApi();
    return api && typeof api.range === "function" ? api.range(selection) : null;
  }

  function axisSelectionContains(type, index) {
    const api = axisApi();
    return !!(api && typeof api.contains === "function" && api.contains(state.selection, type, index));
  }

  function axisSelectionCount(type) {
    const range = axisSelectionRange();
    return range && range.type === type ? Math.max(1, range.count || 1) : 1;
  }

  function axisOperationLabel(action, type) {
    const api = axisApi();
    if (!api || typeof api.operationLabel !== "function") return "";
    return api.operationLabel(action, type, axisSelectionCount(type));
  }

  function axisInsertSpec(type, fallbackIndex, total, limit) {
    const api = axisApi();
    if (!api || typeof api.insertSpec !== "function") return { index: 0, count: 0 };
    return api.insertSpec(state.selection, type, fallbackIndex, total, limit);
  }

  function axisDeleteSpec(type, fallbackIndex, total) {
    const api = axisApi();
    if (!api || typeof api.deleteSpec !== "function") return { index: 0, count: 0 };
    return api.deleteSpec(state.selection, type, fallbackIndex, total);
  }

  function makeAxisSelection(type, index, modifiers = {}) {
    const api = axisApi();
    if (!api || typeof api.selectionFromClick !== "function") return null;
    return api.selectionFromClick(state.selection, type, index, modifiers);
  }

  function setSpreadsheetAxisSelection(type, index) {
    if ((type !== "row" && type !== "column") || !Number.isInteger(index) || index < 0) {
      return;
    }

    state.selection = makeAxisSelection(type, index, {});
    state.activeCell = null;
    state.rangeSelection = null;
    repaintSpreadsheetSelection();
    updateFormatToolbar();
  }

  function selectSpreadsheetAxis(type, index, ev = null) {
    if ((type !== "row" && type !== "column") || !Number.isInteger(index) || index < 0) {
      return;
    }

    const modifiers = {
      shiftKey: !!(ev && ev.shiftKey),
      ctrlKey: !!(ev && ev.ctrlKey),
      metaKey: !!(ev && ev.metaKey)
    };

    const current = axisSelectionRange();
    const sameSingle =
      !modifiers.shiftKey &&
      !modifiers.ctrlKey &&
      !modifiers.metaKey &&
      current &&
      current.type === type &&
      current.start === index &&
      current.end === index;

    state.selection = sameSingle ? null : makeAxisSelection(type, index, modifiers);
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
    const api = FM && FM.spreadsheetAxis;
    if (api && typeof api.hideContextMenu === "function") {
      return api.hideContextMenu();
    }
    return false;
  }

  function openAxisMenu(type, index, x, y) {
    if ((type !== "row" && type !== "column") || !Number.isInteger(index) || index < 0) return;
    if (state.readOnly || state.tooLarge) return;

    const api = FM && FM.spreadsheetAxis;
    if (!api || typeof api.openContextMenu !== "function") return;

    api.openContextMenu(type, index, x, y, {
      disabled: state.readOnly || state.tooLarge,
      contains: (axisType, axisIndex) => axisSelectionContains(axisType, axisIndex),
      select: (axisType, axisIndex) => setSpreadsheetAxisSelection(axisType, axisIndex),
      label: (action, axisType) => axisOperationLabel(action, axisType),
      insert: (axisType) => {
        if (axisType === "column") addColumn();
        else addRow();
      },
      delete: (axisType, axisIndex) => deleteSelectedAxis(axisType, axisIndex)
    });
  }

  function attachHeaderSelectionHandlers(table) {
    const api = FM && FM.spreadsheetAxis;

    if (!api || typeof api.attachHeaderSelectionHandlers !== "function") {
      return;
    }

    api.attachHeaderSelectionHandlers(table, {
      select: (type, index, ev) => selectSpreadsheetAxis(type, index, ev),
      contextMenu: (type, index, ev) => openAxisMenu(type, index, ev.clientX, ev.clientY)
    });
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
        rowHeights: Array.from({ length: DEFAULT_ROWS }, () => DEFAULT_ROW_HEIGHT),
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

    const rowHeights = Array.from({ length: rowCount }, (_v, r) => {
      const meta = ws["!rows"] && ws["!rows"][r];
      return xlsxRowToPixelHeight(meta);
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
          valign: normalizeVerticalAlign(style && style.alignment && style.alignment.vertical),
          bg: cellFillKeyFromRgb(style && style.fill && style.fill.fgColor && style.fill.fgColor.rgb),
          fg: cellTextColorKeyFromRgb(style && style.font && style.font.color && style.font.color.rgb),
          border: cellBorderFromXlsxStyle(style && style.border)
        });
        cellFormats[r][c] = isEmptyCellFormat(fmt) ? null : fmt;
      }
    }

    return { rows, colWidths, rowHeights, cellFormats, tooLarge };
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
        rowHeights: Array.from({ length: DEFAULT_ROWS }, () => DEFAULT_ROW_HEIGHT),
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
        rowHeights: converted.rowHeights,
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

  function makeOutputFile(_XLSX) {
    // Export uses our own small XLSX writer so DNA-Nexus cell styles become
    // real Excel styles.xml + s="..." references. SheetJS CE still handles
    // parsing/import; this writer handles the styled save path.
    const out = buildStyledXlsxArrayBuffer();
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
            <button id="spreadsheetEditorDecimalsDecrease" type="button" class="btn secondary spreadsheetToolBtn" aria-pressed="false" aria-label="${tr("filemgr.spreadsheet_editor.decimals_decrease", null, "Decrease decimals")}" title="${tr("filemgr.spreadsheet_editor.decimals_decrease", null, "Decrease decimals")}">.0←</button>
            <button id="spreadsheetEditorDecimalsIncrease" type="button" class="btn secondary spreadsheetToolBtn" aria-pressed="false" aria-label="${tr("filemgr.spreadsheet_editor.decimals_increase", null, "Increase decimals")}" title="${tr("filemgr.spreadsheet_editor.decimals_increase", null, "Increase decimals")}">.00→</button>
            <button id="spreadsheetEditorAlignLeft" type="button" class="btn secondary spreadsheetToolBtn" aria-pressed="false" aria-label="${tr("filemgr.spreadsheet_editor.align_left", null, "Align left")}" title="${tr("filemgr.spreadsheet_editor.align_left", null, "Align left")}">
              <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M5 6h14"></path><path d="M5 10h10"></path><path d="M5 14h14"></path><path d="M5 18h8"></path></svg>
            </button>
            <button id="spreadsheetEditorAlignCenter" type="button" class="btn secondary spreadsheetToolBtn" aria-pressed="false" aria-label="${tr("filemgr.spreadsheet_editor.align_center", null, "Align center")}" title="${tr("filemgr.spreadsheet_editor.align_center", null, "Align center")}">
              <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M5 6h14"></path><path d="M7 10h10"></path><path d="M5 14h14"></path><path d="M8 18h8"></path></svg>
            </button>
            <button id="spreadsheetEditorValignTop" type="button" class="btn secondary spreadsheetToolBtn" aria-pressed="false" aria-label="${tr("filemgr.spreadsheet_editor.valign_top", null, "Align top")}" title="${tr("filemgr.spreadsheet_editor.valign_top", null, "Align top")}">
              <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M5 5h14"></path><path d="M8 10h8"></path><path d="M8 14h8"></path><path d="M12 20V9"></path><path d="M9 12l3-3 3 3"></path></svg>
            </button>
            <button id="spreadsheetEditorValignMiddle" type="button" class="btn secondary spreadsheetToolBtn" aria-pressed="false" aria-label="${tr("filemgr.spreadsheet_editor.valign_middle", null, "Align middle")}" title="${tr("filemgr.spreadsheet_editor.valign_middle", null, "Align middle")}">
              <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M5 5h14"></path><path d="M5 19h14"></path><path d="M8 12h8"></path><path d="M12 8v8"></path><path d="M9 11l3-3 3 3"></path><path d="M9 13l3 3 3-3"></path></svg>
            </button>
            <button id="spreadsheetEditorValignBottom" type="button" class="btn secondary spreadsheetToolBtn" aria-pressed="false" aria-label="${tr("filemgr.spreadsheet_editor.valign_bottom", null, "Align bottom")}" title="${tr("filemgr.spreadsheet_editor.valign_bottom", null, "Align bottom")}">
              <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M5 19h14"></path><path d="M8 10h8"></path><path d="M8 14h8"></path><path d="M12 4v11"></path><path d="M9 12l3 3 3-3"></path></svg>
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
        <div class="spreadsheetFormulaBar" aria-label="${tr("filemgr.spreadsheet_editor.formula_bar", null, "Formula bar")}">
          <div id="spreadsheetFormulaBarName" class="spreadsheetFormulaBarName mono" aria-label="${tr("filemgr.spreadsheet_editor.active_cell", null, "Active cell")}"></div>
          <input id="spreadsheetFormulaBarInput" class="spreadsheetFormulaBarInput" type="text" autocomplete="off" spellcheck="false" aria-label="${tr("filemgr.spreadsheet_editor.cell_contents", null, "Cell contents")}">
        </div>
        <div id="spreadsheetEditorTabs" class="spreadsheetEditorTabs"></div>
        <div id="spreadsheetEditorBody" class="spreadsheetEditorBody"></div>
      </div>
    `;

    document.body.appendChild(modal);

    titleEl = modal.querySelector("#spreadsheetEditorTitle");
    pathEl = modal.querySelector("#spreadsheetEditorPath");
    infoEl = modal.querySelector("#spreadsheetEditorInfo");
    formulaBarNameEl = modal.querySelector("#spreadsheetFormulaBarName");
    formulaBarInput = modal.querySelector("#spreadsheetFormulaBarInput");
    tabsEl = modal.querySelector("#spreadsheetEditorTabs");
    bodyEl = modal.querySelector("#spreadsheetEditorBody");
    saveBtn = modal.querySelector("#spreadsheetEditorSave");
    addRowBtn = modal.querySelector("#spreadsheetEditorAddRow");
    addColBtn = modal.querySelector("#spreadsheetEditorAddCol");
    boldBtn = modal.querySelector("#spreadsheetEditorBold");
    italicBtn = modal.querySelector("#spreadsheetEditorItalic");
    underlineBtn = modal.querySelector("#spreadsheetEditorUnderline");
    fontSizeSelect = modal.querySelector("#spreadsheetEditorFontSize");
    decreaseDecimalsBtn = modal.querySelector("#spreadsheetEditorDecimalsDecrease");
    increaseDecimalsBtn = modal.querySelector("#spreadsheetEditorDecimalsIncrease");
    alignLeftBtn = modal.querySelector("#spreadsheetEditorAlignLeft");
    alignCenterBtn = modal.querySelector("#spreadsheetEditorAlignCenter");
    valignTopBtn = modal.querySelector("#spreadsheetEditorValignTop");
    valignMiddleBtn = modal.querySelector("#spreadsheetEditorValignMiddle");
    valignBottomBtn = modal.querySelector("#spreadsheetEditorValignBottom");
    textColorBtn = modal.querySelector("#spreadsheetEditorTextColor");
    fillBtn = modal.querySelector("#spreadsheetEditorFill");
    closeBtn = modal.querySelector("#spreadsheetEditorClose");

    boldBtn?.addEventListener("click", () => applyFormatCommand("bold"));
    italicBtn?.addEventListener("click", () => applyFormatCommand("italic"));
    underlineBtn?.addEventListener("click", () => applyFormatCommand("underline"));
    fontSizeSelect?.addEventListener("change", () => applyFormatCommand("fontSize", fontSizeSelect.value));
    decreaseDecimalsBtn?.addEventListener("click", () => applyFormatCommand("decimals", "decrease"));
    increaseDecimalsBtn?.addEventListener("click", () => applyFormatCommand("decimals", "increase"));
    alignLeftBtn?.addEventListener("click", () => applyFormatCommand("align", "left"));
    alignCenterBtn?.addEventListener("click", () => applyFormatCommand("align", "center"));
    valignTopBtn?.addEventListener("click", () => applyFormatCommand("valign", "top"));
    valignMiddleBtn?.addEventListener("click", () => applyFormatCommand("valign", "middle"));
    valignBottomBtn?.addEventListener("click", () => applyFormatCommand("valign", "bottom"));
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

    formulaBarInput?.addEventListener("focus", () => {
      beginFormulaBarEdit();
      updateFormulaBar(true);
    });

    formulaBarInput?.addEventListener("input", () => {
      updateActiveCellFromFormulaBar();
    });

    formulaBarInput?.addEventListener("blur", () => {
      if (formulaFocus && formulaFocus.input === formulaBarInput) formulaFocus = null;
      updateFormulaBar(true);
    });

    formulaBarInput?.addEventListener("keydown", (ev) => {
      if (ev.key === "Enter") {
        ev.preventDefault();
        ev.stopPropagation();
        if (formulaFocus && formulaFocus.input === formulaBarInput) formulaFocus = null;
        refreshFormulaDisplays(null);
        focusActiveCellFromFormulaBar();
        return;
      }

      if (ev.key === "Escape") {
        ev.preventDefault();
        ev.stopPropagation();
        cancelFormulaBarEdit();
        formulaBarInput.blur();
        return;
      }

      if ((ev.key === "Delete" || ev.key === "Backspace") && !ev.ctrlKey && !ev.metaKey && !ev.altKey) {
        // Keep formula bar text editing local to the spreadsheet editor. This
        // prevents File Manager's global delete shortcut from seeing the key.
        ev.stopPropagation();
      }
    });

    modal.addEventListener("click", () => {
      hideTextColorMenu();
      hideFillMenu();
      hideAxisMenu();
      hideBorderMenu();
      // Clicking the editor backdrop should not be treated as an intent to
      // close. This protects normal spreadsheet work from accidental discard
      // prompts when the user clicks outside the grid area.
    });

    document.addEventListener("keydown", (ev) => {
      if (!modal.classList.contains("show")) return;
      if ((ev.ctrlKey || ev.metaKey) && ev.key.toLowerCase() === "s") {
        ev.preventDefault();
        saveCurrent();
      }
      if (ev.key === "Escape") {
        if (document.activeElement === formulaBarInput) {
          ev.preventDefault();
          ev.stopPropagation();
          return;
        }
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
    updateFormulaBar();

    const sheet = state.sheets[state.active] || { rows: [] };
    const rows = Array.isArray(sheet.rows) ? sheet.rows : [];
    const colCount = rows.reduce((m, row) => Math.max(m, Array.isArray(row) ? row.length : 0), 0);
    const colWidths = ensureSheetColWidths(sheet, colCount);
    const rowHeights = ensureSheetRowHeights(sheet, rows.length);
    const cache = computeSheetCache(sheet);

    const table = document.createElement("table");
    table.className = "spreadsheetEditorTable";
    table.setAttribute("aria-label", tr("filemgr.spreadsheet_editor.cell_grid", null, "Editable spreadsheet cells"));
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
      th.addEventListener("click", (ev) => selectSpreadsheetAxis("column", c, ev));
      th.addEventListener("keydown", (ev) => {
        if (ev.key === "Enter" || ev.key === " ") {
          ev.preventDefault();
          selectSpreadsheetAxis("column", c, ev);
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
      applyRowHeight(rh, rowHeights[rIdx]);

      const rowLabel = document.createElement("span");
      rowLabel.className = "spreadsheetRowLabel";
      rowLabel.textContent = String(rIdx + 1);
      rh.appendChild(rowLabel);

      const rowResize = document.createElement("span");
      rowResize.className = "spreadsheetRowResize";
      rowResize.setAttribute("role", "separator");
      rowResize.setAttribute("aria-orientation", "horizontal");
      rowResize.setAttribute("aria-label", tr("filemgr.spreadsheet_editor.resize_row", { row: String(rIdx + 1) }, `Resize row ${rIdx + 1}`));
      rowResize.addEventListener("pointerdown", (ev) => startRowResize(ev, rIdx));
      rh.appendChild(rowResize);

      rh.title = tr("filemgr.spreadsheet_editor.select_row", { row: String(rIdx + 1) }, `Select row ${rIdx + 1}`);
      rh.addEventListener("click", (ev) => selectSpreadsheetAxis("row", rIdx, ev));
      rh.addEventListener("keydown", (ev) => {
        if (ev.key === "Enter" || ev.key === " ") {
          ev.preventDefault();
          selectSpreadsheetAxis("row", rIdx, ev);
        }
      });
      trEl.appendChild(rh);

      for (let c = 0; c < colCount; c++) {
        const td = document.createElement("td");
        td.dataset.row = String(rIdx);
        td.dataset.col = String(c);
        applyColumnWidth(td, colWidths[c]);
        applyRowHeight(td, rowHeights[rIdx]);

        const input = document.createElement("input");

        input.type = "text";
        input.value = displayCellValue(sheet, rIdx, c, cache);
        input.dataset.row = String(rIdx);
        input.dataset.col = String(c);
        applyColumnWidth(input, colWidths[c]);
        applyRowHeight(input, rowHeights[rIdx]);
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
          updateFormulaBar(true);

          const raw = cellRaw(sheet, r, col);

          pendingCellEditHistory = {
            input,
            row: r,
            col,
            originalRaw: String(raw == null ? "" : raw),
            before: captureHistorySnapshot(),
            recorded: false
          };

          // Keep formatted numeric cells visually formatted when merely selected.
          // Raw formulas are still shown for direct formula editing; raw numeric
          // values remain available in the formula bar.
          input.value = isFormulaValue(raw) ? raw : displayCellValue(sheet, r, col);

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
          commitPendingCellEditHistory(input, r, col);
          refreshFormulaDisplays(null);
          input.value = displayCellValue(sheet, r, col);
          input.title = isFormulaValue(cellRaw(sheet, r, col)) ? cellRaw(sheet, r, col) : "";
          updateFormulaBar();
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

          if (ev.key === "Delete" || ev.key === "Backspace") {
            if (shouldSpreadsheetClearKey(input, ev)) {
              ev.preventDefault();
              ev.stopPropagation();
              clearSpreadsheetTargets(input);
              return;
            }

            // Keep spreadsheet text editing local to the cell. Without this,
            // Delete can bubble to File Manager's global delete shortcut and
            // open the trash confirmation for the selected file/item.
            ev.stopPropagation();
            return;
          }

          if (ev.key === "Enter") {
            ev.preventDefault();
            ev.stopPropagation();
            navigateSpreadsheetCell(input, ev.shiftKey ? -1 : 1, 0);
            return;
          }

          if (ev.key === "Tab") {
            ev.preventDefault();
            ev.stopPropagation();
            navigateSpreadsheetCell(input, 0, ev.shiftKey ? -1 : 1);
            return;
          }

          if (!ev.altKey && !ev.ctrlKey && !ev.metaKey && (
            ev.key === "ArrowUp" ||
            ev.key === "ArrowDown" ||
            ev.key === "ArrowLeft" ||
            ev.key === "ArrowRight"
          )) {
            if (!shouldSpreadsheetArrowNavigate(input, ev.key)) return;

            const delta =
              ev.key === "ArrowUp" ? [-1, 0] :
              ev.key === "ArrowDown" ? [1, 0] :
              ev.key === "ArrowLeft" ? [0, -1] :
              [0, 1];

            ev.preventDefault();
            ev.stopPropagation();
            navigateSpreadsheetCell(input, delta[0], delta[1]);
          }
        });

        input.addEventListener("copy", (ev) => {
          handleSpreadsheetCopy(ev, input);
        });

        input.addEventListener("paste", (ev) => {
          handleSpreadsheetPaste(ev, input);
        });

        input.addEventListener("input", () => {
          const r = Number(input.dataset.row);
          const col = Number(input.dataset.col);
          if (!Number.isInteger(r) || !Number.isInteger(col)) return;
          if (!state.sheets[state.active] || !state.sheets[state.active].rows[r]) return;

          const previousRaw = cellRaw(state.sheets[state.active], r, col);
          const previousFormulaFocus = formulaFocus && formulaFocus.input === input ? formulaFocus : null;

          if (previousRaw !== String(input.value == null ? "" : input.value)) {
            beginPendingCellEditHistory(input, r, col, previousRaw);
          }

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
          updateFormulaBar();
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
    const fallbackIndex = state.selection && state.selection.type === "row" && Number.isInteger(state.selection.index)
      ? state.selection.index
      : sheet.rows.length;
    const spec = axisInsertSpec("row", fallbackIndex, sheet.rows.length, MAX_EDIT_ROWS);
    const insertAt = Math.max(0, Math.min(spec.index, sheet.rows.length));
    const count = Math.max(0, Math.min(spec.count, MAX_EDIT_ROWS - sheet.rows.length));

    if (!count) {
      setStatus(tr("filemgr.spreadsheet_editor.row_limit", null, "Row limit reached."), "warn");
      return;
    }

    const historyBefore = captureHistorySnapshot();

    for (let i = 0; i < count; i++) {
      adjustSheetFormulasForAxisChange(sheet, "row", insertAt, 1);
    }

    ensureSheetCellFormats(sheet, sheet.rows.length, cols);
    ensureSheetRowHeights(sheet, sheet.rows.length);

    for (let i = 0; i < count; i++) {
      const pos = insertAt + i;
      sheet.cellFormats.splice(pos, 0, Array.from({ length: cols }, () => null));
      sheet.rowHeights.splice(pos, 0, DEFAULT_ROW_HEIGHT);
      sheet.rows.splice(pos, 0, Array.from({ length: cols }, () => ""));
    }

    state.selection = { type: "row", index: insertAt, start: insertAt, end: insertAt + count - 1, anchor: insertAt };
    commitHistorySnapshot(historyBefore);
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

    const fallbackIndex = state.selection && state.selection.type === "column" && Number.isInteger(state.selection.index)
      ? state.selection.index
      : cols;
    const spec = axisInsertSpec("column", fallbackIndex, cols, MAX_EDIT_COLS);
    const insertAt = Math.max(0, Math.min(spec.index, cols));
    const count = Math.max(0, Math.min(spec.count, MAX_EDIT_COLS - cols));

    if (!count) {
      setStatus(tr("filemgr.spreadsheet_editor.col_limit", null, "Column limit reached."), "warn");
      return;
    }

    const historyBefore = captureHistorySnapshot();

    for (let i = 0; i < count; i++) {
      adjustSheetFormulasForAxisChange(sheet, "column", insertAt, 1);
    }

    ensureSheetColWidths(sheet, cols);
    ensureSheetCellFormats(sheet, sheet.rows.length, cols);

    sheet.colWidths.splice(insertAt, 0, ...Array.from({ length: count }, () => DEFAULT_COL_WIDTH));

    for (const fmtRow of sheet.cellFormats) {
      if (Array.isArray(fmtRow)) {
        fmtRow.splice(insertAt, 0, ...Array.from({ length: count }, () => null));
      }
    }

    if (!sheet.rows.length) {
      sheet.rows.push(Array.from({ length: Math.max(1, insertAt + count) }, () => ""));
    }

    for (const row of sheet.rows) {
      while (row.length < cols) row.push("");
      row.splice(insertAt, 0, ...Array.from({ length: count }, () => ""));
    }

    state.selection = { type: "column", index: insertAt, start: insertAt, end: insertAt + count - 1, anchor: insertAt };
    commitHistorySnapshot(historyBefore);
    render();
  }

  function deleteSelectedAxis(type, index) {
    if (state.readOnly || state.tooLarge) return;

    const sheet = state.sheets[state.active];
    if (!sheet || !Array.isArray(sheet.rows)) return;

    if (type === "row") {
      if (!sheet.rows.length) return;

      const cols = Math.max(
        DEFAULT_COLS,
        sheet.rows.reduce((m, row) => Math.max(m, Array.isArray(row) ? row.length : 0), 0)
      );
      const spec = axisDeleteSpec("row", index, sheet.rows.length);
      const deleteAt = Math.max(0, Math.min(spec.index, sheet.rows.length - 1));
      const count = Math.max(0, Math.min(spec.count, sheet.rows.length - deleteAt));
      if (!count) return;

      const historyBefore = captureHistorySnapshot();

      ensureSheetCellFormats(sheet);
      ensureSheetRowHeights(sheet, sheet.rows.length);

      for (let i = 0; i < count && sheet.rows.length; i++) {
        adjustSheetFormulasForAxisChange(sheet, "row", deleteAt, -1);
        sheet.cellFormats.splice(deleteAt, 1);
        sheet.rowHeights.splice(deleteAt, 1);
        sheet.rows.splice(deleteAt, 1);
      }

      if (!sheet.rows.length) {
        sheet.rows.push(Array.from({ length: cols }, () => ""));
        sheet.rowHeights = [DEFAULT_ROW_HEIGHT];
      } else {
        ensureSheetRowHeights(sheet, sheet.rows.length);
      }

      const nextIndex = Math.max(0, Math.min(deleteAt, sheet.rows.length - 1));
      state.selection = { type: "row", index: nextIndex, start: nextIndex, end: nextIndex, anchor: nextIndex };

      commitHistorySnapshot(historyBefore);
      render();
      return;
    }

    if (type === "column") {
      if (!sheet.rows.length) {
        sheet.rows.push(Array.from({ length: DEFAULT_COLS }, () => ""));
      }

      const colCount = sheet.rows.reduce((m, row) => Math.max(m, Array.isArray(row) ? row.length : 0), 0);
      if (!colCount) return;

      const spec = axisDeleteSpec("column", index, colCount);
      const deleteAt = Math.max(0, Math.min(spec.index, colCount - 1));
      const count = Math.max(0, Math.min(spec.count, colCount - deleteAt));
      if (!count) return;

      const historyBefore = captureHistorySnapshot();

      ensureSheetColWidths(sheet, colCount);
      ensureSheetCellFormats(sheet, sheet.rows.length, colCount);

      for (let i = 0; i < count; i++) {
        adjustSheetFormulasForAxisChange(sheet, "column", deleteAt, -1);
      }

      if (colCount <= count) {
        sheet.colWidths = [DEFAULT_COL_WIDTH];
        sheet.cellFormats = sheet.rows.map(() => [null]);
        for (const row of sheet.rows) {
          if (!Array.isArray(row)) continue;
          row.splice(0, row.length, "");
        }

        state.selection = { type: "column", index: 0, start: 0, end: 0, anchor: 0 };
      } else {
        sheet.colWidths.splice(deleteAt, count);

        for (const fmtRow of sheet.cellFormats) {
          if (Array.isArray(fmtRow)) fmtRow.splice(deleteAt, count);
        }

        for (const row of sheet.rows) {
          if (!Array.isArray(row)) continue;
          while (row.length < colCount) row.push("");
          row.splice(deleteAt, count);
        }

        const nextIndex = Math.max(0, Math.min(deleteAt, colCount - count - 1));
        state.selection = { type: "column", index: nextIndex, start: nextIndex, end: nextIndex, anchor: nextIndex };
      }

      commitHistorySnapshot(historyBefore);
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

      if (spreadsheetHistory && typeof spreadsheetHistory.markClean === "function") {
        spreadsheetHistory.markClean();
      }
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
    attachHistoryKeyboard();
    setStatus(tr("filemgr.spreadsheet_editor.loading", null, "Loading spreadsheet editor…"), "warn");
    updateButtons();

    try {
      state.sheets = await readWorkbook({ rel, name, url });
      resetSpreadsheetHistory();
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
