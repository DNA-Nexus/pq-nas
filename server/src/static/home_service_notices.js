(() => {
    "use strict";

    const STYLE_ID = "pqnasServiceNoticesStyle";
    const WIDGET_ID = "serviceNoticesWidget";

    const labels = {
        en: {
            title: "Service Notices",
            empty: "No active notices.",
            open: "Open",
            close: "Close",
            updated: "Updated"
        },
        fi: {
            title: "Tiedotteet",
            empty: "Ei aktiivisia tiedotteita.",
            open: "Avaa",
            close: "Sulje",
            updated: "Päivitetty"
        },
        sv: {
            title: "Servicemeddelanden",
            empty: "Inga aktiva meddelanden.",
            open: "Öppna",
            close: "Stäng",
            updated: "Uppdaterad"
        },
        uk: {
            title: "Сервісні повідомлення",
            empty: "Немає активних повідомлень.",
            open: "Відкрити",
            close: "Закрити",
            updated: "Оновлено"
        },
        zh: {
            title: "服务通知",
            empty: "没有活动通知。",
            open: "打开",
            close: "关闭",
            updated: "已更新"
        },
        de: {
            title: "Servicehinweise",
            empty: "Keine aktiven Hinweise.",
            open: "Öffnen",
            close: "Schließen",
            updated: "Aktualisiert"
        },
        et: {
            title: "Teenuse teated",
            empty: "Aktiivseid teateid pole.",
            open: "Ava",
            close: "Sulge",
            updated: "Uuendatud"
        },
        pl: {
            title: "Komunikaty serwisowe",
            empty: "Brak aktywnych komunikatów.",
            open: "Otwórz",
            close: "Zamknij",
            updated: "Zaktualizowano"
        },
        es: {
            title: "Avisos de servicio",
            empty: "No hay avisos activos.",
            open: "Abrir",
            close: "Cerrar",
            updated: "Actualizado"
        },
        fr: {
            title: "Avis de service",
            empty: "Aucun avis actif.",
            open: "Ouvrir",
            close: "Fermer",
            updated: "Mis à jour"
        },
        it: {
            title: "Avvisi di servizio",
            empty: "Nessun avviso attivo.",
            open: "Apri",
            close: "Chiudi",
            updated: "Aggiornato"
        },
        tr: {
            title: "Servis Bildirimleri",
            empty: "Etkin bildirim yok.",
            open: "Aç",
            close: "Kapat",
            updated: "Güncellendi"
        }
    };

    function lang() {
        try {
            const api = window.PQNAS_I18N;
            const raw = api && typeof api.getLanguage === "function" ? api.getLanguage() : "";
            const value = String(raw || "").toLowerCase();

            if (value && labels[value]) return value;

            const short = value.split("-")[0].split("_")[0];
            if (short && labels[short]) return short;
        } catch (_) {}

        return "en";
    }

    function t(key) {
        const table = labels[lang()] || labels.en;
        return table[key] || labels.en[key] || key;
    }

    function ensureStyle() {
        if (document.getElementById(STYLE_ID)) return;

        const style = document.createElement("style");
        style.id = STYLE_ID;
        style.textContent = `
.serviceNoticesWidget{
    position:absolute;
    right:18px;
    bottom:18px;
    z-index:70;
    width:min(360px, calc(100% - 36px));
    border:1px solid rgba(var(--fg-rgb),0.16);
    background:rgba(var(--bg-rgb),0.72);
    color:var(--fg);
    border-radius:18px;
    box-shadow:var(--shadow);
    backdrop-filter:blur(14px);
    -webkit-backdrop-filter:blur(14px);
    overflow:hidden;
    font-family:var(--sans);
}
.serviceNoticesHead{
    display:flex;
    align-items:center;
    justify-content:space-between;
    gap:10px;
    padding:11px 13px;
    border-bottom:1px solid rgba(var(--fg-rgb),0.12);
}
.serviceNoticesTitle{
    font-weight:950;
    letter-spacing:.2px;
}
.serviceNoticesCount{
    border:1px solid rgba(var(--fg-rgb),0.18);
    border-radius:999px;
    padding:2px 8px;
    color:var(--fg-dim);
    font-size:12px;
    font-weight:850;
}
.serviceNoticesList{
    display:grid;
    gap:0;
}
.serviceNoticeButton{
    width:100%;
    text-align:left;
    border:0;
    border-bottom:1px solid rgba(var(--fg-rgb),0.10);
    background:transparent;
    color:var(--fg);
    padding:11px 13px;
    cursor:pointer;
    font:inherit;
}
.serviceNoticeButton:last-child{
    border-bottom:0;
}
.serviceNoticeButton:hover{
    background:rgba(var(--fg-rgb),0.06);
}
.serviceNoticeButtonTitle{
    font-weight:900;
    overflow:hidden;
    text-overflow:ellipsis;
    white-space:nowrap;
}
.serviceNoticeMetaRow{
    margin-top:4px;
    display:flex;
    align-items:center;
    gap:6px;
    min-width:0;
}
.serviceNoticeButtonMeta{
    font-size:11px;
    color:var(--fg-dim);
    overflow:hidden;
    text-overflow:ellipsis;
    white-space:nowrap;
    min-width:0;
}
.serviceNoticeSeverity{
    display:inline-flex;
    align-items:center;
    border:1px solid rgba(var(--fg-rgb),0.18);
    background:rgba(var(--fg-rgb),0.06);
    border-radius:999px;
    padding:2px 8px;
    font-size:10px;
    font-weight:900;
    letter-spacing:.2px;
    white-space:nowrap;
    flex:0 0 auto;
}
.serviceNoticeSeverity.info{
    border-color:rgba(var(--fg-rgb),0.18);
    background:rgba(var(--fg-rgb),0.06);
    color:var(--fg-dim);
}
.serviceNoticeSeverity.important{
    border-color:rgba(var(--accent-rgb, var(--fg-rgb)),0.34);
    background:rgba(var(--accent-rgb, var(--fg-rgb)),0.12);
    color:rgba(var(--accent-rgb, var(--fg-rgb)),0.98);
}
.serviceNoticeSeverity.critical{
    border-color:rgba(var(--danger-rgb, var(--accent-rgb, var(--fg-rgb))),0.58);
    background:rgba(var(--danger-rgb, var(--accent-rgb, var(--fg-rgb))),0.18);
    color:rgba(var(--danger-rgb, var(--accent-rgb, var(--fg-rgb))),1);
    font-weight:950;
    box-shadow:0 0 0 1px rgba(var(--danger-rgb, var(--accent-rgb, var(--fg-rgb))),0.18);
}
.serviceNoticeButton.hasCritical{
    border-left:4px solid rgba(var(--danger-rgb, var(--accent-rgb, var(--fg-rgb))),0.82);
}
.serviceNoticeButton.hasImportant{
    border-left:4px solid rgba(var(--accent-rgb, var(--fg-rgb)),0.62);
}
.serviceNoticeModalCard.hasCritical{
    border-left:5px solid rgba(var(--danger-rgb, var(--accent-rgb, var(--fg-rgb))),0.82);
}
.serviceNoticeModalCard.hasImportant{
    border-left:5px solid rgba(var(--accent-rgb, var(--fg-rgb)),0.62);
}
.serviceNoticeModalBackdrop{
    position:fixed;
    inset:0;
    z-index:100000;
    display:flex;
    align-items:center;
    justify-content:center;
    padding:18px;
    background:rgba(var(--bg-rgb),0.66);
    backdrop-filter:blur(8px);
    -webkit-backdrop-filter:blur(8px);
}
.serviceNoticeModalCard{
    width:min(680px, calc(100vw - 36px));
    max-height:calc(100vh - 36px);
    overflow:auto;
    border:1px solid rgba(var(--fg-rgb),0.18);
    border-radius:18px;
    background:var(--panel);
    color:var(--fg);
    box-shadow:var(--shadow);
}
.serviceNoticeModalHead{
    display:flex;
    align-items:flex-start;
    justify-content:space-between;
    gap:12px;
    padding:14px 16px;
    border-bottom:1px solid rgba(var(--fg-rgb),0.12);
}
.serviceNoticeModalTitle{
    font-weight:950;
    font-size:18px;
    line-height:1.25;
    overflow-wrap:anywhere;
}
.serviceNoticeModalMeta{
    margin-top:5px;
    color:var(--fg-dim);
    font-size:12px;
}
.serviceNoticeModalBody{
    padding:16px;
    color:var(--fg);
    line-height:1.5;
    white-space:pre-wrap;
    overflow-wrap:anywhere;
}
.serviceNoticeClose{
    border:1px solid rgba(var(--fg-rgb),0.18);
    background:rgba(var(--fg-rgb),0.06);
    color:var(--fg);
    border-radius:12px;
    padding:8px 10px;
    cursor:pointer;
    font:inherit;
    font-weight:900;
}
.serviceNoticeClose:hover{
    background:rgba(var(--fg-rgb),0.10);
}
@media (max-width: 720px){
    .serviceNoticesWidget{
        right:12px;
        bottom:12px;
        width:calc(100% - 24px);
    }
}
`;
        document.head.appendChild(style);
    }

    function fmtEpoch(epoch) {
        const n = Number(epoch || 0);
        if (!Number.isFinite(n) || n <= 0) return "";

        try {
            return new Date(n * 1000).toLocaleString(lang() === "fi" ? "fi-FI" : undefined);
        } catch (_) {
            return String(n);
        }
    }

    function severityClass(notice) {
        const v = String((notice && notice.severity) || "info").toLowerCase();
        if (v === "critical") return "critical";
        if (v === "important") return "important";
        return "info";
    }

    function severityText(notice) {
        return String((notice && notice.severity) || "info").toUpperCase();
    }

    function severityLabel(notice) {
        const parts = [];
        if (notice.kind) parts.push(notice.kind);
        if (notice.updated_at) parts.push(`${t("updated")}: ${fmtEpoch(notice.updated_at)}`);
        return parts.join(" · ");
    }

    function makeSeverityBadge(notice) {
        const badge = document.createElement("span");
        badge.className = `serviceNoticeSeverity ${severityClass(notice)}`;
        badge.textContent = severityText(notice);
        return badge;
    }

    function openNoticeModal(notice) {
        const backdrop = document.createElement("div");
        backdrop.className = "serviceNoticeModalBackdrop";
        backdrop.setAttribute("role", "dialog");
        backdrop.setAttribute("aria-modal", "true");

        const card = document.createElement("div");
        card.className = "serviceNoticeModalCard";
        if (severityClass(notice) === "critical") card.classList.add("hasCritical");
        if (severityClass(notice) === "important") card.classList.add("hasImportant");

        const head = document.createElement("div");
        head.className = "serviceNoticeModalHead";

        const titleBlock = document.createElement("div");

        const title = document.createElement("div");
        title.className = "serviceNoticeModalTitle";
        title.textContent = notice.title || t("title");

        const metaRow = document.createElement("div");
        metaRow.className = "serviceNoticeMetaRow";

        const meta = document.createElement("div");
        meta.className = "serviceNoticeModalMeta";
        meta.textContent = severityLabel(notice);

        metaRow.appendChild(makeSeverityBadge(notice));
        if (meta.textContent) metaRow.appendChild(meta);

        titleBlock.appendChild(title);
        titleBlock.appendChild(metaRow);

        const close = document.createElement("button");
        close.type = "button";
        close.className = "serviceNoticeClose";
        close.textContent = t("close");

        const body = document.createElement("div");
        body.className = "serviceNoticeModalBody";
        body.textContent = notice.body || "";

        head.appendChild(titleBlock);
        head.appendChild(close);
        card.appendChild(head);
        card.appendChild(body);
        backdrop.appendChild(card);

        function finish() {
            document.removeEventListener("keydown", onKey, true);
            backdrop.remove();
        }

        function onKey(ev) {
            if (ev.key === "Escape") {
                ev.preventDefault();
                finish();
            }
        }

        close.addEventListener("click", finish);
        backdrop.addEventListener("click", (ev) => {
            if (ev.target === backdrop) finish();
        });

        document.addEventListener("keydown", onKey, true);
        document.body.appendChild(backdrop);
        close.focus();
    }

    function render(notices) {
        ensureStyle();

        const surface = document.getElementById("desktopSurface");
        if (!surface) return;

        let widget = document.getElementById(WIDGET_ID);
        if (!Array.isArray(notices) || notices.length === 0) {
            if (widget) widget.remove();
            return;
        }

        if (!widget) {
            widget = document.createElement("section");
            widget.id = WIDGET_ID;
            widget.className = "serviceNoticesWidget";
            widget.setAttribute("aria-label", t("title"));
            surface.appendChild(widget);
        }

        widget.textContent = "";

        const head = document.createElement("div");
        head.className = "serviceNoticesHead";

        const title = document.createElement("div");
        title.className = "serviceNoticesTitle";
        title.textContent = t("title");

        const count = document.createElement("div");
        count.className = "serviceNoticesCount";
        count.textContent = String(notices.length);

        head.appendChild(title);
        head.appendChild(count);

        const list = document.createElement("div");
        list.className = "serviceNoticesList";

        for (const notice of notices) {
            const btn = document.createElement("button");
            btn.type = "button";
            btn.className = "serviceNoticeButton";
            if (severityClass(notice) === "critical") btn.classList.add("hasCritical");
            if (severityClass(notice) === "important") btn.classList.add("hasImportant");
            btn.title = t("open");

            const itemTitle = document.createElement("div");
            itemTitle.className = "serviceNoticeButtonTitle";
            itemTitle.textContent = notice.title || t("title");

            const metaRow = document.createElement("div");
            metaRow.className = "serviceNoticeMetaRow";

            const meta = document.createElement("div");
            meta.className = "serviceNoticeButtonMeta";
            meta.textContent = severityLabel(notice);

            metaRow.appendChild(makeSeverityBadge(notice));
            if (meta.textContent) metaRow.appendChild(meta);

            btn.appendChild(itemTitle);
            btn.appendChild(metaRow);

            btn.addEventListener("click", () => openNoticeModal(notice));
            list.appendChild(btn);
        }

        widget.appendChild(head);
        widget.appendChild(list);
    }

    async function refresh() {
        try {
            const r = await fetch("/api/v4/service-notices/active", {
                credentials: "include",
                cache: "no-store",
                headers: { "Accept": "application/json" }
            });

            const j = await r.json().catch(() => null);
            if (!r.ok || !j || j.ok === false) {
                render([]);
                return;
            }

            render(Array.isArray(j.notices) ? j.notices : []);
        } catch (_) {
            render([]);
        }
    }

    function start() {
        refresh();
        window.setInterval(refresh, 60 * 1000);
    }

    if (document.readyState === "loading") {
        document.addEventListener("DOMContentLoaded", start, { once: true });
    } else {
        start();
    }

    window.addEventListener("pqnas-language-changed", refresh);
})();
