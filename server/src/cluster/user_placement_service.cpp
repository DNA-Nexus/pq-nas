#include "user_placement_service.h"

#include "node_registry.h"
#include "users_registry.h"

#include <map>
#include <string>
#include <utility>

namespace pqnas::cluster {
namespace {

UserPlacementResult failure_result(
    const std::string& error_code,
    const std::string& previous_home_node = {}
) {
    UserPlacementResult result;
    result.error_code = error_code;
    result.previous_home_node =
        previous_home_node;

    return result;
}

} // namespace

UserPlacementService::UserPlacementService(
    UserPlacementServiceConfig config
)
    : config_(std::move(config)) {}

bool UserPlacementService::config_valid() const {
    return
        config_.users &&
        config_.nodes &&
        config_.placement_mutex &&
        !config_.users_path.empty() &&
        static_cast<bool>(config_.audit_emit);
}

void UserPlacementService::audit_best_effort(
    const std::string& outcome,
    const std::string& fingerprint,
    const std::string& node_id,
    const std::string& actor_fingerprint,
    const std::string& reason_code,
    bool changed,
    const std::string& previous_home_node
) const {
    if (!config_.audit_emit) {
        return;
    }

    // Security: audit only stable identifiers and bounded result categories.
    // Never include node URLs, filesystem paths, credentials or raw errors.
    std::map<std::string, std::string> fields{
        {"fingerprint", fingerprint},
        {"reason_code", reason_code},
        {"changed", changed ? "true" : "false"}
    };

    if (!node_id.empty()) {
        fields["node_id"] = node_id;
    }

    if (!actor_fingerprint.empty()) {
        fields["actor_fp"] =
            actor_fingerprint;
    }

    if (!previous_home_node.empty()) {
        fields["previous_home_node"] =
            previous_home_node;
    }

    try {
        config_.audit_emit(
            "admin.user_home_node.assign",
            outcome,
            fields
        );
    } catch (...) {
        // Placement persistence must not be rolled back by audit failure.
    }
}

UserPlacementResult UserPlacementService::assign(
    const std::string& fingerprint,
    const std::string& node_id,
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

    if (node_id.empty() ||
        !valid_user_home_node_id(node_id)) {
        audit_best_effort(
            "fail",
            fingerprint,
            "",
            actor_fingerprint,
            "invalid_node_id",
            false,
            ""
        );

        return failure_result(
            "invalid_node_id"
        );
    }

    // Integrity: node eligibility and users.json persistence are
    // serialized with node disable and delete operations.
    std::lock_guard<std::mutex> placement_lock(
        *config_.placement_mutex
    );

    const auto user =
        config_.users->get(fingerprint);

    if (!user.has_value()) {
        audit_best_effort(
            "fail",
            fingerprint,
            node_id,
            actor_fingerprint,
            "user_not_found",
            false,
            ""
        );

        return failure_result(
            "user_not_found"
        );
    }

    const std::string previous_home_node =
        user->home_node;

    const auto node =
        config_.nodes->get(node_id);

    if (!node.has_value()) {
        audit_best_effort(
            "fail",
            fingerprint,
            node_id,
            actor_fingerprint,
            "node_not_found",
            false,
            previous_home_node
        );

        return failure_result(
            "node_not_found",
            previous_home_node
        );
    }

    if (!node->enabled) {
        audit_best_effort(
            "fail",
            fingerprint,
            node_id,
            actor_fingerprint,
            "node_disabled",
            false,
            previous_home_node
        );

        return failure_result(
            "node_disabled",
            previous_home_node
        );
    }

    bool changed = false;
    std::string persistence_error;

    if (!config_.users->set_home_node_persisted(
            fingerprint,
            node_id,
            config_.users_path,
            &changed,
            &persistence_error)) {
        const std::string error_code =
            persistence_error == "user not found"
                ? "user_not_found"
                : "persistence_failed";

        audit_best_effort(
            "fail",
            fingerprint,
            node_id,
            actor_fingerprint,
            error_code,
            false,
            previous_home_node
        );

        return failure_result(
            error_code,
            previous_home_node
        );
    }

    audit_best_effort(
        "ok",
        fingerprint,
        node_id,
        actor_fingerprint,
        changed
            ? "placement_updated"
            : "placement_unchanged",
        changed,
        previous_home_node
    );

    UserPlacementResult result;
    result.ok = true;
    result.changed = changed;
    result.previous_home_node =
        previous_home_node;
    result.home_node = node_id;

    return result;
}

} // namespace pqnas::cluster
