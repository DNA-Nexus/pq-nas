function tr(key, vars = null, fallback = "") {
    try {
        if (window.PQNAS_I18N && typeof window.PQNAS_I18N.t === "function") {
            return window.PQNAS_I18N.t(key, vars, fallback || key);
        }
    } catch (_) {}
    return fallback || key;
}

function applyStaticI18n() {
    try {
        if (window.PQNAS_I18N && typeof window.PQNAS_I18N.apply === "function") {
            window.PQNAS_I18N.apply(document);
        }
    } catch (_) {}
}

function statusLabel(status) {
    const s = String(status || "disabled").toLowerCase();
    if (s === "enabled") return tr("admin.approvals.status.enabled", null, "enabled");
    if (s === "disabled") return tr("admin.approvals.status.disabled", null, "disabled");
    if (s === "revoked") return tr("admin.approvals.status.revoked", null, "revoked");
    return tr("admin.approvals.status.unknown", null, s || "unknown");
}

function roleLabel(role) {
    const r = String(role || "").toLowerCase();
    if (r === "admin") return tr("admin.approvals.role.admin", null, "admin");
    if (r === "user") return tr("admin.approvals.role.user", null, "user");
    return String(role || "");
}

async function apiGet(path) {
    const r = await fetch(path, { credentials: "same-origin", headers: { "Accept": "application/json" }, cache: "no-store" });
    const j = await r.json().catch(() => ({}));
    if (!r.ok || !j.ok) throw new Error(j.message || j.error || ("HTTP " + r.status));
    return j;
}

async function apiPost(path, body) {
    const r = await fetch(path, {
        method: "POST",
        credentials: "same-origin",
        headers: { "Content-Type": "application/json", "Accept": "application/json" },
        body: JSON.stringify(body),
        cache: "no-store",
    });
    const j = await r.json().catch(() => ({}));
    if (!r.ok || !j.ok) throw new Error(j.message || j.error || ("HTTP " + r.status));
    return j;
}

function $(id) { return document.getElementById(id); }

function pill(status) {
    const cls = (status || "disabled");
    const variant = cls === "enabled" ? " ok" : cls === "revoked" ? " err" : "";
    return `<span class="pq-badge${variant}">${esc(statusLabel(cls))}</span>`;
}

function esc(s) {
    return (s || "").replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
}

let allUsers = [];
let authConfig = null;
let opaqueOnboarding = [];

function setMsg(text) {
    const el = $("msg");
    if (el) el.textContent = text || "";
}


function currentAuthMode() {
    return String((authConfig && authConfig.mode) || "qr").toLowerCase();
}

function onboardingStateLabel(state) {
    const s = String(state || "").toLowerCase();
    if (s === "waiting_password") return tr("admin.approvals.opaque.state.waiting_password", null, "Odottaa salasanan asetusta");
    if (s === "setup_link_created") return tr("admin.approvals.opaque.state.setup_link_created", null, "Setup-linkki luotu");
    if (s === "setup_link_expired") return tr("admin.approvals.opaque.state.setup_link_expired", null, "Setup-linkki vanhentunut");
    if (s === "setup_done") return tr("admin.approvals.opaque.state.setup_done", null, "Setup valmis");
    if (s === "revoked") return tr("admin.approvals.opaque.state.revoked", null, "Revoked / peruttu");
    if (s === "reset_required") return tr("admin.approvals.opaque.state.reset_required", null, "Reset vaaditaan");
    return s || tr("admin.approvals.opaque.state.unknown", null, "Tuntematon");
}

function onboardingBadge(state) {
    const s = String(state || "").toLowerCase();
    const variant =
        s === "setup_done" ? " ok" :
        s === "revoked" || s === "setup_link_expired" ? " err" :
        "";
    return `<span class="pq-badge${variant}">${esc(onboardingStateLabel(s))}</span>`;
}

