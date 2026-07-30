#include "cluster/user_placement_recommendation_apply_service.h"

#include "cluster/node_status.h"
#include "cluster/user_placement_preview_service.h"
#include "users_registry.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unistd.h>
#include <vector>

namespace {

namespace fs = std::filesystem;

int failures = 0;

struct AuditRecord {
    std::string event;
    std::string outcome;
    std::map<std::string, std::string> fields;
};

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

fs::path make_test_directory() {
    std::string pattern =
        (
            fs::temp_directory_path() /
            "pqnas-placement-apply-XXXXXX"
        ).string();

    std::vector<char> writable(
        pattern.begin(),
        pattern.end()
    );

    writable.push_back('\0');

    char* result =
        ::mkdtemp(
            writable.data()
        );

    return result
        ? fs::path(result)
        : fs::path{};
}

pqnas::UserRec make_user() {
    pqnas::UserRec user;
    user.fingerprint = "user-fingerprint";
    user.role = "user";
    user.status = "enabled";
    user.storage_state = "unallocated";
    user.app_prefs_json = "{}";
    user.home_node = "node-01";

    user.placement_policy_id =
        "business-backup";

    return user;
}

pqnas::cluster::NodeRegistryRecord make_node(
    const std::string& node_id,
    std::uint64_t free_bytes
) {
    pqnas::cluster::NodeRegistryRecord node;
    node.node_id = node_id;
    node.enabled = true;
    node.health = "online";

    node.protocol_version =
        pqnas::cluster::
            kNodeStatusProtocolVersion;

    node.last_seen_at =
        "2026-07-29T06:00:00Z";

    node.node_reported_at =
        "2026-07-29T06:00:00Z";

    node.storage_total_bytes = 1000;
    node.storage_free_bytes = free_bytes;

    node.storage_used_bytes =
        node.storage_total_bytes -
        node.storage_free_bytes;

    node.storage_fstype = "ext4";

    return node;
}

pqnas::cluster::UserPlacementPolicy
make_pinned_policy(
    const std::string& node_id
) {
    pqnas::cluster::UserPlacementPolicy policy;

    policy.policy_id =
        "business-backup";

    policy.mode =
        pqnas::cluster::
            UserPlacementPolicyMode::Pinned;

    policy.fallback =
        pqnas::cluster::
            UserPlacementPolicyFallback::Deny;

    policy.node_ids = {
        node_id
    };

    return policy;
}

} // namespace

