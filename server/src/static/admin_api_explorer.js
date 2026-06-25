(() => {
    "use strict";

    const state = {
        routes: [],
        filtered: [],
        selectedId: ""
    };

    const $ = (id) => document.getElementById(id);

    function text(v) {
        if (v === null || v === undefined) return "";
        return String(v);
    }

    function setStatus(msg) {
        const el = $("statusText");
        if (el) el.textContent = msg;
    }

    function routeSearchText(r) {
        return [
            r.id,
            r.category,
            r.risk,
            r.method,
            r.path,
            r.title,
            r.purpose,
            r.auth,
            r.source,
            ...(Array.isArray(r.tags) ? r.tags : [])
        ].map(text).join(" ").toLowerCase();
    }

    function uniqueCategories(routes) {
        return [...new Set(routes.map((r) => r.category).filter(Boolean))].sort();
    }

    function fillCategoryFilter() {
        const sel = $("categoryFilter");
        if (!sel) return;

        const current = sel.value;
        sel.innerHTML = "";

        const all = document.createElement("option");
        all.value = "";
        all.textContent = "All categories";
        sel.appendChild(all);

        for (const cat of uniqueCategories(state.routes)) {
            const opt = document.createElement("option");
            opt.value = cat;
            opt.textContent = cat;
            sel.appendChild(opt);
        }

        sel.value = current;
    }

    function applyFilters() {
        const q = text($("q")?.value).trim().toLowerCase();
        const cat = text($("categoryFilter")?.value);
        const risk = text($("riskFilter")?.value);

        state.filtered = state.routes.filter((r) => {
            if (cat && r.category !== cat) return false;
            if (risk && r.risk !== risk) return false;
            if (q && !routeSearchText(r).includes(q)) return false;
            return true;
        });

        if (!state.filtered.some((r) => r.id === state.selectedId)) {
            state.selectedId = state.filtered.length ? state.filtered[0].id : "";
        }

        renderList();
        renderDetail();
    }

    function badge(label, extraClass) {
        const span = document.createElement("span");
        span.className = extraClass ? `badge ${extraClass}` : "badge";
        span.textContent = label;
        return span;
    }

    function renderList() {
        const list = $("routeList");
        if (!list) return;

        list.innerHTML = "";

        if (!state.filtered.length) {
            const empty = document.createElement("div");
            empty.className = "empty";
            empty.textContent = "No API routes matched the current filters.";
            list.appendChild(empty);
            setStatus("0 routes");
            return;
        }

        for (const r of state.filtered) {
            const btn = document.createElement("button");
            btn.type = "button";
            btn.className = "routeCard";
            btn.setAttribute("aria-selected", r.id === state.selectedId ? "true" : "false");

            const top = document.createElement("div");
            top.className = "routeTop";

            const method = document.createElement("span");
            method.className = "method";
            method.textContent = r.method;

            const path = document.createElement("span");
            path.className = "path";
            path.textContent = r.path;

            top.appendChild(method);
            top.appendChild(path);

            const title = document.createElement("div");
            title.className = "title";
            title.textContent = r.title || r.id;

            const meta = document.createElement("div");
            meta.className = "metaRow";
            meta.appendChild(badge(r.category || "Other"));
            meta.appendChild(badge(r.risk || "read", r.risk || ""));

            btn.appendChild(top);
            btn.appendChild(title);
            btn.appendChild(meta);

            btn.addEventListener("click", () => {
                state.selectedId = r.id;
                renderList();
                renderDetail();
            });

            list.appendChild(btn);
        }

        setStatus(`${state.filtered.length} route${state.filtered.length === 1 ? "" : "s"}`);
    }

    function addKv(parent, key, value) {
        const k = document.createElement("div");
        k.className = "kvKey";
        k.textContent = key;

        const v = document.createElement("div");
        v.textContent = text(value);

        parent.appendChild(k);
        parent.appendChild(v);
    }

    function section(title) {
        const box = document.createElement("section");
        box.className = "section";

        const h = document.createElement("h3");
        h.textContent = title;
        box.appendChild(h);

        return box;
    }

    function renderParams(params) {
        const box = section("Parameters");

        if (!Array.isArray(params) || params.length === 0) {
            const p = document.createElement("div");
            p.className = "hint";
            p.textContent = "No query/path parameters documented for this route.";
            box.appendChild(p);
            return box;
        }

        const kv = document.createElement("div");
        kv.className = "kv";

        for (const p of params) {
            addKv(kv, `${p.name} (${p.in})`, `${p.required === "yes" ? "required" : "optional"} — ${p.description || ""}`);
        }

        box.appendChild(kv);
        return box;
    }

    function renderJsonSection(title, value) {
        const box = section(title);
        const pre = document.createElement("pre");
        pre.textContent = JSON.stringify(value || {}, null, 2);
        box.appendChild(pre);
        return box;
    }

    function renderCurl(r) {
        const box = section("Curl template");

        const row = document.createElement("div");
        row.className = "copyRow";

        const hint = document.createElement("div");
        hint.className = "hint";
        hint.textContent = "Uses placeholders: $BASE, $COOKIE and optional $UPLOAD_ID.";

        const btn = document.createElement("button");
        btn.type = "button";
        btn.className = "pq-btn secondary";
        btn.textContent = "Copy";

        row.appendChild(hint);
        row.appendChild(btn);

        const pre = document.createElement("pre");
        pre.textContent = r.curl || "";

        btn.addEventListener("click", async () => {
            try {
                await navigator.clipboard.writeText(r.curl || "");
                btn.textContent = "Copied";
                setTimeout(() => { btn.textContent = "Copy"; }, 1200);
            } catch {
                btn.textContent = "Copy failed";
                setTimeout(() => { btn.textContent = "Copy"; }, 1200);
            }
        });

        box.appendChild(row);
        box.appendChild(pre);
        return box;
    }

    function renderDetail() {
        const detail = $("detail");
        const hint = $("detailHint");
        if (!detail) return;

        detail.innerHTML = "";

        const r = state.routes.find((item) => item.id === state.selectedId);

        if (!r) {
            if (hint) hint.textContent = "Select a route from the list.";
            const empty = document.createElement("div");
            empty.className = "empty";
            empty.textContent = "No route selected.";
            detail.appendChild(empty);
            return;
        }

        if (hint) hint.textContent = `${r.method} ${r.path}`;

        const head = document.createElement("div");
        head.className = "detailTitle";

        const left = document.createElement("div");
        const h2 = document.createElement("h2");
        h2.textContent = r.title || r.id;

        const sub = document.createElement("div");
        sub.className = "hint mono";
        sub.textContent = `${r.method} ${r.path}`;

        left.appendChild(h2);
        left.appendChild(sub);

        const risk = badge(r.risk || "read", r.risk || "");

        head.appendChild(left);
        head.appendChild(risk);
        detail.appendChild(head);

        const overview = section("Overview");
        const kv = document.createElement("div");
        kv.className = "kv";
        addKv(kv, "Purpose", r.purpose || "");
        addKv(kv, "Auth", r.auth || "");
        addKv(kv, "Category", r.category || "");
        addKv(kv, "Source", r.source || "");
        overview.appendChild(kv);
        detail.appendChild(overview);

        detail.appendChild(renderParams(r.params));
        detail.appendChild(renderJsonSection("Body", r.body));
        detail.appendChild(renderJsonSection("Responses", r.responses));

        const tags = section("Tags");
        const tagList = document.createElement("div");
        tagList.className = "tagList";
        for (const tag of Array.isArray(r.tags) ? r.tags : []) {
            tagList.appendChild(badge(tag));
        }
        if (!tagList.children.length) {
            const p = document.createElement("div");
            p.className = "hint";
            p.textContent = "No tags.";
            tags.appendChild(p);
        } else {
            tags.appendChild(tagList);
        }
        detail.appendChild(tags);

        detail.appendChild(renderCurl(r));
    }

    async function loadRoutes() {
        setStatus("Loading…");

        const res = await fetch("/api/v4/admin/api-explorer/routes", {
            cache: "no-store",
            credentials: "include"
        });

        const data = await res.json().catch(() => null);
        if (!res.ok || !data || data.ok !== true || !Array.isArray(data.routes)) {
            throw new Error((data && data.message) || "Failed to load API routes");
        }

        state.routes = data.routes.slice().sort((a, b) => {
            const ca = text(a.category).localeCompare(text(b.category));
            if (ca !== 0) return ca;
            return text(a.path).localeCompare(text(b.path));
        });

        fillCategoryFilter();
        applyFilters();
    }

    function wire() {
        $("q")?.addEventListener("input", applyFilters);
        $("categoryFilter")?.addEventListener("change", applyFilters);
        $("riskFilter")?.addEventListener("change", applyFilters);
        $("btnRefresh")?.addEventListener("click", () => {
            loadRoutes().catch((err) => {
                setStatus("Load failed");
                const detail = $("detail");
                if (detail) {
                    detail.innerHTML = "";
                    const box = document.createElement("div");
                    box.className = "empty";
                    box.textContent = err.message || "Failed to load API routes";
                    detail.appendChild(box);
                }
            });
        });
    }

    document.addEventListener("DOMContentLoaded", () => {
        wire();
        loadRoutes().catch((err) => {
            setStatus("Load failed");
            const detail = $("detail");
            if (detail) {
                detail.innerHTML = "";
                const box = document.createElement("div");
                box.className = "empty";
                box.textContent = err.message || "Failed to load API routes";
                detail.appendChild(box);
            }
        });
    });
})();
