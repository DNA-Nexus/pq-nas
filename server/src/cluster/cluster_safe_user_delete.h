#pragma once

#include <memory>
#include <mutex>
#include <string>


namespace pqnas {

class UsersRegistry;

} // namespace pqnas


namespace pqnas::cluster {

class StorageUserProjectionSyncStateStore;
class StorageUserProjectionTransitionStore;


// Security: physical Director user deletion is an authorization-state
// transition. It must serialize with projection reconciliation and verify
// that no durable confirmed or pending Storage authorization remains.
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
);

} // namespace pqnas::cluster
