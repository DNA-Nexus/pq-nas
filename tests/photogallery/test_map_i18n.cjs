"use strict";
const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const root = path.join(__dirname, "../..");
const www = path.join(root, "apps/bundled/photogallery/src/www");
const geo = require(path.join(www, "gallery_geo_scope.js"));
const i18n = fs.readFileSync(path.join(root, "server/src/static/i18n.js"), "utf8");
const languageDeclaration = i18n.match(/const LANGS\s*=\s*new Set\((\[[^;]+?\])\);/);
assert.ok(languageDeclaration, "shared language registry must be readable");
const languages = JSON.parse(languageDeclaration[1]);
const params = {folders: 7, files: 23, count: 8, scope: "SCOPE"};
const placeholderSet = text => [...text.matchAll(/\{([A-Za-z]\w*)\}/g)].map(x => x[1]).sort();

test("map dictionaries cover exactly the shared language registry", () => {
    assert.deepEqual([...geo.languages].sort(), [...languages].sort());
    assert.equal(geo.messageKeys.length, 17);
});
for (const language of languages) {
    test(`all map labels, placeholders and status messages exist: ${language}`, () => {
        for (const key of geo.messageKeys) {
            const original = geo.text(language, key);
            assert.equal(typeof original, "string");
            assert.ok(original.trim().length > 0 && original !== key, `${language}.${key}`);
            assert.deepEqual(placeholderSet(original), placeholderSet(geo.text("en", key)), `${language}.${key}`);
            assert.equal(placeholderSet(geo.text(language, key, params)).length, 0);
            // "Error" is the correct same-spelling translation in Spanish.
            if (language !== "en" && !(language === "es" && key === "error")) {
                assert.notEqual(original, geo.text("en", key), `unexpected English fallback: ${language}.${key}`);
            }
        }
    });
}
test("regional, case and underscore language codes normalize like the shared selector", () => {
    for (const [input, language] of [[" FI_fi ", "fi"], ["sv-SE", "sv"], ["uk-UA", "uk"], ["zh-CN", "zh"], ["de-AT", "de"], ["TR_tr", "tr"], ["pt-BR", "en"], ["", "en"]]) {
        assert.equal(geo.normalizeLanguage(input), language);
        assert.equal(geo.text(input, "loading", params), geo.text(language, "loading", params));
    }
});
test("interpolation is one-pass text, not markup or a recursively expanded template", () => {
    const value = geo.text("fi", "mapCount", {count: "{scope}", scope: "<b>alue</b>"});
    assert.equal(value, "Kartta: {scope} GPS-kuvaa • <b>alue</b>");
    assert.equal(geo.text("en", "missing_key"), "missing_key");
});

