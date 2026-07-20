const assert = require("assert");
const path = require("path");

global.window = {
  PQNAS_FILEMGR: {}
};

require(
  path.resolve(
    __dirname,
    "../../apps/bundled/filemgr/src/www/" +
      "spreadsheet_cell_context_menu.js"
  )
);

const api =
  global.window.PQNAS_FILEMGR
    .spreadsheetCellContextMenu;

assert.ok(api);

assert.deepStrictEqual(
  api.normalizeCommandGroups([
    [
      {
        id: "cut",
        label: "Cut",
        shortcut: "Ctrl+X"
      },
      {
        id: "copy",
        label: "Copy",
        disabled: true
      }
    ],
    [
      {
        id: "clear",
        label: "Clear contents"
      }
    ]
  ]),
  [
    [
      {
        id: "cut",
        label: "Cut",
        shortcut: "Ctrl+X",
        disabled: false
      },
      {
        id: "copy",
        label: "Copy",
        shortcut: "",
        disabled: true
      }
    ],
    [
      {
        id: "clear",
        label: "Clear contents",
        shortcut: "",
        disabled: false
      }
    ]
  ]
);

assert.deepStrictEqual(
  api.normalizeCommandGroups([
    [
      {
        id: "<script>",
        label: "Invalid"
      }
    ]
  ]),
  []
);

console.log(
  "ok: spreadsheet cell context-menu tests passed"
);
