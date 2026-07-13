window.PQNAS_FILEMGR = window.PQNAS_FILEMGR || {};

(() => {
  "use strict";

  const FM = window.PQNAS_FILEMGR;

  const EOCD_SIGNATURE = 0x06054b50;
  const CENTRAL_DIR_SIGNATURE = 0x02014b50;
  const LOCAL_FILE_SIGNATURE = 0x04034b50;
  const MAX_EOCD_SCAN = 65557;
  const REL_DRAWING_TYPE = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing";
  const DRAWING_CONTENT_TYPE = "application/vnd.openxmlformats-officedocument.drawing+xml";

  const IMAGE_CONTENT_TYPES = Object.freeze({
    jpeg: "image/jpeg",
    jpg: "image/jpeg",
    png: "image/png",
    gif: "image/gif",
    bmp: "image/bmp",
    tif: "image/tiff",
    tiff: "image/tiff",
    webp: "image/webp"
  });

  function safeName(name) {
    return String(name || "").replace(/\\/g, "/").replace(/^\/+/, "");
  }

  function isUnsafeZipName(name) {
    const normalized = safeName(name);
    return !normalized ||
      normalized.startsWith("../") ||
      normalized.includes("/../") ||
      normalized === ".." ||
      normalized.includes("\0");
  }

  function xmlAttr(value) {
    return String(value == null ? "" : value)
      .replace(/&/g, "&amp;")
      .replace(/"/g, "&quot;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;");
  }

  function findEndOfCentralDirectory(view) {
    const min = Math.max(0, view.byteLength - MAX_EOCD_SCAN);

    for (let pos = view.byteLength - 22; pos >= min; pos--) {
      if (view.getUint32(pos, true) === EOCD_SIGNATURE) return pos;
    }

    return -1;
  }

  function localDataOffset(view, entry) {
    const offset = Number(entry && entry.localHeaderOffset);
    if (!Number.isInteger(offset) || offset < 0 || offset + 30 > view.byteLength) return -1;
    if (view.getUint32(offset, true) !== LOCAL_FILE_SIGNATURE) return -1;

    const nameLen = view.getUint16(offset + 26, true);
    const extraLen = view.getUint16(offset + 28, true);
    const dataOffset = offset + 30 + nameLen + extraLen;

    return dataOffset <= view.byteLength ? dataOffset : -1;
  }

  function readZipEntries(arrayBuffer) {
    if (!(arrayBuffer instanceof ArrayBuffer)) return [];

    const view = new DataView(arrayBuffer);
    const eocd = findEndOfCentralDirectory(view);
    if (eocd < 0 || eocd + 22 > view.byteLength) return [];

    const totalEntries = view.getUint16(eocd + 10, true);
    const centralDirOffset = view.getUint32(eocd + 16, true);

    if (!Number.isInteger(centralDirOffset) || centralDirOffset < 0 || centralDirOffset >= view.byteLength) {
      return [];
    }

    const decoder = new TextDecoder("utf-8");
    const entries = [];
    let offset = centralDirOffset;

    for (let i = 0; i < totalEntries; i++) {
      if (offset + 46 > view.byteLength) break;
      if (view.getUint32(offset, true) !== CENTRAL_DIR_SIGNATURE) break;

      const flags = view.getUint16(offset + 8, true);
      const method = view.getUint16(offset + 10, true);
      const crc = view.getUint32(offset + 16, true);
      const compressedSize = view.getUint32(offset + 20, true);
      const uncompressedSize = view.getUint32(offset + 24, true);
      const nameLen = view.getUint16(offset + 28, true);
      const extraLen = view.getUint16(offset + 30, true);
      const commentLen = view.getUint16(offset + 32, true);
      const localHeaderOffset = view.getUint32(offset + 42, true);
      const nameStart = offset + 46;
      const nameEnd = nameStart + nameLen;

      if (nameEnd > view.byteLength) break;

      const name = safeName(decoder.decode(new Uint8Array(arrayBuffer, nameStart, nameLen)));

      // Security: XLSX image preservation stays inside the zip directory.
      // Traversal/NUL names are ignored and never treated as filesystem paths.
      if (!isUnsafeZipName(name)) {
        entries.push({
          name,
          flags,
          method,
          crc,
          compressedSize,
          uncompressedSize,
          localHeaderOffset
        });
      }

      offset = nameEnd + extraLen + commentLen;
    }

    return entries;
  }

  function compressedDataForEntry(arrayBuffer, entry) {
    if (!(arrayBuffer instanceof ArrayBuffer) || !entry) return null;

    const view = new DataView(arrayBuffer);
    const start = localDataOffset(view, entry);
    if (start < 0) return null;

    const size = Number(entry.compressedSize);
    if (!Number.isInteger(size) || size < 0) return null;

    const end = start + size;
    if (end > arrayBuffer.byteLength) return null;

    return new Uint8Array(arrayBuffer.slice(start, end));
  }

  function extensionForName(name) {
    const m = String(name || "").toLowerCase().match(/\.([a-z0-9]+)$/);
    return m ? m[1] : "";
  }

  function basename(path) {
    return String(path || "").split("/").pop() || "";
  }

  function drawingNumber(path) {
    const m = String(path || "").match(/^xl\/drawings\/drawing(\d+)\.xml$/i);
    return m ? Number(m[1]) : null;
  }

  function worksheetIndexForDrawing(drawingPath, sheetCount, drawingCount) {
    const n = drawingNumber(drawingPath);
    if (Number.isInteger(n) && n >= 1 && n <= sheetCount) return n - 1;
    if (drawingCount === 1 && sheetCount > 0) return 0;
    return null;
  }

  function targetForDrawing(drawingPath) {
    return `../drawings/${basename(drawingPath)}`;
  }

  function preserveEntryFromZip(arrayBuffer, entry) {
    if (!entry || (entry.method !== 0 && entry.method !== 8)) return null;

    const compressedData = compressedDataForEntry(arrayBuffer, entry);
    if (!compressedData) return null;

    return {
      name: entry.name,
      raw: {
        method: entry.method,
        crc: entry.crc >>> 0,
        compressedSize: compressedData.length,
        uncompressedSize: entry.uncompressedSize >>> 0,
        compressedData
      }
    };
  }

  function buildSheetDrawingMap(drawings, sheetCount) {
    const out = [];
    const usedSheets = new Set();

    for (const drawingPath of drawings) {
      const sheetIndex = worksheetIndexForDrawing(drawingPath, sheetCount, drawings.length);
      if (!Number.isInteger(sheetIndex) || sheetIndex < 0 || sheetIndex >= sheetCount) continue;
      if (usedSheets.has(sheetIndex)) continue;

      usedSheets.add(sheetIndex);
      out.push({
        sheetIndex,
        relId: `rIdPqnasDrawing${sheetIndex + 1}`,
        target: targetForDrawing(drawingPath),
        drawing: drawingPath
      });
    }

    return out;
  }

  function inspectArrayBuffer(arrayBuffer) {
    const entries = readZipEntries(arrayBuffer);
    const byName = new Map(entries.map((entry) => [entry.name, entry]));
    const names = entries.map((entry) => entry.name);

    const media = names.filter((name) => /^xl\/media\/[^/]+$/i.test(name)).sort();
    const drawings = names.filter((name) => /^xl\/drawings\/drawing\d+\.xml$/i.test(name)).sort();
    const drawingRels = names.filter((name) => /^xl\/drawings\/_rels\/drawing\d+\.xml\.rels$/i.test(name)).sort();
    const worksheetRels = names.filter((name) => /^xl\/worksheets\/_rels\/sheet\d+\.xml\.rels$/i.test(name)).sort();

    const preserveNames = new Set([...media, ...drawings, ...drawingRels]);
    const preservedEntries = [];

    for (const name of preserveNames) {
      const preserved = preserveEntryFromZip(arrayBuffer, byName.get(name));
      if (preserved) preservedEntries.push(preserved);
    }

    return {
      entryCount: entries.length,
      imageCount: media.length,
      drawingCount: drawings.length,
      drawingRelationshipCount: drawingRels.length,
      worksheetRelationshipCount: worksheetRels.length,
      hasImages: media.length > 0 || drawings.length > 0,
      media,
      drawings,
      drawingRels,
      worksheetRels,
      preservedEntries,
      sheetDrawings: []
    };
  }

  function prepareExport(info, sheetCount) {
    if (!info || !info.hasImages) return null;

    const drawings = Array.isArray(info.drawings) ? info.drawings.slice() : [];
    const media = Array.isArray(info.media) ? info.media.slice() : [];
    const preservedEntries = Array.isArray(info.preservedEntries) ? info.preservedEntries.slice() : [];
    const sheetDrawings = buildSheetDrawingMap(drawings, Math.max(0, Number(sheetCount) || 0));

    if (!preservedEntries.length || !sheetDrawings.length) return null;

    return {
      media,
      drawings,
      preservedEntries,
      sheetDrawings
    };
  }

  function summaryText(info) {
    if (!info || !info.hasImages) return "";
    return `images=${info.imageCount}, drawings=${info.drawingCount}, drawingRels=${info.drawingRelationshipCount}`;
  }

  function contentTypeDefaultsXml(exportInfo) {
    if (!exportInfo) return "";

    const seen = new Set();
    const out = [];

    for (const name of exportInfo.media || []) {
      const ext = extensionForName(name);
      const contentType = IMAGE_CONTENT_TYPES[ext];
      if (!ext || !contentType || seen.has(ext)) continue;

      seen.add(ext);
      out.push(`<Default Extension="${xmlAttr(ext)}" ContentType="${xmlAttr(contentType)}"/>`);
    }

    return out.join("");
  }

  function contentTypeOverridesXml(exportInfo) {
    if (!exportInfo) return "";

    const seen = new Set();
    const out = [];

    for (const name of exportInfo.drawings || []) {
      if (seen.has(name)) continue;
      seen.add(name);
      out.push(`<Override PartName="/${xmlAttr(name)}" ContentType="${DRAWING_CONTENT_TYPE}"/>`);
    }

    return out.join("");
  }

  function worksheetDrawingRelId(exportInfo, sheetIndex) {
    if (!exportInfo || !Array.isArray(exportInfo.sheetDrawings)) return "";

    const item = exportInfo.sheetDrawings.find((drawing) => drawing.sheetIndex === sheetIndex);
    return item ? item.relId : "";
  }

  function worksheetRelXml(drawing) {
    return [
      '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>',
      '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">',
      `<Relationship Id="${xmlAttr(drawing.relId)}" Type="${REL_DRAWING_TYPE}" Target="${xmlAttr(drawing.target)}"/>`,
      '</Relationships>'
    ].join("");
  }

  function appendExportEntries(entries, exportInfo) {
    if (!Array.isArray(entries) || !exportInfo) return;

    const existing = new Set(entries.map((entry) => entry && entry.name).filter(Boolean));

    for (const entry of exportInfo.preservedEntries || []) {
      if (!entry || !entry.name || existing.has(entry.name)) continue;
      entries.push(entry);
      existing.add(entry.name);
    }

    for (const drawing of exportInfo.sheetDrawings || []) {
      const sheetNo = drawing.sheetIndex + 1;
      const relName = `xl/worksheets/_rels/sheet${sheetNo}.xml.rels`;

      if (existing.has(relName)) continue;

      entries.push({
        name: relName,
        data: worksheetRelXml(drawing)
      });
      existing.add(relName);
    }
  }


  const MAX_OVERLAY_IMAGES = 20;
  const MAX_OVERLAY_IMAGE_BYTES = 8 * 1024 * 1024;

  function workbookFileBytes(file) {
    const content = file && file.content != null ? file.content : null;
    if (content instanceof Uint8Array) return content;
    if (content instanceof ArrayBuffer) return new Uint8Array(content);
    if (typeof content === "string") {
      const out = new Uint8Array(content.length);
      for (let i = 0; i < content.length; i++) out[i] = content.charCodeAt(i) & 0xFF;
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

  function bytesToBase64(bytes) {
    if (!(bytes instanceof Uint8Array)) return "";

    let binary = "";
    const chunk = 0x8000;

    for (let i = 0; i < bytes.length; i += chunk) {
      binary += String.fromCharCode.apply(null, bytes.subarray(i, i + chunk));
    }

    return btoa(binary);
  }

  function dirname(path) {
    const normalized = safeName(path);
    const idx = normalized.lastIndexOf("/");
    return idx >= 0 ? normalized.slice(0, idx) : "";
  }

  function normalizeZipPath(path) {
    const parts = String(path || "").replace(/\\/g, "/").split("/");
    const out = [];

    for (const part of parts) {
      if (!part || part === ".") continue;
      if (part === "..") {
        out.pop();
        continue;
      }
      out.push(part);
    }

    return safeName(out.join("/"));
  }

  function resolveZipTarget(sourcePart, target) {
    const raw = String(target || "").replace(/\\/g, "/");
    if (!raw || raw.includes("\0")) return "";

    if (raw.startsWith("/")) return normalizeZipPath(raw.slice(1));
    return normalizeZipPath(`${dirname(sourcePart)}/${raw}`);
  }

  function drawingRelsPath(drawingPath) {
    return `${dirname(drawingPath)}/_rels/${basename(drawingPath)}.rels`;
  }

  function workbookSheetPart(sheetIndex) {
    return `xl/worksheets/sheet${sheetIndex + 1}.xml`;
  }

  function workbookSheetRelsPart(sheetIndex) {
    return `xl/worksheets/_rels/sheet${sheetIndex + 1}.xml.rels`;
  }

  function localNameElements(root, localName) {
    if (!root || !root.getElementsByTagName) return [];
    return Array.from(root.getElementsByTagName("*")).filter((el) => el.localName === localName);
  }

  function directLocalNameElement(root, localName) {
    if (!root || !root.children) return null;

    for (const child of Array.from(root.children)) {
      if (child.localName === localName) return child;
    }

    return null;
  }

  function localText(root, localName) {
    const el = directLocalNameElement(root, localName);
    return el ? String(el.textContent || "") : "";
  }

  function relAttr(el, name) {
    if (!el || !el.getAttribute) return "";
    return el.getAttribute(name) || el.getAttribute(`r:${name}`) || "";
  }

  function parseXml(text) {
    if (typeof DOMParser === "undefined") return null;

    try {
      const doc = new DOMParser().parseFromString(String(text || ""), "application/xml");
      if (doc.getElementsByTagName("parsererror").length) return null;
      return doc;
    } catch (_) {
      return null;
    }
  }

  function parseRelationships(xmlText) {
    const doc = parseXml(xmlText);
    if (!doc) return [];

    return localNameElements(doc, "Relationship").map((el) => ({
      id: el.getAttribute("Id") || "",
      type: el.getAttribute("Type") || "",
      target: el.getAttribute("Target") || ""
    })).filter((rel) => rel.id && rel.target);
  }

  function parseMarker(anchor, markerName) {
    const marker = directLocalNameElement(anchor, markerName);
    if (!marker) return null;

    return {
      col: Number(localText(marker, "col")) || 0,
      colOff: Number(localText(marker, "colOff")) || 0,
      row: Number(localText(marker, "row")) || 0,
      rowOff: Number(localText(marker, "rowOff")) || 0
    };
  }

  function firstBlipEmbed(anchor) {
    const blips = localNameElements(anchor, "blip");
    const blip = blips[0];
    return relAttr(blip, "embed");
  }

  function firstPictureName(anchor) {
    const props = localNameElements(anchor, "cNvPr")[0];
    return {
      name: props && props.getAttribute ? props.getAttribute("name") || "" : "",
      descr: props && props.getAttribute ? props.getAttribute("descr") || "" : ""
    };
  }

  function mediaDataUrl(mediaPath, file) {
    const bytes = workbookFileBytes(file);
    if (!bytes || bytes.byteLength <= 0 || bytes.byteLength > MAX_OVERLAY_IMAGE_BYTES) return "";

    const ext = extensionForName(mediaPath);
    const mime = IMAGE_CONTENT_TYPES[ext];
    if (!mime) return "";

    const encoded = bytesToBase64(bytes);
    return encoded ? `data:${mime};base64,${encoded}` : "";
  }

  function imagesFromDrawing(files, sheetIndex, drawingPath) {
    const drawingXml = workbookFileText(files[drawingPath]);
    const drawingDoc = parseXml(drawingXml);
    if (!drawingDoc) return [];

    const relsPath = drawingRelsPath(drawingPath);
    const rels = parseRelationships(workbookFileText(files[relsPath]));
    const relTargets = new Map();

    for (const rel of rels) {
      relTargets.set(rel.id, resolveZipTarget(drawingPath, rel.target));
    }

    const anchors = localNameElements(drawingDoc, "twoCellAnchor");
    const out = [];

    for (const anchor of anchors) {
      const from = parseMarker(anchor, "from");
      const to = parseMarker(anchor, "to");
      const embedId = firstBlipEmbed(anchor);

      if (!from || !to || !embedId) continue;

      const mediaPath = relTargets.get(embedId);
      const src = mediaDataUrl(mediaPath, files[mediaPath]);
      if (!src) continue;

      out.push({
        sheetIndex,
        mediaPath,
        drawingPath,
        from,
        to,
        ...firstPictureName(anchor),
        src
      });

      if (out.length >= MAX_OVERLAY_IMAGES) break;
    }

    return out;
  }

  function imagesFromWorkbookFiles(wb, visibleSheetNames) {
    const files = wb && wb.files && typeof wb.files === "object" ? wb.files : null;
    const sheetNames = Array.isArray(visibleSheetNames) ? visibleSheetNames : [];

    if (!files || !sheetNames.length) return [];

    const out = [];

    for (let sheetIndex = 0; sheetIndex < sheetNames.length; sheetIndex++) {
      const sheetPart = workbookSheetPart(sheetIndex);
      const relsPart = workbookSheetRelsPart(sheetIndex);
      const rels = parseRelationships(workbookFileText(files[relsPart]));

      for (const rel of rels) {
        if (rel.type !== REL_DRAWING_TYPE) continue;

        const drawingPath = resolveZipTarget(sheetPart, rel.target);
        if (!drawingPath || isUnsafeZipName(drawingPath)) continue;

        out.push(...imagesFromDrawing(files, sheetIndex, drawingPath));
        if (out.length >= MAX_OVERLAY_IMAGES) return out.slice(0, MAX_OVERLAY_IMAGES);
      }
    }

    return out.slice(0, MAX_OVERLAY_IMAGES);
  }

  FM.spreadsheetXlsxImages = {
    inspectArrayBuffer,
    prepareExport,
    summaryText,
    contentTypeDefaultsXml,
    contentTypeOverridesXml,
    worksheetDrawingRelId,
    appendExportEntries,
    imagesFromWorkbookFiles
  };
})();
