#!/usr/bin/env python3
from pathlib import Path
import json
import sys

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
    p(rel).write_text(text, encoding="utf-8")
    print(f"patched: {rel}")

def replace_once(rel: str, old: str, new: str) -> None:
    text = read(rel)
    if new in text:
        print(f"unchanged: {rel}")
        return
    if old not in text:
        die(f"anchor not found in {rel}")
    write(rel, text.replace(old, new, 1))

auth = "server/src/static/auth_login.js"

replace_once(
    auth,
    '''    function setStatus(msg) {
        const status = el("status");
        if (status) status.textContent = msg;
    }
''',
    '''    function setStatus(msg) {
        const status = el("status");
        if (status) status.textContent = msg;
    }

    function setStatusKey(key, fallback, vars) {
        setStatus(tr(key, vars || null, fallback));
    }
''',
)

replace_once(
    auth,
    '''            <h1>Sign in</h1>

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
''',
    '''            <h1 data-i18n="auth.login.title">Sign in</h1>

            <div class="hint" data-i18n="auth.login.password_hint">
                Use your DNA-Nexus username or email address.
            </div>

            <div class="presentationLinkWrap">
                <a class="presentationLink"
                   href="/static/nexus-presentation/index.html"
                   target="_blank"
                   rel="noopener"
                   data-i18n="auth.login.presentation_link">What is DNA-Nexus?</a>
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

            <div class="footer" data-i18n="auth.login.footer">© CPUNK 2026 · DNA-Nexus</div>
''',
)

replace_once(
    auth,
    '''        if (loginInput) loginInput.focus();
''',
    '''        applyStaticI18n();

        if (loginInput) loginInput.focus();
''',
)

replace_once(
    auth,
    '''                setStatus("Enter username/email and password.");
''',
    '''                setStatusKey("auth.login.enter_login_password", "Enter username/email and password.");
''',
)

replace_once(
    auth,
    '''            setStatus("Signing in…");
''',
    '''            setStatusKey("auth.login.signing_in", "Signing in…");
''',
)

replace_once(
    auth,
    '''                    setStatus("Invalid login or password.");
''',
    '''                    setStatusKey("auth.login.invalid_login_password", "Invalid login or password.");
''',
)

replace_once(
    auth,
    '''                    setStatus("Login OK, but session cookie did not stick.");
''',
    '''                    setStatusKey("auth.login.cookie_failed", "Login OK, but session cookie did not stick.");
''',
)

replace_once(
    auth,
    '''                setStatus("Network error during login.");
''',
    '''                setStatusKey("auth.login.network_error", "Network error during login.");
''',
)

replace_once(
    auth,
    '''            setStatus("Login UI missing");
''',
    '''            setStatusKey("auth.login.ui_missing", "Login UI missing");
''',
)

replace_once(
    auth,
    '''            <h1>Zero-knowledge sign in</h1>

            <div class="hint">
                OPAQUE login is selected for this server, but the browser-side OPAQUE crypto module is not installed in this build yet.
            </div>

            <div class="hint">
                For safety, this page will not send your password to the server as a fallback.
            </div>

            <div id="status" class="status">OPAQUE backend not configured.</div>

            <div class="footer">© CPUNK 2026 · DNA-Nexus</div>
''',
    '''            <h1 data-i18n="auth.opaque.title">Zero-knowledge sign in</h1>

            <div class="hint" data-i18n="auth.opaque.client_missing_hint">
                OPAQUE login is selected for this server, but the browser-side OPAQUE crypto module is not installed in this build yet.
            </div>

            <div class="hint" data-i18n="auth.opaque.no_password_fallback_hint">
                For safety, this page will not send your password to the server as a fallback.
            </div>

            <div id="status" class="status" data-i18n="auth.opaque.client_missing_status">OPAQUE client module not available.</div>

            <div class="footer" data-i18n="auth.login.footer">© CPUNK 2026 · DNA-Nexus</div>
''',
)

replace_once(
    auth,
    '''        `;
    }
''',
    '''        `;

        applyStaticI18n();
    }
''',
)

