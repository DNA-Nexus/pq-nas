#include "cluster/user_placement_preview_service.h"

#include "cluster/node_status.h"

#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
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

pqnas::cluster::NodeRegistryRecord make_node(
    const std::string& node_id,
    std::uint64_t free_bytes
) {
    pqnas::cluster::NodeRegistryRecord node;
    node.node_id = node_id;
    node.enabled = true;
    node.health = "online";

    // RT-007: normal placement fixtures represent Director-approved capacity.
    node.capacity_ceiling_bytes = 1000;

    node.protocol_version =
        pqnas::cluster::
            kNodeStatusProtocolVersion;

    node.last_seen_at =
        "2026-07-28T14:00:00Z";

    node.node_reported_at =
        "2026-07-28T14:00:00Z";

    node.storage_total_bytes = 1000;
    node.storage_free_bytes = free_bytes;
    node.storage_used_bytes =
        node.storage_total_bytes -
        node.storage_free_bytes;

    node.storage_fstype = "ext4";

    return node;
}

pqnas::cluster::UserPlacementPolicy
make_business_policy() {
    pqnas::cluster::UserPlacementPolicy policy;
    policy.policy_id = "business-backup";

    policy.mode =
        pqnas::cluster::
            UserPlacementPolicyMode::Pinned;

    policy.fallback =
        pqnas::cluster::
            UserPlacementPolicyFallback::Deny;

    policy.node_ids = {
        "node-business-01"
    };

    return policy;
}

} // namespace

int main() {
    using UserSnapshot =
        pqnas::cluster::
            UserPlacementPreviewUserSnapshot;

    std::map<std::string, UserSnapshot> users{
        {
            "policy-user",
            UserSnapshot{
                "node-01",
                "business-backup"
            }
        },
        {
            "automatic-user",
            UserSnapshot{
                "node-01",
                ""
            }
        },
        {
            "missing-policy-user",
            UserSnapshot{
                "node-01",
                "missing-policy"
            }
        }
    };

    std::vector<
        pqnas::cluster::NodeRegistryRecord
    > nodes{
        make_node(
            "node-01",
            900
        ),
        make_node(
            "node-business-01",
            500
        )
    };

    std::map<
        std::string,
        pqnas::cluster::UserPlacementPolicy
    > policies{
        {
            "business-backup",
            make_business_policy()
        }
    };

    std::unordered_map<
        std::string,
        std::size_t
    > counts{
        {"node-01", 4},
        {"node-business-01", 2}
    };

    pqnas::cluster::
        UserPlacementPreviewServiceConfig
            config;

    config.get_user =
        [&](const std::string& fingerprint)
            -> std::optional<UserSnapshot> {
            const auto iterator =
                users.find(
                    fingerprint
                );

            if (iterator == users.end()) {
                return std::nullopt;
            }

            return iterator->second;
        };

    config.node_snapshot =
        [&]() {
            return nodes;
        };

    config.get_policy =
        [&](const std::string& policy_id)
            -> std::optional<
                pqnas::cluster::
                    UserPlacementPolicy
            > {
            const auto iterator =
                policies.find(
                    policy_id
                );

            if (iterator ==
                policies.end()) {
                return std::nullopt;
            }

            return iterator->second;
        };

    config.assigned_user_counts =
        [&]() {
            return counts;
        };

    config.placement_mutex =
        std::make_shared<std::mutex>();

    pqnas::cluster::
        UserPlacementPreviewService
            service(config);

    const auto policy_preview =
        service.preview(
            "policy-user"
        );

    expect(
        policy_preview.ok &&
        policy_preview.recommendation_available &&
        policy_preview.placement_policy_id ==
            "business-backup" &&
        policy_preview.policy_source ==
            "registry" &&
        policy_preview.policy_mode ==
            "pinned" &&
        policy_preview.selected_scope ==
            "policy" &&
        policy_preview.recommended_home_node ==
            "node-business-01" &&
        !policy_preview.current_home_node_matches,
        "explicit pinned policy should recommend its eligible target"
    );

    expect(
        policy_preview.considered_nodes == 1 &&
        policy_preview.eligible_nodes == 1 &&
        policy_preview.assigned_users == 2,
        "explicit policy preview should expose aggregate selector data"
    );

    const auto automatic_preview =
        service.preview(
            "automatic-user"
        );

    expect(
        automatic_preview.ok &&
        automatic_preview.recommendation_available &&
        automatic_preview.placement_policy_id.empty() &&
        automatic_preview.policy_source ==
            "implicit_automatic" &&
        automatic_preview.policy_mode ==
            "automatic" &&
        automatic_preview.selected_scope ==
            "automatic" &&
        automatic_preview.recommended_home_node ==
            "node-business-01" &&
        !automatic_preview.current_home_node_matches,
        "empty policy reference should use bounded implicit automatic selection"
    );

    nodes[1].health = "offline";

    const auto unavailable_preview =
        service.preview(
            "policy-user"
        );

    expect(
        unavailable_preview.ok &&
        !unavailable_preview.
            recommendation_available &&
        unavailable_preview.error_code ==
            "policy_target_unavailable" &&
        unavailable_preview.recommended_home_node.empty() &&
        !unavailable_preview.
            current_home_node_matches,
        "unavailable pinned target should return a successful diagnostic preview"
    );

    expect(
        unavailable_preview.rejected.count(
            "not_online"
        ) == 1 &&
        unavailable_preview.rejected.at(
            "not_online"
        ) == 1,
        "unavailable preview should preserve bounded selector rejection counts"
    );

    nodes[1].health = "online";

    const auto missing_policy =
        service.preview(
            "missing-policy-user"
        );

    expect(
        !missing_policy.ok &&
        missing_policy.error_code ==
            "policy_not_found",
        "missing explicit policy should fail closed"
    );

    const auto missing_user =
        service.preview(
            "missing-user"
        );

    expect(
        !missing_user.ok &&
        missing_user.error_code ==
            "user_not_found",
        "unknown user should be rejected"
    );

    const auto invalid_fingerprint =
        service.preview("");

    expect(
        !invalid_fingerprint.ok &&
        invalid_fingerprint.error_code ==
            "invalid_fingerprint",
        "empty fingerprint should be rejected"
    );

    expect(
        users.at("policy-user").home_node ==
            "node-01" &&
        users.at("policy-user").
            placement_policy_id ==
                "business-backup",
        "preview must not mutate user placement state"
    );

    if (failures != 0) {
        std::cerr
            << failures
            << " placement preview service test(s) failed\n";
        return 1;
    }

    std::cout
        << "OK: user placement preview service tests passed\n";

    return 0;
}
