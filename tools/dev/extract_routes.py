#!/usr/bin/env python3
from __future__ import annotations

import re
from pathlib import Path
from collections import defaultdict

ROOT = Path(__file__).resolve().parents[2]
MAIN = ROOT / "server/src/main.cpp"
OUT_DIR = ROOT / "docs/research"
OUT = OUT_DIR / "main_cpp_routes.md"

if not MAIN.exists():
    raise SystemExit(f"ERROR: missing {MAIN}")

text = MAIN.read_text(errors="replace")
lines = text.splitlines()

# Supports:
#   srv.Get("/path", ...)
#   srv.Post(R"(/regex/(.*))", ...)
#   server.Put(...)
#   app.Delete(...)
route_re = re.compile(
    r'\b(?:srv|server|app)\s*\.\s*'
    r'(Get|Post|Put|Delete|Patch|Options)\s*'
    r'\(\s*'
    r'(?:R"[^(\n]*\((.*?)\)[^"\n]*"|"(.*?)")'
)

routes = []

for lineno, line in enumerate(lines, start=1):
    m = route_re.search(line)
    if not m:
        continue

    method = m.group(1).upper()
    path = m.group(2) if m.group(2) is not None else m.group(3)
    path = path or ""

    routes.append({
        "line": lineno,
        "method": method,
        "path": path,
        "raw": line.strip(),
    })

groups = defaultdict(list)

for route in routes:
    path = route["path"]
    parts = [p for p in path.split("/") if p]

    if len(parts) >= 3 and parts[0] == "api":
        key = "/" + "/".join(parts[:3])
    elif len(parts) >= 2 and parts[0] == "api":
        key = "/" + "/".join(parts[:2])
    elif parts:
        key = "/" + parts[0]
    else:
        key = "/"

    groups[key].append(route)

sorted_groups = sorted(groups.items(), key=lambda kv: (-len(kv[1]), kv[0]))

OUT_DIR.mkdir(parents=True, exist_ok=True)

out = []
out.append("# main.cpp route map")
out.append("")
out.append(f"- Source: `server/src/main.cpp`")
out.append(f"- Total source lines: {len(lines)}")
out.append(f"- Route registrations found: {len(routes)}")
out.append("")

out.append("## Route groups")
out.append("")
out.append("| Count | Group |")
out.append("|---:|---|")
for key, items in sorted_groups:
    out.append(f"| {len(items)} | `{key}` |")
out.append("")

out.append("## Routes")
out.append("")
out.append("| Line | Method | Path |")
out.append("|---:|---|---|")
for route in routes:
    out.append(f"| {route['line']} | `{route['method']}` | `{route['path']}` |")
out.append("")

out.append("## Raw route lines")
out.append("")
for route in routes:
    out.append(f"### Line {route['line']}: {route['method']} `{route['path']}`")
    out.append("")
    out.append("```cpp")
    out.append(route["raw"])
    out.append("```")
    out.append("")

OUT.write_text("\n".join(out) + "\n")

print(f"Wrote {OUT.relative_to(ROOT)}")
print(f"Routes found: {len(routes)}")
print()
print("Largest route groups:")
for key, items in sorted_groups[:25]:
    print(f"{len(items):4d}  {key}")
