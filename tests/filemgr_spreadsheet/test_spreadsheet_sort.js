"use strict";

const assert = require(
  "node:assert/strict"
);

global.window = {
  PQNAS_FILEMGR: {}
};

require(
  "../../apps/bundled/filemgr/src/www/spreadsheet_sort.js"
);

const api =
  window.PQNAS_FILEMGR
    .spreadsheetSort;

assert.ok(
  api &&
  typeof api.sortRows ===
    "function"
);

function column(
  result,
  index = 0
) {
  assert.equal(
    result.ok,
    true,
    result.error ||
      "sort failed"
  );

  return result.rows.map(
    (row) => row[index]
  );
}

{
  const result = api.sortRows(
    [
      ["Charlie", "C"],
      ["Alice", "A"],
      ["Bob", "B"]
    ],
    {
      keyCol: 0,
      direction: "asc",
      header: "no"
    }
  );

  assert.deepEqual(
    result.rows,
    [
      ["Alice", "A"],
      ["Bob", "B"],
      ["Charlie", "C"]
    ]
  );
}

{
  const result = api.sortRows(
    [
      ["2"],
      ["10"],
      ["1"],
      ["1,5"]
    ],
    {
      keyCol: 0,
      direction: "asc",
      header: "no"
    }
  );

  assert.equal(
    result.type,
    "number"
  );

  assert.deepEqual(
    column(result),
    [
      "1",
      "1,5",
      "2",
      "10"
    ]
  );
}

{
  const result = api.sortRows(
    [
      ["Date", "ID"],
      [
        "18.07.2026",
        "later"
      ],
      [
        "2025-01-02",
        "earlier"
      ],
      [
        "03.12.2025",
        "middle"
      ]
    ],
    {
      keyCol: 0,
      direction: "asc",
      header: "auto"
    }
  );

  assert.equal(
    result.hasHeader,
    true
  );

  assert.equal(
    result.type,
    "date"
  );

  assert.deepEqual(
    result.rows.map(
      (row) => row[1]
    ),
    [
      "ID",
      "earlier",
      "middle",
      "later"
    ]
  );
}

{
  const result = api.sortRows(
    [
      ["A"],
      ["Ö"],
      ["Ä"],
      ["Å"],
      ["Z"]
    ],
    {
      keyCol: 0,
      direction: "asc",
      header: "no"
    }
  );

  assert.deepEqual(
    column(result),
    [
      "A",
      "Z",
      "Å",
      "Ä",
      "Ö"
    ]
  );
}

{
  const result = api.sortRows(
    [
      ["Name", "id"],
      ["same", 1],
      ["other", 2],
      ["same", 3]
    ],
    {
      keyCol: 0,
      direction: "asc",
      header: "yes"
    }
  );

  assert.deepEqual(
    result.rows.map(
      (row) => row[1]
    ),
    [
      "id",
      2,
      1,
      3
    ]
  );
}

for (
  const direction of
  ["asc", "desc"]
) {
  const result = api.sortRows(
    [
      ["2"],
      [""],
      ["1"],
      ["   "]
    ],
    {
      keyCol: 0,
      direction,
      header: "no"
    }
  );

  assert.deepEqual(
    column(result).slice(-2),
    [
      "",
      "   "
    ]
  );
}

{
  const input = [
    ["Amount", "id"],
    ["10", "ten"],
    ["2", "two"]
  ];

  const snapshot =
    JSON.parse(
      JSON.stringify(input)
    );

  const result = api.sortRows(
    input,
    {
      keyCol: 0,
      direction: "asc",
      header: "auto"
    }
  );

  assert.equal(
    result.hasHeader,
    true
  );

  assert.deepEqual(
    result.order,
    [
      0,
      2,
      1
    ]
  );

  assert.deepEqual(
    result.rows.map(
      (row) => row[1]
    ),
    [
      "id",
      "two",
      "ten"
    ]
  );

  assert.deepEqual(
    input,
    snapshot
  );
}

{
  const result = api.sortRows(
    [["A"]],
    {
      keyCol: -1
    }
  );

  assert.equal(
    result.ok,
    false
  );

  assert.equal(
    result.error,
    "invalid_key_column"
  );
}

console.log(
  "ok: spreadsheet sort regression tests passed"
);
