#pragma once

#include "node_registry.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace pqnas::cluster {

// Read-only recommendation result.
//
// Security:
// - contains stable node IDs and aggregate capacity/load values only
// - never copies node URLs, hostnames, credentials or user identifiers
struct UserPlacementSelectionResult {
    bool ok = false;

    std::string error_code;
    std::string selection_reason;
    std::string node_id;

    std::uint64_t storage_total_bytes = 0;
    std::uint64_t storage_free_bytes = 0;
    std::size_t assigned_users = 0;

    std::size_t considered_nodes = 0;
    std::size_t eligible_nodes = 0;

    // Stable bounded rejection categories, counted across all nodes.
    std::map<
        std::string,
        std::size_t
    > rejected;
};

// Produces a deterministic recommendation without mutating either registry.
//
// Ranking:
// 1. lowest assigned-user count
// 2. highest free storage
// 3. lexicographically smallest stable node ID
UserPlacementSelectionResult select_user_home_node(
    const std::vector<NodeRegistryRecord>& nodes,
    const std::unordered_map<
        std::string,
        std::size_t
    >& assigned_user_counts
);

} // namespace pqnas::cluster
