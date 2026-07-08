function tr(key, vars = null, fallback = "") {
    try {
        if (window.PQNAS_I18N && typeof window.PQNAS_I18N.t === "function") {
            return window.PQNAS_I18N.t(key, vars, fallback || key);
        }
    } catch (_) {}
    return fallback || key;
}

function applyStaticI18n() {
    try {
        if (window.PQNAS_I18N && typeof window.PQNAS_I18N.apply === "function") {
            window.PQNAS_I18N.apply(document);
        }
    } catch (_) {}
}

function statusLabel(status) {
    const s = String(status || "unknown").toLowerCase();
    if (s === "enabled") return tr("admin.users.status.enabled", null, "enabled");
    if (s === "disabled") return tr("admin.users.status.disabled", null, "disabled");
    if (s === "revoked") return tr("admin.users.status.revoked", null, "revoked");
    return tr("admin.users.status.unknown", null, s || "unknown");
}

function roleLabel(role) {
    const r = String(role || "").toLowerCase();
    if (r === "admin") return tr("admin.users.role.admin", null, "admin");
    if (r === "user") return tr("admin.users.role.user", null, "user");
    return String(role || "");
}

function storageStateLabel(state) {
    const s = String(state || "unallocated").toLowerCase();
    if (s === "allocated") return tr("admin.users.storage.allocated", null, "allocated");
    if (s === "unallocated") return tr("admin.users.storage.unallocated", null, "unallocated");
    return s;
}

async function apiGet(path) {
    const r = await fetch(path, { headers: { "Accept": "application/json" }, cache: "no-store" });
    const j = await r.json().catch(() => ({}));
    if (!r.ok || !j.ok) throw new Error(j.message || j.error || ("HTTP " + r.status));
    return j;
}


// for showing quota usage bar
function clamp01(x) {
    x = Number(x);
    if (!isFinite(x)) return 0;
    return Math.max(0, Math.min(1, x));
}

function fmtBytesShort(n) {
    if (!Number.isFinite(n) || n < 0) return "—";
    const units = ["B","KiB","MiB","GiB","TiB"];
    let x = n, i = 0;
    while (x >= 1024 && i < units.length - 1) { x /= 1024; i++; }
    return `${x.toFixed(i === 0 ? 0 : (i === 1 ? 1 : 2))} ${units[i]}`;
}

function quotaUsageText(usedBytes, quotaBytes) {
    const used = Number(usedBytes);
    const quota = Number(quotaBytes);
    if (!isFinite(quota) || quota <= 0) return "—";
    return `${fmtBytesShort(isFinite(used) ? used : NaN)} / ${fmtBytesShort(quota)}`;
}

function quotaUsagePct(usedBytes, quotaBytes) {
    const used = Number(usedBytes);
    const quota = Number(quotaBytes);
    if (!isFinite(used) || !isFinite(quota) || quota <= 0) return 0;
    return clamp01(used / quota);
}


// allow multiple open rows
const openUsers = new Set(); // fingerprints

const ADMIN_USERS_STORAGE_JOBS_KEY = "pqnas.adminUsers.storageJobs.v1";
const ADMIN_USERS_DONE_JOB_AUTO_HIDE_MS = 2500;
const adminUsersStorageJobPolls = new Map();
const adminUsersStorageJobDoneTimers = new Map();

function adminUsersValidStorageJobKind(kind) {
    return kind === "migration" || kind === "cleanup";
}

function adminUsersSafeJobId(jobId) {
    const s = String(jobId || "").trim();
    return /^[a-f0-9]{64}$/.test(s) ? s : "";
}

function adminUsersSafeFingerprint(fp) {
    const s = String(fp || "").trim();
    return /^[a-f0-9]{32,160}$/.test(s) ? s : "";
}

function adminUsersStorageJobKey(kind, jobId) {
    return `${kind}:${jobId}`;
}

function adminUsersStorageJobsLoad() {
    try {
        const raw = localStorage.getItem(ADMIN_USERS_STORAGE_JOBS_KEY);
        const arr = JSON.parse(raw || "[]");
        if (!Array.isArray(arr)) return [];

        const now = Date.now();
        const maxAgeMs = 24 * 60 * 60 * 1000;

        return arr
            .filter(j => j && typeof j === "object")
            .map(j => {
                const kind = String(j.kind || "");
                const job_id = adminUsersSafeJobId(j.job_id);
                const fingerprint = adminUsersSafeFingerprint(j.fingerprint);
                if (!adminUsersValidStorageJobKind(kind) || !job_id || !fingerprint) return null;

                const updatedAt = Date.parse(String(j.updated_at || j.created_at || "")) || now;
                if ((now - updatedAt) > maxAgeMs) return null;

                const stateForExpiry = String(j.state || "queued").toLowerCase();

                // UX: completed bulk jobs create a huge panel during mass moves.
                // Keep failures visible for manual review, but let successful
                // jobs disappear shortly after completion.
                if (stateForExpiry === "done" && (now - updatedAt) > ADMIN_USERS_DONE_JOB_AUTO_HIDE_MS) {
                    return null;
                }

                return {
                    kind,
                    job_id,
                    fingerprint,
                    state: String(j.state || "queued"),
                    phase: String(j.phase || ""),
                    percent: Number.isFinite(Number(j.percent)) ? Number(j.percent) : null,
                    copy_percent: Number.isFinite(Number(j.copy_percent)) ? Number(j.copy_percent) : null,
                    bytes_done: Number.isFinite(Number(j.bytes_done)) ? Number(j.bytes_done) : null,
                    bytes_total: Number.isFinite(Number(j.bytes_total)) ? Number(j.bytes_total) : null,
                    message: String(j.message || ""),
                    from_pool_id: String(j.from_pool_id || ""),
                    to_pool_id: String(j.to_pool_id || ""),
                    active_pool_id: String(j.active_pool_id || ""),
                    old_pool_id: String(j.old_pool_id || ""),
                    created_at: String(j.created_at || new Date(now).toISOString()),
                    updated_at: String(j.updated_at || new Date(now).toISOString()),
                };
            })
            .filter(Boolean)
            .slice(0, 25);
    } catch (_) {
        return [];
    }
}

function adminUsersStorageJobsSave(jobs) {
    try {
        localStorage.setItem(ADMIN_USERS_STORAGE_JOBS_KEY, JSON.stringify((Array.isArray(jobs) ? jobs : []).slice(0, 25)));
    } catch (_) {}
}

function adminUsersUpsertStorageJob(kind, jobId, fp, patch = {}) {
    if (!adminUsersValidStorageJobKind(kind)) return;
    const job_id = adminUsersSafeJobId(jobId);
    const fingerprint = adminUsersSafeFingerprint(fp);
    if (!job_id || !fingerprint) return;

    const nowIso = new Date().toISOString();
    const jobs = adminUsersStorageJobsLoad();
    const key = adminUsersStorageJobKey(kind, job_id);
    const idx = jobs.findIndex(j => adminUsersStorageJobKey(j.kind, j.job_id) === key);

    const prev = idx >= 0 ? jobs[idx] : {
        kind,
        job_id,
        fingerprint,
        created_at: nowIso,
        state: "queued",
        phase: "queued",
    };

    const next = {
        ...prev,
        ...patch,
        kind,
        job_id,
        fingerprint,
        updated_at: nowIso,
    };

    if (idx >= 0) jobs[idx] = next;
    else jobs.unshift(next);

    adminUsersStorageJobsSave(jobs);
    renderAdminUsersStorageJobPanel();
}

function adminUsersRemoveStorageJob(kind, jobId) {
    const job_id = adminUsersSafeJobId(jobId);
    if (!adminUsersValidStorageJobKind(kind) || !job_id) return;

    const key = adminUsersStorageJobKey(kind, job_id);

    const timer = adminUsersStorageJobDoneTimers.get(key);
    if (timer) {
        clearTimeout(timer);
        adminUsersStorageJobDoneTimers.delete(key);
    }

    adminUsersStorageJobsSave(
        adminUsersStorageJobsLoad().filter(j => adminUsersStorageJobKey(j.kind, j.job_id) !== key)
    );
    renderAdminUsersStorageJobPanel();
}

function adminUsersScheduleDoneStorageJobRemoval(job) {
    const kind = String(job?.kind || "");
    const jobId = adminUsersSafeJobId(job?.job_id);
    if (!adminUsersValidStorageJobKind(kind) || !jobId) return;

    const key = adminUsersStorageJobKey(kind, jobId);
    if (adminUsersStorageJobDoneTimers.has(key)) return;

    const updatedAt = Date.parse(String(job.updated_at || job.created_at || "")) || Date.now();
    const ageMs = Math.max(0, Date.now() - updatedAt);
    const delayMs = Math.max(0, ADMIN_USERS_DONE_JOB_AUTO_HIDE_MS - ageMs);

    const timer = setTimeout(() => {
        adminUsersStorageJobDoneTimers.delete(key);
        adminUsersRemoveStorageJob(kind, jobId);
    }, delayMs);

    adminUsersStorageJobDoneTimers.set(key, timer);
}

function adminUsersUpdateStorageJobFromRecord(kind, jobId, fp, job) {
    const rec = job || {};
    const state = String(rec.state || "");
    const phase = String(rec.phase || "");
    const percentRaw = Number(rec.percent);
    const percent = Number.isFinite(percentRaw) ? Math.max(0, Math.min(100, percentRaw)) : null;

    const copyPercentRaw = Number(rec.copy_percent);
    const bytesDoneRaw = Number(rec.bytes_done);
    const bytesTotalRaw = Number(rec.bytes_total);

    const patch = {
        state: state || "running",
        phase,
        percent,
        copy_percent: Number.isFinite(copyPercentRaw) ? copyPercentRaw : null,
        bytes_done: Number.isFinite(bytesDoneRaw) ? bytesDoneRaw : null,
        bytes_total: Number.isFinite(bytesTotalRaw) ? bytesTotalRaw : null,
        message: String(rec.message || rec.error || ""),
    };

    if (kind === "migration") {
        patch.from_pool_id = String(rec.resolved_source_pool_id || rec.from_pool_id || "default");
        patch.to_pool_id = String(rec.resolved_dest_pool_id || rec.requested_target_pool_id || rec.to_pool_id || "");
    } else if (kind === "cleanup") {
        patch.active_pool_id = String(rec.resolved_active_pool_id || rec.expected_active_pool_id || "");
        patch.old_pool_id = String(rec.resolved_old_pool_id || rec.old_pool_id || "");
    }

    adminUsersUpsertStorageJob(kind, jobId, fp, patch);
}

function adminUsersStorageJobStateLabel(state) {
    const s = String(state || "").toLowerCase();
    if (s === "queued") return tr("admin.users.job_state.queued", null, "queued");
    if (s === "running") return tr("admin.users.job_state.running", null, "running");
    if (s === "done") return tr("admin.users.job_state.done", null, "done");
    if (s === "failed") return tr("admin.users.job_state.failed", null, "failed");
    return s;
}

function adminUsersStorageJobPhaseLabel(phase) {
    const p = String(phase || "").toLowerCase();
    const map = {
        queued: "queued",
        starting: "starting",
        acquiring_lock: "acquiring_lock",
        resolving_paths: "resolving_paths",
        validating_destination_capacity: "validating_destination_capacity",
        creating_destination: "creating_destination",
        copying: "copying",
        verifying: "verifying",
        switching_metadata: "switching_metadata",
        reloading_metadata: "reloading_metadata",
        resolving_paths_cleanup: "resolving_paths",
        validating_active_mapping: "validating_active_mapping",
        validating_old_copy: "validating_old_copy",
        deleting_old_copy: "deleting_old_copy",
        done: "done",
    };

    const key = map[p] || p;
    if (!key) return "";
    return tr(`admin.users.job_phase.${key}`, null, key.replaceAll("_", " "));
}

function adminUsersStorageJobPoolLabel(poolId) {
    const p = String(poolId || "").trim();
    if (!p || p === "default") return tr("admin.users.pool.default", null, "default");
    return p;
}

function adminUsersStorageJobMessage(job) {
    const j = job || {};
    const state = String(j.state || "").toLowerCase();
    const phase = String(j.phase || "").toLowerCase();

    if (state === "failed") {
        return String(j.message || tr("admin.users.job_state.failed", null, "failed"));
    }

    if (j.kind === "migration" && phase === "copying") {
        const done = Number(j.bytes_done);
        const total = Number(j.bytes_total);
        const pct = Number(j.copy_percent);

        if (Number.isFinite(done) && Number.isFinite(total) && total > 0) {
            const vars = {
                done: fmtBytesShort(done),
                total: fmtBytesShort(total),
                percent: Number.isFinite(pct) ? pct.toFixed(pct >= 10 ? 0 : 1) : "",
            };

            if (vars.percent) {
                return tr("admin.users.job_message.copy_progress", vars, `${vars.done} / ${vars.total} copied (${vars.percent}%)`);
            }

            return tr("admin.users.job_message.copy_progress_no_percent", vars, `${vars.done} / ${vars.total} copied`);
        }
    }

    if (state === "done" && j.kind === "migration") {
        return tr("admin.users.job_message.migration_done", null, "migration completed");
    }

    if (state === "done" && j.kind === "cleanup") {
        return tr("admin.users.job_message.cleanup_done", null, "old inactive copy deleted");
    }

    if (phase) {
        return adminUsersStorageJobPhaseLabel(phase);
    }

    return String(j.message || "");
}

function injectAdminUsersStorageJobCss() {
    if (document.getElementById("adminUsersStorageJobCss")) return;

    const style = document.createElement("style");
    style.id = "adminUsersStorageJobCss";
    style.textContent = `
.adminUsersJobPanel{
    border:1px solid var(--border2);
    border-radius:var(--radius);
    background:linear-gradient(180deg, var(--panel2), var(--panel));
    color:var(--fg);
    padding:12px;
    display:grid;
    gap:10px;
    box-shadow:var(--shadow);
}
.adminUsersJobPanelHead{
    display:flex;
    align-items:center;
    justify-content:space-between;
    gap:12px;
}
.adminUsersJobPanelTitle{
    font-weight:950;
    letter-spacing:.02em;
}
.adminUsersJobList{
    display:grid;
    gap:8px;
}
.adminUsersJobItem{
    border:1px solid var(--border2);
    border-radius:14px;
    background:var(--panel);
    padding:10px;
    display:grid;
    gap:7px;
}
.adminUsersJobTop{
    display:flex;
    align-items:center;
    justify-content:space-between;
    gap:10px;
}
.adminUsersJobName{
    font-weight:900;
    overflow-wrap:anywhere;
}
.adminUsersJobMeta{
    color:var(--fg-dim);
    font-size:12px;
    overflow-wrap:anywhere;
}
.adminUsersJobBar{
    height:9px;
    overflow:hidden;
    border-radius:999px;
    border:1px solid var(--border2);
    background:var(--panel2);
}
.adminUsersJobFill{
    height:100%;
    width:var(--pct, 0%);
    border-radius:999px;
    background:var(--accent, var(--fg));
    transition:width .25s ease;
}
.adminUsersJobFill.indeterminate{
    width:36%;
    animation:adminUsersJobSweep 1.4s ease-in-out infinite;
}
.adminUsersJobFill.failed{
    background:rgba(var(--fail-rgb),0.72);
}
@keyframes adminUsersJobSweep{
    from{ transform:translateX(-120%); }
    to{ transform:translateX(300%); }
}
.adminUsersJobActions{
    display:flex;
    justify-content:flex-end;
    gap:8px;
}
`;
    document.head.appendChild(style);
}

