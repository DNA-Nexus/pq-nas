// DNA-Nexus Update Center v1

function updateCenterT(key, vars, fallback) {
    const raw = String(key || "");
    const fullKey = raw.startsWith("admin.") || raw.startsWith("common.")
        ? raw
        : `admin.updates.${raw}`;

    try {
        const api = window.PQNAS_I18N;
        if (api && typeof api.t === "function") {
            return api.t(fullKey, vars || null, fallback);
        }
    } catch (_) {}

    return String(fallback ?? fullKey);
}

function updateCenterReady(fn) {
    const ready = window.PQNAS_I18N && typeof window.PQNAS_I18N.ready === "function"
        ? window.PQNAS_I18N.ready()
        : Promise.resolve();

    ready.then(fn).catch(fn);
}

function updateCenterBackendMessage(msg) {
    const text = String(msg || "");

    if (text === "Update helper applied supported update actions. Restart may be required.") {
        return updateCenterT(
            "message.apply_success_restart_may_be_required",
            {},
            text
        );
    }

    if (text === "Update helper validated plan and package. Nothing was installed in validation-only mode.") {
        return updateCenterT(
            "message.validation_success_nothing_installed",
            {},
            text
        );
    }

    if (text === "Dry-run succeeded. No files were modified.") {
        return updateCenterT(
            "message.dry_run_success_no_files_modified",
            {},
            text
        );
    }

    if (text === "Plan has no installable update actions.") {
        return updateCenterT(
            "message.no_installable_update_actions",
            {},
            text
        );
    }

    if (text === "Plan contains reject actions; refusing dry-run.") {
        return updateCenterT(
            "message.reject_actions_refusing_dry_run",
            {},
            text
        );
    }

    if (text === "Plan contains reject actions; refusing apply.") {
        return updateCenterT(
            "message.reject_actions_refusing_apply",
            {},
            text
        );
    }

    if (text === "Plan contains reject actions; refusing install.") {
        return updateCenterT(
            "message.reject_actions_refusing_install",
            {},
            text
        );
    }

    return text;
}



function updateCenterBackendError(code) {
    const text = String(code || "");

    if (text === "no_applicable_actions") {
        return updateCenterT(
            "error_code.no_applicable_actions",
            {},
            text
        );
    }

    if (text === "reject_action_present") {
        return updateCenterT(
            "error_code.reject_action_present",
            {},
            text
        );
    }

    return text;
}

function updateCenterValidationErrorMessage(e) {
    const obj = e && typeof e === "object" ? e : {};
    const code = String(obj.code || "");
    const action = obj.action && typeof obj.action === "object" ? obj.action : {};
    const rawMessage = String(obj.message || action.reason || "").trim();
    const rawReason = String(action.reason || rawMessage).trim();

    const tooOld = rawReason.match(
        /current server version\s+([0-9A-Za-z._+-]+)\s+is too old for this update;\s*minimum required version is\s+([0-9A-Za-z._+-]+)/i
    );

    if (code === "reject_action_present" && tooOld) {
        return updateCenterT(
            "error.current_version_too_old",
            { current: tooOld[1], min: tooOld[2] },
            `Current server version ${tooOld[1]} is too old for this update. Install version ${tooOld[2]} first.`
        );
    }

    if (code === "reject_action_present") {
        return updateCenterT(
            "error.reject_action_present",
            { message: rawMessage || rawReason || "" },
            rawMessage || rawReason || "The update plan was rejected."
        );
    }

    return rawMessage || updateCenterT(
        "error.unknown_validation_error",
        null,
        "Unknown validation error."
    );
}

function updateCenterYesNo(v) {
    return updateCenterT(v ? "value.yes" : "value.no", null, v ? "yes" : "no");
}

function updateCenterLabel(key, value, fallback) {
    return `${updateCenterT(`label.${key}`, null, fallback)}: ${value}`;
}

function updateCenterPlanIdFromStatus(statusEl) {
    const text = String((statusEl && statusEl.textContent) || "");
    const fromDataset = String((statusEl && statusEl.dataset && statusEl.dataset.planId) || "").trim();

    if (fromDataset && text.includes(fromDataset)) {
        return fromDataset;
    }

    const m = text.match(/Plan ID:\s*([A-Za-z0-9._-]+)/);
    return m ? m[1] : "";
}

function updateCenterRefreshTitle() {
    document.title = updateCenterT(
        "page.title",
        null,
        "DNA-Nexus NAS • Admin • Update Center"
    );
}

