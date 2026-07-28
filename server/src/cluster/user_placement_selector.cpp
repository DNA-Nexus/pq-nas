#include "user_placement_selector.h"

#include "node_status.h"

#include <cstddef>
#include <string>

namespace pqnas::cluster {
namespace {

void reject(
    UserPlacementSelectionResult* result,
    const std::string& reason
) {
    if (!result) {
        return;
    }

    ++result->rejected[reason];
}

std::size_t assigned_users_for(
    const std::unordered_map<
        std::string,
        std::size_t
    >& counts,
    const std::string& node_id
) {
    const auto iterator =
        counts.find(node_id);

    if (iterator == counts.end()) {
        return 0;
    }

    return iterator->second;
}

bool candidate_is_better(
    const NodeRegistryRecord& candidate,
    std::size_t candidate_assigned_users,
    const UserPlacementSelectionResult& current
) {
    if (!current.ok) {
        return true;
    }

    if (candidate.storage_free_bytes !=
        current.storage_free_bytes) {
        return
            candidate.storage_free_bytes >
            current.storage_free_bytes;
    }

    if (candidate_assigned_users !=
        current.assigned_users) {
        return
            candidate_assigned_users <
            current.assigned_users;
    }

    return candidate.node_id <
        current.node_id;
}

} // namespace

UserPlacementSelectionResult select_user_home_node(
    const std::vector<NodeRegistryRecord>& nodes,
    const std::unordered_map<
        std::string,
        std::size_t
    >& assigned_user_counts
) {
    UserPlacementSelectionResult result;

    for (const NodeRegistryRecord& node : nodes) {
        ++result.considered_nodes;

        if (node.node_id.empty()) {
            reject(
                &result,
                "invalid_node_id"
            );
            continue;
        }

        if (!node.enabled) {
            reject(
                &result,
                "disabled"
            );
            continue;
        }

        if (node.health != "online") {
            reject(
                &result,
                "not_online"
            );
            continue;
        }

        // Fail closed unless this state came from a complete and current
        // protocol-compatible heartbeat observation.
        if (node.protocol_version !=
                kNodeStatusProtocolVersion ||
            node.last_seen_at.empty() ||
            node.node_reported_at.empty()) {
            reject(
                &result,
                "missing_status"
            );
            continue;
        }

        // Security: reject absent or internally inconsistent capacity data.
        // The selector must never recommend a node based on wrapped or
        // incomplete storage counters.
        if (node.storage_total_bytes == 0 ||
            node.storage_used_bytes >
                node.storage_total_bytes ||
            node.storage_free_bytes >
                node.storage_total_bytes ||
            node.storage_fstype.empty()) {
            reject(
                &result,
                "invalid_storage"
            );
            continue;
        }

        if (node.storage_free_bytes == 0) {
            reject(
                &result,
                "no_free_storage"
            );
            continue;
        }

        ++result.eligible_nodes;

        const std::size_t assigned_users =
            assigned_users_for(
                assigned_user_counts,
                node.node_id
            );

        if (!candidate_is_better(
                node,
                assigned_users,
                result)) {
            continue;
        }

        result.ok = true;
        result.node_id = node.node_id;
        result.storage_total_bytes =
            node.storage_total_bytes;
        result.storage_free_bytes =
            node.storage_free_bytes;
        result.assigned_users =
            assigned_users;
    }

    if (!result.ok) {
        result.error_code =
            "no_eligible_nodes";
        return result;
    }

    result.selection_reason =
        "free_bytes_desc_assigned_users_asc_node_id_asc";

    return result;
}

} // namespace pqnas::cluster
