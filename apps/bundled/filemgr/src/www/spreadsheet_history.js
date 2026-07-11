window.PQNAS_FILEMGR = window.PQNAS_FILEMGR || {};

(() => {
  "use strict";

  const FM = window.PQNAS_FILEMGR;
  const DEFAULT_MAX_DEPTH = 100;

  function clampDepth(value) {
    const n = Number(value);
    if (!Number.isFinite(n)) return DEFAULT_MAX_DEPTH;
    return Math.max(10, Math.min(500, Math.round(n)));
  }

  function create(options = {}) {
    const maxDepth = clampDepth(options.maxDepth);
    const undoStack = [];
    const redoStack = [];
    let revision = 0;
    let cleanRevision = 0;

    function pushLimited(stack, entry) {
      stack.push(entry);
      while (stack.length > maxDepth) stack.shift();
    }

    function record(beforeSnapshot) {
      if (!beforeSnapshot || typeof beforeSnapshot !== "object") return false;

      // History snapshots are workbook data only. They are restored into state
      // and rendered through the editor's normal text/value paths, never as HTML.
      pushLimited(undoStack, {
        snapshot: beforeSnapshot,
        revision
      });

      redoStack.length = 0;
      revision += 1;
      return true;
    }

    function undo(currentSnapshot) {
      if (!undoStack.length) return null;

      const entry = undoStack.pop();
      if (currentSnapshot && typeof currentSnapshot === "object") {
        pushLimited(redoStack, {
          snapshot: currentSnapshot,
          revision
        });
      }

      revision = entry.revision;
      return entry.snapshot;
    }

    function redo(currentSnapshot) {
      if (!redoStack.length) return null;

      const entry = redoStack.pop();
      if (currentSnapshot && typeof currentSnapshot === "object") {
        pushLimited(undoStack, {
          snapshot: currentSnapshot,
          revision
        });
      }

      revision = entry.revision;
      return entry.snapshot;
    }

    function markClean() {
      cleanRevision = revision;
    }

    function isDirty() {
      return revision !== cleanRevision;
    }

    return {
      record,
      undo,
      redo,
      markClean,
      isDirty,
      canUndo: () => undoStack.length > 0,
      canRedo: () => redoStack.length > 0
    };
  }

  FM.spreadsheetHistory = { create };
})();
