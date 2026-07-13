window.PQNAS_FILEMGR = window.PQNAS_FILEMGR || {};

(() => {
  "use strict";

  const FM = window.PQNAS_FILEMGR;

  const EOCD_SIGNATURE = 0x06054b50;
  const CENTRAL_DIR_SIGNATURE = 0x02014b50;
  const MAX_EOCD_SCAN = 65557;

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

  function findEndOfCentralDirectory(view) {
    const min = Math.max(0, view.byteLength - MAX_EOCD_SCAN);

    for (let pos = view.byteLength - 22; pos >= min; pos--) {
      if (view.getUint32(pos, true) === EOCD_SIGNATURE) {
        return pos;
      }
    }

    return -1;
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

      const method = view.getUint16(offset + 10, true);
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

      // Security: XLSX image inspection stays inside the zip directory.
      // Names with traversal or NULs are ignored and never treated as paths.
      if (!isUnsafeZipName(name)) {
        entries.push({
          name,
          method,
          compressedSize,
          uncompressedSize,
          localHeaderOffset
        });
      }

      offset = nameEnd + extraLen + commentLen;
    }

    return entries;
  }

  function inspectArrayBuffer(arrayBuffer) {
    const entries = readZipEntries(arrayBuffer);
    const names = entries.map((entry) => entry.name);
    const media = names.filter((name) => /^xl\/media\/[^/]+$/i.test(name));
    const drawings = names.filter((name) => /^xl\/drawings\/drawing\d+\.xml$/i.test(name));
    const drawingRels = names.filter((name) => /^xl\/drawings\/_rels\/drawing\d+\.xml\.rels$/i.test(name));
    const worksheetRels = names.filter((name) => /^xl\/worksheets\/_rels\/sheet\d+\.xml\.rels$/i.test(name));

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
      worksheetRels
    };
  }

  function summaryText(info) {
    if (!info || !info.hasImages) return "";
    return `images=${info.imageCount}, drawings=${info.drawingCount}, drawingRels=${info.drawingRelationshipCount}`;
  }

  FM.spreadsheetXlsxImages = {
    inspectArrayBuffer,
    summaryText
  };
})();
