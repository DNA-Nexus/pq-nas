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
            titleRequired: "Title is required.",
            editButton: "Edit",
            deleteButton: "Delete"
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
            titleRequired: "Otsikko on pakollinen.",
            editButton: "Muokkaa",
            deleteButton: "Poista"
        },
        sv: {
            pageTitle: "Servicemeddelanden",
            pageLead: "Publicera operatörsmeddelanden, underhållsnotiser och tjänsteuppdateringar på kundernas startsida.",
            newNotice: "Nytt meddelande",
            editNotice: "Redigera meddelande",
            notices: "Meddelanden",
            refresh: "Uppdatera",
            save: "Spara",
            fresh: "Ny",
            saved: "Sparat.",
            deleted: "Raderat.",
            loadFailed: "Det gick inte att läsa meddelanden.",
            saveFailed: "Det gick inte att spara meddelandet.",
            deleteFailed: "Det gick inte att radera meddelandet.",
            empty: "Inga meddelanden ännu.",
            confirmDelete: "Radera detta meddelande?",
            enabled: "aktiv",
            disabled: "inaktiv",
            pinned: "fäst",
            activeWindow: "giltighet",
            titleRequired: "Rubrik krävs.",
            editButton: "Redigera",
            deleteButton: "Radera"
        },
        uk: {
            pageTitle: "Сервісні повідомлення",
            pageLead: "Публікуйте повідомлення оператора, технічні роботи та оновлення сервісу на головному екрані клієнта.",
            newNotice: "Нове повідомлення",
            editNotice: "Редагувати повідомлення",
            notices: "Повідомлення",
            refresh: "Оновити",
            save: "Зберегти",
            fresh: "Нове",
            saved: "Збережено.",
            deleted: "Видалено.",
            loadFailed: "Не вдалося завантажити повідомлення.",
            saveFailed: "Не вдалося зберегти повідомлення.",
            deleteFailed: "Не вдалося видалити повідомлення.",
            empty: "Повідомлень ще немає.",
            confirmDelete: "Видалити це повідомлення?",
            enabled: "увімкнено",
            disabled: "вимкнено",
            pinned: "закріплено",
            activeWindow: "період",
            titleRequired: "Потрібен заголовок.",
            editButton: "Редагувати",
            deleteButton: "Видалити"
        },
        zh: {
            pageTitle: "服务通知",
            pageLead: "将运营商消息、维护通知和服务更新发布到客户主页。",
            newNotice: "新通知",
            editNotice: "编辑通知",
            notices: "通知",
            refresh: "刷新",
            save: "保存",
            fresh: "新建",
            saved: "已保存。",
            deleted: "已删除。",
            loadFailed: "无法加载通知。",
            saveFailed: "无法保存通知。",
            deleteFailed: "无法删除通知。",
            empty: "暂无通知。",
            confirmDelete: "删除此通知？",
            enabled: "已启用",
            disabled: "已禁用",
            pinned: "已置顶",
            activeWindow: "有效期",
            titleRequired: "标题为必填项。",
            editButton: "编辑",
            deleteButton: "删除"
        },
        de: {
            pageTitle: "Servicehinweise",
            pageLead: "Veröffentliche Betreibermeldungen, Wartungshinweise und Service-Updates auf dem Startbildschirm der Kunden.",
            newNotice: "Neuer Hinweis",
            editNotice: "Hinweis bearbeiten",
            notices: "Hinweise",
            refresh: "Aktualisieren",
            save: "Speichern",
            fresh: "Neu",
            saved: "Gespeichert.",
            deleted: "Gelöscht.",
            loadFailed: "Hinweise konnten nicht geladen werden.",
            saveFailed: "Hinweis konnte nicht gespeichert werden.",
            deleteFailed: "Hinweis konnte nicht gelöscht werden.",
            empty: "Noch keine Hinweise.",
            confirmDelete: "Diesen Hinweis löschen?",
            enabled: "aktiv",
            disabled: "inaktiv",
            pinned: "angeheftet",
            activeWindow: "Zeitraum",
            titleRequired: "Titel ist erforderlich.",
            editButton: "Bearbeiten",
            deleteButton: "Löschen"
        },
        et: {
            pageTitle: "Teenuse teated",
            pageLead: "Avalda operaatori sõnumeid, hooldusteateid ja teenuseuuendusi kliendi avalehel.",
            newNotice: "Uus teade",
            editNotice: "Muuda teadet",
            notices: "Teated",
            refresh: "Värskenda",
            save: "Salvesta",
            fresh: "Uus",
            saved: "Salvestatud.",
            deleted: "Kustutatud.",
            loadFailed: "Teadete laadimine ebaõnnestus.",
            saveFailed: "Teate salvestamine ebaõnnestus.",
            deleteFailed: "Teate kustutamine ebaõnnestus.",
            empty: "Teateid veel pole.",
            confirmDelete: "Kustutada see teade?",
            enabled: "lubatud",
            disabled: "keelatud",
            pinned: "kinnitatud",
            activeWindow: "kehtivus",
            titleRequired: "Pealkiri on kohustuslik.",
            editButton: "Muuda",
            deleteButton: "Kustuta"
        },
        pl: {
            pageTitle: "Komunikaty serwisowe",
            pageLead: "Publikuj komunikaty operatora, informacje o pracach serwisowych i aktualizacje usług na ekranie głównym klienta.",
            newNotice: "Nowy komunikat",
            editNotice: "Edytuj komunikat",
            notices: "Komunikaty",
            refresh: "Odśwież",
            save: "Zapisz",
            fresh: "Nowy",
            saved: "Zapisano.",
            deleted: "Usunięto.",
            loadFailed: "Nie udało się wczytać komunikatów.",
            saveFailed: "Nie udało się zapisać komunikatu.",
            deleteFailed: "Nie udało się usunąć komunikatu.",
            empty: "Brak komunikatów.",
            confirmDelete: "Usunąć ten komunikat?",
            enabled: "włączony",
            disabled: "wyłączony",
            pinned: "przypięty",
            activeWindow: "okres",
            titleRequired: "Tytuł jest wymagany.",
            editButton: "Edytuj",
            deleteButton: "Usuń"
        },
        es: {
            pageTitle: "Avisos de servicio",
            pageLead: "Publica mensajes del operador, avisos de mantenimiento y actualizaciones del servicio en la pantalla de inicio del cliente.",
            newNotice: "Nuevo aviso",
            editNotice: "Editar aviso",
            notices: "Avisos",
            refresh: "Actualizar",
            save: "Guardar",
            fresh: "Nuevo",
            saved: "Guardado.",
            deleted: "Eliminado.",
            loadFailed: "No se pudieron cargar los avisos.",
            saveFailed: "No se pudo guardar el aviso.",
            deleteFailed: "No se pudo eliminar el aviso.",
            empty: "No hay avisos todavía.",
            confirmDelete: "¿Eliminar este aviso?",
            enabled: "activo",
            disabled: "inactivo",
            pinned: "fijado",
            activeWindow: "vigencia",
            titleRequired: "El título es obligatorio.",
            editButton: "Editar",
            deleteButton: "Eliminar"
        },
        fr: {
            pageTitle: "Avis de service",
            pageLead: "Publiez les messages opérateur, avis de maintenance et mises à jour de service sur l’écran d’accueil du client.",
            newNotice: "Nouvel avis",
            editNotice: "Modifier l’avis",
            notices: "Avis",
            refresh: "Actualiser",
            save: "Enregistrer",
            fresh: "Nouveau",
            saved: "Enregistré.",
            deleted: "Supprimé.",
            loadFailed: "Impossible de charger les avis.",
            saveFailed: "Impossible d’enregistrer l’avis.",
            deleteFailed: "Impossible de supprimer l’avis.",
            empty: "Aucun avis pour le moment.",
            confirmDelete: "Supprimer cet avis ?",
            enabled: "activé",
            disabled: "désactivé",
            pinned: "épinglé",
            activeWindow: "période",
            titleRequired: "Le titre est obligatoire.",
            editButton: "Modifier",
            deleteButton: "Supprimer"
        },
        it: {
            pageTitle: "Avvisi di servizio",
            pageLead: "Pubblica messaggi dell’operatore, avvisi di manutenzione e aggiornamenti del servizio nella schermata iniziale del cliente.",
            newNotice: "Nuovo avviso",
            editNotice: "Modifica avviso",
            notices: "Avvisi",
            refresh: "Aggiorna",
            save: "Salva",
            fresh: "Nuovo",
            saved: "Salvato.",
            deleted: "Eliminato.",
            loadFailed: "Impossibile caricare gli avvisi.",
            saveFailed: "Impossibile salvare l’avviso.",
            deleteFailed: "Impossibile eliminare l’avviso.",
            empty: "Nessun avviso.",
            confirmDelete: "Eliminare questo avviso?",
            enabled: "attivo",
            disabled: "disattivato",
            pinned: "fissato",
            activeWindow: "periodo",
            titleRequired: "Il titolo è obbligatorio.",
            editButton: "Modifica",
            deleteButton: "Elimina"
        },
        tr: {
            pageTitle: "Servis Bildirimleri",
            pageLead: "Operatör mesajlarını, bakım bildirimlerini ve servis güncellemelerini müşterinin ana ekranında yayınlayın.",
            newNotice: "Yeni bildirim",
            editNotice: "Bildirimi düzenle",
            notices: "Bildirimler",
            refresh: "Yenile",
            save: "Kaydet",
            fresh: "Yeni",
            saved: "Kaydedildi.",
            deleted: "Silindi.",
            loadFailed: "Bildirimler yüklenemedi.",
            saveFailed: "Bildirim kaydedilemedi.",
            deleteFailed: "Bildirim silinemedi.",
            empty: "Henüz bildirim yok.",
            confirmDelete: "Bu bildirim silinsin mi?",
            enabled: "etkin",
            disabled: "devre dışı",
            pinned: "sabitlendi",
            activeWindow: "geçerlilik",
            titleRequired: "Başlık zorunludur.",
            editButton: "Düzenle",
            deleteButton: "Sil"
        }
    };

    function lang() {
        try {
            const api = window.PQNAS_I18N;
            const raw = api && typeof api.getLanguage === "function" ? api.getLanguage() : "";
            const value = String(raw || "").toLowerCase();

            if (value && i18n[value]) return value;

            const short = value.split("-")[0].split("_")[0];
            if (short && i18n[short]) return short;
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
        try {
            const api = window.PQNAS_I18N;
            if (api && typeof api.apply === "function") api.apply(document);
        } catch (_) {}

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
            edit.textContent = t("editButton");
            edit.addEventListener("click", () => fillForm(notice));

            const del = document.createElement("button");
            del.type = "button";
            del.className = "btn secondary";
            del.textContent = t("deleteButton");
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

    function boot() {
        applyText();
        loadNotices();
    }

    if (window.PQNAS_I18N && typeof window.PQNAS_I18N.ready === "function") {
        window.PQNAS_I18N.ready().then(boot).catch(boot);
    } else {
        boot();
    }
})();
