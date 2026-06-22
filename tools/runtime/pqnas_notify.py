#!/usr/bin/env python3
"""
PQ-NAS Notifications + Warnings worker.

Runs from systemd timers:
  - pqnas-notify-warnings.timer  -> --check-warnings
  - pqnas-notify-weekly.timer    -> --weekly-summary

Reads:
  - /etc/pqnas/pqnas.env
  - PQNAS_NOTIFICATIONS_PATH, default /etc/pqnas/notifications.json

Secrets:
  - Telegram bot token is read from server-side config only.
  - The token is never printed.
"""

from __future__ import annotations

import argparse
from email.message import EmailMessage
import fcntl
import json
import os
import shutil
import subprocess
import sys
import time
from contextlib import contextmanager
import urllib.parse
import urllib.request
import smtplib
import ssl
from pathlib import Path
from typing import Any, Dict, Tuple


ENV_PATH = Path("/etc/pqnas/pqnas.env")
DEFAULT_SETTINGS_PATH = Path("/etc/pqnas/notifications.json")
STATE_DIR = Path("/var/lib/pqnas/notifications")
STATE_PATH = STATE_DIR / "state.json"

WARNING_THROTTLE_SECONDS = 6 * 60 * 60


@contextmanager
def notification_state_lock():
    STATE_DIR.mkdir(parents=True, exist_ok=True)
    lock_path = STATE_DIR / "state.json.lock"

    with lock_path.open("a+", encoding="utf-8") as lock_file:
        try:
            os.chmod(lock_path, 0o600)
        except Exception:
            pass

        fcntl.flock(lock_file.fileno(), fcntl.LOCK_EX)
        try:
            yield
        finally:
            fcntl.flock(lock_file.fileno(), fcntl.LOCK_UN)


def read_env_file(path: Path = ENV_PATH) -> Dict[str, str]:
    # Start with the actual process environment. This is the important path
    # for systemd services: systemd reads EnvironmentFile as root before
    # switching to User=pqnas.
    env: Dict[str, str] = {
        k: v for k, v in os.environ.items()
        if k.startswith("PQNAS_")
    }

    if not path.exists():
        return env

    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except PermissionError:
        # Expected when running as pqnas and /etc/pqnas/pqnas.env is root:root 0600.
        # Keep using process env and built-in safe defaults.
        return env

    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue

        k, v = line.split("=", 1)
        k = k.strip()
        v = v.strip().strip('"').strip("'")
        if k:
            env[k] = v

    return env


def load_json(path: Path, fallback: Any) -> Any:
    try:
        if not path.exists():
            return fallback
        data = json.loads(path.read_text(encoding="utf-8"))
        return data
    except Exception:
        return fallback


