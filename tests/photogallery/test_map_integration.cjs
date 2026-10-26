"use strict";
const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const geo = require("../../apps/bundled/photogallery/src/www/gallery_geo_scope.js");
const www = path.join(__dirname, "../../apps/bundled/photogallery/src/www");
const source = fs.readFileSync(path.join(www, "app.js"), "utf8");
function section(start, end) {
    const first = source.indexOf(start), last = source.indexOf(end, first + start.length);
    assert.ok(first >= 0 && last > first, "production section exists: " + start);
    return source.slice(first, last);
}
const code = [
    section("    // Map scope is deliberately independent", "    function injectTopModeButtonCss()"),
    section("    function hasUsableGps(item)", "    function showItemOnMap(item)"),
    section("    function showItemOnMap(item)", "    function mapItems()"),
    section("    function mapItems()", "    function applyViewModeUi()"),
    section("    function setViewMode(mode)", "    function renderMap()"),
    section("    function renderMap()", "    function escapeHtml(s)"),
    "window.testApi = {refreshMapView, cancelMapScope, hasUsableGps, mapItems, setViewMode, showItemOnMap, mapSourceItems, syncMapScopeControls};"
].join("\n");
function photo(name, lat = 60, lon = 22) {return {name, type: "file", has_gps: true, gps_latitude: lat, gps_longitude: lon};}
function folder(name) {return {name, type: "dir"};}
const tick = () => new Promise(resolve => setTimeout(resolve, 0));
async function settle(condition) {for (let n = 0; n < 100; n++) {if (condition()) return; await tick();} assert.fail("async UI did not settle");}

function harness(list = async () => ({ok: true, items: []})) {
    const elements = new Map();
    function element() { return {checked: false, title: "", textContent: "", children: [], listeners: {}, attrs: {},
        addEventListener(name, fn) {this.listeners[name] = fn;},
        setAttribute(key, value) {this.attrs[key] = value;},
        replaceChildren(...children) {this.children = children;}, appendChild(child) {this.children.push(child);}}; }
    const el = id => {if (!elements.has(id)) elements.set(id, element()); return elements.get(id);};
    const state = {curPath: "root", items: [photo("root.jpg"), photo("zero.jpg", 0, 0), folder("child")],
        viewMode: "map", mapIncludeSubfolders: false, mapIncludeZero: false, mapFocusRelPath: "",
        searchLoading: false,
        mapScan: {root: "", items: [], loaded: false, loading: false, error: "", truncated: false, folders: 0, files: 0}};
    const requests = [], rendered = [];
    let gpsFilter = "", nameFilter = "";
    const document = {documentElement: {lang: "fi"}, createElement: element};
    const window = {document, addEventListener() {}, setTimeout,
        PQNAS_PHOTOGALLERY: {geoScope: geo, map: {
            destroyMap() {}, render(canvas, items, deps) {rendered.push({items: Array.from(items), deps});}
        }}
    };
    const box = {window, document, state, el, MutationObserver: class { observe() {} },
        fetchJson: async (url, options) => {requests.push(url); return list(url, options.signal);},
        galleryListUrl: p => p,
        filteredItems: items => (items || state.items).filter(item =>
            (!nameFilter || item.name.includes(nameFilter)) &&
            (gpsFilter === "yes" ? window.testApi.hasUsableGps(item) : gpsFilter === "no" ? !window.testApi.hasUsableGps(item) : true)),
        applyViewModeUi() {}, updateViewStatus() {}, setBadge() {}, refreshFooterStats() {},
        currentRelPathFor: item => item.rel_path || state.curPath + "/" + item.name,
        parentPath: p => p.split("/").slice(0, -1).join("/"), normalizeRelPath: p => p,
        fmtTime() {}, openPreviewFor() {}, galleryThumbUrl() {},
        invalidateSearchCache() {state.searchLoading = false;},
        refreshVisibleView: async force => {if (state.viewMode === "map") await window.testApi.refreshMapView(force);},
        mapCanvas: el("mapCanvas")
    };
    vm.runInNewContext(code, box, {filename: "production-photo-gallery-map-sections.js"});
    return {state, requests, rendered, el, api: window.testApi,
        filters: (gps, name = "") => {gpsFilter = gps; nameFilter = name;}};
}

