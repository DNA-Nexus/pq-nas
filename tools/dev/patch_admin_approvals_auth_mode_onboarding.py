#!/usr/bin/env python3
from pathlib import Path
import re
import sys

routes = Path("server/src/routes_v5.cc")
js = Path("server/src/static/admin_approvals.js")
html = Path("server/src/static/admin_approvals.html")

for p in (routes, js, html):
    if not p.exists():
        print(f"ERROR: missing {p}", file=sys.stderr)
        sys.exit(1)

s = routes.read_text()

if "routes_v5_opaque_enrollments_path" not in s:
    print("ERROR: OPAQUE enrollment helpers not found in routes_v5.cc. Run patch_opaque_enrollment_setup_link.py first.", file=sys.stderr)
    sys.exit(1)

if '/api/admin/auth/opaque/onboarding/status' not in s:
    anchor = '    // ---- POST /api/auth/opaque/login/start ----'
    if anchor not in s:
        print("ERROR: onboarding route insertion anchor not found", file=sys.stderr)
        sys.exit(1)

    route = r'''
    // ---- GET /api/admin/auth/opaque/onboarding/status ----
    //
    // Admin-only OPAQUE onboarding state view.
    //
    // This does not introduce new users.json statuses. It derives the OPAQUE
    // setup state from:
    // - users.json status: enabled / disabled / revoked
    // - opaque_credentials.json: credential exists/enabled
    // - opaque_enrollments.json: active/used/expired setup token
    srv.Get("/api/admin/auth/opaque/onboarding/status", [&](const httplib::Request& req, httplib::Response& res) {
        if (!routes_v5_opaque_auth_enabled()) {
            reply_json(res, 404, json{
                {"ok", false},
                {"error", "opaque_auth_disabled"},
                {"mode", routes_v5_auth_mode()}
            }.dump());
            return;
        }

        if (!ctx.require_user_cookie) {
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "auth_cookie_checker_not_configured"}
            }.dump());
            return;
        }

        std::string actor_fp;
        std::string actor_role;
        if (!ctx.require_user_cookie(req, res, &actor_fp, &actor_role)) {
            return;
        }

        if (actor_role != "admin") {
            routes_v5_audit_password(ctx, req, "opaque.onboarding_status", "deny", "", actor_fp, "not_admin");
            reply_json(res, 403, json{{"ok", false}, {"error", "admin_required"}}.dump());
            return;
        }

        if (!ctx.users) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "users_registry_not_configured"}}.dump());
            return;
        }

        pqnas::OpaqueCredentials creds;
        const std::string creds_path = routes_v5_opaque_credentials_path();
        if (!creds.load(creds_path)) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "opaque_credentials_load_failed"}}.dump());
            return;
        }

        const long now = routes_v5_now_epoch_safe(ctx);
        const std::string enrollments_path = routes_v5_opaque_enrollments_path(ctx);

        json enroll_doc;
        {
            std::lock_guard<std::mutex> lock(routes_v5_opaque_enrollments_file_mu());

            std::string lerr;
            enroll_doc = routes_v5_load_opaque_enrollments_no_lock(enrollments_path, &lerr);
            if (!lerr.empty()) {
                reply_json(res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "opaque_enrollments_load_failed"},
                    {"detail", lerr}
                }.dump());
                return;
            }

            routes_v5_prune_opaque_enrollments_doc(enroll_doc, now);
        }

        json entries = json::array();
        const auto snap = ctx.users->snapshot();

        for (const auto& kv : snap) {
            const pqnas::UserRec& u = kv.second;

            const std::string login =
                pqnas::OpaqueCredentials::normalize_login(u.email);

            if (login.empty()) {
                continue;
            }

            const auto cred = creds.get(login);
            const bool credential_exists =
                cred.has_value() &&
                cred->fingerprint == u.fingerprint &&
                !cred->opaque_password_file_b64.empty();

            const bool credential_enabled =
                credential_exists && cred->enabled;

            std::string token_state = "none";
            long token_expires_at = 0;
            long token_used_at = 0;
            std::string token_purpose;

            if (enroll_doc.contains("tokens") && enroll_doc["tokens"].is_array()) {
                for (const auto& token : enroll_doc["tokens"]) {
                    if (!token.is_object()) continue;
                    if (token.value("login", "") != login) continue;
                    if (token.value("fingerprint", "") != u.fingerprint) continue;

                    const long expires_at = token.value("expires_at", 0L);
                    const long used_at = token.value("used_at", 0L);

                    if (used_at <= 0 && expires_at > now) {
                        token_state = "active";
                        token_expires_at = expires_at;
                        token_used_at = 0;
                        token_purpose = token.value("purpose", "");
                        break;
                    }

                    if (token_state != "used" && used_at > 0) {
                        token_state = "used";
                        token_expires_at = expires_at;
                        token_used_at = used_at;
                        token_purpose = token.value("purpose", "");
                        continue;
                    }

                    if (token_state == "none" && expires_at > 0 && expires_at <= now) {
                        token_state = "expired";
                        token_expires_at = expires_at;
                        token_used_at = used_at;
                        token_purpose = token.value("purpose", "");
                    }
                }
            }

            const bool opaque_provisioned =
                u.notes.find("OPAQUE provisioning") != std::string::npos;

            if (!opaque_provisioned && !credential_exists && token_state == "none") {
                continue;
            }

            std::string onboarding_state;
            if (u.status == "revoked") {
                onboarding_state = "revoked";
            } else if (credential_exists && credential_enabled && u.status == "enabled") {
                onboarding_state = "setup_done";
            } else if (token_state == "active") {
                onboarding_state = "setup_link_created";
            } else if (token_state == "expired") {
                onboarding_state = "setup_link_expired";
            } else {
                onboarding_state = "waiting_password";
            }

            entries.push_back(json{
                {"login", login},
                {"fingerprint", u.fingerprint},
                {"name", u.name},
                {"notes", u.notes},
                {"role", u.role},
                {"user_status", u.status},
                {"added_at", u.added_at},
                {"last_seen", u.last_seen},
                {"credential_exists", credential_exists},
                {"credential_enabled", credential_enabled},
                {"credential_updated_at", cred.has_value() ? cred->updated_at : std::string{}},
                {"token_state", token_state},
                {"token_purpose", token_purpose},
                {"token_expires_at", token_expires_at},
                {"token_used_at", token_used_at},
                {"onboarding_state", onboarding_state}
            });
        }

        routes_v5_audit_password(ctx, req, "opaque.onboarding_status", "ok", "", actor_fp, "");

        reply_json(res, 200, json{
            {"ok", true},
            {"mode", routes_v5_auth_mode()},
            {"entries", entries}
        }.dump());
    });

'''
    s = s.replace(anchor, route + "\n" + anchor, 1)
    routes.write_text(s)
    print("patched routes_v5.cc: added OPAQUE onboarding status endpoint")
