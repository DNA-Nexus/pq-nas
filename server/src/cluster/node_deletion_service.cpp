#include "cluster/node_deletion_service.h"

#include "cluster/cluster_identity_validation.h"
#include "cluster/node_credential_registry.h"
#include "cluster/node_enrollment_registry.h"
#include "cluster/node_registry.h"

#include <utility>
#include <vector>

namespace pqnas::cluster {

const char* node_deletion_service_result_code(
    NodeDeletionServiceResult result
) noexcept {
    switch (result) {
        case NodeDeletionServiceResult::Ok:
            return "ok";

        case NodeDeletionServiceResult::
                ServiceUnavailable:
            return "node_deletion_service_unavailable";

        case NodeDeletionServiceResult::
                InvalidInput:
            return "node_deletion_invalid";

        case NodeDeletionServiceResult::NotFound:
            return "node_deletion_not_found";

        case NodeDeletionServiceResult::
                ProvisioningActive:
            return "node_deletion_provisioning_active";

        case NodeDeletionServiceResult::
                EnrollmentCleanupFailed:
            return "node_deletion_enrollment_cleanup_failed";

        case NodeDeletionServiceResult::
                CredentialCleanupFailed:
            return "node_deletion_credential_cleanup_failed";

        case NodeDeletionServiceResult::
                RegistryCleanupFailed:
            return "node_deletion_registry_cleanup_failed";
    }

    return "node_deletion_service_unavailable";
}

NodeDeletionService::NodeDeletionService(
    NodeDeletionServiceConfig config
)
    : config_(std::move(config)) {}

bool NodeDeletionService::config_valid()
    const noexcept {
    return
        config_.node_registry &&
        config_.enrollment_registry &&
        config_.credential_registry;
}

NodeDeletionResult
NodeDeletionService::erase_unassigned_node_locked(
    const std::string& node_id,
    const std::string& revoked_at
) {
    NodeDeletionResult output;

    if (!config_valid()) {
        return output;
    }

    if (!valid_cluster_node_id(node_id) ||
        revoked_at.empty()) {
        output.result =
            NodeDeletionServiceResult::InvalidInput;

        return output;
    }

    if (!config_.node_registry
             ->get(node_id)
             .has_value()) {
        output.result =
            NodeDeletionServiceResult::NotFound;

        return output;
    }

    const std::vector<NodeEnrollmentRecord>
        enrollments =
            config_.enrollment_registry
                ->snapshot();

    // Security: preflight every matching enrollment before revoking any
    // verifier. An authenticated provisioning transaction may already have
    // persisted external credential or node state and must finish/recover
    // under the shared lifecycle lock instead of being torn down midway.
    for (const NodeEnrollmentRecord& enrollment :
         enrollments) {
        if (enrollment.node_id != node_id ||
            enrollment.state !=
                NodeEnrollmentState::Pending) {
            continue;
        }

        if (enrollment.provisioning_stage !=
            NodeEnrollmentProvisioningStage::None) {
            output.result =
                NodeDeletionServiceResult::
                    ProvisioningActive;

            return output;
        }
    }

    for (const NodeEnrollmentRecord& enrollment :
         enrollments) {
        if (enrollment.node_id != node_id ||
            enrollment.state !=
                NodeEnrollmentState::Pending) {
            continue;
        }

        NodeEnrollmentRecord revoked;
        std::string enrollment_error;

        const NodeEnrollmentRegistryResult
            revoke_result =
                config_.enrollment_registry
                    ->revoke_pending(
                        enrollment.enrollment_id,
                        enrollment.generation,
                        revoked_at,
                        &revoked,
                        &enrollment_error
                    );

        if (revoke_result !=
            NodeEnrollmentRegistryResult::Ok) {
            output.result =
                NodeDeletionServiceResult::
                    EnrollmentCleanupFailed;

            return output;
        }

        ++output.revoked_pending_enrollments;
    }

    std::string credential_error;

    const NodeCredentialRegistryResult
        credential_result =
            config_.credential_registry->erase(
                node_id,
                &credential_error
            );

    if (credential_result ==
        NodeCredentialRegistryResult::Ok) {
        output.credential_removed = true;
    } else if (
        credential_result !=
            NodeCredentialRegistryResult::NotFound) {
        // Security: never remove the trusted Node registry entry while a
        // long-lived Director bearer may still remain usable or its durable
        // deletion is uncertain.
        output.result =
            NodeDeletionServiceResult::
                CredentialCleanupFailed;

        return output;
    }

    std::string registry_error;

    // Security: NodeRegistry is the final commit boundary. Everything capable
    // of authenticating the deleted Node has already been revoked before this
    // trusted configuration entry disappears.
    if (!config_.node_registry->erase(
            node_id,
            &registry_error)) {
        output.result =
            NodeDeletionServiceResult::
                RegistryCleanupFailed;

        return output;
    }

    output.node_removed = true;
    output.result =
        NodeDeletionServiceResult::Ok;

    return output;
}

} // namespace pqnas::cluster