def save_json_private(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    os.chmod(tmp, 0o600)
    os.replace(tmp, path)
    os.chmod(path, 0o600)


def config_paths(env: Dict[str, str]) -> Tuple[Path, Path, Path]:
    settings = Path(env.get("PQNAS_NOTIFICATIONS_PATH") or str(DEFAULT_SETTINGS_PATH))

    config_root = Path(env.get("PQNAS_CONFIG") or env.get("PQNAS_CONFIG_DIR") or "/etc/pqnas")
    users = Path(env.get("PQNAS_USERS_PATH") or str(config_root / "users.json"))

    data_root = Path(env.get("PQNAS_DATA_ROOT") or "/srv/pqnas/data")
    return settings, users, data_root


def load_settings(env: Dict[str, str]) -> Dict[str, Any]:
    settings_path, _, _ = config_paths(env)

    defaults: Dict[str, Any] = {
        "version": 1,
        "info_email_enabled": True,
        "info_telegram_enabled": False,
        "warnings_email_enabled": True,
        "warnings_telegram_enabled": False,
        "weekly_summary_enabled": True,
        "extra_emails": [],
        "telegram_bot_token": "",
        "telegram_chat_id": "",
        "warning_free_percent": 10,
        "warning_free_gib": 10,
    }

    data = load_json(settings_path, {})
    if isinstance(data, dict):
        defaults.update(data)

    return defaults


def load_state() -> Dict[str, Any]:
    state = load_json(STATE_PATH, {})
    return state if isinstance(state, dict) else {}


def save_state(state: Dict[str, Any]) -> None:
    save_json_private(STATE_PATH, state)


def human_bytes(n: int) -> str:
    x = float(n)
    for unit in ("B", "KiB", "MiB", "GiB", "TiB", "PiB"):
        if abs(x) < 1024.0 or unit == "PiB":
            if unit == "B":
                return f"{int(x)} {unit}"
            return f"{x:.1f} {unit}"
        x /= 1024.0
    return f"{n} B"


def telegram_enabled(settings: Dict[str, Any], kind: str) -> bool:
    token = str(settings.get("telegram_bot_token") or "").strip()
    chat = str(settings.get("telegram_chat_id") or "").strip()
    if not token or not chat:
        return False

    if kind == "warning":
        return bool(settings.get("warnings_telegram_enabled"))
    return bool(settings.get("info_telegram_enabled"))


def send_telegram(settings: Dict[str, Any], text: str) -> bool:
    token = str(settings.get("telegram_bot_token") or "").strip()
    chat_id = str(settings.get("telegram_chat_id") or "").strip()

    if not token or not chat_id:
        print("[notify] telegram not configured")
        return False

    url = f"https://api.telegram.org/bot{token}/sendMessage"
    payload = urllib.parse.urlencode({
        "chat_id": chat_id,
        "text": text,
        "disable_web_page_preview": "true",
    }).encode("utf-8")

    req = urllib.request.Request(
        url,
        data=payload,
        headers={"Content-Type": "application/x-www-form-urlencoded"},
        method="POST",
    )

    try:
        with urllib.request.urlopen(req, timeout=20) as r:
            body = r.read(4096).decode("utf-8", errors="replace")
            if r.status < 200 or r.status >= 300:
                print(f"[notify] telegram http error: {r.status}")
                return False

            try:
                j = json.loads(body)
                if isinstance(j, dict) and j.get("ok") is False:
                    print(f"[notify] telegram api error: {j.get('description') or 'ok=false'}")
                    return False
            except Exception:
                pass

            print("[notify] telegram sent")
            return True

    except Exception as e:
        print(f"[notify] telegram send failed: {e}")
        return False



def email_enabled(settings: Dict[str, Any], kind: str) -> bool:
    if kind == "warning":
        return bool(settings.get("warnings_email_enabled"))
    return bool(settings.get("info_email_enabled"))


def normalize_email_list(value: Any) -> list[str]:
    out: list[str] = []

    if isinstance(value, list):
        items = value
    elif isinstance(value, str):
        items = value.replace(";", ",").split(",")
    else:
        items = []

    for item in items:
        e = str(item or "").strip()
        if "@" in e and "." in e and e not in out:
            out.append(e)

    return out


def admin_emails(users_path: Path) -> list[str]:
    data = load_json(users_path, None)

    rows = []
    if isinstance(data, list):
        rows = data
    elif isinstance(data, dict):
        if isinstance(data.get("users"), list):
            rows = data["users"]
        elif isinstance(data.get("by_fp"), dict):
            rows = list(data["by_fp"].values())

    out: list[str] = []

    for u in rows:
        if not isinstance(u, dict):
            continue

        role = str(u.get("role") or "").lower()
        if role != "admin":
            continue

        candidates = [
            str(u.get("email") or "").strip(),
            str(u.get("login") or "").strip(),
            str(u.get("username") or "").strip(),
        ]

        for e in candidates:
            if "@" in e and "." in e and e not in out:
                out.append(e)

    return out


def email_recipients(settings: Dict[str, Any], users_path: Path) -> list[str]:
    out: list[str] = []

    for e in admin_emails(users_path):
        if e not in out:
            out.append(e)

    for e in normalize_email_list(settings.get("extra_emails")):
        if e not in out:
            out.append(e)

    return out


def smtp_port(env: Dict[str, str], settings: Dict[str, Any] | None = None) -> int:
    settings = settings or {}
    raw = str(settings.get("smtp_port") or env.get("PQNAS_SMTP_PORT") or "").strip()
    if raw:
        try:
            return int(raw)
        except Exception:
            pass

    mode = str(settings.get("smtp_tls") or env.get("PQNAS_SMTP_TLS") or "starttls").strip().lower()
    return 465 if mode == "ssl" else 587


def send_email(
    settings: Dict[str, Any],
    env: Dict[str, str],
    users_path: Path,
    *,
    kind: str,
    subject: str,
    body: str,
) -> bool:
    if not email_enabled(settings, kind):
        print(f"[notify] {kind} email disabled")
        return False

    recipients = email_recipients(settings, users_path)
    if not recipients:
        print("[notify] email has no recipients")
        return False

    from_addr = str(settings.get("smtp_from") or env.get("PQNAS_SMTP_FROM") or "dna-nexus@localhost").strip()
    host = str(settings.get("smtp_host") or env.get("PQNAS_SMTP_HOST") or "").strip()
    tls_mode = str(settings.get("smtp_tls") or env.get("PQNAS_SMTP_TLS") or "starttls").strip().lower()
    user = str(settings.get("smtp_user") or env.get("PQNAS_SMTP_USER") or "").strip()
    password = str(settings.get("smtp_password") or env.get("PQNAS_SMTP_PASSWORD") or "")

    msg = EmailMessage()
    msg["From"] = from_addr
    msg["To"] = ", ".join(recipients)
    msg["Subject"] = subject
    msg.set_content(body)

    if host:
        port = smtp_port(env, settings)

        try:
            ctx = ssl.create_default_context()

            if tls_mode == "ssl":
                with smtplib.SMTP_SSL(host, port, timeout=20, context=ctx) as smtp:
                    if user:
                        smtp.login(user, password)
                    smtp.send_message(msg)
            else:
                with smtplib.SMTP(host, port, timeout=20) as smtp:
                    smtp.ehlo()
                    if tls_mode != "none":
                        smtp.starttls(context=ctx)
                        smtp.ehlo()
                    if user:
                        smtp.login(user, password)
                    smtp.send_message(msg)

            print("[notify] email sent via SMTP")
            return True

        except Exception as e:
            print(f"[notify] SMTP email failed: {e}")
            return False

    sendmail = None
    for candidate in ("/usr/sbin/sendmail", "/usr/bin/sendmail", "/usr/bin/msmtp"):
        if Path(candidate).exists():
            sendmail = candidate
            break

    if not sendmail:
        print("[notify] email not configured: set PQNAS_SMTP_HOST or install/configure sendmail/msmtp")
        return False

    try:
        p = subprocess.run(
            [sendmail, "-t", "-oi"],
            input=msg.as_string(),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=30,
            check=False,
        )

        if p.returncode == 0:
            print("[notify] email sent via sendmail")
            return True

        print(f"[notify] sendmail failed: {(p.stdout or '').strip()}")
        return False

    except Exception as e:
        print(f"[notify] sendmail failed: {e}")
        return False


def deliver_warning(
    settings: Dict[str, Any],
    env: Dict[str, str],
    users_path: Path,
    *,
    subject: str,
    body: str,
) -> bool:
    ok = False

    if telegram_enabled(settings, "warning"):
        ok = send_telegram(settings, body) or ok

    if email_enabled(settings, "warning"):
        ok = send_email(settings, env, users_path, kind="warning", subject=subject, body=body) or ok

    return ok


def deliver_notification(
    settings: Dict[str, Any],
    env: Dict[str, str],
    users_path: Path,
    *,
    subject: str,
    body: str,
) -> bool:
    ok = False

    if telegram_enabled(settings, "notification"):
        ok = send_telegram(settings, body) or ok

    if email_enabled(settings, "notification"):
        ok = send_email(settings, env, users_path, kind="notification", subject=subject, body=body) or ok

    return ok


def users_summary(users_path: Path) -> Dict[str, int]:
    data = load_json(users_path, None)

    rows = []
    if isinstance(data, list):
        rows = data
    elif isinstance(data, dict):
        if isinstance(data.get("users"), list):
            rows = data["users"]
        elif isinstance(data.get("by_fp"), dict):
            rows = list(data["by_fp"].values())

    total = 0
    enabled = 0
    disabled = 0
    admins = 0

    for u in rows:
        if not isinstance(u, dict):
            continue
        total += 1
        if str(u.get("status") or "").lower() == "enabled":
            enabled += 1
        else:
            disabled += 1
        if str(u.get("role") or "").lower() == "admin":
            admins += 1

    return {
        "total": total,
        "enabled": enabled,
        "disabled": disabled,
        "admins": admins,
    }


def disk_summary(data_root: Path) -> Dict[str, Any]:
    usage = shutil.disk_usage(str(data_root))
    free_pct = (usage.free / usage.total * 100.0) if usage.total else 0.0

    return {
        "path": str(data_root),
        "total": int(usage.total),
        "used": int(usage.used),
        "free": int(usage.free),
        "free_pct": free_pct,
    }


def pqnas_service_state() -> str:
    try:
        out = subprocess.run(
            ["systemctl", "is-active", "pqnas.service"],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            timeout=10,
            check=False,
        )
        return (out.stdout or "").strip() or "unknown"
    except Exception:
        return "unknown"


def pqnas_started_at() -> str:
    try:
        out = subprocess.run(
            ["systemctl", "show", "pqnas.service", "-p", "ActiveEnterTimestamp", "--value"],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            timeout=10,
            check=False,
        )
        return (out.stdout or "").strip() or "unknown"
    except Exception:
        return "unknown"


def should_send_throttled(state: Dict[str, Any], key: str, now: int) -> bool:
    sent = state.setdefault("last_warning_sent_epoch_by_key", {})
    if not isinstance(sent, dict):
        sent = {}
        state["last_warning_sent_epoch_by_key"] = sent

    last = int(sent.get(key) or 0)
    if now - last < WARNING_THROTTLE_SECONDS:
        return False

    sent[key] = now
    return True


def check_warnings() -> int:
    env = read_env_file()
    settings = load_settings(env)
    _, users_path, data_root = config_paths(env)

    now = int(time.time())
    sent_count = 0

    if not telegram_enabled(settings, "warning") and not email_enabled(settings, "warning"):
        print("[notify] warning delivery disabled")
        return 0

    with notification_state_lock():
        state = load_state()

        svc = pqnas_service_state()
        if svc != "active":
            key = "service:pqnas"
            if should_send_throttled(state, key, now):
                body = f"⚠️ DNA-Nexus warning\n\npqnas.service is not active: {svc}"
                if deliver_warning(
                    settings,
                    env,
                    users_path,
                    subject="DNA-Nexus warning: pqnas.service is not active",
                    body=body,
                ):
                    sent_count += 1

        try:
            d = disk_summary(data_root)
            free_pct = float(d["free_pct"])
            free_gib = float(d["free"]) / (1024 ** 3)

            threshold_pct = float(settings.get("warning_free_percent") or 10)
            threshold_gib = float(settings.get("warning_free_gib") or 10)

            if free_pct <= threshold_pct or free_gib <= threshold_gib:
                key = f"disk:{d['path']}"
                if should_send_throttled(state, key, now):
                    body = (
                        "⚠️ DNA-Nexus storage warning\n\n"
                        f"Path: {d['path']}\n"
                        f"Free: {human_bytes(d['free'])} ({free_pct:.1f}%)\n"
                        f"Used: {human_bytes(d['used'])}\n"
                        f"Total: {human_bytes(d['total'])}\n\n"
                        f"Threshold: <= {threshold_pct:.1f}% or <= {threshold_gib:.1f} GiB free"
                    )
                    if deliver_warning(
                        settings,
                        env,
                        users_path,
                        subject="DNA-Nexus warning: storage space is low",
                        body=body,
                    ):
                        sent_count += 1

        except Exception as e:
            key = "disk:check_failed"
            if should_send_throttled(state, key, now):
                body = f"⚠️ DNA-Nexus warning\n\nStorage check failed: {e}"
                if deliver_warning(
                    settings,
                    env,
                    users_path,
                    subject="DNA-Nexus warning: storage check failed",
                    body=body,
                ):
                    sent_count += 1

        save_state(state)

    print(f"[notify] warnings complete, sent={sent_count}")
    return 0


def weekly_summary() -> int:
    env = read_env_file()
    settings = load_settings(env)
    _, users_path, data_root = config_paths(env)

    if not bool(settings.get("weekly_summary_enabled", True)):
        print("[notify] weekly summary disabled")
        return 0

    if not telegram_enabled(settings, "notification") and not email_enabled(settings, "notification"):
        print("[notify] notification delivery disabled")
        return 0

    with notification_state_lock():
        state = load_state()
        users = users_summary(users_path)
        disk = disk_summary(data_root)

        last_total = state.get("last_weekly_user_total")
        growth_text = "first summary" if last_total is None else f"{users['total'] - int(last_total):+d} since previous summary"

    body = (
        "📊 DNA-Nexus weekly summary\n\n"
        f"Users: {users['total']} total ({users['enabled']} enabled, {users['admins']} admin) — {growth_text}\n"
        f"Service: pqnas.service is {pqnas_service_state()}\n"
        f"Started: {pqnas_started_at()}\n\n"
        f"Storage: {disk['path']}\n"
        f"Used: {human_bytes(disk['used'])}\n"
        f"Free: {human_bytes(disk['free'])} ({disk['free_pct']:.1f}%)\n"
        f"Total: {human_bytes(disk['total'])}"
    )

    ok = deliver_notification(
        settings,
        env,
        users_path,
        subject="DNA-Nexus weekly summary",
        body=body,
    )

    if ok:
        with notification_state_lock():
            state = load_state()
            state["last_weekly_user_total"] = users["total"]
            state["last_weekly_summary_epoch"] = int(time.time())
            save_state(state)
        return 0

    return 1


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check-warnings", action="store_true")
    ap.add_argument("--weekly-summary", action="store_true")
    ap.add_argument("--test-telegram", action="store_true")
    ap.add_argument("--test-email", action="store_true")
    args = ap.parse_args()

    if args.check_warnings:
        return check_warnings()
    if args.weekly_summary:
        return weekly_summary()
    if args.test_telegram:
        env = read_env_file()
        settings = load_settings(env)
        return 0 if send_telegram(settings, "DNA-Nexus notification test from pqnas_notify.py") else 1

    if args.test_email:
        env = read_env_file()
        settings = load_settings(env)
        _, users_path, _ = config_paths(env)
        body = (
            "DNA-Nexus email test from pqnas_notify.py\n\n"
            "Email delivery is configured well enough for the notification worker to send this message."
        )
        return 0 if send_email(
            settings,
            env,
            users_path,
            kind="notification",
            subject="DNA-Nexus email test",
            body=body,
        ) else 1

    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