updateCenterReady(updateCenterRefreshTitle);
window.addEventListener("pqnas-language-changed", updateCenterRefreshTitle);

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
    const githubProgressWrap = document.getElementById("githubProgressWrap");
    const githubProgressText = document.getElementById("githubProgressText");
    const githubProgressPct = document.getElementById("githubProgressPct");
    const githubProgressBar = document.getElementById("githubProgressBar");
    const githubProgressFill = document.getElementById("githubProgressFill");

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
            .trim() || updateCenterT("status.no_release_notes", null, "No release notes provided.");
    }

    function fmtDate(s) {
        const d = new Date(s || "");
        return Number.isFinite(d.getTime()) ? d.toLocaleString() : updateCenterT("value.unknown_date", null, "unknown date");
    }

    function githubFmtBytes(n) {
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

    function githubFmtSpeed(bytesPerSec) {
        const v = Number(bytesPerSec || 0);
        if (!Number.isFinite(v) || v <= 0) return "—";
        return `${githubFmtBytes(v)}/s`;
    }

    let githubDownloadPollTimer = null;
    let githubDownloadStartedAt = 0;
    let githubDownloadTotalHint = 0;

    function stopGithubDownloadPoll() {
        if (githubDownloadPollTimer) {
            clearTimeout(githubDownloadPollTimer);
            githubDownloadPollTimer = null;
        }
    }

    function showGithubProgress(show) {
        if (!githubProgressWrap) return;
        githubProgressWrap.hidden = !show;
        if (!show) {
            setGithubProgress(0, "", "");
        }
    }

    function setGithubProgress(pct, text, kind) {
        const p = Math.max(0, Math.min(100, Number(pct || 0)));

        if (githubProgressFill) {
            githubProgressFill.style.width = `${p.toFixed(1)}%`;
        }

        if (githubProgressPct) {
            githubProgressPct.textContent = `${Math.round(p)}%`;
        }

        if (githubProgressText) {
            githubProgressText.textContent = String(text || "");
            githubProgressText.classList.toggle("ok", kind === "ok");
            githubProgressText.classList.toggle("err", kind === "err");
        }

        if (githubProgressBar) {
            githubProgressBar.setAttribute("aria-valuenow", String(Math.round(p)));
        }
    }

    async function pollGithubServerDownload(jobId) {
        try {
            const qs = new URLSearchParams();
            qs.set("job_id", jobId);

            const r = await fetch(`/api/v4/admin/updates/github-download/status?${qs.toString()}`, {
                credentials: "include",
                cache: "no-store",
                headers: {
                    "Accept": "application/json",
                },
            });

            const j = await r.json().catch(() => null);
            if (!r.ok || !j || !j.ok || !j.job) {
                throw new Error(j && (j.message || j.error) ? `${j.error || ""} ${j.message || ""}`.trim() : `HTTP ${r.status}`);
            }

            const job = j.job;
            const downloaded = Number(job.downloaded_bytes || 0);
            const total = Number(job.total_bytes || githubDownloadTotalHint || 0);
            const pct = total > 0 ? (downloaded / total) * 100 : 0;
            const elapsedSec = Math.max(0.001, (performance.now() - githubDownloadStartedAt) / 1000);
            const speedBps = downloaded / elapsedSec;
            const detail = total > 0
                ? `${githubFmtBytes(downloaded)} / ${githubFmtBytes(total)} • ${githubFmtSpeed(speedBps)}`
                : `${githubFmtBytes(downloaded)} downloaded • ${githubFmtSpeed(speedBps)}`;

            if (job.status === "done") {
                stopGithubDownloadPoll();
                setGithubProgress(
                    100,
                    updateCenterT("download.github.done", { name: job.stored_name || "" }, `Downloaded to server: ${job.stored_name || ""}`),
                    "ok"
                );
                statusLine.textContent = updateCenterT("download.github.done_detail", { detail }, `GitHub download complete. ${detail}`);
                downloadBtn.disabled = !preferredAsset || !preferredAsset.browser_download_url;
                window.dispatchEvent(new CustomEvent("pqnas-update-packages-changed", {
                    detail: { storedName: job.stored_name || "" }
                }));
                return;
            }

            if (job.status === "failed") {
                stopGithubDownloadPoll();
                const err = job.message || job.error || "GitHub download failed.";
                setGithubProgress(100, err, "err");
                statusLine.textContent = err;
                downloadBtn.disabled = !preferredAsset || !preferredAsset.browser_download_url;
                return;
            }

            setGithubProgress(
                pct,
                updateCenterT("download.github.progress", { detail }, detail),
                ""
            );
            statusLine.textContent = updateCenterT("download.github.downloading", { detail }, `Downloading GitHub package to server… ${detail}`);

            githubDownloadPollTimer = setTimeout(() => pollGithubServerDownload(jobId), 700);
        } catch (e) {
            stopGithubDownloadPoll();
            const err = String(e && e.message ? e.message : e);
            setGithubProgress(100, err, "err");
            statusLine.textContent = err;
            downloadBtn.disabled = !preferredAsset || !preferredAsset.browser_download_url;
        }
    }

    async function startGithubServerDownload() {
        if (!preferredAsset || !preferredAsset.browser_download_url) {
            statusLine.textContent = updateCenterT("status.no_core_found", null, "Release loaded, but no core/server update package asset was found.");
            return;
        }

        try {
            stopGithubDownloadPoll();

            downloadBtn.disabled = true;
            githubDownloadStartedAt = performance.now();
            githubDownloadTotalHint = Number(preferredAsset.size || 0);

            showGithubProgress(true);
            setGithubProgress(
                0,
                updateCenterT("download.github.starting", { name: preferredAsset.name || "" }, `Starting server download: ${preferredAsset.name || ""}`),
                ""
            );
            statusLine.textContent = updateCenterT("download.github.starting_status", null, "Starting GitHub download on server…");

            const r = await fetch("/api/v4/admin/updates/github-download", {
                method: "POST",
                credentials: "include",
                headers: {
                    "Content-Type": "application/json",
                    "Accept": "application/json",
                },
                body: JSON.stringify({
                    url: preferredAsset.browser_download_url,
                    name: preferredAsset.name || "",
                    size: preferredAsset.size || 0,
                }),
            });

            const j = await r.json().catch(() => null);
            if (!r.ok || !j || !j.ok || !j.job || !j.job.id) {
                throw new Error(j && (j.message || j.error) ? `${j.error || ""} ${j.message || ""}`.trim() : `HTTP ${r.status}`);
            }

            await pollGithubServerDownload(j.job.id);
        } catch (e) {
            stopGithubDownloadPoll();
            const err = String(e && e.message ? e.message : e);
            setGithubProgress(100, err, "err");
            statusLine.textContent = err;
            downloadBtn.disabled = !preferredAsset || !preferredAsset.browser_download_url;
        }
    }

    async function checkRelease() {
        try {
            setBadge(stateBadge, "warn", updateCenterT("status.checking", null, "checking…"));
            statusLine.textContent = updateCenterT("status.checking_releases", null, "Checking GitHub releases…");
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

            setBadge(stateBadge, "ok", updateCenterT("status.ready", null, "ready"));
            setBadge(
                releaseBadge,
                preferredAsset ? "ok" : "info",
                preferredAsset
                    ? updateCenterT("status.core_found", null, "core package found")
                    : updateCenterT("status.no_core_asset", null, "no core package asset")
            );

            const releaseName = j.tag_name || j.name || "release";
            const releaseDate = fmtDate(j.published_at || j.created_at);
            releaseLine.textContent = updateCenterT(
                "status.release_line",
                { release: releaseName, date: releaseDate },
                `${releaseName} • published ${releaseDate}`
            );
            releaseBody.textContent = cleanReleaseBody(j.body);

            downloadBtn.disabled = !preferredAsset || !preferredAsset.browser_download_url;
            openReleaseBtn.disabled = !j.html_url;

            statusLine.textContent = preferredAsset
                ? updateCenterT("status.preferred_package", { name: preferredAsset.name }, `Preferred package: ${preferredAsset.name}`)
                : updateCenterT("status.no_core_found", null, "Release loaded, but no core/server update package asset was found.");
        } catch (e) {
            setBadge(stateBadge, "err", updateCenterT("status.error", null, "error"));
            setBadge(releaseBadge, "err", updateCenterT("status.check_failed", null, "check failed"));
            statusLine.textContent = String(e && e.message ? e.message : e);
            releaseLine.textContent = updateCenterT("status.release_load_failed", null, "Could not load GitHub release data.");
            releaseBody.textContent = updateCenterT("status.network_hint", null, "Check network access from this browser/server environment.");
        } finally {
            checkBtn.disabled = false;
        }
    }

    checkBtn?.addEventListener("click", checkRelease);

    downloadBtn?.addEventListener("click", startGithubServerDownload);

    openReleaseBtn?.addEventListener("click", () => {
        if (latestRelease && latestRelease.html_url) {
            window.open(latestRelease.html_url, "_blank", "noopener");
        }
    });

    updateCenterReady(() => {
        setBadge(stateBadge, "warn", updateCenterT("common.loading", null, "loading…"));
        statusLine.textContent = updateCenterT("status.auto_checking", null, "Auto-checking latest release…");
        checkRelease();
    });
})();


