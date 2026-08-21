#!/usr/bin/env python3

from pathlib import Path


root = Path(".")

route = Path(
    "server/src/routes/routes_admin_user_lifecycle.cpp"
).read_text()

registry = Path(
    "server/src/users_registry.cpp"
).read_text()

registry_header = Path(
    "server/src/users_registry.h"
).read_text()

service = Path(
    "server/src/cluster/cluster_safe_user_delete.cpp"
).read_text()

main = Path(
    "server/src/main.cpp"
).read_text()

files_put = Path(
    "server/src/routes/routes_files_put.inc"
).read_text()

workspace_wiring = Path(
    "server/src/routes/routes_admin_stats_badges.inc"
).read_text()

invite_header = Path(
    "server/src/routes/routes_workspace_external_invites.h"
).read_text()

workspace_header = Path(
    "server/src/routes/routes_workspaces_files.h"
).read_text()

v5_header = Path(
    "server/include/routes_v5.h"
).read_text()

invite = Path(
    "server/src/routes/routes_workspace_external_invites.cpp"
).read_text()

workspace_files = Path(
    "server/src/routes/routes_workspaces_files.cpp"
).read_text()

v5 = Path(
    "server/src/routes/routes_v5.cc"
).read_text()

errors = []


def require(condition, message):
    if not condition:
        errors.append(message)


# ---------------------------------------------------------------------------
# Admin lifecycle route helpers must own their captured context.
# ---------------------------------------------------------------------------

require(
    "auto context_ok = [ctx]() -> bool {" in route,
    "admin lifecycle context_ok must capture route context by value",
)

require(
    "auto reply_json_ctx = [ctx](" in route,
    "admin lifecycle reply helper must capture route context by value",
)

require(
    "auto context_ok = [&]" not in route and
    "auto reply_json_ctx = [&]" not in route,
    "admin lifecycle route helpers must not retain registration-stack references",
)


# ---------------------------------------------------------------------------
# UsersRegistry primitive remains persisted and fail-closed.
# ---------------------------------------------------------------------------

require(
    "erase_persisted_if_unplaced(" in route,
    "lifecycle delete must retain standalone persisted-delete fallback",
)

require(
    "if (!iterator->second.home_node.empty())" in registry,
    "persisted delete must check placement under UsersRegistry lock",
)

require(
    "by_fp_[previous.fingerprint] =" in registry,
    "failed pre-commit delete must restore the complete previous user",
)


# Raw erase must no longer be public API.

private_pos = registry_header.find(
    "private:"
)

erase_decl_pos = registry_header.find(
    "bool erase(const std::string& fp_hex);"
)

require(
    private_pos >= 0 and
    erase_decl_pos > private_pos,
    "UsersRegistry::erase must be private/internal-only",
)


# ---------------------------------------------------------------------------
# All production route / rollback callers must avoid raw erase().
# ---------------------------------------------------------------------------

production_sources = []

routes_root = Path(
    "server/src/routes"
)

for suffix in (
    "*.cpp",
    "*.cc",
    "*.inc",
):
    production_sources.extend(
        routes_root.rglob(suffix)
    )

src_root = Path(
    "server/src"
)

for suffix in (
    "*.cpp",
    "*.cc",
    "*.inc",
):
    production_sources.extend(
        src_root.glob(suffix)
    )

for path in sorted(set(production_sources)):
    text = path.read_text()

    for forbidden in (
        "users->erase(",
        "users.erase(",
    ):
        require(
            forbidden not in text,
            f"{path}: direct UsersRegistry erase caller remains: "
            f"{forbidden}",
        )


# ---------------------------------------------------------------------------
# Shared Director delete transaction.
# ---------------------------------------------------------------------------

lock_pos = service.find(
    "transition_lock("
)

durability_pos = service.find(
    "state_store.ensure_durable(",
    lock_pos,
)

confirmed_pos = service.find(
    ".get(fingerprint)",
    durability_pos,
)

delete_pos = service.find(
    "users.erase_persisted_if_unplaced(",
    confirmed_pos,
)

