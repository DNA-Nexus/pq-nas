/* server/src/static/admin_settings.js
 *
 * PQ-NAS Admin Settings UI
 * - Loads/saves: audit_min_level, audit_retention, audit_rotation, ui_theme
 * - Uses /api/v4/admin/settings (+ audit retention/rotation endpoints)
 *
 * IMPORTANT:
 * - This file intentionally has ONE theme system:
 *   applyTheme() prefers window.pqnasSetTheme() from /static/theme.js.
 * - No duplicate const declarations (previous breakage).
 */

(function () {
    const $ = (id) => document.getElementById(id);

    // --- audit level controls ---
    const statusPill = $("statusPill");
    const persistedVal = $("persistedVal");
    const runtimeVal = $("runtimeVal");
    const levelSelect = $("levelSelect");
    const btnSave = $("btnSave");
    const btnReload = $("btnReload");

    // --- retention controls ---
    const retentionPill = $("retentionPill");
    const retMode = $("retMode");
    const retDays = $("retDays");
    const retMaxFiles = $("retMaxFiles");
    const retMaxMB = $("retMaxMB");

    const btnRetentionSave = $("btnRetentionSave");
    const btnRetentionPreview = $("btnRetentionPreview");
    const btnRetentionPrune = $("btnRetentionPrune");

    const retPreviewPill = $("retPreviewPill");
    const retSummaryPill = $("retSummaryPill");
    const retTbody = $("retTbody");

    // --- toast ---
    const toast = $("toast");
    const toastTitle = $("toastTitle");
    const toastMsg = $("toastMsg");

    // --- rotation (manual) ---
    const activeSizePill = $("activeSizePill");
    const btnRotateNow = $("btnRotateNow");

    // --- rotation policy (automatic) ---
    const rotatePolicyPill = $("rotatePolicyPill");
    const rotMode = $("rotMode");
    const rotMaxMB = $("rotMaxMB");
    const btnRotatePolicySave = $("btnRotatePolicySave");

    // --- language ---
    const languagePill = $("languagePill");
    const languageSelect = $("languageSelect");

    // --- theme ---
    const themePill = $("themePill");
    const themeSelect = $("themeSelect");
    const btnThemeSave = $("btnThemeSave");
    const btnThemeApply = $("btnThemeApply");

    // --- snapshots ---
    const snapPill = $("snapPill");
    const snapEnabled = $("snapEnabled");
    const snapAuto = $("snapAuto");
    const snapTimesPerDay = $("snapTimesPerDay");
    const snapJitter = $("snapJitter");
    const snapRoot = $("snapRoot");
    const btnSnapSave = $("btnSnapSave");
    const btnSnapReload = $("btnSnapReload");
    const snapPerVolume = $("snapPerVolume");
    const snapVolTbody = $("snapVolTbody");

    // --- uploads ---
    const uploadPill = $("uploadPill");
    const uploadSoftMax = $("uploadSoftMax");
    const btnUploadSave = $("btnUploadSave");
    const btnUploadReload = $("btnUploadReload");
    const uploadSoftPill = $("uploadSoftPill");
    const uploadHardPill = $("uploadHardPill");
    const uploadEffectivePill = $("uploadEffectivePill");

    // --- upload tiering ---
    const tieringPill = $("tieringPill");
    const tieringEnabled = $("tieringEnabled");
    const tieringLandingPool = $("tieringLandingPool");
    const tieringIntervalSec = $("tieringIntervalSec");
    const tieringMinAgeSec = $("tieringMinAgeSec");
    const tieringMaxPass = $("tieringMaxPass");
    const btnTieringSave = $("btnTieringSave");
    const btnTieringReload = $("btnTieringReload");
    const tieringMountPill = $("tieringMountPill");
    const tieringSpacePill = $("tieringSpacePill");
    const tieringEligibilityPill = $("tieringEligibilityPill");
    const tieringWarnPill = $("tieringWarnPill");

    const btnUploadsHelp = $("btnUploadsHelp");
    const helpModalBackdrop = $("helpModalBackdrop");
    const btnHelpModalClose = $("btnHelpModalClose");

    // --- DNA Connect alerts ---
    const dnaAlertsPill = $("dnaAlertsPill");
    const dnaAlertsEnabled = $("dnaAlertsEnabled");
    const dnaAlertsRecipient = $("dnaAlertsRecipient");
    const dnaAlertsMinLevel = $("dnaAlertsMinLevel");
    const dnaAlertsCliPath = $("dnaAlertsCliPath");
    const dnaAlertsDataDir = $("dnaAlertsDataDir");
    const btnDnaAlertsSave = $("btnDnaAlertsSave");
    const btnDnaAlertsReload = $("btnDnaAlertsReload");
    const btnDnaAlertsTest = $("btnDnaAlertsTest");
    const dnaAlertsInfoPill = $("dnaAlertsInfoPill");

    const btnDnaAlertsCreateId = $("btnDnaAlertsCreateId");
    const btnDnaAlertsShowId = $("btnDnaAlertsShowId");
    const btnDnaAlertsSendRequest = $("btnDnaAlertsSendRequest");
    const dnaAlertsIdentityPill = $("dnaAlertsIdentityPill");

    const dnaIdentityModalBackdrop = $("dnaIdentityModalBackdrop");
    const btnDnaIdentityModalClose = $("btnDnaIdentityModalClose");
    const dnaIdentityModalBody = $("dnaIdentityModalBody");

    let gDnaConnectIdentity = null;

    const ALLOWED_THEMES = new Set(["dark", "bright", "cpunk_orange", "win_classic"]);
    const ALLOWED_ROT_MODES = new Set(["manual", "daily", "size_mb", "daily_or_size_mb"]);
    let gStorageRoots = null; // populated from GET /api/v4/admin/settings
    let gTieringCandidates = []; // populated from GET /api/v4/admin/settings
    let gSnapshotsLast = null;

    function serverDataRootOrFallback() {
        const dr = gStorageRoots && typeof gStorageRoots.data_root === "string" ? gStorageRoots.data_root.trim() : "";
        return dr || "/srv/pqnas/data";
    }
    function escapeHtml(s) {
        return String(s ?? "")
            .replaceAll("&", "&amp;")
            .replaceAll("<", "&lt;")
            .replaceAll(">", "&gt;")
            .replaceAll('"', "&quot;")
            .replaceAll("'", "&#39;");
    }
    function adminLabel(label) {
        const s = String(label || "");
        const map = {
            status: "admin.common.status",
            "Persisted": "admin.common.persisted",
            "Runtime": "admin.common.runtime",
            "Policy": "admin.common.policy",
            "Active log": "admin.common.active_log",
            "Preview": "admin.common.preview",
            "Summary": "admin.common.summary",
            "Theme": "admin.theme.pill",
            "Tiering": "admin.uploads.tiering_label",
            "Mount": "admin.uploads.mount",
            "Space": "admin.uploads.space",
            "Eligibility": "admin.uploads.eligibility",
            "Warnings": "admin.uploads.warnings",
            "Route": "admin.dna.route",
            "PQ-NAS ID": "admin.dna.pqnas_id",
            "Soft cap": "admin.uploads.soft_cap",
            "Hard cap": "admin.uploads.hard_cap",
            "Effective": "admin.uploads.effective"
        };
        const key = map[s];
        return key ? tr(key, null, s) : s;
    }

    function adminStatusText(text) {
        const s = String(text || "");
        if (s === "loading…" || s === "loading...") return tr("admin.common.loading", null, "loading…");
        if (s === "ready") return tr("admin.common.ready", null, "ready");
        if (s === "error") return tr("admin.common.error", null, "error");
        if (s === "saving…" || s === "saving...") return tr("admin.common.saving", null, "saving…");
        return s;
    }

    function setSnapshotsPill(kind, text) {
        if (!snapPill) return;
        snapPill.className = "pill " + (kind || "");
        snapPill.innerHTML = `<span class="k">${escapeHtml(adminLabel("status"))}:</span> <span class="v">${escapeHtml(adminStatusText(text))}</span>`;
    }
    function tpdOptionsHtml(selected) {
        const vals = [1,2,4,6,12,24];
        return vals.map(v => `<option value="${v}" ${String(v)===String(selected)?"selected":""}>${v}</option>`).join("");
    }

    function renderSnapshotVolumesTable(sn) {
        if (!snapVolTbody) return;
        snapVolTbody.innerHTML = "";

        const vols = Array.isArray(sn?.volumes) ? sn.volumes : [];
        const globalSched = sn?.schedule && typeof sn.schedule === "object" ? sn.schedule : {};
        const globalTpd = Number(globalSched.times_per_day ?? 6);
        const globalJit = Number(globalSched.jitter_seconds ?? 120);

        const perVol = !!(snapPerVolume && snapPerVolume.checked);

        for (let i = 0; i < vols.length; i++) {
            const v = vols[i] && typeof vols[i] === "object" ? vols[i] : {};
            const name = String(v.name || `vol${i}`);
            const src  = String(v.source_subvolume || "");

            const vs = (v.schedule && typeof v.schedule === "object") ? v.schedule : {};
            const tpd = perVol ? Number(vs.times_per_day ?? globalTpd) : globalTpd;
            const jit = perVol ? Number(vs.jitter_seconds ?? globalJit) : globalJit;

            const tr = document.createElement("tr");
            tr.innerHTML = `
          <td class="mono" title="${escapeHtml(name)}">${escapeHtml(name)}</td>
          <td class="mono" title="${escapeHtml(src)}">${escapeHtml(src)}</td>
          <td>
            <select class="snapVolTpd" data-i="${i}" ${perVol ? "" : "disabled"}>
              ${tpdOptionsHtml(Math.min(24, Math.max(1, tpd || 6)))}
            </select>
          </td>
          <td>
            <input class="input mono snapVolJit"
                   style="min-width:120px"
                   type="number" min="0" max="3600"
                   data-i="${i}"
                   value="${String(Math.min(3600, Math.max(0, jit || 120)))}"
                   ${perVol ? "" : "disabled"} />
          </td>
        `;
            snapVolTbody.appendChild(tr);
        }
    }

    function showToast(kind, title, msg) {
        if (!toast) return;
        toast.className = "toast show " + (kind || "");
        if (toastTitle) toastTitle.textContent = title || "";
        if (toastMsg) toastMsg.textContent = msg || "";
        window.clearTimeout(showToast._t);
        showToast._t = window.setTimeout(() => {
            toast.className = "toast";
        }, 2600);
    }

    function setStatusPill(kind, text) {
        if (!statusPill) return;
        statusPill.className = "pill " + (kind || "");
        statusPill.innerHTML = `<span class="k">${escapeHtml(adminLabel("status"))}:</span> <span class="v">${escapeHtml(adminStatusText(text))}</span>`;
    }

    function setSimplePill(el, kind, k, v) {
        if (!el) return;
        el.className = "pill " + (kind || "");
        el.innerHTML = `<span class="k">${escapeHtml(adminLabel(k))}:</span> <span class="v">${escapeHtml(v)}</span>`;
    }

    function tr(key, vars = null, fallback = "") {
        try {
            if (window.PQNAS_I18N && typeof window.PQNAS_I18N.t === "function") {
                return window.PQNAS_I18N.t(key, vars, fallback || key);
            }
        } catch (_) {}
        return fallback || key;
    }

    function fmtBytes(n) {
        if (n === null || n === undefined) return "—";
        const x = Number(n);
        if (!Number.isFinite(x)) return "—";
        if (x < 0) return "unknown";
        if (x < 1024) return `${Math.trunc(x)} B`;
        if (x < 1024 * 1024) return `${(x / 1024).toFixed(1)} KB`;
        if (x < 1024 * 1024 * 1024) return `${(x / 1024 / 1024).toFixed(1)} MB`;
        return `${(x / 1024 / 1024 / 1024).toFixed(2)} GB`;
    }

    function setOptions(allowed, selected) {
        if (!levelSelect) return;
        levelSelect.innerHTML = "";
        for (const lvl of allowed) {
            const opt = document.createElement("option");
            opt.value = lvl;
            opt.textContent = lvl;
            if (lvl === selected) opt.selected = true;
            levelSelect.appendChild(opt);
        }
    }

    // ---------------------------
    // Language (browser-local i18n)
    // ---------------------------
    function normalizeLanguage(lang) {
        try {
            if (window.PQNAS_I18N && typeof window.PQNAS_I18N.normalizeLanguage === "function") {
                return window.PQNAS_I18N.normalizeLanguage(lang);
            }
        } catch (_) {}

        const raw = String(lang || "").trim().toLowerCase().replace("_", "-");
        const aliases = {
            "zh-cn": "zh",
            "zh-hans": "zh",
            "uk-ua": "uk",
            "de-de": "de",
            "et-ee": "et",
            "pl-pl": "pl",
            "es-es": "es",
            "fr-fr": "fr",
            "fr-be": "fr",
            "fr-ca": "fr",
            "fr-ch": "fr",
            "it-it": "it",
            "tr-tr": "tr",
            "it-ch": "it"
        };
        const allowed = new Set(["en", "fi", "zh", "sv", "uk", "de", "et", "pl", "es", "fr", "it"]);
        const aliased = aliases[raw] || raw;

        if (allowed.has(aliased)) return aliased;

        const base = aliased.split("-")[0];
        if (allowed.has(base)) return base;

        return "en";
    }

    function currentLanguageName() {
        try {
            if (window.PQNAS_I18N && typeof window.PQNAS_I18N.getLanguage === "function") {
                return normalizeLanguage(window.PQNAS_I18N.getLanguage());
            }
        } catch (_) {}

        try {
            return normalizeLanguage(localStorage.getItem("pqnas_lang") || "en");
        } catch (_) {}

        return "en";
    }

    function languageDisplayName(lang) {
        const l = normalizeLanguage(lang);
        if (l === "fi") return tr("admin.language.finnish", null, "🇫🇮 Suomi");
        if (l === "zh") return tr("admin.language.chinese_simplified", null, "🇨🇳 简体中文");
        if (l === "sv") return tr("admin.language.swedish", null, "🇸🇪 Svenska");
        if (l === "uk") return tr("admin.language.ukrainian", null, "🇺🇦 Українська");
        if (l === "de") return tr("admin.language.german", null, "🇩🇪 Deutsch");
        if (l === "et") return tr("admin.language.estonian", null, "🇪🇪 Eesti");
        if (l === "pl") return tr("admin.language.polish", null, "🇵🇱 Polski");
        return tr("admin.language.english", null, "🇬🇧 English");
    }

    function updateLanguagePill(lang) {
        const l = normalizeLanguage(lang);
        if (languageSelect) languageSelect.value = l;

        const label = languageDisplayName(l);

        if (languagePill) {
            setSimplePill(languagePill, "info", tr("admin.language.pill", null, "Language"), label);
        }
    }

    async function applyAdminLanguage(lang) {
        const l = normalizeLanguage(lang);

        try {
            if (window.PQNAS_I18N && typeof window.PQNAS_I18N.setLanguage === "function") {
                await window.PQNAS_I18N.setLanguage(l);
            } else {
                localStorage.setItem("pqnas_lang", l);
                document.documentElement.setAttribute("lang", l);
            }
        } catch (_) {}

        updateLanguagePill(l);

        try {
            if (window.PQNAS_I18N && typeof window.PQNAS_I18N.apply === "function") {
                window.PQNAS_I18N.apply(document);
            }
        } catch (_) {}

        showToast(
            "ok",
            tr("admin.language.applied", null, "Language applied"),
            languageDisplayName(l)
        );
    }

    // ---------------------------
    // Theme (single clean implementation)
    // ---------------------------
    function normalizeTheme(t) {
        t = String(t || "").trim();
        return ALLOWED_THEMES.has(t) ? t : "dark";
    }

    function applyTheme(theme) {
        const t = normalizeTheme(theme);

        // Preferred: centralized theme.js (shared across pages)
        try {
            if (typeof window.pqnasSetTheme === "function") {
                window.pqnasSetTheme(t);
            } else {
                document.documentElement.dataset.theme = t;
                try {
                    localStorage.setItem("pqnas_theme", t);
                } catch (_) {}
            }
        } catch (_) {}

        if (themeSelect) themeSelect.value = t;
        if (themePill) setSimplePill(themePill, "info", tr("admin.theme.pill", null, "Theme"), t);
        return t;
    }
    async function apiCreateDnaAlertIdentity() {
        return await fetchJsonOrThrow("/api/v4/admin/settings/create-dna-alert-identity", {
            method: "POST",
            cache: "no-store",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({})
        });
    }

    async function apiGetDnaAlertIdentityInfo() {
        return await fetchJsonOrThrow("/api/v4/admin/settings/dna-alert-identity-info", {
            cache: "no-store"
        });
    }

    async function apiSendDnaAlertContactRequest() {
        return await fetchJsonOrThrow("/api/v4/admin/settings/send-dna-alert-contact-request", {
            method: "POST",
            cache: "no-store",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({})
        });
    }
    function openDnaIdentityModal(text) {
        if (dnaIdentityModalBody) dnaIdentityModalBody.textContent = text || "—";
        dnaIdentityModalBackdrop?.classList.remove("hidden");
    }

    function closeDnaIdentityModal() {
        dnaIdentityModalBackdrop?.classList.add("hidden");
    }
    // ---------------------------
    // HTTP helper (robust JSON parsing)
    // ---------------------------
    async function fetchJsonOrThrow(url, opts) {
        const r = await fetch(url, opts);

        // Always read as text first, then JSON.parse (works even if server lies about content-type)
        const text = await r.text().catch(() => "");
        let j = null;
        try {
            j = text ? JSON.parse(text) : null;
        } catch (_) {}

        if (!r.ok) {
            const msg =
                j && (j.message || j.error)
                    ? [
                        j.message || j.error,
                        j.detail ? `detail: ${j.detail}` : "",
                        j.body_snip ? `body: ${j.body_snip}` : "",
                    ]
                        .filter(Boolean)
                        .join(" • ")
                    : text && text.trim()
                        ? text.trim().slice(0, 200)
                        : `${url} failed (HTTP ${r.status})`;
            throw new Error(msg);
        }

        // Expect JSON with ok:true from these endpoints
        if (!j || j.ok !== true) {
            const msg =
                j && (j.message || j.error)
                    ? [
                        j.message || j.error,
                        j.detail ? `detail: ${j.detail}` : "",
                        j.body_snip ? `body: ${j.body_snip}` : "",
                    ]
                        .filter(Boolean)
                        .join(" • ")
                    : text && text.trim()
                        ? text.trim().slice(0, 200)
                        : `${url}: invalid JSON response`;
            throw new Error(msg);
        }

        return j;
    }

    // ---------------------------
    // Settings API
    // ---------------------------
    async function apiSettingsGet() {
        return await fetchJsonOrThrow("/api/v4/admin/settings", { cache: "no-store" });
    }

    async function apiSettingsPost(payload) {
        // Never allow an empty/undefined body: JSON.stringify(undefined) -> undefined -> fetch sends no body.
        let body = "{}";
        try {
            const s = JSON.stringify(payload ?? {});
            body = typeof s === "string" && s.length ? s : "{}";
        } catch (_) {
            body = "{}";
        }

        return await fetchJsonOrThrow("/api/v4/admin/settings", {
            method: "POST",
            cache: "no-store",
            headers: { "Content-Type": "application/json" },
            body,
        });
    }

    // ---------------------------
    // Retention API
    // ---------------------------
    async function apiPreviewPrune(policy) {
        // server expects { audit_retention: { ... } }
        return await fetchJsonOrThrow("/api/v4/admin/audit/preview-prune", {
            method: "POST",
            cache: "no-store",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ audit_retention: policy }),
        });
    }


    function injectAdminConfirmCss() {
        if (document.getElementById("adminConfirmCss")) return;

        const style = document.createElement("style");
        style.id = "adminConfirmCss";
        style.textContent = `
.adminConfirmBackdrop{
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

.adminConfirmCard{
    width:min(640px, calc(100vw - 24px));
    max-height:min(84vh, 900px);
    display:flex;
    flex-direction:column;
    overflow:hidden;
    border:1px solid var(--border2, rgba(120,120,120,0.45));
    border-radius:18px;
    background:linear-gradient(180deg, var(--panel2, #f8f8f8), var(--panel, #eeeeee));
    box-shadow:0 18px 70px rgba(0,0,0,0.42);
    color:var(--fg, #111);
}

.adminConfirmHead{
    padding:14px 16px;
    border-bottom:1px solid var(--border2, rgba(120,120,120,0.35));
    background:rgba(0,0,0,0.08);
}

.adminConfirmTitle{
    font-weight:950;
    letter-spacing:.2px;
    font-size:16px;
}

.adminConfirmSub{
    margin-top:4px;
    font-size:12px;
    color:var(--fg-dim, rgba(0,0,0,0.65));
}

.adminConfirmBody{
    padding:16px;
    display:grid;
    grid-template-columns:140px minmax(0, 1fr);
    gap:10px 14px;
    overflow:auto;
    min-height:0;
}

.adminConfirmKey{
    color:var(--fg-dim, rgba(0,0,0,0.68));
    font-weight:850;
}

.adminConfirmValue{
    color:var(--fg, #111);
    overflow-wrap:anywhere;
    white-space:pre-wrap;
}

.adminConfirmValue.mono{
    font-family:var(--mono, ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace);
    font-size:12px;
}

.adminConfirmNote{
    grid-column:1 / -1;
    padding:10px 12px;
    border:1px solid rgba(var(--warn-rgb, 180,120,20),0.35);
    border-radius:14px;
    background:rgba(var(--warn-rgb, 180,120,20),0.10);
    color:var(--fg, #111);
    font-weight:850;
}

.adminConfirmFoot{
    display:flex;
    align-items:center;
    gap:12px;
    padding:12px 16px;
    border-top:1px solid var(--border2, rgba(120,120,120,0.35));
    background:rgba(0,0,0,0.08);
}

.adminConfirmBtn{
    border:1px solid var(--border2, rgba(120,120,120,0.45));
    border-radius:14px;
    padding:9px 14px;
    font:inherit;
    font-weight:850;
    color:var(--fg, #111);
    background:linear-gradient(180deg, rgba(255,255,255,0.20), rgba(0,0,0,0.04));
    cursor:pointer;
}

.adminConfirmBtn:hover{
    filter:brightness(1.05);
}

.adminConfirmBtn.secondary{
    opacity:.90;
}

.adminConfirmBtn.warn{
    border-color:rgba(var(--warn-rgb, 180,120,20),0.48);
    background:rgba(var(--warn-rgb, 180,120,20),0.16);
    color:var(--fg, #111);
}

html[data-theme="bright"] .adminConfirmBackdrop{
    background:rgba(0,0,0,0.30);
}

html[data-theme="bright"] .adminConfirmCard{
    background:linear-gradient(180deg, #ffffff, #f2f4f7) !important;
    border-color:rgba(70,80,95,0.32) !important;
    color:#111827 !important;
    box-shadow:0 22px 80px rgba(0,0,0,0.28) !important;
}

html[data-theme="bright"] .adminConfirmHead,
html[data-theme="bright"] .adminConfirmFoot{
    background:rgba(15,23,42,0.045) !important;
    border-color:rgba(70,80,95,0.22) !important;
}

html[data-theme="bright"] .adminConfirmTitle,
html[data-theme="bright"] .adminConfirmValue,
html[data-theme="bright"] .adminConfirmBtn{
    color:#111827 !important;
}

html[data-theme="bright"] .adminConfirmSub,
html[data-theme="bright"] .adminConfirmKey{
    color:rgba(17,24,39,0.68) !important;
}

html[data-theme="bright"] .adminConfirmNote{
    background:rgba(190,125,20,0.12) !important;
    border-color:rgba(190,125,20,0.34) !important;
    color:#111827 !important;
}

html[data-theme="bright"] .adminConfirmBtn.secondary{
    background:linear-gradient(180deg, #ffffff, #e8ebef) !important;
}

html[data-theme="bright"] .adminConfirmBtn.warn{
    background:rgba(190,125,20,0.16) !important;
    border-color:rgba(190,125,20,0.38) !important;
    color:#111827 !important;
}

html[data-theme="win_classic"] .adminConfirmBackdrop{
    background:rgba(0,0,0,0.38);
}
`;
        document.head.appendChild(style);
    }

    function openAdminConfirmModal(opts = {}) {
        injectAdminConfirmCss();

        return new Promise((resolve) => {
            const options = opts || {};

            const modal = document.createElement("div");
            modal.className = "adminConfirmBackdrop";
            modal.setAttribute("role", "dialog");
            modal.setAttribute("aria-modal", "true");

            const card = document.createElement("div");
            card.className = "adminConfirmCard";

            const head = document.createElement("div");
            head.className = "adminConfirmHead";

            const title = document.createElement("div");
            title.className = "adminConfirmTitle";
            title.textContent = options.title || tr("admin.confirm.title", null, "Confirm action");

            const sub = document.createElement("div");
            sub.className = "adminConfirmSub";
            sub.textContent = options.subtitle || "";

            head.appendChild(title);
            if (sub.textContent) head.appendChild(sub);

            const body = document.createElement("div");
            body.className = "adminConfirmBody";

            const rows = Array.isArray(options.rows) ? options.rows : [];
            for (const row of rows) {
                const k = document.createElement("div");
                k.className = "adminConfirmKey";
                k.textContent = String(row.label || "");

                const v = document.createElement("div");
                v.className = row.mono ? "adminConfirmValue mono" : "adminConfirmValue";
                v.textContent = String(row.value || "");

                body.appendChild(k);
                body.appendChild(v);
            }

            if (options.note) {
                const note = document.createElement("div");
                note.className = "adminConfirmNote";
                note.textContent = String(options.note || "");
                body.appendChild(note);
            }

            const foot = document.createElement("div");
            foot.className = "adminConfirmFoot";

            const spacer = document.createElement("div");
            spacer.style.flex = "1 1 auto";

            const cancelBtn = document.createElement("button");
            cancelBtn.type = "button";
            cancelBtn.className = "adminConfirmBtn secondary";
            cancelBtn.textContent = options.cancelText || tr("admin.common.cancel", null, "Cancel");

            const okBtn = document.createElement("button");
            okBtn.type = "button";
            okBtn.className = options.warn ? "adminConfirmBtn warn" : "adminConfirmBtn";
            okBtn.textContent = options.confirmText || tr("admin.common.ok", null, "OK");

            foot.appendChild(spacer);
            foot.appendChild(cancelBtn);
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
                    finish(false);
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
                if (ev.target === modal) finish(false);
            });

            cancelBtn.addEventListener("click", () => finish(false));
            okBtn.addEventListener("click", () => finish(true));

            window.setTimeout(() => {
                cancelBtn.focus();
            }, 0);
        });
    }

    async function apiRotateAudit() {
        return await fetchJsonOrThrow("/api/v4/admin/rotate-audit", {
            method: "POST",
            cache: "no-store",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({}),
        });
    }

    async function apiRunPrune() {
        return await fetchJsonOrThrow("/api/v4/admin/audit/prune", {
            method: "POST",
            cache: "no-store",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({}),
        });
    }
    async function apiTestDnaAlert() {
        return await fetchJsonOrThrow("/api/v4/admin/settings/test-dna-alert", {
            method: "POST",
            cache: "no-store",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({})
        });
    }
    function defaultSnapshots() {
        return {
            enabled: false,
            backend: "btrfs",
            per_volume_policy: false,
            volumes: [{
                name: "data",
                source_subvolume: serverDataRootOrFallback(),
                snap_root: "/srv/pqnas/.snapshots/data"
            }],
            schedule: { mode: "times_per_day", times_per_day: 6, jitter_seconds: 120 },
            retention: { keep_days: 7, keep_min: 12, keep_max: 500 }
        };
    }

    function applySnapshotsToUi(sn) {
        const s = sn && typeof sn === "object" ? sn : defaultSnapshots();
        gSnapshotsLast = s;
        const enabled = !!s.enabled;

        if (snapEnabled) snapEnabled.checked = enabled;
        const vols = Array.isArray(s.volumes) ? s.volumes : [];
        const inferredPerVol = (typeof s.per_volume_policy === "boolean")
            ? s.per_volume_policy
            : vols.some(v => v && typeof v === "object" && v.schedule && typeof v.schedule === "object");

        if (snapPerVolume) snapPerVolume.checked = !!inferredPerVol;


        const sched = s.schedule || {};
        const schedMode = String(sched.mode || "times_per_day").trim();
        const auto = schedMode !== "manual";
        const tpd = Number(sched.times_per_day ?? 6);
        const jit = Number(sched.jitter_seconds ?? 120);

        if (snapAuto) snapAuto.checked = enabled && auto;
        if (snapTimesPerDay) snapTimesPerDay.value = String(Math.min(24, Math.max(1, tpd)));
        if (snapJitter) snapJitter.value = String(Math.min(3600, Math.max(0, jit)));

        // v1: one volume "data"
        let root = "/srv/pqnas/.snapshots/data";
        let src  = serverDataRootOrFallback();

        try {
            if (vols[0] && typeof vols[0] === "object") {
                if (typeof vols[0].snap_root === "string") root = vols[0].snap_root;
                if (typeof vols[0].source_subvolume === "string") src = vols[0].source_subvolume;
            }
        } catch (_) {}


        if (snapRoot) snapRoot.value = root;

        // Store src on the checkbox as data- so currentSnapshotsFromUi() can reuse it
        if (snapEnabled) snapEnabled.dataset.src = src;

        renderSnapshotVolumesTable(s);
        syncSnapshotsEnabledUi();
    }

    function currentSnapshotsFromUi() {
        const enabled = !!snapEnabled?.checked;
        const auto = !!snapAuto?.checked;
        const schedMode = auto ? "times_per_day" : "manual";

        const global_tpd = Math.min(24, Math.max(1, parseInt(snapTimesPerDay?.value || "6", 10) || 6));
        const global_jitter = Math.min(3600, Math.max(0, parseInt(snapJitter?.value || "120", 10) || 120));

        const root0 = String(snapRoot?.value || "/srv/pqnas/.snapshots/data").trim();
        const src0 = String(snapEnabled?.dataset?.src || serverDataRootOrFallback());

        const perVol = !!snapPerVolume?.checked;

        // Start from last server snapshots if available so we don't wipe multi-volume configs
        const base = (gSnapshotsLast && typeof gSnapshotsLast === "object") ? gSnapshotsLast : defaultSnapshots();
        const baseVols = Array.isArray(base.volumes) ? base.volumes : [];

        // Clone volumes so we can edit safely
        const volumes = baseVols.map(v => (v && typeof v === "object") ? { ...v } : {}).filter(v => !!v);

        // If server had no volumes, keep at least one
        if (volumes.length === 0) {
            volumes.push({ name: "data", source_subvolume: src0, snap_root: root0 });
        }

        // Always keep volume[0] wired to the simple UI root + src (for now)
        volumes[0].name = String(volumes[0].name || "data");
        volumes[0].source_subvolume = String(volumes[0].source_subvolume || src0);
        volumes[0].snap_root = root0;

        // Global schedule is always present (A mode)
        const out = {
            enabled,
            backend: "btrfs",
            per_volume_policy: perVol,
            volumes,
            schedule: { mode: schedMode, times_per_day: global_tpd, jitter_seconds: global_jitter },
            retention: base.retention && typeof base.retention === "object"
                ? base.retention
                : { keep_days: 7, keep_min: 12, keep_max: 500 }
        };

        // If per-volume enabled, apply table values to each volume schedule
        if (perVol && snapVolTbody) {
            for (let i = 0; i < volumes.length; i++) {
                const tpdSel = snapVolTbody.querySelector(`.snapVolTpd[data-i="${i}"]`);
                const jitInp = snapVolTbody.querySelector(`.snapVolJit[data-i="${i}"]`);

                const vtpd = Math.min(24, Math.max(1, parseInt(tpdSel?.value || String(global_tpd), 10) || global_tpd));
                const vjit = Math.min(3600, Math.max(0, parseInt(jitInp?.value || String(global_jitter), 10) || global_jitter));

                volumes[i].schedule = { mode: schedMode, times_per_day: vtpd, jitter_seconds: vjit };
            }
        } else {
            // If per-volume disabled, remove per-volume schedules to keep config clean
            for (const v of volumes) {
                if (v && typeof v === "object") delete v.schedule;
            }
        }

        return out;
    }


    function syncSnapshotsEnabledUi() {
        const enabled = !!snapEnabled?.checked;
        const auto = !!snapAuto?.checked;
        const perVol = !!snapPerVolume?.checked;

        // Grey out the section visually when disabled
        const card = snapEnabled?.closest?.(".card");
        const bd = card ? card.querySelector(".bd") : null;
        // Do not dim the whole card body here, because Save/Reload stay enabled
        // and should continue to look clickable.
        void bd;

        // Always allow toggling + saving + reload
        if (snapEnabled) snapEnabled.disabled = false;
        if (btnSnapSave) btnSnapSave.disabled = false;
        if (btnSnapReload) btnSnapReload.disabled = false;

        // Root remains editable whenever snapshots are enabled
        if (snapRoot) snapRoot.disabled = !enabled;

        // Automatic schedule toggle can be changed only when feature is enabled
        if (snapAuto) snapAuto.disabled = !enabled;

        // Schedule-only controls
        if (snapTimesPerDay) snapTimesPerDay.disabled = !(enabled && auto);
        if (snapJitter) snapJitter.disabled = !(enabled && auto);
        if (snapPerVolume) snapPerVolume.disabled = !(enabled && auto);

        // Enable/disable the per-volume table inputs based on enabled + auto + perVol
        if (snapVolTbody) {
            const rowControls = snapVolTbody.querySelectorAll(".snapVolTpd, .snapVolJit");
            for (const el of rowControls) {
                el.disabled = !(enabled && auto && perVol);
            }
        }

        let label = "Disabled";
        let kind = "warn";

        if (enabled && !auto) {
            label = "Enabled • manual only";
            kind = "ok";
        } else if (enabled && auto) {
            label = perVol ? "Enabled • automatic per-volume" : "Enabled • automatic";
            kind = "ok";
        }

        setSnapshotsPill(kind, label);

        // Re-render table using latest loaded config (or current UI)
        renderSnapshotVolumesTable(gSnapshotsLast || currentSnapshotsFromUi());
    }


    // ---------------------------
    // Retention UI helpers
    // ---------------------------
    function currentRetentionPolicyFromUi() {
        const mode = retMode?.value || "never";

        let days = 90;
        let max_files = 50;
        let max_total_mb = 20480;

        if (retDays && retDays.value) days = parseInt(retDays.value, 10) || days;
        if (retMaxFiles && retMaxFiles.value) max_files = parseInt(retMaxFiles.value, 10) || max_files;
        if (retMaxMB && retMaxMB.value) max_total_mb = parseInt(retMaxMB.value, 10) || max_total_mb;

        return { mode, days, max_files, max_total_mb };
    }

    function syncRetentionModeUi() {
        const mode = retMode?.value || "never";
        if (!retDays || !retMaxFiles || !retMaxMB) return;

        retDays.classList.add("hidden");
        retMaxFiles.classList.add("hidden");
        retMaxMB.classList.add("hidden");

        if (mode === "days") retDays.classList.remove("hidden");
        if (mode === "files") retMaxFiles.classList.remove("hidden");
        if (mode === "size_mb") retMaxMB.classList.remove("hidden");

        updateRetentionPill();
    }

    function applyRetentionToUi(pol) {
        const mode = pol && pol.mode ? String(pol.mode) : "never";
        if (retMode) retMode.value = mode;

        if (retDays && pol && pol.days != null) retDays.value = String(pol.days);
        if (retMaxFiles && pol && pol.max_files != null) retMaxFiles.value = String(pol.max_files);
        if (retMaxMB && pol && pol.max_total_mb != null) retMaxMB.value = String(pol.max_total_mb);

        syncRetentionModeUi();
        updateRetentionPill();
    }

    function updateRetentionPill() {
        if (!retentionPill) return;
        const p = currentRetentionPolicyFromUi();

        let label = "Never";
        if (p.mode === "days") label = `Keep ${p.days} days`;
        if (p.mode === "files") label = `Keep ${p.max_files} files`;
        if (p.mode === "size_mb") label = `Keep ≤ ${p.max_total_mb} MB`;

        setSimplePill(retentionPill, "info", "Policy", label);
    }

    function clearPreview() {
        if (retTbody) retTbody.innerHTML = "";
        setSimplePill(retPreviewPill, "warn", "Preview", "—");
        setSimplePill(retSummaryPill, "info", "Summary", "—");
    }

    function renderPreview(j) {
        // Expected:
        // { ok:true, candidates:[{name,size_bytes,mtime_iso,reason}], summary:{candidate_files,candidate_bytes,total_archives,total_bytes} }
        const cands = Array.isArray(j.candidates) ? j.candidates : [];
        const sum = j.summary || {};

        const files = Number(sum.candidate_files ?? cands.length ?? 0);
        const bytes = Number(sum.candidate_bytes ?? 0);

        setSimplePill(
            retPreviewPill,
            files > 0 ? "warn" : "ok",
            "Preview",
            files > 0 ? `${files} file(s)` : "Nothing to delete"
        );

        setSimplePill(
            retSummaryPill,
            "info",
            "Summary",
            `Would free ${fmtBytes(bytes)} • archives total ${fmtBytes(Number(sum.total_bytes || 0))}`
        );

        if (!retTbody) return;
        retTbody.innerHTML = "";

        for (const it of cands) {
            const tr = document.createElement("tr");
            const name = String(it.name || "—");
            const size = fmtBytes(it.size_bytes);
            const mtime = String(it.mtime_iso || "—");
            const reason = String(it.reason || "—");

            tr.innerHTML = `
        <td class="col-name mono" title="${escapeHtml(name)}">${escapeHtml(name)}</td>
        <td class="col-size mono" title="${escapeHtml(size)}">${escapeHtml(size)}</td>
        <td class="col-age mono" title="${escapeHtml(mtime)}">${escapeHtml(mtime)}</td>
        <td class="col-reason" title="${escapeHtml(reason)}">${escapeHtml(reason)}</td>
      `;
            retTbody.appendChild(tr);
        }
    }

    // ---------------------------
    // Rotation UI helpers (AUTOMATIC policy)
    // Server modes: manual | daily | size_mb | daily_or_size_mb
    // ---------------------------
    function normalizeRotateMode(m) {
        m = String(m || "").trim();
        return ALLOWED_ROT_MODES.has(m) ? m : "manual";
    }

    function currentRotatePolicyFromUi() {
        const mode = normalizeRotateMode(rotMode?.value || "manual");
        let max_active_mb = 256; // UI default
        if (rotMaxMB && rotMaxMB.value) {
            max_active_mb = parseInt(rotMaxMB.value, 10) || max_active_mb;
        }
        return { mode, max_active_mb };
    }

    function syncRotateModeUi() {
        const mode = normalizeRotateMode(rotMode?.value || "manual");
        if (!rotMaxMB) return;

        // show max MB only for size-based modes
        rotMaxMB.classList.add("hidden");
        if (mode === "size_mb" || mode === "daily_or_size_mb") {
            rotMaxMB.classList.remove("hidden");
        }

        updateRotatePolicyPill();
    }

    function applyRotatePolicyToUi(pol) {
        const mode = normalizeRotateMode(pol && pol.mode ? String(pol.mode) : "manual");
        if (rotMode) rotMode.value = mode;

        if (rotMaxMB && pol && pol.max_active_mb != null) {
            rotMaxMB.value = String(pol.max_active_mb);
        }

        syncRotateModeUi();
        updateRotatePolicyPill();
    }

    function updateRotatePolicyPill() {
        if (!rotatePolicyPill) return;

        const p = currentRotatePolicyFromUi();
        let label = "Manual only";
        if (p.mode === "daily") label = "Daily (UTC)";
        if (p.mode === "size_mb") label = `When > ${p.max_active_mb} MB`;
        if (p.mode === "daily_or_size_mb") label = `> ${p.max_active_mb} MB OR daily (UTC)`;

        setSimplePill(rotatePolicyPill, "info", "Policy", label);
    }

    // ---------------------------
    // Active file info pill
    // ---------------------------
    function updateActiveSizePill(j) {
        if (!activeSizePill) return;
        const bytes = j && typeof j.audit_active_bytes === "number" ? j.audit_active_bytes : null;
        const path = j && j.audit_active_path ? String(j.audit_active_path) : "";

        const label = bytes == null || bytes < 0 ? "—" : `${fmtBytes(bytes)}${path ? " • " + path : ""}`;
        setSimplePill(activeSizePill, "info", "Active log", label);
    }
    // ---------------------------
    // Upload limits UI helpers
    // ---------------------------
    function tieringCandidateByPoolId(poolId) {
        const want = String(poolId || "").trim();
        if (!want) return null;
        for (const c of gTieringCandidates) {
            if (String(c?.pool_id || "").trim() === want) return c;
        }
        return null;
    }

    function fmtBytesGiB(n) {
        const x = Number(n);
        if (!Number.isFinite(x) || x < 0) return "—";
        return `${(x / (1024 ** 3)).toFixed(1)} GiB`;
    }

    function tieringWarningsText(c) {
        const ws = Array.isArray(c?.warnings) ? c.warnings : [];
        if (!ws.length) return tr("admin.settings.none", null, "None");

        return ws.map(w => {
            if (w === "usb_blocked") return tr("admin.settings.warn_usb_blocked", null, "USB blocked");
            if (w === "removable_blocked") return tr("admin.settings.warn_removable_blocked", null, "Removable blocked");
            if (w === "low_free_space") return tr("admin.settings.warn_low_free_space", null, "Low free space");
            if (w === "not_mounted") return tr("admin.settings.warn_not_mounted", null, "Not mounted");
            if (w === "not_writable") return tr("admin.settings.warn_not_writable", null, "Not writable");
            if (w === "statvfs_failed") return tr("admin.settings.warn_space_unknown", null, "Space unknown");
            if (w === "missing_mount") return tr("admin.settings.warn_missing_mount", null, "Missing mount");
            return String(w);
        }).join(", ");
    }

    function renderTieringPoolOptions(selectedPoolId) {
        if (!tieringLandingPool) return;

        const pools = Array.isArray(gTieringCandidates) ? [...gTieringCandidates] : [];
        tieringLandingPool.innerHTML = "";

        if (!pools.length) {
            const opt = document.createElement("option");
            opt.value = "";
            opt.textContent = tr("admin.settings.no_pools_available", null, "(no pools available)");
            tieringLandingPool.appendChild(opt);
            tieringLandingPool.disabled = true;
            return;
        }

        // eligible first, then blocked
        pools.sort((a, b) => {
            const ae = !!a?.eligible;
            const be = !!b?.eligible;
            if (ae !== be) return ae ? -1 : 1;
            return String(a?.pool_id || "").localeCompare(String(b?.pool_id || ""));
        });

        tieringLandingPool.disabled = false;

        let selectedFound = false;

        for (const p of pools) {
            const poolId = String(p?.pool_id || "").trim();
            const mount = String(p?.mount_path || "").trim();
            const eligible = !!p?.eligible;
            if (!poolId) continue;

            const freeTxt = fmtBytesGiB(p?.free_bytes);
            const warnTxt = tieringWarningsText(p);

            const opt = document.createElement("option");
            opt.value = poolId;
            opt.disabled = !eligible;

            if (eligible) {
                opt.textContent = tr("admin.settings.pool_free_label", { pool: poolId, mount, free: freeTxt }, `${poolId} — ${mount} — ${freeTxt} free`);
            } else {
                opt.textContent = tr("admin.settings.pool_blocked_label", { pool: poolId, mount, warnings: warnTxt }, `${poolId} — ${mount} — BLOCKED (${warnTxt})`);
            }

            if (poolId === String(selectedPoolId || "")) {
                opt.selected = true;
                selectedFound = true;
            }

            tieringLandingPool.appendChild(opt);
        }

        // If current saved pool was blocked/unknown and no option got selected,
        // select first eligible one if available.
        if (!selectedFound) {
            const firstEligible = pools.find(p => !!p?.eligible);
            if (firstEligible) tieringLandingPool.value = String(firstEligible.pool_id || "");
        }
    }
    function updateTieringDetailPills() {
        const c = tieringCandidateByPoolId(tieringLandingPool?.value || "");

        if (!c) {
            setSimplePill(tieringMountPill, "info", tr("admin.settings.mount", null, "Mount"), "—");
            setSimplePill(tieringSpacePill, "info", tr("admin.settings.space", null, "Space"), "—");
            setSimplePill(tieringEligibilityPill, "warn", tr("admin.settings.eligibility", null, "Eligibility"), tr("admin.settings.no_selection", null, "No selection"));
            setSimplePill(tieringWarnPill, "warn", tr("admin.settings.warnings", null, "Warnings"), "—");
            return;
        }

        const mount = String(c.mount_path || "—");
        const freeTxt = fmtBytesGiB(c.free_bytes);
        const totalTxt = fmtBytesGiB(c.total_bytes);
        const eligible = !!c.eligible;
        const warnTxt = tieringWarningsText(c);

        setSimplePill(tieringMountPill, "info", tr("admin.settings.mount", null, "Mount"), mount);
        setSimplePill(tieringSpacePill, "info", tr("admin.settings.space", null, "Space"), tr("admin.settings.free_total", { free: freeTxt, total: totalTxt }, `${freeTxt} free / ${totalTxt} total`));
        setSimplePill(
            tieringEligibilityPill,
            eligible ? "ok" : "warn",
            tr("admin.settings.eligibility", null, "Eligibility"),
            eligible ? tr("admin.settings.eligible", null, "Eligible") : tr("admin.settings.blocked", null, "Blocked")
        );
        setSimplePill(
            tieringWarnPill,
            warnTxt === tr("admin.settings.none", null, "None") ? "info" : "warn",
            tr("admin.settings.warnings", null, "Warnings"),
            warnTxt
        );
    }
    function applyTieringToUi(j) {
        gTieringCandidates = Array.isArray(j?.upload_tiering_candidates) ? j.upload_tiering_candidates : [];

        const t = (j && typeof j.tiering === "object" && j.tiering) ? j.tiering : {};
        const enabled = !!t.enabled;
        const landingPoolId = String(t.landing_pool_id || "").trim();

        if (tieringEnabled) tieringEnabled.checked = enabled;
        renderTieringPoolOptions(landingPoolId);

        if (tieringIntervalSec) tieringIntervalSec.value = String(Number(t.worker_interval_sec ?? 60));
        if (tieringMinAgeSec) tieringMinAgeSec.value = String(Number(t.min_age_sec ?? 60));
        if (tieringMaxPass) tieringMaxPass.value = String(Number(t.max_candidates_per_pass ?? 8));

        updateTieringDetailPills();

        if (tieringPill) {
            let txt = tr("admin.settings.disabled", null, "Disabled");
            let kind = "warn";

            if (enabled) {
                const c = tieringCandidateByPoolId(landingPoolId);
                if (landingPoolId && c && c.eligible) {
                    txt = tr("admin.settings.enabled_pool", { pool: landingPoolId }, `Enabled • ${landingPoolId}`);
                    kind = "ok";
                } else if (landingPoolId) {
                    txt = tr("admin.settings.enabled_pool_blocked", { pool: landingPoolId }, `Enabled • ${landingPoolId} (blocked)`);
                    kind = "warn";
                } else {
                    txt = tr("admin.settings.enabled_no_pool", null, "Enabled • no pool");
                    kind = "warn";
                }
            }

            tieringPill.className = "pill " + kind;
            tieringPill.innerHTML = `<span class="k">${escapeHtml(tr("admin.settings.tiering", null, "Tiering:"))}</span> <span class="v">${escapeHtml(txt)}</span>`;
        }
    }

    function currentTieringFromUi() {
        return {
            enabled: !!tieringEnabled?.checked,
            landing_pool_id: String(tieringLandingPool?.value || "").trim(),
            worker_interval_sec: clampInt(tieringIntervalSec?.value, 60, 5, 3600),
            min_age_sec: clampInt(tieringMinAgeSec?.value, 60, 0, 86400),
            max_candidates_per_pass: clampInt(tieringMaxPass?.value, 8, 1, 1000)
        };
    }
    // ---------------------------
    // DNA Connect alerts UI helpers
    // ---------------------------
    function normalizeDnaAlertLevel(v) {
        const s = String(v || "").trim().toLowerCase();
        if (s === "security") return "security";
        if (s === "error") return "error";
        if (s === "warning") return "warning";
        if (s === "info") return "info";
        return "warning";
    }

    function applyDnaAlertsToUi(j) {
        const d = (j && typeof j.dna_connect_alerts === "object" && j.dna_connect_alerts)
            ? j.dna_connect_alerts
            : {};

        const enabled = !!d.enabled;
        const recipient = String(d.recipient || "").trim();
        const minLevel = normalizeDnaAlertLevel(d.min_level || "warning");
        const cliPath = String(d.cli_path || "/usr/local/bin/dna-connect-cli").trim();
        const dataDir = String(d.data_dir || "/var/lib/pqnas/dna-alerts").trim();

        if (dnaAlertsEnabled) dnaAlertsEnabled.checked = enabled;
        if (dnaAlertsRecipient) dnaAlertsRecipient.value = recipient;
        if (dnaAlertsMinLevel) dnaAlertsMinLevel.value = minLevel;
        if (dnaAlertsCliPath) dnaAlertsCliPath.value = cliPath;
        if (dnaAlertsDataDir) dnaAlertsDataDir.value = dataDir;

        let txt = tr("admin.settings.disabled", null, "Disabled");
        let kind = "warn";
        if (enabled) {
            if (recipient) {
                const recipientShort =
                    recipient.length > 48
                        ? `${recipient.slice(0, 48)}…`
                        : recipient;
                txt = tr("admin.dna.enabled_for", { recipient: recipientShort }, `Enabled • ${recipientShort}`);
                kind = "ok";
            } else {
                txt = tr("admin.dna.enabled_recipient_missing", null, "Enabled • recipient missing");
                kind = "warn";
            }
        }

        if (dnaAlertsPill) {
            dnaAlertsPill.className = "pill " + kind;
            dnaAlertsPill.innerHTML =
                `<span class="k">${escapeHtml(tr("admin.dna.alerts", null, "DNA alerts:"))}</span> <span class="v">${escapeHtml(txt)}</span>`;
        }

        if (dnaAlertsInfoPill) {
            const info = `${minLevel} • ${cliPath}`;
            dnaAlertsInfoPill.className = "pill info";
            dnaAlertsInfoPill.innerHTML =
                `<span class="k">${escapeHtml(tr("admin.dna.route", null, "Route:"))}</span> <span class="v">${escapeHtml(info)}</span>`;
        }
    }
    function applyDnaIdentityToUi(j) {
        const d = (j && typeof j.dna_connect_identity === "object" && j.dna_connect_identity)
            ? j.dna_connect_identity
            : {};

        gDnaConnectIdentity = d;

        const exists = !!d.exists;
        const fp = String(d.fingerprint || "").trim();

        if (dnaAlertsIdentityPill) {
            const txt = exists
                ? (fp ? `${fp.slice(0, 16)}…` : tr("admin.settings.present", null, "Present"))
                : tr("admin.settings.not_created", null, "Not created");
            dnaAlertsIdentityPill.className = "pill " + (exists ? "ok" : "warn");
            dnaAlertsIdentityPill.innerHTML =
                `<span class="k">${escapeHtml(tr("admin.dna.pqnas_id", null, "PQ-NAS ID:"))}</span><span class="v">${escapeHtml(txt)}</span>`;
        }

        if (btnDnaAlertsCreateId) btnDnaAlertsCreateId.disabled = exists;
        if (btnDnaAlertsShowId) btnDnaAlertsShowId.disabled = !exists;
        if (btnDnaAlertsSendRequest) btnDnaAlertsSendRequest.disabled = !exists;
    }
    function currentDnaAlertsFromUi() {
        return {
            enabled: !!dnaAlertsEnabled?.checked,
            recipient: String(dnaAlertsRecipient?.value || "").trim(),
            min_level: normalizeDnaAlertLevel(dnaAlertsMinLevel?.value || "warning"),
            cli_path: String(dnaAlertsCliPath?.value || "").trim(),
            data_dir: String(dnaAlertsDataDir?.value || "").trim()
        };
    }
    function clampU64(n) {
        const x = Number(n);
        if (!Number.isFinite(x)) return null;
        if (x <= 0) return null;
        // JS can represent up to 2^53-1 safely; your caps are well below that
        return Math.floor(x);
    }
    function clampInt(v, def, lo, hi) {
        const x = parseInt(String(v ?? ""), 10);
        if (!Number.isFinite(x)) return def;
        if (x < lo) return lo;
        if (x > hi) return hi;
        return x;
    }
    function applyUploadLimitsToUi(j) {
        const hard = (j && typeof j.payload_max_upload_bytes === "number") ? j.payload_max_upload_bytes : null;
        const soft = (j && typeof j.transport_max_upload_bytes === "number") ? j.transport_max_upload_bytes : null;

        const eff = (hard != null && soft != null) ? Math.min(hard, soft)
            : (hard != null) ? hard
                : (soft != null) ? soft
                    : null;

        if (uploadSoftMax && soft != null) uploadSoftMax.value = String(Math.floor(soft));

        // Pills
        if (uploadSoftPill) setSimplePill(uploadSoftPill, "info", "Soft cap", soft != null ? fmtBytes(soft) : "—");
        if (uploadHardPill) setSimplePill(uploadHardPill, "info", "Hard cap", hard != null ? fmtBytes(hard) : "—");
        if (uploadEffectivePill) setSimplePill(uploadEffectivePill, "info", "Effective", eff != null ? fmtBytes(eff) : "—");

        // Header pill shows effective
        if (uploadPill) {
            const kind = (eff != null) ? "info" : "warn";
            uploadPill.className = "pill " + kind;
            uploadPill.innerHTML = `<span class="k">Effective:</span> <span class="v">${escapeHtml(eff != null ? fmtBytes(eff) : "—")}</span>`;
        }
    }
    // ---------------------------
    // Main refresh: load all settings
    // ---------------------------
    async function refreshAll() {
        setStatusPill("warn", "loading…");
        try {
            const j = await apiSettingsGet();

            gStorageRoots = (j && typeof j.storage_roots === "object" && j.storage_roots) ? j.storage_roots : null;
            gTieringCandidates = Array.isArray(j?.upload_tiering_candidates) ? j.upload_tiering_candidates : [];

            const allowed = Array.isArray(j.allowed) ? j.allowed : ["SECURITY", "ADMIN", "INFO", "DEBUG"];
            const persisted = j.audit_min_level || "ADMIN";
            const runtime = j.audit_min_level_runtime || persisted;

            if (persistedVal) persistedVal.textContent = persisted;
            if (runtimeVal) runtimeVal.textContent = runtime;

            setOptions(allowed, persisted);

            // retention
            const ret = j.audit_retention || { mode: "never", days: 90, max_files: 50, max_total_mb: 20480 };
            applyRetentionToUi(ret);

            // rotation policy (automatic)
            const rp = j.audit_rotation || { mode: "manual", max_active_mb: 256, rotate_utc_day: "" };
            applyRotatePolicyToUi(rp);

            // theme (server -> apply)
            const serverTheme = j && j.ui_theme ? String(j.ui_theme) : "dark";
            applyTheme(serverTheme);
            // snapshots
            applySnapshotsToUi(j.snapshots || defaultSnapshots());

            // active audit file info
            updateActiveSizePill(j);

            // upload limits
            applyUploadLimitsToUi(j);

            // tiering
            applyTieringToUi(j);

            // DNA Connect alerts
            applyDnaAlertsToUi(j);
            applyDnaIdentityToUi(j);


            clearPreview();
            setStatusPill("ok", "ready");
        } catch (e) {
            console.error(e);
            setStatusPill("error", "error");
            setSimplePill(activeSizePill, "warn", "Active log", "—");
            showToast("fail", tr("admin.settings.load_failed", null, "Failed to load settings"), String(e.message || e));
        }
    }

    // ---------------------------
    // Wire audit level
    // ---------------------------
    if (btnReload) {
        btnReload.addEventListener("click", (ev) => {
            ev.preventDefault();
            refreshAll();
        });
    }

    if (btnSave) {
        btnSave.addEventListener("click", async (ev) => {
            ev.preventDefault();
            const lvl = levelSelect ? levelSelect.value : "";
            btnSave.disabled = true;
            setStatusPill("warn", "saving…");
            try {
                await apiSettingsPost({ audit_min_level: lvl });
                showToast("ok", tr("admin.common.saved", null, "Saved"), `audit_min_level = ${lvl}`);
                await refreshAll();
            } catch (e) {
                console.error(e);
                showToast("fail", tr("admin.common.save_failed", null, "Save failed"), String(e.message || e));
                setStatusPill("error", "error");
            } finally {
                btnSave.disabled = false;
            }
        });
    }

    function openHelpModal() {
        document.getElementById("helpModalBackdrop")?.classList.remove("hidden");
    }

    function closeHelpModal() {
        document.getElementById("helpModalBackdrop")?.classList.add("hidden");
    }

    document.addEventListener("click", (ev) => {
        const openBtn = ev.target.closest("#btnUploadsHelp");
        if (openBtn) {
            ev.preventDefault();
            ev.stopPropagation();
            openHelpModal();
            return;
        }

        const closeBtn = ev.target.closest("#btnHelpModalClose");
        if (closeBtn) {
            ev.preventDefault();
            ev.stopPropagation();
            closeHelpModal();
            return;
        }

        const backdrop = document.getElementById("helpModalBackdrop");
        if (backdrop && ev.target === backdrop) {
            closeHelpModal();
            return;
        }

        const closeDnaBtn = ev.target.closest("#btnDnaIdentityModalClose");
        if (closeDnaBtn) {
            ev.preventDefault();
            ev.stopPropagation();
            closeDnaIdentityModal();
            return;
        }

        if (dnaIdentityModalBackdrop && ev.target === dnaIdentityModalBackdrop) {
            closeDnaIdentityModal();
            return;
        }
    });

    document.addEventListener("keydown", (ev) => {
        if (ev.key === "Escape") {
            closeHelpModal();
            closeDnaIdentityModal();
        }
    });
    // ---------------------------
    // Wire retention
    // ---------------------------
    retMode?.addEventListener("change", () => {
        syncRetentionModeUi();
        clearPreview();
    });

    retDays?.addEventListener("change", () => {
        updateRetentionPill();
        clearPreview();
    });
    retMaxFiles?.addEventListener("input", () => {
        updateRetentionPill();
        clearPreview();
    });
    retMaxMB?.addEventListener("input", () => {
        updateRetentionPill();
        clearPreview();
    });

    snapEnabled?.addEventListener("change", () => {
        syncSnapshotsEnabledUi();
    });

    snapAuto?.addEventListener("change", () => {
        syncSnapshotsEnabledUi();
    });

    snapPerVolume?.addEventListener("change", () => {
        renderSnapshotVolumesTable(gSnapshotsLast || currentSnapshotsFromUi());
        syncSnapshotsEnabledUi();
    });

    // ---------------------------
    // Wire upload limits
    // ---------------------------
    btnUploadReload?.addEventListener("click", (ev) => {
        ev.preventDefault();
        refreshAll();
    });
    btnTieringReload?.addEventListener("click", (ev) => {
        ev.preventDefault();
        refreshAll();
    });
    tieringLandingPool?.addEventListener("change", () => {
        updateTieringDetailPills();
    });
    tieringEnabled?.addEventListener("change", () => {
        updateTieringDetailPills();
    });
    btnTieringSave?.addEventListener("click", async (ev) => {
        ev.preventDefault();

        const t = currentTieringFromUi();
        if (t.enabled && !t.landing_pool_id) {
            showToast("fail", tr("admin.settings.invalid_tiering", null, "Invalid tiering"), tr("admin.settings.select_landing_pool_or_disable", null, "Please select a landing pool or disable tiering."));
            return;
        }

        if (t.enabled) {
            const c = tieringCandidateByPoolId(t.landing_pool_id);
            if (!c) {
                showToast("fail", tr("admin.settings.invalid_tiering", null, "Invalid tiering"), tr("admin.settings.landing_pool_not_found", null, "Selected landing pool was not found."));
                return;
            }
            if (!c.eligible) {
                showToast("fail", tr("admin.settings.invalid_tiering", null, "Invalid tiering"), tr("admin.settings.landing_pool_blocked", { warnings: tieringWarningsText(c) }, `Selected landing pool is blocked: ${tieringWarningsText(c)}`));
                return;
            }
        }

        btnTieringSave.disabled = true;
        setStatusPill("warn", "saving…");
        try {
            await apiSettingsPost({ tiering: t });
            showToast("ok", tr("admin.common.saved", null, "Saved"), t.enabled
                ? tr("admin.settings.landing_tier_enabled", { pool: t.landing_pool_id }, `Landing tier enabled • pool ${t.landing_pool_id}`)
                : tr("admin.settings.landing_tier_disabled", null, "Landing tier disabled"));
            await refreshAll();
        } catch (e) {
            console.error(e);
            showToast("fail", tr("admin.common.save_failed", null, "Save failed"), String(e.message || e));
            setStatusPill("error", "error");
        } finally {
            btnTieringSave.disabled = false;
        }
    });
    btnUploadSave?.addEventListener("click", async (ev) => {
        ev.preventDefault();

        const v = clampU64(uploadSoftMax?.value);
        if (v == null) {
            showToast("fail", tr("admin.settings.invalid_value", null, "Invalid value"), tr("admin.settings.transport_max_positive", null, "transport_max_upload_bytes must be a positive integer (bytes)."));
            return;
        }

        btnUploadSave.disabled = true;
        setStatusPill("warn", "saving…");
        try {
            await apiSettingsPost({ transport_max_upload_bytes: v });
            showToast("ok", tr("admin.common.saved", null, "Saved"), tr("admin.settings.max_upload_saved", { size: fmtBytes(v), bytes: v }, `Max upload = ${fmtBytes(v)} (${v} bytes)`));
            await refreshAll();
        } catch (e) {
            console.error(e);
            showToast("fail", tr("admin.common.save_failed", null, "Save failed"), String(e.message || e));
            setStatusPill("error", "error");
        } finally {
            btnUploadSave.disabled = false;
        }
    });
    btnRetentionSave?.addEventListener("click", async (ev) => {
        ev.preventDefault();
        const pol = currentRetentionPolicyFromUi();

        btnRetentionSave.disabled = true;
        setStatusPill("warn", "saving…");
        try {
            await apiSettingsPost({ audit_retention: pol });
            showToast("ok", tr("admin.common.saved", null, "Saved"), tr("admin.settings.retention_policy_updated", null, "Retention policy updated"));
            await refreshAll();
        } catch (e) {
            console.error(e);
            showToast("fail", tr("admin.common.save_failed", null, "Save failed"), String(e.message || e));
            setStatusPill("error", "error");
        } finally {
            btnRetentionSave.disabled = false;
        }
    });

    btnRetentionPreview?.addEventListener("click", async (ev) => {
        ev.preventDefault();
        const pol = currentRetentionPolicyFromUi();

        btnRetentionPreview.disabled = true;
        setSimplePill(retPreviewPill, "warn", "Preview", "checking…");

        try {
            const j = await apiPreviewPrune(pol);
            renderPreview(j);
            showToast("ok", tr("admin.settings.preview_ready", null, "Preview ready"), tr("admin.settings.candidate_files", { count: (j.summary && j.summary.candidate_files) || 0 }, `${(j.summary && j.summary.candidate_files) || 0} candidate file(s)`));
        } catch (e) {
            console.error(e);
            setSimplePill(retPreviewPill, "fail", "Preview", "error");
            showToast("fail", tr("admin.settings.preview_failed", null, "Preview failed"), String(e.message || e));
        } finally {
            btnRetentionPreview.disabled = false;
        }
    });

    btnRetentionPrune?.addEventListener("click", async (ev) => {
        ev.preventDefault();

        const ok = await openAdminConfirmModal({
            title: tr("admin.confirm.run_prune_title", null, "Run audit prune now?"),
            subtitle: tr("admin.confirm.run_prune_sub", null, "This deletes rotated audit archives according to the saved retention policy."),
            rows: [
                { label: tr("admin.settings.target", null, "Target"), value: tr("admin.settings.rotated_archives_only", null, "Rotated audit archives only") },
                { label: tr("admin.settings.active_log", null, "Active log"), value: tr("admin.settings.active_log_never_deleted", null, "pqnas_audit.jsonl is never deleted"), mono: true },
                { label: tr("admin.settings.policy", null, "Policy"), value: tr("admin.settings.uses_saved_retention_policy", null, "Uses the currently saved retention policy") },
            ],
            note: tr("admin.settings.prune_permanent_note", null, "This is permanent for selected rotated archive files. Preview prune first if you want to review candidates."),
            confirmText: tr("admin.audit.run_prune", null, "Run prune now"),
            cancelText: tr("admin.common.cancel", null, "Cancel"),
            warn: true,
        });
        if (!ok) return;

        btnRetentionPrune.disabled = true;
        setSimplePill(retPreviewPill, "warn", "Preview", "pruning…");

        try {
            const j = await apiRunPrune();
            showToast(
                "ok",
                tr("admin.settings.prune_complete", null, "Prune complete"),
                tr("admin.settings.prune_deleted_freed", { count: (j.deleted_files || 0), size: fmtBytes(j.deleted_bytes || 0) }, `Deleted ${(j.deleted_files || 0)} file(s) • freed ${fmtBytes(j.deleted_bytes || 0)}`)
            );
            const pol = currentRetentionPolicyFromUi();
            const pv = await apiPreviewPrune(pol);
            renderPreview(pv);
        } catch (e) {
            console.error(e);
            setSimplePill(retPreviewPill, "fail", "Preview", "error");
            showToast("fail", tr("admin.settings.prune_failed", null, "Prune failed"), String(e.message || e));
        } finally {
            btnRetentionPrune.disabled = false;
        }
    });
    // ---------------------------
    // Wire snapshot buttons
    // ---------------------------
    btnSnapReload?.addEventListener("click", (ev) => {
        ev.preventDefault();
        refreshAll();
    });

    btnSnapSave?.addEventListener("click", async (ev) => {
        ev.preventDefault();
        const sn = currentSnapshotsFromUi();
        btnSnapSave.disabled = true;
        setSnapshotsPill("warn", tr("admin.settings.saving", null, "Saving…"));
        try {
            const j = await apiSettingsPost({ snapshots: sn });

            // Merge: server may omit per_volume_policy and/or schedules; don’t let that reset UI.
            const merged = (j && j.snapshots && typeof j.snapshots === "object")
                ? {
                    ...sn,
                    ...j.snapshots,
                    per_volume_policy: (typeof j.snapshots.per_volume_policy === "boolean")
                        ? j.snapshots.per_volume_policy
                        : sn.per_volume_policy,
                    volumes: Array.isArray(j.snapshots.volumes) ? j.snapshots.volumes : sn.volumes
                }
                : sn;

            applySnapshotsToUi(merged);
            showToast("ok", tr("admin.common.saved", null, "Saved"), tr("admin.settings.snapshots_updated", null, "Snapshots settings updated"));

        } catch (e) {
            console.error(e);
            setSnapshotsPill("fail", tr("admin.settings.error", null, "Error"));
            showToast("fail", tr("admin.common.save_failed", null, "Save failed"), String(e.message || e));
        } finally {
            btnSnapSave.disabled = false;
        }
    });

    // ---------------------------
    // Wire manual rotation
    // ---------------------------
    btnRotateNow?.addEventListener("click", async (ev) => {
        ev.preventDefault();

        const ok = await openAdminConfirmModal({
            title: tr("admin.confirm.rotate_title", null, "Rotate audit log now?"),
            subtitle: tr("admin.confirm.rotate_sub", null, "This closes the current audit log and starts a fresh active log."),
            rows: [
                { label: tr("admin.settings.active_log", null, "Active log"), value: "pqnas_audit.jsonl", mono: true },
                { label: tr("admin.settings.action", null, "Action"), value: tr("admin.settings.rename_current_log", null, "Rename current log into timestamped archive") },
                { label: tr("admin.settings.chain", null, "Chain"), value: tr("admin.settings.continuity_preserved", null, "Continuity preserved by rotate header") },
            ],
            note: tr("admin.settings.rotate_note", null, "Already-written audit lines remain unchanged. New audit events will continue in the fresh log."),
            confirmText: tr("admin.audit.rotate_now", null, "Rotate now"),
            cancelText: tr("admin.common.cancel", null, "Cancel"),
            warn: true,
        });
        if (!ok) return;

        btnRotateNow.disabled = true;
        setStatusPill("warn", "rotating…");

        try {
            await apiRotateAudit();
            showToast("ok", tr("admin.settings.rotated", null, "Rotated"), tr("admin.settings.new_active_log_started", null, "New active audit log started"));
            await refreshAll();
        } catch (e) {
            console.error(e);
            showToast("fail", tr("admin.settings.rotate_failed", null, "Rotate failed"), String(e.message || e));
            setStatusPill("error", "error");
        } finally {
            btnRotateNow.disabled = false;
        }
    });
    btnDnaAlertsReload?.addEventListener("click", (ev) => {
        ev.preventDefault();
        refreshAll();
    });

    btnDnaAlertsSave?.addEventListener("click", async (ev) => {
        ev.preventDefault();

        const d = currentDnaAlertsFromUi();

        if (d.enabled && !d.recipient) {
            showToast("fail", tr("admin.dna.invalid", null, "Invalid DNA alerts"), tr("admin.dna.recipient_required", null, "Recipient is required when DNA alerts are enabled."));
            return;
        }
        if (d.enabled && !d.cli_path) {
            showToast("fail", tr("admin.dna.invalid", null, "Invalid DNA alerts"), tr("admin.dna.cli_required", null, "CLI path is required when DNA alerts are enabled."));
            return;
        }


        btnDnaAlertsSave.disabled = true;
        setStatusPill("warn", "saving…");
        try {
            await apiSettingsPost({ dna_connect_alerts: d });
            showToast("ok", tr("admin.common.saved", null, "Saved"), d.enabled
                ? tr("admin.dna.enabled_for_full", { recipient: d.recipient }, `DNA alerts enabled for ${d.recipient}`)
                : tr("admin.dna.disabled", null, "DNA alerts disabled"));
            await refreshAll();
        } catch (e) {
            console.error(e);
            showToast("fail", tr("admin.common.save_failed", null, "Save failed"), String(e.message || e));
            setStatusPill("error", "error");
        } finally {
            btnDnaAlertsSave.disabled = false;
        }
    });
    btnDnaAlertsCreateId?.addEventListener("click", async (ev) => {
        ev.preventDefault();

        btnDnaAlertsCreateId.disabled = true;
        setStatusPill("warn", "creating DNA ID…");
        try {
            const j = await apiCreateDnaAlertIdentity();
            showToast("ok", tr("admin.dna.id_created", null, "PQ-NAS ID created"), String(j.message || tr("admin.dna.identity_created", null, "DNA identity created")));
            await refreshAll();
        } catch (e) {
            console.error(e);
            showToast("fail", tr("admin.dna.create_id_failed", null, "Create ID failed"), String(e.message || e));
            setStatusPill("error", "error");
        } finally {
            await refreshAll();
        }
    });

    btnDnaAlertsShowId?.addEventListener("click", async (ev) => {
        ev.preventDefault();

        try {
            const j = await apiGetDnaAlertIdentityInfo();
            const d = j.dna_connect_identity || {};
            const text = [
                tr("admin.dna.exists_line", { value: d.exists ? tr("admin.apps.yes", null, "yes") : tr("admin.apps.no", null, "no") }, `Exists: ${d.exists ? "yes" : "no"}`),
                tr("admin.dna.name_line", { value: d.name || "—" }, `Name: ${d.name || "—"}`),
                tr("admin.dna.fingerprint_line", { value: d.fingerprint || "—" }, `Fingerprint: ${d.fingerprint || "—"}`),
                tr("admin.dna.cli_line", { value: d.cli_path || "—" }, `CLI: ${d.cli_path || "—"}`),
                tr("admin.dna.data_dir_line", { value: d.data_dir || "—" }, `Data dir: ${d.data_dir || "—"}`)
            ].join("\n");
            openDnaIdentityModal(text);
        } catch (e) {
            console.error(e);
            showToast("fail", tr("admin.dna.load_id_failed", null, "Load ID info failed"), String(e.message || e));
        }
    });

    btnDnaAlertsSendRequest?.addEventListener("click", async (ev) => {
        ev.preventDefault();

        btnDnaAlertsSendRequest.disabled = true;
        setStatusPill("warn", "sending contact request…");
        try {
            const j = await apiSendDnaAlertContactRequest();
            showToast("ok", tr("admin.dna.contact_request_sent", null, "Contact request sent"), String(j.message || tr("admin.dna.contact_request_sent", null, "Contact request sent")));
            await refreshAll();
        } catch (e) {
            console.error(e);
            showToast("fail", tr("admin.dna.contact_request_failed", null, "Contact request failed"), String(e.message || e));
            setStatusPill("error", "error");
        } finally {
            btnDnaAlertsSendRequest.disabled = false;
        }
    });
    btnDnaAlertsTest?.addEventListener("click", async (ev) => {
        ev.preventDefault();

        const d = currentDnaAlertsFromUi();
        if (!d.enabled) {
            showToast("fail", tr("admin.dna.disabled", null, "DNA alerts disabled"), tr("admin.dna.enable_before_test", null, "Enable DNA alerts before sending a test."));
            return;
        }
        if (!d.recipient || !d.cli_path || !d.data_dir) {
            showToast("fail", tr("admin.dna.incomplete", null, "Incomplete DNA alerts settings"), tr("admin.dna.incomplete_detail", null, "Recipient, CLI path and DNA data directory are required."));
            return;
        }

        btnDnaAlertsTest.disabled = true;
        setStatusPill("warn", "testing…");
        try {
            const j = await apiTestDnaAlert();
            const detail = String(j.message || tr("admin.dna.test_alert_sent", null, "Test alert sent"));
            showToast("ok", tr("admin.dna.test_sent", null, "Test sent"), detail);
            await refreshAll();
        } catch (e) {
            console.error(e);
            showToast("fail", tr("admin.dna.test_failed", null, "Test failed"), String(e.message || e));
            setStatusPill("error", "error");
        } finally {
            btnDnaAlertsTest.disabled = false;
        }
    });
    // ---------------------------
    // Wire language
    // ---------------------------
    languageSelect?.addEventListener("change", (ev) => {
        ev.preventDefault();
        applyAdminLanguage(languageSelect.value);
    });

    function applyAdminStaticI18n() {
        try {
            if (window.PQNAS_I18N && typeof window.PQNAS_I18N.apply === "function") {
                window.PQNAS_I18N.apply(document);
            }
        } catch (_) {}
    }

    window.addEventListener("pqnas-language-changed", () => {
        updateLanguagePill(currentLanguageName());
        applyAdminStaticI18n();
        if (themePill && themeSelect) setSimplePill(themePill, "info", tr("admin.theme.pill", null, "Theme"), themeSelect.value || "dark");
    });

    // ---------------------------
    // Wire theme
    // ---------------------------
    themeSelect?.addEventListener("change", () => {
        // Update pill and preview instantly
        applyTheme(themeSelect.value);
    });

    btnThemeApply?.addEventListener("click", (ev) => {
        ev.preventDefault();
        const t = normalizeTheme(themeSelect?.value || "dark");
        applyTheme(t);
        showToast("ok", tr("admin.theme.applied", null, "Theme applied"), tr("admin.theme.theme_value", { theme: t }, `Theme: ${t}`));
    });

    btnThemeSave?.addEventListener("click", async (ev) => {
        ev.preventDefault();
        const t = normalizeTheme(themeSelect?.value || "dark");
        try {
            const j = await apiSettingsPost({ ui_theme: t });
            applyTheme(j && j.ui_theme ? j.ui_theme : t);
            showToast("ok", tr("admin.theme.saved", null, "Theme saved"), tr("admin.theme.theme_value", { theme: t }, `Theme: ${t}`));
        } catch (e) {
            showToast("fail", tr("admin.common.save_failed", null, "Save failed"), String(e && e.message ? e.message : e));
        }
    });

    // ---------------------------
    // Wire rotation policy (automatic)
    // ---------------------------
    rotMode?.addEventListener("change", () => {
        syncRotateModeUi();
    });

    rotMaxMB?.addEventListener("input", () => {
        updateRotatePolicyPill();
    });

    btnRotatePolicySave?.addEventListener("click", async (ev) => {
        ev.preventDefault();

        const pol = currentRotatePolicyFromUi();
        btnRotatePolicySave.disabled = true;
        setStatusPill("warn", "saving…");

        try {
            await apiSettingsPost({ audit_rotation: pol });
            showToast("ok", tr("admin.common.saved", null, "Saved"), tr("admin.settings.rotation_policy_updated", null, "Rotation policy updated"));
            await refreshAll();
        } catch (e) {
            console.error(e);
            showToast("fail", tr("admin.common.save_failed", null, "Save failed"), String(e.message || e));
            setStatusPill("error", "error");
        } finally {
            btnRotatePolicySave.disabled = false;
        }
    });

    // init
    updateLanguagePill(currentLanguageName());
    syncRotateModeUi();
    syncRetentionModeUi();
    refreshAll();
})();
