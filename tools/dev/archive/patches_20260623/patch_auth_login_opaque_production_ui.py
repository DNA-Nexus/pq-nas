#!/usr/bin/env python3
from pathlib import Path
import json
import shutil
import sys
import re

ROOT = Path(__file__).resolve().parents[2]

def die(msg: str) -> None:
    print(f"ERROR: {msg}", file=sys.stderr)
    sys.exit(1)

def p(rel: str) -> Path:
    return ROOT / rel

def read(rel: str) -> str:
    path = p(rel)
    if not path.exists():
        die(f"missing file: {rel}")
    return path.read_text(encoding="utf-8")

def write(rel: str, text: str) -> None:
    path = p(rel)
    path.write_text(text, encoding="utf-8")
    print(f"patched: {rel}")

AUTH = "server/src/static/auth_login.js"
LOGIN_HTML = "server/src/static/login.html"

WASM_JS_SRC = "tools/opaque_browser_client/pkg/pqnas_opaque_browser_client.js"
WASM_BG_SRC = "tools/opaque_browser_client/pkg/pqnas_opaque_browser_client_bg.wasm"
WASM_JS_DST = "server/src/static/opaque/pqnas_opaque_browser_client.js"
WASM_BG_DST = "server/src/static/opaque/pqnas_opaque_browser_client_bg.wasm"

for rel in (WASM_JS_SRC, WASM_BG_SRC):
    if not p(rel).exists():
        die(f"missing OPAQUE browser client artifact: {rel}")

p("server/src/static/opaque").mkdir(parents=True, exist_ok=True)
shutil.copy2(p(WASM_JS_SRC), p(WASM_JS_DST))
shutil.copy2(p(WASM_BG_SRC), p(WASM_BG_DST))
print(f"copied: {WASM_JS_SRC} -> {WASM_JS_DST}")
print(f"copied: {WASM_BG_SRC} -> {WASM_BG_DST}")

text = read(AUTH)

const_anchor = '    const OPAQUE_MODE = "opaque";\n'
opaque_consts = '''    const OPAQUE_CLIENT_MODULE_URL = "/static/opaque/pqnas_opaque_browser_client.js?v=20260613-opaque-prod-ui-1";
    const OPAQUE_CLIENT_WASM_URL = "/static/opaque/pqnas_opaque_browser_client_bg.wasm?v=20260613-opaque-prod-ui-1";
    let opaqueClientModulePromise = null;
'''

if "OPAQUE_CLIENT_MODULE_URL" not in text:
    if const_anchor not in text:
        die("auth_login.js OPAQUE_MODE anchor not found")
    text = text.replace(const_anchor, const_anchor + opaque_consts, 1)
else:
    print("unchanged: OPAQUE client constants already present")

helpers_anchor = '    function renderOpaqueLogin() {\n'
opaque_helpers = r'''
    function opaqueObjectResult(value) {
        if (!value) return {};

        if (typeof value === "string") {
            try {
                return JSON.parse(value);
            } catch {
                return {};
            }
        }

        if (typeof value === "object") return value;
        return {};
    }

    function opaquePick(obj, names) {
        for (const name of names) {
            if (obj && Object.prototype.hasOwnProperty.call(obj, name)) {
                const value = obj[name];
                if (typeof value === "string" && value.trim()) return value.trim();
            }
        }
        return "";
    }

    async function loadOpaqueBrowserClient() {
        if (!opaqueClientModulePromise) {
            opaqueClientModulePromise = (async () => {
                const mod = await import(OPAQUE_CLIENT_MODULE_URL);

                if (typeof mod.default === "function") {
                    await mod.default(OPAQUE_CLIENT_WASM_URL);
                }

                const opaqueLoginStart =
                    mod.opaqueLoginStart ||
                    mod.opaque_login_start;

                const opaqueLoginFinish =
                    mod.opaqueLoginFinish ||
                    mod.opaque_login_finish;

                if (typeof opaqueLoginStart !== "function" ||
                    typeof opaqueLoginFinish !== "function") {
                    throw new Error("OPAQUE browser client exports are missing.");
                }

                return { opaqueLoginStart, opaqueLoginFinish };
            })();
        }

        return opaqueClientModulePromise;
    }

    async function postOpaqueJson(path, body) {
        const res = await fetch(path, {
            method: "POST",
            cache: "no-store",
            credentials: "include",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify(body)
        });

        const data = await res.json().catch(() => ({}));
        return { res, data };
    }

'''

