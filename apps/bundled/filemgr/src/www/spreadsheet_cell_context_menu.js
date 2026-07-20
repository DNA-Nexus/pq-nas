window.PQNAS_FILEMGR =
  window.PQNAS_FILEMGR || {};

(() => {
  "use strict";

  const FM = window.PQNAS_FILEMGR;

  /*
   * Security: command IDs are internal identifiers only.
   * Workbook text is never accepted as a command name.
   */
  function normalizeCommandGroups(groups) {
    if (!Array.isArray(groups)) {
      return [];
    }

    return groups
      .map((group) => {
        if (!Array.isArray(group)) {
          return [];
        }

        return group
          .map((item) => {
            const source =
              item &&
              typeof item === "object"
                ? item
                : {};

            const id =
              String(source.id || "");

            if (
              !/^[a-z][a-z0-9_-]{0,63}$/
                .test(id)
            ) {
              return null;
            }

            return {
              id,
              label:
                String(source.label || ""),
              shortcut:
                String(source.shortcut || ""),
              disabled:
                !!source.disabled
            };
          })
          .filter(Boolean);
      })
      .filter((group) => group.length);
  }

  function appendSeparator(host) {
    if (!host) return null;

    const separator =
      document.createElement("div");

    separator.className =
      "spreadsheetCellContextSeparator";

    separator.setAttribute(
      "role",
      "separator"
    );

    host.appendChild(separator);
    return separator;
  }

  function createCommandButton(
    item,
    onCommand
  ) {
    const button =
      document.createElement("button");

    button.type = "button";
    button.className =
      "spreadsheetCellContextCommand";

    button.disabled =
      !!item.disabled;

    button.dataset.command =
      item.id;

    button.setAttribute(
      "role",
      "menuitem"
    );

    const label =
      document.createElement("span");

    label.className =
      "spreadsheetCellContextLabel";

    /*
     * Security: translated labels are written as text,
     * never interpreted as HTML.
     */
    label.textContent =
      item.label;

    button.appendChild(label);

    if (item.shortcut) {
      const shortcut =
        document.createElement("span");

      shortcut.className =
        "spreadsheetCellContextShortcut";

      shortcut.textContent =
        item.shortcut;

      button.appendChild(shortcut);
    }

    button.addEventListener(
      "click",
      (event) => {
        event.preventDefault();
        event.stopPropagation();

        if (
          button.disabled ||
          typeof onCommand !== "function"
        ) {
          return;
        }

        onCommand(item.id);
      }
    );

    return button;
  }

  function appendCommandGroups(
    host,
    groups,
    options = {}
  ) {
    if (!host) return [];

    const normalized =
      normalizeCommandGroups(groups);

    const buttons = [];

    normalized.forEach(
      (group, groupIndex) => {
        if (groupIndex > 0) {
          appendSeparator(host);
        }

        for (const item of group) {
          const button =
            createCommandButton(
              item,
              options.onCommand
            );

          buttons.push(button);
          host.appendChild(button);
        }
      }
    );

    return buttons;
  }

  FM.spreadsheetCellContextMenu =
    Object.freeze({
      normalizeCommandGroups,
      appendSeparator,
      appendCommandGroups
    });
})();
