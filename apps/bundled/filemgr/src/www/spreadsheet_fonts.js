window.PQNAS_FILEMGR = window.PQNAS_FILEMGR || {};

(() => {
  "use strict";

  const FM = window.PQNAS_FILEMGR;
  const DEFAULT_FONT = Object.freeze({
    name: "Calibri",
    size: 11,
    family: "2",
    scheme: "minor"
  });

  const MAX_FONT_NAME_LENGTH = 128;

  function cleanFontName(value) {
    return String(value == null ? "" : value)
      .replace(/[\u0000-\u001F\u007F]/g, "")
      .trim()
      .slice(0, MAX_FONT_NAME_LENGTH);
  }

  function positiveNumber(value, fallback) {
    const n = Number(value);
    if (Number.isFinite(n) && n > 0 && n <= 409) return n;

    const fallbackNumber = Number(fallback);
    return Number.isFinite(fallbackNumber) && fallbackNumber > 0
      ? fallbackNumber
      : DEFAULT_FONT.size;
  }

  function normalizeFontDescriptor(font, fallback = DEFAULT_FONT) {
    const source = font && typeof font === "object" ? font : {};
    const base = fallback && typeof fallback === "object"
      ? fallback
      : DEFAULT_FONT;

    return {
      name: cleanFontName(source.name) ||
        cleanFontName(base.name) ||
        DEFAULT_FONT.name,
      size: positiveNumber(source.size, base.size),
      family: cleanFontName(source.family) ||
        cleanFontName(base.family) ||
        DEFAULT_FONT.family,
      scheme: cleanFontName(source.scheme)
    };
  }

  function workbookFileByName(wb, name) {
    const files = wb && wb.files && typeof wb.files === "object"
      ? wb.files
      : null;

    if (!files) return null;

    const wanted = String(name || "").replace(/^\/+/, "");
    if (!wanted) return null;

    if (files[wanted]) return files[wanted];
    if (files[`/${wanted}`]) return files[`/${wanted}`];

    if (Array.isArray(files.FileIndex)) {
      for (const file of files.FileIndex) {
        const fileName = String(file && file.name || "").replace(/^\/+/, "");
        if (fileName === wanted) return file;
      }
    }

    return null;
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
      /*
       * Security: styles.xml is parsed only as inert XML data. Workbook text is
       * never inserted into HTML and cannot create executable browser content.
       */
      const doc = new DOMParser().parseFromString(
        String(text || ""),
        "application/xml"
      );

      return doc.getElementsByTagName("parsererror").length
        ? null
        : doc;
    } catch (_) {
      return null;
    }
  }

  function directChildrenByLocalName(parent, localName) {
    if (!parent || !parent.children) return [];

    return Array.from(parent.children).filter(
      (child) => child.localName === localName
    );
  }

  function firstElementByLocalName(root, localName) {
    if (!root || !root.getElementsByTagName) return null;

    for (const element of Array.from(root.getElementsByTagName("*"))) {
      if (element.localName === localName) return element;
    }

    return null;
  }

  function childAttribute(parent, localName, attribute) {
    const child = directChildrenByLocalName(parent, localName)[0];

    return child && child.getAttribute
      ? child.getAttribute(attribute) || ""
      : "";
  }

  function defaultFontIdFromStyles(doc) {
    for (const collectionName of ["cellXfs", "cellStyleXfs"]) {
      const collection = firstElementByLocalName(doc, collectionName);
      const xf = directChildrenByLocalName(collection, "xf")[0];

      if (!xf || !xf.getAttribute) continue;

      const fontId = Number(xf.getAttribute("fontId"));
      if (Number.isInteger(fontId) && fontId >= 0) return fontId;
    }

    return 0;
  }

  function readWorkbookDefaultFont(wb, fallback = DEFAULT_FONT) {
    const base = normalizeFontDescriptor(fallback, DEFAULT_FONT);
    const file = workbookFileByName(wb, "xl/styles.xml");
    const doc = parseXml(workbookFileText(file));

    if (!doc) return base;

    const fonts = firstElementByLocalName(doc, "fonts");
    const fontList = directChildrenByLocalName(fonts, "font");
    const fontId = defaultFontIdFromStyles(doc);
    const font = fontList[fontId] || fontList[0];

    if (!font) return base;

    return normalizeFontDescriptor({
      name: childAttribute(font, "name", "val"),
      size: childAttribute(font, "sz", "val"),
      family: childAttribute(font, "family", "val"),
      scheme: childAttribute(font, "scheme", "val")
    }, base);
  }

  FM.spreadsheetFonts = {
    defaultFont: () => ({ ...DEFAULT_FONT }),
    normalizeFontDescriptor,
    readWorkbookDefaultFont
  };
})();
