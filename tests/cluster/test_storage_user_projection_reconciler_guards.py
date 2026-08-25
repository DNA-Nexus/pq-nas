#!/usr/bin/env python3

from pathlib import Path

from production_translation_unit import (
    read_main_translation_unit_text,
)

source = Path(
    "server/src/cluster/"
    "storage_user_projection_reconciler.cpp"
).read_text()

header = Path(
    "server/src/cluster/"
    "storage_user_projection_reconciler.h"
).read_text()

users_source = Path(
    "server/src/users_registry.cpp"
).read_text()

users_header = Path(
    "server/src/users_registry.h"
).read_text()

main = Path(
    "server/src/main.cpp"
).read_text()

# Security: main.cpp includes route/helper .inc files inside the production
# translation unit. RT-017 absence checks must cover those compiled sources
# too, not just the top-level main.cpp text.
production_tu = read_main_translation_unit_text()

cmake = Path(
    "CMakeLists.txt"
).read_text()

obsolete_sync_cpp = Path(
    "server/src/cluster/"
    "storage_user_projection_sync_service.cpp"
)

obsolete_sync_h = Path(
    "server/src/cluster/"
    "storage_user_projection_sync_service.h"
)

errors = []


def require(condition, message):
    if not condition:
        errors.append(message)


# RT-017: the old state-blind immediate-sync service was never
# production-wired and could bypass durable confirmed-sync-state tracking.
# Keep it deleted rather than allowing a second authorization write path.
require(
    not obsolete_sync_cpp.exists() and
    not obsolete_sync_h.exists(),
    "obsolete state-blind projection sync service must stay deleted",
)

require(
    "StorageUserProjectionSyncService" not in production_tu,
    "production translation unit must not reintroduce obsolete projection sync service",
)

require(
    "pqnas_storage_user_projection_sync_service" not in cmake and
    "test_storage_user_projection_sync_service" not in cmake,
    "obsolete projection sync CMake targets must stay removed",
)

require(
    "config_.users->snapshot()" in source,
    "reconciler must operate from a stable Director user snapshot",
)

# RT-016: a new authorization target is made deletion-visible while holding
# the shared boundary, but all Storage HTTPS I/O remains outside that mutex.
require(
    "std::shared_ptr<std::mutex>\n        transition_mutex;" in header,
    "reconciler config must expose the shared transition boundary",
)

prepare_start = source.find(
    "bool prepare_pending_authorization_transition("
)

prepare_end = source.find(
    "bool erase_pending_transition(",
    prepare_start,
)

prepare_block = (
    source[prepare_start:prepare_end]
    if prepare_start >= 0 and prepare_end > prepare_start
    else ""
)

prepare_lock = prepare_block.find(
    "std::unique_lock<std::mutex>"
)

prepare_reread = prepare_block.find(
    "config.users->get(",
    prepare_lock,
)

prepare_journal = prepare_block.find(
    "persist_pending_transition(",
    prepare_reread,
)

require(
    prepare_start >= 0 and
    prepare_end > prepare_start and
    prepare_lock >= 0 and
    prepare_reread > prepare_lock and
    prepare_journal > prepare_reread,
    "RT-016 boundary must lock, re-read Director user, then journal pending",
)

require(
    "sync_projection(" not in prepare_block and
    "audit_projection_range(" not in prepare_block,
    "RT-016 transition critical section must contain no network I/O",
)

require(
    source.count(
        "prepare_pending_authorization_transition("
    ) == 3,
    "both initial and A-to-B authorization paths must use the narrow boundary",
)

# NEW-005: direct sends from the long-lived reconciliation snapshot must
# re-read the authoritative user immediately before network I/O.
freshness_start = source.find(
    "bool projection_still_authoritative_before_send("
)

freshness_end = source.find(
    "bool prepare_pending_authorization_transition(",
    freshness_start,
)

freshness_block = (
    source[freshness_start:freshness_end]
    if freshness_start >= 0 and
    freshness_end > freshness_start
    else ""
)

require(
    freshness_start >= 0 and
    freshness_end > freshness_start and
    "config.users->get(" in freshness_block and
    "projection_equal(" in freshness_block,
    "NEW-005 pre-send freshness helper must re-read and compare Director state",
)

require(
    "sync_projection(" not in freshness_block and
    "audit_projection_range(" not in freshness_block,
    "NEW-005 freshness check must contain no network I/O",
)

require(
    source.count(
        "projection_still_authoritative_before_send("
    ) == 5,
    "NEW-005 / FINAL-003 helper must guard every snapshot-derived live projection send",
)

require(
    source.count(
        "if (!projection_still_authoritative_before_send(\n"
        "                        config_,\n"
        "                        recovered,\n"
        "                        &result))"
    ) == 1,
    "FINAL-003 pending recovery must revalidate Director authority before send",
)

require(
    source.count(
        "if (!projection_still_authoritative_before_send(\n"
        "                    config_,\n"
        "                    current,\n"
        "                    &result))"
    ) == 1 and
    source.count(
        "if (!projection_still_authoritative_before_send(\n"
        "                config_,\n"
        "                current,\n"
        "                &result))"
    ) == 1,
    "NEW-005 equal-revision verification and same-Node refresh must fail stale before send",
)

require(
    "if (!projection_still_authoritative_before_send(\n"
    "                        config_,\n"
    "                        deferred->second,\n"
    "                        &result))" in source,
    "NEW-005 audit repair must re-read Director state before replay",
)