function epochLabel(epoch) {
    const n = Number(epoch || 0);
    if (!Number.isFinite(n) || n <= 0) return "";
    try {
        return new Date(n * 1000).toLocaleString();
    } catch (_) {
        return String(n);
    }
}

async function showSetupLinkModal(row, tokenResponse) {
    const setupUrl = String(tokenResponse.setup_url || tokenResponse.setup_path || "");
    const ok = await openApprovalsConfirmModal({
        title: tr("admin.approvals.opaque.setup_link_created_title", null, "Setup-linkki luotu"),
        subtitle: row.login || "",
        rows: [
            { label: tr("admin.approvals.opaque.user", null, "Käyttäjä"), value: row.name || row.login || "" },
            { label: "Login", value: row.login || "", mono: true },
            { label: tr("admin.approvals.opaque.expires", null, "Vanhenee"), value: epochLabel(tokenResponse.expires_at) || String(tokenResponse.expires_at || "") },
            { label: tr("admin.approvals.opaque.setup_url", null, "Setup URL"), value: setupUrl, mono: true },
        ],
        note: tr("admin.approvals.opaque.copy_note", null, "Kopioi tämä linkki käyttäjälle. Linkki näytetään tässä muodossa vain kerran."),
        confirmText: tr("admin.approvals.opaque.copy_link", null, "Kopioi linkki"),
        cancelText: tr("admin.approvals.opaque.close", null, "Sulje"),
    });

    if (ok && setupUrl && navigator.clipboard && navigator.clipboard.writeText) {
        try {
            await navigator.clipboard.writeText(setupUrl);
            setMsg(tr("admin.approvals.opaque.copied", null, "Setup-linkki kopioitu leikepöydälle"));
        } catch (_) {
            setMsg(setupUrl);
        }
    } else if (setupUrl) {
        setMsg(setupUrl);
    }
}

async function createOpaqueSetupLink(row) {
    const j = await apiPost("/api/admin/auth/opaque/enrollment-token/create", {
        login: row.login,
        fingerprint: row.fingerprint,
        purpose: row.credential_exists ? "reset_password" : "new_user",
        enable_user_on_finish: true,
        expires_in_seconds: 86400
    });

    await showSetupLinkModal(row, j);
    await refresh();
}


async function forceOpaqueReset(row) {
    const ok = await openApprovalsConfirmModal({
        title: tr("admin.approvals.opaque.force_reset_title", null, "Pakota salasanan reset"),
        subtitle: row.login || "",
        rows: [
            { label: tr("admin.approvals.opaque.user", null, "Käyttäjä"), value: row.name || row.login || "" },
            { label: "Login", value: row.login || "", mono: true },
            { label: tr("admin.approvals.fingerprint", null, "Fingerprint"), value: row.fingerprint || "", mono: true },
        ],
        note: tr("admin.approvals.opaque.force_reset_note", null, "Vanha OPAQUE-salasana lakkaa toimimasta heti. Käyttäjä pääsee sisään vasta uuden reset-linkin suorittamisen jälkeen."),
        confirmText: tr("admin.approvals.opaque.force_reset_confirm", null, "Pakota reset"),
        cancelText: tr("admin.approvals.cancel", null, "Cancel"),
        danger: true,
    });
    if (!ok) return;

    setMsg(tr("admin.approvals.opaque.force_resetting", null, "Pakotetaan reset…"));

    await apiPost("/api/admin/auth/opaque/credential/disable", {
        login: row.login,
        fingerprint: row.fingerprint
    });

    const token = await apiPost("/api/admin/auth/opaque/enrollment-token/create", {
        login: row.login,
        fingerprint: row.fingerprint,
        purpose: "reset_password",
        enable_user_on_finish: true,
        expires_in_seconds: 86400
    });

    await showSetupLinkModal(row, token);
    await refresh();
}

