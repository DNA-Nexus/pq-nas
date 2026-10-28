#pragma once

#include "cluster/storage_user_projection_store.h"

#include <cstdint>
#include <functional>
#include <string>

namespace pqnas::cluster {

class NodeRegistry;
class NodeCredentialRegistry;
class NodeTlsTrustStore;

enum class StorageUserProjectionSyncTransportCode {
    Applied,
    Unchanged,
    Rejected,
    AuthenticationFailed,
    WrongNode,
    Stale,
    Conflict,
    RateLimited,
    ServiceUnavailable,
    InvalidResponse,
    TransportFailed
};

struct StorageUserProjectionSyncTransportRequest {
    std::string api_base_url;

    // Public, canonical TLS identity of the exact Storage node.
    // Security: authenticated transport has no blank/unpinned path.
    std::string tls_spki_sha256_pin;

    // Exact short-lived "Bearer <token>" value obtained from the
    // Director's per-node credential registry.
    std::string authorization;

    StorageUserProjection projection;
};

struct StorageUserProjectionSyncTransportResponse {
    StorageUserProjectionSyncTransportCode code =
        StorageUserProjectionSyncTransportCode::TransportFailed;

    int http_status = 0;
    std::uint64_t revision = 0;
};

using StorageUserProjectionSyncTransport =
    std::function<
        StorageUserProjectionSyncTransportResponse(
            const StorageUserProjectionSyncTransportRequest&
        )
    >;

enum class StorageUserProjectionSyncClientCode {
    Applied,
    Unchanged,
    InvalidInput,
    ClientUnavailable,
    NodeNotFound,
    NodeDisabled,
    TlsTrustUnavailable,
    CredentialUnavailable,
    Rejected,
    AuthenticationFailed,
    WrongNode,
    Stale,
    Conflict,
    RateLimited,
    ServiceUnavailable,
    InvalidResponse,
    TransportFailed
};

const char* storage_user_projection_sync_client_code(
    StorageUserProjectionSyncClientCode code
) noexcept;

struct StorageUserProjectionSyncResult {
    StorageUserProjectionSyncClientCode code =
        StorageUserProjectionSyncClientCode::ClientUnavailable;

    int http_status = 0;
    std::uint64_t revision = 0;
};

struct StorageUserProjectionSyncClientConfig {
    const NodeRegistry* node_registry = nullptr;
    const NodeCredentialRegistry* credential_registry = nullptr;

    // Security: trust is resolved before any reusable bearer credential is
    // materialized for authenticated Director -> Storage traffic.
    const NodeTlsTrustStore* tls_trust_store = nullptr;

    StorageUserProjectionSyncTransport transport;
};

class StorageUserProjectionSyncClient {
public:
    explicit StorageUserProjectionSyncClient(
        StorageUserProjectionSyncClientConfig config
    );

    StorageUserProjectionSyncResult sync(
        const StorageUserProjection& projection
    ) const;

private:
    StorageUserProjectionSyncClientConfig config_;
};

} // namespace pqnas::cluster