# Add i18n keys to all supported language files.
translations = {
    "en": {
        "auth.login.title": "Sign in",
        "auth.login.password_hint": "Use your DNA-Nexus username or email address.",
        "auth.login.presentation_link": "What is DNA-Nexus?",
        "auth.login.username_label": "Email / username",
        "auth.login.password_label": "Password",
        "auth.login.sign_in_button": "Sign in",
        "auth.login.ready": "Ready.",
        "auth.login.footer": "© CPUNK 2026 · DNA-Nexus",
        "auth.login.enter_login_password": "Enter username/email and password.",
        "auth.login.signing_in": "Signing in…",
        "auth.login.invalid_login_password": "Invalid login or password.",
        "auth.login.cookie_failed": "Login OK, but session cookie did not stick.",
        "auth.login.network_error": "Network error during login.",
        "auth.login.ui_missing": "Login UI missing",
        "auth.opaque.title": "Zero-knowledge sign in",
        "auth.opaque.client_missing_hint": "OPAQUE login is selected for this server, but the browser-side OPAQUE crypto module is not installed in this build yet.",
        "auth.opaque.no_password_fallback_hint": "For safety, this page will not send your password to the server as a fallback.",
        "auth.opaque.client_missing_status": "OPAQUE client module not available."
    },
    "fi": {
        "auth.login.title": "Kirjaudu sisään",
        "auth.login.password_hint": "Käytä DNA-Nexus-käyttäjänimeäsi tai sähköpostiosoitettasi.",
        "auth.login.presentation_link": "Mikä DNA-Nexus on?",
        "auth.login.username_label": "Sähköposti / käyttäjänimi",
        "auth.login.password_label": "Salasana",
        "auth.login.sign_in_button": "Kirjaudu sisään",
        "auth.login.ready": "Valmis.",
        "auth.login.footer": "© CPUNK 2026 · DNA-Nexus",
        "auth.login.enter_login_password": "Anna käyttäjänimi/sähköposti ja salasana.",
        "auth.login.signing_in": "Kirjaudutaan…",
        "auth.login.invalid_login_password": "Virheellinen käyttäjätunnus tai salasana.",
        "auth.login.cookie_failed": "Kirjautuminen onnistui, mutta istuntoeväste ei tallentunut.",
        "auth.login.network_error": "Verkkovirhe kirjautumisen aikana.",
        "auth.login.ui_missing": "Kirjautumisnäkymä puuttuu",
        "auth.opaque.title": "Nollatietokirjautuminen",
        "auth.opaque.client_missing_hint": "Tämä palvelin käyttää OPAQUE-kirjautumista, mutta selaimen OPAQUE-salausmoduulia ei ole vielä tässä versiossa.",
        "auth.opaque.no_password_fallback_hint": "Turvallisuuden vuoksi tämä sivu ei lähetä salasanaasi palvelimelle varamenettelynä.",
        "auth.opaque.client_missing_status": "OPAQUE-asiakasmoduuli ei ole käytettävissä."
    },
    "sv": {
        "auth.login.title": "Logga in",
        "auth.login.password_hint": "Använd ditt DNA-Nexus-användarnamn eller din e-postadress.",
        "auth.login.presentation_link": "Vad är DNA-Nexus?",
        "auth.login.username_label": "E-post / användarnamn",
        "auth.login.password_label": "Lösenord",
        "auth.login.sign_in_button": "Logga in",
        "auth.login.ready": "Klar.",
        "auth.login.footer": "© CPUNK 2026 · DNA-Nexus",
        "auth.login.enter_login_password": "Ange användarnamn/e-post och lösenord.",
        "auth.login.signing_in": "Loggar in…",
        "auth.login.invalid_login_password": "Felaktig inloggning eller lösenord.",
        "auth.login.cookie_failed": "Inloggningen lyckades, men sessionscookien sparades inte.",
        "auth.login.network_error": "Nätverksfel vid inloggning.",
        "auth.login.ui_missing": "Inloggningsvyn saknas",
        "auth.opaque.title": "Nollkunskapsinloggning",
        "auth.opaque.client_missing_hint": "OPAQUE-inloggning är vald för denna server, men webbläsarens OPAQUE-kryptomodul är ännu inte installerad i denna version.",
        "auth.opaque.no_password_fallback_hint": "Av säkerhetsskäl skickar denna sida inte ditt lösenord till servern som reservlösning.",
        "auth.opaque.client_missing_status": "OPAQUE-klientmodulen är inte tillgänglig."
    },
    "de": {
        "auth.login.title": "Anmelden",
        "auth.login.password_hint": "Verwende deinen DNA-Nexus-Benutzernamen oder deine E-Mail-Adresse.",
        "auth.login.presentation_link": "Was ist DNA-Nexus?",
        "auth.login.username_label": "E-Mail / Benutzername",
        "auth.login.password_label": "Passwort",
        "auth.login.sign_in_button": "Anmelden",
        "auth.login.ready": "Bereit.",
        "auth.login.footer": "© CPUNK 2026 · DNA-Nexus",
        "auth.login.enter_login_password": "Benutzernamen/E-Mail und Passwort eingeben.",
        "auth.login.signing_in": "Anmeldung läuft…",
        "auth.login.invalid_login_password": "Ungültige Anmeldung oder ungültiges Passwort.",
        "auth.login.cookie_failed": "Anmeldung erfolgreich, aber das Sitzungscookie wurde nicht gespeichert.",
        "auth.login.network_error": "Netzwerkfehler bei der Anmeldung.",
        "auth.login.ui_missing": "Anmeldeansicht fehlt",
        "auth.opaque.title": "Zero-Knowledge-Anmeldung",
        "auth.opaque.client_missing_hint": "OPAQUE-Anmeldung ist für diesen Server ausgewählt, aber das browserseitige OPAQUE-Kryptomodul ist in diesem Build noch nicht installiert.",
        "auth.opaque.no_password_fallback_hint": "Aus Sicherheitsgründen sendet diese Seite dein Passwort nicht als Fallback an den Server.",
        "auth.opaque.client_missing_status": "OPAQUE-Clientmodul nicht verfügbar."
    },
    "et": {
        "auth.login.title": "Logi sisse",
        "auth.login.password_hint": "Kasuta oma DNA-Nexus kasutajanime või e-posti aadressi.",
        "auth.login.presentation_link": "Mis on DNA-Nexus?",
        "auth.login.username_label": "E-post / kasutajanimi",
        "auth.login.password_label": "Parool",
        "auth.login.sign_in_button": "Logi sisse",
        "auth.login.ready": "Valmis.",
        "auth.login.footer": "© CPUNK 2026 · DNA-Nexus",
        "auth.login.enter_login_password": "Sisesta kasutajanimi/e-post ja parool.",
        "auth.login.signing_in": "Sisselogimine…",
        "auth.login.invalid_login_password": "Vale kasutaja või parool.",
        "auth.login.cookie_failed": "Sisselogimine õnnestus, kuid seansiküpsis ei salvestunud.",
        "auth.login.network_error": "Võrguviga sisselogimisel.",
        "auth.login.ui_missing": "Sisselogimisvaade puudub",
        "auth.opaque.title": "Nullteadmisega sisselogimine",
        "auth.opaque.client_missing_hint": "Selle serveri jaoks on valitud OPAQUE sisselogimine, kuid brauseripoolne OPAQUE krüptomoodul pole selles versioonis veel paigaldatud.",
        "auth.opaque.no_password_fallback_hint": "Turvalisuse huvides ei saada see leht sinu parooli serverile varuvariandina.",
        "auth.opaque.client_missing_status": "OPAQUE kliendimoodul pole saadaval."
    },
    "pl": {
        "auth.login.title": "Zaloguj się",
        "auth.login.password_hint": "Użyj nazwy użytkownika DNA-Nexus lub adresu e-mail.",
        "auth.login.presentation_link": "Czym jest DNA-Nexus?",
        "auth.login.username_label": "E-mail / nazwa użytkownika",
        "auth.login.password_label": "Hasło",
        "auth.login.sign_in_button": "Zaloguj się",
        "auth.login.ready": "Gotowe.",
        "auth.login.footer": "© CPUNK 2026 · DNA-Nexus",
        "auth.login.enter_login_password": "Podaj nazwę użytkownika/e-mail i hasło.",
        "auth.login.signing_in": "Logowanie…",
        "auth.login.invalid_login_password": "Nieprawidłowy login lub hasło.",
        "auth.login.cookie_failed": "Logowanie się powiodło, ale ciasteczko sesji nie zostało zapisane.",
        "auth.login.network_error": "Błąd sieci podczas logowania.",
        "auth.login.ui_missing": "Brak widoku logowania",
        "auth.opaque.title": "Logowanie zero-knowledge",
        "auth.opaque.client_missing_hint": "Dla tego serwera wybrano logowanie OPAQUE, ale moduł kryptograficzny OPAQUE po stronie przeglądarki nie jest jeszcze zainstalowany w tej wersji.",
        "auth.opaque.no_password_fallback_hint": "Ze względów bezpieczeństwa ta strona nie wyśle hasła na serwer jako metody awaryjnej.",
        "auth.opaque.client_missing_status": "Moduł klienta OPAQUE jest niedostępny."
    },
    "es": {
        "auth.login.title": "Iniciar sesión",
        "auth.login.password_hint": "Usa tu nombre de usuario o correo de DNA-Nexus.",
        "auth.login.presentation_link": "¿Qué es DNA-Nexus?",
        "auth.login.username_label": "Correo / usuario",
        "auth.login.password_label": "Contraseña",
        "auth.login.sign_in_button": "Iniciar sesión",
        "auth.login.ready": "Listo.",
        "auth.login.footer": "© CPUNK 2026 · DNA-Nexus",
        "auth.login.enter_login_password": "Introduce usuario/correo y contraseña.",
        "auth.login.signing_in": "Iniciando sesión…",
        "auth.login.invalid_login_password": "Usuario o contraseña incorrectos.",
        "auth.login.cookie_failed": "El inicio de sesión funcionó, pero la cookie de sesión no se guardó.",
        "auth.login.network_error": "Error de red durante el inicio de sesión.",
        "auth.login.ui_missing": "Falta la vista de inicio de sesión",
        "auth.opaque.title": "Inicio de sesión de conocimiento cero",
        "auth.opaque.client_missing_hint": "Este servidor usa inicio de sesión OPAQUE, pero el módulo criptográfico OPAQUE del navegador aún no está instalado en esta compilación.",
        "auth.opaque.no_password_fallback_hint": "Por seguridad, esta página no enviará tu contraseña al servidor como alternativa.",
        "auth.opaque.client_missing_status": "El módulo cliente OPAQUE no está disponible."
    },
    "fr": {
        "auth.login.title": "Se connecter",
        "auth.login.password_hint": "Utilisez votre nom d’utilisateur ou adresse e-mail DNA-Nexus.",
        "auth.login.presentation_link": "Qu’est-ce que DNA-Nexus ?",
        "auth.login.username_label": "E-mail / nom d’utilisateur",
        "auth.login.password_label": "Mot de passe",
        "auth.login.sign_in_button": "Se connecter",
        "auth.login.ready": "Prêt.",
        "auth.login.footer": "© CPUNK 2026 · DNA-Nexus",
        "auth.login.enter_login_password": "Saisissez votre identifiant/e-mail et votre mot de passe.",
        "auth.login.signing_in": "Connexion…",
        "auth.login.invalid_login_password": "Identifiant ou mot de passe invalide.",
        "auth.login.cookie_failed": "Connexion réussie, mais le cookie de session n’a pas été enregistré.",
        "auth.login.network_error": "Erreur réseau pendant la connexion.",
        "auth.login.ui_missing": "Interface de connexion manquante",
        "auth.opaque.title": "Connexion à divulgation nulle",
        "auth.opaque.client_missing_hint": "La connexion OPAQUE est sélectionnée pour ce serveur, mais le module cryptographique OPAQUE côté navigateur n’est pas encore installé dans cette version.",
        "auth.opaque.no_password_fallback_hint": "Par sécurité, cette page n’enverra pas votre mot de passe au serveur comme solution de secours.",
        "auth.opaque.client_missing_status": "Module client OPAQUE non disponible."
    },
    "it": {
        "auth.login.title": "Accedi",
        "auth.login.password_hint": "Usa il tuo nome utente o indirizzo e-mail DNA-Nexus.",
        "auth.login.presentation_link": "Che cos’è DNA-Nexus?",
        "auth.login.username_label": "E-mail / nome utente",
        "auth.login.password_label": "Password",
        "auth.login.sign_in_button": "Accedi",
        "auth.login.ready": "Pronto.",
        "auth.login.footer": "© CPUNK 2026 · DNA-Nexus",
        "auth.login.enter_login_password": "Inserisci nome utente/e-mail e password.",
        "auth.login.signing_in": "Accesso in corso…",
        "auth.login.invalid_login_password": "Login o password non validi.",
        "auth.login.cookie_failed": "Accesso riuscito, ma il cookie di sessione non è stato salvato.",
        "auth.login.network_error": "Errore di rete durante l’accesso.",
        "auth.login.ui_missing": "Vista di accesso mancante",
        "auth.opaque.title": "Accesso zero-knowledge",
        "auth.opaque.client_missing_hint": "Per questo server è selezionato l’accesso OPAQUE, ma il modulo crittografico OPAQUE lato browser non è ancora installato in questa build.",
        "auth.opaque.no_password_fallback_hint": "Per sicurezza, questa pagina non invierà la tua password al server come fallback.",
        "auth.opaque.client_missing_status": "Modulo client OPAQUE non disponibile."
    },
    "tr": {
        "auth.login.title": "Oturum aç",
        "auth.login.password_hint": "DNA-Nexus kullanıcı adınızı veya e-posta adresinizi kullanın.",
        "auth.login.presentation_link": "DNA-Nexus nedir?",
        "auth.login.username_label": "E-posta / kullanıcı adı",
        "auth.login.password_label": "Parola",
        "auth.login.sign_in_button": "Oturum aç",
        "auth.login.ready": "Hazır.",
        "auth.login.footer": "© CPUNK 2026 · DNA-Nexus",
        "auth.login.enter_login_password": "Kullanıcı adı/e-posta ve parola girin.",
        "auth.login.signing_in": "Oturum açılıyor…",
        "auth.login.invalid_login_password": "Geçersiz kullanıcı veya parola.",
        "auth.login.cookie_failed": "Oturum açıldı, ancak oturum çerezi kaydedilmedi.",
        "auth.login.network_error": "Oturum açma sırasında ağ hatası.",
        "auth.login.ui_missing": "Oturum açma görünümü eksik",
        "auth.opaque.title": "Sıfır bilgiyle oturum açma",
        "auth.opaque.client_missing_hint": "Bu sunucu için OPAQUE oturum açma seçili, ancak tarayıcı tarafı OPAQUE kripto modülü bu derlemede henüz yüklü değil.",
        "auth.opaque.no_password_fallback_hint": "Güvenlik için bu sayfa parolanızı yedek yöntem olarak sunucuya göndermeyecek.",
        "auth.opaque.client_missing_status": "OPAQUE istemci modülü kullanılamıyor."
    },
    "uk": {
        "auth.login.title": "Увійти",
        "auth.login.password_hint": "Використайте ім’я користувача або e-mail DNA-Nexus.",
        "auth.login.presentation_link": "Що таке DNA-Nexus?",
        "auth.login.username_label": "E-mail / ім’я користувача",
        "auth.login.password_label": "Пароль",
        "auth.login.sign_in_button": "Увійти",
        "auth.login.ready": "Готово.",
        "auth.login.footer": "© CPUNK 2026 · DNA-Nexus",
        "auth.login.enter_login_password": "Введіть ім’я користувача/e-mail і пароль.",
        "auth.login.signing_in": "Вхід…",
        "auth.login.invalid_login_password": "Неправильний логін або пароль.",
        "auth.login.cookie_failed": "Вхід виконано, але cookie сеансу не зберігся.",
        "auth.login.network_error": "Помилка мережі під час входу.",
        "auth.login.ui_missing": "Відсутній екран входу",
        "auth.opaque.title": "Вхід із нульовим розголошенням",
        "auth.opaque.client_missing_hint": "Для цього сервера вибрано вхід OPAQUE, але браузерний криптомодуль OPAQUE ще не встановлено в цій збірці.",
        "auth.opaque.no_password_fallback_hint": "З міркувань безпеки ця сторінка не надсилатиме ваш пароль на сервер як запасний варіант.",
        "auth.opaque.client_missing_status": "Клієнтський модуль OPAQUE недоступний."
    },
    "zh": {
        "auth.login.title": "登录",
        "auth.login.password_hint": "使用你的 DNA-Nexus 用户名或电子邮件地址。",
        "auth.login.presentation_link": "什么是 DNA-Nexus？",
        "auth.login.username_label": "电子邮件 / 用户名",
        "auth.login.password_label": "密码",
        "auth.login.sign_in_button": "登录",
        "auth.login.ready": "就绪。",
        "auth.login.footer": "© CPUNK 2026 · DNA-Nexus",
        "auth.login.enter_login_password": "请输入用户名/电子邮件和密码。",
        "auth.login.signing_in": "正在登录…",
        "auth.login.invalid_login_password": "登录名或密码无效。",
        "auth.login.cookie_failed": "登录成功，但会话 Cookie 未保存。",
        "auth.login.network_error": "登录时发生网络错误。",
        "auth.login.ui_missing": "缺少登录界面",
        "auth.opaque.title": "零知识登录",
        "auth.opaque.client_missing_hint": "此服务器已选择 OPAQUE 登录，但此版本尚未安装浏览器端 OPAQUE 加密模块。",
        "auth.opaque.no_password_fallback_hint": "为安全起见，本页面不会将你的密码作为备用方案发送到服务器。",
        "auth.opaque.client_missing_status": "OPAQUE 客户端模块不可用。"
    },
}

# Use English fallback for any future language file not explicitly mapped.
en = translations["en"]
i18n_dir = p("server/src/static/i18n")
if not i18n_dir.exists():
    die("missing i18n dir")

for path in sorted(i18n_dir.glob("*.json")):
    lang = path.stem
    add = translations.get(lang, en)

    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        die(f"{path} is not a JSON object")

    changed = False
    for key, value in add.items():
        if data.get(key) != value:
            data[key] = value
            changed = True

    if changed:
        path.write_text(
            json.dumps(data, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
            encoding="utf-8"
        )
        print(f"patched: {path.relative_to(ROOT)}")
    else:
        print(f"unchanged: {path.relative_to(ROOT)}")

print("done")
