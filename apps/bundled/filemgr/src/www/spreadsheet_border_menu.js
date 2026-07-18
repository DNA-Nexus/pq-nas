window.PQNAS_FILEMGR = window.PQNAS_FILEMGR || {};

(() => {
  "use strict";

  const FM = window.PQNAS_FILEMGR;
  const SVG_NS =
    "http://www.w3.org/2000/svg";
  const CLOSE_DELAY_MS = 180;
  const hostStates = new WeakMap();

  function svgElement(name, attrs = {}) {
    const element =
      document.createElementNS(
        SVG_NS,
        name
      );

    for (const [key, value] of Object.entries(attrs)) {
      element.setAttribute(
        key,
        String(value)
      );
    }

    return element;
  }

  function appendSvgLine(
    svg,
    x1,
    y1,
    x2,
    y2,
    className,
    width = 1.8
  ) {
    svg.appendChild(
      svgElement("line", {
        x1,
        y1,
        x2,
        y2,
        class: className,
        "stroke-width": width,
        "vector-effect": "non-scaling-stroke"
      })
    );
  }

  function appendSvgRect(
    svg,
    className,
    width = 1.8
  ) {
    svg.appendChild(
      svgElement("rect", {
        x: 4,
        y: 4,
        width: 16,
        height: 16,
        class: className,
        "stroke-width": width,
        "vector-effect": "non-scaling-stroke"
      })
    );
  }

  function createBorderIcon(action, style = "") {
    const svg = svgElement("svg", {
      viewBox: "0 0 24 24",
      "aria-hidden": "true",
      class: "spreadsheetBorderMenuIcon"
    });

    appendSvgRect(
      svg,
      "spreadsheetBorderMenuIconGuide",
      1
    );

    appendSvgLine(
      svg,
      12,
      4,
      12,
      20,
      "spreadsheetBorderMenuIconGuide",
      1
    );

    appendSvgLine(
      svg,
      4,
      12,
      20,
      12,
      "spreadsheetBorderMenuIconGuide",
      1
    );

    const activeClass =
      "spreadsheetBorderMenuIconActive";

    const strong =
      style === "thick" ||
      style === "medium"
        ? 2.8
        : 1.8;

    if (action === "clear") {
      appendSvgLine(
        svg,
        6,
        6,
        18,
        18,
        activeClass,
        2
      );
      appendSvgLine(
        svg,
        18,
        6,
        6,
        18,
        activeClass,
        2
      );
    } else if (action === "all") {
      appendSvgRect(svg, activeClass, strong);
      appendSvgLine(
        svg,
        12,
        4,
        12,
        20,
        activeClass,
        strong
      );
      appendSvgLine(
        svg,
        4,
        12,
        20,
        12,
        activeClass,
        strong
      );
    } else if (action === "outside") {
      appendSvgRect(svg, activeClass, strong);
    } else if (action === "top") {
      appendSvgLine(
        svg,
        4,
        4,
        20,
        4,
        activeClass,
        strong
      );
    } else if (action === "bottom") {
      if (style === "double") {
        appendSvgLine(
          svg,
          4,
          17.5,
          20,
          17.5,
          activeClass,
          1.7
        );
        appendSvgLine(
          svg,
          4,
          20,
          20,
          20,
          activeClass,
          1.7
        );
      } else {
        appendSvgLine(
          svg,
          4,
          20,
          20,
          20,
          activeClass,
          strong
        );
      }
    } else if (action === "left") {
      appendSvgLine(
        svg,
        4,
        4,
        4,
        20,
        activeClass,
        strong
      );
    } else if (action === "right") {
      appendSvgLine(
        svg,
        20,
        4,
        20,
        20,
        activeClass,
        strong
      );
    }

    return svg;
  }

  function clearCloseTimer(state) {
    if (!state || state.closeTimer == null) return;

    window.clearTimeout(
      state.closeTimer
    );

    state.closeTimer = null;
  }

  function positionSubmenu(state) {
    if (
      !state ||
      !state.trigger ||
      !state.submenu
    ) {
      return;
    }

    const trigger = state.trigger;
    const submenu = state.submenu;

    submenu.classList.remove(
      "spreadsheetBorderSubmenuOpenLeft"
    );

    submenu.style.top =
      `${trigger.offsetTop - 6}px`;

    let rect =
      submenu.getBoundingClientRect();

    if (rect.right > window.innerWidth - 8) {
      submenu.classList.add(
        "spreadsheetBorderSubmenuOpenLeft"
      );
      rect = submenu.getBoundingClientRect();
    }

    let top =
      Number.parseFloat(submenu.style.top) || 0;

    if (rect.bottom > window.innerHeight - 8) {
      top -=
        rect.bottom -
        (window.innerHeight - 8);
    }

    rect = submenu.getBoundingClientRect();

    if (rect.top < 8) {
      top += 8 - rect.top;
    }

    submenu.style.top =
      `${Math.round(top)}px`;
  }

  function openSubmenu(
    host,
    focusFirst = false
  ) {
    const state = hostStates.get(host);
    if (!state) return false;

    clearCloseTimer(state);

    state.submenu.hidden = false;
    state.trigger.setAttribute(
      "aria-expanded",
      "true"
    );

    positionSubmenu(state);

    if (focusFirst) {
      window.requestAnimationFrame(() => {
        const first =
          state.submenu.querySelector(
            'button:not(:disabled)'
          );

        if (first) first.focus();
      });
    }

    return true;
  }

  function closeSubmenu(
    host,
    focusTrigger = false
  ) {
    const state = hostStates.get(host);
    if (!state) return false;

    clearCloseTimer(state);

    state.submenu.hidden = true;
    state.trigger.setAttribute(
      "aria-expanded",
      "false"
    );

    if (focusTrigger) {
      state.trigger.focus();
    }

    return true;
  }

  function scheduleClose(host) {
    const state = hostStates.get(host);
    if (!state) return;

    clearCloseTimer(state);

    state.closeTimer =
      window.setTimeout(
        () => closeSubmenu(host),
        CLOSE_DELAY_MS
      );
  }

  function nextMenuButton(
    submenu,
    current,
    direction
  ) {
    const buttons = Array.from(
      submenu.querySelectorAll(
        'button:not(:disabled)'
      )
    );

    if (!buttons.length) return null;

    const currentIndex =
      buttons.indexOf(current);

    if (direction === "home") {
      return buttons[0];
    }

    if (direction === "end") {
      return buttons[buttons.length - 1];
    }

    const delta =
      direction === "previous"
        ? -1
        : 1;

    const start =
      currentIndex >= 0
        ? currentIndex
        : 0;

    return buttons[
      (start + delta + buttons.length) %
      buttons.length
    ];
  }

  function createActionButton(
    host,
    item,
    options
  ) {
    const button =
      document.createElement("button");

    button.type = "button";
    button.className =
      "spreadsheetBorderMenuRow";
    button.setAttribute(
      "role",
      "menuitem"
    );

    const label =
      document.createElement("span");

    label.textContent =
      String(item.label || "");

    button.appendChild(
      createBorderIcon(
        item.action,
        item.style
      )
    );

    button.appendChild(label);

    button.addEventListener("click", (event) => {
      event.preventDefault();
      event.stopPropagation();

      if (
        typeof options.onClose === "function"
      ) {
        options.onClose();
      } else {
        close(host);
      }

      if (
        typeof options.onAction === "function"
      ) {
        options.onAction(
          item.action,
          item.style
        );
      }
    });

    return button;
  }

  function appendSubmenu(host, options = {}) {
    if (!host) return null;

    close(host);

    const trigger =
      document.createElement("button");

    trigger.type = "button";
    trigger.className =
      "spreadsheetBorderMenuRow spreadsheetBorderSubmenuTrigger";
    trigger.setAttribute(
      "role",
      "menuitem"
    );
    trigger.setAttribute(
      "aria-haspopup",
      "menu"
    );
    trigger.setAttribute(
      "aria-expanded",
      "false"
    );
    trigger.setAttribute(
      "aria-controls",
      "spreadsheetBorderSubmenu"
    );

    const triggerMain =
      document.createElement("span");

    triggerMain.className =
      "spreadsheetBorderSubmenuTriggerMain";

    const triggerLabel =
      document.createElement("span");

    triggerLabel.textContent =
      String(options.label || "Borders");

    triggerMain.appendChild(
      createBorderIcon(
        "outside",
        "thin"
      )
    );

    triggerMain.appendChild(
      triggerLabel
    );

    const chevron =
      document.createElement("span");

    chevron.className =
      "spreadsheetBorderSubmenuChevron";
    chevron.setAttribute(
      "aria-hidden",
      "true"
    );
    chevron.textContent = "›";

    trigger.appendChild(triggerMain);
    trigger.appendChild(chevron);

    const submenu =
      document.createElement("div");

    submenu.id =
      "spreadsheetBorderSubmenu";
    submenu.className =
      "spreadsheetBorderSubmenu";
    submenu.hidden = true;
    submenu.setAttribute(
      "role",
      "menu"
    );
    submenu.setAttribute(
      "aria-label",
      String(options.label || "Borders")
    );

    const items =
      Array.isArray(options.items)
        ? options.items
        : [];

    for (const item of items) {
      if (
        !item ||
        typeof item !== "object"
      ) {
        continue;
      }

      submenu.appendChild(
        createActionButton(
          host,
          item,
          options
        )
      );
    }

    const state = {
      trigger,
      submenu,
      closeTimer: null
    };

    hostStates.set(host, state);

    trigger.addEventListener(
      "pointerenter",
      () => openSubmenu(host)
    );

    trigger.addEventListener(
      "pointerleave",
      () => scheduleClose(host)
    );

    trigger.addEventListener(
      "focus",
      () => openSubmenu(host)
    );

    trigger.addEventListener(
      "focusout",
      (event) => {
        const next = event.relatedTarget;

        if (
          next &&
          (
            trigger.contains(next) ||
            submenu.contains(next)
          )
        ) {
          return;
        }

        scheduleClose(host);
      }
    );

    trigger.addEventListener(
      "click",
      (event) => {
        event.preventDefault();
        event.stopPropagation();

        if (submenu.hidden) {
          openSubmenu(host, true);
        } else {
          closeSubmenu(host);
        }
      }
    );

    trigger.addEventListener(
      "keydown",
      (event) => {
        if (
          event.key === "ArrowRight" ||
          event.key === "ArrowDown" ||
          event.key === "Enter" ||
          event.key === " "
        ) {
          event.preventDefault();
          event.stopPropagation();
          openSubmenu(host, true);
        }
      }
    );

    submenu.addEventListener(
      "pointerenter",
      () => clearCloseTimer(state)
    );

    submenu.addEventListener(
      "pointerleave",
      () => scheduleClose(host)
    );

    submenu.addEventListener(
      "focusin",
      () => clearCloseTimer(state)
    );

    submenu.addEventListener(
      "focusout",
      (event) => {
        const next = event.relatedTarget;

        if (
          next &&
          (
            submenu.contains(next) ||
            trigger.contains(next)
          )
        ) {
          return;
        }

        scheduleClose(host);
      }
    );

    submenu.addEventListener(
      "keydown",
      (event) => {
        if (
          event.key === "Escape" ||
          event.key === "ArrowLeft"
        ) {
          event.preventDefault();
          event.stopPropagation();
          closeSubmenu(host, true);
          return;
        }

        let direction = "";

        if (event.key === "ArrowDown") {
          direction = "next";
        } else if (event.key === "ArrowUp") {
          direction = "previous";
        } else if (event.key === "Home") {
          direction = "home";
        } else if (event.key === "End") {
          direction = "end";
        }

        if (!direction) return;

        const next =
          nextMenuButton(
            submenu,
            document.activeElement,
            direction
          );

        if (!next) return;

        event.preventDefault();
        event.stopPropagation();
        next.focus();
      }
    );

    host.appendChild(trigger);
    host.appendChild(submenu);

    return trigger;
  }

  function close(host) {
    const state = hostStates.get(host);

    if (!state) return false;

    clearCloseTimer(state);
    state.submenu.hidden = true;
    state.trigger.setAttribute(
      "aria-expanded",
      "false"
    );

    hostStates.delete(host);
    return true;
  }

  FM.spreadsheetBorderMenu = {
    appendSubmenu,
    close,
    closeSubmenu
  };
})();