// DNA-Nexus Update Center manual upload v1
(() => {
    const fileInput = document.getElementById("manualPackageFile");
    const chooseFileBtn = document.getElementById("manualChooseFileBtn");
    const chosenFileName = document.getElementById("manualChosenFileName");
    const uploadBtn = document.getElementById("manualUploadBtn");
    const refreshBtn = document.getElementById("refreshUploadsBtn");
    const deleteBtn = document.getElementById("deletePackageBtn");
    const statusEl = document.getElementById("manualUploadStatus");
    const packageListEl = document.getElementById("uploadedPackagesList");
    const progressWrap = document.getElementById("manualProgressWrap");
    const progressText = document.getElementById("manualProgressText");
    const progressPct = document.getElementById("manualProgressPct");
    const progressBar = document.getElementById("manualProgressBar");
    const progressFill = document.getElementById("manualProgressFill");

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

    function updateChosenFileName() {
        if (!chosenFileName || !fileInput) return;

        const f = fileInput.files && fileInput.files[0] ? fileInput.files[0] : null;
        chosenFileName.textContent = f
            ? `${f.name} — ${fmtBytes(f.size)}`
            : updateCenterT("manual.no_file_chosen", null, "No file selected");
    }

    function fmtSpeed(bytesPerSec) {
        const v = Number(bytesPerSec || 0);
        if (!Number.isFinite(v) || v <= 0) return "—";
        return `${fmtBytes(v)}/s`;
    }

    function showUploadProgress(show) {
        if (!progressWrap) return;
        progressWrap.hidden = !show;

        if (!show) {
            setUploadProgress(0, "", "");
        }
    }

    function setUploadProgress(pct, text, kind) {
        const p = Math.max(0, Math.min(100, Number(pct || 0)));

        if (progressFill) {
            progressFill.style.width = `${p.toFixed(1)}%`;
        }

        if (progressPct) {
            progressPct.textContent = `${Math.round(p)}%`;
        }

        if (progressText) {
            progressText.textContent = String(text || "");
            progressText.classList.toggle("ok", kind === "ok");
            progressText.classList.toggle("err", kind === "err");
        }

        if (progressBar) {
            progressBar.setAttribute("aria-valuenow", String(Math.round(p)));
        }
    }

    function xhrUploadPackage(file, onProgress) {
        return new Promise((resolve, reject) => {
            const xhr = new XMLHttpRequest();

            xhr.open("POST", "/api/v4/admin/updates/upload", true);
            xhr.withCredentials = true;
            xhr.timeout = 60 * 60 * 1000;
            xhr.setRequestHeader("Content-Type", "application/octet-stream");
            xhr.setRequestHeader("X-PQNAS-Filename", file.name);

            let lastProgressTs = 0;

            xhr.upload.onprogress = (ev) => {
                if (!onProgress) return;

                const now = performance.now();
                const total = ev.lengthComputable ? ev.total : (file.size || 0);

                if (now - lastProgressTs < 80 && ev.loaded < total) {
                    return;
                }

                lastProgressTs = now;
                onProgress(ev.loaded || 0, total || 0);
            };

            xhr.onerror = () => reject(new Error("upload failed (network)"));
            xhr.ontimeout = () => reject(new Error("upload failed (timeout)"));
            xhr.onabort = () => reject(new Error("upload aborted"));

            xhr.onload = () => {
                const status = xhr.status || 0;
                const raw = String(xhr.responseText || "").trim();
                let j = null;

                if (raw && (raw.startsWith("{") || raw.startsWith("["))) {
                    try { j = JSON.parse(raw); } catch (_) {}
                }

                if (status >= 200 && status < 300 && j && j.ok) {
                    resolve(j);
                    return;
                }

                if (j && (j.message || j.error)) {
                    reject(new Error(`${j.error || ""} ${j.message || ""}`.trim() || `HTTP ${status}`));
                    return;
                }

                reject(new Error(raw || `HTTP ${status}`));
            };

            xhr.send(file);
        });
    }

    function renderJson(prefix, obj) {
        statusEl.textContent = `${prefix}\n` + JSON.stringify(obj, null, 2);
    }

    window.PQNAS_UPDATE_SELECTED_PACKAGE = window.PQNAS_UPDATE_SELECTED_PACKAGE || "";

    function escHtmlLocal(s) {
        return String(s ?? "").replace(/[&<>"']/g, c => ({
            "&": "&amp;",
            "<": "&lt;",
            ">": "&gt;",
            "\"": "&quot;",
            "'": "&#39;",
        }[c]));
    }

    function displayNameFromStoredName(name) {
        return String(name || "").replace(/^[0-9a-f]{12}_/i, "");
    }

    function versionPartsFromPackageName(name) {
        const n = displayNameFromStoredName(name).toLowerCase();
        const m = n.match(/^pqnas-([0-9][a-z0-9._-]*)-linux-/);
        if (!m) return [];
        return String(m[1]).split(/[^0-9]+/).filter(Boolean).map(x => {
            const v = Number(x);
            return Number.isFinite(v) ? v : 0;
        });
    }

    function comparePackageItems(a, b) {
        const av = versionPartsFromPackageName(a && a.name);
        const bv = versionPartsFromPackageName(b && b.name);
        const n = Math.max(av.length, bv.length);

        for (let i = 0; i < n; i++) {
            const ai = i < av.length ? av[i] : 0;
            const bi = i < bv.length ? bv[i] : 0;
            if (ai !== bi) return bi - ai;
        }

        return String(b && b.name || "").localeCompare(String(a && a.name || ""));
    }

    function getSelectedPackage() {
        return String(window.PQNAS_UPDATE_SELECTED_PACKAGE || "").trim();
    }

    function updatePackageSelectionUi() {
        if (!packageListEl) return;

        const selected = getSelectedPackage();
        packageListEl.querySelectorAll(".updatePackageChoice").forEach(row => {
            const input = row.querySelector("input[type='radio']");
            const isSelected = !!input && input.value === selected;
            row.classList.toggle("selected", isSelected);
            if (input) input.checked = isSelected;
        });
    }

    function setSelectedPackage(name) {
        window.PQNAS_UPDATE_SELECTED_PACKAGE = String(name || "").trim();
        updatePackageSelectionUi();
    }

    function renderUploadedPackages(items) {
        if (!packageListEl) return;

        const arr = Array.isArray(items) ? items.slice() : [];
        arr.sort(comparePackageItems);

        if (!arr.length) {
            packageListEl.className = "updatePackageList empty";
            packageListEl.textContent = updateCenterT(
                "upload.none_staged",
                null,
                "No uploaded update packages staged on this server."
            );
            setSelectedPackage("");
            return;
        }

        const existingNames = new Set(arr.map(it => String((it && it.name) || "")));
        let selected = getSelectedPackage();

        if (!selected || !existingNames.has(selected)) {
            selected = String((arr[0] && arr[0].name) || "");
            window.PQNAS_UPDATE_SELECTED_PACKAGE = selected;
        }

        packageListEl.className = "updatePackageList";
        packageListEl.innerHTML = arr.map((it) => {
            const storedName = String((it && it.name) || "");
            const displayName = displayNameFromStoredName(storedName);
            const checked = storedName === selected ? "checked" : "";
            const selectedClass = storedName === selected ? " selected" : "";

            return `
                <label class="updatePackageChoice${selectedClass}">
                    <input type="radio"
                           name="updatePackageChoice"
                           value="${escHtmlLocal(storedName)}"
                           ${checked}>
                    <span class="updatePackageText">
                        <span class="updatePackageName">${escHtmlLocal(displayName || storedName)}</span>
                        <span class="updatePackageMeta">${escHtmlLocal(storedName)} — ${escHtmlLocal(fmtBytes(it && it.size))}</span>
                    </span>
                </label>
            `;
        }).join("");

        updatePackageSelectionUi();
    }

    async function refreshUploadedPackages() {
        try {
            statusEl.textContent = updateCenterT("upload.loading", null, "Loading uploaded packages…");

            const r = await fetch("/api/v4/admin/updates/status", {
                credentials: "include",
                cache: "no-store",
            });

            const j = await r.json().catch(() => null);
            if (!r.ok || !j || !j.ok) {
                throw new Error(j && (j.message || j.error) ? `${j.error || ""} ${j.message || ""}`.trim() : `HTTP ${r.status}`);
            }

            const items = Array.isArray(j.incoming) ? j.incoming : [];
            renderUploadedPackages(items);

            if (!items.length) {
                statusEl.textContent = updateCenterT("upload.none_staged", null, "No uploaded update packages staged on this server.");
                return;
            }

            const selected = getSelectedPackage();
            statusEl.textContent = updateCenterT(
                "upload.selected_package",
                { name: selected },
                `Selected package: ${selected}`
            );
        } catch (e) {
            if (packageListEl) {
                packageListEl.className = "updatePackageList empty";
                packageListEl.textContent = updateCenterT(
                    "upload.load_failed",
                    { error: String(e && e.message ? e.message : e) },
                    "Failed to load uploaded packages: {error}"
                );
            }
            statusEl.textContent = updateCenterT("upload.load_failed", { error: String(e && e.message ? e.message : e) }, "Failed to load uploaded packages: {error}");
        }
    }



    function showDeletePackageConfirmModal(storedName) {
        return new Promise((resolve) => {
            const existing = document.querySelector(".updateDeleteModalOverlay");
            if (existing) existing.remove();

            const esc = (s) => String(s ?? "").replace(/[&<>"]/g, c => ({
                "&": "&amp;",
                "<": "&lt;",
                ">": "&gt;",
                "\"": "&quot;",
            }[c]));

            const overlay = document.createElement("div");
            overlay.className = "updateDeleteModalOverlay";
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
            modal.className = "updateDeleteModal";
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
                        background: rgba(255, 90, 90, 0.16);
                        border: 1px solid rgba(255, 90, 90, 0.48);
                        font-size: 22px;
                    ">🗑️</div>
                    <div>
                        <div style="font-size: 19px; font-weight: 800;">
                            ${esc(updateCenterT("delete.modal.title", null, "Delete staged package?"))}
                        </div>
                        <div style="font-size: 13px; opacity: 0.72; margin-top: 2px;">
                            ${esc(updateCenterT("delete.modal.subtitle", null, "This removes the package from the server staging area."))}
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
                    ">${esc(updateCenterT("delete.modal.package", null, "Package"))}</div>

                    <div style="
                        padding: 12px 14px;
                        border-radius: 12px;
                        background: rgba(255,255,255,0.07);
                        border: 1px solid rgba(255,255,255,0.12);
                        font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace;
                        font-size: 13px;
                        line-height: 1.45;
                        word-break: break-all;
                    ">${esc(storedName)}</div>

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
                        ${esc(updateCenterT("delete.modal.warning", null, "This does not uninstall anything, but the staged package file and its metadata will be deleted."))}
                    </div>
                </div>

                <div style="
                    padding: 18px 22px 22px 22px;
                    display: flex;
                    justify-content: flex-end;
                    gap: 12px;
                    flex-wrap: wrap;
                ">
                    <button type="button" class="updateDeleteCancel" style="
                        border: 1px solid rgba(255,255,255,0.22);
                        background: rgba(255,255,255,0.08);
                        color: #f5f7fb;
                        border-radius: 999px;
                        padding: 10px 18px;
                        cursor: pointer;
                        font-weight: 700;
                    ">${esc(updateCenterT("delete.modal.cancel", null, "Cancel"))}</button>

                    <button type="button" class="updateDeleteConfirm" style="
                        border: 1px solid rgba(255, 80, 80, 0.7);
                        background: linear-gradient(180deg, #ff5d5d, #d82929);
                        color: white;
                        border-radius: 999px;
                        padding: 10px 18px;
                        cursor: pointer;
                        font-weight: 800;
                        box-shadow: 0 10px 24px rgba(216, 41, 41, 0.28);
                    ">${esc(updateCenterT("delete.modal.confirm", null, "Delete package"))}</button>
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

            modal.querySelector(".updateDeleteCancel")?.addEventListener("click", () => cleanup(false));
            modal.querySelector(".updateDeleteConfirm")?.addEventListener("click", () => cleanup(true));

            document.addEventListener("keydown", onKey, true);
            setTimeout(() => modal.querySelector(".updateDeleteCancel")?.focus(), 0);
        });
    }

    async function deleteSelectedPackage() {
        const storedName = getSelectedPackage();

        if (!storedName) {
            statusEl.textContent = updateCenterT(
                "delete.no_package",
                null,
                "No staged package selected."
            );
            return;
        }

        const ok = await showDeletePackageConfirmModal(storedName);

        if (!ok) return;

        try {
            if (deleteBtn) deleteBtn.disabled = true;
            if (refreshBtn) refreshBtn.disabled = true;

            statusEl.textContent = updateCenterT(
                "delete.deleting",
                { name: storedName },
                `Deleting ${storedName}…`
            );

            const r = await fetch("/api/v4/admin/updates/delete", {
                method: "POST",
                credentials: "include",
                headers: {
                    "Content-Type": "application/json",
                    "Accept": "application/json",
                },
                body: JSON.stringify({ stored_name: storedName }),
            });

            const j = await r.json().catch(() => null);
            if (!r.ok || !j || !j.ok) {
                throw new Error(j && (j.message || j.error) ? `${j.error || ""} ${j.message || ""}`.trim() : `HTTP ${r.status}`);
            }

            setSelectedPackage("");
            statusEl.textContent = updateCenterT(
                "delete.deleted",
                { name: storedName },
                `Deleted staged package: ${storedName}`
            );

            await refreshUploadedPackages();
        } catch (e) {
            statusEl.textContent = updateCenterT(
                "delete.failed",
                { error: String(e && e.message ? e.message : e) },
                "Delete failed: {error}"
            );
        } finally {
            if (deleteBtn) deleteBtn.disabled = false;
            if (refreshBtn) refreshBtn.disabled = false;
        }
    }

    async function uploadPackage() {
        const f = fileInput.files && fileInput.files[0] ? fileInput.files[0] : null;
        if (!f) {
            statusEl.textContent = updateCenterT("upload.choose_first", null, "Choose a pqnas-*.tar.gz / .tgz / .zip / .dnxupd package first.");
            return;
        }

        try {
            uploadBtn.disabled = true;
            refreshBtn.disabled = true;

            const totalBytes = Number(f.size || 0);
            const startedAt = performance.now();

            showUploadProgress(true);
            setUploadProgress(
                0,
                updateCenterT("upload.progress_start", { name: f.name, size: fmtBytes(totalBytes) }, `Starting upload: ${f.name} (${fmtBytes(totalBytes)})…`),
                ""
            );

            statusEl.textContent = updateCenterT("upload.uploading", { name: f.name, size: fmtBytes(f.size) }, `Uploading ${f.name} (${fmtBytes(f.size)})…`);

            const j = await xhrUploadPackage(f, (loaded, total) => {
                const safeTotal = Number(total || totalBytes || 0);
                const safeLoaded = Math.max(0, Number(loaded || 0));
                const pct = safeTotal > 0 ? (safeLoaded / safeTotal) * 100 : 0;
                const elapsedSec = Math.max(0.001, (performance.now() - startedAt) / 1000);
                const speedBps = safeLoaded / elapsedSec;
                const detail = `${fmtBytes(safeLoaded)} / ${fmtBytes(safeTotal)} • ${fmtSpeed(speedBps)}`;

                setUploadProgress(
                    pct,
                    updateCenterT("upload.progress_detail", { detail }, detail),
                    ""
                );

                statusEl.textContent =
                    updateCenterT("upload.uploading", { name: f.name, size: fmtBytes(f.size) }, `Uploading ${f.name} (${fmtBytes(f.size)})…`) +
                    "\n" +
                    detail;
            });

            setUploadProgress(
                100,
                updateCenterT("upload.progress_done", null, "Upload complete."),
                "ok"
            );

            setSelectedPackage(j.stored_name || "");
            renderJson(updateCenterT("upload.staged_ok", null, "Upload staged successfully. Nothing has been installed yet."), j);
            await refreshUploadedPackages();
        } catch (e) {
            const err = String(e && e.message ? e.message : e);
            setUploadProgress(
                100,
                updateCenterT("upload.failed", { error: err }, "Upload failed: {error}"),
                "err"
            );
            statusEl.textContent = updateCenterT("upload.failed", { error: err }, "Upload failed: {error}");
        } finally {
            uploadBtn.disabled = false;
            refreshBtn.disabled = false;
        }
    }

    chooseFileBtn?.addEventListener("click", () => fileInput.click());
    fileInput?.addEventListener("change", updateChosenFileName);
    updateChosenFileName();

    uploadBtn.addEventListener("click", uploadPackage);
    refreshBtn.addEventListener("click", refreshUploadedPackages);
    deleteBtn?.addEventListener("click", deleteSelectedPackage);


    if (packageListEl) {
        packageListEl.addEventListener("change", (ev) => {
            const input = ev.target;
            if (!input || input.name !== "updatePackageChoice") return;
            setSelectedPackage(input.value || "");
            statusEl.textContent = updateCenterT(
                "upload.selected_package",
                { name: getSelectedPackage() },
                `Selected package: ${getSelectedPackage()}`
            );
        });
    }

    window.addEventListener("pqnas-update-packages-changed", (ev) => {
        const storedName = ev && ev.detail && ev.detail.storedName ? String(ev.detail.storedName) : "";
        if (storedName) setSelectedPackage(storedName);
        refreshUploadedPackages();
    });

    updateCenterReady(refreshUploadedPackages);
})();


