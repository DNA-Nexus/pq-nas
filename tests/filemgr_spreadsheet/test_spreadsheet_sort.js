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


{
  const input = [
    [
      "fixed-0",
      "Name",
      "Score",
      "tail-0"
    ],
    [
      "fixed-1",
      "Charlie",
      "30",
      "tail-1"
    ],
    [
      "fixed-2",
      "Alice",
      "10",
      "tail-2"
    ],
    [
      "fixed-3",
      "Bob",
      "20",
      "tail-3"
    ]
  ];

  const snapshot =
    JSON.parse(
      JSON.stringify(input)
    );

  const result = api.sortRange(
    input,
    {
      row1: 0,
      row2: 3,
      col1: 1,
      col2: 2
    },
    {
      keyCol: 1,
      direction: "asc",
      header: "yes"
    }
  );

  assert.equal(
    result.ok,
    true
  );

  assert.deepEqual(
    result.order,
    [
      0,
      2,
      3,
      1
    ]
  );

  /*
   * Only the selected rectangle moves. Cells outside
   * that rectangle remain attached to their original
   * worksheet rows.
   */
  assert.deepEqual(
    result.rows,
    [
      [
        "fixed-0",
        "Name",
        "Score",
        "tail-0"
      ],
      [
        "fixed-1",
        "Alice",
        "10",
        "tail-1"
      ],
      [
        "fixed-2",
        "Bob",
        "20",
        "tail-2"
      ],
      [
        "fixed-3",
        "Charlie",
        "30",
        "tail-3"
      ]
    ]
  );

  assert.deepEqual(
    input,
    snapshot
  );
}

{
  const result = api.sortRange(
    [
      ["outside-a", "x", "2"],
      ["outside-b", "y", ""],
      ["outside-c", "z", "10"],
      ["outside-d", "w", "1"]
    ],
    {
      row1: 1,
      row2: 3,
      col1: 1,
      col2: 2
    },
    {
      keyCol: 2,
      direction: "desc",
      header: "no"
    }
  );

  assert.equal(
    result.ok,
    true
  );

  assert.deepEqual(
    result.order,
    [
      2,
      3,
      1
    ]
  );

  assert.deepEqual(
    result.rows.map(
      (row) => row[2]
    ),
    [
      "2",
      "10",
      "1",
      ""
    ]
  );

  assert.deepEqual(
    result.rows.map(
      (row) => row[0]
    ),
    [
      "outside-a",
      "outside-b",
      "outside-c",
      "outside-d"
    ]
  );
}

{
  const input = [
    ["A", "B"],
    ["C", "D"]
  ];

  const result = api.sortRange(
    input,
    {
      row1: 0,
      row2: 99,
      col1: 0,
      col2: 1
    },
    {
      keyCol: 0
    }
  );

  assert.equal(
    result.ok,
    false
  );

  assert.equal(
    result.error,
    "invalid_range"
  );

  assert.deepEqual(
    result.rows,
    input
  );
}

{
  const result = api.sortRange(
    [
      ["A", "B"],
      ["C", "D"]
    ],
    {
      row1: 0,
      row2: 1,
      col1: 1,
      col2: 1
    },
    {
      keyCol: 0
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


{
  const numberCases = [
    ["100.00 €", 100],
    ["€ 100.00", 100],
    ["$100.00", 100],
    ["£ 1200,50", 1200.5],
    ["76000 kr", 76000],
    ["kr 95000", 95000],
    ["¥40000", 40000],
    ["-100.00 €", -100],
    ["€ -100.00", -100]
  ];

  for (
    const [input, expected]
    of numberCases
  ) {
    assert.equal(
      api.parseNumberValue(input),
      expected,
      `currency number parsing failed for ${input}`
    );
  }

  const rejectedValues = [
    "100 euroa",
    "EUR 100",
    "100 USD",
    "€100$",
    "kr100€",
    "100 € extra"
  ];

  for (const input of rejectedValues) {
    assert.equal(
      api.parseNumberValue(input),
      null,
      `unexpected numeric parsing for ${input}`
    );
  }
}

{
  const result = api.sortRows(
    [
      ["KPL", "NIMI", "NET WORTH"],
      ["22", "Timo", "100000.00 €"],
      ["14", "Leo", "50000.00 €"],
      ["122", "Ying", "40000.00 €"],
      ["6", "Saku", "95000.00 €"],
      ["100", "Niina", "76000.00 €"],
      ["99", "Fanny", "120000.00 €"],
      ["1", "Ellen", "16000.00 €"],
      ["2", "Päivi", "55000.00 €"],
      ["24", "Tuomas", "1200.00 €"],
      ["212", "Pauliina", "100.00 €"]
    ],
    {
      keyCol: 2,
      direction: "desc",
      header: "yes"
    }
  );

  assert.equal(
    result.ok,
    true
  );

  assert.equal(
    result.type,
    "number"
  );

  assert.deepEqual(
    result.rows
      .slice(1)
      .map((row) => row[1]),
    [
      "Fanny",
      "Timo",
      "Saku",
      "Niina",
      "Päivi",
      "Leo",
      "Ying",
      "Ellen",
      "Tuomas",
      "Pauliina"
    ]
  );

  assert.deepEqual(
    result.rows
      .slice(1)
      .map((row) => row[2]),
    [
      "120000.00 €",
      "100000.00 €",
      "95000.00 €",
      "76000.00 €",
      "55000.00 €",
      "50000.00 €",
      "40000.00 €",
      "16000.00 €",
      "1200.00 €",
      "100.00 €"
    ]
  );
}

console.log(
  "ok: spreadsheet sort regression tests passed"
);
