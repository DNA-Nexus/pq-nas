const assert = require('node:assert/strict');
const { test } = require('node:test');
const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const { chromium } = require('playwright');

const www = path.resolve(__dirname, '../../apps/bundled/photogallery/src/www');
const photo = (rel, extra = {}) => ({
    type: 'file', name: rel.split('/').at(-1), rel_path: rel,
    size_bytes: 1234, mtime_unix: 1700000000, rating: 0,
    keywords: ['cat'], exif: { make: 'Canon' }, ...extra
});
const dir = (name) => ({ type: 'dir', name });
const tinyPng = Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aDqkAAAAASUVORK5CYII=', 'base64');
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
async function until(fn, description) {
    const deadline = Date.now() + 10000;
    while (!fn()) {
        if (Date.now() > deadline) throw new Error(`Timed out: ${description}`);
        await pause(10);
    }
}

async function harness(t, options = {}) {
    const streams = [];
    const pendingLists = new Map();
    let legacyCalls = 0;
    const lists = options.lists || { '': [dir('Photos'), dir('Other')], Photos: [], Other: [] };
    const server = http.createServer((req, res) => {
        const url = new URL(req.url, 'http://localhost');
        const json = (body, status = 200) => {
            res.writeHead(status, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify(body));
        };
        if (url.pathname === '/api/v4/gallery/search_stream') {
            if (options.stream404) return json({ ok: false, error: 'not_found' }, 404);
            res.writeHead(200, { 'Content-Type': 'application/x-ndjson', 'Cache-Control': 'no-store' });
            res.flushHeaders();
            const stream = {
                path: url.searchParams.get('path') || '', closed: false, response: res,
                send(frame) { if (!res.destroyed) res.write(JSON.stringify(frame) + '\n'); },
                finish(frame = { type: 'done', stats: {}, truncated: false }) {
                    if (!res.destroyed) res.end(JSON.stringify(frame) + '\n');
                }
            };
            res.on('close', () => { stream.closed = true; });
            streams.push(stream);
            return;
        }
        if (url.pathname === '/api/v4/gallery/search') {
            legacyCalls++;
            return options.legacy404
                ? json({ ok: false, error: 'not_found' }, 404)
                : json({ ok: true, items: options.legacyItems || [], truncated: !!options.truncated });
        }
        if (url.pathname === '/api/v4/gallery/list') {
            const folder = url.searchParams.get('path') || '';
            const reply = () => json({ ok: true, items: lists[folder] || [] });
            if (options.delayLists?.has(folder)) pendingLists.set(folder, reply);
            else reply();
            return;
        }
        if (url.pathname === '/api/v4/gallery/tree_stats') {
            return json({ ok: true, stats: { dirs: 2, files: 10000, file_bytes: 10000 } });
        }
        if (url.pathname === '/api/v4/gallery/thumb') {
            res.writeHead(200, { 'Content-Type': 'image/png' });
            return res.end(tinyPng);
        }
        if (url.pathname.endsWith('/manifest.json')) return json({ version: '1.1.7' });
        if (url.pathname.startsWith('/static/')) {
            res.writeHead(200, { 'Content-Type': 'text/javascript' });
            return res.end('');
        }
        const filename = url.pathname === '/' ? 'index.html' : path.basename(url.pathname);
        if (!/^[\w.-]+$/.test(filename) || !fs.existsSync(path.join(www, filename))) {
            res.writeHead(404); return res.end();
        }
        let content = fs.readFileSync(path.join(www, filename));
        if (filename === 'index.html') {
            // Load actual gallery, burst and EXIF code. Unrelated network apps
            // are excluded so the test exercises only this feature's lifecycle.
            content = content.toString().replace(/<script\b[^>]*src="([^"]+)"[^>]*><\/script>/g,
                (tag, src) => /(?:\/|^)(?:app|bursts|gallery_geo_scope|gallery_search_stream|gallery_exif_search)\.js/.test(src) ? tag : '');
        }
        res.writeHead(200, { 'Content-Type': filename.endsWith('.html') ? 'text/html' : filename.endsWith('.css') ? 'text/css' : 'text/javascript' });
        res.end(content);
    });
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    let browser;
    const errors = [];
    t.after(async () => {
        await browser?.close();
        for (const stream of streams) stream.response.destroy();
        server.closeAllConnections();
        await new Promise(resolve => server.close(resolve));
        assert.deepEqual(errors, [], 'no uncaught application errors');
    });
    browser = await chromium.launch({
        headless: true,
        ...(process.env.CHROME_EXECUTABLE_PATH ? { executablePath: process.env.CHROME_EXECUTABLE_PATH } : {})
    });
    const page = await browser.newPage({
        viewport: { width: 1200, height: 900 },
        // The fixture owns a loopback listener with an OS-assigned port.
        baseURL: `http://127.0.0.1:${server.address().port}/`
    });
    page.on('pageerror', error => errors.push(error.message));
    await page.addInitScript(() => {
        // Keep this suite offline; exercise the production map scope but replace
        // only its external Leaflet rendering dependency.
        const PG = window.PQNAS_PHOTOGALLERY = window.PQNAS_PHOTOGALLERY || {};
        PG.map = {
            destroyMap() {},
            render(canvas, items) {
                window.renderedMapPaths = items.map(item => item.rel_path || item.name);
                canvas.textContent = window.renderedMapPaths.join(',');
            }
        };
    });
    await page.goto('/');
    await page.waitForFunction(() => document.querySelector('#badge')?.textContent === 'ready');
    return { page, streams, pendingLists, legacyCalls: () => legacyCalls };
}

