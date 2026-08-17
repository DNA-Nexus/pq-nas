#include "cluster/storage_user_projection_sync_service.h"

#include "cluster/cluster_identity_validation.h"
#include "cluster/storage_user_projection_builder.h"
#include "users_registry.h"

#include <string>
#include <utility>

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

StorageUserProjectionSyncServiceResult
result(
    StorageUserProjectionSyncServiceCode code,
    bool ok,
    StorageUserProjectionSyncClientCode client_code =
        StorageUserProjectionSyncClientCode::
            ClientUnavailable,
    bool previous_node_revoked = false,
    bool current_node_synced = false
) {
    StorageUserProjectionSyncServiceResult value;
    value.code = code;
    value.ok = ok;
    value.client_code = client_code;
    value.previous_node_revoked =
        previous_node_revoked;
    value.current_node_synced =
        current_node_synced;

    return value;
}

StorageUserProjection previous_node_tombstone(
    const StorageUserProjection& current,
    const std::string& previous_home_node
) {
    StorageUserProjection tombstone;

    tombstone.fingerprint =
        current.fingerprint;

    // Security: the previous Node receives no usable authorization or storage
    // mapping after Director placement has moved the user elsewhere.
    tombstone.enabled = false;
    tombstone.storage_allocated = false;
    tombstone.quota_bytes = 0;
    tombstone.root_rel.clear();
    tombstone.storage_pool_id.clear();

    tombstone.home_node =
        previous_home_node;

    // Use the same new Director revision as the placement commit. This makes
    // delayed older writes stale on both the previous and current Nodes.
    tombstone.revision =
        current.revision;

    return tombstone;
}

} // namespace

const char*
storage_user_projection_sync_service_code(
    StorageUserProjectionSyncServiceCode code
) noexcept {
    switch (code) {
        case StorageUserProjectionSyncServiceCode::Synced:
            return "synced";

        case StorageUserProjectionSyncServiceCode::NoPlacement:
            return "no_placement";

        case StorageUserProjectionSyncServiceCode::UserNotFound:
            return "user_not_found";

        case StorageUserProjectionSyncServiceCode::
                InvalidPreviousNode:
            return "invalid_previous_node";

        case StorageUserProjectionSyncServiceCode::
                ProjectionBuildFailed:
            return "projection_build_failed";

        case StorageUserProjectionSyncServiceCode::
                PreviousNodeRevokeFailed:
            return "previous_node_revoke_failed";

        case StorageUserProjectionSyncServiceCode::
                CurrentNodeSyncFailed:
            return "current_node_sync_failed";

        case StorageUserProjectionSyncServiceCode::
                ServiceUnavailable:
            return "service_unavailable";
    }

    return "unknown";
}

StorageUserProjectionSyncService::
StorageUserProjectionSyncService(
    StorageUserProjectionSyncServiceConfig config
)
    : config_(std::move(config)) {}

StorageUserProjectionSyncServiceResult
StorageUserProjectionSyncService::sync_current(
    const std::string& fingerprint
) const {
    if (!config_.users ||
        !config_.sync_projection) {
        return result(
            StorageUserProjectionSyncServiceCode::
                ServiceUnavailable,
            false
        );
    }

    const auto user =
        config_.users->get(
            fingerprint
        );

    if (!user.has_value()) {
        return result(
            StorageUserProjectionSyncServiceCode::
                UserNotFound,
            false
        );
    }

    if (user->home_node.empty()) {
        return result(
            StorageUserProjectionSyncServiceCode::
                NoPlacement,
            true
        );
    }

    StorageUserProjection projection;
    std::string build_error;

    if (!build_storage_user_projection(
            *user,
            &projection,
            &build_error)) {
        build_error.clear();

        return result(
            StorageUserProjectionSyncServiceCode::
                ProjectionBuildFailed,
            false
        );
    }

    StorageUserProjectionSyncResult sync_result;

    try {
        sync_result =
            config_.sync_projection(
                projection
            );
    } catch (...) {
        return result(
            StorageUserProjectionSyncServiceCode::
                CurrentNodeSyncFailed,
            false,
            StorageUserProjectionSyncClientCode::
                TransportFailed
        );
    }

    if (!sync_succeeded(
            sync_result.code)) {
        return result(
            StorageUserProjectionSyncServiceCode::
                CurrentNodeSyncFailed,
            false,
            sync_result.code
        );
    }

    return result(
        StorageUserProjectionSyncServiceCode::
            Synced,
        true,
        sync_result.code,
        false,
        true
    );
}

StorageUserProjectionSyncServiceResult
StorageUserProjectionSyncService::
sync_after_placement(
    const std::string& fingerprint,
    const std::string& previous_home_node
) const {
    if (!config_.users ||
        !config_.sync_projection) {
        return result(
            StorageUserProjectionSyncServiceCode::
                ServiceUnavailable,
            false
        );
    }

    const auto user =
        config_.users->get(
            fingerprint
        );

    if (!user.has_value()) {
        return result(
            StorageUserProjectionSyncServiceCode::
                UserNotFound,
            false
        );
    }

    if (user->home_node.empty()) {
        // A successful placement mutation is expected to have a current Node.
        // Fail closed instead of silently treating malformed state as synced.
        return result(
            StorageUserProjectionSyncServiceCode::
                ProjectionBuildFailed,
            false
        );
    }

    if (!previous_home_node.empty() &&
        !valid_cluster_node_id(
            previous_home_node)) {
        return result(
            StorageUserProjectionSyncServiceCode::
                InvalidPreviousNode,
            false
        );
    }

    StorageUserProjection current;
    std::string build_error;

    if (!build_storage_user_projection(
            *user,
            &current,
            &build_error)) {
        build_error.clear();

        return result(
            StorageUserProjectionSyncServiceCode::
                ProjectionBuildFailed,
            false
        );
    }

    bool previous_revoked = false;

    if (!previous_home_node.empty() &&
        previous_home_node !=
            current.home_node) {
        const StorageUserProjection tombstone =
            previous_node_tombstone(
                current,
                previous_home_node
            );

        StorageUserProjectionSyncResult
            revoke_result;

        try {
            revoke_result =
                config_.sync_projection(
                    tombstone
                );
        } catch (...) {
            return result(
                StorageUserProjectionSyncServiceCode::
                    PreviousNodeRevokeFailed,
                false,
                StorageUserProjectionSyncClientCode::
                    TransportFailed
            );
        }

        if (!sync_succeeded(
                revoke_result.code)) {
            // Security: do not intentionally establish the new Node projection
            // while the previous Node still has an unreconciled authorization
            // state.
            return result(
                StorageUserProjectionSyncServiceCode::
                    PreviousNodeRevokeFailed,
                false,
                revoke_result.code
            );
        }

        previous_revoked = true;
    }

    StorageUserProjectionSyncResult current_result;

    try {
        current_result =
            config_.sync_projection(
                current
            );
    } catch (...) {
        return result(
            StorageUserProjectionSyncServiceCode::
                CurrentNodeSyncFailed,
            false,
            StorageUserProjectionSyncClientCode::
                TransportFailed,
            previous_revoked,
            false
        );
    }

    if (!sync_succeeded(
            current_result.code)) {
        return result(
            StorageUserProjectionSyncServiceCode::
                CurrentNodeSyncFailed,
            false,
            current_result.code,
            previous_revoked,
            false
        );
    }

    return result(
        StorageUserProjectionSyncServiceCode::
            Synced,
        true,
        current_result.code,
        previous_revoked,
        true
    );
}

} // namespace pqnas::cluster
