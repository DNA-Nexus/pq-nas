// DNA-Nexus Update Center v1
(() => {
    const latestUrl = "https://api.github.com/repos/DNA-Nexus/pq-nas/releases/latest";

    const stateBadge = document.getElementById("stateBadge");
    const statusLine = document.getElementById("statusLine");
    const releaseBadge = document.getElementById("releaseBadge");
    const releaseLine = document.getElementById("releaseLine");
    const releaseBody = document.getElementById("releaseBody");
    const checkBtn = document.getElementById("checkBtn");
    const downloadBtn = document.getElementById("downloadBtn");
    const openReleaseBtn = document.getElementById("openReleaseBtn");

    let latestRelease = null;
    let preferredAsset = null;

    function setBadge(el, kind, text) {
        if (!el) return;
        el.className = `badge ${kind || ""}`.trim();
        el.textContent = text;
    }

    function chooseAsset(assets) {
        const arr = Array.isArray(assets) ? assets : [];

        function nameOf(a) {
            return String((a && a.name) || "").toLowerCase();
        }

        function isCorePackage(a) {
            const n = nameOf(a);

            // Existing DNA-Nexus / PQ-NAS release naming:
            //   pqnas-1.1.0-linux-x86_64.tar.gz
            //   pqnas-1.1.0-linux-amd64.tar.gz
            //
            // Future explicit names are also accepted:
            //   dna-nexus-server-1.1.0-linux-x86_64.dnxupd
            //   pqnas-server-1.1.0-linux-x86_64.tar.gz
            return (
                /^pqnas-[0-9][a-z0-9.\-_]*-linux-(x86_64|amd64)\.(tar\.gz|tgz|zip)$/i.test(n) ||
                n.includes("dna-nexus-server") ||
                n.includes("pqnas-server") ||
                n.includes("pq-nas-server") ||
                n.includes("pqnas_server") ||
                n.includes("server-update") ||
                n.endsWith(".dnxupd")
            );
        }

        // Prefer real core/server update packages. Do not accidentally pick app zips
        // such as dropzone, circle-stack, echo-stack, filemgr, etc.
        return arr.find(a => /\.dnxupd$/i.test(a.name || "")) ||
               arr.find(a => isCorePackage(a) && /\.(zip|tar\.gz|tgz)$/i.test(a.name || "")) ||
               null;
    }

    function cleanReleaseBody(body) {
        return String(body || "")
            .replace(/<img\b[^>]*>/gi, "")
            .replace(/!\[[^\]]*\]\([^)]*\)/g, "")
            .trim() || "No release notes provided.";
    }

    function fmtDate(s) {
        const d = new Date(s || "");
        return Number.isFinite(d.getTime()) ? d.toLocaleString() : "unknown date";
    }

    async function checkRelease() {
        try {
            setBadge(stateBadge, "warn", "checking…");
            statusLine.textContent = "Checking GitHub releases…";
            checkBtn.disabled = true;
            downloadBtn.disabled = true;
            openReleaseBtn.disabled = true;

            const r = await fetch(latestUrl, {
                cache: "no-store",
                headers: { "Accept": "application/vnd.github+json" },
            });

            const j = await r.json().catch(() => null);
            if (!r.ok || !j) {
                throw new Error(j && j.message ? j.message : `GitHub HTTP ${r.status}`);
            }

            latestRelease = j;
            preferredAsset = chooseAsset(j.assets);

            setBadge(stateBadge, "ok", "ready");
            setBadge(releaseBadge, preferredAsset ? "warn" : "info", preferredAsset ? "core package found" : "no core package asset");
            releaseLine.textContent = `${j.tag_name || j.name || "release"} • published ${fmtDate(j.published_at || j.created_at)}`;
            releaseBody.textContent = cleanReleaseBody(j.body);

            downloadBtn.disabled = !preferredAsset || !preferredAsset.browser_download_url;
            openReleaseBtn.disabled = !j.html_url;

            statusLine.textContent = preferredAsset
                ? `Preferred package: ${preferredAsset.name}`
                : "Release loaded, but no core/server update package asset was found.";
        } catch (e) {
            setBadge(stateBadge, "err", "error");
            setBadge(releaseBadge, "err", "check failed");
            statusLine.textContent = String(e && e.message ? e.message : e);
            releaseLine.textContent = "Could not load GitHub release data.";
            releaseBody.textContent = "Check network access from this browser/server environment.";
        } finally {
            checkBtn.disabled = false;
        }
    }

    checkBtn?.addEventListener("click", checkRelease);

    downloadBtn?.addEventListener("click", () => {
        if (preferredAsset && preferredAsset.browser_download_url) {
            window.open(preferredAsset.browser_download_url, "_blank", "noopener");
        }
    });

    openReleaseBtn?.addEventListener("click", () => {
        if (latestRelease && latestRelease.html_url) {
            window.open(latestRelease.html_url, "_blank", "noopener");
        }
    });

    setBadge(stateBadge, "warn", "loading…");
    statusLine.textContent = "Auto-checking latest release…";
    checkRelease();
})();


