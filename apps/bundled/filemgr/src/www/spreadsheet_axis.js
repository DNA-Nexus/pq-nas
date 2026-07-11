window.PQNAS_FILEMGR = window.PQNAS_FILEMGR || {};

(() => {
  "use strict";

  const FM = window.PQNAS_FILEMGR;

  function isAxis(type) {
    return type === "row" || type === "column";
  }

  function toIndex(value) {
    const n = Number(value);
    return Number.isInteger(n) && n >= 0 ? n : -1;
  }

  function normalizeSelection(selection) {
    if (!selection || !isAxis(selection.type)) return null;

    const index = toIndex(selection.index);
    const startRaw = toIndex(selection.start);
    const endRaw = toIndex(selection.end);
    const anchorRaw = toIndex(selection.anchor);

    const start = startRaw >= 0 ? startRaw : index;
    const end = endRaw >= 0 ? endRaw : index;
    const anchor = anchorRaw >= 0 ? anchorRaw : start;

    if (index < 0 || start < 0 || end < 0) return null;

    return {
      type: selection.type,
      index,
      start: Math.min(start, end),
      end: Math.max(start, end),
      anchor
    };
  }

  function single(type, index) {
    if (!isAxis(type) || toIndex(index) < 0) return null;
    const i = toIndex(index);
    return { type, index: i, start: i, end: i, anchor: i };
  }

  function selectionFromClick(current, type, index, modifiers = {}) {
    if (!isAxis(type) || toIndex(index) < 0) return null;

    const i = toIndex(index);
    const prev = normalizeSelection(current);
    const extend = !!(modifiers.shiftKey || modifiers.ctrlKey || modifiers.metaKey);

    if (extend && prev && prev.type === type) {
      const anchor = toIndex(prev.anchor) >= 0 ? toIndex(prev.anchor) : toIndex(prev.index);
      return {
        type,
        index: i,
        start: Math.min(anchor, i),
        end: Math.max(anchor, i),
        anchor
      };
    }

    return single(type, i);
  }

  function range(selection) {
    const s = normalizeSelection(selection);
    if (!s) return null;
    return { type: s.type, start: s.start, end: s.end, count: s.end - s.start + 1 };
  }

  function contains(selection, type, index) {
    const r = range(selection);
    const i = toIndex(index);
    return !!r && r.type === type && i >= r.start && i <= r.end;
  }

  function insertSpec(selection, type, fallbackIndex, total, limit) {
    const totalCount = Math.max(0, Number(total) || 0);
    const maxCount = Math.max(0, Number(limit) || 0);
    const remaining = Math.max(0, maxCount - totalCount);
    if (!remaining) return { index: totalCount, count: 0 };

    const r = range(selection);
    const selected = r && r.type === type
      ? r
      : { start: toIndex(fallbackIndex), end: toIndex(fallbackIndex), count: 1 };

    const rawIndex = selected.start >= 0 ? selected.start : totalCount;
    const index = Math.max(0, Math.min(rawIndex, totalCount));
    const count = Math.max(1, Math.min(selected.count || 1, remaining));

    return { index, count };
  }

  function deleteSpec(selection, type, fallbackIndex, total) {
    const totalCount = Math.max(0, Number(total) || 0);
    if (!totalCount) return { index: 0, count: 0 };

    const r = range(selection);
    const selected = r && r.type === type
      ? r
      : { start: toIndex(fallbackIndex), end: toIndex(fallbackIndex), count: 1 };

    const rawIndex = selected.start >= 0 ? selected.start : toIndex(fallbackIndex);
    const index = Math.max(0, Math.min(rawIndex, totalCount - 1));
    const count = Math.max(1, Math.min(selected.count || 1, totalCount - index));

    return { index, count };
  }

  function operationLabel(action, type, count) {
    const n = Math.max(1, Number(count) || 1);
    const many = n > 1;

    if (action === "insert" && type === "row") return many ? `Insert ${n} rows here` : "Insert row here";
    if (action === "delete" && type === "row") return many ? `Delete ${n} rows` : "Delete row";
    if (action === "insert" && type === "column") return many ? `Insert ${n} columns here` : "Insert column here";
    if (action === "delete" && type === "column") return many ? `Delete ${n} columns` : "Delete column";

    return "";
  }

  function headerAxisFromEvent(table, ev) {
    const header = ev && ev.target && ev.target.closest
      ? ev.target.closest("th[data-col], th[data-row]")
      : null;

    if (!header || !table || !table.contains(header)) return null;

    if (header.dataset.col != null) {
      const index = toIndex(header.dataset.col);
      return index >= 0 ? { type: "column", index } : null;
    }

    if (header.dataset.row != null) {
      const index = toIndex(header.dataset.row);
      return index >= 0 ? { type: "row", index } : null;
    }

    return null;
  }

  function stopAxisHeaderEvent(ev) {
    if (!ev) return;
    ev.preventDefault();
    ev.stopPropagation();

    if (typeof ev.stopImmediatePropagation === "function") {
      ev.stopImmediatePropagation();
    }
  }

  function attachHeaderSelectionHandlers(table, handlers = {}) {
    if (!table || table.dataset.axisSelectionCleanAttached === "1") return;
    table.dataset.axisSelectionCleanAttached = "1";

    const onSelect = typeof handlers.select === "function" ? handlers.select : null;
    const onContextMenu = typeof handlers.contextMenu === "function" ? handlers.contextMenu : null;

    const activate = (ev) => {
      const axis = headerAxisFromEvent(table, ev);
      if (!axis || !onSelect) return;

      stopAxisHeaderEvent(ev);
      onSelect(axis.type, axis.index, ev);
    };

    table.addEventListener("click", activate, true);
    table.addEventListener("keydown", (ev) => {
      if (ev.key === "Enter" || ev.key === " ") activate(ev);
    }, true);

    table.addEventListener("contextmenu", (ev) => {
      const axis = headerAxisFromEvent(table, ev);
      if (!axis || !onContextMenu) return;

      stopAxisHeaderEvent(ev);
      onContextMenu(axis.type, axis.index, ev);
    }, true);
  }


  let contextMenu = null;

  function hideContextMenu() {
    if (!contextMenu || contextMenu.hidden) return false;
    contextMenu.hidden = true;
    contextMenu.replaceChildren();
    return true;
  }

  function makeContextMenuButton(label, onClick) {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.textContent = String(label || "");
    btn.addEventListener("click", (ev) => {
      ev.preventDefault();
      ev.stopPropagation();
      hideContextMenu();
      if (typeof onClick === "function") onClick();
    });
    return btn;
  }

  function ensureContextMenu() {
    if (contextMenu) return contextMenu;

    contextMenu = document.createElement("div");
    contextMenu.className = "spreadsheetAxisMenu";
    contextMenu.hidden = true;
    contextMenu.setAttribute("role", "menu");

    contextMenu.addEventListener("click", (ev) => {
      ev.stopPropagation();
    });

    document.body.appendChild(contextMenu);
    return contextMenu;
  }

  function positionContextMenu(menu, x, y) {
    const left = Math.max(8, Number(x) || 8);
    const top = Math.max(8, Number(y) || 8);

    menu.style.left = `${left}px`;
    menu.style.top = `${top}px`;

    const rect = menu.getBoundingClientRect();
    const maxLeft = Math.max(8, window.innerWidth - rect.width - 8);
    const maxTop = Math.max(8, window.innerHeight - rect.height - 8);

    menu.style.left = `${Math.min(left, maxLeft)}px`;
    menu.style.top = `${Math.min(top, maxTop)}px`;
  }

  function openContextMenu(type, index, x, y, options = {}) {
    if (!isAxis(type) || toIndex(index) < 0) return;
    if (options.disabled) return;

    const i = toIndex(index);

    if (typeof options.contains === "function" && !options.contains(type, i)) {
      if (typeof options.select === "function") options.select(type, i);
    }

    const labelFor = (action) => {
      if (typeof options.label === "function") {
        const label = options.label(action, type);
        if (label) return label;
      }
      return operationLabel(action, type, 1);
    };

    const menu = ensureContextMenu();
    menu.replaceChildren();

    menu.appendChild(makeContextMenuButton(labelFor("insert"), () => {
      if (typeof options.insert === "function") options.insert(type, i);
    }));

    menu.appendChild(makeContextMenuButton(labelFor("delete"), () => {
      if (typeof options.delete === "function") options.delete(type, i);
    }));

    menu.hidden = false;
    positionContextMenu(menu, x, y);
  }

  FM.spreadsheetAxis = {
    normalizeSelection,
    selectionFromClick,
    range,
    contains,
    insertSpec,
    deleteSpec,
    operationLabel,
    attachHeaderSelectionHandlers,
    hideContextMenu,
    openContextMenu
  };
})();