test("map defaults to the current folder and hides only the suspicious zero pair", async () => {
    const h = harness(); await h.api.refreshMapView();
    assert.equal(h.requests.length, 0);
    assert.deepEqual(h.rendered.at(-1).items.map(x => x.name), ["root.jpg"]);
    assert.match(h.el("mapScopeMessage").textContent, /piilotettiin 1/);
});
test("checkbox loads nested images, then turning it off restores current folder", async () => {
    const h = harness(async p => ({ok: true, items: p.endsWith("deep") ? [photo("deep.jpg")] : [photo("child.jpg"), folder("deep")]}));
    h.el("mapIncludeSubfolders").checked = true;
    h.el("mapIncludeSubfolders").listeners.change();
    await settle(() => h.state.mapScan.loaded);
    assert.deepEqual(h.rendered.at(-1).items.map(x => x.name), ["root.jpg", "child.jpg", "deep.jpg"]);
    assert.deepEqual(h.requests, ["root/child", "root/child/deep"]);
    h.el("mapIncludeSubfolders").checked = false; h.el("mapIncludeSubfolders").listeners.change();
    await tick(); assert.deepEqual(h.rendered.at(-1).items.map(x => x.name), ["root.jpg"]);
});
test("zero override and the GPS filter share the same predicate", async () => {
    const h = harness(); h.filters("yes"); await h.api.refreshMapView();
    assert.equal(h.rendered.at(-1).items.length, 1);
    h.el("mapIncludeZero").checked = true; h.el("mapIncludeZero").listeners.change(); await tick();
    assert.equal(h.rendered.at(-1).items.length, 2);
    assert.match(h.el("mapScopeMessage").textContent, /sallitaan 1/);
});
test("filtering a completed recursive map does not issue another scan", async () => {
    const h = harness(async () => ({ok: true, items: [photo("child.jpg")]}));
    h.state.mapIncludeSubfolders = true; await h.api.refreshMapView();
    h.filters("yes", "child"); await h.api.refreshMapView();
    assert.deepEqual(h.rendered.at(-1).items.map(x => x.name), ["child.jpg"]);
    assert.equal(h.requests.length, 1);
});
test("leaving map during a slow response cancels scan without painting stale results", async () => {
    let signal, release;
    const h = harness(async (p, s) => {signal = s; return new Promise(resolve => {release = resolve;});});
    h.state.mapIncludeSubfolders = true; const task = h.api.refreshMapView(); await tick();
    h.api.setViewMode("grid"); release({ok: true, items: [photo("late.jpg")]}); await task;
    assert.equal(signal.aborted, true); assert.equal(h.rendered.length, 0);
});
test("turning off subfolders cancels without allowing late response to re-enable them", async () => {
    let release;
    const h = harness(async () => new Promise(resolve => {release = resolve;}));
    h.state.mapIncludeSubfolders = true; const old = h.api.refreshMapView(); await tick();
    h.el("mapIncludeSubfolders").checked = false; h.el("mapIncludeSubfolders").listeners.change();
    release({ok: true, items: [photo("late.jpg")]}); await old;
    assert.equal(h.state.mapIncludeSubfolders, false);
    assert.deepEqual(h.rendered.at(-1).items.map(x => x.name), ["root.jpg"]);
});
test("navigating to another folder ignores the old scan", async () => {
    let release;
    const h = harness(async () => new Promise(resolve => {release = resolve;}));
    h.state.mapIncludeSubfolders = true; const old = h.api.refreshMapView(); await tick();
    h.api.cancelMapScope(true); h.state.curPath = "new"; h.state.items = [photo("new.jpg")];
    await h.api.refreshMapView(); release({ok: true, items: [photo("late.jpg")]}); await old;
    assert.deepEqual(h.rendered.at(-1).items.map(x => x.name), ["new.jpg"]);
});
test("failed subtree displays an error rather than no GPS", async () => {
    const h = harness(async () => {throw Error("HTTP 403");}); h.state.mapIncludeSubfolders = true;
    await h.api.refreshMapView(); assert.equal(h.rendered.length, 0);
    assert.match(h.el("mapScopeMessage").textContent, /ei saatu valmiiksi/);
    assert.equal(h.state.mapScan.loaded, false);
});
test("partial results are announced", async () => {
    const h = harness(async () => ({ok: true, items: [photo("child.jpg")], truncated: true}));
    h.state.mapIncludeSubfolders = true; await h.api.refreshMapView();
    assert.match(h.el("mapScopeMessage").textContent, /Osittainen tulos/);
});
test("Show on map enables subfolders for an explicitly selected child photo", async () => {
    const h = harness(async () => ({ok: true, items: [photo("child.jpg")]})); h.state.viewMode = "grid";
    h.api.showItemOnMap({...photo("child.jpg"), rel_path: "root/child/child.jpg"});
    await settle(() => h.state.mapScan.loaded);
    assert.equal(h.state.mapIncludeSubfolders, true);
    assert.equal(h.rendered.at(-1).deps.focusRelPath, "root/child/child.jpg");
});
test("HTML loads classifier before app and keeps the original map provider", () => {
    const html = fs.readFileSync(path.join(www, "index.html"), "utf8");
    assert.ok(html.indexOf('src="./gallery_geo_scope.js') < html.indexOf('src="./app.js'));
    assert.ok(html.includes('id="mapIncludeSubfolders"')); assert.ok(html.includes('id="mapIncludeZero"'));
    const exif = fs.readFileSync(path.join(www, "gallery_exif_search.js"), "utf8");
    assert.ok(exif.includes("PG.hasUsableGps(item)"));
    assert.ok(!exif.includes('v.gps === "yes" && !item.has_gps'));
});