if "function loadOpaqueBrowserClient()" not in text:
    if helpers_anchor not in text:
        die("auth_login.js renderOpaqueLogin helper anchor not found")
    text = text.replace(helpers_anchor, opaque_helpers + helpers_anchor, 1)
else:
    print("unchanged: OPAQUE helper functions already present")

start_marker = '    function renderOpaqueLogin() {\n'
end_marker = '\n    async function start() {\n'

if start_marker not in text:
    die("renderOpaqueLogin start marker not found")
start = text.index(start_marker)

try:
    end = text.index(end_marker, start)
except ValueError:
    die("renderOpaqueLogin end marker not found")

new_render = r'''    function renderOpaqueLogin() {
        injectPasswordCss();

        const card = document.querySelector(".card");
        if (!card) {
            setStatusKey("auth.login.ui_missing", "Login UI missing");
            return;
        }

        card.innerHTML = `
            <div class="loginMark">
                <div class="pq-badge loginMarkBadge">DNA-Nexus</div>
            </div>

            <h1 data-i18n="auth.opaque.title">Zero-knowledge sign in</h1>

            <div class="hint" data-i18n="auth.opaque.signin_hint">
                Sign in with OPAQUE. Your password is processed locally in this browser and is not sent to the server.
            </div>

            <div class="presentationLinkWrap">
                <a class="presentationLink"
                   href="/static/nexus-presentation/index.html"
                   target="_blank"
                   rel="noopener"
                   data-i18n="auth.login.presentation_link">What is DNA-Nexus?</a>
            </div>

            <form id="opaqueLoginForm" class="passwordForm" autocomplete="on">
                <label>
                    <span data-i18n="auth.login.username_label">Email / username</span>
                    <input id="opaqueLoginName"
                           name="username"
                           type="text"
                           inputmode="email"
                           autocomplete="username"
                           maxlength="254"
                           data-i18n-aria-label="auth.login.username_label"
                           aria-label="Email / username"
                           required>
                </label>

                <label>
                    <span data-i18n="auth.login.password_label">Password</span>
                    <input id="opaqueLoginPassword"
                           name="password"
                           type="password"
                           autocomplete="current-password"
                           maxlength="1024"
                           data-i18n-aria-label="auth.login.password_label"
                           aria-label="Password"
                           required>
                </label>

                <button id="opaqueLoginButton" type="submit" data-i18n="auth.login.sign_in_button">Sign in</button>
            </form>

            <div class="hint" data-i18n="auth.opaque.no_password_fallback_hint">
                This page never falls back to sending your password to the server.
            </div>

            <div id="status" class="status" data-i18n="auth.opaque.ready">OPAQUE login ready.</div>

            <div class="footer" data-i18n="auth.login.footer">© CPUNK 2026 · DNA-Nexus</div>
        `;

        const form = el("opaqueLoginForm");
        const loginInput = el("opaqueLoginName");
        const passwordInput = el("opaqueLoginPassword");
        const button = el("opaqueLoginButton");

        applyStaticI18n();

        try {
            const saved =
                localStorage.getItem("pqnas_opaque_login") ||
                localStorage.getItem("pqnas_password_login") ||
                "";
            if (saved && loginInput) loginInput.value = saved;
        } catch {}

        if (loginInput && loginInput.value && passwordInput) {
            passwordInput.focus();
        } else if (loginInput) {
            loginInput.focus();
        }

        form.addEventListener("submit", async (ev) => {
            ev.preventDefault();

            const login = String(loginInput.value || "").trim();
            let password = String(passwordInput.value || "");

            if (!login || !password) {
                setStatusKey("auth.login.enter_login_password", "Enter username/email and password.");
                return;
            }

            setBusy(true);
            if (button) button.disabled = true;
            setStatusKey("auth.opaque.loading_client", "Loading OPAQUE client…");

            try {
                const client = await loadOpaqueBrowserClient();

                setStatusKey("auth.opaque.creating_request", "Creating OPAQUE login request…");
                const startResult = opaqueObjectResult(await client.opaqueLoginStart(password));

                const client_state =
                    opaquePick(startResult, [
                        "client_state",
                        "client_state_b64",
                        "client_login_state_b64",
                        "clientLoginStateB64"
                    ]);

                const credential_request_b64 =
                    opaquePick(startResult, [
                        "credential_request_b64",
                        "credentialRequestB64",
                        "request_b64"
                    ]);

                if (!client_state || !credential_request_b64) {
                    throw new Error("OPAQUE login start did not produce required client values.");
                }

                setStatusKey("auth.opaque.contacting_server", "Contacting server…");
                const startHttp = await postOpaqueJson("/api/auth/opaque/login/start", {
                    login,
                    credential_request_b64
                });

                if (!startHttp.res.ok || !startHttp.data || startHttp.data.ok === false) {
                    throw new Error("invalid_login_or_password");
                }

                const opaque_login_id = String(startHttp.data.opaque_login_id || "").trim();
                const credential_response_b64 = String(startHttp.data.credential_response_b64 || "").trim();

                if (!opaque_login_id || !credential_response_b64) {
                    throw new Error("OPAQUE server response was incomplete.");
                }

                setStatusKey("auth.opaque.finalizing", "Finalizing OPAQUE login…");
                const finishResult = opaqueObjectResult(
                    await client.opaqueLoginFinish(password, client_state, credential_response_b64)
                );

                const credential_finalization_b64 =
                    opaquePick(finishResult, [
                        "credential_finalization_b64",
                        "credentialFinalizationB64",
                        "finalization_b64"
                    ]);

                password = "";
                if (passwordInput) passwordInput.value = "";

                if (!credential_finalization_b64) {
                    throw new Error("OPAQUE login finish did not produce finalization.");
                }

                const finishHttp = await postOpaqueJson("/api/auth/opaque/login/finish", {
                    opaque_login_id,
                    credential_finalization_b64
                });

                if (!finishHttp.res.ok ||
                    !finishHttp.data ||
                    finishHttp.data.ok === false ||
                    finishHttp.data.authenticated !== true ||
                    finishHttp.data.session_minting !== true) {
                    throw new Error("invalid_login_or_password");
                }

                try {
                    localStorage.setItem("pqnas_opaque_login", login);
                } catch {}

                setStatusKey("auth.opaque.verifying_cookie", "Verifying session cookie…");
                const ping = await fetch("/api/v4/me", {
                    cache: "no-store",
                    credentials: "include"
                });

                if (!ping.ok) {
                    throw new Error("Login OK, but session cookie did not stick.");
                }

                window.location.href = "/app";
            } catch (e) {
                console.error(e);
                setStatusKey("auth.login.invalid_login_password", "Invalid login or password.");
                setBusy(false);
                if (button) button.disabled = false;
            } finally {
                password = "";
                if (passwordInput) passwordInput.value = "";
            }
        });
    }
'''

