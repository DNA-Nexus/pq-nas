#include "cluster/node_registry.h"
#include "cluster/user_placement_service.h"
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
            "pqnas-user-placement-XXXXXX"
        ).string();

    std::vector<char> writable(
        pattern.begin(),
        pattern.end()
    );

    writable.push_back('\0');

    char* result =
        ::mkdtemp(writable.data());

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
    user.home_node = "";

    return user;
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
        fs::remove_all(directory, error);
    };

    const fs::path users_path =
        directory / "users.json";

    pqnas::UsersRegistry users;

    expect(
        users.upsert(
            make_user("abc123")
        ),
        "create placement test user"
    );

    expect(
        users.save(users_path.string()),
        "persist placement test user"
    );

    pqnas::cluster::NodeRegistry nodes;
    std::string error;

    expect(
        nodes.load(
            (
                directory /
                "node_registry.json"
            ).string(),
            &error
        ),
        "load node registry: " + error
    );

    const auto add_node =
        [&](
            const std::string& node_id,
            bool enabled
        ) {
            error.clear();

            expect(
                nodes.upsert_configured_node(
                    node_id,
                    "https://" +
                        node_id +
                        ".internal",
                    enabled,
                    "2026-07-28T12:00:00Z",
                    &error
                ),
                "add " + node_id + ": " + error
            );
        };

    add_node("node-01", true);
    add_node("node-02", false);
    add_node("node-03", true);
    add_node("node-04", true);

    bool transitioned_offline = false;

    error.clear();

    expect(
        nodes.mark_offline(
            "node-03",
            &error,
            &transitioned_offline
        ) &&
        transitioned_offline,
        "mark enabled node-03 offline: " +
            error
    );

    std::vector<AuditRecord> audits;

    pqnas::cluster::UserPlacementServiceConfig
        config;

    config.users = &users;
    config.nodes = &nodes;
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

    pqnas::cluster::UserPlacementService
        service(config);

    const auto first =
        service.assign(
            "abc123",
            "node-01",
            "admin-fp"
        );

    expect(
        first.ok &&
        first.changed &&
        first.previous_home_node.empty() &&
        first.home_node == "node-01",
        "first placement should persist node-01"
    );

    pqnas::UsersRegistry reloaded;

    expect(
        reloaded.load(users_path.string()),
        "reload users after first placement"
    );

    const auto reloaded_user =
        reloaded.get("abc123");

    expect(
        reloaded_user.has_value() &&
        reloaded_user->home_node ==
            "node-01",
        "placement should survive reload"
    );

    const auto unchanged =
        service.assign(
            "abc123",
            "node-01",
            "admin-fp"
        );

    expect(
        unchanged.ok &&
        !unchanged.changed &&
        unchanged.previous_home_node ==
            "node-01",
        "same placement should be idempotent"
    );

    const auto offline_node =
        service.assign(
            "abc123",
            "node-03",
            "admin-fp"
        );

    expect(
        offline_node.ok &&
        offline_node.changed &&
        offline_node.home_node ==
            "node-03",
        "enabled but offline node should remain a valid target"
    );

    const auto assignment_counts =
        users.home_node_assignment_counts();

    expect(
        assignment_counts.size() == 1 &&
        assignment_counts.at("node-03") == 1,
        "assignment counts should expose only aggregate node references"
    );

    const auto disabled_node =
        service.assign(
            "abc123",
            "node-02",
            "admin-fp"
        );

    expect(
        !disabled_node.ok &&
        disabled_node.error_code ==
            "node_disabled",
        "disabled node must be rejected"
    );

    const auto unknown_node =
        service.assign(
            "abc123",
            "node-99",
            "admin-fp"
        );

    expect(
        !unknown_node.ok &&
        unknown_node.error_code ==
            "node_not_found",
        "unknown node must be rejected"
    );

    const auto invalid_node =
        service.assign(
            "abc123",
            "../node",
            "admin-fp"
        );

    expect(
        !invalid_node.ok &&
        invalid_node.error_code ==
            "invalid_node_id",
        "invalid node ID must be rejected"
    );

    const auto unknown_user =
        service.assign(
            "missing-user",
            "node-01",
            "admin-fp"
        );

    expect(
        !unknown_user.ok &&
        unknown_user.error_code ==
            "user_not_found",
        "unknown user must be rejected"
    );

    const auto before_failed_save =
        users.get("abc123");

    expect(
        before_failed_save.has_value() &&
        before_failed_save->home_node ==
            "node-03",
        "precondition before persistence failure"
    );

    std::error_code filesystem_error;

    fs::remove_all(
        directory,
        filesystem_error
    );

    expect(
        !filesystem_error,
        "remove placement test directory"
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

    const auto failed_save =
        service.assign(
            "abc123",
            "node-04",
            "admin-fp"
        );

    expect(
        !failed_save.ok &&
        failed_save.error_code ==
            "persistence_failed",
        "persistence failure must be reported"
    );

    const auto after_failed_save =
        users.get("abc123");

    expect(
        after_failed_save.has_value() &&
        after_failed_save->home_node ==
            "node-03",
        "failed persistence must roll back in-memory placement"
    );

    expect(
        audits.size() == 8,
        "every placement operation should emit one audit result"
    );

    for (const AuditRecord& audit : audits) {
        expect(
            audit.event ==
                "admin.user_home_node.assign",
            "placement audit event name"
        );

        expect(
            audit.fields.count(
                "reason_code"
            ) == 1 &&
            audit.fields.count(
                "changed"
            ) == 1,
            "placement audit should use bounded result fields"
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
                "error"
            ) == 0,
            "audit must exclude URLs, paths, credentials and raw errors"
        );
    }

    cleanup();

    if (failures != 0) {
        std::cerr
            << failures
            << " user placement service test(s) failed\n";
        return 1;
    }

    std::cout
        << "OK: user placement service tests passed\n";

    return 0;
}