function batch(items, dirs = 1, files = items.length) {
    return { type: 'batch', items, stats: { dirs, files, file_bytes: files * 1234 } };
}

test('real grid displays partial matches, reuses scan/tiles/selection and handles 10k candidates', { timeout: 60000 }, async t => {
    const h = await harness(t);
    const { page, streams } = h;
    await page.locator('#filterInput').fill('cat');
    await until(() => streams.length === 1, 'first scan');
    streams[0].send(batch(Array.from({ length: 120 }, (_, i) => photo(`Photos/cat${String(i).padStart(4, '0')}.jpg`))));
    await page.waitForSelector('#grid .tile[data-rel-path="Photos/cat0000.jpg"]');
    assert.equal(streams[0].closed, false, 'photos visible before server sends done');
    assert.match(await page.locator('#status').textContent(), /Searching/);
    await page.locator('#grid .tile[data-rel-path="Photos/cat0000.jpg"]').click();
    await page.evaluate(() => {
        window.retainedTile = document.querySelector('[data-rel-path="Photos/cat0000.jpg"]');
        window.retainedThumb = window.retainedTile.querySelector('img');
        window.viewUpdates = 0;
        window.addEventListener('photogallery:view-updated', () => window.viewUpdates++);
    });
    await page.locator('#filterInput').fill('ca');
    await page.locator('#exifSearchToggleBtn').click();
    await page.locator('#exifMakeInput').fill('Canon');
    await pause(250);
    assert.equal(streams.length, 1, 'text/EXIF changes reuse in-flight scan');
    await page.evaluate(() => { window.viewUpdates = 0; });
    streams[0].send({ type: 'progress', stats: { dirs: 5, files: 500 } });
    await page.waitForFunction(() => document.querySelector('#status').textContent.includes('500'));
    assert.equal(await page.evaluate(() => window.viewUpdates), 0, 'stats-only heartbeat does not rebuild/listen through entire grid');

    // Place the viewport on an existing tile, then insert an earlier-sorting
    // folder while a later batch adds the rest of a 10,000-image collection.
    const scroll = await page.evaluate(() => {
        const wrap = document.querySelector('#gridWrap');
        wrap.scrollTop = 1400;
        const bounds = wrap.getBoundingClientRect();
        window.scrollAnchor = Array.from(document.querySelectorAll('#grid .tile'))
            .find(tile => tile.getBoundingClientRect().bottom > bounds.top + 30);
        return { rel: window.scrollAnchor.dataset.relPath, top: window.scrollAnchor.getBoundingClientRect().top };
    });
    const started = Date.now();
    const rest = Array.from({ length: 9880 }, (_, i) => photo(`ZZZ/dog${i}.jpg`, { keywords: ['dog'], exif: { make: 'Nikon' } }));
    streams[0].send(batch([photo('AAA/cat-new.jpg'), ...rest], 50, 10001));
    await page.waitForSelector('#grid .tile[data-rel-path="AAA/cat-new.jpg"]');
    assert.equal(await page.locator('#grid .tile').count(), 121, '10k candidates filtered accurately');
    assert.equal(await page.evaluate(() => window.retainedTile === document.querySelector('[data-rel-path="Photos/cat0000.jpg"]')), true);
    assert.equal(await page.evaluate(() => window.retainedThumb === window.retainedTile.querySelector('img')), true);
    assert.deepEqual(await page.evaluate(() => window.PQNAS_PHOTOGALLERY.getSelectedRelPaths()), ['Photos/cat0000.jpg']);
    const anchorTop = await page.evaluate(() => window.scrollAnchor.getBoundingClientRect().top);
    assert.ok(Math.abs(anchorTop - scroll.top) < 3, `scroll anchor moved ${anchorTop - scroll.top}px`);
    t.diagnostic(`10,001 candidates, 121 matches: partial update visible after ${Date.now() - started}ms`);
    streams[0].finish({ type: 'done', stats: { dirs: 50, files: 10001 }, truncated: false });
    await page.waitForFunction(() => document.querySelector('#badge').textContent === 'ready');
    assert.match(await page.locator('#status').textContent(), /Found 121/);
});