function renderOpaqueApprovals() {
    const f = ($("filter")?.value || "").toLowerCase().trim();

    const rows = opaqueOnboarding.filter(row => {
        const state = String(row.onboarding_state || "").toLowerCase();

        // Approvals/onboarding view should focus on users needing action.
        // Completed OPAQUE users belong in the normal user list. Keep them
        // searchable with the filter for troubleshooting/history.
        if (!f && state === "setup_done") {
            return false;
        }

        const hay = [
            row.fingerprint,
            row.login,
            row.name,
            row.notes,
            row.role,
            row.user_status,
            row.onboarding_state,
            onboardingStateLabel(row.onboarding_state)
        ].join(" ").toLowerCase();

        return !f || hay.includes(f);
    });

    const tb = $("tbody");
    if (!tb) return;

    if (!rows.length) {
        tb.innerHTML = `<tr><td colspan="7" class="muted">${esc(tr("admin.approvals.opaque.no_rows", null, "Ei OPAQUE onboarding -rivejä."))}</td></tr>`;
        return;
    }

    tb.innerHTML = rows.map(row => {
        const canCreateLink = row.onboarding_state !== "revoked";
        const linkText = row.credential_exists ? tr("admin.approvals.opaque.create_reset_link", null, "Luo reset-linkki") : tr("admin.approvals.opaque.create_setup_link", null, "Luo setup-linkki");
        const tokenInfo =
            row.token_state === "active" ? `${tr("admin.approvals.opaque.active_link_expires", null, "Aktiivinen linkki, vanhenee:")} ${epochLabel(row.token_expires_at)}` :
            row.token_state === "used" ? `${tr("admin.approvals.opaque.link_used", null, "Linkki käytetty:")} ${epochLabel(row.token_used_at)}` :
            row.token_state === "expired" ? `${tr("admin.approvals.opaque.link_expired", null, "Linkki vanhentui:")} ${epochLabel(row.token_expires_at)}` :
            tr("admin.approvals.opaque.no_active_link", null, "Ei aktiivista linkkiä");

        const credInfo = row.credential_exists
            ? `${tr("admin.approvals.opaque.credential", null, "Credential")}: ${row.credential_enabled ? tr("admin.approvals.opaque.credential_enabled", null, "enabled") : tr("admin.approvals.opaque.credential_disabled", null, "disabled")} ${row.credential_updated_at || ""}`
            : tr("admin.approvals.opaque.credential_missing", null, "Credential puuttuu");

        return `<tr>
            <td class="mono">${esc(row.fingerprint || "")}</td>

            <td>
                <div><b>${esc(row.name || row.login || "")}</b></div>
                <div class="muted mono">${esc(row.login || "")}</div>
                <div class="muted" style="white-space:pre-wrap;">${esc(row.notes || "")}</div>
            </td>

            <td>${esc(roleLabel(row.role || ""))}</td>

            <td>
                ${onboardingBadge(row.onboarding_state)}
                <div class="muted" style="margin-top:6px;">User: ${esc(statusLabel(row.user_status || ""))}</div>
            </td>

            <td>
                <div class="mono">${esc(row.added_at || "")}</div>
                <div class="muted">${esc(tokenInfo)}</div>
            </td>

            <td>
                <div class="mono">${esc(row.last_seen || "")}</div>
                <div class="muted">${esc(credInfo)}</div>
            </td>

            <td class="row-actions">
                ${canCreateLink ? `<button class="pq-btn secondary" data-act="opaque-setup" data-fp="${esc(row.fingerprint)}" type="button">${esc(linkText)}</button>` : ""}
                ${row.credential_exists && row.credential_enabled && row.onboarding_state !== "revoked" ? `<button class="pq-btn danger" data-act="opaque-force-reset" data-fp="${esc(row.fingerprint)}" type="button">${esc(tr("admin.approvals.opaque.force_reset", null, "Pakota reset"))}</button>` : ""}
                <button class="pq-btn secondary" data-act="opaque-revoke" data-fp="${esc(row.fingerprint)}" type="button">${esc(tr("admin.approvals.opaque.revoke_cancel", null, "Peru / revoke"))}</button>
                <button class="pq-btn danger" data-act="opaque-delete" data-fp="${esc(row.fingerprint)}" type="button">${esc(tr("admin.approvals.delete", null, "Delete"))}</button>
            </td>
        </tr>`;
    }).join("");

    tb.querySelectorAll("button").forEach(b => {
        b.addEventListener("click", async () => {
            const fp = b.getAttribute("data-fp");
            const act = b.getAttribute("data-act");
            const row = opaqueOnboarding.find(x => x.fingerprint === fp);
            if (!fp || !act || !row) return;

            if (act === "opaque-setup") {
                try {
                    setMsg(tr("admin.approvals.opaque.creating_setup", null, "Luodaan OPAQUE setup-linkkiä…"));
                    await createOpaqueSetupLink(row);
                } catch (e) {
                    setMsg(tr("admin.approvals.opaque.error", { error: e.message }, "Virhe: " + e.message));
                }
                return;
            }

            if (act === "opaque-force-reset") {
                try {
                    await forceOpaqueReset(row);
                } catch (e) {
                    setMsg(tr("admin.approvals.opaque.error", { error: e.message }, "Virhe: " + e.message));
                }
                return;
            }

            if (act === "opaque-revoke") {
                const ok = await openApprovalsConfirmModal({
                    title: tr("admin.approvals.opaque.revoke_title", null, "Peru OPAQUE onboarding?"),
                    subtitle: row.login || "",
                    rows: [
                        { label: tr("admin.approvals.opaque.user", null, "Käyttäjä"), value: row.name || row.login || "" },
                        { label: "Login", value: row.login || "", mono: true },
                        { label: "Fingerprint", value: fp, mono: true },
                    ],
                    note: tr("admin.approvals.opaque.revoke_note", null, "Tämä asettaa käyttäjän revoked-tilaan. Olemassa oleva setup-linkki ei saa enää herättää käyttäjää takaisin."),
                    confirmText: tr("admin.approvals.opaque.revoke_cancel", null, "Peru / revoke"),
                    cancelText: tr("admin.approvals.cancel", null, "Cancel"),
                    danger: true,
                });
                if (!ok) return;

                try {
                    setMsg(tr("admin.approvals.opaque.revoking", null, "Perutaan…"));
                    await apiPost("/api/v4/admin/users/status", { fingerprint: fp, status: "revoked" });
                    await refresh();
                    setMsg(tr("admin.approvals.opaque.revoked_msg", null, "Peruttu / revoked"));
                } catch (e) {
                    setMsg(tr("admin.approvals.opaque.error", { error: e.message }, "Virhe: " + e.message));
                }
                return;
            }

            if (act === "opaque-delete") {
                const ok = await openApprovalsConfirmModal({
                    title: tr("admin.approvals.delete_title", null, "Delete user entry?"),
                    subtitle: row.login || "",
                    rows: [
                        { label: tr("admin.approvals.opaque.user", null, "Käyttäjä"), value: row.name || row.login || "" },
                        { label: "Login", value: row.login || "", mono: true },
                        { label: "Fingerprint", value: fp, mono: true },
                    ],
                    note: tr("admin.approvals.opaque.delete_note", null, "Poistaa users.json-rivin. Käytä vain testidatan siivoukseen."),
                    confirmText: tr("admin.approvals.delete", null, "Delete"),
                    cancelText: tr("admin.approvals.cancel", null, "Cancel"),
                    danger: true,
                });
                if (!ok) return;

                try {
                    setMsg(tr("admin.approvals.deleting", null, "Deleting…"));
                    await apiPost("/api/v4/admin/users/delete", { fingerprint: fp });
                    await refresh();
                    setMsg(tr("admin.approvals.delete_ok", null, "Delete OK"));
                } catch (e) {
                    setMsg(tr("admin.approvals.opaque.error", { error: e.message }, "Virhe: " + e.message));
                }
            }
        });
    });
}


