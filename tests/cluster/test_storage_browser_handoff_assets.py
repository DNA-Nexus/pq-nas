#!/usr/bin/env python3

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]

DIRECTOR = (
    ROOT /
    "server/src/static/handoff/director.js"
)

BOOTSTRAP = (
    ROOT /
    "server/src/static/handoff/storage_bootstrap.js"
)

FINISH = (
    ROOT /
    "server/src/static/handoff/storage_finish.js"
)

CORE = (
    ROOT /
    "server/src/routes/routes_core_ui_shell.cpp"
)


def fail(message: str) -> None:
    raise SystemExit(f"FAIL: {message}")


for path in (DIRECTOR, BOOTSTRAP, FINISH):
    if not path.is_file():
        fail(f"missing handoff asset: {path}")

    source = path.read_text(encoding="utf-8")

    for forbidden in (
        "localStorage",
        "sessionStorage",
        "document.cookie",
        "console.",
        "document.referrer",
        "X-Forwarded-",
        "eval(",
    ):
        if forbidden in source:
            fail(
                f"{path.name} contains forbidden browser primitive: "
                f"{forbidden}"
            )

    history_pos = source.find(
        "window.history.replaceState("
    )
    fetch_pos = source.find("fetch(")

    if history_pos < 0 or fetch_pos < 0:
        fail(
            f"{path.name} must strip browser history and perform fetch"
        )

    if history_pos > fetch_pos:
        fail(
            f"{path.name} must strip URL state before first fetch"
        )


director = DIRECTOR.read_text(encoding="utf-8")
bootstrap = BOOTSTRAP.read_text(encoding="utf-8")
finish = FINISH.read_text(encoding="utf-8")
core = CORE.read_text(encoding="utf-8")


for required in (
    "/api/storage/handoff/begin",
    "/api/storage/handoff/start",
    "/storage/handoff/bootstrap",
    "/storage/handoff/finish#handoff=",
):
    if required not in director:
        fail(
            f"Director browser flow missing: {required}"
        )


for required in (
    "/api/storage/handoff/preauth",
    "body.browser_binding_hash",
    "body.director_origin",
    "/storage/handoff/director#handoff=",
):
    if required not in bootstrap:
        fail(
            f"Storage bootstrap flow missing: {required}"
        )


for required in (
    "/api/storage/handoff/consume",
    "application/x-www-form-urlencoded",
    'window.location.replace("/app")',
):
    if required not in finish:
        fail(
            f"Storage finish flow missing: {required}"
        )


if "Storage routing is not available yet." in core:
    fail(
        "Director /app must no longer use temporary 503 routing"
    )

for required in (
    '"/storage/handoff/director"',
    '"/storage/handoff/bootstrap"',
    '"/storage/handoff/finish"',
    "default-src 'none'",
    "frame-ancestors 'none'",
    '"Referrer-Policy"',
    '"no-referrer"',
    '"X-Frame-Options"',
    '"DENY"',
):
    if required not in core:
        fail(
            f"handoff page security contract missing: {required}"
        )


print("OK: storage browser handoff asset guards passed")
