#!/usr/bin/env python3
from pathlib import Path
import json
import sys

js = Path("server/src/static/admin_approvals.js")
html = Path("server/src/static/admin_approvals.html")
i18n_dir = Path("server/src/static/i18n")

for p in (js, html, i18n_dir):
    if not p.exists():
        print(f"ERROR: missing {p}", file=sys.stderr)
        sys.exit(1)

s = js.read_text()

helpers = r'''

function syncOpaqueCreateBox() {
    const box = $("opaqueCreateBox");
    if (!box) return;
    box.classList.toggle("hidden", currentAuthMode() !== "opaque");
}

function opaqueCreateQuotaBytes() {
    const raw = String($("opaqueCreateQuota")?.value || "").trim();
    if (!raw) return 0;

    const n = Number(raw);
    if (!Number.isFinite(n) || n < 0 || Math.floor(n) !== n) {
        throw new Error(tr("admin.approvals.opaque.create_user_invalid_quota", null, "Quota must be a non-negative integer."));
    }

    return n;
}

function clearOpaqueCreateForm() {
    if ($("opaqueCreateName")) $("opaqueCreateName").value = "";
    if ($("opaqueCreateLogin")) $("opaqueCreateLogin").value = "";
    if ($("opaqueCreateRole")) $("opaqueCreateRole").value = "user";
    if ($("opaqueCreateQuota")) $("opaqueCreateQuota").value = "";
}

async function showOpaqueCreatedModal(created, tokenResponse) {
    const setupUrl = String(tokenResponse.setup_url || tokenResponse.setup_path || "");
    const recoveryWords = String(created.recovery_words || "");

    const ok = await openApprovalsConfirmModal({
        title: tr("admin.approvals.opaque.create_user_created_title", null, "OPAQUE user created"),
        subtitle: created.login || "",
        rows: [
            { label: tr("admin.approvals.opaque.user", null, "Käyttäjä"), value: created.name || created.login || "" },
            { label: "Login", value: created.login || "", mono: true },
            { label: tr("admin.approvals.fingerprint", null, "Fingerprint"), value: created.fingerprint || "", mono: true },
            { label: tr("admin.approvals.opaque.recovery_words", null, "Recovery words"), value: recoveryWords, mono: true },
            { label: tr("admin.approvals.opaque.expires", null, "Vanhenee"), value: epochLabel(tokenResponse.expires_at) || String(tokenResponse.expires_at || "") },
            { label: tr("admin.approvals.opaque.setup_url", null, "Setup URL"), value: setupUrl, mono: true },
        ],
        note: tr("admin.approvals.opaque.create_user_created_note", null, "Copy the recovery words and setup link now. Recovery words are shown only once."),
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

async function createOpaqueUserAndSetupLink() {
    const name = String($("opaqueCreateName")?.value || "").trim();
    const login = String($("opaqueCreateLogin")?.value || "").trim().toLowerCase();
    const role = String($("opaqueCreateRole")?.value || "user").trim().toLowerCase();
    const quota_bytes = opaqueCreateQuotaBytes();

    if (!name || !login) {
        throw new Error(tr("admin.approvals.opaque.create_user_required", null, "Name and login are required."));
    }

    if (role !== "user" && role !== "admin") {
        throw new Error(tr("admin.approvals.opaque.create_user_invalid_role", null, "Invalid role."));
    }

    const created = await apiPost("/api/admin/users/opaque-create", {
        login,
        name,
        role,
        quota_bytes,
        include_public_key: false
    });

    const token = await apiPost("/api/admin/auth/opaque/enrollment-token/create", {
        login: created.login,
        fingerprint: created.fingerprint,
        purpose: "new_user",
        enable_user_on_finish: true,
        expires_in_seconds: 86400
    });

    await showOpaqueCreatedModal(created, token);
    clearOpaqueCreateForm();
    await refresh();
}
'''

if "function syncOpaqueCreateBox" not in s:
    anchor = '''function setMsg(text) {
    const el = $("msg");
    if (el) el.textContent = text || "";
}
'''
    if anchor not in s:
        raise SystemExit("ERROR: setMsg anchor not found")
    s = s.replace(anchor, anchor + helpers, 1)
    print("inserted OPAQUE create user helpers")

if "syncOpaqueCreateBox();" not in s:
    needle = '    if (currentAuthMode() === "opaque") {'
    if needle not in s:
        raise SystemExit('ERROR: refresh mode branch anchor not found')
    s = s.replace(needle, '    syncOpaqueCreateBox();\n\n' + needle, 1)
    print("inserted syncOpaqueCreateBox into refresh()")

if '$("btnOpaqueCreateUser")?.addEventListener("click"' not in s:
    needle = '    $("filter")?.addEventListener("input", render);\n'
    if needle not in s:
        raise SystemExit("ERROR: load filter listener anchor not found")

    block = r'''
    $("btnOpaqueCreateUser")?.addEventListener("click", async () => {
        const btn = $("btnOpaqueCreateUser");
        const oldText = btn ? btn.textContent : "";
        try {
            if (btn) {
                btn.disabled = true;
                btn.textContent = tr("admin.approvals.opaque.creating_user", null, "Creating…");
            }
            setMsg(tr("admin.approvals.opaque.creating_user", null, "Creating…"));
            await createOpaqueUserAndSetupLink();
        } catch (e) {
            setMsg(tr("admin.approvals.opaque.error", { error: e.message }, "Virhe: " + e.message));
        } finally {
            if (btn) {
                btn.disabled = false;
                btn.textContent = oldText || tr("admin.approvals.opaque.create_user_button", null, "Create user and setup link");
            }
        }
    });
'''
    s = s.replace(needle, needle + block + "\n", 1)
    print("inserted OPAQUE create user button handler")