function injectApprovalsConfirmCss() {
    if (document.getElementById("approvalsConfirmCss")) return;

    const style = document.createElement("style");
    style.id = "approvalsConfirmCss";
    style.textContent = `
.approvalsConfirmBackdrop{
    position:fixed;
    inset:0;
    z-index:100000;
    display:flex;
    align-items:center;
    justify-content:center;
    padding:18px;
    background:rgba(0,0,0,0.55);
    backdrop-filter:blur(6px);
    -webkit-backdrop-filter:blur(6px);
}

.approvalsConfirmCard{
    width:min(640px, calc(100vw - 24px));
    max-height:min(84vh, 900px);
    display:flex;
    flex-direction:column;
    overflow:hidden;
    border:1px solid var(--border2, rgba(120,120,120,0.45));
    border-radius:18px;
    background:linear-gradient(180deg, var(--panel2, #f8f8f8), var(--panel, #eeeeee));
    box-shadow:0 18px 70px rgba(0,0,0,0.42);
    color:var(--fg, #111);
}

.approvalsConfirmHead{
    padding:14px 16px;
    border-bottom:1px solid var(--border2, rgba(120,120,120,0.35));
    background:rgba(0,0,0,0.08);
}

.approvalsConfirmTitle{
    font-weight:950;
    letter-spacing:.2px;
    font-size:16px;
}

.approvalsConfirmSub{
    margin-top:4px;
    font-size:12px;
    color:var(--fg-dim, rgba(0,0,0,0.65));
}

.approvalsConfirmBody{
    padding:16px;
    display:grid;
    grid-template-columns:130px minmax(0, 1fr);
    gap:10px 14px;
    overflow:auto;
    min-height:0;
}

.approvalsConfirmKey{
    color:var(--fg-dim, rgba(0,0,0,0.68));
    font-weight:850;
}

.approvalsConfirmValue{
    color:var(--fg, #111);
    overflow-wrap:anywhere;
    white-space:pre-wrap;
}

.approvalsConfirmValue.mono{
    font-family:var(--mono, ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace);
    font-size:12px;
}

.approvalsConfirmNote{
    grid-column:1 / -1;
    padding:10px 12px;
    border:1px solid rgba(var(--fail-rgb, 180,40,40),0.35);
    border-radius:14px;
    background:rgba(var(--fail-rgb, 180,40,40),0.10);
    color:var(--fg, #111);
    font-weight:850;
}

.approvalsConfirmFoot{
    display:flex;
    align-items:center;
    gap:12px;
    padding:12px 16px;
    border-top:1px solid var(--border2, rgba(120,120,120,0.35));
    background:rgba(0,0,0,0.08);
}

.approvalsConfirmBtn{
    border:1px solid var(--border2, rgba(120,120,120,0.45));
    border-radius:14px;
    padding:9px 14px;
    font:inherit;
    font-weight:850;
    color:var(--fg, #111);
    background:linear-gradient(180deg, rgba(255,255,255,0.20), rgba(0,0,0,0.04));
    cursor:pointer;
}

.approvalsConfirmBtn:hover{
    filter:brightness(1.05);
}

.approvalsConfirmBtn.secondary{
    opacity:.90;
}

.approvalsConfirmBtn.danger{
    border-color:rgba(var(--fail-rgb, 180,40,40),0.48);
    background:rgba(var(--fail-rgb, 180,40,40),0.14);
    color:var(--fg, #111);
}

html[data-theme="bright"] .approvalsConfirmBackdrop{
    background:rgba(0,0,0,0.30);
}

html[data-theme="bright"] .approvalsConfirmCard{
    background:linear-gradient(180deg, #ffffff, #f2f4f7) !important;
    border-color:rgba(70,80,95,0.32) !important;
    color:#111827 !important;
    box-shadow:0 22px 80px rgba(0,0,0,0.28) !important;
}

html[data-theme="bright"] .approvalsConfirmHead,
html[data-theme="bright"] .approvalsConfirmFoot{
    background:rgba(15,23,42,0.045) !important;
    border-color:rgba(70,80,95,0.22) !important;
}

html[data-theme="bright"] .approvalsConfirmTitle,
html[data-theme="bright"] .approvalsConfirmValue,
html[data-theme="bright"] .approvalsConfirmBtn{
    color:#111827 !important;
}

html[data-theme="bright"] .approvalsConfirmSub,
html[data-theme="bright"] .approvalsConfirmKey{
    color:rgba(17,24,39,0.68) !important;
}

html[data-theme="bright"] .approvalsConfirmNote{
    background:rgba(180,40,40,0.10) !important;
    border-color:rgba(180,40,40,0.30) !important;
    color:#111827 !important;
}

html[data-theme="bright"] .approvalsConfirmBtn.secondary{
    background:linear-gradient(180deg, #ffffff, #e8ebef) !important;
}

html[data-theme="bright"] .approvalsConfirmBtn.danger{
    background:rgba(180,40,40,0.14) !important;
    border-color:rgba(180,40,40,0.38) !important;
    color:#111827 !important;
}

html[data-theme="win_classic"] .approvalsConfirmBackdrop{
    background:rgba(0,0,0,0.38);
}
`;
    document.head.appendChild(style);
}

