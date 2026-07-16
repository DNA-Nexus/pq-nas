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


  function oneCellTransformSize(image) {
    if (
      String(image && image.editAs || "") !==
      "oneCell"
    ) {
      return null;
    }

    const transform =
      image && image.transform;

    const cx = Number(
      transform && transform.cx
    );

    const cy = Number(
      transform && transform.cy
    );

    if (
      !Number.isFinite(cx) ||
      !Number.isFinite(cy) ||
      cx <= 0 ||
      cy <= 0
    ) {
      return null;
    }

    /*
     * oneCell objects move with their anchor cell but retain their DrawingML
     * size. Using the rendered width of the from/to columns would make the
     * image change size between preview, editor, LibreOffice and Excel.
     */
    return {
      width: Math.max(
        8,
        Math.min(
          MAX_OVERLAY_EDGE,
          cx / EMU_PER_PIXEL
        )
      ),
      height: Math.max(
        8,
        Math.min(
          MAX_OVERLAY_EDGE,
          cy / EMU_PER_PIXEL
        )
      )
    };
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


  function setImageRect(img, frame, rect) {
    if (!img || !rect) return;

    const left = clampPixel(rect.left);
    const top = clampPixel(rect.top);
    const width = clampPixel(
      Math.max(8, Number(rect.width) || 8)
    );
    const height = clampPixel(
      Math.max(8, Number(rect.height) || 8)
    );

    img.style.left = `${left}px`;
    img.style.top = `${top}px`;
    img.style.width = `${width}px`;
    img.style.height = `${height}px`;

    img.dataset.spreadsheetAnchorLeft =
      String(left);

    img.dataset.spreadsheetAnchorTop =
      String(top);

    img.dataset.spreadsheetAnchorWidth =
      String(width);

    img.dataset.spreadsheetAnchorHeight =
      String(height);

    if (frame) {
      frame.style.left = `${left}px`;
      frame.style.top = `${top}px`;
      frame.style.width = `${width}px`;
      frame.style.height = `${height}px`;
    }
  }

  function beginImageMove(
    ev,
    surface,
    layer,
    img,
    imageId,
    image,
    geometry,
    callbacks
  ) {
    if (
      !ev ||
      ev.button !== 0 ||
      !surface ||
      !layer ||
      !img
    ) {
      return;
    }

    ev.preventDefault();
    ev.stopPropagation();

    const startRect = visibleImageRect(img);

    if (
      startRect.width <= 0 ||
      startRect.height <= 0
    ) {
      return;
    }

    if (callbacks.onSelect) {
      callbacks.onSelect(
        imageId,
        image,
        ev
      );
    }

    const frame = layer.querySelector(
      ".spreadsheetImageSelectionFrame"
    );

    const startX = Number(ev.clientX);
    const startY = Number(ev.clientY);

    let changed = false;
    let currentRect = {
      ...startRect
    };

    if (callbacks.onTransformStart) {
      callbacks.onTransformStart(
        imageId,
        image,
        startRect,
        geometry
      );
    }

    const cleanup = () => {
      document.removeEventListener(
        "pointermove",
        onMove
      );

      document.removeEventListener(
        "pointerup",
        onUp
      );

      document.removeEventListener(
        "pointercancel",
        onCancel
      );

      document.body.classList.remove(
        "spreadsheetImageMoving"
      );
    };

    const onMove = (moveEv) => {
      const dx =
        Number(moveEv.clientX) - startX;

      const dy =
        Number(moveEv.clientY) - startY;

      if (
        !Number.isFinite(dx) ||
        !Number.isFinite(dy)
      ) {
        return;
      }

      if (
        !changed &&
        Math.abs(dx) < 1 &&
        Math.abs(dy) < 1
      ) {
        return;
      }

      changed = true;

      currentRect = {
        left: Math.max(
          geometry.gridLeft,
          startRect.left + dx
        ),
        top: Math.max(
          geometry.gridTop,
          startRect.top + dy
        ),
        width: startRect.width,
        height: startRect.height
      };

      setImageRect(
        img,
        frame,
        currentRect
      );

      if (callbacks.onTransformPreview) {
        callbacks.onTransformPreview(
          imageId,
          image,
          currentRect,
          geometry
        );
      }
    };

    const onUp = () => {
      cleanup();

      if (!changed) {
        if (callbacks.onTransformCancel) {
          callbacks.onTransformCancel(
            imageId,
            image,
            startRect,
            geometry
          );
        }

        return;
      }

      if (callbacks.onTransformCommit) {
        callbacks.onTransformCommit(
          imageId,
          image,
          currentRect,
          geometry
        );
      }
    };

    const onCancel = () => {
      cleanup();

      setImageRect(
        img,
        frame,
        startRect
      );

      if (callbacks.onTransformCancel) {
        callbacks.onTransformCancel(
          imageId,
          image,
          startRect,
          geometry
        );
      }
    };

    document.body.classList.add(
      "spreadsheetImageMoving"
    );

    document.addEventListener(
      "pointermove",
      onMove
    );

    document.addEventListener(
      "pointerup",
      onUp,
      { once: true }
    );

    document.addEventListener(
      "pointercancel",
      onCancel,
      { once: true }
    );
  }


  const MIN_IMAGE_RESIZE_EDGE = 8;

  function clampResizeValue(
    value,
    minimum,
    maximum = Number.POSITIVE_INFINITY
  ) {
    const numeric = Number(value);

    if (!Number.isFinite(numeric)) {
      return minimum;
    }

    return Math.max(
      minimum,
      Math.min(maximum, numeric)
    );
  }

  function resizedImageRect(
    startRect,
    handle,
    dx,
    dy,
    geometry,
    preserveAspect
  ) {
    const west =
      handle === "nw" ||
      handle === "sw";

    const north =
      handle === "nw" ||
      handle === "ne";

    const fixedX = west
      ? startRect.left + startRect.width
      : startRect.left;

    const fixedY = north
      ? startRect.top + startRect.height
      : startRect.top;

    const gridLeft = Number.isFinite(
      Number(geometry && geometry.gridLeft)
    )
      ? Number(geometry.gridLeft)
      : 0;

    const gridTop = Number.isFinite(
      Number(geometry && geometry.gridTop)
    )
      ? Number(geometry.gridTop)
      : 0;

    const rawWidth = Math.max(
      MIN_IMAGE_RESIZE_EDGE,
      startRect.width +
        (west ? -dx : dx)
    );

    const rawHeight = Math.max(
      MIN_IMAGE_RESIZE_EDGE,
      startRect.height +
        (north ? -dy : dy)
    );

    const maximumWidth = west
      ? Math.max(
          MIN_IMAGE_RESIZE_EDGE,
          fixedX - gridLeft
        )
      : Number.POSITIVE_INFINITY;

    const maximumHeight = north
      ? Math.max(
          MIN_IMAGE_RESIZE_EDGE,
          fixedY - gridTop
        )
      : Number.POSITIVE_INFINITY;

    let width;
    let height;

    if (preserveAspect) {
      const horizontalScale =
        rawWidth / startRect.width;

      const verticalScale =
        rawHeight / startRect.height;

      /*
       * Follow the pointer axis with the larger relative change. This avoids
       * the image jumping when a user starts a mostly horizontal or vertical
       * corner drag.
       */
      let scale =
        Math.abs(horizontalScale - 1) >=
        Math.abs(verticalScale - 1)
          ? horizontalScale
          : verticalScale;

      const minimumScale = Math.max(
        MIN_IMAGE_RESIZE_EDGE /
          startRect.width,

        MIN_IMAGE_RESIZE_EDGE /
          startRect.height
      );

      let maximumScale =
        Number.POSITIVE_INFINITY;

      if (west) {
        maximumScale = Math.min(
          maximumScale,
          maximumWidth /
            startRect.width
        );
      }

      if (north) {
        maximumScale = Math.min(
          maximumScale,
          maximumHeight /
            startRect.height
        );
      }

      scale = clampResizeValue(
        scale,
        minimumScale,
        maximumScale
      );

      width =
        startRect.width * scale;

      height =
        startRect.height * scale;
    } else {
      width = clampResizeValue(
        rawWidth,
        MIN_IMAGE_RESIZE_EDGE,
        maximumWidth
      );

      height = clampResizeValue(
        rawHeight,
        MIN_IMAGE_RESIZE_EDGE,
        maximumHeight
      );
    }

    return {
      left: west
        ? fixedX - width
        : fixedX,

      top: north
        ? fixedY - height
        : fixedY,

      width,
      height
    };
  }

  function imageRectsDiffer(a, b) {
    if (!a || !b) return false;

    return (
      Math.abs(a.left - b.left) >= 0.1 ||
      Math.abs(a.top - b.top) >= 0.1 ||
      Math.abs(a.width - b.width) >= 0.1 ||
      Math.abs(a.height - b.height) >= 0.1
    );
  }

  function beginImageResize(
    ev,
    surface,
    layer,
    img,
    frame,
    handle,
    imageId,
    image,
    geometry,
    callbacks
  ) {
    if (
      !ev ||
      ev.button !== 0 ||
      !surface ||
      !layer ||
      !img ||
      !frame ||
      !image ||
      !["nw", "ne", "se", "sw"].includes(handle)
    ) {
      return;
    }

    ev.preventDefault();
    ev.stopPropagation();

    if (
      typeof ev.stopImmediatePropagation ===
      "function"
    ) {
      ev.stopImmediatePropagation();
    }

    const startRect =
      visibleImageRect(img);

    if (
      startRect.width <
        MIN_IMAGE_RESIZE_EDGE ||
      startRect.height <
        MIN_IMAGE_RESIZE_EDGE
    ) {
      return;
    }

    const startX = Number(ev.clientX);
    const startY = Number(ev.clientY);

    if (
      !Number.isFinite(startX) ||
      !Number.isFinite(startY)
    ) {
      return;
    }

    let changed = false;

    let currentRect = {
      ...startRect
    };

    if (callbacks.onTransformStart) {
      callbacks.onTransformStart(
        imageId,
        image,
        startRect,
        geometry
      );
    }

    const resizeClass =
      `spreadsheetImageResizing-${handle}`;

    const cleanup = () => {
      document.removeEventListener(
        "pointermove",
        onMove
      );

      document.removeEventListener(
        "pointerup",
        onUp
      );

      document.removeEventListener(
        "pointercancel",
        onCancel
      );

      document.body.classList.remove(
        "spreadsheetImageResizing"
      );

      document.body.classList.remove(
        resizeClass
      );
    };

    const onMove = (moveEv) => {
      const dx =
        Number(moveEv.clientX) -
        startX;

      const dy =
        Number(moveEv.clientY) -
        startY;

      if (
        !Number.isFinite(dx) ||
        !Number.isFinite(dy)
      ) {
        return;
      }

      const nextRect =
        resizedImageRect(
          startRect,
          handle,
          dx,
          dy,
          geometry,

          /*
           * Preserve the current displayed aspect ratio by default. Holding
           * Shift explicitly enables free horizontal/vertical stretching.
           */
          !moveEv.shiftKey
        );

      if (
        !imageRectsDiffer(
          startRect,
          nextRect
        )
      ) {
        return;
      }

      changed = true;
      currentRect = nextRect;

      setImageRect(
        img,
        frame,
        currentRect
      );

      if (callbacks.onTransformPreview) {
        callbacks.onTransformPreview(
          imageId,
          image,
          currentRect,
          geometry
        );
      }
    };

    const onUp = () => {
      cleanup();

      if (!changed) {
        setImageRect(
          img,
          frame,
          startRect
        );

        if (callbacks.onTransformCancel) {
          callbacks.onTransformCancel(
            imageId,
            image,
            startRect,
            geometry
          );
        }

        return;
      }

      if (callbacks.onTransformCommit) {
        callbacks.onTransformCommit(
          imageId,
          image,
          currentRect,
          geometry
        );
      }
    };

    const onCancel = () => {
      cleanup();

      setImageRect(
        img,
        frame,
        startRect
      );

      if (callbacks.onTransformCancel) {
        callbacks.onTransformCancel(
          imageId,
          image,
          startRect,
          geometry
        );
      }
    };

    document.body.classList.add(
      "spreadsheetImageResizing"
    );

    document.body.classList.add(
      resizeClass
    );

    document.addEventListener(
      "pointermove",
      onMove
    );

    document.addEventListener(
      "pointerup",
      onUp,
      { once: true }
    );

    document.addEventListener(
      "pointercancel",
      onCancel,
      { once: true }
    );
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

    const interaction =
      layer._spreadsheetImageInteraction;

    const image =
      img._spreadsheetImageValue;

    const imageId = String(
      img.dataset &&
      img.dataset.spreadsheetImageId ||
      ""
    );

    for (const pos of ["nw", "ne", "se", "sw"]) {
      const handle = document.createElement("span");

      handle.className =
        `spreadsheetImageSelectionHandle ` +
        `spreadsheetImageSelectionHandle-${pos}`;

      handle.dataset.spreadsheetImageResizeHandle =
        pos;

      handle.addEventListener(
        "pointerdown",
        (ev) => {
          if (
            !interaction ||
            !image
          ) {
            return;
          }

          beginImageResize(
            ev,
            interaction.surface,
            layer,
            img,
            frame,
            pos,
            imageId,
            image,
            interaction.geometry,
            interaction.callbacks
          );
        }
      );

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
    const onSelect =
      typeof options.onSelect === "function"
        ? options.onSelect
        : null;

    const onTransformStart =
      typeof options.onTransformStart === "function"
        ? options.onTransformStart
        : null;

    const onTransformPreview =
      typeof options.onTransformPreview === "function"
        ? options.onTransformPreview
        : null;

    const onTransformCommit =
      typeof options.onTransformCommit === "function"
        ? options.onTransformCommit
        : null;

    const onTransformCancel =
      typeof options.onTransformCancel === "function"
        ? options.onTransformCancel
        : null;

    const defaults = {
      colWidth: Number(options.defaultColWidth) || 120,
      rowHeight: Number(options.defaultRowHeight) || 28
    };

    const metrics = tableHeaderMetrics(table);

    const gridOrigin = pointForMarker(
      surface,
      table,
      sheet,
      {
        col: 0,
        colOff: 0,
        row: 0,
        rowOff: 0
      },
      defaults,
      metrics
    );

    const transformGeometry = {
      gridLeft: clampPixel(gridOrigin.x),
      gridTop: clampPixel(gridOrigin.y),
      defaultColWidth: defaults.colWidth,
      defaultRowHeight: defaults.rowHeight
    };

    const layer = document.createElement("div");
    layer.className = "spreadsheetImageOverlayLayer";

    /*
     * Interaction callbacks and workbook image references stay on the
     * short-lived DOM overlay. They are never serialized into workbook state.
     */
    layer._spreadsheetImageInteraction = {
      surface,
      geometry: transformGeometry,
      callbacks: {
        onSelect,
        onTransformStart,
        onTransformPreview,
        onTransformCommit,
        onTransformCancel
      }
    };

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
      const fixedSize = oneCellTransformSize(image);

      const width = fixedSize
        ? clampPixel(fixedSize.width)
        : clampPixel(Math.max(8, p2.x - p1.x));

      const height = fixedSize
        ? clampPixel(fixedSize.height)
        : clampPixel(Math.max(8, p2.y - p1.y));

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
      img.dataset.spreadsheetImageEditAs = String(
        image && image.editAs || ""
      );

      img._spreadsheetImageValue =
        image;

      if (fixedSize) {
        img.dataset.spreadsheetTransformWidth =
          String(fixedSize.width);

        img.dataset.spreadsheetTransformHeight =
          String(fixedSize.height);
      }

      if (selectable) {
        img.classList.add("spreadsheetImageOverlayImageSelectable");
        img.tabIndex = 0;
        img.setAttribute("role", "button");
        img.setAttribute("aria-selected", imageId === selectedImageId ? "true" : "false");

        img.addEventListener(
          "pointerdown",
          (ev) => {
            beginImageMove(
              ev,
              surface,
              layer,
              img,
              imageId,
              image,
              transformGeometry,
              {
                onSelect,
                onTransformStart,
                onTransformPreview,
                onTransformCommit,
                onTransformCancel
              }
            );
          }
        );

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
