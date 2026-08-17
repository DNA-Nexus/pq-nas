#pragma once

#include "cluster/storage_user_projection_sync_client.h"

#include <functional>
#include <string>

namespace pqnas {

class UsersRegistry;

} // namespace pqnas

namespace pqnas::cluster {

enum class StorageUserProjectionSyncServiceCode {
    Synced,
    NoPlacement,
    UserNotFound,
    InvalidPreviousNode,
    ProjectionBuildFailed,
    PreviousNodeRevokeFailed,
    CurrentNodeSyncFailed,
    ServiceUnavailable
};

const char* storage_user_projection_sync_service_code(
    StorageUserProjectionSyncServiceCode code
) noexcept;

struct StorageUserProjectionSyncServiceResult {
    StorageUserProjectionSyncServiceCode code =
        StorageUserProjectionSyncServiceCode::
            ServiceUnavailable;

    bool ok = false;
    bool previous_node_revoked = false;
    bool current_node_synced = false;

    StorageUserProjectionSyncClientCode client_code =
        StorageUserProjectionSyncClientCode::
            ClientUnavailable;
};

using StorageUserProjectionSyncInvoke =
    std::function<
        StorageUserProjectionSyncResult(
            const StorageUserProjection&
        )
    >;

struct StorageUserProjectionSyncServiceConfig {
    pqnas::UsersRegistry* users = nullptr;
    StorageUserProjectionSyncInvoke sync_projection;
};

class StorageUserProjectionSyncService {
public:
    explicit StorageUserProjectionSyncService(
        StorageUserProjectionSyncServiceConfig config
    );

    // Synchronize the user's current Director projection to home_node.
    //
    // An unplaced user is a successful no-op.
    StorageUserProjectionSyncServiceResult
    sync_current(
        const std::string& fingerprint
    ) const;

    // Synchronize a persisted placement transition.
    //
    // If previous_home_node differs from the current home_node, revoke the
    // previous Node first with a same-revision disabled/unallocated tombstone.
    // The new Node is synchronized only after that revoke succeeds.
    StorageUserProjectionSyncServiceResult
    sync_after_placement(
        const std::string& fingerprint,
        const std::string& previous_home_node
    ) const;

private:
    StorageUserProjectionSyncServiceConfig config_;
};

} // namespace pqnas::cluster
