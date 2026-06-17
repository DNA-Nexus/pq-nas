document.addEventListener("DOMContentLoaded", () => {
  document.querySelectorAll("img.wiki-img").forEach((img) => {
    if (img.closest("a.wiki-img-link")) {
      return;
    }

    const link = document.createElement("a");
    link.className = "wiki-img-link";
    link.href = img.currentSrc || img.src;
    link.target = "_blank";
    link.rel = "noopener noreferrer";
    link.title = "Open full-size image";

    img.parentNode.insertBefore(link, img);
    link.appendChild(img);
  });
});

// wiki-link-preview:start
document.addEventListener("DOMContentLoaded", () => {
  const article = document.querySelector(".article");
  if (!article) return;

  const cache = new Map();
  let preview = null;
  let activeLink = null;
  let hideTimer = 0;
  let moveHandlerInstalled = false;

  function ensurePreview() {
    if (preview) return preview;

    preview = document.createElement("div");
    preview.className = "wiki-link-preview";
    preview.setAttribute("role", "tooltip");
    preview.innerHTML = `
      <div class="wiki-link-preview-title"></div>
      <div class="wiki-link-preview-path"></div>
      <div class="wiki-link-preview-body"></div>
    `;
    document.body.appendChild(preview);
    return preview;
  }

  function cleanText(value) {
    return String(value || "")
      .replace(/\s+/g, " ")
      .trim();
  }

  function isPreviewableLink(a) {
    if (!a || !a.href) return false;
    if (a.closest(".sidebar") || a.closest(".wiki-topbar") || a.closest(".toc")) return false;
    if (a.classList.contains("wiki-img-link")) return false;

    let url;
    try {
      url = new URL(a.href, window.location.href);
    } catch (_) {
      return false;
    }

    if (url.origin !== window.location.origin) return false;
    if (!url.pathname.endsWith(".html") && !url.pathname.endsWith("/")) return false;
    if (url.pathname === window.location.pathname && !url.hash) return false;

    return true;
  }

  function cacheKeyFor(a) {
    const url = new URL(a.href, window.location.href);
    return url.pathname + url.search + url.hash;
  }

  function fetchUrlFor(a) {
    const url = new URL(a.href, window.location.href);
    url.hash = "";
    return url.toString();
  }

  function pickSummaryFromDocument(doc, hash) {
    const article = doc.querySelector(".article") || doc.body;

    if (hash) {
      const id = decodeURIComponent(hash.replace(/^#/, ""));
      const target = id ? doc.getElementById(id) : null;

      if (target) {
        const title = cleanText(target.textContent);
        let node = target.nextElementSibling;
        while (node && !/^H[1-3]$/i.test(node.tagName)) {
          if (node.matches("p, li")) {
            const body = cleanText(node.textContent);
            if (body) return { sectionTitle: title, body };
          }
          node = node.nextElementSibling;
        }
        if (title) return { sectionTitle: title, body: "" };
      }
    }

    const lead = article.querySelector(".lead");
    if (lead && cleanText(lead.textContent)) {
      return { sectionTitle: "", body: cleanText(lead.textContent) };
    }

    const firstP = article.querySelector("p");
    if (firstP && cleanText(firstP.textContent)) {
      return { sectionTitle: "", body: cleanText(firstP.textContent) };
    }

    return { sectionTitle: "", body: "" };
  }

  async function loadPreviewData(a) {
    const key = cacheKeyFor(a);
    if (cache.has(key)) return cache.get(key);

    const promise = (async () => {
      const r = await fetch(fetchUrlFor(a), {
        credentials: "same-origin",
        cache: "force-cache",
        headers: { "Accept": "text/html" }
      });

      if (!r.ok) throw new Error(`HTTP ${r.status}`);

      const html = await r.text();
      const doc = new DOMParser().parseFromString(html, "text/html");
      const h1 = cleanText(doc.querySelector(".article h1, h1")?.textContent);
      const title = h1 || cleanText(doc.title).replace(/\s*·\s*DNA-Nexus Wiki\s*$/i, "");
      const summary = pickSummaryFromDocument(doc, new URL(a.href, window.location.href).hash);
      const path = decodeURIComponent(new URL(a.href, window.location.href).pathname.split("/").slice(-2).join("/"));

      return {
        title: summary.sectionTitle || title || cleanText(a.textContent),
        pageTitle: title || "",
        path,
        body: summary.body
      };
    })();

    cache.set(key, promise);
    return promise;
  }

  function positionPreview(evOrLink) {
    if (!preview || !preview.classList.contains("show")) return;

    let x = 0;
    let y = 0;

    if (evOrLink && typeof evOrLink.clientX === "number") {
      x = evOrLink.clientX;
      y = evOrLink.clientY;
    } else if (activeLink) {
      const r = activeLink.getBoundingClientRect();
      x = r.left + Math.min(260, r.width / 2);
      y = r.bottom + 8;
    }

    const margin = 14;
    const gap = 16;
    const box = preview.getBoundingClientRect();

    let left = x + gap;
    let top = y + gap;

    if (left + box.width + margin > window.innerWidth) {
      left = Math.max(margin, window.innerWidth - box.width - margin);
    }
    if (top + box.height + margin > window.innerHeight) {
      top = Math.max(margin, y - box.height - gap);
    }

    preview.style.left = `${Math.round(left)}px`;
    preview.style.top = `${Math.round(top)}px`;
  }

  async function showPreview(a, ev = null) {
    activeLink = a;
    clearTimeout(hideTimer);

    const box = ensurePreview();
    box.classList.add("loading");
    box.classList.add("show");
    box.querySelector(".wiki-link-preview-title").textContent = cleanText(a.textContent) || "Loading…";
    box.querySelector(".wiki-link-preview-path").textContent = "";
    box.querySelector(".wiki-link-preview-body").textContent = "Loading preview…";
    positionPreview(ev || a);

    try {
      const data = await loadPreviewData(a);
      if (activeLink !== a) return;

      box.classList.remove("loading");
      box.querySelector(".wiki-link-preview-title").textContent = data.title || cleanText(a.textContent);
      box.querySelector(".wiki-link-preview-path").textContent = data.path || "";
      box.querySelector(".wiki-link-preview-body").textContent = data.body || "No preview text available.";
      positionPreview(ev || a);
    } catch (_) {
      if (activeLink !== a) return;

      box.classList.remove("loading");
      box.querySelector(".wiki-link-preview-title").textContent = cleanText(a.textContent);
      box.querySelector(".wiki-link-preview-path").textContent = "";
      box.querySelector(".wiki-link-preview-body").textContent = "Preview unavailable.";
      positionPreview(ev || a);
    }

    if (!moveHandlerInstalled) {
      moveHandlerInstalled = true;
      document.addEventListener("mousemove", (ev) => {
        if (activeLink && preview && preview.classList.contains("show")) {
          positionPreview(ev);
        }
      }, { passive: true });
    }
  }

  function hidePreview() {
    clearTimeout(hideTimer);
    hideTimer = window.setTimeout(() => {
      activeLink = null;
      if (preview) preview.classList.remove("show", "loading");
    }, 120);
  }

  article.querySelectorAll("a[href]").forEach((a) => {
    if (!isPreviewableLink(a)) return;

    a.classList.add("wiki-preview-link");

    a.addEventListener("mouseenter", (ev) => {
      showPreview(a, ev);
    });

    a.addEventListener("mouseleave", () => {
      hidePreview();
    });

    a.addEventListener("focus", () => {
      showPreview(a);
    });

    a.addEventListener("blur", () => {
      hidePreview();
    });
  });
});
// wiki-link-preview:end

