#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>

namespace pqnas::cluster {

class NodeCredentialRegistry;
class NodeRegistry;

enum class NodeCredentialRotationTransportCode {
    Ok,
    AuthenticationFailed,
    RateLimited,
    ServiceUnavailable,
    InvalidResponse,
    TransportFailed
};

struct NodeCredentialRotationPrepareRequest {
    std::string api_base_url;

    // Security: exact short-lived "Bearer <token>" value. The transport must
    // never log or retain this field after the request completes.
    std::string authorization;
};

struct NodeCredentialRotationPrepareResponse {
    NodeCredentialRotationTransportCode code =
        NodeCredentialRotationTransportCode::
            TransportFailed;

    int http_status = 0;

    // Present only when code == Ok. This is plaintext credential material and
    // must be wiped by every layer that temporarily owns a copy.
    std::string pending_credential;
};

struct NodeCredentialRotationFinalizeRequest {
    std::string api_base_url;

    // Security: finalization must be authenticated specifically by NEW while
    // the Storage node has a pending rotation.
    std::string authorization;
};

struct NodeCredentialRotationFinalizeResponse {
    NodeCredentialRotationTransportCode code =
        NodeCredentialRotationTransportCode::
            TransportFailed;

    int http_status = 0;
};

using NodeCredentialRotationPrepareTransport =
    std::function<
        NodeCredentialRotationPrepareResponse(
            const NodeCredentialRotationPrepareRequest&
        )
    >;

using NodeCredentialRotationFinalizeTransport =
    std::function<
        NodeCredentialRotationFinalizeResponse(
            const NodeCredentialRotationFinalizeRequest&
        )
    >;

enum class NodeCredentialRotationServiceCode {
    Ok,
    InvalidInput,
    ServiceUnavailable,
    NodeNotFound,
    NodeDisabled,
    RotationInProgress,
    NodeChanged,
    CredentialUnavailable,
    PrepareFailed,
    CredentialStateConflict,
    CredentialCommitFailed,

    // Director has already durably committed NEW. Storage finalization was not
    // confirmed, so OLD must never be restored. A later retry can continue
    // using NEW.
    FinalizePending
};

const char* node_credential_rotation_service_code(
    NodeCredentialRotationServiceCode code
) noexcept;

struct NodeCredentialRotationResult {
    NodeCredentialRotationServiceCode code =
        NodeCredentialRotationServiceCode::
            ServiceUnavailable;

    int http_status = 0;

    // These are state flags only. No credential material is returned.
    bool director_credential_committed = false;
    bool storage_finalized = false;

    bool ok() const noexcept {
        return
            code ==
            NodeCredentialRotationServiceCode::Ok;
    }
};

struct NodeCredentialRotationServiceConfig {
    NodeRegistry* node_registry = nullptr;
    NodeCredentialRegistry* credential_registry =
        nullptr;

    // Shared with node lifecycle/placement administration.
    //
    // Security: this mutex is held only while checking/committing Director
    // state. prepare/finalize transports are always called without it held.
    std::shared_ptr<std::mutex>
        coordination_mutex;

    NodeCredentialRotationPrepareTransport
        prepare_transport;

    NodeCredentialRotationFinalizeTransport
        finalize_transport;
};

class NodeCredentialRotationService {
public:
    explicit NodeCredentialRotationService(
        NodeCredentialRotationServiceConfig config
    );

    NodeCredentialRotationResult rotate(
        const std::string& node_id
    );

    // Security: lifecycle callers that use this to gate deletion/update must
    // perform their check while holding the same coordination_mutex. rotate()
    // establishes the busy marker while holding that mutex, closing the
    // check-before-start race without holding a mutex during network I/O.
    bool rotation_in_progress(
        const std::string& node_id
    ) const;

private:
    bool config_valid() const noexcept;

    void clear_busy(
        const std::string& node_id
    ) noexcept;

    NodeCredentialRotationServiceConfig config_;

    mutable std::mutex busy_mutex_;

    std::unordered_set<std::string>
        busy_nodes_;
};

} // namespace pqnas::cluster
