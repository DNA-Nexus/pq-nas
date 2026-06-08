(() => {
    "use strict";

    const QR_SCRIPT = "/static/pqnas_v5.js?v=20260518-login-i18n-1";
    const PASSWORD_MODE = "password";

    const el = (id) => document.getElementById(id);

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

    function setBusy(isBusy) {
        document.body.classList.toggle("busy", !!isBusy);
    }

    function setStatus(msg) {
        const status = el("status");
        if (status) status.textContent = msg;
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
            setStatus("Login UI missing");
            return;
        }

        card.innerHTML = `
            <div class="loginMark">
                <div class="loginMarkBadge">DNA-Nexus</div>
            </div>

            <h1>Sign in</h1>

            <div class="hint">
                Use your DNA-Nexus username or email address.
            </div>

            <div class="presentationLinkWrap">
                <a class="presentationLink"
                   href="/static/nexus-presentation/index.html"
                   target="_blank"
                   rel="noopener">What is DNA-Nexus?</a>
            </div>

            <form id="passwordLoginForm" class="passwordForm" autocomplete="on">
                <label>
                    Email / username
                    <input id="passwordLoginName"
                           name="username"
                           type="text"
                           inputmode="email"
                           autocomplete="username"
                           maxlength="254"
                           required>
                </label>

                <label>
                    Password
                    <input id="passwordLoginPassword"
                           name="password"
                           type="password"
                           autocomplete="current-password"
                           maxlength="1024"
                           required>
                </label>

                <button id="passwordLoginButton" type="submit">Sign in</button>
            </form>

            <div id="status" class="status">Ready.</div>

            <div class="footer">© CPUNK 2026 · DNA-Nexus</div>
        `;

        const form = el("passwordLoginForm");
        const loginInput = el("passwordLoginName");
        const passwordInput = el("passwordLoginPassword");

        if (loginInput) loginInput.focus();

        form.addEventListener("submit", async (ev) => {
            ev.preventDefault();

            const login = String(loginInput.value || "").trim();
            const password = String(passwordInput.value || "");

            if (!login || !password) {
                setStatus("Enter username/email and password.");
                return;
            }

            setBusy(true);
            setStatus("Signing in…");

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
                    setStatus("Invalid login or password.");
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
                    setStatus("Login OK, but session cookie did not stick.");
                    return;
                }

                window.location.href = "/app";
            } catch (e) {
                console.error(e);
                setBusy(false);
                setStatus("Network error during login.");
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
        return String(s ?? "")
            .replaceAll("&", "&amp;")
            .replaceAll("<", "&lt;")
            .replaceAll(">", "&gt;")
            .replaceAll('"', "&quot;")
            .replaceAll("'", "&#39;");
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
        return String(s ?? "")
            .replaceAll("&", "&amp;")
            .replaceAll("<", "&lt;")
            .replaceAll(">", "&gt;")
            .replaceAll('"', "&quot;")
            .replaceAll("'", "&#39;");
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
