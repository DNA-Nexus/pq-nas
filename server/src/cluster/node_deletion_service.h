#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <string>

namespace pqnas::cluster {

class NodeCredentialRegistry;
class NodeEnrollmentRegistry;
class NodeRegistry;
class NodeTlsTrustStore;
class StorageUserProjectionSyncStateStore;

enum class NodeDeletionServiceResult {
    Ok,
    ServiceUnavailable,
    InvalidInput,
    NotFound,
    ProvisioningActive,
    ConfirmedProjectionActive,
    ProjectionStateCheckFailed,
    EnrollmentCleanupFailed,
    CredentialCleanupFailed,
    TlsTrustCleanupFailed,
    RegistryCleanupFailed
};

const char* node_deletion_service_result_code(
    NodeDeletionServiceResult result
) noexcept;

struct NodeDeletionResult {
    NodeDeletionServiceResult result =
        NodeDeletionServiceResult::ServiceUnavailable;

    std::size_t revoked_pending_enrollments = 0;
    bool credential_removed = false;
    bool tls_trust_removed = false;
    bool node_removed = false;

    bool ok() const noexcept {
        return result ==
            NodeDeletionServiceResult::Ok;
    }

    const char* error_code() const noexcept {
        return
            node_deletion_service_result_code(
                result
            );
    }
};

struct NodeDeletionServiceConfig {
    NodeRegistry* node_registry = nullptr;

    NodeEnrollmentRegistry* enrollment_registry =
        nullptr;

    NodeCredentialRegistry* credential_registry =
        nullptr;

    NodeTlsTrustStore* tls_trust_store =
        nullptr;

    StorageUserProjectionSyncStateStore*
        projection_sync_state_store =
            nullptr;

    // Security: this is the same transition boundary used by projection
    // reconciliation and cluster-safe user deletion.
    std::shared_ptr<std::mutex>
        projection_transition_mutex;
};

class NodeDeletionService {
public:
    explicit NodeDeletionService(
        NodeDeletionServiceConfig config
    );

    // Security: the caller must hold the shared Director lifecycle/
    // placement coordination mutex for the complete operation. This service
    // then acquires projection_transition_mutex before confirmed-state
    // preflight and keeps it through the final NodeRegistry commit.
    //
    // Lock order is therefore lifecycle/placement -> projection transition.
    //
    // The node registry is removed last. A crash or persistence failure
    // before that point therefore leaves either the old node intact or a
    // registered node whose reusable bootstrap, bearer and TLS identity
    // state has already been revoked.
    NodeDeletionResult erase_unassigned_node_locked(
        const std::string& node_id,
        const std::string& revoked_at
    );

private:
    bool config_valid() const noexcept;

    NodeDeletionServiceConfig config_;
};

} // namespace pqnas::cluster
