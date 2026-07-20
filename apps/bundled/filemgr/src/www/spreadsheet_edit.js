window.PQNAS_FILEMGR = window.PQNAS_FILEMGR || {};

(() => {
  "use strict";

  const FM = window.PQNAS_FILEMGR;
  const COLOR_PALETTES =
    FM && FM.spreadsheetColorPalettes;

  if (
    !COLOR_PALETTES ||
    typeof COLOR_PALETTES.createColorMap !==
      "function"
  ) {
    throw new Error(
      "spreadsheet color palette module did not register"
    );
  }

  const XLSX_VENDOR_URL = "./vendor/xlsx.full.min.js";
  const XLSX_MIME = "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";

  const MAX_EDIT_ROWS = 200;
  const MAX_EDIT_COLS = 50;
  const DEFAULT_ROWS = 10;
  const DEFAULT_COLS = 5;
  const DEFAULT_COL_WIDTH = 96;
  const MIN_COL_WIDTH = 72;
  const MAX_COL_WIDTH = 520;
  const DEFAULT_ROW_HEIGHT = 24;
  const MIN_ROW_HEIGHT = 18;
  const MAX_ROW_HEIGHT = 220;
  const STYLE_SHEET_NAME = "_pqnas_styles";
  const STYLE_META_VERSION = "pqnas-spreadsheet-style-v1";
  const STYLE_META_CHUNK_SIZE = 30000;

  // Document content colors: these are spreadsheet cell fill values, not
  // DNA-Nexus UI theme colors. Fixed values are intentional for XLSX output.
  const CELL_FILL_COLORS =
    COLOR_PALETTES.createColorMap(
      Object.freeze({
        yellow: {
          css: "rgb(255, 242, 204)",
          rgb: "FFFFF2CC"
        },
        green: {
          css: "rgb(217, 234, 211)",
          rgb: "FFD9EAD3"
        },
        blue: {
          css: "rgb(207, 226, 243)",
          rgb: "FFCFE2F3"
        },
        red: {
          css: "rgb(244, 204, 204)",
          rgb: "FFF4CCCC"
        },
        gray: {
          css: "rgb(217, 217, 217)",
          rgb: "FFD9D9D9"
        }
      })
    );

  // Document content colors for spreadsheet text/font color.
  const TEXT_COLOR_COLORS =
    COLOR_PALETTES.createColorMap(
      Object.freeze({
        black: {
          css: "rgb(0, 0, 0)",
          rgb: "FF000000"
        },
        red: {
          css: "rgb(204, 0, 0)",
          rgb: "FFCC0000"
        },
        green: {
          css: "rgb(56, 118, 29)",
          rgb: "FF38761D"
        },
        blue: {
          css: "rgb(17, 85, 204)",
          rgb: "FF1155CC"
        },
        gray: {
          css: "rgb(102, 102, 102)",
          rgb: "FF666666"
        },
        white: {
          css: "rgb(255, 255, 255)",
          rgb: "FFFFFFFF"
        }
      })
    );

  const FONT_SIZE_OPTIONS = Object.freeze([10, 12, 14, 16, 18, 24, 32]);
  const BORDER_STYLE_KEYS = Object.freeze(["thin", "medium", "double"]);
  const BORDER_SIDES = Object.freeze(["top", "right", "bottom", "left"]);
  const BORDER_XLSX_COLOR = "FF000000";
  const MIN_DECIMAL_PLACES = 0;
  const MAX_DECIMAL_PLACES = 10;

  // Whitelisted display formats only. Do not accept arbitrary Excel format
  // strings from user content; export builds safe numFmt values from this map.
  const CURRENCY_FORMATS = Object.freeze({
    eur: { label: "€ Euro", prefix: "", suffix: " €" },
    usd: { label: "$ Dollar", prefix: "$", suffix: "" },
    gbp: { label: "£ Pound", prefix: "£", suffix: "" },
    sek: { label: "kr Krona", prefix: "", suffix: " kr" },
    cny: { label: "¥ Yuan", prefix: "¥", suffix: "" }
  });

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
  let insertImageBtn = null;
  let insertImageInput = null;
  let boldBtn = null;
  let italicBtn = null;
  let underlineBtn = null;
  let fontNameSelect = null;
  let fontSizeSelect = null;
  let decreaseDecimalsBtn = null;
  let increaseDecimalsBtn = null;
  let numberFormatSelect = null;
  let alignSelect = null;
  let valignSelect = null;
  let sortSelect = null;
  let alignMenu = null;
  let valignMenu = null;
  let sortMenu = null;
  let freezeTopRowBtn = null;
  let freezeFirstColumnBtn = null;
  let textColorBtn = null;
  let fillBtn = null;
  let closeBtn = null;
  let confirmModal = null;
  let sheetTabMenu = null;
  let axisMenu = null;
  let fillMenu = null;
  let textColorMenu = null;
  let borderMenu = null;
  let xlsxLoadPromise = null;
  let formulaFocus = null;
  let spreadsheetHistory = null;
  let historyKeyboardAttached = false;
  let pendingCellEditHistory = null;
  let preserveSelectionForContextMenu = null;
  let fillHandleState = null;

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
    editorGeometry: null,
    selectedImageId: "",
    workbookFont: null
  };

  function tr(key, vars = null, fallback = "") {
    try {
      if (window.PQNAS_I18N && typeof window.PQNAS_I18N.t === "function") {
        return window.PQNAS_I18N.t(key, vars, fallback || key);
      }
    } catch (_) {}
    return fallback || key;
  }

  const TOOLBAR_ICON_OPTIONS = Object.freeze({
    align: Object.freeze({
      left: Object.freeze({ key: "filemgr.spreadsheet_editor.align_left", fallback: "Left" }),
      center: Object.freeze({ key: "filemgr.spreadsheet_editor.align_center", fallback: "Center" }),
      right: Object.freeze({ key: "filemgr.spreadsheet_editor.align_right", fallback: "Tasaa oikealle" })
    }),
    valign: Object.freeze({
      top: Object.freeze({ key: "filemgr.spreadsheet_editor.valign_top", fallback: "Top" }),
      middle: Object.freeze({ key: "filemgr.spreadsheet_editor.valign_middle", fallback: "Middle" }),
      bottom: Object.freeze({ key: "filemgr.spreadsheet_editor.valign_bottom", fallback: "Bottom" })
    })
  });

  function normalizeToolbarIconValue(kind, value) {
    const key = String(value || "").trim();
    const options = TOOLBAR_ICON_OPTIONS[kind] || {};
    if (Object.prototype.hasOwnProperty.call(options, key)) return key;
    return kind === "valign" ? "middle" : "left";
  }

  function toolbarIconLabel(kind, value) {
    const normalized = normalizeToolbarIconValue(kind, value);
    const def = TOOLBAR_ICON_OPTIONS[kind] && TOOLBAR_ICON_OPTIONS[kind][normalized];
    return def ? tr(def.key, null, def.fallback) : normalized;
  }

  function toolbarIconPath(kind, value) {
    const normalized = normalizeToolbarIconValue(kind, value);

    // Security: toolbar icons are selected from a fixed whitelist. Workbook or
    // user-controlled values are never rendered as arbitrary SVG/HTML.
    if (kind === "valign") {
      if (normalized === "top") return "M5 5h14M8 10h8M8 14h8M12 20V8M9 11l3-3 3 3";
      if (normalized === "bottom") return "M5 19h14M8 10h8M8 14h8M12 4v12M9 13l3 3 3-3";
      return "M5 12h14M8 7h8M8 17h8M12 4v5M9 7l3 3 3-3M12 20v-5M9 17l3-3 3 3";
    }

    if (normalized === "center") return "M4 6h16M7 10h10M4 14h16M8 18h8";
    if (normalized === "right") return "M4 6h16M9 10h11M4 14h16M10 18h10";
    return "M4 6h16M4 10h11M4 14h16M4 18h10";
  }

  function toolbarIconSvg(kind, value) {
    return `<svg viewBox="0 0 24 24" aria-hidden="true"><path d="${toolbarIconPath(kind, value)}"/></svg>`;
  }

  function createToolbarIconSvg(kind, value) {
    const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
    svg.setAttribute("viewBox", "0 0 24 24");
    svg.setAttribute("aria-hidden", "true");

    const path = document.createElementNS("http://www.w3.org/2000/svg", "path");
    path.setAttribute("d", toolbarIconPath(kind, value));
    svg.appendChild(path);

    return svg;
  }

  function setToolbarIconButtonValue(button, kind, value) {
    if (!button) return;

    const normalized = normalizeToolbarIconValue(kind, value);
    const label = toolbarIconLabel(kind, normalized);

    button.value = normalized;
    button.replaceChildren(createToolbarIconSvg(kind, normalized));
    button.title = label;
    button.setAttribute("aria-label", label);

    const menu = kind === "valign" ? valignMenu : alignMenu;
    if (!menu) return;

    for (const item of menu.querySelectorAll("[data-spreadsheet-toolbar-value]")) {
      item.setAttribute("aria-checked", item.dataset.spreadsheetToolbarValue === normalized ? "true" : "false");
    }
  }

  function closeToolbarIconMenus(exceptMenu = null) {
    for (const [button, menu] of [
      [alignSelect, alignMenu],
      [valignSelect, valignMenu],
      [sortSelect, sortMenu]
    ]) {
      if (!menu || menu === exceptMenu) continue;
      menu.hidden = true;
      if (button) button.setAttribute("aria-expanded", "false");
    }
  }

  function positionToolbarIconMenu(button, menu) {
    if (!button || !menu) return;

    const rect = button.getBoundingClientRect();
    const gap = 6;

    menu.style.left = "0px";
    menu.style.top = "0px";
    menu.hidden = false;

    const menuRect = menu.getBoundingClientRect();
    const left = Math.min(Math.max(8, rect.left), Math.max(8, window.innerWidth - menuRect.width - 8));
    const top = Math.min(Math.max(8, rect.bottom + gap), Math.max(8, window.innerHeight - menuRect.height - 8));

    menu.style.left = `${left}px`;
    menu.style.top = `${top}px`;
  }

  function toggleToolbarIconMenu(button, menu) {
    if (!button || !menu || button.disabled) return;

    const willOpen = menu.hidden;
    closeToolbarIconMenus(menu);

    if (willOpen) {
      positionToolbarIconMenu(button, menu);
      button.setAttribute("aria-expanded", "true");
    } else {
      menu.hidden = true;
      button.setAttribute("aria-expanded", "false");
    }
  }

  function bindToolbarIconMenu(menu, kind, button) {
    if (!menu || !button) return;

    for (const item of menu.querySelectorAll("[data-spreadsheet-toolbar-value]")) {
      item.addEventListener("click", () => {
        const value = normalizeToolbarIconValue(kind, item.dataset.spreadsheetToolbarValue);
        applyFormatCommand(kind, value);
        setToolbarIconButtonValue(button, kind, value);
        closeToolbarIconMenus();
      });
    }
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
    return !!TEXT_COLOR_COLORS[
      String(value || "")
    ];
  }

  function normalizeTextColorKey(value) {
    const key =
      String(value || "").trim();

    return isValidTextColorKey(key)
      ? key
      : "";
  }

  function cellTextColorKeyFromRgb(value) {
    const normalized =
      COLOR_PALETTES.normalizeArgb(value);

    for (
      const [key, def] of
      Object.entries(TEXT_COLOR_COLORS)
    ) {
      if (
        String(def.rgb || "")
          .toUpperCase() === normalized
      ) {
        return key;
      }
    }

    return normalizeTextColorKey(
      normalized
    );
  }

  function isValidFillColorKey(value) {
    return !!CELL_FILL_COLORS[
      String(value || "")
    ];
  }

  function normalizeFillColorKey(value) {
    const key =
      String(value || "").trim();

    return isValidFillColorKey(key)
      ? key
      : "";
  }

  function cellFillKeyFromRgb(value) {
    const normalized =
      COLOR_PALETTES.normalizeArgb(value);

    for (
      const [key, def] of
      Object.entries(CELL_FILL_COLORS)
    ) {
      if (
        String(def.rgb || "")
          .toUpperCase() === normalized
      ) {
        return key;
      }
    }

    return normalizeFillColorKey(
      normalized
    );
  }

  function normalizeFontSize(value) {
    const n = Number(value);
    if (!Number.isFinite(n)) return 0;
    const rounded = Math.round(n);
    return FONT_SIZE_OPTIONS.includes(rounded) ? rounded : 0;
  }

  function normalizeBorderSide(value) {
    const api = FM && FM.spreadsheetXlsxBorders;

    if (api && typeof api.normalizeSide === "function") {
      return api.normalizeSide(value);
    }

    const key = String(value || "").trim().toLowerCase();
    if (key === "thick") return "medium";
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
    const rowHeight = normalizeRowGeometry(
      parseFloat(input.style.height) ||
      (typeof input.getBoundingClientRect === "function" ? input.getBoundingClientRect().height : 0),
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
    const api = FM && FM.spreadsheetXlsxBorders;

    if (api && typeof api.normalizeBorder === "function") {
      return api.normalizeBorder(border);
    }

    const src = border && typeof border === "object" ? border : {};
    return {
      top: normalizeBorderSide(src.top),
      right: normalizeBorderSide(src.right),
      bottom: normalizeBorderSide(src.bottom),
      left: normalizeBorderSide(src.left)
    };
  }

  function isEmptyBorderFormat(border) {
    const api = FM && FM.spreadsheetXlsxBorders;

    if (api && typeof api.isEmptyBorder === "function") {
      return api.isEmptyBorder(border);
    }

    const b = normalizeBorderFormat(border);
    return !b.top && !b.right && !b.bottom && !b.left;
  }

  function borderCssWidth(style) {
    const api = FM && FM.spreadsheetXlsxBorders;

    if (api && typeof api.cssWidth === "function") {
      return api.cssWidth(style);
    }

    const normalized = normalizeBorderSide(style);
    if (normalized === "double") return "3px";
    if (normalized === "medium") return "2px";
    return normalized === "thin" ? "1px" : "";
  }

  function borderXlsxStyle(style) {
    const api = FM && FM.spreadsheetXlsxBorders;

    if (api && typeof api.toXlsxStyle === "function") {
      return api.toXlsxStyle(style);
    }

    return normalizeBorderSide(style);
  }

  function cellBorderSideFromXlsx(side) {
    const api = FM && FM.spreadsheetXlsxBorders;

    if (api && typeof api.fromXlsxSide === "function") {
      return api.fromXlsxSide(side);
    }

    return normalizeBorderSide(side && side.style);
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
      const varName = `--spreadsheet-cell-border-${side}-width`;
      const styleVarName = `--spreadsheet-cell-border-${side}-style`;
      const borderApi = FM && FM.spreadsheetXlsxBorders;
      const lineStyle =
        borderApi && typeof borderApi.cssLineStyle === "function"
          ? borderApi.cssLineStyle(b[side])
          : (b[side] === "double" ? "double" : "solid");

      // Custom spreadsheet borders are painted by a td::after overlay. This
      // avoids theme-level table border overrides, especially Win Classic's
      // global tbody td border-bottom rule, from hiding the user's border.
      cell.style.removeProperty(prop);
      cell.style.removeProperty(`border-${side}`);

      if (width) {
        hasBorder = true;
        cell.style.setProperty(varName, width);
        cell.style.setProperty(styleVarName, lineStyle);
      } else {
        cell.style.removeProperty(varName);
        cell.style.removeProperty(styleVarName);
      }
    }

    if (hasBorder) {
      cell.dataset.spreadsheetCellBorder = "1";
    } else {
      cell.removeAttribute("data-spreadsheet-cell-border");
      for (const side of BORDER_SIDES) {
        cell.style.removeProperty(`--spreadsheet-cell-border-${side}-width`);
        cell.style.removeProperty(`--spreadsheet-cell-border-${side}-style`);
      }
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


  function spreadsheetDateSerialFromParts(year, month, day) {
    const y = Number(year);
    const m = Number(month);
    const d = Number(day);

    if (!Number.isInteger(y) || !Number.isInteger(m) || !Number.isInteger(d)) return null;
    if (y < 1900 || y > 9999 || m < 1 || m > 12 || d < 1 || d > 31) return null;

    const ms = Date.UTC(y, m - 1, d);
    const date = new Date(ms);

    if (
      date.getUTCFullYear() !== y ||
      date.getUTCMonth() !== m - 1 ||
      date.getUTCDate() !== d
    ) {
      return null;
    }

    return Math.floor(ms / 86400000);
  }

  function spreadsheetDateSerialFromText(value) {
    const text = String(value == null ? "" : value).trim();
    if (!text) return null;

    let m = text.match(/^([0-9]{1,2})\.([0-9]{1,2})\.([0-9]{4})$/);
    if (m) return spreadsheetDateSerialFromParts(Number(m[3]), Number(m[2]), Number(m[1]));

    m = text.match(/^([0-9]{4})-([0-9]{1,2})-([0-9]{1,2})$/);
    if (m) return spreadsheetDateSerialFromParts(Number(m[1]), Number(m[2]), Number(m[3]));

    return null;
  }

  function formatSpreadsheetDateSerial(serial) {
    const n = Number(serial);
    if (!Number.isFinite(n)) return "";

    const date = new Date(Math.round(n) * 86400000);
    const y = date.getUTCFullYear();
    const m = String(date.getUTCMonth() + 1).padStart(2, "0");
    const d = String(date.getUTCDate()).padStart(2, "0");

    return `${d}.${m}.${y}`;
  }

  function formatSpreadsheetDateDisplayValue(value) {
    const serial = spreadsheetDateSerialFromText(value);
    return serial == null ? "" : formatSpreadsheetDateSerial(serial);
  }

function isValidCurrencyKey(value) {
    return Object.prototype.hasOwnProperty.call(CURRENCY_FORMATS, String(value || ""));
  }

  function normalizeCurrencyKey(value) {
    const key = String(value || "").trim().toLowerCase();
    return isValidCurrencyKey(key) ? key : "";
  }

  function normalizeNumberFormat(value, currency) {
    const key = String(value || "").trim().toLowerCase();
    if (key === "percent") return "percent";
    if (key === "date") return "date";
    return key === "currency" && normalizeCurrencyKey(currency) ? "currency" : "";
  }

  function formatNumericDisplayValue(value, fmt) {
    const f = normalizeCellFormat(fmt);
    const n = Number(value);

    if (!Number.isFinite(n)) {
      return String(value == null ? "" : value);
    }

    if (f.numberFormat === "date") {
      return formatSpreadsheetDateSerial(n) || String(value == null ? "" : value);
    }

    if (f.numberFormat === "percent") {
      const decimals = f.decimals == null ? 2 : f.decimals;
      return `${formatDecimalNumber(n * 100, decimals)}%`;
    }

    const currency = f.numberFormat === "currency" ? CURRENCY_FORMATS[f.currency] : null;
    const decimals = currency && f.decimals == null ? 2 : f.decimals;
    const base = decimals == null ? String(value == null ? "" : value) : formatDecimalNumber(n, decimals);

    return currency ? `${currency.prefix}${base}${currency.suffix}` : base;
  }

function inferCellDecimalPlaces(sheet, row, col, cache = null) {
    const raw = cellRaw(sheet, row, col);

    if (isForcedTextValue(raw)) {
      return 0;
    }

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

  function normalizeSpreadsheetFontName(value) {
    const api = FM && FM.spreadsheetFonts;

    if (api && typeof api.normalizeFontName === "function") {
      return api.normalizeFontName(value);
    }

    return String(value == null ? "" : value)
      .replace(/[\u0000-\u001F\u007F]/g, "")
      .trim()
      .slice(0, 128);
  }

  function workbookDefaultFontName() {
    const font = normalizedWorkbookFont(state.workbookFont);
    return normalizeSpreadsheetFontName(font.name) || "Calibri";
  }

  function effectiveCellFontName(fmt) {
    const f = normalizeCellFormat(fmt);
    return f.fontName || workbookDefaultFontName();
  }

  function spreadsheetCssFontFamily(name, fallback = "") {
    const api = FM && FM.spreadsheetFonts;

    if (api && typeof api.cssFontFamily === "function") {
      return api.cssFontFamily(name, fallback);
    }

    const normalized =
      normalizeSpreadsheetFontName(name) ||
      normalizeSpreadsheetFontName(fallback);

    return normalized ? `"${normalized}"` : "";
  }

  function normalizeCellFormat(fmt) {
    const src = fmt && typeof fmt === "object" ? fmt : {};
    const align = src.align === "center" || src.align === "right" || src.align === "left" ? src.align : "";
    const valign = normalizeVerticalAlign(src.valign || src.verticalAlign || src.vertical);
    const rawCurrency = normalizeCurrencyKey(src.currency);
    const numberFormat = normalizeNumberFormat(src.numberFormat, rawCurrency);
    const currency = numberFormat === "currency" ? rawCurrency : "";
    const decimals = normalizeDecimalPlaces(src.decimals);
    return {
      bold: !!src.bold,
      italic: !!src.italic,
      underline: !!src.underline,
      fontName: normalizeSpreadsheetFontName(
        src.fontName || src.fontFamily || src.font
      ),
      fontSize: normalizeFontSize(src.fontSize || src.sz),
      decimals,
      numberFormat,
      currency,
      align,
      valign,
      bg: normalizeFillColorKey(src.bg),
      fg: normalizeTextColorKey(src.fg),
      border: normalizeBorderFormat(src.border)
    };
  }

  function isEmptyCellFormat(fmt) {
    const f = normalizeCellFormat(fmt);
    return !f.bold && !f.italic && !f.underline && !f.fontName && !f.fontSize && f.decimals == null && !f.numberFormat && !f.currency && !f.align && !f.valign && !f.bg && !f.fg && isEmptyBorderFormat(f.border);
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




  function cellFormatKey(fmt) {
    return JSON.stringify(normalizeCellFormat(fmt));
  }
function normalizeMergeRange(merge, rowCount = null, colCount = null) {
    const src = merge && typeof merge === "object" ? merge : {};
    const start = src.s && typeof src.s === "object" ? src.s : {};
    const end = src.e && typeof src.e === "object" ? src.e : {};

    const sr = Number(start.r);
    const sc = Number(start.c);
    const er = Number(end.r);
    const ec = Number(end.c);

    if (![sr, sc, er, ec].every(Number.isInteger)) return null;

    let row1 = Math.min(sr, er);
    let row2 = Math.max(sr, er);
    let col1 = Math.min(sc, ec);
    let col2 = Math.max(sc, ec);

    if (Number.isInteger(rowCount) && rowCount > 0) {
      if (row1 >= rowCount || row2 < 0) return null;
      row1 = Math.max(0, row1);
      row2 = Math.min(rowCount - 1, row2);
    }

    if (Number.isInteger(colCount) && colCount > 0) {
      if (col1 >= colCount || col2 < 0) return null;
      col1 = Math.max(0, col1);
      col2 = Math.min(colCount - 1, col2);
    }

    if (row1 < 0 || col1 < 0 || row2 < row1 || col2 < col1) return null;
    if (row1 === row2 && col1 === col2) return null;

    return { s: { r: row1, c: col1 }, e: { r: row2, c: col2 } };
  }

  function mergeRangeKey(merge) {
    const m = normalizeMergeRange(merge);
    return m ? `${m.s.r}:${m.s.c}:${m.e.r}:${m.e.c}` : "";
  }

  function mergeRangesOverlap(a, b) {
    const ma = normalizeMergeRange(a);
    const mb = normalizeMergeRange(b);
    if (!ma || !mb) return false;

    return ma.s.r <= mb.e.r &&
      ma.e.r >= mb.s.r &&
      ma.s.c <= mb.e.c &&
      ma.e.c >= mb.s.c;
  }

  function ensureSheetMerges(sheet, rowCount = null, colCount = null) {
    if (!sheet) return [];

    const src = Array.isArray(sheet.merges) ? sheet.merges : [];
    const out = [];
    const seen = new Set();

    for (const raw of src) {
      const merge = normalizeMergeRange(raw, rowCount, colCount);
      if (!merge) continue;

      const key = mergeRangeKey(merge);
      if (!key || seen.has(key)) continue;
      if (out.some((existing) => mergeRangesOverlap(existing, merge))) continue;

      seen.add(key);
      out.push(merge);
    }

    sheet.merges = out;
    return sheet.merges;
  }

  function mergeContainsCell(merge, row, col) {
    const m = normalizeMergeRange(merge);
    return !!m &&
      Number.isInteger(row) &&
      Number.isInteger(col) &&
      row >= m.s.r &&
      row <= m.e.r &&
      col >= m.s.c &&
      col <= m.e.c;
  }

  function mergeIsAnchorCell(merge, row, col) {
    const m = normalizeMergeRange(merge);
    return !!m && row === m.s.r && col === m.s.c;
  }

  function mergeAtCell(sheet, row, col) {
    const merges = ensureSheetMerges(sheet);
    return merges.find((merge) => mergeContainsCell(merge, row, col)) || null;
  }

  function isMergeCoveredCell(sheet, row, col) {
    const merge = mergeAtCell(sheet, row, col);
    return !!merge && !mergeIsAnchorCell(merge, row, col);
  }

  function selectedMergeRange() {
    const range = normalizedRangeSelection();
    if (!range) return null;
    if (range.row1 === range.row2 && range.col1 === range.col2) return null;

    return normalizeMergeRange({
      s: { r: range.row1, c: range.col1 },
      e: { r: range.row2, c: range.col2 }
    });
  }

  function mergeColumnPixelWidth(colWidths, col1, col2) {
    let total = 0;
    for (let c = col1; c <= col2; c++) {
      total += normalizeColumnGeometry(colWidths[c], DEFAULT_COL_WIDTH);
    }
    return Math.max(1, total);
  }

  function mergeRowPixelHeight(rowHeights, row1, row2) {
    let total = 0;
    for (let r = row1; r <= row2; r++) {
      total += normalizeRowGeometry(rowHeights[r], DEFAULT_ROW_HEIGHT);
    }
    return Math.max(1, total);
  }

  function mergeRangeHasHiddenData(sheet, merge) {
    const m = normalizeMergeRange(merge);
    if (!sheet || !m) return false;

    for (let r = m.s.r; r <= m.e.r; r++) {
      for (let c = m.s.c; c <= m.e.c; c++) {
        if (r === m.s.r && c === m.s.c) continue;
        if (String(cellRaw(sheet, r, c) || "").trim() !== "") return true;
      }
    }

    return false;
  }

  function applyMergeSelectedCells() {
    if (state.readOnly || state.tooLarge) return;

    const sheet = state.sheets[state.active];
    const merge = selectedMergeRange();

    if (!sheet || !merge) {
      setStatus(tr("filemgr.spreadsheet_editor.merge_select_range", null, "Select a cell range first."), "warn");
      return;
    }

    const existing = ensureSheetMerges(sheet);
    if (existing.some((item) => mergeRangesOverlap(item, merge))) {
      setStatus(tr("filemgr.spreadsheet_editor.merge_overlaps", null, "Cannot merge a range that overlaps existing merged cells."), "warn");
      return;
    }

    if (mergeRangeHasHiddenData(sheet, merge)) {
      setStatus(tr(
        "filemgr.spreadsheet_editor.merge_hidden_data",
        null,
        "Cannot merge cells because the selected range contains values outside the top-left cell."
      ), "warn");
      return;
    }

    const historyBefore = captureHistorySnapshot();

    // Preserve workbook data: merging keeps the top-left cell as the only value
    // owner and only allows the operation when no hidden cell values would be lost.
    ensureSheetMerges(sheet).push(merge);
    state.rangeSelection = null;
    state.selection = null;
    state.activeCell = { row: merge.s.r, col: merge.s.c };
    setDirty(true);

    commitHistorySnapshot(historyBefore);
    render();
    setStatus(tr("filemgr.spreadsheet_editor.merge_done", null, "Merged cells."), "");
  }

  function applyUnmergeAtCell(row, col) {
    if (state.readOnly || state.tooLarge) return;

    const sheet = state.sheets[state.active];
    if (!sheet) return;

    const merge = mergeAtCell(sheet, row, col);
    if (!merge) {
      setStatus(tr("filemgr.spreadsheet_editor.unmerge_none", null, "This cell is not merged."), "warn");
      return;
    }

    const key = mergeRangeKey(merge);
    const historyBefore = captureHistorySnapshot();

    sheet.merges = ensureSheetMerges(sheet).filter((item) => mergeRangeKey(item) !== key);
    state.rangeSelection = null;
    state.selection = null;
    state.activeCell = { row: merge.s.r, col: merge.s.c };
    setDirty(true);

    commitHistorySnapshot(historyBefore);
    render();
    setStatus(tr("filemgr.spreadsheet_editor.unmerge_done", null, "Unmerged cells."), "");
  }

  function applyCellFormatToInput(input, fmt) {
    if (!input) return;

    const cell = input.closest ? input.closest("td[data-row][data-col]") : null;
    const f = normalizeCellFormat(fmt);

    input.style.fontWeight = f.bold ? "700" : "";
    input.style.fontStyle = f.italic ? "italic" : "";
    input.style.textDecoration = f.underline ? "underline" : "";
    input.style.fontFamily = spreadsheetCssFontFamily(
      effectiveCellFontName(f)
    );
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
      } else if (action === "top") {
        border.top = borderStyle;
      } else if (action === "left") {
        border.left = borderStyle;
      } else if (action === "right") {
        border.right = borderStyle;
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
    openBorderMenu(ev.clientX, ev.clientY, row, col);
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
      } else if (kind === "fontName") {
        fmt.fontName = normalizeSpreadsheetFontName(value);
      } else if (kind === "fontSize") {
        fmt.fontSize = normalizeFontSize(value);
      } else if (kind === "align") {
        fmt.align = value === "center" || value === "right" ? value : "left";
      } else if (kind === "valign") {
        fmt.valign = normalizeVerticalAlign(value);
      } else if (kind === "decimals") {
        const current = fmt.decimals == null
          ? inferCellDecimalPlaces(sheet, row, col, decimalCache)
          : fmt.decimals;
        fmt.decimals = normalizeDecimalPlaces(current + decimalDelta);
      } else if (kind === "numberFormat") {
        const rawValue = String(value || "");
        const currencyMatch = rawValue.match(/^currency:([a-z0-9]+)$/i);
        const currency = currencyMatch ? normalizeCurrencyKey(currencyMatch[1]) : "";
        const wasFormattedNumber = fmt.numberFormat === "currency" || fmt.numberFormat === "percent" || fmt.numberFormat === "date";

        if (currency) {
          fmt.numberFormat = "currency";
          fmt.currency = currency;

          // Currency formats default to two decimals when converting from a
          // plain/number cell. When switching from another number format,
          // preserve any decimal count the user already chose.
          if (!wasFormattedNumber || fmt.decimals == null) fmt.decimals = 2;
        } else if (rawValue === "percent") {
          fmt.numberFormat = "percent";
          fmt.currency = "";
        } else if (rawValue === "date") {
          fmt.numberFormat = "date";
          fmt.currency = "";

          // Date display does not use numeric decimal places.
          fmt.decimals = null;
        } else {
          fmt.numberFormat = "";
          fmt.currency = "";

          /*
           * Correctness: General/plain format must reveal
           * the underlying raw value instead of retaining
           * decimal places inherited from currency or
           * percent formatting.
           */
          fmt.decimals = null;
        }
      } else if (kind === "bg") {
        fmt.bg = normalizeFillColorKey(value);
      } else if (kind === "fg") {
        fmt.fg = normalizeTextColorKey(value);
      }

      setCellFormat(sheet, row, col, fmt);
      paintVisibleCellFormat(row, col);

      if (kind === "decimals" || kind === "numberFormat") {
        const refreshFormattedCell = () => syncVisibleInputForCell(row, col, { forceValue: true });

        refreshFormattedCell();

        // Toolbar clicks can interleave with input focus/blur. Refresh once more
        // after the browser has settled focus so number formatting is visible
        // immediately instead of only after another click.
        window.requestAnimationFrame(refreshFormattedCell);
      }
    }

    commitHistorySnapshot(historyBefore);
    updateFormatToolbar();

    // Long cell text is rendered through a separate overflow element.
    // Rebuild it after formatting so font, size and color changes are visible
    // immediately instead of waiting for another cell focus/blur cycle.
    refreshVisibleEditorTextOverflows();
    window.requestAnimationFrame(refreshVisibleEditorTextOverflows);
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

  function workbookFormatFontNames() {
    const names = [];

    for (const sheet of Array.isArray(state.sheets) ? state.sheets : []) {
      const rows = Array.isArray(sheet && sheet.cellFormats)
        ? sheet.cellFormats
        : [];

      for (const row of rows) {
        for (const rawFormat of Array.isArray(row) ? row : []) {
          const name = normalizeCellFormat(rawFormat).fontName;
          if (name) names.push(name);
        }
      }
    }

    return names;
  }

  function refreshFontNameOptions(selectedName = "") {
    if (!fontNameSelect) return;

    const defaultName = workbookDefaultFontName();
    const api = FM && FM.spreadsheetFonts;
    const names =
      api && typeof api.availableFontNames === "function"
        ? api.availableFontNames(
            state.workbookFont,
            workbookFormatFontNames().concat([selectedName])
          )
        : [defaultName, "Arial", "Calibri", "Courier New", "Georgia", "Verdana"];

    fontNameSelect.replaceChildren();

    const defaultOption = document.createElement("option");
    defaultOption.value = "";
    defaultOption.textContent = tr(
      "filemgr.spreadsheet_editor.font_family_default",
      { font: defaultName },
      `Default (${defaultName})`
    );
    defaultOption.style.fontFamily = spreadsheetCssFontFamily(defaultName);
    fontNameSelect.appendChild(defaultOption);

    /*
     * Security: workbook-provided font names are assigned with value and
     * textContent. They are never inserted through innerHTML.
     */
    for (const rawName of names) {
      const name = normalizeSpreadsheetFontName(rawName);
      if (!name) continue;

      const option = document.createElement("option");
      option.value = name;
      option.textContent = name;
      option.style.fontFamily = spreadsheetCssFontFamily(name);
      fontNameSelect.appendChild(option);
    }
  }

  function updateFormatToolbar() {
    const disabled = state.saving || state.readOnly || state.tooLarge;
    const first = firstFormatTargetCell();
    const sheet = state.sheets[state.active];
    const fmt = first && sheet ? getCellFormat(sheet, first.row, first.col) : normalizeCellFormat(null);

    for (const btn of [boldBtn, italicBtn, underlineBtn, decreaseDecimalsBtn, increaseDecimalsBtn, textColorBtn, fillBtn]) {
      if (btn) btn.disabled = disabled;
    }
    if (fontNameSelect) fontNameSelect.disabled = disabled;
    if (fontSizeSelect) fontSizeSelect.disabled = disabled;
    if (numberFormatSelect) numberFormatSelect.disabled = disabled;
    if (alignSelect) alignSelect.disabled = disabled;
    if (valignSelect) valignSelect.disabled = disabled;

    setToolButtonActive(boldBtn, !!fmt.bold);
    setToolButtonActive(italicBtn, !!fmt.italic);
    setToolButtonActive(underlineBtn, !!fmt.underline);

    if (fontNameSelect) {
      refreshFontNameOptions(fmt.fontName);
      fontNameSelect.value = fmt.fontName || "";
    }

    if (fontSizeSelect) {
      fontSizeSelect.value = fmt.fontSize ? String(fmt.fontSize) : "";
    }
    if (numberFormatSelect) {
      numberFormatSelect.value = fmt.numberFormat === "currency" && fmt.currency
        ? `currency:${fmt.currency}`
        : fmt.numberFormat === "percent"
          ? "percent"
          : fmt.numberFormat === "date"
            ? "date"
            : "";
    }
    setToolbarIconButtonValue(alignSelect, "align", fmt.align || "left");
    setToolbarIconButtonValue(valignSelect, "valign", fmt.valign || "middle");
    setToolButtonActive(decreaseDecimalsBtn, false);
    setToolButtonActive(increaseDecimalsBtn, false);
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
    if (f.bold || f.italic || f.underline || f.fontName || f.fontSize || (f.fg && TEXT_COLOR_COLORS[f.fg])) {
      style.font = {};
      if (f.bold) style.font.bold = true;
      if (f.italic) style.font.italic = true;
      if (f.underline) style.font.underline = true;
      if (f.fontName) style.font.name = f.fontName;
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

  function roundedXlsxDimension(value) {
    const n = Number(value);
    return Number.isFinite(n)
      ? Math.round(n * 100000000) / 100000000
      : 0;
  }

  function xlsxColumnPixelWidthToExcelWidth(px) {
    const widthPx = normalizeColumnGeometry(px, DEFAULT_COL_WIDTH);
    const api = FM && FM.spreadsheetXlsxDimensions;
    const converted = api && typeof api.cssPixelsToExcelColumnWidth === "function"
      ? api.cssPixelsToExcelColumnWidth(widthPx)
      : (widthPx <= 12 ? 1 : (widthPx - 5) / 7);

    return Math.max(1, roundedXlsxDimension(converted));
  }

  function xlsxRowPixelHeightToPointHeight(px) {
    const heightPx = normalizeRowGeometry(px, DEFAULT_ROW_HEIGHT);
    const api = FM && FM.spreadsheetXlsxDimensions;
    const converted = api && typeof api.cssPixelsToPoints === "function"
      ? api.cssPixelsToPoints(heightPx)
      : heightPx * 0.75;

    return Math.max(1, roundedXlsxDimension(converted));
  }

  function xlsxRowHeightForSheetRow(sheet, rowIndex, colCount) {
    const explicitHeights = ensureSheetRowHeights(sheet, Math.max(rowIndex + 1, 0));
    const explicitPx = explicitHeights[rowIndex];
    const defaultPx = sheetDefaultRowHeight(sheet);

    if (!sameSheetDimension(explicitPx, defaultPx)) {
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
      fontName: f.fontName || "",
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

  function xlsxCurrencyNumFmtCode(currency, decimals) {
    const key = normalizeCurrencyKey(currency);
    if (!key) return "";

    const base = xlsxDecimalNumFmtCode(decimals == null ? 2 : decimals);
    if (!base) return "";

    if (key === "eur") return `${base} "€"`;
    if (key === "usd") return `"$"${base}`;
    if (key === "gbp") return `"£"${base}`;
    if (key === "sek") return `${base} "kr"`;
    if (key === "cny") return `"¥"${base}`;
    return "";
  }

  function xlsxPercentNumFmtCode(decimals) {
    const base = xlsxDecimalNumFmtCode(decimals == null ? 2 : decimals);
    return base ? `${base}%` : "";
  }

  function xlsxStyleNumberFormatKey(fmt) {
    const f = normalizeCellFormat(fmt);
    if (f.numberFormat === "percent") {
      return xlsxPercentNumFmtCode(f.decimals);
    }
    if (f.numberFormat === "currency" && f.currency) {
      return xlsxCurrencyNumFmtCode(f.currency, f.decimals);
    }
    return xlsxDecimalNumFmtCode(f.decimals);
  }

  function normalizedWorkbookFont(font = null) {
    const api = FM && FM.spreadsheetFonts;

    if (api && typeof api.normalizeFontDescriptor === "function") {
      return api.normalizeFontDescriptor(font || state.workbookFont);
    }

    const source = font && typeof font === "object"
      ? font
      : (state.workbookFont && typeof state.workbookFont === "object"
          ? state.workbookFont
          : {});

    return {
      name: String(source.name || "Calibri"),
      size: Number(source.size) > 0 ? Number(source.size) : 11,
      family: String(source.family || "2"),
      scheme: String(source.scheme || "minor")
    };
  }

  function xlsxFontXml(font) {
    const normalized = normalizedWorkbookFont(font);
    const parts = [
      "<font>",
      `<sz val="${xlsxAttrEscape(normalized.size)}"/>`,
      '<color theme="1"/>',
      `<name val="${xlsxAttrEscape(normalized.name)}"/>`
    ];

    if (normalized.family) {
      parts.push(`<family val="${xlsxAttrEscape(normalized.family)}"/>`);
    }

    if (normalized.scheme) {
      parts.push(`<scheme val="${xlsxAttrEscape(normalized.scheme)}"/>`);
    }

    parts.push("</font>");
    return parts.join("");
  }

  function buildXlsxStyleCatalog(workbookFont = null) {
    const baseFont = normalizedWorkbookFont(workbookFont);
    const fonts = [{
      xml: xlsxFontXml(baseFont)
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
      const defaultKey = JSON.stringify({ bold: false, italic: false, underline: false, fontName: "", fontSize: 0, fg: "" });

      if (key === defaultKey) return 0;
      if (fontIds.has(key)) return fontIds.get(key);

      const parts = ["<font>"];
      if (f.bold) parts.push("<b/>");
      if (f.italic) parts.push("<i/>");
      if (f.underline) parts.push("<u/>");
      parts.push(`<sz val="${f.fontSize || baseFont.size}"/>`);

      if (f.fg && TEXT_COLOR_COLORS[f.fg]) {
        parts.push(`<color rgb="${xlsxAttrEscape(TEXT_COLOR_COLORS[f.fg].rgb)}"/>`);
      } else {
        parts.push('<color theme="1"/>');
      }

      const fontsApi = FM && FM.spreadsheetFonts;
      const selectedFont =
        fontsApi && typeof fontsApi.fontDescriptorForName === "function"
          ? fontsApi.fontDescriptorForName(f.fontName, baseFont)
          : {
              ...baseFont,
              name: f.fontName || baseFont.name,
              scheme: f.fontName ? "" : baseFont.scheme
            };

      parts.push(`<name val="${xlsxAttrEscape(selectedFont.name)}"/>`);

      if (selectedFont.family) {
        parts.push(`<family val="${xlsxAttrEscape(selectedFont.family)}"/>`);
      }

      if (selectedFont.scheme) {
        parts.push(`<scheme val="${xlsxAttrEscape(selectedFont.scheme)}"/>`);
      }

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


  function xlsxMergeCellsXml(sheet, rowCount, colCount) {
    const merges = ensureSheetMerges(sheet, rowCount, colCount);
    if (!merges.length) return "";

    const refs = merges.map((merge) => {
      const ref = `${xlsxCellRef(merge.s.r, merge.s.c)}:${xlsxCellRef(merge.e.r, merge.e.c)}`;
      // Merge refs are generated from normalized numeric coordinates, not user
      // strings; still XML-escape before serializing into worksheet XML.
      return `<mergeCell ref="${xlsxAttrEscape(ref)}"/>`;
    });

    return `<mergeCells count="${refs.length}">${refs.join("")}</mergeCells>`;
  }


  function clampSheetFreezeCount(value, limit) {
    const n = Number(value);
    if (!Number.isFinite(n)) return 0;

    const max = Math.max(0, Number(limit) || 0);
    return Math.max(0, Math.min(max, Math.floor(n)));
  }

  function normalizeSheetFreeze(src, rowCount = MAX_EDIT_ROWS, colCount = MAX_EDIT_COLS) {
    const topRows = clampSheetFreezeCount(src && src.topRows, rowCount);
    const leftCols = clampSheetFreezeCount(src && src.leftCols, colCount);

    return { topRows, leftCols };
  }

  function hasSheetFreeze(freeze) {
    const f = normalizeSheetFreeze(freeze);
    return f.topRows > 0 || f.leftCols > 0;
  }

  function xlsxWorksheetTopLeftCell(topRows, leftCols) {
    return xlsxCellRef(Math.max(0, topRows), Math.max(0, leftCols));
  }

  function xlsxSheetActivePane(freeze) {
    const f = normalizeSheetFreeze(freeze);

    if (f.topRows > 0 && f.leftCols > 0) return "bottomRight";
    if (f.topRows > 0) return "bottomLeft";
    if (f.leftCols > 0) return "topRight";
    return "";
  }

  function xlsxSheetViewsXml(sheet, rowCount, colCount) {
    const freeze = normalizeSheetFreeze(sheet && sheet.freeze, rowCount, colCount);

    if (!hasSheetFreeze(freeze)) {
      return '<sheetViews><sheetView workbookViewId="0"/></sheetViews>';
    }

    const paneAttrs = [];
    if (freeze.leftCols > 0) paneAttrs.push(`xSplit="${freeze.leftCols}"`);
    if (freeze.topRows > 0) paneAttrs.push(`ySplit="${freeze.topRows}"`);

    paneAttrs.push(`topLeftCell="${xlsxAttrEscape(xlsxWorksheetTopLeftCell(freeze.topRows, freeze.leftCols))}"`);

    const activePane = xlsxSheetActivePane(freeze);
    if (activePane) paneAttrs.push(`activePane="${xlsxAttrEscape(activePane)}"`);

    paneAttrs.push('state="frozen"');

    return `<sheetViews><sheetView workbookViewId="0"><pane ${paneAttrs.join(" ")}/></sheetView></sheetViews>`;
  }

  function xlsxXmlAttrMap(tag) {
    const out = {};
    const src = String(tag || "");
    const attrRe = /([A-Za-z_][A-Za-z0-9_.:-]*)\s*=\s*"([^"]*)"/g;
    let m = null;

    while ((m = attrRe.exec(src))) {
      out[m[1]] = m[2];
    }

    return out;
  }

  function worksheetFreezeFromPaneAttrs(attrs) {
    const stateValue = String(attrs && attrs.state || "").toLowerCase();

    // Only treat Excel frozen panes as persisted freeze metadata. Plain split
    // panes use the same xSplit/ySplit attributes with different units.
    if (!stateValue.startsWith("frozen")) return { topRows: 0, leftCols: 0 };

    return normalizeSheetFreeze({
      topRows: attrs.ySplit,
      leftCols: attrs.xSplit
    });
  }

  function worksheetFreezeFromXml(xml) {
    const src = String(xml || "");
    const m = src.match(/<pane\b[^>]*>/i);

    if (!m) return { topRows: 0, leftCols: 0 };

    return worksheetFreezeFromPaneAttrs(xlsxXmlAttrMap(m[0]));
  }

  function sheetJsFileBytes(file) {
    if (!file) return null;
    if (file instanceof Uint8Array) return file;
    if (file instanceof ArrayBuffer) return new Uint8Array(file);

    for (const key of ["content", "data", "_data"]) {
      const value = file[key];
      if (value instanceof Uint8Array) return value;
      if (value instanceof ArrayBuffer) return new Uint8Array(value);
      if (Array.isArray(value)) return new Uint8Array(value);
    }

    if (typeof file.asUint8Array === "function") {
      const value = file.asUint8Array();
      if (value instanceof Uint8Array) return value;
    }

    return null;
  }

  function sheetJsFileText(file) {
    if (!file) return "";
    if (typeof file === "string") return file;

    const bytes = sheetJsFileBytes(file);
    if (!bytes) return "";

    try {
      return new TextDecoder("utf-8").decode(bytes);
    } catch (_) {
      return "";
    }
  }

  function workbookFileByName(wb, name) {
    const wanted = String(name || "").replace(/^\/+/, "");

    if (!wanted || !wb || !wb.files) return null;

    const files = wb.files;

    if (files[wanted]) return files[wanted];
    if (files["/" + wanted]) return files["/" + wanted];

    if (Array.isArray(files.FileIndex)) {
      for (const file of files.FileIndex) {
        const rawName = String(file && file.name || "").replace(/^\/+/, "");
        if (rawName === wanted) return file;
      }
    }

    return null;
  }

  function worksheetXmlTextFromWorkbook(wb, sheetIndex) {
    const sheetNo = Math.max(1, Number(sheetIndex) + 1 || 1);
    const path = `xl/worksheets/sheet${sheetNo}.xml`;

    return sheetJsFileText(workbookFileByName(wb, path));
  }

  function worksheetFreezeFromSheetJs(ws) {
    const views = Array.isArray(ws && ws["!views"]) ? ws["!views"] : [];

    for (const view of views) {
      const pane = view && (view.pane || view);
      const freeze = worksheetFreezeFromPaneAttrs({
        state: pane && pane.state,
        xSplit: pane && pane.xSplit,
        ySplit: pane && pane.ySplit
      });

      if (hasSheetFreeze(freeze)) return freeze;
    }

    const rawFreeze = ws && ws["!freeze"];
    if (rawFreeze) {
      return normalizeSheetFreeze({
        topRows: rawFreeze.ySplit ?? rawFreeze.topRows,
        leftCols: rawFreeze.xSplit ?? rawFreeze.leftCols
      });
    }

    return { topRows: 0, leftCols: 0 };
  }

  function worksheetFreezeFromWorkbook(_XLSX, wb, ws, sheetIndex) {
    const fromSheetJs = worksheetFreezeFromSheetJs(ws);
    if (hasSheetFreeze(fromSheetJs)) return fromSheetJs;

    return worksheetFreezeFromXml(worksheetXmlTextFromWorkbook(wb, sheetIndex));
  }

  function buildWorksheetXml(sheet, styleCatalog, drawingRelId = "") {
    const rows = Array.isArray(sheet.rows) ? sheet.rows : [];
    const rowCount = rows.length;
    const colCount = rows.reduce((m, row) => Math.max(m, Array.isArray(row) ? row.length : 0), 0);
    const colWidths = ensureSheetColWidths(sheet, colCount);
    const rowHeights = ensureSheetRowHeights(sheet, rowCount);
    const mergeCellsXml = xlsxMergeCellsXml(sheet, rowCount, colCount);
    const drawingXml = drawingRelId ? `<drawing r:id="${xlsxAttrEscape(drawingRelId)}"/>` : "";
    const cache = computeSheetCache(sheet);

    const defaultColWidthPx = sheetDefaultColumnWidth(sheet);
    const defaultRowHeightPx = sheetDefaultRowHeight(sheet);
    const defaultColWidth = xlsxColumnPixelWidthToExcelWidth(defaultColWidthPx);
    const defaultRowHeight = xlsxRowPixelHeightToPointHeight(defaultRowHeightPx);

    const customCols = colWidths
      .map((width, index) => ({ width, index }))
      .filter((item) => !sameSheetDimension(item.width, defaultColWidthPx));

    const colsXml = customCols.length
      ? `<cols>${customCols.map((item) => {
          const width = xlsxColumnPixelWidthToExcelWidth(item.width);
          return `<col min="${item.index + 1}" max="${item.index + 1}" width="${width}" customWidth="1"/>`;
        }).join("")}</cols>`
      : "";

    const rowXml = [];

    for (let r = 0; r < rowCount; r++) {
      const row = Array.isArray(rows[r]) ? rows[r] : [];
      const cells = [];

      for (let c = 0; c < colCount; c++) {
        if (isMergeCoveredCell(sheet, r, c)) continue;

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

        if (isForcedTextValue(raw)) {
          const text = forcedTextDisplayValue(raw);
          if (text === "" && !styleIndex) continue;
          cells.push(`<c r="${ref}"${styleAttr} t="inlineStr">${xlsxInlineStringXml(text)}</c>`);
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
      const explicitCustomHeight = rowHeight > 0;

      if (cells.length || explicitCustomHeight) {
        const rowAttrs = explicitCustomHeight
          ? ` r="${r + 1}" ht="${rowHeight}" customHeight="1"`
          : ` r="${r + 1}"`;
        rowXml.push(`<row${rowAttrs}>${cells.join("")}</row>`);
      }
    }

    return [
      '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>',
      '<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">',
      `<dimension ref="${xlsxDimensionRef(rowCount || 1, colCount || 1)}"/>`,
      xlsxSheetViewsXml(sheet, rowCount || 1, colCount || 1),
      `<sheetFormatPr defaultColWidth="${defaultColWidth}" defaultRowHeight="${defaultRowHeight}"/>`,
      colsXml,
      `<sheetData>${rowXml.join("")}</sheetData>`,
      mergeCellsXml,
      drawingXml,
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

  function buildContentTypesXml(sheetCount, imageExport = null) {
    const sheetOverrides = Array.from({ length: sheetCount }, (_v, i) =>
      `<Override PartName="/xl/worksheets/sheet${i + 1}.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>`
    ).join("");

    const imageApi = FM && FM.spreadsheetXlsxImages;
    const imageDefaults = imageApi && typeof imageApi.contentTypeDefaultsXml === "function"
      ? imageApi.contentTypeDefaultsXml(imageExport)
      : "";
    const imageOverrides = imageApi && typeof imageApi.contentTypeOverridesXml === "function"
      ? imageApi.contentTypeOverridesXml(imageExport)
      : "";

    return [
      '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>',
      '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">',
      '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>',
      '<Default Extension="xml" ContentType="application/xml"/>',
      imageDefaults,
      '<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>',
      sheetOverrides,
      imageOverrides,
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
      const raw = entry && entry.raw && entry.raw.compressedData instanceof Uint8Array
        ? entry.raw
        : null;
      const dataBytes = raw ? raw.compressedData : zipEntryBytes(entry.data);
      const method = raw && (raw.method === 0 || raw.method === 8) ? raw.method : 0;
      const compressedSize = raw ? dataBytes.length : dataBytes.length;
      const uncompressedSize = raw && Number.isInteger(raw.uncompressedSize) ? raw.uncompressedSize : dataBytes.length;
      const crc = raw && Number.isInteger(raw.crc) ? (raw.crc >>> 0) : zipCrc32(dataBytes);

      return {
        name: entry.name,
        nameBytes,
        dataBytes,
        method,
        compressedSize,
        uncompressedSize,
        crc
      };
    });

    const { dosTime, dosDate } = zipDosTimeDate();
    let localSize = 0;
    let centralSize = 0;

    for (const entry of prepared) {
      localSize += 30 + entry.nameBytes.length + entry.compressedSize;
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
      writeU16(out, offset, entry.method); offset += 2;
      writeU16(out, offset, dosTime); offset += 2;
      writeU16(out, offset, dosDate); offset += 2;
      writeU32(out, offset, entry.crc); offset += 4;
      writeU32(out, offset, entry.compressedSize); offset += 4;
      writeU32(out, offset, entry.uncompressedSize); offset += 4;
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
      writeU16(out, offset, entry.method); offset += 2;
      writeU16(out, offset, dosTime); offset += 2;
      writeU16(out, offset, dosDate); offset += 2;
      writeU32(out, offset, entry.crc); offset += 4;
      writeU32(out, offset, entry.compressedSize); offset += 4;
      writeU32(out, offset, entry.uncompressedSize); offset += 4;
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
    const styleCatalog = buildXlsxStyleCatalog(state.workbookFont);
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
    const imageApi = FM && FM.spreadsheetXlsxImages;
    const imageExport = imageApi && typeof imageApi.prepareExport === "function"
      ? imageApi.prepareExport(
          state.workbookImageInfo,
          visibleSheets.map((item) => item.sheet)
        )
      : null;
    const entries = [];

    for (let i = 0; i < visibleSheets.length; i++) {
      const item = visibleSheets[i];
      const drawingRelId = imageApi && typeof imageApi.worksheetDrawingRelId === "function"
        ? imageApi.worksheetDrawingRelId(imageExport, i)
        : "";

      entries.push({
        name: `xl/worksheets/sheet${i + 1}.xml`,
        data: buildWorksheetXml(item.sheet, styleCatalog, drawingRelId)
      });
    }

    entries.push({
      name: `xl/worksheets/sheet${visibleSheets.length + 1}.xml`,
      data: buildStyleMetadataWorksheetXml(stylePayload)
    });

    entries.push({ name: "[Content_Types].xml", data: buildContentTypesXml(allSheetNames.length, imageExport) });
    entries.push({ name: "_rels/.rels", data: buildRootRelsXml() });
    entries.push({ name: "docProps/core.xml", data: buildCorePropsXml() });
    entries.push({ name: "docProps/app.xml", data: buildAppPropsXml(allSheetNames) });
    entries.push({ name: "xl/workbook.xml", data: buildWorkbookXml(allSheetNames) });
    entries.push({ name: "xl/_rels/workbook.xml.rels", data: buildWorkbookRelsXml(allSheetNames.length) });
    entries.push({ name: "xl/styles.xml", data: styleCatalog.stylesXml() });

    if (imageApi && typeof imageApi.appendExportEntries === "function") {
      imageApi.appendExportEntries(entries, imageExport);
    }

    return buildZipStore(entries);
  }

  function clampColumnWidth(width) {
    const n = Number(width);
    if (!Number.isFinite(n)) return DEFAULT_COL_WIDTH;

    // Interactive resizing keeps the existing usability limits.
    return Math.max(MIN_COL_WIDTH, Math.min(MAX_COL_WIDTH, Math.round(n)));
  }

  function clampRowHeight(height) {
    const n = Number(height);
    if (!Number.isFinite(n)) return DEFAULT_ROW_HEIGHT;

    // Interactive resizing keeps the existing usability limits.
    return Math.max(MIN_ROW_HEIGHT, Math.min(MAX_ROW_HEIGHT, Math.round(n)));
  }

  function normalizeColumnGeometry(width, fallback = DEFAULT_COL_WIDTH) {
    const n = Number(width);
    const fallbackValue = Number(fallback);
    const value = Number.isFinite(n) && n > 0
      ? n
      : (Number.isFinite(fallbackValue) && fallbackValue > 0
          ? fallbackValue
          : DEFAULT_COL_WIDTH);

    return Math.max(1, Math.min(MAX_COL_WIDTH, value));
  }

  function normalizeRowGeometry(height, fallback = DEFAULT_ROW_HEIGHT) {
    const n = Number(height);
    const fallbackValue = Number(fallback);
    const value = Number.isFinite(n) && n > 0
      ? n
      : (Number.isFinite(fallbackValue) && fallbackValue > 0
          ? fallbackValue
          : DEFAULT_ROW_HEIGHT);

    return Math.max(1, Math.min(MAX_ROW_HEIGHT, value));
  }

  function sheetDefaultColumnWidth(sheet) {
    return normalizeColumnGeometry(
      sheet && sheet.defaultColWidth,
      DEFAULT_COL_WIDTH
    );
  }

  function sheetDefaultRowHeight(sheet) {
    return normalizeRowGeometry(
      sheet && sheet.defaultRowHeight,
      DEFAULT_ROW_HEIGHT
    );
  }

  function sameSheetDimension(a, b) {
    const api = FM && FM.spreadsheetXlsxDimensions;

    if (api && typeof api.sameDimension === "function") {
      return api.sameDimension(a, b);
    }

    const left = Number(a);
    const right = Number(b);

    return Number.isFinite(left) &&
      Number.isFinite(right) &&
      Math.abs(left - right) <= 0.01;
  }

  function xlsxColumnToPixelWidth(col, defaultWidth) {
    const fallback = normalizeColumnGeometry(defaultWidth, DEFAULT_COL_WIDTH);
    const api = FM && FM.spreadsheetXlsxDimensions;

    if (api && typeof api.columnToCssPixels === "function") {
      return normalizeColumnGeometry(
        api.columnToCssPixels(col, fallback),
        fallback
      );
    }

    if (!col || typeof col !== "object") return fallback;
    if (Number.isFinite(col.wpx)) {
      return normalizeColumnGeometry(col.wpx, fallback);
    }
    if (Number.isFinite(col.width)) {
      return normalizeColumnGeometry((col.width * 7) + 5, fallback);
    }
    if (Number.isFinite(col.wch)) {
      return normalizeColumnGeometry((col.wch * 7) + 5, fallback);
    }

    return fallback;
  }

  function xlsxRowToPixelHeight(row, defaultHeight) {
    const fallback = normalizeRowGeometry(defaultHeight, DEFAULT_ROW_HEIGHT);
    const api = FM && FM.spreadsheetXlsxDimensions;

    if (api && typeof api.rowToCssPixels === "function") {
      return normalizeRowGeometry(
        api.rowToCssPixels(row, fallback),
        fallback
      );
    }

    if (!row || typeof row !== "object") return fallback;

    // SheetJS may expose hpx with the point value. Prefer hpt/ht.
    if (Number.isFinite(row.hpt)) {
      return normalizeRowGeometry(row.hpt / 0.75, fallback);
    }
    if (Number.isFinite(row.ht)) {
      return normalizeRowGeometry(row.ht / 0.75, fallback);
    }
    if (Number.isFinite(row.hpx)) {
      return normalizeRowGeometry(row.hpx, fallback);
    }

    return fallback;
  }

  function ensureSheetColWidths(sheet, colCount) {
    if (!sheet) return [];
    const count = Math.max(0, Number.isInteger(colCount) ? colCount : 0);

    if (!Array.isArray(sheet.colWidths)) {
      sheet.colWidths = [];
    }

    const defaultWidth = sheetDefaultColumnWidth(sheet);

    while (sheet.colWidths.length < count) {
      sheet.colWidths.push(defaultWidth);
    }

    if (sheet.colWidths.length > count) {
      sheet.colWidths.length = count;
    }

    for (let i = 0; i < sheet.colWidths.length; i++) {
      sheet.colWidths[i] = normalizeColumnGeometry(
        sheet.colWidths[i],
        defaultWidth
      );
    }

    return sheet.colWidths;
  }

  function sheetColumnWidth(sheet, col) {
    const widths = ensureSheetColWidths(sheet, col + 1);
    return normalizeColumnGeometry(
      widths[col],
      sheetDefaultColumnWidth(sheet)
    );
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

    const px = `${normalizeColumnGeometry(width, DEFAULT_COL_WIDTH)}px`;
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

    const defaultHeight = sheetDefaultRowHeight(sheet);

    while (sheet.rowHeights.length < count) {
      sheet.rowHeights.push(defaultHeight);
    }

    if (sheet.rowHeights.length > count) {
      sheet.rowHeights.length = count;
    }

    for (let i = 0; i < sheet.rowHeights.length; i++) {
      sheet.rowHeights[i] = normalizeRowGeometry(
        sheet.rowHeights[i],
        defaultHeight
      );
    }

    return sheet.rowHeights;
  }

  function sheetRowHeight(sheet, row) {
    const heights = ensureSheetRowHeights(sheet, row + 1);
    return normalizeRowGeometry(
      heights[row],
      sheetDefaultRowHeight(sheet)
    );
  }

  function applyRowHeight(el, height) {
    if (!el) return;

    const requestedHeight = normalizeRowGeometry(
      height,
      DEFAULT_ROW_HEIGHT
    );

    const tagName = String(
      el.tagName || ""
    ).toUpperCase();

    /*
     * XLSX row height represents the complete visible row. With separate
     * table borders, TH/TD height acts as a content minimum and the 1 px
     * bottom border otherwise makes every rendered row one pixel too tall.
     *
     * Inputs receive the same compensation so their fixed height cannot
     * force the table row back to requestedHeight + 1.
     */
    const renderedHeight = tagName === "TR"
      ? requestedHeight
      : Math.max(1, requestedHeight - 1);

    const px = `${renderedHeight}px`;

    el.style.height = px;
    el.style.minHeight = px;
    el.style.maxHeight = px;
  }

  function isEditorPlainTextOverflowCandidate(value, fmt) {
    const text = String(value == null ? "" : value);
    if (!text || /[\r\n]/.test(text)) return false;

    const f = normalizeCellFormat(fmt);
    if (f.align === "center" || f.align === "right") return false;

    // Match spreadsheet behavior for plain text. Numeric-looking values should
    // stay inside their own cell instead of spilling across the grid.
    return !/^[+-]?(?:\d+|\d*[.,]\d+)(?:[%€$])?$/.test(text.trim());
  }

  function isEditorOverflowEmptyCell(sheet, rows, row, col) {
    if (!Array.isArray(rows) || row < 0 || row >= rows.length || col < 0) return false;
    if (mergeAtCell(sheet, row, col)) return false;

    const sourceRow = Array.isArray(rows[row]) ? rows[row] : [];
    return String(sourceRow[col] == null ? "" : sourceRow[col]) === "";
  }

  function editorTextOverflowWidth(sheet, rows, row, col, colWidths, colCount) {
    let width = clampColumnWidth(colWidths[col]);

    for (let c = col + 1; c < colCount; c++) {
      if (!isEditorOverflowEmptyCell(sheet, rows, row, c)) break;
      width += clampColumnWidth(colWidths[c]);
    }

    return width;
  }

  function clearEditorTextOverflow(td, input) {
    if (td) {
      td.removeAttribute("data-spreadsheet-text-overflow");
      td.style.removeProperty("--spreadsheet-text-overflow-width");
      for (const el of td.querySelectorAll(".spreadsheetEditorTextOverflow")) {
        el.remove();
      }
    }

    if (input) {
      input.removeAttribute("data-spreadsheet-text-overflow-input");
    }
  }

  function applyEditorOverflowTextStyle(span, fmt) {
    const f = normalizeCellFormat(fmt);

    span.style.fontWeight = f.bold ? "700" : "";
    span.style.fontStyle = f.italic ? "italic" : "";
    span.style.textDecoration = f.underline ? "underline" : "";
    span.style.fontFamily = spreadsheetCssFontFamily(
      effectiveCellFontName(f)
    );
    span.style.fontSize = f.fontSize ? `${f.fontSize}px` : "";

    if (f.fg && TEXT_COLOR_COLORS[f.fg]) {
      span.style.color = TEXT_COLOR_COLORS[f.fg].css;
    }
  }

  function renderEditorCellTextOverflow(td, input, sheet, rows, row, col, colWidths, colCount, cache = null) {
    clearEditorTextOverflow(td, input);

    if (!td || !input || !sheet || mergeAtCell(sheet, row, col)) return;

    const fmt = getCellFormat(sheet, row, col);
    const text = displayCellValue(sheet, row, col, cache || computeSheetCache(sheet));

    if (!isEditorPlainTextOverflowCandidate(text, fmt)) return;

    const overflowWidth = editorTextOverflowWidth(sheet, rows, row, col, colWidths, colCount);
    const ownWidth = clampColumnWidth(colWidths[col]);

    if (overflowWidth <= ownWidth) return;

    td.dataset.spreadsheetTextOverflow = "1";
    td.style.setProperty("--spreadsheet-text-overflow-width", `${overflowWidth}px`);
    input.dataset.spreadsheetTextOverflowInput = "1";

    const span = document.createElement("span");
    span.className = "spreadsheetEditorTextOverflow";
    span.textContent = text;
    applyEditorOverflowTextStyle(span, fmt);
    td.appendChild(span);
  }

  function refreshVisibleEditorTextOverflows() {
    if (!bodyEl) return;

    const sheet = state.sheets[state.active];
    const rows = sheet && Array.isArray(sheet.rows) ? sheet.rows : [];
    const colCount = rows.reduce((m, row) => Math.max(m, Array.isArray(row) ? row.length : 0), 0);
    const colWidths = ensureSheetColWidths(sheet, colCount);
    const cache = sheet ? computeSheetCache(sheet) : null;

    const table = bodyEl.querySelector(".spreadsheetEditorTable");
    if (!table || !sheet) return;

    for (const td of table.querySelectorAll("td[data-row][data-col]")) {
      const row = Number(td.dataset.row);
      const col = Number(td.dataset.col);
      const input = td.querySelector("input[data-row][data-col]");

      if (!Number.isInteger(row) || !Number.isInteger(col) || !input) {
        clearEditorTextOverflow(td, input);
        continue;
      }

      renderEditorCellTextOverflow(td, input, sheet, rows, row, col, colWidths, colCount, cache);
    }
  }

  function paintVisibleRowHeight(row, height) {
    if (!bodyEl || !Number.isInteger(row) || row < 0) return;

    const table = bodyEl.querySelector(".spreadsheetEditorTable");
    if (!table) return;

    const pxHeight = clampRowHeight(height);

    const rowElement =
      table.tBodies &&
      table.tBodies[0] &&
      table.tBodies[0].rows
        ? table.tBodies[0].rows[row]
        : null;

    applyRowHeight(rowElement, pxHeight);

    const header = table.querySelector(
      `th[data-row="${row}"]`
    );

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


  function isForcedTextValue(value) {
    return String(value == null ? "" : value).startsWith("'");
  }

  function forcedTextDisplayValue(value) {
    const raw = String(value == null ? "" : value);
    return isForcedTextValue(raw) ? raw.slice(1) : raw;
  }

  function normalizeSpreadsheetUserInput(value, previousRaw = "") {
    const text = String(value == null ? "" : value);

    if (!text) return "";
    if (text.startsWith("'")) return text;

    // Excel-style text prefix: once a cell was explicitly forced to text,
    // direct edits keep it as text so "=..." does not silently become a formula.
    if (isForcedTextValue(previousRaw)) return "'" + text;

    return text;
  }

  function isFormulaValue(value) {
    const raw = String(value == null ? "" : value);
    return raw.startsWith("=") && !isForcedTextValue(raw);
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
    state.selectedImageId = "";
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
      const currentRaw = cellRaw(cell.sheet, cell.row, cell.col);
      return normalizeSpreadsheetUserInput(formulaBarInput.value, currentRaw) !== currentRaw;
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


  function selectedSpreadsheetImage() {
    const sheet =
      state.sheets[
        state.active
      ];

    const selectedId =
      String(
        state.selectedImageId ||
        ""
      );

    const images =
      Array.isArray(
        sheet &&
        sheet.images
      )
        ? sheet.images
        : [];

    if (
      !sheet ||
      !selectedId ||
      !images.length
    ) {
      return null;
    }

    const index =
      images.findIndex(
        (image) =>
          String(
            image &&
            image.id ||
            ""
          ) === selectedId
      );

    if (index < 0) {
      return null;
    }

    return {
      sheet,
      images,
      index,
      image: images[index]
    };
  }

  function deleteSelectedSpreadsheetImage() {
    if (
      state.readOnly ||
      state.tooLarge ||
      state.saving
    ) {
      return false;
    }

    const selected =
      selectedSpreadsheetImage();

    if (!selected) {
      return false;
    }

    const {
      sheet,
      images,
      index,
      image
    } = selected;

    const imported =
      image &&
      image.source !== "inserted";

    if (imported) {
      const drawingPath =
        String(
          image.drawingPath || ""
        );

      const relationshipId =
        String(
          image.relationshipId || ""
        );

      const anchorIndex =
        Number(
          image.anchorIndex
        );

      /*
       * Security: never remove an imported picture unless its exact drawing
       * anchor identity was captured during XLSX import.
       */
      if (
        !drawingPath ||
        !relationshipId ||
        !Number.isInteger(
          anchorIndex
        ) ||
        anchorIndex < 0
      ) {
        setStatus(
          tr(
            "filemgr.spreadsheet_editor.image_delete_failed",
            null,
            "The image could not be identified safely for deletion."
          ),
          "warn"
        );

        return false;
      }
    }

    const historyBefore =
      captureHistorySnapshot();

    if (imported) {
      if (
        !Array.isArray(
          sheet.deletedImages
        )
      ) {
        sheet.deletedImages = [];
      }

      const deletedImage = {
        id: String(
          image.id ||
          state.selectedImageId ||
          ""
        ),

        source: "imported",

        drawingPath:
          String(
            image.drawingPath ||
            ""
          ),

        relationshipId:
          String(
            image.relationshipId ||
            ""
          ),

        anchorIndex:
          Number(
            image.anchorIndex
          )
      };

      const alreadyRecorded =
        sheet.deletedImages.some(
          (item) =>
            item &&
            item.drawingPath ===
              deletedImage.drawingPath &&
            item.relationshipId ===
              deletedImage.relationshipId &&
            item.anchorIndex ===
              deletedImage.anchorIndex
        );

      if (!alreadyRecorded) {
        sheet.deletedImages.push(
          deletedImage
        );
      }
    }

    /*
     * Inserted binary data remains in the session asset registry until the
     * editor closes. Undo can therefore restore a deleted inserted image
     * without duplicating or losing its binary payload.
     */
    images.splice(
      index,
      1
    );

    state.selectedImageId = "";
    state.selection = null;
    state.rangeSelection = null;
    state.activeCell = null;
    formulaFocus = null;

    commitHistorySnapshot(
      historyBefore
    );

    render();
    return true;
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
      /*
       * Image selection takes precedence even when an old cell input still
       * owns DOM focus. This also keeps File Manager's global delete shortcut
       * from opening a trash confirmation for the XLSX file itself.
       */
      if (state.selectedImageId) {
        ev.preventDefault();
        ev.stopPropagation();

        deleteSelectedSpreadsheetImage();
        return;
      }

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
      formulaBarInput.value = forcedTextDisplayValue(cellRaw(cell.sheet, cell.row, cell.col));
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
    formulaBarInput.value = forcedTextDisplayValue(raw);

    if (isFormulaValue(raw)) {
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

    paintSpreadsheetFormulaReferences();
  }

  function updateActiveCellFromFormulaBar() {
    if (state.readOnly || state.tooLarge || !formulaBarInput) return false;

    const cell = activeFormulaBarCell();
    if (!cell) return false;

    const previousRaw = cellRaw(cell.sheet, cell.row, cell.col);
    const previousFormulaFocus = formulaFocus && formulaFocus.input === formulaBarInput ? formulaFocus : null;
    const nextRaw = normalizeSpreadsheetUserInput(formulaBarInput.value, previousRaw);
    const historyBefore = previousRaw !== nextRaw ? captureHistorySnapshot() : null;

    setCellRaw(cell.sheet, cell.row, cell.col, nextRaw);

    if (isFormulaValue(nextRaw)) {
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

    formulaBarInput.value = forcedTextDisplayValue(originalRaw);
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
    return forcedTextDisplayValue(value)
      .replace(/\r\n/g, "\n")
      .replace(/[\r\n\t]/g, " ");
  }


  function spreadsheetCellKey(row, col) {
    return `${row}:${col}`;
  }

  function mergeRangeOverlapsRect(merge, row1, col1, row2, col2) {
    const m = normalizeMergeRange(merge);
    if (!m) return false;

    return m.s.r <= row2 &&
      m.e.r >= row1 &&
      m.s.c <= col2 &&
      m.e.c >= col1;
  }

  function rangeIntersectsMergedCells(sheet, row1, col1, row2, col2) {
    if (!sheet) return false;

    const r1 = Math.min(row1, row2);
    const r2 = Math.max(row1, row2);
    const c1 = Math.min(col1, col2);
    const c2 = Math.max(col1, col2);

    return ensureSheetMerges(sheet).some((merge) => mergeRangeOverlapsRect(merge, r1, c1, r2, c2));
  }

  function mergedWriteTargetCell(sheet, row, col) {
    const merge = mergeAtCell(sheet, row, col);
    if (!merge) return { row, col, merge: null };

    return {
      row: merge.s.r,
      col: merge.s.c,
      merge
    };
  }

  function clearMergeRangeValues(sheet, merge) {
    const m = normalizeMergeRange(merge);
    if (!sheet || !m) return false;

    let changed = false;

    for (let r = m.s.r; r <= m.e.r; r++) {
      for (let c = m.s.c; c <= m.e.c; c++) {
        if (cellRaw(sheet, r, c) !== "") changed = true;
        setCellRaw(sheet, r, c, "");
      }
    }

    return changed;
  }

  function clearSpreadsheetCellsWithMergeSafety(sheet, cells) {
    if (!sheet || !Array.isArray(cells) || !cells.length) return false;

    const targets = new Map();

    for (const { row, col } of cells) {
      if (!Number.isInteger(row) || !Number.isInteger(col) || row < 0 || col < 0) continue;

      const merge = mergeAtCell(sheet, row, col);
      if (merge) {
        const m = normalizeMergeRange(merge);
        if (!m) continue;

        for (let r = m.s.r; r <= m.e.r; r++) {
          for (let c = m.s.c; c <= m.e.c; c++) {
            targets.set(spreadsheetCellKey(r, c), { row: r, col: c });
          }
        }
      } else {
        targets.set(spreadsheetCellKey(row, col), { row, col });
      }
    }

    let changed = false;

    // Merge safety: clear every physical cell in an intersected merge so covered
    // cells cannot keep hidden data that later appears after unmerge.
    for (const { row, col } of targets.values()) {
      if (cellRaw(sheet, row, col) !== "") changed = true;
      setCellRaw(sheet, row, col, "");
    }

    return changed;
  }

  function clipboardRowsColumnCount(rows) {
    if (!Array.isArray(rows)) return 0;
    return rows.reduce((max, row) => Math.max(max, Array.isArray(row) ? row.length : 0), 0);
  }

  function isSingleCellClipboardRows(rows) {
    return Array.isArray(rows) &&
      rows.length === 1 &&
      Array.isArray(rows[0]) &&
      rows[0].length <= 1;
  }

  function buildSpreadsheetClipboardText(input) {
    const sheet = state.sheets[state.active];
    const range = spreadsheetTargetRange(input);
    if (!sheet || !range) return null;

    const lines = [];
    for (let row = range.row1; row <= range.row2; row++) {
      const values = [];
      for (let col = range.col1; col <= range.col2; col++) {
        values.push(isMergeCoveredCell(sheet, row, col) ? "" : spreadsheetClipboardCellText(cellRaw(sheet, row, col)));
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

    const pasteRowCount = rows.length;
    const pasteColCount = clipboardRowsColumnCount(rows);
    if (!pasteColCount) return false;

    const singleCellPaste = isSingleCellClipboardRows(rows);

    if (!singleCellPaste && rangeIntersectsMergedCells(
      sheet,
      start.row,
      start.col,
      start.row + pasteRowCount - 1,
      start.col + pasteColCount - 1
    )) {
      setStatus(tr(
        "filemgr.spreadsheet_editor.paste_over_merge_blocked",
        null,
        "Cannot paste multiple cells over merged cells."
      ), "warn");

      // Consume blocked spreadsheet paste events. Otherwise the browser's
      // native input paste writes tab-separated values into the merged anchor.
      return true;
    }

    const historyBefore = captureHistorySnapshot();
    let changed = false;
    let focusRow = start.row;
    let focusCol = start.col;

    if (singleCellPaste) {
      const target = mergedWriteTargetCell(sheet, start.row, start.col);
      const next = String((rows[0] && rows[0][0]) == null ? "" : rows[0][0]);

      if (target.merge) {
        changed = clearMergeRangeValues(sheet, target.merge) || changed;
      }

      if (cellRaw(sheet, target.row, target.col) !== next) changed = true;
      setCellRaw(sheet, target.row, target.col, next);

      focusRow = target.row;
      focusCol = target.col;
    } else {
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
    }

    formulaFocus = null;
    state.selection = null;
    state.rangeSelection = null;
    state.activeCell = { row: focusRow, col: focusCol };

    if (changed) commitHistorySnapshot(historyBefore);
    render();

    window.requestAnimationFrame(() => {
      focusSpreadsheetCell(focusRow, focusCol, { select: true });
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
    const changed = clearSpreadsheetCellsWithMergeSafety(sheet, cells);

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
    const m = String(ref || "").trim().match(/^\$?([A-Z]+)\$?([1-9][0-9]*)$/i);
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

    function readCellRefToken() {
      skipWs();

      const start = pos;

      if (src[pos] === "$") pos++;

      const lettersStart = pos;
      while (/[A-Za-z]/.test(src[pos] || "")) pos++;
      if (pos === lettersStart) {
        pos = start;
        return "";
      }

      if (src[pos] === "$") pos++;

      const digitsStart = pos;
      while (/[0-9]/.test(src[pos] || "")) pos++;
      if (pos === digitsStart) {
        pos = start;
        return "";
      }

      return src.slice(start, pos);
    }

    function parseCellTokenAtCurrent() {
      skipWs();

      const save = pos;
      const token = readCellRefToken();
      if (!token) {
        pos = save;
        return null;
      }

      const ref = parseCellRef(token);
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

      const cellSave = pos;
      const cell = parseCellTokenAtCurrent();
      if (cell) {
        if (match(":")) throw new Error("#VALUE!");
        return cellNumericValue(cell);
      }
      pos = cellSave;

      const save = pos;
      const letters = readLetters();
      if (letters) {
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

    if (isForcedTextValue(raw)) {
      const text = forcedTextDisplayValue(raw);
      const result = { raw: text, value: text, blank: text === "", error: "" };
      cache.set(key, result);
      return result;
    }

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

    if (fmt.numberFormat === "date" && !isFormulaValue(raw) && !isForcedTextValue(raw)) {
      const dateText = formatSpreadsheetDateDisplayValue(raw);
      if (dateText) return dateText;
    }

    if (isForcedTextValue(raw)) {
      return forcedTextDisplayValue(raw);
    }

    if (!isFormulaValue(raw)) {
      const parsed = parsePlainNumber(raw);
      if ((fmt.decimals != null || fmt.numberFormat === "currency" || fmt.numberFormat === "percent" || fmt.numberFormat === "date") && !parsed.blank && typeof parsed.number === "number") {
        return formatNumericDisplayValue(parsed.number, fmt);
      }
      return raw;
    }

    const effectiveCache = cache || computeSheetCache(sheet);
    const result = evaluateCell(sheet, row, col, effectiveCache, new Set());
    if (result.error) return result.error;

    if ((fmt.decimals != null || fmt.numberFormat === "currency" || fmt.numberFormat === "percent" || fmt.numberFormat === "date") && typeof result.value === "number") {
      return formatNumericDisplayValue(result.value, fmt);
    }

    return formatFormulaNumber(result.value);
  }

  function spreadsheetFormulaReferenceApi() {
    return (
      FM &&
      FM.spreadsheetFormulaReferences
    ) || null;
  }

  function paintSpreadsheetFormulaReferences() {
    const api =
      spreadsheetFormulaReferenceApi();

    if (
      !api ||
      typeof api.paint !== "function" ||
      !bodyEl
    ) {
      return;
    }

    const input =
      formulaFocus &&
      formulaFocus.input;

    const formula =
      input &&
      isFormulaValue(input.value)
        ? String(input.value)
        : "";

    api.paint(
      bodyEl,
      formula,
      {
        maxRows: MAX_EDIT_ROWS,
        maxCols: MAX_EDIT_COLS
      }
    );
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

    paintSpreadsheetFormulaReferences();
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



function spreadsheetCellElement(row, col) {
    if (!bodyEl || !Number.isInteger(row) || !Number.isInteger(col)) return null;
    return bodyEl.querySelector(`td[data-row="${row}"][data-col="${col}"]`);
  }

function spreadsheetFillEndRowFromViewportY(startRow, col, viewportY) {
    if (!Number.isInteger(startRow) || !Number.isInteger(col)) return startRow;

    let bestRow = startRow;
    let bestDistance = Number.POSITIVE_INFINITY;

    for (let row = startRow; row < MAX_EDIT_ROWS; row++) {
      const td = spreadsheetCellElement(row, col);
      if (!td) break;

      const rect = td.getBoundingClientRect();
      const middle = rect.top + rect.height / 2;

      if (viewportY >= rect.top && viewportY <= rect.bottom) {
        return row;
      }

      const distance = Math.abs(viewportY - middle);
      if (distance < bestDistance) {
        bestDistance = distance;
        bestRow = row;
      }

      if (rect.top > viewportY && row > startRow) break;
    }

    return bestRow;
  }


  function spreadsheetFillEndColFromViewportX(startCol, row, viewportX) {
    if (!Number.isInteger(startCol) || !Number.isInteger(row)) return startCol;

    let bestCol = startCol;
    let bestDistance = Number.POSITIVE_INFINITY;

    for (let col = startCol; col < MAX_EDIT_COLS; col++) {
      const td = spreadsheetCellElement(row, col);
      if (!td) break;

      const rect = td.getBoundingClientRect();
      const middle = rect.left + rect.width / 2;

      if (viewportX >= rect.left && viewportX <= rect.right) {
        return col;
      }

      const distance = Math.abs(viewportX - middle);
      if (distance < bestDistance) {
        bestDistance = distance;
        bestCol = col;
      }

      if (rect.left > viewportX && col > startCol) break;
    }

    return bestCol;
  }

  function paintFillHandlePreview(source, direction, endRow, endCol) {
    if (!source || !direction) return;

    state.selection = null;
    state.activeCell = { row: source.startRow, col: source.startCol };

    state.rangeSelection = {
      startRow: source.startRow,
      startCol: source.startCol,
      endRow: direction === "down" ? Math.max(source.endRow, endRow) : source.endRow,
      endCol: direction === "right" ? Math.max(source.endCol, endCol) : source.endCol
    };

    repaintSpreadsheetSelection();
  }

  function fillHandleSourceRangeForCell(row, col) {
    if (!Number.isInteger(row) || !Number.isInteger(col)) return null;

    const range = normalizedRangeSelection(state.rangeSelection);
    if (range && row === range.row2 && col === range.col2) {
      const singleColumn = range.col1 === range.col2;
      const singleRow = range.row1 === range.row2;

      if (singleColumn || singleRow) {
        return {
          startRow: range.row1,
          endRow: range.row2,
          startCol: range.col1,
          endCol: range.col2,
          orientation: singleRow && !singleColumn
            ? "right"
            : singleColumn && !singleRow
              ? "down"
              : ""
        };
      }
    }

    const active = state.activeCell;
    if (active && Number(active.row) === row && Number(active.col) === col) {
      return {
        startRow: row,
        endRow: row,
        startCol: col,
        endCol: col,
        orientation: ""
      };
    }

    return null;
  }

  function fillHandleSourceValues(sheet, source, direction) {
    const values = [];
    if (!sheet || !source) return values;

    if (direction === "right") {
      const row = source.startRow;
      for (let col = source.startCol; col <= source.endCol; col++) {
        values.push({
          row,
          col,
          raw: cellRaw(sheet, row, col),
          format: normalizeCellFormat(getCellFormat(sheet, row, col))
        });
      }
      return values;
    }

    const col = source.startCol;
    for (let row = source.startRow; row <= source.endRow; row++) {
      values.push({
        row,
        col,
        raw: cellRaw(sheet, row, col),
        format: normalizeCellFormat(getCellFormat(sheet, row, col))
      });
    }

    return values;
  }

function fillHandleNumericSeries(sourceValues) {
    if (!Array.isArray(sourceValues) || sourceValues.length < 2) return null;

    const parsed = [];

    for (const item of sourceValues) {
      const raw = String(item && item.raw == null ? "" : item.raw);
      if (isFormulaValue(raw) || isForcedTextValue(raw)) return null;

      const number = parsePlainNumber(raw);
      if (number.blank || typeof number.number !== "number" || !Number.isFinite(number.number)) {
        return null;
      }

      parsed.push({ raw, value: number.number });
    }

    const last = parsed[parsed.length - 1].value;
    const previous = parsed[parsed.length - 2].value;
    const step = last - previous;

    if (!Number.isFinite(step)) return null;

    const decimals = parsed.reduce(
      (max, item) => Math.max(max, decimalPlacesFromText(item.raw)),
      0
    );

    return { last, step, decimals };
  }


  function fillHandleDateSeries(sourceValues) {
    if (!Array.isArray(sourceValues) || !sourceValues.length) return null;

    const parsed = [];

    for (const item of sourceValues) {
      const raw = String(item && item.raw == null ? "" : item.raw);
      if (isFormulaValue(raw) || isForcedTextValue(raw)) return null;

      const serial = spreadsheetDateSerialFromText(raw);
      if (serial == null) return null;

      parsed.push(serial);
    }

    const lastSerial = parsed[parsed.length - 1];
    const previousSerial = parsed.length >= 2
      ? parsed[parsed.length - 2]
      : lastSerial - 1;

    const stepDays = lastSerial - previousSerial;

    if (!Number.isFinite(stepDays)) return null;

    return {
      type: "date",
      lastSerial,
      stepDays
    };
  }

  function fillHandleSeriesRawValue(series, offset) {
    if (series && series.type === "date") {
      return formatSpreadsheetDateSerial(series.lastSerial + series.stepDays * offset);
    }

    const next = series.last + series.step * offset;

    if (series.decimals > 0) {
      return formatDecimalNumber(next, series.decimals);
    }

    return String(Math.round(next));
  }

function formulaRefBoundaryBefore(ch) {
    return !ch || !/[A-Za-z0-9_$]/.test(ch);
  }

  function formulaRefBoundaryAfter(ch) {
    return !ch || !/[A-Za-z0-9_]/.test(ch);
  }

  function shiftSpreadsheetFormulaRefs(raw, rowOffset, colOffset) {
    const formula = String(raw == null ? "" : raw);
    const rowDelta = Number(rowOffset);
    const colDelta = Number(colOffset);

    if (
      !isFormulaValue(formula) ||
      (!Number.isFinite(rowDelta) && !Number.isFinite(colDelta)) ||
      (rowDelta === 0 && colDelta === 0)
    ) {
      return formula;
    }

    let out = "";
    let i = 0;
    let inString = false;

    while (i < formula.length) {
      const ch = formula[i];

      // Excel formula string literals use double quotes. Keep references inside
      // strings untouched so values like ="A1" do not become ="B1" on fill.
      if (ch === '"') {
        out += ch;

        if (inString && formula[i + 1] === '"') {
          out += formula[i + 1];
          i += 2;
          continue;
        }

        inString = !inString;
        i += 1;
        continue;
      }

      if (inString) {
        out += ch;
        i += 1;
        continue;
      }

      const before = i > 0 ? formula[i - 1] : "";
      if (!formulaRefBoundaryBefore(before)) {
        out += ch;
        i += 1;
        continue;
      }

      const match = /^(\$?)([A-Za-z]{1,3})(\$?)([0-9]{1,7})/.exec(formula.slice(i));
      if (!match) {
        out += ch;
        i += 1;
        continue;
      }

      const token = match[0];
      const after = formula[i + token.length] || "";

      if (!formulaRefBoundaryAfter(after)) {
        out += ch;
        i += 1;
        continue;
      }

      const colLock = match[1];
      const colText = match[2];
      const rowLock = match[3];
      const rowText = match[4];
      const originalCol = colLettersToIndex(colText);
      const originalRow = Number.parseInt(rowText, 10);

      if (
        originalCol < 0 ||
        !Number.isInteger(originalRow) ||
        originalRow < 1
      ) {
        out += token;
        i += token.length;
        continue;
      }

      const nextCol = colLock
        ? colText
        : columnName(Math.max(0, originalCol + (Number.isFinite(colDelta) ? colDelta : 0)));

      const nextRow = rowLock
        ? originalRow
        : Math.max(1, originalRow + (Number.isFinite(rowDelta) ? rowDelta : 0));

      out += `${colLock}${nextCol}${rowLock}${nextRow}`;
      i += token.length;
    }

    return out;
  }

  function fillHandleRawValueForTargetCell(sourceValues, source, series, targetRow, targetCol, direction) {
    if (series) {
      const offset = direction === "right"
        ? targetCol - source.endCol
        : targetRow - source.endRow;

      return fillHandleSeriesRawValue(series, offset);
    }

    const patternIndex = direction === "right"
      ? (targetCol - source.startCol) % sourceValues.length
      : (targetRow - source.startRow) % sourceValues.length;

    const pattern = sourceValues[patternIndex];
    if (!pattern) return "";

    if (isFormulaValue(pattern.raw)) {
      return shiftSpreadsheetFormulaRefs(
        pattern.raw,
        targetRow - pattern.row,
        targetCol - pattern.col
      );
    }

    return pattern.raw;
  }

  function fillHandleBlockedByMerge(sheet, source, direction, endRow, endCol) {
    if (!sheet || !source || !direction) return false;

    if (direction === "right") {
      if (endCol <= source.endCol) return false;

      for (let c = source.endCol + 1; c <= endCol; c++) {
        if (mergeAtCell(sheet, source.startRow, c)) return true;
      }

      return false;
    }

    if (endRow <= source.endRow) return false;

    for (let r = source.endRow + 1; r <= endRow; r++) {
      if (mergeAtCell(sheet, r, source.startCol)) return true;
    }

    return false;
  }

  function applySpreadsheetFillDown(ctx) {
    if (!ctx || state.readOnly || state.tooLarge) return false;

    const sheet = state.sheets[state.active];
    if (!sheet) return false;

    const source = ctx.source;
    const direction = ctx.direction === "right" ? "right" : "down";
    const endRow = Number(ctx.endRow);
    const endCol = Number(ctx.endCol);

    if (!source || !Number.isInteger(endRow) || !Number.isInteger(endCol)) return false;

    const hasTarget = direction === "right"
      ? endCol > source.endCol
      : endRow > source.endRow;

    if (!hasTarget) return false;

    if (fillHandleBlockedByMerge(sheet, source, direction, endRow, endCol)) {
      setStatus(tr(
        "filemgr.spreadsheet_editor.fill_merge_blocked",
        null,
        "Cannot fill over merged cells."
      ), "warn");
      return false;
    }

    const sourceValues = fillHandleSourceValues(sheet, source, direction);
    if (!sourceValues.length) return false;

    const series = fillHandleDateSeries(sourceValues) || fillHandleNumericSeries(sourceValues);
    let changed = false;

    if (direction === "right") {
      const row = source.startRow;

      for (let c = source.endCol + 1; c <= Math.min(endCol, MAX_EDIT_COLS - 1); c++) {
        const patternIndex = (c - source.startCol) % sourceValues.length;
        const pattern = sourceValues[patternIndex];

        const nextRaw = fillHandleRawValueForTargetCell(sourceValues, source, series, row, c, direction);
        const nextFormat = pattern.format;
        const nextFormatKey = cellFormatKey(nextFormat);

        if (cellRaw(sheet, row, c) !== nextRaw) changed = true;
        if (cellFormatKey(getCellFormat(sheet, row, c)) !== nextFormatKey) changed = true;

        setCellRaw(sheet, row, c, nextRaw);
        setCellFormat(sheet, row, c, nextFormat);
      }
    } else {
      const col = source.startCol;

      for (let r = source.endRow + 1; r <= Math.min(endRow, MAX_EDIT_ROWS - 1); r++) {
        const patternIndex = (r - source.startRow) % sourceValues.length;
        const pattern = sourceValues[patternIndex];

        const nextRaw = fillHandleRawValueForTargetCell(sourceValues, source, series, r, col, direction);
        const nextFormat = pattern.format;
        const nextFormatKey = cellFormatKey(nextFormat);

        if (cellRaw(sheet, r, col) !== nextRaw) changed = true;
        if (cellFormatKey(getCellFormat(sheet, r, col)) !== nextFormatKey) changed = true;

        setCellRaw(sheet, r, col, nextRaw);
        setCellFormat(sheet, r, col, nextFormat);
      }
    }

    if (!changed) return false;

    state.selection = null;
    state.rangeSelection = {
      startRow: source.startRow,
      startCol: source.startCol,
      endRow: direction === "down" ? endRow : source.endRow,
      endCol: direction === "right" ? endCol : source.endCol
    };
    state.activeCell = { row: source.startRow, col: source.startCol };
    formulaFocus = null;

    commitHistorySnapshot(ctx.before || captureHistorySnapshot());
    render();

    return true;
  }

function finishSpreadsheetFillHandle(apply) {
    const ctx = fillHandleState;
    fillHandleState = null;

    if (!ctx) return false;

    state.selection = null;
    state.rangeSelection = ctx.source
      ? {
          startRow: ctx.source.startRow,
          startCol: ctx.source.col,
          endRow: ctx.source.endRow,
          endCol: ctx.source.col
        }
      : null;
    state.activeCell = ctx.source
      ? { row: ctx.source.startRow, col: ctx.source.col }
      : null;

    if (!ctx.moved || !apply) {
      repaintSpreadsheetSelection();
      return false;
    }

    return applySpreadsheetFillDown(ctx);
  }

  function startSpreadsheetFillHandle(ev, row, col) {
    if (state.readOnly || state.tooLarge) return;
    if (!ev || ev.button !== 0) return;

    const sheet = state.sheets[state.active];
    const source = fillHandleSourceRangeForCell(row, col);
    if (!sheet || !source) return;

    for (let r = source.startRow; r <= source.endRow; r++) {
      for (let c = source.startCol; c <= source.endCol; c++) {
        if (mergeAtCell(sheet, r, c)) return;
      }
    }

    ev.preventDefault();
    ev.stopPropagation();

    state.selection = null;
    state.rangeSelection = source.endRow > source.startRow || source.endCol > source.startCol
      ? {
          startRow: source.startRow,
          startCol: source.startCol,
          endRow: source.endRow,
          endCol: source.endCol
        }
      : null;
    state.activeCell = { row: source.startRow, col: source.startCol };
    formulaFocus = null;
    repaintSpreadsheetSelection();

    const ctx = {
      source,
      direction: source.orientation || "",
      endRow: source.endRow,
      endCol: source.endCol,
      moved: false,
      startX: Number(ev.clientX),
      startY: Number(ev.clientY),
      before: captureHistorySnapshot()
    };

    fillHandleState = ctx;

    const onMove = (moveEv) => {
      if (!fillHandleState || fillHandleState !== ctx) return;

      const dx = Number(moveEv.clientX) - ctx.startX;
      const dy = Number(moveEv.clientY) - ctx.startY;

      if (!ctx.direction) {
        ctx.direction = Math.abs(dx) > Math.abs(dy) ? "right" : "down";
      }

      if (ctx.direction === "right") {
        const nextEndCol = Math.max(
          source.endCol,
          spreadsheetFillEndColFromViewportX(source.endCol, source.startRow, Number(moveEv.clientX))
        );

        if (nextEndCol === ctx.endCol) return;

        ctx.endCol = nextEndCol;
        ctx.moved = ctx.endCol > source.endCol;
        paintFillHandlePreview(source, ctx.direction, ctx.endRow, ctx.endCol);
        return;
      }

      const nextEndRow = Math.max(
        source.endRow,
        spreadsheetFillEndRowFromViewportY(source.endRow, source.startCol, Number(moveEv.clientY))
      );

      if (nextEndRow === ctx.endRow) return;

      ctx.endRow = nextEndRow;
      ctx.moved = ctx.endRow > source.endRow;
      paintFillHandlePreview(source, ctx.direction, ctx.endRow, ctx.endCol);
    };

    const cleanup = () => {
      document.removeEventListener("pointermove", onMove, true);
      document.removeEventListener("pointerup", onUp, true);
      document.removeEventListener("pointercancel", onCancel, true);
    };

    const onUp = (upEv) => {
      upEv.preventDefault();
      upEv.stopPropagation();
      cleanup();
      finishSpreadsheetFillHandle(true);
    };

    const onCancel = () => {
      cleanup();
      finishSpreadsheetFillHandle(false);
    };

    document.addEventListener("pointermove", onMove, true);
    document.addEventListener("pointerup", onUp, true);
    document.addEventListener("pointercancel", onCancel, true);
  }

function createSpreadsheetFillHandle(row, col) {
    const handle = document.createElement("span");
    handle.className = "spreadsheetFillHandle";
    handle.dataset.row = String(row);
    handle.dataset.col = String(col);
    handle.tabIndex = -1;
    handle.setAttribute("role", "button");
    handle.setAttribute(
      "aria-label",
      tr("filemgr.spreadsheet_editor.fill_down", null, "Drag to fill down")
    );
    handle.title = tr("filemgr.spreadsheet_editor.fill_down", null, "Drag to fill down");
    handle.addEventListener("pointerdown", (ev) => startSpreadsheetFillHandle(ev, row, col));
    handle.addEventListener("click", (ev) => {
      ev.preventDefault();
      ev.stopPropagation();
    });
    return handle;
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

    /*
     * Sticky spreadsheet headers need the same inline Highlight cleanup as
     * whole-row and whole-column selections.
     */
    for (const el of bodyEl.querySelectorAll(
      '[data-spreadsheet-range-header-style="1"]'
    )) {
      el.removeAttribute(
        "data-spreadsheet-range-header-style"
      );
      el.style.removeProperty("background");
      el.style.removeProperty("color");
      el.style.removeProperty("box-shadow");
      el.style.removeProperty("position");
      el.style.removeProperty("z-index");
    }

    const classes = [
      "spreadsheetRangeSelectedCell",
      "spreadsheetRangeSelectedInput",
      "spreadsheetRangeSelectedHeader",
      "spreadsheetRangeEdgeTop",
      "spreadsheetRangeEdgeRight",
      "spreadsheetRangeEdgeBottom",
      "spreadsheetRangeEdgeLeft"
    ];

    const selector = classes
      .map((name) => `.${name}`)
      .join(", ");

    for (const el of bodyEl.querySelectorAll(selector)) {
      el.classList.remove(...classes);
      el.removeAttribute("aria-selected");
    }
  }

  function markRangeSelectionHeader(header, axis) {
    if (!header) return;

    header.classList.add(
      "spreadsheetRangeSelectedHeader"
    );
    header.dataset.spreadsheetRangeHeaderStyle = "1";

    /*
     * UI compatibility: system Highlight applied inline is reliable on sticky
     * table headers across the supported DNA-Nexus themes.
     */
    header.style.setProperty(
      "background",
      "Highlight"
    );
    header.style.setProperty(
      "color",
      "HighlightText"
    );
    header.style.setProperty(
      "box-shadow",
      axis === "column"
        ? "inset 0 0 0 999px Highlight"
        : "inset -3px 0 0 Highlight"
    );

    const label = header.querySelector(
      axis === "column"
        ? ".spreadsheetColumnLabel"
        : ".spreadsheetRowLabel"
    );

    if (label) {
      label.dataset.spreadsheetRangeHeaderStyle = "1";
      label.style.setProperty(
        "position",
        "relative"
      );
      label.style.setProperty(
        "z-index",
        "6"
      );
      label.style.setProperty(
        "color",
        "HighlightText"
      );
    }

    header.setAttribute(
      "aria-selected",
      "true"
    );
  }

  function paintRangeSelection() {
    clearRangeSelectionClasses();

    const range = normalizedRangeSelection();
    if (!range || !bodyEl) return;

    /*
     * UI correctness: highlight the workbook row/column headers covered by the
     * cell range, matching familiar spreadsheet selection behavior.
     */
    for (let col = range.col1; col <= range.col2; col++) {
      const header = bodyEl.querySelector(
        `th.colHead[data-col="${col}"]`
      );

      if (!header) continue;

      markRangeSelectionHeader(
        header,
        "column"
      );
    }

    for (let row = range.row1; row <= range.row2; row++) {
      const header = bodyEl.querySelector(
        `tbody th.rowHead[data-row="${row}"]`
      );

      if (!header) continue;

      markRangeSelectionHeader(
        header,
        "row"
      );
    }

    for (let row = range.row1; row <= range.row2; row++) {
      for (let col = range.col1; col <= range.col2; col++) {
        const input = bodyEl.querySelector(
          `input[data-row="${row}"][data-col="${col}"]`
        );

        if (!input) continue;

        const cell = input.closest(
          "td[data-row][data-col]"
        );

        if (cell) {
          cell.classList.add(
            "spreadsheetRangeSelectedCell"
          );

          if (row === range.row1) {
            cell.classList.add(
              "spreadsheetRangeEdgeTop"
            );
          }

          if (col === range.col2) {
            cell.classList.add(
              "spreadsheetRangeEdgeRight"
            );
          }

          if (row === range.row2) {
            cell.classList.add(
              "spreadsheetRangeEdgeBottom"
            );
          }

          if (col === range.col1) {
            cell.classList.add(
              "spreadsheetRangeEdgeLeft"
            );
          }

          cell.setAttribute(
            "aria-selected",
            "true"
          );
        }

        input.classList.add(
          "spreadsheetRangeSelectedInput"
        );
        input.setAttribute(
          "aria-selected",
          "true"
        );
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


  function selectAllSpreadsheetCells() {
    if (state.tooLarge) return false;

    hideAxisMenu();
    hideTextColorMenu();
    hideFillMenu();
    hideBorderMenu();

    state.selectedImageId = "";
    state.selection = null;
    state.activeCell = null;
    state.rangeSelection = {
      startRow: 0,
      startCol: 0,
      endRow: MAX_EDIT_ROWS - 1,
      endCol: MAX_EDIT_COLS - 1
    };
    formulaFocus = null;

    repaintSpreadsheetSelection();
    updateFormulaBar(true);
    updateFormatToolbar();

    return true;
  }

  function configureSpreadsheetSelectAllCorner(corner) {
    if (!corner || corner.dataset.spreadsheetSelectAllCorner === "1") return;

    corner.dataset.spreadsheetSelectAllCorner = "1";
    corner.classList.add("spreadsheetSelectAllCorner");
    corner.tabIndex = 0;
    corner.setAttribute("role", "button");
    corner.setAttribute(
      "aria-label",
      tr("filemgr.spreadsheet_editor.select_all_cells", null, "Select all cells")
    );
    corner.title = tr("filemgr.spreadsheet_editor.select_all_cells", null, "Select all cells");

    const activate = (ev) => {
      ev.preventDefault();
      ev.stopPropagation();
      selectAllSpreadsheetCells();
    };

    corner.addEventListener("pointerdown", activate);
    corner.addEventListener("click", activate);
    corner.addEventListener("keydown", (ev) => {
      if (ev.key !== "Enter" && ev.key !== " ") return;
      activate(ev);
    });
  }

function beginCellRangePointer(ev, row, col) {
    if (state.readOnly || state.tooLarge) return;
    if (!ev || ev.button !== 0) return;
    if (!Number.isInteger(row) || !Number.isInteger(col)) return;

    const activeFormula = formulaFocus && formulaFocus.input;
    if (activeFormula && activeFormula !== ev.currentTarget && isFormulaValue(activeFormula.value)) {
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

  function clearSpreadsheetFillHandleAnchor() {
    if (!bodyEl) return;

    for (const el of bodyEl.querySelectorAll(".spreadsheetFillHandleAnchorCell")) {
      el.classList.remove("spreadsheetFillHandleAnchorCell");
    }
  }

  function paintSpreadsheetFillHandleAnchor() {
    clearSpreadsheetFillHandleAnchor();

    if (!bodyEl || state.readOnly || state.tooLarge) return;

    const range = normalizedRangeSelection(state.rangeSelection);
    let row = null;
    let col = null;

    if (range && (range.col1 === range.col2 || range.row1 === range.row2)) {
      row = range.row2;
      col = range.col2;
    } else if (!state.selection && state.activeCell) {
      row = Number(state.activeCell.row);
      col = Number(state.activeCell.col);
    }

    if (!Number.isInteger(row) || !Number.isInteger(col)) return;

    const cell = spreadsheetCellElement(row, col);
    if (cell) cell.classList.add("spreadsheetFillHandleAnchorCell");
  }

function repaintSpreadsheetSelection() {
    paintAxisSelection();
    paintRangeSelection();
    paintActiveCellSelection();
    paintSpreadsheetFillHandleAnchor();
    paintSpreadsheetFormulaReferences();
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
    if (
      type === "column" &&
      action === "sort_asc"
    ) {
      return tr(
        "filemgr.spreadsheet_editor.sort_ascending",
        null,
        "Sort A–Z / smallest to largest"
      );
    }

    if (
      type === "column" &&
      action === "sort_desc"
    ) {
      return tr(
        "filemgr.spreadsheet_editor.sort_descending",
        null,
        "Sort Z–A / largest to smallest"
      );
    }

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
    if (
      !textColorMenu ||
      textColorMenu.hidden
    ) {
      return false;
    }

    textColorMenu.hidden = true;
    textColorMenu.replaceChildren();
    return true;
  }

  function colorPaletteLabel(id) {
    switch (id) {
      case "pastel":
        return tr(
          "filemgr.spreadsheet_editor.color_palette_pastel",
          null,
          "Pastel"
        );

      case "material":
        return tr(
          "filemgr.spreadsheet_editor.color_palette_material",
          null,
          "Material"
        );

      case "grayscale":
        return tr(
          "filemgr.spreadsheet_editor.color_palette_grayscale",
          null,
          "Grayscale"
        );

      case "libreoffice":
        return tr(
          "filemgr.spreadsheet_editor.color_palette_libreoffice",
          null,
          "LibreOffice"
        );

      default:
        return tr(
          "filemgr.spreadsheet_editor.color_palette_standard",
          null,
          "Standard"
        );
    }
  }

  function positionSpreadsheetColorMenu(
    menu,
    button
  ) {
    const rect =
      button.getBoundingClientRect();

    menu.hidden = false;

    const left = Math.max(
      8,
      Math.min(
        rect.left,
        window.innerWidth -
          menu.offsetWidth -
          8
      )
    );

    const top = Math.max(
      8,
      Math.min(
        rect.bottom + 6,
        window.innerHeight -
          menu.offsetHeight -
          8
      )
    );

    menu.style.left = `${left}px`;
    menu.style.top = `${top}px`;
  }

  function ensureTextColorMenu() {
    if (textColorMenu) {
      return textColorMenu;
    }

    textColorMenu =
      document.createElement("div");

    textColorMenu.className =
      "spreadsheetTextColorMenu " +
      "spreadsheetColorPaletteMenu";

    textColorMenu.hidden = true;
    textColorMenu.setAttribute(
      "role",
      "dialog"
    );

    textColorMenu.addEventListener(
      "click",
      (event) => {
        event.stopPropagation();
      }
    );

    document.body.appendChild(
      textColorMenu
    );

    return textColorMenu;
  }

  function openTextColorMenu() {
    if (!textColorBtn) return;

    const menu =
      ensureTextColorMenu();

    const current =
      TEXT_COLOR_COLORS[
        textColorBtn.dataset.fg || ""
      ];

    COLOR_PALETTES.renderMenu(
      menu,
      {
        titleLabel: tr(
          "filemgr.spreadsheet_editor.text_color",
          null,
          "Text color"
        ),
        clearLabel: tr(
          "filemgr.spreadsheet_editor.text_none",
          null,
          "Default text"
        ),
        paletteTitle: tr(
          "filemgr.spreadsheet_editor.color_palette",
          null,
          "Color palette"
        ),
        paletteLabel:
          colorPaletteLabel,
        recentLabel: tr(
          "filemgr.spreadsheet_editor.color_recent",
          null,
          "Recent"
        ),
        customLabel: tr(
          "filemgr.spreadsheet_editor.color_custom",
          null,
          "Custom color…"
        ),
        selectedColor:
          current ? current.rgb : "",
        paletteStorageKey:
          "pqnas.spreadsheet.text.palette",
        recentStorageKey:
          "pqnas.spreadsheet.text.recent",
        onSelect(value) {
          hideTextColorMenu();
          applyFormatCommand(
            "fg",
            value
          );
        }
      }
    );

    positionSpreadsheetColorMenu(
      menu,
      textColorBtn
    );
  }

  function hideFillMenu() {
    if (
      !fillMenu ||
      fillMenu.hidden
    ) {
      return false;
    }

    fillMenu.hidden = true;
    fillMenu.replaceChildren();
    return true;
  }

  function ensureFillMenu() {
    if (fillMenu) {
      return fillMenu;
    }

    fillMenu =
      document.createElement("div");

    fillMenu.className =
      "spreadsheetFillMenu " +
      "spreadsheetColorPaletteMenu";

    fillMenu.hidden = true;
    fillMenu.setAttribute(
      "role",
      "dialog"
    );

    fillMenu.addEventListener(
      "click",
      (event) => {
        event.stopPropagation();
      }
    );

    document.body.appendChild(
      fillMenu
    );

    return fillMenu;
  }

  function openFillMenu() {
    if (!fillBtn) return;

    const menu =
      ensureFillMenu();

    const current =
      CELL_FILL_COLORS[
        fillBtn.dataset.fill || ""
      ];

    COLOR_PALETTES.renderMenu(
      menu,
      {
        titleLabel: tr(
          "filemgr.spreadsheet_editor.fill_color",
          null,
          "Fill color"
        ),
        clearLabel: tr(
          "filemgr.spreadsheet_editor.fill_none",
          null,
          "No fill"
        ),
        paletteTitle: tr(
          "filemgr.spreadsheet_editor.color_palette",
          null,
          "Color palette"
        ),
        paletteLabel:
          colorPaletteLabel,
        recentLabel: tr(
          "filemgr.spreadsheet_editor.color_recent",
          null,
          "Recent"
        ),
        customLabel: tr(
          "filemgr.spreadsheet_editor.color_custom",
          null,
          "Custom color…"
        ),
        selectedColor:
          current ? current.rgb : "",
        paletteStorageKey:
          "pqnas.spreadsheet.fill.palette",
        recentStorageKey:
          "pqnas.spreadsheet.fill.recent",
        onSelect(value) {
          hideFillMenu();
          applyFormatCommand(
            "bg",
            value
          );
        }
      }
    );

    positionSpreadsheetColorMenu(
      menu,
      fillBtn
    );
  }

  function hideBorderMenu() {
    if (!borderMenu) return false;

    const submenuApi =
      FM && FM.spreadsheetBorderMenu;

    if (
      submenuApi &&
      typeof submenuApi.close === "function"
    ) {
      submenuApi.close(borderMenu);
    }

    if (borderMenu.hidden) return false;

    borderMenu.hidden = true;
    borderMenu.replaceChildren();
    return true;
  }

  function borderMenuLabel(action, style = "") {
    if (action === "clear") return tr("filemgr.spreadsheet_editor.border_none", null, "No custom borders");
    if (action === "all" && style === "thin") return tr("filemgr.spreadsheet_editor.border_all_thin", null, "All thin borders");
    if (action === "outside" && style === "thin") return tr("filemgr.spreadsheet_editor.border_outside_thin", null, "Outside thin border");
    if (action === "outside" && style === "thick") return tr("filemgr.spreadsheet_editor.border_outside_thick", null, "Outside thick border");
    if (action === "top" && style === "thin") return tr("filemgr.spreadsheet_editor.border_top_thin", null, "Top border");
    if (action === "left" && style === "thin") return tr("filemgr.spreadsheet_editor.border_left_thin", null, "Left border");
    if (action === "right" && style === "thin") return tr("filemgr.spreadsheet_editor.border_right_thin", null, "Right border");
    if (action === "bottom" && style === "thin") return tr("filemgr.spreadsheet_editor.border_bottom_thin", null, "Bottom thin border");
    if (action === "bottom" && style === "double") return tr("filemgr.spreadsheet_editor.border_bottom_double", null, "Double bottom border");
    if (action === "bottom" && style === "thick") return tr("filemgr.spreadsheet_editor.border_bottom_thick", null, "Bottom thick border");
    return tr("filemgr.spreadsheet_editor.borders", null, "Borders");
  }

  function borderMenuItems() {
    return [
      { action: "clear", style: "" },
      { action: "all", style: "thin" },
      { action: "outside", style: "thin" },
      { action: "outside", style: "thick" },
      { action: "top", style: "thin" },
      { action: "bottom", style: "thin" },
      { action: "bottom", style: "double" },
      { action: "bottom", style: "thick" },
      { action: "left", style: "thin" },
      { action: "right", style: "thin" }
    ].map((item) => ({
      ...item,
      label: borderMenuLabel(
        item.action,
        item.style
      )
    }));
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

  function makeCellContextMenuButton(label, handler, disabled = false) {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.disabled = !!disabled;
    btn.setAttribute("role", "menuitem");
    btn.textContent = label;
    btn.addEventListener("click", (ev) => {
      ev.preventDefault();
      ev.stopPropagation();
      if (btn.disabled) return;
      hideBorderMenu();
      handler();
    });
    return btn;
  }

  function makeBorderMenuButton(action, style = "") {
    return makeCellContextMenuButton(borderMenuLabel(action, style), () => applyBorderCommand(action, style));
  }

  function openBorderMenu(x, y, row = null, col = null) {
    const menu = ensureBorderMenu();
    menu.replaceChildren();

    const sheet = state.sheets[state.active];
    const clickedMerge = Number.isInteger(row) && Number.isInteger(col) ? mergeAtCell(sheet, row, col) : null;
    const mergeSelection = selectedMergeRange();

    if (clickedMerge || mergeSelection) {
      const cellTitle = document.createElement("div");
      cellTitle.className = "spreadsheetBorderMenuTitle";
      cellTitle.textContent = tr("filemgr.spreadsheet_editor.cell", null, "Cell");
      menu.appendChild(cellTitle);

      if (clickedMerge) {
        menu.appendChild(makeCellContextMenuButton(
          tr("filemgr.spreadsheet_editor.unmerge_cells", null, "Unmerge cells"),
          () => applyUnmergeAtCell(row, col)
        ));
      } else if (mergeSelection) {
        menu.appendChild(makeCellContextMenuButton(
          tr("filemgr.spreadsheet_editor.merge_cells", null, "Merge cells"),
          () => applyMergeSelectedCells()
        ));
      }
    }

    const submenuApi =
      FM && FM.spreadsheetBorderMenu;

    if (
      submenuApi &&
      typeof submenuApi.appendSubmenu === "function"
    ) {
      submenuApi.appendSubmenu(menu, {
        label: tr(
          "filemgr.spreadsheet_editor.borders",
          null,
          "Borders"
        ),
        items: borderMenuItems(),
        onAction: (action, style) => {
          applyBorderCommand(action, style);
        },
        onClose: hideBorderMenu
      });
    } else {
      const title = document.createElement("div");
      title.className = "spreadsheetBorderMenuTitle";
      title.textContent = tr(
        "filemgr.spreadsheet_editor.borders",
        null,
        "Borders"
      );
      menu.appendChild(title);

      for (const item of borderMenuItems()) {
        menu.appendChild(
          makeBorderMenuButton(
            item.action,
            item.style
          )
        );
      }
    }

    menu.hidden = false;
    menu.style.left = `${Math.max(8, Number(x) || 8)}px`;
    menu.style.top = `${Math.max(8, Number(y) || 8)}px`;

    const rect = menu.getBoundingClientRect();
    const maxLeft = Math.max(8, window.innerWidth - rect.width - 8);
    const maxTop = Math.max(8, window.innerHeight - rect.height - 8);

    menu.style.left = `${Math.min(Math.max(8, Number(x) || 8), maxLeft)}px`;
    menu.style.top = `${Math.min(Math.max(8, Number(y) || 8), maxTop)}px`;
  }


  function spreadsheetSortApi() {
    return (
      FM &&
      FM.spreadsheetSort
        ? FM.spreadsheetSort
        : null
    );
  }

  function sheetHasSpreadsheetFormulas(sheet) {
    if (!sheet || !Array.isArray(sheet.rows)) {
      return false;
    }

    /*
     * Correctness: formulas are blocked until sorting
     * can safely update all affected cell references.
     * Moving formula rows without doing that could
     * silently change workbook meaning.
     */
    for (const row of sheet.rows) {
      if (!Array.isArray(row)) continue;

      for (const value of row) {
        if (isFormulaValue(value)) {
          return true;
        }
      }
    }

    return false;
  }

  function spreadsheetRangeHasFormulas(
    sheet,
    range
  ) {
    if (
      !sheet ||
      !Array.isArray(sheet.rows) ||
      !range
    ) {
      return false;
    }

    const row1 = Number(range.row1);
    const row2 = Number(range.row2);
    const col1 = Number(range.col1);
    const col2 = Number(range.col2);

    if (
      ![
        row1,
        row2,
        col1,
        col2
      ].every(Number.isInteger) ||
      row1 < 0 ||
      row2 < row1 ||
      col1 < 0 ||
      col2 < col1
    ) {
      return false;
    }

    /*
     * Correctness: only formulas inside the cells that
     * will actually move block a range sort. Formulas in
     * disconnected areas remain in their original cells.
     */
    for (
      let row = row1;
      row <= row2;
      row += 1
    ) {
      for (
        let col = col1;
        col <= col2;
        col += 1
      ) {
        if (
          isFormulaValue(
            cellRaw(
              sheet,
              row,
              col
            )
          )
        ) {
          return true;
        }
      }
    }

    return false;
  }


  function validSpreadsheetSortOrder(order, rowCount) {
    if (
      !Array.isArray(order) ||
      order.length !== rowCount
    ) {
      return false;
    }

    const seen = new Set();

    for (const value of order) {
      const index = Number(value);

      if (
        !Number.isInteger(index) ||
        index < 0 ||
        index >= rowCount ||
        seen.has(index)
      ) {
        return false;
      }

      seen.add(index);
    }

    return seen.size === rowCount;
  }

  function applySpreadsheetColumnSort(index, direction) {
    if (
      state.readOnly ||
      state.tooLarge ||
      state.saving ||
      !Number.isInteger(index) ||
      index < 0
    ) {
      return false;
    }

    const sheet = state.sheets[state.active];
    const api = spreadsheetSortApi();

    if (
      !sheet ||
      !Array.isArray(sheet.rows) ||
      !api ||
      typeof api.sortRows !== "function"
    ) {
      return false;
    }

    const nonEmptyRows = sheet.rows
      .slice(1)
      .filter((row) => {
        if (!Array.isArray(row)) return false;

        return String(
          row[index] == null
            ? ""
            : row[index]
        ).trim() !== "";
      });

    if (nonEmptyRows.length < 2) {
      setStatus(
        tr(
          "filemgr.spreadsheet_editor.sort_not_enough_data",
          null,
          "At least two data rows are required below the header."
        ),
        "warn"
      );
      return false;
    }

    if (
      Array.isArray(sheet.merges) &&
      sheet.merges.length
    ) {
      setStatus(
        tr(
          "filemgr.spreadsheet_editor.sort_blocked_merges",
          null,
          "Sorting is unavailable while the sheet contains merged cells."
        ),
        "warn"
      );
      return false;
    }

    if (
      Array.isArray(sheet.images) &&
      sheet.images.length
    ) {
      setStatus(
        tr(
          "filemgr.spreadsheet_editor.sort_blocked_images",
          null,
          "Sorting is unavailable while the sheet contains images."
        ),
        "warn"
      );
      return false;
    }

    if (sheetHasSpreadsheetFormulas(sheet)) {
      setStatus(
        tr(
          "filemgr.spreadsheet_editor.sort_blocked_formulas",
          null,
          "Sorting sheets containing formulas is not supported in this first version."
        ),
        "warn"
      );
      return false;
    }

    const normalizedDirection =
      direction === "desc"
        ? "desc"
        : "asc";

    /*
     * The column-header command keeps row 1 as the
     * header. A selectable header mode belongs to the
     * later toolbar Sort & Filter dialog.
     */
    const result = api.sortRows(
      sheet.rows,
      {
        keyCol: index,
        direction: normalizedDirection,
        header: "yes"
      }
    );

    if (
      !result ||
      result.ok !== true ||
      !validSpreadsheetSortOrder(
        result.order,
        sheet.rows.length
      )
    ) {
      setStatus(
        tr(
          "filemgr.spreadsheet_editor.failed",
          null,
          "Spreadsheet operation failed."
        ),
        "err"
      );
      return false;
    }

    const unchanged = result.order.every(
      (sourceIndex, targetIndex) =>
        sourceIndex === targetIndex
    );

    const column = columnName(index);

    if (unchanged) {
      setStatus(
        tr(
          "filemgr.spreadsheet_editor.sort_no_change",
          { column },
          `Column ${column} is already in that order.`
        ),
        ""
      );
      return false;
    }

    const historyBefore = captureHistorySnapshot();

    const oldRows = sheet.rows;
    const oldCellFormats = Array.isArray(
      sheet.cellFormats
    )
      ? sheet.cellFormats
      : [];

    const oldRowHeights = Array.isArray(
      sheet.rowHeights
    )
      ? sheet.rowHeights
      : [];

    const defaultRowHeight =
      sheetDefaultRowHeight(sheet);

    sheet.rows = result.rows;

    sheet.cellFormats = result.order.map(
      (sourceIndex) => {
        const source =
          oldCellFormats[sourceIndex];

        if (Array.isArray(source)) {
          return source;
        }

        const oldRow = oldRows[sourceIndex];

        return Array.from(
          {
            length: Array.isArray(oldRow)
              ? oldRow.length
              : 0
          },
          () => null
        );
      }
    );

    sheet.rowHeights = result.order.map(
      (sourceIndex) =>
        normalizeRowGeometry(
          oldRowHeights[sourceIndex],
          defaultRowHeight
        )
    );

    state.selection = makeAxisSelection(
      "column",
      index,
      {}
    );

    state.activeCell = null;
    state.rangeSelection = null;
    state.selectedImageId = "";
    formulaFocus = null;

    commitHistorySnapshot(historyBefore);
    render();

    const statusKey =
      normalizedDirection === "desc"
        ? "filemgr.spreadsheet_editor.sort_done_desc"
        : "filemgr.spreadsheet_editor.sort_done_asc";

    const fallback =
      normalizedDirection === "desc"
        ? `Sorted column ${column} descending.`
        : `Sorted column ${column} ascending.`;

    setStatus(
      tr(
        statusKey,
        { column },
        fallback
      ),
      "ok"
    );

    return true;
  }


  function validSpreadsheetRangeSortOrder(
    order,
    range
  ) {
    if (
      !range ||
      !Array.isArray(order)
    ) {
      return false;
    }

    const rowCount =
      range.row2 -
      range.row1 +
      1;

    if (order.length !== rowCount) {
      return false;
    }

    const seen = new Set();

    for (const value of order) {
      const index = Number(value);

      if (
        !Number.isInteger(index) ||
        index < range.row1 ||
        index > range.row2 ||
        seen.has(index)
      ) {
        return false;
      }

      seen.add(index);
    }

    return seen.size === rowCount;
  }

  function spreadsheetFormatAt(
    formats,
    row,
    col
  ) {
    if (
      !Array.isArray(formats) ||
      !Array.isArray(formats[row])
    ) {
      return null;
    }

    return formats[row][col] || null;
  }

  function applySpreadsheetRangeSort(
    range,
    keyCol,
    direction
  ) {
    if (
      state.readOnly ||
      state.tooLarge ||
      state.saving ||
      !range ||
      !Number.isInteger(keyCol)
    ) {
      return false;
    }

    const sheet =
      state.sheets[state.active];

    const api = spreadsheetSortApi();

    if (
      !sheet ||
      !Array.isArray(sheet.rows) ||
      !api ||
      typeof api.sortRange !== "function"
    ) {
      return false;
    }

    /*
     * Correctness: partial sorting remains blocked for
     * objects whose positions or references require a
     * separate coordinate transformation.
     */
    if (
      Array.isArray(sheet.merges) &&
      sheet.merges.length
    ) {
      setStatus(
        tr(
          "filemgr.spreadsheet_editor.sort_blocked_merges",
          null,
          "Sorting is unavailable while the sheet contains merged cells."
        ),
        "warn"
      );
      return false;
    }

    if (
      Array.isArray(sheet.images) &&
      sheet.images.length
    ) {
      setStatus(
        tr(
          "filemgr.spreadsheet_editor.sort_blocked_images",
          null,
          "Sorting is unavailable while the sheet contains images."
        ),
        "warn"
      );
      return false;
    }

    if (
      spreadsheetRangeHasFormulas(
        sheet,
        range
      )
    ) {
      setStatus(
        tr(
          "filemgr.spreadsheet_editor.sort_blocked_formulas",
          null,
          "Sorting sheets containing formulas is not supported in this first version."
        ),
        "warn"
      );
      return false;
    }

    const normalizedDirection =
      direction === "desc"
        ? "desc"
        : "asc";

    const result = api.sortRange(
      sheet.rows,
      range,
      {
        keyCol,
        direction: normalizedDirection,
        header: "auto"
      }
    );

    if (
      !result ||
      result.ok !== true ||
      !validSpreadsheetRangeSortOrder(
        result.order,
        range
      )
    ) {
      setStatus(
        tr(
          "filemgr.spreadsheet_editor.failed",
          null,
          "Spreadsheet operation failed."
        ),
        "err"
      );
      return false;
    }

    const unchanged =
      result.order.every(
        (sourceRow, offset) =>
          sourceRow ===
            range.row1 +
            offset
      );

    const column =
      columnName(keyCol);

    if (unchanged) {
      setStatus(
        tr(
          "filemgr.spreadsheet_editor.sort_no_change",
          { column },
          `Column ${column} is already in that order.`
        ),
        ""
      );
      return false;
    }

    const historyBefore =
      captureHistorySnapshot();

    const oldFormats =
      Array.isArray(sheet.cellFormats)
        ? sheet.cellFormats
        : [];

    /*
     * Clone row arrays before changing the selected
     * rectangle. Formatting outside the range must stay
     * attached to its original worksheet coordinates.
     */
    const nextFormats =
      result.rows.map(
        (row, rowIndex) => {
          const source =
            Array.isArray(
              oldFormats[rowIndex]
            )
              ? oldFormats[rowIndex]
              : [];

          const width =
            Math.max(
              Array.isArray(row)
                ? row.length
                : 0,
              source.length
            );

          return Array.from(
            { length: width },
            (_value, col) =>
              source[col] || null
          );
        }
      );

    for (
      let offset = 0;
      offset < result.order.length;
      offset++
    ) {
      const targetRow =
        range.row1 +
        offset;

      const sourceRow =
        result.order[offset];

      while (
        nextFormats[targetRow].length <=
        range.col2
      ) {
        nextFormats[targetRow].push(null);
      }

      for (
        let col = range.col1;
        col <= range.col2;
        col++
      ) {
        nextFormats[targetRow][col] =
          spreadsheetFormatAt(
            oldFormats,
            sourceRow,
            col
          );
      }
    }

    sheet.rows = result.rows;
    sheet.cellFormats = nextFormats;

    state.selection = null;
    state.rangeSelection = {
      startRow: range.row1,
      startCol: range.col1,
      endRow: range.row2,
      endCol: range.col2
    };

    state.activeCell = {
      row: range.row1,
      col: keyCol
    };

    state.selectedImageId = "";
    formulaFocus = null;

    commitHistorySnapshot(
      historyBefore
    );

    render();

    const statusKey =
      normalizedDirection === "desc"
        ? "filemgr.spreadsheet_editor.sort_done_desc"
        : "filemgr.spreadsheet_editor.sort_done_asc";

    const fallback =
      normalizedDirection === "desc"
        ? `Sorted column ${column} descending.`
        : `Sorted column ${column} ascending.`;

    setStatus(
      tr(
        statusKey,
        { column },
        fallback
      ),
      "ok"
    );

    return true;
  }

  function spreadsheetSortRangeLabel(range) {
    if (!range) return "";

    const row1 = Number(range.row1);
    const row2 = Number(range.row2);
    const col1 = Number(range.col1);
    const col2 = Number(range.col2);

    if (
      ![
        row1,
        row2,
        col1,
        col2
      ].every(Number.isInteger)
    ) {
      return "";
    }

    const first =
      `${columnName(col1)}${row1 + 1}`;

    const last =
      `${columnName(col2)}${row2 + 1}`;

    return first === last
      ? first
      : `${first}:${last}`;
  }

  function spreadsheetSortRangesEqual(
    first,
    second
  ) {
    return !!(
      first &&
      second &&
      first.row1 === second.row1 &&
      first.row2 === second.row2 &&
      first.col1 === second.col1 &&
      first.col2 === second.col2
    );
  }

  function spreadsheetSortRangeContains(
    outer,
    inner
  ) {
    return !!(
      outer &&
      inner &&
      outer.row1 <= inner.row1 &&
      outer.row2 >= inner.row2 &&
      outer.col1 <= inner.col1 &&
      outer.col2 >= inner.col2
    );
  }

  function spreadsheetUsedRangeForColumns(
    sheet,
    col1,
    col2
  ) {
    if (
      !sheet ||
      !Array.isArray(sheet.rows) ||
      !Number.isInteger(col1) ||
      !Number.isInteger(col2)
    ) {
      return null;
    }

    const firstCol =
      Math.min(col1, col2);

    const lastCol =
      Math.max(col1, col2);

    let firstRow = null;
    let lastRow = null;

    for (
      let row = 0;
      row < sheet.rows.length;
      row += 1
    ) {
      let hasValue = false;

      for (
        let col = firstCol;
        col <= lastCol;
        col += 1
      ) {
        if (
          String(
            cellRaw(sheet, row, col)
          ).trim() !== ""
        ) {
          hasValue = true;
          break;
        }
      }

      if (!hasValue) continue;

      if (firstRow == null) {
        firstRow = row;
      }

      lastRow = row;
    }

    if (
      firstRow == null ||
      lastRow == null
    ) {
      return null;
    }

    return {
      row1: firstRow,
      row2: lastRow,
      col1: firstCol,
      col2: lastCol
    };
  }

  function spreadsheetExpandedSortRange(
    sheet,
    selectedRange,
    keyCol
  ) {
    const api =
      spreadsheetSortApi();

    if (
      !sheet ||
      !Array.isArray(sheet.rows) ||
      !selectedRange ||
      !api ||
      typeof api.detectConnectedDataRange !==
        "function"
    ) {
      return null;
    }

    const preferredCol =
      Number.isInteger(keyCol) &&
      keyCol >= selectedRange.col1 &&
      keyCol <= selectedRange.col2
        ? keyCol
        : selectedRange.col1;

    let seed = null;

    for (
      let row = selectedRange.row1;
      row <= selectedRange.row2;
      row += 1
    ) {
      if (
        String(
          cellRaw(
            sheet,
            row,
            preferredCol
          )
        ).trim() !== ""
      ) {
        seed = {
          row,
          col: preferredCol
        };
        break;
      }
    }

    if (!seed) {
      for (
        let row = selectedRange.row1;
        row <= selectedRange.row2 &&
        !seed;
        row += 1
      ) {
        for (
          let col = selectedRange.col1;
          col <= selectedRange.col2;
          col += 1
        ) {
          if (
            String(
              cellRaw(sheet, row, col)
            ).trim() !== ""
          ) {
            seed = {
              row,
              col
            };
            break;
          }
        }
      }
    }

    if (!seed) return null;

    const expanded =
      api.detectConnectedDataRange(
        sheet.rows,
        seed.row,
        seed.col
      );

    if (
      !expanded ||
      !spreadsheetSortRangeContains(
        expanded,
        selectedRange
      )
    ) {
      return null;
    }

    return expanded;
  }

  function chooseSpreadsheetSortRange(
    selectedRange,
    expandedRange
  ) {
    if (
      !selectedRange ||
      !expandedRange ||
      spreadsheetSortRangesEqual(
        selectedRange,
        expandedRange
      )
    ) {
      return Promise.resolve("current");
    }

    if (confirmModal) {
      confirmModal.remove();
      confirmModal = null;
    }

    return new Promise((resolve) => {
      let done = false;

      const finish = (choice) => {
        if (done) return;
        done = true;

        document.removeEventListener(
          "keydown",
          onKeyDown
        );

        if (confirmModal) {
          confirmModal.remove();
          confirmModal = null;
        }

        resolve(choice);
      };

      const onKeyDown = (event) => {
        if (event.key !== "Escape") {
          return;
        }

        event.preventDefault();
        event.stopPropagation();
        finish("cancel");
      };

      const selected =
        spreadsheetSortRangeLabel(
          selectedRange
        );

      const expanded =
        spreadsheetSortRangeLabel(
          expandedRange
        );

      confirmModal =
        document.createElement("div");

      confirmModal.className =
        "spreadsheetEditorConfirmModal";

      confirmModal.setAttribute(
        "role",
        "presentation"
      );

      const box =
        document.createElement("div");

      box.className =
        "spreadsheetEditorConfirmBox " +
        "spreadsheetEditorSortRangeBox";

      box.setAttribute(
        "role",
        "dialog"
      );

      box.setAttribute(
        "aria-modal",
        "true"
      );

      box.setAttribute(
        "aria-labelledby",
        "spreadsheetEditorSortRangeTitle"
      );

      box.setAttribute(
        "aria-describedby",
        "spreadsheetEditorSortRangeText " +
        "spreadsheetEditorSortRangeTip"
      );

      const title =
        document.createElement("div");

      title.id =
        "spreadsheetEditorSortRangeTitle";

      title.className =
        "spreadsheetEditorConfirmTitle";

      title.textContent = tr(
        "filemgr.spreadsheet_editor.sort_range_dialog_title",
        null,
        "Sort range"
      );

      const message =
        document.createElement("div");

      message.id =
        "spreadsheetEditorSortRangeText";

      message.className =
        "spreadsheetEditorConfirmText";

      /*
       * Security: translations and cell references use
       * textContent, so workbook data cannot become HTML.
       */
      message.textContent = tr(
        "filemgr.spreadsheet_editor.sort_range_dialog_message",
        {
          selected,
          expanded
        },
        (
          "Cells next to the selected range also contain " +
          `data. Extend the sort range to ${expanded}, ` +
          `or sort only ${selected}?`
        )
      );

      const tip =
        document.createElement("div");

      tip.id =
        "spreadsheetEditorSortRangeTip";

      tip.className =
        "spreadsheetEditorSortRangeTip";

      tip.textContent = tr(
        "filemgr.spreadsheet_editor.sort_range_dialog_tip",
        null,
        (
          "Tip: Place the cell cursor inside a list and " +
          "choose Sort to detect the connected data range."
        )
      );

      const actions =
        document.createElement("div");

      actions.className =
        "spreadsheetEditorConfirmActions";

      const expandButton =
        document.createElement("button");

      expandButton.type = "button";
      expandButton.className = "btn";

      expandButton.textContent = tr(
        "filemgr.spreadsheet_editor.sort_range_dialog_expand",
        null,
        "Extend selection"
      );

      const currentButton =
        document.createElement("button");

      currentButton.type = "button";
      currentButton.className =
        "btn secondary";

      currentButton.textContent = tr(
        "filemgr.spreadsheet_editor.sort_range_dialog_current",
        null,
        "Current selection"
      );

      const cancelButton =
        document.createElement("button");

      cancelButton.type = "button";
      cancelButton.className =
        "btn secondary";

      cancelButton.textContent = tr(
        "common.cancel",
        null,
        "Cancel"
      );

      actions.appendChild(
        expandButton
      );

      actions.appendChild(
        currentButton
      );

      actions.appendChild(
        cancelButton
      );

      box.appendChild(title);
      box.appendChild(message);
      box.appendChild(tip);
      box.appendChild(actions);
      confirmModal.appendChild(box);

      confirmModal.addEventListener(
        "click",
        (event) => {
          if (event.target === confirmModal) {
            finish("cancel");
          }
        }
      );

      expandButton.addEventListener(
        "click",
        () => finish("expand")
      );

      currentButton.addEventListener(
        "click",
        () => finish("current")
      );

      cancelButton.addEventListener(
        "click",
        () => finish("cancel")
      );

      document.addEventListener(
        "keydown",
        onKeyDown
      );

      document.body.appendChild(
        confirmModal
      );

      window.setTimeout(
        () => expandButton.focus(),
        0
      );
    });
  }

  async function applySpreadsheetToolbarSort(
    direction
  ) {
    const sheet =
      state.sheets[state.active];

    const sortExplicitRange =
      async (
        selectedRange,
        keyCol
      ) => {
        const expandedRange =
          spreadsheetExpandedSortRange(
            sheet,
            selectedRange,
            keyCol
          );

        let targetRange =
          selectedRange;

        if (
          expandedRange &&
          !spreadsheetSortRangesEqual(
            selectedRange,
            expandedRange
          )
        ) {
          const choice =
            await chooseSpreadsheetSortRange(
              selectedRange,
              expandedRange
            );

          if (choice === "cancel") {
            return false;
          }

          if (choice === "expand") {
            targetRange =
              expandedRange;
          }
        }

        return applySpreadsheetRangeSort(
          targetRange,
          keyCol,
          direction
        );
      };

    const range =
      normalizedRangeSelection();

    if (range) {
      let keyCol =
        state.activeCell &&
        Number.isInteger(
          Number(state.activeCell.col)
        )
          ? Number(state.activeCell.col)
          : range.col1;

      if (
        keyCol < range.col1 ||
        keyCol > range.col2
      ) {
        keyCol = range.col1;
      }

      return sortExplicitRange(
        range,
        keyCol
      );
    }

    const selectedAxis =
      axisSelectionRange();

    if (
      selectedAxis &&
      selectedAxis.type === "column" &&
      Number.isInteger(
        Number(selectedAxis.start)
      )
    ) {
      const firstCol =
        Number(selectedAxis.start);

      const lastCol =
        Number.isInteger(
          Number(selectedAxis.end)
        )
          ? Number(selectedAxis.end)
          : firstCol;

      const selectedRange =
        spreadsheetUsedRangeForColumns(
          sheet,
          firstCol,
          lastCol
        );

      if (selectedRange) {
        return sortExplicitRange(
          selectedRange,
          firstCol
        );
      }

      return applySpreadsheetColumnSort(
        firstCol,
        direction
      );
    }

    if (
      state.activeCell &&
      Number.isInteger(
        Number(state.activeCell.row)
      ) &&
      Number.isInteger(
        Number(state.activeCell.col)
      )
    ) {
      const row =
        Number(state.activeCell.row);

      const col =
        Number(state.activeCell.col);

      const api =
        spreadsheetSortApi();

      const detectedRange =
        sheet &&
        Array.isArray(sheet.rows) &&
        api &&
        typeof api.detectConnectedDataRange ===
          "function"
          ? api.detectConnectedDataRange(
              sheet.rows,
              row,
              col
            )
          : null;

      /*
       * A lone active cell sorts its connected table
       * automatically without opening the expansion dialog.
       */
      if (
        detectedRange &&
        detectedRange.row2 >
          detectedRange.row1
      ) {
        return applySpreadsheetRangeSort(
          detectedRange,
          col,
          direction
        );
      }

      return applySpreadsheetColumnSort(
        col,
        direction
      );
    }

    setStatus(
      tr(
        "filemgr.spreadsheet_editor.sort_select_target",
        null,
        "Select a cell, column or cell range to sort."
      ),
      "warn"
    );

    return false;
  }

  function bindSpreadsheetSortToolbarMenu() {
    if (!sortMenu) return;

    for (
      const button of
      sortMenu.querySelectorAll(
        "[data-spreadsheet-sort-action]"
      )
    ) {
      button.addEventListener(
        "click",
        (event) => {
          event.preventDefault();
          event.stopPropagation();

          const direction =
            button.dataset
              .spreadsheetSortAction ===
              "desc"
              ? "desc"
              : "asc";

          closeToolbarIconMenus();

          void applySpreadsheetToolbarSort(
            direction
          );
        }
      );
    }
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
      delete: (axisType, axisIndex) => deleteSelectedAxis(axisType, axisIndex),
      sort: (axisIndex, direction) => {
        /*
         * UX correctness: column-header context-menu sorting
         * must use the same current-region detection and
         * range-expansion dialog as toolbar sorting.
         *
         * Preserve an existing multi-column selection when the
         * clicked column is already inside it. Otherwise select
         * the clicked column before starting the shared flow.
         */
        if (
          !axisSelectionContains(
            "column",
            axisIndex
          )
        ) {
          setSpreadsheetAxisSelection(
            "column",
            axisIndex
          );
        }

        void applySpreadsheetToolbarSort(
          direction
        );
      }
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

  function spreadsheetFormulaCellIsEditing(input) {
    return !!(
      input &&
      input.dataset &&
      input.dataset.spreadsheetFormulaEditing === "1"
    );
  }

  function beginSpreadsheetFormulaCellEdit(
    input,
    options = {}
  ) {
    if (
      !input ||
      state.readOnly ||
      state.tooLarge
    ) {
      return false;
    }

    const row = Number(
      input.dataset && input.dataset.row
    );
    const col = Number(
      input.dataset && input.dataset.col
    );
    const sheet = state.sheets[state.active];

    if (
      !sheet ||
      !Number.isInteger(row) ||
      !Number.isInteger(col)
    ) {
      return false;
    }

    const raw = cellRaw(
      sheet,
      row,
      col
    );

    if (!isFormulaValue(raw)) {
      return false;
    }

    /*
     * UX correctness: selecting a formula cell and editing
     * it are separate modes. Only explicit editing exposes
     * the raw formula and enables reference insertion.
     */
    input.readOnly = false;
    input.dataset.spreadsheetFormulaEditing = "1";
    input.value = String(raw);

    formulaFocus = {
      input,
      row,
      col,
      originalRaw: raw,
      wasDirty: state.dirty
    };

    paintSpreadsheetFormulaReferences();

    try {
      input.focus({
        preventScroll: true
      });
    } catch (_) {
      input.focus();
    }

    window.requestAnimationFrame(() => {
      const length =
        String(input.value || "").length;

      try {
        if (options.selectAll) {
          input.select();
        } else {
          input.setSelectionRange(
            length,
            length
          );
        }
      } catch (_) {}
    });

    return true;
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


  function worksheetMergeRanges(XLSX, ws, rowCount, colCount) {
    if (!ws || !ws["!ref"] || !Array.isArray(ws["!merges"]) || !XLSX || !XLSX.utils) return [];

    const out = [];

    for (const raw of ws["!merges"]) {
      const merge = normalizeMergeRange({
        s: {
          r: Number(raw && raw.s && raw.s.r),
          c: Number(raw && raw.s && raw.s.c)
        },
        e: {
          r: Number(raw && raw.e && raw.e.r),
          c: Number(raw && raw.e && raw.e.c)
        }
      }, rowCount, colCount);

      if (!merge) continue;
      if (out.some((existing) => mergeRangesOverlap(existing, merge))) continue;
      out.push(merge);
    }

    return out;
  }

  function worksheetDimensionDefaults(wb, sheetIndex) {
    const api = FM && FM.spreadsheetXlsxDimensions;

    if (api && typeof api.worksheetDefaults === "function") {
      return api.worksheetDefaults(wb, sheetIndex, {
        defaultColWidth: DEFAULT_COL_WIDTH,
        defaultRowHeight: DEFAULT_ROW_HEIGHT
      });
    }

    return {
      colWidth: DEFAULT_COL_WIDTH,
      rowHeight: DEFAULT_ROW_HEIGHT
    };
  }

  function isXlsxStringCell(cell) {
    const type = String(cell && cell.t || "");
    return type === "s" || type === "str" || type === "inlineStr";
  }

  function importSpreadsheetCellText(cell, value) {
    const text = String(value == null ? "" : value);

    // XLSX string cells can legally contain text beginning with "=". Keep such
    // values as explicit text so reopening our own saved file does not turn them
    // into formulas.
    if (isXlsxStringCell(cell) && (text.startsWith("=") || text.startsWith("'"))) {
      return "'" + text;
    }

    return text;
  }

  function worksheetToEditableRows(XLSX, ws, defaults = {}) {
    const defaultColWidth = normalizeColumnGeometry(
      defaults.colWidth,
      DEFAULT_COL_WIDTH
    );
    const defaultRowHeight = normalizeRowGeometry(
      defaults.rowHeight,
      DEFAULT_ROW_HEIGHT
    );

    if (!ws || !ws["!ref"]) {
      return {
        rows: Array.from({ length: DEFAULT_ROWS }, () => Array.from({ length: DEFAULT_COLS }, () => "")),
        colWidths: Array.from({ length: DEFAULT_COLS }, () => defaultColWidth),
        rowHeights: Array.from({ length: DEFAULT_ROWS }, () => defaultRowHeight),
        merges: [],
        tooLarge: false
      };
    }

    const range = XLSX.utils.decode_range(ws["!ref"]);
    /*
     * Correctness: retain the workbook's absolute A1 coordinate grid. Empty
     * leading rows and columns are meaningful because styles, merges and image
     * anchors use original Excel coordinates.
     */
    const sourceRows = Math.max(0, range.e.r + 1);
    const sourceCols = Math.max(0, range.e.c + 1);
    const tooLarge = sourceRows > MAX_EDIT_ROWS || sourceCols > MAX_EDIT_COLS;

    const rowCount = Math.max(Math.min(sourceRows, MAX_EDIT_ROWS), DEFAULT_ROWS);
    const minimumCols = Math.min(
      MAX_EDIT_COLS,
      Math.max(
        DEFAULT_COLS,
        Math.floor(
          Number(defaults.minimumCols) || 0
        )
      )
    );

    const colCount = Math.max(
      Math.min(sourceCols, MAX_EDIT_COLS),
      minimumCols
    );
    const rows = [];

    for (let r = 0; r < rowCount; r++) {
      const row = [];
      for (let c = 0; c < colCount; c++) {
        const addr = XLSX.utils.encode_cell({ r, c });
        const cell = ws[addr];

        if (!cell) {
          row.push("");
        } else if (cell.f) {
          row.push("=" + String(cell.f));
        } else if (
          defaults.preferRawValues &&
          cell.v != null
        ) {
          /*
           * Correctness: PQ-NAS metadata already preserves
           * the number format separately. Import the raw
           * value so currency/percent text is not baked into
           * the editable cell value a second time.
           */
          row.push(
            importSpreadsheetCellText(
              cell,
              cell.v
            )
          );
        } else if (cell.w != null) {
          row.push(
            importSpreadsheetCellText(
              cell,
              cell.w
            )
          );
        } else if (cell.v != null) {
          row.push(
            importSpreadsheetCellText(
              cell,
              cell.v
            )
          );
        } else {
          row.push("");
        }
      }
      rows.push(row);
    }

    const dimensionsApi =
      FM && FM.spreadsheetXlsxDimensions;

    const rawColumnWidths =
      dimensionsApi &&
      typeof dimensionsApi.worksheetColumnWidths ===
        "function"
        ? dimensionsApi.worksheetColumnWidths(
            defaults.workbook,
            defaults.sheetIndex,
            colCount,
            {
              defaultColWidth,
              defaultRowHeight
            }
          )
        : null;

    const colWidths = Array.from(
      { length: colCount },
      (_v, col) => {
        if (
          Array.isArray(rawColumnWidths) &&
          Number.isFinite(
            Number(rawColumnWidths[col])
          )
        ) {
          return normalizeColumnGeometry(
            rawColumnWidths[col],
            defaultColWidth
          );
        }

        const meta =
          ws["!cols"] &&
          ws["!cols"][col];

        return xlsxColumnToPixelWidth(
          meta,
          defaultColWidth
        );
      }
    );

    const rowHeights = Array.from({ length: rowCount }, (_v, r) => {
      const meta = ws["!rows"] && ws["!rows"][r];
      return xlsxRowToPixelHeight(meta, defaultRowHeight);
    });

    const cellFormats = Array.from({ length: rowCount }, () => Array.from({ length: colCount }, () => null));

    for (let r = 0; r < rowCount; r++) {
      for (let c = 0; c < colCount; c++) {
        const addr = XLSX.utils.encode_cell({ r, c });
        const style = ws[addr] && ws[addr].s;
        const rawXlsxBorder =
          defaults.xlsxBorders &&
          defaults.xlsxBorders[addr]
            ? defaults.xlsxBorders[addr]
            : null;

        const fmt = normalizeCellFormat({
          bold: !!(style && style.font && style.font.bold),
          italic: !!(style && style.font && style.font.italic),
          underline: !!(style && style.font && style.font.underline),
          fontName: normalizeSpreadsheetFontName(
            style && style.font && style.font.name
          ),
          fontSize: normalizeFontSize(style && style.font && style.font.sz),
          align: style && style.alignment && style.alignment.horizontal === "center" ? "center" : "",
          valign: normalizeVerticalAlign(style && style.alignment && style.alignment.vertical),
          bg: cellFillKeyFromRgb(style && style.fill && style.fill.fgColor && style.fill.fgColor.rgb),
          fg: cellTextColorKeyFromRgb(style && style.font && style.font.color && style.font.color.rgb),
          border: rawXlsxBorder ||
            cellBorderFromXlsxStyle(style && style.border)
        });
        cellFormats[r][c] = isEmptyCellFormat(fmt) ? null : fmt;
      }
    }

    const merges = worksheetMergeRanges(XLSX, ws, rowCount, colCount);

    return { rows, colWidths, rowHeights, cellFormats, merges, tooLarge };
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

    const imageApi = FM && FM.spreadsheetXlsxImages;
    state.workbookImageWarning = "";
    state.workbookImageInfo = imageApi && typeof imageApi.inspectArrayBuffer === "function"
      ? imageApi.inspectArrayBuffer(buf)
      : null;

    /*
     * Supported spreadsheet images are preserved and editable. Merely finding
     * drawing parts is no longer an error or warning condition.
     */
    // Security: formulas are preserved as inert strings and evaluated only by
    // our small allowlisted parser. No eval(), macros, links or active content.
    const wb = XLSX.read(buf, {
      type: "array",
      cellFormula: true,
      cellHTML: false,
      cellNF: false,
      cellStyles: true,
      bookFiles: true
    });

    const fontsApi = FM && FM.spreadsheetFonts;
    state.workbookFont =
      fontsApi && typeof fontsApi.readWorkbookDefaultFont === "function"
        ? fontsApi.readWorkbookDefaultFont(wb)
        : normalizedWorkbookFont(null);

    const storedCellFormats = readStoredCellFormats(XLSX, wb);
    const names = Array.isArray(wb.SheetNames)
      ? wb.SheetNames.filter((name) => name !== STYLE_SHEET_NAME)
      : [];

    const borderApi = FM && FM.spreadsheetXlsxBorders;
    const workbookBorders =
      borderApi &&
      typeof borderApi.bordersBySheet === "function"
        ? borderApi.bordersBySheet(wb, names)
        : [];

    const workbookImages =
      imageApi &&
      typeof imageApi.imagesFromWorkbookFiles ===
        "function"
        ? imageApi.imagesFromWorkbookFiles(
            wb,
            names,
            state.workbookImageInfo
          )
        : [];

    if (!names.length) {
      return [{
        name: tr("filemgr.spreadsheet_create.sheet.sheet1", null, "Sheet1"),
        rows: Array.from({ length: DEFAULT_ROWS }, () => Array.from({ length: DEFAULT_COLS }, () => "")),
        defaultColWidth: DEFAULT_COL_WIDTH,
        defaultRowHeight: DEFAULT_ROW_HEIGHT,
        colWidths: Array.from({ length: DEFAULT_COLS }, () => DEFAULT_COL_WIDTH),
        rowHeights: Array.from({ length: DEFAULT_ROWS }, () => DEFAULT_ROW_HEIGHT),
        cellFormats: Array.from({ length: DEFAULT_ROWS }, () => Array.from({ length: DEFAULT_COLS }, () => null)),
        merges: [],
        freeze: { topRows: 0, leftCols: 0 }
      }];
    }

    let anyTooLarge = false;
    const sheets = names.map((name, idx) => {
      const defaults =
        worksheetDimensionDefaults(wb, idx);

      const safeName =
        safeSheetName(name, idx);

      const storedFormats =
        storedCellFormats[safeName] ||
        storedCellFormats[name] ||
        null;

      const sheetImages = workbookImages.filter(
        (image) => image.sheetIndex === idx
      );

      const imageBounds =
        imageApi &&
        typeof imageApi.imageAnchorBounds ===
          "function"
          ? imageApi.imageAnchorBounds(
              sheetImages
            )
          : {
              rows: 0,
              cols: 0
            };

      const converted = worksheetToEditableRows(
        XLSX,
        wb.Sheets[name],
        {
          ...defaults,
          workbook: wb,
          sheetIndex: idx,
          xlsxBorders: workbookBorders[idx] || null,
          minimumCols: imageBounds.cols,

          /*
           * Stored PQ-NAS metadata keeps formatting
           * separate from values, so import cell.v rather
           * than SheetJS's formatted cell.w text.
           */
          preferRawValues:
            Array.isArray(storedFormats)
        }
      );

      anyTooLarge =
        anyTooLarge || converted.tooLarge;

      const sheet = {
        name: safeName,
        rows: converted.rows,
        defaultColWidth: defaults.colWidth,
        defaultRowHeight: defaults.rowHeight,
        colWidths: converted.colWidths,
        rowHeights: converted.rowHeights,
        cellFormats: Array.isArray(storedFormats) ? storedFormats : converted.cellFormats,
        merges: converted.merges,
        freeze: worksheetFreezeFromWorkbook(XLSX, wb, wb.Sheets[name], idx),
        images: sheetImages
      };

      if (imageApi && typeof imageApi.expandSheetForImages === "function") {
        imageApi.expandSheetForImages(sheet, {
          defaultColWidth: sheet.defaultColWidth,
          defaultRowHeight: sheet.defaultRowHeight
        });
      }

      return sheet;
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
    if (sortSelect) sortSelect.disabled = disabled;
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


  function insertedImageTargetCell() {
    const active = state.activeCell;

    if (
      active &&
      Number.isInteger(active.row) &&
      Number.isInteger(active.col) &&
      active.row >= 0 &&
      active.col >= 0
    ) {
      return {
        row: active.row,
        col: active.col
      };
    }

    return {
      row: 0,
      col: 0
    };
  }

  function insertedImageErrorText(error) {
    const code = String(
      error && error.code || ""
    );

    if (code === "unsupported_image_type") {
      return tr(
        "filemgr.spreadsheet_editor.image_invalid",
        null,
        "Only valid PNG and JPEG images are supported."
      );
    }

    if (
      code === "image_file_too_large" ||
      code === "empty_image_file"
    ) {
      return tr(
        "filemgr.spreadsheet_editor.image_too_large",
        null,
        "The selected image is empty or larger than 8 MiB."
      );
    }

    if (code === "image_dimensions_too_large") {
      return tr(
        "filemgr.spreadsheet_editor.image_dimensions_too_large",
        null,
        "The image dimensions are too large."
      );
    }

    if (code === "image_count_limit") {
      return tr(
        "filemgr.spreadsheet_editor.image_count_limit",
        null,
        "The spreadsheet image limit has been reached."
      );
    }

    return tr(
      "filemgr.spreadsheet_editor.image_add_failed",
      null,
      "The image could not be added."
    );
  }

  async function insertSpreadsheetImageFile(file) {
    if (
      state.readOnly ||
      state.tooLarge ||
      !file
    ) {
      return;
    }

    const imageApi =
      FM && FM.spreadsheetXlsxImages;

    const sheetIndex = state.active;
    const sheet = state.sheets[sheetIndex];

    if (
      !sheet ||
      !imageApi ||
      typeof imageApi.createInsertedImageFromFile !==
        "function"
    ) {
      setStatus(
        tr(
          "filemgr.spreadsheet_editor.image_add_failed",
          null,
          "The image could not be added."
        ),
        "err"
      );
      return;
    }

    if (
      typeof imageApi.sheetHasImportedDrawing ===
        "function" &&
      imageApi.sheetHasImportedDrawing(sheet)
    ) {
      setStatus(
        tr(
          "filemgr.spreadsheet_editor.image_existing_drawing_unsupported",
          null,
          "Adding images to a sheet that already contains imported drawing objects is not supported yet."
        ),
        "warn"
      );
      return;
    }

    const target = insertedImageTargetCell();

    try {
      const image =
        await imageApi.createInsertedImageFromFile(
          file,
          sheet,
          {
            row: target.row,
            col: target.col,
            sheetIndex,
            defaultColWidth:
              sheetDefaultColumnWidth(sheet),
            defaultRowHeight:
              sheetDefaultRowHeight(sheet)
          }
        );

      /*
       * The user may have changed sheet while the image was being read.
       * Never insert the result into a stale workbook object.
       */
      if (
        state.active !== sheetIndex ||
        state.sheets[sheetIndex] !== sheet
      ) {
        setStatus(
          tr(
            "filemgr.spreadsheet_editor.image_add_failed",
            null,
            "The image could not be added."
          ),
          "err"
        );
        return;
      }

      const historyBefore =
        captureHistorySnapshot();

      if (!Array.isArray(sheet.images)) {
        sheet.images = [];
      }

      sheet.images.push(image);

      if (
        typeof imageApi.expandSheetForImages ===
        "function"
      ) {
        imageApi.expandSheetForImages(
          sheet,
          {
            defaultColWidth:
              sheetDefaultColumnWidth(sheet),
            defaultRowHeight:
              sheetDefaultRowHeight(sheet)
          }
        );
      }

      state.selectedImageId = String(
        image.id || ""
      );

      state.selection = null;
      state.rangeSelection = null;
      state.activeCell = null;
      formulaFocus = null;

      commitHistorySnapshot(historyBefore);
      render();

      const imageName = String(
        image.name ||
        file.name ||
        ""
      );

      setStatus(
        tr(
          "filemgr.spreadsheet_editor.image_added",
          { name: imageName },
          `Image added: ${imageName}`
        ),
        "ok"
      );
    } catch (error) {
      console.warn(
        "Spreadsheet image insertion failed:",
        error
      );

      setStatus(
        insertedImageErrorText(error),
        "err"
      );
    }
  }

  function chooseSpreadsheetImage() {
    if (
      state.readOnly ||
      state.tooLarge ||
      !insertImageInput
    ) {
      return;
    }

    insertImageInput.value = "";
    insertImageInput.click();
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
            <select id="spreadsheetEditorFontName" class="spreadsheetFontSizeSelect spreadsheetFontNameSelect" aria-label="${tr("filemgr.spreadsheet_editor.font_family", null, "Font")}" title="${tr("filemgr.spreadsheet_editor.font_family", null, "Font")}"></select>
            <select id="spreadsheetEditorFontSize" class="spreadsheetFontSizeSelect" aria-label="${tr("filemgr.spreadsheet_editor.font_size", null, "Font size")}" title="${tr("filemgr.spreadsheet_editor.font_size", null, "Font size")}">
              <option value="">${tr("filemgr.spreadsheet_editor.font_size_default", null, "Size")}</option>
              ${FONT_SIZE_OPTIONS.map((size) => `<option value="${size}">${size}</option>`).join("")}
            </select>
            <button id="spreadsheetEditorDecimalsDecrease" type="button" class="btn secondary spreadsheetToolBtn" aria-pressed="false" aria-label="${tr("filemgr.spreadsheet_editor.decimals_decrease", null, "Decrease decimals")}" title="${tr("filemgr.spreadsheet_editor.decimals_decrease", null, "Decrease decimals")}">−.0</button>
            <button id="spreadsheetEditorDecimalsIncrease" type="button" class="btn secondary spreadsheetToolBtn" aria-pressed="false" aria-label="${tr("filemgr.spreadsheet_editor.decimals_increase", null, "Increase decimals")}" title="${tr("filemgr.spreadsheet_editor.decimals_increase", null, "Increase decimals")}">+.0</button>
            <select id="spreadsheetEditorNumberFormat" class="spreadsheetFontSizeSelect" aria-label="${tr("filemgr.spreadsheet_editor.number_format", null, "Number format")}" title="${tr("filemgr.spreadsheet_editor.number_format", null, "Number format")}">
              <option value="">${tr("filemgr.spreadsheet_editor.number_format_plain", null, "Plain")}</option>
              <option value="percent">% Percent</option>
              <option value="date">${tr("filemgr.spreadsheet_editor.number_format_date", null, "Date")}</option>
              <option value="currency:eur">€ Euro</option>
              <option value="currency:usd">$ Dollar</option>
              <option value="currency:gbp">£ Pound</option>
              <option value="currency:sek">kr Krona</option>
              <option value="currency:cny">¥ Yuan</option>
            </select>
            <button id="spreadsheetEditorAlign" class="spreadsheetToolBtn spreadsheetIconMenuBtn" type="button" value="left" aria-haspopup="menu" aria-expanded="false" aria-label="${tr("filemgr.spreadsheet_editor.horizontal_align", null, "Horizontal align")}" title="${tr("filemgr.spreadsheet_editor.horizontal_align", null, "Horizontal align")}">${toolbarIconSvg("align", "left")}</button>
            <div id="spreadsheetEditorAlignMenu" class="spreadsheetIconMenu" role="menu" hidden aria-label="${tr("filemgr.spreadsheet_editor.horizontal_align", null, "Horizontal align")}">
              <button type="button" role="menuitemradio" aria-checked="true" data-spreadsheet-toolbar-value="left">${toolbarIconSvg("align", "left")}<span>${tr("filemgr.spreadsheet_editor.align_left", null, "Left")}</span></button>
              <button type="button" role="menuitemradio" aria-checked="false" data-spreadsheet-toolbar-value="center">${toolbarIconSvg("align", "center")}<span>${tr("filemgr.spreadsheet_editor.align_center", null, "Center")}</span></button>
              <button type="button" role="menuitemradio" aria-checked="false" data-spreadsheet-toolbar-value="right">${toolbarIconSvg("align", "right")}<span>${tr("filemgr.spreadsheet_editor.align_right", null, "Align right")}</span></button>
            </div>
            <button id="spreadsheetEditorValign" class="spreadsheetToolBtn spreadsheetIconMenuBtn" type="button" value="middle" aria-haspopup="menu" aria-expanded="false" aria-label="${tr("filemgr.spreadsheet_editor.vertical_align", null, "Vertical align")}" title="${tr("filemgr.spreadsheet_editor.vertical_align", null, "Vertical align")}">${toolbarIconSvg("valign", "middle")}</button>
            <div id="spreadsheetEditorValignMenu" class="spreadsheetIconMenu" role="menu" hidden aria-label="${tr("filemgr.spreadsheet_editor.vertical_align", null, "Vertical align")}">
              <button type="button" role="menuitemradio" aria-checked="false" data-spreadsheet-toolbar-value="top">${toolbarIconSvg("valign", "top")}<span>${tr("filemgr.spreadsheet_editor.valign_top", null, "Top")}</span></button>
              <button type="button" role="menuitemradio" aria-checked="true" data-spreadsheet-toolbar-value="middle">${toolbarIconSvg("valign", "middle")}<span>${tr("filemgr.spreadsheet_editor.valign_middle", null, "Middle")}</span></button>
              <button type="button" role="menuitemradio" aria-checked="false" data-spreadsheet-toolbar-value="bottom">${toolbarIconSvg("valign", "bottom")}<span>${tr("filemgr.spreadsheet_editor.valign_bottom", null, "Bottom")}</span></button>
            </div>
            <button id="spreadsheetEditorSort" class="spreadsheetToolBtn spreadsheetIconMenuBtn" type="button" aria-haspopup="menu" aria-expanded="false" aria-label="${tr("filemgr.spreadsheet_editor.sort_toolbar", null, "Sort")}" title="${tr("filemgr.spreadsheet_editor.sort_toolbar", null, "Sort")}">
              <svg viewBox="0 0 24 24" aria-hidden="true">
                <path d="M7 5v14"></path>
                <path d="m4 8 3-3 3 3"></path>
                <path d="M13 7h7"></path>
                <path d="M13 12h5"></path>
                <path d="M13 17h3"></path>
              </svg>
            </button>
            <div id="spreadsheetEditorSortMenu" class="spreadsheetIconMenu" role="menu" hidden aria-label="${tr("filemgr.spreadsheet_editor.sort_toolbar", null, "Sort")}">
              <button type="button" role="menuitem" data-spreadsheet-sort-action="asc">
                <span aria-hidden="true">↑</span>
                <span>${tr("filemgr.spreadsheet_editor.sort_ascending", null, "Sort A–Z / smallest to largest")}</span>
              </button>
              <button type="button" role="menuitem" data-spreadsheet-sort-action="desc">
                <span aria-hidden="true">↓</span>
                <span>${tr("filemgr.spreadsheet_editor.sort_descending", null, "Sort Z–A / largest to smallest")}</span>
              </button>
            </div>
            <button id="spreadsheetEditorTextColor" type="button" class="btn secondary spreadsheetToolBtn spreadsheetTextToolBtn" aria-pressed="false" aria-label="${tr("filemgr.spreadsheet_editor.text_color", null, "Text color")}" title="${tr("filemgr.spreadsheet_editor.text_color", null, "Text color")}">
              <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 4 6 20"></path><path d="M12 4 18 20"></path><path d="M8 14h8"></path><path d="M5 22h14"></path></svg>
            </button>
            <button id="spreadsheetEditorFill" type="button" class="btn secondary spreadsheetToolBtn spreadsheetFillToolBtn" aria-pressed="false" aria-label="${tr("filemgr.spreadsheet_editor.fill_color", null, "Fill color")}" title="${tr("filemgr.spreadsheet_editor.fill_color", null, "Fill color")}">
              <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M4 14 12 6l6 6-8 8z"></path><path d="M14 4 20 10"></path><path d="M4 14h16"></path></svg>
            </button>
            <button id="spreadsheetEditorFreezeTopRow" type="button" class="btn secondary spreadsheetToolBtn spreadsheetFreezeToolBtn" aria-pressed="false" aria-label="${tr("filemgr.spreadsheet_editor.freeze_top_row", null, "Freeze top row")}" title="${tr("filemgr.spreadsheet_editor.freeze_top_row", null, "Freeze top row")}">
              <span aria-hidden="true">1↕</span>
            </button>
            <button id="spreadsheetEditorFreezeFirstColumn" type="button" class="btn secondary spreadsheetToolBtn spreadsheetFreezeToolBtn" aria-pressed="false" aria-label="${tr("filemgr.spreadsheet_editor.freeze_first_column", null, "Freeze first column")}" title="${tr("filemgr.spreadsheet_editor.freeze_first_column", null, "Freeze first column")}">
              <span aria-hidden="true">A↔</span>
            </button>
            <button id="spreadsheetEditorInsertImage" type="button" class="btn secondary spreadsheetToolBtn" aria-label="${tr("filemgr.spreadsheet_editor.insert_image", null, "Insert image")}" title="${tr("filemgr.spreadsheet_editor.insert_image", null, "Insert image")}">
              <svg viewBox="0 0 24 24" aria-hidden="true">
                <rect x="3" y="4" width="18" height="16" rx="2"></rect>
                <circle cx="8.5" cy="9" r="1.5"></circle>
                <path d="m5 18 5-5 3 3 2-2 4 4"></path>
              </svg>
            </button>
            <input id="spreadsheetEditorInsertImageInput" type="file" accept="image/png,image/jpeg" hidden>
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
    insertImageBtn = modal.querySelector("#spreadsheetEditorInsertImage");
    insertImageInput = modal.querySelector("#spreadsheetEditorInsertImageInput");
    boldBtn = modal.querySelector("#spreadsheetEditorBold");
    italicBtn = modal.querySelector("#spreadsheetEditorItalic");
    underlineBtn = modal.querySelector("#spreadsheetEditorUnderline");
    fontNameSelect = modal.querySelector("#spreadsheetEditorFontName");
    fontSizeSelect = modal.querySelector("#spreadsheetEditorFontSize");
    decreaseDecimalsBtn = modal.querySelector("#spreadsheetEditorDecimalsDecrease");
    increaseDecimalsBtn = modal.querySelector("#spreadsheetEditorDecimalsIncrease");
    numberFormatSelect = modal.querySelector("#spreadsheetEditorNumberFormat");
    alignSelect = modal.querySelector("#spreadsheetEditorAlign");
    valignSelect = modal.querySelector("#spreadsheetEditorValign");
    sortSelect = modal.querySelector("#spreadsheetEditorSort");
    alignMenu = modal.querySelector("#spreadsheetEditorAlignMenu");
    valignMenu = modal.querySelector("#spreadsheetEditorValignMenu");
    sortMenu = modal.querySelector("#spreadsheetEditorSortMenu");
    freezeTopRowBtn = modal.querySelector("#spreadsheetEditorFreezeTopRow");
    freezeFirstColumnBtn = modal.querySelector("#spreadsheetEditorFreezeFirstColumn");
    textColorBtn = modal.querySelector("#spreadsheetEditorTextColor");
    fillBtn = modal.querySelector("#spreadsheetEditorFill");
    closeBtn = modal.querySelector("#spreadsheetEditorClose");

    boldBtn?.addEventListener("click", () => applyFormatCommand("bold"));
    italicBtn?.addEventListener("click", () => applyFormatCommand("italic"));
    underlineBtn?.addEventListener("click", () => applyFormatCommand("underline"));
    fontNameSelect?.addEventListener("change", () => {
      applyFormatCommand("fontName", fontNameSelect.value);
    });
    fontSizeSelect?.addEventListener("change", () => applyFormatCommand("fontSize", fontSizeSelect.value));
    decreaseDecimalsBtn?.addEventListener("click", () => applyFormatCommand("decimals", "decrease"));
    increaseDecimalsBtn?.addEventListener("click", () => applyFormatCommand("decimals", "increase"));
    numberFormatSelect?.addEventListener("change", () => applyFormatCommand("numberFormat", numberFormatSelect.value));
    alignSelect?.addEventListener("click", () => toggleToolbarIconMenu(alignSelect, alignMenu));
    valignSelect?.addEventListener("click", () => toggleToolbarIconMenu(valignSelect, valignMenu));
    sortSelect?.addEventListener("click", () => toggleToolbarIconMenu(sortSelect, sortMenu));
    bindToolbarIconMenu(alignMenu, "align", alignSelect);
    bindToolbarIconMenu(valignMenu, "valign", valignSelect);
    bindSpreadsheetSortToolbarMenu();

    freezeTopRowBtn?.addEventListener("click", () => {
      const freeze = currentEditorFreeze();
      setEditorFreezeMode("topRow", !(freeze.topRows > 0));
    });

    freezeFirstColumnBtn?.addEventListener("click", () => {
      const freeze = currentEditorFreeze();
      setEditorFreezeMode("firstColumn", !(freeze.leftCols > 0));
    });
    document.addEventListener("click", (ev) => {
      const target = ev.target;
      if (target && target.closest && target.closest(".spreadsheetIconMenu, .spreadsheetIconMenuBtn")) return;
      closeToolbarIconMenus();
    });
    document.addEventListener("keydown", (ev) => {
      if (ev.key === "Escape") closeToolbarIconMenus();
    });
    window.addEventListener("resize", () => closeToolbarIconMenus());
    window.addEventListener("scroll", () => closeToolbarIconMenus(), true);
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

    insertImageBtn?.addEventListener(
      "click",
      chooseSpreadsheetImage
    );

    insertImageInput?.addEventListener(
      "change",
      () => {
        const file =
          insertImageInput.files &&
          insertImageInput.files[0];

        insertImageInput.value = "";

        if (file) {
          void insertSpreadsheetImageFile(file);
        }
      }
    );

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
      paintSpreadsheetFormulaReferences();
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

    const imageApi =
      FM && FM.spreadsheetXlsxImages;

    if (
      imageApi &&
      typeof imageApi.releaseInsertedImageAssets ===
        "function"
    ) {
      imageApi.releaseInsertedImageAssets();
    }
  }

  function sheetNameKey(value) {
    return String(value || "").trim().toLocaleLowerCase();
  }

  function validateEditorSheetName(value, excludeIndex = -1) {
    const name = String(value || "").trim();

    if (!name || name.length > 31 || /[:\\/?*\[\]]/.test(name)) {
      return {
        ok: false,
        message: tr(
          "filemgr.spreadsheet_editor.sheet_name_invalid",
          null,
          "Sheet name must contain 1–31 characters and cannot contain : \\ / ? * [ ]."
        )
      };
    }

    if (sheetNameKey(name) === sheetNameKey(STYLE_SHEET_NAME)) {
      return {
        ok: false,
        message: tr(
          "filemgr.spreadsheet_editor.sheet_name_reserved",
          null,
          "This sheet name is reserved by DNA-Nexus."
        )
      };
    }

    const duplicate = state.sheets.some((sheet, idx) => (
      idx !== excludeIndex &&
      sheetNameKey(sheet && sheet.name) === sheetNameKey(name)
    ));

    if (duplicate) {
      return {
        ok: false,
        message: tr(
          "filemgr.spreadsheet_editor.sheet_name_duplicate",
          null,
          "A sheet with this name already exists."
        )
      };
    }

    return { ok: true, name };
  }

  function nextEditorSheetName() {
    const used = new Set(state.sheets.map((sheet) => sheetNameKey(sheet && sheet.name)));
    let number = 1;

    while (used.has(sheetNameKey(`Sheet${number}`))) {
      number++;
    }

    return `Sheet${number}`;
  }

  function createBlankEditorSheet(name) {
    return {
      name,
      defaultColWidth: DEFAULT_COL_WIDTH,
      defaultRowHeight: DEFAULT_ROW_HEIGHT,
      rows: Array.from(
        { length: DEFAULT_ROWS },
        () => Array.from({ length: DEFAULT_COLS }, () => "")
      ),
      colWidths: Array.from({ length: DEFAULT_COLS }, () => DEFAULT_COL_WIDTH),
      rowHeights: Array.from({ length: DEFAULT_ROWS }, () => DEFAULT_ROW_HEIGHT),
      cellFormats: Array.from(
        { length: DEFAULT_ROWS },
        () => Array.from({ length: DEFAULT_COLS }, () => null)
      ),
      merges: [],
      freeze: { topRows: 0, leftCols: 0 },
      images: []
    };
  }

  function replaceCaseInsensitiveLiteral(source, needle, replacement) {
    const src = String(source || "");
    const token = String(needle || "");

    if (!token) return src;

    const srcKey = src.toLowerCase();
    const tokenKey = token.toLowerCase();
    const parts = [];
    let cursor = 0;

    while (cursor < src.length) {
      const index = srcKey.indexOf(tokenKey, cursor);
      if (index < 0) break;

      parts.push(src.slice(cursor, index), replacement);
      cursor = index + token.length;
    }

    parts.push(src.slice(cursor));
    return parts.join("");
  }

  function isBareSheetReferenceBoundary(ch) {
    return !ch || !/[A-Za-z0-9_.\]']/.test(ch);
  }

  function replaceBareSheetReferenceLiteral(source, sheetName, replacement) {
    const src = String(source || "");
    const name = String(sheetName || "");
    const token = `${name}!`;

    if (!name) return src;

    const srcKey = src.toLowerCase();
    const tokenKey = token.toLowerCase();
    const parts = [];
    let cursor = 0;
    let searchFrom = 0;

    while (searchFrom < src.length) {
      const index = srcKey.indexOf(tokenKey, searchFrom);
      if (index < 0) break;

      const previous = index > 0 ? src[index - 1] : "";

      if (!isBareSheetReferenceBoundary(previous)) {
        searchFrom = index + 1;
        continue;
      }

      parts.push(src.slice(cursor, index), replacement);
      cursor = index + token.length;
      searchFrom = cursor;
    }

    parts.push(src.slice(cursor));
    return parts.join("");
  }

  function quotedFormulaSheetName(name) {
    return `'${String(name || "").replace(/'/g, "''")}'!`;
  }

  function rewriteFormulaSheetReferencesOutsideStrings(segment, oldName, newName) {
    const quotedOld = quotedFormulaSheetName(oldName);
    const quotedNew = quotedFormulaSheetName(newName);

    // Security: sheet names are user-controlled. Use literal scanning instead
    // of constructing RegExp objects, avoiding ReDoS on the browser main thread.
    const withQuotedReferences = replaceCaseInsensitiveLiteral(
      segment,
      quotedOld,
      quotedNew
    );

    return replaceBareSheetReferenceLiteral(
      withQuotedReferences,
      oldName,
      quotedNew
    );
  }

  function rewriteFormulaSheetReferences(raw, oldName, newName) {
    const formula = String(raw == null ? "" : raw);
    if (!formula.startsWith("=") || oldName === newName) return formula;

    let out = "";
    let segmentStart = 0;
    let inString = false;

    for (let i = 0; i < formula.length; i++) {
      if (formula[i] !== '"') continue;

      if (inString && formula[i + 1] === '"') {
        i++;
        continue;
      }

      if (!inString) {
        out += rewriteFormulaSheetReferencesOutsideStrings(
          formula.slice(segmentStart, i),
          oldName,
          newName
        );
        segmentStart = i;
        inString = true;
      } else {
        out += formula.slice(segmentStart, i + 1);
        segmentStart = i + 1;
        inString = false;
      }
    }

    const tail = formula.slice(segmentStart);
    out += inString
      ? tail
      : rewriteFormulaSheetReferencesOutsideStrings(tail, oldName, newName);

    return out;
  }

  function rewriteWorkbookSheetReferences(oldName, newName) {
    for (const sheet of state.sheets) {
      if (!sheet || !Array.isArray(sheet.rows)) continue;

      for (const row of sheet.rows) {
        if (!Array.isArray(row)) continue;

        for (let col = 0; col < row.length; col++) {
          const raw = row[col];
          const next = rewriteFormulaSheetReferences(raw, oldName, newName);
          if (next !== raw) row[col] = next;
        }
      }
    }
  }

  function rewriteDeletedSheetReferencesOutsideStrings(segment, deletedName) {
    const quotedOld = quotedFormulaSheetName(deletedName);

    // Security: avoid compiling a user-controlled worksheet name as a regular
    // expression. Literal scanning also prevents browser main-thread ReDoS.
    const withQuotedReferences = replaceCaseInsensitiveLiteral(
      segment,
      quotedOld,
      "#REF!"
    );

    // Excel converts references to a deleted worksheet into #REF!.
    return replaceBareSheetReferenceLiteral(
      withQuotedReferences,
      deletedName,
      "#REF!"
    );
  }

  function rewriteDeletedSheetReferences(raw, deletedName) {
    const formula = String(raw == null ? "" : raw);
    if (!formula.startsWith("=")) return formula;

    let out = "";
    let segmentStart = 0;
    let inString = false;

    for (let i = 0; i < formula.length; i++) {
      if (formula[i] !== '"') continue;

      if (inString && formula[i + 1] === '"') {
        i++;
        continue;
      }

      if (!inString) {
        out += rewriteDeletedSheetReferencesOutsideStrings(
          formula.slice(segmentStart, i),
          deletedName
        );
        segmentStart = i;
        inString = true;
      } else {
        out += formula.slice(segmentStart, i + 1);
        segmentStart = i + 1;
        inString = false;
      }
    }

    const tail = formula.slice(segmentStart);
    out += inString
      ? tail
      : rewriteDeletedSheetReferencesOutsideStrings(tail, deletedName);

    return out;
  }

  function rewriteWorkbookDeletedSheetReferences(deletedName) {
    for (const sheet of state.sheets) {
      if (!sheet || !Array.isArray(sheet.rows)) continue;

      for (const row of sheet.rows) {
        if (!Array.isArray(row)) continue;

        for (let col = 0; col < row.length; col++) {
          const raw = row[col];
          const next = rewriteDeletedSheetReferences(raw, deletedName);
          if (next !== raw) row[col] = next;
        }
      }
    }
  }

  function addEditorSheet() {
    if (state.readOnly || state.tooLarge || state.saving) return;

    const before = captureHistorySnapshot();
    const name = nextEditorSheetName();

    state.sheets.push(createBlankEditorSheet(name));
    state.active = state.sheets.length - 1;
    state.selection = null;
    state.activeCell = null;
    state.rangeSelection = null;
    state.selectedImageId = "";

    commitHistorySnapshot(before);
    render();
  }

  function beginEditorSheetRename(index, button) {
    if (
      state.readOnly ||
      state.tooLarge ||
      state.saving ||
      !tabsEl ||
      !button ||
      !button.isConnected
    ) {
      return;
    }

    const sheet = state.sheets[index];
    if (!sheet) return;

    const oldName = String(sheet.name || `Sheet${index + 1}`);
    const input = document.createElement("input");
    input.type = "text";
    input.className = "spreadsheetEditorTabRename";
    input.value = oldName;
    input.maxLength = 31;
    input.autocomplete = "off";
    input.spellcheck = false;
    input.setAttribute(
      "aria-label",
      tr("filemgr.spreadsheet_editor.rename_sheet", null, "Rename sheet")
    );

    const currentWidth = Math.ceil(button.getBoundingClientRect().width);
    if (currentWidth > 0) {
      input.style.width = `${Math.max(120, currentWidth)}px`;
    }

    let finished = false;

    const cancel = () => {
      if (finished) return;
      finished = true;
      renderTabs();
    };

    const commit = () => {
      if (finished) return;

      const checked = validateEditorSheetName(input.value, index);
      if (!checked.ok) {
        setStatus(checked.message, "err");
        window.requestAnimationFrame(() => {
          input.focus();
          input.select();
        });
        return;
      }

      const newName = checked.name;
      finished = true;

      if (newName === oldName) {
        renderTabs();
        return;
      }

      const before = captureHistorySnapshot();
      rewriteWorkbookSheetReferences(oldName, newName);
      sheet.name = newName;
      commitHistorySnapshot(before);
      render();
    };

    input.addEventListener("keydown", (ev) => {
      ev.stopPropagation();

      if (ev.key === "Enter") {
        ev.preventDefault();
        commit();
      } else if (ev.key === "Escape") {
        ev.preventDefault();
        cancel();
      }
    });

    input.addEventListener("click", (ev) => ev.stopPropagation());
    input.addEventListener("blur", commit);

    tabsEl.replaceChild(input, button);
    input.focus();
    input.select();
  }


  function confirmDeleteEditorSheet(sheetName) {
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
          ev.stopPropagation();
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
      box.setAttribute("aria-labelledby", "spreadsheetEditorDeleteSheetTitle");
      box.setAttribute("aria-describedby", "spreadsheetEditorDeleteSheetText");

      const title = document.createElement("div");
      title.id = "spreadsheetEditorDeleteSheetTitle";
      title.className = "spreadsheetEditorConfirmTitle";
      title.textContent = tr(
        "filemgr.spreadsheet_editor.delete_sheet_title",
        null,
        "Delete sheet?"
      );

      const msg = document.createElement("div");
      msg.id = "spreadsheetEditorDeleteSheetText";
      msg.className = "spreadsheetEditorConfirmText";
      msg.textContent = tr(
        "filemgr.spreadsheet_editor.delete_sheet_message",
        { name: sheetName },
        `Sheet “${sheetName}” and all its contents will be deleted.`
      );

      const actions = document.createElement("div");
      actions.className = "spreadsheetEditorConfirmActions";

      const cancelBtn = document.createElement("button");
      cancelBtn.type = "button";
      cancelBtn.className = "btn secondary";
      cancelBtn.textContent = tr("common.cancel", null, "Cancel");

      const deleteBtn = document.createElement("button");
      deleteBtn.type = "button";
      deleteBtn.className = "btn";
      deleteBtn.textContent = tr(
        "filemgr.spreadsheet_editor.delete_sheet_confirm",
        null,
        "Delete"
      );

      actions.appendChild(cancelBtn);
      actions.appendChild(deleteBtn);
      box.appendChild(title);
      box.appendChild(msg);
      box.appendChild(actions);
      confirmModal.appendChild(box);

      confirmModal.addEventListener("click", (ev) => {
        if (ev.target === confirmModal) finish(false);
      });

      cancelBtn.addEventListener("click", () => finish(false));
      deleteBtn.addEventListener("click", () => finish(true));

      document.addEventListener("keydown", onKeyDown);
      document.body.appendChild(confirmModal);

      window.setTimeout(() => cancelBtn.focus(), 0);
    });
  }

  async function deleteEditorSheet(index) {
    if (
      state.readOnly ||
      state.tooLarge ||
      state.saving ||
      !Number.isInteger(index) ||
      index < 0 ||
      index >= state.sheets.length
    ) {
      return;
    }

    if (state.sheets.length <= 1) {
      setStatus(
        tr(
          "filemgr.spreadsheet_editor.delete_last_sheet",
          null,
          "The last remaining sheet cannot be deleted."
        ),
        "err"
      );
      return;
    }

    const sheet = state.sheets[index];
    if (!sheet) return;

    const sheetName = String(sheet.name || `Sheet${index + 1}`);
    const confirmed = await confirmDeleteEditorSheet(sheetName);

    if (!confirmed) return;

    // Recheck after the asynchronous confirmation in case editor state changed.
    if (
      state.readOnly ||
      state.tooLarge ||
      state.saving ||
      state.sheets.length <= 1 ||
      state.sheets[index] !== sheet
    ) {
      return;
    }

    const before = captureHistorySnapshot();
    const previousActive = state.active;

    rewriteWorkbookDeletedSheetReferences(sheetName);
    state.sheets.splice(index, 1);

    if (previousActive > index) {
      state.active = previousActive - 1;
    } else if (previousActive === index) {
      state.active = Math.min(index, state.sheets.length - 1);
      state.selection = null;
      state.activeCell = null;
      state.rangeSelection = null;
      state.selectedImageId = "";
      formulaFocus = null;
    }

    commitHistorySnapshot(before);
    render();
  }

  function hideSheetTabMenu() {
    if (!sheetTabMenu || sheetTabMenu.hidden) return false;

    sheetTabMenu.hidden = true;
    sheetTabMenu.replaceChildren();
    return true;
  }

  function ensureSheetTabMenu() {
    if (sheetTabMenu) return sheetTabMenu;

    sheetTabMenu = document.createElement("div");
    sheetTabMenu.className = "spreadsheetSheetMenu";
    sheetTabMenu.setAttribute("role", "menu");
    sheetTabMenu.hidden = true;

    document.body.appendChild(sheetTabMenu);

    document.addEventListener("pointerdown", (ev) => {
      if (
        sheetTabMenu &&
        !sheetTabMenu.hidden &&
        !sheetTabMenu.contains(ev.target)
      ) {
        hideSheetTabMenu();
      }
    });

    document.addEventListener("keydown", (ev) => {
      if (ev.key === "Escape" && hideSheetTabMenu()) {
        ev.preventDefault();
        ev.stopPropagation();
      }
    });

    window.addEventListener("resize", hideSheetTabMenu);
    window.addEventListener("scroll", hideSheetTabMenu, true);

    return sheetTabMenu;
  }

  function sheetMenuButton(label, action, disabled = false, title = "") {
    const button = document.createElement("button");
    button.type = "button";
    button.setAttribute("role", "menuitem");
    button.textContent = label;
    button.disabled = !!disabled;

    if (title) button.title = title;

    button.addEventListener("click", () => {
      if (button.disabled) return;
      hideSheetTabMenu();
      action();
    });

    return button;
  }

  function openSheetTabMenu(index, button, x, y) {
    if (
      !Number.isInteger(index) ||
      index < 0 ||
      index >= state.sheets.length ||
      !button
    ) {
      return;
    }

    hideAxisMenu();
    hideTextColorMenu();
    hideFillMenu();
    hideBorderMenu();

    const menu = ensureSheetTabMenu();
    const disabled = state.readOnly || state.tooLarge || state.saving;
    const lastSheet = state.sheets.length <= 1;

    menu.replaceChildren();

    menu.appendChild(
      sheetMenuButton(
        tr(
          "filemgr.spreadsheet_editor.rename_sheet",
          null,
          "Rename sheet"
        ),
        () => beginEditorSheetRename(index, button),
        disabled
      )
    );

    menu.appendChild(
      sheetMenuButton(
        tr(
          "filemgr.spreadsheet_editor.delete_sheet",
          null,
          "Delete sheet"
        ),
        () => {
          void deleteEditorSheet(index);
        },
        disabled || lastSheet,
        lastSheet
          ? tr(
              "filemgr.spreadsheet_editor.delete_last_sheet",
              null,
              "The last remaining sheet cannot be deleted."
            )
          : ""
      )
    );

    menu.hidden = false;

    const requestedLeft = Math.max(8, Number(x) || 8);
    const requestedTop = Math.max(8, Number(y) || 8);

    menu.style.left = `${requestedLeft}px`;
    menu.style.top = `${requestedTop}px`;

    const rect = menu.getBoundingClientRect();
    const maxLeft = Math.max(8, window.innerWidth - rect.width - 8);
    const maxTop = Math.max(8, window.innerHeight - rect.height - 8);

    menu.style.left = `${Math.min(requestedLeft, maxLeft)}px`;
    menu.style.top = `${Math.min(requestedTop, maxTop)}px`;

    const firstEnabled = menu.querySelector("button:not(:disabled)");
    window.setTimeout(() => firstEnabled?.focus(), 0);
  }

  function renderTabs() {
    if (!tabsEl) return;
    hideSheetTabMenu();
    tabsEl.replaceChildren();

    state.sheets.forEach((sheet, idx) => {
      const btn = document.createElement("button");
      btn.type = "button";
      btn.className = "spreadsheetEditorTab" + (idx === state.active ? " active" : "");
      btn.textContent = sheet.name || `Sheet ${idx + 1}`;
      btn.title = tr(
        "filemgr.spreadsheet_editor.rename_sheet",
        null,
        "Rename sheet"
      );

      btn.addEventListener("click", () => {
        if (state.active === idx) return;
        state.active = idx;
        render();
      });

      btn.addEventListener("dblclick", (ev) => {
        ev.preventDefault();
        ev.stopPropagation();
        beginEditorSheetRename(idx, btn);
      });

      btn.addEventListener("contextmenu", (ev) => {
        ev.preventDefault();
        ev.stopPropagation();
        openSheetTabMenu(idx, btn, ev.clientX, ev.clientY);
      });

      btn.addEventListener("keydown", (ev) => {
        if (ev.key !== "F2") return;
        ev.preventDefault();
        ev.stopPropagation();
        beginEditorSheetRename(idx, btn);
      });

      tabsEl.appendChild(btn);
    });

    const addBtn = document.createElement("button");
    addBtn.type = "button";
    addBtn.className = "spreadsheetEditorTab spreadsheetEditorTabAdd";
    addBtn.textContent = "+";
    addBtn.disabled = state.readOnly || state.tooLarge || state.saving;
    addBtn.setAttribute(
      "aria-label",
      tr("filemgr.spreadsheet_editor.add_sheet", null, "Add sheet")
    );
    addBtn.title = tr(
      "filemgr.spreadsheet_editor.add_sheet",
      null,
      "Add sheet"
    );
    addBtn.addEventListener("click", addEditorSheet);
    tabsEl.appendChild(addBtn);
  }


  function currentEditorFreeze() {
    const sheet = state.sheets[state.active];
    return normalizeSheetFreeze(sheet && sheet.freeze);
  }

  function updateFreezeToolbarButtons() {
    const freeze = currentEditorFreeze();

    if (freezeTopRowBtn) {
      const active = freeze.topRows > 0;
      freezeTopRowBtn.setAttribute("aria-pressed", active ? "true" : "false");
      freezeTopRowBtn.classList.toggle("active", active);
    }

    if (freezeFirstColumnBtn) {
      const active = freeze.leftCols > 0;
      freezeFirstColumnBtn.setAttribute("aria-pressed", active ? "true" : "false");
      freezeFirstColumnBtn.classList.toggle("active", active);
    }
  }

  function setEditorFreezeMode(kind, enabled) {
    const sheet = state.sheets[state.active];
    if (!sheet) return;

    const before = captureHistorySnapshot();
    const freeze = normalizeSheetFreeze(sheet.freeze);

    if (kind === "topRow") {
      freeze.topRows = enabled ? 1 : 0;
    } else if (kind === "firstColumn") {
      freeze.leftCols = enabled ? 1 : 0;
    } else {
      return;
    }

    sheet.freeze = freeze;
    commitHistorySnapshot(before);
    state.dirty = true;

    updateButtons();
    render();
  }

  function applyEditorFreezeMode(table, sheet) {
    if (!table) return;

    const freeze = normalizeSheetFreeze(sheet && sheet.freeze);
    table.toggleAttribute("data-spreadsheet-freeze-top-row", freeze.topRows > 0);
    table.toggleAttribute("data-spreadsheet-freeze-first-column", freeze.leftCols > 0);
  }

  function refreshEditorFreezeOffsets(table) {
    if (!table) return;

    const corner = table.querySelector("thead th.corner");
    const firstRowHead = table.querySelector("tbody th.rowHead");

    const topOffset = Math.ceil(corner ? corner.getBoundingClientRect().height : 24);
    const leftOffset = Math.ceil(firstRowHead ? firstRowHead.getBoundingClientRect().width : 44);

    table.style.setProperty("--spreadsheet-editor-freeze-top-offset", `${topOffset}px`);
    table.style.setProperty("--spreadsheet-editor-freeze-left-offset", `${leftOffset}px`);
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
    applyEditorFreezeMode(table, sheet);
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

      /*
       * Pin the actual table row to the workbook geometry. Cell and input
       * heights alone are interpreted as minimums by the table layout.
       */
      applyRowHeight(
        trEl,
        rowHeights[rIdx]
      );

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
        const merge = mergeAtCell(sheet, rIdx, c);
        if (merge && !mergeIsAnchorCell(merge, rIdx, c)) continue;

        const colSpan = merge ? Math.max(1, merge.e.c - merge.s.c + 1) : 1;
        const rowSpan = merge ? Math.max(1, merge.e.r - merge.s.r + 1) : 1;
        const cellWidth = merge ? mergeColumnPixelWidth(colWidths, merge.s.c, merge.e.c) : colWidths[c];
        const cellHeight = merge ? mergeRowPixelHeight(rowHeights, merge.s.r, merge.e.r) : rowHeights[rIdx];

        const td = document.createElement("td");
        td.dataset.row = String(rIdx);
        td.dataset.col = String(c);
        if (colSpan > 1) td.colSpan = colSpan;
        if (rowSpan > 1) td.rowSpan = rowSpan;
        applyColumnWidth(td, cellWidth);
        applyRowHeight(td, cellHeight);

        const input = document.createElement("input");

        input.type = "text";
        input.value = displayCellValue(sheet, rIdx, c, cache);
        input.dataset.row = String(rIdx);
        input.dataset.col = String(c);
        applyColumnWidth(input, cellWidth);
        applyRowHeight(input, cellHeight);
        input.title = isFormulaValue(cellRaw(sheet, rIdx, c)) ? cellRaw(sheet, rIdx, c) : "";
        input.disabled = state.readOnly || state.tooLarge;

        /*
         * Formula cells initially behave as selectable
         * result cells. Explicit double-click, F2 or the
         * formula bar enables raw formula editing.
         */
        input.readOnly = isFormulaValue(
          cellRaw(sheet, rIdx, c)
        );

        input.addEventListener("pointerdown", (ev) => {
          if (!formulaFocus || formulaFocus.input === input) return;
          const active = formulaFocus.input;
          if (!active || !isFormulaValue(active.value)) return;
          ev.preventDefault();
          insertFormulaReference(active, rIdx, c);
        });

        input.addEventListener("pointerdown", (ev) => {
          if (ev.button === 2 && formatTargetsContainCell(rIdx, c)) {
            // Right-clicking inside an existing selection should open the cell
            // context menu without collapsing the selected merge/border target.
            preserveSelectionForContextMenu = { row: rIdx, col: c };
            return;
          }

          beginCellRangePointer(ev, rIdx, c);
        });

        input.addEventListener("focus", () => {
          const r = Number(input.dataset.row);
          const col = Number(input.dataset.col);
          const preserveContextSelection =
            preserveSelectionForContextMenu &&
            preserveSelectionForContextMenu.row === r &&
            preserveSelectionForContextMenu.col === col &&
            formatTargetsContainCell(r, col);

          if (!preserveContextSelection) {
            state.activeCell = Number.isInteger(r) && Number.isInteger(col) ? { row: r, col } : null;

            if (state.selection || state.rangeSelection) {
              state.selection = null;
              state.rangeSelection = null;
            }
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

          /*
           * UX correctness: a single click selects the cell
           * and keeps its calculated result visible. The raw
           * value remains available in the formula bar.
           */
          input.removeAttribute(
            "data-spreadsheet-formula-editing"
          );
          input.readOnly =
            isFormulaValue(raw);
          input.value =
            displayCellValue(
              sheet,
              r,
              col
            );

          if (
            formulaFocus &&
            formulaFocus.input === input
          ) {
            formulaFocus = null;
          }
        });

        input.addEventListener("dblclick", (ev) => {
          const r = Number(
            input.dataset.row
          );
          const col = Number(
            input.dataset.col
          );

          if (
            !Number.isInteger(r) ||
            !Number.isInteger(col) ||
            !isFormulaValue(
              cellRaw(sheet, r, col)
            )
          ) {
            return;
          }

          ev.preventDefault();
          ev.stopPropagation();

          beginSpreadsheetFormulaCellEdit(
            input
          );
        });

        input.addEventListener("blur", () => {
          const r = Number(input.dataset.row);
          const col = Number(input.dataset.col);
          if (!Number.isInteger(r) || !Number.isInteger(col)) return;
          if (
            formulaFocus &&
            formulaFocus.input === input
          ) {
            formulaFocus = null;
          }

          input.removeAttribute(
            "data-spreadsheet-formula-editing"
          );

          commitPendingCellEditHistory(
            input,
            r,
            col
          );
          refreshFormulaDisplays(null);

          const raw = cellRaw(
            sheet,
            r,
            col
          );

          input.readOnly =
            isFormulaValue(raw);
          input.value =
            displayCellValue(
              sheet,
              r,
              col
            );
          input.title =
            isFormulaValue(raw)
              ? raw
              : "";
          refreshVisibleEditorTextOverflows();
          updateFormulaBar();
        });

        input.addEventListener("keydown", (ev) => {
          if (ev.key === "F2") {
            const started =
              beginSpreadsheetFormulaCellEdit(
                input
              );

            if (started) {
              ev.preventDefault();
              ev.stopPropagation();
              return;
            }
          }

          if (ev.key === "Escape") {
            const r = Number(input.dataset.row);
            const col = Number(input.dataset.col);
            const raw = Number.isInteger(r) && Number.isInteger(col) ? cellRaw(sheet, r, col) : "";
            const editingFormula =
              spreadsheetFormulaCellIsEditing(
                input
              ) &&
              (
                (
                  formulaFocus &&
                  formulaFocus.input === input
                ) ||
                isFormulaValue(
                  normalizeSpreadsheetUserInput(
                    input.value,
                    raw
                  )
                )
              );

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

          const nextRaw = normalizeSpreadsheetUserInput(input.value, previousRaw);

          if (previousRaw !== nextRaw) {
            beginPendingCellEditHistory(input, r, col, previousRaw);
          }

          setCellRaw(state.sheets[state.active], r, col, nextRaw);

          if (isFormulaValue(nextRaw)) {
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
          refreshVisibleEditorTextOverflows();
          updateFormulaBar();
        });

        // Security: cell content is edited through input.value and never
        // injected as HTML, so workbook text cannot become executable markup.
        input.addEventListener("contextmenu", (ev) => {
          openCellBorderMenu(ev, input);
          preserveSelectionForContextMenu = null;
        });

        td.appendChild(input);
        applyCellFormatToInput(input, getCellFormat(sheet, rIdx, c));
        renderEditorCellTextOverflow(td, input, sheet, rows, rIdx, c, colWidths, colCount, cache);
        td.appendChild(createSpreadsheetFillHandle(rIdx, c));

        trEl.appendChild(td);
      }

      tbody.appendChild(trEl);
    });

    table.appendChild(tbody);

    const selectAllCorner = table.querySelector("thead tr:first-child th:first-child");
    configureSpreadsheetSelectAllCorner(selectAllCorner);

    const surface = document.createElement("div");
    surface.className = "spreadsheetSheetSurface";
    surface.appendChild(table);
    bodyEl.appendChild(surface);
    refreshEditorFreezeOffsets(table);
    requestAnimationFrame(() => refreshEditorFreezeOffsets(table));
    updateFreezeToolbarButtons();

    const overlayApi =
      FM && FM.spreadsheetImageOverlay;

    const imageGeometryApi =
      FM && FM.spreadsheetXlsxImages;

    let imageTransformBefore = null;

    if (overlayApi && typeof overlayApi.render === "function") {
      overlayApi.render(surface, table, sheet, {
        defaultColWidth: sheetDefaultColumnWidth(sheet),
        defaultRowHeight: sheetDefaultRowHeight(sheet),
        selectable: true,
        selectedImageId: state.selectedImageId,
        onSelect: (imageId) => {
          state.selectedImageId = String(imageId || "");
          state.selection = null;
          state.rangeSelection = null;
          state.activeCell = null;
          formulaFocus = null;

          repaintSpreadsheetSelection();
          updateFormulaBar(true);
          updateFormatToolbar();

          if (typeof overlayApi.select === "function") {
            overlayApi.select(surface, state.selectedImageId);
          }
        },

        onTransformStart: () => {
          imageTransformBefore =
            captureHistorySnapshot();
        },

        onTransformCancel: () => {
          imageTransformBefore = null;
        },

        onTransformCommit: (
          imageId,
          image,
          rect,
          geometry
        ) => {
          const before = imageTransformBefore;
          imageTransformBefore = null;

          if (
            !before ||
            state.sheets[state.active] !== sheet ||
            !imageGeometryApi ||
            typeof imageGeometryApi.applyImagePixelRect !==
              "function"
          ) {
            render();
            return;
          }

          const changed =
            imageGeometryApi.applyImagePixelRect(
              image,
              sheet,
              rect,
              {
                ...geometry,
                imageId
              }
            );

          if (!changed) {
            render();
            return;
          }

          state.selectedImageId = String(
            imageId ||
            image.id ||
            ""
          );

          commitHistorySnapshot(before);
          render();
        }
      });
    }

    table.addEventListener("pointerdown", () => {
      if (!state.selectedImageId) return;

      state.selectedImageId = "";
      if (overlayApi && typeof overlayApi.select === "function") {
        overlayApi.select(surface, "");
      }
    });

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


  function adjustMergeIntervalForInsert(start, end, index, count) {
    if (index <= start) {
      return { start: start + count, end: end + count };
    }

    if (index <= end) {
      return { start, end: end + count };
    }

    return { start, end };
  }

  function adjustMergeIntervalForDelete(start, end, index, count) {
    const deleteEnd = index + count - 1;

    if (end < index) {
      return { start, end };
    }

    if (start > deleteEnd) {
      return { start: start - count, end: end - count };
    }

    if (index <= start && deleteEnd >= end) {
      return null;
    }

    if (index <= start) {
      return { start: index, end: end - count };
    }

    if (deleteEnd >= end) {
      return { start, end: index - 1 };
    }

    return { start, end: end - count };
  }

  function adjustMergeRangeForAxisChange(merge, axis, index, count, action) {
    const m = normalizeMergeRange(merge);
    if (!m) return null;

    const change =
      action === "insert" ? adjustMergeIntervalForInsert :
      action === "delete" ? adjustMergeIntervalForDelete :
      null;

    if (!change) return m;

    if (axis === "row") {
      const rows = change(m.s.r, m.e.r, index, count);
      if (!rows) return null;

      return normalizeMergeRange({
        s: { r: rows.start, c: m.s.c },
        e: { r: rows.end, c: m.e.c }
      });
    }

    if (axis === "column") {
      const cols = change(m.s.c, m.e.c, index, count);
      if (!cols) return null;

      return normalizeMergeRange({
        s: { r: m.s.r, c: cols.start },
        e: { r: m.e.r, c: cols.end }
      });
    }

    return m;
  }

  function adjustSheetMergesForAxisChange(sheet, axis, index, count, action) {
    if (!sheet || (axis !== "row" && axis !== "column")) return;
    if (!Number.isInteger(index) || index < 0) return;
    if (!Number.isInteger(count) || count <= 0) return;

    const adjusted = [];

    for (const merge of ensureSheetMerges(sheet)) {
      const next = adjustMergeRangeForAxisChange(merge, axis, index, count, action);
      if (!next) continue;

      // Keep the merge model valid after structural edits. 1x1 ranges are
      // intentionally dropped so export never writes invalid/useless merges.
      if (!adjusted.some((existing) => mergeRangesOverlap(existing, next))) {
        adjusted.push(next);
      }
    }

    sheet.merges = adjusted;
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
    adjustSheetMergesForAxisChange(sheet, "row", insertAt, count, "insert");

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
    adjustSheetMergesForAxisChange(sheet, "column", insertAt, count, "insert");

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
      adjustSheetMergesForAxisChange(sheet, "row", deleteAt, count, "delete");

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
      adjustSheetMergesForAxisChange(sheet, "column", deleteAt, count, "delete");

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
    state.selectedImageId = "";
    state.workbookFont = null;
    state.workbookImageInfo = null;
    state.workbookImageWarning = "";

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

      if (state.workbookImageWarning) {
        // Restore workbook image warning after render's normal ready status.
        setStatus(state.workbookImageWarning, "warn");
      }
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
