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

  function modelPointForMarker(sheet, marker, defaults) {
    const col = Math.max(0, Math.floor(markerNumber(marker, "col")));
    const row = Math.max(0, Math.floor(markerNumber(marker, "row")));
    const colOff = markerNumber(marker, "colOff") / EMU_PER_PIXEL;
    const rowOff = markerNumber(marker, "rowOff") / EMU_PER_PIXEL;

    return {
      x: sumDimension(sheet && sheet.colWidths, col, defaults.colWidth) + colOff,
      y: sumDimension(sheet && sheet.rowHeights, row, defaults.rowHeight) + rowOff
    };
  }

  function domPointForMarker(surface, table, marker) {
    if (!surface || !table || !marker) return null;

    const col = Math.max(0, Math.floor(markerNumber(marker, "col")));
    const row = Math.max(0, Math.floor(markerNumber(marker, "row")));
    const colOff = markerNumber(marker, "colOff") / EMU_PER_PIXEL;
    const rowOff = markerNumber(marker, "rowOff") / EMU_PER_PIXEL;

    if (typeof surface.getBoundingClientRect !== "function") {
      return null;
    }

    const surfaceRect = surface.getBoundingClientRect();

    /*
     * Prefer the actual worksheet cell boundary. This includes browser table
     * borders and subpixel layout in the same geometry as the visible grid.
     * Row/column indexes are bounded integers parsed from OOXML, not raw text.
     */
    const cell = table.querySelector(
      `td[data-row="${row}"][data-col="${col}"]`
    );

    if (cell && typeof cell.getBoundingClientRect === "function") {
      const cellRect = cell.getBoundingClientRect();
      const x = cellRect.left - surfaceRect.left + colOff;
      const y = cellRect.top - surfaceRect.top + rowOff;

      if (Number.isFinite(x) && Number.isFinite(y)) {
        return {
          x: clampPixel(x),
          y: clampPixel(y)
        };
      }
    }

    /*
     * Fallback supports merged or partially rendered worksheets where the
     * exact target cell does not exist in the DOM.
     */
    const columnHeader = table.querySelector(
      `thead th[data-col="${col}"]`
    );

    const tbody = table.tBodies && table.tBodies[0];
    const rowElement = tbody && tbody.rows
      ? tbody.rows[row]
      : null;

    if (
      !columnHeader ||
      !rowElement ||
      typeof columnHeader.getBoundingClientRect !== "function" ||
      typeof rowElement.getBoundingClientRect !== "function"
    ) {
      return null;
    }

    const columnRect = columnHeader.getBoundingClientRect();
    const rowRect = rowElement.getBoundingClientRect();

    const x = columnRect.left - surfaceRect.left + colOff;
    const y = rowRect.top - surfaceRect.top + rowOff;

    if (!Number.isFinite(x) || !Number.isFinite(y)) return null;

    return {
      x: clampPixel(x),
      y: clampPixel(y)
    };
  }

  function pointForMarker(surface, table, sheet, marker, defaults, metrics) {
    const domPoint = domPointForMarker(surface, table, marker);
    if (domPoint) return domPoint;

    /*
     * Fallback keeps malformed or partially rendered worksheets usable.
     * It uses only bounded numeric workbook dimensions.
     */
    const modelPoint = modelPointForMarker(sheet, marker, defaults);

    return {
      x: clampPixel(metrics.rowHeaderWidth + modelPoint.x),
      y: clampPixel(metrics.colHeaderHeight + modelPoint.y)
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

  function imageIdFor(image, index) {
    const explicit = String(image && image.id ? image.id : "").trim();
    if (explicit) return explicit;

    const parts = [
      image && image.sheetIndex,
      image && image.drawingPath,
      image && image.mediaPath,
      image && image.from && image.from.row,
      image && image.from && image.from.col,
      index
    ];

    return parts.map((part) => String(part == null ? "" : part)).join(":");
  }

  function cssPx(value) {
    const n = Number.parseFloat(String(value || ""));
    return Number.isFinite(n) ? n : 0;
  }

  function imageAnchorRect(img) {
    return {
      left: cssPx((img && img.dataset && img.dataset.spreadsheetAnchorLeft) || (img && img.style && img.style.left)),
      top: cssPx((img && img.dataset && img.dataset.spreadsheetAnchorTop) || (img && img.style && img.style.top)),
      width: cssPx((img && img.dataset && img.dataset.spreadsheetAnchorWidth) || (img && img.style && img.style.width)),
      height: cssPx((img && img.dataset && img.dataset.spreadsheetAnchorHeight) || (img && img.style && img.style.height))
    };
  }

  function visibleImageRect(img) {
    const anchor = imageAnchorRect(img);

    /*
     * Existing XLSX images use the complete drawing anchor as their visible
     * rectangle. Do not silently restore the media file's natural aspect ratio,
     * because Excel may intentionally stretch the image inside its anchor.
     */
    return {
      left: clampPixel(anchor.left),
      top: clampPixel(anchor.top),
      width: clampPixel(anchor.width),
      height: clampPixel(anchor.height)
    };
  }

  function fitSelectableImageHitbox(img) {
    if (!img || !img.classList || !img.classList.contains("spreadsheetImageOverlayImageSelectable")) {
      return;
    }

    const rect = visibleImageRect(img);
    if (rect.width <= 0 || rect.height <= 0) return;

    // Keep pointer hit testing on the visible image only. The larger XLSX anchor
    // rectangle may include blank space and must not block spreadsheet cells.
    img.style.left = `${rect.left}px`;
    img.style.top = `${rect.top}px`;
    img.style.width = `${rect.width}px`;
    img.style.height = `${rect.height}px`;
  }

  function appendSelectionFrame(layer, img) {
    if (!layer || !img) return;

    const rect = visibleImageRect(img);
    if (rect.width <= 0 || rect.height <= 0) return;

    const frame = document.createElement("div");
    frame.className = "spreadsheetImageSelectionFrame";
    frame.style.left = `${rect.left}px`;
    frame.style.top = `${rect.top}px`;
    frame.style.width = `${rect.width}px`;
    frame.style.height = `${rect.height}px`;

    for (const pos of ["nw", "ne", "se", "sw"]) {
      const handle = document.createElement("span");
      handle.className = `spreadsheetImageSelectionHandle spreadsheetImageSelectionHandle-${pos}`;
      frame.appendChild(handle);
    }

    layer.appendChild(frame);
  }

  function clear(surface) {
    if (!surface) return;

    for (const node of surface.querySelectorAll(":scope > .spreadsheetImageOverlayLayer")) {
      node.remove();
    }

    surface.classList.remove("spreadsheetImageSurface");
    surface.style.removeProperty("--spreadsheet-image-overlay-width");
    surface.style.removeProperty("--spreadsheet-image-overlay-height");
  }

  function select(surface, selectedImageId = "") {
    if (!surface) return;

    const layer = surface.querySelector(":scope > .spreadsheetImageOverlayLayer");
    if (!layer) return;

    for (const frame of layer.querySelectorAll(".spreadsheetImageSelectionFrame")) {
      frame.remove();
    }

    const selected = String(selectedImageId || "");

    for (const img of layer.querySelectorAll(".spreadsheetImageOverlayImage")) {
      const isSelected = !!selected && img.dataset.spreadsheetImageId === selected;
      img.classList.toggle("selected", isSelected);
      img.setAttribute("aria-selected", isSelected ? "true" : "false");

      if (isSelected) {
        appendSelectionFrame(layer, img);
      }
    }
  }

  function render(surface, table, sheet, options = {}) {
    if (!surface || !table) return;

    clear(surface);

    const images = Array.isArray(sheet && sheet.images) ? sheet.images : [];
    if (!images.length) return;

    const selectable = options.selectable === true;
    const selectedImageId = String(options.selectedImageId || "");
    const onSelect = typeof options.onSelect === "function" ? options.onSelect : null;

    const defaults = {
      colWidth: Number(options.defaultColWidth) || 120,
      rowHeight: Number(options.defaultRowHeight) || 28
    };

    const metrics = tableHeaderMetrics(table);
    const layer = document.createElement("div");
    layer.className = "spreadsheetImageOverlayLayer";
    if (selectable) {
      layer.classList.add("spreadsheetImageOverlayLayerSelectable");
      layer.removeAttribute("aria-hidden");
    } else {
      layer.setAttribute("aria-hidden", "true");
    }

    let maxRight = table.scrollWidth || 0;
    let maxBottom = table.scrollHeight || 0;

    images.forEach((image, imageIndex) => {
      const from = image && image.from;
      const to = image && image.to;
      const src = image && image.src;

      if (!from || !to || !src) return;

      const p1 = pointForMarker(
        surface,
        table,
        sheet,
        from,
        defaults,
        metrics
      );

      const p2 = pointForMarker(
        surface,
        table,
        sheet,
        to,
        defaults,
        metrics
      );

      const left = clampPixel(p1.x);
      const top = clampPixel(p1.y);
      const width = clampPixel(Math.max(8, p2.x - p1.x));
      const height = clampPixel(Math.max(8, p2.y - p1.y));
      const imageId = imageIdFor(image, imageIndex);

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
      img.dataset.spreadsheetImageId = imageId;
      img.dataset.spreadsheetAnchorLeft = String(left);
      img.dataset.spreadsheetAnchorTop = String(top);
      img.dataset.spreadsheetAnchorWidth = String(width);
      img.dataset.spreadsheetAnchorHeight = String(height);

      if (selectable) {
        img.classList.add("spreadsheetImageOverlayImageSelectable");
        img.tabIndex = 0;
        img.setAttribute("role", "button");
        img.setAttribute("aria-selected", imageId === selectedImageId ? "true" : "false");

        img.addEventListener("pointerdown", (ev) => {
          ev.preventDefault();
          ev.stopPropagation();
          if (onSelect) onSelect(imageId, image, ev);
        });

        img.addEventListener("keydown", (ev) => {
          if (ev.key !== "Enter" && ev.key !== " ") return;
          ev.preventDefault();
          ev.stopPropagation();
          if (onSelect) onSelect(imageId, image, ev);
        });

        img.addEventListener("load", () => {
          fitSelectableImageHitbox(img);

          if (img.classList.contains("selected")) {
            select(surface, selectedImageId);
          }
        }, { once: true });
      }

      layer.appendChild(img);

      if (img.complete) {
        fitSelectableImageHitbox(img);
      }

      maxRight = Math.max(maxRight, left + width);
      maxBottom = Math.max(maxBottom, top + height);
    });

    if (!layer.childElementCount) return;

    surface.classList.add("spreadsheetImageSurface");
    surface.style.setProperty("--spreadsheet-image-overlay-width", `${Math.ceil(maxRight)}px`);
    surface.style.setProperty("--spreadsheet-image-overlay-height", `${Math.ceil(maxBottom)}px`);
    surface.appendChild(layer);

    if (selectedImageId) {
      select(surface, selectedImageId);
    }
  }

  FM.spreadsheetImageOverlay = {
    clear,
    select,
    render
  };
})();
