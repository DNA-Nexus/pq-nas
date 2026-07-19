(function registerSpreadsheetFormulaReferences(global) {
  "use strict";

  const MAX_REFERENCES = 256;
  const REFERENCE_COLOR_COUNT = 10;
  const DEFAULT_MAX_ROWS = 1048576;
  const DEFAULT_MAX_COLS = 16384;

  function positiveInteger(value, fallback) {
    const number = Number(value);

    return Number.isInteger(number) && number > 0
      ? number
      : fallback;
  }

  function referenceColorIndex(value) {
    const index = Number(value);

    if (
      !Number.isInteger(index) ||
      index < 0
    ) {
      return 0;
    }

    return index % REFERENCE_COLOR_COUNT;
  }

  function columnIndexFromName(name) {
    const text = String(
      name || ""
    ).toUpperCase();

    if (!/^[A-Z]{1,3}$/.test(text)) {
      return -1;
    }

    let value = 0;

    for (const character of text) {
      value =
        value * 26 +
        character.charCodeAt(0) -
        64;
    }

    return value - 1;
  }

  function parseCellReference(
    value,
    options = {}
  ) {
    const match = String(
      value || ""
    ).match(
      /^\$?([A-Za-z]{1,3})\$?([1-9][0-9]*)$/
    );

    if (!match) return null;

    const col = columnIndexFromName(
      match[1]
    );
    const row = Number(match[2]) - 1;

    const maxRows = positiveInteger(
      options.maxRows,
      DEFAULT_MAX_ROWS
    );
    const maxCols = positiveInteger(
      options.maxCols,
      DEFAULT_MAX_COLS
    );

    if (
      row < 0 ||
      row >= maxRows ||
      col < 0 ||
      col >= maxCols
    ) {
      return null;
    }

    return {
      row,
      col
    };
  }

  function maskFormulaStrings(value) {
    const characters = Array.from(
      String(value || "")
    );

    let insideString = false;

    for (
      let index = 0;
      index < characters.length;
      index += 1
    ) {
      if (characters[index] !== '"') {
        if (insideString) {
          characters[index] = " ";
        }

        continue;
      }

      if (
        insideString &&
        characters[index + 1] === '"'
      ) {
        characters[index] = " ";
        characters[index + 1] = " ";
        index += 1;
        continue;
      }

      insideString = !insideString;
      characters[index] = " ";
    }

    return characters.join("");
  }

  function parseFormulaReferences(
    formula,
    options = {}
  ) {
    const masked = maskFormulaStrings(
      formula
    );

    const references = [];
    const seen = new Set();

    /*
     * Security and performance: parse only bounded A1-style
     * references. Formula text is never evaluated and the
     * number of generated highlights is strictly limited.
     */
    const expression =
      /(^|[^A-Za-z0-9_.])(\$?[A-Za-z]{1,3}\$?[1-9][0-9]*)(?:\s*:\s*(\$?[A-Za-z]{1,3}\$?[1-9][0-9]*))?(?![A-Za-z0-9_.])/g;

    let match = null;

    while (
      references.length < MAX_REFERENCES &&
      (match = expression.exec(masked))
    ) {
      const prefix = match[1] || "";
      const firstText = match[2];
      const secondText = match[3] || "";
      const followingCharacter =
        masked.charAt(expression.lastIndex);

      /*
       * References connected to a sheet-name separator are
       * cross-sheet references. Painting another sheet is
       * intentionally deferred.
       */
      if (
        prefix === "!" ||
        followingCharacter === "!"
      ) {
        continue;
      }

      /*
       * Avoid treating function names such as LOG10(...)
       * as cell references.
       */
      if (
        !secondText &&
        /^\s*\(/.test(
          masked.slice(expression.lastIndex)
        )
      ) {
        continue;
      }

      const first = parseCellReference(
        firstText,
        options
      );

      const second = secondText
        ? parseCellReference(
            secondText,
            options
          )
        : first;

      if (!first || !second) {
        continue;
      }

      const row1 = Math.min(
        first.row,
        second.row
      );
      const row2 = Math.max(
        first.row,
        second.row
      );
      const col1 = Math.min(
        first.col,
        second.col
      );
      const col2 = Math.max(
        first.col,
        second.col
      );

      const key =
        `${row1}:${col1}:${row2}:${col2}`;

      if (seen.has(key)) {
        continue;
      }

      seen.add(key);

      references.push({
        row1,
        col1,
        row2,
        col2,
        referenceIndex:
          references.length,
        text: secondText
          ? `${firstText}:${secondText}`
          : firstText
      });
    }

    return references;
  }

  function clear(root) {
    if (
      !root ||
      typeof root.querySelectorAll !==
        "function"
    ) {
      return;
    }

    for (
      const overlay of root.querySelectorAll(
        ".spreadsheetFormulaReferenceOverlay"
      )
    ) {
      overlay.remove();
    }

    for (
      const cell of root.querySelectorAll(
        ".spreadsheetFormulaReferenceHost"
      )
    ) {
      cell.classList.remove(
        "spreadsheetFormulaReferenceHost"
      );
    }
  }

  function paint(
    root,
    formula,
    options = {}
  ) {
    clear(root);

    if (
      !root ||
      typeof root.querySelectorAll !==
        "function"
    ) {
      return [];
    }

    const references =
      parseFormulaReferences(
        formula,
        options
      );

    if (!references.length) {
      return references;
    }

    const documentRef =
      root.ownerDocument ||
      (
        typeof document !== "undefined"
          ? document
          : null
      );

    if (!documentRef) {
      return references;
    }

    const visibleCells = new Map();

    let minRow = Number.POSITIVE_INFINITY;
    let maxRow = -1;
    let minCol = Number.POSITIVE_INFINITY;
    let maxCol = -1;

    for (
      const cell of root.querySelectorAll(
        "td[data-row][data-col]"
      )
    ) {
      const row = Number(
        cell.dataset && cell.dataset.row
      );
      const col = Number(
        cell.dataset && cell.dataset.col
      );

      if (
        !Number.isInteger(row) ||
        !Number.isInteger(col)
      ) {
        continue;
      }

      visibleCells.set(
        `${row}:${col}`,
        cell
      );

      minRow = Math.min(minRow, row);
      maxRow = Math.max(maxRow, row);
      minCol = Math.min(minCol, col);
      maxCol = Math.max(maxCol, col);
    }

    if (!visibleCells.size) {
      return references;
    }

    for (const reference of references) {
      const visibleRow1 = Math.max(
        reference.row1,
        minRow
      );
      const visibleRow2 = Math.min(
        reference.row2,
        maxRow
      );
      const visibleCol1 = Math.max(
        reference.col1,
        minCol
      );
      const visibleCol2 = Math.min(
        reference.col2,
        maxCol
      );

      if (
        visibleRow1 > visibleRow2 ||
        visibleCol1 > visibleCol2
      ) {
        continue;
      }

      for (
        let row = visibleRow1;
        row <= visibleRow2;
        row += 1
      ) {
        for (
          let col = visibleCol1;
          col <= visibleCol2;
          col += 1
        ) {
          const cell = visibleCells.get(
            `${row}:${col}`
          );

          if (!cell) continue;

          const overlay =
            documentRef.createElement("span");

          const colorIndex =
            referenceColorIndex(
              reference.referenceIndex
            );

          overlay.className =
            "spreadsheetFormulaReferenceOverlay " +
            `spreadsheetFormulaReferenceColor${colorIndex}`;

          overlay.setAttribute(
            "aria-hidden",
            "true"
          );

          if (row === reference.row1) {
            overlay.classList.add(
              "spreadsheetFormulaReferenceEdgeTop"
            );
          }

          if (row === reference.row2) {
            overlay.classList.add(
              "spreadsheetFormulaReferenceEdgeBottom"
            );
          }

          if (col === reference.col1) {
            overlay.classList.add(
              "spreadsheetFormulaReferenceEdgeLeft"
            );
          }

          if (col === reference.col2) {
            overlay.classList.add(
              "spreadsheetFormulaReferenceEdgeRight"
            );
          }

          cell.classList.add(
            "spreadsheetFormulaReferenceHost"
          );

          /*
           * Security: no workbook-controlled content is
           * inserted as HTML. The overlay is an empty,
           * application-created element.
           */
          cell.appendChild(overlay);
        }
      }
    }

    return references;
  }

  const api = {
    columnIndexFromName,
    referenceColorIndex,
    parseCellReference,
    parseFormulaReferences,
    clear,
    paint
  };

  if (
    typeof module !== "undefined" &&
    module.exports
  ) {
    module.exports = api;
  }

  if (global) {
    global.PQNAS_FILEMGR =
      global.PQNAS_FILEMGR || {};

    global.PQNAS_FILEMGR
      .spreadsheetFormulaReferences = api;
  }
})(
  typeof window !== "undefined"
    ? window
    : globalThis
);