test('folder navigation and refresh abort old streams without stale results', { timeout: 30000 }, async t => {
    const { page, streams } = await harness(t);
    await page.locator('#grid .tile[data-rel-path="Photos"]').dblclick();
    await page.waitForFunction(() => window.PQNAS_PHOTOGALLERY.getCurrentPath() === 'Photos' && document.querySelector('#badge').textContent === 'ready');
    await page.locator('#filterInput').fill('cat');
    await until(() => streams.length === 1, 'folder scan');
    assert.equal(streams[0].path, 'Photos');
    streams[0].send(batch([photo('Photos/cat-old.jpg')]));
    await page.waitForSelector('[data-rel-path="Photos/cat-old.jpg"]');
    await page.locator('#upBtn').click();
    await until(() => streams.length === 2 && streams[0].closed, 'navigation cancels stream');
    assert.equal(streams[1].path, '');
    streams[1].send(batch([photo('Other/cat-current.jpg')]));
    await page.waitForSelector('[data-rel-path="Other/cat-current.jpg"]');
    assert.equal(await page.locator('[data-rel-path="Photos/cat-old.jpg"]').count(), 0);
    await page.locator('#refreshBtn').click();
    await until(() => streams.length === 3 && streams[1].closed, 'refresh cancels old stream');
    streams[2].send(batch([photo('Other/cat-refreshed.jpg')]));
    streams[2].finish();
    await page.waitForSelector('[data-rel-path="Other/cat-refreshed.jpg"]');
    assert.equal(await page.locator('[data-rel-path="Other/cat-current.jpg"]').count(), 0);
});

test('stream errors retain partial results and require explicit refresh to retry', { timeout: 30000 }, async t => {
    const { page, streams } = await harness(t);
    await page.locator('#filterInput').fill('cat');
    await until(() => streams.length === 1, 'scan');
    streams[0].send(batch([photo('Photos/cat.jpg')]));
    await page.waitForSelector('[data-rel-path="Photos/cat.jpg"]');
    streams[0].finish({ type: 'error', error: 'read_failed', message: 'Disk unavailable' });
    await page.waitForFunction(() => document.querySelector('#status').textContent.includes('results are incomplete'));
    assert.equal(await page.locator('[data-rel-path="Photos/cat.jpg"]').count(), 1);
    await page.locator('#filterInput').fill('ca');
    await pause(250);
    assert.equal(streams.length, 1);
    await page.locator('#refreshBtn').click();
    await until(() => streams.length === 2, 'explicit retry');
    streams[1].finish();
    await page.waitForFunction(() => document.querySelector('#badge').textContent === 'ready');
});

test('older servers fall back to an incremental browser folder walk', { timeout: 30000 }, async t => {
    const { page, pendingLists, legacyCalls } = await harness(t, {
        stream404: true, legacy404: true, delayLists: new Set(['ZZZ']),
        lists: { '': [dir('AAA'), dir('ZZZ')], AAA: [photo('AAA/cat.jpg')], ZZZ: [] }
    });
    await page.locator('#filterInput').fill('cat');
    await until(() => pendingLists.has('ZZZ'), 'fallback folder walk');
    await page.waitForSelector('[data-rel-path="AAA/cat.jpg"]');
    assert.equal(legacyCalls(), 1);
    assert.equal(await page.locator('#badge').textContent(), 'search…', 'fallback displays matches before last folder replies');
    pendingLists.get('ZZZ')();
    await page.waitForFunction(() => document.querySelector('#badge').textContent === 'ready');
});

test('legacy capped results are labelled incomplete', { timeout: 30000 }, async t => {
    const { page, legacyCalls } = await harness(t, {
        stream404: true, legacyItems: [photo('Photos/cat.jpg')], truncated: true
    });
    await page.locator('#filterInput').fill('cat');
    await page.waitForSelector('[data-rel-path="Photos/cat.jpg"]');
    assert.equal(legacyCalls(), 1);
    assert.match(await page.locator('#status').textContent(), /limit reached.*incomplete/);
});

