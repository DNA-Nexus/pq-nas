window.PQNAS_FILEMGR = window.PQNAS_FILEMGR || {};

(() => {
  "use strict";

  const FM = window.PQNAS_FILEMGR;
  const HEADER_MODES = new Set(["auto", "yes", "no"]);
  const SORT_TYPES = new Set(["text", "number", "date"]);

  const textCollator = (() => {
    try {
      return new Intl.Collator("fi", {
        usage: "sort",
        sensitivity: "base",
        numeric: false
      });
    } catch (_) {
      return null;
    }
  })();

  function cellText(value) {
    return String(value == null ? "" : value).trim();
  }

  function isEmptyValue(value) {
    return cellText(value) === "";
  }

  function parseNumberValue(value) {
    if (typeof value === "number") {
      return Number.isFinite(value) ? value : null;
    }

    let text = cellText(value).replace(
      /[\u00a0\u202f ]/g,
      ""
    );

    /*
     * Correctness: accept only a fixed whitelist of
     * supported currency tokens at one edge of the
     * value. Do not remove arbitrary letters, because
     * text such as "100 euros" must remain text.
     */
    const currencyPrefix =
      text.match(/^(?:€|\$|£|¥|kr)/i);

    const currencySuffix =
      text.match(/(?:€|\$|£|¥|kr)$/i);

    /*
     * Reject ambiguous values containing currency
     * markers at both ends, such as "€100$".
     */
    if (currencyPrefix && currencySuffix) {
      return null;
    }

    if (currencyPrefix) {
      text = text.slice(
        currencyPrefix[0].length
      );
    } else if (currencySuffix) {
      text = text.slice(
        0,
        -currencySuffix[0].length
      );
    }

    if (
      !/^[+-]?(?:\d+(?:[.,]\d+)?|[.,]\d+)$/.test(
        text
      )
    ) {
      return null;
    }

    const parsed = Number(
      text.replace(",", ".")
    );

    return Number.isFinite(parsed)
      ? parsed
      : null;
  }

  function dateValueFromParts(year, month, day) {
    const y = Number(year);
    const m = Number(month);
    const d = Number(day);

    if (
      !Number.isInteger(y) ||
      !Number.isInteger(m) ||
      !Number.isInteger(d)
    ) {
      return null;
    }

    if (
      y < 1900 ||
      y > 9999 ||
      m < 1 ||
      m > 12 ||
      d < 1 ||
      d > 31
    ) {
      return null;
    }

    const ms = Date.UTC(y, m - 1, d);
    const date = new Date(ms);

    if (
      date.getUTCFullYear() !== y ||
      date.getUTCMonth() !== m - 1 ||
      date.getUTCDate() !== d
    ) {
      return null;
    }

    return ms;
  }

  function parseDateValue(value) {
    const text = cellText(value);
    if (!text) return null;

    let match = text.match(
      /^([0-9]{1,2})\.([0-9]{1,2})\.([0-9]{4})$/
    );

    if (match) {
      return dateValueFromParts(
        match[3],
        match[2],
        match[1]
      );
    }

    match = text.match(
      /^([0-9]{4})-([0-9]{1,2})-([0-9]{1,2})$/
    );

    if (match) {
      return dateValueFromParts(
        match[1],
        match[2],
        match[3]
      );
    }

    return null;
  }

  function valueType(value) {
    if (isEmptyValue(value)) return "empty";
    if (parseDateValue(value) != null) return "date";
    if (parseNumberValue(value) != null) return "number";
    return "text";
  }

  function inferSortType(values) {
    const types = new Set();

    for (
      const value of
      Array.isArray(values) ? values : []
    ) {
      const type = valueType(value);
      if (type !== "empty") types.add(type);
    }

    if (
      types.size === 1 &&
      types.has("number")
    ) {
      return "number";
    }

    if (
      types.size === 1 &&
      types.has("date")
    ) {
      return "date";
    }

    return "text";
  }

  function normalizeHeaderMode(value) {
    const mode = String(value || "auto")
      .trim()
      .toLowerCase();

    return HEADER_MODES.has(mode)
      ? mode
      : "auto";
  }

  function normalizeDirection(value) {
    const direction = String(value || "asc")
      .trim()
      .toLowerCase();

    return direction === "desc"
      ? "desc"
      : "asc";
  }

  function normalizeSortType(
    value,
    fallbackValues
  ) {
    const type = String(value || "")
      .trim()
      .toLowerCase();

    return SORT_TYPES.has(type)
      ? type
      : inferSortType(fallbackValues);
  }

  function autoHeaderDetected(
    rows,
    keyCol
  ) {
    if (
      !Array.isArray(rows) ||
      rows.length < 2
    ) {
      return false;
    }

    const first = Array.isArray(rows[0])
      ? rows[0][keyCol]
      : "";

    if (valueType(first) !== "text") {
      return false;
    }

    const remainder = rows
      .slice(1)
      .map((row) =>
        Array.isArray(row)
          ? row[keyCol]
          : ""
      );

    const remainderType =
      inferSortType(remainder);

    /*
     * Correctness: automatic detection is deliberately
     * conservative. The first text value is considered a
     * header only when the remaining column is uniformly
     * numeric or date-valued. This avoids dropping the
     * first ordinary row from text-only data.
     */
    return (
      remainderType === "number" ||
      remainderType === "date"
    );
  }

  function compareText(a, b) {
    const left = cellText(a);
    const right = cellText(b);

    if (textCollator) {
      return textCollator.compare(
        left,
        right
      );
    }

    return left < right
      ? -1
      : left > right
        ? 1
        : 0;
  }

  function compareTypedValues(
    a,
    b,
    type
  ) {
    if (type === "number") {
      const left = parseNumberValue(a);
      const right = parseNumberValue(b);

      if (
        left != null &&
        right != null
      ) {
        return left - right;
      }
    } else if (type === "date") {
      const left = parseDateValue(a);
      const right = parseDateValue(b);

      if (
        left != null &&
        right != null
      ) {
        return left - right;
      }
    }

    return compareText(a, b);
  }

  function sortRows(
    rows,
    options = {}
  ) {
    if (!Array.isArray(rows)) {
      return {
        ok: false,
        error: "invalid_rows",
        rows: []
      };
    }

    const keyCol = Number(
      options.keyCol
    );

    if (
      !Number.isInteger(keyCol) ||
      keyCol < 0
    ) {
      return {
        ok: false,
        error: "invalid_key_column",
        rows: rows.map((row) =>
          Array.isArray(row)
            ? row.slice()
            : []
        )
      };
    }

    /*
     * Correctness: clone before sorting so history
     * snapshots and workbook state are not mutated
     * before the editor explicitly accepts the result.
     */
    const clonedRows = rows.map(
      (row) =>
        Array.isArray(row)
          ? row.slice()
          : []
    );

    const headerMode =
      normalizeHeaderMode(
        options.header
      );

    const hasHeader =
      headerMode === "yes" ||
      (
        headerMode === "auto" &&
        autoHeaderDetected(
          clonedRows,
          keyCol
        )
      );

    const headerRows =
      hasHeader && clonedRows.length
        ? clonedRows.slice(0, 1)
        : [];

    const dataRows =
      hasHeader
        ? clonedRows.slice(1)
        : clonedRows;

    const type = normalizeSortType(
      options.type,
      dataRows.map(
        (row) => row[keyCol]
      )
    );

    const direction =
      normalizeDirection(
        options.direction
      );

    const decorated = dataRows.map(
      (row, index) => ({
        row,
        index,
        sourceIndex:
          index + (hasHeader ? 1 : 0),
        value: row[keyCol]
      })
    );

    decorated.sort(
      (left, right) => {
        const leftEmpty =
          isEmptyValue(left.value);

        const rightEmpty =
          isEmptyValue(right.value);

        /*
         * Spreadsheet convention: empty cells stay at
         * the bottom in both sort directions.
         */
        if (
          leftEmpty ||
          rightEmpty
        ) {
          if (
            leftEmpty &&
            rightEmpty
          ) {
            return (
              left.index -
              right.index
            );
          }

          return leftEmpty
            ? 1
            : -1;
        }

        let compared =
          compareTypedValues(
            left.value,
            right.value,
            type
          );

        if (
          direction === "desc"
        ) {
          compared = -compared;
        }

        /*
         * Stable fallback keeps equal rows in their
         * original relative order.
         */
        return (
          compared ||
          left.index -
            right.index
        );
      }
    );

    return {
      ok: true,
      rows: headerRows.concat(
        decorated.map(
          (item) => item.row
        )
      ),
      order: (
        hasHeader ? [0] : []
      ).concat(
        decorated.map(
          (item) => item.sourceIndex
        )
      ),
      keyCol,
      direction,
      type,
      hasHeader
    };
  }


  function normalizeSortRange(range, rowCount) {
    if (
      !range ||
      typeof range !== "object"
    ) {
      return null;
    }

    const row1 = Number(range.row1);
    const row2 = Number(range.row2);
    const col1 = Number(range.col1);
    const col2 = Number(range.col2);

    if (
      ![
        row1,
        row2,
        col1,
        col2
      ].every(Number.isInteger)
    ) {
      return null;
    }

    if (
      row1 < 0 ||
      row2 < row1 ||
      row2 >= rowCount ||
      col1 < 0 ||
      col2 < col1
    ) {
      return null;
    }

    return {
      row1,
      row2,
      col1,
      col2
    };
  }

  function sortRange(
    rows,
    range,
    options = {}
  ) {
    if (!Array.isArray(rows)) {
      return {
        ok: false,
        error: "invalid_rows",
        rows: []
      };
    }

    const normalized =
      normalizeSortRange(
        range,
        rows.length
      );

    if (!normalized) {
      return {
        ok: false,
        error: "invalid_range",
        rows: rows.map((row) =>
          Array.isArray(row)
            ? row.slice()
            : []
        )
      };
    }

    const keyCol = Number(
      options.keyCol
    );

    if (
      !Number.isInteger(keyCol) ||
      keyCol < normalized.col1 ||
      keyCol > normalized.col2
    ) {
      return {
        ok: false,
        error: "invalid_key_column",
        rows: rows.map((row) =>
          Array.isArray(row)
            ? row.slice()
            : []
        )
      };
    }

    /*
     * Correctness: clone the complete matrix before
     * sorting. Invalid input or a failed sort must not
     * partially modify the workbook's live row arrays.
     */
    const clonedRows = rows.map(
      (row) =>
        Array.isArray(row)
          ? row.slice()
          : []
    );

    const width =
      normalized.col2 -
      normalized.col1 +
      1;

    const blockRows = [];

    for (
      let row = normalized.row1;
      row <= normalized.row2;
      row++
    ) {
      const source = clonedRows[row];
      const block = [];

      for (
        let offset = 0;
        offset < width;
        offset++
      ) {
        const value =
          source[
            normalized.col1 +
            offset
          ];

        block.push(
          value == null
            ? ""
            : value
        );
      }

      blockRows.push(block);
    }

    const result = sortRows(
      blockRows,
      {
        keyCol:
          keyCol -
          normalized.col1,
        direction:
          options.direction,
        header:
          options.header,
        type:
          options.type
      }
    );

    if (
      !result ||
      result.ok !== true ||
      !Array.isArray(result.rows) ||
      !Array.isArray(result.order) ||
      result.rows.length !==
        blockRows.length ||
      result.order.length !==
        blockRows.length
    ) {
      return {
        ok: false,
        error: "sort_failed",
        rows: clonedRows
      };
    }

    for (
      let offset = 0;
      offset < result.rows.length;
      offset++
    ) {
      const targetRow =
        normalized.row1 +
        offset;

      const target =
        clonedRows[targetRow];

      while (
        target.length <=
        normalized.col2
      ) {
        target.push("");
      }

      for (
        let colOffset = 0;
        colOffset < width;
        colOffset++
      ) {
        target[
          normalized.col1 +
          colOffset
        ] =
          result.rows[offset][
            colOffset
          ];
      }
    }

    return {
      ok: true,
      rows: clonedRows,

      /*
       * Absolute source row indexes let the editor move
       * matching formatting slices without guessing how
       * the selected block was reordered.
       */
      order: result.order.map(
        (sourceOffset) =>
          normalized.row1 +
          sourceOffset
      ),

      range: normalized,
      keyCol,
      direction: result.direction,
      type: result.type,
      hasHeader: result.hasHeader
    };
  }

  FM.spreadsheetSort = {
    parseNumberValue,
    parseDateValue,
    valueType,
    inferSortType,
    autoHeaderDetected,
    sortRows,
    sortRange
  };
})();
