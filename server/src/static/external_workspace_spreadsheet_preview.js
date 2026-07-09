(() => {
    "use strict";

    if (window.__pqnasExternalSpreadsheetPreviewV1) return;
    window.__pqnasExternalSpreadsheetPreviewV1 = true;

    const SPREADSHEET_EXTS = new Set(["csv", "tsv", "xls", "xlsx", "ods"]);
    const MAX_RENDER_ROWS = 1000;
    const MAX_RENDER_COLS = 80;
    const XLSX_VENDOR_URL = "/static/vendor/xlsx.full.min.js";

    let root = null;
    let titleEl = null;
    let pathEl = null;
    let infoEl = null;
    let tabsEl = null;
    let bodyEl = null;
    let downloadBtn = null;
    let openSeq = 0;
    let resizeState = null;
    let xlsxLoadPromise = null;

    function tr(key, vars = null, fallback = "") {
        try {
            if (window.PQNAS_I18N && typeof window.PQNAS_I18N.t === "function") {
                return window.PQNAS_I18N.t(key, vars, fallback || key);
            }
        } catch (_) {}
        return fallback || key;
    }

    function escapeHtml(s) {
        return String(s == null ? "" : s).replace(/[&<>"']/g, (ch) => ({
            "&": "&amp;",
            "<": "&lt;",
            ">": "&gt;",
            '"': "&quot;",
            "'": "&#39;"
        }[ch]));
    }

    function fileExtLower(name) {
        const n = String(name || "").toLowerCase().trim();
        const clean = n.split("?")[0].split("#")[0];
        const slash = Math.max(clean.lastIndexOf("/"), clean.lastIndexOf("\\"));
        const base = slash >= 0 ? clean.slice(slash + 1) : clean;
        const dot = base.lastIndexOf(".");
        if (dot <= 0 || dot === base.length - 1) return "";
        return base.slice(dot + 1);
    }

    function isSpreadsheetName(name) {
        return SPREADSHEET_EXTS.has(fileExtLower(name));
    }

    function canOpenFor(item) {
        return !!item && !item.isDir && isSpreadsheetName(item.name || item.rel || "");
    }

    function normalizeRelPath(path) {
        return String(path || "").replace(/^\/+/, "");
    }

    function basenameFromPath(path) {
        const p = normalizeRelPath(path);
        const i = p.lastIndexOf("/");
        return i >= 0 ? p.slice(i + 1) : p;
    }

    function downloadUrl(relPath) {
        const qs = new URLSearchParams();
        qs.set("path", normalizeRelPath(relPath));
        return `/api/v4/workspaces/files/get?${qs.toString()}`;
    }

    function columnName(idx) {
        let n = idx + 1;
        let out = "";
        while (n > 0) {
            const r = (n - 1) % 26;
            out = String.fromCharCode(65 + r) + out;
            n = Math.floor((n - 1) / 26);
        }
        return out;
    }

    function parseDelimited(text, delimiter) {
        const rows = [];
        let row = [];
        let field = "";
        let quoted = false;
        const s = String(text || "");

        for (let i = 0; i < s.length; i++) {
            const ch = s[i];

            if (quoted) {
                if (ch === '"') {
                    if (s[i + 1] === '"') {
                        field += '"';
                        i++;
                    } else {
                        quoted = false;
                    }
                } else {
                    field += ch;
                }
                continue;
            }

            if (ch === '"') quoted = true;
            else if (ch === delimiter) {
                row.push(field);
                field = "";
            } else if (ch === "\n") {
                row.push(field);
                rows.push(row);
                row = [];
                field = "";
            } else if (ch !== "\r") {
                field += ch;
            }
        }

        row.push(field);
        if (row.length > 1 || row[0] !== "" || rows.length === 0) rows.push(row);
        return rows;
    }

    function normalizeRows(rows) {
        const out = Array.isArray(rows) ? rows : [];
        let maxCols = 0;
        for (const row of out) {
            if (Array.isArray(row)) maxCols = Math.max(maxCols, row.length);
        }
        maxCols = Math.min(maxCols, MAX_RENDER_COLS);
        return {
            rows: out.slice(0, MAX_RENDER_ROWS).map((row) => {
                const arr = Array.isArray(row) ? row : [];
                return arr.slice(0, maxCols).map((v) => v == null ? "" : String(v));
            }),
            cols: maxCols,
            truncatedRows: out.length > MAX_RENDER_ROWS,
            truncatedCols: out.some((row) => Array.isArray(row) && row.length > MAX_RENDER_COLS)
        };
    }

    function ensureXlsxLibrary() {
        if (window.XLSX && typeof window.XLSX.read === "function") {
            return Promise.resolve(window.XLSX);
        }
        if (xlsxLoadPromise) return xlsxLoadPromise;

        xlsxLoadPromise = new Promise((resolve, reject) => {
            const existing = document.querySelector('script[data-pqnas-xlsx-lib="1"]');
            if (existing) {
                existing.addEventListener("load", () => resolve(window.XLSX), { once: true });
                existing.addEventListener("error", () => reject(new Error("XLSX parser failed to load.")), { once: true });
                return;
            }

            const script = document.createElement("script");
            script.src = XLSX_VENDOR_URL;
            script.defer = true;
            script.dataset.pqnasXlsxLib = "1";
            script.onload = () => {
                if (window.XLSX && typeof window.XLSX.read === "function") resolve(window.XLSX);
                else reject(new Error("XLSX parser loaded, but window.XLSX is missing."));
            };
            script.onerror = () => reject(new Error("XLSX parser is not installed. Vendor xlsx.full.min.js under server/src/static/vendor/."));
            document.head.appendChild(script);
        });

        return xlsxLoadPromise;
    }

    async function readWorkbookRows(url, ext) {
        const r = await fetch(url, { credentials: "include", cache: "no-store" });
        if (!r.ok) {
            const msg = await r.text().catch(() => "");
            throw new Error(msg || `HTTP ${r.status}`);
        }

        if (ext === "csv" || ext === "tsv") {
            const text = await r.text();
            return [{ name: ext.toUpperCase(), rows: parseDelimited(text, ext === "tsv" ? "\t" : ",") }];
        }

        const XLSX = await ensureXlsxLibrary();
        const buf = await r.arrayBuffer();

        // Security: parse spreadsheet data only. Macros, active content and
        // formula execution are not run by this preview.
        const wb = XLSX.read(buf, {
            type: "array",
            cellFormula: false,
            cellHTML: false,
            cellNF: false,
            cellStyles: false
        });

        const names = Array.isArray(wb.SheetNames) ? wb.SheetNames : [];
        return names.map((name) => {
            const ws = wb.Sheets[name];
            const rows = XLSX.utils.sheet_to_json(ws, {
                header: 1,
                raw: false,
                defval: "",
                blankrows: false
            });
            return { name, rows };
        });
    }

    function ensureStyles() {
        if (document.getElementById("externalSpreadsheetPreviewStyles")) return;

        const style = document.createElement("style");
        style.id = "externalSpreadsheetPreviewStyles";
        style.textContent = `
.externalSpreadsheetPreviewOverlay{
    --spreadsheet-overlay:var(--overlay-bg,var(--modal-backdrop));
    --spreadsheet-surface:var(--modal-bg,var(--elevated-bg,var(--card,var(--panel))));
    --spreadsheet-surface-2:var(--panel-head-bg,var(--elevated-bg,var(--card2,var(--panel2,var(--spreadsheet-surface)))));
    --spreadsheet-header-bg:var(--sheet-header-bg,var(--table-header-bg,var(--spreadsheet-surface-2)));
    --spreadsheet-cell-bg:var(--sheet-cell-bg,var(--table-cell-bg,var(--spreadsheet-surface)));
    --spreadsheet-muted-text:var(--muted-text,var(--fg-dim,var(--text)));

    position:fixed; inset:0; display:none; z-index:9999;
    background:var(--spreadsheet-overlay);
    padding:22px; box-sizing:border-box;
}
.externalSpreadsheetPreviewOverlay.show{display:flex; align-items:center; justify-content:center;}
.externalSpreadsheetPreviewCard{
    width:min(1320px,96vw); height:min(860px,92vh); display:flex; flex-direction:column;
    overflow:hidden; background:var(--spreadsheet-surface); color:var(--text);
    border:1px solid var(--border); border-radius:18px;
    box-shadow:var(--modal-shadow,var(--shadow-lg,var(--shadow)));
    resize:none; min-width:min(720px,96vw); min-height:min(420px,92vh);
    max-width:96vw; max-height:92vh;
}
.externalSpreadsheetPreviewHead{
    display:flex; align-items:center; gap:12px; padding:12px 14px;
    border-bottom:1px solid var(--border); background:var(--spreadsheet-surface-2);
}
.externalSpreadsheetPreviewTitleWrap{min-width:0; flex:1 1 auto;}
.externalSpreadsheetPreviewTitle{font-weight:900; font-size:15px; white-space:nowrap; overflow:hidden; text-overflow:ellipsis;}
.externalSpreadsheetPreviewPath{color:var(--spreadsheet-muted-text); font-size:12px; white-space:nowrap; overflow:hidden; text-overflow:ellipsis;}
.externalSpreadsheetPreviewActions{display:flex; align-items:center; gap:8px; flex-wrap:wrap;}
.externalSpreadsheetPreviewInfo{padding:8px 14px; border-bottom:1px solid var(--border); font-size:12px; color:var(--spreadsheet-muted-text); background:var(--spreadsheet-surface);}
.externalSpreadsheetPreviewTabs{display:flex; gap:6px; padding:8px 14px; border-bottom:1px solid var(--border); overflow-x:auto; background:var(--spreadsheet-surface);}
.externalSpreadsheetPreviewTab{border:1px solid var(--border); background:var(--button-bg,var(--spreadsheet-surface-2)); color:var(--button-text,var(--text)); border-radius:999px; padding:6px 10px; font:inherit; font-size:12px; cursor:pointer;}
.externalSpreadsheetPreviewTab.active{background:var(--accent-bg,var(--button-active-bg,var(--button-bg,var(--spreadsheet-surface-2)))); color:var(--accent-text,var(--button-text,var(--text))); border-color:var(--accent,var(--border));}
.externalSpreadsheetPreviewBody{flex:1 1 auto; min-height:0; overflow:auto; background:var(--spreadsheet-surface);}
.externalSpreadsheetPreviewTable{border-collapse:separate; border-spacing:0; width:max-content; min-width:100%; font-size:13px;}
.externalSpreadsheetPreviewTable th,.externalSpreadsheetPreviewTable td{
    border-right:1px solid var(--border); border-bottom:1px solid var(--border);
    padding:6px 8px; min-width:96px; max-width:360px; height:28px; vertical-align:top;
    white-space:pre; overflow:hidden; text-overflow:ellipsis;
}
.externalSpreadsheetPreviewTable thead th,
.externalSpreadsheetPreviewTable .rowHead{background:var(--spreadsheet-header-bg); color:var(--table-header-text,var(--text)); font-weight:900;}
.externalSpreadsheetPreviewTable td{background:var(--spreadsheet-cell-bg); color:var(--table-cell-text,var(--text));}
.externalSpreadsheetPreviewTable thead th{position:sticky; top:0; z-index:3; text-align:center;}
.externalSpreadsheetPreviewTable .rowHead{position:sticky; left:0; z-index:2; min-width:54px; width:54px; text-align:right; font-weight:800;}
.externalSpreadsheetPreviewTable thead .corner{left:0; z-index:4;}
.externalSpreadsheetPreviewCard{position:relative;}
.externalSpreadsheetPreviewResizeHandle{position:absolute; z-index:8; background:var(--border2,var(--border)); opacity:.42; touch-action:none;}
.externalSpreadsheetPreviewResizeHandle:hover,
.externalSpreadsheetPreviewResizeHandle.active{opacity:.78;}
.externalSpreadsheetPreviewResizeRight{top:0; right:0; bottom:0; width:8px; cursor:ew-resize;}
.externalSpreadsheetPreviewResizeBottom{left:0; right:0; bottom:0; height:8px; cursor:ns-resize;}
.externalSpreadsheetPreviewEmpty,.externalSpreadsheetPreviewError{padding:22px;}
.externalSpreadsheetPreviewError{color:var(--danger,var(--text));}
@media(max-width:720px){
    .externalSpreadsheetPreviewOverlay{padding:8px;}
    .externalSpreadsheetPreviewCard{width:100%; height:96vh; border-radius:12px; resize:none; min-width:0; min-height:0;}
    .externalSpreadsheetPreviewHead{align-items:flex-start; flex-direction:column;}
    .externalSpreadsheetPreviewActions{width:100%;}

/* pqnas-external-spreadsheet-edge-resize-rails-20260709
   Visible token-based resize rails. Keep them out of table scrollbars. */
.externalSpreadsheetPreviewHead,
.externalSpreadsheetPreviewInfo,
.externalSpreadsheetPreviewTabs,
.externalSpreadsheetPreviewBody{margin-right:14px;}
.externalSpreadsheetPreviewResizeHandle{background:var(--accent,var(--border2,var(--border))); opacity:.62;}
.externalSpreadsheetPreviewResizeHandle:hover,
.externalSpreadsheetPreviewResizeHandle.active{opacity:.88;}
.externalSpreadsheetPreviewResizeRight{top:0; right:0; bottom:14px; width:14px; cursor:ew-resize; border-left:1px solid var(--border);}
.externalSpreadsheetPreviewResizeBottom{position:relative; left:auto; right:auto; top:auto; bottom:auto; width:auto; height:14px; flex:0 0 14px; cursor:ns-resize; border-top:1px solid var(--border);}
@media(max-width:720px){
    .externalSpreadsheetPreviewHead,
    .externalSpreadsheetPreviewInfo,
    .externalSpreadsheetPreviewTabs,
    .externalSpreadsheetPreviewBody{margin-right:0;}
}

/* pqnas-external-spreadsheet-corner-resize-handle-20260709 */
.externalSpreadsheetPreviewResizeRight{bottom:16px;}
.externalSpreadsheetPreviewResizeBottom{right:16px;}
.externalSpreadsheetPreviewResizeCorner{
    position:absolute; right:0; bottom:0; width:16px; height:16px; z-index:10;
    cursor:nwse-resize; touch-action:none;
    background:var(--accent,var(--border2,var(--border)));
    opacity:.76; border-left:1px solid var(--border); border-top:1px solid var(--border);
}
.externalSpreadsheetPreviewResizeCorner:hover,
.externalSpreadsheetPreviewResizeCorner.active{opacity:.95;}
@media(max-width:720px){.externalSpreadsheetPreviewResizeCorner{display:none;}}
}`;
        document.head.appendChild(style);
    }

    function clampNumber(v, min, max) {
        return Math.max(min, Math.min(max, v));
    }

    function beginResize(ev, mode) {
        const card = root ? root.querySelector(".externalSpreadsheetPreviewCard") : null;
        if (!card) return;

        const r = card.getBoundingClientRect();
        resizeState = {
            mode,
            startX: ev.clientX,
            startY: ev.clientY,
            startWidth: r.width,
            startHeight: r.height
        };

        card.style.width = `${r.width}px`;
        card.style.height = `${r.height}px`;

        ev.currentTarget.classList.add("active");
        try { ev.currentTarget.setPointerCapture(ev.pointerId); } catch (_) {}
        ev.preventDefault();
    }

    function moveResize(ev) {
        if (!resizeState || !root) return;

        const card = root.querySelector(".externalSpreadsheetPreviewCard");
        if (!card) return;

        const maxW = Math.max(320, window.innerWidth * 0.96);
        const maxH = Math.max(260, window.innerHeight * 0.92);
        const minW = Math.min(720, maxW);
        const minH = Math.min(420, maxH);

        if (resizeState.mode === "right") {
            card.style.width = `${clampNumber(resizeState.startWidth + ev.clientX - resizeState.startX, minW, maxW)}px`;
        } else if (resizeState.mode === "bottom") {
            card.style.height = `${clampNumber(resizeState.startHeight + ev.clientY - resizeState.startY, minH, maxH)}px`;
        } else if (resizeState.mode === "corner") {
            card.style.width = `${clampNumber(resizeState.startWidth + ev.clientX - resizeState.startX, minW, maxW)}px`;
            card.style.height = `${clampNumber(resizeState.startHeight + ev.clientY - resizeState.startY, minH, maxH)}px`;
        }
    }

    function endResize() {
        if (!resizeState || !root) return;
        for (const h of root.querySelectorAll(".externalSpreadsheetPreviewResizeHandle.active")) {
            h.classList.remove("active");
        }
        resizeState = null;
    }

    function ensureUi() {
        if (root) return;
        ensureStyles();

        root = document.createElement("div");
        root.className = "externalSpreadsheetPreviewOverlay";
        root.setAttribute("aria-hidden", "true");
        root.innerHTML = `
            <div class="externalSpreadsheetPreviewCard" role="dialog" aria-modal="true" aria-label="${escapeHtml(tr("external.spreadsheet.title", null, "Spreadsheet preview"))}">
                <div class="externalSpreadsheetPreviewHead">
                    <div class="externalSpreadsheetPreviewTitleWrap">
                        <div class="externalSpreadsheetPreviewTitle">${escapeHtml(tr("external.spreadsheet.title", null, "Spreadsheet preview"))}</div>
                        <div class="externalSpreadsheetPreviewPath mono">/</div>
                    </div>
                    <div class="externalSpreadsheetPreviewActions">
                        <button class="pq-btn secondary" id="externalSpreadsheetPreviewDownload" type="button">${escapeHtml(tr("external.menu.download", null, "Download"))}</button>
                        <button class="pq-btn secondary" id="externalSpreadsheetPreviewClose" type="button">${escapeHtml(tr("external.modal.close", null, "Close"))}</button>
                    </div>
                </div>
                <div class="externalSpreadsheetPreviewInfo">${escapeHtml(tr("common.loading", null, "Loading…"))}</div>
                <div class="externalSpreadsheetPreviewTabs"></div>
                <div class="externalSpreadsheetPreviewBody"></div>
                <div class="externalSpreadsheetPreviewResizeHandle externalSpreadsheetPreviewResizeRight" data-external-spreadsheet-resize="right" aria-hidden="true"></div>
                <div class="externalSpreadsheetPreviewResizeHandle externalSpreadsheetPreviewResizeBottom" data-external-spreadsheet-resize="bottom" aria-hidden="true"></div>
                <div class="externalSpreadsheetPreviewResizeHandle externalSpreadsheetPreviewResizeCorner" data-external-spreadsheet-resize="corner" aria-hidden="true"></div>
            </div>
        `;
        document.body.appendChild(root);

        titleEl = root.querySelector(".externalSpreadsheetPreviewTitle");
        pathEl = root.querySelector(".externalSpreadsheetPreviewPath");
        infoEl = root.querySelector(".externalSpreadsheetPreviewInfo");
        tabsEl = root.querySelector(".externalSpreadsheetPreviewTabs");
        bodyEl = root.querySelector(".externalSpreadsheetPreviewBody");
        downloadBtn = root.querySelector("#externalSpreadsheetPreviewDownload");

        root.querySelector("#externalSpreadsheetPreviewClose")?.addEventListener("click", closePreview);
        root.addEventListener("click", (ev) => {
            if (ev.target === root) closePreview();
        });
        document.addEventListener("keydown", (ev) => {
            if (ev.key === "Escape" && root.classList.contains("show")) closePreview();
        });

        for (const handle of root.querySelectorAll("[data-external-spreadsheet-resize]")) {
            handle.addEventListener("pointerdown", (ev) => beginResize(ev, handle.dataset.externalSpreadsheetResize || ""));
        }
        document.addEventListener("pointermove", moveResize);
        document.addEventListener("pointerup", endResize);
        document.addEventListener("pointercancel", endResize);
    }

    function setInfo(text) {
        if (infoEl) infoEl.textContent = text || "";
    }

    function renderSheet(sheets, index) {
        const sheet = sheets[index] || { name: "Sheet", rows: [] };
        const normalized = normalizeRows(sheet.rows);

        tabsEl.innerHTML = "";
        sheets.forEach((s, i) => {
            const btn = document.createElement("button");
            btn.type = "button";
            btn.className = "externalSpreadsheetPreviewTab" + (i === index ? " active" : "");
            btn.textContent = s.name || `Sheet ${i + 1}`;
            btn.addEventListener("click", () => renderSheet(sheets, i));
            tabsEl.appendChild(btn);
        });

        bodyEl.innerHTML = "";

        if (!normalized.rows.length || normalized.cols <= 0) {
            const empty = document.createElement("div");
            empty.className = "externalSpreadsheetPreviewEmpty";
            empty.textContent = tr("external.spreadsheet.empty", null, "This sheet is empty.");
            bodyEl.appendChild(empty);
            return;
        }

        const table = document.createElement("table");
        table.className = "externalSpreadsheetPreviewTable";

        const thead = document.createElement("thead");
        const hr = document.createElement("tr");
        const corner = document.createElement("th");
        corner.className = "rowHead corner";
        hr.appendChild(corner);

        for (let c = 0; c < normalized.cols; c++) {
            const th = document.createElement("th");
            th.textContent = columnName(c);
            hr.appendChild(th);
        }

        thead.appendChild(hr);
        table.appendChild(thead);

        const tbody = document.createElement("tbody");
        normalized.rows.forEach((row, rIdx) => {
            const trEl = document.createElement("tr");
            const rh = document.createElement("th");
            rh.className = "rowHead";
            rh.textContent = String(rIdx + 1);
            trEl.appendChild(rh);

            for (let c = 0; c < normalized.cols; c++) {
                const td = document.createElement("td");
                // Security: cell values are rendered as text, not HTML.
                td.textContent = row[c] == null ? "" : String(row[c]);
                trEl.appendChild(td);
            }

            tbody.appendChild(trEl);
        });

        table.appendChild(tbody);
        bodyEl.appendChild(table);

        const notes = [`${normalized.rows.length} × ${normalized.cols}`];
        if (normalized.truncatedRows || normalized.truncatedCols) {
            notes.push(tr("external.spreadsheet.truncated", null, "large sheet truncated for preview"));
        }
        setInfo(notes.join(" · "));
    }

    function openPreview(item) {
        if (!canOpenFor(item)) return;

        ensureUi();

        const rel = normalizeRelPath(item.rel || item.path || item.name || "");
        const ext = fileExtLower(item.name || rel);
        const url = downloadUrl(rel);
        const seq = ++openSeq;

        if (titleEl) titleEl.textContent = tr("external.spreadsheet.title", null, "Spreadsheet preview");
        if (pathEl) pathEl.textContent = "/" + rel;
        if (tabsEl) tabsEl.innerHTML = "";
        if (bodyEl) bodyEl.innerHTML = "";
        if (downloadBtn) downloadBtn.onclick = () => { window.location.href = url; };

        root.classList.add("show");
        root.setAttribute("aria-hidden", "false");
        document.body.classList.add("externalSpreadsheetPreviewOpen");
        setInfo(tr("common.loading", null, "Loading…"));

        readWorkbookRows(url, ext).then((sheets) => {
            if (seq !== openSeq) return;
            if (!sheets.length) {
                setInfo(tr("external.spreadsheet.empty_workbook", null, "No sheets found."));
                return;
            }
            renderSheet(sheets, 0);
        }).catch((e) => {
            if (seq !== openSeq) return;
            const msg = String(e && e.message ? e.message : e);
            bodyEl.innerHTML = "";
            const err = document.createElement("div");
            err.className = "externalSpreadsheetPreviewError";
            err.textContent = msg;
            bodyEl.appendChild(err);
            setInfo(tr("external.spreadsheet.failed", { error: msg }, `Spreadsheet preview failed: ${msg}`));
        });
    }

    function closePreview() {
        openSeq++;
        if (!root) return;
        root.classList.remove("show");
        root.setAttribute("aria-hidden", "true");
        document.body.classList.remove("externalSpreadsheetPreviewOpen");
    }

    window.PQNAS_EXTERNAL_SPREADSHEET_PREVIEW = {
        open: openPreview,
        canOpenFor,
        isSpreadsheetName
    };
})();
