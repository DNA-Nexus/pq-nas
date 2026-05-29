#!/usr/bin/env python3
import argparse
import json
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

def parse_args():
    ap = argparse.ArgumentParser(
        description="Dump i18n keys whose target-language value is still identical to English."
    )
    ap.add_argument("--lang", help="Target language code, e.g. sv, de, es, fr, it, tr, zh, uk, et, pl")
    ap.add_argument("--all-langs", action="store_true", help="Show summary for all non-English language files")
    ap.add_argument("--prefix", action="append", help="Key prefix to include. Can be repeated.")
    ap.add_argument("--all-circlestack", action="store_true", help="Use prefix circlestack. instead of achievement-only prefixes")
    ap.add_argument("--format", choices=["json", "kv", "md"], default="json")
    ap.add_argument("--limit", type=int, default=0, help="Limit number of dumped keys. 0 means no limit.")
    ap.add_argument("--include-fi", action="store_true", help="Include fi when using --all-langs summary")
    return ap.parse_args()

def wanted_key(key, prefixes):
    return any(key.startswith(p) for p in prefixes)

def untranslated_items(en, target, prefixes):
    out = []
    for key, en_value in en.items():
        if not wanted_key(key, prefixes):
            continue
        if key not in target:
            continue
        if target.get(key) == en_value:
            out.append((key, en_value))
    return out

def print_items(lang, items, fmt, limit):
    shown = items if not limit else items[:limit]

    if fmt == "json":
        print(json.dumps({k: v for k, v in shown}, ensure_ascii=False, indent=2))
        return

    if fmt == "kv":
        for k, v in shown:
            print(f'{k} = {v}')
        return

    if fmt == "md":
        print(f"# {lang}: untranslated strings")
        print()
        for k, v in shown:
            print(f"## {k}")
            print(v)
            print()
        return

def main():
    args = parse_args()

    if not I18N_DIR.exists():
        die(f"run from repo root; missing {I18N_DIR}")

    en = load_json(I18N_DIR / "en.json")

    if args.all_circlestack:
        prefixes = ["circlestack."]
    elif args.prefix:
        prefixes = args.prefix
    else:
        prefixes = DEFAULT_PREFIXES

    if args.all_langs:
        langs = sorted(p.stem for p in I18N_DIR.glob("*.json") if p.stem != "en")
        if not args.include_fi:
            langs = [x for x in langs if x != "fi"]

        for lang in langs:
            target = load_json(I18N_DIR / f"{lang}.json")
            items = untranslated_items(en, target, prefixes)
            missing = [
                k for k in en
                if wanted_key(k, prefixes) and k not in target
            ]
            print(f"{lang:2} untranslated={len(items):4} missing={len(missing):4}")
        return

    if not args.lang:
        die("use --lang <code> or --all-langs")

    target = load_json(I18N_DIR / f"{args.lang}.json")
    items = untranslated_items(en, target, prefixes)

    print_items(args.lang, items, args.format, args.limit)

if __name__ == "__main__":
    main()
