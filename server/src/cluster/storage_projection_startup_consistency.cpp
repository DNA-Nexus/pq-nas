#include "cluster/storage_projection_startup_consistency.h"

#include "cluster/storage_user_projection_sync_state_store.h"
#include "cluster/storage_user_projection_transition_store.h"
#include "users_registry.h"


namespace pqnas::cluster {

StorageProjectionStartupConsistencyResult
check_storage_projection_startup_consistency(
    const pqnas::UsersRegistry& users,
    const StorageUserProjectionSyncStateStore& state_store,
    const StorageUserProjectionTransitionStore& transition_store
) {
    StorageProjectionStartupConsistencyResult result;

    const auto user_snapshot =
        users.snapshot();

    const auto confirmed_states =
        state_store.snapshot();

    const auto pending_states =
        transition_store.snapshot();

    for (const auto& confirmed :
         confirmed_states) {
        const auto user_iterator =
            user_snapshot.find(
                confirmed.fingerprint
            );

        // Orphaned confirmed state is already handled fail-closed by the
        // reconciler. RT-003 specifically prevents a persisted derived
        // revision from outrunning its existing authoritative user record.
        if (user_iterator ==
            user_snapshot.end()) {
            continue;
        }

        ++result.confirmed_states_checked;

        if (confirmed.revision >
            user_iterator->second
                .storage_projection_revision) {
            // Security: the Director must not start with derived durable
            // authorization knowledge newer than users.json. Continuing would
            // permanently wedge reconciliation and may retain stale Node auth.
            ++result.confirmed_revision_ahead;
        }
    }

    for (const auto& pending :
         pending_states) {
        const auto user_iterator =
            user_snapshot.find(
                pending.fingerprint
            );

        // Orphaned pending state is handled fail-closed by reconciliation.
        // Here we reject users.json rollback below authorization knowledge
        // that may identify a Node which already accepted the projection.
        if (user_iterator ==
            user_snapshot.end()) {
            continue;
        }

        ++result.pending_states_checked;

        if (pending.revision >
            user_iterator->second
                .storage_projection_revision) {
            // Security: RT-009 journal knowledge may be authorization-bearing
            // even when confirmed state still points at an older Node.
            ++result.pending_revision_ahead;
        }
    }

    result.ok =
        result.confirmed_revision_ahead == 0 &&
        result.pending_revision_ahead == 0;

    return result;
}

} // namespace pqnas::cluster
