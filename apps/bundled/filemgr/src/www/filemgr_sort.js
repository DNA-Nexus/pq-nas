window.PQNAS_FILEMGR = window.PQNAS_FILEMGR || {};

(() => {
    "use strict";

    const FM = window.PQNAS_FILEMGR;
    const SORT_KEY = "pqnas_filemgr_sort_mode_v1";

    function tr(key, vars = null, fallback = "") {
        try {
            if (window.PQNAS_I18N && typeof window.PQNAS_I18N.t === "function") {
                return window.PQNAS_I18N.t(key, vars, fallback || key);
            }
        } catch (_) {}
        return fallback || key;
    }

    const MODES = [
        {
            id: "dirs_first",
            shortLabel: "Dirs",
            shortLabelKey: "filemgr.sort.dirs",
            title: "Directories first, then name",
            titleKey: "filemgr.sort.dirs_title"
        },
        {
            id: "name_az",
            shortLabel: "A–Z",
            shortLabelKey: "filemgr.sort.name_az",
            title: "Alphabetical (A–Z)",
            titleKey: "filemgr.sort.name_az_title"
        },
        {
            id: "type_az",
            shortLabel: "Type",
            shortLabelKey: "filemgr.sort.type_az",
            title: "File type, then name",
            titleKey: "filemgr.sort.type_az_title"
        },
        {
            id: "favorites_first",
            shortLabel: "★ First",
            shortLabelKey: "filemgr.sort.favorites_first",
            title: "Favorites first",
            titleKey: "filemgr.sort.favorites_first_title"
        }
    ];

    let currentModeId = MODES[0].id;

    function cmpText(a, b) {
        return String(a || "").localeCompare(String(b || ""), undefined, {
            numeric: true,
            sensitivity: "base"
        });
    }

    function fileExtLower(name) {
        const n = String(name || "").toLowerCase().trim();
        const slash = Math.max(n.lastIndexOf("/"), n.lastIndexOf("\\"));
        const base = slash >= 0 ? n.slice(slash + 1) : n;

        if (!base) return "";
        if (base.startsWith(".") && base.indexOf(".", 1) === -1) return "";

        if (base.endsWith(".tar.gz")) return "gz";
        if (base.endsWith(".tar.bz2")) return "bz2";
        if (base.endsWith(".tar.xz")) return "xz";

        const dot = base.lastIndexOf(".");
        if (dot <= 0 || dot === base.length - 1) return "";
        return base.slice(dot + 1);
    }

    function getMode() {
        return MODES.find((m) => m.id === currentModeId) || MODES[0];
    }

    function modeShortLabel(mode = null) {
        const m = mode || getMode();
        return tr(m.shortLabelKey || "", null, m.shortLabel || m.id || "");
    }

    function modeTitle(mode = null) {
        const m = mode || getMode();
        return tr(m.titleKey || "", null, m.title || m.id || "");
    }

    function loadMode() {
        try {
            const raw = String(localStorage.getItem(SORT_KEY) || "").trim();
            if (MODES.some((m) => m.id === raw)) {
                currentModeId = raw;
                return;
            }
        } catch (_) {}
        currentModeId = MODES[0].id;
    }

    function saveMode() {
        try {
            localStorage.setItem(SORT_KEY, currentModeId);
        } catch (_) {}
    }

    function setMode(modeId) {
        const found = MODES.find((m) => m.id === modeId);
        currentModeId = found ? found.id : MODES[0].id;
        saveMode();
        return getMode();
    }

    function cycleMode() {
        const idx = MODES.findIndex((m) => m.id === currentModeId);
        const nextIdx = (idx >= 0 ? idx + 1 : 1) % MODES.length;
        currentModeId = MODES[nextIdx].id;
        saveMode();
        return getMode();
    }

    function cmpDirsFirst(a, b) {
        const aDir = a && a.type === "dir";
        const bDir = b && b.type === "dir";

        if (aDir !== bDir) return aDir ? -1 : 1;
        return cmpText(a && a.name, b && b.name);
    }

    function cmpNameAz(a, b) {
        const c = cmpText(a && a.name, b && b.name);
        if (c) return c;

        const aDir = a && a.type === "dir";
        const bDir = b && b.type === "dir";
        if (aDir !== bDir) return aDir ? -1 : 1;

        return 0;
    }

    function cmpTypeAz(a, b) {
        const aDir = a && a.type === "dir";
        const bDir = b && b.type === "dir";

        if (aDir !== bDir) return aDir ? -1 : 1;
        if (aDir && bDir) return cmpText(a && a.name, b && b.name);

        const aExt = fileExtLower(a && a.name);
        const bExt = fileExtLower(b && b.name);

        const extCmp = cmpText(aExt, bExt);
        if (extCmp) return extCmp;

        return cmpText(a && a.name, b && b.name);
    }

    function isFavoriteForItem(item, ctx) {
        try {
            if (ctx && typeof ctx.isFavoriteItem === "function") {
                return !!ctx.isFavoriteItem(item);
            }

            if (
                ctx &&
                typeof ctx.currentRelPathFor === "function" &&
                typeof ctx.isFavoriteRelPath === "function"
            ) {
                return !!ctx.isFavoriteRelPath(ctx.currentRelPathFor(item), item && item.type);
            }
        } catch (_) {}

        return false;
    }

    function cmpFavoritesFirst(a, b, ctx) {
        const aFav = isFavoriteForItem(a, ctx) ? 1 : 0;
        const bFav = isFavoriteForItem(b, ctx) ? 1 : 0;

        if (aFav !== bFav) return bFav - aFav;
        return cmpDirsFirst(a, b);
    }

    function sortItems(items, ctx = {}) {
        const arr = Array.isArray(items) ? items.slice() : [];
        const mode = getMode();

        arr.sort((a, b) => {
            if (mode.id === "name_az") return cmpNameAz(a, b);
            if (mode.id === "type_az") return cmpTypeAz(a, b);
            if (mode.id === "favorites_first") return cmpFavoritesFirst(a, b, ctx);
            return cmpDirsFirst(a, b);
        });

        return arr;
    }

    function applyButtonUi(btn, txtEl, iconEl) {
        const mode = getMode();
        const idx = MODES.findIndex((m) => m.id === mode.id);
        const title = modeTitle(mode);
        const buttonTitle = tr("filemgr.sort.button_title", { sort: title }, `Sort: ${title}. Click to change.`);

        if (btn) {
            btn.title = buttonTitle;
            btn.setAttribute("aria-label", buttonTitle);
            btn.dataset.sortMode = mode.id;
        }

        if (txtEl) {
            txtEl.textContent = modeShortLabel(mode);
        }

        if (iconEl) {
            iconEl.style.transform = `rotate(${idx * 90}deg)`;
        }
    }

    FM.sort = {
        STORAGE_KEY: SORT_KEY,
        MODES: MODES.map((m) => ({ ...m })),
        loadMode,
        saveMode,
        getMode,
        modeShortLabel,
        modeTitle,
        setMode,
        cycleMode,
        sortItems,
        applyButtonUi
    };

    loadMode();
})();