else:
    print("unchanged: OPAQUE onboarding status endpoint already present")

j = js.read_text()

# Make admin API calls explicitly same-origin.
j = j.replace(
'''async function apiGet(path) {
    const r = await fetch(path, { headers: { "Accept": "application/json" }, cache: "no-store" });
''',
'''async function apiGet(path) {
    const r = await fetch(path, { credentials: "same-origin", headers: { "Accept": "application/json" }, cache: "no-store" });
'''
)

j = j.replace(
'''    const r = await fetch(path, {
        method: "POST",
        headers: { "Content-Type": "application/json", "Accept": "application/json" },
''',
'''    const r = await fetch(path, {
        method: "POST",
        credentials: "same-origin",
        headers: { "Content-Type": "application/json", "Accept": "application/json" },
'''
)

if "let authConfig = null;" not in j:
    j = j.replace(
'''let allUsers = [];
''',
'''let allUsers = [];
let authConfig = null;
let opaqueOnboarding = [];
'''
    )

if "function onboardingStateLabel" not in j:
    anchor = '''function setMsg(text) {
    const el = $("msg");
    if (el) el.textContent = text || "";
}
'''
    if anchor not in j:
        print("ERROR: JS helper insertion anchor not found", file=sys.stderr)
        sys.exit(1)

    helpers = r'''

function currentAuthMode() {
    return String((authConfig && authConfig.mode) || "qr").toLowerCase();
}

function onboardingStateLabel(state) {
    const s = String(state || "").toLowerCase();
    if (s === "waiting_password") return "Odottaa salasanan asetusta";
    if (s === "setup_link_created") return "Setup-linkki luotu";
    if (s === "setup_link_expired") return "Setup-linkki vanhentunut";
    if (s === "setup_done") return "Setup valmis";
    if (s === "revoked") return "Revoked / peruttu";
    return s || "Tuntematon";
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
        title: "Setup-linkki luotu",
        subtitle: row.login || "",
        rows: [
            { label: "Käyttäjä", value: row.name || row.login || "" },
            { label: "Login", value: row.login || "", mono: true },
            { label: "Vanhenee", value: epochLabel(tokenResponse.expires_at) || String(tokenResponse.expires_at || "") },
            { label: "Setup URL", value: setupUrl, mono: true },
        ],
        note: "Kopioi tämä linkki käyttäjälle. Linkki näytetään tässä muodossa vain kerran.",
        confirmText: "Kopioi linkki",
        cancelText: "Sulje",
    });

    if (ok && setupUrl && navigator.clipboard && navigator.clipboard.writeText) {
        try {
            await navigator.clipboard.writeText(setupUrl);
            setMsg("Setup-linkki kopioitu leikepöydälle");
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

function renderOpaqueApprovals() {
    const f = ($("filter")?.value || "").toLowerCase().trim();

    const rows = opaqueOnboarding.filter(row => {
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
        tb.innerHTML = `<tr><td colspan="7" class="muted">Ei OPAQUE onboarding -rivejä.</td></tr>`;
        return;
    }

    tb.innerHTML = rows.map(row => {
        const canCreateLink = row.onboarding_state !== "revoked";
        const linkText = row.credential_exists ? "Luo reset-linkki" : "Luo setup-linkki";
        const tokenInfo =
            row.token_state === "active" ? `Aktiivinen linkki, vanhenee: ${epochLabel(row.token_expires_at)}` :
            row.token_state === "used" ? `Linkki käytetty: ${epochLabel(row.token_used_at)}` :
            row.token_state === "expired" ? `Linkki vanhentui: ${epochLabel(row.token_expires_at)}` :
            "Ei aktiivista linkkiä";

        const credInfo = row.credential_exists
            ? `Credential: ${row.credential_enabled ? "enabled" : "disabled"} ${row.credential_updated_at || ""}`
            : "Credential puuttuu";

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
                <button class="pq-btn secondary" data-act="opaque-revoke" data-fp="${esc(row.fingerprint)}" type="button">Peru / revoke</button>
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
                    setMsg("Luodaan OPAQUE setup-linkkiä…");
                    await createOpaqueSetupLink(row);
                } catch (e) {
                    setMsg("Virhe: " + e.message);
                }
                return;
            }

            if (act === "opaque-revoke") {
                const ok = await openApprovalsConfirmModal({
                    title: "Peru OPAQUE onboarding?",
                    subtitle: row.login || "",
                    rows: [
                        { label: "Käyttäjä", value: row.name || row.login || "" },
                        { label: "Login", value: row.login || "", mono: true },
                        { label: "Fingerprint", value: fp, mono: true },
                    ],
                    note: "Tämä asettaa käyttäjän revoked-tilaan. Olemassa oleva setup-linkki ei saa enää herättää käyttäjää takaisin.",
                    confirmText: "Peru / revoke",
                    cancelText: tr("admin.approvals.cancel", null, "Cancel"),
                    danger: true,
                });
                if (!ok) return;

                try {
                    setMsg("Perutaan…");
                    await apiPost("/api/v4/admin/users/status", { fingerprint: fp, status: "revoked" });
                    await refresh();
                    setMsg("Peruttu / revoked");
                } catch (e) {
                    setMsg("Virhe: " + e.message);
                }
                return;
            }

            if (act === "opaque-delete") {
                const ok = await openApprovalsConfirmModal({
                    title: tr("admin.approvals.delete_title", null, "Delete user entry?"),
                    subtitle: row.login || "",
                    rows: [
                        { label: "Käyttäjä", value: row.name || row.login || "" },
                        { label: "Login", value: row.login || "", mono: true },
                        { label: "Fingerprint", value: fp, mono: true },
                    ],
                    note: "Poistaa users.json-rivin. Käytä vain testidatan siivoukseen.",
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
                    setMsg("Virhe: " + e.message);
                }
            }
        });
    });
}
'''
    j = j.replace(anchor, anchor + helpers, 1)
