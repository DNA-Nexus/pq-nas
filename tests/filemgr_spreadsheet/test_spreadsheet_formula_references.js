"use strict";

const assert = require("assert");

const references = require(
  "../../apps/bundled/filemgr/src/www/" +
  "spreadsheet_formula_references.js"
);

function compact(items) {
  return items.map((item) => ({
    row1: item.row1,
    col1: item.col1,
    row2: item.row2,
    col2: item.col2,
    referenceIndex:
      item.referenceIndex
  }));
}

assert.strictEqual(
  references.columnIndexFromName("A"),
  0
);

assert.strictEqual(
  references.columnIndexFromName("Z"),
  25
);

assert.strictEqual(
  references.columnIndexFromName("AA"),
  26
);

assert.strictEqual(
  references.columnIndexFromName("XFD"),
  16383
);

assert.deepStrictEqual(
  compact(
    references.parseFormulaReferences(
      "=A5*A6+C2"
    )
  ),
  [
    {
      row1: 4,
      col1: 0,
      row2: 4,
      col2: 0,
      referenceIndex: 0
    },
    {
      row1: 5,
      col1: 0,
      row2: 5,
      col2: 0,
      referenceIndex: 1
    },
    {
      row1: 1,
      col1: 2,
      row2: 1,
      col2: 2,
      referenceIndex: 2
    }
  ]
);

assert.deepStrictEqual(
  compact(
    references.parseFormulaReferences(
      "=SUM($B$3:$A$1)+C2"
    )
  ),
  [
    {
      row1: 0,
      col1: 0,
      row2: 2,
      col2: 1,
      referenceIndex: 0
    },
    {
      row1: 1,
      col1: 2,
      row2: 1,
      col2: 2,
      referenceIndex: 1
    }
  ]
);

assert.deepStrictEqual(
  compact(
    references.parseFormulaReferences(
      '="A1"&B2&"C3"'
    )
  ),
  [
    {
      row1: 1,
      col1: 1,
      row2: 1,
      col2: 1,
      referenceIndex: 0
    }
  ]
);

assert.deepStrictEqual(
  compact(
    references.parseFormulaReferences(
      "=LOG10(A1)+B2"
    )
  ),
  [
    {
      row1: 0,
      col1: 0,
      row2: 0,
      col2: 0,
      referenceIndex: 0
    },
    {
      row1: 1,
      col1: 1,
      row2: 1,
      col2: 1,
      referenceIndex: 1
    }
  ]
);

assert.deepStrictEqual(
  compact(
    references.parseFormulaReferences(
      "=Sheet2!A1+B2"
    )
  ),
  [
    {
      row1: 1,
      col1: 1,
      row2: 1,
      col2: 1,
      referenceIndex: 0
    }
  ]
);

assert.deepStrictEqual(
  compact(
    references.parseFormulaReferences(
      "=ABC12!B2+C3"
    )
  ),
  [
    {
      row1: 2,
      col1: 2,
      row2: 2,
      col2: 2,
      referenceIndex: 0
    }
  ]
);

assert.deepStrictEqual(
  compact(
    references.parseFormulaReferences(
      "=A1+A1+$A$1"
    )
  ),
  [
    {
      row1: 0,
      col1: 0,
      row2: 0,
      col2: 0,
      referenceIndex: 0
    }
  ]
);

assert.deepStrictEqual(
  references.parseFormulaReferences(
    "=A11+B2",
    {
      maxRows: 10,
      maxCols: 10
    }
  ).map((item) => item.text),
  ["B2"]
);

console.log(
  "ok: spreadsheet formula reference tests passed"
);
