(() => {
    "use strict";

    const MESSAGE_TYPE = "pqnas-app-background-task";
    const MESSAGE_VERSION = 1;
    const REASON_RE = /^[a-z0-9][a-z0-9._:-]{0,63}$/;

    function createController(options = {}) {
        const cache = options.cache;
        const prune = options.prune;

        const maxReasonsPerFrame = Math.max(
            1,
            Number(options.maxReasonsPerFrame || 8)
        );

        const maxPinnedFrames = Math.max(
            1,
            Number(options.maxPinnedFrames || 8)
        );

        if (!(cache instanceof Map) || typeof prune !== "function") {
            return null;
        }

        function taskSet(rec) {
            if (!rec || typeof rec !== "object") return null;

            if (!(rec.backgroundTasks instanceof Set)) {
                rec.backgroundTasks = new Set();
            }

            return rec.backgroundTasks;
        }

        function isPinned(rec) {
            const tasks = taskSet(rec);
            return !!(tasks && tasks.size > 0);
        }

        function activeKey() {
            for (const [key, rec] of cache.entries()) {
                if (!rec || !rec.frameWrap) continue;

                if (
                    rec.frameWrap.hidden !== true &&
                    rec.frameWrap.classList.contains("active")
                ) {
                    return key;
                }
            }

            return "";
        }

        function pruneNow() {
            prune(activeKey());
        }

        function recordForSource(sourceWindow) {
            if (!sourceWindow) return null;

            for (const [key, rec] of cache.entries()) {
                if (!rec || !rec.frame) continue;

                try {
                    // Security: derive the sender from the real WindowProxy.
                    // Never trust an app id supplied inside message data.
                    if (rec.frame.contentWindow === sourceWindow) {
                        return { key, rec };
                    }
                } catch (_) {}
            }

            return null;
        }

        function pinnedFrameCount() {
            let count = 0;

            for (const rec of cache.values()) {
                if (isPinned(rec)) count++;
            }

            return count;
        }

        function clear(rec) {
            const tasks = taskSet(rec);
            if (tasks) tasks.clear();
        }

        function handleMessage(event) {
            try {
                // Security: cross-origin frames cannot control shell cache state.
                if (!event || event.origin !== window.location.origin) return;

                const data = event.data;

                if (!data || typeof data !== "object" || Array.isArray(data)) {
                    return;
                }

                // Security: accept only the exact versioned protocol.
                if (data.type !== MESSAGE_TYPE) return;
                if (data.version !== MESSAGE_VERSION) return;
                if (data.action !== "acquire" && data.action !== "release") {
                    return;
                }

                if (
                    typeof data.reason !== "string" ||
                    !REASON_RE.test(data.reason)
                ) {
                    return;
                }

                const found = recordForSource(event.source);
                if (!found) return;

                const tasks = taskSet(found.rec);
                if (!tasks) return;

                if (data.action === "acquire") {
                    if (tasks.has(data.reason)) return;

                    // Resource safety: keep lock storage strictly bounded.
                    if (tasks.size >= maxReasonsPerFrame) return;

                    // Resource safety: protected frames may grow the cache only
                    // up to a fixed limit.
                    if (
                        tasks.size === 0 &&
                        pinnedFrameCount() >= maxPinnedFrames
                    ) {
                        return;
                    }

                    tasks.add(data.reason);
                    return;
                }

                const removed = tasks.delete(data.reason);

                if (removed && tasks.size === 0) {
                    pruneNow();
                }
            } catch (_) {
                // Fail closed: malformed messages do not alter cache state.
            }
        }

        window.addEventListener("message", handleMessage);

        return Object.freeze({
            isPinned,
            clear,
            pruneNow,

            destroy() {
                window.removeEventListener("message", handleMessage);
            }
        });
    }

    window.PQNAS_APP_BACKGROUND_TASKS = Object.freeze({
        createController
    });
})();
