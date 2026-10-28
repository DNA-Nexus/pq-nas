"use strict";

const assert = require("node:assert/strict");
const { test } = require("node:test");
const { read } = require("../../apps/bundled/photogallery/src/www/gallery_search_stream.js");

const encoder = new TextEncoder();
const line = (frame) => JSON.stringify(frame) + "\n";

function mockResponse(chunks, { nextRead } = {}) {
    let index = 0;
    const cleanup = { cancelled: 0, released: 0 };
    const reader = {
        async read() {
            if (nextRead) return nextRead(index++);
            return index < chunks.length
                ? { value: chunks[index++], done: false }
                : { done: true };
        },
        async cancel() { cleanup.cancelled++; },
        releaseLock() { cleanup.released++; }
    };
    return { response: { ok: true, status: 200, body: { getReader: () => reader } }, cleanup };
}

async function withFetch(mock, run) {
    const previous = global.fetch;
    global.fetch = mock;
    try { return await run(); } finally { global.fetch = previous; }
}

test("decodes split UTF-8, CRLF, blank lines and final frame without newline", async () => {
    const item = { type: "file", name: "Kesäkuva 🐱.jpg", rel_path: "Suomi/Kesäkuva 🐱.jpg" };
    const stats = { dirs: 3, files: 1, file_bytes: 123 };
    const wire = encoder.encode("\r\n" + line({ type: "batch", items: [item], stats }).replace(/\n$/, "\r\n") +
        line({ type: "progress", stats }) + JSON.stringify({ type: "done", stats, truncated: false }));
    // One-byte chunks split both JSON tokens and the UTF-8 code points.
    const chunks = Array.from(wire, (byte) => Uint8Array.of(byte));
    const { response, cleanup } = mockResponse(chunks);
    const batches = [];
    const progress = [];
    await withFetch(async (url, options) => {
        assert.equal(url, "/api/v4/gallery/search_stream?path=Suomi");
        assert.equal(options.credentials, "include");
        assert.equal(options.cache, "no-store");
        assert.equal(options.headers.Accept, "application/x-ndjson");
        return response;
    }, async () => {
        const result = await read("/api/v4/gallery/search_stream?path=Suomi", {
            onBatch: (items, counts) => batches.push({ items, counts }),
            onProgress: (counts) => progress.push(counts)
        });
        assert.deepEqual(batches, [{ items: [item], counts: stats }]);
        assert.deepEqual(progress, [stats, stats]);
        assert.equal(result.type, "done");
        assert.equal(result.truncated, false);
    });
    assert.deepEqual(cleanup, { cancelled: 1, released: 1 });
});

test("delivers the first batch while the terminal event is still withheld", async () => {
    let finish;
    const terminal = new Promise((resolve) => { finish = resolve; });
    const batch = { type: "batch", items: [{ name: "first.jpg" }], stats: { files: 1 } };
    const { response, cleanup } = mockResponse([], {
        nextRead: (index) => index === 0
            ? { value: encoder.encode(line(batch)), done: false }
            : terminal
    });
    let delivered = false;
    let settled = false;
    await withFetch(async () => response, async () => {
        const running = read("/search", { onBatch: () => { delivered = true; } });
        running.finally(() => { settled = true; });
        await new Promise((resolve) => setImmediate(resolve));
        assert.equal(delivered, true);
        assert.equal(settled, false);
        finish({ value: encoder.encode(line({ type: "done", stats: { files: 1 }, truncated: true })), done: false });
        assert.equal((await running).truncated, true);
    });
    assert.deepEqual(cleanup, { cancelled: 1, released: 1 });
});

test("preserves an earlier batch and reports an explicit server error", async () => {
    const { response, cleanup } = mockResponse([encoder.encode(
        line({ type: "batch", items: [{ name: "kept.jpg" }] }) +
        line({ type: "error", error: "scan_failed", message: "Folder became unavailable" })
    )]);
    const received = [];
    await withFetch(async () => response, async () => {
        await assert.rejects(read("/search", { onBatch: (items) => received.push(...items) }),
            /Folder became unavailable/);
    });
    assert.deepEqual(received, [{ name: "kept.jpg" }]);
    assert.deepEqual(cleanup, { cancelled: 1, released: 1 });
});

test("rejects premature EOF instead of marking incomplete results complete", async () => {
    const { response, cleanup } = mockResponse([encoder.encode(line({ type: "batch", items: [] }))]);
    await withFetch(async () => response, async () => {
        await assert.rejects(read("/search"), /closed before completion/);
    });
    assert.deepEqual(cleanup, { cancelled: 1, released: 1 });
});

test("404 permits legacy fallback and other HTTP failures remain failures", async () => {
    await withFetch(async () => ({ ok: false, status: 404 }), async () => {
        assert.equal(await read("/search"), null);
    });
    for (const status of [401, 403, 500]) {
        await withFetch(async () => ({ ok: false, status, json: async () => ({ error: "denied", message: `failure ${status}` }) }),
            async () => assert.rejects(read("/search"), new RegExp(`failure ${status}`)));
    }
    await withFetch(async () => ({ ok: false, status: 503, json: async () => { throw new Error("not JSON"); } }),
        async () => assert.rejects(read("/search"), /HTTP 503/));
});

test("forwards cancellation before fetch and while reading without fallback", async () => {
    const controller = new AbortController();
    const aborted = new DOMException("The operation was aborted", "AbortError");
    await withFetch(async (_url, options) => {
        assert.equal(options.signal, controller.signal);
        throw aborted;
    }, async () => assert.rejects(read("/search", { signal: controller.signal }), (error) => error === aborted));

    let reading;
    const started = new Promise((resolve) => { reading = resolve; });
    const { response, cleanup } = mockResponse([], {
        nextRead: () => new Promise((_resolve, reject) => {
            reading();
            controller.signal.addEventListener("abort", () => reject(aborted), { once: true });
        })
    });
    await withFetch(async () => response, async () => {
        const running = read("/search", { signal: controller.signal });
        await started;
        controller.abort();
        await assert.rejects(running, (error) => error === aborted);
    });
    assert.deepEqual(cleanup, { cancelled: 1, released: 1 });
});

test("malformed frames and failed callbacks cancel and release the reader", async () => {
    const cases = [
        { wire: "{bad JSON}\n", expected: /JSON/ },
        { wire: line({ type: "batch", items: {} }), expected: /Invalid search batch/ },
        { wire: line({ type: "unknown" }), expected: /Unknown search stream event/ },
        { wire: line(null), expected: /Invalid search stream/ },
        { wire: line({ type: "batch", items: [] }), callback: () => { throw new Error("consumer failed"); }, expected: /consumer failed/ }
    ];
    for (const scenario of cases) {
        const { response, cleanup } = mockResponse([encoder.encode(scenario.wire)]);
        await withFetch(async () => response, async () => {
            await assert.rejects(read("/search", { onBatch: scenario.callback }), scenario.expected);
        });
        assert.deepEqual(cleanup, { cancelled: 1, released: 1 });
    }
});

test("rejects oversized unterminated frames and non-streaming success responses", async () => {
    const { response, cleanup } = mockResponse([encoder.encode(" ".repeat(8 * 1024 * 1024 + 1))]);
    await withFetch(async () => response, async () => assert.rejects(read("/search"), /too large/));
    assert.deepEqual(cleanup, { cancelled: 1, released: 1 });
    await withFetch(async () => ({ ok: true, status: 200, body: null }),
        async () => assert.rejects(read("/search"), /unavailable in this browser/));
});