text = text[:start] + new_render + text[end:]
write(AUTH, text)

html = read(LOGIN_HTML)
script_re = re.compile(r'<script src="/static/auth_login\.js\?v=[^"]+"></script>')
matches = script_re.findall(html)
if len(matches) != 1:
    die(f"expected exactly one auth_login.js script tag in {LOGIN_HTML}, found {len(matches)}")
html = script_re.sub(
    '<script src="/static/auth_login.js?v=20260613-opaque-prod-ui-1"></script>',
    html,
    count=1
)
write(LOGIN_HTML, html)

i18n_keys = {
    "auth.opaque.signin_hint": "Sign in with OPAQUE. Your password is processed locally in this browser and is not sent to the server.",
    "auth.opaque.ready": "OPAQUE login ready.",
    "auth.opaque.loading_client": "Loading OPAQUE client…",
    "auth.opaque.creating_request": "Creating OPAQUE login request…",
    "auth.opaque.contacting_server": "Contacting server…",
    "auth.opaque.finalizing": "Finalizing OPAQUE login…",
    "auth.opaque.verifying_cookie": "Verifying session cookie…",
}

i18n_dir = p("server/src/static/i18n")
if not i18n_dir.exists():
    die("missing i18n directory: server/src/static/i18n")

for path in sorted(i18n_dir.glob("*.json")):
    rel = path.relative_to(ROOT).as_posix()
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except Exception as e:
        die(f"failed to parse {rel}: {e}")

    if not isinstance(data, dict):
        die(f"{rel} is not a JSON object")

    changed = False
    for key, value in i18n_keys.items():
        if key not in data:
            data[key] = value
            changed = True

    if changed:
        path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(f"patched: {rel}")
    else:
        print(f"unchanged: {rel}")

print("done: production login now uses browser OPAQUE client in OPAQUE mode")
