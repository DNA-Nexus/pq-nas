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
        die(f"anchor not found in {rel}: {old!r}")
    write(rel, text.replace(old, new, 1))

# 1) Add resolved paths to admin/internal diagnostic JSON.
replace_once(
    "server/src/opaque_backend_status.cpp",
    """    out << '{'
        << "\\"ready_for_login\\":" << json_bool(status.ready_for_login) << ','
        << "\\"credentials_file_exists\\":" << json_bool(status.credentials_file_exists) << ','
""",
    """    out << '{'
        << "\\"ready_for_login\\":" << json_bool(status.ready_for_login) << ','
        << "\\"credentials_path\\":" << json_escape(status.credentials_path.string()) << ','
        << "\\"server_setup_path\\":" << json_escape(status.server_setup_path.string()) << ','
        << "\\"helper_path\\":" << json_escape(status.helper_path.string()) << ','
        << "\\"credentials_file_exists\\":" << json_bool(status.credentials_file_exists) << ','
""",
)

# 2) Tests: diagnostic JSON should include path fields.
replace_once(
    "tests/opaque_backend_status/test_opaque_backend_status.cpp",
    """    require_true(contains_text(missing_diag, "opaque_credentials_missing"),
                 "internal diagnostic should include missing credential reason");
""",
    """    require_true(contains_text(missing_diag, "opaque_credentials_missing"),
                 "internal diagnostic should include missing credential reason");
    require_true(contains_text(missing_diag, "\\"credentials_path\\":"),
                 "internal diagnostic should include resolved credentials path");
    require_true(contains_text(missing_diag, "\\"server_setup_path\\":"),
                 "internal diagnostic should include resolved server setup path");
    require_true(contains_text(missing_diag, "\\"helper_path\\":"),
                 "internal diagnostic should include resolved helper path");
""",
)

# 3) Design doc update.
replace_once(
    "docs/technical/opaque_login_design.md",
    """- OPAQUE config paths use `PQNAS_CONFIG_ROOT` when set, otherwise the existing deployment `PQNAS_CONFIG`, otherwise `/etc/pqnas`.
""",
    """- OPAQUE config paths use `PQNAS_CONFIG_ROOT` when set, otherwise the existing deployment `PQNAS_CONFIG`, otherwise `/etc/pqnas`.
- Admin-only OPAQUE diagnostics include resolved credentials/setup/helper paths for troubleshooting.
""",
)

print("done")
