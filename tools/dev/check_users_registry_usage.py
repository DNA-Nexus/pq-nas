#!/usr/bin/env python3
"""
Guard against inconsistent UsersRegistry loading.

PQ-NAS should load the authoritative UsersRegistry once in main.cpp and pass
that in-process registry to route modules through deps/context pointers.

This check fails if a new direct UsersRegistry::load() call is introduced
outside the approved startup/implementation files.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path


REPO = Path(__file__).resolve().parents[2]

# users_registry.cpp defines the method; main.cpp owns startup loading.
ALLOWED_LOAD_CALL_FILES = {
    "server/src/main.cpp",
    "server/src/users_registry.cpp",
}

LOAD_CALL_RE = re.compile(r"\b[A-Za-z_][A-Za-z0-9_]*\.load\s*\(")

SKIP_DIR_PARTS = {
    ".git",
    "build",
    "build-release",
    "third_party",
}


def should_skip(path: Path) -> bool:
    rel_parts = set(path.relative_to(REPO).parts)
    return bool(rel_parts & SKIP_DIR_PARTS)


def main() -> int:
    bad: list[tuple[str, int, str]] = []

    for path in sorted((REPO / "server" / "src").rglob("*")):
        if not path.is_file() or path.suffix not in {".cpp", ".cc", ".h", ".hpp"}:
            continue
        if should_skip(path):
            continue

        rel = path.relative_to(REPO).as_posix()
        text = path.read_text(encoding="utf-8", errors="replace")

        for lineno, line in enumerate(text.splitlines(), start=1):
            if "UsersRegistry::load" in line:
                continue

            if LOAD_CALL_RE.search(line) and "users" in line.lower():
                if rel not in ALLOWED_LOAD_CALL_FILES:
                    bad.append((rel, lineno, line.strip()))

    if bad:
        print("ERROR: unexpected users registry load-like calls found:", file=sys.stderr)
        for rel, lineno, line in bad:
            print(f"  {rel}:{lineno}: {line}", file=sys.stderr)
        print("", file=sys.stderr)
        print("Load UsersRegistry once in main.cpp and pass &users through route deps/context.", file=sys.stderr)
        return 1

    print("ok: UsersRegistry direct loading is limited to approved startup/implementation files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
