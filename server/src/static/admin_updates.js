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
