(() => {
    "use strict";

    // Keep transport independent of gallery state so filters can reuse one scan.
    async function read(url, { signal, onBatch, onProgress } = {}) {
        const response = await fetch(url, {
            credentials: "include",
            cache: "no-store",
            headers: { Accept: "application/x-ndjson" },
            signal
        });
        if (response.status === 404) return null;
        if (!response.ok) {
            const body = await response.json().catch(() => null);
            throw new Error(body?.message || body?.error || `HTTP ${response.status}`);
        }
        if (!response.body || typeof response.body.getReader !== "function") {
            throw new Error("Streaming search is unavailable in this browser");
        }

        const reader = response.body.getReader();
        const decoder = new TextDecoder();
        let pending = "";
        let terminal = null;

        function accept(line) {
            if (!line.trim()) return;
            const frame = JSON.parse(line);
            if (!frame || typeof frame !== "object" || terminal) {
                throw new Error("Invalid search stream");
            }
            switch (frame.type) {
                case "batch":
                    if (!Array.isArray(frame.items)) throw new Error("Invalid search batch");
                    onBatch?.(frame.items, frame.stats || {});
                    break;
                case "progress":
                    onProgress?.(frame.stats || {});
                    break;
                case "done":
                    terminal = frame;
                    onProgress?.(frame.stats || {});
                    break;
                case "error":
                    throw new Error(frame.message || frame.error || "Search failed");
                default:
                    throw new Error("Unknown search stream event");
            }
        }

        try {
            while (!terminal) {
                const { value, done } = await reader.read();
                pending += done ? decoder.decode() : decoder.decode(value, { stream: true });
                let newline;
                while ((newline = pending.indexOf("\n")) >= 0) {
                    const line = pending.slice(0, newline);
                    pending = pending.slice(newline + 1);
                    accept(line);
                }
                if (pending.length > 8 * 1024 * 1024) throw new Error("Search stream frame is too large");
                if (done) {
                    if (pending.trim()) accept(pending);
                    if (!terminal) throw new Error("Search connection closed before completion");
                }
            }
            return terminal;
        } finally {
            // Stop the producer on malformed/failed streams as well as success.
            await reader.cancel().catch(() => {});
            reader.releaseLock();
        }
    }

    const api = { read };
    if (typeof module === "object" && module.exports) module.exports = api;
    if (typeof window !== "undefined") {
        const PG = window.PQNAS_PHOTOGALLERY = window.PQNAS_PHOTOGALLERY || {};
        PG.searchStream = api;
    }
})();
