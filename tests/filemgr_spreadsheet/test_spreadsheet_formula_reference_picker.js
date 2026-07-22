"use strict";

const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");

const sourcePath = path.resolve(
  __dirname,
  "../../apps/bundled/filemgr/src/www/" +
    "spreadsheet_formula_reference_picker.js"
);

const source = fs.readFileSync(
  sourcePath,
  "utf8"
);

const sandbox = {
  globalThis: {}
};

sandbox.globalThis = sandbox;

vm.runInNewContext(
  source,
  sandbox,
  {
    filename: sourcePath
  }
);

const picker =
  sandbox.PQNAS_FILEMGR &&
  sandbox.PQNAS_FILEMGR
    .spreadsheetFormulaReferencePicker;

assert.ok(
  picker,
  "formula reference picker API missing"
);

assert.deepStrictEqual(
  JSON.parse(JSON.stringify(
    picker.normalizeRange(
      9,
      3,
      1,
      1,
      {
        maxRows: 100,
        maxCols: 20
      }
    )
  )),
  {
    row1: 1,
    col1: 1,
    row2: 9,
    col2: 3
  }
);

assert.strictEqual(
  picker.normalizeRange(
    -1,
    0,
    1,
    1,
    {
      maxRows: 100,
      maxCols: 20
    }
  ),
  null
);

assert.deepStrictEqual(
  JSON.parse(JSON.stringify(
    picker.replaceSelection(
      "=SUM()",
      5,
      5,
      "Sheet2!B2:B10"
    )
  )),
  {
    value: "=SUM(Sheet2!B2:B10)",
    selectionStart: 20,
    selectionEnd: 20,
    referenceStart: 5,
    referenceEnd: 20
  }
);

const targetInput = {
  dataset: {
    row: "4",
    col: "2"
  }
};

const documentObject = {
  elementFromPoint() {
    return {
      closest(selector) {
        assert.strictEqual(
          selector,
          "input[data-row][data-col]"
        );

        return targetInput;
      }
    };
  }
};

const pointed =
  picker.cellAtPoint(
    documentObject,
    12,
    34
  );

assert.strictEqual(
  pointed.input,
  targetInput
);

assert.strictEqual(pointed.row, 4);
assert.strictEqual(pointed.col, 2);

console.log(
  "ok: spreadsheet formula reference picker tests passed"
);
