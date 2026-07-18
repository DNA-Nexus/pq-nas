window.PQNAS_FILEMGR = window.PQNAS_FILEMGR || {};

(() => {
  "use strict";

  const FM = window.PQNAS_FILEMGR;
  const previousOfficePreview = FM.officePreview || null;

  const SPREADSHEET_EXTS = new Set(["csv", "tsv", "xls", "xlsx", "ods"]);
  const MAX_RENDER_ROWS = 1000;
  const MAX_RENDER_COLS = 80;
  const XLSX_VENDOR_URL = "./vendor/xlsx.full.min.js";
  const DEFAULT_PREVIEW_COL_WIDTH = 96;
  const MIN_PREVIEW_COL_WIDTH = 1;
  const MAX_PREVIEW_COL_WIDTH = 520;
  const DEFAULT_PREVIEW_ROW_HEIGHT = 22;
  const MIN_PREVIEW_ROW_HEIGHT = 1;
  const MAX_PREVIEW_ROW_HEIGHT = 260;

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
  let spreadsheetImageLoadPromise = null;

  const STYLE_SHEET_NAME = "_pqnas_styles";
  const STYLE_META_VERSION = "pqnas-spreadsheet-style-v1";
  const STYLE_META_CHUNK_SIZE = 30000;

  // Document content colors used by the spreadsheet editor metadata. These are
  // spreadsheet values, not DNA-Nexus UI theme colors.
  const PREVIEW_FILL_COLORS = Object.freeze({
    yellow: "rgb(255, 242, 204)",
    green: "rgb(217, 234, 211)",
    blue: "rgb(207, 226, 243)",
    red: "rgb(244, 204, 204)",
    gray: "rgb(217, 217, 217)"
  });

  const PREVIEW_TEXT_COLORS = Object.freeze({
    black: "rgb(0, 0, 0)",
    red: "rgb(204, 0, 0)",
    green: "rgb(56, 118, 29)",
    blue: "rgb(17, 85, 204)",
    gray: "rgb(102, 102, 102)",
    white: "rgb(255, 255, 255)"
  });

  const PREVIEW_FONT_SIZE_OPTIONS = Object.freeze([10, 12, 14, 16, 18, 24, 32]);
  const PREVIEW_BORDER_STYLE_KEYS = Object.freeze(["thin", "medium", "double"]);
  const PREVIEW_BORDER_SIDES = Object.freeze(["top", "right", "bottom", "left"]);
  const PREVIEW_MIN_DECIMAL_PLACES = 0;
  const PREVIEW_MAX_DECIMAL_PLACES = 10;

  const PREVIEW_CURRENCY_FORMATS = Object.freeze({
    eur: { prefix: "", suffix: " €" },
    usd: { prefix: "$", suffix: "" },
    gbp: { prefix: "£", suffix: "" },
    sek: { prefix: "", suffix: " kr" },
    cny: { prefix: "¥", suffix: "" }
  });
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
      loadStyleOnce("./spreadsheet_edit.css?v=spreadsheet-border-submenu-1", "data-pqnas-spreadsheet-edit-css"),
      loadScriptOnce("./spreadsheet_axis.js?v=spreadsheet-decimal-format-1", "data-pqnas-spreadsheet-axis-js"),
      loadScriptOnce("./spreadsheet_history.js?v=spreadsheet-decimal-format-1", "data-pqnas-spreadsheet-history-js"),
      loadScriptOnce("./spreadsheet_fonts.js?v=spreadsheet-font-family-1", "data-pqnas-spreadsheet-fonts-js"),
      loadScriptOnce("./spreadsheet_xlsx_dimensions.js?v=spreadsheet-column-layout-1", "data-pqnas-spreadsheet-xlsx-dimensions-js"),
      loadScriptOnce("./spreadsheet_xlsx_borders.js?v=spreadsheet-xlsx-borders-1", "data-pqnas-spreadsheet-xlsx-borders-js"),
      loadScriptOnce("./spreadsheet_border_menu.js?v=spreadsheet-border-submenu-1", "data-pqnas-spreadsheet-border-menu-js"),
      loadScriptOnce("./spreadsheet_xlsx_images.js?v=spreadsheet-image-delete-1", "data-pqnas-spreadsheet-xlsx-images-js"),
      loadScriptOnce("./spreadsheet_image_overlay.js?v=spreadsheet-image-resize-1", "data-pqnas-spreadsheet-image-overlay-js")
    ]).then(() => {
      return loadScriptOnce("./spreadsheet_edit.js?v=spreadsheet-border-submenu-1", "data-pqnas-spreadsheet-edit-js");
    }).then(() => {
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

  function ensureSpreadsheetImageModules() {
    if (
      FM &&
      FM.spreadsheetXlsxDimensions &&
      FM.spreadsheetXlsxBorders &&
      FM.spreadsheetXlsxImages &&
      FM.spreadsheetImageOverlay &&
      typeof FM.spreadsheetXlsxDimensions.worksheetDefaults === "function" &&
      typeof FM.spreadsheetXlsxBorders.bordersBySheet === "function" &&
      typeof FM.spreadsheetXlsxImages.imagesFromWorkbookFiles === "function" &&
      typeof FM.spreadsheetImageOverlay.render === "function"
    ) {
      return Promise.resolve();
    }

    if (spreadsheetImageLoadPromise) return spreadsheetImageLoadPromise;

    spreadsheetImageLoadPromise = Promise.all([
      loadStyleOnce("./spreadsheet_edit.css?v=spreadsheet-border-submenu-1", "data-pqnas-spreadsheet-edit-css"),
      loadScriptOnce("./spreadsheet_fonts.js?v=spreadsheet-font-family-1", "data-pqnas-spreadsheet-fonts-js"),

      loadScriptOnce("./spreadsheet_xlsx_dimensions.js?v=spreadsheet-column-layout-1", "data-pqnas-spreadsheet-xlsx-dimensions-js"),
      loadScriptOnce("./spreadsheet_xlsx_borders.js?v=spreadsheet-xlsx-borders-1", "data-pqnas-spreadsheet-xlsx-borders-js"),
      loadScriptOnce("./spreadsheet_xlsx_images.js?v=spreadsheet-image-delete-1", "data-pqnas-spreadsheet-xlsx-images-js"),
      loadScriptOnce("./spreadsheet_image_overlay.js?v=spreadsheet-image-resize-1", "data-pqnas-spreadsheet-image-overlay-js")
    ]);

    return spreadsheetImageLoadPromise;
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

  function safePreviewSheetName(name, idx) {
    const raw = String(name || `Sheet${idx + 1}`).trim() || `Sheet${idx + 1}`;
    return raw.replace(/[:\\/?*\[\]]/g, "-").slice(0, 31) || `Sheet${idx + 1}`;
  }

  function previewCssRgbToArgb(value) {
    const m = String(value || "").match(/rgba?\s*\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)/i);
    if (!m) return "";

    const parts = [m[1], m[2], m[3]].map((part) => {
      const n = Math.max(0, Math.min(255, Number(part) || 0));
      return n.toString(16).padStart(2, "0").toUpperCase();
    });

    return `FF${parts.join("")}`;
  }

  function previewColorKeyFromRgb(value, palette) {
    const raw = String(value || "").replace(/^#/, "").toUpperCase();
    const normalized = raw.length === 6 ? `FF${raw}` : raw;

    for (const [key, cssValue] of Object.entries(palette || {})) {
      if (previewCssRgbToArgb(cssValue) === normalized) return key;
    }

    return "";
  }

  function previewFillKeyFromRgb(value) {
    return previewColorKeyFromRgb(value, PREVIEW_FILL_COLORS);
  }

  function previewTextKeyFromRgb(value) {
    return previewColorKeyFromRgb(value, PREVIEW_TEXT_COLORS);
  }

  function normalizePreviewFontSize(value) {
    const n = Number(value);
    if (!Number.isFinite(n)) return 0;
    const rounded = Math.round(n);
    return PREVIEW_FONT_SIZE_OPTIONS.includes(rounded) ? rounded : 0;
  }

  function normalizePreviewBorderSide(value) {
    const api = FM && FM.spreadsheetXlsxBorders;

    if (api && typeof api.normalizeSide === "function") {
      return api.normalizeSide(value);
    }

    const key = String(value || "").trim().toLowerCase();
    if (key === "thick") return "medium";
    return PREVIEW_BORDER_STYLE_KEYS.includes(key) ? key : "";
  }

  function normalizePreviewVerticalAlign(value) {
    const key = String(value || "").trim().toLowerCase();
    if (key === "center") return "middle";
    return key === "top" || key === "middle" || key === "bottom" ? key : "";
  }

  function previewVerticalAlignCss(value) {
    const key = normalizePreviewVerticalAlign(value);
    return key === "top" ? "top" : key === "bottom" ? "bottom" : "middle";
  }

  function normalizePreviewBorderFormat(border) {
    const api = FM && FM.spreadsheetXlsxBorders;

    if (api && typeof api.normalizeBorder === "function") {
      return api.normalizeBorder(border);
    }

    const src = border && typeof border === "object" ? border : {};
    return {
      top: normalizePreviewBorderSide(src.top),
      right: normalizePreviewBorderSide(src.right),
      bottom: normalizePreviewBorderSide(src.bottom),
      left: normalizePreviewBorderSide(src.left)
    };
  }

  function isEmptyPreviewBorderFormat(border) {
    const api = FM && FM.spreadsheetXlsxBorders;

    if (api && typeof api.isEmptyBorder === "function") {
      return api.isEmptyBorder(border);
    }

    const b = normalizePreviewBorderFormat(border);
    return !b.top && !b.right && !b.bottom && !b.left;
  }

  function previewBorderCssWidth(style) {
    const api = FM && FM.spreadsheetXlsxBorders;

    if (api && typeof api.cssWidth === "function") {
      return api.cssWidth(style);
    }

    const normalized = normalizePreviewBorderSide(style);
    if (normalized === "double") return "3px";
    if (normalized === "medium") return "2px";
    return normalized === "thin" ? "1px" : "";
  }

  function previewBorderSideFromXlsx(side) {
    const api = FM && FM.spreadsheetXlsxBorders;

    if (api && typeof api.fromXlsxSide === "function") {
      return api.fromXlsxSide(side);
    }

    return normalizePreviewBorderSide(side && side.style);
  }

  function previewBorderFromXlsxStyle(border) {
    const src = border && typeof border === "object" ? border : {};
    return normalizePreviewBorderFormat({
      top: previewBorderSideFromXlsx(src.top),
      right: previewBorderSideFromXlsx(src.right),
      bottom: previewBorderSideFromXlsx(src.bottom),
      left: previewBorderSideFromXlsx(src.left)
    });
  }

  function applyPreviewBorderFormat(td, border) {
    if (!td) return;

    const b = normalizePreviewBorderFormat(border);
    const sideMap = { top: "Top", right: "Right", bottom: "Bottom", left: "Left" };

    let hasBorder = false;
    for (const side of PREVIEW_BORDER_SIDES) {
      const cssSide = sideMap[side];
      const width = previewBorderCssWidth(b[side]);
      const prop = `border${cssSide}`;
      const borderApi = FM && FM.spreadsheetXlsxBorders;
      const lineStyle =
        borderApi && typeof borderApi.cssLineStyle === "function"
          ? borderApi.cssLineStyle(b[side])
          : (b[side] === "double" ? "double" : "solid");

      if (width) {
        hasBorder = true;
        td.style[prop] = `${width} ${lineStyle} var(--fg)`;
      } else {
        td.style.removeProperty(`border-${side}`);
      }
    }

    if (hasBorder) {
      td.dataset.spreadsheetCellBorder = "1";
    } else {
      td.removeAttribute("data-spreadsheet-cell-border");
    }
  }

  function normalizePreviewDecimalPlaces(value) {
    if (value == null || value === "") return null;

    const n = Number(value);
    if (!Number.isFinite(n)) return null;

    const rounded = Math.round(n);
    return Math.max(PREVIEW_MIN_DECIMAL_PLACES, Math.min(PREVIEW_MAX_DECIMAL_PLACES, rounded));
  }

  function parsePreviewPlainNumber(value) {
    const s = String(value == null ? "" : value).trim();
    if (!s) return { blank: true, number: 0 };

    if (/^-?\d+(?:[.,]\d+)?$/.test(s)) {
      const n = Number(s.replace(",", "."));
      if (Number.isFinite(n)) return { blank: false, number: n };
    }

    return { blank: false, text: s };
  }

  function normalizePreviewCurrencyKey(value) {
    const key = String(value || "").trim().toLowerCase();
    return Object.prototype.hasOwnProperty.call(PREVIEW_CURRENCY_FORMATS, key) ? key : "";
  }

  function normalizePreviewNumberFormat(value, currency) {
    const key = String(value || "").trim().toLowerCase();
    if (key === "percent") return "percent";
    return key === "currency" && normalizePreviewCurrencyKey(currency) ? "currency" : "";
  }

  function formatPreviewNumericDisplayValue(value, fmt) {
    const f = normalizePreviewCellFormat(fmt);
    const n = Number(value);

    if (!Number.isFinite(n)) {
      return String(value == null ? "" : value);
    }

    if (f.numberFormat === "percent") {
      const decimals = f.decimals == null ? 2 : f.decimals;
      return `${(n * 100).toFixed(decimals)}%`;
    }

    const currency = f.numberFormat === "currency" ? PREVIEW_CURRENCY_FORMATS[f.currency] : null;
    const decimals = currency && f.decimals == null ? 2 : f.decimals;
    const base = decimals == null ? String(value == null ? "" : value) : n.toFixed(decimals);

    return currency ? `${currency.prefix}${base}${currency.suffix}` : base;
  }

  function displayPreviewCellValue(value, fmt) {
    const f = normalizePreviewCellFormat(fmt);
    const parsed = parsePreviewPlainNumber(value);

    if ((f.decimals != null || f.numberFormat === "currency" || f.numberFormat === "percent") && !parsed.blank && typeof parsed.number === "number") {
      return formatPreviewNumericDisplayValue(parsed.number, f);
    }

    return String(value == null ? "" : value);
  }

  function normalizePreviewFontName(value) {
    const api = FM && FM.spreadsheetFonts;

    if (api && typeof api.normalizeFontName === "function") {
      return api.normalizeFontName(value);
    }

    return String(value == null ? "" : value)
      .replace(/[\u0000-\u001F\u007F]/g, "")
      .trim()
      .slice(0, 128);
  }

  function previewCssFontFamily(value, fallback = "") {
    const api = FM && FM.spreadsheetFonts;

    if (api && typeof api.cssFontFamily === "function") {
      return api.cssFontFamily(value, fallback);
    }

    const name =
      normalizePreviewFontName(value) ||
      normalizePreviewFontName(fallback);

    return name ? `"${name}"` : "";
  }

  function normalizePreviewCellFormat(fmt) {
    const src = fmt && typeof fmt === "object" ? fmt : {};
    const align = src.align === "center" || src.align === "right" || src.align === "left" ? src.align : "";
    const bg = Object.prototype.hasOwnProperty.call(PREVIEW_FILL_COLORS, String(src.bg || "")) ? String(src.bg) : "";
    const fg = Object.prototype.hasOwnProperty.call(PREVIEW_TEXT_COLORS, String(src.fg || "")) ? String(src.fg) : "";
    const rawCurrency = normalizePreviewCurrencyKey(src.currency);
    const numberFormat = normalizePreviewNumberFormat(src.numberFormat, rawCurrency);
    const currency = numberFormat === "currency" ? rawCurrency : "";
    const decimals = normalizePreviewDecimalPlaces(src.decimals);
    return {
      bold: !!src.bold,
      italic: !!src.italic,
      underline: !!src.underline,
      fontName: normalizePreviewFontName(
        src.fontName || src.fontFamily || src.font
      ),
      fontSize: normalizePreviewFontSize(src.fontSize || src.sz),
      decimals,
      numberFormat,
      currency,
      align,
      bg,
      fg,
      border: normalizePreviewBorderFormat(src.border)
    };
  }

  function readPreviewStyleMetadataJsonFromSheet(ws) {
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

  function readStoredPreviewCellFormats(wb) {
    const ws = wb && wb.Sheets ? wb.Sheets[STYLE_SHEET_NAME] : null;
    if (!ws) return {};

    const raw = readPreviewStyleMetadataJsonFromSheet(ws);
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

  function clampPreviewColumnWidth(width) {
    const n = Number(width);
    if (!Number.isFinite(n)) return DEFAULT_PREVIEW_COL_WIDTH;
    return Math.max(MIN_PREVIEW_COL_WIDTH, Math.min(MAX_PREVIEW_COL_WIDTH, n));
  }

  function clampPreviewRowHeight(height) {
    const n = Number(height);
    if (!Number.isFinite(n)) return DEFAULT_PREVIEW_ROW_HEIGHT;
    return Math.max(MIN_PREVIEW_ROW_HEIGHT, Math.min(MAX_PREVIEW_ROW_HEIGHT, n));
  }

  function previewWorksheetDefaults(wb, sheetIndex) {
    const api = FM && FM.spreadsheetXlsxDimensions;

    if (api && typeof api.worksheetDefaults === "function") {
      return api.worksheetDefaults(wb, sheetIndex, {
        defaultColWidth: DEFAULT_PREVIEW_COL_WIDTH,
        defaultRowHeight: DEFAULT_PREVIEW_ROW_HEIGHT
      });
    }

    return {
      colWidth: DEFAULT_PREVIEW_COL_WIDTH,
      rowHeight: DEFAULT_PREVIEW_ROW_HEIGHT
    };
  }

  function xlsxPreviewColumnToPixelWidth(col, defaultWidth) {
    const api = FM && FM.spreadsheetXlsxDimensions;

    if (api && typeof api.columnToCssPixels === "function") {
      return clampPreviewColumnWidth(
        api.columnToCssPixels(col, defaultWidth)
      );
    }

    return clampPreviewColumnWidth(defaultWidth);
  }

  function xlsxPreviewRowToPixelHeight(row, defaultHeight) {
    const api = FM && FM.spreadsheetXlsxDimensions;

    if (api && typeof api.rowToCssPixels === "function") {
      return clampPreviewRowHeight(
        api.rowToCssPixels(row, defaultHeight)
      );
    }

    return clampPreviewRowHeight(defaultHeight);
  }

  function previewColumnWidths(
    wb,
    ws,
    sheetIndex,
    colCount,
    defaultWidth
  ) {
    const count = Math.max(
      0,
      Number.isInteger(colCount) ? colCount : 0
    );

    const api =
      FM && FM.spreadsheetXlsxDimensions;

    if (
      api &&
      typeof api.worksheetColumnWidths ===
        "function"
    ) {
      return api.worksheetColumnWidths(
        wb,
        sheetIndex,
        count,
        {
          defaultColWidth: defaultWidth,
          defaultRowHeight:
            DEFAULT_PREVIEW_ROW_HEIGHT
        }
      ).map((width) =>
        clampPreviewColumnWidth(width)
      );
    }

    return Array.from(
      { length: count },
      (_v, col) => {
        const meta =
          ws &&
          ws["!cols"] &&
          ws["!cols"][col];

        return xlsxPreviewColumnToPixelWidth(
          meta,
          defaultWidth
        );
      }
    );
  }

  function previewRowHeights(ws, rowCount, defaultHeight) {
    const count = Math.max(0, Number.isInteger(rowCount) ? rowCount : 0);

    return Array.from({ length: count }, (_v, r) => {
      const meta = ws && ws["!rows"] && ws["!rows"][r];
      return xlsxPreviewRowToPixelHeight(meta, defaultHeight);
    });
  }

  function applyPreviewColumnWidth(el, width) {
    if (!el || !Number.isFinite(Number(width))) return;
    const px = `${clampPreviewColumnWidth(width)}px`;
    el.style.width = px;
    el.style.minWidth = px;
    el.style.maxWidth = px;
  }

  function applyPreviewRowHeight(el, height) {
    if (!el || !Number.isFinite(Number(height)) || Number(height) <= 0) return;
    const px = `${clampPreviewRowHeight(height)}px`;
    el.style.height = px;
    el.style.minHeight = px;
  }

  function isPreviewPlainTextOverflowCandidate(value, fmt) {
    const text = String(value == null ? "" : value);
    if (!text || /[\r\n]/.test(text)) return false;

    const f = normalizePreviewCellFormat(fmt);
    if (f.align === "center" || f.align === "right") return false;

    // Match spreadsheet behavior for plain text. Numeric-looking values should
    // remain clipped to their own cell instead of spilling across the grid.
    return !/^[+-]?(?:\d+|\d*[.,]\d+)(?:[%€$])?$/.test(text.trim());
  }

  function isPreviewOverflowEmptyCell(sheet, rows, row, col) {
    if (!Array.isArray(rows) || row < 0 || row >= rows.length || col < 0) return false;
    if (previewMergeAtCell(sheet, row, col)) return false;

    const sourceRow = Array.isArray(rows[row]) ? rows[row] : [];
    return String(sourceRow[col] == null ? "" : sourceRow[col]) === "";
  }

  function previewTextOverflowWidth(sheet, rows, row, col, colWidths, colCount) {
    let width = clampPreviewColumnWidth(colWidths[col]);

    for (let c = col + 1; c < colCount; c++) {
      if (!isPreviewOverflowEmptyCell(sheet, rows, row, c)) break;
      width += clampPreviewColumnWidth(colWidths[c]);
    }

    return width;
  }

  function renderPreviewCellText(td, sheet, rows, row, col, colWidths, colCount, value, fmt) {
    const text = displayPreviewCellValue(value, fmt);

    if (!isPreviewPlainTextOverflowCandidate(text, fmt)) {
      td.textContent = text;
      return;
    }

    const overflowWidth = previewTextOverflowWidth(sheet, rows, row, col, colWidths, colCount);
    const ownWidth = clampPreviewColumnWidth(colWidths[col]);

    if (overflowWidth <= ownWidth) {
      td.textContent = text;
      return;
    }

    td.dataset.spreadsheetTextOverflow = "1";
    td.style.setProperty("--spreadsheet-text-overflow-width", `${overflowWidth}px`);

    const span = document.createElement("span");
    span.className = "spreadsheetPreviewTextOverflow";
    span.textContent = text;
    td.appendChild(span);
  }

  function extractPreviewCellFormats(
    XLSX,
    ws,
    rows,
    cols,
    xlsxBorders = null
  ) {
    const rowCount = Math.max(0, Number.isInteger(rows) ? rows : 0);
    const colCount = Math.max(0, Number.isInteger(cols) ? cols : 0);
    const out = Array.from({ length: rowCount }, () => Array.from({ length: colCount }, () => null));

    if (!ws || !ws["!ref"] || !XLSX || !XLSX.utils) return out;

    for (let r = 0; r < rowCount; r++) {
      for (let c = 0; c < colCount; c++) {
        const addr = XLSX.utils.encode_cell({ r, c });
        const style = ws[addr] && ws[addr].s;
        const rawXlsxBorder =
          xlsxBorders && xlsxBorders[addr]
            ? xlsxBorders[addr]
            : null;

        const fmt = normalizePreviewCellFormat({
          bold: !!(style && style.font && style.font.bold),
          italic: !!(style && style.font && style.font.italic),
          underline: !!(style && style.font && style.font.underline),
          fontName: normalizePreviewFontName(
            style && style.font && style.font.name
          ),
          fontSize: normalizePreviewFontSize(style && style.font && style.font.sz),
          align: style && style.alignment && style.alignment.horizontal === "center" ? "center" : "",
          valign: normalizePreviewVerticalAlign(style && style.alignment && style.alignment.vertical),
          bg: previewFillKeyFromRgb(style && style.fill && style.fill.fgColor && style.fill.fgColor.rgb),
          fg: previewTextKeyFromRgb(style && style.font && style.font.color && style.font.color.rgb),
          border: rawXlsxBorder ||
            previewBorderFromXlsxStyle(style && style.border)
        });

        if (fmt.bold || fmt.italic || fmt.underline || fmt.fontName || fmt.fontSize || fmt.decimals != null || fmt.numberFormat || fmt.currency || fmt.align || fmt.valign || fmt.bg || fmt.fg || !isEmptyPreviewBorderFormat(fmt.border)) {
          out[r][c] = fmt;
        }
      }
    }

    return out;
  }

  function applyPreviewCellFormat(td, fmt) {
    if (!td) return;

    const f = normalizePreviewCellFormat(fmt);
    td.style.fontWeight = f.bold ? "700" : "";
    td.style.fontStyle = f.italic ? "italic" : "";
    td.style.textDecoration = f.underline ? "underline" : "";
    td.style.fontFamily = f.fontName
      ? previewCssFontFamily(f.fontName)
      : "";
    td.style.fontSize = f.fontSize ? `${f.fontSize}px` : "";
    td.style.textAlign = f.align || "";
    td.style.verticalAlign = previewVerticalAlignCss(f.valign);

    if (f.bg && PREVIEW_FILL_COLORS[f.bg]) {
      td.style.background = PREVIEW_FILL_COLORS[f.bg];
    }

    if (f.fg && PREVIEW_TEXT_COLORS[f.fg]) {
      td.style.color = PREVIEW_TEXT_COLORS[f.fg];
    }

    applyPreviewBorderFormat(td, f.border);
  }


  function normalizePreviewMergeRange(merge, rowCount = null, colCount = null) {
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

  function previewMergeRangesOverlap(a, b) {
    const ma = normalizePreviewMergeRange(a);
    const mb = normalizePreviewMergeRange(b);
    if (!ma || !mb) return false;

    return ma.s.r <= mb.e.r &&
      ma.e.r >= mb.s.r &&
      ma.s.c <= mb.e.c &&
      ma.e.c >= mb.s.c;
  }

  function extractPreviewMerges(XLSX, ws, rowCount, colCount) {
    if (!ws || !ws["!ref"] || !Array.isArray(ws["!merges"]) || !XLSX || !XLSX.utils) return [];

    const out = [];

    for (const raw of ws["!merges"]) {
      const merge = normalizePreviewMergeRange({
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
      if (out.some((existing) => previewMergeRangesOverlap(existing, merge))) continue;
      out.push(merge);
    }

    return out;
  }

  function previewMergeAtCell(sheet, row, col) {
    const merges = Array.isArray(sheet && sheet.merges) ? sheet.merges : [];
    return merges
      .map((merge) => normalizePreviewMergeRange(merge))
      .find((merge) =>
        merge &&
        row >= merge.s.r &&
        row <= merge.e.r &&
        col >= merge.s.c &&
        col <= merge.e.c
      ) || null;
  }

  function previewMergeIsAnchorCell(merge, row, col) {
    const m = normalizePreviewMergeRange(merge);
    return !!m && row === m.s.r && col === m.s.c;
  }

  function previewMergeColumnPixelWidth(colWidths, col1, col2) {
    let total = 0;
    for (let c = col1; c <= col2; c++) {
      total += clampPreviewColumnWidth(colWidths[c]);
    }
    return Math.max(MIN_PREVIEW_COL_WIDTH, total);
  }

  function previewMergeRowPixelHeight(rowHeights, row1, row2) {
    let total = 0;
    for (let r = row1; r <= row2; r++) {
      total += clampPreviewRowHeight(rowHeights[r]);
    }
    return Math.max(MIN_PREVIEW_ROW_HEIGHT, total);
  }


  function worksheetPreviewBounds(XLSX, ws, rows) {
    const sourceRows = Array.isArray(rows) ? rows : [];
    const range = ws && ws["!ref"] && XLSX && XLSX.utils
      ? XLSX.utils.decode_range(ws["!ref"])
      : null;

    // Preserve the workbook coordinate grid. Hidden/empty rows must not be
    // collapsed, because merge refs and cell formats use original row indexes.
    let rowCount = range
      ? Math.max(0, range.e.r + 1)
      : sourceRows.length;
    let colCount = range
      ? Math.max(0, range.e.c + 1)
      : sourceRows.reduce(
          (m, row) =>
            Math.max(
              m,
              Array.isArray(row)
                ? row.length
                : 0
            ),
          0
        );

    rowCount = Math.max(rowCount, sourceRows.length);
    colCount = Math.max(colCount, sourceRows.reduce((m, row) => Math.max(m, Array.isArray(row) ? row.length : 0), 0));

    if (ws && Array.isArray(ws["!merges"])) {
      for (const raw of ws["!merges"]) {
        const endRow = Number(raw && raw.e && raw.e.r);
        const endCol = Number(raw && raw.e && raw.e.c);

        if (Number.isInteger(endRow) && endRow >= 0) {
          rowCount = Math.max(rowCount, endRow + 1);
        }
        if (Number.isInteger(endCol) && endCol >= 0) {
          colCount = Math.max(colCount, endCol + 1);
        }
      }
    }

    return {
      rows: Math.max(0, Math.min(MAX_RENDER_ROWS, rowCount)),
      cols: Math.max(0, Math.min(MAX_RENDER_COLS, colCount))
    };
  }

  function padPreviewRowsForBounds(rows, rowCount, colCount) {
    const sourceRows = Array.isArray(rows) ? rows : [];
    const out = [];

    for (let r = 0; r < rowCount; r++) {
      const sourceRow = Array.isArray(sourceRows[r]) ? sourceRows[r] : [];
      const row = [];

      for (let c = 0; c < colCount; c++) {
        const value = sourceRow[c];
        row.push(value == null ? "" : value);
      }

      out.push(row);
    }

    return out;
  }


  function clampPreviewFreezeCount(value, limit) {
    const n = Number(value);
    if (!Number.isFinite(n)) return 0;

    const max = Math.max(0, Number(limit) || 0);
    return Math.max(0, Math.min(max, Math.floor(n)));
  }

  function normalizePreviewFreeze(src, rowCount = MAX_RENDER_ROWS, colCount = MAX_RENDER_COLS) {
    return {
      topRows: clampPreviewFreezeCount(src && src.topRows, rowCount),
      leftCols: clampPreviewFreezeCount(src && src.leftCols, colCount)
    };
  }

  function hasPreviewFreeze(freeze) {
    const normalized = normalizePreviewFreeze(freeze);
    return normalized.topRows > 0 || normalized.leftCols > 0;
  }

  function previewXlsxXmlAttrMap(tag) {
    const attrs = {};
    const src = String(tag || "");
    const attrRe = /([A-Za-z_][A-Za-z0-9_.:-]*)\s*=\s*"([^"]*)"/g;
    let match = null;

    while ((match = attrRe.exec(src))) {
      attrs[match[1]] = match[2];
    }

    return attrs;
  }

  function previewFreezeFromPaneAttrs(attrs) {
    const paneState = String(attrs && attrs.state || "").toLowerCase();

    // A normal split pane uses the same attributes with different units.
    // Only import actual Excel frozen panes.
    if (!paneState.startsWith("frozen")) {
      return { topRows: 0, leftCols: 0 };
    }

    return normalizePreviewFreeze({
      topRows: attrs.ySplit,
      leftCols: attrs.xSplit
    });
  }

  function previewFreezeFromWorksheetXml(xml) {
    const match = String(xml || "").match(/<pane\b[^>]*>/i);
    if (!match) return { topRows: 0, leftCols: 0 };

    return previewFreezeFromPaneAttrs(previewXlsxXmlAttrMap(match[0]));
  }

  function previewSheetJsFileBytes(file) {
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

  function previewSheetJsFileText(file) {
    if (!file) return "";
    if (typeof file === "string") return file;

    for (const key of ["content", "data", "_data"]) {
      if (typeof file[key] === "string") return file[key];
    }

    const bytes = previewSheetJsFileBytes(file);
    if (!bytes) return "";

    try {
      return new TextDecoder("utf-8").decode(bytes);
    } catch (_) {
      return "";
    }
  }

  function previewWorkbookFileByName(wb, name) {
    const wanted = String(name || "").replace(/^\/+/, "");
    if (!wanted || !wb || !wb.files) return null;

    const files = wb.files;

    if (files[wanted]) return files[wanted];
    if (files["/" + wanted]) return files["/" + wanted];

    if (Array.isArray(files.FileIndex)) {
      for (const file of files.FileIndex) {
        const candidate = String(file && file.name || "").replace(/^\/+/, "");
        if (candidate === wanted) return file;
      }
    }

    return null;
  }

  function previewWorksheetXmlFromWorkbook(wb, sheetIndex) {
    const sheetNo = Math.max(1, Number(sheetIndex) + 1 || 1);
    const path = `xl/worksheets/sheet${sheetNo}.xml`;

    return previewSheetJsFileText(previewWorkbookFileByName(wb, path));
  }

  function previewFreezeFromSheetJs(ws) {
    const views = Array.isArray(ws && ws["!views"]) ? ws["!views"] : [];

    for (const view of views) {
      const pane = view && (view.pane || view);
      const freeze = previewFreezeFromPaneAttrs({
        state: pane && pane.state,
        xSplit: pane && pane.xSplit,
        ySplit: pane && pane.ySplit
      });

      if (hasPreviewFreeze(freeze)) return freeze;
    }

    const rawFreeze = ws && ws["!freeze"];
    if (rawFreeze) {
      return normalizePreviewFreeze({
        topRows: rawFreeze.ySplit ?? rawFreeze.topRows,
        leftCols: rawFreeze.xSplit ?? rawFreeze.leftCols
      });
    }

    return { topRows: 0, leftCols: 0 };
  }

  function previewWorksheetFreezeFromWorkbook(wb, ws, sheetIndex) {
    const sheetJsFreeze = previewFreezeFromSheetJs(ws);
    if (hasPreviewFreeze(sheetJsFreeze)) return sheetJsFreeze;

    return previewFreezeFromWorksheetXml(
      previewWorksheetXmlFromWorkbook(wb, sheetIndex)
    );
  }

  function applyPreviewFreezeMode(table, sheet, rowCount, colCount) {
    if (!table) return;

    const freeze = normalizePreviewFreeze(
      sheet && sheet.freeze,
      rowCount,
      colCount
    );

    table.toggleAttribute(
      "data-spreadsheet-freeze-top-row",
      freeze.topRows > 0
    );

    table.toggleAttribute(
      "data-spreadsheet-freeze-first-column",
      freeze.leftCols > 0
    );
  }

  function refreshPreviewFreezeOffsets(table) {
    if (!table) return;

    const corner = table.querySelector("thead th.corner");
    const firstRowHead = table.querySelector("tbody th.rowHead");

    const topOffset = Math.ceil(
      corner ? corner.getBoundingClientRect().height : 24
    );

    const leftOffset = Math.ceil(
      firstRowHead ? firstRowHead.getBoundingClientRect().width : 54
    );

    table.style.setProperty(
      "--spreadsheet-preview-freeze-top-offset",
      `${topOffset}px`
    );

    table.style.setProperty(
      "--spreadsheet-preview-freeze-left-offset",
      `${leftOffset}px`
    );
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
    await ensureSpreadsheetImageModules();
    const buf = await r.arrayBuffer();

    // Security: parse the workbook as data only; do not execute macros, formulas,
    // external links or embedded active content in the browser.
    const wb = XLSX.read(buf, {
      type: "array",
      cellFormula: false,
      cellHTML: false,
      cellNF: false,
      cellStyles: true,
      bookFiles: true
    });

    const storedCellFormats = readStoredPreviewCellFormats(wb);
    const names = Array.isArray(wb.SheetNames)
      ? wb.SheetNames.filter((name) => name !== STYLE_SHEET_NAME)
      : [];

    const borderApi = FM && FM.spreadsheetXlsxBorders;
    const workbookBorders =
      borderApi &&
      typeof borderApi.bordersBySheet === "function"
        ? borderApi.bordersBySheet(wb, names)
        : [];

    const imageApi = FM && FM.spreadsheetXlsxImages;
    const workbookImages = imageApi && typeof imageApi.imagesFromWorkbookFiles === "function"
      ? imageApi.imagesFromWorkbookFiles(wb, names)
      : [];

    return names.map((name, idx) => {
      const ws = wb.Sheets[name];
      const rawRowsOptions = {
        header: 1,
        raw: false,
        defval: "",
        blankrows: true
      };

      if (ws && ws["!ref"]) {
        const usedRange =
          XLSX.utils.decode_range(
            ws["!ref"]
          );

        /*
         * Correctness: request rows from A1 instead of letting SheetJS make the
         * used range's top-left cell the first preview cell.
         */
        rawRowsOptions.range = {
          s: { r: 0, c: 0 },
          e: {
            r: usedRange.e.r,
            c: usedRange.e.c
          }
        };
      }

      const rawRows =
        XLSX.utils.sheet_to_json(
          ws,
          rawRowsOptions
        );
      const bounds = worksheetPreviewBounds(
        XLSX,
        ws,
        rawRows
      );

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

      /*
       * Images can extend beyond the worksheet's populated cell range.
       * Determine the final grid bounds before reading row and column
       * geometry, so extended columns retain their raw OOXML widths instead
       * of receiving the generic preview fallback width.
       */
      const rowCount = Math.max(
        bounds.rows,
        Math.max(
          0,
          Math.floor(
            Number(imageBounds.rows) || 0
          )
        )
      );

      const colCount = Math.max(
        bounds.cols,
        Math.max(
          0,
          Math.floor(
            Number(imageBounds.cols) || 0
          )
        )
      );

      const rows = padPreviewRowsForBounds(
        rawRows,
        rowCount,
        colCount
      );

      const safeName =
        safePreviewSheetName(name, idx);

      const storedFormats =
        storedCellFormats[safeName] ||
        storedCellFormats[name] ||
        null;

      const defaults =
        previewWorksheetDefaults(wb, idx);
      const sheet = {
        name,
        rows,
        defaultColWidth: defaults.colWidth,
        defaultRowHeight: defaults.rowHeight,
        colWidths: previewColumnWidths(
          wb,
          ws,
          idx,
          colCount,
          defaults.colWidth
        ),
        rowHeights: previewRowHeights(ws, rows.length, defaults.rowHeight),
        cellFormats: Array.isArray(storedFormats)
          ? storedFormats
          : extractPreviewCellFormats(
              XLSX,
              ws,
              rows.length,
              colCount,
              workbookBorders[idx] || null
            ),
        merges: extractPreviewMerges(XLSX, ws, rows.length, colCount),
        freeze: previewWorksheetFreezeFromWorkbook(wb, ws, idx),
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
    applyPreviewFreezeMode(table, sheet, normalized.rows.length, normalized.cols);

    const colWidths = Array.isArray(sheet.colWidths) ? sheet.colWidths : [];
    const rowHeights = Array.isArray(sheet.rowHeights) ? sheet.rowHeights : [];
    const colgroup = document.createElement("colgroup");
    const rowHeadCol = document.createElement("col");
    rowHeadCol.style.width = "44px";
    colgroup.appendChild(rowHeadCol);

    for (let c = 0; c < normalized.cols; c++) {
      const colEl = document.createElement("col");
      applyPreviewColumnWidth(colEl, colWidths[c]);
      colgroup.appendChild(colEl);
    }

    table.appendChild(colgroup);

    const thead = document.createElement("thead");
    const hr = document.createElement("tr");
    const corner = document.createElement("th");
    corner.className = "rowHead corner";
    corner.textContent = "";
    hr.appendChild(corner);

    for (let c = 0; c < normalized.cols; c++) {
      const th = document.createElement("th");
      th.dataset.col = String(c);
      applyPreviewColumnWidth(th, colWidths[c]);
      th.textContent = columnName(c);
      hr.appendChild(th);
    }

    thead.appendChild(hr);
    table.appendChild(thead);

    const tbody = document.createElement("tbody");
    normalized.rows.forEach((row, rIdx) => {
      const trEl = document.createElement("tr");
      const rowHeight = rowHeights[rIdx];

      applyPreviewRowHeight(trEl, rowHeight);

      const rh = document.createElement("th");
      rh.className = "rowHead";
      applyPreviewRowHeight(rh, rowHeight);
      rh.textContent = String(rIdx + 1);
      trEl.appendChild(rh);

      for (let c = 0; c < normalized.cols; c++) {
        const merge = previewMergeAtCell(sheet, rIdx, c);
        if (merge && !previewMergeIsAnchorCell(merge, rIdx, c)) continue;

        const colSpan = merge ? Math.max(1, merge.e.c - merge.s.c + 1) : 1;
        const rowSpan = merge ? Math.max(1, merge.e.r - merge.s.r + 1) : 1;
        const cellWidth = merge ? previewMergeColumnPixelWidth(colWidths, merge.s.c, merge.e.c) : colWidths[c];
        const cellHeight = merge ? previewMergeRowPixelHeight(rowHeights, merge.s.r, merge.e.r) : rowHeight;

        const td = document.createElement("td");
        td.dataset.row = String(rIdx);
        td.dataset.col = String(c);
        if (colSpan > 1) td.colSpan = colSpan;
        if (rowSpan > 1) td.rowSpan = rowSpan;
        applyPreviewColumnWidth(td, cellWidth);
        applyPreviewRowHeight(td, cellHeight);
        const fmt = sheet.cellFormats && sheet.cellFormats[rIdx] && sheet.cellFormats[rIdx][c];
        applyPreviewCellFormat(td, fmt);
        // Security: always render cell values as text, never HTML.
        renderPreviewCellText(td, sheet, normalized.rows, rIdx, c, colWidths, normalized.cols, row[c], fmt);
        trEl.appendChild(td);
      }

      tbody.appendChild(trEl);
    });

    table.appendChild(tbody);

    const surface = document.createElement("div");
    surface.className = "spreadsheetSheetSurface";
    surface.appendChild(table);
    bodyEl.appendChild(surface);

    refreshPreviewFreezeOffsets(table);
    requestAnimationFrame(() => refreshPreviewFreezeOffsets(table));

    const overlayApi = FM && FM.spreadsheetImageOverlay;
    if (overlayApi && typeof overlayApi.render === "function") {
      overlayApi.render(surface, table, sheet, {
        defaultColWidth: Number(sheet.defaultColWidth) || DEFAULT_PREVIEW_COL_WIDTH,
        defaultRowHeight: Number(sheet.defaultRowHeight) || DEFAULT_PREVIEW_ROW_HEIGHT
      });
    }

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