require(
    "previous_node_tombstone(" in source,
    "placement reconciliation must construct an old-Node tombstone",
)

require(
    "tombstone.enabled = false;" in source,
    "old-Node tombstone must disable authorization",
)

require(
    "tombstone.storage_allocated = false;" in source,
    "old-Node tombstone must remove storage allocation",
)

# Scope A->B ordering to the placed-user move branch. Explicit unplacement
# now has its own earlier tombstone and must not weaken this invariant.
move_branch = source.find(
    "// confirmed revision is older than current Director revision."
)

move_tombstone = source.find(
    "const StorageUserProjection tombstone =",
    move_branch,
)

revoke_pos = source.find(
    "config_.sync_projection(\n"
    "                        tombstone",
    move_tombstone,
)

current_pos = source.find(
    "config_.sync_projection(\n"
    "                        current",
    revoke_pos + 1 if revoke_pos >= 0 else move_tombstone,
)

require(
    move_branch >= 0 and
    move_tombstone >= move_branch and
    revoke_pos >= move_tombstone and
    current_pos > revoke_pos,
    "old Node must be revoked before current Node is synchronized",
)

# Explicit A->empty ordering:
# durable Director revision -> newer tombstone to the last confirmed Node ->
# CAS erase of that exact confirmed state.
unplaced_branch = source.find(
    "if (user.home_node.empty())"
)

unplaced_tombstone = source.find(
    "unplaced_user_tombstone(",
    unplaced_branch,
)

unplaced_revoke = source.find(
    "config_.sync_projection(\n"
    "                        tombstone",
    unplaced_tombstone,
)

unplaced_erase = source.find(
    "erase_if_current(",
    unplaced_revoke,
)

placed_projection_build = source.find(
    "StorageUserProjection current;",
    unplaced_branch,
)

require(
    unplaced_branch >= 0 and
    unplaced_tombstone >= unplaced_branch and
    unplaced_revoke >= unplaced_tombstone and
    unplaced_erase > unplaced_revoke and
    (
        placed_projection_build < 0 or
        unplaced_erase < placed_projection_build
    ),
    "unplacement must revoke the last confirmed Node before erasing sync-state",
)

require(
    "confirmed->revision >\n"
    "                user.storage_projection_revision" in source,
    "unplacement must reject newer confirmed revisions",
)

require(
    "confirmed->revision ==\n"
    "                user.storage_projection_revision" in source,
    "unplacement must fail closed on ambiguous equal revisions",
)

require(
    "++result.revoked_unplaced;" in source,
    "successful explicit unplacement must be surfaced separately",
)

require(
    "confirmed->revision >\n            current.revision" in source,
    "newer confirmed state must block Director rollback",
)

require(
    "confirmed->revision ==\n            current.revision" in source,
    "same-revision state must be explicitly checked",
)

require(
    "confirmed->home_node !=\n                current.home_node" in source,
    "same-revision different-Node state must fail closed",
)

require(
    "store->mark_synced(" in source,
    "network success must be followed by persistent confirmation",
)

require(
    "orphaned_states" in source and
    "OrphanAuthorizationRepair" in source and
    '"cluster.storage_projection.orphan_revoked"' in source and
    '"mismatch_kind"' in source and
    '"unexpected_authorization"' in source,
    "NEW-006 unexpected authorization must be revoked and audit-correlated",
)

require(
    "list_audit_online_nodes" in header and
    "audit_deferred_nodes" in header and
    "audit_nodes_this_pass" in source,
    "NEW-006 offline v2 Nodes must defer audit without legacy downgrade",
)

revision_floor_name = (
    "raise_storage_projection_revision_floor_"
    "persisted_if_unplaced"
)

revision_floor_start = users_source.find(
    "bool UsersRegistry::\n" +
    revision_floor_name + "("
)

revision_floor_end = users_source.find(
    "/*\nset_status(), set_role()",
    revision_floor_start,
)

revision_floor_block = (
    users_source[
        revision_floor_start:
        revision_floor_end
    ]
    if revision_floor_start >= 0 and
    revision_floor_end > revision_floor_start
    else ""
)

require(
    revision_floor_name in users_header and
    revision_floor_start >= 0 and
    "ensure_durable_locked(error)" in
        revision_floor_block and
    "home_node.empty()" in
        revision_floor_block and
    "expected_revision" in
        revision_floor_block and
    "registry_commit_visible" in
        revision_floor_block,
    "NEW-006 Director revision floor must use durable unplaced-user CAS",
)

require(
    '"cluster.storage_projection.synced"' in source and
    '"cluster.storage_projection.moved"' in source and
    '"cluster.storage_projection.revoked"' in source,
    "successful projection reconciliation transitions must be audit-correlated",
)

require(
    "emit_reconcile_audit_best_effort(" in source,
    "reconciler must use the bounded best-effort audit helper",
)

# Still foundation-only. Worker/thread and production wiring come later.


# Production phase: deterministic reconciler is now intentionally Director-wired.
require(
    "StorageUserProjectionReconciler" in main,
    "Director must production-wire the deterministic reconciler",
)

require(
    "director_storage_projection_reconciler" in main,
    "production worker must delegate through the Director reconciler",
)

require(
    "director_storage_projection_reconciler_config.audit_emit =" in main and
    "audit_append(audit);" in main,
    "Director must bridge reconciliation success events into the audit log",
)

if errors:
    for error in errors:
        print(f"FAIL: {error}")

    raise SystemExit(1)

print("OK: storage projection reconciler structural guards passed")
