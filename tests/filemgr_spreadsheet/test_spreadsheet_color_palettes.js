const assert = require("assert");
const path = require("path");

global.window = {
  PQNAS_FILEMGR: {},
  localStorage: {
    getItem() {
      return null;
    },
    setItem() {}
  }
};

require(
  path.resolve(
    __dirname,
    "../../apps/bundled/filemgr/src/www/" +
      "spreadsheet_color_palettes.js"
  )
);

const api =
  global.window.PQNAS_FILEMGR
    .spreadsheetColorPalettes;

assert.ok(api);

assert.deepStrictEqual(
  api.paletteIds(),
  [
    "standard",
    "pastel",
    "material",
    "grayscale",
    "libreoffice"
  ]
);

assert.strictEqual(
  api.normalizeArgb("#112233"),
  "FF112233"
);

assert.strictEqual(
  api.normalizeArgb("80112233"),
  "FF112233"
);

assert.strictEqual(
  api.normalizeArgb("not-a-color"),
  ""
);

assert.strictEqual(
  api.argbToCss("FF112233"),
  "#112233"
);

assert.ok(
  api.paletteColors("standard").length >= 70
);

assert.ok(
  api.paletteColors("pastel").length >= 40
);

assert.ok(
  api.paletteColors("material").length >= 40
);

assert.ok(
  api.paletteColors("grayscale").length >= 30
);

assert.ok(
  api.paletteColors("libreoffice").length >= 70
);

const colors =
  api.createColorMap({
    legacy: {
      css: "rgb(1, 2, 3)",
      rgb: "FF010203"
    }
  });

assert.strictEqual(
  colors.legacy.rgb,
  "FF010203"
);

assert.strictEqual(
  colors.FF112233.rgb,
  "FF112233"
);

assert.strictEqual(
  colors["112233"].css,
  "#112233"
);

assert.strictEqual(
  colors.invalid,
  undefined
);

console.log(
  "ok: spreadsheet color palette tests passed"
);
