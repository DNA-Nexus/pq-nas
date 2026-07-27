(() => {
  "use strict";

  const surface = document.querySelector(".rsApp");
  const grid = document.getElementById("grid");

  if (!surface || !grid) {
    return;
  }

  const selectedPaths = new Set();

  let dragState = null;
  let marqueeBox = null;
  let suppressNextClick = false;
  let refreshQueued = false;

  function cardNodes() {
    return Array.from(
      grid.querySelectorAll(".rsCard[data-rs-path]")
    );
  }

  function cardPath(card) {
    return String(card && card.dataset.rsPath || "");
  }

  function elementTarget(target) {
    return target instanceof Element ? target : null;
  }

  function isInteractiveTarget(target) {
    const node = elementTarget(target);

    if (!node) {
      return false;
    }

    return !!node.closest(
      "button, a, input, select, textarea, label, " +
      "[contenteditable='true'], [role='button']"
    );
  }

  function isExcludedArea(target) {
    const node = elementTarget(target);

    if (!node) {
      return true;
    }

    return !!node.closest(".rsTop, .rsStatus, .rsEmpty");
  }

  function notifyFooter() {
    const footer = window.PQNAS_REELSTACK_FOOTER;

    if (footer && typeof footer.setSelection === "function") {
      footer.setSelection(Array.from(selectedPaths));
    }
  }

  function applySelectionStyles() {
    for (const card of cardNodes()) {
      const selected = selectedPaths.has(cardPath(card));

      card.classList.toggle("rsMultiSelected", selected);
      card.setAttribute("aria-selected", selected ? "true" : "false");
    }
  }

  function publishSelection() {
    applySelectionStyles();
    notifyFooter();
  }

  function replaceSelection(paths) {
    selectedPaths.clear();

    for (const path of paths || []) {
      const normalized = String(path || "");

      if (normalized) {
        selectedPaths.add(normalized);
      }
    }

    publishSelection();
  }

  function toggleSelection(path) {
    path = String(path || "");

    if (!path) {
      return;
    }

    if (selectedPaths.has(path)) {
      selectedPaths.delete(path);
    } else {
      selectedPaths.add(path);
    }

    publishSelection();
  }

  function refresh() {
    refreshQueued = false;

    const availablePaths = new Set(
      cardNodes()
        .map(cardPath)
        .filter(Boolean)
    );

    for (const path of Array.from(selectedPaths)) {
      if (!availablePaths.has(path)) {
        selectedPaths.delete(path);
      }
    }

    publishSelection();
  }

  function scheduleRefresh() {
    if (refreshQueued) {
      return;
    }

    refreshQueued = true;

    window.requestAnimationFrame(refresh);
  }

  function selectionRect(x1, y1, x2, y2) {
    const left = Math.min(x1, x2);
    const top = Math.min(y1, y2);
    const right = Math.max(x1, x2);
    const bottom = Math.max(y1, y2);

    return {
      left,
      top,
      right,
      bottom,
      width: right - left,
      height: bottom - top
    };
  }

  function rectsIntersect(a, b) {
    return (
      a.left <= b.right &&
      a.right >= b.left &&
      a.top <= b.bottom &&
      a.bottom >= b.top
    );
  }

  function ensureMarqueeBox() {
    if (marqueeBox) {
      return marqueeBox;
    }

    marqueeBox = document.createElement("div");
    marqueeBox.className = "rsMarqueeBox";
    marqueeBox.setAttribute("aria-hidden", "true");
    document.body.appendChild(marqueeBox);

    return marqueeBox;
  }

  function renderMarquee(rect) {
    const box = ensureMarqueeBox();

    box.style.left = `${rect.left}px`;
    box.style.top = `${rect.top}px`;
    box.style.width = `${rect.width}px`;
    box.style.height = `${rect.height}px`;
  }

  function pathsInsideRect(rect) {
    const paths = [];

    for (const card of cardNodes()) {
      const cardRect = card.getBoundingClientRect();

      if (rectsIntersect(rect, cardRect)) {
        const path = cardPath(card);

        if (path) {
          paths.push(path);
        }
      }
    }

    return paths;
  }

  function updateDragSelection(rect) {
    const hitPaths = pathsInsideRect(rect);
    const nextPaths = new Set(
      dragState && dragState.additive
        ? dragState.basePaths
        : []
    );

    for (const path of hitPaths) {
      nextPaths.add(path);
    }

    selectedPaths.clear();

    for (const path of nextPaths) {
      selectedPaths.add(path);
    }

    publishSelection();
  }

  function cleanupDrag() {
    document.body.classList.remove("rsMarqueeActive");

    if (marqueeBox) {
      marqueeBox.remove();
      marqueeBox = null;
    }
  }

  function onPointerDown(event) {
    if (event.button !== 0) {
      return;
    }

    if (event.pointerType && event.pointerType !== "mouse") {
      return;
    }

    if (
      isInteractiveTarget(event.target) ||
      isExcludedArea(event.target)
    ) {
      return;
    }

    dragState = {
      pointerId: event.pointerId,
      startX: event.clientX,
      startY: event.clientY,
      basePaths: new Set(selectedPaths),
      additive: !!(
        event.ctrlKey ||
        event.metaKey ||
        event.shiftKey
      ),
      moved: false
    };

    try {
      surface.setPointerCapture(event.pointerId);
    } catch (_) {}
  }

  function onPointerMove(event) {
    if (
      !dragState ||
      event.pointerId !== dragState.pointerId
    ) {
      return;
    }

    const deltaX = event.clientX - dragState.startX;
    const deltaY = event.clientY - dragState.startY;
    const distance = Math.hypot(deltaX, deltaY);

    if (!dragState.moved && distance < 5) {
      return;
    }

    if (!dragState.moved) {
      dragState.moved = true;
      document.body.classList.add("rsMarqueeActive");
    }

    event.preventDefault();

    const rect = selectionRect(
      dragState.startX,
      dragState.startY,
      event.clientX,
      event.clientY
    );

    renderMarquee(rect);
    updateDragSelection(rect);
  }

  function finishPointer(event, cancelled) {
    if (
      !dragState ||
      event.pointerId !== dragState.pointerId
    ) {
      return;
    }

    const wasMoved = dragState.moved;

    try {
      surface.releasePointerCapture(event.pointerId);
    } catch (_) {}

    dragState = null;
    cleanupDrag();

    if (wasMoved && !cancelled) {
      event.preventDefault();
      suppressNextClick = true;

      window.setTimeout(() => {
        suppressNextClick = false;
      }, 0);
    }
  }

  function onPointerUp(event) {
    finishPointer(event, false);
  }

  function onPointerCancel(event) {
    finishPointer(event, true);
  }

  function onClick(event) {
    if (suppressNextClick) {
      event.preventDefault();
      event.stopPropagation();
      event.stopImmediatePropagation();
      suppressNextClick = false;
      return;
    }

    if (isInteractiveTarget(event.target)) {
      return;
    }

    const node = elementTarget(event.target);
    const card = node && node.closest(".rsCard[data-rs-path]");

    if (card && grid.contains(card)) {
      const path = cardPath(card);
      const additive = !!(
        event.ctrlKey ||
        event.metaKey ||
        event.shiftKey
      );

      if (additive) {
        toggleSelection(path);
      } else {
        replaceSelection([path]);
      }

      return;
    }

    if (
      !isExcludedArea(event.target) &&
      !event.ctrlKey &&
      !event.metaKey &&
      !event.shiftKey
    ) {
      replaceSelection([]);
    }
  }

  function onDragStart(event) {
    const node = elementTarget(event.target);

    if (node && node.closest(".rsCard[data-rs-path]")) {
      event.preventDefault();
    }
  }

  const observer = new MutationObserver(scheduleRefresh);

  observer.observe(grid, {
    childList: true,
    subtree: true
  });

  surface.addEventListener("pointerdown", onPointerDown);
  surface.addEventListener("pointermove", onPointerMove);
  surface.addEventListener("pointerup", onPointerUp);
  surface.addEventListener("pointercancel", onPointerCancel);
  surface.addEventListener("click", onClick, true);
  surface.addEventListener("dragstart", onDragStart, true);

  window.PQNAS_REELSTACK_SELECTION = Object.freeze({
    clear() {
      replaceSelection([]);
    },

    getSelectedPaths() {
      return Array.from(selectedPaths);
    },

    setSelectedPaths(paths) {
      replaceSelection(paths);
    },

    refresh
  });

  refresh();
})();
