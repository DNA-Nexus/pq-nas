#include "user_placement_policy.h"

#include <cstddef>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

namespace pqnas::cluster {
namespace {

bool valid_stable_identifier(
    const std::string& value
) {
    if (value.empty() ||
        value.size() > 64) {
        return false;
    }

    for (std::size_t index = 0;
         index < value.size();
         ++index) {
        const unsigned char character =
            static_cast<unsigned char>(
                value[index]
            );

        const bool alphanumeric =
            (character >= 'A' &&
             character <= 'Z') ||
            (character >= 'a' &&
             character <= 'z') ||
            (character >= '0' &&
             character <= '9');

        if (index == 0) {
            if (!alphanumeric) {
                return false;
            }

            continue;
        }

        if (!alphanumeric &&
            character != '-' &&
            character != '_' &&
            character != '.') {
            return false;
        }
    }

    return true;
}

void set_error(
    std::string* error_code,
    const std::string& value
) {
    if (error_code) {
        *error_code = value;
    }
}

bool valid_mode(
    UserPlacementPolicyMode mode
) {
    switch (mode) {
    case UserPlacementPolicyMode::Automatic:
    case UserPlacementPolicyMode::Pinned:
    case UserPlacementPolicyMode::AllowedNodes:
        return true;
    }

    return false;
}

bool valid_fallback(
    UserPlacementPolicyFallback fallback
) {
    switch (fallback) {
    case UserPlacementPolicyFallback::Deny:
    case UserPlacementPolicyFallback::Automatic:
        return true;
    }

    return false;
}

std::vector<NodeRegistryRecord> filter_nodes(
    const std::vector<NodeRegistryRecord>& nodes,
    const std::vector<std::string>& allowed_node_ids
) {
    const std::unordered_set<std::string> allowed(
        allowed_node_ids.begin(),
        allowed_node_ids.end()
    );

    std::vector<NodeRegistryRecord> filtered;
    filtered.reserve(nodes.size());

    for (const NodeRegistryRecord& node : nodes) {
        if (allowed.find(node.node_id) !=
            allowed.end()) {
            filtered.push_back(node);
        }
    }

    return filtered;
}

UserPlacementPolicyResult make_invalid_policy_result(
    const UserPlacementPolicy& policy,
    const std::string& error_code
) {
    UserPlacementPolicyResult result;
    result.error_code = error_code;
    result.policy_id = policy.policy_id;
    result.policy_mode =
        user_placement_policy_mode_name(
            policy.mode
        );

    result.policy_fallback =
        user_placement_policy_fallback_name(
            policy.fallback
        );

    return result;
}

UserPlacementPolicyResult make_success_result(
    const UserPlacementPolicy& policy,
    UserPlacementSelectionResult selection,
    const std::string& selected_scope,
    bool used_fallback
) {
    UserPlacementPolicyResult result;
    result.ok = true;
    result.used_fallback = used_fallback;
    result.policy_id = policy.policy_id;

    result.policy_mode =
        user_placement_policy_mode_name(
            policy.mode
        );

    result.policy_fallback =
        user_placement_policy_fallback_name(
            policy.fallback
        );

    result.selected_scope =
        selected_scope;

    result.selection =
        std::move(selection);

    return result;
}

UserPlacementPolicyResult make_unavailable_result(
    const UserPlacementPolicy& policy,
    const std::string& error_code,
    UserPlacementSelectionResult selection
) {
    UserPlacementPolicyResult result;
    result.error_code = error_code;
    result.policy_id = policy.policy_id;

    result.policy_mode =
        user_placement_policy_mode_name(
            policy.mode
        );

    result.policy_fallback =
        user_placement_policy_fallback_name(
            policy.fallback
        );

    result.selection =
        std::move(selection);

    return result;
}

} // namespace

const char* user_placement_policy_mode_name(
    UserPlacementPolicyMode mode
) {
    switch (mode) {
    case UserPlacementPolicyMode::Automatic:
        return "automatic";

    case UserPlacementPolicyMode::Pinned:
        return "pinned";

    case UserPlacementPolicyMode::AllowedNodes:
        return "allowed_nodes";
    }

    return "invalid";
}

const char* user_placement_policy_fallback_name(
    UserPlacementPolicyFallback fallback
) {
    switch (fallback) {
    case UserPlacementPolicyFallback::Deny:
        return "deny";

    case UserPlacementPolicyFallback::Automatic:
        return "automatic";
    }

    return "invalid";
}

bool validate_user_placement_policy(
    const UserPlacementPolicy& policy,
    std::string* error_code
) {
    if (error_code) {
        error_code->clear();
    }

    if (!valid_stable_identifier(
            policy.policy_id)) {
        set_error(
            error_code,
            "invalid_policy_id"
        );
        return false;
    }

    if (!valid_mode(policy.mode)) {
        set_error(
            error_code,
            "invalid_policy_mode"
        );
        return false;
    }

    if (!valid_fallback(policy.fallback)) {
        set_error(
            error_code,
            "invalid_policy_fallback"
        );
        return false;
    }

    if (policy.node_ids.size() >
        kUserPlacementPolicyMaxNodeCount) {
        set_error(
            error_code,
            "too_many_policy_nodes"
        );
        return false;
    }

    switch (policy.mode) {
    case UserPlacementPolicyMode::Automatic:
        if (!policy.node_ids.empty()) {
            set_error(
                error_code,
                "automatic_policy_has_nodes"
            );
            return false;
        }
        break;

    case UserPlacementPolicyMode::Pinned:
        if (policy.node_ids.size() != 1) {
            set_error(
                error_code,
                "pinned_policy_requires_one_node"
            );
            return false;
        }
        break;

    case UserPlacementPolicyMode::AllowedNodes:
        if (policy.node_ids.empty()) {
            set_error(
                error_code,
                "allowed_nodes_policy_is_empty"
            );
            return false;
        }
        break;
    }

    std::set<std::string> unique_node_ids;

    for (const std::string& node_id :
         policy.node_ids) {
        if (!valid_stable_identifier(node_id)) {
            set_error(
                error_code,
                "invalid_policy_node_id"
            );
            return false;
        }

        if (!unique_node_ids.insert(
                node_id).second) {
            set_error(
                error_code,
                "duplicate_policy_node_id"
            );
            return false;
        }
    }

    return true;
}

UserPlacementPolicyResult resolve_user_placement_policy(
    const UserPlacementPolicy& policy,
    const std::vector<NodeRegistryRecord>& nodes,
    const std::unordered_map<
        std::string,
        std::size_t
    >& assigned_user_counts
) {
    std::string validation_error;

    if (!validate_user_placement_policy(
            policy,
            &validation_error)) {
        return make_invalid_policy_result(
            policy,
            validation_error
        );
    }

    if (policy.mode ==
        UserPlacementPolicyMode::Automatic) {
        UserPlacementSelectionResult selection =
            select_user_home_node(
                nodes,
                assigned_user_counts
            );

        if (!selection.ok) {
            return make_unavailable_result(
                policy,
                selection.error_code,
                std::move(selection)
            );
        }

        return make_success_result(
            policy,
            std::move(selection),
            "automatic",
            false
        );
    }

    const std::vector<NodeRegistryRecord>
        policy_nodes =
            filter_nodes(
                nodes,
                policy.node_ids
            );

    UserPlacementSelectionResult
        policy_selection =
            select_user_home_node(
                policy_nodes,
                assigned_user_counts
            );

    if (policy_selection.ok) {
        return make_success_result(
            policy,
            std::move(policy_selection),
            "policy",
            false
        );
    }

    if (policy.fallback ==
        UserPlacementPolicyFallback::Deny) {
        const std::string error_code =
            policy.mode ==
                UserPlacementPolicyMode::Pinned
                    ? "policy_target_unavailable"
                    : "no_policy_eligible_nodes";

        return make_unavailable_result(
            policy,
            error_code,
            std::move(policy_selection)
        );
    }

    UserPlacementSelectionResult
        fallback_selection =
            select_user_home_node(
                nodes,
                assigned_user_counts
            );

    if (!fallback_selection.ok) {
        return make_unavailable_result(
            policy,
            "no_fallback_eligible_nodes",
            std::move(fallback_selection)
        );
    }

    return make_success_result(
        policy,
        std::move(fallback_selection),
        "fallback_automatic",
        true
    );
}

} // namespace pqnas::cluster