int main() {
    const fs::path directory =
        make_test_directory();

    if (directory.empty()) {
        std::cerr
            << "FAIL: test directory creation failed\n";
        return 1;
    }

    const auto cleanup = [&]() {
        std::error_code error;

        fs::remove_all(
            directory,
            error
        );
    };

    const fs::path users_path =
        directory / "users.json";

    pqnas::UsersRegistry users;

    expect(
        users.upsert(
            make_user()
        ) &&
        users.save(
            users_path.string()
        ),
        "create apply test user"
    );

    std::vector<
        pqnas::cluster::NodeRegistryRecord
    > nodes{
        make_node(
            "node-business-01",
            600
        ),
        make_node(
            "node-business-02",
            500
        )
    };

    std::map<
        std::string,
        pqnas::cluster::UserPlacementPolicy
    > policies{
        {
            "business-backup",
            make_pinned_policy(
                "node-business-01"
            )
        }
    };

    std::unordered_map<
        std::string,
        std::size_t
    > assigned_user_counts{
        {"node-business-01", 2},
        {"node-business-02", 1}
    };

    bool mutate_policy_during_counts =
        false;

    pqnas::cluster::
        UserPlacementPreviewServiceConfig
            preview_config;

    preview_config.get_user =
        [&](const std::string& fingerprint)
            -> std::optional<
                pqnas::cluster::
                    UserPlacementPreviewUserSnapshot
            > {
            const auto user =
                users.get(
                    fingerprint
                );

            if (!user.has_value()) {
                return std::nullopt;
            }

            return
                pqnas::cluster::
                    UserPlacementPreviewUserSnapshot{
                        user->home_node,
                        user->placement_policy_id
                    };
        };

    preview_config.node_snapshot =
        [&]() {
            return nodes;
        };

    preview_config.get_policy =
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

    preview_config.assigned_user_counts =
        [&]() {
            if (mutate_policy_during_counts) {
                mutate_policy_during_counts =
                    false;

                bool changed = false;
                std::string error;

                expect(
                    users.set_placement_policy_id_persisted(
                        "user-fingerprint",
                        "other-policy",
                        users_path.string(),
                        &changed,
                        &error
                    ) &&
                    changed,
                    "inject direct policy mutation during apply: " +
                        error
                );
            }

            return assigned_user_counts;
        };

    preview_config.placement_mutex =
        std::make_shared<std::mutex>();

    auto preview_service =
        std::make_shared<
            pqnas::cluster::
                UserPlacementPreviewService
        >(
            preview_config
        );

    std::vector<AuditRecord> audits;

    pqnas::cluster::
        UserPlacementRecommendationApplyServiceConfig
            apply_config;

    apply_config.users = &users;
    apply_config.preview_service =
        preview_service;

    apply_config.users_path =
        users_path.string();

    apply_config.audit_emit =
        [&](const std::string& event,
            const std::string& outcome,
            const std::map<
                std::string,
                std::string
            >& fields) {
            audits.push_back(
                AuditRecord{
                    event,
                    outcome,
                    fields
                }
            );
        };

    pqnas::cluster::
        UserPlacementRecommendationApplyService
            service(
                apply_config
            );

    const auto applied =
        service.apply(
            "user-fingerprint",
            "node-01",
            "business-backup",
            "node-business-01",
            "admin-fingerprint"
        );

    expect(
        applied.ok &&
        applied.changed &&
        applied.previous_home_node ==
            "node-01" &&
        applied.home_node ==
            "node-business-01" &&
        applied.placement_policy_id ==
            "business-backup",
        "matching preview should apply recommendation"
    );

    pqnas::UsersRegistry reloaded;

    expect(
        reloaded.load(
            users_path.string()
        ),
        "reload applied placement"
    );

    const auto reloaded_user =
        reloaded.get(
            "user-fingerprint"
        );

    expect(
        reloaded_user.has_value() &&
        reloaded_user->home_node ==
            "node-business-01" &&
        reloaded_user->
            placement_policy_id ==
                "business-backup",
        "applied recommendation should persist"
    );

    const auto stale_current =
        service.apply(
            "user-fingerprint",
            "node-01",
            "business-backup",
            "node-business-01",
            "admin-fingerprint"
        );

    expect(
        !stale_current.ok &&
        stale_current.error_code ==
            "placement_preview_stale",
        "changed current placement should stale the preview"
    );

    const auto idempotent =
        service.apply(
            "user-fingerprint",
            "node-business-01",
            "business-backup",
            "node-business-01",
            "admin-fingerprint"
        );

    expect(
        idempotent.ok &&
        !idempotent.changed &&
        idempotent.home_node ==
            "node-business-01",
        "already applied recommendation should be idempotent"
    );

    policies["business-backup"] =
        make_pinned_policy(
            "node-business-02"
        );

    const auto changed_recommendation =
        service.apply(
            "user-fingerprint",
            "node-business-01",
            "business-backup",
            "node-business-01",
            "admin-fingerprint"
        );

    expect(
        !changed_recommendation.ok &&
        changed_recommendation.error_code ==
            "placement_preview_stale",
        "changed policy recommendation should stale preview"
    );

    policies["business-backup"] =
        make_pinned_policy(
            "node-business-01"
        );

    nodes[0].health = "offline";

    const auto unavailable_recommendation =
        service.apply(
            "user-fingerprint",
            "node-business-01",
            "business-backup",
            "node-business-01",
            "admin-fingerprint"
        );

    expect(
        !unavailable_recommendation.ok &&
        unavailable_recommendation.error_code ==
            "placement_preview_stale",
        "unavailable target should stale preview"
    );

    nodes[0].health = "online";

    mutate_policy_during_counts =
        true;

    const auto cas_race =
        service.apply(
            "user-fingerprint",
            "node-business-01",
            "business-backup",
            "node-business-01",
            "admin-fingerprint"
        );

    expect(
        !cas_race.ok &&
        cas_race.error_code ==
            "placement_preview_stale",
        "CAS should reject a mutation bypassing coordination lock"
    );

    const auto after_cas_race =
        users.get(
            "user-fingerprint"
        );

    expect(
        after_cas_race.has_value() &&
        after_cas_race->home_node ==
            "node-business-01" &&
        after_cas_race->
            placement_policy_id ==
                "other-policy",
        "CAS race must not change home_node"
    );

    bool policy_changed = false;
    std::string policy_error;

    expect(
        users.set_placement_policy_id_persisted(
            "user-fingerprint",
            "business-backup",
            users_path.string(),
            &policy_changed,
            &policy_error
        ) &&
        policy_changed,
        "restore policy before persistence test: " +
            policy_error
    );

    bool home_changed = false;
    std::string home_error;

    expect(
        users.set_home_node_persisted(
            "user-fingerprint",
            "node-01",
            users_path.string(),
            &home_changed,
            &home_error
        ) &&
        home_changed,
        "restore home node before persistence test: " +
            home_error
    );

    std::error_code filesystem_error;

    fs::remove_all(
        directory,
        filesystem_error
    );

    expect(
        !filesystem_error,
        "remove apply test directory"
    );

    {
        std::ofstream blocker(
            directory,
            std::ios::binary |
            std::ios::trunc
        );

        expect(
            blocker.good(),
            "replace apply directory with file"
        );

        blocker << "not a directory\n";
    }

    const auto persistence_failure =
        service.apply(
            "user-fingerprint",
            "node-01",
            "business-backup",
            "node-business-01",
            "admin-fingerprint"
        );

    expect(
        !persistence_failure.ok &&
        persistence_failure.error_code ==
            "persistence_failed",
        "apply persistence failure should be reported"
    );

    const auto after_persistence_failure =
        users.get(
            "user-fingerprint"
        );

    expect(
        after_persistence_failure.has_value() &&
        after_persistence_failure->home_node ==
            "node-01",
        "failed apply persistence must roll back home_node"
    );

    expect(
        !audits.empty(),
        "apply attempts should be audited"
    );

    for (const AuditRecord& audit : audits) {
        expect(
            audit.event ==
                "admin.user_placement_recommendation.apply",
            "apply audit event name"
        );

        expect(
            audit.fields.count(
                "reason_code"
            ) == 1 &&
            audit.fields.count(
                "changed"
            ) == 1,
            "apply audit should use bounded result fields"
        );

        expect(
            audit.fields.count(
                "api_base_url"
            ) == 0 &&
            audit.fields.count(
                "users_path"
            ) == 0 &&
            audit.fields.count(
                "authorization"
            ) == 0 &&
            audit.fields.count(
                "request_body"
            ) == 0 &&
            audit.fields.count(
                "error"
            ) == 0,
            "apply audit must exclude sensitive internals"
        );
    }

    cleanup();

    if (failures != 0) {
        std::cerr
            << failures
            << " placement apply service test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: user placement recommendation apply service tests passed\n";

    return 0;
}
