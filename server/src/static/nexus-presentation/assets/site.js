(function () {
  const body = document.body;
  const toggle = document.querySelector("[data-nav-toggle]");
  const links = document.querySelectorAll(".nav-links a");

  if (toggle) {
    toggle.addEventListener("click", () => {
      const open = body.classList.toggle("menu-open");
      toggle.setAttribute("aria-expanded", open ? "true" : "false");
    });
  }

  links.forEach((link) => {
    link.addEventListener("click", () => {
      body.classList.remove("menu-open");
      if (toggle) toggle.setAttribute("aria-expanded", "false");
    });
  });

  const current = (location.pathname.split("/").pop() || "index.html").toLowerCase();
  links.forEach((link) => {
    const href = (link.getAttribute("href") || "").toLowerCase();
    if (href === current || (current === "" && href === "index.html")) link.classList.add("active");
  });

  const meta = {
    en: ["GB", "English"], fi: ["FI", "Suomi"], zh: ["CN", "简体中文"], sv: ["SE", "Svenska"],
    uk: ["UA", "Українська"], de: ["DE", "Deutsch"], et: ["EE", "Eesti"], pl: ["PL", "Polski"],
    es: ["ES", "Español"], fr: ["FR", "Français"], it: ["IT", "Italiano"], tr: ["TR", "Türkçe"]
  };

  const select = document.querySelector("[data-language-select]");
  const badge = document.querySelector("[data-language-badge]");
  if (select && badge) {
    const saved = localStorage.getItem("dnaNexusPresentationLang");
    if (saved && meta[saved]) select.value = saved;
    const update = () => {
      const m = meta[select.value] || meta.en;
      badge.textContent = m[0];
      select.setAttribute("aria-label", "Language: " + m[1]);
    };
    update();
    select.addEventListener("change", () => {
      localStorage.setItem("dnaNexusPresentationLang", select.value);
      update();
    });
  }
})();
