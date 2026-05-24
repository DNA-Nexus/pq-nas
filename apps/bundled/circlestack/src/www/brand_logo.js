(() => {
  "use strict";

  function findTitleElement() {
    const candidates = [
      ...document.querySelectorAll("h1, .cs-title, .app-title, .title")
    ];

    return candidates.find(el =>
      /circle\s*stack/i.test(String(el.textContent || ""))
    ) || document.querySelector("h1, .cs-title, .app-title, .title");
  }

  function applyBrandLogo() {
    const title = findTitleElement();
    if (!title) return false;
    if (title.querySelector("#csBrandTitle")) return true;

    const wrap = document.createElement("span");
    wrap.id = "csBrandTitle";
    wrap.className = "cs-brand-title";

    const img = document.createElement("img");
    img.src = "circle_stack_wordmark.svg?v=20260523-brandlogo1";
    img.alt = "Circle Stack";
    img.decoding = "async";

    img.addEventListener("error", () => {
      wrap.textContent = "Circle Stack";
    });

    wrap.appendChild(img);

    title.textContent = "";
    title.classList.add("cs-title-has-brand");
    title.appendChild(wrap);
    return true;
  }

  function bootBrandLogo() {
    if (applyBrandLogo()) return;

    let tries = 0;
    const timer = setInterval(() => {
      tries += 1;
      if (applyBrandLogo() || tries > 40) {
        clearInterval(timer);
      }
    }, 250);
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", bootBrandLogo);
  } else {
    bootBrandLogo();
  }
})();