function openApprovalsConfirmModal(opts = {}) {
    injectApprovalsConfirmCss();

    return new Promise((resolve) => {
        const options = opts || {};

        const modal = document.createElement("div");
        modal.className = "approvalsConfirmBackdrop";
        modal.setAttribute("role", "dialog");
        modal.setAttribute("aria-modal", "true");

        const card = document.createElement("div");
        card.className = "approvalsConfirmCard";

        const head = document.createElement("div");
        head.className = "approvalsConfirmHead";

        const title = document.createElement("div");
        title.className = "approvalsConfirmTitle";
        title.textContent = options.title || tr("admin.approvals.confirm_action", null, "Confirm action");

        const sub = document.createElement("div");
        sub.className = "approvalsConfirmSub";
        sub.textContent = options.subtitle || "";

        head.appendChild(title);
        if (sub.textContent) head.appendChild(sub);

        const body = document.createElement("div");
        body.className = "approvalsConfirmBody";

        for (const row of Array.isArray(options.rows) ? options.rows : []) {
            const k = document.createElement("div");
            k.className = "approvalsConfirmKey";
            k.textContent = String(row.label || "");

            const v = document.createElement("div");
            v.className = row.mono ? "approvalsConfirmValue mono" : "approvalsConfirmValue";
            v.textContent = String(row.value || "");

            body.appendChild(k);
            body.appendChild(v);
        }

        if (options.note) {
            const note = document.createElement("div");
            note.className = "approvalsConfirmNote";
            note.textContent = String(options.note || "");
            body.appendChild(note);
        }

        const foot = document.createElement("div");
        foot.className = "approvalsConfirmFoot";

        const spacer = document.createElement("div");
        spacer.style.flex = "1 1 auto";

        const cancelBtn = document.createElement("button");
        cancelBtn.type = "button";
        cancelBtn.className = "approvalsConfirmBtn secondary";
        cancelBtn.textContent = options.cancelText || tr("admin.approvals.cancel", null, "Cancel");

        const okBtn = document.createElement("button");
        okBtn.type = "button";
        okBtn.className = options.danger ? "approvalsConfirmBtn danger" : "approvalsConfirmBtn";
        okBtn.textContent = options.confirmText || tr("admin.approvals.ok", null, "OK");

        foot.appendChild(spacer);
        foot.appendChild(cancelBtn);
        foot.appendChild(okBtn);

        card.appendChild(head);
        card.appendChild(body);
        card.appendChild(foot);
        modal.appendChild(card);
        document.body.appendChild(modal);

        const finish = (value) => {
            document.removeEventListener("keydown", onKey, true);
            modal.remove();
            resolve(!!value);
        };

        const onKey = (ev) => {
            if (ev.key === "Escape") {
                ev.preventDefault();
                ev.stopPropagation();
                finish(false);
                return;
            }

            if (ev.key === "Enter") {
                ev.preventDefault();
                ev.stopPropagation();
                finish(true);
            }
        };

        document.addEventListener("keydown", onKey, true);

        modal.addEventListener("click", (ev) => {
            if (ev.target === modal) finish(false);
        });

        cancelBtn.addEventListener("click", () => finish(false));
        okBtn.addEventListener("click", () => finish(true));

        window.setTimeout(() => {
            if (options.danger) cancelBtn.focus();
            else okBtn.focus();
        }, 0);
    });
}