require(
    lock_pos >= 0 and
    durability_pos > lock_pos and
    confirmed_pos > durability_pos and
    delete_pos > confirmed_pos,
    "Director physical delete must lock, ensure durable sync state, "
    "check confirmed authorization, then persist deletion",
)

require(
    "if (!current->home_node.empty())" in service,
    "Director delete must block a currently placed user",
)

require(
    "cluster_revoke_pending" in service,
    "Director delete must expose confirmed-revoke-pending state",
)


# ---------------------------------------------------------------------------
# Reconciliation and deletion share the same transition boundary.
# ---------------------------------------------------------------------------

worker_callback = main.find(
    "director_storage_projection_reconcile_worker_config"
    "\n        .reconcile_once ="
)

worker_lock = main.find(
    "transition_lock(",
    worker_callback,
)

worker_reconcile = main.find(
    ".reconcile_once();",
    worker_lock,
)

require(
    worker_callback >= 0 and
    worker_lock > worker_callback and
    worker_reconcile > worker_lock,
    "production reconciler must hold transition mutex before reconcile_once",
)

require(
    "cluster_safe_user_delete =" in main and
    "erase_user_persisted_cluster_safe(" in main,
    "main must compose the shared Director delete callback",
)


# ---------------------------------------------------------------------------
# Every relevant route context receives the same callback.
# ---------------------------------------------------------------------------

require(
    "admin_user_lifecycle_ctx.erase_user_persisted_cluster_safe ="
    in files_put and
    "cluster_safe_user_delete" in files_put,
    "admin lifecycle must receive shared cluster-safe delete callback",
)

require(
    "ws_file_deps.erase_user_persisted_cluster_safe ="
    in workspace_wiring,
    "workspace file routes must receive cluster-safe delete callback",
)

require(
    "ws_external_invite_deps.erase_user_persisted_cluster_safe ="
    in workspace_wiring,
    "external invite routes must receive cluster-safe delete callback",
)

require(
    "v5.erase_user_persisted_cluster_safe =" in main,
    "v5 rollback routes must receive cluster-safe delete callback",
)

for name, text in (
    ("external invite", invite_header),
    ("workspace files", workspace_header),
    ("v5", v5_header),
):
    require(
        "erase_user_persisted_cluster_safe" in text,
        f"{name} context must expose cluster-safe delete callback",
    )


# ---------------------------------------------------------------------------
# Orphan cleanup must delete user before destroying credentials.
# ---------------------------------------------------------------------------

invite_function = invite.find(
    "bool workspace_external_delete_orphan_user_local("
)

invite_delete = invite.find(
    "workspace_external_erase_user_persisted_local(",
    invite_function,
)

invite_credentials = invite.find(
    "workspace_external_mark_opaque_enrollments_used_for_fp_local(",
    invite_delete,
)

require(
    invite_function >= 0 and
    invite_delete > invite_function and
    invite_credentials > invite_delete,
    "external invite orphan cleanup must decide/commit user deletion "
    "before credential cleanup",
)

workspace_function = workspace_files.find(
    "static bool ws_files_delete_orphan_external_user_local("
)

workspace_delete = workspace_files.find(
    "ws_files_erase_user_persisted_local(",
    workspace_function,
)

workspace_credentials = workspace_files.find(
    "ws_files_mark_opaque_enrollments_used_for_fp_local(",
    workspace_delete,
)

require(
    workspace_function >= 0 and
    workspace_delete > workspace_function and
    workspace_credentials > workspace_delete,
    "workspace orphan cleanup must decide/commit user deletion "
    "before credential cleanup",
)


# ---------------------------------------------------------------------------
# Rollback paths use persisted helpers.
# ---------------------------------------------------------------------------

require(
    "workspace_external_rollback_user_record_local(" in invite,
    "external invite rollback must use persisted delete helper",
)

require(
    "(void)workspace_external_rollback_user_record_local(deps, fp_hex);\n"
    "                creds.erase(login);" not in invite,
    "credential cleanup must not continue after an unchecked user rollback",
)

require(
    "routes_v5_rollback_new_user(" in v5,
    "v5 rollback must use persisted delete helper",
)


if errors:
    for error in errors:
        print(f"FAIL: {error}")

    raise SystemExit(1)

print("OK: persisted user-delete caller guards passed")
