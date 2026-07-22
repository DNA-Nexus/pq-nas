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

  function columnNameFromIndex(index) {
    const value = Number(index);

    if (
      !Number.isInteger(value) ||
      value < 0 ||
      value >= DEFAULT_MAX_COLS
    ) {
      return "";
    }

    let number = value + 1;
    let out = "";

    while (number > 0) {
      const remainder =
        (number - 1) % 26;

      out =
        String.fromCharCode(
          65 + remainder
        ) + out;

      number =
        Math.floor(
          (number - 1) / 26
        );
    }

    return out;
  }

  function cellReferenceText(row, col) {
    const rowIndex = Number(row);
    const colIndex = Number(col);
    const column =
      columnNameFromIndex(colIndex);

    if (
      !column ||
      !Number.isInteger(rowIndex) ||
      rowIndex < 0 ||
      rowIndex >= DEFAULT_MAX_ROWS
    ) {
      return "";
    }

    return `${column}${rowIndex + 1}`;
  }

  function parseFormulaSheetName(value) {
    const token = String(value || "");

    if (!token.endsWith("!")) {
      return "";
    }

    const source =
      token.slice(0, -1);

    if (
      source.length >= 2 &&
      source.startsWith("'") &&
      source.endsWith("'")
    ) {
      return source
        .slice(1, -1)
        .replace(/''/g, "'");
    }

    return source;
  }

  function formatFormulaSheetName(value) {
    const name = String(value || "");

    if (!name) return "";

    /*
     * Correctness: cell-like worksheet names must be quoted
     * so Excel does not interpret the name itself as A1 data.
     */
    const canStayBare =
      /^[A-Za-z0-9_.]+$/.test(name) &&
      !parseCellReference(name);

    if (canStayBare) {
      return name;
    }

    return (
      "'" +
      name.replace(/'/g, "''") +
      "'"
    );
  }

  function formatFormulaReference(
    sheetName,
    row1,
    col1,
    row2 = row1,
    col2 = col1
  ) {
    const first =
      cellReferenceText(row1, col1);

    const second =
      cellReferenceText(row2, col2);

    if (!first || !second) {
      return "";
    }

    const prefix = sheetName
      ? `${formatFormulaSheetName(sheetName)}!`
      : "";

    return (
      prefix +
      (
        first === second
          ? first
          : `${first}:${second}`
      )
    );
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
    const includeCrossSheet =
      options.includeCrossSheet === true;

    /*
     * Security and performance: parse only bounded A1-style
     * references. Formula text is never evaluated and the
     * number of generated highlights is strictly limited.
     *
     * Supported worksheet qualifiers:
     *   Sheet2!A1
     *   Sheet2!A1:B5
     *   'Myynti 2026'!$B$2
     *   'O''Brien'!A1
     *
     * External workbook references are intentionally excluded.
     */
    const expression =
      /(^|[^A-Za-z0-9_.\]'#])((?:'(?:[^']|'')+'|[A-Za-z0-9_.]+)!)?(\$?[A-Za-z]{1,3}\$?[1-9][0-9]*)(?:\s*:\s*(\$?[A-Za-z]{1,3}\$?[1-9][0-9]*))?(?![A-Za-z0-9_.])/g;

    let match = null;

    while (
      references.length < MAX_REFERENCES &&
      (match = expression.exec(masked))
    ) {
      const prefix =
        match[1] || "";

      const qualifier =
        match[2] || "";

      const sheetName =
        parseFormulaSheetName(
          qualifier
        );

      const firstText = match[3];
      const secondText = match[4] || "";

      /*
       * Correctness and security: if an unsupported qualifier
       * ends immediately before "!A1", do not reinterpret the
       * trailing A1 token as a same-sheet reference. This keeps
       * external workbook references such as
       * [Book.xlsx]Sheet1!A1 outside the supported grammar.
       */
      if (
        !qualifier &&
        prefix === "!"
      ) {
        continue;
      }

      if (
        sheetName &&
        !includeCrossSheet
      ) {
        continue;
      }

      /*
       * Avoid treating function names such as LOG10(...)
       * as cell references. A qualified LOG10!A1 remains a
       * valid worksheet reference.
       */
      if (
        !sheetName &&
        !secondText &&
        /^\s*\(/.test(
          masked.slice(
            expression.lastIndex
          )
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

      const sheetKey =
        sheetName.toLowerCase();

      const key =
        `${sheetKey}:${row1}:${col1}:${row2}:${col2}`;

      if (seen.has(key)) {
        continue;
      }

      seen.add(key);

      references.push({
        sheetName,
        row1,
        col1,
        row2,
        col2,
        referenceIndex:
          references.length,
        text:
          qualifier +
          (
            secondText
              ? `${firstText}:${secondText}`
              : firstText
          )
      });
    }

    return references;
  }

  function normalizedSheetKey(value) {
    return String(value || "")
      .trim()
      .toLowerCase();
  }

  function referenceTargetsSheet(
    reference,
    activeSheetName,
    formulaSheetName
  ) {
    const activeKey =
      normalizedSheetKey(activeSheetName);

    if (!activeKey) {
      return true;
    }

    const referenceSheet =
      reference &&
      reference.sheetName
        ? reference.sheetName
        : formulaSheetName;

    return (
      normalizedSheetKey(
        referenceSheet
      ) === activeKey
    );
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

    const parsedReferences =
      parseFormulaReferences(
        formula,
        options
      );

    const references =
      options.activeSheetName
        ? parsedReferences.filter(
            (reference) =>
              referenceTargetsSheet(
                reference,
                options.activeSheetName,
                options.formulaSheetName
              )
          )
        : parsedReferences;

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
    columnNameFromIndex,
    referenceColorIndex,
    parseCellReference,
    cellReferenceText,
    parseFormulaSheetName,
    formatFormulaSheetName,
    formatFormulaReference,
    parseFormulaReferences,
    referenceTargetsSheet,
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
