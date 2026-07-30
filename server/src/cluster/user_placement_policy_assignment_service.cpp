#include "user_placement_policy_assignment_service.h"

#include "user_placement_policy_registry.h"
#include "users_registry.h"

#include <map>
#include <string>
#include <utility>

namespace pqnas::cluster {
namespace {

UserPlacementPolicyAssignmentResult failure_result(
    const std::string& error_code,
    const std::string& previous_policy_id = {}
) {
    UserPlacementPolicyAssignmentResult result;
    result.error_code = error_code;
    result.previous_policy_id =
        previous_policy_id;

    return result;
}

} // namespace

UserPlacementPolicyAssignmentService::
UserPlacementPolicyAssignmentService(
    UserPlacementPolicyAssignmentServiceConfig
        config
)
    : config_(std::move(config)) {}

bool UserPlacementPolicyAssignmentService::
config_valid() const {
    return
        config_.users &&
        config_.policies &&
        config_.placement_mutex &&
        !config_.users_path.empty() &&
        static_cast<bool>(config_.audit_emit);
}

void UserPlacementPolicyAssignmentService::
audit_best_effort(
    const std::string& outcome,
    const std::string& fingerprint,
    const std::string& policy_id,
    const std::string& actor_fingerprint,
    const std::string& reason_code,
    bool changed,
    const std::string& previous_policy_id
) const {
    if (!config_.audit_emit) {
        return;
    }

    // Security: audit only stable identifiers and bounded outcome categories.
    // Never include policy node sets, paths, URLs, credentials or raw errors.
    std::map<std::string, std::string> fields{
        {"fingerprint", fingerprint},
        {"reason_code", reason_code},
        {"changed", changed ? "true" : "false"}
    };

    if (!policy_id.empty()) {
        fields["policy_id"] =
            policy_id;
    }

    if (!previous_policy_id.empty()) {
        fields["previous_policy_id"] =
            previous_policy_id;
    }

    if (!actor_fingerprint.empty()) {
        fields["actor_fp"] =
            actor_fingerprint;
    }

    try {
        config_.audit_emit(
            "admin.user_placement_policy.assign",
            outcome,
            fields
        );
    } catch (...) {
        // Persistence must not be rolled back because audit emission failed.
    }
}

UserPlacementPolicyAssignmentResult
UserPlacementPolicyAssignmentService::assign(
    const std::string& fingerprint,
    const std::string& policy_id,
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
            "",
            actor_fingerprint,
            "invalid_fingerprint",
            false,
            ""
        );

        return failure_result(
            "invalid_fingerprint"
        );
    }

    // Empty is a deliberate clear operation.
    if (!valid_user_placement_policy_id(
            policy_id)) {
        audit_best_effort(
            "fail",
            fingerprint,
            "",
            actor_fingerprint,
            "invalid_policy_id",
            false,
            ""
        );

        return failure_result(
            "invalid_policy_id"
        );
    }

    // Integrity: policy lookup and users.json persistence are serialized with
    // home-node placement and node disable/delete operations.
    std::lock_guard<std::mutex> placement_lock(
        *config_.placement_mutex
    );

    const auto user =
        config_.users->get(
            fingerprint
        );

    if (!user.has_value()) {
        audit_best_effort(
            "fail",
            fingerprint,
            policy_id,
            actor_fingerprint,
            "user_not_found",
            false,
            ""
        );

        return failure_result(
            "user_not_found"
        );
    }

    const std::string previous_policy_id =
        user->placement_policy_id;

    if (!policy_id.empty() &&
        !config_.policies->get(
            policy_id).has_value()) {
        audit_best_effort(
            "fail",
            fingerprint,
            policy_id,
            actor_fingerprint,
            "policy_not_found",
            false,
            previous_policy_id
        );

        return failure_result(
            "policy_not_found",
            previous_policy_id
        );
    }

    bool changed = false;
    std::string persistence_error;

    if (!config_.users->
            set_placement_policy_id_persisted(
                fingerprint,
                policy_id,
                config_.users_path,
                &changed,
                &persistence_error
            )) {
        const std::string error_code =
            persistence_error == "user not found"
                ? "user_not_found"
                : "persistence_failed";

        audit_best_effort(
            "fail",
            fingerprint,
            policy_id,
            actor_fingerprint,
            error_code,
            false,
            previous_policy_id
        );

        return failure_result(
            error_code,
            previous_policy_id
        );
    }

    std::string reason_code;

    if (!changed) {
        reason_code =
            "policy_unchanged";
    } else if (policy_id.empty()) {
        reason_code =
            "policy_cleared";
    } else {
        reason_code =
            "policy_updated";
    }

    audit_best_effort(
        "ok",
        fingerprint,
        policy_id,
        actor_fingerprint,
        reason_code,
        changed,
        previous_policy_id
    );

    UserPlacementPolicyAssignmentResult result;
    result.ok = true;
    result.changed = changed;
    result.previous_policy_id =
        previous_policy_id;
    result.placement_policy_id =
        policy_id;

    return result;
}

} // namespace pqnas::cluster