// DNA-Nexus Update Center manual upload v1
(() => {
    const fileInput = document.getElementById("manualPackageFile");
    const uploadBtn = document.getElementById("manualUploadBtn");
    const refreshBtn = document.getElementById("refreshUploadsBtn");
    const statusEl = document.getElementById("manualUploadStatus");

    if (!fileInput || !uploadBtn || !refreshBtn || !statusEl) {
        return;
    }

    function fmtBytes(n) {
        const v = Number(n || 0);
        if (!Number.isFinite(v) || v <= 0) return "0 B";
        const units = ["B", "KB", "MB", "GB"];
        let x = v;
        let i = 0;
        while (x >= 1024 && i < units.length - 1) {
            x /= 1024;
            i++;
        }
        return `${x.toFixed(i === 0 ? 0 : 1)} ${units[i]}`;
    }

    function renderJson(prefix, obj) {
        statusEl.textContent = `${prefix}\n` + JSON.stringify(obj, null, 2);
    }

    async function refreshUploadedPackages() {
        try {
            statusEl.textContent = "Loading uploaded packages…";

            const r = await fetch("/api/v4/admin/updates/status", {
                credentials: "include",
                cache: "no-store",
            });

            const j = await r.json().catch(() => null);
            if (!r.ok || !j || !j.ok) {
                throw new Error(j && (j.message || j.error) ? `${j.error || ""} ${j.message || ""}`.trim() : `HTTP ${r.status}`);
            }

            const items = Array.isArray(j.incoming) ? j.incoming : [];
            if (!items.length) {
                statusEl.textContent = "No uploaded update packages staged on this server.";
                return;
            }

            statusEl.textContent = items
                .map(it => `${it.name || "(unnamed)"} — ${fmtBytes(it.size)}`)
                .join("\n");
        } catch (e) {
            statusEl.textContent = "Failed to load uploaded packages: " + String(e && e.message ? e.message : e);
        }
    }

    async function uploadPackage() {
        const f = fileInput.files && fileInput.files[0] ? fileInput.files[0] : null;
        if (!f) {
            statusEl.textContent = "Choose a pqnas-*.tar.gz / .tgz / .zip / .dnxupd package first.";
            return;
        }

        try {
            uploadBtn.disabled = true;
            statusEl.textContent = `Uploading ${f.name} (${fmtBytes(f.size)})…`;

            const r = await fetch("/api/v4/admin/updates/upload", {
                method: "POST",
                credentials: "include",
                headers: {
                    "Content-Type": "application/octet-stream",
                    "X-PQNAS-Filename": f.name,
                },
                body: f,
            });

            const j = await r.json().catch(() => null);
            if (!r.ok || !j || !j.ok) {
                throw new Error(j && (j.message || j.error) ? `${j.error || ""} ${j.message || ""}`.trim() : `HTTP ${r.status}`);
            }

            renderJson("Upload staged successfully. Nothing has been installed yet.", j);
            await refreshUploadedPackages();
        } catch (e) {
            statusEl.textContent = "Upload failed: " + String(e && e.message ? e.message : e);
        } finally {
            uploadBtn.disabled = false;
        }
    }

    uploadBtn.addEventListener("click", uploadPackage);
    refreshBtn.addEventListener("click", refreshUploadedPackages);

    refreshUploadedPackages();
})();


