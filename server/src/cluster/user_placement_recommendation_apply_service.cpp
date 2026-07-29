#include "user_placement_recommendation_apply_service.h"

#include "user_placement_preview_service.h"
#include "users_registry.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace pqnas::cluster {
namespace {

UserPlacementRecommendationApplyResult
failure_result(
    const std::string& error_code,
    const std::string& previous_home_node = {},
    const std::string& placement_policy_id = {}
) {
    UserPlacementRecommendationApplyResult result;
    result.error_code = error_code;
    result.previous_home_node =
        previous_home_node;

    result.placement_policy_id =
        placement_policy_id;

    return result;
}

} // namespace

UserPlacementRecommendationApplyService::
UserPlacementRecommendationApplyService(
    UserPlacementRecommendationApplyServiceConfig
        config
)
    : config_(std::move(config)) {}

bool UserPlacementRecommendationApplyService::
config_valid() const {
    return
        config_.users &&
        config_.preview_service &&
        !config_.users_path.empty() &&
        static_cast<bool>(
            config_.audit_emit
        ) &&
        config_.preview_service->
            coordination_mutex();
}

void UserPlacementRecommendationApplyService::
audit_best_effort(
    const std::string& outcome,
    const std::string& fingerprint,
    const std::string& actor_fingerprint,
    const std::string& previous_home_node,
    const std::string& home_node,
    const std::string& placement_policy_id,
    const std::string& reason_code,
    bool changed
) const {
    if (!config_.audit_emit) {
        return;
    }

    // Security: audit only stable identifiers and bounded outcome categories.
    // Never include node URLs, filesystem paths, credentials, request bodies
    // or raw resolver/persistence errors.
    std::map<std::string, std::string> fields{
        {"fingerprint", fingerprint},
        {"reason_code", reason_code},
        {"changed", changed ? "true" : "false"}
    };

    if (!actor_fingerprint.empty()) {
        fields["actor_fp"] =
            actor_fingerprint;
    }

    if (!previous_home_node.empty()) {
        fields["previous_home_node"] =
            previous_home_node;
    }

    if (!home_node.empty()) {
        fields["home_node"] =
            home_node;
    }

    if (!placement_policy_id.empty()) {
        fields["placement_policy_id"] =
            placement_policy_id;
    }

    try {
        config_.audit_emit(
            "admin.user_placement_recommendation.apply",
            outcome,
            fields
        );
    } catch (...) {
        // Successful persistence must not be rolled back by audit failure.
    }
}