function renderAdminUsersStorageJobPanel() {
    injectAdminUsersStorageJobCss();

    const jobs = adminUsersStorageJobsLoad();
    let panel = document.getElementById("adminUsersStorageJobsPanel");

    if (!jobs.length) {
        if (panel) panel.remove();
        return;
    }

    if (!panel) {
        panel = document.createElement("section");
        panel.id = "adminUsersStorageJobsPanel";
        panel.className = "adminUsersJobPanel";
        panel.setAttribute("aria-live", "polite");

        const content = document.querySelector(".content") || document.body;
        content.insertBefore(panel, content.firstChild || null);
    }

    panel.replaceChildren();

    const head = document.createElement("div");
    head.className = "adminUsersJobPanelHead";

    const title = document.createElement("div");
    title.className = "adminUsersJobPanelTitle";
    title.textContent = tr("admin.users.storage_jobs_title", null, "Storage jobs");

    const hint = document.createElement("div");
    hint.className = "adminUsersJobMeta";
    hint.textContent = tr("admin.users.storage_jobs_hint", null, "Server-side jobs continue even if this page is closed.");

    head.appendChild(title);
    head.appendChild(hint);
    panel.appendChild(head);

    const list = document.createElement("div");
    list.className = "adminUsersJobList";

    for (const job of jobs) {
        if (String(job.state || "").toLowerCase() === "done") {
            adminUsersScheduleDoneStorageJobRemoval(job);
        }

        const item = document.createElement("article");
        item.className = "adminUsersJobItem";

        const top = document.createElement("div");
        top.className = "adminUsersJobTop";

        const name = document.createElement("div");
        name.className = "adminUsersJobName";
        name.textContent = job.kind === "cleanup"
            ? tr("admin.users.cleanup_job", null, "Old storage cleanup")
            : tr("admin.users.migration_job", null, "Storage migration");

        const state = document.createElement("div");
        state.className = "adminUsersJobMeta";
        state.textContent = [
            adminUsersStorageJobStateLabel(job.state),
            adminUsersStorageJobPhaseLabel(job.phase)
        ].filter(Boolean).join(" · ");

        top.appendChild(name);
        top.appendChild(state);
        item.appendChild(top);

        const meta = document.createElement("div");
        meta.className = "adminUsersJobMeta";
        meta.textContent = job.kind === "migration"
            ? `${adminUsersStorageJobPoolLabel(job.from_pool_id || "default")} → ${adminUsersStorageJobPoolLabel(job.to_pool_id || "?")} · ${job.fingerprint}`
            : `${adminUsersStorageJobPoolLabel(job.active_pool_id || "?")} / ${tr("admin.users.old_pool_label", { pool: adminUsersStorageJobPoolLabel(job.old_pool_id || "?") }, "old {pool}")} · ${job.fingerprint}`;
        item.appendChild(meta);

        const jobMessage = adminUsersStorageJobMessage(job);
        if (jobMessage) {
            const message = document.createElement("div");
            message.className = "adminUsersJobMeta";
            message.textContent = jobMessage;
            item.appendChild(message);
        }

        const bar = document.createElement("div");
        bar.className = "adminUsersJobBar";

        const fill = document.createElement("div");
        fill.className = "adminUsersJobFill";

        const stateLower = String(job.state || "").toLowerCase();
        const pctRaw = Number(job.percent);
        const hasUsefulPercent =
            Number.isFinite(pctRaw) &&
            pctRaw > 0 &&
            pctRaw < 100;

        if (stateLower === "done") {
            fill.style.setProperty("--pct", "100%");
        } else if (stateLower === "failed") {
            fill.style.setProperty("--pct", "100%");
            fill.classList.add("failed");
        } else if (hasUsefulPercent) {
            fill.style.setProperty("--pct", `${Math.max(0, Math.min(100, pctRaw))}%`);
        } else {
            fill.classList.add("indeterminate");
        }

        bar.appendChild(fill);
        item.appendChild(bar);

        const actions = document.createElement("div");
        actions.className = "adminUsersJobActions";

        const hideBtn = document.createElement("button");
        hideBtn.type = "button";
        hideBtn.className = "pq-btn secondary small";
        hideBtn.textContent = tr("admin.users.hide", null, "Hide");
        hideBtn.addEventListener("click", () => adminUsersRemoveStorageJob(job.kind, job.job_id));

        actions.appendChild(hideBtn);
        item.appendChild(actions);
        list.appendChild(item);
    }

    panel.appendChild(list);
}

async function adminUsersFetchStorageJobStatus(kind, jobId) {
    if (kind === "migration") return await apiGetMigrationStatus(jobId);
    if (kind === "cleanup") return await apiGetCleanupStatus(jobId);
    throw new Error("bad storage job kind");
}

function adminUsersStartStorageJobMonitor(kind, jobId, fp) {
    if (!adminUsersValidStorageJobKind(kind)) return;
    const safeJobId = adminUsersSafeJobId(jobId);
    const safeFp = adminUsersSafeFingerprint(fp);
    if (!safeJobId || !safeFp) return;

    const key = adminUsersStorageJobKey(kind, safeJobId);
    if (adminUsersStorageJobPolls.has(key)) return;

    const control = { stopped: false };
    adminUsersStorageJobPolls.set(key, control);

    (async () => {
        try {
            for (;;) {
                if (control.stopped) return;

                const j = await adminUsersFetchStorageJobStatus(kind, safeJobId);
                const job = j?.job || {};
                adminUsersUpdateStorageJobFromRecord(kind, safeJobId, safeFp, job);

                const state = String(job.state || "").toLowerCase();
                if (state === "done" || state === "failed") {
                    try { await refresh(); } catch (_) {}
                    return;
                }

                await new Promise(resolve => setTimeout(resolve, 5000));
            }
        } catch (e) {
            adminUsersUpsertStorageJob(kind, safeJobId, safeFp, {
                state: "status_error",
                message: String(e?.message || e),
            });
        } finally {
            adminUsersStorageJobPolls.delete(key);
            renderAdminUsersStorageJobPanel();
        }
    })();
}

function adminUsersRestoreStorageJobMonitors() {
    renderAdminUsersStorageJobPanel();

    for (const job of adminUsersStorageJobsLoad()) {
        const state = String(job.state || "").toLowerCase();
        if (state === "done" || state === "failed") continue;
        adminUsersStartStorageJobMonitor(job.kind, job.job_id, job.fingerprint);
    }
}



async function apiPost(path, body) {
    const r = await fetch(path, {
        method: "POST",
        headers: { "Content-Type": "application/json", "Accept": "application/json" },
        body: JSON.stringify(body),
        cache: "no-store",
    });
    const j = await r.json().catch(() => ({}));
    if (!r.ok || !j.ok) throw new Error(j.message || j.error || ("HTTP " + r.status));
    return j;
}


function adminUsersInstallConfirmThemeFix() {
    if (window.__adminUsersConfirmThemeFixInstalled) return;
    window.__adminUsersConfirmThemeFixInstalled = true;

    const installTokens = () => {
        if (document.getElementById("adminUsersConfirmTokenCss")) return;

        const style = document.createElement("style");
        style.id = "adminUsersConfirmTokenCss";
        style.textContent = `
:root{
    --admin-users-modal-scrim: color-mix(in oklab, var(--bg) 72%, transparent);
    --admin-users-modal-surface: color-mix(in oklab, var(--bg) 92%, var(--fg) 8%);
    --admin-users-modal-surface-2: color-mix(in oklab, var(--bg) 86%, var(--fg) 14%);
}
`;
        document.head.appendChild(style);
    };

    const applyOverlayTokens = (el) => {
        // Theme safety: keep backdrop dimmed but not opaque. Use only theme
        // tokens so dark/orange modes do not become a light gray page.
        el.style.background = "var(--admin-users-modal-scrim)";
        el.style.backgroundColor = "var(--admin-users-modal-scrim)";
        el.style.color = "var(--fg)";
        el.style.backdropFilter = "none";
        el.style.webkitBackdropFilter = "none";
    };

    const applyCardTokens = (el) => {
        // Theme safety: modal surface must be solid, because --panel is glassy
        // in dark/orange themes. This avoids text and table rows bleeding
        // through the confirm dialog.
        el.style.background = "var(--admin-users-modal-surface)";
        el.style.backgroundColor = "var(--admin-users-modal-surface)";
        el.style.color = "var(--fg)";
        el.style.border = "1px solid var(--border)";
        el.style.boxShadow = "var(--shadow)";
        el.style.backdropFilter = "none";
        el.style.webkitBackdropFilter = "none";
    };

    const applyInsetTokens = (el) => {
        el.style.background = "var(--admin-users-modal-surface-2)";
        el.style.backgroundColor = "var(--admin-users-modal-surface-2)";
        el.style.color = "var(--fg)";
        el.style.borderColor = "var(--border2)";
    };

    const looksLikeFullscreenOverlay = (el) => {
        if (!(el instanceof HTMLElement)) return false;

        const cs = window.getComputedStyle(el);
        if (cs.position !== "fixed") return false;

        const r = el.getBoundingClientRect();
        return r.width >= window.innerWidth * 0.75 &&
               r.height >= window.innerHeight * 0.75;
    };

    const looksLikeDialogCard = (el) => {
        if (!(el instanceof HTMLElement)) return false;

        if (el.getAttribute("role") === "dialog") return true;
        if (el.getAttribute("aria-modal") === "true") return true;

        const hint = `${el.id || ""} ${el.className || ""}`.toLowerCase();
        if (hint.includes("backdrop") || hint.includes("overlay")) return false;

        const hasNameHint =
            hint.includes("confirm") ||
            hint.includes("dialog") ||
            hint.includes("modal") ||
            hint.includes("card");

        const r = el.getBoundingClientRect();
        return hasNameHint &&
               r.width >= 260 &&
               r.width <= window.innerWidth * 0.92 &&
               r.height >= 120 &&
               r.height <= window.innerHeight * 0.92;
    };

    const normalizeRoot = (root) => {
        installTokens();

        const nodes = [];
        if (root instanceof HTMLElement) nodes.push(root);
        if (root && root.querySelectorAll) nodes.push(...root.querySelectorAll("*"));

        for (const el of nodes) {
            if (!(el instanceof HTMLElement)) continue;

            const hint = `${el.id || ""} ${el.className || ""}`.toLowerCase();
            const nameLooksModal =
                hint.includes("confirm") ||
                hint.includes("modal") ||
                hint.includes("dialog") ||
                hint.includes("backdrop") ||
                hint.includes("overlay");

            if (looksLikeFullscreenOverlay(el) && nameLooksModal) {
                applyOverlayTokens(el);
                continue;
            }

            if (looksLikeDialogCard(el)) {
                applyCardTokens(el);

                for (const sub of el.querySelectorAll("*")) {
                    const subHint = `${sub.id || ""} ${sub.className || ""}`.toLowerCase();

                    if (
                        subHint.includes("note") ||
                        subHint.includes("warning") ||
                        subHint.includes("row") ||
                        subHint.includes("body") ||
                        subHint.includes("content")
                    ) {
                        applyInsetTokens(sub);
                    }
                }
            }
        }
    };

    const observer = new MutationObserver((mutations) => {
        for (const m of mutations) {
            for (const n of m.addedNodes) normalizeRoot(n);
        }
    });

    observer.observe(document.body, {
        childList: true,
        subtree: true,
    });

    normalizeRoot(document.body);
}

function $(id) { return document.getElementById(id); }

const ADMIN_AVATAR_MAX_BYTES = 256 * 1024;
const ADMIN_AVATAR_TARGET_BYTES = 240 * 1024;
const ADMIN_AVATAR_MAX_DIM = 512;

function fmtBytesForAvatar(n) {
    const x = Number(n || 0);
    if (!Number.isFinite(x) || x <= 0) return "0 B";
    if (x < 1024) return `${x} B`;
    if (x < 1024 * 1024) return `${(x / 1024).toFixed(1)} KiB`;
    return `${(x / (1024 * 1024)).toFixed(2)} MiB`;
}

function blobToBase64(blob) {
    return new Promise((resolve, reject) => {
        const rd = new FileReader();

        rd.onload = () => {
            const s = String(rd.result || "");
            const comma = s.indexOf(",");
            resolve(comma >= 0 ? s.slice(comma + 1) : s);
        };

        rd.onerror = () => reject(new Error(tr("admin.users.failed_read_avatar", null, "failed to read avatar file")));
        rd.readAsDataURL(blob);
    });
}

