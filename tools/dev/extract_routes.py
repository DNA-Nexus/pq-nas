#!/usr/bin/env python3
import argparse
import re
from pathlib import Path
from datetime import datetime

METHODS = [
    "Get", "Post", "Put", "Delete", "Patch", "Options"
]

ROUTE_RE = re.compile(
    r'''
    (?P<server>\b[a-zA-Z_][a-zA-Z0-9_]*)
    \s*\.\s*
    (?P<method>Get|Post|Put|Delete|Patch|Options)
    \s*\(
    \s*
    (?P<route>
        R"[^"]*\([^)]*\)[^"]*" |
        "([^"\\]|\\.)*"
    )
    ''',
    re.VERBOSE,
)

def clean_route(raw: str) -> str:
    raw = raw.strip()

    # C++ raw string literal: R"(/path/([^/]+))"
    if raw.startswith('R"'):
        start = raw.find("(")
        end = raw.rfind(")")
        if start != -1 and end != -1 and end > start:
            return raw[start + 1:end]

    # Normal C++ string literal: "/api/v4/foo"
    if raw.startswith('"') and raw.endswith('"'):
        return raw[1:-1]

    return raw

def auth_guess(route: str) -> str:
    if route.startswith("/api/debug/"):
        return "Debug/dev only"

    if (
        route.startswith("/api/public/")
        or route.startswith("/dz/")
        or route.startswith("/s/")
        or route.startswith("/pq/invite/")
    ):
        return "Public token/link"

/")
        or route.startswith("/s/")
        or route.startswith("/pq/invite/")
    ):
        return "Public token/link"

    if (
        route.startswith("/api/v5/verify")
        or route.startswith("/api/v4/session")
        or route.startswith("/login")
        or route.startswith("/auth")
        or route in ["/wait-approval", "/success"]
    ):
        return "Public entry / session flow"

    if (
        route.startswith("/api/admin/")
        or route.startswith("/api/v4/admin/")
        or route.startswith("/admin")
        or route.startswith("/static/admin")
    ):
        return "Admin session"

    if route.startswith("/api/v4/"):
        return "User session"

    if route.startswith("/static/") or route in ["/", "/favicon.ico"]:
        return "Public/static"

    if route in ["/app", "/system"]:
        return "Page route"

    if route.startswith("/apps/"):
        return "Bundled app asset route"

    return "Unknown"

def purpose_guess(method: str, route: str) -> str:
    low = route.lower()

    if "session" in low or "login" in low or "auth" in low:
        return "Authentication/session related endpoint."
    if "user" in low or "users" in low:
        return "User management or user profile related endpoint."
    if "admin" in low:
        return "Admin management endpoint."
    if "file" in low or "files" in low:
        return "File operation endpoint."
    if "share" in low or "shares" in low:
        return "File sharing or public link endpoint."
    if "workspace" in low or "workspaces" in low:
        return "Workspace collaboration endpoint."
    if "dropzone" in low or "dropzones" in low or low.startswith("/dz/"):
        return "Drop Zone upload/link endpoint."
    if "gallery" in low or "photo" in low or "album" in low:
        return "Gallery/photo related endpoint."
    if "circle" in low:
        return "Circle Stack social/federation endpoint."
    if "echo" in low:
        return "Echo Stack bookmark/archive endpoint."
    if "storage" in low or "pool" in low or "drive" in low or "disk" in low:
        return "Storage, pool, or drive management endpoint."
    if "health" in low or "status" in low:
        return "Health/status endpoint."

    if method == "Get":
        return "Read or page-serving endpoint."
    if method == "Post":
        return "Create/action endpoint."
    if method == "Put":
        return "Replace/update endpoint."
    if method == "Patch":
        return "Partial update endpoint."
    if method == "Delete":
        return "Delete endpoint."

    return "TODO: describe purpose."

def extract(path: Path):
    text = path.read_text(errors="replace")
    lines = text.splitlines()

    results = []
    for idx, line in enumerate(lines, start=1):
        for m in ROUTE_RE.finditer(line):
            method = m.group("method").upper()
            route = clean_route(m.group("route"))
            results.append({
                "method": method,
                "route": route,
                "line": idx,
                "source": str(path),
                "auth": auth_guess(route),
                "purpose": purpose_guess(method, route),
            })

    return results

def write_markdown(routes, output: Path, title: str):
    now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    with output.open("w", encoding="utf-8") as f:
        f.write(f"# {title}\n\n")
        f.write("Status: generated draft.\n\n")
        f.write("This document is an initial route inventory generated from source code.\n")
        f.write("Descriptions and auth classifications should be reviewed manually before treating this as authoritative API documentation.\n\n")
        f.write(f"Generated: {now}\n\n")
        f.write("## Route Summary\n\n")
        f.write("| Method | Route | Auth | Source |\n")
        f.write("|---|---|---|---|\n")
        for r in routes:
            f.write(f"| `{r['method']}` | `{r['route']}` | {r['auth']} | `{r['source']}:{r['line']}` |\n")

        f.write("\n## Route Details\n\n")
        for r in routes:
            f.write(f"### {r['method']} `{r['route']}`\n\n")
            f.write(f"Purpose:\n{r['purpose']}\n\n")
            f.write(f"Auth:\n{r['auth']}\n\n")
            f.write("Request:\nTODO.\n\n")
            f.write("Response:\nTODO.\n\n")
            f.write(f"Source:\n`{r['source']}:{r['line']}`\n\n")
            f.write("---\n\n")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("inputs", nargs="+", help="Source files to scan")
    ap.add_argument("-o", "--output", default="docs/technical/api-main-routes.md")
    ap.add_argument("--title", default="Main API Route Inventory")
    args = ap.parse_args()

    all_routes = []
    for item in args.inputs:
        path = Path(item)
        if not path.exists():
            print(f"missing: {path}")
            continue
        all_routes.extend(extract(path))

    all_routes.sort(key=lambda r: (r["route"], r["method"], r["source"], r["line"]))

    out = Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    write_markdown(all_routes, out, args.title)

    print(f"wrote {out}")
    print(f"routes found: {len(all_routes)}")

if __name__ == "__main__":
    main()
