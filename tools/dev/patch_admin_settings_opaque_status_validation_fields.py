#!/usr/bin/env python3
from pathlib import Path
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

replace_once(
    "server/src/static/admin_settings.js",
    """        const credsOk = !!j.credentials_file_exists && !!j.credentials_file_readable;
        const setupOk = !!j.server_setup_file_exists && !!j.server_setup_file_readable;

        setOpaqueStatusPill(
            ready ? "ok" : (helperOk ? "warn" : "fail"),
            ready ? "ready" : (helperOk ? "helper ok • login disabled" : "needs attention")
        );
""",
    """        const credsOk =
            !!j.credentials_file_exists &&
            !!j.credentials_file_readable &&
            !!j.credentials_store_valid;

        const setupOk =
            !!j.server_setup_file_exists &&
            !!j.server_setup_file_readable &&
            !!j.server_setup_valid;

        const infraOk = helperOk && credsOk && setupOk;

        setOpaqueStatusPill(
            ready ? "ok" : (infraOk ? "warn" : "fail"),
            ready ? "ready" : (infraOk ? "backend ok • login disabled" : "needs attention")
        );
""",
)

replace_once(
    "server/src/static/admin_settings.js",
    """        setOpaqueLight(opaqueCredentialsLight, credsOk ? "ok" : "warn");
        if (opaqueCredentialsValue) {
            opaqueCredentialsValue.textContent =
                `exists=${yesNo(j.credentials_file_exists)} • readable=${yesNo(j.credentials_file_readable)}`;
        }

        setOpaqueLight(opaqueServerSetupLight, setupOk ? "ok" : "warn");
        if (opaqueServerSetupValue) {
            opaqueServerSetupValue.textContent =
                `exists=${yesNo(j.server_setup_file_exists)} • readable=${yesNo(j.server_setup_file_readable)}`;
        }
""",
    """        setOpaqueLight(opaqueCredentialsLight, credsOk ? "ok" : "warn");
        if (opaqueCredentialsValue) {
            const accountCount = Number.isFinite(Number(j.credentials_account_count))
                ? Number(j.credentials_account_count)
                : 0;

            opaqueCredentialsValue.textContent =
                `exists=${yesNo(j.credentials_file_exists)} • readable=${yesNo(j.credentials_file_readable)} • valid=${yesNo(j.credentials_store_valid)} • accounts=${accountCount}`;
        }

        setOpaqueLight(opaqueServerSetupLight, setupOk ? "ok" : "warn");
        if (opaqueServerSetupValue) {
            opaqueServerSetupValue.textContent =
                `exists=${yesNo(j.server_setup_file_exists)} • readable=${yesNo(j.server_setup_file_readable)} • valid=${yesNo(j.server_setup_valid)}`;
        }
""",
)

print("done")
