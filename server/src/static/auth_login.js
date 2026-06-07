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
