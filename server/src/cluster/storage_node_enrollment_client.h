#pragma once

#include "cluster/storage_node_provisioning_store.h"

#include <functional>
#include <string>

namespace pqnas::cluster {

enum class StorageNodeEnrollmentTransportCode {
    Ok,
    AuthenticationFailed,
    Conflict,
    RateLimited,
    ServiceUnavailable,
    TransportFailed,
    InvalidResponse
};

const char* storage_node_enrollment_transport_code(
    StorageNodeEnrollmentTransportCode code
) noexcept;

struct StorageNodeEnrollmentTransportRequest {
    std::string director_url;
    std::string authorization_token;

    std::string enrollment_id;
    std::uint64_t expected_generation = 0;
    std::string node_id;
    std::string api_base_url;
    int protocol_version = 0;

    // Public TLS identity presented during authenticated enrollment.
    // Security: the transport requires a canonical SPKI SHA-256 pin.
    std::string tls_spki_sha256_pin;

    std::string node_credential;
};

struct StorageNodeEnrollmentTransportResponse {
    StorageNodeEnrollmentTransportCode code =
        StorageNodeEnrollmentTransportCode::
            TransportFailed;

    bool recovered = false;
    int http_status = 0;
};

using StorageNodeEnrollmentTransport =
    std::function<
        StorageNodeEnrollmentTransportResponse(
            const StorageNodeEnrollmentTransportRequest&
        )
    >;

enum class StorageNodeEnrollmentClientCode {
    Ok,
    InvalidInput,
    ClientUnavailable,
    LocalStateUnavailable,
    StateConflict,
    AuthenticationFailed,
    DirectorConflict,
    RateLimited,
    DirectorUnavailable,
    ProtocolError,
    PersistFailed
};

const char* storage_node_enrollment_client_code(
    StorageNodeEnrollmentClientCode code
) noexcept;

struct StorageNodeEnrollmentClientResult {
    StorageNodeEnrollmentClientCode code =
        StorageNodeEnrollmentClientCode::
            ClientUnavailable;

    bool ok = false;
    bool recovered = false;
    bool already_enrolled = false;

    const char* error_code() const noexcept;
};

struct StorageNodeEnrollmentClientConfig {
    StorageNodeProvisioningStore* store =
        nullptr;

    // Security: enrollment has no blank/default TLS identity path.
    // The client requires a canonical node SPKI SHA-256 pin before transport.
    std::string tls_spki_sha256_pin;

    StorageNodeEnrollmentTransport transport;

    std::function<std::string()>
        now_iso_utc;
};

class StorageNodeEnrollmentClient {
public:
    explicit StorageNodeEnrollmentClient(
        StorageNodeEnrollmentClientConfig config
    );

    // Runs one bounded enrollment attempt.
    //
    // Security and recovery:
    // - the bootstrap token is never persisted by this client
    // - the node credential is generated and persisted by the store first
    // - a 401 after bootstrap authentication retries exactly once with the
    //   persisted node credential for the post-Director-commit crash window
    // - no other failure triggers credential fallback
    // - local Enrolled state is committed only after an exact success response
    StorageNodeEnrollmentClientResult enroll(
        const StorageNodeProvisioningIntent& intent,
        const std::string& bootstrap_token
    );

private:
    StorageNodeEnrollmentClientConfig config_;
};

} // namespace pqnas::cluster
