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
    "server/src/backups/system_backup_worker.cpp",
    """    const std::filesystem::path password_credentials_path =
        password_credentials_path_for_backup_local(users_path);

    add_regular("config", "Admin settings", admin_settings_path, "config/admin_settings.json");
""",
    """    const std::filesystem::path password_credentials_path =
        password_credentials_path_for_backup_local(users_path);

    const std::filesystem::path opaque_credentials_path = configured_path_local(
        "PQNAS_OPAQUE_CREDENTIALS_PATH",
        {"/etc/pqnas/opaque_credentials.json", "/srv/pqnas/config/opaque_credentials.json"}
    );

    const std::filesystem::path opaque_server_setup_path = configured_path_local(
        "PQNAS_OPAQUE_SERVER_SETUP_PATH",
        {"/etc/pqnas/opaque_server_setup.bin", "/srv/pqnas/config/opaque_server_setup.bin"}
    );

    add_regular("config", "Admin settings", admin_settings_path, "config/admin_settings.json");
""",
)

replace_once(
    "server/src/backups/system_backup_worker.cpp",
    """    add_regular("users_auth", "Users registry", users_path, "users/users.json");
    add_regular("users_auth", "Password credentials", password_credentials_path, "users/password_credentials.json");
    add_regular("shares", "Share registry", shares_path, "shares/shares.json");
""",
    """    add_regular("users_auth", "Users registry", users_path, "users/users.json");
    add_regular("users_auth", "Password credentials", password_credentials_path, "users/password_credentials.json");
    add_regular("users_auth", "OPAQUE credentials store", opaque_credentials_path, "users/opaque_credentials.json");
    add_regular("users_auth", "OPAQUE server setup", opaque_server_setup_path, "users/opaque_server_setup.bin");
    add_regular("shares", "Share registry", shares_path, "shares/shares.json");
""",
)

print("done")
