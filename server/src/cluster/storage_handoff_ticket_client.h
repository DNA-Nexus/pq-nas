#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace pqnas::cluster {

class NodeRegistry;
class NodeCredentialRegistry;
class NodeTlsTrustStore;

enum class StorageHandoffTicketTransportCode {
    Ok,
    Rejected,
    AuthenticationFailed,
    UserNotAuthorized,
    RestartRequired,
    RateLimited,
    ServiceUnavailable,
    InvalidResponse,
    TransportFailed
};

struct StorageHandoffTicketTransportRequest {
    std::string api_base_url;

    // Security: authenticated Director -> Storage traffic has no unpinned path.
    std::string tls_spki_sha256_pin;

    // Exact reusable "Bearer <token>" value from the Director's per-node
    // credential registry. Keep application-owned copies as short-lived as
    // practical and wipe them after transport.
    std::string authorization;

    std::string fingerprint;
    std::string browser_binding_hash;
};

struct StorageHandoffTicketTransportResponse {
    StorageHandoffTicketTransportCode code =
        StorageHandoffTicketTransportCode::TransportFailed;

    int http_status = 0;

    // Populated only for Ok.
    std::string ticket;
    std::int64_t expires_at = 0;
};

using StorageHandoffTicketTransport =
    std::function<
        StorageHandoffTicketTransportResponse(
            const StorageHandoffTicketTransportRequest&
        )
    >;

enum class StorageHandoffTicketClientCode {
    Ok,
    InvalidInput,
    ClientUnavailable,
    NodeNotFound,
    NodeDisabled,
    TargetChanged,
    TlsTrustUnavailable,
    CredentialUnavailable,
    Rejected,
    AuthenticationFailed,
    UserNotAuthorized,
    RestartRequired,
    RateLimited,
    ServiceUnavailable,
    InvalidResponse,
    TransportFailed
};

const char* storage_handoff_ticket_client_code(
    StorageHandoffTicketClientCode code
) noexcept;

struct StorageHandoffTicketClientResult {
    StorageHandoffTicketClientCode code =
        StorageHandoffTicketClientCode::ClientUnavailable;

    int http_status = 0;

    // Security: plaintext ticket is intentionally returned only on success.
    std::string ticket;
    std::int64_t expires_at = 0;
};

struct StorageHandoffTicketClientConfig {
    const NodeRegistry* node_registry = nullptr;
    const NodeCredentialRegistry* credential_registry = nullptr;

    // Security: TLS identity must be resolved before reusable bearer material
    // is obtained.
    const NodeTlsTrustStore* tls_trust_store = nullptr;

    StorageHandoffTicketTransport transport;
};

class StorageHandoffTicketClient {
public:
    explicit StorageHandoffTicketClient(
        StorageHandoffTicketClientConfig config
    );

    StorageHandoffTicketClientResult issue(
        const std::string& target_node_id,
        const std::string& fingerprint,
        const std::string& browser_binding_hash
    ) const;

private:
    StorageHandoffTicketClientConfig config_;
};

} // namespace pqnas::cluster
