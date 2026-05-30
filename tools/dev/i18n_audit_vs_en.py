#!/usr/bin/env python3
from pathlib import Path
import argparse
import json
import sys

ROOT = Path("server/src/static/i18n")

def load_json_with_dupe_check(path):
    text = path.read_text(encoding="utf-8")
    dupes = []

    def hook(pairs):
        out = {}
        seen = set()
        for k, v in pairs:
            if k in seen:
                dupes.append(k)
            seen.add(k)
            out[k] = v
        return out

    data = json.loads(text, object_pairs_hook=hook)
    if not isinstance(data, dict):
        raise SystemExit(f"{path}: top-level JSON is not an object")
    return data, dupes, len(text.splitlines())

def subset(data, prefix):
    if not prefix:
        return data
    return {k: v for k, v in data.items() if k.startswith(prefix)}

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--prefix", default="", help="Only compare keys with this prefix, e.g. circlestack.")
    ap.add_argument("--details", action="store_true", help="Print missing/extra/duplicate keys")
    args = ap.parse_args()

    en_path = ROOT / "en.json"
    en_all, en_dupes, en_lines = load_json_with_dupe_check(en_path)
    en = subset(en_all, args.prefix)
    en_keys = list(en.keys())
    en_keyset = set(en_keys)

    files = sorted(p for p in ROOT.glob("*.json") if p.name != "en.json")

    title = f"i18n audit vs en.json"
    if args.prefix:
        title += f" prefix={args.prefix!r}"
    print(title)
    print("-" * len(title))
    print(f"{'lang':<5} {'lines':>6} {'Δlines':>7} {'keys':>6} {'Δkeys':>6} {'missing':>8} {'extra':>6} {'same':>6} {'diff':>6} {'dupes':>6}")

    failed = False

    for path in files:
        lang = path.stem
        data_all, dupes, lines = load_json_with_dupe_check(path)
        data = subset(data_all, args.prefix)
        keys = list(data.keys())
        keyset = set(keys)

        missing = [k for k in en_keys if k not in keyset]
        extra = [k for k in keys if k not in en_keyset]
        common = [k for k in en_keys if k in keyset]
        same = [k for k in common if data[k] == en[k]]
        diff = [k for k in common if data[k] != en[k]]

        if missing or extra or dupes:
            failed = True

        print(
            f"{lang:<5} "
            f"{lines:>6} {lines - en_lines:>7} "
            f"{len(keys):>6} {len(keys) - len(en_keys):>6} "
            f"{len(missing):>8} {len(extra):>6} "
            f"{len(same):>6} {len(diff):>6} "
            f"{len(dupes):>6}"
        )

        if args.details and (missing or extra or dupes):
            if dupes:
                print(f"\n[{lang}] duplicate keys:")
                for k in dupes:
                    print(f"  DUPLICATE {k}")
            if missing:
                print(f"\n[{lang}] missing keys:")
                for k in missing:
                    print(f"  MISSING {k}")
            if extra:
                print(f"\n[{lang}] extra keys:")
                for k in extra:
                    print(f"  EXTRA {k}")
            print()

    if failed:
        sys.exit(1)

if __name__ == "__main__":
    main()
