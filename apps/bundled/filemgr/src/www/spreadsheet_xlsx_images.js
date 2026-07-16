window.PQNAS_FILEMGR = window.PQNAS_FILEMGR || {};

(() => {
  "use strict";

  const FM = window.PQNAS_FILEMGR;

  const EOCD_SIGNATURE = 0x06054b50;
  const CENTRAL_DIR_SIGNATURE = 0x02014b50;
  const LOCAL_FILE_SIGNATURE = 0x04034b50;
  const MAX_EOCD_SCAN = 65557;
  const REL_DRAWING_TYPE = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing";
  const REL_IMAGE_TYPE = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image";
  const DRAWING_CONTENT_TYPE = "application/vnd.openxmlformats-officedocument.drawing+xml";

  const EMU_PER_PIXEL = 9525;
  const MAX_INSERTED_IMAGE_BYTES = 8 * 1024 * 1024;
  const MAX_INSERTED_IMAGE_EDGE = 16384;
  const MAX_INSERTED_IMAGE_PIXELS = 40 * 1000 * 1000;
  const MAX_INSERTED_DISPLAY_WIDTH = 480;
  const MAX_INSERTED_DISPLAY_HEIGHT = 360;
  const MIN_INSERTED_DISPLAY_EDGE = 8;

  /*
   * Binary image assets stay outside sheet.images so the JSON-based undo
   * history does not duplicate several megabytes of image data per snapshot.
   */
  const insertedImageAssets = new Map();
  let insertedImageSequence = 0;

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

  function normalizedExportSheets(sheetsOrCount) {
    if (Array.isArray(sheetsOrCount)) {
      return sheetsOrCount;
    }

    const count = Math.max(
      0,
      Math.floor(Number(sheetsOrCount) || 0)
    );

    return Array.from(
      { length: count },
      () => null
    );
  }

  function usedDrawingPartNumbers(paths) {
    const out = new Set();

    for (const path of paths || []) {
      const number = drawingNumber(path);

      if (
        Number.isInteger(number) &&
        number > 0
      ) {
        out.add(number);
      }
    }

    return out;
  }

  function usedMediaPartNumbers(paths) {
    const out = new Set();

    for (const path of paths || []) {
      const match = String(path || "").match(
        /^xl\/media\/image(\d+)\.[a-z0-9]+$/i
      );

      if (!match) continue;

      const number = Number(match[1]);

      if (
        Number.isInteger(number) &&
        number > 0
      ) {
        out.add(number);
      }
    }

    return out;
  }

  function nextFreePartNumber(used, start = 1) {
    let number = Math.max(
      1,
      Math.floor(Number(start) || 1)
    );

    while (used.has(number)) {
      number++;
    }

    return number;
  }

  function normalizedImageMarker(marker) {
    return {
      col: Math.max(
        0,
        Math.floor(Number(marker && marker.col) || 0)
      ),
      colOff: Math.max(
        0,
        Math.round(Number(marker && marker.colOff) || 0)
      ),
      row: Math.max(
        0,
        Math.floor(Number(marker && marker.row) || 0)
      ),
      rowOff: Math.max(
        0,
        Math.round(Number(marker && marker.rowOff) || 0)
      )
    };
  }

  function markerPixelPosition(
    values,
    marker,
    fallback,
    indexKey,
    offsetKey
  ) {
    const normalized = normalizedImageMarker(marker);

    /*
     * Security: cap malformed anchor traversal. Inserted images normally
     * reference only the small visible spreadsheet area.
     */
    const index = Math.min(
      100000,
      normalized[indexKey]
    );

    let pixels = 0;

    for (let i = 0; i < index; i++) {
      pixels += sheetDimensionAt(
        values,
        i,
        fallback
      );
    }

    pixels += normalized[offsetKey] / EMU_PER_PIXEL;

    return pixels;
  }

  function imageExtentEmu(image, sheet) {
    const defaultColWidth = Math.max(
      1,
      Number(sheet && sheet.defaultColWidth) || 80
    );

    const defaultRowHeight = Math.max(
      1,
      Number(sheet && sheet.defaultRowHeight) || 20
    );

    const from = normalizedImageMarker(
      image && image.from
    );

    const to = normalizedImageMarker(
      image && image.to
    );

    const left = markerPixelPosition(
      sheet && sheet.colWidths,
      from,
      defaultColWidth,
      "col",
      "colOff"
    );

    const right = markerPixelPosition(
      sheet && sheet.colWidths,
      to,
      defaultColWidth,
      "col",
      "colOff"
    );

    const top = markerPixelPosition(
      sheet && sheet.rowHeights,
      from,
      defaultRowHeight,
      "row",
      "rowOff"
    );

    const bottom = markerPixelPosition(
      sheet && sheet.rowHeights,
      to,
      defaultRowHeight,
      "row",
      "rowOff"
    );

    const storedWidth = Number(
      image && image.displayWidth
    );

    const storedHeight = Number(
      image && image.displayHeight
    );

    const widthPx = right > left
      ? right - left
      : (
          Number.isFinite(storedWidth) &&
          storedWidth > 0
            ? storedWidth
            : 1
        );

    const heightPx = bottom > top
      ? bottom - top
      : (
          Number.isFinite(storedHeight) &&
          storedHeight > 0
            ? storedHeight
            : 1
        );

    return {
      cx: Math.max(
        1,
        Math.round(widthPx * EMU_PER_PIXEL)
      ),
      cy: Math.max(
        1,
        Math.round(heightPx * EMU_PER_PIXEL)
      )
    };
  }

  function drawingMarkerXml(name, marker) {
    const value = normalizedImageMarker(marker);

    return [
      `<xdr:${name}>`,
      `<xdr:col>${value.col}</xdr:col>`,
      `<xdr:colOff>${value.colOff}</xdr:colOff>`,
      `<xdr:row>${value.row}</xdr:row>`,
      `<xdr:rowOff>${value.rowOff}</xdr:rowOff>`,
      `</xdr:${name}>`
    ].join("");
  }

  function normalizedAnchorEditAs(value) {
    const editAs = String(value || "");

    if (
      editAs === "twoCell" ||
      editAs === "absolute"
    ) {
      return editAs;
    }

    return "oneCell";
  }

  function insertedDrawingXml(items, sheet) {
    const anchors = [];

    for (let index = 0; index < items.length; index++) {
      const item = items[index];
      const image = item.image;
      const pictureId = index + 1;
      const extent = imageExtentEmu(
        image,
        sheet
      );

      const pictureName = String(
        image && image.name ||
        `Picture ${pictureId}`
      ).slice(0, 180);

      const description = String(
        image && image.descr ||
        ""
      ).slice(0, 500);

      const descriptionAttr = description
        ? ` descr="${xmlAttr(description)}"`
        : "";

      anchors.push([
        `<xdr:twoCellAnchor editAs="${xmlAttr(normalizedAnchorEditAs(image && image.editAs))}">`,
        drawingMarkerXml("from", image && image.from),
        drawingMarkerXml("to", image && image.to),
        "<xdr:pic>",
        "<xdr:nvPicPr>",
        `<xdr:cNvPr id="${pictureId}" name="${xmlAttr(pictureName)}"${descriptionAttr}/>`,
        "<xdr:cNvPicPr/>",
        "</xdr:nvPicPr>",
        "<xdr:blipFill>",
        `<a:blip r:embed="${xmlAttr(item.relId)}"/>`,
        "<a:stretch><a:fillRect/></a:stretch>",
        "</xdr:blipFill>",
        "<xdr:spPr>",
        "<a:xfrm>",
        '<a:off x="0" y="0"/>',
        `<a:ext cx="${extent.cx}" cy="${extent.cy}"/>`,
        "</a:xfrm>",
        '<a:prstGeom prst="rect"><a:avLst/></a:prstGeom>',
        "</xdr:spPr>",
        "</xdr:pic>",
        "<xdr:clientData/>",
        "</xdr:twoCellAnchor>"
      ].join(""));
    }

    return [
      '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>',
      '<xdr:wsDr xmlns:xdr="http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing" xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">',
      anchors.join(""),
      "</xdr:wsDr>"
    ].join("");
  }

  function insertedDrawingRelationshipsXml(items) {
    const relationships = items.map((item) => {
      const target = `../media/${basename(item.mediaPath)}`;

      return `<Relationship Id="${xmlAttr(item.relId)}" Type="${REL_IMAGE_TYPE}" Target="${xmlAttr(target)}"/>`;
    });

    return [
      '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>',
      '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">',
      relationships.join(""),
      "</Relationships>"
    ].join("");
  }

  function prepareExport(info, sheetsOrCount) {
    const sheets = normalizedExportSheets(
      sheetsOrCount
    );

    const preservedEntries = Array.isArray(
      info && info.preservedEntries
    )
      ? info.preservedEntries.slice()
      : [];

    /*
     * Preserve original drawing parts only as a complete set. Partial ZIP
     * preservation must not produce relationships to missing drawing files.
     */
    const preserveOriginal =
      !!(info && info.hasImages) &&
      preservedEntries.length > 0;

    const drawings = preserveOriginal &&
      Array.isArray(info.drawings)
        ? info.drawings.slice()
        : [];

    const media = preserveOriginal &&
      Array.isArray(info.media)
        ? info.media.slice()
        : [];

    const sheetDrawings = preserveOriginal
      ? buildSheetDrawingMap(
          drawings,
          sheets.length
        )
      : [];

    const generatedEntries = [];

    const usedDrawingNumbers =
      usedDrawingPartNumbers(drawings);

    const usedMediaNumbers =
      usedMediaPartNumbers(media);

    let nextDrawingNumber =
      nextFreePartNumber(usedDrawingNumbers);

    let nextMediaNumber =
      nextFreePartNumber(usedMediaNumbers);

    for (
      let sheetIndex = 0;
      sheetIndex < sheets.length;
      sheetIndex++
    ) {
      const sheet = sheets[sheetIndex];

      const insertedImages = Array.isArray(
        sheet && sheet.images
      )
        ? sheet.images.filter(
            (image) =>
              image &&
              image.source === "inserted"
          )
        : [];

      if (!insertedImages.length) continue;

      /*
       * Existing drawing XML is preserved byte-for-byte. Do not replace or
       * merge it until unknown shapes, charts and controls can be retained
       * safely.
       */
      if (
        sheetDrawings.some(
          (drawing) =>
            drawing.sheetIndex === sheetIndex
        )
      ) {
        throw insertedImageError(
          "inserted_image_drawing_conflict",
          "Cannot safely merge an inserted image with an existing drawing part."
        );
      }

      const drawingNumberValue =
        nextDrawingNumber;

      usedDrawingNumbers.add(
        drawingNumberValue
      );

      nextDrawingNumber = nextFreePartNumber(
        usedDrawingNumbers,
        drawingNumberValue + 1
      );

      const drawingPath =
        `xl/drawings/drawing${drawingNumberValue}.xml`;

      const drawingRelsPath =
        `xl/drawings/_rels/drawing${drawingNumberValue}.xml.rels`;

      const drawingItems = [];

      for (
        let imageIndex = 0;
        imageIndex < insertedImages.length;
        imageIndex++
      ) {
        const image = insertedImages[imageIndex];
        const asset = insertedAssetForImage(image);

        if (
          !asset ||
          !(asset.bytes instanceof Uint8Array) ||
          !asset.bytes.length
        ) {
          throw insertedImageError(
            "inserted_image_asset_missing",
            "The inserted image data is no longer available."
          );
        }

        const extension =
          asset.extension === "png"
            ? "png"
            : "jpeg";

        const mediaNumberValue =
          nextMediaNumber;

        usedMediaNumbers.add(
          mediaNumberValue
        );

        nextMediaNumber = nextFreePartNumber(
          usedMediaNumbers,
          mediaNumberValue + 1
        );

        const mediaPath =
          `xl/media/image${mediaNumberValue}.${extension}`;

        const relId =
          `rId${imageIndex + 1}`;

        generatedEntries.push({
          name: mediaPath,
          data: asset.bytes
        });

        media.push(mediaPath);

        drawingItems.push({
          image,
          mediaPath,
          relId
        });
      }

      generatedEntries.push({
        name: drawingPath,
        data: insertedDrawingXml(
          drawingItems,
          sheet
        )
      });

      generatedEntries.push({
        name: drawingRelsPath,
        data: insertedDrawingRelationshipsXml(
          drawingItems
        )
      });

      drawings.push(drawingPath);

      sheetDrawings.push({
        sheetIndex,
        relId: `rIdPqnasDrawing${sheetIndex + 1}`,
        target: targetForDrawing(drawingPath),
        drawing: drawingPath
      });
    }

    if (!sheetDrawings.length) return null;

    return {
      media,
      drawings,
      preservedEntries,
      generatedEntries,
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

    for (const entry of exportInfo.generatedEntries || []) {
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


  function drawingAnchorEditAs(anchor) {
    const raw = String(
      anchor &&
      anchor.getAttribute &&
      anchor.getAttribute("editAs") ||
      ""
    );

    if (
      raw === "oneCell" ||
      raw === "twoCell" ||
      raw === "absolute"
    ) {
      return raw;
    }

    /*
     * A twoCellAnchor without editAs uses the normal two-cell behavior.
     */
    return "twoCell";
  }

  function firstPictureTransform(anchor) {
    const transforms = localNameElements(
      anchor,
      "xfrm"
    );

    for (const transform of transforms) {
      const off = directLocalNameElement(
        transform,
        "off"
      );

      const ext = directLocalNameElement(
        transform,
        "ext"
      );

      if (!ext || !ext.getAttribute) continue;

      const x = Number(
        off && off.getAttribute
          ? off.getAttribute("x")
          : 0
      );

      const y = Number(
        off && off.getAttribute
          ? off.getAttribute("y")
          : 0
      );

      const cx = Number(ext.getAttribute("cx"));
      const cy = Number(ext.getAttribute("cy"));

      if (
        !Number.isFinite(cx) ||
        !Number.isFinite(cy) ||
        cx <= 0 ||
        cy <= 0
      ) {
        continue;
      }

      return {
        x: Number.isFinite(x) ? x : 0,
        y: Number.isFinite(y) ? y : 0,
        cx,
        cy
      };
    }

    return null;
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
        editAs: drawingAnchorEditAs(anchor),
        transform: firstPictureTransform(anchor),
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


  function imageAnchorBounds(images) {
    const list = Array.isArray(images) ? images : [];
    let rows = 0;
    let cols = 0;

    for (const image of list) {
      const from = image && image.from;
      const to = image && image.to;

      for (const marker of [from, to]) {
        const row = Number(marker && marker.row);
        const col = Number(marker && marker.col);

        if (Number.isInteger(row) && row >= 0) rows = Math.max(rows, row + 1);
        if (Number.isInteger(col) && col >= 0) cols = Math.max(cols, col + 1);
      }
    }

    return { rows, cols };
  }

  function expandSheetForImages(sheet, options = {}) {
    if (!sheet || !Array.isArray(sheet.images) || !sheet.images.length) return sheet;

    const bounds = imageAnchorBounds(sheet.images);
    const defaultColWidth = Number(options.defaultColWidth) || 120;
    const defaultRowHeight = Number(options.defaultRowHeight) || 28;

    if (!Array.isArray(sheet.rows)) sheet.rows = [];
    if (!Array.isArray(sheet.colWidths)) sheet.colWidths = [];
    if (!Array.isArray(sheet.rowHeights)) sheet.rowHeights = [];

    while (sheet.rows.length < bounds.rows) sheet.rows.push([]);

    for (let r = 0; r < sheet.rows.length; r++) {
      if (!Array.isArray(sheet.rows[r])) sheet.rows[r] = [];
      while (sheet.rows[r].length < bounds.cols) sheet.rows[r].push("");
    }

    while (sheet.colWidths.length < bounds.cols) sheet.colWidths.push(defaultColWidth);
    while (sheet.rowHeights.length < bounds.rows) sheet.rowHeights.push(defaultRowHeight);

    return sheet;
  }


  function insertedImageError(code, message) {
    const error = new Error(message);
    error.code = code;
    return error;
  }

  function cleanInsertedImageName(value) {
    const name = basename(
      String(value == null ? "" : value)
        .replace(/[\u0000-\u001F\u007F]/g, "")
        .trim()
    );

    return name.slice(0, 180) || "image";
  }

  function pngImageInfo(bytes) {
    if (!(bytes instanceof Uint8Array) || bytes.length < 24) return null;

    const signature = [
      0x89, 0x50, 0x4E, 0x47,
      0x0D, 0x0A, 0x1A, 0x0A
    ];

    for (let i = 0; i < signature.length; i++) {
      if (bytes[i] !== signature[i]) return null;
    }

    if (
      bytes[12] !== 0x49 ||
      bytes[13] !== 0x48 ||
      bytes[14] !== 0x44 ||
      bytes[15] !== 0x52
    ) {
      return null;
    }

    const view = new DataView(
      bytes.buffer,
      bytes.byteOffset,
      bytes.byteLength
    );

    return {
      mimeType: "image/png",
      extension: "png",
      width: view.getUint32(16, false),
      height: view.getUint32(20, false)
    };
  }

  function isJpegStartOfFrameMarker(marker) {
    return (
      marker === 0xC0 ||
      marker === 0xC1 ||
      marker === 0xC2 ||
      marker === 0xC3 ||
      marker === 0xC5 ||
      marker === 0xC6 ||
      marker === 0xC7 ||
      marker === 0xC9 ||
      marker === 0xCA ||
      marker === 0xCB ||
      marker === 0xCD ||
      marker === 0xCE ||
      marker === 0xCF
    );
  }

  function jpegImageInfo(bytes) {
    if (
      !(bytes instanceof Uint8Array) ||
      bytes.length < 11 ||
      bytes[0] !== 0xFF ||
      bytes[1] !== 0xD8
    ) {
      return null;
    }

    let offset = 2;

    while (offset + 3 < bytes.length) {
      while (offset < bytes.length && bytes[offset] === 0xFF) {
        offset++;
      }

      if (offset >= bytes.length) break;

      const marker = bytes[offset++];

      if (marker === 0xD9 || marker === 0xDA) break;

      if (
        marker === 0x01 ||
        (marker >= 0xD0 && marker <= 0xD7)
      ) {
        continue;
      }

      if (offset + 1 >= bytes.length) break;

      const segmentLength =
        (bytes[offset] << 8) |
        bytes[offset + 1];

      if (
        segmentLength < 2 ||
        offset + segmentLength > bytes.length
      ) {
        break;
      }

      if (
        isJpegStartOfFrameMarker(marker) &&
        segmentLength >= 7
      ) {
        const height =
          (bytes[offset + 3] << 8) |
          bytes[offset + 4];

        const width =
          (bytes[offset + 5] << 8) |
          bytes[offset + 6];

        return {
          mimeType: "image/jpeg",
          extension: "jpeg",
          width,
          height
        };
      }

      offset += segmentLength;
    }

    return null;
  }

  function validatedInsertedImageInfo(bytes) {
    const info =
      pngImageInfo(bytes) ||
      jpegImageInfo(bytes);

    if (!info) {
      throw insertedImageError(
        "unsupported_image_type",
        "Only valid PNG and JPEG images are supported."
      );
    }

    const width = Number(info.width);
    const height = Number(info.height);
    const pixels = width * height;

    /*
     * Security: reject dangerous dimensions before the browser attempts to
     * decode the image. A small compressed file can otherwise expand into a
     * very large in-memory bitmap.
     */
    if (
      !Number.isInteger(width) ||
      !Number.isInteger(height) ||
      width <= 0 ||
      height <= 0 ||
      width > MAX_INSERTED_IMAGE_EDGE ||
      height > MAX_INSERTED_IMAGE_EDGE ||
      !Number.isSafeInteger(pixels) ||
      pixels > MAX_INSERTED_IMAGE_PIXELS
    ) {
      throw insertedImageError(
        "image_dimensions_too_large",
        "The image dimensions are too large."
      );
    }

    return info;
  }

  function nextInsertedImageToken(prefix) {
    insertedImageSequence++;

    let randomPart = "";

    try {
      if (
        typeof crypto !== "undefined" &&
        typeof crypto.randomUUID === "function"
      ) {
        randomPart = crypto.randomUUID();
      } else if (
        typeof crypto !== "undefined" &&
        typeof crypto.getRandomValues === "function"
      ) {
        const randomBytes = new Uint8Array(12);
        crypto.getRandomValues(randomBytes);
        randomPart = Array.from(
          randomBytes,
          (value) => value.toString(16).padStart(2, "0")
        ).join("");
      }
    } catch (_) {}

    if (!randomPart) {
      randomPart = [
        Date.now().toString(36),
        insertedImageSequence.toString(36)
      ].join("-");
    }

    return `${prefix}-${randomPart}`;
  }

  function sheetDimensionAt(values, index, fallback) {
    const raw = Number(
      Array.isArray(values) ? values[index] : null
    );

    if (Number.isFinite(raw) && raw > 0) return raw;

    const defaultValue = Number(fallback);
    return Number.isFinite(defaultValue) && defaultValue > 0
      ? defaultValue
      : 1;
  }

  function markerAfterPixels(
    values,
    startIndex,
    pixels,
    fallback,
    indexKey,
    offsetKey
  ) {
    let index = Math.max(
      0,
      Math.floor(Number(startIndex) || 0)
    );

    let remaining = Math.max(
      0,
      Number(pixels) || 0
    );

    let guard = 0;

    while (guard++ < 100000) {
      const size = sheetDimensionAt(
        values,
        index,
        fallback
      );

      if (remaining < size) break;

      remaining -= size;
      index++;

      if (remaining <= 0.000001) {
        remaining = 0;
        break;
      }
    }

    return {
      [indexKey]: index,
      [offsetKey]: Math.max(
        0,
        Math.round(remaining * EMU_PER_PIXEL)
      )
    };
  }

  function insertedDisplaySize(width, height) {
    const naturalWidth = Math.max(1, Number(width) || 1);
    const naturalHeight = Math.max(1, Number(height) || 1);

    const scale = Math.min(
      1,
      MAX_INSERTED_DISPLAY_WIDTH / naturalWidth,
      MAX_INSERTED_DISPLAY_HEIGHT / naturalHeight
    );

    return {
      width: Math.max(
        MIN_INSERTED_DISPLAY_EDGE,
        naturalWidth * scale
      ),
      height: Math.max(
        MIN_INSERTED_DISPLAY_EDGE,
        naturalHeight * scale
      )
    };
  }

  function insertedImageAnchor(sheet, options, imageInfo) {
    const row = Math.max(
      0,
      Math.floor(Number(options && options.row) || 0)
    );

    const col = Math.max(
      0,
      Math.floor(Number(options && options.col) || 0)
    );

    const defaultColWidth = Math.max(
      1,
      Number(options && options.defaultColWidth) || 80
    );

    const defaultRowHeight = Math.max(
      1,
      Number(options && options.defaultRowHeight) || 20
    );

    const display = insertedDisplaySize(
      imageInfo.width,
      imageInfo.height
    );

    const toCol = markerAfterPixels(
      sheet && sheet.colWidths,
      col,
      display.width,
      defaultColWidth,
      "col",
      "colOff"
    );

    const toRow = markerAfterPixels(
      sheet && sheet.rowHeights,
      row,
      display.height,
      defaultRowHeight,
      "row",
      "rowOff"
    );

    return {
      displayWidth: display.width,
      displayHeight: display.height,
      from: {
        col,
        colOff: 0,
        row,
        rowOff: 0
      },
      to: {
        col: toCol.col,
        colOff: toCol.colOff,
        row: toRow.row,
        rowOff: toRow.rowOff
      }
    };
  }

  function sheetHasImportedDrawing(sheet) {
    const images = Array.isArray(sheet && sheet.images)
      ? sheet.images
      : [];

    return images.some((image) => (
      image &&
      image.source !== "inserted" &&
      (
        image.drawingPath ||
        image.mediaPath
      )
    ));
  }

  async function createInsertedImageFromFile(
    file,
    sheet,
    options = {}
  ) {
    if (
      !file ||
      typeof file.arrayBuffer !== "function"
    ) {
      throw insertedImageError(
        "invalid_image_file",
        "No readable image file was selected."
      );
    }

    const declaredSize = Number(file.size);

    if (
      !Number.isFinite(declaredSize) ||
      declaredSize <= 0
    ) {
      throw insertedImageError(
        "empty_image_file",
        "The selected image file is empty."
      );
    }

    if (declaredSize > MAX_INSERTED_IMAGE_BYTES) {
      throw insertedImageError(
        "image_file_too_large",
        "The selected image file is too large."
      );
    }

    const existingImages = Array.isArray(sheet && sheet.images)
      ? sheet.images
      : [];

    if (existingImages.length >= MAX_OVERLAY_IMAGES) {
      throw insertedImageError(
        "image_count_limit",
        "The spreadsheet image limit has been reached."
      );
    }

    const arrayBuffer = await file.arrayBuffer();

    if (
      !(arrayBuffer instanceof ArrayBuffer) ||
      arrayBuffer.byteLength <= 0 ||
      arrayBuffer.byteLength > MAX_INSERTED_IMAGE_BYTES
    ) {
      throw insertedImageError(
        "image_file_too_large",
        "The selected image file is too large."
      );
    }

    const bytes = new Uint8Array(arrayBuffer);
    const imageInfo = validatedInsertedImageInfo(bytes);
    const anchor = insertedImageAnchor(
      sheet,
      options,
      imageInfo
    );

    const assetId = nextInsertedImageToken(
      "pqnas-image-asset"
    );

    const imageId = nextInsertedImageToken(
      "pqnas-image"
    );

    const blob = new Blob(
      [bytes],
      { type: imageInfo.mimeType }
    );

    const objectUrl = URL.createObjectURL(blob);

    insertedImageAssets.set(assetId, {
      assetId,
      bytes,
      objectUrl,
      mimeType: imageInfo.mimeType,
      extension: imageInfo.extension,
      width: imageInfo.width,
      height: imageInfo.height
    });

    return {
      id: imageId,
      source: "inserted",
      assetId,
      sheetIndex: Math.max(
        0,
        Math.floor(Number(options.sheetIndex) || 0)
      ),
      name: cleanInsertedImageName(file.name),
      descr: "",
      mimeType: imageInfo.mimeType,
      extension: imageInfo.extension,
      editAs: "oneCell",
      naturalWidth: imageInfo.width,
      naturalHeight: imageInfo.height,
      displayWidth: anchor.displayWidth,
      displayHeight: anchor.displayHeight,
      from: anchor.from,
      to: anchor.to,
      src: objectUrl
    };
  }

  function insertedAssetForImage(image) {
    const assetId = String(
      image && image.assetId || ""
    );

    return assetId
      ? insertedImageAssets.get(assetId) || null
      : null;
  }

  function releaseInsertedImageAssets() {
    for (const asset of insertedImageAssets.values()) {
      const objectUrl = String(
        asset && asset.objectUrl || ""
      );

      if (!objectUrl) continue;

      try {
        URL.revokeObjectURL(objectUrl);
      } catch (_) {}
    }

    insertedImageAssets.clear();
  }

  function insertedImageAssetCount() {
    return insertedImageAssets.size;
  }

  FM.spreadsheetXlsxImages = {
    inspectArrayBuffer,
    prepareExport,
    summaryText,
    contentTypeDefaultsXml,
    contentTypeOverridesXml,
    worksheetDrawingRelId,
    appendExportEntries,
    imagesFromWorkbookFiles,
    imageAnchorBounds,
    expandSheetForImages,
    sheetHasImportedDrawing,
    createInsertedImageFromFile,
    insertedAssetForImage,
    releaseInsertedImageAssets,
    insertedImageAssetCount
  };
})();