function canvasToBlobSafe(canvas, type, quality) {
    return new Promise((resolve, reject) => {
        canvas.toBlob((blob) => {
            if (!blob) {
                reject(new Error(tr("admin.users.avatar_conversion_failed", null, "avatar conversion failed")));
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
            reject(new Error(tr("admin.users.avatar_read_failed", null, "Could not read this image. Try PNG, JPEG, or WebP.")));
        };

        img.src = url;
    });
}

async function prepareAdminAvatarUploadBlob(file) {
    if (!file) throw new Error(tr("admin.users.no_avatar_selected", null, "No avatar file selected."));

    const originalMime = String(file.type || "").toLowerCase();

    const directlyAllowed =
        originalMime === "image/png" ||
        originalMime === "image/jpeg" ||
        originalMime === "image/webp";

    if (directlyAllowed && file.size <= ADMIN_AVATAR_MAX_BYTES) {
        return {
            blob: file,
            mime: originalMime,
            note: tr("admin.users.using_original", { size: fmtBytesForAvatar(file.size) }, `Using original image (${fmtBytesForAvatar(file.size)}).`)
        };
    }

    const img = await loadImageForAvatar(file);

    const srcW = img.naturalWidth || img.width || 0;
    const srcH = img.naturalHeight || img.height || 0;

    if (!srcW || !srcH) {
        throw new Error(tr("admin.users.avatar_dims_failed", null, "Could not read image dimensions."));
    }

    const scale = Math.min(1, ADMIN_AVATAR_MAX_DIM / Math.max(srcW, srcH));
    const dstW = Math.max(1, Math.round(srcW * scale));
    const dstH = Math.max(1, Math.round(srcH * scale));

    const canvas = document.createElement("canvas");
    canvas.width = dstW;
    canvas.height = dstH;

    const ctx = canvas.getContext("2d", { alpha: false });
    if (!ctx) throw new Error("Canvas is not available for avatar resize.");

    ctx.fillStyle = "#ffffff";
    ctx.fillRect(0, 0, dstW, dstH);
    ctx.drawImage(img, 0, 0, dstW, dstH);

    const qualities = [0.86, 0.78, 0.70, 0.62, 0.54, 0.46, 0.38];

    let best = null;

    for (const q of qualities) {
        const blob = await canvasToBlobSafe(canvas, "image/jpeg", q);
        best = blob;

        if (blob.size <= ADMIN_AVATAR_TARGET_BYTES) {
            return {
                blob,
                mime: "image/jpeg",
                note: tr("admin.users.resized_avatar", { srcW, srcH, dstW, dstH, oldSize: fmtBytesForAvatar(file.size), newSize: fmtBytesForAvatar(blob.size) }, `Resized ${srcW}×${srcH} → ${dstW}×${dstH}, ${fmtBytesForAvatar(file.size)} → ${fmtBytesForAvatar(blob.size)}.`)
            };
        }
    }

    if (best && best.size <= ADMIN_AVATAR_MAX_BYTES) {
        return {
            blob: best,
            mime: "image/jpeg",
            note: tr("admin.users.resized_avatar", { srcW, srcH, dstW, dstH, oldSize: fmtBytesForAvatar(file.size), newSize: fmtBytesForAvatar(best.size) }, `Resized ${srcW}×${srcH} → ${dstW}×${dstH}, ${fmtBytesForAvatar(file.size)} → ${fmtBytesForAvatar(best.size)}.`)
        };
    }

    throw new Error(
        tr("admin.users.avatar_too_large", { size: fmtBytesForAvatar(best ? best.size : file.size) }, `Avatar is still too large after resizing (${fmtBytesForAvatar(best ? best.size : file.size)}). Try a smaller image.`)
    );
}

function badge(variant, text, title = "") {
    const v = String(variant || "").trim();
    const cls = v ? `pq-badge ${v}` : "pq-badge";
    const t = title ? ` title="${esc(title)}"` : "";
    return `<span class="${cls}"${t}>${esc(text || "")}</span>`;
}

function pill(status) {
    const s = String(status || "disabled").toLowerCase();
    const variant =
        s === "enabled" ? "ok" :
        s === "disabled" ? "warn" :
        s === "revoked" ? "err" :
        "muted";

    return badge(variant, statusLabel(s));
}

function rolePill(role) {
    const r = String(role || "").toLowerCase();
    if (!r) return `<span class="muted">—</span>`;

    const variant =
        r === "admin" ? "info" :
        r === "user" ? "muted" :
        "muted";

    return badge(variant, roleLabel(r));
}

function groupPill(group) {
    const g = String(group || "").trim();
    if (!g) return `<span class="muted">—</span>`;
    return badge("muted", g);
}

function poolPill(poolId) {
    const p = String(poolId || "").trim();
    if (!p) return "";
    return badge("info", p);
}

function esc(s) {
    return (s || "").replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
}

function avatarSrc(u) {
    const s = String(u?.avatar_url || "").trim();
    return s || "";
}

function fmtGBFromBytes(b) {
    const n = Number(b || 0);
    if (!isFinite(n) || n <= 0) return "";
    const gb = n / (1024 * 1024 * 1024);
    // keep it simple: show up to 2 decimals, trim trailing zeros
    return (Math.round(gb * 100) / 100).toString();
}

function fmtQuotaCell(u) {
    const st = String(u.storage_state || "unallocated").toLowerCase();
    if (st !== "allocated") return `<span class="muted">—</span>`;

    const quotaBytes = Number(u.quota_bytes ?? 0);
    const usedBytes = Number(u.used_bytes ?? u.storage_used_bytes ?? 0);

    if (!Number.isFinite(quotaBytes) || quotaBytes <= 0) {
        return `<span class="muted">0</span>`;
    }

    const text = quotaUsageText(
        Number.isFinite(usedBytes) ? usedBytes : 0,
        quotaBytes
    );

    return `<span title="${esc(text)}">${esc(text)}</span>`;
}
function shortFp(fp) {
    fp = String(fp || "");
    if (fp.length <= 20) return fp;
    return fp.slice(0, 12) + "…" + fp.slice(-12);
}
function avatarThumb(u) {
    const src = avatarSrc(u);
    if (!src) return `<div class="muted">—</div>`;
    return `
      <img
        src="${esc(src)}"
        alt="${esc(tr("admin.users.avatar_alt", null, "avatar"))}"
        style="width:26px;height:26px;border-radius:8px;object-fit:cover;border:1px solid var(--border);background:var(--panel2);"
        title="Avatar"
        onerror="this.style.opacity='0.35'; this.title='${esc(tr("admin.users.avatar_failed", null, "Avatar failed to load"))}';"
      />
    `.trim();
}
let avatarModalFp = "";
let avatarModalUrl = "";

function openAvatarModal(fp, url) {
    avatarModalFp = String(fp || "");
    avatarModalUrl = String(url || "");

    const m = $("avatarModal");
    const img = $("avatarModalImg");
    const rm = $("avatarRemoveBtn");

    if (!m || !img || !rm) return;

    img.src = avatarModalUrl || "";
    rm.disabled = !avatarModalFp;

    m.classList.add("open");
    m.setAttribute("aria-hidden", "false");
}

function closeAvatarModal() {
    const m = $("avatarModal");
    const img = $("avatarModalImg");
    if (img) img.src = ""; // stop loading / free memory
    avatarModalFp = "";
    avatarModalUrl = "";

    if (m) {
        m.classList.remove("open");
        m.setAttribute("aria-hidden", "true");
    }
}

function storagePill(state) {
    const s = String(state || "unallocated").toLowerCase();
    const variant = s === "allocated" ? "ok" : "muted";
    return badge(variant, storageStateLabel(s));
}
function storagePoolIdForUser(u) {
    const raw =
        (u?.pool_id != null ? String(u.pool_id) :
            (u?.pool != null ? String(u.pool) :
                (u?.storage_pool_id != null ? String(u.storage_pool_id) : "")));

    const v = raw.trim();
    return v ? v : "default";
}

function storageStateCellHtml(u) {
    const state = String(u?.storage_state || "unallocated").toLowerCase();
    return storagePill(state);
}

function storagePoolCellHtml(u) {
    const state = String(u?.storage_state || "unallocated").toLowerCase();
    if (state !== "allocated") return `<span class="muted">—</span>`;

    const poolId = storagePoolIdForUser(u);
    return poolPill(poolId);
}

function storageCellHtml(u) {
    const state = String(u?.storage_state || "unallocated").toLowerCase();
    const main = storageStateCellHtml(u);

    if (state !== "allocated") return main;

    return `${main} ${storagePoolCellHtml(u)}`;
}
function fmtBytes(n) {
    n = Number(n || 0);
    const units = ["B","KiB","MiB","GiB","TiB"];
    let u = 0;
    while (n >= 1024 && u < units.length - 1) { n /= 1024; u++; }
    return `${n.toFixed(u === 0 ? 0 : 2)} ${units[u]}`;
}

function showToast(msg, ms = 10000) {
    let t = document.getElementById("toast");
    if (!t) {
        t = document.createElement("div");
        t.id = "toast";
        t.style.position = "fixed";
        t.style.right = "18px";
        t.style.bottom = "18px";
        t.style.zIndex = "99999";

        /* sizing */
        t.style.maxWidth = "520px";
        t.style.width = "min(520px, calc(100vw - 36px))";

        t.style.padding = "12px 14px";
        t.style.borderRadius = "12px";
        t.style.border = "1px solid var(--border)";
        t.style.background = "linear-gradient(180deg, var(--panel2), var(--panel))";
        t.style.color = "var(--fg)";
        t.style.boxShadow = "var(--shadow)";
        t.style.font = "14px system-ui, -apple-system, Segoe UI, Roboto, sans-serif";

        /* wrapping + scroll */
        t.style.whiteSpace = "pre-wrap";
        t.style.wordBreak = "break-all";
        t.style.overflowWrap = "anywhere";
        t.style.maxHeight = "60vh";
        t.style.overflow = "auto";

        /* allow selection */
        t.style.userSelect = "text";
        t.style.cursor = "text";

        t.style.display = "none";
        document.body.appendChild(t);
    }

    t.textContent = msg;
    t.onclick = () => {
        navigator.clipboard.writeText(msg).catch(() => {});
    };

    t.style.display = "block";
    clearTimeout(t._hideTimer);
    t._hideTimer = setTimeout(() => { t.style.display = "none"; }, ms);
}

function gbToBytes(gb) {
    const x = Number(gb);
    if (!isFinite(x) || x < 0) return null;
    // allow 0
    return Math.floor(x * 1024 * 1024 * 1024);
}

function setAllocError(msg) {
    const el = $("allocErr");
    if (!el) return;
    if (!msg) {
        el.textContent = "";
        el.classList.remove("show");
        return;
    }
    el.textContent = String(msg);
    el.classList.add("show");
}
function renderAllocPreview(preview, requestedQuotaBytes = null) {
    const box = $("allocPreview");
    if (!box) return;

    if (!preview || !preview.ok) {
        box.innerHTML = `<div class="muted">${esc(tr("admin.users.no_pool_preview", null, "No pool preview available."))}</div>`;
        return;
    }

    const used = Number(preview.used_bytes || 0);
    const currentQuota = Number(preview.current_quota_bytes || 0);
    const poolTotal = Number(preview.pool_total_bytes || 0);
    const poolFree = Number(preview.pool_free_bytes || 0);
    const allocatedOther = Number(preview.allocated_other_bytes || 0);
    const remainingAlloc = Number(preview.remaining_allocatable_bytes || 0);

    const rq = Number(requestedQuotaBytes);
    const haveRq = Number.isFinite(rq) && rq >= 0;

    const overAlloc = haveRq && rq > remainingAlloc;
    const belowUsed = haveRq && rq < used;

    const warnHtml = (overAlloc || belowUsed)
        ? `
          <div class="allocPreviewWarn">
            ${belowUsed ? `${esc(tr("admin.users.warn_below_used", null, "Requested quota is below current used space."))}` : ``}
            ${belowUsed && overAlloc ? `<br>` : ``}
            ${overAlloc ? `${esc(tr("admin.users.warn_over_alloc", null, "Requested quota exceeds remaining allocatable capacity on this pool."))}` : ``}
          </div>
        `
        : ``;

    box.innerHTML = `
      <div class="allocPreviewGrid">
        <div class="detailKV"><div class="k">${esc(tr("admin.users.user_used", null, "User used"))}</div><div class="v mono">${esc(fmtBytes(used))}</div></div>
        <div class="detailKV"><div class="k">${esc(tr("admin.users.current_quota", null, "Current quota"))}</div><div class="v mono">${esc(currentQuota ? fmtBytes(currentQuota) : "—")}</div></div>
        <div class="detailKV"><div class="k">${esc(tr("admin.users.pool_total", null, "Pool total"))}</div><div class="v mono">${esc(fmtBytes(poolTotal))}</div></div>
        <div class="detailKV"><div class="k">${esc(tr("admin.users.pool_free", null, "Pool free (fs)"))}</div><div class="v mono">${esc(fmtBytes(poolFree))}</div></div>
        <div class="detailKV"><div class="k">${esc(tr("admin.users.allocated_to_others", null, "Allocated to others"))}</div><div class="v mono">${esc(fmtBytes(allocatedOther))}</div></div>
        <div class="detailKV"><div class="k">${esc(tr("admin.users.remaining_allocatable", null, "Remaining allocatable"))}</div><div class="v mono">${esc(fmtBytes(remainingAlloc))}</div></div>
        ${haveRq ? `<div class="detailKV"><div class="k">${esc(tr("admin.users.requested_quota", null, "Requested quota"))}</div><div class="v mono">${esc(fmtBytes(rq))}</div></div>` : ``}
      </div>
      ${warnHtml}
    `;
}
function normalizePoolsFromResponse(j) {
    const arr = Array.isArray(j?.pools) ? j.pools : [];

    const out = [];
    for (const p of arr) {
        if (!p || typeof p !== "object") continue;

        const rawId = String(p.pool_id || "").trim();
        if (!rawId) continue;

        const mount = String(p.mount || "").trim();
        const disp = String(p.display_name || "").trim();

        const isDefault =
            rawId === "default" ||
            mount === "/srv/pqnas" ||
            mount === "/srv/pqnas/data";

        const id = isDefault ? "default" : rawId;
        const name = isDefault ? tr("admin.users.default_pool", null, "Default pool") : (disp || rawId);

        const hintParts = [];
        if (mount) hintParts.push(mount);
        if (p.profile_data) hintParts.push(`data:${String(p.profile_data)}`);
        if (p.profile_metadata) hintParts.push(`meta:${String(p.profile_metadata)}`);
        const hint = hintParts.join(" • ");

        const total_bytes = adminUsersPoolCapacityNumber(p, [
            "total_bytes",
            "pool_total_bytes",
            "capacity_bytes",
            "bytes_total",
            "size_bytes",
            "fs_total_bytes",
            "stat_total_bytes",
        ]);

        const free_bytes = adminUsersPoolCapacityNumber(p, [
            "free_bytes",
            "pool_free_bytes",
            "available_bytes",
            "avail_bytes",
            "bytes_available",
            "usable_free_bytes",
            "fs_free_bytes",
            "stat_free_bytes",
        ]);

        const used_bytes = adminUsersPoolCapacityNumber(p, [
            "used_bytes",
            "pool_used_bytes",
            "bytes_used",
            "allocated_bytes",
            "quota_used_bytes",
            "assigned_quota_bytes",
        ]);

        const remaining_allocatable_bytes = adminUsersPoolCapacityNumber(p, [
            "remaining_allocatable_bytes",
            "pool_remaining_allocatable_bytes",
            "remaining_bytes",
            "remaining_quota_bytes",
            "quota_remaining_bytes",
            "quota_available_bytes",
            "available_quota_bytes",
            "allocatable_quota_bytes",
        ]);

        out.push({
            ...p,
            id,
            name,
            hint,
            mount,
            total_bytes,
            free_bytes,
            used_bytes,
            remaining_allocatable_bytes,
        });
    }
    return out;
}

async function apiGetPoolsBestEffort() {
    // Prefer raidmgr pools endpoint (most likely already exists)
    const candidates = [
        "/api/v4/storage/pools",
        "/api/v4/raid/pools",
        "/api/v4/pools",
        "/api/v4/admin/pools",
    ];

    let lastErr = null;
    for (const url of candidates) {
        try {
            const j = await apiGet(url);
            return normalizePoolsFromResponse(j);
        } catch (e) {
            lastErr = e;
        }
    }
    console.warn("Pools load failed, falling back to default pool:", lastErr?.message || lastErr);
    return [];
}

async function ensurePoolsLoaded() {
    const pools = await apiGetPoolsBestEffort();
    const out = Array.isArray(pools) ? [...pools] : [];

    if (!out.some(p => String(p.id) === "default")) {
        out.unshift({
            id: "default",
            name: "Default pool",
            hint: "/srv/pqnas/data"
        });
    }

    gPools = out;
    return gPools;
}
async function refreshAllocPreview() {
    const fp = gAllocFp;
    const poolSel = $("allocPoolSel");
    const gbInp = $("allocGb");

    if (!fp || !poolSel) return;

    const poolId = String(poolSel.value || "default");
    const quotaGb = Number(String(gbInp?.value || "").trim());
    const requestedQuotaBytes = isFinite(quotaGb) && quotaGb >= 0 ? gbToBytes(quotaGb) : null;

    const j = await apiGetStoragePreview(fp, poolId);
    gAllocPreview = j;
    renderAllocPreview(j, requestedQuotaBytes);
}
function openAllocModal(fp, curUser) {
    gAllocFp = String(fp || "");
    gAllocForce = false;

    const m = $("allocModal");
    const fpLabel = $("allocFpLabel");
    const poolSel = $("allocPoolSel");
    const poolHint = $("allocPoolHint");
    const gbInp = $("allocGb");

    if (!m || !fpLabel || !poolSel || !poolHint || !gbInp) return;

    setAllocError("");

    fpLabel.textContent = gAllocFp || "—";

    const suggested = fmtGBFromBytes(curUser?.quota_bytes) || "10";
    gbInp.value = suggested;

    m.classList.add("open");
    m.setAttribute("aria-hidden", "false");

    (async () => {
        const pools = await ensurePoolsLoaded();

        poolSel.innerHTML = "";
        for (const p of pools) {
            const opt = document.createElement("option");
            opt.value = p.id;
            opt.textContent = p.name;
            poolSel.appendChild(opt);
        }

        const curPool =
            (curUser?.pool_id != null ? String(curUser.pool_id) :
                (curUser?.pool != null ? String(curUser.pool) :
                    (curUser?.storage_pool_id != null ? String(curUser.storage_pool_id) :
                        "")));

        if (curPool) {
            const match = Array.from(poolSel.options).find(o => o.value === curPool);
            if (match) poolSel.value = curPool;
        }

        const selected = pools.find(x => x.id === poolSel.value) || pools[0];
        poolHint.textContent = selected?.hint ? selected.hint : "—";

        poolSel.onchange = async () => {
            const s = (gPools || []).find(x => x.id === poolSel.value);
            poolHint.textContent = s?.hint ? s.hint : "—";
            try {
                await refreshAllocPreview();
            } catch (e) {
                setAllocError(tr("admin.users.pool_preview_refresh_failed", { error: e?.message || e }, "Failed to refresh pool preview: " + (e?.message || e)));
            }
        };

        gbInp.addEventListener("input", () => {
            const quotaGb = Number(String(gbInp.value || "").trim());
            const requestedQuotaBytes = isFinite(quotaGb) && quotaGb >= 0 ? gbToBytes(quotaGb) : null;
            renderAllocPreview(gAllocPreview, requestedQuotaBytes);
        }, { passive: true });

        try {
            await refreshAllocPreview();
        } catch (e) {
            setAllocError(tr("admin.users.pool_preview_load_failed", { error: e?.message || e }, "Failed to load pool preview: " + (e?.message || e)));
        }

        gbInp.focus();
        gbInp.select();

    })().catch(e => {
        setAllocError(tr("admin.users.pools_load_failed", { error: e?.message || e }, "Failed to load pools: " + (e?.message || e)));
    });
}


function closeAllocModal() {
    const m = $("allocModal");
    if (!m) return;
    m.classList.remove("open");
    m.setAttribute("aria-hidden", "true");
    setAllocError("");
    gAllocFp = "";
    gAllocPreview = null;

    const box = $("allocPreview");
    if (box) {
        box.innerHTML = `<div class="muted">${esc(tr("admin.users.loading_pool_preview", null, "Loading pool preview…"))}</div>`;
    }
}

function setMigrateError(msg) {
    const el = $("migrateErr");
    if (!el) return;
    if (!msg) {
        el.textContent = "";
        el.classList.remove("show");
        return;
    }
    el.textContent = String(msg);
    el.classList.add("show");
}



function currentPoolNameFromList(poolId, pools) {
    const p = (pools || []).find(x => x.id === poolId);
    return p?.name || poolId || "default";
}

function openMigrateModal(fp, curUser) {
    gMigrateFp = String(fp || "");
    gPools = null;
    const m = $("migrateModal");
    const fpLabel = $("migrateFpLabel");
    const curPoolInp = $("migrateCurPool");
    const curHint = $("migrateCurHint");
    const poolSel = $("migratePoolSel");
    const poolHint = $("migratePoolHint");

    if (!m || !fpLabel || !curPoolInp || !curHint || !poolSel || !poolHint) return;

    setMigrateError("");
    fpLabel.textContent = gMigrateFp || "—";

    m.classList.add("open");
    m.setAttribute("aria-hidden", "false");

    (async () => {
        const pools = await ensurePoolsLoaded();
        const curPoolId = storagePoolIdForUser(curUser);

        curPoolInp.value = currentPoolNameFromList(curPoolId, pools);
        const curPoolObj = pools.find(x => x.id === curPoolId);
        curHint.textContent = curPoolObj?.hint || "—";

        const candidates = pools.filter(p => p.id !== curPoolId);


        poolSel.innerHTML = "";
        if (candidates.length === 0) {
            const opt = document.createElement("option");
            opt.value = "";
            opt.textContent = tr("admin.users.no_other_pools", null, "No other pools available");
            poolSel.appendChild(opt);
            poolSel.disabled = true;
            poolHint.textContent = tr("admin.users.create_pool_first", null, "Create another pool first.");
            return;
        }

        poolSel.disabled = false;
        for (const p of candidates) {
            const opt = document.createElement("option");
            opt.value = p.id;
            opt.textContent = p.name;
            poolSel.appendChild(opt);
        }

        const selected = candidates[0];
        poolSel.value = selected.id;
        poolHint.textContent = selected?.hint || "—";

        poolSel.onchange = () => {
            const s = candidates.find(x => x.id === poolSel.value);
            poolHint.textContent = s?.hint || "—";
        };
    })().catch(e => {
        setMigrateError(tr("admin.users.pools_load_failed", { error: e?.message || e }, "Failed to load pools: " + (e?.message || e)));
    });
}

function closeMigrateModal() {
    const m = $("migrateModal");
    if (!m) return;
    m.classList.remove("open");
    m.setAttribute("aria-hidden", "true");
    setMigrateError("");
    gMigrateFp = "";
}
async function apiGetMigrationStatus(jobId) {
    const q = encodeURIComponent(String(jobId || "").trim());
    return await apiGet(`/api/v4/admin/users/migrate_storage_status?job_id=${q}`);
}
async function apiGetStoragePreview(fp, poolId) {
    const qfp = encodeURIComponent(String(fp || "").trim());
    const qpool = encodeURIComponent(String(poolId || "default").trim() || "default");
    return await apiGet(`/api/v4/admin/users/storage_preview?fingerprint=${qfp}&pool_id=${qpool}`);
}

function injectAdminUsersPromptCss() {
    if (document.getElementById("adminUsersPromptCss")) return;

    const style = document.createElement("style");
    style.id = "adminUsersPromptCss";
    style.textContent = `
.adminUsersPromptBackdrop{
    position:fixed;
    inset:0;
    z-index:100000;
    display:flex;
    align-items:center;
    justify-content:center;
    padding:18px;
    background:rgba(var(--fg-rgb),0.38);
    backdrop-filter:blur(6px);
    -webkit-backdrop-filter:blur(6px);
}
.adminUsersPromptCard{
    width:min(660px, calc(100vw - 24px));
    max-height:min(84vh, 900px);
    display:flex;
    flex-direction:column;
    overflow:hidden;
    border:1px solid var(--border2);
    border-radius:18px;
    background:linear-gradient(180deg, var(--panel2), var(--panel));
    box-shadow:var(--shadow);
    color:var(--fg);
}
.adminUsersPromptHead{
    padding:14px 16px;
    border-bottom:1px solid var(--border2);
    background:var(--panel2);
}
.adminUsersPromptTitle{
    font-weight:950;
    letter-spacing:.2px;
    font-size:16px;
    color:var(--fg);
}
.adminUsersPromptSub{
    margin-top:4px;
    font-size:12px;
    color:var(--fg-dim);
}
.adminUsersPromptBody{
    padding:16px;
    display:grid;
    grid-template-columns:140px minmax(0, 1fr);
    gap:10px 14px;
    overflow:auto;
    min-height:0;
}
.adminUsersPromptKey{
    color:var(--fg-dim);
    font-weight:850;
}
.adminUsersPromptValue{
    color:var(--fg);
    overflow-wrap:anywhere;
    white-space:pre-wrap;
}
.adminUsersPromptValue.mono{
    font-family:var(--mono, ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace);
    font-size:12px;
}
.adminUsersPromptInput{
    width:100%;
    padding:10px 12px;
    border-radius:12px;
    border:1px solid var(--border2);
    background:var(--panel2);
    color:var(--fg);
    font:inherit;
    font-family:var(--mono, ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace);
}
.adminUsersPromptNote{
    grid-column:1 / -1;
    padding:10px 12px;
    border:1px solid rgba(var(--warn-rgb),0.35);
    border-radius:14px;
    background:rgba(var(--warn-rgb),0.10);
    color:var(--fg);
    font-weight:850;
}
.adminUsersPromptErr{
    grid-column:1 / -1;
    display:none;
    padding:8px 10px;
    border:1px solid rgba(var(--fail-rgb),0.35);
    border-radius:12px;
    background:rgba(var(--fail-rgb),0.10);
    color:var(--fg);
    font-weight:850;
}
.adminUsersPromptFoot{
    display:flex;
    align-items:center;
    gap:12px;
    padding:12px 16px;
    border-top:1px solid var(--border2);
    background:var(--panel2);
}
.adminUsersPromptBtn{
    border:1px solid var(--border2);
    border-radius:14px;
    padding:9px 14px;
    font:inherit;
    font-weight:850;
    color:var(--fg);
    background:var(--panel);
    cursor:pointer;
}
.adminUsersPromptBtn.warn{
    border-color:rgba(var(--warn-rgb),0.48);
    background:rgba(var(--warn-rgb),0.16);
}
.adminUsersPromptBtn.danger{
    border-color:rgba(var(--fail-rgb),0.48);
    background:rgba(var(--fail-rgb),0.14);
}
`;
    document.head.appendChild(style);
}

function openAdminUsersPromptModal(opts = {}) {
    injectAdminUsersPromptCss();

    return new Promise((resolve) => {
        const options = opts || {};

        const modal = document.createElement("div");
        modal.className = "adminUsersPromptBackdrop";
        modal.setAttribute("role", "dialog");
        modal.setAttribute("aria-modal", "true");

        const card = document.createElement("div");
        card.className = "adminUsersPromptCard";

        const head = document.createElement("div");
        head.className = "adminUsersPromptHead";

        const title = document.createElement("div");
        title.className = "adminUsersPromptTitle";
        title.textContent = options.title || tr("admin.users.prompt.enter_value", null, "Enter value");

        const sub = document.createElement("div");
        sub.className = "adminUsersPromptSub";
        sub.textContent = options.subtitle || "";

        head.appendChild(title);
        if (sub.textContent) head.appendChild(sub);

        const body = document.createElement("div");
        body.className = "adminUsersPromptBody";

        for (const row of Array.isArray(options.rows) ? options.rows : []) {
            const k = document.createElement("div");
            k.className = "adminUsersPromptKey";
            k.textContent = String(row.label || "");

            const v = document.createElement("div");
            v.className = row.mono ? "adminUsersPromptValue mono" : "adminUsersPromptValue";
            v.textContent = String(row.value || "");

            body.appendChild(k);
            body.appendChild(v);
        }

        const label = document.createElement("label");
        label.className = "adminUsersPromptKey";
        label.textContent = options.label || tr("admin.users.prompt.value", null, "Value");

        const input = document.createElement("input");
        input.type = "text";
        input.className = "adminUsersPromptInput";
        input.value = options.value || "";
        input.placeholder = options.placeholder || "";
        input.autocomplete = "off";
        input.spellcheck = false;

        body.appendChild(label);
        body.appendChild(input);

        if (options.note) {
            const note = document.createElement("div");
            note.className = "adminUsersPromptNote";
            note.textContent = String(options.note || "");
            body.appendChild(note);
        }

        const err = document.createElement("div");
        err.className = "adminUsersPromptErr";
        body.appendChild(err);

        const foot = document.createElement("div");
        foot.className = "adminUsersPromptFoot";

        const spacer = document.createElement("div");
        spacer.style.flex = "1 1 auto";

        const cancelBtn = document.createElement("button");
        cancelBtn.type = "button";
        cancelBtn.className = "adminUsersPromptBtn";
        cancelBtn.textContent = options.cancelText || tr("admin.users.cancel", null, "Cancel");

        const okBtn = document.createElement("button");
        okBtn.type = "button";
        okBtn.className = options.warn ? "adminUsersPromptBtn warn" : "adminUsersPromptBtn";
        okBtn.textContent = options.confirmText || tr("admin.users.ok", null, "OK");

        foot.appendChild(spacer);
        foot.appendChild(cancelBtn);
        foot.appendChild(okBtn);

        card.appendChild(head);
        card.appendChild(body);
        card.appendChild(foot);
        modal.appendChild(card);
        document.body.appendChild(modal);

        const showError = (text) => {
            err.textContent = text || "";
            err.style.display = text ? "block" : "none";
        };

        const finish = (value) => {
            document.removeEventListener("keydown", onKey, true);
            modal.remove();
            resolve(value);
        };

        const submit = () => {
            const value = String(input.value || "").trim();

            if (options.required !== false && !value) {
                showError(tr("admin.users.prompt.value_required", null, "Value is required."));
                input.focus();
                return;
            }

            if (typeof options.validate === "function") {
                const msg = options.validate(value);
                if (msg) {
                    showError(msg);
                    input.focus();
                    input.select();
                    return;
                }
            }

            finish(value);
        };

        const onKey = (ev) => {
            if (ev.key === "Escape") {
                ev.preventDefault();
                ev.stopPropagation();
                finish(null);
                return;
            }

            if (ev.key === "Enter") {
                ev.preventDefault();
                ev.stopPropagation();
                submit();
            }
        };

        document.addEventListener("keydown", onKey, true);

        modal.addEventListener("click", (ev) => {
            if (ev.target === modal) finish(null);
        });

        cancelBtn.addEventListener("click", () => finish(null));
        okBtn.addEventListener("click", submit);

        window.setTimeout(() => {
            input.focus();
            input.select();
        }, 0);
    });
}



function openAdminUsersConfirmModal(opts = {}) {
    injectAdminUsersPromptCss();

    return new Promise((resolve) => {
        const options = opts || {};

        const modal = document.createElement("div");
        modal.className = "adminUsersPromptBackdrop";
        modal.setAttribute("role", "dialog");
        modal.setAttribute("aria-modal", "true");

        const card = document.createElement("div");
        card.className = "adminUsersPromptCard";

        const head = document.createElement("div");
        head.className = "adminUsersPromptHead";

        const title = document.createElement("div");
        title.className = "adminUsersPromptTitle";
        title.textContent = options.title || tr("admin.users.confirm_action", null, "Confirm action");

        const sub = document.createElement("div");
        sub.className = "adminUsersPromptSub";
        sub.textContent = options.subtitle || "";

        head.appendChild(title);
        if (sub.textContent) head.appendChild(sub);

        const body = document.createElement("div");
        body.className = "adminUsersPromptBody";

        for (const row of Array.isArray(options.rows) ? options.rows : []) {
            const k = document.createElement("div");
            k.className = "adminUsersPromptKey";
            k.textContent = String(row.label || "");

            const v = document.createElement("div");
            v.className = row.mono ? "adminUsersPromptValue mono" : "adminUsersPromptValue";
            v.textContent = String(row.value || "");

            body.appendChild(k);
            body.appendChild(v);
        }

        if (options.note) {
            const note = document.createElement("div");
            note.className = "adminUsersPromptNote";
            note.textContent = String(options.note || "");
            body.appendChild(note);
        }

        const foot = document.createElement("div");
        foot.className = "adminUsersPromptFoot";

        const spacer = document.createElement("div");
        spacer.style.flex = "1 1 auto";

        const cancelBtn = document.createElement("button");
        cancelBtn.type = "button";
        cancelBtn.className = "adminUsersPromptBtn";
        cancelBtn.textContent = options.cancelText || tr("admin.users.cancel", null, "Cancel");

        const okBtn = document.createElement("button");
        okBtn.type = "button";
        okBtn.className = options.danger ? "adminUsersPromptBtn danger" : (options.warn ? "adminUsersPromptBtn warn" : "adminUsersPromptBtn");
        okBtn.textContent = options.confirmText || tr("admin.users.ok", null, "OK");

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
            if (options.danger) cancelBtn.focus();
            else okBtn.focus();
        }, 0);
    });
}


