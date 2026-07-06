/* server/src/static/admin_settings.js
 *
 * Admin Settings UI
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

    // --- password auth / own password ---
    const adminPasswordChangeCard = $("adminPasswordChangeCard");
    const adminPasswordAuthPill = $("adminPasswordAuthPill");
    const adminPasswordLogin = $("adminPasswordLogin");
    const adminPasswordCurrent = $("adminPasswordCurrent");
    const adminPasswordNew = $("adminPasswordNew");
    const adminPasswordConfirm = $("adminPasswordConfirm");
    const btnAdminPasswordChange = $("btnAdminPasswordChange");

    // --- password user creation ---
    const adminPasswordUserCreateCard = $("adminPasswordUserCreateCard");
    const adminPasswordUserCreatePill = $("adminPasswordUserCreatePill");
    const adminCreatePasswordUserName = $("adminCreatePasswordUserName");
    const adminCreatePasswordUserLogin = $("adminCreatePasswordUserLogin");
    const adminCreatePasswordUserPassword = $("adminCreatePasswordUserPassword");
    const adminCreatePasswordUserRole = $("adminCreatePasswordUserRole");
    const adminCreatePasswordUserStatus = $("adminCreatePasswordUserStatus");
    const adminCreatePasswordUserQuota = $("adminCreatePasswordUserQuota");
    const btnAdminCreatePasswordUser = $("btnAdminCreatePasswordUser");
    const adminCreatePasswordUserResult = $("adminCreatePasswordUserResult");
    const adminCreatePasswordUserRecovery = $("adminCreatePasswordUserRecovery");
    const adminCreatePasswordUserResultLogin = $("adminCreatePasswordUserResultLogin");
    const adminCreatePasswordUserResultFingerprint = $("adminCreatePasswordUserResultFingerprint");
    const adminCreatePasswordUserResultStatus = $("adminCreatePasswordUserResultStatus");
    const adminCreatePasswordUserResultQuota = $("adminCreatePasswordUserResultQuota");
    const adminCreatePasswordUserCopied = $("adminCreatePasswordUserCopied");

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
    const languageSelect = $("languageSelect"); // legacy fallback if old HTML is still cached
    const languagePicker = $("languagePicker");

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
    const snapPoolSelect = $("snapPoolSelect");
    const btnSnapAddPool = $("btnSnapAddPool");

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


    // --- Nodus federation ---
    const nodusStatusPill = $("nodusStatusPill");
    const btnNodusRefresh = $("btnNodusRefresh");
    const btnNodusCreateIdentity = $("btnNodusCreateIdentity");
    const nodusCliLight = $("nodusCliLight");
    const nodusCliValue = $("nodusCliValue");
    const nodusIdentityLight = $("nodusIdentityLight");
    const nodusIdentityValue = $("nodusIdentityValue");
    const nodusSeedsLight = $("nodusSeedsLight");
    const nodusSeedsValue = $("nodusSeedsValue");
    const nodusPublicUrlLight = $("nodusPublicUrlLight");
    const nodusPublicUrlValue = $("nodusPublicUrlValue");
    const nodusWorkerLight = $("nodusWorkerLight");
    const nodusWorkerValue = $("nodusWorkerValue");

    // --- System Backups ---
    const systemBackupPill = $("systemBackupPill");
    const systemBackupStoragePill = $("systemBackupStoragePill");
    const systemBackupNextPill = $("systemBackupNextPill");
    const systemBackupLastPill = $("systemBackupLastPill");
    const btnSystemBackupNow = $("btnSystemBackupNow");
    const btnSystemBackupReload = $("btnSystemBackupReload");
    const btnSystemBackupPrune = $("btnSystemBackupPrune");
    const systemBackupSetsTbody = $("systemBackupSetsTbody");
    const systemBackupListTbody = $("systemBackupListTbody");

    let gDnaConnectIdentity = null;

    const ALLOWED_THEMES = new Set(["dark", "bright", "cpunk_orange", "win_classic"]);
    const ALLOWED_ROT_MODES = new Set(["manual", "daily", "size_mb", "daily_or_size_mb"]);
    let gStorageRoots = null; // populated from GET /api/v4/admin/settings
    let gTieringCandidates = []; // populated from GET /api/v4/admin/settings
    let gSnapshotsLast = null;
    let gSnapshotPoolCandidates = [];

    function serverDataRootOrFallback() {
        const dr = gStorageRoots && typeof gStorageRoots.data_root === "string" ? gStorageRoots.data_root.trim() : "";
        return dr || "/srv/pqnas/data";
    }
    function escapeHtml(s) {
        // Security: escape HTML text with one regex/callback instead of chained replaceAll.
        return String(s ?? "").replace(/[&<>"\']/g, (c) => ({
            "&": "&amp;",
            "<": "&lt;",
            ">": "&gt;",
            '"': "&quot;",
            "'": "&#39;",
        }[c]));
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
            "Server ID": "admin.dna.pqnas_id",
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
        if (s === "needs attention") return tr("admin.nodus.status.needs_attention", null, "needs attention");
        if (s === "creating identity…" || s === "creating identity...") return tr("admin.nodus.status.creating_identity", null, "creating identity…");
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

    function snapshotPathBasename(path) {
        const s = String(path || "").replace(/\/+$/, "");
        const parts = s.split("/").filter(Boolean);
        return parts.length ? parts[parts.length - 1] : "volume";
    }

    function safeSnapshotVolumeName(raw) {
        let s = String(raw || "").trim();
        if (!s) s = "volume";
        s = s.replace(/[^A-Za-z0-9_.-]+/g, "_").replace(/^_+|_+$/g, "");
        if (!s) s = "volume";
        return s.length > 48 ? s.slice(0, 48) : s;
    }

    function snapshotRootForSource(source, name) {
        const src = String(source || "").replace(/\/+$/, "");
        const nm = safeSnapshotVolumeName(name || snapshotPathBasename(src));

        if (src === serverDataRootOrFallback()) {
            return "/srv/pqnas/.snapshots/data";
        }

        return `${src}/.snapshots`;
    }

    function snapshotVolumeSource(v) {
        return String(v?.source_subvolume || "").replace(/\/+$/, "");
    }

    function snapshotCandidateSource(c) {
        return String(c?.source_subvolume || c?.mount_path || c?.mount || c?.path || "").replace(/\/+$/, "");
    }

    function isSnapshotSourceProtected(source) {
        const src = String(source || "").replace(/\/+$/, "");
        const vols = Array.isArray(gSnapshotsLast?.volumes) ? gSnapshotsLast.volumes : [];
        return vols.some(v => snapshotVolumeSource(v) === src);
    }

    function normalizeSnapshotPoolCandidate(raw) {
        const c = raw && typeof raw === "object" ? raw : {};
        const mount = String(
            c.mount_path ||
            c.mount ||
            c.mountpoint ||
            c.path ||
            c.root ||
            c.source_subvolume ||
            ""
        ).trim().replace(/\/+$/, "");

        if (!mount) return null;

        // Do not offer the whole runtime root as a snapshot volume. The default protected
        // data volume is /srv/pqnas/data; additional pools should normally be under
        // /srv/pqnas/pools/<pool_id>.
        if (mount === "/srv/pqnas") return null;

        const status = c.status && typeof c.status === "object" ? c.status : {};
        const fs = String(
            c.fs_type ||
            c.fstype ||
            c.filesystem ||
            c.fs ||
            status.fs_type ||
            status.fstype ||
            ""
        ).trim().toLowerCase();

        const mounted = (typeof status.mounted === "boolean") ? status.mounted : true;
        const eligible = mounted && (!fs || fs.includes("btrfs"));

        const nameRaw =
            c.pool_id ||
            c.id ||
            c.name ||
            c.display_name ||
            snapshotPathBasename(mount);

        const name = safeSnapshotVolumeName(nameRaw);
        const display = String(c.display_name || c.name || name).trim() || name;

        return {
            name,
            display_name: display,
            source_subvolume: mount,
            snap_root: snapshotRootForSource(mount, name),
            eligible,
            reason: !mounted ? "not mounted" : (fs && !fs.includes("btrfs") ? `not btrfs (${fs})` : "")
        };
    }

    function renderSnapshotPoolOptions() {
        if (!snapPoolSelect) return;

        snapPoolSelect.innerHTML = "";

        const usable = (Array.isArray(gSnapshotPoolCandidates) ? gSnapshotPoolCandidates : [])
            .filter(c => c && c.eligible)
            .filter(c => !isSnapshotSourceProtected(c.source_subvolume));

        if (!usable.length) {
            const opt = document.createElement("option");
            opt.value = "";
            opt.textContent = tr("admin.snap.no_unprotected_pools", null, "No unprotected Btrfs pools found");
            snapPoolSelect.appendChild(opt);
            if (btnSnapAddPool) btnSnapAddPool.disabled = true;
            return;
        }

        const ph = document.createElement("option");
        ph.value = "";
        ph.textContent = tr("admin.snap.select_pool", null, "Select pool…");
        snapPoolSelect.appendChild(ph);

        for (const c of usable) {
            const opt = document.createElement("option");
            opt.value = c.source_subvolume;
            opt.textContent = `${c.display_name || c.name} • ${c.source_subvolume}`;
            opt.dataset.name = c.name;
            opt.dataset.snapRoot = c.snap_root;
            snapPoolSelect.appendChild(opt);
        }

        if (btnSnapAddPool) btnSnapAddPool.disabled = false;
    }

    async function loadSnapshotPoolCandidates() {
        const candidates = [];

        // Always include the main data root as a candidate; it is normally already protected.
        candidates.push({
            name: "data",
            display_name: "Data volume",
            source_subvolume: serverDataRootOrFallback(),
            snap_root: "/srv/pqnas/.snapshots/data",
            eligible: true,
            reason: ""
        });

        try {
            const j = await fetchJsonOrThrow("/api/v4/storage/pools", { cache: "no-store" });
            const rawPools =
                Array.isArray(j?.pools) ? j.pools :
                Array.isArray(j?.storage_pools) ? j.storage_pools :
                Array.isArray(j?.items) ? j.items :
                Array.isArray(j) ? j :
                [];

            for (const raw of rawPools) {
                const c = normalizeSnapshotPoolCandidate(raw);
                if (!c) continue;

                const dup = candidates.some(x => snapshotCandidateSource(x) === snapshotCandidateSource(c));
                if (!dup) candidates.push(c);
            }
        } catch (e) {
            console.warn("snapshot pool candidate load failed", e);
        }

        gSnapshotPoolCandidates = candidates;
        renderSnapshotPoolOptions();
    }

    function addSelectedPoolToSnapshots() {
        const src = String(snapPoolSelect?.value || "").trim().replace(/\/+$/, "");
        if (!src) {
            showToast("warn", tr("admin.snap.no_pool_selected", null, "No pool selected"), tr("admin.snap.select_pool_first", null, "Select a mounted Btrfs pool first."));
            return;
        }

        const c = (Array.isArray(gSnapshotPoolCandidates) ? gSnapshotPoolCandidates : [])
            .find(x => snapshotCandidateSource(x) === src);

        if (!c) {
            showToast("fail", tr("admin.snap.pool_not_found", null, "Pool not found"), src);
            return;
        }

        if (!c.eligible) {
            showToast("fail", tr("admin.snap.pool_not_eligible", null, "Pool not eligible"), c.reason || src);
            return;
        }

        const cur = currentSnapshotsFromUi();
        const volumes = Array.isArray(cur.volumes) ? cur.volumes.map(v => ({ ...v })) : [];

        if (volumes.some(v => snapshotVolumeSource(v) === src)) {
            showToast("warn", tr("admin.snap.already_protected", null, "Already protected"), src);
            return;
        }

        volumes.push({
            name: safeSnapshotVolumeName(c.name || snapshotPathBasename(src)),
            source_subvolume: src,
            snap_root: c.snap_root || snapshotRootForSource(src, c.name)
        });

        const next = { ...cur, volumes };
        gSnapshotsLast = next;
        applySnapshotsToUi(next);

        showToast("ok", tr("admin.snap.pool_added", null, "Pool added"), tr("admin.snap.pool_added_msg", null, "Click Save snapshots to persist this change."));
    }

    function removeSnapshotVolumeAt(index) {
        const cur = currentSnapshotsFromUi();
        const volumes = Array.isArray(cur.volumes) ? cur.volumes.map(v => ({ ...v })) : [];

        if (index <= 0) {
            showToast("warn", tr("admin.snap.default_data_kept", null, "Default data volume kept"), tr("admin.snap.default_data_kept_msg", null, "The main data volume is kept as the first snapshot volume."));
            return;
        }

        if (index >= volumes.length) return;

        const removed = volumes.splice(index, 1)[0];
        const next = { ...cur, volumes };
        gSnapshotsLast = next;
        applySnapshotsToUi(next);

        showToast("warn", tr("admin.snap.volume_removed", null, "Snapshot volume removed"), tr("admin.snap.volume_removed_msg", { volume: removed?.name || "volume" }, "{volume} removed. Click Save snapshots to persist."));
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
            const root = String(v.snap_root || "");

            const vs = (v.schedule && typeof v.schedule === "object") ? v.schedule : {};
            const tpd = perVol ? Number(vs.times_per_day ?? globalTpd) : globalTpd;
            const jit = perVol ? Number(vs.jitter_seconds ?? globalJit) : globalJit;

            const rowEl = document.createElement("tr");
            rowEl.innerHTML = `
          <td class="mono" title="${escapeHtml(name)}">${escapeHtml(name)}</td>
          <td class="mono" title="${escapeHtml(src)}">${escapeHtml(src)}</td>
          <td class="mono" title="${escapeHtml(root)}">${escapeHtml(root || "—")}</td>
          <td>
            <select class="pq-select snapVolTpd" data-i="${i}" style="width:54px; min-width:0; max-width:54px; box-sizing:border-box; padding-left:6px; padding-right:4px;" ${perVol ? "" : "disabled"}>
              ${tpdOptionsHtml(Math.min(24, Math.max(1, tpd || 6)))}
            </select>
          </td>
          <td>
            <input class="pq-input mono snapVolJit"
                   style="width:54px; min-width:0; max-width:54px; box-sizing:border-box; padding-left:6px; padding-right:4px;"
                   type="number" min="0" max="3600"
                   data-i="${i}"
                   value="${String(Math.min(3600, Math.max(0, jit || 120)))}"
                   ${perVol ? "" : "disabled"} />
          </td>
          <td>
            <button class="pq-btn snapVolRemoveBtn" type="button" title="${escapeHtml(tr("admin.snap.remove", null, "Remove"))}" aria-label="${escapeHtml(tr("admin.snap.remove", null, "Remove"))}" style="width:32px; min-width:0; box-sizing:border-box; padding-left:7px; padding-right:7px;" data-i="${i}" ${i === 0 ? "disabled" : ""}>×</button>
          </td>
        `;

            rowEl.querySelector(".snapVolRemoveBtn")?.addEventListener("click", (ev) => {
                ev.preventDefault();
                const idx = Number(ev.currentTarget?.getAttribute("data-i") || "-1");
                removeSnapshotVolumeAt(idx);
            });

            snapVolTbody.appendChild(rowEl);
        }

        renderSnapshotPoolOptions();
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
        const allowed = new Set(["en", "fi", "zh", "sv", "uk", "de", "et", "pl", "es", "fr", "it", "tr"]);
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
        if (l === "fi") return tr("admin.language.finnish", null, "Suomi");
        if (l === "zh") return tr("admin.language.chinese_simplified", null, "简体中文");
        if (l === "sv") return tr("admin.language.swedish", null, "Svenska");
        if (l === "uk") return tr("admin.language.ukrainian", null, "Українська");
        if (l === "de") return tr("admin.language.german", null, "Deutsch");
        if (l === "et") return tr("admin.language.estonian", null, "Eesti");
        if (l === "pl") return tr("admin.language.polish", null, "Polski");
        if (l === "es") return tr("admin.language.spanish", null, "Español");
        if (l === "fr") return tr("admin.language.french", null, "Français");
        if (l === "it") return tr("admin.language.italian", null, "Italiano");
        if (l === "tr") return tr("admin.language.turkish", null, "Türkçe");
        return tr("admin.language.english", null, "English");
    }

    function updateLanguagePill(lang) {
        const l = normalizeLanguage(lang);
        if (languageSelect) languageSelect.value = l;

        if (languagePicker) {
            for (const btn of languagePicker.querySelectorAll("[data-language]")) {
                const on = btn.getAttribute("data-language") === l;
                btn.classList.toggle("is-active", on);
                btn.setAttribute("aria-pressed", on ? "true" : "false");
            }
        }

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
    function tAdminPassword(key, fallback, vars) {
        const api = window.PQNAS_I18N;
        if (api && typeof api.t === "function") {
            return api.t(key, vars || null, fallback);
        }

        let out = String(fallback ?? key);
        if (vars && typeof vars === "object") {
            for (const [k, v] of Object.entries(vars)) {
                out = out.replaceAll(`{${k}}`, String(v));
            }
        }
        return out;
    }

    async function loadAdminPasswordAuthConfig() {
        if (!adminPasswordChangeCard) return;

        try {
            const r = await fetch("/api/auth/config", {
                credentials: "include",
                cache: "no-store"
            });

            const j = await r.json().catch(() => null);

            if (!r.ok || !j || j.ok === false) {
                throw new Error((j && (j.message || j.error)) ? (j.message || j.error) : `HTTP ${r.status}`);
            }

            const enabled = !!j.password_enabled;

            adminPasswordChangeCard.classList.toggle("hidden", !enabled);

            if (adminPasswordAuthPill) {
                adminPasswordAuthPill.className = "pill " + (enabled ? "ok" : "warn");
                adminPasswordAuthPill.innerHTML =
                    `<span class="k">${escapeHtml(tAdminPassword("admin.password.auth_label", "Password auth:"))}</span> <span class="v">${escapeHtml(enabled ? tAdminPassword("admin.password.enabled", "enabled") : tAdminPassword("admin.password.disabled", "disabled"))}</span>`;
            }

            if (enabled && adminPasswordLogin && !adminPasswordLogin.value) {
                try {
                    adminPasswordLogin.value = localStorage.getItem("pqnas_password_login") || "";
                } catch (_) {}
            }
        } catch (e) {
            adminPasswordChangeCard.classList.add("hidden");

            if (adminPasswordAuthPill) {
                adminPasswordAuthPill.className = "pill fail";
                adminPasswordAuthPill.innerHTML =
                    `<span class="k">${escapeHtml(tAdminPassword("admin.password.auth_label", "Password auth:"))}</span> <span class="v">${escapeHtml(String(e && e.message ? e.message : e))}</span>`;
            }
        }
    }

    async function changeAdminOwnPassword() {
        const login = String(adminPasswordLogin?.value || "").trim();
        const current_password = String(adminPasswordCurrent?.value || "");
        const new_password = String(adminPasswordNew?.value || "");
        const confirm_password = String(adminPasswordConfirm?.value || "");

        if (!login) {
            throw new Error(tAdminPassword("settings.password.login_required", "Enter your login/email."));
        }

        if (!current_password) {
            throw new Error(tAdminPassword("settings.password.current_required", "Enter your current password."));
        }

        if (new_password.length < 12) {
            throw new Error(tAdminPassword("settings.password.too_short", "New password must be at least 12 characters."));
        }

        if (new_password !== confirm_password) {
            throw new Error(tAdminPassword("settings.password.mismatch", "New password and confirmation do not match."));
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
        } catch (_) {}

        if (adminPasswordCurrent) adminPasswordCurrent.value = "";
        if (adminPasswordNew) adminPasswordNew.value = "";
        if (adminPasswordConfirm) adminPasswordConfirm.value = "";

        return j;
    }

    async function loadAdminPasswordUserCreateAuthConfig() {
        if (!adminPasswordUserCreateCard) return;

        try {
            const r = await fetch("/api/auth/config", {
                credentials: "include",
                cache: "no-store"
            });

            const j = await r.json().catch(() => null);

            if (!r.ok || !j || j.ok === false) {
                throw new Error((j && (j.message || j.error)) ? (j.message || j.error) : `HTTP ${r.status}`);
            }

            const enabled = !!j.password_enabled;

            adminPasswordUserCreateCard.classList.toggle("hidden", !enabled);

            if (adminPasswordUserCreatePill) {
                adminPasswordUserCreatePill.className = "pill " + (enabled ? "ok" : "warn");
                adminPasswordUserCreatePill.innerHTML =
                    `<span class="k">${escapeHtml(tAdminPassword("admin.password.auth_label", "Password auth:"))}</span> <span class="v">${escapeHtml(enabled ? tAdminPassword("admin.password.enabled", "enabled") : tAdminPassword("admin.password.disabled", "disabled"))}</span>`;
            }
        } catch (e) {
            adminPasswordUserCreateCard.classList.add("hidden");

            if (adminPasswordUserCreatePill) {
                adminPasswordUserCreatePill.className = "pill fail";
                adminPasswordUserCreatePill.innerHTML =
                    `<span class="k">${escapeHtml(tAdminPassword("admin.password.auth_label", "Password auth:"))}</span> <span class="v">${escapeHtml(String(e && e.message ? e.message : e))}</span>`;
            }
        }
    }

    function clearPasswordUserCreateResult() {
        adminCreatePasswordUserResult?.classList.remove("show");
        if (adminCreatePasswordUserRecovery) adminCreatePasswordUserRecovery.value = "";
        if (adminCreatePasswordUserResultLogin) adminCreatePasswordUserResultLogin.textContent = "";
        if (adminCreatePasswordUserResultFingerprint) adminCreatePasswordUserResultFingerprint.textContent = "";
        if (adminCreatePasswordUserResultStatus) adminCreatePasswordUserResultStatus.textContent = "";
        if (adminCreatePasswordUserResultQuota) adminCreatePasswordUserResultQuota.textContent = "";
        if (adminCreatePasswordUserCopied) adminCreatePasswordUserCopied.checked = false;
    }

    async function createPasswordUserFromAdminSettings() {
        const name = String(adminCreatePasswordUserName?.value || "").trim();
        const login = String(adminCreatePasswordUserLogin?.value || "").trim();
        const role = String(adminCreatePasswordUserRole?.value || "user").trim() || "user";
        const status = String(adminCreatePasswordUserStatus?.value || "disabled").trim() || "disabled";
        const quotaRaw = String(adminCreatePasswordUserQuota?.value || "").trim();

        if (!login) throw new Error(tAdminPassword("admin.password_user_create.login_required", "Login/email is required."));
        if (role !== "user" && role !== "admin") throw new Error(tAdminPassword("admin.password_user_create.invalid_role", "Invalid role."));
        if (status !== "enabled" && status !== "disabled" && status !== "pending") throw new Error(tAdminPassword("admin.password_user_create.invalid_status", "Invalid status."));

        const body = {
            name,
            login,
            role,
            status,
            setup_language: currentLanguageName()
        };

        if (quotaRaw !== "") {
            const quota = Number(quotaRaw);
            if (!Number.isSafeInteger(quota) || quota < 0) {
                throw new Error(tAdminPassword("admin.password_user_create.invalid_quota_bytes", "Quota bytes must be a non-negative whole number."));
            }
            body.quota_bytes = quota;
        }

        const r = await fetch("/api/admin/users/password-create", {
            method: "POST",
            credentials: "include",
            cache: "no-store",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify(body)
        });

        const j = await r.json().catch(() => null);

        if (!r.ok || !j || j.ok === false) {
            const msg = j && (j.message || j.error) ? (j.message || j.error) : `HTTP ${r.status}`;
            throw new Error(msg);
        }

        return j;
    }

    function passwordUserCreateStatusText(status) {
        const s = String(status || "");
        if (s === "enabled") return tAdminPassword("admin.password_user_create.status_result_enabled", "enabled");
        if (s === "disabled") return tAdminPassword("admin.password_user_create.status_result_disabled", "disabled");
        if (s === "pending") return tAdminPassword("admin.password_user_create.status_result_pending", "pending");
        return s;
    }

    function showPasswordUserCreateResult(j) {
        if (!adminCreatePasswordUserResult) return;

        adminCreatePasswordUserResult.classList.add("show");

        if (adminCreatePasswordUserRecovery) {
            adminCreatePasswordUserRecovery.value = j.setup_url || j.setup_path || "";
            adminCreatePasswordUserRecovery.focus();
            adminCreatePasswordUserRecovery.select();
        }

        if (adminCreatePasswordUserResultLogin) {
            adminCreatePasswordUserResultLogin.textContent = j.login || "";
        }

        if (adminCreatePasswordUserResultFingerprint) {
            adminCreatePasswordUserResultFingerprint.textContent = j.fingerprint || "created when user completes setup";
        }

        if (adminCreatePasswordUserResultStatus) {
            adminCreatePasswordUserResultStatus.textContent = passwordUserCreateStatusText(j.status);
        }

        if (adminCreatePasswordUserResultQuota) {
            adminCreatePasswordUserResultQuota.textContent = String(j.quota_bytes ?? 0);
        }

                if (adminCreatePasswordUserCopied) {
            adminCreatePasswordUserCopied.checked = false;
        }
    }

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
        renderSnapshotPoolOptions();
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
        if (!ws.length) return tr("admin.uploads.none", null, "None");

        return ws.map(w => {
            if (w === "usb_blocked") return tr("admin.uploads.warn_usb_blocked", null, "USB blocked");
            if (w === "removable_blocked") return tr("admin.uploads.warn_removable_blocked", null, "Removable blocked");
            if (w === "low_free_space") return tr("admin.uploads.warn_low_free_space", null, "Low free space");
            if (w === "not_mounted") return tr("admin.uploads.warn_not_mounted", null, "Not mounted");
            if (w === "not_writable") return tr("admin.uploads.warn_not_writable", null, "Not writable");
            if (w === "statvfs_failed") return tr("admin.uploads.warn_space_unknown", null, "Space unknown");
            if (w === "missing_mount") return tr("admin.uploads.warn_missing_mount", null, "Missing mount");
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
                opt.textContent = tr("admin.uploads.pool_free_label", { pool: poolId, mount, free: freeTxt }, `${poolId} — ${mount} — ${freeTxt} free`);
            } else {
                opt.textContent = tr("admin.uploads.pool_blocked_label", { pool: poolId, mount, warnings: warnTxt }, `${poolId} — ${mount} — BLOCKED (${warnTxt})`);
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
            setSimplePill(tieringMountPill, "info", tr("admin.uploads.mount", null, "Mount"), "—");
            setSimplePill(tieringSpacePill, "info", tr("admin.uploads.space", null, "Space"), "—");
            setSimplePill(tieringEligibilityPill, "warn", tr("admin.uploads.eligibility", null, "Eligibility"), tr("admin.uploads.no_selection", null, "No selection"));
            setSimplePill(tieringWarnPill, "warn", tr("admin.uploads.warnings", null, "Warnings"), "—");
            return;
        }

        const mount = String(c.mount_path || "—");
        const freeTxt = fmtBytesGiB(c.free_bytes);
        const totalTxt = fmtBytesGiB(c.total_bytes);
        const eligible = !!c.eligible;
        const warnTxt = tieringWarningsText(c);

        setSimplePill(tieringMountPill, "info", tr("admin.uploads.mount", null, "Mount"), mount);
        setSimplePill(tieringSpacePill, "info", tr("admin.uploads.space", null, "Space"), tr("admin.uploads.free_total", { free: freeTxt, total: totalTxt }, `${freeTxt} free / ${totalTxt} total`));
        setSimplePill(
            tieringEligibilityPill,
            eligible ? "ok" : "warn",
            tr("admin.uploads.eligibility", null, "Eligibility"),
            eligible ? tr("admin.uploads.eligible", null, "Eligible") : tr("admin.uploads.blocked", null, "Blocked")
        );
        setSimplePill(
            tieringWarnPill,
            warnTxt === tr("admin.uploads.none", null, "None") ? "info" : "warn",
            tr("admin.uploads.warnings", null, "Warnings"),
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
            let txt = tr("admin.uploads.disabled", null, "Disabled");
            let kind = "warn";

            if (enabled) {
                const c = tieringCandidateByPoolId(landingPoolId);
                if (landingPoolId && c && c.eligible) {
                    txt = tr("admin.uploads.status_enabled_pool", { pool: landingPoolId }, `Enabled • ${landingPoolId}`);
                    kind = "ok";
                } else if (landingPoolId) {
                    txt = tr("admin.uploads.status_enabled_pool_blocked", { pool: landingPoolId }, `Enabled • ${landingPoolId} (blocked)`);
                    kind = "warn";
                } else {
                    txt = tr("admin.uploads.status_enabled_no_pool", null, "Enabled • no pool");
                    kind = "warn";
                }
            }

            tieringPill.className = "pill " + kind;
            tieringPill.innerHTML = `<span class="k">${escapeHtml(tr("admin.uploads.tiering_label", null, "Tiering:"))}</span> <span class="v">${escapeHtml(txt)}</span>`;
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
                `<span class="k">${escapeHtml(tr("admin.dna.pqnas_id", null, "Server ID:"))}</span><span class="v">${escapeHtml(txt)}</span>`;
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
    // OPAQUE backend status
    // ---------------------------
    async function apiOpaqueStatus() {
        return await fetchJsonOrThrow("/api/admin/auth/opaque/status", {
            credentials: "include",
            cache: "no-store"
        });
    }
// ---------------------------
    // System Backups
    // ---------------------------
    async function apiSystemBackupStatus() {
        return await fetchJsonOrThrow("/api/v4/admin/system-backups/status", {
            cache: "no-store"
        });
    }

    async function apiSystemBackupList() {
        return await fetchJsonOrThrow("/api/v4/admin/system-backups/list?limit=100", {
            cache: "no-store"
        });
    }

    async function apiSystemBackupRunNow() {
        return await fetchJsonOrThrow("/api/v4/admin/system-backups/run", {
            method: "POST",
            cache: "no-store",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ tier: "manual", reason: "admin-ui" })
        });
    }

    async function apiSystemBackupPrune() {
        return await fetchJsonOrThrow("/api/v4/admin/system-backups/prune", {
            method: "POST",
            cache: "no-store",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({})
        });
    }

    function fmtEpochLocal(epoch) {
        const n = Number(epoch || 0);
        if (!Number.isFinite(n) || n <= 0) return "—";

        try {
            return new Date(n * 1000).toLocaleString();
        } catch (_) {
            return String(n);
        }
    }

    function backupSetLabel(id) {
        const s = String(id || "");
        if (s === "core") return tr("admin.backups.core", null, "Core System");
        if (s === "users_auth") return tr("admin.backups.users_auth", null, "Users & Auth");
        if (s === "circle_stack") return tr("admin.backups.circle_stack", null, "Circle Stack");
        return s || "—";
    }

    function setSystemBackupPill(kind, text) {
        if (!systemBackupPill) return;
        systemBackupPill.className = "pill " + (kind || "");
        systemBackupPill.innerHTML = `<span class="k">${escapeHtml(adminLabel("status"))}:</span> <span class="v">${escapeHtml(text || "—")}</span>`;
    }

    function renderSystemBackupSets(j) {
        if (!systemBackupSetsTbody) return;

        systemBackupSetsTbody.innerHTML = "";

        const sets = j && j.sets && typeof j.sets === "object" ? j.sets : {};
        const keys = Object.keys(sets).sort();

        for (const key of keys) {
            const set = sets[key] || {};
            const sources = Array.isArray(set.sources) ? set.sources : [];
            const present = Number(set.present || 0);
            const missing = Number(set.missing || 0);
            const size = sources.reduce((acc, src) => acc + Number(src?.size_bytes || 0), 0);

            const sourceText = sources
                .map(src => {
                    const label = String(src?.label || src?.path || "—");
                    const state = src?.present ? "ok" : "missing";
                    return `${label} (${state})`;
                })
                .join(" • ");

            const trEl = document.createElement("tr");
            trEl.innerHTML = `
                <td>${escapeHtml(backupSetLabel(set.id || key))}</td>
                <td class="mono">${escapeHtml(String(present))}</td>
                <td class="mono">${escapeHtml(String(missing))}</td>
                <td class="mono">${escapeHtml(fmtBytes(size))}</td>
                <td class="mono" title="${escapeHtml(sourceText)}">${escapeHtml(sourceText || "—")}</td>
            `;
            systemBackupSetsTbody.appendChild(trEl);
        }

        if (!keys.length) {
            const trEl = document.createElement("tr");
            trEl.innerHTML = `<td colspan="5">—</td>`;
            systemBackupSetsTbody.appendChild(trEl);
        }
    }

    function renderSystemBackupList(j) {
        if (!systemBackupListTbody) return;

        systemBackupListTbody.innerHTML = "";

        const backups = Array.isArray(j?.backups) ? j.backups : [];

        for (const b of backups) {
            const trEl = document.createElement("tr");
            const backupId = String(b?.backup_id || "—");
            const tier = String(b?.tier || "—");
            const bytes = Number(b?.bytes_written || 0);
            const files = Number(b?.files_written || 0);
            const skipped = Number(b?.files_skipped || 0);
            const created = fmtEpochLocal(b?.created_epoch);

            trEl.innerHTML = `
                <td class="mono" title="${escapeHtml(backupId)}">${escapeHtml(backupId)}</td>
                <td>${escapeHtml(tier)}</td>
                <td class="mono">${escapeHtml(fmtBytes(bytes))}</td>
                <td class="mono">${escapeHtml(`${files}${skipped ? " +" + skipped + " skipped" : ""}`)}</td>
                <td class="mono">${escapeHtml(created)}</td>
            `;
            systemBackupListTbody.appendChild(trEl);
        }

        if (!backups.length) {
            const trEl = document.createElement("tr");
            trEl.innerHTML = `<td colspan="5">${escapeHtml(tr("admin.backups.no_backups", null, "No backups yet"))}</td>`;
            systemBackupListTbody.appendChild(trEl);
        }
    }

    function renderSystemBackupStatus(j) {
        if (!j || j.ok !== true) {
            setSystemBackupPill("fail", "error");
            return;
        }

        const scheduler = j.scheduler && typeof j.scheduler === "object" ? j.scheduler : {};
        const sets = j.sets && typeof j.sets === "object" ? j.sets : {};

        let missing = 0;
        for (const key of Object.keys(sets)) {
            missing += Number(sets[key]?.missing || 0);
        }

        const running = !!scheduler.running;
        const statusText = missing > 0
            ? tr("admin.backups.missing_optional", { count: missing }, `Ready • ${missing} missing optional source(s)`)
            : (running
                ? tr("admin.backups.ready_scheduler", null, "Ready • scheduler running")
                : tr("admin.backups.ready", null, "Ready"));

        setSystemBackupPill(missing > 0 ? "warn" : "ok", statusText);
        setSimplePill(systemBackupStoragePill, "info", "Storage used", fmtBytes(j.storage_used_bytes || 0));
        setSimplePill(systemBackupNextPill, running ? "info" : "warn", "Next run", fmtEpochLocal(scheduler.next_run_epoch));
        setSimplePill(
            systemBackupLastPill,
            scheduler.last_error ? "fail" : "info",
            "Last run",
            scheduler.last_error
                ? String(scheduler.last_error)
                : `${fmtEpochLocal(scheduler.last_run_epoch)}${scheduler.last_tier ? " • " + scheduler.last_tier : ""}`
        );

        renderSystemBackupSets(j);
    }

    async function refreshSystemBackups() {
        setSystemBackupPill("warn", "loading…");

        try {
            const status = await apiSystemBackupStatus();
            renderSystemBackupStatus(status);

            const list = await apiSystemBackupList();
            renderSystemBackupList(list);
        } catch (e) {
            console.error(e);
            setSystemBackupPill("fail", "error");
            setSimplePill(systemBackupStoragePill, "warn", "Storage used", "—");
            setSimplePill(systemBackupNextPill, "warn", "Next run", "—");
            setSimplePill(systemBackupLastPill, "warn", "Last run", "—");
            showToast("fail", tr("admin.backups.status_failed", null, "System backup status failed"), String(e.message || e));
        }
    }

    // ---------------------------
    // Nodus federation status
    // ---------------------------
    async function apiNodusStatus() {
        return await fetchJsonOrThrow("/api/v4/admin/nodus/status", {
            cache: "no-store"
        });
    }

    async function apiNodusCreateIdentity() {
        return await fetchJsonOrThrow("/api/v4/admin/nodus/identity/init", {
            method: "POST",
            cache: "no-store",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({})
        });
    }

    function setLight(el, kind) {
        if (!el) return;
        el.className = "lightDot " + (kind || "warn");
    }

    function setNodusStatusPill(kind, text) {
        if (!nodusStatusPill) return;
        nodusStatusPill.className = "pill " + (kind || "");
        nodusStatusPill.innerHTML = `<span class="k">${escapeHtml(adminLabel("status"))}:</span> <span class="v">${escapeHtml(adminStatusText(text || "—"))}</span>`;
    }

    function renderNodusStatus(j) {
        if (!j || j.ok !== true) {
            setNodusStatusPill("fail", "error");
            return;
        }

        const cli = (j.cli && typeof j.cli === "object") ? j.cli : {};
        const identity = (j.identity && typeof j.identity === "object") ? j.identity : {};
        const seeds = (j.seeds_summary && typeof j.seeds_summary === "object") ? j.seeds_summary : {};
        const worker = (j.worker && typeof j.worker === "object") ? j.worker : {};

        const cliOk = !!cli.installed;
        const idOk = !!identity.exists;
        const totalSeeds = Number(seeds.total || 0);
        const reachableSeeds = Number(seeds.reachable || 0);
        const seedsOk = totalSeeds > 0 && reachableSeeds > 0;
        const seedsAllOk = totalSeeds > 0 && reachableSeeds === totalSeeds;
        const publicUrl = String(j.public_base_url || "").trim();
        const publicOk = !!publicUrl;
        const workerEnabled = !!worker.enabled;

        setLight(nodusCliLight, cliOk ? "ok" : "fail");
        if (nodusCliValue) {
            nodusCliValue.textContent = cliOk
                ? `${cli.path || "/usr/local/bin/nodus-cli"}${cli.version ? " • " + cli.version : ""}`
                : tr("admin.nodus.cli_missing", { path: cli.path || "/usr/local/bin/nodus-cli" }, `Missing: ${cli.path || "/usr/local/bin/nodus-cli"}`);
        }

        setLight(nodusIdentityLight, idOk ? "ok" : "fail");
        if (nodusIdentityValue) {
            nodusIdentityValue.textContent = idOk
                ? tr("admin.nodus.identity_present", { fp: identity.fingerprint_short || "present", dir: identity.dir || "" }, `${identity.fingerprint_short || "present"}… • ${identity.dir || ""}`)
                : tr("admin.nodus.identity_missing", { dir: identity.dir || "identity dir" }, `Missing in ${identity.dir || "identity dir"}`);
        }

        setLight(nodusSeedsLight, seedsAllOk ? "ok" : (seedsOk ? "warn" : "fail"));
        if (nodusSeedsValue) {
            nodusSeedsValue.textContent = tr("admin.nodus.seeds_reachable", { reachable: reachableSeeds, total: totalSeeds }, `${reachableSeeds} / ${totalSeeds} reachable`);
        }

        setLight(nodusPublicUrlLight, publicOk ? "ok" : "warn");
        if (nodusPublicUrlValue) {
            nodusPublicUrlValue.textContent = publicOk ? publicUrl : tr("admin.nodus.not_configured", null, "Not configured");
        }

        setLight(nodusWorkerLight, workerEnabled ? "ok" : "warn");
        if (nodusWorkerValue) {
            nodusWorkerValue.textContent = workerEnabled
                ? tr("admin.nodus.enabled", null, "Enabled")
                : tr("admin.nodus.worker_disabled", { env: "PQNAS_CIRCLE_FEDERATION_WORKER" }, "Disabled • set PQNAS_CIRCLE_FEDERATION_WORKER=1");
        }

        const overallOk = cliOk && idOk && seedsOk && publicOk;
        setNodusStatusPill(
            overallOk ? "ok" : "warn",
            overallOk ? "ready" : "needs attention"
        );

        if (btnNodusCreateIdentity) btnNodusCreateIdentity.disabled = idOk || !cliOk;
    }

    async function refreshNodusStatus() {
        setNodusStatusPill("warn", "loading…");

        try {
            const j = await apiNodusStatus();
            renderNodusStatus(j);
        } catch (e) {
            console.error(e);
            setNodusStatusPill("fail", "error");
            setLight(nodusCliLight, "fail");
            setLight(nodusIdentityLight, "fail");
            setLight(nodusSeedsLight, "fail");
            showToast("fail", tr("admin.nodus.status_failed", null, "Nodus status failed"), String(e.message || e));
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
            await loadSnapshotPoolCandidates();

            // active audit file info
            updateActiveSizePill(j);

            // upload limits
            applyUploadLimitsToUi(j);

            // tiering
            applyTieringToUi(j);

            // DNA Connect alerts
            applyDnaAlertsToUi(j);
            applyDnaIdentityToUi(j);

            // Nodus federation
            await refreshNodusStatus();

            // OPAQUE backend status

            // System Backups
            await refreshSystemBackups();

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
    // Wire password auth / own password
    // ---------------------------
    btnAdminPasswordChange?.addEventListener("click", async (ev) => {
        ev.preventDefault();

        const oldText = btnAdminPasswordChange.textContent;
        btnAdminPasswordChange.disabled = true;
        btnAdminPasswordChange.textContent = tAdminPassword("settings.password.changing", "Changing…");

        try {
            await changeAdminOwnPassword();
            showToast("ok", tAdminPassword("admin.password.changed_title", "Password changed"), tAdminPassword("admin.password.changed_body", "Your admin password was changed."));
        } catch (e) {
            console.error(e);
            showToast("fail", tAdminPassword("admin.password.failed_title", "Password change failed"), String(e && e.message ? e.message : e));
        } finally {
            btnAdminPasswordChange.disabled = false;
            btnAdminPasswordChange.textContent = oldText;
        }
    });

    loadAdminPasswordAuthConfig();

    // ---------------------------
    // Wire password user creation
    // ---------------------------
    btnAdminCreatePasswordUser?.addEventListener("click", async (ev) => {
        ev.preventDefault();

        clearPasswordUserCreateResult();

        const oldText = btnAdminCreatePasswordUser.textContent;
        btnAdminCreatePasswordUser.disabled = true;
        btnAdminCreatePasswordUser.textContent = "Creating…";

        try {
            const j = await createPasswordUserFromAdminSettings();
            showPasswordUserCreateResult(j);
            showToast("ok", "Password user created", "Copy the recovery words now. They are shown only once.");
        } catch (e) {
            console.error(e);
            showToast("fail", "User creation failed", String(e && e.message ? e.message : e));
        } finally {
            btnAdminCreatePasswordUser.disabled = false;
            btnAdminCreatePasswordUser.textContent = oldText;
        }
    });

    loadAdminPasswordUserCreateAuthConfig();

    // ---------------------------
    // Wire System Backups
    // ---------------------------
    btnSystemBackupReload?.addEventListener("click", (ev) => {
        ev.preventDefault();
        refreshSystemBackups();
    });

    btnSystemBackupNow?.addEventListener("click", async (ev) => {
        ev.preventDefault();

        const ok = await openAdminConfirmModal({
            title: tr("admin.backups.run_confirm_title", null, "Run system backup now?"),
            subtitle: tr("admin.backups.run_confirm_subtitle", null, "Creates a manual backup of core config, users/auth data, and Circle Stack databases."),
            rows: [
                { label: tr("admin.backups.tier", null, "Tier"), value: tr("admin.backups.manual", null, "manual") },
                { label: tr("admin.backups.included", null, "Included"), value: tr("admin.backups.included_value", null, "Core System, Users & Auth, Circle Stack") },
                { label: tr("admin.backups.excluded", null, "Excluded"), value: tr("admin.backups.excluded_value", null, "User files, media, Drop Zones, Echo Stack, gallery data, caches") }
            ],
            note: tr("admin.backups.run_note", null, "This does not backup user files. User data must be protected separately with snapshots, RAID, and/or external backups."),
            confirmText: tr("admin.backups.backup_now", null, "Backup now"),
            cancelText: tr("admin.common.cancel", null, "Cancel"),
            warn: false
        });

        if (!ok) return;

        btnSystemBackupNow.disabled = true;
        setSystemBackupPill("warn", "running…");

        try {
            const result = await apiSystemBackupRunNow();
            showToast(
                "ok",
                tr("admin.backups.complete", null, "System backup complete"),
                tr("admin.backups.file_count_summary", {
                    count: result.files_written || 0,
                    bytes: fmtBytes(result.bytes_written || 0)
                }, `${result.files_written || 0} file(s) • ${fmtBytes(result.bytes_written || 0)}`)
            );
            await refreshSystemBackups();
        } catch (e) {
            console.error(e);
            showToast("fail", tr("admin.backups.failed", null, "System backup failed"), String(e.message || e));
            await refreshSystemBackups();
        } finally {
            btnSystemBackupNow.disabled = false;
        }
    });

    btnSystemBackupPrune?.addEventListener("click", async (ev) => {
        ev.preventDefault();

        const ok = await openAdminConfirmModal({
            title: tr("admin.backups.prune_confirm_title", null, "Prune old scheduled backups?"),
            subtitle: tr("admin.backups.prune_confirm_subtitle", null, "Removes backups older than the retention policy."),
            rows: [
                { label: tr("admin.backups.tier_quarter_hourly", null, "Quarter-hourly"), value: tr("admin.backups.keep_24h", null, "keep 24 h") },
                { label: tr("admin.backups.tier_hourly", null, "Hourly"), value: tr("admin.backups.keep_7_days", null, "keep 7 days") },
                { label: tr("admin.backups.tier_daily", null, "Daily"), value: tr("admin.backups.keep_30_days", null, "keep 30 days") },
                { label: tr("admin.backups.tier_weekly", null, "Weekly"), value: tr("admin.backups.keep_12_weeks", null, "keep 12 weeks") },
                { label: tr("admin.backups.manual", null, "Manual"), value: tr("admin.backups.manual_kept", null, "kept until admin deletes") }
            ],
            note: tr("admin.backups.prune_note", null, "Manual backups are not removed by automatic retention."),
            confirmText: tr("admin.backups.prune", null, "Prune old backups"),
            cancelText: tr("admin.common.cancel", null, "Cancel"),
            warn: true
        });

        if (!ok) return;

        btnSystemBackupPrune.disabled = true;

        try {
            const result = await apiSystemBackupPrune();
            showToast(
                "ok",
                tr("admin.backups.prune_complete", null, "Backup prune complete"),
                tr("admin.backups.removed_folders", { count: result.dirs_removed || 0 }, `${result.dirs_removed || 0} folder(s) removed`)
            );
            await refreshSystemBackups();
        } catch (e) {
            console.error(e);
            showToast("fail", tr("admin.backups.prune_failed", null, "Backup prune failed"), String(e.message || e));
            await refreshSystemBackups();
        } finally {
            btnSystemBackupPrune.disabled = false;
        }
    });

    // ---------------------------
    // Wire Nodus federation
    // ---------------------------
    btnNodusRefresh?.addEventListener("click", (ev) => {
        ev.preventDefault();
        refreshNodusStatus();
    });

    btnNodusCreateIdentity?.addEventListener("click", async (ev) => {
        ev.preventDefault();

        const ok = await openAdminConfirmModal({
            title: tr("admin.nodus.identity_confirm_title", null, "Generate Nodus identity?"),
            subtitle: tr("admin.nodus.identity_confirm_subtitle", null, "This creates a local NAS federation identity if missing."),
            rows: [
                { label: tr("admin.nodus.identity_confirm_target", null, "Target"), value: "/srv/pqnas/config/nodus/identity", mono: true },
                { label: tr("admin.nodus.identity_confirm_effect", null, "Effect"), value: tr("admin.nodus.identity_confirm_effect_value", null, "This NAS gets a unique federation origin fingerprint.") }
            ],
            note: tr("admin.nodus.identity_confirm_note", null, "Do not replace an existing identity unless you intentionally want this NAS to appear as a different node."),
            confirmText: tr("admin.nodus.identity_confirm_button", null, "Generate identity"),
            cancelText: tr("admin.common.cancel", null, "Cancel"),
            warn: true
        });

        if (!ok) return;

        btnNodusCreateIdentity.disabled = true;
        setNodusStatusPill("warn", "creating identity…");

        try {
            const j = await apiNodusCreateIdentity();
            showToast(
                "ok",
                j.created ? tr("admin.nodus.identity_created", null, "Nodus identity created") : tr("admin.nodus.identity_exists", null, "Nodus identity already exists"),
                j.fingerprint_short ? `${j.fingerprint_short}…` : tr("admin.common.ok", null, "OK")
            );
            await refreshNodusStatus();
        } catch (e) {
            console.error(e);
            showToast("fail", tr("admin.nodus.identity_failed", null, "Nodus identity failed"), String(e.message || e));
            await refreshNodusStatus();
        } finally {
            btnNodusCreateIdentity.disabled = false;
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

    btnSnapAddPool?.addEventListener("click", (ev) => {
        ev.preventDefault();
        addSelectedPoolToSnapshots();
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
            showToast("ok", tr("admin.dna.id_created", null, "Server ID created"), String(j.message || tr("admin.dna.identity_created", null, "DNA identity created")));
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

    languagePicker?.querySelectorAll("[data-language]").forEach((btn) => {
        btn.addEventListener("click", (ev) => {
            ev.preventDefault();
            applyAdminLanguage(btn.getAttribute("data-language") || "en");
        });
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

        if (themePill && themeSelect) {
            setSimplePill(themePill, "info", tr("admin.theme.pill", null, "Theme"), themeSelect.value || "dark");
        }

        // Dynamic admin settings sections build their text in JavaScript.
        // Re-render them after language changes so pills and tables do not keep
        // stale text from the previously selected language.
        refreshAll().catch((e) => {
            console.error("[admin_settings] refresh after language change failed", e);
        });
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


// pqnas-admin-notifications-backend-v1
(() => {
    function $(id) { return document.getElementById(id); }

    const pill = $("notificationsPill");
    if (!pill) return;

    const defaultEmailPill = $("notificationsDefaultEmailPill");
    const deliveryPill = $("notificationsDeliveryPill");

    const infoEmail = $("notifyInfoEmailEnabled");
    const infoTelegram = $("notifyInfoTelegramEnabled");
    const warnEmail = $("notifyWarnEmailEnabled");
    const warnTelegram = $("notifyWarnTelegramEnabled");
    const extraEmails = $("notifyInfoExtraEmails");
    const schedule = $("notifyInfoSchedule");
    const telegramToken = $("notifyTelegramToken");
    const telegramChatId = $("notifyTelegramChatId");
    const smtpHost = $("notifySmtpHost");
    const smtpPort = $("notifySmtpPort");
    const smtpTls = $("notifySmtpTls");
    const smtpUser = $("notifySmtpUser");
    const smtpPassword = $("notifySmtpPassword");
    const smtpFrom = $("notifySmtpFrom");
    const btnSave = $("btnNotificationsSave");
    const btnTestEmail = $("btnNotificationsTestEmail");
    const btnTestTelegram = $("btnNotificationsTestTelegram");

    function tr(key, vars = null, fallback = "") {
        try {
            if (window.PQNAS_I18N && typeof window.PQNAS_I18N.t === "function") {
                return window.PQNAS_I18N.t(key, vars, fallback || key);
            }
        } catch (_) {}
        return fallback || key;
    }

    function esc(s) {
        return String(s ?? "").replace(/[&<>"']/g, c => ({
            "&":"&amp;", "<":"&lt;", ">":"&gt;", '"':"&quot;", "'":"&#39;"
        }[c]));
    }

    function setPill(kind, text) {
        pill.className = "pill " + (kind || "");
        pill.innerHTML =
            `<span class="k">${esc(tr("admin.common.status", null, "Status"))}:</span> <span class="v">${esc(text || "—")}</span>`;
    }

    function setInlinePill(el, kind, labelKey, labelFallback, value) {
        if (!el) return;
        el.className = "pill " + (kind || "");
        el.innerHTML =
            `<span class="k">${esc(tr(labelKey, null, labelFallback))}</span> <span class="v">${esc(value || "—")}</span>`;
    }

    async function apiJson(url, opts = {}) {
        const r = await fetch(url, {
            credentials: "include",
            cache: "no-store",
            ...opts
        });
        const text = await r.text().catch(() => "");
        let data = null;
        try { data = text ? JSON.parse(text) : null; } catch (_) {}

        if (!r.ok || !data || data.ok !== true) {
            const msg = data && (data.message || data.error)
                ? (data.message || data.error)
                : (text.trim() || `HTTP ${r.status}`);
            throw new Error(msg);
        }
        return data;
    }

    function splitEmails(s) {
        return String(s || "")
            .split(/[,\n;]/g)
            .map(x => x.trim())
            .filter(Boolean);
    }

    function currentPayload() {
        const payload = {
            info_email_enabled: !!infoEmail?.checked,
            info_telegram_enabled: !!infoTelegram?.checked,
            warnings_email_enabled: !!warnEmail?.checked,
            warnings_telegram_enabled: !!warnTelegram?.checked,
            weekly_summary_enabled: String(schedule?.value || "weekly") !== "disabled",
            extra_emails: splitEmails(extraEmails?.value || ""),
            telegram_chat_id: String(telegramChatId?.value || "").trim(),
            smtp_host: String(smtpHost?.value || "").trim(),
            smtp_port: Number(String(smtpPort?.value || "587").trim() || 587),
            smtp_tls: String(smtpTls?.value || "starttls").trim(),
            smtp_user: String(smtpUser?.value || "").trim(),
            smtp_from: String(smtpFrom?.value || "").trim()
        };

        const token = String(telegramToken?.value || "").trim();
        if (token) payload.telegram_bot_token = token;

        const smtpPass = String(smtpPassword?.value || "").trim();
        if (smtpPass) payload.smtp_password = smtpPass;

        return payload;
    }

    function applySettings(data) {
        const s = data && data.settings ? data.settings : {};

        if (infoEmail) infoEmail.checked = !!s.info_email_enabled;
        if (infoTelegram) infoTelegram.checked = !!s.info_telegram_enabled;
        if (warnEmail) warnEmail.checked = !!s.warnings_email_enabled;
        if (warnTelegram) warnTelegram.checked = !!s.warnings_telegram_enabled;

        if (schedule) schedule.value = s.weekly_summary_enabled === false ? "disabled" : "weekly";
        if (extraEmails) extraEmails.value = Array.isArray(s.extra_emails) ? s.extra_emails.join(", ") : "";
        if (telegramChatId) telegramChatId.value = String(s.telegram_chat_id || "");

        if (telegramToken) {
            telegramToken.value = "";
            const masked = String(s.telegram_bot_token_masked || "");
            telegramToken.placeholder = masked || tr("admin.notifications.telegram_token_placeholder", null, "123456:ABC...");
            telegramToken.title = masked
                ? tr("admin.notifications.token_saved_masked", { token: masked }, `Saved token: ${masked}`)
                : "";
        }

        if (smtpHost) smtpHost.value = String(s.smtp_host || "");
        if (smtpPort) smtpPort.value = String(s.smtp_port || 587);
        if (smtpTls) smtpTls.value = String(s.smtp_tls || "starttls");
        if (smtpUser) smtpUser.value = String(s.smtp_user || "");
        if (smtpFrom) smtpFrom.value = String(s.smtp_from || "");

        if (smtpPassword) {
            smtpPassword.value = "";
            const masked = String(s.smtp_password_masked || "");
            smtpPassword.placeholder = masked || tr("admin.notifications.smtp_password_placeholder", null, "app password");
            smtpPassword.title = masked
                ? tr("admin.notifications.smtp_password_saved_masked", { password: masked }, `Saved password: ${masked}`)
                : "";
        }

        const channels = [];
        if (s.info_email_enabled || s.warnings_email_enabled) channels.push(tr("admin.notifications.email", null, "Email"));
        if (s.info_telegram_enabled || s.warnings_telegram_enabled) channels.push(tr("admin.notifications.telegram", null, "Telegram"));

        const defaultEmail = String(data.default_email || "");
        const detail = defaultEmail
            ? tr("admin.notifications.ready_with_email", { email: defaultEmail }, `ready • ${defaultEmail}`)
            : tr("admin.notifications.ready_no_admin_email", null, "ready • admin email missing");

        setInlinePill(
            defaultEmailPill,
            defaultEmail ? "info" : "warn",
            "admin.notifications.default_email",
            "Default email:",
            defaultEmail || tr("admin.notifications.admin_email_hint", null, "admin account email")
        );

        setInlinePill(
            deliveryPill,
            channels.length ? "ok" : "warn",
            "admin.notifications.delivery",
            "Delivery:",
            channels.length ? channels.join(" + ") : tr("admin.notifications.not_enabled_yet", null, "not enabled yet")
        );

        setPill(channels.length ? "ok" : "warn", detail);
    }

    async function refreshNotifications() {
        setPill("warn", tr("admin.common.loading", null, "loading…"));
        const data = await apiJson("/api/v4/admin/notifications/settings");
        applySettings(data);
    }

    function toast(kind, title, message) {
        try {
            const t = document.getElementById("toast");
            const tt = document.getElementById("toastTitle");
            const tm = document.getElementById("toastMsg");
            if (t && tt && tm) {
                t.className = "toast show " + (kind || "");
                tt.textContent = title || "";
                tm.textContent = message || "";
                window.clearTimeout(toast._timer);
                toast._timer = window.setTimeout(() => { t.className = "toast"; }, 3000);
                return;
            }
        } catch (_) {}
        console.log(title, message);
    }

    btnSave?.addEventListener("click", async (ev) => {
        ev.preventDefault();
        btnSave.disabled = true;
        setPill("warn", tr("admin.common.saving", null, "saving…"));
        try {
            const data = await apiJson("/api/v4/admin/notifications/settings", {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify(currentPayload())
            });
            applySettings(data);
            toast("ok", tr("admin.common.saved", null, "Saved"), tr("admin.notifications.saved", null, "Notification settings saved"));
        } catch (e) {
            setPill("fail", tr("admin.common.error", null, "error"));
            toast("fail", tr("admin.common.save_failed", null, "Save failed"), String(e.message || e));
        } finally {
            btnSave.disabled = false;
        }
    });

    btnTestTelegram?.addEventListener("click", async (ev) => {
        ev.preventDefault();
        btnTestTelegram.disabled = true;
        setPill("warn", tr("admin.notifications.testing_telegram", null, "testing Telegram…"));
        try {
            await apiJson("/api/v4/admin/notifications/test-telegram", {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify({ ...currentPayload(), kind: "warning" })
            });
            toast("ok", tr("admin.notifications.test_sent", null, "Test sent"), tr("admin.notifications.telegram_test_sent", null, "Telegram test message sent"));
            await refreshNotifications();
        } catch (e) {
            setPill("fail", tr("admin.common.error", null, "error"));
            toast("fail", tr("admin.notifications.test_failed", null, "Test failed"), String(e.message || e));
        } finally {
            btnTestTelegram.disabled = false;
        }
    });

    btnTestEmail?.addEventListener("click", async (ev) => {
        ev.preventDefault();
        btnTestEmail.disabled = true;
        setPill("warn", tr("admin.notifications.testing_email", null, "testing email…"));
        try {
            const saved = await apiJson("/api/v4/admin/notifications/settings", {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify(currentPayload())
            });
            applySettings(saved);

            await apiJson("/api/v4/admin/notifications/test-email", {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify({})
            });
            toast("ok", tr("admin.notifications.test_sent", null, "Test sent"), tr("admin.notifications.email_test_sent", null, "Email test sent"));
            await refreshNotifications();
        } catch (e) {
            setPill("fail", tr("admin.common.error", null, "error"));
            toast("fail", tr("admin.notifications.email_not_ready", null, "Email not ready"), String(e.message || e));
        } finally {
            btnTestEmail.disabled = false;
        }
    });

    window.addEventListener("pqnas-language-changed", () => {
        refreshNotifications().catch(e => console.error("[notifications] refresh failed", e));
    });

    refreshNotifications().catch(e => {
        console.error(e);
        setPill("fail", tr("admin.common.error", null, "error"));
    });
})();



// pqnas-vault-org-recovery-admin-ui-v1
(() => {
    "use strict";

    const $ = (id) => document.getElementById(id);

    const pill = $("vaultRecoveryPill");
    const recoveryKeyIdEl = $("vaultRecoveryKeyId");
    const createdEl = $("vaultRecoveryCreated");
    const btnGenerate = $("btnVaultRecoveryGenerate");
    const btnReload = $("btnVaultRecoveryReload");

    const win = $("vaultRecoveryDetachedWindow");
    const winHead = $("vaultRecoveryWindowHead");
    const btnWindowClose = $("btnVaultRecoveryWindowClose");
    const privateKeyEl = $("vaultRecoveryPrivateKey");
    const btnCopyPrivate = $("btnVaultRecoveryCopyPrivate");
    const btnDownloadPrivate = $("btnVaultRecoveryDownloadPrivate");
    const btnDiscardPrivate = $("btnVaultRecoveryDiscardPrivate");
    const ackEl = $("vaultRecoveryAck");
    const btnStored = $("btnVaultRecoveryStored");

    if (!pill || !btnGenerate || !win) {
        return;
    }

    let currentRecovery = null;

    // Security: the private key is held only in these in-memory JS variables
    // while the detached one-time window is open. It is never included in the
    // POST body sent to the DNA-Nexus server.
    let pendingPrivateKeyB64 = "";
    let pendingPublicKeyB64 = "";
    let pendingPublicKeySha256 = "";
    let pendingCreatedAt = 0;
    let zCounter = 7300;

    function t(key, fallback) {
        const api = window.PQNAS_I18N;
        if (api && typeof api.t === "function") {
            return api.t(key, null, fallback);
        }
        return fallback;
    }

    function setPill(kind, text) {
        if (!pill) return;
        pill.className = "pill " + (kind || "");
        const v = pill.querySelector(".v");
        if (v) v.textContent = text || "—";
    }

    function toast(kind, title, message) {
        try {
            const tEl = $("toast");
            const tt = $("toastTitle");
            const tm = $("toastMsg");
            if (tEl && tt && tm) {
                tEl.className = "toast show " + (kind || "");
                tt.textContent = title || "";
                tm.textContent = message || "";
                window.clearTimeout(toast._timer);
                toast._timer = window.setTimeout(() => { tEl.className = "toast"; }, 3200);
                return;
            }
        } catch (_) {}
        console.log(title || "", message || "");
    }

    async function apiJson(url, options) {
        const res = await fetch(url, {
            cache: "no-store",
            credentials: "include",
            ...(options || {})
        });

        let data = null;
        try {
            data = await res.json();
        } catch (_) {
            data = {};
        }

        if (!res.ok || data.ok === false) {
            throw new Error(data.message || data.error || `HTTP ${res.status}`);
        }

        return data;
    }

    function b64ToBytes(b64) {
        const bin = atob(String(b64 || "").replace(/-/g, "+").replace(/_/g, "/"));
        const out = new Uint8Array(bin.length);
        for (let i = 0; i < bin.length; i += 1) {
            out[i] = bin.charCodeAt(i);
        }
        return out;
    }

    function bytesToHex(bytes) {
        return Array.from(bytes).map((b) => b.toString(16).padStart(2, "0")).join("");
    }

    async function sha256PublicKeyB64(publicKeyB64) {
        const hash = await crypto.subtle.digest("SHA-256", b64ToBytes(publicKeyB64));
        return bytesToHex(new Uint8Array(hash));
    }

    function shortRecoveryKeyId(fp) {
        const s = String(fp || "");
        if (!s) return "—";
        if (s.length <= 24) return s;
        return `${s.slice(0, 16)}…${s.slice(-8)}`;
    }

    function formatCreated(ms) {
        const n = Number(ms || 0);
        if (!Number.isFinite(n) || n <= 0) return "—";
        try {
            return new Date(n).toLocaleString();
        } catch (_) {
            return String(n);
        }
    }

    function setGenerateButtonLabel(text) {
        const span = btnGenerate?.querySelector("span");
        if (span) span.textContent = text;
        else if (btnGenerate) btnGenerate.textContent = text;
    }

    function renderVaultRecovery(vr) {
        const v = vr && typeof vr === "object" ? vr : {};
        currentRecovery = v;

        const active = !!v.enabled && String(v.status || "") === "active";
        setPill(active ? "ok" : "warn", active ? "active" : "not configured");

        if (recoveryKeyIdEl) {
            const keyId = String(v.public_key_sha256 || "");
            recoveryKeyIdEl.textContent = shortRecoveryKeyId(keyId);
            recoveryKeyIdEl.title = keyId || "";
        }

        if (createdEl) {
            createdEl.textContent = formatCreated(v.created_at);
        }

        setGenerateButtonLabel(active ? "Rotate organization recovery key" : "Generate organization recovery key");
    }

    async function refreshVaultRecovery() {
        setPill("warn", t("admin.common.loading", "loading…"));
        const data = await apiJson("/api/v4/admin/settings");
        renderVaultRecovery(data.vault_recovery || {});
    }

    function bringWindowToFront() {
        if (!win) return;
        zCounter += 1;
        win.style.zIndex = String(zCounter);
    }

    function openPrivateKeyWindow() {
        if (!win || !privateKeyEl) return;

        privateKeyEl.value = pendingPrivateKeyB64;
        if (ackEl) ackEl.checked = false;
        if (btnStored) btnStored.disabled = true;

        win.classList.remove("hidden");
        bringWindowToFront();

        const rect = win.getBoundingClientRect();
        if (!win.style.left) {
            win.style.left = `${Math.max(16, rect.left)}px`;
            win.style.top = `${Math.max(16, rect.top)}px`;
        }

        window.setTimeout(() => {
            try {
                privateKeyEl.focus();
                privateKeyEl.select();
            } catch (_) {}
        }, 40);
    }

    function clearPendingPrivateKey() {
        pendingPrivateKeyB64 = "";
        pendingPublicKeyB64 = "";
        pendingPublicKeySha256 = "";
        pendingCreatedAt = 0;
        if (privateKeyEl) privateKeyEl.value = "";
        if (ackEl) ackEl.checked = false;
        if (btnStored) btnStored.disabled = true;
    }

    function closePrivateKeyWindowAfterClear() {
        clearPendingPrivateKey();
        win?.classList.add("hidden");
    }

    function requestWindowClose() {
        if (pendingPrivateKeyB64) {
            toast(
                "warn",
                "Recovery key is still pending",
                "Store it safely and save the public key, or discard it without saving."
            );
            return;
        }
        win?.classList.add("hidden");
    }

    async function copyPrivateKey() {
        const value = String(privateKeyEl?.value || pendingPrivateKeyB64 || "");
        if (!value) return;

        try {
            await navigator.clipboard.writeText(value);
            toast("ok", "Copied", "Private recovery key copied to clipboard.");
        } catch (_) {
            try {
                privateKeyEl?.focus();
                privateKeyEl?.select();
                document.execCommand("copy");
                toast("ok", "Copied", "Private recovery key copied to clipboard.");
            } catch (err) {
                toast("fail", "Copy failed", String(err?.message || err));
            }
        }
    }

    function downloadPrivateKeyFile() {
        if (!pendingPrivateKeyB64) return;

        const payload = {
            type: "dna-nexus-vault-organization-recovery-private-key",
            alg: "ML-KEM-768",
            private_key_format: "compact-seed-64-bytes",
            created_at: pendingCreatedAt,
            public_key_sha256: pendingPublicKeySha256,
            private_key_b64: pendingPrivateKeyB64,
            warning: "Store this file offline or in a company password manager. DNA-Nexus does not store this private key."
        };

        const blob = new Blob([JSON.stringify(payload, null, 2) + "\n"], {
            type: "application/json"
        });

        const a = document.createElement("a");
        const url = URL.createObjectURL(blob);
        a.href = url;
        a.download = `dna-nexus-vault-recovery-key-${pendingPublicKeySha256.slice(0, 12) || "new"}.json`;
        a.rel = "noopener";
        document.body.appendChild(a);
        a.click();

        window.setTimeout(() => {
            URL.revokeObjectURL(url);
            a.remove();
        }, 1000);
    }

    async function savePendingPublicKeyAndClose() {
        if (!pendingPublicKeyB64 || !pendingPublicKeySha256) {
            toast("fail", "Missing key", "No pending recovery key is available.");
            return;
        }

        if (!ackEl?.checked) {
            toast("warn", "Confirmation required", "Confirm that you have stored the private key safely.");
            return;
        }

        if (btnStored) btnStored.disabled = true;

        try {
            // Security: deliberately send only public key material and metadata.
            // The organization recovery private key never leaves this browser.
            const data = await apiJson("/api/v4/admin/settings", {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify({
                    vault_recovery: {
                        enabled: true,
                        status: "active",
                        public_key_b64: pendingPublicKeyB64,
                        public_key_sha256: pendingPublicKeySha256,
                        created_at: pendingCreatedAt,
                        label: "Organization recovery key"
                    }
                })
            });

            renderVaultRecovery(data.vault_recovery || {});
            closePrivateKeyWindowAfterClear();
            toast("ok", "Saved", "Vault organization recovery public key and recovery key ID saved.");
        } catch (err) {
            if (btnStored) btnStored.disabled = false;
            toast("fail", "Save failed", String(err?.message || err));
        }
    }

    function confirmRecoveryRotationDetached() {
        return new Promise((resolve) => {
            document.getElementById("vaultRecoveryRotateConfirmWindow")?.remove();

            const root = document.createElement("div");
            root.id = "vaultRecoveryRotateConfirmWindow";
            root.className = "vaultRecoveryDetached";
            root.setAttribute("role", "dialog");
            root.setAttribute("aria-modal", "false");
            root.setAttribute("aria-labelledby", "vaultRecoveryRotateConfirmTitle");
            root.style.width = "min(560px, calc(100vw - 32px))";
            root.style.left = "max(24px, calc(50vw - 280px))";
            root.style.top = "112px";

            root.innerHTML = `
                <div class="vaultRecoveryDetachedHead">
                    <div id="vaultRecoveryRotateConfirmTitle" class="vaultRecoveryDetachedTitle">Rotate organization recovery key?</div>
                    <button class="pq-btn" type="button" data-action="cancel">×</button>
                </div>
                <div class="vaultRecoveryDetachedBody">
                    <div class="vaultRecoveryWarning">
                        New Vault uploads will use the new recovery public key. Old encrypted packages still require the old private key.
                    </div>
                    <div class="note">
                        Make sure old recovery private keys remain stored safely before rotating.
                    </div>
                </div>
                <div class="vaultRecoveryDetachedFoot">
                    <span class="vaultRecoveryAck">This does not re-wrap old Vault packages.</span>
                    <div class="row">
                        <button class="pq-btn" type="button" data-action="cancel">Cancel</button>
                        <button class="pq-btn primary" type="button" data-action="ok">Rotate key</button>
                    </div>
                </div>
            `;

            document.body.appendChild(root);

            zCounter += 1;
            root.style.zIndex = String(zCounter);

            const head = root.querySelector(".vaultRecoveryDetachedHead");
            const okBtn = root.querySelector('[data-action="ok"]');
            const cancelBtns = root.querySelectorAll('[data-action="cancel"]');

            let done = false;
            const finish = (value) => {
                if (done) return;
                done = true;
                document.removeEventListener("keydown", onKey, true);
                root.remove();
                resolve(value);
            };

            const onKey = (ev) => {
                if (ev.key === "Escape") {
                    ev.preventDefault();
                    finish(false);
                }
            };

            document.addEventListener("keydown", onKey, true);
            okBtn?.addEventListener("click", () => finish(true));
            cancelBtns.forEach((btn) => btn.addEventListener("click", () => finish(false)));

            // Detached Windows-like dialog: draggable titlebar, no blocking browser alert.
            let drag = null;
            head?.addEventListener("pointerdown", (ev) => {
                if (ev.target && ev.target.closest && ev.target.closest("button,input,textarea,select,a")) {
                    return;
                }

                zCounter += 1;
                root.style.zIndex = String(zCounter);

                const rect = root.getBoundingClientRect();
                root.style.left = `${rect.left}px`;
                root.style.top = `${rect.top}px`;

                drag = {
                    pointerId: ev.pointerId,
                    startX: ev.clientX,
                    startY: ev.clientY,
                    left: rect.left,
                    top: rect.top
                };

                try { head.setPointerCapture(ev.pointerId); } catch (_) {}
                ev.preventDefault();
            });

            head?.addEventListener("pointermove", (ev) => {
                if (!drag || drag.pointerId !== ev.pointerId) return;

                const maxLeft = Math.max(16, window.innerWidth - root.offsetWidth - 16);
                const maxTop = Math.max(16, window.innerHeight - root.offsetHeight - 16);

                const left = Math.min(maxLeft, Math.max(16, drag.left + ev.clientX - drag.startX));
                const top = Math.min(maxTop, Math.max(16, drag.top + ev.clientY - drag.startY));

                root.style.left = `${left}px`;
                root.style.top = `${top}px`;
            });

            const stopDrag = (ev) => {
                if (!drag || drag.pointerId !== ev.pointerId) return;
                try { head.releasePointerCapture(ev.pointerId); } catch (_) {}
                drag = null;
            };

            head?.addEventListener("pointerup", stopDrag);
            head?.addEventListener("pointercancel", stopDrag);

            window.setTimeout(() => okBtn?.focus(), 40);
        });
    }

    function bytesEqual(a, b) {
        if (!a || !b || a.length !== b.length) return false;
        let diff = 0;
        for (let i = 0; i < a.length; i += 1) {
            diff |= a[i] ^ b[i];
        }
        return diff === 0;
    }

    function zeroBytes(bytes) {
        if (bytes && typeof bytes.fill === "function") {
            try { bytes.fill(0); } catch (_) {}
        }
    }

    async function assertRecoveryKeyRoundtrip(helper, publicKeyB64, privateKeyB64) {
        if (!helper ||
            typeof helper.encapsulate768 !== "function" ||
            typeof helper.decapsulate768 !== "function") {
            throw new Error("ML-KEM helper does not support recovery key self-test");
        }

        const publicKeyBytes = b64ToBytes(publicKeyB64);
        const privateKeyBytes = b64ToBytes(privateKeyB64);

        if (publicKeyBytes.length !== 1184) {
            throw new Error(`Unexpected ML-KEM-768 public key size: ${publicKeyBytes.length} bytes`);
        }

        if (privateKeyBytes.length !== 64) {
            throw new Error(`Unexpected ML-KEM-768 compact private key size: ${privateKeyBytes.length} bytes`);
        }

        let encSecret = null;
        let decSecret = null;

        try {
            const enc = await helper.encapsulate768({ publicKeyB64 });
            encSecret = enc && enc.shared_secret_bytes;
            decSecret = await helper.decapsulate768({
                privateKeyB64,
                ciphertextB64: enc.ciphertext_b64
            });

            if (!(encSecret instanceof Uint8Array) || !(decSecret instanceof Uint8Array)) {
                throw new Error("ML-KEM recovery key self-test returned invalid shared secret buffers");
            }

            if (encSecret.length !== 32 || decSecret.length !== 32 || !bytesEqual(encSecret, decSecret)) {
                throw new Error("ML-KEM recovery key self-test failed");
            }
        } finally {
            // Security: wipe temporary shared secrets created only for the
            // pre-save recovery key self-test.
            zeroBytes(encSecret);
            zeroBytes(decSecret);
            zeroBytes(publicKeyBytes);
            zeroBytes(privateKeyBytes);
        }
    }

    async function generateRecoveryKey() {
        const active = !!currentRecovery?.enabled && String(currentRecovery?.status || "") === "active";
        if (active) {
            const ok = await confirmRecoveryRotationDetached();
            if (!ok) return;
        }

        const helper = window.PqShareMlKemV1;
        if (!helper || typeof helper.keygen768 !== "function") {
            toast("fail", "ML-KEM helper missing", "Reload the page and try again.");
            return;
        }

        btnGenerate.disabled = true;
        setPill("warn", "generating…");

        try {
            const kp = await helper.keygen768();
            pendingPrivateKeyB64 = String(kp.private_key_b64 || "");
            pendingPublicKeyB64 = String(kp.public_key_b64 || "");
            pendingPublicKeySha256 = await sha256PublicKeyB64(pendingPublicKeyB64);
            pendingCreatedAt = Date.now();

            if (!pendingPrivateKeyB64 || !pendingPublicKeyB64 || pendingPublicKeySha256.length !== 64) {
                clearPendingPrivateKey();
                throw new Error("ML-KEM key generation returned invalid key material");
            }

            await assertRecoveryKeyRoundtrip(
                helper,
                pendingPublicKeyB64,
                pendingPrivateKeyB64
            );

            openPrivateKeyWindow();
            setPill(active ? "ok" : "warn", active ? "active" : "not configured");
        } catch (err) {
            clearPendingPrivateKey();
            toast("fail", "Key generation failed", String(err?.message || err));
            await refreshVaultRecovery().catch(() => {});
        } finally {
            btnGenerate.disabled = false;
        }
    }

    function confirmRecoveryDiscardDetached() {
        return new Promise((resolve) => {
            document.getElementById("vaultRecoveryDiscardConfirmWindow")?.remove();

            const root = document.createElement("div");
            root.id = "vaultRecoveryDiscardConfirmWindow";
            root.className = "vaultRecoveryDetached";
            root.setAttribute("role", "dialog");
            root.setAttribute("aria-modal", "false");
            root.setAttribute("aria-labelledby", "vaultRecoveryDiscardConfirmTitle");
            root.style.width = "min(560px, calc(100vw - 32px))";
            root.style.left = "max(24px, calc(50vw - 280px))";
            root.style.top = "128px";

            root.innerHTML = `
                <div class="vaultRecoveryDetachedHead">
                    <div id="vaultRecoveryDiscardConfirmTitle" class="vaultRecoveryDetachedTitle">Discard generated private key?</div>
                    <button class="pq-btn" type="button" data-action="cancel">×</button>
                </div>
                <div class="vaultRecoveryDetachedBody">
                    <div class="vaultRecoveryWarning">
                        This will close the one-time private key window without saving the matching public key to DNA-Nexus.
                    </div>
                    <div class="note">
                        Use this only if you do not want to activate this generated recovery key.
                    </div>
                </div>
                <div class="vaultRecoveryDetachedFoot">
                    <span class="vaultRecoveryAck">The generated keypair will be forgotten by this browser view.</span>
                    <div class="row">
                        <button class="pq-btn" type="button" data-action="cancel">Cancel</button>
                        <button class="pq-btn primary" type="button" data-action="ok">Discard key</button>
                    </div>
                </div>
            `;

            document.body.appendChild(root);

            zCounter += 1;
            root.style.zIndex = String(zCounter);

            const head = root.querySelector(".vaultRecoveryDetachedHead");
            const okBtn = root.querySelector('[data-action="ok"]');
            const cancelBtns = root.querySelectorAll('[data-action="cancel"]');

            let done = false;
            const finish = (value) => {
                if (done) return;
                done = true;
                document.removeEventListener("keydown", onKey, true);
                root.remove();
                resolve(value);
            };

            const onKey = (ev) => {
                if (ev.key === "Escape") {
                    ev.preventDefault();
                    finish(false);
                }
            };

            document.addEventListener("keydown", onKey, true);
            okBtn?.addEventListener("click", () => finish(true));
            cancelBtns.forEach((btn) => btn.addEventListener("click", () => finish(false)));

            // Detached Windows-like dialog: draggable titlebar, no native browser confirm.
            let drag = null;
            head?.addEventListener("pointerdown", (ev) => {
                if (ev.target && ev.target.closest && ev.target.closest("button,input,textarea,select,a")) {
                    return;
                }

                zCounter += 1;
                root.style.zIndex = String(zCounter);

                const rect = root.getBoundingClientRect();
                root.style.left = `${rect.left}px`;
                root.style.top = `${rect.top}px`;

                drag = {
                    pointerId: ev.pointerId,
                    startX: ev.clientX,
                    startY: ev.clientY,
                    left: rect.left,
                    top: rect.top
                };

                try { head.setPointerCapture(ev.pointerId); } catch (_) {}
                ev.preventDefault();
            });

            head?.addEventListener("pointermove", (ev) => {
                if (!drag || drag.pointerId !== ev.pointerId) return;

                const maxLeft = Math.max(16, window.innerWidth - root.offsetWidth - 16);
                const maxTop = Math.max(16, window.innerHeight - root.offsetHeight - 16);

                const left = Math.min(maxLeft, Math.max(16, drag.left + ev.clientX - drag.startX));
                const top = Math.min(maxTop, Math.max(16, drag.top + ev.clientY - drag.startY));

                root.style.left = `${left}px`;
                root.style.top = `${top}px`;
            });

            const stopDrag = (ev) => {
                if (!drag || drag.pointerId !== ev.pointerId) return;
                try { head.releasePointerCapture(ev.pointerId); } catch (_) {}
                drag = null;
            };

            head?.addEventListener("pointerup", stopDrag);
            head?.addEventListener("pointercancel", stopDrag);

            window.setTimeout(() => okBtn?.focus(), 40);
        });
    }

    async function discardPendingKey() {
        if (!pendingPrivateKeyB64) {
            closePrivateKeyWindowAfterClear();
            return;
        }

        const ok = await confirmRecoveryDiscardDetached();
        if (!ok) return;

        closePrivateKeyWindowAfterClear();
        toast("warn", "Discarded", "Generated recovery key was discarded and not saved.");
    }

    function initDrag() {
        if (!win || !winHead) return;

        let drag = null;

        winHead.addEventListener("pointerdown", (ev) => {
            if (ev.target && ev.target.closest && ev.target.closest("button,input,textarea,select,a")) {
                return;
            }

            bringWindowToFront();

            const rect = win.getBoundingClientRect();
            win.style.left = `${rect.left}px`;
            win.style.top = `${rect.top}px`;

            drag = {
                pointerId: ev.pointerId,
                startX: ev.clientX,
                startY: ev.clientY,
                left: rect.left,
                top: rect.top
            };

            try { winHead.setPointerCapture(ev.pointerId); } catch (_) {}
            ev.preventDefault();
        });

        winHead.addEventListener("pointermove", (ev) => {
            if (!drag || drag.pointerId !== ev.pointerId) return;

            const maxLeft = Math.max(16, window.innerWidth - win.offsetWidth - 16);
            const maxTop = Math.max(16, window.innerHeight - win.offsetHeight - 16);

            const left = Math.min(maxLeft, Math.max(16, drag.left + ev.clientX - drag.startX));
            const top = Math.min(maxTop, Math.max(16, drag.top + ev.clientY - drag.startY));

            win.style.left = `${left}px`;
            win.style.top = `${top}px`;
        });

        const stop = (ev) => {
            if (!drag || drag.pointerId !== ev.pointerId) return;
            try { winHead.releasePointerCapture(ev.pointerId); } catch (_) {}
            drag = null;
        };

        winHead.addEventListener("pointerup", stop);
        winHead.addEventListener("pointercancel", stop);
    }

    btnGenerate?.addEventListener("click", (ev) => {
        ev.preventDefault();
        generateRecoveryKey();
    });

    btnReload?.addEventListener("click", (ev) => {
        ev.preventDefault();
        refreshVaultRecovery().catch((err) => {
            setPill("fail", "error");
            toast("fail", "Reload failed", String(err?.message || err));
        });
    });

    btnWindowClose?.addEventListener("click", (ev) => {
        ev.preventDefault();
        requestWindowClose();
    });

    btnCopyPrivate?.addEventListener("click", (ev) => {
        ev.preventDefault();
        copyPrivateKey();
    });

    btnDownloadPrivate?.addEventListener("click", (ev) => {
        ev.preventDefault();
        downloadPrivateKeyFile();
    });

    btnDiscardPrivate?.addEventListener("click", (ev) => {
        ev.preventDefault();
        discardPendingKey().catch((err) => {
            toast("fail", "Discard failed", String(err?.message || err));
        });
    });

    ackEl?.addEventListener("change", () => {
        if (btnStored) btnStored.disabled = !ackEl.checked;
    });

    btnStored?.addEventListener("click", (ev) => {
        ev.preventDefault();
        savePendingPublicKeyAndClose();
    });

    win?.addEventListener("pointerdown", () => bringWindowToFront());

    initDrag();
    refreshVaultRecovery().catch((err) => {
        setPill("fail", "error");
        console.error("[vault_recovery] refresh failed", err);
    });
})();




// pqnas-admin-create-password-user-i18n-v1
(() => {
    function t(key, fallback) {
        const api = window.PQNAS_I18N;
        if (api && typeof api.t === "function") {
            return api.t(key, null, fallback);
        }
        return fallback;
    }

    function setText(el, key, fallback) {
        if (el) el.textContent = t(key, fallback);
    }

    function apply() {
        const card = document.getElementById("adminPasswordUserCreateCard");
        if (!card) return;

        setText(card.querySelector(".hd .h"), "admin.password_user_create.card_title", "Security • Create password user");

        const bd = card.querySelector(".bd");
        if (!bd) return;

        const rowTitle = bd.querySelector(".row div");
        setText(rowTitle, "admin.password_user_create.heading", "Create password-auth user with DNA recovery phrase");

        const notes = bd.querySelectorAll(".note");
        setText(notes[0], "admin.password_user_create.desc", "Creates a user with a real DNA fingerprint. Recovery words are shown once and are not stored by the server.");
        setText(notes[1], "admin.password_user_create.default_status_note", "Default status is disabled, so the user cannot sign in until approved/enabled.");

        const labels = bd.querySelectorAll(".passwordUserCreateField .label");
        setText(labels[0], "admin.password_user_create.name", "Name");
        setText(labels[1], "admin.password_user_create.login", "Login / email");
        setText(labels[2], "admin.password_user_create.initial_password", "Initial password");
        setText(labels[3], "admin.password_user_create.role", "Role");
        setText(labels[4], "admin.password_user_create.status", "Status");
        setText(labels[5], "admin.password_user_create.quota_bytes", "Quota bytes");

        const status = document.getElementById("adminCreatePasswordUserStatus");
        if (status && status.options.length >= 3) {
            status.options[0].textContent = t("admin.password_user_create.status_disabled", "disabled — needs approval");
            status.options[1].textContent = t("admin.password_user_create.status_enabled", "enabled — can sign in immediately");
            status.options[2].textContent = t("admin.password_user_create.status_pending", "pending");
        }

        const btn = document.getElementById("btnAdminCreatePasswordUser");
        if (btn && btn.textContent !== "Creating…") {
            setText(btn, "admin.password_user_create.create_button", "Create password user");
        }

        const result = document.getElementById("adminCreatePasswordUserResult");
        if (result) {
            const title = result.querySelector("div[style*='font-weight']");
            setText(title, "admin.password_user_create.result_title", "Recovery words — shown once");

            const resultNotes = result.querySelectorAll(".note");
            setText(resultNotes[0], "admin.password_user_create.result_note", "Copy these 24 words now. They are not stored by the server and cannot be shown again.");

            const infoDivs = resultNotes[1]?.querySelectorAll("div") || [];
            if (infoDivs[0]?.firstElementChild) setText(infoDivs[0].firstElementChild, "admin.password_user_create.result_login", "Login:");
            if (infoDivs[1]?.firstElementChild) setText(infoDivs[1].firstElementChild, "admin.password_user_create.result_fingerprint", "Fingerprint:");
            if (infoDivs[2]?.firstElementChild) setText(infoDivs[2].firstElementChild, "admin.password_user_create.result_status", "Status:");
            if (infoDivs[3]?.firstElementChild) setText(infoDivs[3].firstElementChild, "admin.password_user_create.result_quota_bytes", "Quota bytes:");

            const copied = document.getElementById("adminCreatePasswordUserCopied");
            const copiedText = copied?.parentElement?.querySelector("span");
            setText(copiedText, "admin.password_user_create.copied_checkbox", "I have copied the recovery words.");
        }
    }

    let tries = 0;
    const timer = setInterval(() => {
        tries += 1;
        apply();
        if (document.getElementById("adminPasswordUserCreateCard") || tries > 80) {
            clearInterval(timer);
        }
    }, 250);

    if (document.readyState === "loading") {
        document.addEventListener("DOMContentLoaded", apply, { once: true });
    } else {
        apply();
    }
})();