function render() {
    if (currentAuthMode() === "opaque") {
        renderOpaqueApprovals();
        return;
    }

    const f = ($("filter")?.value || "").toLowerCase().trim();

    // Approvals view behavior:
    // - If filter is empty: show only non-enabled (disabled/revoked)
    // - If filter is non-empty: show whatever matches (including enabled)
    const rows = allUsers.filter(u => {
        const st = String(u.status || "disabled").toLowerCase();
        if (!f && st === "enabled") return false;

        const hay = [
            u.fingerprint, u.name, u.notes, u.role, u.status
        ].join(" ").toLowerCase();

        return !f || hay.includes(f);
    });

    const tb = $("tbody");
    if (!tb) return;

    tb.innerHTML = rows.map(u => {
        return `<tr>
            <td class="mono">${esc(u.fingerprint)}</td>

            <td>
                <div><b>${esc(u.name || "")}</b></div>
                <div class="muted" style="white-space:pre-wrap;">${esc(u.notes || "")}</div>
            </td>

            <td>${esc(roleLabel(u.role || ""))}</td>
            <td>${pill(u.status)}</td>

            <td class="mono">${esc(u.added_at || "")}</td>
            <td class="mono">${esc(u.last_seen || "")}</td>

            <td class="row-actions">
                <button class="pq-btn secondary" data-act="enable" data-fp="${esc(u.fingerprint)}" type="button">${esc(tr("admin.approvals.enable", null, "Enable"))}</button>
                <button class="pq-btn secondary" data-act="disable" data-fp="${esc(u.fingerprint)}" type="button">${esc(tr("admin.approvals.disable", null, "Disable"))}</button>
                <button class="pq-btn secondary" data-act="revoke" data-fp="${esc(u.fingerprint)}" type="button">${esc(tr("admin.approvals.revoke", null, "Revoke"))}</button>
                <button class="pq-btn danger" data-act="delete" data-fp="${esc(u.fingerprint)}" type="button">${esc(tr("admin.approvals.delete", null, "Delete"))}</button>
            </td>
        </tr>`;
    }).join("");

    tb.querySelectorAll("button").forEach(b => {
        b.addEventListener("click", async () => {
            const fp = b.getAttribute("data-fp");
            const act = b.getAttribute("data-act");

            if (!fp || !act) return;

            if (act === "delete") {
                const ok = await openApprovalsConfirmModal({
                    title: tr("admin.approvals.delete_title", null, "Delete user entry?"),
                    subtitle: tr("admin.approvals.delete_sub", null, "This removes the user from users.json."),
                    rows: [
                        { label: tr("admin.approvals.fingerprint", null, "Fingerprint"), value: fp, mono: true },
                    ],
                    note: tr("admin.approvals.delete_note", null, "This removes the entry entirely as cleanup. If they scan again, they will re-appear as disabled."),
                    confirmText: tr("admin.approvals.delete", null, "Delete"),
                    cancelText: tr("admin.approvals.cancel", null, "Cancel"),
                    danger: true,
                });
                if (!ok) return;

                try {
                    setMsg(tr("admin.approvals.deleting", null, "Deleting…"));
                    await apiPost("/api/v4/admin/users/delete", { fingerprint: fp });
                    await refresh();
                    setMsg(tr("admin.approvals.delete_ok", null, "Delete OK"));
                } catch (e) {
                    setMsg(tr("admin.approvals.error", { error: e.message }, "Error: " + e.message));
                }
                return;
            }

            if (act === "enable") {
                try {
                    setMsg(tr("admin.approvals.enabling", null, "Enabling…"));
                    await apiPost("/api/v4/admin/users/enable", { fingerprint: fp });
                    await refresh();
                    setMsg(tr("admin.approvals.enabled", null, "Enabled"));
                } catch (e) {
                    setMsg(tr("admin.approvals.error", { error: e.message }, "Error: " + e.message));
                }
                return;
            }

            const status =
                (act === "disable") ? "disabled" :
                    (act === "revoke") ? "revoked" : "";

            if (!status) return;

            if (act === "revoke") {
                const ok = await openApprovalsConfirmModal({
                    title: tr("admin.approvals.revoke_title", null, "Revoke user?"),
                    subtitle: tr("admin.approvals.revoke_sub", null, "This hard-blocks login for this fingerprint."),
                    rows: [
                        { label: tr("admin.approvals.fingerprint", null, "Fingerprint"), value: fp, mono: true },
                    ],
                    note: tr("admin.approvals.revoke_note", null, "Use this when the identity should not be allowed to log in again."),
                    confirmText: tr("admin.approvals.revoke", null, "Revoke"),
                    cancelText: tr("admin.approvals.cancel", null, "Cancel"),
                    danger: true,
                });
                if (!ok) return;
            }

            try {
                setMsg(tr("admin.approvals.saving", null, "Saving…"));
                await apiPost("/api/v4/admin/users/status", { fingerprint: fp, status });
                await refresh();
                setMsg(tr("admin.approvals.saved", null, "Saved"));
            } catch (e) {
                setMsg(tr("admin.approvals.error", { error: e.message }, "Error: " + e.message));
            }
        });
    });
}

