// External workspace users must never enter the normal desktop/app launcher.
// The server-side landing endpoint is the source of truth.
(function pqnasExternalWorkspaceAppGuard(){
    "use strict";

    if (window.pqnasExternalWorkspaceAppGuardInstalled) return;
    window.pqnasExternalWorkspaceAppGuardInstalled = true;

    try {
        const path = String(window.location.pathname || "");
        const isNormalApp =
            path === "/app" ||
            path === "/app/" ||
            path === "/static/app.html" ||
            path.endsWith("/app.html");

        if (!isNormalApp) return;

        fetch("/api/v4/workspaces/external-session/landing", {
            method: "GET",
            credentials: "include",
            cache: "no-store",
            headers: { "Accept": "application/json" }
        })
        .then(r => r.json().catch(() => null))
        .then(j => {
            if (!j || !j.ok || !j.external_workspace_only) return;

            if (j.workspace_url) {
                window.location.replace(String(j.workspace_url));
                return;
            }

            window.location.replace(String(j.login_url || "/static/login.html"));
        })
        .catch(() => {});
    } catch (_) {}
})();

(() => {
    const out = document.getElementById("out");

    const badge = document.getElementById("stateBadge");
    const statusLine = document.getElementById("statusLine");
    const refreshBtn = document.getElementById("refreshBtn");

    const stateDisabled = document.getElementById("state_disabled");
    const stateUnauth = document.getElementById("state_unauth");

    const navHome = document.getElementById("nav_home");
    const navTrustedDevices = document.getElementById("nav_trusted_devices");
    const navWorkspaceInvites = document.getElementById("nav_workspace_invites");
    const navWorkspaceInvitesCount = document.getElementById("nav_workspace_invites_count");
    const navPeople = document.getElementById("nav_people");
    const navUserSettings = document.getElementById("nav_user_settings");

    const navAdmin = document.getElementById("nav_admin");
    const navUsers = document.getElementById("nav_users");
    const navAudit = document.getElementById("nav_audit");
    const navSettings = document.getElementById("nav_settings");

    const navLogin = document.getElementById("nav_login");
    const navLogout = document.getElementById("nav_logout");

    const wsTitle = document.getElementById("wsTitle");
    const wsSubtitle = document.getElementById("wsSubtitle");
    const mainPaneTitle = document.getElementById("mainPaneTitle");
    const homeBlurb = document.getElementById("homeBlurb");

    const activityPane = document.getElementById("activityPane");
    const toggleActivityBtn = document.getElementById("toggleActivityBtn");
    const activityRefreshBtn = document.getElementById("activityRefreshBtn");
    const activityList = document.getElementById("activityList");
    const activityStatus = document.getElementById("activityStatus");
    const activityPager = document.getElementById("activityPager");
    const activityPrevBtn = document.getElementById("activityPrevBtn");
    const activityNextBtn = document.getElementById("activityNextBtn");
    const activityPageInfo = document.getElementById("activityPageInfo");
    const contentGrid = document.getElementById("contentGrid");

    const appsList = document.getElementById("appsList");

    function tr(key, vars = null, fallback = "") {
        try {
            if (window.PQNAS_I18N && typeof window.PQNAS_I18N.t === "function") {
                return window.PQNAS_I18N.t(key, vars, fallback || key);
            }
        } catch {}
        return fallback || key;
    }


    function brandedProductShortName(fallback = "Server") {
        try {
            const api = window.PQNAS_BRANDING;
            if (!api || typeof api.current !== "function") return fallback;

            const brand = api.current();
            if (!brand || brand.enabled !== true) return fallback;

            return String(brand.product_short_name || brand.product_name || fallback).trim() || fallback;
        } catch {
            return fallback;
        }
    }


    async function brandedProductShortNameReady(fallback = "Server") {
        try {
            const api = window.PQNAS_BRANDING;
            if (api && typeof api.ready === "function") {
                await api.ready();
            }
        } catch {}

        return brandedProductShortName(fallback);
    }


    function injectShellDialogCss() {
        if (document.getElementById("shellDialogCss")) return;

        const style = document.createElement("style");
        style.id = "shellDialogCss";
        style.textContent = `
.shellDialogBackdrop{
    position:fixed;
    inset:0;
    z-index:100000;
    display:flex;
    align-items:center;
    justify-content:center;
    padding:18px;
    background:rgba(0,0,0,0.55);
    backdrop-filter:blur(6px);
    -webkit-backdrop-filter:blur(6px);
}
.shellDialogCard{
    width:min(560px, calc(100vw - 24px));
    border:1px solid var(--border2, rgba(120,120,120,0.45));
    border-radius:18px;
    background:linear-gradient(180deg, var(--panel2, #f8f8f8), var(--panel, #eeeeee));
    box-shadow:0 18px 70px rgba(0,0,0,0.42);
    color:var(--fg, #111);
    overflow:hidden;
}
.shellDialogHead{
    padding:14px 16px;
    border-bottom:1px solid var(--border2, rgba(120,120,120,0.35));
    background:rgba(0,0,0,0.08);
}
.shellDialogTitle{
    font-weight:950;
    letter-spacing:.2px;
    font-size:16px;
}
.shellDialogBody{
    padding:16px;
    color:var(--fg, #111);
    line-height:1.5;
    white-space:pre-wrap;
    overflow-wrap:anywhere;
}
.shellDialogFoot{
    display:flex;
    align-items:center;
    gap:12px;
    padding:12px 16px;
    border-top:1px solid var(--border2, rgba(120,120,120,0.35));
    background:rgba(0,0,0,0.08);
}
.shellDialogBtn{
    border:1px solid var(--border2, rgba(120,120,120,0.45));
    border-radius:14px;
    padding:9px 14px;
    font:inherit;
    font-weight:850;
    color:var(--fg, #111);
    background:linear-gradient(180deg, rgba(255,255,255,0.20), rgba(0,0,0,0.04));
    cursor:pointer;
}
.shellDialogBtn.secondary{ opacity:.90; }
.shellDialogBtn.danger{
    border-color:rgba(var(--fail-rgb, 180,40,40),0.48);
    background:rgba(var(--fail-rgb, 180,40,40),0.14);
}
html[data-theme="bright"] .shellDialogBackdrop{ background:rgba(0,0,0,0.30); }
html[data-theme="bright"] .shellDialogCard{
    background:linear-gradient(180deg, #ffffff, #f2f4f7) !important;
    border-color:rgba(70,80,95,0.32) !important;
    color:#111827 !important;
    box-shadow:0 22px 80px rgba(0,0,0,0.28) !important;
}
html[data-theme="bright"] .shellDialogHead,
html[data-theme="bright"] .shellDialogFoot{
    background:rgba(15,23,42,0.045) !important;
    border-color:rgba(70,80,95,0.22) !important;
}
html[data-theme="bright"] .shellDialogTitle,
html[data-theme="bright"] .shellDialogBody,
html[data-theme="bright"] .shellDialogBtn{
    color:#111827 !important;
}
html[data-theme="win_classic"] .shellDialogBackdrop{ background:rgba(0,0,0,0.38); }
`;
        document.head.appendChild(style);
    }

    function openShellDialog(opts = {}) {
        injectShellDialogCss();

        return new Promise((resolve) => {
            const options = opts || {};
            const alertOnly = !!options.alertOnly;

            const modal = document.createElement("div");
            modal.className = "shellDialogBackdrop";
            modal.setAttribute("role", "dialog");
            modal.setAttribute("aria-modal", "true");

            const card = document.createElement("div");
            card.className = "shellDialogCard";

            const head = document.createElement("div");
            head.className = "shellDialogHead";

            const title = document.createElement("div");
            title.className = "shellDialogTitle";
            title.textContent = options.title || tr("shell.dialog.title", null, "Server");

            head.appendChild(title);

            const body = document.createElement("div");
            body.className = "shellDialogBody";
            body.textContent = options.message || "";

            const foot = document.createElement("div");
            foot.className = "shellDialogFoot";

            const spacer = document.createElement("div");
            spacer.style.flex = "1 1 auto";
            foot.appendChild(spacer);

            let cancelBtn = null;
            if (!alertOnly) {
                cancelBtn = document.createElement("button");
                cancelBtn.type = "button";
                cancelBtn.className = "shellDialogBtn secondary";
                cancelBtn.textContent = options.cancelText || tr("admin.common.cancel", null, "Cancel");
                foot.appendChild(cancelBtn);
            }

            const okBtn = document.createElement("button");
            okBtn.type = "button";
            okBtn.className = options.danger ? "shellDialogBtn danger" : "shellDialogBtn";
            okBtn.textContent = options.confirmText || tr("shell.dialog.ok", null, "OK");
            foot.appendChild(okBtn);

            card.appendChild(head);
            card.appendChild(body);
            card.appendChild(foot);
            modal.appendChild(card);
            document.body.appendChild(modal);

            const finish = (value) => {
                document.removeEventListener("keydown", onKey, true);
                modal.remove();
                resolve(!!value);
            };

            const onKey = (ev) => {
                if (ev.key === "Escape") {
                    ev.preventDefault();
                    ev.stopPropagation();
                    finish(alertOnly ? true : false);
                    return;
                }

                if (ev.key === "Enter") {
                    ev.preventDefault();
                    ev.stopPropagation();
                    finish(true);
                }
            };

            document.addEventListener("keydown", onKey, true);

            modal.addEventListener("click", (ev) => {
                if (ev.target === modal) finish(alertOnly ? true : false);
            });

            if (cancelBtn) cancelBtn.addEventListener("click", () => finish(false));
            okBtn.addEventListener("click", () => finish(true));

            window.setTimeout(() => okBtn.focus(), 0);
        });
    }

    function openShellAlertDialog(opts = {}) {
        return openShellDialog({ ...opts, alertOnly: true });
    }

    function openShellConfirmDialog(opts = {}) {
        return openShellDialog(opts);
    }

    function currentLanguageName() {
        try {
            if (window.PQNAS_I18N && typeof window.PQNAS_I18N.getLanguage === "function") {
                return window.PQNAS_I18N.getLanguage();
            }
        } catch {}

        try {
            return (localStorage.getItem("pqnas_lang") || "en").trim() || "en";
        } catch {}

        return "en";
    }

    async function applyUserLanguage(langName) {
        const lang = String(langName || "en").trim() || "en";

        try {
            if (window.PQNAS_I18N && typeof window.PQNAS_I18N.setLanguage === "function") {
                await window.PQNAS_I18N.setLanguage(lang);
            } else {
                localStorage.setItem("pqnas_lang", lang);
                document.documentElement.setAttribute("lang", lang);
            }
        } catch {}

        if (currentView === "user_settings") {
            renderUserSettings(tr("settings.language.saved", null, "Language saved."), "ok");
        }
    }
    function getMainHost() {
        return document.querySelector("[data-main-host]")
            || (homeBlurb ? homeBlurb.closest(".mainArea") : null)
            || (homeBlurb ? homeBlurb.closest(".pane") : null);
    }

    function setMainHostMode(mode) {
        const mainHost = getMainHost();
        if (!mainHost) return;

        mainHost.classList.remove("appHost", "homeHost");

        if (mode === "app") {
            mainHost.classList.add("appHost");
            mainHost.style.overflow = "hidden";

            if (homeBlurb) {
                homeBlurb.style.display = "";
                homeBlurb.classList.add("appHostBlurb");
                homeBlurb.style.overflowY = "hidden";
                homeBlurb.style.maxHeight = "100%";
            }

            getHomeContentHost();
            showHomeContent(false);

            const dock = getAppFrameDock();
            if (dock) {
                dock.style.display = "";
                dock.style.pointerEvents = "auto";
            }
        } else {
            mainHost.classList.add("homeHost");
            mainHost.style.overflow = "auto";

            if (homeBlurb) {
                homeBlurb.style.display = "";
                homeBlurb.classList.remove("appHostBlurb");

                // Home/settings views must be scrollable again after app iframe mode.
                homeBlurb.style.overflowY = "auto";
                homeBlurb.style.overflowX = "hidden";
                homeBlurb.style.maxHeight = "100%";
                homeBlurb.style.minHeight = "0";
            }

            showHomeContent(true);
            hideAllCachedAppFrames();
        }
    }

    // app state
    let installedApps = [];     // [{id, ver, name?, title?, ...}, ...]
    let meFpHex = "";           // fingerprint_hex from /api/v4/me (for desktop layout storage)
    let launchPolicyByAppId = {}; // { [appId]: { default_launch, window_profile, allow_user_override, admin_only, show_in_sidebar } }
    let appUserPrefs = {};       // server-persisted app preferences for current user
    let appUserPrefsLoaded = false;
    let appUserPrefsLoadInFlight = null;

    const ACTIVITY_PAGE_SIZE = 25;
    let activityOffset = 0;
    let activityTotal = 0;
    let activityHasMore = false;

    const APP_FRAME_CACHE_MAX = 3;
    const appFrameCache = new Map(); // key: "appId@ver" -> { frameWrap, frame, lastUsed }

    function appFrameKey(app) {
        return `${app.id}@${app.ver}`;
    }

    function getAppFrameDock() {
        if (!homeBlurb) return null;

        let dock = document.getElementById("appFrameDock");
        if (dock) return dock;

        dock = document.createElement("div");
        dock.id = "appFrameDock";
        dock.className = "appFrameDock";
        dock.style.display = "none";
        dock.style.width = "100%";
        dock.style.height = "100%";
        dock.style.minHeight = "0";
        dock.style.overflow = "hidden";
        dock.style.flex = "1 1 auto";
        dock.style.display = "none";

        homeBlurb.appendChild(dock);
        return dock;
    }

    function ensureAppOpeningSpinCss() {
        if (document.getElementById("appOpeningSpinCss")) return;

        const style = document.createElement("style");
        style.id = "appOpeningSpinCss";
        style.textContent = "@keyframes appOpeningSpin{to{transform:rotate(360deg)}}";
        document.head.appendChild(style);
    }

    function showAppOpeningOverlay(label, appKey) {
        const dock = getAppFrameDock();
        if (!dock) return;

        ensureAppOpeningSpinCss();

        if (!dock.style.position) {
            dock.style.position = "relative";
        }

        let overlay = document.getElementById("appOpeningOverlay");
        if (!overlay) {
            overlay = document.createElement("div");
            overlay.id = "appOpeningOverlay";
            overlay.setAttribute("role", "status");
            overlay.setAttribute("aria-live", "polite");

            overlay.style.position = "absolute";
            overlay.style.inset = "0";
            overlay.style.zIndex = "9999";
            overlay.style.display = "flex";
            overlay.style.alignItems = "center";
            overlay.style.justifyContent = "center";
            overlay.style.gap = "12px";
            overlay.style.background = "rgba(0,0,0,.30)";
            overlay.style.color = "var(--fg)";
            overlay.style.fontWeight = "900";
            overlay.style.pointerEvents = "none";

            const spin = document.createElement("span");
            spin.setAttribute("aria-hidden", "true");
            spin.style.width = "26px";
            spin.style.height = "26px";
            spin.style.borderRadius = "999px";
            spin.style.border = "3px solid rgba(255,255,255,.24)";
            spin.style.borderTopColor = "currentColor";
            spin.style.animation = "appOpeningSpin .8s linear infinite";

            const txt = document.createElement("span");
            txt.id = "appOpeningText";

            overlay.appendChild(spin);
            overlay.appendChild(txt);
        }

        overlay.dataset.appKey = String(appKey || "");
        overlay.hidden = false;
        overlay.style.display = "flex";

        const txt = overlay.querySelector("#appOpeningText");
        if (txt) txt.textContent = label || "Opening app…";

        dock.appendChild(overlay);
    }

    function updateAppOpeningOverlay(label, appKey) {
        const overlay = document.getElementById("appOpeningOverlay");
        if (!overlay || overlay.hidden) return;
        if (appKey && overlay.dataset.appKey && overlay.dataset.appKey !== String(appKey)) return;

        const txt = overlay.querySelector("#appOpeningText");
        if (txt) txt.textContent = label || "Still opening…";
    }

    function hideAppOpeningOverlay(appKey) {
        const overlay = document.getElementById("appOpeningOverlay");
        if (!overlay) return;
        if (appKey && overlay.dataset.appKey && overlay.dataset.appKey !== String(appKey)) return;

        overlay.hidden = true;
        overlay.style.display = "none";
    }

    function getHomeContentHost() {
        if (!homeBlurb) return null;

        let host = document.getElementById("homeContent");
        const dock = document.getElementById("appFrameDock");

        if (!host) {
            host = document.createElement("div");
            host.id = "homeContent";
            host.className = "homeContent";

            // Important:
            // Move existing Home/Desktop children into #homeContent so app mode can hide them.
            const oldChildren = Array.from(homeBlurb.childNodes)
                .filter((node) => node !== dock);

            for (const node of oldChildren) {
                host.appendChild(node);
            }

            if (dock && dock.parentElement === homeBlurb) {
                homeBlurb.insertBefore(host, dock);
            } else {
                homeBlurb.appendChild(host);
            }
        } else {
            // Repair older/bad DOM state: any direct child except host/dock belongs to homeContent.
            const strayChildren = Array.from(homeBlurb.childNodes)
                .filter((node) => node !== host && node !== dock);

            for (const node of strayChildren) {
                host.appendChild(node);
            }
        }

        return host;
    }

    function setHomeContentHtml(html) {
        const host = getHomeContentHost();
        if (!host) return null;

        host.innerHTML = html || "";

        // Keep dynamic home/settings content inside the viewport.
        // This prevents tall Settings pages from expanding the shell without a scrollbar.
        host.style.overflowY = "auto";
        host.style.overflowX = "hidden";
        host.style.minHeight = "0";
        host.style.paddingRight = "8px";
        host.style.boxSizing = "border-box";
        host.style.scrollbarGutter = "stable";
        window.setTimeout(fitHomeContentToViewport, 0);

        const dock = document.getElementById("appFrameDock");
        if (dock && dock.parentElement !== homeBlurb) {
            homeBlurb.appendChild(dock);
        }

        return host;
    }

    function fitHomeContentToViewport() {
        const host = document.getElementById("homeContent");
        if (!host) return;

        const rect = host.getBoundingClientRect();
        const top = Math.max(0, rect.top || 0);
        const available = Math.max(260, window.innerHeight - top - 18);

        host.style.height = `${available}px`;
        host.style.maxHeight = `${available}px`;
        host.style.overflowY = "auto";
        host.style.overflowX = "hidden";
        host.style.minHeight = "0";
        host.style.paddingRight = "8px";
        host.style.boxSizing = "border-box";
        host.style.scrollbarGutter = "stable";

        if (homeBlurb) {
            homeBlurb.style.overflow = "hidden";
            homeBlurb.style.minHeight = "0";
            homeBlurb.style.maxHeight = "none";
        }

        const mainHost = getMainHost();
        if (mainHost) {
            mainHost.style.overflow = "hidden";
            mainHost.style.minHeight = "0";
        }
    }

    function showHomeContent(on) {
        const host = document.getElementById("homeContent");
        if (host) host.style.display = on ? "" : "none";
    }
    function hideAllCachedAppFrames() {
        const dock = document.getElementById("appFrameDock");
        if (dock) {
            dock.style.display = "none";
            dock.style.pointerEvents = "none";
        }

        for (const rec of appFrameCache.values()) {
            if (!rec || !rec.frameWrap) continue;

            rec.frameWrap.classList.remove("active");
            rec.frameWrap.hidden = true;
            rec.frameWrap.style.pointerEvents = "none";
        }
    }

    function pruneCachedAppFrames(activeKey) {
        const entries = Array.from(appFrameCache.entries())
            .filter(([key]) => key !== activeKey)
            .sort((a, b) => Number(a[1].lastUsed || 0) - Number(b[1].lastUsed || 0));

        while (appFrameCache.size > APP_FRAME_CACHE_MAX && entries.length) {
            const [key, rec] = entries.shift();
            try { rec.frameWrap.remove(); } catch (_) {}
            appFrameCache.delete(key);
        }
    }

    function clearCachedAppFrames() {
        for (const rec of appFrameCache.values()) {
            try { rec.frameWrap.remove(); } catch (_) {}
        }
        appFrameCache.clear();

        const dock = document.getElementById("appFrameDock");
        if (dock) dock.remove();
    }

    // desktop state (icon layout + manifests)

    const manifestCache = new Map(); // key: "id@ver" -> parsed manifest json
    let desktopLayout = null;        // loaded from localStorage
    const DESKTOP_GRID_X = 20;
    const DESKTOP_GRID_Y = 22;
    let desktopSelectedKeys = new Set();
    let selectionBox = null;
    let selectionStartX = 0;
    let selectionStartY = 0;
    let selectionActive = false;


    let currentView = "home";   // "home" | "trusted_devices" | "workspace_invites" | "app:<id>@<ver>"
    let currentApp = null;      // {id, ver} or null
    let lastAppsKey = "";
    let authed = false;
    let isAdmin = false;

    let externalWorkspaceRedirectInFlight = false;

    function currentPathLooksExternalWorkspace() {
        try {
            return String(window.location.pathname || "").endsWith("/static/external_workspace.html");
        } catch {
            return false;
        }
    }

    async function maybeRedirectExternalWorkspaceOnly(ok, admin) {
        if (!ok || admin) return false;
        if (externalWorkspaceRedirectInFlight) return true;
        if (currentPathLooksExternalWorkspace()) return false;

        externalWorkspaceRedirectInFlight = true;

        try {
            const r = await fetch("/api/v4/workspaces/external-session/landing", {
                credentials: "include",
                cache: "no-store",
                headers: { "Accept": "application/json" }
            });

            const j = await r.json().catch(() => null);

            if (
                r.ok &&
                j &&
                j.ok === true &&
                j.external_workspace_only === true &&
                typeof j.workspace_url === "string" &&
                j.workspace_url.trim()
            ) {
                window.location.replace(j.workspace_url);
                return true;
            }
        } catch (_) {
            // If the helper route is unavailable, do not lock out normal users.
        }

        externalWorkspaceRedirectInFlight = false;
        return false;
    }


    let currentPairing = null; // { pair_id, expires_at, qr_svg, qr_uri }
    let pairPollTimer = null;

    // small UI state
    let versionShown = false;
    let trustedDevices = [];
    let trustedDevicesError = "";

    let workspaceInvites = [];
    let workspaceInvitesError = "";
    let userProfile = null;
    let userProfileLoading = false;
    let userProfileError = "";
    let userAvatarBust = 0;

    const USER_AVATAR_MAX_BYTES = 256 * 1024;
    const USER_AVATAR_TARGET_BYTES = 240 * 1024;
    const USER_AVATAR_MAX_DIM = 512;

    function show(el, on) {
        if (!el) return;
        el.style.display = on ? "" : "none";
    }
    function fmtDateTime(value) {
        if (!value) return "—";

        if (typeof value === "number") {
            const d = new Date(value * 1000);
            if (isNaN(d.getTime())) return "—";
            return d.toLocaleString();
        }

        const d = new Date(value);
        if (isNaN(d.getTime())) return String(value);
        return d.toLocaleString();
    }

    function fmtRemainingSec(sec) {
        if (!Number.isFinite(sec)) return "—";
        if (sec <= 0) return "expired";

        const days = Math.floor(sec / 86400);
        const hours = Math.floor((sec % 86400) / 3600);
        const mins = Math.floor((sec % 3600) / 60);

        if (days > 0) return `${days}d ${hours}h`;
        if (hours > 0) return `${hours}h ${mins}m`;
        return `${mins}m`;
    }
    function setBadge(kind, text) {
        if (!badge) return;

        const t = (text || "").trim();

        // Hide badge completely when no text
        if (!t) {
            badge.style.display = "none";
            badge.textContent = "";
            return;
        }

        // Show badge when there is content
        badge.style.display = "";
        badge.className = `badge ${kind}`;
        badge.textContent = t;
    }

    function setWsSubtitleSafe(txt) {
        // Don’t override app subtitle while an app is open.
        if (currentApp) return;
        if (wsSubtitle) wsSubtitle.textContent = txt;
    }

    function setActiveNav(activeId) {
        const ids = [
            "nav_home",
            "nav_trusted_devices",
            "nav_workspace_invites",
            "nav_people",
            "nav_user_settings"
        ];
        for (const id of ids) {
            const b = document.getElementById(id);
            if (!b) continue;
            b.classList.toggle("active", id === activeId);
        }
    }

    function clearAppsList() {
        if (!appsList) return;
        appsList.innerHTML = "";
    }
    function updateWorkspaceInvitesNav() {
        const count = Array.isArray(workspaceInvites) ? workspaceInvites.length : 0;

        if (navWorkspaceInvitesCount) {
            navWorkspaceInvitesCount.textContent = count > 99 ? "99+" : String(count);
            navWorkspaceInvitesCount.style.display = count > 0 ? "inline-flex" : "none";
            navWorkspaceInvitesCount.setAttribute("aria-hidden", count > 0 ? "false" : "true");
        }

        if (navWorkspaceInvites) {
            navWorkspaceInvites.classList.toggle("needs-attn", count > 0 && currentView !== "workspace_invites");
        }
    }
    function stopPairPolling() {
        if (pairPollTimer) {
            clearInterval(pairPollTimer);
            pairPollTimer = null;
        }
    }
    function appNavFallback(label, appId) {
        const raw = String(label || appId || "?").trim();
        const words = raw.split(/\s+/).filter(Boolean);
        if (words.length >= 2) {
            return (words[0][0] + words[1][0]).toUpperCase();
        }
        return raw.slice(0, 2).toUpperCase();
    }

    function compareAppVersions(a, b) {
        return String(a || "").localeCompare(String(b || ""), undefined, {
            numeric: true,
            sensitivity: "base"
        });
    }

    function newestInstalledAppsById(apps) {
        const byId = new Map();

        for (const app of apps || []) {
            const id = String(app && app.id ? app.id : "");
            if (!id) continue;

            const prev = byId.get(id);
            if (!prev || compareAppVersions(app.ver, prev.ver) >= 0) {
                byId.set(id, app);
            }
        }

        return Array.from(byId.values());
    }

    function addAppNavButton(appId, label, href, iconUrl) {
        if (!appsList) return;

        const a = document.createElement("a");
        a.className = "navbtn";
        a.href = href;
        a.dataset.appid = appId;
        a.title = label || appId || "App";

        const left = document.createElement("span");
        left.textContent = label;

        const icon = document.createElement("span");
        icon.className = "k appNavIcon";
        icon.setAttribute("aria-hidden", "true");

        if (iconUrl) {
            const safeUrl = String(iconUrl).replaceAll('"', "%22");
            icon.classList.add("hasMaskIcon");

            const glyph = document.createElement("i");
            glyph.className = "appNavIconGlyph";
            glyph.setAttribute("aria-hidden", "true");
            glyph.style.webkitMaskImage = `url("${safeUrl}")`;
            glyph.style.maskImage = `url("${safeUrl}")`;
            glyph.style.webkitMaskRepeat = "no-repeat";
            glyph.style.maskRepeat = "no-repeat";
            glyph.style.webkitMaskPosition = "center";
            glyph.style.maskPosition = "center";
            glyph.style.webkitMaskSize = "contain";
            glyph.style.maskSize = "contain";
            icon.appendChild(glyph);
        } else {
            const fallback = document.createElement("span");
            fallback.className = "appNavIconFallback";
            fallback.textContent = appNavFallback(label, appId);
            icon.appendChild(fallback);
        }

        a.appendChild(left);
        a.appendChild(icon);

        a.addEventListener("click", (ev) => {
            ev.preventDefault();
            openAppById(appId);
        });

        appsList.appendChild(a);
    }

    function setActiveApp(appId) {
        if (!appsList) return;
        for (const el of appsList.querySelectorAll(".navbtn")) {
            el.classList.toggle("active", el.dataset.appid === appId);
        }
    }

    const NOTEPAD_APP_ID = "notepad";
    const NOTEPAD_TITLE = "Notepad";
    const NOTEPAD_LAYOUT_KEY = "builtin:notepad";
    const NOTEPAD_DRAFT_MAX_CHARS = 65536;
    const NOTEPAD_MAX_MARKS = 200;
    const NOTEPAD_MARK_COLORS = ["yellow", "green", "blue", "red", "purple"];
    const NOTEPAD_MARK_CLASS = {
        yellow: "notepadMarkYellow",
        green: "notepadMarkGreen",
        blue: "notepadMarkBlue",
        red: "notepadMarkRed",
        purple: "notepadMarkPurple"
    };

    let notepadWindowZ = 4200;
    let notepadRestored = false;
    let notepadWindowDraft = "";
    let notepadMarks = [];
    let notepadActiveMarkColor = "yellow";
    let notepadLoaded = false;
    let notepadDirty = false;
    let notepadRevision = 0;
    let notepadSaveTimer = null;
    let notepadSaving = false;
    let notepadSavePending = false;
    let notepadLoadInFlight = null;

    function notepadClamp(value, min, max) {
        const n = Number(value);
        if (!Number.isFinite(n)) return min;
        return Math.min(max, Math.max(min, n));
    }

    function notepadUiStorageKey() {
        const fp = meFpHex ? meFpHex : "anon";
        return `pqnas_notepad_ui_v1::${fp}`;
    }

    function readNotepadUiState() {
        try {
            const raw = localStorage.getItem(notepadUiStorageKey());
            const j = raw ? JSON.parse(raw) : {};
            if (!j || typeof j !== "object") return {};
            return j;
        } catch {
            return {};
        }
    }

    function writeNotepadUiState(next) {
        const cur = readNotepadUiState();
        const merged = { ...cur, ...(next && typeof next === "object" ? next : {}) };
        try {
            localStorage.setItem(notepadUiStorageKey(), JSON.stringify(merged));
        } catch {}
    }

    function notepadRoot(win = null) {
        return win || document.getElementById("notepadFloatingWindow");
    }

    function notepadTextarea(win = null) {
        const root = notepadRoot(win);
        return root ? root.querySelector(".notepadText") : null;
    }

    function notepadHighlightLayer(win = null) {
        const root = notepadRoot(win);
        return root ? root.querySelector(".notepadHighlightLayer") : null;
    }

    function notepadStatusEl(win = null) {
        const root = notepadRoot(win);
        return root ? root.querySelector(".notepadStatus") : null;
    }

    function setNotepadStatus(text) {
        const el = notepadStatusEl();
        if (el) el.textContent = text;
    }

    function notepadColorOk(color) {
        return NOTEPAD_MARK_COLORS.includes(color);
    }

    function notepadColorLabel(color) {
        return tr(`notepad.color.${color}`, null, color);
    }

    function notepadTr(key, fallback, vars = null) {
        return tr(key, vars, fallback);
    }

    function normalizeNotepadMarks(marks, body) {
        const text = String(body || "");
        const max = text.length;
        const out = [];

        for (const raw of Array.isArray(marks) ? marks : []) {
            if (out.length >= NOTEPAD_MAX_MARKS) break;
            if (!raw || typeof raw !== "object") continue;

            const start = Number(raw.start);
            const end = Number(raw.end);
            const color = String(raw.color || "");

            if (!Number.isInteger(start) || !Number.isInteger(end)) continue;
            if (!notepadColorOk(color)) continue;
            if (start < 0 || end <= start || end > max) continue;

            out.push({ start, end, color });
        }

        out.sort((a, b) => {
            if (a.start !== b.start) return a.start - b.start;
            return a.end - b.end;
        });

        const nonOverlapping = [];
        let cursor = 0;
        for (const mark of out) {
            if (mark.start < cursor) continue;
            nonOverlapping.push(mark);
            cursor = mark.end;
        }

        return nonOverlapping;
    }

    function syncNotepadHighlightScroll(win = null) {
        const textarea = notepadTextarea(win);
        const layer = notepadHighlightLayer(win);
        if (!textarea || !layer) return;

        layer.scrollTop = textarea.scrollTop;
        layer.scrollLeft = textarea.scrollLeft;
    }

    function renderNotepadHighlights(win = null) {
        const textarea = notepadTextarea(win);
        const layer = notepadHighlightLayer(win);
        if (!textarea || !layer) return;

        const body = textarea.value || "";
        notepadMarks = normalizeNotepadMarks(notepadMarks, body);

        const frag = document.createDocumentFragment();
        let cursor = 0;

        for (const mark of notepadMarks) {
            if (mark.start > cursor) {
                frag.appendChild(document.createTextNode(body.slice(cursor, mark.start)));
            }

            const el = document.createElement("mark");
            el.className = NOTEPAD_MARK_CLASS[mark.color] || NOTEPAD_MARK_CLASS.yellow;
            el.dataset.color = mark.color;
            el.appendChild(document.createTextNode(body.slice(mark.start, mark.end)));
            frag.appendChild(el);

            cursor = mark.end;
        }

        if (cursor < body.length) {
            frag.appendChild(document.createTextNode(body.slice(cursor)));
        }

        if (!body) {
            frag.appendChild(document.createTextNode(""));
        }

        layer.replaceChildren(frag);
        syncNotepadHighlightScroll(win);
    }

    function markNotepadSelection(color) {
        if (!notepadColorOk(color)) return;

        const textarea = notepadTextarea();
        if (!textarea) return;

        const start = textarea.selectionStart;
        const end = textarea.selectionEnd;

        if (!Number.isInteger(start) || !Number.isInteger(end) || end <= start) {
            setNotepadStatus(notepadTr("notepad.status.select_text_first", "Select text first"));
            textarea.focus();
            return;
        }

        notepadMarks = normalizeNotepadMarks(notepadMarks, textarea.value)
            .filter(mark => mark.end <= start || mark.start >= end);

        notepadMarks.push({ start, end, color });
        notepadMarks = normalizeNotepadMarks(notepadMarks, textarea.value);

        notepadDirty = true;
        renderNotepadHighlights();
        scheduleNotepadSave();
        textarea.focus();
    }

    function clearNotepadMarksInSelection() {
        const textarea = notepadTextarea();
        if (!textarea) return;

        const start = textarea.selectionStart;
        const end = textarea.selectionEnd;

        if (!Number.isInteger(start) || !Number.isInteger(end) || end <= start) {
            setNotepadStatus(notepadTr("notepad.status.select_highlighted_text_first", "Select highlighted text first"));
            textarea.focus();
            return;
        }

        const before = notepadMarks.length;
        notepadMarks = normalizeNotepadMarks(notepadMarks, textarea.value)
            .filter(mark => mark.end <= start || mark.start >= end);

        if (notepadMarks.length !== before) {
            notepadDirty = true;
            renderNotepadHighlights();
            scheduleNotepadSave();
        }

        textarea.focus();
    }

    function updateNotepadToolbarActive(win = null) {
        const root = notepadRoot(win);
        if (!root) return;

        for (const btn of root.querySelectorAll(".notepadMarkBtn")) {
            btn.classList.toggle("active", btn.dataset.color === notepadActiveMarkColor);
        }
    }

    function refreshNotepadI18n(win = null) {
        const root = notepadRoot(win);
        if (!root) return;

        const closeBtn = root.querySelector(".notepadWindowBtn");
        if (closeBtn) {
            const closeText = notepadTr("notepad.close", "Close Notepad");
            closeBtn.title = closeText;
            closeBtn.setAttribute("aria-label", closeText);
        }

        const toolLabel = root.querySelector(".notepadToolLabel");
        if (toolLabel) {
            toolLabel.textContent = notepadTr("notepad.highlight_label", "Highlight:");
        }

        for (const btn of root.querySelectorAll(".notepadMarkBtn")) {
            const color = btn.dataset.color || "";
            if (!notepadColorOk(color)) continue;

            const colorLabel = notepadColorLabel(color);
            const title = notepadTr(
                "notepad.highlight_color",
                `Highlight ${colorLabel}`,
                { color: colorLabel }
            );
            btn.title = title;
            btn.setAttribute("aria-label", title);
        }

        const clearBtn = root.querySelector(".notepadClearMarkBtn");
        if (clearBtn) {
            clearBtn.textContent = notepadTr("notepad.clear", "Clear");
            const clearTitle = notepadTr("notepad.clear_highlight", "Clear highlight from selected text");
            clearBtn.title = clearTitle;
            clearBtn.setAttribute("aria-label", clearTitle);
        }

        const textarea = root.querySelector(".notepadText");
        if (textarea) {
            textarea.placeholder = notepadTr("notepad.placeholder", "Quick note...");
        }
    }

    async function loadNotepadFromServer(win = null) {
        if (!authed) return null;
        if (notepadLoadInFlight) return notepadLoadInFlight;

        const textarea = notepadTextarea(win);

        setNotepadStatus(notepadTr("notepad.status.loading", "Loading..."));

        notepadLoadInFlight = (async () => {
            try {
                const r = await fetch("/api/v4/notepad", {
                    credentials: "include",
                    cache: "no-store",
                    headers: { "Accept": "application/json" }
                });

                const j = await r.json().catch(() => null);
                if (!r.ok || !j || j.ok === false) {
                    throw new Error((j && (j.message || j.error)) ? (j.message || j.error) : `HTTP ${r.status}`);
                }

                notepadRevision = Number.isFinite(Number(j.revision)) ? Number(j.revision) : 0;
                notepadWindowDraft = typeof j.body === "string" ? j.body : "";
                notepadMarks = normalizeNotepadMarks(j.marks, notepadWindowDraft);
                notepadLoaded = true;

                if (textarea && !notepadDirty) {
                    textarea.value = notepadWindowDraft;
                }

                renderNotepadHighlights(win);
                setNotepadStatus(notepadRevision > 0 ? notepadTr("notepad.status.saved", "Saved") : notepadTr("notepad.status.ready", "Ready"));
                return j;
            } catch (e) {
                setNotepadStatus(notepadTr("notepad.status.load_failed", "Load failed"));
                throw e;
            } finally {
                notepadLoadInFlight = null;
            }
        })();

        return notepadLoadInFlight;
    }

    function scheduleNotepadSave(delayMs = 1000) {
        if (!authed || !notepadLoaded) return;

        if (notepadSaveTimer) {
            window.clearTimeout(notepadSaveTimer);
            notepadSaveTimer = null;
        }

        setNotepadStatus(notepadTr("notepad.status.unsaved_changes", "Unsaved changes"));

        notepadSaveTimer = window.setTimeout(() => {
            notepadSaveTimer = null;
            notepadSaveNow().catch(() => {});
        }, delayMs);
    }

    async function notepadSaveNow() {
        if (!authed || !notepadLoaded || !notepadDirty) return;

        if (notepadSaving) {
            notepadSavePending = true;
            return;
        }

        const textarea = notepadTextarea();
        if (!textarea) return;

        notepadSaving = true;
        notepadSavePending = false;
        setNotepadStatus(notepadTr("notepad.status.saving", "Saving..."));

        const body = textarea.value;
        const marks = normalizeNotepadMarks(notepadMarks, body);

        try {
            const r = await fetch("/api/v4/notepad", {
                method: "POST",
                credentials: "include",
                cache: "no-store",
                headers: {
                    "Content-Type": "application/json",
                    "Accept": "application/json"
                },
                body: JSON.stringify({
                    body,
                    marks,
                    revision: notepadRevision
                })
            });

            const j = await r.json().catch(() => null);

            if (r.status === 409 && j && j.error === "revision_mismatch") {
                if (j.current && Number.isFinite(Number(j.current.revision))) {
                    notepadRevision = Number(j.current.revision);
                }
                setNotepadStatus(notepadTr("notepad.status.changed_elsewhere", "Changed elsewhere. Copy your text, then reload Notepad."));
                return;
            }

            if (!r.ok || !j || j.ok === false) {
                throw new Error((j && (j.message || j.error)) ? (j.message || j.error) : `HTTP ${r.status}`);
            }

            notepadRevision = Number.isFinite(Number(j.revision)) ? Number(j.revision) : notepadRevision;
            notepadWindowDraft = body;
            notepadMarks = normalizeNotepadMarks(j.marks || marks, body);
            notepadDirty = false;
            renderNotepadHighlights();
            setNotepadStatus(notepadTr("notepad.status.saved", "Saved"));
        } catch (e) {
            setNotepadStatus(notepadTr("notepad.status.save_failed", "Save failed"));
            throw e;
        } finally {
            notepadSaving = false;
            if (notepadSavePending) {
                scheduleNotepadSave(250);
            }
        }
    }

    function applyNotepadWindowState(win) {
        if (!win) return;

        const st = readNotepadUiState();
        const vw = Math.max(320, window.innerWidth || 1024);
        const vh = Math.max(320, window.innerHeight || 768);

        const width = notepadClamp(st.width, 300, Math.max(300, vw - 32));
        const height = notepadClamp(st.height, 240, Math.max(240, vh - 72));
        const left = notepadClamp(st.left, 8, Math.max(8, vw - width - 8));
        const top = notepadClamp(st.top, 8, Math.max(8, vh - height - 8));

        win.style.width = `${width}px`;
        win.style.height = `${height}px`;
        win.style.left = `${left}px`;
        win.style.top = `${top}px`;
    }

    function saveNotepadWindowRect(win, openState = null) {
        if (!win) return;

        const rect = win.getBoundingClientRect();
        const patch = {
            left: Math.round(rect.left),
            top: Math.round(rect.top),
            width: Math.round(rect.width),
            height: Math.round(rect.height)
        };

        if (typeof openState === "boolean") patch.open = openState;

        writeNotepadUiState(patch);
    }

    function bringNotepadWindowToFront(win) {
        if (!win) return;
        notepadWindowZ += 1;
        win.style.zIndex = String(notepadWindowZ);
    }

    function attachNotepadWindowDrag(win, handle) {
        if (!win || !handle || handle.dataset.notepadDragReady === "1") return;
        handle.dataset.notepadDragReady = "1";

        let dragging = false;
        let startX = 0;
        let startY = 0;
        let baseLeft = 0;
        let baseTop = 0;

        const onMove = (ev) => {
            if (!dragging) return;
            ev.preventDefault();

            const rect = win.getBoundingClientRect();
            const vw = Math.max(320, window.innerWidth || 1024);
            const vh = Math.max(320, window.innerHeight || 768);

            const nextLeft = notepadClamp(baseLeft + ev.clientX - startX, 8, Math.max(8, vw - rect.width - 8));
            const nextTop = notepadClamp(baseTop + ev.clientY - startY, 8, Math.max(8, vh - rect.height - 8));

            win.style.left = `${nextLeft}px`;
            win.style.top = `${nextTop}px`;
        };

        const onUp = (ev) => {
            if (!dragging) return;
            dragging = false;
            try { handle.releasePointerCapture(ev.pointerId); } catch {}
            saveNotepadWindowRect(win, true);
        };

        handle.addEventListener("pointerdown", (ev) => {
            if (ev.button !== 0) return;
            if (ev.target && ev.target.closest && ev.target.closest("button")) return;

            dragging = true;
            bringNotepadWindowToFront(win);

            const rect = win.getBoundingClientRect();
            startX = ev.clientX;
            startY = ev.clientY;
            baseLeft = rect.left;
            baseTop = rect.top;

            try { handle.setPointerCapture(ev.pointerId); } catch {}
            ev.preventDefault();
        });

        handle.addEventListener("pointermove", onMove);
        handle.addEventListener("pointerup", onUp);
        handle.addEventListener("pointercancel", onUp);
    }

    function ensureNotepadWindow() {
        const existing = document.getElementById("notepadFloatingWindow");
        if (existing) return existing;

        const win = document.createElement("section");
        win.id = "notepadFloatingWindow";
        win.className = "notepadFloating";
        win.hidden = true;
        win.setAttribute("role", "dialog");
        win.setAttribute("aria-label", NOTEPAD_TITLE);

        const titlebar = document.createElement("div");
        titlebar.className = "notepadTitlebar";

        const title = document.createElement("div");
        title.className = "notepadTitle";
        title.textContent = NOTEPAD_TITLE;

        const closeBtn = document.createElement("button");
        closeBtn.type = "button";
        closeBtn.className = "notepadWindowBtn";
        closeBtn.textContent = "×";
        closeBtn.title = notepadTr("notepad.close", "Close Notepad");
        closeBtn.setAttribute("aria-label", notepadTr("notepad.close", "Close Notepad"));

        titlebar.appendChild(title);
        titlebar.appendChild(closeBtn);

        const body = document.createElement("div");
        body.className = "notepadBody";

        const toolbar = document.createElement("div");
        toolbar.className = "notepadToolbar";

        const toolLabel = document.createElement("span");
        toolLabel.className = "notepadToolLabel";
        toolLabel.textContent = notepadTr("notepad.highlight_label", "Highlight:");
        toolbar.appendChild(toolLabel);

        for (const color of NOTEPAD_MARK_COLORS) {
            const btn = document.createElement("button");
            btn.type = "button";
            btn.className = `notepadMarkBtn ${NOTEPAD_MARK_CLASS[color] || ""}`;
            btn.dataset.color = color;
            btn.title = notepadTr("notepad.highlight_color", `Highlight ${notepadColorLabel(color)}`, { color: notepadColorLabel(color) });
            btn.setAttribute("aria-label", notepadTr("notepad.highlight_color", `Highlight ${notepadColorLabel(color)}`, { color: notepadColorLabel(color) }));
            btn.textContent = "●";
            btn.addEventListener("mousedown", (ev) => ev.preventDefault());
            btn.addEventListener("click", () => {
                notepadActiveMarkColor = color;
                updateNotepadToolbarActive(win);
                markNotepadSelection(color);
            });
            toolbar.appendChild(btn);
        }

        const clearBtn = document.createElement("button");
        clearBtn.type = "button";
        clearBtn.className = "notepadClearMarkBtn";
        clearBtn.textContent = notepadTr("notepad.clear", "Clear");
        clearBtn.title = notepadTr("notepad.clear_highlight", "Clear highlight from selected text");
        clearBtn.setAttribute("aria-label", notepadTr("notepad.clear_highlight", "Clear highlight from selected text"));
        clearBtn.addEventListener("mousedown", (ev) => ev.preventDefault());
        clearBtn.addEventListener("click", () => clearNotepadMarksInSelection());
        toolbar.appendChild(clearBtn);

        const editorWrap = document.createElement("div");
        editorWrap.className = "notepadEditorWrap";

        const highlightLayer = document.createElement("div");
        highlightLayer.className = "notepadHighlightLayer";
        highlightLayer.setAttribute("aria-hidden", "true");

        const textarea = document.createElement("textarea");
        textarea.className = "notepadText";
        textarea.maxLength = NOTEPAD_DRAFT_MAX_CHARS;
        textarea.spellcheck = false;
        textarea.setAttribute("spellcheck", "false");
        textarea.setAttribute("autocomplete", "off");
        textarea.setAttribute("autocorrect", "off");
        textarea.setAttribute("autocapitalize", "off");
        textarea.placeholder = notepadTr("notepad.placeholder", "Quick note...");
        textarea.setAttribute("aria-label", "Notepad text");
        textarea.value = notepadWindowDraft;

        textarea.addEventListener("input", () => {
            notepadWindowDraft = textarea.value;
            notepadMarks = normalizeNotepadMarks(notepadMarks, textarea.value);
            notepadDirty = true;
            renderNotepadHighlights(win);
            scheduleNotepadSave();
        });

        textarea.addEventListener("scroll", () => syncNotepadHighlightScroll(win));

        const status = document.createElement("div");
        status.className = "notepadStatus";
        status.textContent = notepadTr("notepad.status.loading", "Loading...");

        editorWrap.appendChild(highlightLayer);
        editorWrap.appendChild(textarea);

        body.appendChild(toolbar);
        body.appendChild(editorWrap);
        body.appendChild(status);

        win.appendChild(titlebar);
        win.appendChild(body);
        document.body.appendChild(win);

        applyNotepadWindowState(win);
        attachNotepadWindowDrag(win, titlebar);
        updateNotepadToolbarActive(win);
        refreshNotepadI18n(win);
        renderNotepadHighlights(win);

        closeBtn.addEventListener("click", () => {
            closeNotepadWindow();
        });

        win.addEventListener("pointerdown", () => {
            bringNotepadWindowToFront(win);
        });

        return win;
    }

    function openNotepadWindow(options = {}) {
        if (!authed) return;

        const win = ensureNotepadWindow();
        win.hidden = false;
        refreshNotepadI18n(win);
        bringNotepadWindowToFront(win);
        saveNotepadWindowRect(win, true);

        setActiveNav("");
        setActiveApp(NOTEPAD_APP_ID);

        if (options.focus !== false) {
            const textarea = notepadTextarea(win);
            if (textarea) textarea.focus();
        }

        loadNotepadFromServer(win).catch(() => {});
    }

    function closeNotepadWindow() {
        const win = document.getElementById("notepadFloatingWindow");
        if (!win) return;

        if (notepadSaveTimer) {
            window.clearTimeout(notepadSaveTimer);
            notepadSaveTimer = null;
        }
        notepadSaveNow().catch(() => {});

        saveNotepadWindowRect(win, false);
        win.hidden = true;

        if (currentApp && currentApp.id) {
            setActiveApp(currentApp.id);
        } else {
            setActiveApp("");
        }
    }

    function restoreNotepadWindowIfOpen() {
        if (notepadRestored || !authed) return;
        notepadRestored = true;

        const st = readNotepadUiState();
        if (st && st.open === true) {
            openNotepadWindow({ focus: false });
        }
    }

    function addNotepadNavButton() {
        if (!appsList) return;
        if (appsList.querySelector(`[data-appid="${NOTEPAD_APP_ID}"]`)) return;

        const a = document.createElement("a");
        a.className = "navbtn";
        a.href = "#notepad";
        a.dataset.appid = NOTEPAD_APP_ID;
        a.title = NOTEPAD_TITLE;

        const left = document.createElement("span");
        left.textContent = NOTEPAD_TITLE;

        const icon = document.createElement("span");
        icon.className = "k appNavIcon";

        const fallback = document.createElement("span");
        fallback.className = "appNavIconFallback";
        fallback.textContent = "NP";

        icon.appendChild(fallback);
        a.appendChild(left);
        a.appendChild(icon);

        a.addEventListener("click", (ev) => {
            ev.preventDefault();
            openNotepadWindow();
        });

        appsList.appendChild(a);
    }

    function ensureNotepadDesktopLayout(surface, apps) {
        if (!surface) return;
        if (!desktopLayout) loadDesktopLayout();
        if (!desktopLayout.items || typeof desktopLayout.items !== "object") {
            desktopLayout.items = {};
        }

        const rect = surface.getBoundingClientRect();
        const pad = 14;
        const colW = Math.max(92, DESKTOP_GRID_X * 5);
        const rowH = Math.max(112, DESKTOP_GRID_Y * 5);
        const cols = Math.max(1, Math.floor(Math.max(colW, rect.width - pad) / colW));

        const cellForPos = (pos) => {
            const x = Number(pos && pos.x);
            const y = Number(pos && pos.y);
            const col = Math.max(0, Math.round(((Number.isFinite(x) ? x : pad) - pad) / colW));
            const row = Math.max(0, Math.round(((Number.isFinite(y) ? y : pad) - pad) / rowH));
            return row * cols + col;
        };

        const posForCell = (cell) => {
            const col = cell % cols;
            const row = Math.floor(cell / cols);
            return {
                x: pad + col * colW,
                y: pad + row * rowH
            };
        };

        const occupied = new Set();
        for (const app of apps || []) {
            const key = layoutKeyFor(app);
            const pos = key ? desktopLayout.items[key] : null;
            if (pos) occupied.add(cellForPos(pos));
        }

        let changed = false;
        const current = desktopLayout.items[NOTEPAD_LAYOUT_KEY];

        if (current && !occupied.has(cellForPos(current))) {
            occupied.add(cellForPos(current));
        } else {
            let cell = 0;
            while (occupied.has(cell)) cell++;
            desktopLayout.items[NOTEPAD_LAYOUT_KEY] = posForCell(cell);
            changed = true;
        }

        if (changed) saveDesktopLayout();
    }

    function attachNotepadDesktopDrag(iconEl) {
        const surface = getDesktopSurface();
        if (!surface || !iconEl || iconEl.dataset.notepadDragReady === "1") return;
        iconEl.dataset.notepadDragReady = "1";

        let dragging = false;
        let startX = 0;
        let startY = 0;
        let baseX = 0;
        let baseY = 0;

        const onMove = (ev) => {
            if (!dragging) return;
            ev.preventDefault();

            const rect = surface.getBoundingClientRect();
            const iconW = iconEl.offsetWidth || 92;
            const iconH = iconEl.offsetHeight || 92;

            const x = notepadClamp(baseX + ev.clientX - startX, 6, Math.max(6, rect.width - iconW - 6));
            const y = notepadClamp(baseY + ev.clientY - startY, 6, Math.max(6, rect.height - iconH - 6));

            iconEl.style.left = `${x}px`;
            iconEl.style.top = `${y}px`;

            if (!desktopLayout) loadDesktopLayout();
            desktopLayout.items[NOTEPAD_LAYOUT_KEY] = { x, y };
        };

        const onUp = (ev) => {
            if (!dragging) return;
            dragging = false;

            const left = parseFloat(iconEl.style.left || "0") || 0;
            const top = parseFloat(iconEl.style.top || "0") || 0;
            const snapped = snapToGrid(left, top);

            iconEl.style.left = `${snapped.x}px`;
            iconEl.style.top = `${snapped.y}px`;

            if (!desktopLayout) loadDesktopLayout();
            desktopLayout.items[NOTEPAD_LAYOUT_KEY] = snapped;
            saveDesktopLayout();

            try { iconEl.releasePointerCapture(ev.pointerId); } catch {}
        };

        iconEl.addEventListener("pointerdown", (ev) => {
            if (ev.button !== 0) return;

            setSelectedIcon(NOTEPAD_LAYOUT_KEY, ev.ctrlKey || ev.metaKey);

            dragging = true;
            startX = ev.clientX;
            startY = ev.clientY;
            baseX = parseFloat(iconEl.style.left || "0") || 0;
            baseY = parseFloat(iconEl.style.top || "0") || 0;

            try { iconEl.setPointerCapture(ev.pointerId); } catch {}
            ev.preventDefault();
        });

        iconEl.addEventListener("pointermove", onMove);
        iconEl.addEventListener("pointerup", onUp);
        iconEl.addEventListener("pointercancel", onUp);
    }

    let notepadI18nObserverStarted = false;

    function startNotepadI18nObserver() {
        if (notepadI18nObserverStarted) return;
        notepadI18nObserverStarted = true;

        const refresh = () => {
            const win = document.getElementById("notepadFloatingWindow");
            if (win) refreshNotepadI18n(win);
        };

        try {
            const observer = new MutationObserver(refresh);
            observer.observe(document.documentElement, {
                attributes: true,
                attributeFilter: ["lang", "data-i18n-ready", "data-i18n-pending"]
            });
        } catch {}

        window.addEventListener("focus", refresh);
        window.setTimeout(refresh, 0);
        window.setTimeout(refresh, 250);
        window.setTimeout(refresh, 1000);
    }

    startNotepadI18nObserver();

    function renderNotepadDesktopIcon(surface, apps) {
        if (!surface || !authed) return;

        ensureNotepadDesktopLayout(surface, apps);

        const pos =
            desktopLayout &&
            desktopLayout.items &&
            desktopLayout.items[NOTEPAD_LAYOUT_KEY]
                ? desktopLayout.items[NOTEPAD_LAYOUT_KEY]
                : { x: 14, y: 14 };

        const el = document.createElement("div");
        el.className = "desktopIcon";
        el.dataset.key = NOTEPAD_LAYOUT_KEY;
        el.dataset.appid = NOTEPAD_APP_ID;
        el.style.left = `${pos.x}px`;
        el.style.top = `${pos.y}px`;

        const glyph = document.createElement("div");
        glyph.className = "notepadDesktopGlyph";
        glyph.setAttribute("aria-hidden", "true");
        glyph.textContent = "N";

        const label = document.createElement("div");
        label.className = "label";
        label.textContent = NOTEPAD_TITLE;

        const sub = document.createElement("div");
        sub.className = "sub";
        sub.textContent = "quick";

        el.appendChild(glyph);
        el.appendChild(label);
        el.appendChild(sub);

        el.addEventListener("click", (ev) => {
            ev.preventDefault();
            setSelectedIcon(NOTEPAD_LAYOUT_KEY, ev.ctrlKey || ev.metaKey);
        });

        el.addEventListener("dblclick", (ev) => {
            ev.preventDefault();
            openNotepadWindow();
        });

        attachNotepadDesktopDrag(el);
        surface.appendChild(el);
    }
    function currentThemeName() {
        // Try data-theme set by your theme system, then localStorage, then default
        const dt = (document.documentElement.getAttribute("data-theme") || "").trim();
        if (dt) return dt;
        try {
            const v = (localStorage.getItem("pqnas_theme") || "").trim();
            if (v) return v;
        } catch {}
        return "dark";
    }

    const USER_THEME_OPTIONS = [
        {
            id: "dark",
            title: "DNA Dark",
            desc: "Default dark theme."
        },
        {
            id: "bright",
            title: "Bright",
            desc: "Light / bright theme."
        },
        {
            id: "cpunk_orange",
            title: "Orange",
            desc: "Cyberpunk orange theme."
        },
        {
            id: "win_classic",
            title: "Win Classic",
            desc: "Retro desktop-style theme."
        }
    ];

    function applyUserTheme(themeName, opts = {}) {
        const persist = opts.persist !== false;
        const rerender = opts.rerender !== false;

        // Save/migrate the current desktop layout before the theme changes.
        // Without this, old theme-specific layouts can appear to "win" during
        // theme switching.
        try {
            loadDesktopLayout();
            saveDesktopLayout();
        } catch {}

        // Important:
        // If an old build stored desktop layout per theme, migrate/save the
        // current theme's layout BEFORE changing data-theme/localStorage.
        // Otherwise theme switching can migrate the target theme's old/default
        // layout and make icons appear to reset.
        try {
            loadDesktopLayout();
            saveDesktopLayout();
        } catch {}

        const theme = String(themeName || "dark").trim() || "dark";

        try {
            if (persist) localStorage.setItem("pqnas_theme", theme);
        } catch {}

        document.documentElement.setAttribute("data-theme", theme);
        document.body.setAttribute("data-theme", theme);

        try {
            if (window.PQNAS_THEME && typeof window.PQNAS_THEME.apply === "function") {
                window.PQNAS_THEME.apply(theme);
            }
        } catch {}

        try {
            if (typeof window.pqnasApplyTheme === "function") {
                window.pqnasApplyTheme(theme);
            }
        } catch {}

        try {
            window.dispatchEvent(new CustomEvent("pqnas-theme-changed", {
                detail: { theme }
            }));
        } catch {}

        manifestCache.clear();
        clearCachedAppFrames();

        if (rerender) {
            loadDesktopLayout();

            if (currentView === "home") {
                renderDesktopIcons();
            }

            if (currentView === "user_settings") {
                renderUserSettings(tr("settings.theme.saved", null, "Theme saved."), "ok");
            }
        }
    }

    function desktopStorageKey() {
        const fp = meFpHex ? meFpHex : "anon";

        // Desktop icon layout is user/browser specific, not theme specific.
        // Do NOT include currentThemeName() here, otherwise every theme gets
        // its own icon positions.
        return `pqnas_desktop_layout_v3::${fp}`;
    }

    function legacyDesktopStorageKeys() {
        const fp = meFpHex ? meFpHex : "anon";
        const current = currentThemeName();

        const keys = [];

        // Prefer the currently visible old theme-specific layout for one-time migration.
        if (current) keys.push(`pqnas_desktop_layout_v1::${fp}::${current}`);

        // Then try the previous attempted shared key.
        keys.push(`pqnas_desktop_layout_v2::${fp}`);

        // Then try known old theme-specific keys.
        for (const t of ["dark", "bright", "cpunk_orange", "win_classic"]) {
            keys.push(`pqnas_desktop_layout_v1::${fp}::${t}`);
        }

        // Finally include any other matching old layout keys.
        try {
            for (let i = 0; i < localStorage.length; i++) {
                const k = localStorage.key(i);
                if (!k) continue;

                if (
                    k.startsWith(`pqnas_desktop_layout_v1::${fp}::`) ||
                    k === `pqnas_desktop_layout_v2::${fp}`
                ) {
                    keys.push(k);
                }
            }
        } catch {}

        return Array.from(new Set(keys));
    }

    function bindDesktopSurfaceOnce()
    {
        const surface = getDesktopSurface();
        if (!surface) return;
        if (surface.dataset.bound === "1") return;
        surface.dataset.bound = "1";

        surface.addEventListener("pointerdown", (ev) =>
        {
            if (ev.target !== surface) return;

            selectionActive = true;

            selectionStartX = ev.offsetX;
            selectionStartY = ev.offsetY;

            selectionBox = document.createElement("div");
            selectionBox.className = "desktopSelectionBox";

            selectionBox.style.left = selectionStartX + "px";
            selectionBox.style.top = selectionStartY + "px";

            surface.appendChild(selectionBox);

            desktopSelectedKeys.clear();
            updateSelectionVisual();
        });

        surface.addEventListener("pointermove", (ev) =>
        {
            if (!selectionActive) return;

            const x = ev.offsetX;
            const y = ev.offsetY;

            const left = Math.min(x, selectionStartX);
            const top  = Math.min(y, selectionStartY);

            const width  = Math.abs(x - selectionStartX);
            const height = Math.abs(y - selectionStartY);

            selectionBox.style.left = left + "px";
            selectionBox.style.top  = top + "px";
            selectionBox.style.width  = width + "px";
            selectionBox.style.height = height + "px";

            selectIconsInRect(left, top, width, height);
        });

        surface.addEventListener("pointerup", () =>
        {
            selectionActive = false;

            if (selectionBox)
            {
                selectionBox.remove();
                selectionBox = null;
            }
        });
    }

    function selectIconsInRect(left, top, width, height)
    {
        const surface = getDesktopSurface();
        if (!surface) return;

        const rect =
            {
                left,
                right: left + width,
                top,
                bottom: top + height
            };

        desktopSelectedKeys.clear();

        for (const el of surface.querySelectorAll(".desktopIcon"))
        {
            const x = parseFloat(el.style.left || "0");
            const y = parseFloat(el.style.top  || "0");

            const w = el.offsetWidth;
            const h = el.offsetHeight;

            if (
                x < rect.right &&
                x + w > rect.left &&
                y < rect.bottom &&
                y + h > rect.top
            )
            {
                desktopSelectedKeys.add(el.dataset.key);
            }
        }

        updateSelectionVisual();
    }

    function updateSelectionVisual()
    {
        const surface = getDesktopSurface();
        if (!surface) return;

        for (const el of surface.querySelectorAll(".desktopIcon"))
        {
            el.classList.toggle("selected",
                desktopSelectedKeys.has(el.dataset.key));
        }
    }

    function loadDesktopLayout() {
        const k = desktopStorageKey();

        try {
            const raw = localStorage.getItem(k);
            desktopLayout = raw ? JSON.parse(raw) : null;

            // One-time migration from older layouts.
            // New key is v3 and is deliberately not theme-specific.
            if (!desktopLayout) {
                for (const oldKey of legacyDesktopStorageKeys()) {
                    const oldRaw = localStorage.getItem(oldKey);
                    if (!oldRaw) continue;

                    const oldLayout = JSON.parse(oldRaw);
                    if (
                        oldLayout &&
                        typeof oldLayout === "object" &&
                        oldLayout.items &&
                        typeof oldLayout.items === "object"
                    ) {
                        desktopLayout = oldLayout;
                        localStorage.setItem(k, JSON.stringify(desktopLayout));
                        break;
                    }
                }
            }

            if (!desktopLayout) desktopLayout = { items: {} };
        } catch {
            desktopLayout = { items: {} };
        }

        if (!desktopLayout || typeof desktopLayout !== "object") desktopLayout = { items: {} };
        if (!desktopLayout.items || typeof desktopLayout.items !== "object") desktopLayout.items = {};
        return desktopLayout;
    }

    function saveDesktopLayout() {
        const k = desktopStorageKey();
        try { localStorage.setItem(k, JSON.stringify(desktopLayout || { items: {} })); } catch {}
    }

    function getDesktopSurface() {
        return document.getElementById("desktopSurface");
    }

    async function fetchManifest(id, ver) {
        const key = `${id}@${ver}`;
        if (manifestCache.has(key)) return manifestCache.get(key);

        // Try the served manifest at app root
        const url = `/apps/${encodeURIComponent(id)}/${encodeURIComponent(ver)}/manifest.json`;
        try {
            const r = await fetch(url, { credentials: "include", cache: "no-store" });
            const j = await r.json().catch(() => null);
            if (r.ok && j && typeof j === "object") {
                manifestCache.set(key, j);
                return j;
            }
        } catch {}
        manifestCache.set(key, null);
        return null;
    }

    function manifestHasSurfaces(mani) {
        return !!(mani && typeof mani === "object" && mani.surfaces && typeof mani.surfaces === "object");
    }

    function manifestSurfaceEnabled(mani, surfaceName, fallbackEnabled) {
        if (!mani || typeof mani !== "object") return !!fallbackEnabled;

        const surfaces = mani.surfaces;
        if (!surfaces || typeof surfaces !== "object") return !!fallbackEnabled;

        const value = surfaces[surfaceName];

        if (typeof value === "boolean") return value;

        if (value && typeof value === "object" && typeof value.enabled === "boolean") {
            return value.enabled;
        }

        return !!fallbackEnabled;
    }

    function appDesktopEnabled(app, mani) {
        // Product decision: every installed app gets a desktop shortcut.
        // Server/API visibility still controls which apps the current user can see.
        return true;
    }

    function appSidebarEnabled(app, mani) {
        // Old manifests without "surfaces" stay visible in the sidebar.
        if (!manifestHasSurfaces(mani)) return true;

        // New manifests must opt in.
        return manifestSurfaceEnabled(mani, "sidebar", false);
    }

    function resolveIconUrl(app, mani) {
        const base = `/apps/${encodeURIComponent(app.id)}/${encodeURIComponent(app.ver)}/`;
        const bust = `?v=${encodeURIComponent(app.ver || "")}`;

        const withBust = (rel) => (base + rel + bust);

        if (mani && mani.icons && typeof mani.icons === "object") {
            const theme = currentThemeName();

            if (mani.icons[theme]) return withBust(mani.icons[theme]);

            const map = {
                "cpunk": "cpunk_orange",
                "orange": "cpunk_orange",
                "win": "win_classic",
                "classic": "win_classic"
            };
            if (map[theme] && mani.icons[map[theme]]) return withBust(mani.icons[map[theme]]);

            if (mani.icons.default) return withBust(mani.icons.default);

            const first = Object.values(mani.icons)[0];
            if (first) return withBust(first);
        }

        return base + "www/icon.png" + bust;
    }

    function resolveSidebarIconUrl(app, mani) {
        const base = `/apps/${encodeURIComponent(app.id)}/${encodeURIComponent(app.ver)}/`;
        const bust = `?v=${encodeURIComponent(app.ver || "")}`;
        const withBust = (rel) => base + String(rel || "").replace(/^\/+/, "") + bust;

        if (mani && mani.icons && typeof mani.icons === "object") {
            if (mani.icons.sidebar) return withBust(mani.icons.sidebar);
            if (mani.icons.nav) return withBust(mani.icons.nav);
        }

        if (mani && typeof mani.sidebar_icon === "string" && mani.sidebar_icon.trim()) {
            return withBust(mani.sidebar_icon.trim());
        }

        return withBust("www/nav_icon.svg");
    }

    function appUrl(app, hostMode = "") {
        const base = `/apps/${encodeURIComponent(app.id)}/${encodeURIComponent(app.ver)}/www/index.html`;
        if (!hostMode) return base;
        return `${base}?host=${encodeURIComponent(hostMode)}`;
    }

    function launchPolicyForAppId(appId) {
        const p = launchPolicyByAppId && launchPolicyByAppId[appId];
        return {
            default_launch: (p && p.default_launch) || "auto",
            window_profile: (p && p.window_profile) || "auto",
            allow_user_override: !!(p && p.allow_user_override),
            admin_only: !!(p && p.admin_only),
            show_in_sidebar: (p && typeof p.show_in_sidebar === "boolean") ? p.show_in_sidebar : undefined
        };
    }

    function appUserPrefsStorageKey() {
        const fp = meFpHex ? meFpHex : "anon";
        return `pqnas_app_user_prefs_v1::${fp}`;
    }

    function readLocalAppUserPrefs() {
        try {
            const raw = localStorage.getItem(appUserPrefsStorageKey());
            const j = raw ? JSON.parse(raw) : {};
            return j && typeof j === "object" ? j : {};
        } catch {
            return {};
        }
    }

    function writeLocalAppUserPrefs(prefs) {
        try {
            localStorage.setItem(appUserPrefsStorageKey(), JSON.stringify(prefs || {}));
        } catch {}
    }

    function loadAppUserPrefs() {
        return appUserPrefs && typeof appUserPrefs === "object" ? appUserPrefs : {};
    }

    async function loadAppUserPrefsFromServer(force = false) {
        if (!authed) {
            appUserPrefs = readLocalAppUserPrefs();
            appUserPrefsLoaded = true;
            return appUserPrefs;
        }

        if (appUserPrefsLoaded && !force) return appUserPrefs;

        if (appUserPrefsLoadInFlight && !force) {
            return appUserPrefsLoadInFlight;
        }

        appUserPrefsLoadInFlight = (async () => {
            try {
                const r = await fetch("/api/v4/user/app_prefs", {
                    credentials: "include",
                    cache: "no-store",
                    headers: { "Accept": "application/json" }
                });

                const j = await r.json().catch(() => null);
                if (!r.ok || !j || j.ok === false) {
                    throw new Error((j && (j.message || j.error)) ? (j.message || j.error) : `HTTP ${r.status}`);
                }

                const serverPrefs =
                    j.app_prefs && typeof j.app_prefs === "object"
                        ? j.app_prefs
                        : {};

                // Migration aid: if server has no prefs yet, keep old browser-local prefs
                // until the user presses Save. The Save writes them to the server.
                const localPrefs = readLocalAppUserPrefs();
                appUserPrefs = Object.keys(serverPrefs).length ? serverPrefs : localPrefs;
                writeLocalAppUserPrefs(appUserPrefs);
            } catch (_) {
                // Offline/old server fallback: keep local settings working.
                appUserPrefs = readLocalAppUserPrefs();
            } finally {
                appUserPrefsLoaded = true;
                appUserPrefsLoadInFlight = null;
            }

            return appUserPrefs;
        })();

        return appUserPrefsLoadInFlight;
    }

    function saveAppUserPrefs(prefs) {
        appUserPrefs = prefs && typeof prefs === "object" ? prefs : {};
        writeLocalAppUserPrefs(appUserPrefs);
    }

    async function saveAppUserPrefsToServer(prefs) {
        const bodyPrefs = prefs && typeof prefs === "object" ? prefs : {};

        const r = await fetch("/api/v4/user/app_prefs", {
            method: "POST",
            credentials: "include",
            cache: "no-store",
            headers: {
                "Content-Type": "application/json",
                "Accept": "application/json"
            },
            body: JSON.stringify({ app_prefs: bodyPrefs })
        });

        const j = await r.json().catch(() => null);
        if (!r.ok || !j || j.ok === false) {
            throw new Error((j && (j.message || j.error)) ? (j.message || j.error) : `HTTP ${r.status}`);
        }

        appUserPrefs = j.app_prefs && typeof j.app_prefs === "object" ? j.app_prefs : bodyPrefs;
        appUserPrefsLoaded = true;
        writeLocalAppUserPrefs(appUserPrefs);
        return appUserPrefs;
    }

    function appUserPrefForAppId(appId) {
        const all = loadAppUserPrefs();
        const p = all && all[appId];
        return p && typeof p === "object" ? p : {};
    }

    function effectiveLaunchPolicyForApp(app, mani = null) {
        const pol = launchPolicyForAppId(app.id);
        const pref = appUserPrefForAppId(app.id);
        const canOverride = !!pol.allow_user_override;
        const manifestSidebarDefault = appSidebarEnabled(app, mani);

        return {
            default_launch:
                canOverride && typeof pref.default_launch === "string"
                    ? pref.default_launch
                    : pol.default_launch,

            window_profile:
                canOverride && typeof pref.window_profile === "string"
                    ? pref.window_profile
                    : pol.window_profile,

            show_in_sidebar:
                canOverride && typeof pref.show_in_sidebar === "boolean"
                    ? pref.show_in_sidebar
                    : (
                        typeof pol.show_in_sidebar === "boolean"
                            ? pol.show_in_sidebar
                            : manifestSidebarDefault
                    ),

            allow_user_override: canOverride,
            admin_only: !!pol.admin_only
        };
    }

    function resolveLaunchMode(app) {
        const pol = effectiveLaunchPolicyForApp(app);
        if (pol.default_launch === "detached") return "detached";
        if (pol.default_launch === "embedded") return "embedded";
        return "embedded"; // auto for now
    }

    function popupFeaturesForProfile(profile) {
        const sw = Math.max(1024, window.screen?.availWidth || 1400);
        const sh = Math.max(700, window.screen?.availHeight || 900);

        let w = 1100;
        let h = 800;

        if (profile === "small") {
            w = Math.min(900, sw - 80);
            h = Math.min(700, sh - 80);
        } else if (profile === "large") {
            w = Math.min(1280, sw - 40);
            h = Math.min(920, sh - 60);
        } else if (profile === "full") {
            w = Math.max(1000, sw - 24);
            h = Math.max(700, sh - 40);
        } else {
            // auto + normal
            w = Math.min(1100, sw - 60);
            h = Math.min(800, sh - 80);
        }

        const left = Math.max(0, Math.round((sw - w) / 2));
        const top = Math.max(0, Math.round((sh - h) / 2));

        return [
            `width=${w}`,
            `height=${h}`,
            `left=${left}`,
            `top=${top}`,
            "resizable=yes",
            "scrollbars=yes"
        ].join(",");
    }

    function openAppDetached(app) {
        const pol = effectiveLaunchPolicyForApp(app);
        const url = appUrl(app, "window");
        const name = `pqnas_app_${app.id}`;
        const features = popupFeaturesForProfile(pol.window_profile || "auto");

        const win = window.open(url, name, features);
        if (!win) return false;

        try { win.focus(); } catch {}
        return true;
    }

    async function loadTrustedDevices() {
        try {
            const r = await fetch("/api/v5/app_devices", {
                credentials: "include",
                cache: "no-store"
            });
            const j = await r.json().catch(() => null);
            if (!r.ok || !j || !j.ok) {
                throw new Error((j && j.message) ? j.message : `HTTP ${r.status}`);
            }
            trustedDevices = Array.isArray(j.devices) ? j.devices : [];
            trustedDevicesError = "";
        } catch (e) {
            trustedDevices = [];
            trustedDevicesError = String(e && e.message ? e.message : e);
        }
    }
    async function openTrustedDevices(errorText = "") {
        await loadTrustedDevices();
        renderTrustedDevices(errorText);
    }
    async function cancelPairing(pairId) {
        const r = await fetch("/api/v5/app_pair/cancel", {
            method: "POST",
            credentials: "include",
            cache: "no-store",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ pair_id: pairId })
        });
        const j = await r.json().catch(() => null);
        if (!r.ok || !j || !j.ok) {
            throw new Error((j && j.message) ? j.message : `HTTP ${r.status}`);
        }
    }
    async function revokeTrustedDevice(deviceId) {
        const r = await fetch("/api/v5/app_devices/revoke", {
            method: "POST",
            credentials: "include",
            cache: "no-store",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ device_id: deviceId })
        });
        const j = await r.json().catch(() => null);
        if (!r.ok || !j || !j.ok) {
            throw new Error((j && j.message) ? j.message : `HTTP ${r.status}`);
        }
    }
    async function loadWorkspaceInvites() {
        // Retired: File Manager Shared Space invites now direct-add enabled members.
        workspaceInvites = [];
        workspaceInvitesError = "";
        updateWorkspaceInvitesNav();
        return;
        // Legacy shell-level workspace invites are disabled.
        // File Manager Shared Space invites now manage members directly.
        workspaceInvites = [];
        workspaceInvitesError = "";
        updateWorkspaceInvitesNav();
        updateHomeInvitesHint();
        return;

        if (!authed) {
            workspaceInvites = [];
            workspaceInvitesError = "";
            updateWorkspaceInvitesNav();
            updateHomeInvitesHint();
            return;
        }

        try {
            const r = await fetch("/api/v4/workspaces/invitations", {
                credentials: "include",
                cache: "no-store"
            });

            const j = await r.json().catch(() => null);
            if (!r.ok || !j || !j.ok) {
                throw new Error((j && (j.message || j.error)) ? (j.message || j.error) : `HTTP ${r.status}`);
            }

            workspaceInvites = Array.isArray(j.invitations) ? j.invitations : [];
            workspaceInvitesError = "";
        } catch (e) {
            workspaceInvites = [];
            workspaceInvitesError = String(e && e.message ? e.message : e);
        }

        updateWorkspaceInvitesNav();

        if (currentView === "workspace_invites") {
            renderWorkspaceInvites();
        } else if (currentView === "home") {
            updateHomeInvitesHint();
        }
    }

    async function acceptWorkspaceInvite(workspaceId) {
        const r = await fetch("/api/v4/workspaces/invitations/accept", {
            method: "POST",
            credentials: "include",
            cache: "no-store",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ workspace_id: workspaceId })
        });

        const j = await r.json().catch(() => null);
        if (!r.ok || !j || !j.ok) {
            throw new Error((j && (j.message || j.error)) ? `${j.error || ""} ${j.message || ""}`.trim() : `HTTP ${r.status}`);
        }

        return j;
    }

    async function declineWorkspaceInvite(workspaceId) {
        const r = await fetch("/api/v4/workspaces/invitations/decline", {
            method: "POST",
            credentials: "include",
            cache: "no-store",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ workspace_id: workspaceId })
        });

        const j = await r.json().catch(() => null);
        if (!r.ok || !j || !j.ok) {
            throw new Error((j && (j.message || j.error)) ? `${j.error || ""} ${j.message || ""}`.trim() : `HTTP ${r.status}`);
        }

        return j;
    }
    function escapeHtml(s) {
        // Security: escape HTML text with one regex/callback instead of chained replaceAll.
        return String(s == null ? "" : s).replace(/[&<>"\']/g, (c) => ({
            "&": "&amp;",
            "<": "&lt;",
            ">": "&gt;",
            '"': "&quot;",
            "'": "&#39;",
        }[c]));
    }
    async function apiUserGet(path) {
        const r = await fetch(path, {
            credentials: "include",
            headers: { "Accept": "application/json" },
            cache: "no-store"
        });

        const j = await r.json().catch(() => null);

        if (!r.ok || !j || !j.ok) {
            throw new Error((j && (j.message || j.error)) ? (j.message || j.error) : `HTTP ${r.status}`);
        }

        return j;
    }

    async function apiUserPost(path, body) {
        const r = await fetch(path, {
            method: "POST",
            credentials: "include",
            cache: "no-store",
            headers: {
                "Content-Type": "application/json",
                "Accept": "application/json"
            },
            body: JSON.stringify(body || {})
        });

        const j = await r.json().catch(() => null);

        if (!r.ok || !j || !j.ok) {
            throw new Error((j && (j.message || j.error)) ? (j.message || j.error) : `HTTP ${r.status}`);
        }

        return j;
    }

    function fileToBase64(file) {
        return new Promise((resolve, reject) => {
            const rd = new FileReader();

            rd.onload = () => {
                const s = String(rd.result || "");
                const comma = s.indexOf(",");
                resolve(comma >= 0 ? s.slice(comma + 1) : s);
            };

            rd.onerror = () => reject(new Error("failed to read file"));
            rd.readAsDataURL(file);
        });
    }
    function fmtBytesForAvatar(n) {
        const x = Number(n || 0);
        if (!Number.isFinite(x) || x <= 0) return "0 B";
        if (x < 1024) return `${x} B`;
        if (x < 1024 * 1024) return `${(x / 1024).toFixed(1)} KiB`;
        return `${(x / (1024 * 1024)).toFixed(2)} MiB`;
    }

    function canvasToBlobSafe(canvas, type, quality) {
        return new Promise((resolve, reject) => {
            canvas.toBlob((blob) => {
                if (!blob) {
                    reject(new Error(tr("settings.profile.avatar_conversion_failed", null, "avatar conversion failed")));
                    return;
                }
                resolve(blob);
            }, type, quality);
        });
    }

    function loadImageForAvatar(file) {
        return new Promise((resolve, reject) => {
            const url = URL.createObjectURL(file);
            const img = new Image();

            img.onload = () => {
                URL.revokeObjectURL(url);
                resolve(img);
            };

            img.onerror = () => {
                URL.revokeObjectURL(url);
                reject(new Error(tr("settings.profile.avatar_read_failed", null, "Could not read this image. Try PNG, JPEG, or WebP.")));
            };

            img.src = url;
        });
    }

    async function prepareAvatarUploadBlob(file) {
        if (!file) throw new Error(tr("settings.profile.no_avatar_selected", null, "No avatar file selected."));

        const originalMime = String(file.type || "").toLowerCase();

        const directlyAllowed =
            originalMime === "image/png" ||
            originalMime === "image/jpeg" ||
            originalMime === "image/webp";

        /*
          Small already-supported files can go as-is.
          Bigger files are resized/compressed below.
        */
        if (directlyAllowed && file.size <= USER_AVATAR_MAX_BYTES) {
            return {
                blob: file,
                mime: originalMime,
                note: tr("settings.profile.using_original_image", { size: fmtBytesForAvatar(file.size) }, `Using original image (${fmtBytesForAvatar(file.size)}).`)
            };
        }

        const img = await loadImageForAvatar(file);

        const srcW = img.naturalWidth || img.width || 0;
        const srcH = img.naturalHeight || img.height || 0;

        if (!srcW || !srcH) {
            throw new Error(tr("settings.profile.avatar_dims_failed", null, "Could not read image dimensions."));
        }

        const scale = Math.min(1, USER_AVATAR_MAX_DIM / Math.max(srcW, srcH));
        const dstW = Math.max(1, Math.round(srcW * scale));
        const dstH = Math.max(1, Math.round(srcH * scale));

        const canvas = document.createElement("canvas");
        canvas.width = dstW;
        canvas.height = dstH;

        const ctx = canvas.getContext("2d", { alpha: false });
        if (!ctx) throw new Error(tr("settings.profile.canvas_unavailable", null, "Canvas is not available for avatar resize."));

        /*
          White background avoids black/transparent artifacts when PNGs are converted
          to JPEG/WebP.
        */
        ctx.fillStyle = "#ffffff";
        ctx.fillRect(0, 0, dstW, dstH);
        ctx.drawImage(img, 0, 0, dstW, dstH);

        /*
          Use JPEG because your backend already allows image/jpeg everywhere.
          Step quality down until it fits the backend limit.
        */
        const qualities = [0.86, 0.78, 0.70, 0.62, 0.54, 0.46, 0.38];

        let best = null;

        for (const q of qualities) {
            const blob = await canvasToBlobSafe(canvas, "image/jpeg", q);
            best = blob;

            if (blob.size <= USER_AVATAR_TARGET_BYTES) {
                return {
                    blob,
                    mime: "image/jpeg",
                    note: `Resized ${srcW}×${srcH} → ${dstW}×${dstH}, ${fmtBytesForAvatar(file.size)} → ${fmtBytesForAvatar(blob.size)}.`
                };
            }
        }

        if (best && best.size <= USER_AVATAR_MAX_BYTES) {
            return {
                blob: best,
                mime: "image/jpeg",
                note: `Resized ${srcW}×${srcH} → ${dstW}×${dstH}, ${fmtBytesForAvatar(file.size)} → ${fmtBytesForAvatar(best.size)}.`
            };
        }

        throw new Error(
            `Avatar is still too large after resizing (${fmtBytesForAvatar(best ? best.size : file.size)}). Try a smaller image.`
        );
    }

    function avatarUrlWithBust(url) {
        const s = String(url || "").trim();
        if (!s) return "";

        const bust = userAvatarBust || Date.now();
        const sep = s.includes("?") ? "&" : "?";

        return `${s}${sep}v=${encodeURIComponent(String(bust))}`;
    }

    function middleEllipsis(s, keepLeft = 20, keepRight = 18) {
        const v = String(s == null ? "" : s);
        if (!v) return "";
        if (v.length <= keepLeft + keepRight + 3) return v;
        return `${v.slice(0, keepLeft)}...${v.slice(v.length - keepRight)}`;
    }

    function clamp(n, a, b) { return Math.max(a, Math.min(b, n)); }

    function layoutKeyFor(app) {
        // Desktop position belongs to the app identity, not to one installed version.
        // This keeps the icon in the same place when an app is upgraded.
        return String(app && app.id ? app.id : "");
    }

    function legacyLayoutKeyFor(app) {
        return `${app.id}@${app.ver}`;
    }

    function migrateLegacyDesktopLayoutKey(app) {
        if (!desktopLayout || !desktopLayout.items || !app || !app.id) return false;

        const key = layoutKeyFor(app);
        if (!key || desktopLayout.items[key]) return false;

        const exactLegacy = legacyLayoutKeyFor(app);
        if (desktopLayout.items[exactLegacy]) {
            desktopLayout.items[key] = desktopLayout.items[exactLegacy];
            return true;
        }

        const prefix = `${app.id}@`;
        const legacyKeys = Object.keys(desktopLayout.items)
            .filter((k) => k.startsWith(prefix))
            .sort((a, b) => a.localeCompare(b, undefined, { numeric: true, sensitivity: "base" }));

        if (legacyKeys.length) {
            desktopLayout.items[key] = desktopLayout.items[legacyKeys[legacyKeys.length - 1]];
            return true;
        }

        return false;
    }
    function snapToGrid(x, y) {

        return {
            x: Math.round(x / DESKTOP_GRID_X) * DESKTOP_GRID_X,
            y: Math.round(y / DESKTOP_GRID_Y) * DESKTOP_GRID_Y
        };
    }

    function ensureDefaultLayout(surface, apps) {
        if (!desktopLayout) loadDesktopLayout();
        if (!surface) return;

        const rect = surface.getBoundingClientRect();
        const pad = 14;
        const colW = Math.max(92, DESKTOP_GRID_X * 5);
        const rowH = Math.max(112, DESKTOP_GRID_Y * 5);
        const cols = Math.max(1, Math.floor(Math.max(colW, rect.width - pad) / colW));

        let changed = false;
        const occupied = new Set();

        const cellForPos = (pos) => {
            const x = Number(pos && pos.x);
            const y = Number(pos && pos.y);
            const col = Math.max(0, Math.round(((Number.isFinite(x) ? x : pad) - pad) / colW));
            const row = Math.max(0, Math.round(((Number.isFinite(y) ? y : pad) - pad) / rowH));
            return row * cols + col;
        };

        const posForCell = (cell) => {
            const col = cell % cols;
            const row = Math.floor(cell / cols);
            return {
                x: pad + col * colW,
                y: pad + row * rowH
            };
        };

        const firstFreeCell = () => {
            let cell = 0;
            while (occupied.has(cell)) cell++;
            return cell;
        };

        // Migrate old id@version desktop positions to the new stable id key.
        for (const app of apps) {
            if (migrateLegacyDesktopLayoutKey(app)) changed = true;
        }

        // Keep existing visible icons, but repair duplicates/collisions.
        for (const app of apps) {
            const k = layoutKeyFor(app);
            if (!k) continue;

            const pos = desktopLayout.items[k];
            if (!pos) continue;

            const cell = cellForPos(pos);
            if (occupied.has(cell)) {
                const free = firstFreeCell();
                desktopLayout.items[k] = posForCell(free);
                occupied.add(free);
                changed = true;
            } else {
                occupied.add(cell);
            }
        }

        // Place missing icons into the first free desktop grid cell.
        for (const app of apps) {
            const k = layoutKeyFor(app);
            if (!k || desktopLayout.items[k]) continue;

            const free = firstFreeCell();
            desktopLayout.items[k] = posForCell(free);
            occupied.add(free);
            changed = true;
        }

        if (changed) saveDesktopLayout();
    }

    function setSelectedIcon(key, additive=false)
    {
        if (!additive)
            desktopSelectedKeys.clear();

        if (key)
            desktopSelectedKeys.add(key);

        const surface = getDesktopSurface();
        if (!surface) return;

        for (const el of surface.querySelectorAll(".desktopIcon"))
        {
            el.classList.toggle("selected",
                desktopSelectedKeys.has(el.dataset.key));
        }
    }


    function attachDrag(iconEl, app) {
        const surface = getDesktopSurface();
        if (!surface) return;

        let dragging = false;
        let startX = 0, startY = 0;
        let baseX = 0, baseY = 0;

        const key = layoutKeyFor(app);


        // --- NEW: capture base positions for group drag (prevents drift) ---
        let dragKeys = null;                 // Set<string>
        let dragBase = new Map();            // key -> {x,y}

        const onMove = (ev) => {
            if (!dragging) return;
            ev.preventDefault();

            const dx = ev.clientX - startX;
            const dy = ev.clientY - startY;

            const rect = surface.getBoundingClientRect();
            const iconW = iconEl.offsetWidth || 92;
            const iconH = iconEl.offsetHeight || 92;

            let nx = baseX + dx;
            let ny = baseY + dy;

            // clamp within surface
            nx = clamp(nx, 6, Math.max(6, rect.width - iconW - 6));
            ny = clamp(ny, 6, Math.max(6, rect.height - iconH - 6));

            // free move while dragging (no snap yet)
            iconEl.style.left = `${nx}px`;
            iconEl.style.top  = `${ny}px`;

            if (!desktopLayout) loadDesktopLayout();

            // Move the whole selection together (raw, no snap)
            const keys = (dragKeys && dragKeys.size) ? dragKeys
                : ((desktopSelectedKeys && desktopSelectedKeys.size) ? desktopSelectedKeys : new Set([key]));

            for (const k of keys) {
                const el = surface.querySelector(`.desktopIcon[data-key="${CSS.escape(k)}"]`);
                if (!el) continue;

                const base = dragBase.get(k) || { x: 0, y: 0 };
                const x = base.x + dx;
                const y = base.y + dy;

                el.style.left = `${x}px`;
                el.style.top  = `${y}px`;

                desktopLayout.items[k] = { x, y };
            }




        };

        const onUp = (ev) => {
            if (!dragging) return;
            dragging = false;

            if (!desktopLayout) loadDesktopLayout();

            const keys = (dragKeys && dragKeys.size) ? dragKeys
                : ((desktopSelectedKeys && desktopSelectedKeys.size) ? desktopSelectedKeys : new Set([key]));

            // snap each selected icon to grid
            for (const k of keys) {
                const el = surface ? surface.querySelector(`.desktopIcon[data-key="${CSS.escape(k)}"]`) : null;
                if (!el) continue;

                const left = parseFloat(el.style.left || "0") || 0;
                const top  = parseFloat(el.style.top  || "0") || 0;

                const s = snapToGrid(left, top);
                el.style.left = `${s.x}px`;
                el.style.top  = `${s.y}px`;
                desktopLayout.items[k] = s;
            }

            iconEl.releasePointerCapture(ev.pointerId);
            dragKeys = null;
            dragBase.clear();
            saveDesktopLayout();
        };


        iconEl.addEventListener("pointerdown", (ev) => {
            // Only left click / primary
            if (ev.button !== 0) return;

            // If the clicked icon is not in current selection, select it (support ctrl/meta additive)
            if (!(desktopSelectedKeys && desktopSelectedKeys.has(key))) {
                setSelectedIcon(key, ev.ctrlKey || ev.metaKey);
            }

            dragging = true;
            iconEl.setPointerCapture(ev.pointerId);

            const left = parseFloat(iconEl.style.left || "0") || 0;
            const top = parseFloat(iconEl.style.top || "0") || 0;

            startX = ev.clientX;
            startY = ev.clientY;
            baseX = left;
            baseY = top;
            if (!desktopLayout) loadDesktopLayout();

            dragKeys = (desktopSelectedKeys && desktopSelectedKeys.size)
                ? new Set(desktopSelectedKeys)
                : new Set([key]);

            // capture starting positions once (prevents drift)
            dragBase.clear();
            for (const k of dragKeys) {
                const el = surface.querySelector(`.desktopIcon[data-key="${CSS.escape(k)}"]`);
                if (!el) continue;

                dragBase.set(k, {
                    x: parseFloat(el.style.left || "0") || 0,
                    y: parseFloat(el.style.top  || "0") || 0
                });
            }

            ev.preventDefault();
        });

        iconEl.addEventListener("pointermove", onMove);
        iconEl.addEventListener("pointerup", onUp);
        iconEl.addEventListener("pointercancel", onUp);
    }

    async function renderDesktopIcons() {
        const surface = getDesktopSurface();
        if (!surface) return;

        // Only render on home view
        if (currentView !== "home") return;

        surface.innerHTML = "";
        if (!authed) return;
        bindDesktopSurfaceOnce();

        // We show installed apps as icons.
        // New manifests can opt out of desktop with surfaces.desktop.enabled=false.
        // Old manifests without "surfaces" stay visible.
        const apps = installedApps.slice();
        const desktopApps = [];

        for (const app of apps) {
            const mani = await fetchManifest(app.id, app.ver);

            if (!appDesktopEnabled(app, mani)) {
                continue;
            }

            app._manifest = mani;
            desktopApps.push(app);
        }

        loadDesktopLayout();
        ensureDefaultLayout(surface, desktopApps);

        for (const app of desktopApps) {
            const key = layoutKeyFor(app);
            const pos = (desktopLayout && desktopLayout.items && desktopLayout.items[key]) || { x: 16, y: 16 };

            const el = document.createElement("div");
            el.className = "desktopIcon";
            el.dataset.key = key;
            el.style.left = `${pos.x}px`;
            el.style.top = `${pos.y}px`;

            const img = document.createElement("img");
            img.alt = (app.name || app.title || app.id || "App");
            img.draggable = false;

            // load icon from manifest (async)
            const mani = await fetchManifest(app.id, app.ver);
            img.src = resolveIconUrl(app, mani);

            const label = document.createElement("div");
            label.className = "label";
            label.textContent = app.name || app.title || app.id;

            const sub = document.createElement("div");
            sub.className = "sub";
            sub.textContent = app.ver;

            el.appendChild(img);
            el.appendChild(label);
            el.appendChild(sub);

            // single click selects
            el.addEventListener("click", (ev) => {
                ev.preventDefault();
                setSelectedIcon(key, ev.ctrlKey || ev.metaKey);
            });


            // double click opens
            el.addEventListener("dblclick", (ev) => {
                ev.preventDefault();
                openAppById(app.id);
            });

            attachDrag(el, app);

            surface.appendChild(el);
        }

        renderNotepadDesktopIcon(surface, desktopApps);
        updateSelectionVisual();
    }
    async function startPairingFlow() {
        try {
            const r = await fetch("/api/v5/app_pair/start", {
                method: "POST",
                credentials: "include",
                cache: "no-store"
            });

            const j = await r.json().catch(() => null);
            if (!r.ok || !j || !j.ok) {
                throw new Error((j && j.message) ? j.message : `HTTP ${r.status}`);
            }

            currentPairing = j;
            await loadTrustedDevices();
            renderTrustedDevices();

            stopPairPolling();
            pairPollTimer = setInterval(async () => {
                if (!currentPairing || !currentPairing.pair_id) return;

                try {
                    const sr = await fetch(`/api/v5/app_pair/status?pair_id=${encodeURIComponent(currentPairing.pair_id)}`, {
                        credentials: "include",
                        cache: "no-store"
                    });

                    const sj = await sr.json().catch(() => null);
                    if (!sr.ok || !sj || !sj.ok) return;

                    const statusEl = document.getElementById("pairStatusLine");
                    if (!statusEl) return;

                    if (sj.state === "pending") {
                        const left = (typeof sj.expires_at === "number" && typeof sj.now === "number")
                            ? Math.max(0, sj.expires_at - sj.now)
                            : 0;

                        statusEl.textContent = tr("shell.trusted.waiting_expires", { expires: fmtDateTime(sj.expires_at), left: fmtRemainingSec(left) }, `Waiting for phone to scan and confirm… Expires ${fmtDateTime(sj.expires_at)} (${fmtRemainingSec(left)} left)`);
                        return;
                    }

                    if (sj.state === "consumed") {
                        statusEl.textContent = tr("shell.trusted.paired_success", { id: sj.device_id || "?" }, `Paired successfully. Device ID: ${sj.device_id || "?"}`);
                        stopPairPolling();
                        await loadTrustedDevices();
                        renderTrustedDevices();
                        return;
                    }

                    if (sj.state === "expired") {
                        statusEl.textContent = tr("shell.trusted.request_expired", null, "Pairing request expired. Start a new one.");
                        stopPairPolling();
                        return;
                    }

                    if (sj.state === "missing") {
                        statusEl.textContent = tr("shell.trusted.request_missing", null, "Pairing request missing.");
                        stopPairPolling();
                    }
                } catch {
                    // keep polling quietly
                }
            }, 1500);
        } catch (e) {
            currentPairing = null;
            renderTrustedDevices(tr("shell.trusted.failed_start", { error: String(e && e.message ? e.message : e) }, `Failed to start pairing: ${String(e && e.message ? e.message : e)}`), "err");
        }
    }

    function renderTrustedDevices(messageText = "", messageKind = "") {
        currentView = "trusted_devices";
        currentApp = null;

        setActiveNav("nav_trusted_devices");
        setActiveApp("");

        if (wsTitle) wsTitle.textContent = tr("shell.trusted.title", null, "Trusted Devices");
        if (wsSubtitle) wsSubtitle.textContent = tr("shell.trusted.subtitle", null, "Pair this phone or other devices with your account");
        if (mainPaneTitle) mainPaneTitle.textContent = tr("shell.trusted.title", null, "Trusted Devices");

        if (!homeBlurb) return;

        setMainHostMode("home");
        homeBlurb.classList.remove("appHostBlurb");

        // Settings can be taller than the viewport. Keep the page shell fixed
        // and let the main content area scroll.
        {
            const mainHost = getMainHost();
            if (mainHost) {
                mainHost.style.height = "calc(100vh - 0px)";
                mainHost.style.maxHeight = "calc(100vh - 0px)";
                mainHost.style.minHeight = "0";
                mainHost.style.overflowY = "auto";
                mainHost.style.overflowX = "hidden";
            }

            homeBlurb.style.height = "auto";
            homeBlurb.style.maxHeight = "none";
            homeBlurb.style.minHeight = "0";
            homeBlurb.style.overflowY = "visible";
            homeBlurb.style.overflowX = "hidden";
            homeBlurb.style.padding = "14px 18px 28px 18px";
            homeBlurb.style.boxSizing = "border-box";
        }

        const qrBlock = currentPairing ? `
        <div style="margin-top:16px; display:flex; flex-direction:column; gap:12px; align-items:flex-start;">
            <img
                src="${currentPairing.qr_svg}"
                alt="${tr("shell.trusted.qr_alt", null, "Pairing QR")}"
                style="width:280px; height:280px; border-radius:16px; border:1px solid var(--border2); background:#fff; padding:12px;"
            />
            <div class="mini" id="pairStatusLine">${tr("shell.trusted.waiting_initial", null, "Waiting for phone to scan and confirm…")}</div>
            <div class="mini" style="word-break:break-all;">
                ${currentPairing.qr_uri || ""}
            </div>
        </div>
    ` : `
        <div class="mini" id="pairStatusLine">${tr("shell.trusted.no_active_pair", null, "No active pairing request.")}</div>
    `;
        const deviceRows = trustedDevices.map((d) => {
            const trustedUntil = d.refresh_expires_at ? fmtDateTime(d.refresh_expires_at) : "—";
            return `
        <div class="card" style="padding:14px; margin-top:10px; border:1px solid var(--border2); border-radius:16px; background:var(--panel);">
            <div style="display:flex; justify-content:space-between; gap:12px; align-items:flex-start;">
                <div>
                    <div style="font-weight:700; color:var(--fg);">${d.device_name || tr("shell.trusted.unnamed_device", null, "Unnamed device")}</div>
                    <div class="mini">
    ${[
                d.device_manufacturer,
                d.device_model,
                d.os_version,
                d.app_version ? tr("shell.trusted.app_version", { version: d.app_version }, `app ${d.app_version}`) : ""
            ].filter(Boolean).join(" · ") || (d.platform || "?")}
</div>
                    <div class="mini">${tr("shell.trusted.paired", { time: fmtDateTime(d.created_at) }, `Paired: ${fmtDateTime(d.created_at)}`)}</div>
                    <div class="mini">${tr("shell.trusted.last_seen", { time: fmtDateTime(d.last_seen_at) }, `Last seen: ${fmtDateTime(d.last_seen_at)}`)}</div>
                    <div class="mini">${tr("shell.trusted.trusted_until", { time: trustedUntil }, `Trusted until: ${trustedUntil}`)}</div>
                    ${d.revoked ? `<span class="pq-badge err" style="margin-top:6px;">${tr("shell.trusted.status_revoked", null, "Status: revoked")}</span>` : `<span class="pq-badge ok" style="margin-top:6px;">${tr("shell.trusted.status_active", null, "Status: active")}</span>`}
                </div>
                ${d.revoked ? "" : `
                    <button class="pq-btn danger trustedRevokeBtn" type="button" data-device-id="${String(d.device_id || "")}">
                        ${tr("shell.trusted.forget_pairing", null, "Forget pairing")}
                    </button>
                `}
            </div>
        </div>
    `;
        }).join("");

        const devicesBlock = `
    <div style="margin-top:24px;">
        <h3 style="margin:0 0 8px 0; font-size:18px;">${tr("shell.trusted.devices_title", null, "Trusted devices")}</h3>
        <div style="color:var(--fg-dim); line-height:1.5; margin-bottom:12px;">
            ${tr("shell.trusted.devices_desc", null, "Devices that can access your account through app pairing.")}
        </div>
        ${trustedDevicesError ? `
            <div class="bigState" style="display:block; margin-top:8px;">
                <h3>${tr("shell.trusted.could_not_load", null, "Could not load devices")}</h3>
                <p>${trustedDevicesError}</p>
            </div>
        ` : ""}
        ${trustedDevices.length ? deviceRows : `<div class="mini">${tr("shell.trusted.no_devices", null, "No trusted devices yet.")}</div>`}
    </div>
`;

    const homeContent = setHomeContentHtml(`
    <div style="max-width:760px; margin:16px 0 28px 22px; padding:0; box-sizing:border-box; font-family:var(--sans);">
        <h3 style="margin:0 0 8px 0; font-size:18px; font-family:inherit;">${tr("shell.trusted.pair_title", null, "Pair a new device")}</h3>
        <div style="color:var(--fg-dim); line-height:1.5; margin-bottom:14px; font-family:inherit;">
            ${tr("shell.trusted.pair_intro", null, "Open the mobile app, choose scan/pair, and scan the QR code shown here. After you confirm on the phone, this page will update automatically.")}
        </div>

        <div style="display:flex; gap:10px; flex-wrap:wrap; margin-bottom:12px; font-family:inherit;">
            <button class="btn" id="pairNewDeviceBtn" type="button">${tr("shell.trusted.pair_new", null, "Pair New Device")}</button>
            <button class="btn secondary" id="pairStopBtn" type="button">${tr("shell.trusted.cancel_pairing", null, "Cancel pairing")}</button>
        </div>

        ${messageText ? `
            <div class="bigState" style="display:block; margin-top:8px;">
                <h3>${messageKind === "ok" ? tr("shell.trusted.success", null, "Success") : tr("shell.trusted.pairing_error", null, "Pairing error")}</h3>
                <p>${messageText}</p>
            </div>
            ` : ""}

            ${qrBlock}
            ${devicesBlock}
    </div>
    `);

        const pairBtn = document.getElementById("pairNewDeviceBtn");
        if (pairBtn) {
            pairBtn.addEventListener("click", () => {
                startPairingFlow();
            });
        }
        for (const btn of (homeContent || homeBlurb).querySelectorAll(".trustedRevokeBtn")) {
            btn.addEventListener("click", async () => {
                const deviceId = btn.dataset.deviceId || "";
                if (!deviceId) return;

                try {
                    await revokeTrustedDevice(deviceId);
                    await loadTrustedDevices();
                    renderTrustedDevices(tr("shell.trusted.forgotten", { id: deviceId }, `Pairing forgotten for device: ${deviceId}`), "ok");
                } catch (e) {
                    renderTrustedDevices(tr("shell.trusted.removed", null, "Device removed from trusted devices."), "ok");
                }
            });
        }

        const clearBtn = document.getElementById("pairStopBtn");
        if (clearBtn) {
            clearBtn.addEventListener("click", async () => {
                try {
                    if (currentPairing && currentPairing.pair_id) {
                        await cancelPairing(currentPairing.pair_id);
                    }
                    stopPairPolling();
                    currentPairing = null;
                    await loadTrustedDevices();
                    renderTrustedDevices();
                } catch (e) {
                    renderTrustedDevices(tr("shell.trusted.failed_cancel", { error: String(e && e.message ? e.message : e) }, `Failed to cancel pairing: ${String(e && e.message ? e.message : e)}`));
                }
            });
        }
    }

    function updateHomeInvitesHint() {
        // Legacy shell-level workspace invite hint disabled.
        return;
        const desktopHint = document.getElementById("desktopHint");
        if (!desktopHint) return;

        if (!authed) {
            desktopHint.innerHTML = "";
            return;
        }

        if (workspaceInvitesError) {
            desktopHint.innerHTML = `
            <div class="bigState" style="display:block; margin-bottom:10px;">
                <h3>Workspace invites unavailable</h3>
                <p>${escapeHtml(workspaceInvitesError)}</p>
            </div>
        `;
            return;
        }

        const count = Array.isArray(workspaceInvites) ? workspaceInvites.length : 0;
        if (!count) {
            desktopHint.innerHTML = "";
            return;
        }

        desktopHint.innerHTML = `
        <div class="bigState" style="display:block; margin-bottom:10px;">
            <h3>You have ${count} workspace invitation${count === 1 ? "" : "s"}</h3>
            <p>
                Accept or decline pending workspace access requests before they appear in File Manager.
            </p>
            <div style="margin-top:10px;">
                <button class="btn" id="homeOpenWorkspaceInvitesBtn" type="button">Open Workspace Invites</button>
            </div>
        </div>
    `;

        const btn = document.getElementById("homeOpenWorkspaceInvitesBtn");
        if (btn) {
            btn.addEventListener("click", () => {
                renderWorkspaceInvites();
            });
        }
    }

    function renderHome() {
        stopPairPolling();

        currentView = "home";
        currentApp = null;

        setActiveNav("nav_home");
        setActiveApp(""); // clears app highlight

        if (wsTitle) wsTitle.textContent = "Home";
        setWsSubtitleSafe("Session, role, and access status");
        if (mainPaneTitle) mainPaneTitle.textContent = "Workspace";

        // Home should be frameless too (avoid border-within-border)
        if (homeBlurb) {
            setMainHostMode("home");
            homeBlurb.classList.remove("appHostBlurb");
            homeBlurb.style.overflowY = "auto";
            homeBlurb.style.maxHeight = "100%";
        }


        if (homeBlurb) {
            show(homeBlurb, true);

            // IMPORTANT: renderApp() overwrote homeBlurb.innerHTML with the iframe.
            // So when returning Home, rebuild the desktop DOM if it’s missing.
            let surface = document.getElementById("desktopSurface");
            if (!surface) {
                setHomeContentHtml(`
                <div id="desktopHint" style="margin-bottom:10px;"></div>
                <div id="desktopSurface" class="desktopSurface" aria-label="Desktop"></div>
            `);
            }
        }

        updateHomeInvitesHint();

// Render desktop icons on Home
        renderDesktopIcons();
    }
    let passwordAuthConfigLoaded = false;
    let passwordAuthEnabled = false;
    let passwordAuthConfigError = "";

    async function loadPasswordAuthConfig() {
        passwordAuthConfigError = "";

        try {
            const r = await fetch("/api/auth/config", {
                credentials: "include",
                cache: "no-store"
            });

            const j = await r.json().catch(() => null);

            if (!r.ok || !j || j.ok === false) {
                throw new Error((j && (j.message || j.error)) ? (j.message || j.error) : `HTTP ${r.status}`);
            }

            passwordAuthEnabled = !!j.password_enabled;
            passwordAuthConfigLoaded = true;
        } catch (e) {
            passwordAuthEnabled = false;
            passwordAuthConfigLoaded = true;
            passwordAuthConfigError = String(e && e.message ? e.message : e);
        }
    }

    async function changeOwnPasswordFromSettings() {
        const login = (document.getElementById("settingsPasswordLogin")?.value || "").trim();
        const current_password = document.getElementById("settingsCurrentPassword")?.value || "";
        const new_password = document.getElementById("settingsNewPassword")?.value || "";
        const confirm_password = document.getElementById("settingsConfirmPassword")?.value || "";

        if (!login) {
            throw new Error(tr("settings.password.login_required", null, "Enter your login/email."));
        }

        if (!current_password) {
            throw new Error(tr("settings.password.current_required", null, "Enter your current password."));
        }

        if (new_password.length < 12) {
            throw new Error(tr("settings.password.too_short", null, "New password must be at least 12 characters."));
        }

        if (new_password !== confirm_password) {
            throw new Error(tr("settings.password.mismatch", null, "New password and confirmation do not match."));
        }

        const r = await fetch("/api/auth/password/change", {
            method: "POST",
            credentials: "include",
            cache: "no-store",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({
                login,
                current_password,
                new_password
            })
        });

        const j = await r.json().catch(() => null);

        if (!r.ok || !j || j.ok === false) {
            const msg = j && (j.message || j.error)
                ? (j.message || j.error)
                : `HTTP ${r.status}`;
            throw new Error(msg);
        }

        try {
            localStorage.setItem("pqnas_password_login", login);
        } catch {}

        return j;
    }

    let userVaultMasterRecovery = null;
    let userVaultMasterRecoveryError = "";
    let pendingVaultMasterRecoveryPrivateKeyB64 = "";
    let pendingVaultMasterRecoveryPublicKeyB64 = "";
    let pendingVaultMasterRecoveryPublicKeySha256 = "";
    let pendingVaultMasterRecoveryCreatedAt = 0;

    function b64ToBytesForMasterRecovery(b64) {
        const bin = atob(String(b64 || "").replace(/-/g, "+").replace(/_/g, "/"));
        const out = new Uint8Array(bin.length);
        for (let i = 0; i < bin.length; i += 1) out[i] = bin.charCodeAt(i);
        return out;
    }

    function bytesToHexForMasterRecovery(bytes) {
        return Array.from(bytes || []).map((b) => b.toString(16).padStart(2, "0")).join("");
    }

    function zeroBytesForMasterRecovery(bytes) {
        if (bytes && typeof bytes.fill === "function") {
            try { bytes.fill(0); } catch {}
        }
    }

    function shortMasterRecoveryKeyId(id) {
        const s = String(id || "");
        if (!s) return "—";
        if (s.length <= 24) return s;
        return `${s.slice(0, 16)}…${s.slice(-8)}`;
    }

    function formatMasterRecoveryCreated(ms) {
        const n = Number(ms || 0);
        if (!Number.isFinite(n) || n <= 0) return "—";
        try { return new Date(n).toLocaleString(); } catch { return String(n); }
    }

    async function loadMasterRecoveryHelper() {
        const existingHelper = window.PQShareMlkem || window.PqShareMlKemV1 || null;
        if (existingHelper &&
            typeof (existingHelper.generateKeypair768 || existingHelper.keygen768) === "function" &&
            typeof existingHelper.encapsulate768 === "function" &&
            typeof existingHelper.decapsulate768 === "function") {
            return existingHelper;
        }

        await new Promise((resolve, reject) => {
            let done = false;
            const finish = (err) => {
                if (done) return;
                done = true;
                window.clearTimeout(timer);
                err ? reject(err) : resolve();
            };

            const helperObj = () => window.PQShareMlkem || window.PqShareMlKemV1 || null;
            const ready = () => {
                const h = helperObj();
                return h &&
                    typeof (h.generateKeypair768 || h.keygen768) === "function" &&
                    typeof h.encapsulate768 === "function" &&
                    typeof h.decapsulate768 === "function";
            };

            const timer = window.setTimeout(() => {
                finish(new Error("Timed out loading ML-KEM helper"));
            }, 10000);

            if (ready()) {
                finish();
                return;
            }

            const existing = document.querySelector('script[data-master-recovery-helper="1"]');
            if (existing) {
                existing.addEventListener("load", () => finish(), { once: true });
                existing.addEventListener("error", () => finish(new Error("Failed to load ML-KEM helper")), { once: true });
                return;
            }

            const s = document.createElement("script");
            s.src = "/static/share_pq_mlkem.js?v=master-recovery-modal-1";
            s.async = true;
            s.dataset.masterRecoveryHelper = "1";
            s.onload = () => finish();
            s.onerror = () => finish(new Error("Failed to load ML-KEM helper"));
            document.head.appendChild(s);
        });

        const h = window.PQShareMlkem || window.PqShareMlKemV1 || null;
        if (!h ||
            typeof (h.generateKeypair768 || h.keygen768) !== "function" ||
            typeof h.encapsulate768 !== "function" ||
            typeof h.decapsulate768 !== "function") {
            throw new Error("ML-KEM helper is unavailable");
        }

        return h;
    }

    async function sha256B64ForMasterRecovery(publicKeyB64) {
        const bytes = b64ToBytesForMasterRecovery(publicKeyB64);
        try {
            const hash = await crypto.subtle.digest("SHA-256", bytes);
            return bytesToHexForMasterRecovery(new Uint8Array(hash));
        } finally {
            zeroBytesForMasterRecovery(bytes);
        }
    }

    async function assertMasterRecoveryKeyRoundtrip(helper, publicKeyB64, privateKeyB64) {
        const publicKeyBytes = b64ToBytesForMasterRecovery(publicKeyB64);
        const privateKeyBytes = b64ToBytesForMasterRecovery(privateKeyB64);
        let encSecret = null;
        let decSecret = null;

        try {
            if (publicKeyBytes.length !== 1184) {
                throw new Error(`Unexpected ML-KEM-768 public key size: ${publicKeyBytes.length} bytes`);
            }

            if (privateKeyBytes.length !== 64) {
                throw new Error(`Unexpected ML-KEM-768 compact private key size: ${privateKeyBytes.length} bytes`);
            }

            const enc = await helper.encapsulate768({ publicKeyB64 });
            encSecret = enc && enc.shared_secret_bytes;
            decSecret = await helper.decapsulate768({
                privateKeyB64,
                ciphertextB64: enc.ciphertext_b64
            });

            if (!encSecret || !decSecret || encSecret.length !== decSecret.length) {
                throw new Error("Master recovery key self-test failed");
            }

            let diff = 0;
            for (let i = 0; i < encSecret.length; i += 1) diff |= encSecret[i] ^ decSecret[i];
            if (diff !== 0) throw new Error("Master recovery key self-test failed");
        } finally {
            zeroBytesForMasterRecovery(publicKeyBytes);
            zeroBytesForMasterRecovery(privateKeyBytes);
            zeroBytesForMasterRecovery(encSecret);
            zeroBytesForMasterRecovery(decSecret);
        }
    }

    async function loadUserVaultMasterRecovery() {
        userVaultMasterRecoveryError = "";
        try {
            const j = await apiUserGet("/api/v4/user/vault/master-recovery");
            userVaultMasterRecovery = j.vault_master_recovery || {};
        } catch (e) {
            userVaultMasterRecovery = null;
            userVaultMasterRecoveryError = String(e && e.message ? e.message : e);
        }
    }

    async function saveUserVaultMasterRecoveryPublicKey() {
        if (!pendingVaultMasterRecoveryPublicKeyB64 || !pendingVaultMasterRecoveryPublicKeySha256) {
            throw new Error("No pending Master recovery public key.");
        }

        const ack = document.getElementById("settingsMasterRecoveryAck");
        if (!ack || !ack.checked) {
            throw new Error(tr("settings.vault_recovery.confirm_stored", null, "Confirm that you have stored the private key safely."));
        }

        const j = await apiUserPost("/api/v4/user/vault/master-recovery", {
            vault_master_recovery: {
                enabled: true,
                status: "active",
                public_key_b64: pendingVaultMasterRecoveryPublicKeyB64,
                public_key_sha256: pendingVaultMasterRecoveryPublicKeySha256,
                created_at: pendingVaultMasterRecoveryCreatedAt,
                label: "Master recovery key"
            }
        });

        userVaultMasterRecovery = j.vault_master_recovery || {};
        pendingVaultMasterRecoveryPrivateKeyB64 = "";
        pendingVaultMasterRecoveryPublicKeyB64 = "";
        pendingVaultMasterRecoveryPublicKeySha256 = "";
        pendingVaultMasterRecoveryCreatedAt = 0;
    }

    function withMasterRecoveryTimeout(promise, label, ms = 15000) {
        return new Promise((resolve, reject) => {
            const timer = window.setTimeout(() => {
                reject(new Error(`${label} timed out after ${Math.round(ms / 1000)} seconds`));
            }, ms);

            Promise.resolve(promise).then(
                (value) => {
                    window.clearTimeout(timer);
                    resolve(value);
                },
                (err) => {
                    window.clearTimeout(timer);
                    reject(err);
                }
            );
        });
    }

    async function generateUserVaultMasterRecoveryKey() {
        const current = userVaultMasterRecovery || {};
        const active = !!current.enabled && String(current.status || "") === "active";

        if (active) {
            const ok = await openShellConfirmDialog({
                title: tr("settings.vault_recovery.rotate_confirm_title", null, "Rotate Master recovery key?"),
                message: tr("settings.vault_recovery.rotate_confirm_message", null, "New Vault uploads will use the new Master recovery public key. Old Vault packages still require the old private key."),
                confirmText: tr("settings.vault_recovery.rotate_key", null, "Rotate key"),
                cancelText: tr("admin.common.cancel", null, "Cancel"),
                danger: false
            });
            if (!ok) return false;
        }

        const helper = await withMasterRecoveryTimeout(
            loadMasterRecoveryHelper(),
            "Loading ML-KEM helper",
            15000
        );

        const keygen768 =
            typeof helper.generateKeypair768 === "function"
                ? helper.generateKeypair768.bind(helper)
                : helper.keygen768.bind(helper);

        const kp = await withMasterRecoveryTimeout(
            keygen768(),
            "Generating ML-KEM keypair",
            15000
        );

        pendingVaultMasterRecoveryPrivateKeyB64 = String(kp.private_key_b64 || "");
        pendingVaultMasterRecoveryPublicKeyB64 = String(kp.public_key_b64 || "");
        pendingVaultMasterRecoveryCreatedAt = Date.now();

        pendingVaultMasterRecoveryPublicKeySha256 = await withMasterRecoveryTimeout(
            sha256B64ForMasterRecovery(pendingVaultMasterRecoveryPublicKeyB64),
            "Hashing Master recovery public key",
            10000
        );

        await withMasterRecoveryTimeout(
            assertMasterRecoveryKeyRoundtrip(
                helper,
                pendingVaultMasterRecoveryPublicKeyB64,
                pendingVaultMasterRecoveryPrivateKeyB64
            ),
            "Testing Master recovery key",
            15000
        );

        return true;
    }

    function downloadPendingMasterRecoveryPrivateKey() {
        if (!pendingVaultMasterRecoveryPrivateKeyB64) {
            throw new Error("No pending Master recovery private key.");
        }

        const payload = {
            type: "dna-nexus-vault-master-recovery-private-key",
            alg: "ML-KEM-768",
            private_key_format: "compact-seed-64-bytes",
            created_at: pendingVaultMasterRecoveryCreatedAt,
            public_key_sha256: pendingVaultMasterRecoveryPublicKeySha256,
            private_key_b64: pendingVaultMasterRecoveryPrivateKeyB64,
            warning: "Store this file offline. Anyone with this private key can recover Vault files protected by this Master recovery key."
        };

        const blob = new Blob([JSON.stringify(payload, null, 2) + "\n"], {
            type: "application/json"
        });

        const a = document.createElement("a");
        const url = URL.createObjectURL(blob);
        a.href = url;
        a.download = `dna-nexus-vault-master-recovery-key-${pendingVaultMasterRecoveryPublicKeySha256.slice(0, 12) || "new"}.json`;
        a.rel = "noopener";
        document.body.appendChild(a);
        a.click();

        window.setTimeout(() => {
            URL.revokeObjectURL(url);
            a.remove();
        }, 1000);
    }

    function ensureMasterRecoveryModalStyles() {
        if (document.getElementById("settingsMasterRecoveryModalStyles")) return;

        const style = document.createElement("style");
        style.id = "settingsMasterRecoveryModalStyles";
        style.textContent = `
            .settingsMasterRecoveryOverlay{
                position:fixed;
                inset:0;
                z-index:9900;
                display:flex;
                align-items:center;
                justify-content:center;
                padding:18px;
                background:rgba(0,0,0,0.38);
                backdrop-filter:blur(8px);
            }
            .settingsMasterRecoveryDialog{
                width:min(760px, calc(100vw - 36px));
                max-height:calc(100vh - 36px);
                overflow:auto;
                border-radius:20px;
                border:1px solid rgba(255,255,255,0.22);
                background:rgba(248,250,255,0.96);
                color:#111827;
                box-shadow:0 28px 90px rgba(0,0,0,0.36);
            }
            html[data-theme="dark"] .settingsMasterRecoveryDialog,
            html[data-theme="cpunk_orange"] .settingsMasterRecoveryDialog{
                background:rgba(9,18,32,0.98);
                color:rgba(235,248,255,0.96);
            }
            .settingsMasterRecoveryHead{
                display:flex;
                align-items:center;
                justify-content:space-between;
                gap:12px;
                padding:14px 16px;
                border-bottom:1px solid rgba(128,128,128,0.25);
            }
            .settingsMasterRecoveryTitle{
                font-weight:900;
                letter-spacing:.03em;
            }
            .settingsMasterRecoveryBody{
                padding:16px;
            }
            .settingsMasterRecoverySecret{
                width:100%;
                min-height:132px;
                margin-top:10px;
                font-family:var(--mono, ui-monospace, SFMono-Regular, Menlo, monospace);
                font-size:12px;
                border-radius:14px;
                padding:10px;
                box-sizing:border-box;
            }
            .settingsMasterRecoveryWarning{
                padding:10px 12px;
                border-radius:14px;
                border:1px solid rgba(180,120,0,0.38);
                background:rgba(255,190,80,0.14);
                line-height:1.5;
            }
            .settingsMasterRecoveryFoot{
                display:flex;
                justify-content:space-between;
                gap:10px;
                flex-wrap:wrap;
                align-items:center;
                padding:14px 16px;
                border-top:1px solid rgba(128,128,128,0.25);
            }
        `;
        document.head.appendChild(style);
    }

    function closeMasterRecoveryModal() {
        document.getElementById("settingsMasterRecoveryModal")?.remove();
    }

    async function openUserVaultMasterRecoveryPrivateKeyDialog() {
        if (!pendingVaultMasterRecoveryPrivateKeyB64) {
            throw new Error("No pending Master recovery private key.");
        }

        ensureMasterRecoveryModalStyles();
        closeMasterRecoveryModal();

        const root = document.createElement("div");
        root.id = "settingsMasterRecoveryModal";
        root.className = "settingsMasterRecoveryOverlay";
        root.setAttribute("role", "dialog");
        root.setAttribute("aria-modal", "true");
        root.setAttribute("aria-labelledby", "settingsMasterRecoveryModalTitle");

        root.innerHTML = `
            <div class="settingsMasterRecoveryDialog">
                <div class="settingsMasterRecoveryHead">
                    <div id="settingsMasterRecoveryModalTitle" class="settingsMasterRecoveryTitle">
                        Master recovery private key
                    </div>
                    <button class="btn secondary" type="button" data-action="close">×</button>
                </div>

                <div class="settingsMasterRecoveryBody">
                    <div class="settingsMasterRecoveryWarning">
                        ${escapeHtml(tr("settings.vault_recovery.private_key_window_warning", null, "Copy or download this private key now. It will not be shown again after this window is closed and DNA-Nexus will not store it."))}
                    </div>

                    <div class="mini" style="line-height:1.5; margin-top:10px;">
                        ${escapeHtml(tr("settings.vault_recovery.private_key_format_note", null, "Format: ML-KEM-768 compact seed private key, 64 bytes. DNA-Nexus verifies this key with a local roundtrip test before showing it."))}
                    </div>

                    <textarea
                        id="settingsMasterRecoveryPrivateKey"
                        class="settingsMasterRecoverySecret"
                        readonly
                        spellcheck="false"
                        aria-label="Master recovery private key"
                    >${escapeHtml(pendingVaultMasterRecoveryPrivateKeyB64)}</textarea>

                    <div class="mini" style="line-height:1.5; margin-top:10px;">
                        ${escapeHtml(tr("settings.vault_recovery.offline_storage_note", null, "Store this offline, in a password manager, on offline USB storage, or as a printed recovery sheet in a safe. Do not email it or leave it in Downloads."))}
                    </div>

                    <div id="settingsMasterRecoveryModalStatus" class="mini" style="line-height:1.5; margin-top:10px;"></div>
                </div>

                <div class="settingsMasterRecoveryFoot">
                    <label style="display:flex; gap:8px; align-items:center;">
                        <input id="settingsMasterRecoveryAck" type="checkbox">
                        <span>${escapeHtml(tr("settings.vault_recovery.ack_stored", null, "I have stored this private key safely."))}</span>
                    </label>

                    <div style="display:flex; gap:10px; flex-wrap:wrap;">
                        <button class="btn secondary" type="button" data-action="copy">${escapeHtml(tr("settings.vault_recovery.copy_private_key", null, "Copy private key"))}</button>
                        <button class="btn secondary" type="button" data-action="download">${escapeHtml(tr("settings.vault_recovery.download_private_key_json", null, "Download private key JSON"))}</button>
                        <button class="btn primary" type="button" data-action="save">${escapeHtml(tr("settings.vault_recovery.save_public_key", null, "Save public key"))}</button>
                        <button class="btn secondary" type="button" data-action="discard">${escapeHtml(tr("settings.vault_recovery.discard", null, "Discard"))}</button>
                    </div>
                </div>
            </div>
        `;

        document.body.appendChild(root);

        const status = root.querySelector("#settingsMasterRecoveryModalStatus");
        const textarea = root.querySelector("#settingsMasterRecoveryPrivateKey");

        const setStatus = (msg, kind = "") => {
            if (!status) return;
            status.textContent = msg || "";
            status.className = kind ? `msg ${kind}` : "mini";
        };

        root.addEventListener("click", async (ev) => {
            const btn = ev.target && ev.target.closest ? ev.target.closest("[data-action]") : null;
            if (!btn) return;

            const action = btn.getAttribute("data-action");

            if (action === "close") {
                setStatus(tr("settings.vault_recovery.store_or_discard", null, "Store the private key and save the public key, or discard it explicitly."), "warn");
                return;
            }

            if (action === "copy") {
                try {
                    await navigator.clipboard.writeText(pendingVaultMasterRecoveryPrivateKeyB64);
                    setStatus(tr("settings.vault_recovery.private_key_copied", null, "Private key copied."), "ok");
                } catch (e) {
                    try {
                        textarea?.focus();
                        textarea?.select();
                        document.execCommand("copy");
                        setStatus(tr("settings.vault_recovery.private_key_copied", null, "Private key copied."), "ok");
                    } catch {
                        setStatus(`Copy failed: ${String(e && e.message ? e.message : e)}`, "err");
                    }
                }
                return;
            }

            if (action === "download") {
                try {
                    downloadPendingMasterRecoveryPrivateKey();
                    setStatus("Private key JSON downloaded.", "ok");
                } catch (e) {
                    setStatus(`Download failed: ${String(e && e.message ? e.message : e)}`, "err");
                }
                return;
            }

            if (action === "save") {
                btn.disabled = true;
                btn.textContent = "Saving…";

                try {
                    await saveUserVaultMasterRecoveryPublicKey();
                    closeMasterRecoveryModal();
                    renderUserSettings("Master recovery public key saved.", "ok");
                } catch (e) {
                    btn.disabled = false;
                    btn.textContent = "Save public key";
                    setStatus(tr("settings.vault_recovery.save_failed", { error: String(e && e.message ? e.message : e) }, `Save failed: ${String(e && e.message ? e.message : e)}`), "err");
                }
                return;
            }

            if (action === "discard") {
                const ok = await openShellConfirmDialog({
                    title: tr("settings.vault_recovery.discard_confirm_title", null, "Discard generated private key?"),
                    message: tr("settings.vault_recovery.discard_confirm_message", null, "The generated private key and public key will be forgotten by this browser view."),
                    confirmText: tr("settings.vault_recovery.discard", null, "Discard"),
                    cancelText: tr("admin.common.cancel", null, "Cancel"),
                    danger: true
                });

                if (!ok) return;

                pendingVaultMasterRecoveryPrivateKeyB64 = "";
                pendingVaultMasterRecoveryPublicKeyB64 = "";
                pendingVaultMasterRecoveryPublicKeySha256 = "";
                pendingVaultMasterRecoveryCreatedAt = 0;
                closeMasterRecoveryModal();
                renderUserSettings(tr("settings.vault_recovery.generated_key_discarded", null, "Generated Master recovery key discarded."), "ok");
            }
        });

        window.setTimeout(() => {
            try {
                textarea?.focus();
                textarea?.select();
            } catch {}
        }, 40);
    }

    async function loadUserProfile() {
        if (!authed) {
            userProfile = null;
            userProfileError = "";
            return;
        }

        userProfileLoading = true;
        userProfileError = "";

        try {
            const j = await apiUserGet("/api/v4/user/profile");
            userProfile = j.profile || {};
        } catch (e) {
            userProfile = null;
            userProfileError = String(e && e.message ? e.message : e);
        } finally {
            userProfileLoading = false;
        }
    }

    async function saveUserProfileFromSettings() {
        const name = (document.getElementById("userProfileName")?.value || "").trim();
        const email = (document.getElementById("userProfileEmail")?.value || "").trim();
        const avatar_url = (document.getElementById("userProfileAvatarUrl")?.value || "").trim();

        const j = await apiUserPost("/api/v4/user/profile/update", {
            name,
            email,
            avatar_url
        });

        userProfile = j.profile || null;
    }

    async function uploadUserAvatarFromSettings(file) {
        if (!file) return "";

        const prepared = await prepareAvatarUploadBlob(file);
        const data_b64 = await fileToBase64(prepared.blob);

        const j = await apiUserPost("/api/v4/user/profile/avatar_upload", {
            filename: file.name || "avatar.jpg",
            mime: prepared.mime,
            data_b64
        });

        userAvatarBust = Date.now();
        await loadUserProfile();

        if (userProfile && j.avatar_url) {
            userProfile.avatar_url = j.avatar_url;
        }

        return {
            avatar_url: j.avatar_url || "",
            note: prepared.note || ""
        };
    }

    async function removeUserAvatarFromSettings() {
        await apiUserPost("/api/v4/user/profile/avatar_remove", {});
        userAvatarBust = Date.now();
        await loadUserProfile();

        if (userProfile) {
            userProfile.avatar_url = "";
        }
    }
    function renderUserSettings(messageText = "", messageKind = "") {
        stopPairPolling();

        currentView = "user_settings";
        currentApp = null;

        setActiveNav("nav_user_settings");
        setActiveApp("");

        if (wsTitle) wsTitle.textContent = tr("settings.page.title", null, "Settings");
        if (wsSubtitle) wsSubtitle.textContent = tr("settings.page.subtitle", null, "Personal preferences");
        if (mainPaneTitle) mainPaneTitle.textContent = tr("settings.page.title", null, "Settings");

        if (!homeBlurb) return;

        setMainHostMode("home");
        homeBlurb.classList.remove("appHostBlurb");

        const activeTheme = currentThemeName();
        const activeLanguage = currentLanguageName();

        if (!userVaultMasterRecovery && !userVaultMasterRecoveryError) {
            loadUserVaultMasterRecovery().then(() => {
                if (currentView === "user_settings") renderUserSettings(messageText, messageKind);
            }).catch(() => {});
        }

        const languageOptions = [
            { code: "en", flag: "en", key: "settings.language.english", fallback: "English" },
            { code: "fi", flag: "fi", key: "settings.language.finnish", fallback: "Suomi" },
            { code: "zh", flag: "zh", key: "settings.language.chinese_simplified", fallback: "简体中文" },
            { code: "sv", flag: "sv", key: "settings.language.swedish", fallback: "Svenska" },
            { code: "uk", flag: "ua", key: "settings.language.ukrainian", fallback: "Українська" },
            { code: "de", flag: "de", key: "settings.language.german", fallback: "Deutsch" },
            { code: "et", flag: "et", key: "settings.language.estonian", fallback: "Eesti" },
            { code: "pl", flag: "pl", key: "settings.language.polish", fallback: "Polski" },
            { code: "es", flag: "es", key: "settings.language.spanish", fallback: "Español" },
            { code: "fr", flag: "fr", key: "settings.language.french", fallback: "Français" },
            { code: "it", flag: "it", key: "settings.language.italian", fallback: "Italiano" },
            { code: "tr", flag: "tr", key: "settings.language.turkish", fallback: "Türkçe" }
        ];

        const langChoice = (opt) => {
            const active = activeLanguage === opt.code;
            const label = tr(opt.key, null, opt.fallback);
            return `
                <label
                    style="
                        display:flex;
                        align-items:center;
                        gap:10px;
                        padding:10px 12px;
                        border-radius:14px;
                        border:1px solid ${active ? "rgba(255,255,255,0.36)" : "rgba(255,255,255,0.14)"};
                        background:${active ? "rgba(255,255,255,0.10)" : "rgba(0,0,0,0.20)"};
                        cursor:pointer;
                        font-weight:850;
                    "
                >
                    <input
                        type="radio"
                        name="userLanguage"
                        value="${escapeHtml(opt.code)}"
                        ${active ? "checked" : ""}
                    >
                    <img
                        src="/static/img/flags/${escapeHtml(opt.flag)}.svg"
                        alt=""
                        aria-hidden="true"
                        style="width:28px; height:21px; border-radius:4px; object-fit:cover; box-shadow:0 0 0 1px rgba(0,0,0,0.24); flex:0 0 auto;"
                    >
                    <span>${escapeHtml(label)}</span>
                </label>`;
        };
        if (!userProfile && !userProfileLoading && !userProfileError && authed) {
            loadUserProfile().then(() => {
                if (currentView === "user_settings") renderUserSettings();
            });
        }

        if (!passwordAuthConfigLoaded && authed) {
            loadPasswordAuthConfig().then(() => {
                if (currentView === "user_settings") renderUserSettings(messageText, messageKind);
            });
        }

        if (!appUserPrefsLoaded && authed) {
            loadAppUserPrefsFromServer().then(() => {
                if (currentView === "user_settings") renderUserSettings(messageText, messageKind);
            });
        }

        const p = userProfile || {};

        const passwordLoginHint = (() => {
            try { return localStorage.getItem("pqnas_password_login") || ""; } catch { return ""; }
        })();

        const passwordLoginValue = passwordLoginHint || p.email || "";

        const passwordCard = passwordAuthEnabled ? `
    <div class="card" style="padding:14px; margin-top:12px;">
        <h3 style="margin:0 0 8px 0; font-size:18px;">
            ${escapeHtml(tr("settings.password.title", null, "Password"))}
        </h3>

        <div class="mini" style="line-height:1.5; margin-bottom:12px;">
            ${escapeHtml(tr("settings.password.desc", null, "Change the password used to sign in on password-auth installations."))}
        </div>

        <div style="display:grid; gap:10px; max-width:540px;">
            <label>
                <div class="mini" style="margin-bottom:4px;">${escapeHtml(tr("settings.password.login", null, "Login / email"))}</div>
                <input
                    id="settingsPasswordLogin"
                    type="text"
                    autocomplete="username"
                    value="${escapeHtml(passwordLoginValue)}"
                    placeholder="you@example.com"
                    style="width:100%; box-sizing:border-box;"
                >
            </label>

            <label>
                <div class="mini" style="margin-bottom:4px;">${escapeHtml(tr("settings.password.current", null, "Current password"))}</div>
                <input
                    id="settingsCurrentPassword"
                    type="password"
                    autocomplete="current-password"
                    style="width:100%; box-sizing:border-box;"
                >
            </label>

            <label>
                <div class="mini" style="margin-bottom:4px;">${escapeHtml(tr("settings.password.new", null, "New password"))}</div>
                <input
                    id="settingsNewPassword"
                    type="password"
                    autocomplete="new-password"
                    minlength="12"
                    style="width:100%; box-sizing:border-box;"
                >
            </label>

            <label>
                <div class="mini" style="margin-bottom:4px;">${escapeHtml(tr("settings.password.confirm", null, "Confirm new password"))}</div>
                <input
                    id="settingsConfirmPassword"
                    type="password"
                    autocomplete="new-password"
                    minlength="12"
                    style="width:100%; box-sizing:border-box;"
                >
            </label>

            <div class="mini" style="line-height:1.5;">
                ${escapeHtml(tr("settings.password.help", null, "Use at least 12 characters. A longer passphrase is recommended."))}
            </div>

            <div style="display:flex; gap:10px; flex-wrap:wrap; margin-top:8px;">
                <button class="btn" id="settingsPasswordChangeBtn" type="button">
                    ${escapeHtml(tr("settings.password.change", null, "Change password"))}
                </button>
            </div>
        </div>
    </div>
` : "";

        const profileCard = `
    <div class="card" style="padding:14px; margin-top:12px;">
        <h3 style="margin:0 0 8px 0; font-size:18px;">
            ${escapeHtml(tr("settings.profile.title", null, "Profile"))}
        </h3>

        <div class="mini" style="line-height:1.5; margin-bottom:12px;">
            ${escapeHtml(tr("settings.profile.desc", null, "Edit your personal profile. Role, status, quota, storage, and admin notes are not editable here."))}
        </div>

        ${userProfileLoading ? `
            <div class="mini">${escapeHtml(tr("settings.profile.loading", null, "Loading profile…"))}</div>
        ` : userProfileError ? `
            <div class="bigState" style="display:block; margin-top:8px;">
                <h3>${escapeHtml(tr("settings.profile.load_failed", null, "Could not load profile"))}</h3>
                <p>${escapeHtml(userProfileError)}</p>
            </div>
        ` : `
            <div style="display:grid; gap:10px; max-width:540px;">
                <label>
                    <div class="mini" style="margin-bottom:4px;">${escapeHtml(tr("settings.profile.name", null, "Name"))}</div>
                    <input
                        id="userProfileName"
                        type="text"
                        value="${escapeHtml(p.name || "")}"
                        placeholder="${escapeHtml(tr("settings.profile.name_placeholder", null, "Your name"))}"
                        style="width:100%; box-sizing:border-box;"
                    >
                </label>

                <label>
                    <div class="mini" style="margin-bottom:4px;">${escapeHtml(tr("settings.profile.email", null, "Email"))}</div>
                    <input
                        id="userProfileEmail"
                        type="email"
                        value="${escapeHtml(p.email || "")}"
                        placeholder="you@example.com"
                        style="width:100%; box-sizing:border-box;"
                    >
                </label>

                <input
                    id="userProfileAvatarUrl"
                    type="hidden"
                    value="${escapeHtml(p.avatar_url || "")}"
                >

                <input
                    id="userProfileAvatarFile"
                    type="file"
                    accept="image/png,image/jpeg,image/webp"
                    style="display:none;"
                >

                <div style="display:flex; gap:12px; align-items:center; margin-top:4px;">
                    ${p.avatar_url ? `
                        <img
                            src="${escapeHtml(avatarUrlWithBust(p.avatar_url))}"
                            alt="${escapeHtml(tr("settings.profile.avatar_alt", null, "avatar"))}"
                            style="width:72px; height:72px; border-radius:16px; object-fit:cover; border:1px solid rgba(255,255,255,0.12); background:rgba(0,0,0,0.18);"
                            onerror="this.style.opacity='0.35'; this.title='${escapeHtml(tr("settings.profile.avatar_failed", null, "Avatar failed to load"))}';"
                        >
                    ` : `
                        <div style="width:72px; height:72px; border-radius:16px; display:flex; align-items:center; justify-content:center; border:1px solid rgba(255,255,255,0.12); background:rgba(0,0,0,0.18); color:var(--fg-dim);">
                            —
                        </div>
                    `}

                    <div style="display:flex; gap:10px; flex-wrap:wrap;">
                        <button class="btn secondary" id="userProfilePickAvatarBtn" type="button">
                            ${escapeHtml(tr("settings.profile.choose_avatar", null, "Choose avatar"))}
                        </button>
                        <div class="mini" style="margin-top:6px; line-height:1.4;">
                            ${escapeHtml(tr("settings.profile.avatar_help", null, "You can pick a normal photo. The server will resize it to a small avatar automatically. PNG, JPEG, and WebP work best."))}
                        </div>
                        ${p.avatar_url ? `
                            <button class="btn secondary" id="userProfileRemoveAvatarBtn" type="button">
                                ${escapeHtml(tr("settings.profile.remove_avatar", null, "Remove avatar"))}
                            </button>
                        ` : ""}
                    </div>
                </div>

                <div style="display:flex; gap:10px; flex-wrap:wrap; margin-top:8px;">
                    <button class="btn" id="userProfileSaveBtn" type="button">
                        ${escapeHtml(tr("settings.profile.save", null, "Save profile"))}
                    </button>
                    <button class="btn secondary" id="userProfileReloadBtn" type="button">
                        ${escapeHtml(tr("settings.profile.reload", null, "Reload"))}
                    </button>
                </div>
            </div>
        `}
    </div>
`;
        const themeCards = USER_THEME_OPTIONS.map((t) => {
            const checked = activeTheme === t.id ? "checked" : "";

            return `
            <label
                style="
                    display:block;
                    padding:14px;
                    border-radius:16px;
                    border:1px solid rgba(255,255,255,0.12);
                    background:rgba(0,0,0,0.20);
                    cursor:pointer;
                    margin-top:10px;
                "
            >
                <div style="display:flex; gap:12px; align-items:flex-start;">
                    <input
                        type="radio"
                        name="userTheme"
                        value="${escapeHtml(t.id)}"
                        ${checked}
                        style="margin-top:4px;"
                    >
                    <div>
                        <div style="font-weight:900;">${escapeHtml(tr(`settings.theme.${t.id}.title`, null, t.title))}</div>
                        <div class="mini">${escapeHtml(tr(`settings.theme.${t.id}.desc`, null, t.desc))}</div>
                        <div class="mini" style="margin-top:4px;">${escapeHtml(tr("settings.theme.id", { id: t.id }, `Theme ID: ${t.id}`))}</div>
                    </div>
                </div>
            </label>
        `;
        }).join("");

        function renderUserAppPrefsCard() {
            const apps = Array.isArray(installedApps) ? installedApps.slice() : [];

            if (!apps.length) {
                return `
                    <div class="card" style="padding:14px; margin-top:12px;">
                        <h3 style="margin:0 0 8px 0; font-size:18px;">
                            ${escapeHtml(tr("settings.apps.title", null, "Apps"))}
                        </h3>
                        <div class="mini">${escapeHtml(tr("settings.apps.none", null, "No installed apps found."))}</div>
                    </div>
                `;
            }

            const rows = apps.map((app) => {
                const id = String(app.id || "");
                if (!id) return "";

                const label = escapeHtml(app.name || app.title || id);
                const pol = launchPolicyForAppId(id);
                const pref = appUserPrefForAppId(id);
                const can = !!pol.allow_user_override;

                const sidebar =
                    typeof pref.show_in_sidebar === "boolean"
                        ? pref.show_in_sidebar
                        : (
                            typeof pol.show_in_sidebar === "boolean"
                                ? pol.show_in_sidebar
                                : true
                        );

                const launch = pref.default_launch || pol.default_launch || "auto";
                const win = pref.window_profile || pol.window_profile || "auto";
                const disabled = can ? "" : "disabled";

                return `
                    <div
                        class="card"
                        data-settings-app-row="${escapeHtml(id)}"
                        style="padding:12px; margin-top:10px; border-radius:14px;"
                    >
                        <div style="font-weight:900;">${label}</div>
                        <div class="mini mono">${escapeHtml(id)} · ${escapeHtml(app.ver || app.version || "")}</div>

                        ${can ? "" : `
                            <div class="mini" style="margin-top:6px;">
                                ${escapeHtml(tr("settings.apps.admin_locked", null, "Admin has disabled user override for this app."))}
                            </div>
                        `}

                        <label style="display:flex; gap:8px; align-items:center; margin-top:10px;">
                            <input
                                type="checkbox"
                                class="settingsAppSidebar"
                                data-app-id="${escapeHtml(id)}"
                                ${sidebar ? "checked" : ""}
                                ${disabled}
                            >
                            <span>${escapeHtml(tr("settings.apps.show_in_sidebar", null, "Show in left quick menu"))}</span>
                        </label>

                        <div style="display:grid; grid-template-columns:repeat(auto-fit,minmax(180px,1fr)); gap:10px; margin-top:10px;">
                            <label>
                                <div class="mini">${escapeHtml(tr("settings.apps.open_mode", null, "Open mode"))}</div>
                                <select class="settingsAppLaunch" data-app-id="${escapeHtml(id)}" ${disabled}>
                                    <option value="auto" ${launch === "auto" ? "selected" : ""}>${escapeHtml(tr("settings.apps.auto", null, "Auto"))}</option>
                                    <option value="embedded" ${launch === "embedded" ? "selected" : ""}>${escapeHtml(tr("settings.apps.embedded", null, "Embedded"))}</option>
                                    <option value="detached" ${launch === "detached" ? "selected" : ""}>${escapeHtml(tr("settings.apps.detached", null, "Own window"))}</option>
                                </select>
                            </label>

                            <label>
                                <div class="mini">${escapeHtml(tr("settings.apps.window_size", null, "Window size"))}</div>
                                <select class="settingsAppWindow" data-app-id="${escapeHtml(id)}" ${disabled}>
                                    <option value="auto" ${win === "auto" ? "selected" : ""}>${escapeHtml(tr("settings.apps.auto", null, "Auto"))}</option>
                                    <option value="small" ${win === "small" ? "selected" : ""}>${escapeHtml(tr("settings.apps.small", null, "Small"))}</option>
                                    <option value="normal" ${win === "normal" ? "selected" : ""}>${escapeHtml(tr("settings.apps.normal", null, "Normal"))}</option>
                                    <option value="large" ${win === "large" ? "selected" : ""}>${escapeHtml(tr("settings.apps.large", null, "Large"))}</option>
                                    <option value="full" ${win === "full" ? "selected" : ""}>${escapeHtml(tr("settings.apps.full", null, "Full"))}</option>
                                </select>
                            </label>
                        </div>
                    </div>
                `;
            }).join("");

            return `
                <div class="card" style="padding:14px; margin-top:12px;">
                    <h3 style="margin:0 0 8px 0; font-size:18px;">
                        ${escapeHtml(tr("settings.apps.title", null, "Apps"))}
                    </h3>
                    <div class="mini" style="line-height:1.5;">
                        ${escapeHtml(tr("settings.apps.desc", null, "Choose which installed apps appear in the left quick menu and how they open."))}
                    </div>
                    ${rows}
                    <div style="display:flex; gap:10px; flex-wrap:wrap; margin-top:14px;">
                        <button class="btn" id="settingsAppPrefsSaveBtn" type="button">
                            ${escapeHtml(tr("settings.apps.save", null, "Save app settings"))}
                        </button>
                    </div>
                </div>
            `;
        }

        const homeContent = setHomeContentHtml(`
        <div style="max-width:760px; font-family:var(--sans);">
            <h3 style="margin:0 0 8px 0; font-size:18px; font-family:inherit;">
                ${escapeHtml(tr("settings.general.title", null, "General settings"))}
            </h3>

            <div style="color:var(--fg-dim); line-height:1.5; margin-bottom:14px; font-family:inherit;">
                ${escapeHtml(tr("settings.general.desc", null, "These settings affect your own browser. They do not change the global admin theme."))}
            </div>

            ${messageText ? `
                <div class="bigState" style="display:block; margin-top:8px;">
                    <h3>${messageKind === "ok" ? escapeHtml(tr("settings.message.saved", null, "Saved")) : escapeHtml(tr("settings.message.settings", null, "Settings"))}</h3>
                    <p>${escapeHtml(messageText)}</p>
                </div>
            ` : ""}
            
        ${profileCard}
        ${passwordCard}
        ${renderUserAppPrefsCard()}
        
            <div class="card" style="padding:14px; margin-top:12px;">
                <h3 style="margin:0 0 8px 0; font-size:18px;">
                    ${escapeHtml(tr("settings.vault_recovery.title", null, "Vault / Master recovery key"))}
                </h3>

                <div class="mini" style="line-height:1.5;">
                    ${escapeHtml(tr("settings.vault_recovery.desc", null, "Master recovery protects only your own Vault files. DNA-Nexus stores only the public key. The private key is shown once and must be stored offline by the Vault owner."))}
                </div>

                <div style="display:flex; gap:8px; flex-wrap:wrap; margin-top:12px;">
                    <span class="chip">${escapeHtml(tr("settings.vault_recovery.status", null, "Status"))}: ${escapeHtml((userVaultMasterRecovery && userVaultMasterRecovery.enabled) ? tr("settings.vault_recovery.status_active", null, "active") : tr("settings.vault_recovery.status_not_configured", null, "not configured"))}</span>
                    <span class="chip">${escapeHtml(tr("settings.vault_recovery.key_id", null, "Key ID"))}: ${escapeHtml(shortMasterRecoveryKeyId(userVaultMasterRecovery && (userVaultMasterRecovery.recovery_key_id || userVaultMasterRecovery.public_key_sha256)))}</span>
                    <span class="chip">${escapeHtml(tr("settings.vault_recovery.created", null, "Created"))}: ${escapeHtml(formatMasterRecoveryCreated(userVaultMasterRecovery && userVaultMasterRecovery.created_at))}</span>
                </div>

                ${userVaultMasterRecoveryError ? `
                    <div class="msg err" style="margin-top:10px;">${escapeHtml(userVaultMasterRecoveryError)}</div>
                ` : ""}

                ${pendingVaultMasterRecoveryPrivateKeyB64 ? `
                    <div class="msg warn" style="margin-top:12px;">
                        ${escapeHtml(tr("settings.vault_recovery.private_key_warning", null, "Copy or download this private key now. It will not be shown again after this view is refreshed."))}
                    </div>

                    <textarea
                        id="settingsMasterRecoveryPrivateKey"
                        readonly
                        spellcheck="false"
                        style="width:100%; min-height:110px; margin-top:10px; font-family:var(--mono);"
                    >${escapeHtml(pendingVaultMasterRecoveryPrivateKeyB64)}</textarea>

                    <label style="display:flex; gap:8px; align-items:center; margin-top:10px;">
                        <input id="settingsMasterRecoveryAck" type="checkbox">
                        <span>${escapeHtml(tr("settings.vault_recovery.ack_stored", null, "I have stored this private key safely."))}</span>
                    </label>

                    <div style="display:flex; gap:10px; flex-wrap:wrap; margin-top:12px;">
                        <button class="btn secondary" id="settingsMasterRecoveryCopyBtn" type="button">${escapeHtml(tr("settings.vault_recovery.copy_private_key", null, "Copy private key"))}</button>
                        <button class="btn secondary" id="settingsMasterRecoveryDownloadBtn" type="button">${escapeHtml(tr("settings.vault_recovery.download_private_key_json", null, "Download private key JSON"))}</button>
                        <button class="btn primary" id="settingsMasterRecoverySaveBtn" type="button">${escapeHtml(tr("settings.vault_recovery.save_public_key", null, "Save public key"))}</button>
                        <button class="btn secondary" id="settingsMasterRecoveryDiscardBtn" type="button">${escapeHtml(tr("settings.vault_recovery.discard", null, "Discard"))}</button>
                    </div>
                ` : `
                    <div style="display:flex; gap:10px; flex-wrap:wrap; margin-top:14px;">
                        <button class="btn primary" id="settingsMasterRecoveryGenerateBtn" type="button">
                            ${escapeHtml((userVaultMasterRecovery && userVaultMasterRecovery.enabled) ? tr("settings.vault_recovery.rotate_button", null, "Rotate Master recovery key") : tr("settings.vault_recovery.generate_button", null, "Generate Master recovery key"))}
                        </button>
                        <button class="btn secondary" id="settingsMasterRecoveryReloadBtn" type="button">${escapeHtml(tr("settings.vault_recovery.reload_status", null, "Reload status"))}</button>
                    </div>
                `}

                <div class="mini" style="line-height:1.5; margin-top:12px;">
                    ${escapeHtml(tr("settings.vault_recovery.scope_note", null, "This is not a service-wide master key. It does not give this account access to other users' Vault files. Anyone with the private key can recover your Vault files protected by this key."))}
                </div>
            </div>

            <div class="card" style="padding:14px; margin-top:12px;">
                <h3 style="margin:0 0 8px 0; font-size:18px;">
                    ${escapeHtml(tr("settings.language.title", null, "Language"))}
                </h3>

                <div class="mini" style="line-height:1.5; margin-bottom:10px;">
                    ${escapeHtml(tr("settings.language.desc", null, "Choose the language used on this device."))}
                </div>

                <div
                    role="radiogroup"
                    aria-label="${escapeHtml(tr("settings.language.title", null, "Language"))}"
                    style="
                        display:grid;
                        grid-template-columns:repeat(auto-fit,minmax(180px,1fr));
                        gap:8px;
                        max-width:640px;
                    "
                >
                    ${languageOptions.map(langChoice).join("")}
                </div>
            </div>

            <div class="card" style="padding:14px; margin-top:12px;">
                <h3 style="margin:0 0 8px 0; font-size:18px;">
                    ${escapeHtml(tr("settings.theme.title", null, "Theme"))}
                </h3>

                <div class="mini" style="line-height:1.5;">
                    ${escapeHtml(tr("settings.theme.desc", null, "Choose how the interface looks on this device."))}
                </div>

                <div style="margin-top:12px;">
                    ${themeCards}
                </div>

                <div style="display:flex; gap:10px; flex-wrap:wrap; margin-top:14px;">
                    <button class="btn secondary" id="userThemeDefaultBtn" type="button">
                        ${escapeHtml(tr("settings.theme.default_dark", null, "Use default dark theme"))}
                    </button>
                </div>
            </div>
        </div>
    `);
        fitHomeContentToViewport();

        const appPrefsSaveBtn = (homeContent || homeBlurb).querySelector("#settingsAppPrefsSaveBtn");
        if (appPrefsSaveBtn) {
            appPrefsSaveBtn.addEventListener("click", async () => {
                const prefs = loadAppUserPrefs();

                for (const app of installedApps || []) {
                    const id = String(app && app.id ? app.id : "");
                    if (!id) continue;

                    const pol = launchPolicyForAppId(id);
                    if (!pol.allow_user_override) continue;

                    const escAttr = (v) => String(v).replace(/\\/g, "\\\\").replace(/"/g, "\\\"");
                    const selectorId = escAttr(id);

                    const sidebar = (homeContent || homeBlurb).querySelector(`.settingsAppSidebar[data-app-id="${selectorId}"]`);
                    const launch = (homeContent || homeBlurb).querySelector(`.settingsAppLaunch[data-app-id="${selectorId}"]`);
                    const win = (homeContent || homeBlurb).querySelector(`.settingsAppWindow[data-app-id="${selectorId}"]`);

                    prefs[id] = {
                        show_in_sidebar: !!(sidebar && sidebar.checked),
                        default_launch: launch ? launch.value : "auto",
                        window_profile: win ? win.value : "auto"
                    };
                }

                saveAppUserPrefs(prefs);

                try {
                    await saveAppUserPrefsToServer(prefs);
                } catch (e) {
                    renderUserSettings(
                        tr(
                            "settings.apps.save_failed",
                            { error: String(e && e.message ? e.message : e) },
                            `App settings save failed: ${String(e && e.message ? e.message : e)}`
                        ),
                        "err"
                    );
                    return;
                }

                lastAppsKey = "";
                await loadApps();

                if (currentView === "home") {
                    await renderDesktopIcons();
                }

                renderUserSettings(tr("settings.apps.saved", null, "App settings saved."), "ok");
            });
        }

        const profileSaveBtn = (homeContent || homeBlurb).querySelector("#userProfileSaveBtn");
        if (profileSaveBtn) {
            profileSaveBtn.addEventListener("click", async () => {
                profileSaveBtn.disabled = true;
                profileSaveBtn.textContent = tr("settings.profile.saving", null, "Saving…");

                try {
                    await saveUserProfileFromSettings();
                    renderUserSettings(tr("settings.profile.saved", null, "Profile saved."), "ok");
                } catch (e) {
                    renderUserSettings(tr("settings.profile.save_failed", { error: String(e && e.message ? e.message : e) }, `Profile save failed: ${String(e && e.message ? e.message : e)}`), "err");
                }
            });
        }

        const profileReloadBtn = (homeContent || homeBlurb).querySelector("#userProfileReloadBtn");
        if (profileReloadBtn) {
            profileReloadBtn.addEventListener("click", async () => {
                profileReloadBtn.disabled = true;
                profileReloadBtn.textContent = tr("settings.profile.reloading", null, "Reloading…");

                try {
                    await loadUserProfile();
                    renderUserSettings();
                } catch (e) {
                    renderUserSettings(tr("settings.profile.reload_failed", { error: String(e && e.message ? e.message : e) }, `Profile reload failed: ${String(e && e.message ? e.message : e)}`), "err");
                }
            });
        }


        const passwordChangeBtn = (homeContent || homeBlurb).querySelector("#settingsPasswordChangeBtn");
        if (passwordChangeBtn) {
            passwordChangeBtn.addEventListener("click", async () => {
                const oldText = passwordChangeBtn.textContent;
                passwordChangeBtn.disabled = true;
                passwordChangeBtn.textContent = tr("settings.password.changing", null, "Changing…");

                try {
                    await changeOwnPasswordFromSettings();
                    renderUserSettings(tr("settings.password.changed", null, "Password changed."), "ok");
                } catch (e) {
                    const msg = tr("settings.password.change_failed", { error: String(e && e.message ? e.message : e) }, `Password change failed: ${String(e && e.message ? e.message : e)}`);
                    renderUserSettings(msg, "err");
                } finally {
                    passwordChangeBtn.disabled = false;
                    passwordChangeBtn.textContent = oldText;
                }
            });
        }

        const pickAvatarBtn = (homeContent || homeBlurb).querySelector("#userProfilePickAvatarBtn");
        const avatarFileInput = (homeContent || homeBlurb).querySelector("#userProfileAvatarFile");

        if (pickAvatarBtn && avatarFileInput) {
            pickAvatarBtn.addEventListener("click", () => {
                avatarFileInput.click();
            });

            avatarFileInput.addEventListener("change", async () => {
                const file = avatarFileInput.files && avatarFileInput.files[0];
                if (!file) return;

                pickAvatarBtn.disabled = true;
                pickAvatarBtn.textContent = tr("settings.profile.uploading", null, "Uploading…");

                try {
                    const result = await uploadUserAvatarFromSettings(file);
                    renderUserSettings(result && result.note ? tr("settings.profile.avatar_uploaded_note", { note: result.note }, `Avatar uploaded. ${result.note}`) : tr("settings.profile.avatar_uploaded", null, "Avatar uploaded."), "ok");
                } catch (e) {
                    const msg = tr("settings.profile.avatar_upload_failed", { error: String(e && e.message ? e.message : e) }, `Avatar upload failed: ${String(e && e.message ? e.message : e)}`);
                    await openShellAlertDialog({
                        title: tr("settings.profile.avatar_upload_failed_title", null, "Avatar upload failed"),
                        message: msg
                    });
                    renderUserSettings(msg, "err");
                } finally {
                    avatarFileInput.value = "";
                }
            });
        }

        const removeAvatarBtn = (homeContent || homeBlurb).querySelector("#userProfileRemoveAvatarBtn");
        if (removeAvatarBtn) {
            removeAvatarBtn.addEventListener("click", async () => {
                const ok = await openShellConfirmDialog({
                    title: tr("settings.profile.remove_avatar_title", null, "Remove avatar?"),
                    message: tr("settings.profile.remove_confirm", null, "Remove your avatar?"),
                    confirmText: tr("settings.profile.remove_avatar_confirm", null, "Remove avatar"),
                    cancelText: tr("settings.profile.remove_avatar_cancel", null, "Cancel"),
                    danger: true
                });
                if (!ok) return;

                removeAvatarBtn.disabled = true;
                removeAvatarBtn.textContent = tr("settings.profile.removing", null, "Removing…");

                try {
                    await removeUserAvatarFromSettings();
                    renderUserSettings(tr("settings.profile.avatar_removed", null, "Avatar removed."), "ok");
                } catch (e) {
                    renderUserSettings(tr("settings.profile.avatar_remove_failed", { error: String(e && e.message ? e.message : e) }, `Avatar remove failed: ${String(e && e.message ? e.message : e)}`), "err");
                }
            });
        }
        const masterRecoveryGenerateBtn = (homeContent || homeBlurb).querySelector("#settingsMasterRecoveryGenerateBtn");
        if (masterRecoveryGenerateBtn) {
            masterRecoveryGenerateBtn.addEventListener("click", async () => {
                masterRecoveryGenerateBtn.disabled = true;
                masterRecoveryGenerateBtn.textContent = tr("settings.vault_recovery.generating", null, "Generating…");

                try {
                    // UX/safety: open the one-time key modal before doing expensive
                    // crypto work. If ML-KEM blocks or fails, the user sees the
                    // progress/error in the modal instead of a silent stuck button.
                    ensureMasterRecoveryModalStyles();
                    closeMasterRecoveryModal();

                    const root = document.createElement("div");
                    root.id = "settingsMasterRecoveryModal";
                    root.className = "settingsMasterRecoveryOverlay";
                    root.setAttribute("role", "dialog");
                    root.setAttribute("aria-modal", "true");
                    root.innerHTML = `
                        <div class="settingsMasterRecoveryDialog">
                            <div class="settingsMasterRecoveryHead">
                                <div class="settingsMasterRecoveryTitle">${escapeHtml(tr("settings.vault_recovery.private_key_title", null, "Master recovery private key"))}</div>
                                <button class="btn secondary" type="button" data-action="close">×</button>
                            </div>
                            <div class="settingsMasterRecoveryBody">
                                <div class="settingsMasterRecoveryWarning">
                                    ${escapeHtml(tr("settings.vault_recovery.generating_locally", null, "Generating Master recovery key locally in this browser…"))}
                                </div>
                                <div id="settingsMasterRecoveryModalStatus" class="mini" style="line-height:1.5; margin-top:10px;">
                                    ${escapeHtml(tr("settings.vault_recovery.loading_helper", null, "Loading ML-KEM helper…"))}
                                </div>
                            </div>
                            <div class="settingsMasterRecoveryFoot">
                                <span class="mini">${escapeHtml(tr("settings.vault_recovery.do_not_close", null, "Do not close this window until the key is ready."))}</span>
                                <button class="btn secondary" type="button" data-action="discard">${escapeHtml(tr("admin.common.cancel", null, "Cancel"))}</button>
                            </div>
                        </div>
                    `;

                    document.body.appendChild(root);

                    const setModalStatus = (msg, kind = "") => {
                        const status = document.getElementById("settingsMasterRecoveryModalStatus");
                        if (!status) return;
                        status.textContent = msg || "";
                        status.className = kind ? `msg ${kind}` : "mini";
                    };

                    root.addEventListener("click", async (ev) => {
                        const btn = ev.target && ev.target.closest ? ev.target.closest("[data-action]") : null;
                        if (!btn) return;

                        const action = btn.getAttribute("data-action");
                        if (action === "close") {
                            setModalStatus(tr("settings.vault_recovery.generation_running", null, "Key generation is still running. Use Cancel only if you want to abandon this view."), "warn");
                            return;
                        }

                        if (action === "discard") {
                            pendingVaultMasterRecoveryPrivateKeyB64 = "";
                            pendingVaultMasterRecoveryPublicKeyB64 = "";
                            pendingVaultMasterRecoveryPublicKeySha256 = "";
                            pendingVaultMasterRecoveryCreatedAt = 0;
                            closeMasterRecoveryModal();
                            renderUserSettings("Master recovery key generation cancelled.", "ok");
                        }
                    });

                    await new Promise((resolve) => window.setTimeout(resolve, 30));

                    setModalStatus(tr("settings.vault_recovery.generating_keypair", null, "Generating ML-KEM keypair…"));
                    const generated = await generateUserVaultMasterRecoveryKey();

                    if (!generated) {
                        closeMasterRecoveryModal();
                        renderUserSettings("Master recovery key generation cancelled.", "ok");
                        return;
                    }

                    setModalStatus(tr("settings.vault_recovery.key_generated_opening", null, "Key generated. Opening private-key view…"));
                    await openUserVaultMasterRecoveryPrivateKeyDialog();
                } catch (e) {
                    const msg = `Master recovery generation failed: ${String(e && e.message ? e.message : e)}`;
                    const status = document.getElementById("settingsMasterRecoveryModalStatus");
                    if (status) {
                        status.textContent = msg;
                        status.className = "msg err";
                    } else {
                        renderUserSettings(msg, "err");
                    }
                } finally {
                    if (masterRecoveryGenerateBtn.isConnected) {
                        const active = !!(userVaultMasterRecovery && userVaultMasterRecovery.enabled);
                        masterRecoveryGenerateBtn.disabled = false;
                        masterRecoveryGenerateBtn.textContent = active
                            ? tr("settings.vault_recovery.rotate_button", null, "Rotate Master recovery key")
                            : tr("settings.vault_recovery.generate_button", null, "Generate Master recovery key");
                    }
                }
            });
        }

        const masterRecoveryReloadBtn = (homeContent || homeBlurb).querySelector("#settingsMasterRecoveryReloadBtn");
        if (masterRecoveryReloadBtn) {
            masterRecoveryReloadBtn.addEventListener("click", async () => {
                await loadUserVaultMasterRecovery();
                renderUserSettings(tr("settings.vault_recovery.status_reloaded", null, "Master recovery status reloaded."), "ok");
            });
        }

        const masterRecoveryCopyBtn = (homeContent || homeBlurb).querySelector("#settingsMasterRecoveryCopyBtn");
        if (masterRecoveryCopyBtn) {
            masterRecoveryCopyBtn.addEventListener("click", async () => {
                const value = String(pendingVaultMasterRecoveryPrivateKeyB64 || "");
                if (!value) return;

                try {
                    await navigator.clipboard.writeText(value);
                    renderUserSettings(tr("settings.vault_recovery.private_key_copied", null, "Private key copied."), "ok");
                } catch (e) {
                    const el = document.getElementById("settingsMasterRecoveryPrivateKey");
                    try {
                        el?.focus();
                        el?.select();
                        document.execCommand("copy");
                        renderUserSettings(tr("settings.vault_recovery.private_key_copied", null, "Private key copied."), "ok");
                    } catch {
                        renderUserSettings(`Copy failed: ${String(e && e.message ? e.message : e)}`, "err");
                    }
                }
            });
        }

        const masterRecoveryDownloadBtn = (homeContent || homeBlurb).querySelector("#settingsMasterRecoveryDownloadBtn");
        if (masterRecoveryDownloadBtn) {
            masterRecoveryDownloadBtn.addEventListener("click", () => {
                try {
                    downloadPendingMasterRecoveryPrivateKey();
                } catch (e) {
                    renderUserSettings(`Download failed: ${String(e && e.message ? e.message : e)}`, "err");
                }
            });
        }

        const masterRecoverySaveBtn = (homeContent || homeBlurb).querySelector("#settingsMasterRecoverySaveBtn");
        if (masterRecoverySaveBtn) {
            masterRecoverySaveBtn.addEventListener("click", async () => {
                masterRecoverySaveBtn.disabled = true;
                masterRecoverySaveBtn.textContent = "Saving…";

                try {
                    await saveUserVaultMasterRecoveryPublicKey();
                    renderUserSettings("Master recovery public key saved.", "ok");
                } catch (e) {
                    renderUserSettings(`Master recovery save failed: ${String(e && e.message ? e.message : e)}`, "err");
                }
            });
        }

        const masterRecoveryDiscardBtn = (homeContent || homeBlurb).querySelector("#settingsMasterRecoveryDiscardBtn");
        if (masterRecoveryDiscardBtn) {
            masterRecoveryDiscardBtn.addEventListener("click", async () => {
                const ok = await openShellConfirmDialog({
                    title: tr("settings.vault_recovery.discard_confirm_title", null, "Discard generated private key?"),
                    message: tr("settings.vault_recovery.discard_confirm_message", null, "The generated private key and public key will be forgotten by this browser view."),
                    confirmText: tr("settings.vault_recovery.discard", null, "Discard"),
                    cancelText: tr("admin.common.cancel", null, "Cancel"),
                    danger: true
                });

                if (!ok) return;

                pendingVaultMasterRecoveryPrivateKeyB64 = "";
                pendingVaultMasterRecoveryPublicKeyB64 = "";
                pendingVaultMasterRecoveryPublicKeySha256 = "";
                pendingVaultMasterRecoveryCreatedAt = 0;
                renderUserSettings(tr("settings.vault_recovery.generated_key_discarded", null, "Generated Master recovery key discarded."), "ok");
            });
        }

        for (const input of (homeContent || homeBlurb).querySelectorAll('input[name="userLanguage"]')) {
            input.addEventListener("change", () => {
                if (!input.checked) return;
                applyUserLanguage(input.value);
            });
        }

        for (const input of (homeContent || homeBlurb).querySelectorAll('input[name="userTheme"]')) {
            input.addEventListener("change", () => {
                if (!input.checked) return;
                applyUserTheme(input.value);
            });
        }

        const defaultBtn = document.getElementById("userThemeDefaultBtn");
        if (defaultBtn) {
            defaultBtn.addEventListener("click", () => {
                applyUserTheme("dark");
            });
        }
    }

    function renderPeople(messageText = "", messageKind = "") {
        stopPairPolling();

        currentView = "people";
        currentApp = null;

        setActiveNav("nav_people");
        setActiveApp("");

        if (wsTitle) wsTitle.textContent = tr("people.title", null, "People");
        if (wsSubtitle) wsSubtitle.textContent = tr("people.subtitle", null, "Friendly names for DNA fingerprints and workspace collaborators");
        if (mainPaneTitle) mainPaneTitle.textContent = tr("people.title", null, "People");

        if (!homeBlurb) return;

        setMainHostMode("home");
        homeBlurb.classList.remove("appHostBlurb");

        if (window.PQPeople && typeof window.PQPeople.render === "function") {
            window.PQPeople.render({
                homeBlurb,
                messageText,
                messageKind
            });
            return;
        }

        homeBlurb.innerHTML = `
            <div class="card" style="padding:16px; margin-top:12px;">
                <h3 style="margin:0 0 8px 0;">${escapeHtml(tr("people.unavailable", null, "People unavailable"))}</h3>
                <div class="mini">${escapeHtml(tr("people.module_missing", null, "The People UI module did not load. Hard-refresh the browser and try again."))}</div>
            </div>
        `;
    }

    function renderWorkspaceInvites(messageText = "", messageKind = "", inviteNotice = null) {
        stopPairPolling();

        currentView = "workspace_invites";
        currentApp = null;

        setActiveNav("nav_workspace_invites");
        setActiveApp("");

        if (wsTitle) wsTitle.textContent = "Workspace Invites";
        if (wsSubtitle) wsSubtitle.textContent = "Accept or decline pending workspace invitations";
        if (mainPaneTitle) mainPaneTitle.textContent = "Workspace Invites";

        if (!homeBlurb) return;
        const hasFileMgr = Array.isArray(installedApps) && installedApps.some(a => a && a.id === "filemgr");

        setMainHostMode("home");
        homeBlurb.classList.remove("appHostBlurb");

        const cards = (workspaceInvites || []).map((inv) => {
            const notes = inv.notes ? `
            <div class="mini" style="margin-top:8px;">${escapeHtml(inv.notes)}</div>
        ` : "";

            const addedBy = inv.added_by ? `
                <div class="mini" title="${escapeHtml(inv.added_by)}">
                    Invited by: ${escapeHtml(middleEllipsis(inv.added_by, 20, 18))}
                </div>
            ` : "";

            const addedAt = inv.added_at ? `
            <div class="mini">Invited at: ${escapeHtml(inv.added_at)}</div>
        ` : "";

            return `
            <div class="card" style="padding:14px; margin-top:10px; border:1px solid rgba(255,255,255,0.10); border-radius:16px; background:rgba(0,0,0,0.20);">
                <div style="font-weight:900; font-size:16px;">${escapeHtml(inv.name || inv.workspace_id || "Workspace")}</div>
                <div class="mini">Role: ${escapeHtml(inv.role || "viewer")}</div>
                ${addedBy}
                ${addedAt}
                ${notes}
                <div style="display:flex; gap:10px; flex-wrap:wrap; margin-top:12px;">
                    <button class="btn workspaceInviteAcceptBtn" type="button" data-workspace-id="${escapeHtml(inv.workspace_id || "")}">
                        Accept
                    </button>
                    <button class="btn secondary workspaceInviteDeclineBtn" type="button" data-workspace-id="${escapeHtml(inv.workspace_id || "")}">
                        Decline
                    </button>
                </div>
            </div>
        `;
        }).join("");

        const homeContent = setHomeContentHtml(`
        <div style="max-width:760px; font-family:var(--sans);">
            <h3 style="margin:0 0 8px 0; font-size:18px; font-family:inherit;">Pending workspace invitations</h3>
            <div style="color:var(--fg-dim); line-height:1.5; margin-bottom:14px; font-family:inherit;">
                Accepted workspaces will appear in the File Manager location switcher. Declined invites stay hidden.
            </div>

        ${(messageKind === "ok" && inviteNotice && inviteNotice.action === "accepted") ? `
            <div class="bigState" style="display:block; margin-top:8px;">
                <h3>Workspace accepted</h3>
                <p>
                    You now have access to
                    <strong>${escapeHtml(inviteNotice.workspaceName || inviteNotice.workspaceId || "this workspace")}</strong>.
                </p>
                <p style="margin-top:8px;">
                    Open File Manager and use the <strong>Location</strong> dropdown in the top bar to switch from
                    <strong>My files</strong> to this workspace.
                </p>
                ${hasFileMgr ? `
                    <div style="margin-top:10px;">
                        <button class="btn" id="workspaceInviteOpenFilemgrBtn" type="button">Open File Manager</button>
                    </div>
                ` : ""}
            </div>
        ` : (messageText ? `
            <div class="bigState" style="display:block; margin-top:8px;">
                <h3>${messageKind === "ok" ? "Success" : "Workspace invite"}</h3>
                <p>${escapeHtml(messageText)}</p>
            </div>
        ` : "")}

            ${workspaceInvitesError ? `
                <div class="bigState" style="display:block; margin-top:8px;">
                    <h3>Could not load invitations</h3>
                    <p>${escapeHtml(workspaceInvitesError)}</p>
                </div>
            ` : ""}

            ${(workspaceInvites && workspaceInvites.length)
            ? cards
            : `<div class="mini">No pending workspace invitations.</div>`}
        </div>
        `);

            for (const btn of (homeContent || homeBlurb).querySelectorAll(".workspaceInviteAcceptBtn")) {
            btn.addEventListener("click", async () => {
                const workspaceId = btn.dataset.workspaceId || "";
                if (!workspaceId) return;

                btn.disabled = true;
                btn.textContent = "Accepting…";

                try {
                    const j = await acceptWorkspaceInvite(workspaceId);
                    await loadWorkspaceInvites();

                    const ws = j && j.workspace ? j.workspace : null;
                    renderWorkspaceInvites("", "ok", {
                        action: "accepted",
                        workspaceId: workspaceId,
                        workspaceName: ws ? (ws.name || ws.workspace_id || workspaceId) : workspaceId
                    });
                } catch (e) {
                    renderWorkspaceInvites(`Accept failed: ${String(e && e.message ? e.message : e)}`, "err");
                }
            });
        }

        for (const btn of (homeContent || homeBlurb).querySelectorAll(".workspaceInviteDeclineBtn")) {
            btn.addEventListener("click", async () => {
                const workspaceId = btn.dataset.workspaceId || "";
                if (!workspaceId) return;

                btn.disabled = true;
                btn.textContent = "Declining…";

                try {
                    await declineWorkspaceInvite(workspaceId);
                    await loadWorkspaceInvites();
                    renderWorkspaceInvites(`Declined workspace invitation: ${workspaceId}`, "ok");
                } catch (e) {
                    renderWorkspaceInvites(`Decline failed: ${String(e && e.message ? e.message : e)}`, "err");
                }
            });
        }
        const openFileMgrBtn = document.getElementById("workspaceInviteOpenFilemgrBtn");
        if (openFileMgrBtn) {
            openFileMgrBtn.addEventListener("click", () => {
                openAppById("filemgr");
            });
        }
    }

    function renderApp(app) {
        stopPairPolling();

        currentView = `app:${app.id}@${app.ver}`;
        currentApp = { id: app.id, ver: app.ver };

        setActiveNav("");
        setActiveApp(app.id);

        if (wsTitle) wsTitle.textContent = app.name || app.title || app.id || "App";
        if (wsSubtitle) wsSubtitle.textContent = "Running app";
        if (mainPaneTitle) mainPaneTitle.textContent = app.name || app.title || app.id || "App";

        setMainHostMode("app");

        const dock = getAppFrameDock();
        if (!dock) return;

        dock.style.display = "";
        dock.style.pointerEvents = "auto";

        const key = appFrameKey(app);
        const now = Date.now();

        const appOpeningLabel = "Opening " + (app.name || app.title || app.id || "app") + "...";
        showAppOpeningOverlay(appOpeningLabel, key);

        const appOpeningSlowTimer = window.setTimeout(function() {
            updateAppOpeningOverlay("Still opening... waiting for app files.", key);
        }, 7000);

        // Keep cached iframes alive, but only one visible/interactive.
        for (const rec of appFrameCache.values()) {
            if (!rec || !rec.frameWrap) continue;

            rec.frameWrap.classList.remove("active");
            rec.frameWrap.hidden = true;
            rec.frameWrap.style.pointerEvents = "none";
        }

        let rec = appFrameCache.get(key);

        if (!rec) {
            const frame = document.createElement("iframe");
            frame.className = "appFrame";
            frame.dataset.appId = app.id;
            frame.dataset.appVer = app.ver;
            frame.dataset.appKey = key;

            frame.addEventListener("load", function() {
                frame.dataset.appLoaded = "1";
                window.clearTimeout(appOpeningSlowTimer);
                hideAppOpeningOverlay(key);
            });

            frame.src = appUrl(app, "embedded");

            const frameWrap = document.createElement("div");
            frameWrap.className = "appFrameWrap";
            frameWrap.dataset.appKey = key;
            frameWrap.hidden = true;
            frameWrap.appendChild(frame);

            dock.appendChild(frameWrap);

            rec = {
                frameWrap,
                frame,
                lastUsed: now
            };

            appFrameCache.set(key, rec);
        }

        rec.lastUsed = now;

        rec.frameWrap.hidden = false;
        rec.frameWrap.classList.add("active");
        rec.frameWrap.style.pointerEvents = "auto";
        rec.frameWrap.style.width = "100%";
        rec.frameWrap.style.height = "100%";
        rec.frameWrap.style.flex = "1 1 auto";

        if (rec.frame && rec.frame.dataset.appLoaded === "1") {
            window.clearTimeout(appOpeningSlowTimer);
            hideAppOpeningOverlay(key);
        }

        pruneCachedAppFrames(key);
    }


    function openAppById(appId) {
        if (appId === NOTEPAD_APP_ID) {
            openNotepadWindow();
            return;
        }

        const matches = installedApps.filter(x => x.id === appId);
        const a = matches.length ? matches[matches.length - 1] : null;

        if (!a) {
            renderHome();
            return;
        }

        if (a.compatibility_ok === false) {
            const min = String(a.min_server_version || "?");
            const current = String(a.server_version || "?");

            const msg = tr(
                "shell.apps.incompatible_message_versions",
                { min, current },
                `This app requires server version ${min} or newer. Current server is ${current}.`
            );

            openShellAlertDialog({
                title: tr("shell.apps.incompatible_title", null, "App requires newer server"),
                message: msg
            });

            return;
        }

        const app = { id: a.id, ver: a.ver, name: a.name || a.title };
        const mode = resolveLaunchMode(app);

        if (mode === "detached") {
            const opened = openAppDetached(app);
            if (opened) return;

            // popup blocked -> fallback to embedded
        }

        renderApp(app);
    }
    async function loadApps() {
        if (!appsList) return;

        try {
            const [rList, rApps] = await Promise.all([
                fetch("/api/v4/apps/list", { credentials: "include", cache: "no-store" }),
                fetch("/api/v4/apps", { credentials: "include", cache: "no-store" })
            ]);

            const jList = await rList.json().catch(() => null);
            const jApps = await rApps.json().catch(() => null);

            if (!rList.ok || !jList || !jList.ok) return;

            launchPolicyByAppId =
                (jApps && jApps.launch_policy_by_app_id && typeof jApps.launch_policy_by_app_id === "object")
                    ? jApps.launch_policy_by_app_id
                    : {};

            const installed = Array.isArray(jList.installed) ? jList.installed : [];

            // server uses: {id, ver, name?, title? ...}
            let usable = installed.filter(x => x && x.id && x.ver);

            // Admin-only apps (UI visibility)
            if (!isAdmin) usable = usable.filter(x => x.id !== "snapshotmgr");

            // Only show one desktop/sidebar entry per app id.
            // When multiple versions are installed, use the newest version.
            usable = newestInstalledAppsById(usable);

            // stable order: id then ver
            usable.sort((a, b) => {
                const ai = String(a.id || "");
                const bi = String(b.id || "");
                if (ai !== bi) return ai.localeCompare(bi);
                return compareAppVersions(a.ver, b.ver);
            });

            await loadAppUserPrefsFromServer();

            const key = JSON.stringify({
                apps: usable.map(x => [x.id, x.ver, x.name || x.title || ""]),
                policy: launchPolicyByAppId || {},
                user_prefs: loadAppUserPrefs()
            });
            installedApps = usable;

            if (key === lastAppsKey) return;
            lastAppsKey = key;

            if (currentView === "home") renderDesktopIcons();

            clearAppsList();
            addNotepadNavButton();
            restoreNotepadWindowIfOpen();

            for (const a of usable) {
                const mani = await fetchManifest(a.id, a.ver);

                const effective = effectiveLaunchPolicyForApp(a, mani);
                if (!effective.show_in_sidebar) {
                    continue;
                }

                const label = (mani && mani.name) || a.name || a.title || a.id;
                const href = appUrl(a, "embedded");
                addAppNavButton(a.id, label, href, resolveSidebarIconUrl(a, mani));
            }

            if (currentApp) {
                if (!isAdmin && currentApp.id === "snapshotmgr") {
                    renderHome();
                } else {
                    const still = installedApps.find(x => x.id === currentApp.id);
                    if (!still) {
                        renderHome();
                    } else {
                        renderApp({ id: still.id, ver: still.ver, name: still.name || still.title });
                    }
                }
            }
        } catch {
            // ignore
        }
    }


        async function loadMe() {
            authed = false;
            isAdmin = false;
            show(stateDisabled, false);
            show(stateUnauth, false);

            try {
                const r = await fetch("/api/v4/me", { credentials: "include", cache: "no-store" });
                const ct = (r.headers.get("content-type") || "").toLowerCase();
                const txt = await r.text();

                let j = null;
                try { j = JSON.parse(txt); } catch {}

                if (j) {
                    if (out) out.textContent = JSON.stringify(j, null, 2);
                    meFpHex = String(j.fingerprint_hex || "");

                    const role = j.role || "?";
                    const ok = !!j.ok;
                    authed = ok && r.ok;

                    // Show backend version from version.h via /api/v4/me.
                    // statusLine starts as an i18n placeholder ("Loading…").
                    // Remove data-i18n before writing the dynamic version, otherwise
                    // a late i18n apply() can translate it back to "Loading…".
                    if (statusLine) {
                        const serverVersion = String(j.server_version || j.current_server_version || j.version || "").trim();
                        statusLine.removeAttribute("data-i18n");
                        statusLine.removeAttribute("data-i18n-fallback");
                        const productShortName = await brandedProductShortNameReady("Server");
                        statusLine.textContent = serverVersion ? `${productShortName} v${serverVersion}` : productShortName;
                        versionShown = true;
                    }

                    isAdmin = ok && role === "admin";

                    const externalWorkspaceRedirected =
                        await maybeRedirectExternalWorkspaceOnly(ok, isAdmin);
                    if (externalWorkspaceRedirected) return;

                    // show admin-only links
                    show(navAdmin, isAdmin);
                    show(navUsers, isAdmin);
                    show(navAudit, isAdmin);
                    show(navSettings, isAdmin);

                    // signed-in vs not-signed-in nav
                    show(navLogin, !ok);

                    // People is user-owned contact data and is available to any signed-in user.
                    show(navPeople, ok);

                    // Normal user settings are only for non-admin users.
                    // Admins use /admin/settings instead.
                    show(navUserSettings, ok && !isAdmin);

                    if (ok && isAdmin && currentView === "user_settings") {
                        renderHome();
                    }

                    if (!r.ok || !ok) {
                        authed = false;
                        clearCachedAppFrames();

                        workspaceInvites = [];
                        workspaceInvitesError = "";

                        userProfile = null;
                        userProfileLoading = false;
                        userProfileError = "";

                        updateWorkspaceInvitesNav();
                        updateHomeInvitesHint();
                        show(navPeople, false);
                        show(navUserSettings, false);
                        const err = String(j.error || "").toLowerCase();
                        const msg = String(j.message || "");

                        if (r.status === 403 || err.includes("disabled") || msg.toLowerCase().includes("disabled")) {
                            setWsSubtitleSafe(tr("shell.status.waiting_admin_approval", null, "Waiting for admin approval"));
                            setBadge("warn", tr("shell.badge.waiting_admin", null, "waiting for admin"));
                            show(stateDisabled, true);
                        } else if (r.status === 401 || err.includes("unauthorized") || msg.toLowerCase().includes("unauthorized")) {
                            setWsSubtitleSafe(tr("shell.status.not_signed_in", null, "Not signed in"));
                            setBadge("warn", tr("shell.badge.not_signed_in", null, "not signed in"));
                            show(stateUnauth, true);
                            show(navLogin, true);
                            show(navPeople, false);
                        show(navUserSettings, false);
                        } else {
                            setWsSubtitleSafe(tr("shell.status.error_code", { code: r.status || "?" }, `Error (${r.status || "?"})`));
                            setBadge("err", "error");
                        }
                        return;
                    }

                    const st = String(j.storage_state || "unallocated");
                    setBadge("ok", "");
                    setWsSubtitleSafe(tr("shell.status.signed_in_role_storage", { role, storage: st }, `Signed in · ${role} · storage: ${st}`));

                    // Update installed apps list (only when signed in ok)
                    // NOTE: don't await; keep UI snappy and avoid blocking auth render
                    loadApps();
                    loadWorkspaceInvites();

                    return;
                }

// Non-JSON body
                authed = false;
                clearCachedAppFrames();

                workspaceInvites = [];
                workspaceInvitesError = "";

                userProfile = null;
                userProfileLoading = false;
                userProfileError = "";

                updateWorkspaceInvitesNav();
                updateHomeInvitesHint();

                show(navAdmin, false);
                show(navUsers, false);
                show(navAudit, false);
                show(navSettings, false);
                show(navLogin, true);

                setWsSubtitleSafe(tr("shell.status.unexpected_me_response", null, "Unexpected response from /api/v4/me"));
                setBadge("err", tr("shell.badge.unexpected_response", null, "unexpected response"));

                if (out) {
                    const body = txt.length > 4000 ? (txt.slice(0, 4000) + "\n…(truncated)…") : txt;
                    out.textContent = `${r.status}${ct ? " · " + ct.split(";")[0] : ""}\n\n${body}`;
                }
            } catch (e) {
                authed = false;
                clearCachedAppFrames();

                workspaceInvites = [];
                workspaceInvitesError = "";

                userProfile = null;
                userProfileLoading = false;
                userProfileError = "";

                updateWorkspaceInvitesNav();
                updateHomeInvitesHint();

                show(navAdmin, false);
                show(navUsers, false);
                show(navAudit, false);
                show(navSettings, false);
                show(navLogin, true);

                setWsSubtitleSafe(tr("shell.status.network_error", null, "Network error"));
                setBadge("err", tr("shell.badge.network_error", null, "network error"));
                if (statusLine) statusLine.textContent = tr("shell.status.failed_load_me", null, "Failed to load /api/v4/me");
                if (out) out.textContent = String(e && e.stack ? e.stack : e);
            }
        }


    if (refreshBtn) refreshBtn.addEventListener("click", () => {
        loadMe();
        loadApps();
    });

    function activityFmtTime(epoch) {
        const n = Number(epoch || 0);
        if (!Number.isFinite(n) || n <= 0) return "";
        try {
            return new Date(n * 1000).toLocaleString([], {
                month: "short",
                day: "numeric",
                hour: "2-digit",
                minute: "2-digit"
            });
        } catch {
            return "";
        }
    }

    function activityFmtBytes(v) {
        const n = Number(v || 0);
        if (!Number.isFinite(n) || n <= 0) return "";
        const units = ["B", "KiB", "MiB", "GiB", "TiB"];
        let x = n;
        let i = 0;
        while (x >= 1024 && i < units.length - 1) {
            x /= 1024;
            i++;
        }
        const digits = i === 0 ? 0 : (x >= 10 ? 1 : 2);
        return `${x.toFixed(digits)} ${units[i]}`;
    }

    function activitySocialIcon() {
        return String.fromCodePoint(0x1F465); // people busts
    }

    function activityIconFor(type) {
        if (type === "share.created") return "🔗";
        if (type === "share.disabled") return "🚫";
        if (type === "dropzone.created") return "▣";
        if (type === "dropzone.disabled") return "🚫";
        if (type === "folder.created") return "📁";
        if (type === "file.moved") return "↔";
        if (type === "file.trashed") return "🗑";
        if (type === "file.restored") return "↩";
        if (type === "file.purged") return "✕";
        if (type === "security.device_paired") return "◇";
        if (type === "security.session_revoked") return "⏻";
        if (type === "service_notice.created") return "📣";
        if (type === "service_notice.updated") return "✎";
        if (type === "service_notice.deleted") return "✕";
        if (String(type || "").startsWith("security.")) return "◇";
        if (String(type || "").startsWith("dropzone.")) return "↓";
        if (String(type || "").startsWith("circlestack.")) return activitySocialIcon();
        return "•";
    }


    function activityBasename(path) {
        const s = String(path || "").replace(/\\/g, "/");
        const parts = s.split("/").filter(Boolean);
        return parts.length ? parts[parts.length - 1] : "";
    }

    function activityActorDisplay(ev) {
        const actorKind = String(ev && ev.actor_kind || "user").toLowerCase();
        const actorDeviceName = String(ev && ev.actor_device_name || "").trim();
        const actorLabel = String(ev && ev.actor_label || "").trim();

        if (actorLabel && actorLabel !== "Someone") return actorLabel;
        if (actorKind === "device") return actorDeviceName || tr("activity.mobile_app", null, "Mobile app");
        if (actorKind === "guest") return tr("activity.guest", null, "Guest");
        if (actorKind === "system") return tr("activity.system", null, "System");
        return tr("activity.someone", null, "Someone");
    }

    function activityTargetDisplay(ev) {
        const name = String(ev && ev.target_name || "").trim();
        if (name) return name;

        const pathName = activityBasename(ev && ev.target_path);
        if (pathName) return pathName;

        const kind = String(ev && ev.target_kind || "").trim();
        return kind ? activityTargetKindLabel(kind) : tr("activity.item", null, "item");
    }

    function activityRoleLabel(role) {
        const r = String(role || "").trim().toLowerCase();
        if (r === "viewer") return tr("activity.role.viewer", null, "viewer");
        if (r === "editor") return tr("activity.role.editor", null, "editor");
        if (r === "owner") return tr("activity.role.owner", null, "owner");
        return String(role || "");
    }

    function activityTargetKindLabel(kind) {
        const k = String(kind || "").trim().toLowerCase();
        if (k === "file") return tr("activity.kind.file", null, "file");
        if (k === "folder" || k === "dir") return tr("activity.kind.folder", null, "folder");
        if (k === "dropzone") return tr("activity.kind.dropzone", null, "dropzone");
        if (k === "member") return tr("activity.kind.member", null, "member");
        if (k === "share") return tr("activity.kind.share", null, "share");
        if (k === "session") return tr("activity.kind.session", null, "session");
        if (k === "device") return tr("activity.kind.device", null, "device");
        if (k === "update_center") return tr("activity.kind.update_center", null, "Update Center");
        if (k === "service_notice") return tr("activity.kind.service_notice", null, "service notice");
        return String(kind || "");
    }

    function activityMessageFor(ev) {
        const type = String(ev && ev.event_type || "");
        const actor = activityActorDisplay(ev || {});
        const target = activityTargetDisplay(ev || {});
        const details = ev && ev.details && typeof ev.details === "object" ? ev.details : {};

        if (type === "file.uploaded") {
            return tr("activity.msg.file_uploaded", { actor, target }, "{actor} uploaded {target}");
        }
        if (type === "folder.created") {
            return tr("activity.msg.folder_created", { actor, target }, "{actor} created folder {target}");
        }
        if (type === "file.moved") {
            const fromPath = String(details.from_path || "").trim();
            const toPath = String(details.to_path || "").trim();
            if (fromPath) {
                return tr("activity.msg.file_moved_from_to", {
                    actor,
                    from: fromPath,
                    to: toPath || target
                }, "{actor} moved {from} to {to}");
            }
            return tr("activity.msg.file_moved", { actor, target }, "{actor} moved {target}");
        }
        if (type === "file.copied") {
            const fromPath = String(details.from_path || "").trim();
            const toPath = String(details.to_path || "").trim();
            if (fromPath) {
                return tr("activity.msg.file_copied_from_to", {
                    actor,
                    from: fromPath,
                    to: toPath || target
                }, "{actor} copied {from} to {to}");
            }
            return tr("activity.msg.file_copied", { actor, target }, "{actor} copied {target}");
        }
        if (type === "file.trashed") {
            return tr("activity.msg.file_trashed", { actor, target }, "{actor} moved {target} to Trash");
        }
        if (type === "file.restored") {
            return tr("activity.msg.file_restored", { actor, target }, "{actor} restored {target}");
        }
        if (type === "file.purged") {
            return tr("activity.msg.file_purged", { actor, target }, "{actor} permanently deleted {target}");
        }
        if (type === "file.locked") {
            return tr("activity.msg.file_locked", { actor, target }, "{actor} locked {target}");
        }
        if (type === "file.unlocked") {
            return tr("activity.msg.file_unlocked", { actor, target }, "{actor} unlocked {target}");
        }
        if (type === "file.lock_force_released") {
            return tr("activity.msg.file_force_unlocked", { actor, target }, "{actor} force-unlocked {target}");
        }
        if (type === "share.created") {
            return tr("activity.msg.share_created", { actor, target }, "{actor} created a share link for {target}");
        }
        if (type === "share.disabled") {
            return tr("activity.msg.share_disabled", { actor, target }, "{actor} disabled a share link for {target}");
        }
        if (type === "dropzone.created") {
            return tr("activity.msg.dropzone_created", { actor, target }, "{actor} created Drop Zone \"{target}\"");
        }
        if (type === "dropzone.disabled") {
            return tr("activity.msg.dropzone_disabled", { actor, target }, "{actor} disabled Drop Zone \"{target}\"");
        }
        if (type === "dropzone.uploaded") {
            const zone = String(details.dropzone_name || "").trim();
            if (zone) {
                return tr("activity.msg.dropzone_uploaded_named", { actor, target, zone }, "{actor} uploaded {target} through Drop Zone \"{zone}\"");
            }
            return tr("activity.msg.dropzone_uploaded", { actor, target }, "{actor} uploaded {target} through Drop Zone");
        }
        if (type === "workspace.member_role_changed") {
            const oldRole = activityRoleLabel(details.old_role);
            const newRole = activityRoleLabel(details.new_role);
            if (oldRole && newRole) {
                return tr("activity.msg.member_role_changed_from_to", {
                    actor,
                    target,
                    oldRole,
                    newRole
                }, "{actor} changed {target}'s role from {oldRole} to {newRole}");
            }
            return tr("activity.msg.member_role_changed", { actor, target }, "{actor} changed {target}'s role");
        }
        if (type === "security.login_success") {
            return tr("activity.msg.login_success", { actor }, "{actor} signed in");
        }
        if (type === "security.login_failed") {
            return tr("activity.msg.login_failed", null, "Failed sign-in attempt");
        }
        if (type === "security.device_paired") {
            return target && target !== tr("activity.item", null, "item")
                ? tr("activity.msg.device_paired_named", { target }, "{target} paired as a new device")
                : tr("activity.msg.device_paired", null, "New device paired");
        }
        if (type === "security.session_revoked") {
            return target && target !== "session"
                ? tr("activity.msg.session_revoked_for", { actor, target }, "{actor} revoked session for {target}")
                : tr("activity.msg.session_revoked", { actor }, "{actor} revoked a session");
        }

        if (type === "service_notice.created") {
            return tr(
                "activity.msg.service_notice_created",
                { actor, target },
                "{actor} published service notice: {target}"
            );
        }

        if (type === "service_notice.updated") {
            return tr(
                "activity.msg.service_notice_updated",
                { actor, target },
                "{actor} updated service notice: {target}"
            );
        }

        if (type === "service_notice.deleted") {
            return tr(
                "activity.msg.service_notice_deleted",
                { actor, target },
                "{actor} deleted service notice: {target}"
            );
        }

        if (type.startsWith("update.")) {
            const fallback = String(ev && ev.message || "").trim() || type;
            return tr(`activity.event.${type}`, null, fallback);
        }

        const serverMessage = String(ev && ev.message || "").trim();
        if (serverMessage) return serverMessage;
        return tr("activity.msg.performed", { actor, type }, "{actor} performed {type}");
    }

    function renderActivityEvents(events) {
        if (!activityList) return;

        activityList.replaceChildren();

        if (!Array.isArray(events) || events.length === 0) {
            const empty = document.createElement("div");
            empty.className = "activityEmpty";
            empty.textContent = tr("activity.empty", null, "No activity yet.");
            activityList.appendChild(empty);
            return;
        }

        for (const ev of events) {
            const actorKind = String(ev.actor_kind || "user").toLowerCase();
            const actorDeviceName = String(ev.actor_device_name || "").trim();
            const actorLabel = String(ev.actor_label || "").trim();

            const card = document.createElement("div");
            card.className = "activityItem";
            if (actorKind === "device") {
                card.classList.add("activityKindDevice");
            } else if (actorKind === "guest") {
                card.classList.add("activityKindGuest");
            } else if (actorKind === "system") {
                card.classList.add("activityKindSystem");
            }

            const msg = document.createElement("div");
            msg.className = "activityMsg";

            const evType = String(ev.event_type || "").trim();
            const evMessage = activityMessageFor(ev);
            const icon = evType.startsWith("circlestack.")
                ? activitySocialIcon()
                : activityIconFor(evType);

            msg.textContent = `${icon} ${evMessage}`;
            card.appendChild(msg);

            const metaBits = [];
            const when = activityFmtTime(ev.created_at_epoch);
            if (when) metaBits.push(when);

            if (actorKind === "device") {
                metaBits.push(actorDeviceName
                    ? tr("activity.mobile_device", { device: actorDeviceName }, `Mobile · ${actorDeviceName}`)
                    : tr("activity.mobile_app", null, "Mobile app"));
            } else if (actorKind === "guest") {
                metaBits.push(actorLabel && actorLabel !== tr("activity.someone", null, "Someone")
                    ? tr("activity.guest_named", { name: actorLabel }, `Guest · ${actorLabel}`)
                    : tr("activity.guest_dropzone", null, "Guest / Drop Zone"));
            } else if (actorKind === "system") {
                metaBits.push(tr("activity.system", null, "System"));
            }

            const targetPath = String(ev.target_path || "");
            if (targetPath) metaBits.push(targetPath);

            const kind = activityTargetKindLabel(ev.target_kind);
            const size = ev.details && typeof ev.details === "object" ? activityFmtBytes(ev.details.size_bytes) : "";
            if (kind || size) metaBits.push([kind, size].filter(Boolean).join(" · "));

            if (metaBits.length) {
                const meta = document.createElement("div");
                meta.className = "activityMeta";
                meta.textContent = metaBits.join(" · ");
                card.appendChild(meta);
            }

            activityList.appendChild(card);
        }
    }

    function updateActivityPager(eventsOnPage = 0) {
        if (!activityPager || !activityPrevBtn || !activityNextBtn || !activityPageInfo) return;

        const total = Math.max(0, Math.trunc(Number(activityTotal || 0)));
        const offset = Math.max(0, Math.trunc(Number(activityOffset || 0)));
        const count = Math.max(0, Math.trunc(Number(eventsOnPage || 0)));

        const show = total > ACTIVITY_PAGE_SIZE || offset > 0 || activityHasMore;
        activityPager.style.display = show ? "" : "none";

        activityPrevBtn.disabled = offset <= 0;
        activityNextBtn.disabled = !activityHasMore;

        if (!show) {
            activityPageInfo.textContent = "";
            return;
        }

        const start = count > 0 ? offset + 1 : 0;
        const end = count > 0 ? offset + count : 0;

        activityPageInfo.textContent = count > 0
            ? tr("activity.page_info", { start, end, total }, `${start}-${end} of ${total}`)
            : tr("activity.page_empty", { total }, `0 of ${total}`);
    }

    async function loadActivity(opts = {}) {
        if (!activityList) return;

        if (opts && opts.reset) {
            activityOffset = 0;
        } else if (opts && Object.prototype.hasOwnProperty.call(opts, "offset")) {
            const requestedOffset = Number(opts.offset);
            activityOffset = Number.isFinite(requestedOffset)
                ? Math.max(0, Math.trunc(requestedOffset))
                : 0;
        }

        const limit = ACTIVITY_PAGE_SIZE;
        const offset = Math.max(0, Math.trunc(Number(activityOffset || 0)));

        try {
            if (window.PQNAS_I18N && typeof window.PQNAS_I18N.ready === "function") {
                await window.PQNAS_I18N.ready();
            }
        } catch (_) {}

        if (activityStatus) activityStatus.textContent = tr("common.loading", null, "Loading…");

        try {
            const r = await fetch(`/api/v4/activity/list?limit=${encodeURIComponent(String(limit))}&offset=${encodeURIComponent(String(offset))}`, {
                credentials: "include",
                headers: { "Accept": "application/json" },
                cache: "no-store"
            });
            const j = await r.json().catch(() => null);

            if (!r.ok || !j || !j.ok) {
                throw new Error((j && j.message) ? j.message : `HTTP ${r.status}`);
            }

            const events = Array.isArray(j.events) ? j.events : [];

            const responseTotal = Number(j.total);
            activityTotal = Number.isFinite(responseTotal)
                ? Math.max(0, Math.trunc(responseTotal))
                : events.length;

            const responseOffset = Number(j.offset);
            activityOffset = Number.isFinite(responseOffset)
                ? Math.max(0, Math.trunc(responseOffset))
                : offset;

            activityHasMore = !!j.has_more;

            // If the current page became empty because activity was deleted/rotated,
            // jump to the last valid page instead of showing a dead page.
            if (events.length === 0 && activityOffset > 0 && activityTotal > 0) {
                const lastOffset = Math.max(0, Math.floor((activityTotal - 1) / limit) * limit);
                if (lastOffset !== activityOffset) {
                    return loadActivity({ offset: lastOffset });
                }
            }

            renderActivityEvents(events);
            updateActivityPager(events.length);

            if (activityStatus) {
                if (events.length) {
                    const start = activityOffset + 1;
                    const end = activityOffset + events.length;
                    activityStatus.textContent = tr(
                        "activity.showing_range",
                        { start, end, total: activityTotal },
                        `Showing ${start}-${end} of ${activityTotal} events`
                    );
                } else {
                    activityStatus.textContent = tr("activity.no_recent", null, "No recent activity");
                }
            }
        } catch (e) {
            activityHasMore = false;
            updateActivityPager(0);

            activityList.replaceChildren();

            const err = document.createElement("div");
            err.className = "activityEmpty";
            err.textContent = tr("activity.load_failed", { error: String(e && e.message ? e.message : e) }, `Could not load activity: ${String(e && e.message ? e.message : e)}`);
            activityList.appendChild(err);

            if (activityStatus) activityStatus.textContent = tr("activity.unavailable", null, "Activity unavailable");
        }
    }

    function setActivityHidden(hidden) {
        if (!activityPane || !toggleActivityBtn) return;

        activityPane.style.display = hidden ? "none" : "";
        if (contentGrid) contentGrid.classList.toggle("noSidePane", hidden);

        toggleActivityBtn.textContent = hidden
            ? tr("activity.my_activity", null, "My Activity")
            : tr("activity.close_activity", null, "Close Activity");

        if (!hidden) loadActivity({ reset: true });

        try { localStorage.setItem("pqnas_hide_activity", hidden ? "1" : "0"); } catch {}
    }

    // default: hidden, but remember user choice
    (() => {
        let hide = true;
        try {
            const v = localStorage.getItem("pqnas_hide_activity");
            if (v === "0") hide = false;
        } catch {}
        setActivityHidden(hide);
        window.addEventListener("resize", () => {
        if (currentView === "user_settings") {
            fitHomeContentToViewport();
        }
    });

})();

    if (toggleActivityBtn && activityPane) {
        toggleActivityBtn.addEventListener("click", () => {
            const hidden = activityPane.style.display === "none";
            setActivityHidden(!hidden);
        });
    }

    if (activityRefreshBtn) {
        activityRefreshBtn.addEventListener("click", () => loadActivity({ offset: activityOffset }));
    }

    if (activityPrevBtn) {
        activityPrevBtn.addEventListener("click", () => {
            loadActivity({ offset: Math.max(0, activityOffset - ACTIVITY_PAGE_SIZE) });
        });
    }

    if (activityNextBtn) {
        activityNextBtn.addEventListener("click", () => {
            if (!activityHasMore) return;
            loadActivity({ offset: activityOffset + ACTIVITY_PAGE_SIZE });
        });
    }

    if (navHome) navHome.addEventListener("click", () => renderHome());
    if (navTrustedDevices) navTrustedDevices.addEventListener("click", () => {
        openTrustedDevices();
    });
    if (navWorkspaceInvites) navWorkspaceInvites.addEventListener("click", () => {
        renderWorkspaceInvites();
    });
    if (navPeople) navPeople.addEventListener("click", () => {
        renderPeople();
    });
    if (navUserSettings) navUserSettings.addEventListener("click", () => {
        renderUserSettings();
    });
// Default view
    renderHome();

    // Load once immediately
    loadMe();

    // Refresh auth state when tab comes back / user focuses
    document.addEventListener("visibilitychange", () => {
        if (!document.hidden) loadMe();
    });
    window.addEventListener("focus", () => loadMe());

    // Slow refresh (only to keep UI honest; not a heartbeat)
    setInterval(() => { if (authed) loadMe(); }, 30000);

    // Apps list can be even slower
    setInterval(() => { if (authed) loadApps(); }, 60000);


})();
