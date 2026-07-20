"use strict";

const assert = require("assert");
const comments = require(
  "../../apps/bundled/filemgr/src/www/" +
  "spreadsheet_comments.js"
);

assert.strictEqual(
  comments.shortFingerprint(
    "1234567890abcdef1234567890abcdef"
  ),
  "12345678…abcdef"
);

assert.strictEqual(
  comments.normalizeAuthor(
    "  Timo\u0000   Erkvaara  "
  ),
  "Timo Erkvaara"
);

assert.strictEqual(
  comments.normalizeCommentText(
    "line 1\r\nline 2\u0000"
  ),
  "line 1\nline 2"
);

assert.deepStrictEqual(
  comments.commentsFromWorksheet({
    B2: {
      c: [{
        a: "Windows User",
        t: "Windows User:\nTest comment"
      }]
    }
  }),
  {
    B2: {
      author: "Windows User",
      text: "Test comment"
    }
  }
);

const sheet = { comments: {} };

assert.strictEqual(
  comments.setComment(
    sheet,
    1,
    1,
    { author: "Timo", text: "Comment" }
  ),
  true
);

assert.deepStrictEqual(
  comments.getComment(sheet, 1, 1),
  { author: "Timo", text: "Comment" }
);

assert.strictEqual(
  comments.adjustAxis(
    sheet,
    "row",
    1,
    2,
    "insert"
  ),
  true
);

assert.deepStrictEqual(
  comments.getComment(sheet, 3, 1),
  { author: "Timo", text: "Comment" }
);

assert.strictEqual(
  comments.adjustAxis(
    sheet,
    "column",
    1,
    1,
    "delete"
  ),
  true
);

assert.strictEqual(
  comments.getComment(sheet, 3, 1),
  null
);

const boundsSheet = {
  comments: {
    D5: { author: "Bounds", text: "Here" }
  }
};

assert.deepStrictEqual(
  comments.commentBounds(boundsSheet),
  { rows: 5, cols: 4 }
);

const wholeSortSheet = {
  comments: {
    A2: { author: "A", text: "row 2" },
    A3: { author: "B", text: "row 3" }
  }
};

assert.strictEqual(
  comments.reorderRows(
    wholeSortSheet,
    0,
    [0, 2, 1]
  ),
  true
);

assert.strictEqual(
  comments.getComment(wholeSortSheet, 1, 0).text,
  "row 3"
);

const rangeSortSheet = {
  comments: {
    A2: { author: "A", text: "inside row 2" },
    A3: { author: "B", text: "inside row 3" },
    C2: { author: "C", text: "outside range" }
  }
};

assert.strictEqual(
  comments.reorderRange(
    rangeSortSheet,
    { row1: 1, row2: 2, col1: 0, col2: 1 },
    [2, 1]
  ),
  true
);

assert.strictEqual(
  comments.getComment(rangeSortSheet, 1, 0).text,
  "inside row 3"
);

assert.strictEqual(
  comments.getComment(rangeSortSheet, 1, 2).text,
  "outside range"
);

comments.setComment(
  sheet,
  0,
  0,
  { author: "Alice", text: "Hello" }
);

comments.setComment(
  sheet,
  2,
  1,
  { author: "Bob", text: "World" }
);

const exportInfo = comments.prepareExport([sheet]);
assert.ok(exportInfo);
assert.strictEqual(exportInfo.count, 2);

const commentsXml = comments.commentsXmlForSheet(
  exportInfo.sheets[0]
);

assert.match(commentsXml, /<author>Alice<\/author>/);
assert.match(commentsXml, /<comment ref="A1" authorId="0">/);
assert.match(commentsXml, />Hello<\/t>/);

const entries = [{
  name: "xl/worksheets/_rels/sheet1.xml.rels",
  data:
    '<?xml version="1.0"?>' +
    '<Relationships xmlns="' +
    'http://schemas.openxmlformats.org/package/2006/relationships' +
    '">' +
    '<Relationship Id="rIdPqnasDrawing1"' +
    ' Type="drawing"' +
    ' Target="../drawings/drawing1.xml"/>' +
    '</Relationships>'
}];

comments.appendExportEntries(entries, exportInfo);

const rel = entries.find((entry) => (
  entry.name ===
  "xl/worksheets/_rels/sheet1.xml.rels"
));

assert.match(rel.data, /rIdPqnasDrawing1/);
assert.match(rel.data, /rIdPqnasComments1/);
assert.match(rel.data, /rIdPqnasVmlComments1/);

assert.ok(entries.some((entry) => (
  entry.name === "xl/comments1.xml"
)));

assert.ok(entries.some((entry) => (
  entry.name ===
  "xl/drawings/vmlDrawing1.vml"
)));

console.log("spreadsheet comments tests: OK");
