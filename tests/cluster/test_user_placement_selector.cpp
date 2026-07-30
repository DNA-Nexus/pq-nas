#include "cluster/node_status.h"
#include "cluster/user_placement_selector.h"

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

std::size_t rejection_count(
    const pqnas::cluster::
        UserPlacementSelectionResult& result,
    const std::string& reason
) {
    const auto iterator =
        result.rejected.find(reason);

    if (iterator == result.rejected.end()) {
        return 0;
    }

    return iterator->second;
}

pqnas::cluster::NodeRegistryRecord make_online_node(
    const std::string& node_id,
    std::uint64_t free_bytes
) {
    pqnas::cluster::NodeRegistryRecord node;

    node.node_id = node_id;
    node.enabled = true;
    node.health = "online";

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
        NodeRegistryRecord;

    using pqnas::cluster::
        select_user_home_node;

    {
        const auto result =
            select_user_home_node(
                {},
                {}
            );

        expect(
            !result.ok &&
            result.error_code ==
                "no_eligible_nodes" &&
            result.considered_nodes == 0 &&
            result.eligible_nodes == 0,
            "empty input should fail closed"
        );
    }

    {
        std::vector<NodeRegistryRecord> nodes;

        NodeRegistryRecord disabled =
            make_online_node(
                "node-disabled",
                500
            );

        disabled.enabled = false;
        disabled.health = "disabled";
        nodes.push_back(disabled);

        NodeRegistryRecord offline =
            make_online_node(
                "node-offline",
                500
            );

        offline.health = "offline";
        nodes.push_back(offline);

        NodeRegistryRecord missing_status =
            make_online_node(
                "node-missing-status",
                500
            );

        missing_status.protocol_version = 0;
        nodes.push_back(missing_status);

        NodeRegistryRecord missing_timestamp =
            make_online_node(
                "node-missing-timestamp",
                500
            );

        missing_timestamp.last_seen_at.clear();
        nodes.push_back(missing_timestamp);

        NodeRegistryRecord invalid_storage =
            make_online_node(
                "node-invalid-storage",
                500
            );

        invalid_storage.storage_total_bytes = 0;
        nodes.push_back(invalid_storage);

        NodeRegistryRecord invalid_counter =
            make_online_node(
                "node-invalid-counter",
                500
            );

        invalid_counter.storage_free_bytes = 1001;
        nodes.push_back(invalid_counter);

        NodeRegistryRecord missing_fstype =
            make_online_node(
                "node-missing-fstype",
                500
            );

        missing_fstype.storage_fstype.clear();
        nodes.push_back(missing_fstype);

        NodeRegistryRecord full =
            make_online_node(
                "node-full",
                0
            );

        nodes.push_back(full);

        const auto result =
            select_user_home_node(
                nodes,
                {}
            );

        expect(
            !result.ok &&
            result.error_code ==
                "no_eligible_nodes",
            "all ineligible nodes should fail closed"
        );

        expect(
            result.considered_nodes == 8 &&
            result.eligible_nodes == 0,
            "selector should account for every input node"
        );

        expect(
            rejection_count(
                result,
                "disabled"
            ) == 1,
            "disabled rejection count"
        );

        expect(
            rejection_count(
                result,
                "not_online"
            ) == 1,
            "offline rejection count"
        );

        expect(
            rejection_count(
                result,
                "missing_status"
            ) == 2,
            "missing heartbeat status rejection count"
        );

        expect(
            rejection_count(
                result,
                "invalid_storage"
            ) == 3,
            "invalid storage rejection count"
        );

        expect(
            rejection_count(
                result,
                "no_free_storage"
            ) == 1,
            "full node rejection count"
        );
    }

    {
        std::vector<NodeRegistryRecord> nodes{
            make_online_node(
                "node-more-free",
                800
            ),
            make_online_node(
                "node-fewer-users",
                700
            )
        };

        std::unordered_map<
            std::string,
            std::size_t
        > counts{
            {"node-more-free", 20},
            {"node-fewer-users", 0}
        };

        const auto result =
            select_user_home_node(
                nodes,
                counts
            );

        expect(
            result.ok &&
            result.node_id ==
                "node-more-free" &&
            result.storage_free_bytes == 800 &&
            result.assigned_users == 20,
            "free capacity should be the primary ranking criterion"
        );
    }

    {
        std::vector<NodeRegistryRecord> nodes{
            make_online_node(
                "node-busy",
                800
            ),
            make_online_node(
                "node-light",
                800
            )
        };

        std::unordered_map<
            std::string,
            std::size_t
        > counts{
            {"node-busy", 7},
            {"node-light", 2}
        };

        const auto result =
            select_user_home_node(
                nodes,
                counts
            );

        expect(
            result.ok &&
            result.node_id ==
                "node-light" &&
            result.assigned_users == 2,
            "assigned-user count should break equal-capacity ties"
        );
    }

    {
        std::vector<NodeRegistryRecord> nodes{
            make_online_node(
                "node-z",
                800
            ),
            make_online_node(
                "node-a",
                800
            )
        };

        std::unordered_map<
            std::string,
            std::size_t
        > counts{
            {"node-z", 2},
            {"node-a", 2}
        };

        const auto result =
            select_user_home_node(
                nodes,
                counts
            );

        expect(
            result.ok &&
            result.node_id == "node-a",
            "stable node ID should provide deterministic final tie-break"
        );

        expect(
            result.selection_reason ==
                "free_bytes_desc_assigned_users_asc_node_id_asc",
            "selection should expose the bounded policy identifier"
        );
    }

    {
        std::vector<NodeRegistryRecord> nodes{
            make_online_node(
                "node-b",
                600
            ),
            make_online_node(
                "node-a",
                600
            )
        };

        const auto result =
            select_user_home_node(
                nodes,
                {}
            );

        expect(
            result.ok &&
            result.node_id == "node-a" &&
            result.assigned_users == 0 &&
            result.considered_nodes == 2 &&
            result.eligible_nodes == 2,
            "missing assignment counters should default to zero"
        );
    }

    if (failures != 0) {
        std::cerr
            << failures
            << " user placement selector test(s) failed\n";
        return 1;
    }

    std::cout
        << "OK: user placement selector tests passed\n";

    return 0;
}
