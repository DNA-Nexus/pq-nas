#include "cluster/node_status.h"
#include "cluster/user_placement_policy.h"

#include <cstddef>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

int failures = 0;

void expect(
    bool condition,
    const std::string& message
) {
    if (!condition) {
        ++failures;

        std::cerr
            << "FAIL: "
            << message
            << '\n';
    }
}

pqnas::cluster::NodeRegistryRecord
make_online_node(
    const std::string& node_id,
    std::uint64_t free_bytes
) {
    pqnas::cluster::NodeRegistryRecord node;

    node.node_id = node_id;
    node.enabled = true;
    node.health = "online";

    // RT-007: normal placement fixtures represent Director-approved capacity.
    node.capacity_ceiling_bytes = 1000;

    node.last_seen_at =
        "2026-07-28T12:00:00Z";

    node.node_reported_at =
        "2026-07-28T11:59:59Z";

    node.protocol_version =
        pqnas::cluster::
            kNodeStatusProtocolVersion;

    node.storage_total_bytes = 1000;
    node.storage_free_bytes = free_bytes;
    node.storage_used_bytes =
        node.storage_total_bytes -
        free_bytes;

    node.storage_fstype = "ext4";

    return node;
}

} // namespace

int main() {
    using pqnas::cluster::
        UserPlacementPolicy;

    using pqnas::cluster::
        UserPlacementPolicyFallback;

    using pqnas::cluster::
        UserPlacementPolicyMode;

    using pqnas::cluster::
        resolve_user_placement_policy;

    const std::vector<
        pqnas::cluster::NodeRegistryRecord
    > nodes{
        make_online_node(
            "node-standard-01",
            900
        ),
        make_online_node(
            "node-business-01",
            600
        ),
        make_online_node(
            "node-business-02",
            700
        )
    };

    const std::unordered_map<
        std::string,
        std::size_t
    > assigned_user_counts{
        {"node-standard-01", 10},
        {"node-business-01", 2},
        {"node-business-02", 5}
    };

    {
        UserPlacementPolicy policy;
        policy.policy_id = "standard";
        policy.mode =
            UserPlacementPolicyMode::Automatic;

        const auto result =
            resolve_user_placement_policy(
                policy,
                nodes,
                assigned_user_counts
            );

        expect(
            result.ok &&
            result.selection.node_id ==
                "node-business-01" &&
            result.selected_scope ==
                "automatic" &&
            !result.used_fallback,
            "automatic policy should use the bounded normal selector"
        );
    }

    {
        UserPlacementPolicy policy;
        policy.policy_id =
            "business-backup";

        policy.mode =
            UserPlacementPolicyMode::Pinned;

        policy.fallback =
            UserPlacementPolicyFallback::Deny;

        policy.node_ids = {
            "node-business-01"
        };

        const auto result =
            resolve_user_placement_policy(
                policy,
                nodes,
                assigned_user_counts
            );

        expect(
            result.ok &&
            result.selection.node_id ==
                "node-business-01" &&
            result.selected_scope ==
                "policy" &&
            !result.used_fallback,
            "pinned business policy must override the more spacious node"
        );
    }

    {
        UserPlacementPolicy policy;
        policy.policy_id =
            "business-pool";

        policy.mode =
            UserPlacementPolicyMode::AllowedNodes;

        policy.fallback =
            UserPlacementPolicyFallback::Deny;

        policy.node_ids = {
            "node-business-01",
            "node-business-02"
        };

        const auto result =
            resolve_user_placement_policy(
                policy,
                nodes,
                assigned_user_counts
            );

        expect(
            result.ok &&
            result.selection.node_id ==
                "node-business-01",
            "allowed-node policy should use bounded ranking within its node set"
        );
    }

    {
        auto unavailable_nodes =
            nodes;

        unavailable_nodes[1].health =
            "offline";

        UserPlacementPolicy policy;
        policy.policy_id =
            "business-backup";

        policy.mode =
            UserPlacementPolicyMode::Pinned;

        policy.fallback =
            UserPlacementPolicyFallback::Deny;

        policy.node_ids = {
            "node-business-01"
        };

        const auto result =
            resolve_user_placement_policy(
                policy,
                unavailable_nodes,
                assigned_user_counts
            );

        expect(
            !result.ok &&
            result.error_code ==
                "policy_target_unavailable" &&
            !result.used_fallback,
            "pinned deny policy must fail closed when its node is offline"
        );
    }

    {
        auto unavailable_nodes =
            nodes;

        unavailable_nodes[1].health =
            "offline";

        UserPlacementPolicy policy;
        policy.policy_id =
            "business-fallback";

        policy.mode =
            UserPlacementPolicyMode::Pinned;

        policy.fallback =
            UserPlacementPolicyFallback::Automatic;

        policy.node_ids = {
            "node-business-01"
        };

        const auto result =
            resolve_user_placement_policy(
                policy,
                unavailable_nodes,
                assigned_user_counts
            );

        expect(
            result.ok &&
            result.used_fallback &&
            result.selected_scope ==
                "fallback_automatic" &&
            result.selection.node_id ==
                "node-business-02",
            "explicit automatic fallback may escape the pinned node"
        );
    }

    {
        UserPlacementPolicy policy;
        policy.policy_id =
            "business-missing";

        policy.mode =
            UserPlacementPolicyMode::AllowedNodes;

        policy.fallback =
            UserPlacementPolicyFallback::Deny;

        policy.node_ids = {
            "node-does-not-exist"
        };

        const auto result =
            resolve_user_placement_policy(
                policy,
                nodes,
                assigned_user_counts
            );

        expect(
            !result.ok &&
            result.error_code ==
                "no_policy_eligible_nodes",
            "missing allowed node must fail closed when fallback is deny"
        );
    }

    {
        UserPlacementPolicy policy;
        policy.policy_id =
            "broken-pinned";

        policy.mode =
            UserPlacementPolicyMode::Pinned;

        policy.node_ids = {
            "node-business-01",
            "node-business-02"
        };

        const auto result =
            resolve_user_placement_policy(
                policy,
                nodes,
                assigned_user_counts
            );

        expect(
            !result.ok &&
            result.error_code ==
                "pinned_policy_requires_one_node",
            "pinned policy must contain exactly one node"
        );
    }

    {
        UserPlacementPolicy policy;
        policy.policy_id =
            "duplicate-nodes";

        policy.mode =
            UserPlacementPolicyMode::AllowedNodes;

        policy.node_ids = {
            "node-business-01",
            "node-business-01"
        };

        const auto result =
            resolve_user_placement_policy(
                policy,
                nodes,
                assigned_user_counts
            );

        expect(
            !result.ok &&
            result.error_code ==
                "duplicate_policy_node_id",
            "duplicate policy nodes must be rejected"
        );
    }

    {
        UserPlacementPolicy policy;
        policy.policy_id =
            "../business";

        policy.mode =
            UserPlacementPolicyMode::Automatic;

        const auto result =
            resolve_user_placement_policy(
                policy,
                nodes,
                assigned_user_counts
            );

        expect(
            !result.ok &&
            result.error_code ==
                "invalid_policy_id",
            "path-like policy identifiers must be rejected"
        );
    }

    if (failures != 0) {
        std::cerr
            << failures
            << " user placement policy test(s) failed\n";
        return 1;
    }

    std::cout
        << "OK: user placement policy tests passed\n";

    return 0;
}
