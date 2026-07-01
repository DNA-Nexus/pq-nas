(() => {
    "use strict";

    const QR_SCRIPT = "/static/pqnas_v5.js?v=20260518-login-i18n-1";
    const PASSWORD_MODE = "password";
    const OPAQUE_MODE = "opaque";
    const OPAQUE_CLIENT_MODULE_URL = "/static/opaque/pqnas_opaque_browser_client.js?v=20260613-opaque-prod-ui-1";
    const OPAQUE_CLIENT_WASM_URL = "/static/opaque/pqnas_opaque_browser_client_bg.wasm?v=20260613-opaque-prod-ui-1";
    let opaqueClientModulePromise = null;

    const el = (id) => document.getElementById(id);

    const loginPageParams = new URLSearchParams(window.location.search || "");

    function safeReturnTo(raw) {
        const value = String(raw || "").trim();
        if (!value) return "";
        if (!value.startsWith("/")) return "";
        if (value.startsWith("//")) return "";
        if (value.includes("\n") || value.includes("\r")) return "";

        // For now this is intentionally narrow. External users should only be
        // returned to the isolated Shared Space surface after login.
        if (!value.startsWith("/static/external_workspace.html?")) return "";

        return value;
    }

    const loginReturnTo = safeReturnTo(loginPageParams.get("return_to") || "");

    function postLoginTarget() {
        return loginReturnTo || "/app";
    }

    async function externalWorkspacePostLoginTarget() {
        const fallback = postLoginTarget();

        // If setup/login explicitly included return_to, trust the already-sanitized value.
        if (fallback && fallback !== "/app") {
            return fallback;
        }

        // Otherwise ask the server whether this session belongs to an
        // external-workspace-only user. This prevents those users from ever
        // landing on the normal desktop after browser login.
        try {
            const r = await fetch("/api/v4/workspaces/external-session/landing", {
                credentials: "include",
                cache: "no-store",
                headers: { "Accept": "application/json" }
            });

            const j = await r.json().catch(() => null);

            if (
                r.ok &&
                j &&
                j.ok === true &&
                j.external_workspace_only === true &&
                typeof j.workspace_url === "string" &&
                j.workspace_url.trim()
            ) {
                return j.workspace_url.trim();
            }
        } catch (_) {
            // Keep normal login usable if the helper endpoint is temporarily unavailable.
        }

        return fallback || "/app";
    }


    function tr(key, vars, fallback) {
        const api = window.PQNAS_I18N;
        if (api && typeof api.t === "function") {
            return api.t(key, vars || null, fallback);
        }
        return String(fallback ?? key);
    }

    function applyStaticI18n() {
        const api = window.PQNAS_I18N;
        if (api && typeof api.apply === "function") {
            api.apply(document);
        }
    }

    function applyLoginBranding() {
        const api = window.PQNAS_BRANDING;
        if (!api) return;

        if (typeof api.apply === "function") {
            api.apply(document);
        }

        if (typeof api.ready === "function") {
            api.ready()
                .then(() => {
                    if (typeof api.apply === "function") {
                        api.apply(document);
                    }
                })
                .catch(() => {});
        }
    }

    function setBusy(isBusy) {
        document.body.classList.toggle("busy", !!isBusy);
    }

    function setStatus(msg) {
        const status = el("status");
        if (status) status.textContent = msg;
    }

    function setStatusKey(key, fallback, vars) {
        setStatus(tr(key, vars || null, fallback));
    }

    function injectPasswordCss() {
        if (document.getElementById("passwordLoginStyle")) return;

        const style = document.createElement("style");
        style.id = "passwordLoginStyle";
        style.textContent = `
            .passwordForm {
                display: grid;
                gap: 12px;
                margin-top: 18px;
            }

            .passwordForm label {
                display: grid;
                gap: 6px;
                color: rgba(var(--fg-rgb),0.82);
                font-size: 13px;
                font-weight: 800;
            }

            .passwordForm input {
                width: 100%;
                border: 1px solid rgba(var(--fg-rgb),0.24);
                border-radius: 14px;
                padding: 12px 13px;
                background: rgba(var(--fg-rgb),0.06);
                color: var(--fg);
                font: inherit;
                outline: none;
            }

            .passwordForm input:focus {
                border-color: rgba(var(--fg-rgb),0.46);
                box-shadow: 0 0 0 3px rgba(var(--fg-rgb),0.10);
            }

            .passwordForm button {
                margin-top: 4px;
                min-height: 44px;
                border: 1px solid rgba(var(--fg-rgb),0.28);
                border-radius: 999px;
                background: rgba(var(--fg-rgb),0.12);
                color: var(--fg);
                font: inherit;
                font-weight: 900;
                cursor: pointer;
            }

            .passwordForm button:hover {
                background: rgba(var(--fg-rgb),0.18);
            }
        `;
        document.head.appendChild(style);
    }

    function loadQrLogin() {
        applyStaticI18n();

        const script = document.createElement("script");
        script.src = QR_SCRIPT;
        script.async = false;
        document.body.appendChild(script);
    }

    function renderPasswordLogin() {
        injectPasswordCss();

        const card = document.querySelector(".card");
        if (!card) {
            setStatusKey("auth.login.ui_missing", "Login UI missing");
            return;
        }

        card.innerHTML = `
            <div class="loginMark">
                <div class="pq-badge loginMarkBadge" data-brand-text="product_short_name">Server</div>
            </div>

            <h1 data-i18n="auth.login.title">Sign in</h1>

            <div class="hint" data-i18n="auth.login.password_hint">
                Use your username or email address.
            </div>

            <div class="presentationLinkWrap" data-brand-hide-if-presentation-disabled>
                <a class="presentationLink"
                   href="/static/nexus-presentation/index.html"
                   target="_blank"
                   rel="noopener"
                   data-brand-presentation-link
                   data-i18n="auth.login.presentation_link">What is this service?</a>
            </div>

            <form id="passwordLoginForm" class="passwordForm" autocomplete="on">
                <label>
                    <span data-i18n="auth.login.username_label">Email / username</span>
                    <input id="passwordLoginName"
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
                    <input id="passwordLoginPassword"
                           name="password"
                           type="password"
                           autocomplete="current-password"
                           maxlength="1024"
                           data-i18n-aria-label="auth.login.password_label"
                           aria-label="Password"
                           required>
                </label>

                <button id="passwordLoginButton" type="submit" data-i18n="auth.login.sign_in_button">Sign in</button>
            </form>

            <div id="status" class="status" data-i18n="auth.login.ready">Ready.</div>

            <div class="footer" data-i18n="auth.login.footer" data-brand-text="copyright">© Server 2026</div>
        `;

        const form = el("passwordLoginForm");
        const loginInput = el("passwordLoginName");
        const passwordInput = el("passwordLoginPassword");

        applyStaticI18n();
        applyLoginBranding();

        if (loginInput) loginInput.focus();

        form.addEventListener("submit", async (ev) => {
            ev.preventDefault();

            const login = String(loginInput.value || "").trim();
            const password = String(passwordInput.value || "");

            if (!login || !password) {
                setStatusKey("auth.login.enter_login_password", "Enter username/email and password.");
                return;
            }

            setBusy(true);
            setStatusKey("auth.login.signing_in", "Signing in…");

            try {
                const res = await fetch("/api/auth/password/login", {
                    method: "POST",
                    cache: "no-store",
                    credentials: "include",
                    headers: { "Content-Type": "application/json" },
                    body: JSON.stringify({ login, password })
                });

                const data = await res.json().catch(() => ({}));

                if (!res.ok || !data || data.ok === false) {
                    setBusy(false);
                    setStatusKey("auth.login.invalid_login_password", "Invalid login or password.");
                    return;
                }

                try {
                    localStorage.setItem("pqnas_password_login", login);
                } catch {}

                const ping = await fetch("/api/v4/me", {
                    cache: "no-store",
                    credentials: "include",
                });

                if (!ping.ok) {
                    setBusy(false);
                    setStatusKey("auth.login.cookie_failed", "Login OK, but session cookie did not stick.");
                    return;
                }

                window.location.href = await externalWorkspacePostLoginTarget();
            } catch (e) {
                console.error(e);
                setBusy(false);
                setStatusKey("auth.login.network_error", "Network error during login.");
            }
        });
    }


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

    function renderOpaqueLogin() {
        injectPasswordCss();

        const card = document.querySelector(".card");
        if (!card) {
            setStatusKey("auth.login.ui_missing", "Login UI missing");
            return;
        }

        card.innerHTML = `
            <div class="loginMark">
                <div class="pq-badge loginMarkBadge" data-brand-text="product_short_name">Server</div>
            </div>

            <h1 data-i18n="auth.opaque.title">Zero-knowledge sign in</h1>

            <div class="hint" data-i18n="auth.opaque.signin_hint">
                Sign in with OPAQUE. Your password is processed locally in this browser and is not sent to the server.
            </div>

            <div class="presentationLinkWrap" data-brand-hide-if-presentation-disabled>
                <a class="presentationLink"
                   href="/static/nexus-presentation/index.html"
                   target="_blank"
                   rel="noopener"
                   data-brand-presentation-link
                   data-i18n="auth.login.presentation_link">What is this service?</a>
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
            <div id="status" class="status" data-i18n="auth.opaque.ready">OPAQUE login ready.</div>

            <div class="footer" data-i18n="auth.login.footer" data-brand-text="copyright">© Server 2026</div>
        `;

        const form = el("opaqueLoginForm");
        const loginInput = el("opaqueLoginName");
        const passwordInput = el("opaqueLoginPassword");
        const button = el("opaqueLoginButton");

        applyStaticI18n();
        applyLoginBranding();

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

                window.location.href = await externalWorkspacePostLoginTarget();
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

    async function start() {
        let cfg = {};
        try {
            const res = await fetch("/api/auth/config", {
                cache: "no-store",
                credentials: "include"
            });
            cfg = await res.json().catch(() => ({}));
        } catch (e) {
            console.debug("auth config failed; falling back to QR", e);
        }

        const mode = String((cfg && cfg.mode) || "qr").toLowerCase();

        if (mode === PASSWORD_MODE) {
            renderPasswordLogin();
        } else if (mode === OPAQUE_MODE) {
            renderOpaqueLogin();
        } else {
            loadQrLogin();
        }
    }

    if (window.PQNAS_I18N && typeof window.PQNAS_I18N.ready === "function") {
        window.PQNAS_I18N.ready().then(start).catch(start);
    } else {
        start();
    }
})();


// pqnas-password-recovery-ui
//
// Adds "Forgot password?" recovery flow to the password login page.
// This is intentionally self-contained so login.html does not need another
// layout change. It is shown only when /api/auth/config reports password auth.
(() => {
    function ready(fn) {
        if (document.readyState === "loading") {
            document.addEventListener("DOMContentLoaded", fn, { once: true });
        } else {
            fn();
        }
    }

    function escapeHtml(s) {
        // Security: escape HTML text with one regex/callback instead of chained replaceAll.
        return String(s ?? "").replace(/[&<>"\']/g, (c) => ({
            "&": "&amp;",
            "<": "&lt;",
            ">": "&gt;",
            '"': "&quot;",
            "'": "&#39;",
        }[c]));
    }

    function findLoginHost() {
        const passwordInput =
            document.querySelector('input[type="password"][autocomplete="current-password"]') ||
            document.querySelector('input[type="password"]');

        if (!passwordInput) return null;

        const form = passwordInput.closest("form");
        if (form) return form;

        return passwordInput.closest(".card") ||
               passwordInput.closest(".panel") ||
               passwordInput.parentElement ||
               document.body;
    }

    function setRecoveryMessage(kind, text) {
        const box = document.getElementById("pqnasRecoveryMessage");
        if (!box) return;

        box.textContent = text || "";
        box.style.display = text ? "block" : "none";
        box.style.color = kind === "ok" ? "#2e7d32" : "#b00020";
    }

    function normalizeRecoveryWords(s) {
        return String(s || "")
            .trim()
            .toLowerCase()
            .replace(/\s+/g, " ");
    }

    async function recoverPassword() {
        const login = String(document.getElementById("pqnasRecoveryLogin")?.value || "").trim();
        const recovery_words = normalizeRecoveryWords(document.getElementById("pqnasRecoveryWords")?.value || "");
        const new_password = String(document.getElementById("pqnasRecoveryNewPassword")?.value || "");
        const confirm_password = String(document.getElementById("pqnasRecoveryConfirmPassword")?.value || "");

        if (!login) {
            throw new Error("Enter your login/email.");
        }

        if (!recovery_words) {
            throw new Error("Enter your 24 recovery words.");
        }

        const wordCount = recovery_words.split(" ").filter(Boolean).length;
        if (wordCount !== 24) {
            throw new Error("Recovery phrase must contain exactly 24 words.");
        }

        if (new_password.length < 12) {
            throw new Error("New password must be at least 12 characters.");
        }

        if (new_password !== confirm_password) {
            throw new Error("New password and confirmation do not match.");
        }

        const r = await fetch("/api/auth/password/recover", {
            method: "POST",
            credentials: "include",
            cache: "no-store",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({
                login,
                recovery_words,
                new_password
            })
        });

        const j = await r.json().catch(() => null);

        if (!r.ok || !j || j.ok === false) {
            throw new Error("Recovery failed. Check your login, recovery words and new password.");
        }

        return j;
    }

    function installRecoveryUi() {
        const host = findLoginHost();
        if (!host) return;

        if (document.getElementById("pqnasForgotPasswordBtn")) return;

        const wrapper = document.createElement("div");
        wrapper.id = "pqnasRecoveryWrap";
        wrapper.style.marginTop = "14px";
        wrapper.innerHTML = `
            <div style="text-align:center; margin-top:10px;">
                <button id="pqnasForgotPasswordBtn"
                        type="button"
                        style="border:0;background:transparent;cursor:pointer;text-decoration:underline;font:inherit;">
                    Forgot password?
                </button>
            </div>

            <div id="pqnasRecoveryPanel"
                 style="display:none; margin-top:14px; padding:14px; border:1px solid rgba(128,128,128,0.28); border-radius:14px;">
                <h3 style="margin:0 0 8px 0;">Recover password</h3>

                <p style="margin:0 0 12px 0; opacity:.78; line-height:1.45;">
                    Enter your login, 24 recovery words, and a new password.
                </p>

                <label style="display:block; margin-bottom:10px;">
                    <div style="font-size:12px; font-weight:700; opacity:.78; margin-bottom:4px;">Login / email</div>
                    <input id="pqnasRecoveryLogin"
                           type="text"
                           autocomplete="username"
                           style="width:100%; box-sizing:border-box;">
                </label>

                <label style="display:block; margin-bottom:10px;">
                    <div style="font-size:12px; font-weight:700; opacity:.78; margin-bottom:4px;">24 recovery words</div>
                    <textarea id="pqnasRecoveryWords"
                              autocomplete="off"
                              spellcheck="false"
                              style="width:100%; min-height:92px; box-sizing:border-box; resize:vertical;"></textarea>
                </label>

                <label style="display:block; margin-bottom:10px;">
                    <div style="font-size:12px; font-weight:700; opacity:.78; margin-bottom:4px;">New password</div>
                    <input id="pqnasRecoveryNewPassword"
                           type="password"
                           autocomplete="new-password"
                           minlength="12"
                           style="width:100%; box-sizing:border-box;">
                </label>

                <label style="display:block; margin-bottom:10px;">
                    <div style="font-size:12px; font-weight:700; opacity:.78; margin-bottom:4px;">Confirm new password</div>
                    <input id="pqnasRecoveryConfirmPassword"
                           type="password"
                           autocomplete="new-password"
                           minlength="12"
                           style="width:100%; box-sizing:border-box;">
                </label>

                <div id="pqnasRecoveryMessage"
                     style="display:none; margin:10px 0; line-height:1.45;"></div>

                <div style="display:flex; gap:10px; flex-wrap:wrap; margin-top:12px;">
                    <button id="pqnasRecoverySubmitBtn" type="button">Reset password</button>
                    <button id="pqnasRecoveryCancelBtn" type="button">Cancel</button>
                </div>
            </div>
        `;

        host.appendChild(wrapper);

        const loginHint = (() => {
            try { return localStorage.getItem("pqnas_password_login") || ""; } catch { return ""; }
        })();

        const loginInput =
            document.querySelector('input[autocomplete="username"]') ||
            document.querySelector('input[type="email"]') ||
            document.querySelector('input[type="text"]');

        const recoveryLogin = document.getElementById("pqnasRecoveryLogin");
        if (recoveryLogin) {
            recoveryLogin.value = loginHint || loginInput?.value || "";
        }

        const panel = document.getElementById("pqnasRecoveryPanel");
        const forgotBtn = document.getElementById("pqnasForgotPasswordBtn");
        const cancelBtn = document.getElementById("pqnasRecoveryCancelBtn");
        const submitBtn = document.getElementById("pqnasRecoverySubmitBtn");

        forgotBtn?.addEventListener("click", () => {
            if (!panel) return;
            panel.style.display = panel.style.display === "none" ? "block" : "none";
            setRecoveryMessage("", "");

            const latestLogin = String(loginInput?.value || "").trim();
            if (recoveryLogin && latestLogin && !recoveryLogin.value) {
                recoveryLogin.value = latestLogin;
            }
        });

        cancelBtn?.addEventListener("click", () => {
            if (panel) panel.style.display = "none";
            setRecoveryMessage("", "");
        });

        submitBtn?.addEventListener("click", async () => {
            const oldText = submitBtn.textContent;
            submitBtn.disabled = true;
            submitBtn.textContent = "Resetting…";
            setRecoveryMessage("", "");

            try {
                const j = await recoverPassword();

                try {
                    const login = String(document.getElementById("pqnasRecoveryLogin")?.value || "").trim();
                    if (login) localStorage.setItem("pqnas_password_login", login);
                } catch {}

                document.getElementById("pqnasRecoveryWords").value = "";
                document.getElementById("pqnasRecoveryNewPassword").value = "";
                document.getElementById("pqnasRecoveryConfirmPassword").value = "";

                const suffix = j.login_allowed === false
                    ? " Password was reset, but the account is not enabled yet."
                    : " You can now sign in with your new password.";

                setRecoveryMessage("ok", "Password reset." + suffix);
            } catch (e) {
                setRecoveryMessage("err", String(e && e.message ? e.message : e));
            } finally {
                submitBtn.disabled = false;
                submitBtn.textContent = oldText;
            }
        });
    }

    ready(async () => {
        try {
            const r = await fetch("/api/auth/config", {
                credentials: "include",
                cache: "no-store"
            });

            const j = await r.json().catch(() => null);
            if (!r.ok || !j || !j.password_enabled) return;

            installRecoveryUi();
        } catch {
            // Do not break login if config lookup fails.
        }
    });
})();


