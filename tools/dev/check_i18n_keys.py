#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path


ALLOWED_SAME_AS_BASE_KEYS = {
    # Product / brand names.
    "dropzone.kicker",
    "dropzone.public.kicker",
    "dropzone.default_name",
    "dropzone.title",

    # Default folder paths / examples. These are intentionally stable path-like values.
    "dropzone.default_destination",

    # Universal UI / templates where translating would add little value or break formatting.
    "dropzone.ok",
    "dropzone.public.upload_progress",
    "dropzone.brand_logo_url",
    "dropzone.password",
    "dropzone.public.password_placeholder",
}


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def main() -> int:
    ap = argparse.ArgumentParser(description="Audit PQ-NAS flat i18n JSON keys.")
    ap.add_argument("--dir", default="server/src/static/i18n", help="i18n directory")
    ap.add_argument("--base", default="en", help="base language code")
    ap.add_argument("--prefix", action="append", default=[], help="key prefix to check, can be repeated")
    ap.add_argument("--strict", action="store_true", help="exit non-zero if problems are found")
    args = ap.parse_args()

    root = Path(args.dir)
    base_path = root / f"{args.base}.json"
    if not base_path.exists():
        print(f"ERROR: missing base file: {base_path}")
        return 2

    base = load_json(base_path)
    prefixes = tuple(args.prefix or [])

    def want_key(k: str) -> bool:
        return not prefixes or k.startswith(prefixes)

    base_keys = {k for k in base.keys() if want_key(k)}
    problems = 0

    for path in sorted(root.glob("*.json")):
        lang = path.stem
        data = load_json(path)

        missing = sorted(base_keys - set(data.keys()))
        suspicious = []

        for k, v in data.items():
            if not want_key(k):
                continue
            if isinstance(v, str):
                if "??" in v or v.strip() == "?":
                    suspicious.append((k, v))
                if (
                    lang != args.base
                    and k in base
                    and v == base[k]
                    and k not in ALLOWED_SAME_AS_BASE_KEYS
                ):
                    suspicious.append((k, "same as base: " + v))

        if missing or suspicious:
            print(f"\n[{lang}] {path}")
            if missing:
                print("  Missing:")
                for k in missing:
                    print(f"    {k}")
            if suspicious:
                print("  Suspicious:")
                for k, v in suspicious:
                    print(f"    {k} = {v}")
            problems += len(missing) + len(suspicious)

    if problems:
        print(f"\nProblems found: {problems}")
        return 1 if args.strict else 0

    print("OK: no missing/suspicious keys found for selected prefixes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
