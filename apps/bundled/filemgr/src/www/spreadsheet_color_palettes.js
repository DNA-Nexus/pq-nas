window.PQNAS_FILEMGR =
  window.PQNAS_FILEMGR || {};

(() => {
  "use strict";

  const FM = window.PQNAS_FILEMGR;

  const DEFAULT_PALETTE_ID =
    "standard";

  const PALETTE_ORDER =
    Object.freeze([
      "standard",
      "pastel",
      "material",
      "grayscale",
      "libreoffice"
    ]);

  /*
   * Security: accept only opaque RGB/ARGB hexadecimal
   * color values. Arbitrary CSS supplied by workbook
   * content or local storage is never applied.
   */
  function normalizeArgb(value) {
    const raw =
      String(value || "")
        .trim()
        .replace(/^#/, "")
        .toUpperCase();

    if (/^[0-9A-F]{6}$/.test(raw)) {
      return `FF${raw}`;
    }

    if (/^[0-9A-F]{8}$/.test(raw)) {
      return `FF${raw.slice(-6)}`;
    }

    return "";
  }

  function argbToCss(value) {
    const argb = normalizeArgb(value);

    return argb
      ? `#${argb.slice(2)}`
      : "";
  }

  function freezePalette(colors) {
    return Object.freeze(
      colors
        .map(normalizeArgb)
        .filter(Boolean)
    );
  }

  const PALETTES = Object.freeze({
    standard: freezePalette([
      "FFFFFF", "E7E6E6", "D0CECE", "AEAAAA", "7F7F7F",
      "595959", "3F3F3F", "262626", "0D0D0D", "000000",

      "FFFF00", "FFC000", "FF0000", "FF00FF", "7030A0",
      "002060", "0070C0", "00B0F0", "00B050", "92D050",

      "FFF2CC", "FCE4D6", "F4CCCC", "EAD1DC", "D9D2E9",
      "CFE2F3", "D0E0E3", "D9EAD3", "E2F0D9", "FFF2F2",

      "FFE699", "F8CBAD", "EA9999", "D5A6BD", "B4A7D6",
      "9FC5E8", "A2C4C9", "B6D7A8", "C6E0B4", "F4CCCC",

      "FFD966", "F4B183", "E06666", "C27BA0", "8E7CC3",
      "6FA8DC", "76A5AF", "93C47D", "A9D18E", "EA9999",

      "FFC000", "ED7D31", "C00000", "A64D79", "674EA7",
      "3D85C6", "45818E", "6AA84F", "70AD47", "E06666",

      "BF9000", "C65911", "990000", "741B47", "351C75",
      "0B5394", "134F5C", "38761D", "548235", "CC0000",

      "7F6000", "783F04", "660000", "4C1130", "20124D",
      "073763", "0C343D", "274E13", "375623", "990000"
    ]),

    pastel: freezePalette([
      "FFF4E6", "FFE8D6", "FFD6D6", "FFE1E8", "F3E5F5",
      "E8EAF6", "E3F2FD", "E0F7FA", "E0F2F1", "E8F5E9",

      "FFF9C4", "FFECB3", "FFCCBC", "F8BBD0", "E1BEE7",
      "C5CAE9", "BBDEFB", "B2EBF2", "B2DFDB", "C8E6C9",

      "FFF59D", "FFE082", "FFAB91", "F48FB1", "CE93D8",
      "9FA8DA", "90CAF9", "80DEEA", "80CBC4", "A5D6A7",

      "FFF176", "FFD54F", "FF8A65", "F06292", "BA68C8",
      "7986CB", "64B5F6", "4DD0E1", "4DB6AC", "81C784",

      "E6EE9C", "DCE775", "D4E157", "AED581", "9CCC65",
      "8BC34A", "B3E5FC", "B2EBF2", "D1C4E9", "FFCDD2"
    ]),

    material: freezePalette([
      "FFEBEE", "FCE4EC", "F3E5F5", "EDE7F6", "E8EAF6",
      "E3F2FD", "E1F5FE", "E0F7FA", "E0F2F1", "E8F5E9",

      "FFCDD2", "F8BBD0", "E1BEE7", "D1C4E9", "C5CAE9",
      "BBDEFB", "B3E5FC", "B2EBF2", "B2DFDB", "C8E6C9",

      "EF9A9A", "F48FB1", "CE93D8", "B39DDB", "9FA8DA",
      "90CAF9", "81D4FA", "80DEEA", "80CBC4", "A5D6A7",

      "EF5350", "EC407A", "AB47BC", "7E57C2", "5C6BC0",
      "42A5F5", "29B6F6", "26C6DA", "26A69A", "66BB6A",

      "C62828", "AD1457", "6A1B9A", "4527A0", "283593",
      "1565C0", "0277BD", "00838F", "00695C", "2E7D32"
    ]),

    grayscale: freezePalette([
      "FFFFFF", "F7F7F7", "EEEEEE", "E5E5E5", "DDDDDD",
      "D4D4D4", "CCCCCC", "C3C3C3", "BBBBBB", "B2B2B2",

      "AAAAAA", "A1A1A1", "999999", "909090", "888888",
      "7F7F7F", "777777", "6E6E6E", "666666", "5D5D5D",

      "555555", "4C4C4C", "444444", "3B3B3B", "333333",
      "2A2A2A", "222222", "191919", "111111", "000000"
    ]),

    libreoffice: freezePalette([
      "FFFFFF", "000000", "333333", "666666", "999999",
      "B3B3B3", "CCCCCC", "E6E6E6", "F2F2F2", "FAFAFA",

      "FFFF00", "FFCC00", "FF6600", "FF0000", "FF0066",
      "CC0099", "660099", "333399", "0066CC", "00A6D6",

      "CCFF00", "FFFF66", "FFCC66", "FF9966", "FF6666",
      "FF6699", "CC66CC", "9966CC", "6699CC", "66CCCC",

      "99CC00", "CCCC00", "CC9900", "CC6600", "CC3333",
      "CC3366", "993399", "663399", "336699", "339999",

      "669900", "999900", "996600", "993300", "990000",
      "990033", "660066", "330066", "003366", "006666",

      "336600", "666600", "663300", "661A00", "660000",
      "660033", "330033", "1A0033", "001A33", "003333",

      "00FF00", "66FF66", "33CC66", "00CC99", "00CCFF",
      "0099FF", "0066FF", "6633FF", "9933FF", "CC33FF",

      "00B050", "70AD47", "548235", "38761D", "274E13",
      "00B0F0", "5B9BD5", "4472C4", "2F5597", "203864"
    ])
  });

  function paletteIds() {
    return [...PALETTE_ORDER];
  }

  function paletteColors(id) {
    const key =
      Object.prototype.hasOwnProperty.call(
        PALETTES,
        String(id || "")
      )
        ? String(id)
        : DEFAULT_PALETTE_ID;

    return PALETTES[key];
  }

  function createColorMap(legacyColors) {
    const source =
      legacyColors &&
      typeof legacyColors === "object"
        ? legacyColors
        : {};

    const cache = new Map();

    return new Proxy(source, {
      get(target, property, receiver) {
        if (
          typeof property !== "string" ||
          Reflect.has(target, property)
        ) {
          return Reflect.get(
            target,
            property,
            receiver
          );
        }

        const argb =
          normalizeArgb(property);

        if (!argb) {
          return undefined;
        }

        if (!cache.has(argb)) {
          cache.set(
            argb,
            Object.freeze({
              css: argbToCss(argb),
              rgb: argb
            })
          );
        }

        return cache.get(argb);
      }
    });
  }

  function readStorage(key, fallback) {
    try {
      const value =
        window.localStorage.getItem(key);

      return value == null
        ? fallback
        : value;
    } catch (_) {
      return fallback;
    }
  }

  function writeStorage(key, value) {
    try {
      window.localStorage.setItem(
        key,
        value
      );
    } catch (_) {
      // Storage may be disabled; the picker still works.
    }
  }

  function readRecentColors(key) {
    try {
      const parsed =
        JSON.parse(
          readStorage(key, "[]")
        );

      if (!Array.isArray(parsed)) {
        return [];
      }

      return [
        ...new Set(
          parsed
            .map(normalizeArgb)
            .filter(Boolean)
        )
      ].slice(0, 10);
    } catch (_) {
      return [];
    }
  }

  function rememberRecentColor(
    key,
    value
  ) {
    const color =
      normalizeArgb(value);

    if (!color) return;

    const colors = [
      color,
      ...readRecentColors(key)
        .filter((item) => item !== color)
    ].slice(0, 10);

    writeStorage(
      key,
      JSON.stringify(colors)
    );
  }

  function makeSwatchButton(
    color,
    selectedColor,
    onSelect
  ) {
    const argb =
      normalizeArgb(color);

    const button =
      document.createElement("button");

    button.type = "button";
    button.className =
      "spreadsheetColorSwatchButton";

    button.dataset.color = argb;
    button.setAttribute(
      "aria-label",
      `#${argb.slice(2)}`
    );

    button.setAttribute(
      "aria-pressed",
      argb === selectedColor
        ? "true"
        : "false"
    );

    button.title =
      `#${argb.slice(2)}`;

    button.style.setProperty(
      "--spreadsheet-document-color",
      argbToCss(argb)
    );

    button.addEventListener(
      "click",
      (event) => {
        event.preventDefault();
        event.stopPropagation();
        onSelect(argb);
      }
    );

    return button;
  }

  function renderMenu(menu, options = {}) {
    if (!menu) return;

    const paletteStorageKey =
      String(
        options.paletteStorageKey ||
        "pqnas.spreadsheet.color.palette"
      );

    const recentStorageKey =
      String(
        options.recentStorageKey ||
        "pqnas.spreadsheet.color.recent"
      );

    let paletteId =
      readStorage(
        paletteStorageKey,
        DEFAULT_PALETTE_ID
      );

    if (!PALETTE_ORDER.includes(paletteId)) {
      paletteId = DEFAULT_PALETTE_ID;
    }

    const selectedColor =
      normalizeArgb(
        options.selectedColor
      );

    const chooseColor = (value) => {
      const argb =
        normalizeArgb(value);

      if (!argb) return;

      rememberRecentColor(
        recentStorageKey,
        argb
      );

      if (
        typeof options.onSelect ===
        "function"
      ) {
        options.onSelect(argb);
      }
    };

    menu.replaceChildren();
    menu.classList.add(
      "spreadsheetColorPaletteMenu"
    );

    menu.setAttribute(
      "aria-label",
      String(
        options.titleLabel || ""
      )
    );

    const title =
      document.createElement("div");

    title.className =
      "spreadsheetColorPaletteTitle";

    title.textContent =
      String(
        options.titleLabel || ""
      );

    const clearButton =
      document.createElement("button");

    clearButton.type = "button";
    clearButton.className =
      "spreadsheetColorPaletteAction";

    clearButton.textContent =
      String(
        options.clearLabel || ""
      );

    clearButton.addEventListener(
      "click",
      (event) => {
        event.preventDefault();
        event.stopPropagation();

        if (
          typeof options.onSelect ===
          "function"
        ) {
          options.onSelect("");
        }
      }
    );

    const paletteSelect =
      document.createElement("select");

    paletteSelect.className =
      "spreadsheetColorPaletteSelect";

    paletteSelect.setAttribute(
      "aria-label",
      String(
        options.paletteTitle ||
        "Color palette"
      )
    );

    for (const id of PALETTE_ORDER) {
      const option =
        document.createElement("option");

      option.value = id;
      option.textContent =
        typeof options.paletteLabel ===
        "function"
          ? String(
              options.paletteLabel(id)
            )
          : id;

      option.selected =
        id === paletteId;

      paletteSelect.appendChild(
        option
      );
    }

    const grid =
      document.createElement("div");

    grid.className =
      "spreadsheetColorGrid";

    grid.setAttribute(
      "role",
      "group"
    );

    const renderGrid = () => {
      grid.replaceChildren();

      for (
        const color of
        paletteColors(paletteId)
      ) {
        grid.appendChild(
          makeSwatchButton(
            color,
            selectedColor,
            chooseColor
          )
        );
      }
    };

    paletteSelect.addEventListener(
      "change",
      () => {
        paletteId =
          PALETTE_ORDER.includes(
            paletteSelect.value
          )
            ? paletteSelect.value
            : DEFAULT_PALETTE_ID;

        writeStorage(
          paletteStorageKey,
          paletteId
        );

        renderGrid();
      }
    );

    renderGrid();

    menu.appendChild(title);
    menu.appendChild(clearButton);
    menu.appendChild(paletteSelect);
    menu.appendChild(grid);

    const recent =
      readRecentColors(
        recentStorageKey
      );

    if (recent.length) {
      const recentTitle =
        document.createElement("div");

      recentTitle.className =
        "spreadsheetColorSectionTitle";

      recentTitle.textContent =
        String(
          options.recentLabel ||
          "Recent"
        );

      const recentGrid =
        document.createElement("div");

      recentGrid.className =
        "spreadsheetColorGrid " +
        "spreadsheetColorRecentGrid";

      recentGrid.setAttribute(
        "role",
        "group"
      );

      for (const color of recent) {
        recentGrid.appendChild(
          makeSwatchButton(
            color,
            selectedColor,
            chooseColor
          )
        );
      }

      menu.appendChild(recentTitle);
      menu.appendChild(recentGrid);
    }

    const customLabel =
      String(
        options.customLabel ||
        "Custom color…"
      );

    /*
     * UX correctness: keep the native color input physically
     * over the visible control. A display:none input opened
     * through click() has no screen anchor, so Chromium may
     * place its native picker at the far edge of the window.
     */
    const customControl =
      document.createElement("label");

    customControl.className =
      "spreadsheetColorCustomControl";

    const customText =
      document.createElement("span");

    customText.textContent =
      customLabel;

    const customInput =
      document.createElement("input");

    customInput.type = "color";
    customInput.className =
      "spreadsheetColorCustomInput";

    customInput.value =
      selectedColor
        ? argbToCss(selectedColor)
        : "#000000";

    customInput.setAttribute(
      "aria-label",
      customLabel
    );

    customInput.title =
      customLabel;

    customInput.addEventListener(
      "change",
      () => {
        chooseColor(
          customInput.value
        );
      }
    );

    customControl.appendChild(
      customText
    );

    customControl.appendChild(
      customInput
    );

    menu.appendChild(
      customControl
    );
  }

  FM.spreadsheetColorPalettes =
    Object.freeze({
      normalizeArgb,
      argbToCss,
      createColorMap,
      paletteIds,
      paletteColors,
      renderMenu
    });
})();