// DNA-Nexus Update Center verify package v1
(() => {
    const verifyBtn = document.getElementById("verifyPackageBtn");
    const statusEl = document.getElementById("manualUploadStatus");

    if (!verifyBtn || !statusEl) {
        return;
    }

    function pickStoredNameFromStatus() {
        const text = String(statusEl.textContent || "");
        const m = text.match(/([0-9a-f]{12}_[A-Za-z0-9._-]+\.(?:tar\.gz|tgz|zip|dnxupd))/);
        return m ? m[1] : "";
    }

    async function verifyPackage() {
        const storedName = pickStoredNameFromStatus();
        if (!storedName) {
            statusEl.textContent = "No staged package selected. Upload or refresh packages first.";
            return;
        }

        try {
            verifyBtn.disabled = true;
            statusEl.textContent = `Verifying ${storedName}…`;

            const r = await fetch("/api/v4/admin/updates/verify", {
                method: "POST",
                credentials: "include",
                headers: {
                    "Content-Type": "application/json",
                },
                body: JSON.stringify({ stored_name: storedName }),
            });

            const j = await r.json().catch(() => null);
            if (!j) {
                throw new Error(`HTTP ${r.status}`);
            }

            statusEl.textContent =
                (j.ok ? "Verification OK. Nothing has been installed yet.\n" : "Verification failed.\n") +
                JSON.stringify(j, null, 2);
        } catch (e) {
            statusEl.textContent = "Verification failed: " + String(e && e.message ? e.message : e);
        } finally {
            verifyBtn.disabled = false;
        }
    }

    verifyBtn.addEventListener("click", verifyPackage);
})();


// DNA-Nexus Update Center install plan v1
(() => {
    const planBtn = document.getElementById("planPackageBtn");
    const statusEl = document.getElementById("manualUploadStatus");

    if (!planBtn || !statusEl) {
        return;
    }

    function pickStoredNameFromStatus() {
        const text = String(statusEl.textContent || "");
        const m = text.match(/([0-9a-f]{12}_[A-Za-z0-9._-]+\.(?:tar\.gz|tgz|zip|dnxupd))/);
        return m ? m[1] : "";
    }

    function escHtml(s) {
        return String(s ?? "").replace(/[&<>"]/g, c => ({
            "&": "&amp;",
            "<": "&lt;",
            ">": "&gt;",
            "\"": "&quot;",
        }[c]));
    }

    function planLineClass(action) {
        const a = String(action || "").toLowerCase();

        if (
            a === "update" ||
            a === "update_existing_app" ||
            a === "update_existing_app_package" ||
            a.includes("update")
        ) {
            return "update";
        }

        if (
            a === "skip" ||
            a === "skip_not_installed" ||
            a === "reject" ||
            a.startsWith("skip")
        ) {
            return "skip";
        }

        return "other";
    }

    function summarizePlan(j) {
        const actions = Array.isArray(j.actions) ? j.actions : [];
        const first = actions.slice(0, 180);

        const summary = [
            "Install plan built. Nothing has been installed yet.",
            "",
            `Package: ${j.stored_name || ""}`,
            `Plan ID: ${j.plan_id || ""}`,
            `Plan hash: ${j.plan_hash || ""}`,
            `Package SHA256: ${j.package_sha256 || ""}`,
            `Package version: ${j.package_server_version || ""}`,
            `Current server version: ${j.current_server_version || ""}`,
            `Entries: ${j.entry_count || 0}`,
            `Planned updates: ${j.planned_updates || 0}`,
            `Skipped: ${j.skipped || 0}`,
            `Core binary action: ${j.has_core_binary_action ? "yes" : "no"}`,
        ].filter(x => x !== null && x !== undefined).join("\n");

        const lines = first.map(a => {
            const app = a.app_id ? ` app=${a.app_id}` : "";
            const target = a.target ? ` -> ${a.target}` : "";
            const reason = a.reason ? ` (${a.reason})` : "";
            const text = `- [${a.action}] ${a.type}${app}: ${a.source}${target}${reason}`;
            return `<div class="planLine ${planLineClass(a.action)}">${escHtml(text)}</div>`;
        });

        if (actions.length > first.length) {
            lines.push(`<div class="planLine other">${escHtml(`... ${actions.length - first.length} more actions not shown in UI preview`)}</div>`);
        }

        return `
            <div class="planPreview">
                <div class="planSummary">${escHtml(summary)}</div>
                <div class="planActions">${lines.join("")}</div>
            </div>
        `;
    }

    async function buildPlan() {
        const storedName = pickStoredNameFromStatus();
        if (!storedName) {
            statusEl.textContent = "No staged package selected. Upload or refresh packages first.";
            return;
        }

        try {
            planBtn.disabled = true;
            statusEl.textContent = `Building install plan for ${storedName}…`;

            const r = await fetch("/api/v4/admin/updates/plan", {
                method: "POST",
                credentials: "include",
                headers: {
                    "Content-Type": "application/json",
                },
                body: JSON.stringify({ stored_name: storedName }),
            });

            const j = await r.json().catch(() => null);
            if (!j) {
                throw new Error(`HTTP ${r.status}`);
            }

            if (!r.ok || !j.ok) {
                statusEl.textContent = "Plan failed.\n" + JSON.stringify(j, null, 2);
                return;
            }

            statusEl.innerHTML = summarizePlan(j);
        } catch (e) {
            statusEl.textContent = "Plan failed: " + String(e && e.message ? e.message : e);
        } finally {
            planBtn.disabled = false;
        }
    }

    planBtn.addEventListener("click", buildPlan);
})();