# Keep language changes from hiding/showing stale state.
if 'window.addEventListener("pqnas-language-changed"' in s and 'syncOpaqueCreateBox();' in s:
    old = '''window.addEventListener("pqnas-language-changed", () => {
    applyStaticI18n();
    try { render(); } catch (_) {}
});
'''
    new = '''window.addEventListener("pqnas-language-changed", () => {
    applyStaticI18n();
    syncOpaqueCreateBox();
    try { render(); } catch (_) {}
});
'''
    if old in s:
        s = s.replace(old, new, 1)
        print("patched language-change handler")

js.write_text(s)

# Ensure HTML cache buster in case the previous script changed HTML.
h = html.read_text()
h = h.replace(
    '/static/admin_approvals.js?v=20260613-opaque-force-reset-i18n-1',
    '/static/admin_approvals.js?v=20260613-opaque-create-user-ui-1'
)
html.write_text(h)

translations = {
    "en": {
        "admin.approvals.opaque.create_user_title": "Create OPAQUE user",
        "admin.approvals.opaque.create_user_name": "Name",
        "admin.approvals.opaque.create_user_name_placeholder": "Jussi Virtanen",
        "admin.approvals.opaque.create_user_login": "Login / email",
        "admin.approvals.opaque.create_user_login_placeholder": "jussi.virtanen@example.invalid",
        "admin.approvals.opaque.create_user_role": "Role",
        "admin.approvals.opaque.create_user_quota": "Quota bytes",
        "admin.approvals.opaque.create_user_quota_placeholder": "0",
        "admin.approvals.opaque.create_user_button": "Create user and setup link",
        "admin.approvals.opaque.create_user_help": "Creates a DNA identity and shows recovery words once. The setup link lets the user choose their OPAQUE password without sending it to the server.",
        "admin.approvals.opaque.creating_user": "Creating…",
        "admin.approvals.opaque.create_user_required": "Name and login are required.",
        "admin.approvals.opaque.create_user_invalid_role": "Invalid role.",
        "admin.approvals.opaque.create_user_invalid_quota": "Quota must be a non-negative integer.",
        "admin.approvals.opaque.create_user_created_title": "OPAQUE user created",
        "admin.approvals.opaque.create_user_created_note": "Copy the recovery words and setup link now. Recovery words are shown only once.",
        "admin.approvals.opaque.recovery_words": "Recovery words"
    },
    "fi": {
        "admin.approvals.opaque.create_user_title": "Luo OPAQUE-käyttäjä",
        "admin.approvals.opaque.create_user_name": "Nimi",
        "admin.approvals.opaque.create_user_name_placeholder": "Jussi Virtanen",
        "admin.approvals.opaque.create_user_login": "Login / sähköposti",
        "admin.approvals.opaque.create_user_login_placeholder": "jussi.virtanen@example.invalid",
        "admin.approvals.opaque.create_user_role": "Rooli",
        "admin.approvals.opaque.create_user_quota": "Kiintiö tavuina",
        "admin.approvals.opaque.create_user_quota_placeholder": "0",
        "admin.approvals.opaque.create_user_button": "Luo käyttäjä ja setup-linkki",
        "admin.approvals.opaque.create_user_help": "Luo DNA-identiteetin ja näyttää recovery wordsit kerran. Setup-linkillä käyttäjä valitsee OPAQUE-salasanansa ilman että salasana lähetetään serverille.",
        "admin.approvals.opaque.creating_user": "Luodaan…",
        "admin.approvals.opaque.create_user_required": "Nimi ja login ovat pakollisia.",
        "admin.approvals.opaque.create_user_invalid_role": "Virheellinen rooli.",
        "admin.approvals.opaque.create_user_invalid_quota": "Kiintiön pitää olla nolla tai positiivinen kokonaisluku.",
        "admin.approvals.opaque.create_user_created_title": "OPAQUE-käyttäjä luotu",
        "admin.approvals.opaque.create_user_created_note": "Kopioi recovery wordsit ja setup-linkki nyt. Recovery wordsit näytetään vain kerran.",
        "admin.approvals.opaque.recovery_words": "Recovery words"
    }
}

fallback_other = {
    "sv": ("Skapa OPAQUE-användare", "Skapa användare och setup-länk"),
    "de": ("OPAQUE-Benutzer erstellen", "Benutzer und Setup-Link erstellen"),
    "es": ("Crear usuario OPAQUE", "Crear usuario y enlace de configuración"),
    "fr": ("Créer un utilisateur OPAQUE", "Créer l’utilisateur et le lien de configuration"),
    "it": ("Crea utente OPAQUE", "Crea utente e link di configurazione"),
    "et": ("Loo OPAQUE kasutaja", "Loo kasutaja ja seadistuslink"),
    "pl": ("Utwórz użytkownika OPAQUE", "Utwórz użytkownika i link konfiguracji"),
    "tr": ("OPAQUE kullanıcısı oluştur", "Kullanıcı ve kurulum bağlantısı oluştur"),
    "uk": ("Створити користувача OPAQUE", "Створити користувача і посилання налаштування"),
    "zh": ("创建 OPAQUE 用户", "创建用户和设置链接"),
}

for lang, (title, button) in fallback_other.items():
    d = dict(translations["en"])
    d["admin.approvals.opaque.create_user_title"] = title
    d["admin.approvals.opaque.create_user_button"] = button
    translations[lang] = d

for p in sorted(i18n_dir.glob("*.json")):
    lang = p.stem
    data = json.loads(p.read_text())
    add = translations.get(lang, translations["en"])
    changed = False
    for k, v in add.items():
        if data.get(k) != v:
            data[k] = v
            changed = True
    if changed:
        p.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n")
        print(f"patched i18n: {p}")

print("done")