// pqnas-password-recovery-ui-retry-v2
//
// The password login card may be rendered after DOMContentLoaded.
// This retry installer waits until the password field exists, then installs
// the recovery UI if the first installer did not already do it.
(() => {
    function recoveryAlreadyInstalled() {
        return !!document.getElementById("pqnasForgotPasswordBtn");
    }

    function isPasswordAuthPageReady() {
        return !!document.querySelector('input[type="password"]');
    }

    async function passwordAuthEnabled() {
        try {
            const r = await fetch("/api/auth/config", {
                credentials: "include",
                cache: "no-store"
            });

            const j = await r.json().catch(() => null);
            return !!(r.ok && j && j.password_enabled);
        } catch {
            return false;
        }
    }

    function escapeHtml(s) {
        // Security: escape HTML text with one regex/callback instead of chained replaceAll.
        return String(s ?? "").replace(/[&<>"\']/g, (c) => ({
            "&": "&amp;",
            "<": "&lt;",
            ">": "&gt;",
            '"': "&quot;",
            "'": "&#39;",
        }[c]));
    }

    function findHost() {
        const passwordInput = document.querySelector('input[type="password"]');
        if (!passwordInput) return null;

        return passwordInput.closest("form") ||
               passwordInput.closest(".card") ||
               passwordInput.closest(".login-card") ||
               passwordInput.closest("[class*='card']") ||
               passwordInput.parentElement ||
               document.body;
    }

    function normalizeRecoveryWords(s) {
        return String(s || "").trim().toLowerCase().replace(/\s+/g, " ");
    }

    function setMsg(kind, text) {
        const box = document.getElementById("pqnasRecoveryMessage");
        if (!box) return;

        box.textContent = text || "";
        box.style.display = text ? "block" : "none";
        box.style.color = kind === "ok" ? "#2e7d32" : "#b00020";
    }

    async function submitRecovery() {
        const login = String(document.getElementById("pqnasRecoveryLogin")?.value || "").trim();
        const recovery_words = normalizeRecoveryWords(document.getElementById("pqnasRecoveryWords")?.value || "");
        const new_password = String(document.getElementById("pqnasRecoveryNewPassword")?.value || "");
        const confirm_password = String(document.getElementById("pqnasRecoveryConfirmPassword")?.value || "");

        if (!login) throw new Error("Enter your login/email.");
        if (!recovery_words) throw new Error("Enter your 24 recovery words.");

        const wordCount = recovery_words.split(" ").filter(Boolean).length;
        if (wordCount !== 24) throw new Error("Recovery phrase must contain exactly 24 words.");

        if (new_password.length < 12) throw new Error("New password must be at least 12 characters.");
        if (new_password !== confirm_password) throw new Error("New password and confirmation do not match.");

        const r = await fetch("/api/auth/password/recover", {
            method: "POST",
            credentials: "include",
            cache: "no-store",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ login, recovery_words, new_password })
        });

        const j = await r.json().catch(() => null);
        if (!r.ok || !j || j.ok === false) {
            throw new Error("Recovery failed. Check your login, recovery words and new password.");
        }

        return j;
    }

    function installRecoveryUiV2() {
        if (recoveryAlreadyInstalled()) return true;

        const host = findHost();
        if (!host) return false;

        const wrap = document.createElement("div");
        wrap.id = "pqnasRecoveryWrapV2";
        wrap.style.marginTop = "14px";

        wrap.innerHTML = `
            <div style="text-align:center; margin-top:10px;">
                <button id="pqnasForgotPasswordBtn"
                        type="button"
                        style="border:0;background:transparent;cursor:pointer;text-decoration:underline;font:inherit;color:#0057b8;">
                    Forgot password?
                </button>
            </div>

            <div id="pqnasRecoveryPanel"
                 style="display:none; margin-top:14px; padding:14px; border:1px solid rgba(128,128,128,0.28); border-radius:14px;">
                <h3 style="margin:0 0 8px 0;">Recover password</h3>

                <p style="margin:0 0 12px 0; opacity:.78; line-height:1.45;">
                    Enter your login, 24 recovery words, and a new password.
                </p>

                <label style="display:block; margin-bottom:10px;">
                    <div style="font-size:12px; font-weight:700; opacity:.78; margin-bottom:4px;">Login / email</div>
                    <input id="pqnasRecoveryLogin" type="text" autocomplete="username" style="width:100%; box-sizing:border-box;">
                </label>

                <label style="display:block; margin-bottom:10px;">
                    <div style="font-size:12px; font-weight:700; opacity:.78; margin-bottom:4px;">24 recovery words</div>
                    <textarea id="pqnasRecoveryWords"
                              autocomplete="off"
                              spellcheck="false"
                              style="width:100%; min-height:92px; box-sizing:border-box; resize:vertical;"></textarea>
                </label>

                <label style="display:block; margin-bottom:10px;">
                    <div style="font-size:12px; font-weight:700; opacity:.78; margin-bottom:4px;">New password</div>
                    <input id="pqnasRecoveryNewPassword" type="password" autocomplete="new-password" minlength="12" style="width:100%; box-sizing:border-box;">
                </label>

                <label style="display:block; margin-bottom:10px;">
                    <div style="font-size:12px; font-weight:700; opacity:.78; margin-bottom:4px;">Confirm new password</div>
                    <input id="pqnasRecoveryConfirmPassword" type="password" autocomplete="new-password" minlength="12" style="width:100%; box-sizing:border-box;">
                </label>

                <div id="pqnasRecoveryMessage" style="display:none; margin:10px 0; line-height:1.45;"></div>

                <div style="display:flex; gap:10px; flex-wrap:wrap; margin-top:12px;">
                    <button id="pqnasRecoverySubmitBtn" type="button">Reset password</button>
                    <button id="pqnasRecoveryCancelBtn" type="button">Cancel</button>
                </div>
            </div>
        `;

        host.appendChild(wrap);

        const loginInput =
            document.querySelector('input[autocomplete="username"]') ||
            document.querySelector('input[type="email"]') ||
            document.querySelector('input[type="text"]');

        const recoveryLogin = document.getElementById("pqnasRecoveryLogin");
        if (recoveryLogin) {
            try {
                recoveryLogin.value =
                    localStorage.getItem("pqnas_password_login") ||
                    loginInput?.value ||
                    "";
            } catch {
                recoveryLogin.value = loginInput?.value || "";
            }
        }

        const panel = document.getElementById("pqnasRecoveryPanel");
        const forgotBtn = document.getElementById("pqnasForgotPasswordBtn");
        const cancelBtn = document.getElementById("pqnasRecoveryCancelBtn");
        const submitBtn = document.getElementById("pqnasRecoverySubmitBtn");

        forgotBtn?.addEventListener("click", () => {
            if (!panel) return;
            panel.style.display = panel.style.display === "none" ? "block" : "none";
            setMsg("", "");

            const latestLogin = String(loginInput?.value || "").trim();
            if (recoveryLogin && latestLogin && !recoveryLogin.value) {
                recoveryLogin.value = latestLogin;
            }
        });

        cancelBtn?.addEventListener("click", () => {
            if (panel) panel.style.display = "none";
            setMsg("", "");
        });

        submitBtn?.addEventListener("click", async () => {
            const oldText = submitBtn.textContent;
            submitBtn.disabled = true;
            submitBtn.textContent = "Resetting…";
            setMsg("", "");

            try {
                const j = await submitRecovery();

                try {
                    const login = String(document.getElementById("pqnasRecoveryLogin")?.value || "").trim();
                    if (login) localStorage.setItem("pqnas_password_login", login);
                } catch {}

                const words = document.getElementById("pqnasRecoveryWords");
                const pw1 = document.getElementById("pqnasRecoveryNewPassword");
                const pw2 = document.getElementById("pqnasRecoveryConfirmPassword");
                if (words) words.value = "";
                if (pw1) pw1.value = "";
                if (pw2) pw2.value = "";

                const suffix = j.login_allowed === false
                    ? " Password was reset, but the account is not enabled yet."
                    : " You can now sign in with your new password.";

                setMsg("ok", "Password reset." + suffix);
            } catch (e) {
                setMsg("err", String(e && e.message ? e.message : e));
            } finally {
                submitBtn.disabled = false;
                submitBtn.textContent = oldText;
            }
        });

        return true;
    }

    async function startRecoveryRetryInstaller() {
        if (!(await passwordAuthEnabled())) return;

        let tries = 0;
        const maxTries = 80;

        const tick = () => {
            tries += 1;

            if (recoveryAlreadyInstalled()) return true;
            if (isPasswordAuthPageReady() && installRecoveryUiV2()) return true;

            return tries >= maxTries;
        };

        if (tick()) return;

        const timer = setInterval(() => {
            if (tick()) {
                clearInterval(timer);
            }
        }, 250);
    }

    if (document.readyState === "loading") {
        document.addEventListener("DOMContentLoaded", startRecoveryRetryInstaller, { once: true });
    } else {
        startRecoveryRetryInstaller();
    }
})();