// DNA-Nexus Update Center install validation v1
(() => {
    const installBtn = document.getElementById("installPlanBtn");
    const statusEl = document.getElementById("manualUploadStatus");

    if (!installBtn || !statusEl) {
        return;
    }

    function escHtml(s) {
        return String(s ?? "").replace(/[&<>"]/g, c => ({
            "&": "&amp;",
            "<": "&lt;",
            ">": "&gt;",
            "\"": "&quot;",
        }[c]));
    }

    function pickPlanIdFromStatus() {
        const text = String(statusEl.textContent || "");
        const m = text.match(/Plan ID:\s*([A-Za-z0-9._-]+)/);
        return m ? m[1] : "";
    }

    function renderInstallValidation(j) {
        const ok = !!j.ok;
        const errors = Array.isArray(j.validation_errors) ? j.validation_errors : [];
        const actions = Array.isArray(j.applicable_actions) ? j.applicable_actions : [];

        const head = [
            ok ? "Install validation OK. Nothing has been installed yet." : "Install validation failed. Nothing has been installed.",
            "",
            `Plan ID: ${j.plan_id || ""}`,
            `Plan hash: ${j.plan_hash || ""}`,
            `Package SHA256: ${j.package_sha256 || ""}`,
            `Package version: ${j.package_server_version || ""}`,
            `Current server version: ${j.current_server_version || ""}`,
            `Applicable actions: ${j.applicable_action_count || 0}`,
            `Install helper enabled: ${j.helper_enabled ? "yes" : "no"}`,
            j.helper_exit_code === undefined ? null : `Helper exit code: ${j.helper_exit_code}`,
        ].join("\n");

        const errorHtml = errors.length
            ? `<div class="planActions">${errors.map(e => {
                const msg = `[${e.code || "error"}] ${e.message || ""}`;
                return `<div class="planLine skip">${escHtml(msg)}</div>`;
            }).join("")}</div>`
            : "";

        const actionHtml = actions.length
            ? `<div class="planActions">${actions.slice(0, 100).map(a => {
                const app = a.app_id ? ` app=${a.app_id}` : "";
                const target = a.target ? ` -> ${a.target}` : "";
                const reason = a.reason ? ` (${a.reason})` : "";
                const msg = `- [${a.action}] ${a.type}${app}: ${a.source}${target}${reason}`;
                return `<div class="planLine update">${escHtml(msg)}</div>`;
            }).join("")}</div>`
            : "";

        return `
            <div class="planPreview">
                <div class="planSummary">${escHtml(head)}</div>
                ${errorHtml}
                ${actionHtml}
            </div>
        `;
    }

    async function validateInstallPlan() {
        const planId = pickPlanIdFromStatus();
        if (!planId) {
            statusEl.textContent = "No saved install plan selected. Build install plan first.";
            return;
        }

        try {
            installBtn.disabled = true;
            statusEl.textContent = `Validating install plan ${planId}…`;

            const r = await fetch("/api/v4/admin/updates/install", {
                method: "POST",
                credentials: "include",
                headers: {
                    "Content-Type": "application/json",
                },
                body: JSON.stringify({ plan_id: planId }),
            });

            const j = await r.json().catch(() => null);
            if (!j) {
                throw new Error(`HTTP ${r.status}`);
            }

            statusEl.innerHTML = renderInstallValidation(j);
        } catch (e) {
            statusEl.textContent = "Install validation failed: " + String(e && e.message ? e.message : e);
        } finally {
            installBtn.disabled = false;
        }
    }

    installBtn.addEventListener("click", validateInstallPlan);
})();


