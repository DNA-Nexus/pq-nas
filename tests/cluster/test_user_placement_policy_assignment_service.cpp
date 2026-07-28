#include "cluster/user_placement_policy_assignment_service.h"

#include "cluster/user_placement_policy_registry.h"
#include "users_registry.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
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
            "pqnas-user-policy-assignment-XXXXXX"
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

    if (!result) {
        return {};
    }

    return fs::path(result);
}

pqnas::UserRec make_user(
    const std::string& fingerprint
) {
    pqnas::UserRec user;
    user.fingerprint = fingerprint;
    user.role = "user";
    user.status = "enabled";
    user.storage_state = "unallocated";
    user.app_prefs_json = "{}";
    user.home_node = "node-01";

    return user;
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

    const fs::path policies_path =
        directory / "placement_policies.json";

    pqnas::UsersRegistry users;

    expect(
        users.upsert(
            make_user("abc123")
        ),
        "create policy assignment user"
    );

    expect(
        users.save(
            users_path.string()
        ),
        "persist policy assignment user"
    );

    pqnas::cluster::
        UserPlacementPolicyRegistry policies;

    std::string error;

    expect(
        policies.load(
            policies_path.string(),
            &error
        ),
        "load policy registry: " + error
    );

    error.clear();

    expect(
        policies.upsert(
            make_business_policy(),
            &error
        ),
        "create business policy: " + error
    );

    std::vector<AuditRecord> audits;

    pqnas::cluster::
        UserPlacementPolicyAssignmentServiceConfig
            config;

    config.users = &users;
    config.policies = &policies;
    config.placement_mutex =
        std::make_shared<std::mutex>();
    config.users_path =
        users_path.string();

    config.audit_emit =
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
        UserPlacementPolicyAssignmentService
            service(config);

    const auto assigned =
        service.assign(
            "abc123",
            "business-backup",
            "admin-fp"
        );

    expect(
        assigned.ok &&
        assigned.changed &&
        assigned.previous_policy_id.empty() &&
        assigned.placement_policy_id ==
            "business-backup",
        "known policy should be assigned"
    );

    const auto assigned_user =
        users.get("abc123");

    expect(
        assigned_user.has_value() &&
        assigned_user->placement_policy_id ==
            "business-backup" &&
        assigned_user->home_node ==
            "node-01",
        "policy assignment must not change home_node"
    );

    pqnas::UsersRegistry reloaded;

    expect(
        reloaded.load(
            users_path.string()
        ),
        "reload user after policy assignment"
    );

    const auto reloaded_user =
        reloaded.get("abc123");

    expect(
        reloaded_user.has_value() &&
        reloaded_user->placement_policy_id ==
            "business-backup" &&
        reloaded_user->home_node ==
            "node-01",
        "policy assignment should persist independently of home_node"
    );

    const auto unchanged =
        service.assign(
            "abc123",
            "business-backup",
            "admin-fp"
        );

    expect(
        unchanged.ok &&
        !unchanged.changed &&
        unchanged.previous_policy_id ==
            "business-backup",
        "same policy assignment should be idempotent"
    );

    const auto cleared =
        service.assign(
            "abc123",
            "",
            "admin-fp"
        );

    expect(
        cleared.ok &&
        cleared.changed &&
        cleared.previous_policy_id ==
            "business-backup" &&
        cleared.placement_policy_id.empty(),
        "empty policy ID should clear override"
    );

    const auto after_clear =
        users.get("abc123");

    expect(
        after_clear.has_value() &&
        after_clear->placement_policy_id.empty() &&
        after_clear->home_node ==
            "node-01",
        "policy clear must preserve home_node"
    );

    const auto missing_policy =
        service.assign(
            "abc123",
            "missing-policy",
            "admin-fp"
        );

    expect(
        !missing_policy.ok &&
        missing_policy.error_code ==
            "policy_not_found",
        "unknown non-empty policy must be rejected"
    );

    const auto invalid_policy =
        service.assign(
            "abc123",
            "../business",
            "admin-fp"
        );

    expect(
        !invalid_policy.ok &&
        invalid_policy.error_code ==
            "invalid_policy_id",
        "unsafe policy ID must be rejected"
    );

    const auto missing_user =
        service.assign(
            "missing-user",
            "business-backup",
            "admin-fp"
        );

    expect(
        !missing_user.ok &&
        missing_user.error_code ==
            "user_not_found",
        "unknown user must be rejected"
    );

    const auto before_failed_write =
        users.get("abc123");

    expect(
        before_failed_write.has_value() &&
        before_failed_write->
            placement_policy_id.empty(),
        "persistence failure precondition"
    );

    std::error_code filesystem_error;

    fs::remove_all(
        directory,
        filesystem_error
    );

    expect(
        !filesystem_error,
        "remove policy assignment directory"
    );

    {
        std::ofstream blocker(
            directory,
            std::ios::binary |
            std::ios::trunc
        );

        expect(
            blocker.good(),
            "replace users directory with regular file"
        );

        blocker << "not a directory\n";
    }

    const auto failed_write =
        service.assign(
            "abc123",
            "business-backup",
            "admin-fp"
        );

    expect(
        !failed_write.ok &&
        failed_write.error_code ==
            "persistence_failed",
        "persistence failure must be reported"
    );

    const auto after_failed_write =
        users.get("abc123");

    expect(
        after_failed_write.has_value() &&
        after_failed_write->
            placement_policy_id.empty() &&
        after_failed_write->home_node ==
            "node-01",
        "failed persistence must restore policy and preserve home_node"
    );

    expect(
        audits.size() == 7,
        "every configured service operation should emit one audit result"
    );

    for (const AuditRecord& audit : audits) {
        expect(
            audit.event ==
                "admin.user_placement_policy.assign",
            "policy assignment audit event name"
        );

        expect(
            audit.fields.count(
                "reason_code"
            ) == 1 &&
            audit.fields.count(
                "changed"
            ) == 1,
            "policy audit should use bounded result fields"
        );

        expect(
            audit.fields.count(
                "node_ids"
            ) == 0 &&
            audit.fields.count(
                "users_path"
            ) == 0 &&
            audit.fields.count(
                "api_base_url"
            ) == 0 &&
            audit.fields.count(
                "authorization"
            ) == 0 &&
            audit.fields.count(
                "error"
            ) == 0,
            "audit must exclude topology, paths, credentials and raw errors"
        );
    }

    cleanup();

    if (failures != 0) {
        std::cerr
            << failures
            << " user policy assignment service test(s) failed\n";
        return 1;
    }

    std::cout
        << "OK: user placement policy assignment service tests passed\n";

    return 0;
}