else:
    print("unchanged: OPAQUE onboarding JS helpers already present")

j = j.replace(
'''function render() {
    const f = ($("filter")?.value || "").toLowerCase().trim();
''',
'''function render() {
    if (currentAuthMode() === "opaque") {
        renderOpaqueApprovals();
        return;
    }

    const f = ($("filter")?.value || "").toLowerCase().trim();
''',
1
)

old_refresh = '''async function refresh() {
    setMsg(tr("admin.approvals.loading_users", null, "Loading users…"));
    const j = await apiGet("/api/v4/admin/users");
    allUsers = (j.users || []).sort((a,b) => (a.fingerprint||"").localeCompare(b.fingerprint||""));
    render();
    setMsg(tr("admin.approvals.loaded_users", { count: allUsers.length }, `Loaded ${allUsers.length} users`));
}
'''

new_refresh = '''async function refresh() {
    setMsg("Loading auth mode…");
    try {
        authConfig = await apiGet("/api/auth/config");
    } catch (_) {
        authConfig = { mode: "qr" };
    }

    if (currentAuthMode() === "opaque") {
        setMsg("Ladataan OPAQUE onboarding…");
        const j = await apiGet("/api/admin/auth/opaque/onboarding/status");
        opaqueOnboarding = (j.entries || []).sort((a,b) => {
            const an = (a.name || a.login || a.fingerprint || "");
            const bn = (b.name || b.login || b.fingerprint || "");
            return an.localeCompare(bn);
        });
        allUsers = [];
        render();
        setMsg(`Ladattu ${opaqueOnboarding.length} OPAQUE onboarding -riviä`);
        return;
    }

    opaqueOnboarding = [];
    setMsg(tr("admin.approvals.loading_users", null, "Loading users…"));
    const j = await apiGet("/api/v4/admin/users");
    allUsers = (j.users || []).sort((a,b) => (a.fingerprint||"").localeCompare(b.fingerprint||""));
    render();
    setMsg(tr("admin.approvals.loaded_users", { count: allUsers.length }, `Loaded ${allUsers.length} users`));
}
'''

if old_refresh not in j:
    print("ERROR: refresh anchor not found in admin_approvals.js", file=sys.stderr)
    sys.exit(1)
j = j.replace(old_refresh, new_refresh, 1)

js.write_text(j)
print("patched admin_approvals.js: auth-mode aware QR/OPAQUE approvals")

h = html.read_text()
h = re.sub(
    r'/static/admin_approvals\.js\?v=[^"]+',
    '/static/admin_approvals.js?v=20260613-opaque-onboarding-ui-1',
    h
)
html.write_text(h)
print("patched admin_approvals.html cache buster")