// DNA-Nexus Update Center dry-run install v1
(() => {
    const dryRunBtn = document.getElementById("dryRunPlanBtn");
    const statusEl = document.getElementById("manualUploadStatus");

    if (!dryRunBtn || !statusEl) {
        return;
    }

    function escHtml(s) {
        return String(s ?? "").replace(/[&<>"]/g, c => ({
            "&": "&amp;",
            "<": "&lt;",
            ">": "&gt;",
            "\"": "&quot;",
        }[c]));
    }

    function pickPlanIdFromStatus() {
        const text = String(statusEl.textContent || "");
        const m = text.match(/Plan ID:\s*([A-Za-z0-9._-]+)/);
        return m ? m[1] : "";
    }

    function renderDryRun(j) {
        const planned = Array.isArray(j.planned_actions) ? j.planned_actions : [];
        const ok = !!j.ok;

        const head = [
            ok ? "Dry-run OK. No files were modified." : "Dry-run failed. No files were modified.",
            "",
            `Plan ID: ${j.plan_id || ""}`,
            `Plan hash: ${j.plan_hash || ""}`,
            `Package SHA256: ${j.package_sha256 || ""}`,
            `Package version: ${j.package_server_version || ""}`,
            `Current server version: ${j.current_server_version || ""}`,
            `Applicable actions: ${j.applicable_action_count || 0}`,
            `Planned actions: ${j.planned_action_count || planned.length || 0}`,
            `Install helper enabled: ${j.helper_enabled ? "yes" : "no"}`,
            j.helper_exit_code === undefined ? null : `Helper exit code: ${j.helper_exit_code}`,
            `Install performed: ${j.install_performed ? "yes" : "no"}`,
            j.error ? `Error: ${j.error}` : null,
            j.message ? `Message: ${j.message}` : null,
        ].filter(x => x !== null && x !== undefined).join("\n");

        const plannedHtml = planned.length
            ? `<div class="planActions">${planned.slice(0, 200).map(a => {
                const replace = a.would_replace === false ? "same" : "replace";
                const msg = `- [${a.type || ""}] ${a.source || ""} -> ${a.target || ""} (${replace})`;
                return `<div class="planLine update">${escHtml(msg)}</div>`;
            }).join("")}</div>`
            : "";

        return `
            <div class="planPreview">
                <div class="planSummary">${escHtml(head)}</div>
                ${plannedHtml}
            </div>
        `;
    }

    async function dryRunInstallPlan() {
        const planId = pickPlanIdFromStatus();
        if (!planId) {
            statusEl.textContent = "No saved install plan selected. Build install plan first.";
            return;
        }

        try {
            dryRunBtn.disabled = true;
            statusEl.textContent = `Running update dry-run for ${planId}…`;

            const r = await fetch("/api/v4/admin/updates/dry-run", {
                method: "POST",
                credentials: "include",
                headers: {
                    "Content-Type": "application/json",
                },
                body: JSON.stringify({ plan_id: planId }),
            });

            const j = await r.json().catch(() => null);
            if (!j) {
                throw new Error(`HTTP ${r.status}`);
            }

            statusEl.innerHTML = renderDryRun(j);
        } catch (e) {
            statusEl.textContent = "Dry-run failed: " + String(e && e.message ? e.message : e);
        } finally {
            dryRunBtn.disabled = false;
        }
    }

    dryRunBtn.addEventListener("click", dryRunInstallPlan);
})();

