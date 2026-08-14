(() => {
    "use strict";

    const API_NODES = "/api/v4/admin/cluster/nodes";
    const NODE_REFRESH_INTERVAL_MS = 30 * 1000;
    const RELATIVE_TIME_TICK_MS = 1000;

    const viewButtons = Array.from(
        document.querySelectorAll("[data-director-view-button]")
    );

    const viewPanels = Array.from(
        document.querySelectorAll("[data-director-view-panel]")
    );

    const summaryEl =
        document.getElementById("directorNodesSummary");

    const listEl =
        document.getElementById("directorNodesList");

    const refreshButton =
        document.getElementById("directorNodesRefresh");

    let nodesLoaded = false;
    let nodesLoading = false;
    let currentView = "overview";
    let nodesRefreshTimer = null;
    let relativeTimeTimer = null;

    function text(value, fallback = "—") {
        if (value === null || value === undefined) {
            return fallback;
        }

        const s = String(value).trim();
        return s || fallback;
    }

    function finiteNumber(value) {
        const n = Number(value);
        return Number.isFinite(n) ? n : null;
    }

    function formatBytes(value) {
        let n = finiteNumber(value);

        if (n === null || n < 0) {
            return "—";
        }

        const units = [
            "B",
            "KiB",
            "MiB",
            "GiB",
            "TiB",
            "PiB"
        ];

        let unit = 0;

        while (n >= 1024 && unit < units.length - 1) {
            n /= 1024;
            unit += 1;
        }

        const decimals =
            unit === 0 ? 0 :
            n >= 100 ? 0 :
            n >= 10 ? 1 : 2;

        return `${n.toFixed(decimals)} ${units[unit]}`;
    }

    function storageUsageInfo(storage) {
        const total =
            finiteNumber(storage?.total_bytes);

        const used =
            finiteNumber(storage?.used_bytes);

        if (total === null ||
            used === null ||
            total <= 0 ||
            used < 0) {
            return null;
        }

        // UI safety: clamp the visual percentage even if a malformed
        // or temporarily inconsistent report says used > total.
        const ratio =
            Math.max(
                0,
                Math.min(
                    1,
                    used / total
                )
            );

        const percent =
            ratio * 100;

        let tone =
            "director-storage-usage-ok";

        if (percent >= 90) {
            tone =
                "director-storage-usage-error";
        } else if (percent >= 75) {
            tone =
                "director-storage-usage-warn";
        }

        const percentLabel =
            percent > 0 && percent < 1
                ? percent.toFixed(1)
                : percent.toFixed(0);

        return {
            used,
            total,
            percent,
            percentLabel,
            tone
        };
    }

    function formatUptime(value) {
        const raw = finiteNumber(value);

        if (raw === null || raw < 0) {
            return "—";
        }

        let seconds = Math.floor(raw);

        const days = Math.floor(seconds / 86400);
        seconds %= 86400;

        const hours = Math.floor(seconds / 3600);
        seconds %= 3600;

        const minutes = Math.floor(seconds / 60);

        if (days > 0) {
            return `${days}d ${hours}h`;
        }

        if (hours > 0) {
            return `${hours}h ${minutes}m`;
        }

        if (minutes > 0) {
            return `${minutes}m`;
        }

        return `${seconds}s`;
    }

    function relativeTime(value) {
        const raw = String(value || "");
        const timestamp = Date.parse(raw);

        if (!Number.isFinite(timestamp)) {
            return {
                label: "—",
                title: ""
            };
        }

        const deltaSeconds =
            Math.max(0, Math.floor((Date.now() - timestamp) / 1000));

        let label;

        if (deltaSeconds < 10) {
            label = "just now";
        } else if (deltaSeconds < 60) {
            label = `${deltaSeconds}s ago`;
        } else if (deltaSeconds < 3600) {
            label = `${Math.floor(deltaSeconds / 60)}m ago`;
        } else if (deltaSeconds < 86400) {
            label = `${Math.floor(deltaSeconds / 3600)}h ago`;
        } else {
            label = `${Math.floor(deltaSeconds / 86400)}d ago`;
        }

        return {
            label,
            title: new Date(timestamp).toLocaleString()
        };
    }

    function healthInfo(value) {
        const health =
            String(value || "unknown")
                .trim()
                .toLowerCase();

        if (health === "online") {
            return {
                label: "ONLINE",
                className: "director-health-online"
            };
        }

        if (health === "offline") {
            return {
                label: "OFFLINE",
                className: "director-health-offline"
            };
        }

        if (health === "disabled") {
            return {
                label: "DISABLED",
                className: "director-health-disabled"
            };
        }

        return {
            label: text(health, "UNKNOWN").toUpperCase(),
            className: "director-health-unknown"
        };
    }

    function appendMetric(
        container,
        label,
        value,
        title = ""
    ) {
        const metric =
            document.createElement("div");

        metric.className =
            "director-node-metric";

        const labelEl =
            document.createElement("div");

        labelEl.className =
            "director-node-metric-label";

        labelEl.textContent = label;

        const valueEl =
            document.createElement("div");

        valueEl.className =
            "director-node-metric-value";

        valueEl.textContent =
            text(value);

        if (title) {
            valueEl.title = title;
        }

        metric.append(
            labelEl,
            valueEl
        );

        container.appendChild(metric);
        return valueEl;
    }

    function updateRelativeTimes() {
        const values = document.querySelectorAll(
            "[data-director-last-seen-at]"
        );

        for (const valueEl of values) {
            const lastSeen =
                relativeTime(
                    valueEl.dataset.directorLastSeenAt
                );

            valueEl.textContent =
                lastSeen.label;

            valueEl.title =
                lastSeen.title;
        }
    }

    function stopNodeTimers() {
        if (nodesRefreshTimer !== null) {
            window.clearInterval(
                nodesRefreshTimer
            );

            nodesRefreshTimer = null;
        }

        if (relativeTimeTimer !== null) {
            window.clearInterval(
                relativeTimeTimer
            );

            relativeTimeTimer = null;
        }
    }

    function startNodeTimers() {
        stopNodeTimers();

        // UX: heartbeat data comes from the Director registry.
        // The browser never probes Storage Nodes directly.
        nodesRefreshTimer =
            window.setInterval(
                () => {
                    if (currentView === "nodes") {
                        loadNodes(false);
                    }
                },
                NODE_REFRESH_INTERVAL_MS
            );

        relativeTimeTimer =
            window.setInterval(
                () => {
                    if (currentView === "nodes") {
                        updateRelativeTimes();
                    }
                },
                RELATIVE_TIME_TICK_MS
            );
    }

    function nodeCard(node) {
        const article =
            document.createElement("article");

        article.className =
            "director-node-card";

        const head =
            document.createElement("div");

        head.className =
            "director-node-card-head";

        const identity =
            document.createElement("div");

        const title =
            document.createElement("h3");

        title.className =
            "director-node-title";

        title.textContent =
            text(node?.node_id, "Unnamed Storage Node");

        const hostname =
            document.createElement("div");

        hostname.className =
            "director-node-meta";

        hostname.textContent =
            `Hostname: ${text(node?.hostname)}`;

        const endpoint =
            document.createElement("div");

        endpoint.className =
            "director-node-meta director-node-endpoint";

        endpoint.textContent =
            text(node?.api_base_url);

        identity.append(
            title,
            hostname,
            endpoint
        );

        const health =
            healthInfo(node?.health);

        const badge =
            document.createElement("div");

        badge.className =
            `director-health ${health.className}`;

        badge.textContent =
            health.label;

        head.append(
            identity,
            badge
        );

        const metrics =
            document.createElement("div");

        metrics.className =
            "director-node-metrics";

        const lastSeen =
            relativeTime(node?.last_seen_at);

        appendMetric(
            metrics,
            "Version",
            node?.server_version
        );

        const lastSeenValue =
            appendMetric(
                metrics,
                "Last seen",
                lastSeen.label,
                lastSeen.title
            );

        const lastSeenAt =
            String(node?.last_seen_at || "").trim();

        if (lastSeenAt) {
            // Security: node-provided data is stored only as a DOM
            // data value and rendered through textContent.
            lastSeenValue.dataset.directorLastSeenAt =
                lastSeenAt;
        }

        appendMetric(
            metrics,
            "Uptime",
            formatUptime(node?.uptime_s)
        );

        appendMetric(
            metrics,
            "Storage",
            formatBytes(node?.storage?.total_bytes)
        );

        appendMetric(
            metrics,
            "Used",
            formatBytes(node?.storage?.used_bytes)
        );

        appendMetric(
            metrics,
            "Free",
            formatBytes(node?.storage?.free_bytes)
        );

        appendMetric(
            metrics,
            "Filesystem",
            node?.storage?.fstype
        );

        const usersEnabled =
            finiteNumber(node?.users?.enabled);

        const usersTotal =
            finiteNumber(node?.users?.total);

        const usersLabel =
            usersEnabled === null || usersTotal === null
                ? "—"
                : `${usersEnabled} enabled / ${usersTotal} total`;

        appendMetric(
            metrics,
            "Users",
            usersLabel
        );

        const assigned =
            finiteNumber(node?.assigned_users);

        appendMetric(
            metrics,
            "Assigned",
            assigned === null
                ? "—"
                : String(assigned)
        );

        article.append(
            head,
            metrics
        );

        const usage =
            storageUsageInfo(node?.storage);

        if (usage) {
            const usagePanel =
                document.createElement("div");

            usagePanel.className =
                "director-storage-usage";

            const usageHeader =
                document.createElement("div");

            usageHeader.className =
                "director-storage-usage-head";

            const usageTitle =
                document.createElement("div");

            usageTitle.className =
                "director-storage-usage-title";

            usageTitle.textContent =
                "Storage usage";

            const usagePercent =
                document.createElement("div");

            usagePercent.className =
                "director-storage-usage-percent";

            usagePercent.textContent =
                `${usage.percentLabel}%`;

            usageHeader.append(
                usageTitle,
                usagePercent
            );

            const track =
                document.createElement("div");

            track.className =
                "director-storage-usage-track";

            track.setAttribute(
                "role",
                "progressbar"
            );

            track.setAttribute(
                "aria-label",
                "Storage usage"
            );

            track.setAttribute(
                "aria-valuemin",
                "0"
            );

            track.setAttribute(
                "aria-valuemax",
                "100"
            );

            track.setAttribute(
                "aria-valuenow",
                usage.percent.toFixed(1)
            );

            const fill =
                document.createElement("div");

            fill.className =
                `director-storage-usage-fill ${usage.tone}`;

            // Security: width is calculated only from finite numeric
            // storage counters and clamped to the range 0..100.
            fill.style.width =
                `${usage.percent.toFixed(2)}%`;

            track.appendChild(fill);

            const usageText =
                document.createElement("div");

            usageText.className =
                "director-storage-usage-text";

            usageText.textContent =
                `${formatBytes(usage.used)} used of ${formatBytes(usage.total)}`;

            usagePanel.append(
                usageHeader,
                track,
                usageText
            );

            article.appendChild(
                usagePanel
            );
        }

        return article;
    }

    function renderNodes(nodes) {
        listEl.replaceChildren();

        const onlineCount =
            nodes.filter(
                node =>
                    String(node?.health || "")
                        .toLowerCase() === "online"
            ).length;

        summaryEl.textContent =
            `${onlineCount} online / ${nodes.length} total`;

        if (nodes.length === 0) {
            const empty =
                document.createElement("div");

            empty.className =
                "director-node-empty";

            empty.textContent =
                "No Storage Nodes are registered.";

            listEl.appendChild(empty);
            return;
        }

        for (const node of nodes) {
            listEl.appendChild(
                nodeCard(node)
            );
        }
    }

    function renderError(message) {
        listEl.replaceChildren();

        const error =
            document.createElement("div");

        error.className =
            "director-node-error";

        error.textContent =
            `Unable to load Storage Nodes: ${text(message, "request failed")}`;

        listEl.appendChild(error);

        summaryEl.textContent =
            "Storage Node status unavailable";
    }

    async function loadNodes(showLoading = true) {
        if (nodesLoading) {
            return;
        }

        nodesLoading = true;

        if (showLoading) {
            refreshButton.disabled = true;
            summaryEl.textContent =
                "Loading Storage Nodes…";
        }

        try {
            const response =
                await fetch(
                    API_NODES,
                    {
                        headers: {
                            "Accept": "application/json"
                        },
                        cache: "no-store",
                        credentials: "same-origin"
                    }
                );

            const payload =
                await response
                    .json()
                    .catch(() => ({}));

            if (!response.ok ||
                payload?.ok !== true ||
                !Array.isArray(payload?.nodes)) {
                throw new Error(
                    text(
                        payload?.message || payload?.error,
                        `HTTP ${response.status}`
                    )
                );
            }

            renderNodes(payload.nodes);
            nodesLoaded = true;
        } catch (error) {
            renderError(
                error instanceof Error
                    ? error.message
                    : String(error)
            );
        } finally {
            nodesLoading = false;
            refreshButton.disabled = false;
        }
    }

    function setView(name) {
        currentView = name;

        for (const button of viewButtons) {
            const active =
                button.dataset.directorViewButton === name;

            button.classList.toggle(
                "active",
                active
            );

            if (active) {
                button.setAttribute(
                    "aria-current",
                    "page"
                );
            } else {
                button.removeAttribute(
                    "aria-current"
                );
            }
        }

        for (const panel of viewPanels) {
            panel.hidden =
                panel.dataset.directorViewPanel !== name;
        }

        if (name === "nodes") {
            if (!nodesLoaded) {
                loadNodes();
            } else {
                updateRelativeTimes();
            }

            startNodeTimers();
        } else {
            stopNodeTimers();
        }
    }

    for (const button of viewButtons) {
        button.addEventListener(
            "click",
            () => {
                setView(
                    button.dataset.directorViewButton
                );
            }
        );
    }

    refreshButton.addEventListener(
        "click",
        loadNodes
    );

    setView("overview");
})();
