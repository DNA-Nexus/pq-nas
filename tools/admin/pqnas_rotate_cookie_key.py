#!/usr/bin/env python3
"""
Rotate the PQ-NAS browser session cookie HMAC key.

This is a hard rotation:
- only PQNAS_COOKIE_KEY_B64URL is replaced
- PQNAS_SERVER_PK_B64URL and PQNAS_SERVER_SK_B64URL are preserved
- existing browser sessions become invalid after pqnas.service restart

The script never prints the new secret value.
"""

from __future__ import annotations

import argparse
import base64
import os
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path


KEY_NAME = "PQNAS_COOKIE_KEY_B64URL"


def make_cookie_key_b64url() -> str:
    return base64.urlsafe_b64encode(os.urandom(32)).decode("ascii").rstrip("=")


def backup_file(path: Path) -> Path | None:
    if not path.exists():
        return None

    ts = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
    backup = path.with_name(f"{path.name}.bak-{ts}")
    shutil.copy2(path, backup)

    os.chmod(backup, 0o600)
    if os.geteuid() == 0:
        os.chown(backup, 0, 0)

    return backup


def replace_or_append_key(lines: list[str], key: str, value: str) -> tuple[list[str], bool]:
    out: list[str] = []
    replaced = False

    for line in lines:
        stripped = line.strip()
        if stripped.startswith(f"{key}=") or stripped.startswith(f"export {key}="):
            prefix = "export " if stripped.startswith("export ") else ""
            out.append(f"{prefix}{key}={value}")
            replaced = True
        else:
            out.append(line)

    if not replaced:
        if out and out[-1].strip():
            out.append("")
        out.append(f"{key}={value}")

    return out, replaced


def write_secure_env(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)

    tmp = path.with_name(f".{path.name}.tmp-{os.getpid()}")
    try:
        tmp.write_text(text, encoding="utf-8")
        os.chmod(tmp, 0o600)
        if os.geteuid() == 0:
            os.chown(tmp, 0, 0)

        os.replace(tmp, path)

        if os.geteuid() == 0:
            os.chown(path, 0, 0)
        os.chmod(path, 0o600)
    finally:
        try:
            if tmp.exists():
                tmp.unlink()
        except OSError:
            pass


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Rotate PQNAS_COOKIE_KEY_B64URL in /etc/pqnas/keys.env without printing the secret."
    )
    ap.add_argument(
        "--keys-env",
        default="/etc/pqnas/keys.env",
        help="Path to keys.env. Default: /etc/pqnas/keys.env",
    )
    ap.add_argument(
        "--restart",
        action="store_true",
        help="Restart pqnas.service after writing the new key.",
    )

    args = ap.parse_args()
    path = Path(args.keys_env)

    if path == Path("/etc/pqnas/keys.env") and os.geteuid() != 0:
        print("ERROR: run as root for /etc/pqnas/keys.env", file=sys.stderr)
        return 2

    old_lines = path.read_text(encoding="utf-8").splitlines() if path.exists() else []
    backup = backup_file(path)

    new_key = make_cookie_key_b64url()
    new_lines, replaced = replace_or_append_key(old_lines, KEY_NAME, new_key)
    write_secure_env(path, "\n".join(new_lines) + "\n")

    print(f"Rotated {KEY_NAME} in {path}")
    if backup:
        print(f"Backup written: {backup}")
    print("File mode set to 0600.")
    print("The new key value was not printed.")

    if args.restart:
        subprocess.run(["systemctl", "restart", "pqnas.service"], check=True)
        print("Restarted pqnas.service. Existing browser sessions are now invalid.")
    else:
        print("Restart pqnas.service to activate the new key and invalidate existing browser sessions.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