// DNA-Nexus Update Center apply update v1
(() => {
    const applyBtn = document.getElementById("applyPlanBtn");
    const statusEl = document.getElementById("manualUploadStatus");

    if (!applyBtn || !statusEl) {
        return;
    }

    function escHtml(s) {
        return String(s ?? "").replace(/[&<>"]/g, c => ({
            "&": "&amp;",
            "<": "&lt;",
            ">": "&gt;",
            "\"": "&quot;",
        }[c]));
    }

    function pickPlanIdFromStatus() {
        const text = String(statusEl.textContent || "");
        const m = text.match(/Plan ID:\s*([A-Za-z0-9._-]+)/);
        return m ? m[1] : "";
    }

    function renderApply(j) {
        const ok = !!j.ok;

        const head = [
            ok ? "Apply OK." : "Apply failed.",
            "",
            `Plan ID: ${j.plan_id || ""}`,
            `Plan hash: ${j.plan_hash || ""}`,
            `Package SHA256: ${j.package_sha256 || ""}`,
            `Package version: ${j.package_server_version || ""}`,
            `Current server version: ${j.current_server_version || ""}`,
            `Applicable actions: ${j.applicable_action_count || 0}`,
            j.applied_action_count === undefined ? null : `Applied actions: ${j.applied_action_count}`,
            `Install helper enabled: ${j.helper_enabled ? "yes" : "no"}`,
            `Apply enabled: ${j.apply_enabled ? "yes" : "no"}`,
            j.helper_exit_code === undefined ? null : `Helper exit code: ${j.helper_exit_code}`,
            `Install performed: ${j.install_performed ? "yes" : "no"}`,
            j.restart_required === undefined ? null : `Restart required: ${j.restart_required ? "yes" : "no"}`,
            j.backup_root ? `Backup root: ${j.backup_root}` : null,
            j.manifest_path ? `Manifest: ${j.manifest_path}` : null,
            j.error ? `Error: ${j.error}` : null,
            j.message ? `Message: ${j.message}` : null,
        ].filter(x => x !== null && x !== undefined).join("\n");

        return `
            <div class="planPreview">
                <div class="planSummary">${escHtml(head)}</div>
            </div>
        `;
    }

    function showUpdateApplyConfirmModal(planId) {
        return new Promise((resolve) => {
            const existing = document.querySelector(".updateApplyModalOverlay");
            if (existing) existing.remove();

            const overlay = document.createElement("div");
            overlay.className = "updateApplyModalOverlay";
            overlay.style.cssText = `
                position: fixed;
                inset: 0;
                z-index: 99999;
                background: rgba(0, 0, 0, 0.62);
                display: flex;
                align-items: center;
                justify-content: center;
                padding: 24px;
                box-sizing: border-box;
            `;

            const modal = document.createElement("div");
            modal.className = "updateApplyModal";
            modal.style.cssText = `
                width: min(620px, 96vw);
                border-radius: 18px;
                border: 1px solid rgba(255, 255, 255, 0.20);
                background: linear-gradient(180deg, rgba(32, 34, 38, 0.98), rgba(18, 20, 24, 0.98));
                color: #f5f7fb;
                box-shadow: 0 28px 80px rgba(0,0,0,0.55);
                overflow: hidden;
                font-family: inherit;
            `;

            modal.innerHTML = `
                <div style="
                    padding: 18px 22px;
                    border-bottom: 1px solid rgba(255,255,255,0.12);
                    display: flex;
                    align-items: center;
                    gap: 12px;
                ">
                    <div style="
                        width: 42px;
                        height: 42px;
                        border-radius: 999px;
                        display: flex;
                        align-items: center;
                        justify-content: center;
                        background: rgba(255, 176, 0, 0.16);
                        border: 1px solid rgba(255, 176, 0, 0.45);
                        font-size: 22px;
                    ">⚠️</div>
                    <div>
                        <div style="font-size: 19px; font-weight: 800;">Apply update?</div>
                        <div style="font-size: 13px; opacity: 0.72; margin-top: 2px;">
                            This action may replace static files and the server binary.
                        </div>
                    </div>
                </div>

                <div style="padding: 20px 22px 8px 22px;">
                    <div style="
                        font-size: 13px;
                        opacity: 0.72;
                        margin-bottom: 7px;
                        font-weight: 700;
                        text-transform: uppercase;
                        letter-spacing: 0.04em;
                    ">Plan ID</div>
                    <div style="
                        padding: 12px 14px;
                        border-radius: 12px;
                        background: rgba(255,255,255,0.07);
                        border: 1px solid rgba(255,255,255,0.12);
                        font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace;
                        font-size: 13px;
                        line-height: 1.45;
                        word-break: break-all;
                    ">${String(planId).replace(/[&<>"]/g, c => ({
                        "&": "&amp;",
                        "<": "&lt;",
                        ">": "&gt;",
                        "\"": "&quot;",
                    }[c]))}</div>

                    <div style="
                        margin-top: 16px;
                        padding: 13px 14px;
                        border-radius: 12px;
                        background: rgba(255, 70, 70, 0.12);
                        border: 1px solid rgba(255, 90, 90, 0.34);
                        color: #ffd7d7;
                        font-size: 14px;
                        line-height: 1.45;
                    ">
                        Continue only if dry-run succeeded and the plan looks correct.
                        The update helper will still validate the immutable plan before applying anything.
                    </div>
                </div>

                <div style="
                    padding: 18px 22px 22px 22px;
                    display: flex;
                    justify-content: flex-end;
                    gap: 12px;
                ">
                    <button type="button" class="updateApplyCancel" style="
                        border: 1px solid rgba(255,255,255,0.22);
                        background: rgba(255,255,255,0.08);
                        color: #f5f7fb;
                        border-radius: 999px;
                        padding: 10px 18px;
                        cursor: pointer;
                        font-weight: 700;
                    ">Cancel</button>
                    <button type="button" class="updateApplyConfirm" style="
                        border: 1px solid rgba(255, 80, 80, 0.7);
                        background: linear-gradient(180deg, #ff5d5d, #d82929);
                        color: white;
                        border-radius: 999px;
                        padding: 10px 18px;
                        cursor: pointer;
                        font-weight: 800;
                        box-shadow: 0 10px 24px rgba(216, 41, 41, 0.28);
                    ">Apply update</button>
                </div>
            `;

            overlay.appendChild(modal);
            document.body.appendChild(overlay);

            const cleanup = (value) => {
                document.removeEventListener("keydown", onKey, true);
                overlay.remove();
                resolve(value);
            };

            const onKey = (ev) => {
                if (ev.key === "Escape") {
                    ev.preventDefault();
                    cleanup(false);
                }
                if (ev.key === "Enter") {
                    ev.preventDefault();
                    cleanup(true);
                }
            };

            overlay.addEventListener("click", (ev) => {
                if (ev.target === overlay) cleanup(false);
            });

            modal.querySelector(".updateApplyCancel")?.addEventListener("click", () => cleanup(false));
            modal.querySelector(".updateApplyConfirm")?.addEventListener("click", () => cleanup(true));

            document.addEventListener("keydown", onKey, true);
            setTimeout(() => modal.querySelector(".updateApplyConfirm")?.focus(), 0);
        });
    }

    async function applyUpdatePlan() {
        const planId = pickPlanIdFromStatus();
        if (!planId) {
            statusEl.textContent = "No saved install plan selected. Build install plan and dry-run it first.";
            return;
        }

        const ok = await showUpdateApplyConfirmModal(planId);
        if (!ok) return;

        try {
            applyBtn.disabled = true;
            statusEl.textContent = `Applying update for ${planId}…`;

            const r = await fetch("/api/v4/admin/updates/apply", {
                method: "POST",
                credentials: "include",
                headers: {
                    "Content-Type": "application/json",
                },
                body: JSON.stringify({ plan_id: planId }),
            });

            const j = await r.json().catch(() => null);
            if (!j) {
                throw new Error(`HTTP ${r.status}`);
            }

            statusEl.innerHTML = renderApply(j);
        } catch (e) {
            statusEl.textContent = "Apply failed: " + String(e && e.message ? e.message : e);
        } finally {
            applyBtn.disabled = false;
        }
    }

    applyBtn.addEventListener("click", applyUpdatePlan);
})();
