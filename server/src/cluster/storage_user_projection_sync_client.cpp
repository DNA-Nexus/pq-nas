#include "cluster/storage_user_projection_sync_client.h"

#include "cluster/cluster_identity_validation.h"
#include "cluster/internal_auth.h"
#include "cluster/node_credential_registry.h"
#include "cluster/node_registry.h"

#include "storage_metadata_validation.h"
#include "storage_path_validation.h"

#include <string>
#include <utility>

namespace pqnas::cluster {
namespace {

class SensitiveStringWiper {
public:
    explicit SensitiveStringWiper(
        std::string& value
    ) noexcept
        : value_(value) {}

    ~SensitiveStringWiper() {
        clear_internal_auth_token_text(
            &value_
        );
    }

    SensitiveStringWiper(
        const SensitiveStringWiper&
    ) = delete;

    SensitiveStringWiper& operator=(
        const SensitiveStringWiper&
    ) = delete;

private:
    std::string& value_;
};

bool valid_projection(
    const StorageUserProjection& projection
) {
    if (!pqnas::valid_storage_user_fingerprint_hex(
            projection.fingerprint) ||
        !valid_cluster_node_id(
            projection.home_node) ||
        projection.revision == 0) {
        return false;
    }

    if (!projection.storage_allocated) {
        return
            projection.quota_bytes == 0 &&
            projection.root_rel.empty() &&
            projection.storage_pool_id.empty();
    }

    if (projection.root_rel.empty() ||
        projection.root_rel.size() >
            kStorageUserProjectionRootRelMaxBytes ||
        !pqnas::valid_storage_pool_reference(
            projection.storage_pool_id)) {
        return false;
    }

    std::string normalized;
    std::string error;

    return
        pqnas::normalize_storage_relative_path_strict(
            projection.root_rel,
            &normalized,
            &error
        ) &&
        normalized == projection.root_rel;
}

StorageUserProjectionSyncClientCode
map_transport_code(
    StorageUserProjectionSyncTransportCode code
) noexcept {
    switch (code) {
        case StorageUserProjectionSyncTransportCode::Applied:
            return
                StorageUserProjectionSyncClientCode::Applied;

        case StorageUserProjectionSyncTransportCode::Unchanged:
            return
                StorageUserProjectionSyncClientCode::Unchanged;

        case StorageUserProjectionSyncTransportCode::Rejected:
            return
                StorageUserProjectionSyncClientCode::Rejected;

        case StorageUserProjectionSyncTransportCode::
                AuthenticationFailed:
            return
                StorageUserProjectionSyncClientCode::
                    AuthenticationFailed;

        case StorageUserProjectionSyncTransportCode::WrongNode:
            return
                StorageUserProjectionSyncClientCode::WrongNode;

        case StorageUserProjectionSyncTransportCode::Stale:
            return
                StorageUserProjectionSyncClientCode::Stale;

        case StorageUserProjectionSyncTransportCode::Conflict:
            return
                StorageUserProjectionSyncClientCode::Conflict;

        case StorageUserProjectionSyncTransportCode::RateLimited:
            return
                StorageUserProjectionSyncClientCode::RateLimited;

        case StorageUserProjectionSyncTransportCode::
                ServiceUnavailable:
            return
                StorageUserProjectionSyncClientCode::
                    ServiceUnavailable;

        case StorageUserProjectionSyncTransportCode::
                InvalidResponse:
            return
                StorageUserProjectionSyncClientCode::
                    InvalidResponse;

        case StorageUserProjectionSyncTransportCode::
                TransportFailed:
            return
                StorageUserProjectionSyncClientCode::
                    TransportFailed;
    }

    return
        StorageUserProjectionSyncClientCode::InvalidResponse;
}

} // namespace

const char* storage_user_projection_sync_client_code(
    StorageUserProjectionSyncClientCode code
) noexcept {
    switch (code) {
        case StorageUserProjectionSyncClientCode::Applied:
            return "applied";
        case StorageUserProjectionSyncClientCode::Unchanged:
            return "unchanged";
        case StorageUserProjectionSyncClientCode::InvalidInput:
            return "invalid_input";
        case StorageUserProjectionSyncClientCode::ClientUnavailable:
            return "client_unavailable";
        case StorageUserProjectionSyncClientCode::NodeNotFound:
            return "node_not_found";
        case StorageUserProjectionSyncClientCode::NodeDisabled:
            return "node_disabled";
        case StorageUserProjectionSyncClientCode::CredentialUnavailable:
            return "credential_unavailable";
        case StorageUserProjectionSyncClientCode::Rejected:
            return "rejected";
        case StorageUserProjectionSyncClientCode::AuthenticationFailed:
            return "authentication_failed";
        case StorageUserProjectionSyncClientCode::WrongNode:
            return "wrong_node";
        case StorageUserProjectionSyncClientCode::Stale:
            return "stale";
        case StorageUserProjectionSyncClientCode::Conflict:
            return "conflict";
        case StorageUserProjectionSyncClientCode::RateLimited:
            return "rate_limited";
        case StorageUserProjectionSyncClientCode::ServiceUnavailable:
            return "service_unavailable";
        case StorageUserProjectionSyncClientCode::InvalidResponse:
            return "invalid_response";
        case StorageUserProjectionSyncClientCode::TransportFailed:
            return "transport_failed";
    }

    return "unknown";
}

StorageUserProjectionSyncClient::
StorageUserProjectionSyncClient(
    StorageUserProjectionSyncClientConfig config
)
    : config_(std::move(config)) {}

StorageUserProjectionSyncResult
StorageUserProjectionSyncClient::sync(
    const StorageUserProjection& projection
) const {
    const auto finish =
        [](
            StorageUserProjectionSyncClientCode code,
            int http_status = 0,
            std::uint64_t revision = 0
        ) {
            StorageUserProjectionSyncResult result;
            result.code = code;
            result.http_status = http_status;
            result.revision = revision;
            return result;
        };

    if (!config_.node_registry ||
        !config_.credential_registry ||
        !config_.transport) {
        return finish(
            StorageUserProjectionSyncClientCode::
                ClientUnavailable
        );
    }

    if (!valid_projection(projection)) {
        return finish(
            StorageUserProjectionSyncClientCode::
                InvalidInput
        );
    }

    const auto node =
        config_.node_registry->get(
            projection.home_node
        );

    if (!node.has_value()) {
        return finish(
            StorageUserProjectionSyncClientCode::
                NodeNotFound
        );
    }

    if (node->node_id !=
            projection.home_node ||
        !node->enabled) {
        return finish(
            node->enabled
                ? StorageUserProjectionSyncClientCode::
                      InvalidInput
                : StorageUserProjectionSyncClientCode::
                      NodeDisabled
        );
    }

    std::string normalized_url;

    if (!normalize_cluster_https_base_url(
            node->api_base_url,
            &normalized_url,
            nullptr) ||
        normalized_url != node->api_base_url) {
        return finish(
            StorageUserProjectionSyncClientCode::
                InvalidInput
        );
    }

    StorageUserProjectionSyncTransportRequest
        request;

    request.api_base_url =
        node->api_base_url;

    request.projection =
        projection;

    std::string credential_error;

    const NodeCredentialRegistryResult
        credential_result =
            config_.credential_registry
                ->make_bearer_authorization(
                    projection.home_node,
                    &request.authorization,
                    &credential_error
                );

    SensitiveStringWiper authorization_wiper(
        request.authorization
    );

    credential_error.clear();

    if (credential_result !=
        NodeCredentialRegistryResult::Ok) {
        return finish(
            StorageUserProjectionSyncClientCode::
                CredentialUnavailable
        );
    }

    StorageUserProjectionSyncTransportResponse
        response;

    try {
        response =
            config_.transport(request);
    } catch (...) {
        return finish(
            StorageUserProjectionSyncClientCode::
                TransportFailed
        );
    }

    return finish(
        map_transport_code(
            response.code
        ),
        response.http_status,
        response.revision
    );
}

} // namespace pqnas::cluster
