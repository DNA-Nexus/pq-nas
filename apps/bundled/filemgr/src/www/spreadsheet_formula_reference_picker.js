(function (root) {
  "use strict";

  const FM =
    root.PQNAS_FILEMGR =
      root.PQNAS_FILEMGR || {};

  function boundedInteger(
    value,
    minimum,
    maximum
  ) {
    const number = Number(value);

    if (
      !Number.isInteger(number) ||
      number < minimum ||
      number > maximum
    ) {
      return null;
    }

    return number;
  }

  function normalizeRange(
    startRow,
    startCol,
    endRow,
    endCol,
    options = {}
  ) {
    const maxRows =
      Number.isInteger(options.maxRows) &&
      options.maxRows > 0
        ? options.maxRows
        : 1000000;

    const maxCols =
      Number.isInteger(options.maxCols) &&
      options.maxCols > 0
        ? options.maxCols
        : 16384;

    /*
     * Security and performance: pointer-derived workbook
     * coordinates are accepted only inside explicit bounds.
     * They are never used as selectors or executable code.
     */
    const sr = boundedInteger(
      startRow,
      0,
      maxRows - 1
    );

    const sc = boundedInteger(
      startCol,
      0,
      maxCols - 1
    );

    const er = boundedInteger(
      endRow,
      0,
      maxRows - 1
    );

    const ec = boundedInteger(
      endCol,
      0,
      maxCols - 1
    );

    if (
      sr == null ||
      sc == null ||
      er == null ||
      ec == null
    ) {
      return null;
    }

    return {
      row1: Math.min(sr, er),
      col1: Math.min(sc, ec),
      row2: Math.max(sr, er),
      col2: Math.max(sc, ec)
    };
  }

  function replaceSelection(
    value,
    selectionStart,
    selectionEnd,
    replacement
  ) {
    const source =
      String(value == null ? "" : value);

    const inserted =
      String(
        replacement == null
          ? ""
          : replacement
      );

    const startNumber =
      Number(selectionStart);

    const endNumber =
      Number(selectionEnd);

    const start =
      Number.isInteger(startNumber)
        ? Math.max(
            0,
            Math.min(
              startNumber,
              source.length
            )
          )
        : source.length;

    const end =
      Number.isInteger(endNumber)
        ? Math.max(
            start,
            Math.min(
              endNumber,
              source.length
            )
          )
        : start;

    const next =
      source.slice(0, start) +
      inserted +
      source.slice(end);

    const caret =
      start + inserted.length;

    return {
      value: next,
      selectionStart: caret,
      selectionEnd: caret,
      referenceStart: start,
      referenceEnd: caret
    };
  }

  function cellAtPoint(
    documentObject,
    clientX,
    clientY
  ) {
    if (
      !documentObject ||
      typeof documentObject.elementFromPoint !==
        "function" ||
      !Number.isFinite(Number(clientX)) ||
      !Number.isFinite(Number(clientY))
    ) {
      return null;
    }

    const target =
      documentObject.elementFromPoint(
        Number(clientX),
        Number(clientY)
      );

    const input =
      target &&
      typeof target.closest === "function"
        ? target.closest(
            "input[data-row][data-col]"
          )
        : null;

    if (!input || !input.dataset) {
      return null;
    }

    const row = Number(input.dataset.row);
    const col = Number(input.dataset.col);

    if (
      !Number.isInteger(row) ||
      !Number.isInteger(col) ||
      row < 0 ||
      col < 0
    ) {
      return null;
    }

    return {
      input,
      row,
      col
    };
  }

  FM.spreadsheetFormulaReferencePicker = {
    normalizeRange,
    replaceSelection,
    cellAtPoint
  };
})(
  typeof window !== "undefined"
    ? window
    : globalThis
);
