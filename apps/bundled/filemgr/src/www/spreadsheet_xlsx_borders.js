window.PQNAS_FILEMGR = window.PQNAS_FILEMGR || {};

(() => {
  "use strict";

  const FM = window.PQNAS_FILEMGR;
  const SIDES = Object.freeze(["top", "right", "bottom", "left"]);
  const STYLE_KEYS = Object.freeze(["thin", "medium", "double"]);
  const MAX_XML_BYTES = 64 * 1024 * 1024;

  function normalizeSide(value) {
    const key = String(value || "").trim().toLowerCase();

    // Compatibility: old DNA-Nexus metadata used "thick" for XLSX medium.
    if (key === "thick") return "medium";

    return STYLE_KEYS.includes(key) ? key : "";
  }

  function normalizeBorder(border) {
    const source =
      border && typeof border === "object"
        ? border
        : {};

    return {
      top: normalizeSide(source.top),
      right: normalizeSide(source.right),
      bottom: normalizeSide(source.bottom),
      left: normalizeSide(source.left)
    };
  }

  function isEmptyBorder(border) {
    const normalized = normalizeBorder(border);
    return SIDES.every((side) => !normalized[side]);
  }

  function cssWidth(style) {
    const normalized = normalizeSide(style);

    if (normalized === "double") return "3px";
    if (normalized === "medium") return "2px";
    if (normalized === "thin") return "1px";

    return "";
  }

  function cssLineStyle(style) {
    return normalizeSide(style) === "double"
      ? "double"
      : "solid";
  }

  function toXlsxStyle(style) {
    return normalizeSide(style);
  }

  function fromXlsxSide(side) {
    const style =
      side && typeof side === "object"
        ? side.style
        : side;

    return normalizeSide(style);
  }

  function fromXlsxBorder(border) {
    const source =
      border && typeof border === "object"
        ? border
        : {};

    return normalizeBorder({
      top: fromXlsxSide(source.top),
      right: fromXlsxSide(source.right),
      bottom: fromXlsxSide(source.bottom),
      left: fromXlsxSide(source.left)
    });
  }

  function workbookFileBytes(file) {
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

  function workbookFileText(file) {
    if (!file) return "";
    if (typeof file === "string") return file;

    for (const key of ["content", "data", "_data"]) {
      if (typeof file[key] === "string") {
        return file[key].length <= MAX_XML_BYTES
          ? file[key]
          : "";
      }
    }

    const bytes = workbookFileBytes(file);

    /*
     * Correctness/security: reject unexpectedly huge XML parts before
     * decoding. Border import is optional and must remain bounded.
     */
    if (!bytes || bytes.length > MAX_XML_BYTES) return "";

    try {
      return new TextDecoder("utf-8").decode(bytes);
    } catch (_) {
      return "";
    }
  }

  function workbookFileByName(workbook, name) {
    const wanted =
      String(name || "")
        .replace(/^\/+/, "");

    if (!wanted || !workbook || !workbook.files) return null;

    const files = workbook.files;

    if (files[wanted]) return files[wanted];
    if (files["/" + wanted]) return files["/" + wanted];

    if (Array.isArray(files.FileIndex)) {
      for (const file of files.FileIndex) {
        const candidate =
          String(file && file.name || "")
            .replace(/^\/+/, "");

        if (candidate === wanted) return file;
      }
    }

    return null;
  }

  function xmlDecode(value) {
    return String(value || "")
      .replace(/&quot;/g, '"')
      .replace(/&apos;/g, "'")
      .replace(/&lt;/g, "<")
      .replace(/&gt;/g, ">")
      .replace(/&amp;/g, "&");
  }

  function xmlAttributes(sourceValue) {
    const source = String(sourceValue || "");
    const out = {};
    const pattern =
      /([A-Za-z_][A-Za-z0-9_.:-]*)\s*=\s*(?:"([^"]*)"|'([^']*)')/g;

    let match = null;

    while ((match = pattern.exec(source))) {
      out[match[1]] = xmlDecode(
        match[2] !== undefined
          ? match[2]
          : match[3]
      );
    }

    return out;
  }

  function xmlSection(xml, name) {
    const source = String(xml || "");
    let match = null;

    /*
     * Security: styles.xml parsing only needs these two known sections.
     * Hardcoded expressions prevent user-controlled dynamic regular
     * expressions and keep the parser bounded and Semgrep-verifiable.
     */
    if (name === "borders") {
      match = source.match(
        /<borders\b[^>]*>([\s\S]*?)<\/borders>/i
      );
    } else if (name === "cellXfs") {
      match = source.match(
        /<cellXfs\b[^>]*>([\s\S]*?)<\/cellXfs>/i
      );
    }

    return match ? match[1] : "";
  }

  function parseBorderElement(xml) {
    const out = {
      top: "",
      right: "",
      bottom: "",
      left: ""
    };

    for (const side of SIDES) {
      const match = String(xml || "").match(
        new RegExp(`<${side}\\b([^>]*)>`, "i")
      );

      if (!match) continue;

      const attrs = xmlAttributes(match[1]);
      out[side] = normalizeSide(attrs.style);
    }

    return normalizeBorder(out);
  }

  function parseStyleCatalog(stylesXml) {
    const bordersSection = xmlSection(stylesXml, "borders");
    const cellXfsSection = xmlSection(stylesXml, "cellXfs");

    const borders = [];
    const borderPattern =
      /<border\b[^>]*>([\s\S]*?)<\/border>/gi;

    let match = null;

    while ((match = borderPattern.exec(bordersSection))) {
      borders.push(parseBorderElement(match[1]));
    }

    const borderIds = [];
    const xfPattern =
      /<xf\b([^>]*?)(?:\/>|>[\s\S]*?<\/xf>)/gi;

    while ((match = xfPattern.exec(cellXfsSection))) {
      const attrs = xmlAttributes(match[1]);
      const borderId = Number(attrs.borderId);

      borderIds.push(
        Number.isInteger(borderId) && borderId >= 0
          ? borderId
          : 0
      );
    }

    return { borders, borderIds };
  }

  function parseWorkbookSheets(workbookXml) {
    const out = [];
    const pattern = /<sheet\b([^>]*)\/?>/gi;
    let match = null;

    while ((match = pattern.exec(String(workbookXml || "")))) {
      const attrs = xmlAttributes(match[1]);
      const name = String(attrs.name || "");
      const relId = String(attrs["r:id"] || attrs.id || "");

      if (name) out.push({ name, relId });
    }

    return out;
  }

  function safeWorkbookPartPath(target) {
    const raw =
      String(target || "")
        .replace(/\\/g, "/")
        .replace(/\0/g, "")
        .trim();

    if (!raw) return "";

    // Security: relationship targets are package paths, never remote URLs.
    if (/^[A-Za-z][A-Za-z0-9+.-]*:/.test(raw)) return "";

    const source = raw.startsWith("/")
      ? raw.slice(1)
      : `xl/${raw}`;

    const parts = [];

    for (const part of source.split("/")) {
      if (!part || part === ".") continue;

      if (part === "..") {
        if (!parts.length) return "";
        parts.pop();
        continue;
      }

      parts.push(part);
    }

    const resolved = parts.join("/");

    // Security: border relationships must stay inside the XLSX xl/ package.
    return resolved.startsWith("xl/")
      ? resolved
      : "";
  }

  function parseWorkbookRelationships(relsXml) {
    const out = Object.create(null);
    const pattern = /<Relationship\b([^>]*)\/?>/gi;
    let match = null;

    while ((match = pattern.exec(String(relsXml || "")))) {
      const attrs = xmlAttributes(match[1]);
      const id = String(attrs.Id || "");
      const target = safeWorkbookPartPath(attrs.Target);

      if (id && target) out[id] = target;
    }

    return out;
  }

  function worksheetBorderMap(worksheetXml, styleCatalog) {
    const out = Object.create(null);

    const borderIds =
      styleCatalog && Array.isArray(styleCatalog.borderIds)
        ? styleCatalog.borderIds
        : [];

    const borders =
      styleCatalog && Array.isArray(styleCatalog.borders)
        ? styleCatalog.borders
        : [];

    const cellPattern = /<c\b([^>]*)>/gi;
    let match = null;

    while ((match = cellPattern.exec(String(worksheetXml || "")))) {
      const attrs = xmlAttributes(match[1]);
      const address = String(attrs.r || "").toUpperCase();
      const styleIndex = Number(attrs.s);

      if (
        !/^[A-Z]+[1-9][0-9]*$/.test(address) ||
        !Number.isInteger(styleIndex) ||
        styleIndex < 0
      ) {
        continue;
      }

      const borderId = Number(borderIds[styleIndex]);

      if (
        !Number.isInteger(borderId) ||
        borderId < 0 ||
        borderId >= borders.length
      ) {
        continue;
      }

      const border = normalizeBorder(borders[borderId]);

      if (!isEmptyBorder(border)) {
        out[address] = border;
      }
    }

    return out;
  }

  function bordersBySheet(workbook, requestedSheetNames) {
    const names = Array.isArray(requestedSheetNames)
      ? requestedSheetNames.map((name) => String(name || ""))
      : [];

    const stylesXml = workbookFileText(
      workbookFileByName(workbook, "xl/styles.xml")
    );

    if (!stylesXml) {
      return names.map(() => Object.create(null));
    }

    const styleCatalog = parseStyleCatalog(stylesXml);

    const workbookXml = workbookFileText(
      workbookFileByName(workbook, "xl/workbook.xml")
    );

    const relsXml = workbookFileText(
      workbookFileByName(
        workbook,
        "xl/_rels/workbook.xml.rels"
      )
    );

    const workbookSheets = parseWorkbookSheets(workbookXml);
    const relationships = parseWorkbookRelationships(relsXml);

    return names.map((name, requestedIndex) => {
      const workbookIndex = workbookSheets.findIndex(
        (sheet) => sheet.name === name
      );

      const sheet =
        workbookIndex >= 0
          ? workbookSheets[workbookIndex]
          : null;

      const relationshipPath =
        sheet && sheet.relId
          ? relationships[sheet.relId]
          : "";

      const fallbackIndex =
        workbookIndex >= 0
          ? workbookIndex
          : requestedIndex;

      const path =
        relationshipPath ||
        `xl/worksheets/sheet${fallbackIndex + 1}.xml`;

      const worksheetXml = workbookFileText(
        workbookFileByName(workbook, path)
      );

      return worksheetBorderMap(
        worksheetXml,
        styleCatalog
      );
    });
  }

  FM.spreadsheetXlsxBorders = {
    normalizeSide,
    normalizeBorder,
    isEmptyBorder,
    cssWidth,
    cssLineStyle,
    toXlsxStyle,
    fromXlsxSide,
    fromXlsxBorder,
    bordersBySheet
  };
})();
