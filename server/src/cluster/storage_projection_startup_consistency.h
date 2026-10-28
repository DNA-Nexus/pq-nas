#pragma once

#include <cstddef>


namespace pqnas {

class UsersRegistry;

} // namespace pqnas


namespace pqnas::cluster {

class StorageUserProjectionSyncStateStore;
class StorageUserProjectionTransitionStore;


struct StorageProjectionStartupConsistencyResult {
    bool ok = true;

    std::size_t confirmed_states_checked = 0;
    std::size_t pending_states_checked = 0;

    // Number of durable authorization records whose revision is newer than
    // the corresponding authoritative users.json projection revision.
    std::size_t confirmed_revision_ahead = 0;
    std::size_t pending_revision_ahead = 0;
};


// Security: durable confirmed or pending authorization knowledge must never
// be newer than the authoritative Director user registry after restart.
//
// Orphaned durable records remain separate reconciliation invariants; this
// check protects the monotonic rollback boundary before reconciliation starts.
StorageProjectionStartupConsistencyResult
check_storage_projection_startup_consistency(
    const pqnas::UsersRegistry& users,
    const StorageUserProjectionSyncStateStore& state_store,
    const StorageUserProjectionTransitionStore& transition_store
);

} // namespace pqnas::cluster