async function refresh() {
    setMsg(tr("admin.approvals.opaque.loading_auth", null, "Loading auth mode…"));
    try {
        authConfig = await apiGet("/api/auth/config");
    } catch (_) {
        authConfig = { mode: "qr" };
    }

    if (currentAuthMode() === "opaque") {
        setMsg(tr("admin.approvals.opaque.loading_onboarding", null, "Ladataan OPAQUE onboarding…"));
        const j = await apiGet("/api/admin/auth/opaque/onboarding/status");
        opaqueOnboarding = (j.entries || []).sort((a,b) => {
            const an = (a.name || a.login || a.fingerprint || "");
            const bn = (b.name || b.login || b.fingerprint || "");
            return an.localeCompare(bn);
        });
        allUsers = [];
        render();
        setMsg(tr("admin.approvals.opaque.loaded_count", { count: opaqueOnboarding.length }, `Ladattu ${opaqueOnboarding.length} OPAQUE onboarding -riviä`));
        return;
    }

    opaqueOnboarding = [];
    setMsg(tr("admin.approvals.loading_users", null, "Loading users…"));
    const j = await apiGet("/api/v4/admin/users");
    allUsers = (j.users || []).sort((a,b) => (a.fingerprint||"").localeCompare(b.fingerprint||""));
    render();
    setMsg(tr("admin.approvals.loaded_users", { count: allUsers.length }, `Loaded ${allUsers.length} users`));
}

window.addEventListener("load", async () => {
    $("btnRefresh")?.addEventListener("click", refresh);
    $("btnRefresh2")?.addEventListener("click", refresh);
    $("filter")?.addEventListener("input", render);

    try { await refresh(); }
    catch (e) { setMsg(tr("admin.approvals.failed_load", { error: e.message }, "Failed to load users: " + e.message)); }
});


window.addEventListener("pqnas-language-changed", () => {
    applyStaticI18n();
    try { render(); } catch (_) {}
});
