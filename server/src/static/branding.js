// DNA-Nexus / PQ-NAS public UI branding helper.
//
// This file applies deployment/operator branding from:
//   GET /api/v4/public/branding
//
// It is intentionally display-only. There is no UI editor here. Operators can
// receive a branded deployment package where /etc/pqnas/branding.json and
// static branding assets are managed as deployment configuration.
(function () {
    "use strict";

    const DEFAULT_BRAND = {
        ok: true,
        enabled: false,
        product_name: "DNA-Nexus NAS",
        product_short_name: "DNA-Nexus",
        company_name: "CPUNK",
        copyright: "© CPUNK 2026 · DNA-Nexus",
        hide_upstream_brand: false,
        logo_dark: "/static/img/logo/Nexus_logo_dark.png",
        logo_bright: "/static/img/logo/Nexus_logo_bright.png",
        logo_wordmark: "/static/img/logo/nexuslogo_text.svg",
        favicon: "/static/favicon.ico",
        primary_color: "",
        accent_color: "",
        support_url: "",
        presentation_url: "/static/nexus-presentation/index.html",
        show_presentation_link: true
    };

    let currentBrand = Object.assign({}, DEFAULT_BRAND);
    let readyPromise = null;

    function cleanString(value, fallback) {
        if (typeof value !== "string") return fallback || "";
        return value.replace(/[\u0000-\u001f\u007f]/g, " ").trim();
    }

    function normalizeBrand(raw) {
        const out = Object.assign({}, DEFAULT_BRAND);
        if (!raw || typeof raw !== "object") return out;

        out.enabled = raw.enabled === true;
        out.product_name = cleanString(raw.product_name, out.product_name) || out.product_name;
        out.product_short_name = cleanString(raw.product_short_name, out.product_short_name) || out.product_short_name;
        out.company_name = cleanString(raw.company_name, out.company_name) || out.company_name;
        out.copyright = cleanString(raw.copyright, out.copyright) || out.copyright;
        out.hide_upstream_brand = raw.hide_upstream_brand === true;

        out.logo_dark = cleanString(raw.logo_dark, out.logo_dark) || out.logo_dark;
        out.logo_bright = cleanString(raw.logo_bright, out.logo_bright) || out.logo_bright;
        out.logo_wordmark = cleanString(raw.logo_wordmark, out.logo_wordmark) || out.logo_wordmark;
        out.favicon = cleanString(raw.favicon, out.favicon) || out.favicon;

        out.primary_color = cleanString(raw.primary_color, "");
        out.accent_color = cleanString(raw.accent_color, "");
        out.support_url = cleanString(raw.support_url, "");
        out.presentation_url = cleanString(raw.presentation_url, out.presentation_url);
        out.show_presentation_link = raw.show_presentation_link !== false;

        return out;
    }

    function pickLogoForTheme(theme, fallback) {
        const brand = currentBrand || DEFAULT_BRAND;
        if (brand.enabled) {
            if (theme === "bright" || theme === "win_classic") {
                return brand.logo_bright || brand.logo_dark || fallback || DEFAULT_BRAND.logo_bright;
            }
            return brand.logo_dark || brand.logo_bright || fallback || DEFAULT_BRAND.logo_dark;
        }

        if (theme === "bright" || theme === "win_classic") {
            return DEFAULT_BRAND.logo_bright || fallback || DEFAULT_BRAND.logo_dark;
        }

        if (theme === "cpunk_orange") {
            return "/static/img/logo/Nexus_logo_orange.png";
        }

        return DEFAULT_BRAND.logo_dark || fallback || DEFAULT_BRAND.logo_bright;
    }

    function setFavicon(href) {
        if (!href) return;

        let link = document.querySelector('link[rel="icon"]');
        if (!link) {
            link = document.createElement("link");
            link.rel = "icon";
            document.head.appendChild(link);
        }
        link.href = href;
    }

    function applyTitle(root) {
        if (!document || !document.title) return;

        const titleEl = document.querySelector("title");
        if (!titleEl) return;

        const brand = currentBrand || DEFAULT_BRAND;
        const suffix = titleEl.getAttribute("data-brand-title-suffix");
        const exact = titleEl.getAttribute("data-brand-title");

        if (exact !== null) {
            document.title = exact ? exact.replace("{product_name}", brand.product_name) : brand.product_name;
            return;
        }

        if (suffix !== null) {
            document.title = brand.product_name + suffix;
        }
    }

    function renderBrandTemplate(template, brand) {
        return String(template || "")
            .replaceAll("{product_name}", brand.product_name || "")
            .replaceAll("{product_short_name}", brand.product_short_name || brand.product_name || "")
            .replaceAll("{company_name}", brand.company_name || "")
            .replaceAll("{copyright}", brand.copyright || "");
    }

    function applyVersionBranding(root, brand) {
        if (!brand || brand.enabled !== true) return;

        const shortName = brand.product_short_name || brand.product_name || "";
        if (!shortName) return;

        const walker = document.createTreeWalker(
            root || document,
            NodeFilter.SHOW_TEXT,
            {
                acceptNode(node) {
                    const value = String(node.nodeValue || "").trim();
                    if (/^DNA-Nexus\s+v\d+(?:\.\d+)*(?:[-+][A-Za-z0-9._-]+)?$/.test(value)) {
                        return NodeFilter.FILTER_ACCEPT;
                    }
                    return NodeFilter.FILTER_REJECT;
                }
            }
        );

        const nodes = [];
        while (walker.nextNode()) {
            nodes.push(walker.currentNode);
        }

        for (const node of nodes) {
            node.nodeValue = String(node.nodeValue || "").replace(/^DNA-Nexus\s+v/, shortName + " v");
        }
    }

    function apply(root) {
        root = root || document;
        const brand = currentBrand || DEFAULT_BRAND;
        const theme = document.documentElement.getAttribute("data-theme") || "dark";

        applyTitle(root);

        if (brand.enabled) {
            document.documentElement.setAttribute("data-branding-enabled", "1");
        } else {
            document.documentElement.removeAttribute("data-branding-enabled");
        }

        if (brand.primary_color) {
            document.documentElement.style.setProperty("--brand-primary", brand.primary_color);
        }

        if (brand.accent_color) {
            document.documentElement.style.setProperty("--brand-accent", brand.accent_color);
        }

        if (brand.favicon) {
            setFavicon(brand.favicon);
        }

        root.querySelectorAll("[data-brand-text]").forEach((el) => {
            const key = el.getAttribute("data-brand-text");
            if (key === "product_name") el.textContent = brand.product_name;
            else if (key === "product_short_name") el.textContent = brand.product_short_name;
            else if (key === "company_name") el.textContent = brand.company_name;
            else if (key === "copyright") el.textContent = brand.copyright;
        });

        root.querySelectorAll("[data-brand-template]").forEach((el) => {
            el.textContent = renderBrandTemplate(el.getAttribute("data-brand-template"), brand);
        });

        applyVersionBranding(root, brand);

        root.querySelectorAll("[data-brand-aria-label]").forEach((el) => {
            const key = el.getAttribute("data-brand-aria-label");
            if (key === "product") el.setAttribute("aria-label", brand.product_short_name || brand.product_name);
            if (key === "product_node_profile") el.setAttribute("aria-label", "Open " + (brand.product_short_name || brand.product_name) + " node profile");
        });

        root.querySelectorAll("[data-brand-alt]").forEach((el) => {
            const key = el.getAttribute("data-brand-alt");
            if (key === "product_logo") el.setAttribute("alt", (brand.product_short_name || brand.product_name) + " logo");
        });

        root.querySelectorAll("img[data-brand-logo]").forEach((img) => {
            const fallback = img.getAttribute("data-brand-logo-fallback") || img.getAttribute("src") || "";
            const mode = img.getAttribute("data-brand-logo") || "theme";

            let src = "";
            if (mode === "wordmark") {
                src = brand.enabled
                    ? (brand.logo_wordmark || fallback || DEFAULT_BRAND.logo_wordmark)
                    : (fallback || DEFAULT_BRAND.logo_wordmark);
            } else {
                src = pickLogoForTheme(theme, fallback);
            }

            if (src && img.getAttribute("src") !== src) {
                img.setAttribute("src", src);
            }
        });

        root.querySelectorAll("[data-brand-presentation-link]").forEach((el) => {
            if (brand.presentation_url && brand.show_presentation_link) {
                el.setAttribute("href", brand.presentation_url);
            }
        });

        root.querySelectorAll("[data-brand-hide-if-presentation-disabled]").forEach((el) => {
            const hide = !brand.show_presentation_link || !brand.presentation_url || brand.hide_upstream_brand;
            el.style.display = hide ? "none" : "";
        });
    }

    function load() {
        if (readyPromise) return readyPromise;

        readyPromise = fetch("/api/v4/public/branding", {
            credentials: "same-origin",
            cache: "no-store",
            headers: { "Accept": "application/json" }
        })
            .then((res) => res.ok ? res.json() : null)
            .then((json) => {
                currentBrand = normalizeBrand(json);
                apply(document);
                return currentBrand;
            })
            .catch(() => {
                currentBrand = Object.assign({}, DEFAULT_BRAND);
                apply(document);
                return currentBrand;
            });

        return readyPromise;
    }

    window.PQNAS_BRANDING = {
        ready: load,
        apply,
        isEnabled: function () {
            return !!(currentBrand && currentBrand.enabled);
        },
        current: function () {
            return Object.assign({}, currentBrand || DEFAULT_BRAND);
        },
        pickLogoForTheme
    };

    if (document.readyState === "loading") {
        document.addEventListener("DOMContentLoaded", function () {
            load();
        });
    } else {
        load();
    }

    window.addEventListener("pqnas-language-changed", function () {
        setTimeout(function () { apply(document); }, 0);
    });

    window.addEventListener("pqnas-theme-changed", function () {
        apply(document);
    });
})();