async function apiGetCleanupStatus(jobId) {
    const q = encodeURIComponent(String(jobId || "").trim());
    return await apiGet(`/api/v4/admin/users/cleanup_old_storage_status?job_id=${q}`);
}

function kvLine(key, value) {
    return `${key}: ${value}`;
}

function fmtMigText(job) {
    const state = String(job?.state || "unknown");
    const phase = String(job?.phase || "");
    const percent = Number(job?.percent);
    const msg = String(job?.message || "");

    let out = kvLine(tr("admin.users.state", null, "State"), state);
    if (phase) out += "\n" + kvLine(tr("admin.users.phase", null, "Phase"), phase);
    if (Number.isFinite(percent)) out += "\n" + kvLine(tr("admin.users.progress", null, "Progress"), `${percent}%`);
    if (msg) out += "\n" + kvLine(tr("admin.users.message", null, "Message"), msg);

    const src = job?.resolved_source_pool_id || "default";
    const dst = job?.resolved_dest_pool_id || job?.requested_target_pool_id || "default";

    if (src) out += "\n" + kvLine(tr("admin.users.from", null, "From"), src);
    if (dst) out += "\n" + kvLine(tr("admin.users.to", null, "To"), dst);

    if (job?.error) out += "\n" + kvLine(tr("admin.users.error", null, "Error"), job.error);

    return out;
}

async function pollMigrationJob(jobId, fp) {
    let lastShownState = "";

    for (;;) {
        const j = await apiGetMigrationStatus(jobId);
        const job = j?.job || {};

        adminUsersUpdateStorageJobFromRecord("migration", jobId, fp, job);
        const state = String(job.state || "");
        const phase = String(job.phase || "");
        const percent = Number(job.percent);

        const progressBits = [];
        if (phase) progressBits.push(phase);
        if (Number.isFinite(percent)) progressBits.push(`${percent}%`);
        setMsg(progressBits.length ? tr("admin.users.migration_progress", { progress: progressBits.join(" · ") }, `Migration ${progressBits.join(" · ")}`) : tr("admin.users.migration_state", { state: state || "running" }, `Migration ${state || "running"}…`));

        // Optional small toast on first visible transition
        const stateKey = `${state}:${phase}:${percent}`;
        if (lastShownState !== stateKey && (state === "queued" || state === "running")) {
            lastShownState = stateKey;
        }

        if (state === "done") {
            closeMigrateModal();
            await refresh();
            showToast(tr("admin.users.migration_completed_toast", { details: fmtMigText(job) }, "Storage migration completed\n" + fmtMigText(job)));
            setMsg(tr("admin.users.migration_completed", null, "Migration completed"));
            return;
        }

        if (state === "failed") {
            await refresh();
            const text = fmtMigText(job);
            setMigrateError(job?.message || job?.error || tr("admin.users.migration_failed", null, "Migration failed"));
            showToast(tr("admin.users.migration_failed_toast", { details: text }, "Storage migration failed\n" + text), 15000);
            setMsg(tr("admin.users.migration_failed", null, "Migration failed"));
            return;
        }

        await new Promise(resolve => setTimeout(resolve, 3000));
    }
}

function fmtCleanupText(job) {
    const state = String(job?.state || "unknown");
    const phase = String(job?.phase || "");
    const percent = Number(job?.percent);
    const msg = String(job?.message || "");

    let out = kvLine(tr("admin.users.state", null, "State"), state);
    if (phase) out += "\n" + kvLine(tr("admin.users.phase", null, "Phase"), phase);
    if (Number.isFinite(percent)) out += "\n" + kvLine(tr("admin.users.progress", null, "Progress"), `${percent}%`);
    if (msg) out += "\n" + kvLine(tr("admin.users.message", null, "Message"), msg);

    const activePool = job?.resolved_active_pool_id || job?.expected_active_pool_id || "default";
    const oldPool = job?.resolved_old_pool_id || job?.old_pool_id || "?";

    out += "\n" + kvLine(tr("admin.users.active_pool", null, "Active pool"), activePool);
    out += "\n" + kvLine(tr("admin.users.old_pool", null, "Old pool"), oldPool);

    if (job?.result?.removed_entries != null) {
        out += "\n" + kvLine(tr("admin.users.removed_entries", null, "Removed entries"), job.result.removed_entries);
    }

    if (job?.error) out += "\n" + kvLine(tr("admin.users.error", null, "Error"), job.error);
    return out;
}

async function pollCleanupJob(jobId, fp) {

    for (;;) {
        const j = await apiGetCleanupStatus(jobId);
        const job = j?.job || {};

        adminUsersUpdateStorageJobFromRecord("cleanup", jobId, fp, job);
        const state = String(job.state || "");
        const phase = String(job.phase || "");
        const percent = Number(job.percent);

        const progressBits = [];
        if (phase) progressBits.push(phase);
        if (Number.isFinite(percent)) progressBits.push(`${percent}%`);
        setMsg(progressBits.length ? tr("admin.users.cleanup_progress", { progress: progressBits.join(" · ") }, `Cleanup ${progressBits.join(" · ")}`) : tr("admin.users.cleanup_state", { state: state || "running" }, `Cleanup ${state || "running"}…`));

        if (state === "done") {
            await refresh();
            showToast(tr("admin.users.cleanup_completed_toast", { details: fmtCleanupText(job) }, "Old storage cleanup completed\n" + fmtCleanupText(job)));
            setMsg(tr("admin.users.cleanup_completed", null, "Cleanup completed"));
            return;
        }

        if (state === "failed") {
            await refresh();

            const err = String(job?.error || "");
            if (err.includes("cleanup_not_needed")) {
                showToast(tr("admin.users.cleanup_not_needed_toast", null, "Old storage cleanup not needed\nNo old inactive copy was found."));
                setMsg(tr("admin.users.cleanup_not_needed", null, "Cleanup not needed"));
                return;
            }

            showToast(tr("admin.users.cleanup_failed_toast", { details: fmtCleanupText(job) }, "Old storage cleanup failed\n" + fmtCleanupText(job)), 15000);
            setMsg(tr("admin.users.cleanup_failed", null, "Cleanup failed"));
            return;
        }

        await new Promise(resolve => setTimeout(resolve, 3000));
    }
}

async function submitMigrationFromModal() {
    const fp = gMigrateFp;
    if (!fp) return;

    const poolSel = $("migratePoolSel");
    const pool_id = String(poolSel?.value || "").trim();

    if (!pool_id) {
        setMigrateError(tr("admin.users.no_destination_pool", null, "No destination pool selected."));
        return;
    }

    const cur = allUsers.find(x => String(x.fingerprint || "") === fp) || {};
    const curPoolId = storagePoolIdForUser(cur);

    if (pool_id === curPoolId) {
        setMigrateError(tr("admin.users.destination_diff", null, "Destination pool must differ from current pool."));
        return;
    }

    const dstPool = (gPools || []).find(x => x.id === pool_id);
    const dstName = dstPool?.name || pool_id;
    const userLabel = String(cur.name || cur.email || cur.username || fp || "");

    const ok = await openAdminUsersConfirmModal({
        title: tr("admin.users.migrate_confirm_title", { pool: dstName }, `Migrate user storage to pool "${dstName}"?`),
        subtitle: tr("admin.users.migrate_confirm_subtitle", null, "This will create an async storage migration job."),
        rows: [
            { label: tr("admin.users.user", null, "User"), value: userLabel || fp, mono: true },
            { label: tr("admin.users.from", null, "From"), value: curPoolId || "default", mono: true },
            { label: tr("admin.users.to", null, "To"), value: dstName, mono: true },
        ],
        note: tr(
            "admin.users.migrate_confirm_note",
            null,
            "The worker will copy data, verify it, then switch the user's storage mapping. The old copy is kept until cleanup."
        ),
        confirmText: tr("admin.users.start_migration", null, "Start migration"),
        cancelText: tr("admin.users.cancel", null, "Cancel"),
        danger: false
    });

    if (!ok) return;

    try {
        setMigrateError("");
        setMsg(tr("admin.users.queuing_migration", null, "Queuing migration…"));

        const j = await apiPost("/api/v4/admin/users/migrate_storage", {
            fingerprint: fp,
            pool_id,
        });

        const jobId = String(j?.job_id || "").trim();
        if (!jobId) {
            throw new Error(tr("admin.users.migration_job_missing", null, "Migration job_id missing from server response"));
        }


        adminUsersUpsertStorageJob("migration", jobId, fp, {
            state: "queued",
            phase: "queued",
            from_pool_id: curPoolId || "default",
            to_pool_id: pool_id || "default",
            message: tr("admin.users.migration_queued", null, "Migration queued"),
        });
        showToast(
            tr("admin.users.migration_queued_toast", { job: jobId, user: fp, to: dstName }, `Storage migration queued\nJob: ${jobId}\nUser: ${fp}\nTo: ${dstName}`)
        );

        setMsg(tr("admin.users.migration_queued", null, "Migration queued"));
        await pollMigrationJob(jobId, fp);
    } catch (e) {
        setMigrateError(String(e?.message || e));
        setMsg(tr("admin.users.error", { error: e?.message || e }, "Error: " + (e?.message || e)));
    }
}

