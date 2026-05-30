#!/usr/bin/env python3
import argparse
import json
import re
import sys
from pathlib import Path

I18N_DIR = Path("server/src/static/i18n")

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

def line_map(path):
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

ap = argparse.ArgumentParser(description="Compare one i18n language JSON against en.json.")
ap.add_argument("--lang", required=True, help="Target language, e.g. de")
ap.add_argument("--prefix", action="append", help="Only compare keys starting with this prefix. Can be repeated.")
ap.add_argument("--show", choices=["summary", "same", "missing", "extra", "different", "all"], default="summary")
ap.add_argument("--format", choices=["lines", "json"], default="lines")
ap.add_argument("--limit", type=int, default=0)
args = ap.parse_args()

en_path = I18N_DIR / "en.json"
target_path = I18N_DIR / f"{args.lang}.json"

en = load_json(en_path)
target = load_json(target_path)
target_lines = line_map(target_path)

def wanted(k):
    if not args.prefix:
        return True
    return any(k.startswith(p) for p in args.prefix)

en_keys = {k for k in en if wanted(k)}
target_keys = {k for k in target if wanted(k)}

missing = sorted(en_keys - target_keys)
extra = sorted(target_keys - en_keys)
common = sorted(en_keys & target_keys)

same = [k for k in common if target[k] == en[k]]
different = [k for k in common if target[k] != en[k]]

if args.show == "summary":
    print(f"compare: en.json -> {args.lang}.json")
    print(f"prefixes: {args.prefix or ['<all>']}")
    print(f"en keys:       {len(en_keys)}")
    print(f"{args.lang} keys:       {len(target_keys)}")
    print(f"common:        {len(common)}")
    print(f"missing:       {len(missing)}")
    print(f"extra:         {len(extra)}")
    print(f"same_as_en:    {len(same)}")
    print(f"different:     {len(different)}")
    sys.exit(0)

groups = {
    "same": same,
    "missing": missing,
    "extra": extra,
    "different": different,
}

if args.show == "all":
    selected = []
    for label, keys in groups.items():
        for k in keys:
            selected.append((label, k))
else:
    selected = [(args.show, k) for k in groups[args.show]]

if args.limit:
    selected = selected[:args.limit]

if args.format == "json":
    out = {}
    for label, k in selected:
        if label == "missing":
            out[k] = en.get(k)
        elif label == "extra":
            out[k] = target.get(k)
        elif label == "same":
            out[k] = target.get(k)
        elif label == "different":
            out[k] = {
                "en": en.get(k),
                args.lang: target.get(k),
            }
    print(json.dumps(out, ensure_ascii=False, indent=2))
    sys.exit(0)

for label, k in selected:
    if label == "missing":
        print(f"{target_path}:?: [MISSING_IN_{args.lang.upper()}] {k} = {en[k]}")
    elif label == "extra":
        line_no, raw = target_lines.get(k, (0, ""))
        print(f"{target_path}:{line_no}: [EXTRA_IN_{args.lang.upper()}] {raw}")
    elif label == "same":
        line_no, raw = target_lines.get(k, (0, ""))
        print(f"{target_path}:{line_no}: [SAME_AS_EN] {raw}")
    elif label == "different":
        line_no, raw = target_lines.get(k, (0, ""))
        print(f"{target_path}:{line_no}: [DIFF_FROM_EN] {raw}")
