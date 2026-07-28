#pragma once

#include "user_placement_selector.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace pqnas::cluster {

inline constexpr std::size_t
    kUserPlacementPolicyMaxNodeCount = 256;

enum class UserPlacementPolicyMode {
    Automatic,
    Pinned,
    AllowedNodes
};

enum class UserPlacementPolicyFallback {
    Deny,
    Automatic
};

struct UserPlacementPolicy {
    // Stable Director-managed identifier such as "business-backup".
    std::string policy_id;

    UserPlacementPolicyMode mode =
        UserPlacementPolicyMode::Automatic;

    UserPlacementPolicyFallback fallback =
        UserPlacementPolicyFallback::Deny;

    // automatic     -> empty
    // pinned        -> exactly one node ID
    // allowed_nodes -> one or more node IDs
    std::vector<std::string> node_ids;
};

// Read-only policy result.
//
// Security:
// - contains stable policy/node identifiers and aggregate selection data only
// - never copies node URLs, hostnames, credentials or user fingerprints
struct UserPlacementPolicyResult {
    bool ok = false;
    bool used_fallback = false;

    std::string error_code;
    std::string policy_id;
    std::string policy_mode;
    std::string policy_fallback;
    std::string selected_scope;

    UserPlacementSelectionResult selection;
};

const char* user_placement_policy_mode_name(
    UserPlacementPolicyMode mode
);

const char* user_placement_policy_fallback_name(
    UserPlacementPolicyFallback fallback
);

// Strict model validation. Error values are bounded stable categories.
bool validate_user_placement_policy(
    const UserPlacementPolicy& policy,
    std::string* error_code
);

// Resolves a policy without mutating users, nodes or persistent state.
//
// automatic:
//   considers all eligible nodes
//
// pinned:
//   considers only the named node
//
// allowed_nodes:
//   considers only the listed node IDs
//
// fallback=deny:
//   never escapes the policy node set
//
// fallback=automatic:
//   may retry against all eligible nodes when the policy node set has no
//   eligible candidate
UserPlacementPolicyResult resolve_user_placement_policy(
    const UserPlacementPolicy& policy,
    const std::vector<NodeRegistryRecord>& nodes,
    const std::unordered_map<
        std::string,
        std::size_t
    >& assigned_user_counts
);

} // namespace pqnas::cluster