UserPlacementRecommendationApplyResult
UserPlacementRecommendationApplyService::apply(
    const std::string& fingerprint,
    const std::string& expected_current_home_node,
    const std::string& expected_placement_policy_id,
    const std::string& expected_recommended_home_node,
    const std::string& actor_fingerprint
) {
    if (!config_valid()) {
        return failure_result(
            "service_unavailable"
        );
    }

    if (fingerprint.empty()) {
        audit_best_effort(
            "fail",
            "",
            actor_fingerprint,
            "",
            "",
            "",
            "invalid_fingerprint",
            false
        );

        return failure_result(
            "invalid_fingerprint"
        );
    }

    if (!valid_user_home_node_id(
            expected_current_home_node)) {
        audit_best_effort(
            "fail",
            fingerprint,
            actor_fingerprint,
            "",
            "",
            "",
            "invalid_expected_home_node",
            false
        );

        return failure_result(
            "invalid_expected_home_node"
        );
    }

    if (!valid_user_placement_policy_id(
            expected_placement_policy_id)) {
        audit_best_effort(
            "fail",
            fingerprint,
            actor_fingerprint,
            expected_current_home_node,
            "",
            "",
            "invalid_expected_policy_id",
            false
        );

        return failure_result(
            "invalid_expected_policy_id",
            expected_current_home_node
        );
    }

    if (expected_recommended_home_node.empty() ||
        !valid_user_home_node_id(
            expected_recommended_home_node)) {
        audit_best_effort(
            "fail",
            fingerprint,
            actor_fingerprint,
            expected_current_home_node,
            "",
            expected_placement_policy_id,
            "invalid_expected_recommended_home_node",
            false
        );

        return failure_result(
            "invalid_expected_recommended_home_node",
            expected_current_home_node,
            expected_placement_policy_id
        );
    }

    const std::shared_ptr<std::mutex>
        coordination_mutex =
            config_.preview_service->
                coordination_mutex();

    if (!coordination_mutex) {
        return failure_result(
            "service_unavailable"
        );
    }

    // TOCTOU protection: current user intent, policy resolution, node health,
    // selector inputs, expectation comparison and CAS persistence all happen
    // under the same Director coordination lock.
    std::lock_guard<std::mutex>
        coordination_lock(
            *coordination_mutex
        );

    const UserPlacementPreviewResult preview =
        config_.preview_service->
            preview_while_holding_coordination_lock(
                fingerprint
            );

    if (!preview.ok) {
        const std::string error_code =
            preview.error_code ==
                "user_not_found"
                    ? "user_not_found"
                    : (
                        preview.error_code ==
                            "policy_not_found"
                                ? "placement_preview_stale"
                                : "placement_evaluation_failed"
                    );

        audit_best_effort(
            "fail",
            fingerprint,
            actor_fingerprint,
            expected_current_home_node,
            "",
            expected_placement_policy_id,
            error_code,
            false
        );

        return failure_result(
            error_code,
            expected_current_home_node,
            expected_placement_policy_id
        );
    }

    const bool preview_matches =
        preview.recommendation_available &&
        preview.current_home_node ==
            expected_current_home_node &&
        preview.placement_policy_id ==
            expected_placement_policy_id &&
        preview.recommended_home_node ==
            expected_recommended_home_node;

    if (!preview_matches) {
        audit_best_effort(
            "fail",
            fingerprint,
            actor_fingerprint,
            expected_current_home_node,
            "",
            expected_placement_policy_id,
            "placement_preview_stale",
            false
        );

        return failure_result(
            "placement_preview_stale",
            expected_current_home_node,
            expected_placement_policy_id
        );
    }

    bool changed = false;
    bool expectation_matched = false;
    std::string persistence_error;

    if (!config_.users->
            set_home_node_persisted_if_current(
                fingerprint,
                expected_current_home_node,
                expected_placement_policy_id,
                expected_recommended_home_node,
                config_.users_path,
                &changed,
                &expectation_matched,
                &persistence_error)) {
        const std::string error_code =
            persistence_error ==
                "user not found"
                    ? "user_not_found"
                    : "persistence_failed";

        audit_best_effort(
            "fail",
            fingerprint,
            actor_fingerprint,
            expected_current_home_node,
            "",
            expected_placement_policy_id,
            error_code,
            false
        );

        return failure_result(
            error_code,
            expected_current_home_node,
            expected_placement_policy_id
        );
    }

    // Defense in depth: a direct registry mutation that bypassed the Director
    // coordination lock is still rejected by the UsersRegistry CAS primitive.
    if (!expectation_matched) {
        audit_best_effort(
            "fail",
            fingerprint,
            actor_fingerprint,
            expected_current_home_node,
            "",
            expected_placement_policy_id,
            "placement_preview_stale",
            false
        );

        return failure_result(
            "placement_preview_stale",
            expected_current_home_node,
            expected_placement_policy_id
        );
    }

    audit_best_effort(
        "ok",
        fingerprint,
        actor_fingerprint,
        expected_current_home_node,
        expected_recommended_home_node,
        expected_placement_policy_id,
        changed
            ? "placement_applied"
            : "placement_already_applied",
        changed
    );

    UserPlacementRecommendationApplyResult result;
    result.ok = true;
    result.changed = changed;

    result.previous_home_node =
        expected_current_home_node;

    result.home_node =
        expected_recommended_home_node;

    result.placement_policy_id =
        expected_placement_policy_id;

    return result;
}

} // namespace pqnas::cluster
