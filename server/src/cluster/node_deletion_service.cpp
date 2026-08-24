#include "cluster/node_deletion_service.h"

#include "cluster/cluster_identity_validation.h"
#include "cluster/node_credential_registry.h"
#include "cluster/node_enrollment_registry.h"
#include "cluster/node_registry.h"
#include "cluster/node_tls_trust_store.h"
#include "cluster/storage_user_projection_sync_state_store.h"

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
                ConfirmedProjectionActive:
            return "node_deletion_confirmed_projection_active";

        case NodeDeletionServiceResult::
                ProjectionStateCheckFailed:
            return "node_deletion_projection_state_check_failed";

        case NodeDeletionServiceResult::
                EnrollmentCleanupFailed:
            return "node_deletion_enrollment_cleanup_failed";

        case NodeDeletionServiceResult::
                CredentialCleanupFailed:
            return "node_deletion_credential_cleanup_failed";

        case NodeDeletionServiceResult::
                TlsTrustCleanupFailed:
            return "node_deletion_tls_trust_cleanup_failed";

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
        config_.credential_registry &&
        config_.tls_trust_store &&
        config_.projection_sync_state_store &&
        config_.projection_transition_mutex;
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

    // Security: serialize this decision with reconciliation before touching
    // bootstrap, bearer or TLS identity state. Otherwise an in-flight
    // reconciler could confirm authorization on this Node after a stale
    // deletion preflight.
    std::lock_guard<std::mutex>
        projection_transition_lock(
            *config_.projection_transition_mutex
        );

    std::string projection_state_error;

    // Security: absence from confirmed state is authoritative only when the
    // visible sync-state snapshot is known durable.
    if (!config_.projection_sync_state_store
             ->ensure_durable(
                 &projection_state_error
             )) {
        output.result =
            NodeDeletionServiceResult::
                ProjectionStateCheckFailed;

        return output;
    }

    const std::vector<
        StorageUserProjectionSyncState
    > confirmed_states =
        config_.projection_sync_state_store
            ->snapshot();

    for (const auto& state :
         confirmed_states) {
        if (state.home_node != node_id) {
            continue;
        }

        // Security: reconciliation must first revoke this confirmed Storage
        // authorization and remove its durable state before Node lifecycle
        // credentials or identity may be destroyed.
        output.result =
            NodeDeletionServiceResult::
                ConfirmedProjectionActive;

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

    std::string tls_trust_error;

    const NodeTlsTrustStoreResult
        tls_trust_result =
            config_.tls_trust_store->erase(
                node_id,
                &tls_trust_error
            );

    if (tls_trust_result ==
        NodeTlsTrustStoreResult::Ok) {
        output.tls_trust_removed = true;
    } else if (
        tls_trust_result !=
            NodeTlsTrustStoreResult::NotFound) {
        // Security: a deleted Node ID must not retain reusable TLS identity
        // state that could interfere with or impersonate a later enrollment.
        output.result =
            NodeDeletionServiceResult::
                TlsTrustCleanupFailed;

        return output;
    }

    std::string registry_error;

    // Security: NodeRegistry is the final commit boundary. Every reusable
    // bootstrap, bearer and TLS identity binding has been revoked before this
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
