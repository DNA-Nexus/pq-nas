(() => {
    "use strict";

    const i18n = {
        en: {
            pageTitle: "Service Notices / Tiedotteet",
            pageLead: "Publish operator messages, maintenance notices and service updates to the customer home screen.",
            newNotice: "New notice",
            editNotice: "Edit notice",
            notices: "Notices",
            refresh: "Refresh",
            save: "Save",
            fresh: "New",
            saved: "Saved.",
            deleted: "Deleted.",
            loadFailed: "Failed to load notices.",
            saveFailed: "Failed to save notice.",
            deleteFailed: "Failed to delete notice.",
            empty: "No notices yet.",
            confirmDelete: "Delete this notice?",
            enabled: "enabled",
            disabled: "disabled",
            pinned: "pinned",
            activeWindow: "window",
            titleRequired: "Title is required."
        },
        fi: {
            pageTitle: "Tiedotteet / Service Notices",
            pageLead: "Julkaise operaattorin viestit, huoltotiedotteet ja palvelupäivitykset asiakkaiden kotiruudulle.",
            newNotice: "Uusi tiedote",
            editNotice: "Muokkaa tiedotetta",
            notices: "Tiedotteet",
            refresh: "Päivitä",
            save: "Tallenna",
            fresh: "Uusi",
            saved: "Tallennettu.",
            deleted: "Poistettu.",
            loadFailed: "Tiedotteiden lataus epäonnistui.",
            saveFailed: "Tiedotteen tallennus epäonnistui.",
            deleteFailed: "Tiedotteen poisto epäonnistui.",
            empty: "Ei tiedotteita vielä.",
            confirmDelete: "Poistetaanko tämä tiedote?",
            enabled: "käytössä",
            disabled: "pois käytöstä",
            pinned: "kiinnitetty",
            activeWindow: "voimassa",
            titleRequired: "Otsikko on pakollinen."
        }
    };

    function lang() {
        try {
            const api = window.PQNAS_I18N;
            const l = api && typeof api.getLanguage === "function" ? api.getLanguage() : "";
            if (String(l).toLowerCase().startsWith("fi")) return "fi";
        } catch (_) {}
        return "en";
    }

    function t(key) {
        const table = i18n[lang()] || i18n.en;
        return table[key] || i18n.en[key] || key;
    }

    const el = (id) => document.getElementById(id);

    const refs = {
        pageTitle: el("noticePageTitle"),
        pageLead: el("noticePageLead"),
        formTitle: el("noticeFormTitle"),
        form: el("noticeForm"),
        id: el("noticeId"),
        title: el("noticeTitle"),
        body: el("noticeBody"),
        kind: el("noticeKind"),
        severity: el("noticeSeverity"),
        starts: el("noticeStarts"),
        ends: el("noticeEnds"),
        enabled: el("noticeEnabled"),
        pinned: el("noticePinned"),
        saveBtn: el("noticeSaveBtn"),
        resetBtn: el("noticeResetBtn"),
        refreshBtn: el("noticeRefreshBtn"),
        status: el("noticeStatus"),
        listStatus: el("noticeListStatus"),
        listTitle: el("noticeListTitle"),
        list: el("noticeList")
    };

    let notices = [];

    function applyText() {
        if (refs.pageTitle) refs.pageTitle.textContent = t("pageTitle");
        if (refs.pageLead) refs.pageLead.textContent = t("pageLead");
        if (refs.formTitle && !refs.id.value) refs.formTitle.textContent = t("newNotice");
        if (refs.listTitle) refs.listTitle.textContent = t("notices");
        if (refs.refreshBtn) refs.refreshBtn.textContent = t("refresh");
        if (refs.saveBtn) refs.saveBtn.textContent = t("save");
        if (refs.resetBtn) refs.resetBtn.textContent = t("fresh");
    }

    function setStatus(text, target = refs.status) {
        if (target) target.textContent = text || "";
    }

    function epochToInput(epoch) {
        const n = Number(epoch || 0);
        if (!Number.isFinite(n) || n <= 0) return "";

        const d = new Date(n * 1000);
        const pad = (v) => String(v).padStart(2, "0");

        return [
            d.getFullYear(),
            "-",
            pad(d.getMonth() + 1),
            "-",
            pad(d.getDate()),
            "T",
            pad(d.getHours()),
            ":",
            pad(d.getMinutes())
        ].join("");
    }

    function inputToEpoch(value) {
        const v = String(value || "").trim();
        if (!v) return 0;

        const ms = new Date(v).getTime();
        if (!Number.isFinite(ms)) return 0;

        return Math.floor(ms / 1000);
    }

    function fmtEpoch(epoch) {
        const n = Number(epoch || 0);
        if (!Number.isFinite(n) || n <= 0) return "—";

        try {
            return new Date(n * 1000).toLocaleString(lang() === "fi" ? "fi-FI" : undefined);
        } catch (_) {
            return String(n);
        }
    }

    async function fetchJson(url, opts = {}) {
        const r = await fetch(url, Object.assign({
            credentials: "include",
            cache: "no-store",
            headers: { "Accept": "application/json" }
        }, opts));

        const j = await r.json().catch(() => null);
        if (!r.ok || !j || j.ok === false) {
            const msg = j && (j.message || j.error) ? (j.message || j.error) : `HTTP ${r.status}`;
            throw new Error(msg);
        }

        return j;
    }

    function resetForm() {
        refs.id.value = "";
        refs.title.value = "";
        refs.body.value = "";
        refs.kind.value = "notice";
        refs.severity.value = "info";
        refs.starts.value = "";
        refs.ends.value = "";
        refs.enabled.checked = true;
        refs.pinned.checked = false;
        refs.formTitle.textContent = t("newNotice");
        setStatus("");
        refs.title.focus();
    }

    function fillForm(notice) {
        refs.id.value = notice.id || "";
        refs.title.value = notice.title || "";
        refs.body.value = notice.body || "";
        refs.kind.value = notice.kind || "notice";
        refs.severity.value = notice.severity || "info";
        refs.starts.value = epochToInput(notice.starts_at);
        refs.ends.value = epochToInput(notice.ends_at);
        refs.enabled.checked = notice.enabled !== false;
        refs.pinned.checked = !!notice.pinned;
        refs.formTitle.textContent = t("editNotice");
        setStatus("");
        refs.title.focus();
    }

    function noticePayloadFromForm() {
        const title = String(refs.title.value || "").trim();
        if (!title) throw new Error(t("titleRequired"));

        return {
            id: String(refs.id.value || "").trim(),
            title,
            body: String(refs.body.value || "").trim(),
            kind: refs.kind.value || "notice",
            severity: refs.severity.value || "info",
            starts_at: inputToEpoch(refs.starts.value),
            ends_at: inputToEpoch(refs.ends.value),
            enabled: !!refs.enabled.checked,
            pinned: !!refs.pinned.checked
        };
    }

    function severityClass(value) {
        const v = String(value || "").toLowerCase();
        if (v === "critical") return "critical";
        if (v === "important") return "important";
        return "info";
    }

    function makeBadge(text, extraClass = "") {
        const span = document.createElement("span");
        span.className = `noticeBadge${extraClass ? " " + extraClass : ""}`;
        span.textContent = text;
        return span;
    }

    function renderList() {
        refs.list.textContent = "";

        if (!notices.length) {
            const empty = document.createElement("div");
            empty.className = "noticeEmpty";
            empty.textContent = t("empty");
            refs.list.appendChild(empty);
            return;
        }

        for (const notice of notices) {
            const item = document.createElement("article");
            item.className = "noticeItem";
            if (severityClass(notice.severity) === "critical") item.classList.add("hasCritical");
            if (severityClass(notice.severity) === "important") item.classList.add("hasImportant");

            const top = document.createElement("div");
            top.className = "noticeItemTop";

            const titleWrap = document.createElement("div");

            const title = document.createElement("div");
            title.className = "noticeItemTitle";
            title.textContent = notice.title || "(untitled)";
            titleWrap.appendChild(title);

            const meta = document.createElement("div");
            meta.className = "noticeMeta";
            meta.textContent = [
                notice.kind || "notice",
                notice.severity || "info",
                notice.enabled === false ? t("disabled") : t("enabled")
            ].join(" · ");
            titleWrap.appendChild(meta);

            const badges = document.createElement("div");
            badges.style.display = "flex";
            badges.style.gap = "6px";
            badges.style.flexWrap = "wrap";

            badges.appendChild(makeBadge((notice.severity || "info").toUpperCase(), severityClass(notice.severity)));
            if (notice.pinned) badges.appendChild(makeBadge(t("pinned")));
            badges.appendChild(makeBadge(`${t("activeWindow")}: ${fmtEpoch(notice.starts_at)} → ${fmtEpoch(notice.ends_at)}`));

            top.appendChild(titleWrap);
            top.appendChild(badges);

            const body = document.createElement("div");
            body.className = "noticeBodyPreview";
            body.textContent = notice.body || "";

            const actions = document.createElement("div");
            actions.className = "noticeItemActions";

            const edit = document.createElement("button");
            edit.type = "button";
            edit.className = "btn secondary";
            edit.textContent = lang() === "fi" ? "Muokkaa" : "Edit";
            edit.addEventListener("click", () => fillForm(notice));

            const del = document.createElement("button");
            del.type = "button";
            del.className = "btn secondary";
            del.textContent = lang() === "fi" ? "Poista" : "Delete";
            del.addEventListener("click", () => deleteNotice(notice.id));

            actions.appendChild(edit);
            actions.appendChild(del);

            item.appendChild(top);
            if (notice.body) item.appendChild(body);
            item.appendChild(actions);

            refs.list.appendChild(item);
        }
    }

    async function loadNotices() {
        setStatus("", refs.listStatus);

        try {
            const j = await fetchJson("/api/v4/admin/service-notices/list");
            notices = Array.isArray(j.notices) ? j.notices : [];
            renderList();
        } catch (e) {
            setStatus(`${t("loadFailed")} ${e.message || e}`, refs.listStatus);
        }
    }

    async function saveNotice(ev) {
        ev.preventDefault();

        let payload;
        try {
            payload = noticePayloadFromForm();
        } catch (e) {
            setStatus(e.message || String(e));
            return;
        }

        try {
            const j = await fetchJson("/api/v4/admin/service-notices/save", {
                method: "POST",
                headers: {
                    "Accept": "application/json",
                    "Content-Type": "application/json"
                },
                body: JSON.stringify(payload)
            });

            setStatus(t("saved"));
            if (j.notice && j.notice.id) refs.id.value = j.notice.id;
            await loadNotices();
        } catch (e) {
            setStatus(`${t("saveFailed")} ${e.message || e}`);
        }
    }

    async function deleteNotice(id) {
        if (!id) return;
        if (!window.confirm(t("confirmDelete"))) return;

        try {
            await fetchJson("/api/v4/admin/service-notices/delete", {
                method: "POST",
                headers: {
                    "Accept": "application/json",
                    "Content-Type": "application/json"
                },
                body: JSON.stringify({ id })
            });

            setStatus(t("deleted"));
            resetForm();
            await loadNotices();
        } catch (e) {
            setStatus(`${t("deleteFailed")} ${e.message || e}`, refs.listStatus);
        }
    }

    refs.form?.addEventListener("submit", saveNotice);
    refs.resetBtn?.addEventListener("click", resetForm);
    refs.refreshBtn?.addEventListener("click", loadNotices);

    window.addEventListener("pqnas-language-changed", () => {
        applyText();
        renderList();
    });

    applyText();
    loadNotices();
})();
