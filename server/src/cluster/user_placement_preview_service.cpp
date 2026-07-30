#include "user_placement_preview_service.h"

#include <string>
#include <utility>

namespace pqnas::cluster {
namespace {

UserPlacementPreviewResult failure_result(
    const std::string& error_code
) {
    UserPlacementPreviewResult result;
    result.error_code = error_code;
    return result;
}

bool recommendation_unavailable_error(
    const std::string& error_code
) {
    return
        error_code == "no_eligible_nodes" ||
        error_code == "policy_target_unavailable" ||
        error_code == "no_policy_eligible_nodes" ||
        error_code == "no_fallback_eligible_nodes";
}

UserPlacementPolicy
make_implicit_automatic_policy() {
    UserPlacementPolicy policy;

    // Internal stable identifier required by the strict policy model. This is
    // not persisted or returned as the user's placement_policy_id.
    policy.policy_id =
        "implicit-automatic";

    policy.mode =
        UserPlacementPolicyMode::Automatic;

    policy.fallback =
        UserPlacementPolicyFallback::Deny;

    return policy;
}

} // namespace

UserPlacementPreviewService::
UserPlacementPreviewService(
    UserPlacementPreviewServiceConfig config
)
    : config_(std::move(config)) {}

bool UserPlacementPreviewService::
config_valid() const {
    return
        static_cast<bool>(config_.get_user) &&
        static_cast<bool>(config_.node_snapshot) &&
        static_cast<bool>(config_.get_policy) &&
        static_cast<bool>(
            config_.assigned_user_counts
        ) &&
        config_.placement_mutex;
}

std::shared_ptr<std::mutex>
UserPlacementPreviewService::
coordination_mutex() const {
    return config_.placement_mutex;
}

UserPlacementPreviewResult
UserPlacementPreviewService::preview(
    const std::string& fingerprint
) const {
    if (!config_valid()) {
        return failure_result(
            "service_unavailable"
        );
    }

    if (fingerprint.empty()) {
        return failure_result(
            "invalid_fingerprint"
        );
    }

    // Integrity: serialize the user/policy lookup and selector inputs against
    // placement and Director admin mutations. Each registry also returns its
    // own internally consistent copy.
    std::lock_guard<std::mutex> placement_lock(
        *config_.placement_mutex
    );

    return
        preview_while_holding_coordination_lock(
            fingerprint
        );
}

UserPlacementPreviewResult
UserPlacementPreviewService::
preview_while_holding_coordination_lock(
    const std::string& fingerprint
) const {
    if (!config_valid()) {
        return failure_result(
            "service_unavailable"
        );
    }

    if (fingerprint.empty()) {
        return failure_result(
            "invalid_fingerprint"
        );
    }

    std::optional<
        UserPlacementPreviewUserSnapshot
    > user;

    std::vector<NodeRegistryRecord> nodes;

    std::unordered_map<
        std::string,
        std::size_t
    > assigned_user_counts;

    try {
        user =
            config_.get_user(
                fingerprint
            );

        if (!user.has_value()) {
            return failure_result(
                "user_not_found"
            );
        }

        nodes =
            config_.node_snapshot();

        assigned_user_counts =
            config_.assigned_user_counts();
    } catch (...) {
        // Security: callback or snapshot failures are reduced to a bounded
        // category instead of exposing exception or storage details.
        return failure_result(
            "snapshot_failed"
        );
    }

    UserPlacementPolicy policy;
    std::string policy_source;

    if (user->placement_policy_id.empty()) {
        policy =
            make_implicit_automatic_policy();

        policy_source =
            "implicit_automatic";
    } else {
        std::optional<UserPlacementPolicy>
            stored_policy;

        try {
            stored_policy =
                config_.get_policy(
                    user->placement_policy_id
                );
        } catch (...) {
            return failure_result(
                "snapshot_failed"
            );
        }

        if (!stored_policy.has_value()) {
            return failure_result(
                "policy_not_found"
            );
        }

        policy =
            std::move(*stored_policy);

        policy_source =
            "registry";
    }

    const UserPlacementPolicyResult
        resolved =
            resolve_user_placement_policy(
                policy,
                nodes,
                assigned_user_counts
            );

    if (!resolved.ok &&
        !recommendation_unavailable_error(
            resolved.error_code)) {
        // A registry-loaded policy should always validate. Treat impossible
        // model failures as control-plane state errors, not placement advice.
        return failure_result(
            "policy_resolution_failed"
        );
    }

    UserPlacementPreviewResult result;
    result.ok = true;
    result.recommendation_available =
        resolved.ok;

    result.used_fallback =
        resolved.used_fallback;

    result.placement_policy_id =
        user->placement_policy_id;

    result.policy_source =
        policy_source;

    result.policy_mode =
        resolved.policy_mode;

    result.policy_fallback =
        resolved.policy_fallback;

    result.selected_scope =
        resolved.selected_scope;

    result.current_home_node =
        user->home_node;

    result.considered_nodes =
        resolved.selection.considered_nodes;

    result.eligible_nodes =
        resolved.selection.eligible_nodes;

    result.assigned_users =
        resolved.selection.assigned_users;

    result.storage_total_bytes =
        resolved.selection.storage_total_bytes;

    result.storage_free_bytes =
        resolved.selection.storage_free_bytes;

    result.selection_reason =
        resolved.selection.selection_reason;

    result.rejected =
        resolved.selection.rejected;

    if (!resolved.ok) {
        result.error_code =
            resolved.error_code;

        return result;
    }

    result.recommended_home_node =
        resolved.selection.node_id;

    result.current_home_node_matches =
        !result.recommended_home_node.empty() &&
        result.current_home_node ==
            result.recommended_home_node;

    return result;
}

} // namespace pqnas::cluster
