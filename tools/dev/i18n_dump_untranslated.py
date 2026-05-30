#!/usr/bin/env python3
import argparse
import json
import re
import sys
from pathlib import Path

I18N_DIR = Path("server/src/static/i18n")

DEFAULT_PREFIXES = [
    "circlestack.profile.achShort.",
    "circlestack.profile.achReplay.",
    "circlestack.profile.achLocked.",
    "circlestack.profile.achDesc.",
]

def die(msg):
    print(f"ERROR: {msg}", file=sys.stderr)
    sys.exit(1)

def load_json(path):
    if not path.exists():
        die(f"missing file: {path}")
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except Exception as e:
        die(f"invalid json in {path}: {e}")

def wanted_key(key, prefixes):
    return any(key.startswith(p) for p in prefixes)

def find_line_info(path):
    out = {}
    key_re = re.compile(r'^\s*"((?:[^"\\]|\\.)+)"\s*:')
    for n, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        m = key_re.match(line)
        if not m:
            continue
        try:
            key = json.loads(f'"{m.group(1)}"')
        except Exception:
            key = m.group(1)
        out[key] = (n, line.rstrip())
    return out

def parse_args():
    ap = argparse.ArgumentParser(
        description="Dump i18n keys for review, especially values still identical to English."
    )
    ap.add_argument("--lang", required=True, help="Target language code, e.g. de, sv, es")
    ap.add_argument("--prefix", action="append", help="Key prefix to include. Can be repeated.")
    ap.add_argument("--all-circlestack", action="store_true", help="Use prefix circlestack.")
    ap.add_argument("--from-line", type=int, default=0, help="Only include keys at or after this line in target file")
    ap.add_argument("--to-line", type=int, default=0, help="Only include keys at or before this line in target file")
    ap.add_argument(
        "--mode",
        choices=["same", "all", "different"],
        default="same",
        help="same = same as en.json, all = all matching keys, different = already different from en.json"
    )
    ap.add_argument("--format", choices=["lines", "json", "md", "kv"], default="lines")
    ap.add_argument("--limit", type=int, default=0)
    return ap.parse_args()

def main():
    args = parse_args()

    if args.all_circlestack:
        prefixes = ["circlestack."]
    elif args.prefix:
        prefixes = args.prefix
    else:
        prefixes = DEFAULT_PREFIXES

    en_path = I18N_DIR / "en.json"
    target_path = I18N_DIR / f"{args.lang}.json"

    en = load_json(en_path)
    target = load_json(target_path)
    line_info = find_line_info(target_path)

    items = []

    for key, value in target.items():
        if not wanted_key(key, prefixes):
            continue

        line_no, raw_line = line_info.get(key, (0, ""))

        if args.from_line and line_no and line_no < args.from_line:
            continue
        if args.to_line and line_no and line_no > args.to_line:
            continue

        en_value = en.get(key)
        same = key in en and value == en_value

        if args.mode == "same" and not same:
            continue
        if args.mode == "different" and same:
            continue

        items.append((line_no, key, value, en_value, raw_line, same, key in en))

    items.sort(key=lambda x: (x[0] or 10**9, x[1]))

    if args.limit:
        items = items[:args.limit]

    if args.format == "lines":
        for line_no, key, value, en_value, raw_line, same, exists_in_en in items:
            if not exists_in_en:
                status = "NO_EN_KEY"
            else:
                status = "SAME_AS_EN" if same else "DIFF_FROM_EN"
            print(f"{target_path}:{line_no}: [{status}] {raw_line}")

    elif args.format == "json":
        print(json.dumps({key: value for _, key, value, _, _, _, _ in items}, ensure_ascii=False, indent=2))

    elif args.format == "kv":
        for _, key, value, _, _, same, exists_in_en in items:
            if not exists_in_en:
                status = "NO_EN_KEY"
            else:
                status = "SAME_AS_EN" if same else "DIFF_FROM_EN"
            print(f"{key} = {value}    # {status}")

    elif args.format == "md":
        print(f"# {args.lang}: i18n review")
        print()
        for line_no, key, value, _, _, same, exists_in_en in items:
            if not exists_in_en:
                status = "NO_EN_KEY"
            else:
                status = "SAME_AS_EN" if same else "DIFF_FROM_EN"
            print(f"## {key}")
            print(f"Line: {line_no}")
            print(f"Status: {status}")
            print()
            print(value)
            print()

if __name__ == "__main__":
    main()