test('RAW/JPEG burst grouping and selected group survive later batches', { timeout: 30000 }, async t => {
    const { page, streams } = await harness(t);
    await page.locator('#filterInput').fill('cat');
    await until(() => streams.length === 1, 'burst scan');
    streams[0].send(batch([
        photo('Shots/a.jpg', { capture_time_unix: 100 }),
        photo('Shots/a.cr2', { capture_time_unix: 100 }),
        photo('Shots/b.jpg', { capture_time_unix: 101 }),
        photo('Shots/b.cr2', { capture_time_unix: 101 })
    ]));
    await page.waitForSelector('#grid .pgBurst');
    assert.equal(await page.locator('#grid .pgBurst').count(), 1);
    assert.equal(await page.locator('#grid .pgPairBadge').count(), 1);
    await page.locator('#grid .pgBurstCoverTile').click();
    const selected = await page.evaluate(() => {
        window.retainedBurst = document.querySelector('#grid .pgBurst');
        return window.PQNAS_PHOTOGALLERY.getSelectedRelPaths();
    });
    assert.equal(selected.length, 2, 'collapsed burst selects both displayed captures');
    streams[0].send(batch([photo('ZZZ/cat.jpg')], 2, 5));
    await page.waitForSelector('[data-rel-path="ZZZ/cat.jpg"]');
    assert.equal(await page.evaluate(() => window.retainedBurst === document.querySelector('#grid .pgBurst')), true);
    assert.deepEqual(await page.evaluate(() => window.PQNAS_PHOTOGALLERY.getSelectedRelPaths()), selected);
    streams[0].finish();
    await page.waitForFunction(() => document.querySelector('#badge').textContent === 'ready');
});

test('broad 10k-match result keeps existing cards while a new batch is appended', { timeout: 60000 }, async t => {
    const { page, streams } = await harness(t);
    await page.locator('#filterInput').fill('cat');
    await until(() => streams.length === 1, 'large scan');
    streams[0].send(batch([photo('Photos/cat00000.jpg')]));
    await page.waitForSelector('[data-rel-path="Photos/cat00000.jpg"]');
    await page.evaluate(() => { window.firstLargeTile = document.querySelector('#grid .tile'); });
    const started = Date.now();
    streams[0].send(batch(Array.from({ length: 9999 }, (_, i) => photo(`Photos/cat${String(i + 1).padStart(5, '0')}.jpg`)), 1, 10000));
    await page.waitForFunction(() => document.querySelectorAll('#grid .tile').length === 10000, { timeout: 30000 });
    assert.equal(await page.evaluate(() => window.firstLargeTile === document.querySelector('[data-rel-path="Photos/cat00000.jpg"]')), true);
    t.diagnostic(`10,000 rendered matches: ${Date.now() - started}ms; original card retained`);
    await page.evaluate(() => { window.lastLargeTile = document.querySelector('[data-rel-path="Photos/cat09999.jpg"]'); });
    streams[0].send(batch([photo('ZZZ/cat-extra.jpg')], 2, 10001));
    await page.waitForSelector('[data-rel-path="ZZZ/cat-extra.jpg"]', { state: 'attached' });
    assert.equal(await page.evaluate(() => window.lastLargeTile === document.querySelector('[data-rel-path="Photos/cat09999.jpg"]')), true);
    streams[0].finish();
    await page.waitForFunction(() => document.querySelector('#badge').textContent === 'ready');
    assert.equal(await page.locator('#grid .tile').count(), 10001);
});

test('switching to map cancels progressive grid scan and preserves current-folder GPS scope', { timeout: 30000 }, async t => {
    const { page, streams } = await harness(t, {
        lists: { '': [
            dir('Subfolder'),
            photo('root-valid.jpg', { has_gps: true, gps_latitude: 60, gps_longitude: 24 }),
            photo('root-zero.jpg', { has_gps: true, gps_latitude: 0, gps_longitude: 0 })
        ], Subfolder: [photo('Subfolder/cat.jpg', { has_gps: true, gps_latitude: 40, gps_longitude: 20 })] }
    });
    await page.locator('#filterInput').fill('cat');
    await until(() => streams.length === 1, 'grid scan before map');
    streams[0].send(batch([photo('Subfolder/cat-stream.jpg', { has_gps: true, gps_latitude: 40, gps_longitude: 20 })]));
    await page.waitForSelector('[data-rel-path="Subfolder/cat-stream.jpg"]');
    await page.locator('#mapBtn').click();
    await until(() => streams[0].closed, 'map switch aborts grid stream');
    await page.waitForFunction(() => Array.isArray(window.renderedMapPaths));
    assert.deepEqual(await page.evaluate(() => window.renderedMapPaths), ['root-valid.jpg']);
    assert.equal(await page.locator('#mapIncludeSubfolders').isChecked(), false);
    assert.equal(await page.locator('#mapIncludeZero').isChecked(), false);
    await page.locator('#filterInput').fill('ca');
    await pause(250);
    assert.equal(streams.length, 1, 'map filtering never starts a grid tree scan');
    assert.deepEqual(await page.evaluate(() => window.renderedMapPaths), ['root-valid.jpg']);
});
