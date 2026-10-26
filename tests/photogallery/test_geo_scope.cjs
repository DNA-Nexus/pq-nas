"use strict";
const test = require("node:test");
const assert = require("node:assert/strict");
const geo = require("../../apps/bundled/photogallery/src/www/gallery_geo_scope.js");
const file = (name, lat = 60, lon = 22, extra = {}) => ({ name, type: "file", has_gps: true, gps_latitude: lat, gps_longitude: lon, ...extra });
const dir = name => ({ name, type: "dir" });
const tick = () => new Promise(resolve => setTimeout(resolve, 0));

test("normal GPS, including numeric strings", () => {
    assert.equal(geo.classifyGps(file("a")), "usable");
    assert.equal(geo.classifyGps(file("a", "60.3", "22.2")), "usable");
});
test("exact zero pair is suspicious, not rewritten or declared impossible", () => {
    const item = file("a", 0, -0); const before = JSON.stringify(item);
    assert.equal(geo.classifyGps(item), "suspect_zero");
    assert.equal(geo.hasUsableGps(item), false);
    assert.equal(geo.hasUsableGps(item, true), true);
    assert.equal(JSON.stringify(item), before);
});
test("equator, prime meridian, and very small valid coordinates survive", () => {
    for (const item of [file("a", 0, 22), file("b", 22, 0), file("c", 1e-9, 1e-9)]) {
        assert.equal(geo.hasUsableGps(item), true);
    }
});
test("all coordinate boundaries are inclusive", () => {
    for (const lat of [-90, 90]) for (const lon of [-180, 180]) assert.equal(geo.hasUsableGps(file("a", lat, lon)), true);
});
test("missing or invalid numeric values are never coerced into GPS", () => {
    for (const value of [null, undefined, "", "  ", true, false, [], {}, "0x10", Infinity, NaN, "junk"]) {
        assert.equal(geo.classifyGps(file("a", 60, 22, {gps_latitude: value})), "invalid", String(value));
    }
    assert.equal(geo.classifyGps(file("a", 91, 22)), "invalid");
    assert.equal(geo.classifyGps(file("a", 60, -181)), "invalid");
});
test("missing flags and folders are not GPS photos", () => {
    for (const flag of [false, "false", null, undefined]) assert.equal(geo.classifyGps(file("a", 60, 22, { has_gps: flag })), "missing");
    assert.equal(geo.classifyGps(dir("x")), "missing");
});
test("GPS summary counts only photos", () => {
    assert.deepEqual(geo.counts([file("a"), file("b", 0, 0), file("c", 100, 22), file("d", 0, 0, {has_gps: false}), dir("x")]),
        {usable: 1, suspect_zero: 1, invalid: 1, missing: 1});
});
test("scan with no subfolders sends no requests", async () => {
    const scanner = geo.createScanner(() => { throw Error("unexpected request"); });
    const result = await scanner.scan("Pictures", [file("a")]);
    assert.equal(result.items.length, 1); assert.equal(result.folders, 1); assert.equal(result.truncated, false);
});
test("nested folders, duplicates and Unicode are handled sequentially", async () => {
    const calls = []; let concurrent = 0, peak = 0;
    const folders = {
        "Kuvat/ä ö": [file("a"), dir("syvä")],
        "Kuvat/b": [file("b")],
        "Kuvat/ä ö/syvä": [file("c")]
    };
    const scanner = geo.createScanner(async path => {
        calls.push(path); concurrent++; peak = Math.max(peak, concurrent); await tick(); concurrent--;
        return { ok: true, items: folders[path] };
    });
    const root = [file("root"), dir("ä ö"), dir("ä ö"), dir("b")];
    const copy = JSON.stringify(root);
    const result = await scanner.scan("Kuvat", root);
    assert.deepEqual(calls, ["Kuvat/ä ö", "Kuvat/b", "Kuvat/ä ö/syvä"]);
    assert.equal(peak, 1); assert.equal(result.items.length, 4); assert.equal(result.folders, 4);
    assert.equal(result.items[3].rel_path, "Kuvat/ä ö/syvä/c");
    assert.equal(JSON.stringify(root), copy);
});
test("unsafe and private child names are not followed", async () => {
    const scanner = geo.createScanner(() => { throw Error("unsafe request"); });
    const result = await scanner.scan("", [dir(".."), dir("/etc"), dir("a/b"), dir("a\\b"), dir(".pqnas"), dir(".pqnas_cache"), file("ok")]);
    assert.equal(result.items.length, 1); assert.equal(result.items[0].rel_path, "ok");
    for (const path of ["/root", "../x", "x/../y", "x\\y", "a\0b"]) {
        await assert.rejects(scanner.scan(path, []), /Unsafe/);
    }
});
test("per-file paths in an API response cannot escape the requested folder", async () => {
    const scanner = geo.createScanner(async () => ({ok: true, items: [file("x", 60, 22, {path: "../../secret", rel_path: "bad"})]}));
    const result = await scanner.scan("safe", [dir("child")]);
    assert.equal(result.items[0].path, "safe/child/x");
});
test("folder limit reports partial results, never silently claims completeness", async () => {
    const scanner = geo.createScanner(async () => ({ok: true, items: [file("a")]}), {maxFolders: 2});
    const result = await scanner.scan("r", [dir("a"), dir("b")]);
    assert.equal(result.folders, 2); assert.equal(result.truncated, true);
});
test("file limit is bounded and reported", async () => {
    const scanner = geo.createScanner(async () => ({ok: true, items: []}), {maxFiles: 2});
    const result = await scanner.scan("r", [file("a"), file("b"), file("c")]);
    assert.equal(result.items.length, 2); assert.equal(result.truncated, true);
});
test("server truncation is retained", async () => {
    const scanner = geo.createScanner(async () => ({ok: true, items: [file("a")], truncated: true}));
    const result = await scanner.scan("r", [dir("a")]); assert.equal(result.truncated, true);
});
test("an unreadable subtree fails rather than pretending no GPS was found", async () => {
    const scanner = geo.createScanner(async () => { throw Error("HTTP 403"); });
    await assert.rejects(scanner.scan("r", [dir("x")]), /HTTP 403/);
});
test("malformed folder responses are explicit failures", async () => {
    for (const response of [null, {ok: false, items: []}, {ok: true}]) {
        const scanner = geo.createScanner(async () => response);
        await assert.rejects(scanner.scan("r", [dir("x")]), /Invalid gallery/);
    }
});
test("cancel aborts the active request and prevents later folder requests", async () => {
    let signal;
    const scanner = geo.createScanner(async (path, s) => { signal = s; return new Promise(() => {}); });
    const pending = scanner.scan("r", [dir("a"), dir("b")]); await tick(); scanner.cancel();
    await assert.rejects(pending, {name: "AbortError"}); assert.equal(signal.aborted, true);
});
test("a new root supersedes an old response even if the fake server ignores abort", async () => {
    let resolveOld;
    const scanner = geo.createScanner(async () => new Promise(resolve => {resolveOld = resolve;}));
    const old = scanner.scan("old", [dir("child")]); await tick();
    const rejected = assert.rejects(old, {name: "AbortError"});
    const fresh = await scanner.scan("new", [file("new")]);
    resolveOld({ok: true, items: [file("stale")]}); await rejected;
    assert.deepEqual(fresh.items.map(x => x.path), ["new/new"]);
});
test("request timeout is reported and aborts request", async () => {
    let signal;
    const scanner = geo.createScanner(async (p, s) => {signal = s; return new Promise(() => {});}, {timeoutMs: 10});
    await assert.rejects(scanner.scan("r", [dir("x")]), {name: "TimeoutError"});
    assert.equal(signal.aborted, true);
});
test("limits cannot be disabled with zero, negative or noninteger settings", () => {
    for (const maxFiles of [0, -1, 2.5, Infinity]) assert.throws(() => geo.createScanner(() => {}, {maxFiles}), /positive integers/);
});
test("Finnish and English labels use plain text and placeholders", () => {
    assert.equal(geo.text("fi-FI", "subfolders"), "Sisällytä alikansiot");
    assert.equal(geo.text("en", "subfolders"), "Include subfolders");
    assert.match(geo.text("fi", "hiddenZero", {count: 8}), /8/);
});
