#pragma once

#include <cstddef>


namespace pqnas {

class UsersRegistry;

} // namespace pqnas


namespace pqnas::cluster {

class StorageUserProjectionSyncStateStore;


struct StorageProjectionStartupConsistencyResult {
    bool ok = true;

    std::size_t confirmed_states_checked = 0;

    // Number of persisted confirmed states whose revision is newer than the
    // corresponding authoritative users.json projection revision.
    std::size_t confirmed_revision_ahead = 0;
};


// Security: derived confirmed synchronization state must never be newer than
// the authoritative Director user registry after restart.
//
// Orphaned confirmed states remain a separate reconciliation invariant; this
// check is specifically the monotonic rollback condition protected by RT-003.
StorageProjectionStartupConsistencyResult
check_storage_projection_startup_consistency(
    const pqnas::UsersRegistry& users,
    const StorageUserProjectionSyncStateStore& state_store
);

} // namespace pqnas::cluster
