from pathlib import Path

root = Path(__file__).resolve().parents[2]

main = (root / "server/src/main.cpp").read_text(
    encoding="utf-8"
)

route = (
    root
    / "server/src/routes/routes_storage_handoff_consume.cpp"
).read_text(
    encoding="utf-8"
)

errors = []


def require(condition: bool, message: str) -> None:
    if not condition:
        errors.append(message)


require(
    "register_storage_handoff_consume_routes(" in route,
    "consume route module must define route registration",
)

# Security architecture invariant:
# browser handoff remains production-disabled until the complete Storage
# request-auth chain and browser E2E handoff are ready.
require(
    "register_storage_handoff_consume_routes(" not in main,
    "Storage browser consume must not yet be production-registered",
)

require(
    '"/api/storage/handoff/consume"' not in main,
    "Storage browser consume endpoint must not be hand-wired in main.cpp",
)

require(
    "register_storage_handoff_preauth_routes(" not in main,
    "Storage browser preauth must remain production-disabled",
)

require(
    "register_internal_storage_handoff_routes(" not in main,
    "internal Storage handoff ticket route must remain production-disabled",
)

if errors:
    for error in errors:
        print(f"FAIL: {error}")

    raise SystemExit(1)

print("OK: Storage handoff consume production wiring remains closed")
