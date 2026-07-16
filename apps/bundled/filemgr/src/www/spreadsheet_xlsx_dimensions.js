window.PQNAS_FILEMGR = window.PQNAS_FILEMGR || {};

(() => {
  "use strict";

  const FM = window.PQNAS_FILEMGR;
  const CSS_PIXELS_PER_POINT = 96 / 72;
  const MAX_SAFE_DIMENSION = 200000;

  function finitePositive(value) {
    const n = Number(value);
    return Number.isFinite(n) && n > 0 ? n : 0;
  }

  function safeDimension(value, fallback) {
    const n = finitePositive(value);
    if (!n) return finitePositive(fallback);
    return Math.min(MAX_SAFE_DIMENSION, n);
  }

  function pointsToCssPixels(points) {
    const n = finitePositive(points);
    return n ? Math.min(MAX_SAFE_DIMENSION, n * CSS_PIXELS_PER_POINT) : 0;
  }

  function cssPixelsToPoints(pixels) {
    const n = finitePositive(pixels);
    return n ? Math.min(MAX_SAFE_DIMENSION, n / CSS_PIXELS_PER_POINT) : 0;
  }

  function excelColumnWidthToCssPixels(width) {
    const n = finitePositive(width);
    if (!n) return 0;

    // Keep this compatible with the editor's existing XLSX width conversion.
    return Math.min(MAX_SAFE_DIMENSION, (n * 7) + 5);
  }

  function cssPixelsToExcelColumnWidth(pixels) {
    const n = finitePositive(pixels);
    if (!n) return 0;
    if (n <= 12) return 1;

    return Math.max(1, (n - 5) / 7);
  }

  function sameDimension(a, b, epsilon = 0.01) {
    const left = Number(a);
    const right = Number(b);
    const tolerance = Math.max(0, Number(epsilon) || 0);

    return Number.isFinite(left) &&
      Number.isFinite(right) &&
      Math.abs(left - right) <= tolerance;
  }

  function workbookFileBytes(file) {
    const content = file && file.content != null ? file.content : null;

    if (content instanceof Uint8Array) return content;
    if (content instanceof ArrayBuffer) return new Uint8Array(content);

    if (typeof content === "string") {
      const out = new Uint8Array(content.length);
      for (let i = 0; i < content.length; i++) {
        out[i] = content.charCodeAt(i) & 0xFF;
      }
      return out;
    }

    if (Array.isArray(content)) return new Uint8Array(content);
    return null;
  }

  function workbookFileText(file) {
    const bytes = workbookFileBytes(file);
    if (!bytes) return "";

    try {
      return new TextDecoder("utf-8").decode(bytes);
    } catch (_) {
      return "";
    }
  }

  function parseXml(text) {
    if (typeof DOMParser === "undefined") return null;

    try {
      // Security: workbook XML is parsed only as inert data. No workbook value
      // is inserted as HTML or used to load an external resource.
      const doc = new DOMParser().parseFromString(
        String(text || ""),
        "application/xml"
      );

      return doc.getElementsByTagName("parsererror").length ? null : doc;
    } catch (_) {
      return null;
    }
  }

  function firstLocalNameElement(root, localName) {
    if (!root || !root.getElementsByTagName) return null;

    for (const el of Array.from(root.getElementsByTagName("*"))) {
      if (el.localName === localName) return el;
    }

    return null;
  }

  function worksheetDefaults(wb, sheetIndex, options = {}) {
    const fallbackColWidth = safeDimension(options.defaultColWidth, 96);
    const fallbackRowHeight = safeDimension(options.defaultRowHeight, 22);
    const files = wb && wb.files && typeof wb.files === "object"
      ? wb.files
      : null;

    if (!files || !Number.isInteger(sheetIndex) || sheetIndex < 0) {
      return {
        colWidth: fallbackColWidth,
        rowHeight: fallbackRowHeight
      };
    }

    const worksheetPath = `xl/worksheets/sheet${sheetIndex + 1}.xml`;
    const doc = parseXml(workbookFileText(files[worksheetPath]));
    const format = firstLocalNameElement(doc, "sheetFormatPr");

    if (!format || !format.getAttribute) {
      return {
        colWidth: fallbackColWidth,
        rowHeight: fallbackRowHeight
      };
    }

    const defaultColWidth = excelColumnWidthToCssPixels(
      format.getAttribute("defaultColWidth")
    );

    const defaultRowHeight = pointsToCssPixels(
      format.getAttribute("defaultRowHeight")
    );

    return {
      colWidth: safeDimension(defaultColWidth, fallbackColWidth),
      rowHeight: safeDimension(defaultRowHeight, fallbackRowHeight)
    };
  }


  function worksheetColumnWidths(
    wb,
    sheetIndex,
    colCount,
    options = {}
  ) {
    const count = Math.max(
      0,
      Math.floor(Number(colCount) || 0)
    );

    const defaults = worksheetDefaults(
      wb,
      sheetIndex,
      options
    );

    const fallbackWidth = safeDimension(
      defaults.colWidth,
      options.defaultColWidth || 96
    );

    const widths = Array.from(
      { length: count },
      () => fallbackWidth
    );

    const files =
      wb &&
      wb.files &&
      typeof wb.files === "object"
        ? wb.files
        : null;

    if (
      !files ||
      !Number.isInteger(sheetIndex) ||
      sheetIndex < 0 ||
      count <= 0
    ) {
      return widths;
    }

    const worksheetPath =
      `xl/worksheets/sheet${sheetIndex + 1}.xml`;

    const doc = parseXml(
      workbookFileText(files[worksheetPath])
    );

    if (!doc || !doc.getElementsByTagName) {
      return widths;
    }

    /*
     * SheetJS may derive wpx using the font referenced by a column style.
     * That can turn an XLSX width such as 19.43 into 272 pixels. The raw OOXML
     * width and its min/max range are authoritative for worksheet geometry.
     */
    for (
      const element of Array.from(
        doc.getElementsByTagName("*")
      )
    ) {
      if (
        element.localName !== "col" ||
        !element.getAttribute
      ) {
        continue;
      }

      const min = Math.floor(
        Number(element.getAttribute("min"))
      );

      const max = Math.floor(
        Number(element.getAttribute("max"))
      );

      const width = excelColumnWidthToCssPixels(
        element.getAttribute("width")
      );

      if (
        !Number.isInteger(min) ||
        !Number.isInteger(max) ||
        min < 1 ||
        max < min ||
        !width
      ) {
        continue;
      }

      const start = Math.max(0, min - 1);
      const end = Math.min(count - 1, max - 1);
      const safeWidth = safeDimension(
        width,
        fallbackWidth
      );

      for (let col = start; col <= end; col++) {
        widths[col] = safeWidth;
      }
    }

    return widths;
  }

  function columnToCssPixels(col, defaultWidth) {
    if (col && typeof col === "object") {
      const wpx = finitePositive(col.wpx);
      if (wpx) return safeDimension(wpx, defaultWidth);

      const width = excelColumnWidthToCssPixels(col.width);
      if (width) return safeDimension(width, defaultWidth);

      const wch = excelColumnWidthToCssPixels(col.wch);
      if (wch) return safeDimension(wch, defaultWidth);
    }

    return safeDimension(defaultWidth, 96);
  }

  function rowToCssPixels(row, defaultHeight) {
    if (row && typeof row === "object") {
      /*
       * SheetJS may expose hpx with the same numeric value as the OOXML point
       * height. Prefer hpt/ht and perform the point-to-CSS-pixel conversion here
       * so image anchors and rendered worksheet rows share one geometry.
       */
      const hpt = pointsToCssPixels(row.hpt);
      if (hpt) return safeDimension(hpt, defaultHeight);

      const ht = pointsToCssPixels(row.ht);
      if (ht) return safeDimension(ht, defaultHeight);

      const hpx = finitePositive(row.hpx);
      if (hpx) return safeDimension(hpx, defaultHeight);
    }

    return safeDimension(defaultHeight, 22);
  }

  FM.spreadsheetXlsxDimensions = {
    pointsToCssPixels,
    cssPixelsToPoints,
    excelColumnWidthToCssPixels,
    cssPixelsToExcelColumnWidth,
    sameDimension,
    worksheetDefaults,
    worksheetColumnWidths,
    columnToCssPixels,
    rowToCssPixels
  };
})();
