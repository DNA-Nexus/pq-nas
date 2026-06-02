#!/usr/bin/env python3
"""
Lightweight staged-file secret guard for PQ-NAS.

Designed for local pre-commit use. It scans staged added/modified text files
for obvious private keys, environment secret files, and PQ-NAS key variables.

It intentionally avoids printing secret values.
"""

from __future__ import annotations

import os
import re
import subprocess
import sys
from pathlib import Path


BLOCKED_BASENAMES = {
    ".env",
    ".env.local",
    ".env.pqnas",
    "keys.env",
    "pqnas.env",
}

BLOCKED_SUFFIXES = (
    ".pem",
    ".key",
    ".p12",
    ".pfx",
)

SECRET_PATTERNS = [
    re.compile(r"-----BEGIN (?:RSA |EC |OPENSSH |PRIVATE )?PRIVATE KEY-----"),
    re.compile(r"\bPQNAS_SERVER_SK_B64URL\s*="),
    re.compile(r"\bPQNAS_COOKIE_KEY_B64URL\s*="),
    re.compile(r"\bPQNAS_UPDATE_APPLY_ENABLED\s*=\s*1\b"),
    re.compile(r"\bAWS_SECRET_ACCESS_KEY\s*="),
    re.compile(r"\bGITHUB_TOKEN\s*="),
    re.compile(r"\bOPENAI_API_KEY\s*="),
]


def run_git(args: list[str]) -> str:
    p = subprocess.run(
        ["git", *args],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    if p.returncode != 0:
        raise RuntimeError((p.stderr or p.stdout or "git command failed").strip())
    return p.stdout


def staged_files() -> list[str]:
    out = run_git(["diff", "--cached", "--name-only", "--diff-filter=ACMR"])
    return [line.strip() for line in out.splitlines() if line.strip()]


def staged_text(path: str) -> str | None:
    p = subprocess.run(
        ["git", "show", f":{path}"],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    if p.returncode != 0:
        return None

    data = p.stdout
    if b"\0" in data:
        return None

    try:
        return data.decode("utf-8")
    except UnicodeDecodeError:
        return None


def should_block_by_name(path: str) -> str | None:
    pp = Path(path)
    base = pp.name

    if base in BLOCKED_BASENAMES:
        return f"blocked secret/env filename: {base}"

    if base.endswith(BLOCKED_SUFFIXES):
        return f"blocked key/certificate filename suffix: {base}"

    parts = {part.lower() for part in pp.parts}
    if "secrets" in parts and "docs" not in parts:
        return "file is under a secrets directory"

    return None


def main() -> int:
    findings: list[tuple[str, str]] = []

    for path in staged_files():
        name_reason = should_block_by_name(path)
        if name_reason:
            findings.append((path, name_reason))
            continue

        text = staged_text(path)
        if text is None:
            continue

        for pat in SECRET_PATTERNS:
            if pat.search(text):
                findings.append((path, f"matched secret pattern: {pat.pattern}"))
                break

    if not findings:
        return 0

    print("ERROR: possible secret material detected in staged files.", file=sys.stderr)
    print("No secret values were printed.", file=sys.stderr)
    print("", file=sys.stderr)

    for path, reason in findings:
        print(f"  - {path}: {reason}", file=sys.stderr)

    print("", file=sys.stderr)
    print("Unstage/remove the secret, or commit with --no-verify only if this is a deliberate false positive.", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
