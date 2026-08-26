#!/usr/bin/env python3

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

routes = (
    ROOT / "server/src/routes/routes_shares_public.inc"
).read_text()

strict = (
    ROOT / "server/include/strict_session_cookie.h"
).read_text()

checks = {
    "dedicated canonical recipient validator exists":
        "canonical_pqnas_share_recipient_cookie_value(" in strict,

    "strict parser accepts credential-specific validator":
        "StrictSessionCookieValueValidator value_validator" in strict and
        "!value_validator(value)" in strict,

    "three live recipient-cookie reads use strict validator":
        routes.count(
            "pqnas::canonical_pqnas_share_recipient_cookie_value"
        ) == 3,

    "open/init reuses already authenticated session":
        "ctx.session.session_id;" in routes,

    "old exact recipient needle parser removed":
        'needle = "pqnas_share_recipient="' not in routes,

    "old named local recipient parser removed":
        'get_cookie_value_local("pqnas_share_recipient")' not in routes,

    "old public-share recipient parser removed":
        'get_cookie_value("pqnas_share_recipient")' not in routes,
}

failed = [name for name, ok in checks.items() if not ok]

if failed:
    for name in failed:
        print(f"FAIL: {name}")
    raise SystemExit(1)

print(
    "OK: FINAL-004 share-recipient cookie wiring uses "
    "strict unambiguous parsing"
)
