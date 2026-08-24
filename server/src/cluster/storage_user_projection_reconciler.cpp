#include "cluster/storage_user_projection_reconciler.h"

#include "cluster/storage_user_projection_builder.h"
#include "cluster/storage_user_projection_sync_state_store.h"
#include "cluster/storage_user_projection_transition_store.h"
#include "users_registry.h"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace pqnas::cluster {
namespace {

bool sync_succeeded(
    StorageUserProjectionSyncClientCode code
) noexcept {
    return
        code ==
            StorageUserProjectionSyncClientCode::Applied ||
        code ==
            StorageUserProjectionSyncClientCode::Unchanged;
}

StorageUserProjection previous_node_tombstone(
    const StorageUserProjection& current,
    const std::string& previous_home_node
) {
    StorageUserProjection tombstone;

    tombstone.fingerprint =
        current.fingerprint;

    // Security: a Node that is no longer authoritative receives no usable
    // authorization or storage mapping.
    tombstone.enabled = false;
    tombstone.storage_allocated = false;
    tombstone.quota_bytes = 0;
    tombstone.root_rel.clear();
    tombstone.storage_pool_id.clear();

    tombstone.home_node =
        previous_home_node;

    // Security: use the new Director revision so older delayed writes become
    // stale at the previous Node.
    tombstone.revision =
        current.revision;

    return tombstone;
}

StorageUserProjection unplaced_user_tombstone(
    const pqnas::UserRec& user,
    const std::string& previous_home_node
) {
    StorageUserProjection revision_source;

    revision_source.fingerprint =
        user.fingerprint;

    revision_source.revision =
        user.storage_projection_revision;

    // Security: explicit unplacement uses the Director's newer durable
    // revision while targeting the last Node confirmed to hold authorization.
    // Reuse the exact A->B tombstone shape so no usable storage mapping leaks.
    return previous_node_tombstone(
        revision_source,
        previous_home_node
    );
}


StorageUserProjectionSyncState
projection_state_from_projection(
    const StorageUserProjection& projection
) {
    StorageUserProjectionSyncState state;

    state.fingerprint =
        projection.fingerprint;

    state.home_node =
        projection.home_node;

    state.revision =
        projection.revision;

    return state;
}


bool persist_pending_transition(
    StorageUserProjectionTransitionStore* store,
    const StorageUserProjection& projection,
    StorageUserProjectionReconcileResult* result
) {
    if (!store || !result) {
        if (result) {
            ++result->state_conflicts;
            ++result->failed;
        }

        return false;
    }

    const StorageUserProjectionTransitionRecord
        record =
            projection_state_from_projection(
                projection
            );

    std::string persistence_error;

    const auto persisted =
        store->mark_pending(
            record,
            &persistence_error
        );

    persistence_error.clear();

    switch (persisted) {
        case StorageUserProjectionSyncStateStoreResult::
                Applied:
        case StorageUserProjectionSyncStateStoreResult::
                Unchanged:
            return true;

        case StorageUserProjectionSyncStateStoreResult::
                PersistFailed:
            ++result->persist_failures;
            ++result->failed;
            return false;

        case StorageUserProjectionSyncStateStoreResult::
                Stale:
            ++result->stale_states;
            ++result->failed;
            return false;

        case StorageUserProjectionSyncStateStoreResult::
                Conflict:
        case StorageUserProjectionSyncStateStoreResult::
                InvalidInput:
            ++result->state_conflicts;
            ++result->failed;
            return false;
    }

    ++result->state_conflicts;
    ++result->failed;
    return false;
}


bool erase_pending_transition(
    StorageUserProjectionTransitionStore* store,
    const StorageUserProjectionTransitionRecord& record,
    StorageUserProjectionReconcileResult* result
) {
    if (!store || !result) {
        if (result) {
            ++result->state_conflicts;
            ++result->failed;
        }

        return false;
    }

    bool expectation_matched = false;
    std::string persistence_error;

    if (!store->erase_if_current(
            record.fingerprint,
            record.home_node,
            record.revision,
            &expectation_matched,
            &persistence_error)) {
        persistence_error.clear();

        ++result->persist_failures;
        ++result->failed;
        return false;
    }

    persistence_error.clear();

    if (!expectation_matched) {
        // Security: an unexpected journal replacement must not be erased or
        // treated as completion of this authorization transition.
        ++result->state_conflicts;
        ++result->failed;
        return false;
    }

    return true;
}


bool persist_confirmed_state(
    StorageUserProjectionSyncStateStore* store,
    const StorageUserProjection& projection,
    StorageUserProjectionReconcileResult* result
) {
    const StorageUserProjectionSyncState
        state =
            projection_state_from_projection(
                projection
            );

    std::string persistence_error;

    const auto persisted =
        store->mark_synced(
            state,
            &persistence_error
        );

    persistence_error.clear();

    switch (persisted) {
        case StorageUserProjectionSyncStateStoreResult::
                Applied:
        case StorageUserProjectionSyncStateStoreResult::
                Unchanged:
            return true;

        case StorageUserProjectionSyncStateStoreResult::
                PersistFailed:
            ++result->persist_failures;
            ++result->failed;
            return false;

        case StorageUserProjectionSyncStateStoreResult::
                Stale:
        case StorageUserProjectionSyncStateStoreResult::
                Conflict:
        case StorageUserProjectionSyncStateStoreResult::
                InvalidInput:
            ++result->state_conflicts;
            ++result->failed;
            return false;
    }

    ++result->state_conflicts;
    ++result->failed;
    return false;
}

void emit_reconcile_audit_best_effort(
    const StorageUserProjectionReconcilerConfig& config,
    const std::string& event,
    const std::string& fingerprint,
    std::uint64_t previous_revision,
    std::uint64_t projection_revision,
    const std::string& previous_home_node,
    const std::string& home_node
) noexcept {
    if (!config.audit_emit) {
        return;
    }

    try {
        config.audit_emit(
            event,
            "ok",
            {
                {
                    "fingerprint",
                    fingerprint
                },
                {
                    "previous_revision",
                    std::to_string(
                        previous_revision
                    )
                },
                {
                    "projection_revision",
                    std::to_string(
                        projection_revision
                    )
                },
                {
                    "previous_home_node",
                    previous_home_node
                },
                {
                    "home_node",
                    home_node
                }
            }
        );
    } catch (...) {
        // Security/availability: audit is best-effort after the durable
        // authorization transition. Never turn an already-completed
        // reconciliation into a retry or rollback because audit I/O failed.
    }
}

bool heal_confirmed_state_durability(
    StorageUserProjectionSyncStateStore* store,
    const StorageUserProjectionSyncState& state,
    StorageUserProjectionReconcileResult* result
) {
    std::string persistence_error;

    const auto persisted =
        store->mark_synced(
            state,
            &persistence_error
        );

    persistence_error.clear();

    switch (persisted) {
        case StorageUserProjectionSyncStateStoreResult::
                Applied:
        case StorageUserProjectionSyncStateStoreResult::
                Unchanged:
            return true;

        case StorageUserProjectionSyncStateStoreResult::
                PersistFailed:
            ++result->persist_failures;
            ++result->failed;
            return false;

        case StorageUserProjectionSyncStateStoreResult::
                Stale:
        case StorageUserProjectionSyncStateStoreResult::
                Conflict:
        case StorageUserProjectionSyncStateStoreResult::
                InvalidInput:
            ++result->state_conflicts;
            ++result->failed;
            return false;
    }

    ++result->state_conflicts;
    ++result->failed;
    return false;
}

} // namespace

