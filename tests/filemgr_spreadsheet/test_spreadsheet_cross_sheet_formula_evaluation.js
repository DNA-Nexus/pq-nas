#!/usr/bin/env node
"use strict";

const assert = require("assert");
const fs = require("fs");
const path = require("path");

const repoRoot = path.resolve(__dirname, "../..");
const editorPath = path.join(
  repoRoot,
  "apps/bundled/filemgr/src/www/spreadsheet_edit.js"
);

const source = fs.readFileSync(editorPath, "utf8");
const startMarker = "  function colLettersToIndex(letters) {";
const endMarker = "  function spreadsheetCommentsApi() {";

const start = source.indexOf(startMarker);
const end = source.indexOf(endMarker, start);

assert.notStrictEqual(
  start,
  -1,
  "formula engine start anchor missing"
);

assert.notStrictEqual(
  end,
  -1,
  "formula engine end anchor missing"
);

const engineSource = source.slice(start, end);

const state = {
  sheets: [
    {
      name: "Sheet1",
      rows: [
        [
          "1",
          "=Sheet2!B2",
          "=SUM(Sheet2!B2:B4)",
          "='Myynti 2026'!B2",
          "='Bob''s Sales'!A1",
          "=sheet2!B2+1",
          "=[Book.xlsx]Sheet2!B2",
          "=Missing!A1",
          "=A1+2",
          "=SUM(Sheet2!B2:Sheet2!B4)"
        ],
        [
          "=Sheet2!A2"
        ]
      ]
    },
    {
      name: "Sheet2",
      rows: [
        ["", "5"],
        ["=Sheet1!A2", "10"],
        ["", "20"],
        ["", "30"]
      ]
    },
    {
      name: "Myynti 2026",
      rows: [
        ["", ""],
        ["", "7"]
      ]
    },
    {
      name: "Bob's Sales",
      rows: [
        ["9"]
      ]
    }
  ]
};

function columnName(index) {
  let n = Number(index) + 1;
  let out = "";

  while (n > 0) {
    const remainder = (n - 1) % 26;
    out =
      String.fromCharCode(65 + remainder) +
      out;
    n = Math.floor((n - 1) / 26);
  }

  return out;
}

function cellRaw(sheet, row, col) {
  const rows =
    sheet && Array.isArray(sheet.rows)
      ? sheet.rows
      : [];

  const currentRow =
    Array.isArray(rows[row])
      ? rows[row]
      : [];

  return String(
    currentRow[col] == null
      ? ""
      : currentRow[col]
  );
}

function isForcedTextValue(raw) {
  return String(raw || "").startsWith("'");
}

function forcedTextDisplayValue(raw) {
  return String(raw || "").slice(1);
}

function isFormulaValue(raw) {
  return String(raw || "").startsWith("=");
}

function getCellFormat() {
  return {};
}

function formatSpreadsheetDateDisplayValue() {
  return "";
}

function formatNumericDisplayValue(value) {
  return String(value);
}

const createEngine = new Function(
  "state",
  "MAX_EDIT_ROWS",
  "MAX_EDIT_COLS",
  "columnName",
  "cellRaw",
  "isForcedTextValue",
  "forcedTextDisplayValue",
  "isFormulaValue",
  "getCellFormat",
  "formatSpreadsheetDateDisplayValue",
  "formatNumericDisplayValue",
  [
    '"use strict";',
    engineSource,
    "return {",
    "  evaluateCell,",
    "  computeSheetCache,",
    "  displayCellValue",
    "};"
  ].join("\n")
);

const engine = createEngine(
  state,
  1000,
  1000,
  columnName,
  cellRaw,
  isForcedTextValue,
  forcedTextDisplayValue,
  isFormulaValue,
  getCellFormat,
  formatSpreadsheetDateDisplayValue,
  formatNumericDisplayValue
);

const sheet1 = state.sheets[0];
const cache = engine.computeSheetCache(sheet1);

function display(row, col) {
  return engine.displayCellValue(
    sheet1,
    row,
    col,
    cache
  );
}

assert.strictEqual(
  display(0, 1),
  "10",
  "unquoted cross-sheet cell reference"
);

assert.strictEqual(
  display(0, 2),
  "60",
  "cross-sheet range in SUM"
);

assert.strictEqual(
  display(0, 3),
  "7",
  "quoted sheet name"
);

assert.strictEqual(
  display(0, 4),
  "9",
  "escaped apostrophe in quoted sheet name"
);

assert.strictEqual(
  display(0, 5),
  "11",
  "case-insensitive sheet lookup"
);

assert.strictEqual(
  display(0, 6),
  "#REF!",
  "external workbook references stay unsupported"
);

assert.strictEqual(
  display(0, 7),
  "#REF!",
  "missing sheet returns REF"
);

assert.strictEqual(
  display(0, 8),
  "3",
  "same-sheet formulas still work"
);

assert.strictEqual(
  display(0, 9),
  "60",
  "explicit sheet qualifier on both range endpoints"
);

assert.strictEqual(
  display(1, 0),
  "#CYCLE!",
  "cross-sheet cycle detection"
);

console.log(
  "ok: spreadsheet cross-sheet formula evaluation tests passed"
);