// DNA-Nexus Update Center verify package v1
(() => {
    const verifyBtn = document.getElementById("verifyPackageBtn");
    const statusEl = document.getElementById("manualUploadStatus");

    if (!verifyBtn || !statusEl) {
        return;
    }

    function pickStoredNameFromStatus() {
        const selected = String(window.PQNAS_UPDATE_SELECTED_PACKAGE || "").trim();
        if (selected) return selected;

        const text = String(statusEl.textContent || "");
        const m = text.match(/([0-9a-f]{12}_[A-Za-z0-9._-]+\.(?:tar\.gz|tgz|zip|dnxupd))/);
        return m ? m[1] : "";
    }

    async function verifyPackage() {
        const storedName = pickStoredNameFromStatus();
        if (!storedName) {
            statusEl.textContent = updateCenterT("verify.no_staged", null, "No staged package selected. Upload or refresh packages first.");
            return;
        }

        try {
            verifyBtn.disabled = true;
            statusEl.textContent = updateCenterT("verify.verifying", { name: storedName }, `Verifying ${storedName}…`);

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
                (j.ok
                    ? updateCenterT("verify.ok", null, "Verification OK. Nothing has been installed yet.") + "\n"
                    : updateCenterT("verify.failed", null, "Verification failed.") + "\n") +
                JSON.stringify(j, null, 2);
        } catch (e) {
            statusEl.textContent = updateCenterT("verify.failed_with_error", { error: String(e && e.message ? e.message : e) }, "Verification failed: {error}");
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
        const selected = String(window.PQNAS_UPDATE_SELECTED_PACKAGE || "").trim();
        if (selected) return selected;

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
            updateCenterT("plan.built", null, "Install plan built. Nothing has been installed yet."),
            "",
            updateCenterLabel("package", j.stored_name || "", "Package"),
            updateCenterLabel("plan_id", j.plan_id || "", "Plan ID"),
            updateCenterLabel("plan_hash", j.plan_hash || "", "Plan hash"),
            updateCenterLabel("package_sha256", j.package_sha256 || "", "Package SHA256"),
            updateCenterLabel("package_version", j.package_server_version || "", "Package version"),
            updateCenterLabel("current_server_version", j.current_server_version || "", "Current server version"),
            updateCenterLabel("entries", j.entry_count || 0, "Entries"),
            updateCenterLabel("planned_updates", j.planned_updates || 0, "Planned updates"),
            updateCenterLabel("skipped", j.skipped || 0, "Skipped"),
            updateCenterLabel("core_binary_action", updateCenterYesNo(j.has_core_binary_action), "Core binary action"),
        ].filter(x => x !== null && x !== undefined).join("\n");

        const lines = first.map(a => {
            const app = a.app_id ? ` app=${a.app_id}` : "";
            const target = a.target ? ` -> ${a.target}` : "";
            const reason = a.reason ? ` (${a.reason})` : "";
            const text = `- [${a.action}] ${a.type}${app}: ${a.source}${target}${reason}`;
            return `<div class="planLine ${planLineClass(a.action)}">${escHtml(text)}</div>`;
        });

        if (actions.length > first.length) {
            lines.push(`<div class="planLine other">${escHtml(updateCenterT("plan.more_actions", { count: actions.length - first.length }, "... {count} more actions not shown in UI preview"))}</div>`);
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
            statusEl.textContent = updateCenterT("verify.no_staged", null, "No staged package selected. Upload or refresh packages first.");
            return;
        }

        try {
            planBtn.disabled = true;
            statusEl.textContent = updateCenterT("plan.building", { name: storedName }, `Building install plan for ${storedName}…`);

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
                statusEl.textContent = updateCenterT("plan.failed", null, "Plan failed.") + "\n" + JSON.stringify(j, null, 2);
                return;
            }

            statusEl.dataset.planId = j.plan_id || "";
            statusEl.innerHTML = summarizePlan(j);
        } catch (e) {
            statusEl.textContent = updateCenterT("plan.failed_with_error", { error: String(e && e.message ? e.message : e) }, "Plan failed: {error}");
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
        return updateCenterPlanIdFromStatus(statusEl);
    }

    function renderInstallValidation(j) {
        const ok = !!j.ok;
        const errors = Array.isArray(j.validation_errors) ? j.validation_errors : [];
        const actions = Array.isArray(j.applicable_actions) ? j.applicable_actions : [];

        const head = [
            ok
                ? updateCenterT("install.ok", null, "Install validation OK. Nothing has been installed yet.")
                : updateCenterT("install.failed", null, "Install validation failed. Nothing has been installed."),
            "",
            updateCenterLabel("plan_id", j.plan_id || "", "Plan ID"),
            updateCenterLabel("plan_hash", j.plan_hash || "", "Plan hash"),
            updateCenterLabel("package_sha256", j.package_sha256 || "", "Package SHA256"),
            updateCenterLabel("package_version", j.package_server_version || "", "Package version"),
            updateCenterLabel("current_server_version", j.current_server_version || "", "Current server version"),
            updateCenterLabel("applicable_actions", j.applicable_action_count || 0, "Applicable actions"),
            updateCenterLabel("install_helper_enabled", updateCenterYesNo(j.helper_enabled), "Install helper enabled"),
            j.helper_exit_code === undefined ? null : updateCenterLabel("helper_exit_code", j.helper_exit_code, "Helper exit code"),
        ].filter(x => x !== null && x !== undefined).join("\n");

        const errorHtml = errors.length
            ? `<div class="planActions">${errors.map(e => {
                const msg = `[${e.code || "error"}] ${updateCenterValidationErrorMessage(e)}`;
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
            statusEl.textContent = updateCenterT("install.no_plan", null, "No saved install plan selected. Build install plan first.");
            return;
        }

        try {
            installBtn.disabled = true;
            statusEl.textContent = updateCenterT("install.validating", { planId }, `Validating install plan ${planId}…`);

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

            statusEl.dataset.planId = j.plan_id || planId;
            statusEl.innerHTML = renderInstallValidation(j);
        } catch (e) {
            statusEl.textContent = updateCenterT("install.failed_with_error", { error: String(e && e.message ? e.message : e) }, "Install validation failed: {error}");
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
        return updateCenterPlanIdFromStatus(statusEl);
    }

    function renderDryRun(j) {
        const planned = Array.isArray(j.planned_actions) ? j.planned_actions : [];
        const errors = Array.isArray(j.validation_errors) ? j.validation_errors : [];
        const ok = !!j.ok;

        const head = [
            ok
                ? updateCenterT("dry.ok", null, "Dry-run OK. No files were modified.")
                : updateCenterT("dry.failed", null, "Dry-run failed. No files were modified."),
            "",
            updateCenterLabel("plan_id", j.plan_id || "", "Plan ID"),
            updateCenterLabel("plan_hash", j.plan_hash || "", "Plan hash"),
            updateCenterLabel("package_sha256", j.package_sha256 || "", "Package SHA256"),
            updateCenterLabel("package_version", j.package_server_version || "", "Package version"),
            updateCenterLabel("current_server_version", j.current_server_version || "", "Current server version"),
            updateCenterLabel("applicable_actions", j.applicable_action_count || 0, "Applicable actions"),
            updateCenterLabel("planned_actions", j.planned_action_count || planned.length || 0, "Planned actions"),
            updateCenterLabel("install_helper_enabled", updateCenterYesNo(j.helper_enabled), "Install helper enabled"),
            j.helper_exit_code === undefined ? null : updateCenterLabel("helper_exit_code", j.helper_exit_code, "Helper exit code"),
            updateCenterLabel("install_performed", updateCenterYesNo(j.install_performed), "Install performed"),
            j.error ? updateCenterLabel("error", updateCenterBackendError(j.error), "Error") : null,
            j.message ? updateCenterLabel("message", updateCenterBackendMessage(j.message), "Message") : null,
        ].filter(x => x !== null && x !== undefined).join("\n");

        const errorHtml = errors.length
            ? `<div class="planActions">${errors.map(e => {
                const msg = `[${e.code || "error"}] ${updateCenterValidationErrorMessage(e)}`;
                return `<div class="planLine skip">${escHtml(msg)}</div>`;
            }).join("")}</div>`
            : "";

        const plannedHtml = planned.length
            ? `<div class="planActions">${planned.slice(0, 200).map(a => {
                const replace = a.would_replace === false
                    ? updateCenterT("value.same", null, "same")
                    : updateCenterT("value.replace", null, "replace");
                const msg = `- [${a.type || ""}] ${a.source || ""} -> ${a.target || ""} (${replace})`;
                return `<div class="planLine update">${escHtml(msg)}</div>`;
            }).join("")}</div>`
            : "";

        return `
            <div class="planPreview">
                <div class="planSummary">${escHtml(head)}</div>
                ${errorHtml}
                ${plannedHtml}
            </div>
        `;
    }

    async function dryRunInstallPlan() {
        const planId = pickPlanIdFromStatus();
        if (!planId) {
            statusEl.textContent = updateCenterT("install.no_plan", null, "No saved install plan selected. Build install plan first.");
            return;
        }

        try {
            dryRunBtn.disabled = true;
            statusEl.textContent = updateCenterT("dry.running", { planId }, `Running update dry-run for ${planId}…`);

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

            statusEl.dataset.planId = j.plan_id || planId;
            statusEl.innerHTML = renderDryRun(j);
        } catch (e) {
            statusEl.textContent = updateCenterT("dry.failed_with_error", { error: String(e && e.message ? e.message : e) }, "Dry-run failed: {error}");
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
        return updateCenterPlanIdFromStatus(statusEl);
    }

    function renderApply(j) {
        const ok = !!j.ok;
        const errors = Array.isArray(j.validation_errors) ? j.validation_errors : [];

        const head = [
            ok
                ? updateCenterT("apply.ok", null, "Apply OK.")
                : updateCenterT("apply.failed", null, "Apply failed."),
            "",
            updateCenterLabel("plan_id", j.plan_id || "", "Plan ID"),
            updateCenterLabel("plan_hash", j.plan_hash || "", "Plan hash"),
            updateCenterLabel("package_sha256", j.package_sha256 || "", "Package SHA256"),
            updateCenterLabel("package_version", j.package_server_version || "", "Package version"),
            updateCenterLabel("current_server_version", j.current_server_version || "", "Current server version"),
            updateCenterLabel("applicable_actions", j.applicable_action_count || 0, "Applicable actions"),
            j.applied_action_count === undefined ? null : updateCenterLabel("applied_actions", j.applied_action_count, "Applied actions"),
            updateCenterLabel("install_helper_enabled", updateCenterYesNo(j.helper_enabled), "Install helper enabled"),
            updateCenterLabel("apply_allowed", updateCenterYesNo(j.apply_allowed), "Apply allowed"),
            j.helper_exit_code === undefined ? null : updateCenterLabel("helper_exit_code", j.helper_exit_code, "Helper exit code"),
            updateCenterLabel("install_performed", updateCenterYesNo(j.install_performed), "Install performed"),
            j.restart_required === undefined ? null : updateCenterLabel("restart_required", updateCenterYesNo(j.restart_required), "Restart required"),
            j.backup_root ? updateCenterLabel("backup_root", j.backup_root, "Backup root") : null,
            j.manifest_path ? updateCenterLabel("manifest", j.manifest_path, "Manifest") : null,
            j.error ? updateCenterLabel("error", updateCenterBackendError(j.error), "Error") : null,
            j.message ? updateCenterLabel("message", updateCenterBackendMessage(j.message), "Message") : null,
        ].filter(x => x !== null && x !== undefined).join("\n");

        const fallbackErrors = errors.length
            ? errors
            : ((!ok && (j.error || j.message))
                ? [{ code: j.error || "error", message: updateCenterBackendMessage(j.message || "") }]
                : []);

        const errorHtml = fallbackErrors.length
            ? `<div class="planActions">${fallbackErrors.map(e => {
                const msg = `[${e.code || "error"}] ${updateCenterValidationErrorMessage(e)}`;
                return `<div class="planLine skip">${escHtml(msg)}</div>`;
            }).join("")}</div>`
            : "";

        return `
            <div class="planPreview">
                <div class="planSummary">${escHtml(head)}</div>
                ${errorHtml}
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
                        <div style="font-size: 19px; font-weight: 800;">${escHtml(updateCenterT("modal.title", null, "Apply update?"))}</div>
                        <div style="font-size: 13px; opacity: 0.72; margin-top: 2px;">
                            ${escHtml(updateCenterT("modal.subtitle", null, "This action may replace static files and the server binary."))}
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
                    ">${escHtml(updateCenterT("modal.plan_id", null, "Plan ID"))}</div>
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
                        ${escHtml(updateCenterT("modal.warning", null, "Continue only if dry-run succeeded and the plan looks correct. The update helper will still validate the immutable plan before applying anything."))}
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
                    ">${escHtml(updateCenterT("modal.cancel", null, "Cancel"))}</button>
                    <button type="button" class="updateApplyConfirm" style="
                        border: 1px solid rgba(255, 80, 80, 0.7);
                        background: linear-gradient(180deg, #ff5d5d, #d82929);
                        color: white;
                        border-radius: 999px;
                        padding: 10px 18px;
                        cursor: pointer;
                        font-weight: 800;
                        box-shadow: 0 10px 24px rgba(216, 41, 41, 0.28);
                    ">${escHtml(updateCenterT("modal.confirm", null, "Apply update"))}</button>
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
            statusEl.textContent = updateCenterT("apply.no_plan", null, "No saved install plan selected. Build install plan and dry-run it first.");
            return;
        }

        const ok = await showUpdateApplyConfirmModal(planId);
        if (!ok) return;

        try {
            applyBtn.disabled = true;
            statusEl.textContent = updateCenterT("apply.applying", { planId }, `Applying update for ${planId}…`);

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

            statusEl.dataset.planId = j.plan_id || planId;
            statusEl.innerHTML = renderApply(j);
        } catch (e) {
            statusEl.textContent = updateCenterT("apply.failed_with_error", { error: String(e && e.message ? e.message : e) }, "Apply failed: {error}");
        } finally {
            applyBtn.disabled = false;
        }
    }

    applyBtn.addEventListener("click", applyUpdatePlan);
})();
