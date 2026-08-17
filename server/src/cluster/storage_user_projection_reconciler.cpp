#include "cluster/storage_user_projection_reconciler.h"

#include "cluster/storage_user_projection_builder.h"
#include "cluster/storage_user_projection_sync_state_store.h"
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

bool persist_confirmed_state(
    StorageUserProjectionSyncStateStore* store,
    const StorageUserProjection& projection,
    StorageUserProjectionReconcileResult* result
) {
    StorageUserProjectionSyncState state;

    state.fingerprint =
        projection.fingerprint;

    state.home_node =
        projection.home_node;

    state.revision =
        projection.revision;

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
        !config_.sync_projection) {
        result.failed = 1;
        return result;
    }

    const auto users =
        config_.users->snapshot();

    const auto states =
        config_.state_store->snapshot();

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

        if (user.home_node.empty()) {
            if (confirmed.has_value()) {
                // Security: an unplaced Director user must not silently leave a
                // previously confirmed Storage authorization behind.
                ++result.state_conflicts;
                ++result.failed;
            } else {
                ++result.no_placement;
            }

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
                ++result.synced_current;
            }

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
                ++result.failed;
                continue;
            }

            if (persist_confirmed_state(
                    config_.state_store,
                    current,
                    &result)) {
                ++result.moved;
            }

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
            ++result.synced_current;
        }
    }

    result.ok =
        result.failed == 0;

    return result;
}

} // namespace pqnas::cluster