async function submitCleanupOldCopy(fp) {
    const cur = allUsers.find(x => String(x.fingerprint || "") === String(fp)) || {};
    if (String(cur.storage_state || "").toLowerCase() !== "allocated") {
        showToast(tr("admin.users.storage_required_migration", null, "Storage must be allocated before cleanup."), 7000);
        return;
    }

    const activePoolId = storagePoolIdForUser(cur);

    let oldPoolId = "";
    if (activePoolId === "default") {
                const cleanupUser =
            (typeof u !== "undefined" && u) ? u :
            (typeof user !== "undefined" && user) ? user :
            (typeof curUser !== "undefined" && curUser) ? curUser :
            (typeof selectedUser !== "undefined" && selectedUser) ? selectedUser :
            null;
        const cleanupActivePool = cleanupUser ? storagePoolIdForUser(cleanupUser) : "default";
        const cleanupUserLabel = cleanupUser
            ? String(cleanupUser.name || cleanupUser.email || cleanupUser.fingerprint || tr("admin.users.selected_user", null, "Selected user"))
            : ((typeof fp !== "undefined" && fp) ? String(fp) : tr("admin.users.selected_user", null, "Selected user"));

        oldPoolId = await openAdminUsersPromptModal({
            title: tr("admin.users.cleanup_title", null, "Cleanup old storage copy?"),
            subtitle: tr("admin.users.cleanup_sub", null, "Choose the old pool copy to remove for this user."),
            rows: [
                { label: tr("admin.users.user", null, "User"), value: cleanupUserLabel, mono: true },
                { label: tr("admin.users.active_pool", null, "Active pool"), value: cleanupActivePool, mono: true },
            ],
            label: tr("admin.users.old_pool_id", null, "Old pool id"),
            value: "raidtest",
            placeholder: tr("admin.users.old_pool_placeholder", null, "old pool id, for example raidtest"),
            note: tr("admin.users.cleanup_note", null, "Only the old inactive storage copy should be removed. The active pool is protected."),
            confirmText: tr("admin.users.continue_cleanup", null, "Continue cleanup"),
            cancelText: tr("admin.users.cancel", null, "Cancel"),
            warn: true,
            validate(value) {
                if (value === cleanupActivePool) return tr("admin.users.old_pool_active_error", null, "Old pool id cannot be the active pool.");
                if (value.includes("/") || value.includes("\\")) return tr("admin.users.pool_id_not_path", null, "Use a pool id, not a path.");
                return "";
            },
        }) || "";
    } else {
        oldPoolId = await openAdminUsersPromptModal({
            title: tr("admin.users.cleanup_title", null, "Cleanup old storage copy?"),
            subtitle: tr("admin.users.cleanup_prompt_subtitle", { pool: activePoolId }, `User is currently active on ${activePoolId}. Choose the old pool copy to remove.`),
            rows: [
                { label: tr("admin.users.user", null, "User"), value: String(cur.name || cur.email || cur.username || fp || ""), mono: true },
                { label: tr("admin.users.active_pool", null, "Active pool"), value: activePoolId, mono: true },
            ],
            label: tr("admin.users.old_pool_id", null, "Old pool id"),
            value: "default",
            placeholder: tr("admin.users.old_pool_placeholder_default", null, "default or old pool id"),
            note: tr("admin.users.cleanup_prompt_note", null, "Enter the inactive old pool id to delete. The active pool is protected."),
            confirmText: tr("admin.users.continue_cleanup", null, "Continue cleanup"),
            cancelText: tr("admin.users.cancel", null, "Cancel"),
            warn: true,
            validate(value) {
                if (value === activePoolId) return tr("admin.users.old_pool_active_error", null, "Old pool id cannot be the active pool.");
                if (value.includes("/") || value.includes("\\")) return tr("admin.users.pool_id_not_path", null, "Use a pool id, not a path.");
                return "";
            },
        }) || "";
    }

    oldPoolId = String(oldPoolId).trim();
    if (!oldPoolId) return;

    if (oldPoolId === activePoolId) {
        showToast(tr("admin.users.old_pool_must_differ", null, "Old pool must differ from the active pool."), 7000);
        return;
    }

    const cleanupUserLabel = String(cur.name || cur.email || cur.username || fp || "");

    const ok = await openAdminUsersConfirmModal({
        title: tr("admin.users.cleanup_confirm_title", null, "Delete old inactive storage copy?"),
        subtitle: tr("admin.users.cleanup_confirm_subtitle", null, "This removes the old passive copy after a successful storage migration."),
        rows: [
            { label: tr("admin.users.user", null, "User"), value: cleanupUserLabel || fp, mono: true },
            { label: tr("admin.users.active_pool", null, "Active pool"), value: activePoolId, mono: true },
            { label: tr("admin.users.old_pool", null, "Old pool"), value: oldPoolId, mono: true },
        ],
        note: tr(
            "admin.users.cleanup_confirm_note",
            null,
            tr("admin.users.cleanup_confirm_note", null, "This deletes the old user subtree from the old pool. The currently active pool is protected.")
        ),
        confirmText: tr("admin.users.delete_old_copy", null, "Delete old copy"),
        cancelText: tr("admin.users.cancel", null, "Cancel"),
        danger: true
    });

    if (!ok) return;

    try {
        setMsg(tr("admin.users.queuing_cleanup", null, "Queuing cleanup…"));

        const j = await apiPost("/api/v4/admin/users/cleanup_old_storage", {
            fingerprint: fp,
            expected_active_pool_id: activePoolId,
            old_pool_id: oldPoolId,
        });

        const jobId = String(j?.job_id || "").trim();
        if (!jobId) throw new Error(tr("admin.users.cleanup_job_missing", null, "Cleanup job_id missing from server response"));


        adminUsersUpsertStorageJob("cleanup", jobId, fp, {
            state: "queued",
            phase: "queued",
            active_pool_id: activePoolId || "default",
            old_pool_id: oldPoolId || "default",
            message: tr("admin.users.cleanup_queued", null, "Cleanup queued"),
        });
        showToast(
            tr("admin.users.cleanup_queued_toast", { job: jobId, user: fp, active: activePoolId, old: oldPoolId }, `Old storage cleanup queued\nJob: ${jobId}\nUser: ${fp}\nActive pool: ${activePoolId}\nOld pool: ${oldPoolId}`)
        );

        setMsg(tr("admin.users.cleanup_queued", null, "Cleanup queued"));
        await pollCleanupJob(jobId, fp);
    } catch (e) {
        showToast(tr("admin.users.cleanup_failed_detail", { error: e?.message || e }, "Cleanup failed: " + (e?.message || e)), 15000);
        setMsg(tr("admin.users.error", { error: e?.message || e }, "Error: " + (e?.message || e)));
    }
}

async function submitAllocationFromModal() {
    const fp = gAllocFp;
    if (!fp) return;

    const poolSel = $("allocPoolSel");
    const gbInp = $("allocGb");

    const pool_id = String(poolSel?.value || "default");
    const quota_gb = Number(String(gbInp?.value || "").trim());

    if (!isFinite(quota_gb) || quota_gb < 0) {
        setAllocError(tr("admin.users.invalid_amount", null, "Invalid amount. Enter a number ≥ 0."));
        return;
    }

    const cur = allUsers.find(x => String(x.fingerprint || "") === fp) || {};
    const isAllocated = String(cur.storage_state || "").toLowerCase() === "allocated";
    const force = isAllocated;

    if (isAllocated) {
        const targetUser = allUsers.find(x => String(x.fingerprint || "") === fp) || {};
        const ok = await openAdminUsersConfirmModal({
            title: tr("admin.users.already_allocated_title", null, "Storage is already allocated"),
            subtitle: tr("admin.users.already_allocated_subtitle", null, "Change this user's storage pool or quota anyway?"),
            rows: [
                { label: tr("admin.users.user", null, "User"), value: String(targetUser.name || targetUser.email || fp), mono: true },
                { label: tr("admin.users.current_pool", null, "Current pool"), value: storagePoolIdForUser(targetUser), mono: true },
                { label: tr("admin.users.new_pool", null, "New pool"), value: pool_id || "default", mono: true },
            ],
            note: tr("admin.users.already_allocated_note", null, "This updates an existing storage allocation instead of creating a new one."),
            confirmText: tr("admin.users.change_storage", null, "Change storage"),
            cancelText: tr("admin.users.cancel", null, "Cancel"),
            danger: false,
        });
        if (!ok) return;
    }

    try {
        setAllocError("");
        setMsg(isAllocated ? tr("admin.users.updating_storage", null, "Updating storage…") : tr("admin.users.allocating", null, "Allocating…"));

        // Reuse your existing endpoint; we add pool_id
        const j = await apiPost("/api/v4/admin/users/storage", {
            fingerprint: fp,
            quota_gb,
            force,
            pool_id,
        });

        closeAllocModal();
        await refresh();

        const qb = Number(j.quota_bytes || 0);
        const quotaText = qb ? fmtBytes(qb) : `${quota_gb} GB`;
        const root = j.root_rel || "";
        const at = j.storage_set_at || "";

        showToast(
            tr(
                isAllocated ? "admin.users.storage_updated_toast" : "admin.users.storage_allocated_toast",
                {
                    pool: pool_id,
                    path: root ? tr("admin.users.path", { path: root }, `Path: ${root}\n`) : "",
                    quota: quotaText,
                    setAt: at ? tr("admin.users.set_at", { time: at }, `Set at: ${at}`) : ""
                },
                (isAllocated ? "Storage updated (click to copy)\n" : "Storage allocated\n") +
                `Pool: ${pool_id}\n` +
                (root ? `Path: ${root}\n` : "") +
                `Quota: ${quotaText}\n` +
                (at ? `Set at: ${at}` : "")
            )
        );

        setMsg(isAllocated ? tr("admin.users.storage_updated", null, "Storage updated") : tr("admin.users.allocated", null, "Allocated"));
    } catch (e) {
        setAllocError(String(e?.message || e));
        setMsg(tr("admin.users.error", { error: e?.message || e }, "Error: " + (e?.message || e)));
    }
}

let allUsers = [];
let actorFp = "";
let visibleUsers = [];
const selectedFingerprints = new Set();
const ADMIN_USERS_BULK_MIGRATE_MAX = 100;
let adminUsersLastBulkBatchId = localStorage.getItem("pqnas.adminUsers.lastBulkBatchId") || "";
let adminUsersEditFingerprint = "";

function isExternalWorkspaceUser(u) {
    const group = String(u && u.group || "").trim().toLowerCase();
    const notes = String(u && u.notes || "").trim().toLowerCase();
    const email = String(u && u.email || "").trim().toLowerCase();
    const name = String(u && u.name || "").trim().toLowerCase();

    if (group === "external" || group === "external workspace") return true;
    if (notes.includes("external_workspace_only=1")) return true;
    if (notes.includes("external workspace opaque account")) return true;
    if (email.startsWith("external-")) return true;
    if (name.startsWith("external-")) return true;

    return false;
}

function adminUsersShowExternal() {
    const el = $("showExternalUsers");
    return !!(el && el.checked);
}

function adminUsersExternalCount() {
    return allUsers.filter(isExternalWorkspaceUser).length;
}

function adminUsersVisibleCount() {
    const showExternal = adminUsersShowExternal();
    return allUsers.filter(u => showExternal || !isExternalWorkspaceUser(u)).length;
}

function adminUsersLoadedMessage() {
    const baseVisible = adminUsersVisibleCount();
    const filteredVisible = Array.isArray(visibleUsers) ? visibleUsers.length : baseVisible;
    const hidden = adminUsersShowExternal() ? 0 : adminUsersExternalCount();
    const selected = selectedFingerprints.size;

    let fallback = filteredVisible === baseVisible
        ? `Loaded ${baseVisible} users`
        : `Showing ${filteredVisible} of ${baseVisible} users`;

    if (hidden > 0) fallback += ` (${hidden} external hidden)`;
    if (selected > 0) fallback += ` · ${selected} selected`;

    return tr(
        "admin.users.loaded_users_filtered",
        { count: filteredVisible, total: baseVisible, hidden, selected },
        fallback
    );
}

function syncExternalUsersNotice() {
    const el = $("externalUsersNotice");
    if (!el) return;

    const hidden = adminUsersShowExternal() ? 0 : adminUsersExternalCount();

    if (hidden > 0) {
        el.textContent = tr(
            "admin.users.external_hidden_count",
            { count: hidden },
            `${hidden} external workspace users hidden`
        );
    } else if (adminUsersShowExternal()) {
        el.textContent = tr(
            "admin.users.external_visible_notice",
            null,
            "External workspace users are visible in this list."
        );
    } else {
        el.textContent = tr(
            "admin.users.external_hidden_default",
            null,
            "External workspace users are hidden from this normal user list by default."
        );
    }
}

const ADMIN_USERS_SORT_STORAGE_KEY = "pqnas_admin_users_sort_v1";
let adminUsersSort = (() => {
    try {
        const raw = JSON.parse(localStorage.getItem(ADMIN_USERS_SORT_STORAGE_KEY) || "{}");
        const key = String(raw.key || "fingerprint");
        const dir = String(raw.dir || "asc") === "desc" ? "desc" : "asc";
        return { key, dir };
    } catch (_) {
        return { key: "fingerprint", dir: "asc" };
    }
})();
// ----- pools + allocation modal state -----
let gPools = null; // array of { id, name, hint }
let gAllocFp = "";
let gMigrateFp = "";
let gAllocPreview = null;

function setMsg(text) {
    const el = $("msg");
    if (el) el.textContent = text || "";
}


function adminUsersAddOption(sel, value, label) {
    const opt = document.createElement("option");
    opt.value = String(value || "");
    opt.textContent = String(label || value || "");
    sel.appendChild(opt);
}

function adminUsersPoolCapacityNumber(pool, keys) {
    const p = pool || {};

    for (const key of keys) {
        if (!Object.prototype.hasOwnProperty.call(p, key)) continue;

        const raw = p[key];

        // UI safety: null/undefined/empty string mean "not provided", not 0 B.
        // Real numeric 0 is still accepted, so a truly full pool can show 0 B.
        if (raw === null || raw === undefined) continue;
        if (typeof raw === "string" && raw.trim() === "") continue;

        const n = Number(raw);
        if (Number.isFinite(n) && n >= 0) return n;
    }

    return null;
}

function adminUsersPoolCapacityParts(pool) {
    const total = adminUsersPoolCapacityNumber(pool, [
        "total_bytes",
        "pool_total_bytes",
        "capacity_bytes",
        "bytes_total",
        "size_bytes",
        "fs_total_bytes",
        "stat_total_bytes",
    ]);

    let free = adminUsersPoolCapacityNumber(pool, [
        "free_bytes",
        "pool_free_bytes",
        "available_bytes",
        "avail_bytes",
        "bytes_available",
        "usable_free_bytes",
        "fs_free_bytes",
        "stat_free_bytes",
    ]);

    const used = adminUsersPoolCapacityNumber(pool, [
        "used_bytes",
        "bytes_used",
        "allocated_bytes",
        "quota_used_bytes",
        "assigned_quota_bytes",
    ]);

    if (free === null && total !== null && used !== null && total >= used) {
        free = total - used;
    }

    return { total, free, used };
}

function adminUsersPoolAllocatedQuotaBytes(poolId) {
    const want = String(poolId || "default").trim() || "default";
    const rows = (typeof allUsers !== "undefined" && Array.isArray(allUsers)) ? allUsers : [];

    let total = 0;
    for (const u of rows) {
        if (String(u?.storage_state || "unallocated").toLowerCase() !== "allocated") continue;
        if (storagePoolIdForUser(u) !== want) continue;

        const quota = Number(u?.quota_bytes ?? 0);
        if (!Number.isFinite(quota) || quota <= 0) continue;

        // UI accounting only: keep quota addition bounded so a bad value cannot
        // produce Infinity/NaN labels. Backend still enforces real capacity.
        total = Math.min(Number.MAX_SAFE_INTEGER, total + quota);
    }

    return total;
}