StorageUserProjectionReconciler::
StorageUserProjectionReconciler(
    StorageUserProjectionReconcilerConfig config
)
    : config_(std::move(config)) {}

StorageUserProjectionReconcileResult
StorageUserProjectionReconciler::reconcile_once() const {
    StorageUserProjectionReconcileResult result;

    if (!config_.users ||
        !config_.state_store ||
        !config_.transition_store ||
        !config_.sync_projection) {
        result.failed = 1;
        return result;
    }

    std::string durability_error;

    // Security: users.json is authoritative Director state. Never derive or
    // send Storage authorization while its latest visible commit has uncertain
    // crash durability.
    if (!config_.users->ensure_durable(
            &durability_error)) {
        durability_error.clear();

        ++result.persist_failures;
        ++result.failed;

        return result;
    }

    durability_error.clear();

    // Security: confirmed Node synchronization state must also be durable
    // before another Storage authorization transition is attempted.
    if (!config_.state_store->ensure_durable(
            &durability_error)) {
        durability_error.clear();

        ++result.persist_failures;
        ++result.failed;

        return result;
    }

    durability_error.clear();

    // Security: pending transition knowledge is authorization state too.
    // Never perform another Storage write until its visible snapshot is
    // known durable.
    if (!config_.transition_store->ensure_durable(
            &durability_error)) {
        durability_error.clear();

        ++result.persist_failures;
        ++result.failed;

        return result;
    }

    const auto users =
        config_.users->snapshot();

    const auto states =
        config_.state_store->snapshot();

    const auto transitions =
        config_.transition_store->snapshot();

    std::unordered_set<std::string>
        current_fingerprints;

    current_fingerprints.reserve(
        users.size()
    );

    std::vector<std::string>
        fingerprints;

    fingerprints.reserve(
        users.size()
    );

    for (const auto& entry :
         users) {
        current_fingerprints.insert(
            entry.first
        );

        fingerprints.push_back(
            entry.first
        );
    }

    std::sort(
        fingerprints.begin(),
        fingerprints.end()
    );

    // Security: a state entry whose Director user no longer exists is never
    // silently discarded. Physical cluster deletion remains blocked until a
    // dedicated newer-revision revoke protocol exists.
    for (const auto& state :
         states) {
        if (current_fingerprints.find(
                state.fingerprint) ==
            current_fingerprints.end()) {
            ++result.orphaned_states;
            ++result.failed;
        }
    }

    // Security: an orphaned pending transition can identify a Storage
    // Node that may still hold authorization. Never silently forget it.
    for (const auto& transition :
         transitions) {
        if (current_fingerprints.find(
                transition.fingerprint) ==
            current_fingerprints.end()) {
            ++result.orphaned_states;
            ++result.failed;
        }
    }

    for (const std::string& fingerprint :
         fingerprints) {
        ++result.users_seen;

        const auto user_iterator =
            users.find(
                fingerprint
            );

        if (user_iterator ==
            users.end()) {
            ++result.failed;
            continue;
        }

        const pqnas::UserRec& user =
            user_iterator->second;

        const auto confirmed =
            config_.state_store->get(
                fingerprint
            );

        const auto pending =
            config_.transition_store->get(
                fingerprint
            );

        if (pending.has_value()) {
            // A confirmed state at least as new as this pending record proves
            // that the same Node is already represented durably. The journal
            // can be cleared without another authorization write.
            if (confirmed.has_value() &&
                confirmed->home_node ==
                    pending->home_node &&
                confirmed->revision >=
                    pending->revision) {
                if (!erase_pending_transition(
                        config_.transition_store,
                        *pending,
                        &result)) {
                    continue;
                }
            } else if (
                pending->revision >
                    user.storage_projection_revision
            ) {
                // Security: Director state is older than possibly-authorized
                // journal knowledge. Never roll the Node backwards.
                ++result.stale_states;
                ++result.failed;
                continue;
            } else if (
                !user.home_node.empty() &&
                user.home_node ==
                    pending->home_node
            ) {
                // The pending Node is still authoritative. Retry the live
                // projection idempotently, durably confirm it, then clear
                // the write-ahead journal.
                StorageUserProjection recovered;
                std::string build_error;

                if (!build_storage_user_projection(
                        user,
                        &recovered,
                        &build_error)) {
                    build_error.clear();

                    ++result.failed;
                    continue;
                }

                // Security: pending recovery must preserve the same rollback
                // and equal-revision conflict invariants as normal
                // reconciliation before any authorization reaches Storage.
                if (confirmed.has_value() &&
                    confirmed->revision >
                        recovered.revision) {
                    ++result.stale_states;
                    ++result.failed;
                    continue;
                }

                if (confirmed.has_value() &&
                    confirmed->revision ==
                        recovered.revision &&
                    confirmed->home_node !=
                        recovered.home_node) {
                    // Security: the Director has two different targets for
                    // one revision. Never authorize either interpretation
                    // further until the durable state is repaired.
                    ++result.state_conflicts;
                    ++result.failed;
                    continue;
                }

                StorageUserProjectionSyncResult
                    recovered_result;

                try {
                    recovered_result =
                        config_.sync_projection(
                            recovered
                        );
                } catch (...) {
                    ++result.failed;
                    continue;
                }

                if (!sync_succeeded(
                        recovered_result.code)) {
                    ++result.failed;
                    continue;
                }

                if (!persist_confirmed_state(
                        config_.state_store,
                        recovered,
                        &result)) {
                    continue;
                }

                if (!erase_pending_transition(
                        config_.transition_store,
                        *pending,
                        &result)) {
                    continue;
                }

                if (confirmed.has_value() &&
                    confirmed->home_node !=
                        recovered.home_node) {
                    emit_reconcile_audit_best_effort(
                        config_,
                        "cluster.storage_projection.moved",
                        fingerprint,
                        confirmed->revision,
                        recovered.revision,
                        confirmed->home_node,
                        recovered.home_node
                    );

                    ++result.moved;
                } else {
                    emit_reconcile_audit_best_effort(
                        config_,
                        "cluster.storage_projection.synced",
                        fingerprint,
                        confirmed.has_value()
                            ? confirmed->revision
                            : 0,
                        recovered.revision,
                        confirmed.has_value()
                            ? confirmed->home_node
                            : "",
                        recovered.home_node
                    );

                    ++result.synced_current;
                }

                continue;
            } else if (
                pending->revision ==
                    user.storage_projection_revision
            ) {
                // Same revision cannot safely mean two different targets or
                // both live authorization and a disabling tombstone.
                ++result.state_conflicts;
                ++result.failed;
                continue;
            } else {
                // Director state advanced beyond an intermediate pending Node.
                // Revoke that possibly-authorized Node first with the newer
                // authoritative revision, then forget the journal record.
                const StorageUserProjection
                    pending_tombstone =
                        unplaced_user_tombstone(
                            user,
                            pending->home_node
                        );

                StorageUserProjectionSyncResult
                    pending_revoke_result;

                try {
                    pending_revoke_result =
                        config_.sync_projection(
                            pending_tombstone
                        );
                } catch (...) {
                    ++result.failed;
                    continue;
                }

                if (!sync_succeeded(
                        pending_revoke_result.code)) {
                    ++result.failed;
                    continue;
                }

                if (!erase_pending_transition(
                        config_.transition_store,
                        *pending,
                        &result)) {
                    continue;
                }

                // Continue with ordinary confirmed-state reconciliation.
            }
        }

        if (user.home_node.empty()) {
            if (!confirmed.has_value()) {
                ++result.no_placement;
                continue;
            }

            if (confirmed->revision >
                user.storage_projection_revision) {
                // Security: persistent knowledge newer than Director state
                // cannot be revoked using an older revision.
                ++result.stale_states;
                ++result.failed;
                continue;
            }

            if (confirmed->revision ==
                user.storage_projection_revision) {
                // Security: equal revision cannot prove whether the Node holds
                // the old live projection or an already-applied tombstone.
                // Never send conflicting same-revision authorization content.
                ++result.state_conflicts;
                ++result.failed;
                continue;
            }

            const StorageUserProjection tombstone =
                unplaced_user_tombstone(
                    user,
                    confirmed->home_node
                );

            StorageUserProjectionSyncResult
                revoke_result;

            try {
                revoke_result =
                    config_.sync_projection(
                        tombstone
                    );
            } catch (...) {
                ++result.failed;
                continue;
            }

            if (!sync_succeeded(
                    revoke_result.code)) {
                // Security: retain confirmed state until the last known Node
                // has accepted the newer disabling tombstone.
                ++result.failed;
                continue;
            }

            bool expectation_matched = false;
            std::string persistence_error;

            if (!config_.state_store->
                    erase_if_current(
                        fingerprint,
                        confirmed->home_node,
                        confirmed->revision,
                        &expectation_matched,
                        &persistence_error)) {
                persistence_error.clear();

                // Security: a revoke is not considered reconciled until the
                // Director has durably removed the old confirmed placement.
                ++result.persist_failures;
                ++result.failed;
                continue;
            }

            persistence_error.clear();

            if (!expectation_matched) {
                // Another state transition won the CAS. Never erase or claim
                // success against newer confirmed synchronization knowledge.
                ++result.state_conflicts;
                ++result.failed;
                continue;
            }

            emit_reconcile_audit_best_effort(
                config_,
                "cluster.storage_projection.revoked",
                fingerprint,
                confirmed->revision,
                user.storage_projection_revision,
                confirmed->home_node,
                ""
            );

            ++result.revoked_unplaced;
            continue;
        }

        StorageUserProjection current;
        std::string build_error;

        if (!build_storage_user_projection(
                user,
                &current,
                &build_error)) {
            build_error.clear();

            ++result.failed;
            continue;
        }

        if (!confirmed.has_value()) {
            // RT009: establish durable knowledge of the new target before the
            // Node can receive usable authorization.
            if (!persist_pending_transition(
                    config_.transition_store,
                    current,
                    &result)) {
                continue;
            }

            StorageUserProjectionSyncResult
                sync_result;

            try {
                sync_result =
                    config_.sync_projection(
                        current
                    );
            } catch (...) {
                ++result.failed;
                continue;
            }

            if (!sync_succeeded(
                    sync_result.code)) {
                // Keep pending state. The request may have reached Storage
                // even when the response was lost.
                ++result.failed;
                continue;
            }

            if (!persist_confirmed_state(
                    config_.state_store,
                    current,
                    &result)) {
                // Keep pending state so restart recovery remembers this Node.
                continue;
            }

            const auto pending_current =
                projection_state_from_projection(
                    current
                );

            if (!erase_pending_transition(
                    config_.transition_store,
                    pending_current,
                    &result)) {
                continue;
            }

            emit_reconcile_audit_best_effort(
                config_,
                "cluster.storage_projection.synced",
                fingerprint,
                0,
                current.revision,
                "",
                current.home_node
            );

            ++result.synced_current;
            continue;
        }

        if (confirmed->revision >
            current.revision) {
            // Security: never roll Storage authorization backwards if the
            // persisted sync state proves a newer revision was already sent.
            ++result.stale_states;
            ++result.failed;
            continue;
        }

        if (confirmed->revision ==
            current.revision) {
            if (confirmed->home_node !=
                current.home_node) {
                // Same Director revision cannot legitimately represent two
                // different authoritative Nodes.
                ++result.state_conflicts;
                ++result.failed;
                continue;
            }

            // Even a no-op is passed through mark_synced(): if an earlier
            // rename succeeded but parent fsync failed, this re-attempts
            // durable persistence without performing network I/O.
            if (heal_confirmed_state_durability(
                    config_.state_store,
                    *confirmed,
                    &result)) {
                ++result.already_synced;
            }

            continue;
        }

        // confirmed revision is older than current Director revision.
        if (confirmed->home_node !=
            current.home_node) {
            const StorageUserProjection tombstone =
                previous_node_tombstone(
                    current,
                    confirmed->home_node
                );

            StorageUserProjectionSyncResult
                revoke_result;

            try {
                revoke_result =
                    config_.sync_projection(
                        tombstone
                    );
            } catch (...) {
                ++result.failed;
                continue;
            }

            if (!sync_succeeded(
                    revoke_result.code)) {
                // Security: do not establish the new Node projection while the
                // last confirmed Node remains unreconciled.
                ++result.failed;
                continue;
            }

            // RT009: the previous Node has accepted its tombstone. Persist B
            // before B can receive authorization, so B survives a Director
            // crash or confirmed-state persistence failure.
            if (!persist_pending_transition(
                    config_.transition_store,
                    current,
                    &result)) {
                continue;
            }

            StorageUserProjectionSyncResult
                current_result;

            try {
                current_result =
                    config_.sync_projection(
                        current
                    );
            } catch (...) {
                ++result.failed;
                continue;
            }

            if (!sync_succeeded(
                    current_result.code)) {
                // Keep pending B: transport failure cannot prove that B did
                // not accept the authorization.
                ++result.failed;
                continue;
            }

            if (!persist_confirmed_state(
                    config_.state_store,
                    current,
                    &result)) {
                // Keep pending B for restart recovery.
                continue;
            }

            const auto pending_current =
                projection_state_from_projection(
                    current
                );

            if (!erase_pending_transition(
                    config_.transition_store,
                    pending_current,
                    &result)) {
                continue;
            }

            emit_reconcile_audit_best_effort(
                config_,
                "cluster.storage_projection.moved",
                fingerprint,
                confirmed->revision,
                current.revision,
                confirmed->home_node,
                current.home_node
            );

            ++result.moved;
            continue;
        }

        StorageUserProjectionSyncResult
            sync_result;

        try {
            sync_result =
                config_.sync_projection(
                    current
                );
        } catch (...) {
            ++result.failed;
            continue;
        }

        if (!sync_succeeded(
                sync_result.code)) {
            ++result.failed;
            continue;
        }

        if (persist_confirmed_state(
                config_.state_store,
                current,
                &result)) {
            emit_reconcile_audit_best_effort(
                config_,
                "cluster.storage_projection.synced",
                fingerprint,
                confirmed->revision,
                current.revision,
                confirmed->home_node,
                current.home_node
            );

            ++result.synced_current;
        }
    }

    result.ok =
        result.failed == 0;

    return result;
}

} // namespace pqnas::cluster
