#include "cluster/cluster_safe_user_delete.h"

#include "cluster/storage_user_projection_sync_state_store.h"
#include "cluster/storage_user_projection_transition_store.h"
#include "users_registry.h"


namespace pqnas::cluster {

bool erase_user_persisted_cluster_safe(
    pqnas::UsersRegistry& users,
    StorageUserProjectionSyncStateStore& state_store,
    StorageUserProjectionTransitionStore& transition_store,
    const std::shared_ptr<std::mutex>& transition_mutex,
    const std::string& users_path,
    const std::string& fingerprint,
    bool* erased,
    bool* placement_blocked,
    bool* cluster_revoke_pending,
    std::string* error
) {
    if (erased) {
        *erased = false;
    }

    if (placement_blocked) {
        *placement_blocked = false;
    }

    if (cluster_revoke_pending) {
        *cluster_revoke_pending = false;
    }

    if (error) {
        error->clear();
    }

    if (!transition_mutex) {
        if (error) {
            *error =
                "projection transition mutex unavailable";
        }

        return false;
    }

    // Security: hold the same transition boundary as reconciliation from the
    // confirmed-state decision through the durable users.json deletion.
    std::lock_guard<std::mutex>
        transition_lock(
            *transition_mutex
        );

    const auto current =
        users.get(
            fingerprint
        );

    if (!current.has_value()) {
        if (error) {
            *error = "user not found";
        }

        return false;
    }

    if (!current->home_node.empty()) {
        if (placement_blocked) {
            *placement_blocked = true;
        }

        return true;
    }

    std::string durability_error;

    // Security: absence from confirmed and pending state is trustworthy
    // for physical deletion only when both authorization snapshots are durable.
    if (!state_store.ensure_durable(
            &durability_error) ||
        !transition_store.ensure_durable(
            &durability_error)) {
        durability_error.clear();

        if (error) {
            *error =
                "projection authorization-state persistence failed";
        }

        return false;
    }

    if (state_store
            .get(fingerprint)
            .has_value() ||
        transition_store
            .get(fingerprint)
            .has_value()) {
        if (cluster_revoke_pending) {
            *cluster_revoke_pending = true;
        }

        return true;
    }

    // Defense in depth: UsersRegistry rechecks home_node while holding its
    // own mutation lock immediately before committing the physical deletion.
    return users.erase_persisted_if_unplaced(
        fingerprint,
        users_path,
        erased,
        placement_blocked,
        error
    );
}

} // namespace pqnas::cluster
