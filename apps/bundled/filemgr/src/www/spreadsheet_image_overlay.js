window.PQNAS_FILEMGR = window.PQNAS_FILEMGR || {};

(() => {
  "use strict";

  const FM = window.PQNAS_FILEMGR;
  const EMU_PER_PIXEL = 9525;
  const MAX_OVERLAY_EDGE = 200000;

  function clampPixel(value) {
    const n = Number(value);
    if (!Number.isFinite(n)) return 0;
    return Math.max(0, Math.min(MAX_OVERLAY_EDGE, n));
  }

  function markerNumber(marker, key) {
    const n = Number(marker && marker[key]);
    return Number.isFinite(n) ? n : 0;
  }

  function sumDimension(values, count, fallback) {
    const n = Math.max(0, Math.min(MAX_OVERLAY_EDGE, Number(count) || 0));
    let total = 0;

    for (let i = 0; i < n; i++) {
      const value = Number(Array.isArray(values) ? values[i] : null);
      total += Number.isFinite(value) && value > 0 ? value : fallback;
    }

    return total;
  }

  function pointForMarker(sheet, marker, defaults) {
    const col = Math.max(0, Math.floor(markerNumber(marker, "col")));
    const row = Math.max(0, Math.floor(markerNumber(marker, "row")));
    const colOff = markerNumber(marker, "colOff") / EMU_PER_PIXEL;
    const rowOff = markerNumber(marker, "rowOff") / EMU_PER_PIXEL;

    return {
      x: sumDimension(sheet && sheet.colWidths, col, defaults.colWidth) + colOff,
      y: sumDimension(sheet && sheet.rowHeights, row, defaults.rowHeight) + rowOff
    };
  }

  function tableHeaderMetrics(table) {
    if (!table) return { rowHeaderWidth: 44, colHeaderHeight: 28 };

    const tableRect = table.getBoundingClientRect();
    const corner = table.querySelector("thead th.rowHead, thead th.corner");
    const firstHeader = table.querySelector("thead th:not(.rowHead)");
    const firstRowHead = table.querySelector("tbody th.rowHead");

    const cornerRect = corner ? corner.getBoundingClientRect() : null;
    const headerRect = firstHeader ? firstHeader.getBoundingClientRect() : null;
    const rowHeadRect = firstRowHead ? firstRowHead.getBoundingClientRect() : null;

    const rowHeaderWidth = cornerRect && cornerRect.width
      ? cornerRect.width
      : (rowHeadRect && rowHeadRect.width ? rowHeadRect.width : 44);

    const colHeaderHeight = cornerRect && cornerRect.height
      ? cornerRect.height
      : (headerRect && headerRect.height ? headerRect.height : 28);

    return {
      rowHeaderWidth: clampPixel(rowHeaderWidth || tableRect.left),
      colHeaderHeight: clampPixel(colHeaderHeight || 28)
    };
  }

  function clear(surface) {
    if (!surface) return;

    for (const node of surface.querySelectorAll(":scope > .spreadsheetImageOverlayLayer")) {
      node.remove();
    }

    surface.style.removeProperty("--spreadsheet-image-overlay-width");
    surface.style.removeProperty("--spreadsheet-image-overlay-height");
  }

  function render(surface, table, sheet, options = {}) {
    if (!surface || !table) return;

    clear(surface);

    const images = Array.isArray(sheet && sheet.images) ? sheet.images : [];
    if (!images.length) return;

    const defaults = {
      colWidth: Number(options.defaultColWidth) || 120,
      rowHeight: Number(options.defaultRowHeight) || 28
    };

    const metrics = tableHeaderMetrics(table);
    const layer = document.createElement("div");
    layer.className = "spreadsheetImageOverlayLayer";
    layer.setAttribute("aria-hidden", "true");

    let maxRight = table.scrollWidth || 0;
    let maxBottom = table.scrollHeight || 0;

    for (const image of images) {
      const from = image && image.from;
      const to = image && image.to;
      const src = image && image.src;

      if (!from || !to || !src) continue;

      const p1 = pointForMarker(sheet, from, defaults);
      const p2 = pointForMarker(sheet, to, defaults);

      const left = clampPixel(metrics.rowHeaderWidth + p1.x);
      const top = clampPixel(metrics.colHeaderHeight + p1.y);
      const width = clampPixel(Math.max(8, p2.x - p1.x));
      const height = clampPixel(Math.max(8, p2.y - p1.y));

      const img = document.createElement("img");
      img.className = "spreadsheetImageOverlayImage";
      img.alt = image.descr || image.name || "";
      img.draggable = false;
      img.loading = "lazy";
      img.decoding = "async";
      img.src = src;
      img.style.left = `${left}px`;
      img.style.top = `${top}px`;
      img.style.width = `${width}px`;
      img.style.height = `${height}px`;

      layer.appendChild(img);
      maxRight = Math.max(maxRight, left + width);
      maxBottom = Math.max(maxBottom, top + height);
    }

    if (!layer.childElementCount) return;

    surface.classList.add("spreadsheetImageSurface");
    surface.style.setProperty("--spreadsheet-image-overlay-width", `${Math.ceil(maxRight)}px`);
    surface.style.setProperty("--spreadsheet-image-overlay-height", `${Math.ceil(maxBottom)}px`);
    surface.appendChild(layer);
  }

  FM.spreadsheetImageOverlay = {
    clear,
    render
  };
})();