// pqnas-password-recovery-i18n-v1
(() => {
    function t(key, fallback) {
        const api = window.PQNAS_I18N;
        if (api && typeof api.t === "function") {
            return api.t(key, null, fallback);
        }
        return fallback;
    }

    function apply() {
        const forgot = document.getElementById("pqnasForgotPasswordBtn");
        if (forgot) forgot.textContent = t("auth.recovery.forgot", "Forgot password?");

        const panel = document.getElementById("pqnasRecoveryPanel");
        if (!panel) return;

        const h3 = panel.querySelector("h3");
        if (h3) h3.textContent = t("auth.recovery.title", "Recover password");

        const desc = panel.querySelector("p");
        if (desc) desc.textContent = t("auth.recovery.desc", "Enter your login, 24 recovery words, and a new password.");

        const labels = panel.querySelectorAll("label > div:first-child");
        if (labels[0]) labels[0].textContent = t("auth.recovery.login", "Login / email");
        if (labels[1]) labels[1].textContent = t("auth.recovery.words", "24 recovery words");
        if (labels[2]) labels[2].textContent = t("auth.recovery.new_password", "New password");
        if (labels[3]) labels[3].textContent = t("auth.recovery.confirm_password", "Confirm new password");

        const submit = document.getElementById("pqnasRecoverySubmitBtn");
        if (submit && submit.textContent !== "Resetting…") {
            submit.textContent = t("auth.recovery.reset", "Reset password");
        }

        const cancel = document.getElementById("pqnasRecoveryCancelBtn");
        if (cancel) cancel.textContent = t("auth.recovery.cancel", "Cancel");
    }

    let tries = 0;
    const timer = setInterval(() => {
        tries += 1;
        apply();
        if (document.getElementById("pqnasForgotPasswordBtn") || tries > 80) {
            clearInterval(timer);
        }
    }, 250);

    if (document.readyState === "loading") {
        document.addEventListener("DOMContentLoaded", apply, { once: true });
    } else {
        apply();
    }
})();