function adminUsersPoolQuotaCapacityParts(pool) {
    const p = pool || {};
    const { total, free } = adminUsersPoolCapacityParts(p);
    const poolId = String(p.id || p.pool_id || "default").trim() || "default";

    const explicitAllocated = adminUsersPoolCapacityNumber(p, [
        "allocated_quota_bytes",
        "quota_allocated_bytes",
        "assigned_quota_bytes",
        "allocated_user_quota_bytes",
        "allocated_total_bytes",
    ]);

    const allocatedQuota =
        explicitAllocated !== null
            ? explicitAllocated
            : adminUsersPoolAllocatedQuotaBytes(poolId);

    const explicitRemaining = adminUsersPoolCapacityNumber(p, [
        "remaining_allocatable_bytes",
        "pool_remaining_allocatable_bytes",
        "remaining_bytes",
        "remaining_quota_bytes",
        "quota_remaining_bytes",
        "quota_available_bytes",
        "available_quota_bytes",
        "allocatable_quota_bytes",
    ]);

    let quotaAvailable = explicitRemaining;
    if (quotaAvailable === null && total !== null) {
        quotaAvailable = total > allocatedQuota ? total - allocatedQuota : 0;
    }

    return { total, free, allocatedQuota, quotaAvailable };
}

function adminUsersPoolCapacityText(pool) {
    const { free, quotaAvailable } = adminUsersPoolQuotaCapacityParts(pool);

    if (quotaAvailable !== null && free !== null) {
        return tr(
            "admin.users.pool_space_quota_free",
            { quota: fmtBytesShort(quotaAvailable), free: fmtBytesShort(free) },
            `${fmtBytesShort(quotaAvailable)} allocatable (${fmtBytesShort(free)} physically free)`
        );
    }

    if (quotaAvailable !== null) {
        return tr(
            "admin.users.pool_space_quota_only",
            { quota: fmtBytesShort(quotaAvailable) },
            `${fmtBytesShort(quotaAvailable)} allocatable`
        );
    }

    if (free !== null) {
        return tr(
            "admin.users.pool_space_physical_free",
            { free: fmtBytesShort(free) },
            `${fmtBytesShort(free)} physically free`
        );
    }

    return tr("admin.users.pool_space_unknown", null, "Pool capacity unavailable");
}

function adminUsersPoolOptionLabel(pool) {
    const p = pool || {};
    const name = String(p.name || p.id || "").trim();
    const capacity = adminUsersPoolCapacityText(p);

    if (!name) return capacity;
    if (capacity === tr("admin.users.pool_space_unknown", null, "Pool capacity unavailable")) return name;

    return `${name} — ${capacity}`;
}

function syncAdminUsersBulkPoolCapacityUi() {
    const box = $("adminUsersBulkPoolCapacity");
    if (!box) return;

    const poolId = String($("adminUsersBulkPool")?.value || "").trim();
    if (!poolId) {
        box.textContent = tr("admin.users.pool_space_unknown", null, "Pool capacity unavailable");
        return;
    }

    const pool = (gPools || []).find(p => String(p.id || "") === poolId);
    if (!pool) {
        box.textContent = tr("admin.users.pool_space_unknown", null, "Pool capacity unavailable");
        return;
    }

    box.textContent = adminUsersPoolCapacityText(pool);
}

function adminUsersSearchText() {
    const primary = String($("adminUsersSearch")?.value || "").trim();
    const legacy = String($("filter")?.value || "").trim();
    return (primary || legacy).toLowerCase();
}

function adminUsersPoolFilterValue() {
    return String($("adminUsersPoolFilter")?.value || "all").trim() || "all";
}

function adminUsersStorageFilterValue() {
    return String($("adminUsersStorageFilter")?.value || "all").trim().toLowerCase() || "all";
}

function adminUsersStatusFilterValue() {
    return String($("adminUsersStatusFilter")?.value || "all").trim().toLowerCase() || "all";
}

function adminUsersFilterMatches(u, searchText, poolFilter, storageFilter, statusFilter) {
    const poolId = storagePoolIdForUser(u);
    const storageState = String(u?.storage_state || "unallocated").toLowerCase();
    const status = String(u?.status || "").toLowerCase();

    if (poolFilter !== "all" && poolId !== poolFilter) return false;
    if (storageFilter !== "all" && storageState !== storageFilter) return false;
    if (statusFilter !== "all" && status !== statusFilter) return false;

    const hay = [
        u.fingerprint, u.name, u.notes, u.role, u.status,
        u.group, u.email, u.storage_state, poolId,
        String(u.quota_bytes || "")
    ].join(" ").toLowerCase();

    return !searchText || hay.includes(searchText);
}

function refreshAdminUsersPoolFilter() {
    const sel = $("adminUsersPoolFilter");
    if (!sel) return;

    const prev = String(sel.value || "all");

    const ids = new Set();

    // Existing users may reference pools even if the pool list endpoint is
    // temporarily unavailable. Keep those visible for filtering.
    for (const u of allUsers) {
        const poolId = storagePoolIdForUser(u);
        if (poolId) ids.add(poolId);
    }

    // New or empty pools must also be filterable. The migration target dropdown
    // loads gPools from the backend pool list; reuse that data here.
    for (const p of Array.isArray(gPools) ? gPools : []) {
        const poolId = String(p?.id || p?.pool_id || "").trim();
        if (poolId) ids.add(poolId);
    }

    const pools = Array.from(ids)
        .filter(Boolean)
        .sort((a, b) => a.localeCompare(b, undefined, { numeric: true, sensitivity: "base" }));

    sel.innerHTML = "";
    adminUsersAddOption(sel, "all", tr("admin.users.all_pools", null, "All pools"));

    for (const poolId of pools) {
        const poolMeta = (gPools || []).find(p => String(p?.id || p?.pool_id || "") === poolId);
        const label = poolId === "default"
            ? tr("admin.users.default_pool", null, "Default pool")
            : String(poolMeta?.name || poolId);
        adminUsersAddOption(sel, poolId, label);
    }

    sel.value = pools.includes(prev) ? prev : "all";
}

function adminUsersPruneSelection() {
    const valid = new Set(allUsers.map(u => String(u.fingerprint || "")).filter(Boolean));
    for (const fp of Array.from(selectedFingerprints)) {
        if (!valid.has(fp)) selectedFingerprints.delete(fp);
    }
}

function adminUsersVisibleFingerprints() {
    return visibleUsers.map(u => String(u.fingerprint || "")).filter(Boolean);
}

function adminUsersSelectVisible(checked) {
    for (const fp of adminUsersVisibleFingerprints()) {
        if (checked) selectedFingerprints.add(fp);
        else selectedFingerprints.delete(fp);
    }

    render();
    setMsg(adminUsersLoadedMessage());
}

function adminUsersSelectedRows() {
    const selected = new Set(selectedFingerprints);
    return allUsers.filter(u => selected.has(String(u.fingerprint || "")));
}

function syncAdminUsersBulkUi() {
    const visibleFps = adminUsersVisibleFingerprints();
    const selectedVisible = visibleFps.filter(fp => selectedFingerprints.has(fp)).length;

    const selectVisible = $("adminUsersSelectVisible");
    if (selectVisible) {
        selectVisible.checked = visibleFps.length > 0 && selectedVisible === visibleFps.length;
        selectVisible.indeterminate = selectedVisible > 0 && selectedVisible < visibleFps.length;
        selectVisible.disabled = visibleFps.length === 0;
    }

    const selectedCount = selectedFingerprints.size;
    const haveLastBatch = !!String(adminUsersLastBulkBatchId || "").trim();

    const bar = $("adminUsersBulkBar");
    const count = $("adminUsersBulkCount");
    const migrateBtn = $("adminUsersBulkMigrateBtn");
    const cleanupBtn = $("adminUsersBulkCleanupBtn");
    const poolSel = $("adminUsersBulkPool");

    // UX: keep the bar visible after migration if there is a backend batch that
    // can still be used for old-copy cleanup. The UI is only a convenience;
    // backend cleanup still validates admin auth, job state, pool ids, and copy
    // completeness before destructive deletion.
    if (bar) bar.hidden = selectedCount === 0 && !haveLastBatch;

    if (count) {
        if (selectedCount > 0) {
            count.textContent = tr(
                "admin.users.bulk_selected_count",
                { count: selectedCount },
                `${selectedCount} selected`
            );
        } else if (haveLastBatch) {
            count.textContent = tr(
                "admin.users.bulk_last_batch_available",
                null,
                "Last bulk migration batch is available for cleanup"
            );
        } else {
            count.textContent = tr(
                "admin.users.bulk_selected_count",
                { count: 0 },
                "0 selected"
            );
        }
    }

    if (migrateBtn) {
        migrateBtn.disabled = selectedCount === 0 || !String(poolSel?.value || "").trim();
    }

    if (cleanupBtn) {
        cleanupBtn.disabled = !haveLastBatch;
    }
}

async function refreshAdminUsersBulkDestPools() {
    const sel = $("adminUsersBulkPool");
    if (!sel) return;

    const prev = String(sel.value || "");
    const pools = await ensurePoolsLoaded();

    sel.innerHTML = "";
    adminUsersAddOption(sel, "", tr("admin.users.choose_target_pool", null, "Choose target pool…"));

    for (const p of pools) {
        adminUsersAddOption(sel, p.id, adminUsersPoolOptionLabel(p));
    }

    if (prev && Array.from(sel.options).some(o => o.value === prev)) {
        sel.value = prev;
    }

    syncAdminUsersBulkUi();
    syncAdminUsersBulkPoolCapacityUi();
}

function adminUsersRefreshAfterFilterChange() {
    render();
    setMsg(adminUsersLoadedMessage());
}

async function submitAdminUsersBulkMigration() {
    const pool_id = String($("adminUsersBulkPool")?.value || "").trim();
    if (!pool_id) {
        showToast(tr("admin.users.no_destination_pool", null, "No destination pool selected."), 7000);
        return;
    }

    const selectedRows = adminUsersSelectedRows();
    if (!selectedRows.length) {
        showToast(tr("admin.users.no_users_selected", null, "No users selected."), 7000);
        return;
    }

    const expectedPool = adminUsersPoolFilterValue();
    const fingerprints = selectedRows
        .map(u => String(u.fingerprint || "").trim())
        .filter(Boolean)
        .slice(0, ADMIN_USERS_BULK_MIGRATE_MAX);

    if (!fingerprints.length) {
        showToast(tr("admin.users.no_users_selected", null, "No users selected."), 7000);
        return;
    }

    const overflow = Math.max(0, selectedRows.length - fingerprints.length);
    const dstPool = (gPools || []).find(p => p.id === pool_id);
    const dstName = dstPool?.name || pool_id;

    const ok = await openAdminUsersConfirmModal({
        title: tr("admin.users.bulk_migrate_confirm_title", { count: fingerprints.length, pool: dstName }, `Migrate ${fingerprints.length} selected users to "${dstName}"?`),
        subtitle: tr("admin.users.bulk_migrate_confirm_subtitle", null, "This creates a durable backend batch and queues one async storage migration job per user."),
        rows: [
            { label: tr("admin.users.selected", null, "Selected"), value: String(selectedRows.length), mono: true },
            { label: tr("admin.users.queued_now", null, "Queued now"), value: String(fingerprints.length), mono: true },
            { label: tr("admin.users.to", null, "To"), value: dstName, mono: true },
        ],
        note: tr(
            "admin.users.bulk_migrate_confirm_note",
            null,
            "The backend stores this batch so the same migrated users can be found later for old-copy cleanup. Each migration still performs server-side source/target validation and copy verification."
        ) + (overflow ? `\n${tr("admin.users.bulk_limit_note", { limit: ADMIN_USERS_BULK_MIGRATE_MAX, overflow }, `Safety limit: first ${ADMIN_USERS_BULK_MIGRATE_MAX} users now; ${overflow} remain selected.`)}` : ""),
        confirmText: tr("admin.users.start_migration", null, "Start migration"),
        cancelText: tr("admin.users.cancel", null, "Cancel"),
        danger: false
    });

    if (!ok) return;

    const btn = $("adminUsersBulkMigrateBtn");
    if (btn) btn.disabled = true;

    try {
        setMsg(tr("admin.users.bulk_queuing_migrations", null, "Queuing bulk migrations…"));

        const body = {
            fingerprints,
            pool_id,
            expected_from_pool_id: expectedPool,
        };

        const j = await apiPost("/api/v4/admin/users/bulk_migrate_storage", body);
        const batch = j?.batch || {};
        const batchId = String(j?.batch_id || batch?.batch_id || "").trim();

        if (batchId) {
            adminUsersLastBulkBatchId = batchId;
            localStorage.setItem("pqnas.adminUsers.lastBulkBatchId", batchId);
        }

        const items = Array.isArray(batch.items) ? batch.items : [];
        let queued = 0;
        let skipped = 0;
        let failed = 0;

        for (const item of items) {
            const fp = String(item.fingerprint || "").trim();
            const state = String(item.state || "");
            const jobId = String(item.migration_job_id || "").trim();

            if (state === "queued" && fp && jobId) {
                queued += 1;
                adminUsersUpsertStorageJob("migration", jobId, fp, {
                    state: "queued",
                    phase: "queued",
                    from_pool_id: String(item.from_pool_id || "default"),
                    to_pool_id: String(item.to_pool_id || pool_id || "default"),
                    message: tr("admin.users.migration_queued", null, "Migration queued"),
                });
                adminUsersStartStorageJobMonitor("migration", jobId, fp);
                selectedFingerprints.delete(fp);
            } else if (state === "skipped") {
                skipped += 1;
            } else if (state === "failed") {
                failed += 1;
            }
        }

        render();

        const summary = [
            tr("admin.users.bulk_migration_summary", { queued, failed }, `Bulk migration queued: ${queued}, failed: ${failed}`),
            skipped ? tr("admin.users.bulk_skipped_count", { count: skipped }, `Skipped: ${skipped}`) : "",
            batchId ? `Batch: ${batchId}` : "",
            tr("admin.users.bulk_cleanup_hint", null, "After the migrations finish, use Cleanup last batch to remove inactive old copies.")
        ].filter(Boolean).join("\n");

        setMsg(summary);
        showToast(summary, 12000);
    } finally {
        syncAdminUsersBulkUi();
    }
}

async function submitAdminUsersBulkCleanupLastBatch() {
    const batchId = String(adminUsersLastBulkBatchId || "").trim();
    if (!batchId) {
        showToast(tr("admin.users.no_bulk_batch", null, "No recent backend bulk migration batch found."), 9000);
        return;
    }

    const ok = await openAdminUsersConfirmModal({
        title: tr("admin.users.bulk_cleanup_confirm_title", null, "Cleanup old copies from last bulk migration?"),
        subtitle: tr("admin.users.bulk_cleanup_confirm_subtitle", null, "Only migration jobs that are done will be queued for cleanup."),
        rows: [
            { label: tr("admin.users.batch", null, "Batch"), value: batchId, mono: true },
        ],
        note: tr(
            "admin.users.bulk_cleanup_confirm_note",
            null,
            "The backend checks each migration job is done, verifies the active and old pool ids, then each cleanup job re-checks the active copy before deleting the inactive old copy."
        ),
        confirmText: tr("admin.users.cleanup_old_copy", null, "Cleanup old copy"),
        cancelText: tr("admin.users.cancel", null, "Cancel"),
        danger: true
    });

    if (!ok) return;

    try {
        setMsg(tr("admin.users.bulk_cleanup_queuing", null, "Queuing cleanup for last batch…"));

        const j = await apiPost("/api/v4/admin/users/bulk_cleanup_old_storage", {
            batch_id: batchId,
        });

        let queued = 0;
        let skipped = 0;
        let failed = 0;

        const results = Array.isArray(j?.results) ? j.results : [];
        for (const item of results) {
            const fp = String(item.fingerprint || "").trim();
            const state = String(item.state || "");
            const jobId = String(item.cleanup_job_id || "").trim();

            if (state === "queued" && fp && jobId) {
                queued += 1;
                adminUsersUpsertStorageJob("cleanup", jobId, fp, {
                    state: "queued",
                    phase: "queued",
                    active_pool_id: String(item.expected_active_pool_id || ""),
                    old_pool_id: String(item.old_pool_id || ""),
                    message: tr("admin.users.cleanup_queued", null, "Cleanup queued"),
                });
                adminUsersStartStorageJobMonitor("cleanup", jobId, fp);
            } else if (state === "skipped") {
                skipped += 1;
            } else if (state === "failed") {
                failed += 1;
            }
        }

        const summary = tr(
            "admin.users.bulk_cleanup_summary",
            { queued, skipped, failed },
            `Cleanup queued: ${queued}, skipped: ${skipped}, failed: ${failed}`
        );

        setMsg(summary);
        showToast(summary, 12000);
    } catch (e) {
        const msg = tr("admin.users.failed", { error: e?.message || e }, "Failed: " + (e?.message || e));
        setMsg(msg);
        showToast(msg, 15000);
    }
}


function isProfileEditorOpen() {
    const body = $("profileEditorBody");
    return !!body && !body.classList.contains("collapsed");
}

