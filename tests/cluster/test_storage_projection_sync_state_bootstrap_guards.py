#!/usr/bin/env python3

from pathlib import Path


installer = Path(
    "tools/installer/pqnas_install.py"
).read_text(encoding="utf-8")

bootstrap = Path(
    "tools/installer/pqnas_director_projection_state.py"
).read_text(encoding="utf-8")

update_apply = Path(
    "server/src/updates/pqnas_update_apply.py"
).read_text(encoding="utf-8")

update_routes = Path(
    "server/src/updates/update_center_routes.cpp"
).read_text(encoding="utf-8")

main = Path(
    "server/src/main.cpp"
).read_text(encoding="utf-8")

errors = []


def require(condition: bool, message: str) -> None:
    if not condition:
        errors.append(message)


def function_body(source: str, name: str) -> str:
    marker = f"def {name}("
    start = source.find(marker)

    if start < 0:
        return ""

    next_function = source.find("\ndef ", start + len(marker))
    next_class = source.find("\nclass ", start + len(marker))

    ends = [
        value
        for value in (next_function, next_class)
        if value >= 0
    ]

    end = min(ends) if ends else len(source)
    return source[start:end]


# ---------------------------------------------------------------------------
# Bootstrap remains explicit and create-only.
# ---------------------------------------------------------------------------

require(
    "bootstrap_approved" in bootstrap,
    "Director sync-state bootstrap must require explicit approval",
)

require(
    "if not bootstrap_approved:" in bootstrap,
    "missing Director sync-state must fail without explicit approval",
)

require(
    "replace_existing=False" in bootstrap,
    "Director sync-state bootstrap must use create-only persistence",
)

require(
    "file_mode=0o600" in bootstrap,
    "Director sync-state bootstrap must create private mode-0600 state",
)

require(
    'DIRECTOR_PROJECTION_SYNC_STATE_SCHEMA_VERSION = 1'
    in bootstrap,
    "Director bootstrap schema must remain explicitly versioned",
)

require(
    '"states": []' in bootstrap,
    "Director bootstrap may seed only an empty confirmed-sync snapshot",
)

require(
    'if os.path.lexists(target):\n        return "preserved"'
    in bootstrap,
    "existing Director sync-state must never be reset by bootstrap",
)


# ---------------------------------------------------------------------------
# General mutable-config seeding must never acquire this authority.
# ---------------------------------------------------------------------------

ensure_config_body = function_body(
    installer,
    "ensure_config_files",
)

require(
    bool(ensure_config_body),
    "installer ensure_config_files() must remain present",
)

require(
    "storage_projection_sync_state.json"
    not in ensure_config_body,
    "general mutable-config seeding must not create Director sync-state",
)

require(
    "ensure_director_projection_sync_state("
    not in ensure_config_body,
    "general mutable-config seeding must not invoke Director bootstrap",
)


# ---------------------------------------------------------------------------
# Installer wiring requires typed acknowledgement and one-shot approval.
# ---------------------------------------------------------------------------

require(
    "BOOTSTRAP DIRECTOR STATE" in bootstrap,
    "Director bootstrap must retain a distinct typed confirmation suffix",
)

require(
    "director_projection_sync_state_bootstrap_approved: bool = False"
    in installer,
    "Director bootstrap approval must default to false",
)

require(
    "director_projection_bootstrap_confirmation_required("
    in installer,
    "confirmation screen must calculate Director bootstrap requirement",
)

require(
    "st.director_projection_sync_state_bootstrap_approved"
    in installer,
    "execution path must pass the explicit approval bit",
)

require(
    "binary_rollback_restart_allowed(" in installer,
    "installer must use the tested rollback restart policy",
)

require(
    'install_mode == "existing"' in bootstrap and
    'projection_state_action != "created"' in bootstrap,
    "rollback policy must require preserved existing-filesystem authority",
)

require(
    "restart_after_rollback=restart_after_rollback"
    in installer,
    "rollback screen must receive the restart safety decision",
)

require(
    "if not self.restart_after_rollback:"
    in installer,
    "rollback implementation must support restoring binaries without restart",
)

require(
    "pqnas.service was intentionally "
    in installer,
    "unsafe rollback path must clearly report that service remains stopped",
)

require(
    """finally:
                # Security: approval cannot leak into a later installer action.
                st.director_projection_sync_state_bootstrap_approved = False"""
    in installer,
    "Director bootstrap approval must be cleared after execution attempt",
)


# ---------------------------------------------------------------------------
# Update Center must remain outside the bootstrap trust boundary.
# ---------------------------------------------------------------------------

for forbidden in (
    "pqnas_director_projection_state",
    "ensure_director_projection_sync_state",
    "storage_projection_sync_state.json",
    "BOOTSTRAP DIRECTOR STATE",
):
    require(
        forbidden not in update_apply,
        f"Update Center apply helper must not reference {forbidden}",
    )

    require(
        forbidden not in update_routes,
        f"Update Center routes must not reference {forbidden}",
    )


# ---------------------------------------------------------------------------
# Production runtime remains fail-closed if authoritative state is absent.
# ---------------------------------------------------------------------------

require(
    '"storage_projection_sync_state.json"' in main,
    "production Director must retain its fixed sync-state filename",
)

require(
    "sync-state file is missing or inaccessible" in main,
    "production Director must still fail closed when sync-state is missing",
)

require(
    "director_storage_projection_sync_state_store.load(" in main,
    "production Director must still load validated persistent sync-state",
)


if errors:
    for error in errors:
        print(f"FAIL: {error}")

    raise SystemExit(1)

print(
    "OK: storage projection sync-state bootstrap structural guards passed"
)
