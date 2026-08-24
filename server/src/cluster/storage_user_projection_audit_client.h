#pragma once

#include "cluster/storage_user_projection_audit.h"

#include <functional>
#include <string>
#include <vector>

namespace pqnas::cluster {

class NodeRegistry;
class NodeCredentialRegistry;
class NodeTlsTrustStore;

struct StorageUserProjectionAuditRequest {
    std::string node_id;
    std::string range_start;
    std::string range_end;

    std::vector<StorageUserProjectionAuditExpected>
        expected;
};

// Shared strict manifest validation used by both the high-level Director client
// and the HTTPS transport. This validates protocol shape only; it performs no
// network or Storage state access.
bool valid_storage_user_projection_audit_request(
    const StorageUserProjectionAuditRequest& request
);

enum class StorageUserProjectionAuditTransportCode {
    Ok,
    Rejected,
    AuthenticationFailed,
    WrongNode,
    RateLimited,
    ServiceUnavailable,
    InvalidResponse,
    TransportFailed
};

struct StorageUserProjectionAuditTransportRequest {
    std::string api_base_url;

    // Public canonical TLS identity of the exact Storage node.
    std::string tls_spki_sha256_pin;

    // Short-lived "Bearer <token>" text obtained only after TLS trust exists.
    std::string authorization;

    StorageUserProjectionAuditRequest audit;
};

struct StorageUserProjectionAuditTransportResponse {
    StorageUserProjectionAuditTransportCode code =
        StorageUserProjectionAuditTransportCode::TransportFailed;

    int http_status = 0;

    // Fail closed by default: only an explicit successful audit response may
    // ever carry complete=true into a caller.
    StorageUserProjectionAuditResult result{
        false,
        0,
        0,
        {}
    };
};

using StorageUserProjectionAuditTransport =
    std::function<
        StorageUserProjectionAuditTransportResponse(
            const StorageUserProjectionAuditTransportRequest&
        )
    >;

enum class StorageUserProjectionAuditClientCode {
    Ok,
    InvalidInput,
    ClientUnavailable,
    NodeNotFound,
    NodeDisabled,
    TlsTrustUnavailable,
    CredentialUnavailable,
    Rejected,
    AuthenticationFailed,
    WrongNode,
    RateLimited,
    ServiceUnavailable,
    InvalidResponse,
    TransportFailed
};

const char* storage_user_projection_audit_client_code(
    StorageUserProjectionAuditClientCode code
) noexcept;

struct StorageUserProjectionAuditClientResult {
    StorageUserProjectionAuditClientCode code =
        StorageUserProjectionAuditClientCode::ClientUnavailable;

    int http_status = 0;

    // Fail closed unless code==Ok explicitly installs a verified audit result.
    StorageUserProjectionAuditResult result{
        false,
        0,
        0,
        {}
    };
};

struct StorageUserProjectionAuditClientConfig {
    const NodeRegistry* node_registry = nullptr;
    const NodeCredentialRegistry* credential_registry = nullptr;

    // Security: trust is resolved before reusable bearer material exists.
    const NodeTlsTrustStore* tls_trust_store = nullptr;

    StorageUserProjectionAuditTransport transport;
};

class StorageUserProjectionAuditClient {
public:
    explicit StorageUserProjectionAuditClient(
        StorageUserProjectionAuditClientConfig config
    );

    StorageUserProjectionAuditClientResult audit(
        const StorageUserProjectionAuditRequest& request
    ) const;

private:
    StorageUserProjectionAuditClientConfig config_;
};

} // namespace pqnas::cluster
