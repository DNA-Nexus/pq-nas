(() => {
  "use strict";

  const CS_MANIFEST_VERSION = "0.1.0";
  const CS_MANIFEST_NAME = "Circle Stack";

  function injectVersionBadgeStyle() {
    if (document.getElementById("csVersionBadgeStyle")) return;

    const style = document.createElement("style");
    style.id = "csVersionBadgeStyle";
    style.textContent = `
      .cs-app-version {
        display:inline-flex;
        align-items:center;
        margin-left:8px;
        padding:2px 6px;
        border:1px solid rgba(255,255,255,.10);
        border-radius:999px;
        color:rgba(255,255,255,.42);
        background:rgba(0,0,0,.10);
        font-size:10px;
        font-weight:650;
        line-height:1.15;
        letter-spacing:.02em;
        vertical-align:middle;
        user-select:none;
      }

      .cs-app-version:hover,
      .cs-app-version:focus {
        color:rgba(255,255,255,.72);
        border-color:rgba(0,240,248,.22);
      }

      .cs-app-version.is-floating {
        position:fixed;
        right:10px;
        bottom:8px;
        z-index:20;
        margin-left:0;
        opacity:.72;
      }
    `;
    document.head.appendChild(style);
  }

  function findTitleElement() {
    const candidates = [
      ...document.querySelectorAll("h1, .cs-title, .app-title, .title")
    ];

    return candidates.find(el =>
      /circle\s+stack/i.test(String(el.textContent || ""))
    ) || document.querySelector("h1");
  }

  function renderVersionBadge(version) {
    if (!version) return;

    injectVersionBadgeStyle();

    let badge = document.getElementById("csAppVersion");
    if (!badge) {
      badge = document.createElement("span");
      badge.id = "csAppVersion";
      badge.className = "cs-app-version";
      badge.tabIndex = 0;
    }

    badge.textContent = `v${version}`;
    badge.setAttribute("aria-label", `${CS_MANIFEST_NAME} version ${version}`);

    const title = findTitleElement();
    if (title) {
      badge.classList.remove("is-floating");
      if (!badge.parentNode || badge.parentNode !== title) {
        title.appendChild(badge);
      }
      return;
    }

    badge.classList.add("is-floating");
    if (!badge.parentNode) {
      document.body.appendChild(badge);
    }
  }

  document.addEventListener("DOMContentLoaded", () => {
    renderVersionBadge(CS_MANIFEST_VERSION);
  });
})();
