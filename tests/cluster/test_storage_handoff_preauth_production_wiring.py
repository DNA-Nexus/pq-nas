from pathlib import Path

from production_translation_unit import (
    read_main_translation_unit_text,
)

root = Path(__file__).resolve().parents[2]

main = (root / "server/src/main.cpp").read_text(
    encoding="utf-8"
)

route = (
    root
    / "server/src/routes/routes_storage_handoff_preauth.cpp"
).read_text(
    encoding="utf-8"
)

# Security: negative production-wiring assertions must inspect the
# implementation .inc files compiled into main.cpp as well as the
# literal main.cpp source. This closes historical RT-011.
production_tu = read_main_translation_unit_text()

errors = []


def require(condition: bool, message: str) -> None:
    if not condition:
        errors.append(message)


require(
    '"/api/storage/handoff/preauth"' in route,
    "preauth route module must define the browser preauth endpoint",
)

# Security architecture invariant:
# browser handoff stays closed in production until preauth, Director ticket
# issue, browser consume, Storage session auth, and E2E wiring are complete.
require(
    "register_storage_handoff_preauth_routes(" not in production_tu,
    "Storage browser preauth must not yet be production-registered",
)

require(
    '"/api/storage/handoff/preauth"' not in production_tu,
    "Storage browser preauth endpoint must not be hand-wired in main.cpp",
)

if errors:
    for error in errors:
        print(f"FAIL: {error}")

    raise SystemExit(1)

print("OK: Storage handoff preauth production wiring remains closed")
