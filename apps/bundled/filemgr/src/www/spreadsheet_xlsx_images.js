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

  FM.spreadsheetXlsxImages = {
    inspectArrayBuffer,
    prepareExport,
    summaryText,
    contentTypeDefaultsXml,
    contentTypeOverridesXml,
    worksheetDrawingRelId,
    appendExportEntries
  };
})();