function setProfileEditorOpen(open) {
    const body = $("profileEditorBody");
    const btn = $("profileEditorToggle");
    if (!body || !btn) return;

    body.classList.toggle("collapsed", !open);
    btn.setAttribute("aria-expanded", open ? "true" : "false");
    btn.textContent = open
        ? tr("admin.users.hide_editor", null, "Hide editor")
        : tr("admin.users.show_editor", null, "Show editor");
}

function adminUsersSortValue(u, key) {
    switch (String(key || "")) {
        case "name":
            return String(`${u.name || ""} ${u.email || ""} ${u.notes || ""}`).toLowerCase();
        case "role":
            return String(u.role || "").toLowerCase();
        case "status":
            return String(u.status || "").toLowerCase();
        case "group":
            return String(u.group || "").toLowerCase();
        case "storage":
            return String(u.storage_state || "").toLowerCase();
        case "pool":
            return String(storagePoolIdForUser(u) || "default").toLowerCase();
        case "quota":
            return Number(u.quota_bytes || 0);
        case "added": {
            const t = Date.parse(String(u.added_at || ""));
            return Number.isFinite(t) ? t : 0;
        }
        case "fingerprint":
        default:
            return String(u.fingerprint || "").toLowerCase();
    }
}

function compareAdminUserValues(a, b) {
    if (typeof a === "number" || typeof b === "number") {
        const an = Number(a || 0);
        const bn = Number(b || 0);
        return an === bn ? 0 : (an < bn ? -1 : 1);
    }

    return String(a || "").localeCompare(String(b || ""), undefined, {
        numeric: true,
        sensitivity: "base"
    });
}

function sortAdminUserRows(rows) {
    const key = adminUsersSort.key || "fingerprint";
    const dir = adminUsersSort.dir === "desc" ? -1 : 1;

    return [...rows].sort((a, b) => {
        const primary = compareAdminUserValues(
            adminUsersSortValue(a, key),
            adminUsersSortValue(b, key)
        );

        if (primary !== 0) return primary * dir;

        const tieName = compareAdminUserValues(
            adminUsersSortValue(a, "name"),
            adminUsersSortValue(b, "name")
        );
        if (tieName !== 0) return tieName;

        return compareAdminUserValues(
            adminUsersSortValue(a, "fingerprint"),
            adminUsersSortValue(b, "fingerprint")
        );
    });
}

function saveAdminUsersSort() {
    try {
        localStorage.setItem(ADMIN_USERS_SORT_STORAGE_KEY, JSON.stringify(adminUsersSort));
    } catch (_) {}
}

function updateAdminUsersSortIndicators() {
    document.querySelectorAll("button.sortTh[data-sort]").forEach(btn => {
        const key = btn.getAttribute("data-sort") || "";
        const active = key === adminUsersSort.key;
        btn.classList.toggle("active", active);

        const icon = btn.querySelector(".sortIcon");
        if (icon) {
            icon.textContent = active
                ? (adminUsersSort.dir === "desc" ? "▼" : "▲")
                : "↕";
        }

        const th = btn.closest("th");
        if (th) {
            th.setAttribute(
                "aria-sort",
                active
                    ? (adminUsersSort.dir === "desc" ? "descending" : "ascending")
                    : "none"
            );
        }
    });
}

function bindAdminUsersSortHeaders() {
    document.querySelectorAll("button.sortTh[data-sort]").forEach(btn => {
        btn.addEventListener("click", () => {
            const key = btn.getAttribute("data-sort") || "fingerprint";

            if (adminUsersSort.key === key) {
                adminUsersSort.dir = adminUsersSort.dir === "asc" ? "desc" : "asc";
            } else {
                adminUsersSort.key = key;
                adminUsersSort.dir = key === "added" || key === "quota" ? "desc" : "asc";
            }

            saveAdminUsersSort();
            render();
        });
    });

    updateAdminUsersSortIndicators();
}

function render() {
    const f = adminUsersSearchText();
    const poolFilter = adminUsersPoolFilterValue();
    const storageFilter = adminUsersStorageFilterValue();
    const statusFilter = adminUsersStatusFilterValue();
    const showExternal = adminUsersShowExternal();

    let rows = allUsers.filter(u => {
        if (!showExternal && isExternalWorkspaceUser(u)) return false;
        return adminUsersFilterMatches(u, f, poolFilter, storageFilter, statusFilter);
    });

    syncExternalUsersNotice();

    rows = sortAdminUserRows(rows);
    visibleUsers = rows;

    const tb = $("tbody");
    if (!tb) return;

    tb.innerHTML = rows.map(u => {

        const fp = String(u.fingerprint || "");
        const isOpen = openUsers.has(fp);
        const isSelf = actorFp && fp === actorFp;
        const isSelected = selectedFingerprints.has(fp);

        const selfTag = isSelf
            ? `<span class="pq-badge ok selfTag" title="${esc(tr("admin.users.this_is_you", null, "This is you"))}">${esc(tr("admin.users.you", null, "you"))}</span>`
            : "";

        // Disallow self-modification (Allocate is allowed for self)
        // Allow self profile editing, but block dangerous self-actions
        const disEditAttr = "";
        const disEditClass = "";

        const disDangerAttr = isSelf
            ? ` disabled title="${esc(tr("admin.users.refuse_self", null, "Refusing to modify your own admin entry"))}"`
            : "";

        const disDangerClass = isSelf
            ? ` style="opacity:0.45; cursor:not-allowed;"`
            : "";


        // detail content
        const quotaBytes = Number(u.quota_bytes || 0);
        const quotaText = quotaBytes ? fmtBytes(quotaBytes) : "—";

        const usedBytes = Number(u.used_bytes ?? u.storage_used_bytes ?? 0);
        const quotaBytes2 = Number(u.quota_bytes ?? 0);

        const pct = quotaBytes2 > 0 ? clamp01(usedBytes / quotaBytes2) : 0;
        const pct100 = (pct * 100).toFixed(1);

        const quotaCls =
            pct >= 0.90 ? "danger" :
                pct >= 0.70 ? "warn" :
                    "";

        const safeUsedBytes = Number.isFinite(usedBytes) ? usedBytes : 0;
        const remainingBytes = quotaBytes2 > 0 ? Math.max(0, quotaBytes2 - safeUsedBytes) : NaN;
        const remainingText = Number.isFinite(remainingBytes) ? fmtBytes(remainingBytes) : "—";
        const usedText = fmtBytes(safeUsedBytes);
        const usagePctText = quotaBytes2 > 0 ? `${pct100}%` : "—";

        const detailRow = isOpen ? `

<tr class="detailRow" data-fp="${esc(fp)}">
  <td colspan="10">
    <div class="detailGrid">
      <div class="detailBox">
        <h3>${esc(tr("admin.users.profile", null, "Profile"))}</h3>
        ${avatarSrc(u) ? `
          <div class="detailAvatarRow">
            <img
              src="${esc(avatarSrc(u))}"
              alt="${esc(tr("admin.users.avatar_alt", null, "avatar"))}"
              data-avatar-open="1"
              data-fp="${esc(fp)}"
              class="detailAvatar"
              title="${esc(tr("admin.users.click_preview", null, "Click to preview"))}"
              onerror="this.classList.add('avatarLoadFailed'); this.title='${esc(tr("admin.users.avatar_failed", null, "Avatar failed to load"))}';"
            />


            <div class="muted detailAvatarMeta">
              ${esc(tr("admin.users.avatar", null, "Avatar"))}<br/>
              <span class="mono detailAvatarPath">${esc(avatarSrc(u))}</span>
            </div>
          </div>
        ` : `
          <div class="muted detailAvatarEmpty">
            ${esc(tr("admin.users.avatar", null, "Avatar"))}: <span class="mono">—</span>
          </div>
        `}

        <div class="detailActions">
            <button class="pq-btn secondary" data-edit="${esc(fp)}" type="button" title="${esc(tr("admin.users.load_edit_title", null, "Load this user into the edit form"))}" ${disEditAttr}${disEditClass}>${esc(tr("admin.users.edit", null, "Edit"))}</button>
        </div>

        <div class="detailKV"><div class="k">${esc(tr("admin.users.fingerprint", null, "Fingerprint"))}</div><div class="v mono">${esc(fp)}</div></div>
        <div class="detailKV"><div class="k">${esc(tr("admin.users.name_placeholder", null, "Name"))}</div><div class="v">${esc(u.name || "—")}</div></div>
        <div class="detailKV"><div class="k">${esc(tr("admin.users.role", null, "Role"))}</div><div class="v">${rolePill(u.role)}</div></div>
        <div class="detailKV"><div class="k">${esc(tr("admin.users.status", null, "Status"))}</div><div class="v">${pill(u.status)}</div></div>
        <div class="detailKV"><div class="k">${esc(tr("admin.users.group", null, "Group"))}</div><div class="v">${groupPill(u.group)}</div></div>
        <div class="detailKV"><div class="k">${esc(tr("admin.users.email", null, "Email"))}</div><div class="v">${esc(u.email || "—")}</div></div>
        <div class="detailKV"><div class="k">${esc(tr("admin.users.storage", null, "Storage"))}</div><div class="v">${storageCellHtml(u)}</div></div>
        <div class="detailKV"><div class="k">${esc(tr("admin.users.quota", null, "Quota"))}</div><div class="v mono">${esc(quotaText)}</div></div>
        <div class="detailKV"><div class="k">${esc(tr("admin.users.added", null, "Added"))}</div><div class="v mono">${esc(u.added_at || "—")}</div></div>
        <div class="detailKV"><div class="k">${esc(tr("admin.users.last_seen", null, "Last seen"))}</div><div class="v mono">${esc(u.last_seen || "—")}</div></div>
      </div>

      <div class="detailBox">
        <h3>${esc(tr("admin.users.storage_usage", null, "Storage usage"))}</h3>

        <div class="usageSummaryGrid">
          <div class="usageMetric">
            <div class="k">${esc(tr("admin.users.used", null, "Used"))}</div>
            <div class="v" title="${esc(usedText)}">${esc(usedText)}</div>
          </div>

          <div class="usageMetric">
            <div class="k">${esc(tr("admin.users.quota", null, "Quota"))}</div>
            <div class="v" title="${esc(quotaText)}">${esc(quotaText)}</div>
          </div>

          <div class="usageMetric">
            <div class="k">${esc(tr("admin.users.remaining", null, "Remaining"))}</div>
            <div class="v" title="${esc(remainingText)}">${esc(remainingText)}</div>
          </div>

          <div class="usageMetric">
            <div class="k">${esc(tr("admin.users.usage_percent", null, "Usage"))}</div>
            <div class="v">${esc(usagePctText)}</div>
          </div>
        </div>

        <div class="quotaBox">
          <div class="quotaTop">
            <div class="quotaLabel">${esc(tr("admin.users.used_quota", null, "Used / quota"))}</div>
            <div class="quotaNum mono">${esc(quotaUsageText(u.used_bytes ?? u.storage_used_bytes, u.quota_bytes))}</div>
          </div>
          <div class="quotaBar" title="${esc(quotaUsageText(usedBytes, quotaBytes2))}">
            <div class="quotaFill ${quotaCls}" style="width:${pct100}%"></div>
          </div>
        </div>

        <div class="usageExplain">
          ${esc(tr(
              "admin.users.quota_includes_hidden",
              null,
              "Quota usage can include normal user files plus hidden per-user storage such as file version history."
          ))}
          <br>
          <span class="mono">.pqnas/versions</span>
        </div>

        <h3 class="detailSubhead">${esc(tr("admin.users.notes", null, "Notes"))}</h3>
        <pre class="detailPre">${esc(u.notes || "—")}</pre>
      </div>

<div class="detailBox detailBoxActions">
  <h3>${esc(tr("admin.users.actions", null, "Actions"))}</h3>
  <div class="detailActions">
    <button class="pq-btn secondary"
            data-act="enable"
            data-fp="${esc(fp)}"
            type="button"
            title="${esc(tr("admin.users.enable_title", null, "Allow this fingerprint to log in again"))}"
            ${disDangerAttr}${disDangerClass}>${esc(tr("admin.users.enable", null, "Enable"))}</button>

    <button class="pq-btn secondary"
            data-act="disable"
            data-fp="${esc(fp)}"
            type="button"
            title="${esc(tr("admin.users.disable_title", null, "Disable login until an admin enables it again"))}"
            ${disDangerAttr}${disDangerClass}>${esc(tr("admin.users.disable", null, "Disable"))}</button>

    <button class="pq-btn secondary"
            data-act="revoke"
            data-fp="${esc(fp)}"
            type="button"
            title="${esc(tr("admin.users.revoke_title", null, "Hard-block this fingerprint from logging in"))}"
            ${disDangerAttr}${disDangerClass}>${esc(tr("admin.users.revoke", null, "Revoke"))}</button>

    <button class="pq-btn secondary"
            data-act="allocate"
            data-fp="${esc(fp)}"
            type="button"
            title="${esc(tr("admin.users.allocate_title", null, "Allocate storage and set quota for this user"))}">${esc(tr("admin.users.allocate", null, "Allocate"))}</button>
            
    ${String(u.storage_state || "").toLowerCase() === "allocated" ? `
        <button class="pq-btn secondary"
            data-act="migrate"
            data-fp="${esc(fp)}"
            type="button"
            title="${esc(tr("admin.users.migrate_title", null, "Move user storage to another pool with async copy and verify"))}">${esc(tr("admin.users.migrate", null, "Migrate"))}</button>

        <button class="pq-btn secondary"
            data-act="cleanup-old-copy"
            data-fp="${esc(fp)}"
            type="button"
            title="${esc(tr("admin.users.cleanup_title", null, "Delete the old inactive storage copy left behind after migration"))}">${esc(tr("admin.users.cleanup_old_copy", null, "Cleanup old copy"))}</button>
    ` : ``}
    
    <button class="pq-btn danger"
            data-act="delete"
            data-fp="${esc(fp)}"
            type="button"
            title="${esc(tr("admin.users.delete_title", null, "Remove this entry from users.json; the user can reappear if they scan again"))}"
            ${disDangerAttr}${disDangerClass}>${esc(tr("admin.users.delete", null, "Delete"))}</button>
  </div>

  ${isSelf
            ? `<div class="muted detailSelfProtection">
         ${esc(tr("admin.users.self_protection", null, "Self-protection: enable / disable / revoke / delete are blocked for your own fingerprint."))}
       </div>`
            : ``}
</div>

    </div>
  </td>
</tr>

    `.trim() : "";

        return `
<tr class="userRow" data-fp="${esc(fp)}" aria-expanded="${isOpen ? "true" : "false"}">
  <td class="adminUsersSelectCell">
    <input
      class="adminUsersRowSelect"
      type="checkbox"
      data-select-fp="${esc(fp)}"
      aria-label="${esc(tr("admin.users.select_user", null, "Select user"))}"
      ${isSelected ? "checked" : ""}
    />
  </td>
  <td>${avatarThumb(u)}</td>

  <td>
    <div class="userMainLine">
      <button class="expBtn" data-exp="${esc(fp)}" type="button" aria-expanded="${isOpen ? "true" : "false"}" title="${esc(tr("admin.users.expand_collapse", null, "Expand/collapse"))}">
        ${isOpen ? "▾" : "▸"}
      </button>
      <span class="userNameText" title="${esc(u.name || u.email || fp)}">${esc(u.name || u.email || "—")}</span>${selfTag}
    </div>
    <div class="muted userNotesText">${esc(u.notes || "")}</div>
  </td>

  <td>${rolePill(u.role)}</td>
  <td>${pill(u.status)}</td>
  <td>${storageStateCellHtml(u)}</td>
  <td>${storagePoolCellHtml(u)}</td>
  <td class="mono userQuotaCell">${fmtQuotaCell(u)}</td>
  <td class="mono">${esc(u.added_at || "")}</td>

  <td class="row-actions">
    <span class="muted">${esc(tr("admin.users.open", null, "Open ▸"))}</span>
  </td>
</tr>
${detailRow}
`.trim();

    }).join("");

    updateAdminUsersSortIndicators();
    syncAdminUsersBulkUi();

    tb.querySelectorAll("input[data-select-fp]").forEach(box => {
        box.addEventListener("change", (ev) => {
            ev.stopPropagation();
            const fp = box.getAttribute("data-select-fp") || "";
            if (!fp) return;

            if (box.checked) selectedFingerprints.add(fp);
            else selectedFingerprints.delete(fp);

            syncAdminUsersBulkUi();
            setMsg(adminUsersLoadedMessage());
        });
    });

    // ✅ Attach avatar modal click via delegation (works across rerenders)
    tb.onclick = (ev) => {
        const img = ev.target?.closest?.('img[data-avatar-open="1"]');
        if (!img) return;
        ev.stopPropagation();
        const fp = img.getAttribute("data-fp") || "";
        const src = img.getAttribute("src") || "";
        if (!src) return;
        openAvatarModal(fp, src);
    };


// -------------------- Edit button: load user into form --------------------
    tb.querySelectorAll("button[data-edit]").forEach(btn => {
        btn.addEventListener("click", (ev) => {
            ev.stopPropagation();

            const fp = btn.getAttribute("data-edit");
            if (!fp) return;

            const u = allUsers.find(x => String(x.fingerprint || "") === fp);
            if (!u) return;

            setProfileEditorOpen(true);

            // Fill the edit form. Fingerprint is identity and must stay immutable.
            adminUsersEditFingerprint = fp;
            $("fp").value = fp;
            $("fp").readOnly = true;
            $("fp").setAttribute("aria-readonly", "true");
            $("fp").title = tr("admin.users.fp_readonly_title", null, "Fingerprint is the immutable user identity and cannot be changed by admin.");
            $("name").value = u.name || "";
            $("role").value = (u.role || "user");
            $("notes").value = u.notes || "";
            $("email").value = u.email || "";
            $("avatar_url").value = u.avatar_url || "";

            // bring it into view + focus
            $("fp").scrollIntoView({ behavior: "smooth", block: "center" });
            $("name").focus();
        });
    });


    // Expand/collapse handlers
    tb.querySelectorAll("button[data-exp]").forEach(btn => {
        btn.addEventListener("click", (ev) => {
            ev.stopPropagation();
            const fp = btn.getAttribute("data-exp");
            if (!fp) return;
            if (openUsers.has(fp)) openUsers.delete(fp);
            else openUsers.add(fp);
            render(); // re-render keeps multiple open
        });
    });

    // Optional: clicking the row toggles (but don't toggle when clicking action buttons)
    tb.querySelectorAll("tr.userRow").forEach(tr => {
        tr.addEventListener("click", (ev) => {
            const t = ev.target;
            if (t && (t.closest("button") || t.closest("a") || t.closest("input") || t.closest("select") || t.closest("textarea"))) {
                return; // let controls work normally
            }
            const fp = tr.getAttribute("data-fp");
            if (!fp) return;
            if (openUsers.has(fp)) openUsers.delete(fp);
            else openUsers.add(fp);
            render();
        });
    });

    // Existing action buttons (enable/disable/revoke/allocate/delete)
    tb.querySelectorAll("button[data-act]").forEach(b => {
        b.addEventListener("click", async () => {
            const fp = b.getAttribute("data-fp");
            const act = b.getAttribute("data-act");
            if (!fp || !act) return;

            const isSelf = actorFp && fp === actorFp;
            if (isSelf && (act === "enable" || act === "disable" || act === "revoke" || act === "delete")) {
                showToast(tr("admin.users.refuse_self", null, "Refusing to modify your own admin entry (prevents lockout or role change)."), 9000);
                return;
            }

            if (act === "delete") {
                const targetUser = allUsers.find(x => String(x.fingerprint || "") === String(fp)) || {};
                const ok = await openAdminUsersConfirmModal({
                    title: tr("admin.users.delete_confirm_title", null, "Delete user entry?"),
                    subtitle: tr("admin.users.delete_confirm_sub", null, "This removes the entry from users.json."),
                    rows: [
                        { label: tr("admin.users.user", null, "User"), value: String(targetUser.name || targetUser.email || fp), mono: true },
                        { label: tr("admin.users.fingerprint", null, "Fingerprint"), value: fp, mono: true },
                        { label: tr("admin.users.status", null, "Status"), value: statusLabel(targetUser.status || "—") },
                    ],
                    note: tr("admin.users.delete_confirm_note", null, "This removes the entry entirely as cleanup. If they scan again, they will re-appear as disabled."),
                    confirmText: tr("admin.users.delete_user", null, "Delete user"),
                    cancelText: tr("admin.users.cancel", null, "Cancel"),
                    danger: true,
                });
                if (!ok) return;

                try {
                    setMsg(tr("admin.users.deleting", null, "Deleting…"));
                    await apiPost("/api/v4/admin/users/delete", { fingerprint: fp });
                    await refresh();
                    setMsg(tr("admin.users.delete_ok", null, "Delete OK"));
                    showToast(tr("admin.users.user_deleted", null, "User deleted"));
                } catch (e) {
                    setMsg(tr("admin.users.error", { error: e.message }, "Error: " + e.message));
                    showToast(tr("admin.users.delete_failed", { error: e.message }, "Delete failed: " + e.message), 15000);
                }
                return;
            }

            if (act === "enable") {
                try {
                    setMsg(tr("admin.users.enabling", null, "Enabling…"));
                    await apiPost("/api/v4/admin/users/enable", { fingerprint: fp });
                    await refresh();
                    setMsg(tr("admin.users.enabled", null, "Enabled"));
                    showToast(tr("admin.users.user_enabled", null, "User enabled"));
                } catch (e) {
                    showToast(tr("admin.users.failed", { error: e.message }, "Failed: " + e.message), 15000);
                    setMsg(tr("admin.users.error", { error: e.message }, "Error: " + e.message));
                }
                return;
            }

            if (act === "allocate") {
                const cur = allUsers.find(x => String(x.fingerprint || "") === String(fp)) || {};
                openAllocModal(fp, cur);
                return;
            }
            if (act === "migrate") {
                const cur = allUsers.find(x => String(x.fingerprint || "") === String(fp)) || {};
                if (String(cur.storage_state || "").toLowerCase() !== "allocated") {
                    showToast(tr("admin.users.storage_required_migration", null, "Storage must be allocated before migration."), 7000);
                    return;
                }
                openMigrateModal(fp, cur);
                return;
            }
            if (act === "cleanup-old-copy") {
                await submitCleanupOldCopy(fp);
                return;
            }
            const status =
                (act === "disable") ? "disabled" :
                    (act === "revoke") ? "revoked" : "";

            if (!status) return;

            if (act === "revoke") {
                const targetUser = allUsers.find(x => String(x.fingerprint || "") === String(fp)) || {};
                const ok = await openAdminUsersConfirmModal({
                    title: tr("admin.users.revoke_confirm_title", null, "Revoke user?"),
                    subtitle: tr("admin.users.revoke_confirm_sub", null, "This hard-blocks login for this fingerprint."),
                    rows: [
                        { label: tr("admin.users.user", null, "User"), value: String(targetUser.name || targetUser.email || fp), mono: true },
                        { label: tr("admin.users.fingerprint", null, "Fingerprint"), value: fp, mono: true },
                        { label: tr("admin.users.current_status", null, "Current status"), value: statusLabel(targetUser.status || "—") },
                    ],
                    note: tr("admin.users.revoke_confirm_note", null, "Use this when this identity should not be allowed to log in again."),
                    confirmText: tr("admin.users.revoke_user", null, "Revoke user"),
                    cancelText: tr("admin.users.cancel", null, "Cancel"),
                    danger: true,
                });
                if (!ok) return;
            }

            try {
                setMsg(tr("admin.users.saving", null, "Saving…"));
                await apiPost("/api/v4/admin/users/status", { fingerprint: fp, status });
                await refresh();
                setMsg(tr("admin.users.saved", null, "Saved"));
                showToast(tr("admin.users.status_saved", { status: statusLabel(status) }, `User status: ${status}`));
            } catch (e) {
                showToast(tr("admin.users.failed", { error: e.message }, "Failed: " + e.message), 15000);
                setMsg(tr("admin.users.error", { error: e.message }, "Error: " + e.message));
            }
        });
    });
}