const source = fs.readFileSync(path.join(www, "app.js"), "utf8");
function section(start, end) {
    const a = source.indexOf(start), b = source.indexOf(end, a + start.length);
    assert.ok(a >= 0 && b > a, "production section exists: " + start);
    return source.slice(a, b);
}
const controller = [
    section("    // Map scope is deliberately independent", "    function injectTopModeButtonCss()"),
    section("    function hasUsableGps(item)", "    function showItemOnMap(item)"),
    section("    function mapItems()", "    function applyViewModeUi()"),
    section("    function renderMap()", "    function escapeHtml(s)"),
    section("    function updateViewStatus()", "    async function refreshVisibleView("),
    "window.testApi = {mapText, refreshMapView, refreshMapLabels, syncMapScopeControls, renderMap};"
].join("\n");
function harness() {
    const elements = new Map(), listeners = new Map();
    let selected = "fi", status = "", badge = null, mapRenders = 0;
    function element(id = "") {
        return {id, title: "", textContent: "", checked: false, attrs: {}, children: [],
            addEventListener() {}, setAttribute(name, value) {this.attrs[name] = value;},
            replaceChildren(...nodes) {for (const node of this.children) if (node.id) elements.delete(node.id); this.children = []; for (const node of nodes) this.appendChild(node);},
            appendChild(node) {this.children.push(node); if (node.id) elements.set(node.id, node);}
        };
    }
    for (const id of ["mapIncludeSubfolders", "mapIncludeZero", "mapScopeMessage", "mapScopeOptions", "mapSubfoldersLabel", "mapZeroLabel", "mapCanvas"]) elements.set(id, element(id));
    const el = id => elements.get(id) || null;
    const state = {curPath: "root", items: [], viewMode: "map", filter: "", ratingFilter: -1,
        mapIncludeSubfolders: true, mapIncludeZero: false, mapFocusRelPath: "",
        mapScan: {root: "root", items: [], loaded: false, loading: true, error: "", truncated: false, folders: 7, files: 23}};
    const document = {documentElement: {lang: "en"}, createElement: () => element()};
    const requests = [];
    const window = {document, addEventListener(name, fn) {listeners.set(name, fn);},
        PQNAS_I18N: {getLanguage: () => selected},
        PQNAS_PHOTOGALLERY: {geoScope: geo, map: {destroyMap() {}, render() {mapRenders++;}}}};
    const box = {window, document, state, el, MutationObserver: class {observe() {}},
        fetchJson: async url => {requests.push(url); return {ok: true, items: []};},
        galleryListUrl: p => p, mapCanvas: el("mapCanvas"), filteredItems: items => items || [],
        applyViewModeUi() {}, setBadge: (kind, label) => {badge = {kind, label};}, setStatus: value => {status = value;},
        refreshFooterStats() {}, currentRelPathFor: item => item.rel_path, fmtTime() {}, openPreviewFor() {}, galleryThumbUrl() {},
        externalSearchSummaryText: () => "", refreshVisibleView() {throw Error("language refresh must not restart search");}
    };
    vm.runInNewContext(controller, box, {filename: "production-map-i18n-controller.js"});
    return {state, el, api: window.testApi, requests, document, window,
        get status() {return status;}, get badge() {return badge;}, get mapRenders() {return mapRenders;},
        select(language) {selected = language; listeners.get("pqnas-language-changed")({detail: {lang: language}});}
    };
}
for (const language of languages) {
    test(`active scan relabels all visible locations without a new request: ${language}`, () => {
        const h = harness(); h.api.renderMap(); h.select(language);
        const expected = geo.text(language, "loading", h.state.mapScan);
        assert.equal(h.el("mapScopeMessage").textContent, expected);
        assert.equal(h.el("mapScopeStateText").textContent, expected);
        assert.equal(h.status, expected);
        assert.equal(h.el("mapSubfoldersLabel").textContent, geo.text(language, "subfolders"));
        assert.equal(h.el("mapZeroLabel").textContent, geo.text(language, "zeros"));
        assert.equal(h.el("mapIncludeSubfolders").title, geo.text(language, "subfoldersTip"));
        assert.equal(h.el("mapScopeMessage").title, geo.text(language, "indexNote"));
        assert.equal(h.badge.label, geo.text(language, "busy"));
        assert.equal(h.requests.length, 0); assert.equal(h.mapRenders, 0);
        assert.equal(h.state.mapScan.loading, true);
    });
}
test("scan progress updates the existing central text without recreating the map", () => {
    const h = harness(); h.api.renderMap(); const panel = h.el("mapScopeStateText");
    h.state.mapScan.folders = 11; h.state.mapScan.files = 41; h.api.syncMapScopeControls();
    assert.equal(h.el("mapScopeStateText"), panel);
    assert.equal(panel.textContent, geo.text("fi", "loading", h.state.mapScan));
    assert.equal(h.requests.length, 0);
});
test("failed scan and completion badges switch language without starting another scan", () => {
    const h = harness(); h.state.mapScan.loading = false; h.state.mapScan.error = "HTTP 403";
    h.api.renderMap(); h.select("de");
    assert.equal(h.el("mapScopeStateText").textContent, geo.text("de", "failed"));
    assert.equal(h.status, geo.text("de", "failed")); assert.equal(h.badge.label, geo.text("de", "error"));
    h.state.mapScan.error = ""; h.state.mapScan.loaded = true; h.api.renderMap(); h.select("sv");
    assert.equal(h.badge.label, geo.text("sv", "ready"));
    assert.equal(h.status, geo.text("sv", "mapCount", {count: 0, scope: geo.text("sv", "recursive")}));
    assert.equal(h.requests.length, 0); assert.equal(h.mapRenders, 1);
});
test("document language is retained as a fallback when the shared API is absent or throws", () => {
    const h = harness(); h.document.documentElement.lang = "it-IT"; delete h.window.PQNAS_I18N;
    assert.equal(h.api.mapText("subfolders"), geo.text("it", "subfolders"));
    h.window.PQNAS_I18N = {getLanguage() {throw Error("not ready");}};
    assert.equal(h.api.mapText("subfolders"), geo.text("it", "subfolders"));
});
test("1.1.7 changed assets are versioned and server minimum stays at 1.2.8", () => {
    const manifest = JSON.parse(fs.readFileSync(path.join(www, "../manifest.json"), "utf8"));
    assert.equal(manifest.version, "1.1.7"); assert.equal(manifest.min_server_version, "1.2.8");
    const html = fs.readFileSync(path.join(www, "index.html"), "utf8");
    for (const asset of ["app.js", "gallery_search_stream.js"]) assert.ok(html.includes(`${asset}?v=1.1.7`));
    for (const asset of ["gallery_geo_scope.js", "gallery_map_scope.css", "gallery_exif_search.js"]) assert.ok(html.includes(`${asset}?v=1.1.6`));
    assert.ok(!html.includes("?v=1.1.5"));
});