// pqnas-base-login-i18n-v1
(() => {
    function t(key, fallback) {
        const api = window.PQNAS_I18N;
        if (api && typeof api.t === "function") {
            return api.t(key, null, fallback);
        }
        return fallback;
    }

    // pqnas-base-login-i18n-preserve-inputs-v2
    function findTextElementByExactText(text) {
        const wanted = String(text).trim();

        // Do NOT include <label> here. Some login labels contain the <input>
        // as a child, and setting label.textContent would remove that input.
        const all = Array.from(document.querySelectorAll("h1,h2,h3,p,div,span"));
        return all.find(el => {
            if (el.querySelector && el.querySelector("a,input,textarea,select,button")) {
                return false;
            }
            return String(el.textContent || "").trim() === wanted;
        }) || null;
    }

    function setExact(oldText, key, fallback) {
        const el = findTextElementByExactText(oldText);
        if (el) el.textContent = t(key, fallback);
    }

    function setLoginLabel(oldText, key, fallback) {
        const labels = Array.from(document.querySelectorAll("label"));
        const label = labels.find(el => String(el.textContent || "").trim() === oldText);
        if (!label) return;

        const translated = t(key, fallback);

        // Prefer replacing an existing text node so child inputs survive.
        for (const node of Array.from(label.childNodes)) {
            if (node.nodeType === Node.TEXT_NODE && String(node.nodeValue || "").trim() === oldText) {
                node.nodeValue = translated;
                return;
            }
        }

        // If the label has an element for the visible text, update only that.
        for (const child of Array.from(label.children)) {
            if (!/^(INPUT|TEXTAREA|SELECT|BUTTON)$/i.test(child.tagName) &&
                String(child.textContent || "").trim() === oldText) {
                child.textContent = translated;
                return;
            }
        }

        // Last resort: insert a text node before the first form control.
        const control = label.querySelector("input,textarea,select,button");
        if (control) {
            label.insertBefore(document.createTextNode(translated), control);
        }
    }

    function applyBaseLoginI18n() {
        setExact("Sign in", "auth.login.title", "Sign in");
        setExact("Use your username or email address.", "auth.login.subtitle", "Use your username or email address.");
        setExact("What is this service?", "auth.login.what_is", "What is this service?");
        setLoginLabel("Email / username", "auth.login.email", "Email / username");
        setLoginLabel("Password", "auth.login.password", "Password");

        const buttons = Array.from(document.querySelectorAll("button"));
        for (const b of buttons) {
            const txt = String(b.textContent || "").trim();
            if (txt === "Sign in") {
                b.textContent = t("auth.login.submit", "Sign in");
            } else if (txt === "Signing in…") {
                b.textContent = t("auth.login.signing_in", "Signing in…");
            }
        }

        const all = Array.from(document.querySelectorAll("div,span,p"));
        for (const el of all) {
            const txt = String(el.textContent || "").trim();
            if (txt === "Ready.") {
                el.textContent = t("auth.login.ready", "Ready.");
            } else if (txt === "Invalid login or password.") {
                el.textContent = t("auth.login.invalid", "Invalid login or password.");
            }
        }
    }

    let tries = 0;
    const timer = setInterval(() => {
        tries += 1;
        applyBaseLoginI18n();
        if (document.querySelector('input[type="password"]') || tries > 80) {
            clearInterval(timer);
        }
    }, 250);

    if (document.readyState === "loading") {
        document.addEventListener("DOMContentLoaded", applyBaseLoginI18n, { once: true });
    } else {
        applyBaseLoginI18n();
    }
})();