async function refresh() {
    setMsg(tr("admin.users.loading_users", null, "Loading users…"));
    const j = await apiGet("/api/v4/admin/users");
    actorFp = String(j.actor_fp || "");
    allUsers = (j.users || []).sort((a,b) => (a.fingerprint||"").localeCompare(b.fingerprint||""));
    adminUsersPruneSelection();
    refreshAdminUsersPoolFilter();

    // Keep quota-aware pool labels fresh after user metadata changes.
    // Migration switches storage_pool_id in users.json, so the bulk target
    // dropdown must be rebuilt from the newly loaded allUsers data.
    try {
        await refreshAdminUsersBulkDestPools();
    } catch (e) {
        console.warn("Admin Users pool label refresh failed:", e?.message || e);
    }

    refreshAdminUsersPoolFilter();
    render();
    renderAdminUsersStorageJobPanel();
    setMsg(adminUsersLoadedMessage());
}

async function upsertFromForm() {
    const visibleFp = ($("fp")?.value || "").trim();
    const fp = String(adminUsersEditFingerprint || visibleFp || "").trim();
    const name = ($("name")?.value || "").trim();
    const role = ($("role")?.value || "user").trim();
    const notes = ($("notes")?.value || "").trim();

    const email = ($("email")?.value || "").trim();
    const avatar_url = ($("avatar_url")?.value || "").trim(); // only if you add this input

    if (!fp || fp.length < 32) throw new Error(tr("admin.users.fp_invalid", null, "fingerprint looks invalid"));
    if (visibleFp && visibleFp !== fp) {
        throw new Error(tr("admin.users.fp_immutable", null, "Fingerprint cannot be changed. Select the correct user and edit profile fields only."));
    }

    await apiPost("/api/v4/admin/users/upsert", {
        fingerprint: fp,
        name,
        role,
        notes,
        email,
        avatar_url,
    });

    await refresh();
    setMsg(tr("admin.users.upsert_ok", null, "Upsert OK"));
    showToast(tr("admin.users.user_upserted", null, "User updated"));
}


window.addEventListener("load", async () => {
    adminUsersInstallConfirmThemeFix();
    $("btnRefresh")?.addEventListener("click", refresh);
    $("filter")?.addEventListener("input", () => {
        const search = $("adminUsersSearch");
        if (search && search.value !== $("filter").value) search.value = $("filter").value;
        adminUsersRefreshAfterFilterChange();
    });
    $("adminUsersSearch")?.addEventListener("input", adminUsersRefreshAfterFilterChange);
    $("adminUsersPoolFilter")?.addEventListener("change", adminUsersRefreshAfterFilterChange);
    $("adminUsersStorageFilter")?.addEventListener("change", adminUsersRefreshAfterFilterChange);
    $("adminUsersStatusFilter")?.addEventListener("change", adminUsersRefreshAfterFilterChange);
    $("showExternalUsers")?.addEventListener("change", adminUsersRefreshAfterFilterChange);

    $("adminUsersSelectVisible")?.addEventListener("change", (ev) => {
        adminUsersSelectVisible(!!ev.target.checked);
    });

    $("adminUsersClearFiltersBtn")?.addEventListener("click", () => {
        if ($("filter")) $("filter").value = "";
        if ($("adminUsersSearch")) $("adminUsersSearch").value = "";
        if ($("adminUsersPoolFilter")) $("adminUsersPoolFilter").value = "all";
        if ($("adminUsersStorageFilter")) $("adminUsersStorageFilter").value = "all";
        if ($("adminUsersStatusFilter")) $("adminUsersStatusFilter").value = "all";
        adminUsersRefreshAfterFilterChange();
    });

    $("adminUsersBulkClearBtn")?.addEventListener("click", () => {
        selectedFingerprints.clear();
        render();
        setMsg(adminUsersLoadedMessage());
    });

    $("adminUsersBulkMigrateBtn")?.addEventListener("click", () => {
        submitAdminUsersBulkMigration().catch(e => {
            setMsg(tr("admin.users.error", { error: e?.message || e }, "Error: " + (e?.message || e)));
            showToast(tr("admin.users.failed", { error: e?.message || e }, "Failed: " + (e?.message || e)), 15000);
        });
    });

    $("adminUsersBulkCleanupBtn")?.addEventListener("click", () => {
        submitAdminUsersBulkCleanupLastBatch().catch(e => {
            setMsg(tr("admin.users.error", { error: e?.message || e }, "Error: " + (e?.message || e)));
            showToast(tr("admin.users.failed", { error: e?.message || e }, "Failed: " + (e?.message || e)), 15000);
        });
    });

    $("adminUsersBulkPool")?.addEventListener("change", () => {
        syncAdminUsersBulkUi();
        syncAdminUsersBulkPoolCapacityUi();
    });
    refreshAdminUsersBulkDestPools().catch(() => {});

    setProfileEditorOpen(false);
    if ($("fp")) {
        $("fp").readOnly = true;
        $("fp").setAttribute("aria-readonly", "true");
        $("fp").title = tr("admin.users.fp_readonly_title", null, "Fingerprint is the immutable user identity and cannot be changed by admin.");
    }
    $("profileEditorToggle")?.addEventListener("click", () => {
        setProfileEditorOpen(!isProfileEditorOpen());
    });
    bindAdminUsersSortHeaders();

    $("btnAdd")?.addEventListener("click", async () => {
        setMsg("");
        try { await upsertFromForm(); }
        catch (e) { setMsg(tr("admin.users.error", { error: e.message }, "Error: " + e.message)); }
    });

    // ---------------- Avatar picker wiring ----------------
    // Avatar modal wiring
    $("avatarCloseBtn")?.addEventListener("click", closeAvatarModal);
    $("avatarModal")?.addEventListener("click", (ev) => {
        // click on backdrop closes; click inside card does not
        if (ev.target && ev.target.id === "avatarModal") closeAvatarModal();
    });
    document.addEventListener("keydown", (ev) => {
        if (ev.key === "Escape") closeAvatarModal();
    });

    // ---------- Allocate modal wiring ----------
    $("allocCancelBtn")?.addEventListener("click", closeAllocModal);
    $("allocSaveBtn")?.addEventListener("click", submitAllocationFromModal);

    $("allocModal")?.addEventListener("click", (ev) => {
        if (ev.target && ev.target.id === "allocModal") closeAllocModal(); // backdrop
    });
    $("migrateCancelBtn")?.addEventListener("click", closeMigrateModal);
    $("migrateSaveBtn")?.addEventListener("click", submitMigrationFromModal);

    $("migrateModal")?.addEventListener("click", (ev) => {
        if (ev.target && ev.target.id === "migrateModal") closeMigrateModal();
    });
    document.addEventListener("keydown", (ev) => {
        if (ev.key === "Escape") {
            const m1 = $("allocModal");
            if (m1 && m1.classList.contains("open")) closeAllocModal();

            const m2 = $("migrateModal");
            if (m2 && m2.classList.contains("open")) closeMigrateModal();
        }
    });
    $("avatarRemoveBtn")?.addEventListener("click", async () => {
        if (!avatarModalFp) return;

        const ok = await openAdminUsersConfirmModal({
            title: tr("admin.users.avatar_remove_confirm_title", null, "Remove this user's avatar?"),
            subtitle: tr("admin.users.avatar_remove_confirm_subtitle", null, "The profile will return to the default generated avatar."),
            rows: [
                { label: tr("admin.users.fingerprint", null, "Fingerprint"), value: avatarModalFp, mono: true },
            ],
            note: tr("admin.users.avatar_remove_confirm_note", null, "This removes the uploaded avatar image reference from this user profile."),
            confirmText: tr("admin.users.remove_avatar", null, "Remove avatar"),
            cancelText: tr("admin.users.cancel", null, "Cancel"),
            danger: true,
        });
        if (!ok) return;

        try {
            setMsg(tr("admin.users.removing_avatar", null, "Removing avatar…"));
            await apiPost("/api/v4/admin/users/avatar_remove", { fingerprint: avatarModalFp });
            closeAvatarModal();
            await refresh();
            setMsg(tr("admin.users.avatar_removed", null, "Avatar removed"));
            showToast(tr("admin.users.avatar_removed", null, "Avatar removed"));
        } catch (e) {
            setMsg(tr("admin.users.error", { error: e.message }, "Error: " + e.message));
            showToast(tr("admin.users.remove_failed", { error: e.message }, "Remove failed: " + e.message), 15000);
        }
    });

    $("avatar_url")?.addEventListener("click", () => {
        $("avatar_file")?.click();
    });

    $("avatar_file")?.addEventListener("change", async () => {
        const file = $("avatar_file").files?.[0];
        if (!file) return;

        const fp = ($("fp")?.value || "").trim();
        if (!fp || fp.length < 32) {
            showToast(tr("admin.users.select_user_first", null, "Select a user first (fingerprint missing)."));
            $("avatar_file").value = "";
            return;
        }

        try {
            setMsg(tr("admin.users.preparing_avatar", null, "Preparing avatar…"));

            const prepared = await prepareAdminAvatarUploadBlob(file);
            const data_b64 = await blobToBase64(prepared.blob);

            setMsg(tr("admin.users.uploading_avatar", null, "Uploading avatar…"));

            const body = {
                fingerprint: fp,
                filename: file.name || "avatar.jpg",
                mime: prepared.mime,
                data_b64,
            };

            const j = await apiPost("/api/v4/admin/users/avatar_upload", body);

            $("avatar_url").value = j.avatar_url || "";

            setMsg(tr("admin.users.avatar_uploaded_save", null, "Avatar uploaded (click Upsert to save)"));
            showToast(
                tr("admin.users.avatar_uploaded_toast", { note: prepared.note ? prepared.note + "\n" : "" }, "Avatar uploaded\n" + (prepared.note ? prepared.note + "\n" : "") + "Click Upsert to save this avatar URL into the user profile.")
            );
        } catch (e) {
            const msg = tr("admin.users.upload_failed", { error: e?.message || e }, "Upload failed: " + (e?.message || e));
            setMsg(tr("admin.users.error", { error: e?.message || e }, "Error: " + (e?.message || e)));
            showToast(msg, 15000);
        } finally {
            $("avatar_file").value = "";
        }
    });


    // ------------------------------------------------------

    try {
        await refresh();
        adminUsersRestoreStorageJobMonitors();
    }
    catch (e) { setMsg(tr("admin.users.failed", { error: e.message }, "Failed: " + e.message)); }
});



window.addEventListener("pqnas-language-changed", () => {
    applyStaticI18n();
    try {
        setProfileEditorOpen(isProfileEditorOpen());
        render();
        renderAdminUsersStorageJobPanel();
    } catch (_) {}
});
