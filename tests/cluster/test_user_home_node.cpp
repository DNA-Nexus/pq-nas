#include "users_registry.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

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

fs::path make_test_directory() {
    std::string pattern =
        (
            fs::temp_directory_path() /
            "pqnas-user-home-node-XXXXXX"
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

bool write_json(
    const fs::path& path,
    const json& value
) {
    std::ofstream output(
        path,
        std::ios::binary |
        std::ios::trunc
    );

    if (!output.good()) {
        return false;
    }

    output << value.dump(2) << '\n';
    return output.good();
}

json read_json(
    const fs::path& path
) {
    std::ifstream input(path);

    if (!input.good()) {
        return json();
    }

    json value =
        json::parse(
            input,
            nullptr,
            false
        );

    return value;
}

const json* find_user(
    const json& root,
    const std::string& fingerprint
) {
    if (!root.is_object() ||
        !root.contains("users") ||
        !root["users"].is_array()) {
        return nullptr;
    }

    for (const auto& user : root["users"]) {
        if (user.is_object() &&
            user.value("fingerprint", "") ==
                fingerprint) {
            return &user;
        }
    }

    return nullptr;
}

} // namespace

int main() {
    expect(
        pqnas::valid_user_home_node_id(""),
        "empty home_node should represent local placement"
    );

    expect(
        pqnas::valid_user_home_node_id("node-01") &&
        pqnas::valid_user_home_node_id("Node_02.eu"),
        "stable node IDs should be accepted"
    );

    expect(
        !pqnas::valid_user_home_node_id("-node") &&
        !pqnas::valid_user_home_node_id("../node") &&
        !pqnas::valid_user_home_node_id("node/01") &&
        !pqnas::valid_user_home_node_id(
            "https://node-01.internal"
        ) &&
        !pqnas::valid_user_home_node_id("node 01") &&
        !pqnas::valid_user_home_node_id(
            std::string(65, 'a')
        ),
        "paths, URLs, whitespace and oversized IDs must be rejected"
    );

    expect(
        pqnas::valid_user_placement_policy_id("") &&
        pqnas::valid_user_placement_policy_id(
            "business-backup"
        ) &&
        pqnas::valid_user_placement_policy_id(
            "Business_02.eu"
        ),
        "empty and stable placement policy IDs should be accepted"
    );

    expect(
        !pqnas::valid_user_placement_policy_id(
            "-business"
        ) &&
        !pqnas::valid_user_placement_policy_id(
            "../business"
        ) &&
        !pqnas::valid_user_placement_policy_id(
            "business/backup"
        ) &&
        !pqnas::valid_user_placement_policy_id(
            "https://policy.internal"
        ) &&
        !pqnas::valid_user_placement_policy_id(
            "business backup"
        ) &&
        !pqnas::valid_user_placement_policy_id(
            std::string(65, 'p')
        ),
        "unsafe placement policy IDs must be rejected"
    );

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

    const fs::path legacy_path =
        directory / "legacy_users.json";

    const json legacy_json{
        {
            "users",
            json::array({
                json{
                    {"fingerprint", "fp-legacy"},
                    {"role", "user"},
                    {"status", "disabled"}
                }
            })
        }
    };

    expect(
        write_json(legacy_path, legacy_json),
        "write legacy users registry"
    );

    pqnas::UsersRegistry registry;

    expect(
        registry.load(legacy_path.string()),
        "legacy users registry without home_node should load"
    );

    const auto legacy_user =
        registry.get("fp-legacy");

    expect(
        legacy_user.has_value() &&
        legacy_user->home_node.empty() &&
        legacy_user->placement_policy_id.empty(),
        "missing placement fields should preserve legacy local behavior"
    );

    expect(
        registry.set_home_node(
            "fp-legacy",
            "node-01"
        ),
        "valid home_node should update existing user"
    );

    expect(
        !registry.set_home_node(
            "fp-legacy",
            "../node"
        ),
        "invalid home_node setter must fail"
    );

    const auto after_invalid_set =
        registry.get("fp-legacy");

    expect(
        after_invalid_set.has_value() &&
        after_invalid_set->home_node ==
            "node-01",
        "failed setter must preserve previous placement"
    );

    pqnas::UserRec valid_upsert =
        *after_invalid_set;

    valid_upsert.fingerprint = "fp-valid";
    valid_upsert.home_node = "node-02";
    valid_upsert.placement_policy_id =
        "business-backup";

    expect(
        registry.upsert(valid_upsert),
        "generic upsert should accept valid placement"
    );

    pqnas::UserRec invalid_upsert =
        valid_upsert;

    invalid_upsert.fingerprint = "fp-invalid";
    invalid_upsert.home_node = "node/03";

    expect(
        !registry.upsert(invalid_upsert) &&
        !registry.exists("fp-invalid"),
        "generic upsert must reject invalid placement"
    );

    pqnas::UserRec invalid_policy_upsert =
        valid_upsert;

    invalid_policy_upsert.fingerprint =
        "fp-invalid-policy";

    invalid_policy_upsert.home_node =
        "node-03";

    invalid_policy_upsert.placement_policy_id =
        "../business";

    expect(
        !registry.upsert(
            invalid_policy_upsert
        ) &&
        !registry.exists(
            "fp-invalid-policy"
        ),
        "generic upsert must reject invalid policy references"
    );

    const fs::path saved_path =
        directory / "saved_users.json";

    expect(
        registry.save(saved_path.string()),
        "save users registry with placement metadata"
    );

    const json saved_json =
        read_json(saved_path);

    const json* saved_legacy =
        find_user(
            saved_json,
            "fp-legacy"
        );

    const json* saved_valid =
        find_user(
            saved_json,
            "fp-valid"
        );

    expect(
        saved_legacy &&
        saved_legacy->value(
            "home_node",
            ""
        ) == "node-01" &&
        saved_legacy->value(
            "placement_policy_id",
            "missing"
        ).empty(),
        "saved legacy user should contain local policy intent"
    );

    expect(
        saved_valid &&
        saved_valid->value(
            "home_node",
            ""
        ) == "node-02" &&
        saved_valid->value(
            "placement_policy_id",
            ""
        ) == "business-backup",
        "saved user should contain node and policy IDs"
    );

    pqnas::UsersRegistry reloaded;

    expect(
        reloaded.load(saved_path.string()),
        "saved placement registry should reload"
    );

    const auto reloaded_user =
        reloaded.get("fp-legacy");

    const auto reloaded_valid =
        reloaded.get("fp-valid");

    expect(
        reloaded_user.has_value() &&
        reloaded_user->home_node ==
            "node-01" &&
        reloaded_user->placement_policy_id.empty(),
        "legacy placement metadata should survive save and reload"
    );

    expect(
        reloaded_valid.has_value() &&
        reloaded_valid->home_node ==
            "node-02" &&
        reloaded_valid->placement_policy_id ==
            "business-backup",
        "policy reference should survive save and reload"
    );

    const auto initial_policy_counts =
        reloaded.placement_policy_assignment_counts();

    expect(
        initial_policy_counts.size() == 1 &&
        initial_policy_counts.count(
            "business-backup"
        ) == 1 &&
        initial_policy_counts.at(
            "business-backup"
        ) == 1 &&
        initial_policy_counts.count("") == 0,
        "policy counts should omit empty overrides and count explicit IDs"
    );


    bool cas_changed = false;
    bool cas_expectation_matched = false;
    std::string cas_error;

    expect(
        reloaded.set_home_node_persisted_if_current(
            "fp-legacy",
            "node-01",
            "",
            "node-03",
            saved_path.string(),
            &cas_changed,
            &cas_expectation_matched,
            &cas_error
        ) &&
        cas_expectation_matched &&
        cas_changed,
        "matching placement expectation should persist: " +
            cas_error
    );

    const auto cas_updated_user =
        reloaded.get("fp-legacy");

    expect(
        cas_updated_user.has_value() &&
        cas_updated_user->home_node ==
            "node-03" &&
        cas_updated_user->
            placement_policy_id.empty(),
        "successful CAS should update only home_node"
    );

    const json cas_saved_json =
        read_json(saved_path);

    const json* cas_saved_user =
        find_user(
            cas_saved_json,
            "fp-legacy"
        );

    expect(
        cas_saved_user &&
        cas_saved_user->value(
            "home_node",
            ""
        ) == "node-03" &&
        cas_saved_user->value(
            "placement_policy_id",
            "missing"
        ).empty(),
        "successful CAS should reach users.json"
    );

    cas_changed = true;
    cas_expectation_matched = true;
    cas_error.clear();

    expect(
        reloaded.set_home_node_persisted_if_current(
            "fp-legacy",
            "node-01",
            "",
            "node-04",
            saved_path.string(),
            &cas_changed,
            &cas_expectation_matched,
            &cas_error
        ) &&
        !cas_expectation_matched &&
        !cas_changed &&
        cas_error.empty(),
        "stale home_node expectation should be a clean no-op"
    );

    const auto after_stale_home =
        reloaded.get("fp-legacy");

    expect(
        after_stale_home.has_value() &&
        after_stale_home->home_node ==
            "node-03",
        "stale home_node expectation must preserve placement"
    );

    cas_changed = true;
    cas_expectation_matched = true;
    cas_error.clear();

    expect(
        reloaded.set_home_node_persisted_if_current(
            "fp-legacy",
            "node-03",
            "business-backup",
            "node-04",
            saved_path.string(),
            &cas_changed,
            &cas_expectation_matched,
            &cas_error
        ) &&
        !cas_expectation_matched &&
        !cas_changed &&
        cas_error.empty(),
        "stale policy expectation should be a clean no-op"
    );

    const auto after_stale_policy =
        reloaded.get("fp-legacy");

    expect(
        after_stale_policy.has_value() &&
        after_stale_policy->home_node ==
            "node-03" &&
        after_stale_policy->
            placement_policy_id.empty(),
        "stale policy expectation must preserve user state"
    );

    cas_changed = true;
    cas_expectation_matched = false;
    cas_error.clear();

    expect(
        reloaded.set_home_node_persisted_if_current(
            "fp-legacy",
            "node-03",
            "",
            "node-03",
            saved_path.string(),
            &cas_changed,
            &cas_expectation_matched,
            &cas_error
        ) &&
        cas_expectation_matched &&
        !cas_changed,
        "matching already-applied CAS should be idempotent"
    );

    cas_changed = true;
    cas_expectation_matched = true;
    cas_error.clear();

    expect(
        !reloaded.set_home_node_persisted_if_current(
            "fp-legacy",
            "../node",
            "",
            "node-04",
            saved_path.string(),
            &cas_changed,
            &cas_expectation_matched,
            &cas_error
        ) &&
        !cas_expectation_matched &&
        !cas_changed &&
        !cas_error.empty(),
        "invalid CAS expectation should fail validation"
    );

    const fs::path cas_rollback_directory =
        directory / "cas-rollback";

    const fs::path cas_rollback_path =
        cas_rollback_directory /
        "users.json";

    expect(
        reloaded.save(
            cas_rollback_path.string()
        ),
        "create CAS rollback fixture"
    );

    std::error_code cas_rollback_error;

    expect(
        fs::remove(
            cas_rollback_path,
            cas_rollback_error
        ) &&
        !cas_rollback_error,
        "remove CAS rollback registry file"
    );

    cas_rollback_error.clear();

    expect(
        fs::remove(
            cas_rollback_directory,
            cas_rollback_error
        ) &&
        !cas_rollback_error,
        "remove CAS rollback registry directory"
    );

    expect(
        write_json(
            cas_rollback_directory,
            json{
                {"blocker", true}
            }
        ),
        "replace CAS rollback directory with a file"
    );

    cas_changed = true;
    cas_expectation_matched = false;
    cas_error.clear();

    expect(
        !reloaded.set_home_node_persisted_if_current(
            "fp-legacy",
            "node-03",
            "",
            "node-04",
            cas_rollback_path.string(),
            &cas_changed,
            &cas_expectation_matched,
            &cas_error
        ) &&
        cas_expectation_matched &&
        !cas_changed &&
        !cas_error.empty(),
        "CAS persistence failure should be reported"
    );

    const auto after_failed_cas_write =
        reloaded.get("fp-legacy");

    expect(
        after_failed_cas_write.has_value() &&
        after_failed_cas_write->home_node ==
            "node-03" &&
        after_failed_cas_write->
            placement_policy_id.empty(),
        "failed CAS persistence must restore prior state"
    );

    bool policy_changed = false;
    std::string policy_error;

    expect(
        reloaded.set_placement_policy_id_persisted(
            "fp-legacy",
            "business-backup",
            saved_path.string(),
            &policy_changed,
            &policy_error
        ) &&
        policy_changed,
        "persist policy assignment: " +
            policy_error
    );

    policy_changed = true;
    policy_error.clear();

    expect(
        reloaded.set_placement_policy_id_persisted(
            "fp-legacy",
            "business-backup",
            saved_path.string(),
            &policy_changed,
            &policy_error
        ) &&
        !policy_changed,
        "unchanged policy assignment should be idempotent"
    );

    const json policy_saved_json =
        read_json(saved_path);

    const json* policy_saved_user =
        find_user(
            policy_saved_json,
            "fp-legacy"
        );

    expect(
        policy_saved_user &&
        policy_saved_user->value(
            "placement_policy_id",
            ""
        ) == "business-backup",
        "persisted policy assignment should reach users.json"
    );

    const auto assigned_policy_counts =
        reloaded.placement_policy_assignment_counts();

    expect(
        assigned_policy_counts.size() == 1 &&
        assigned_policy_counts.at(
            "business-backup"
        ) == 2,
        "policy count should increase after assigning another user"
    );

    policy_changed = false;
    policy_error.clear();

    expect(
        reloaded.set_placement_policy_id_persisted(
            "fp-legacy",
            "",
            saved_path.string(),
            &policy_changed,
            &policy_error
        ) &&
        policy_changed,
        "empty policy ID should clear the override"
    );

    const auto cleared_policy_user =
        reloaded.get("fp-legacy");

    expect(
        cleared_policy_user.has_value() &&
        cleared_policy_user->
            placement_policy_id.empty(),
        "cleared policy override should update memory"
    );

    const auto cleared_policy_counts =
        reloaded.placement_policy_assignment_counts();

    expect(
        cleared_policy_counts.size() == 1 &&
        cleared_policy_counts.at(
            "business-backup"
        ) == 1 &&
        cleared_policy_counts.count("") == 0,
        "policy count should decrease when an override is cleared"
    );

    const fs::path rollback_directory =
        directory / "policy-rollback";

    const fs::path rollback_path =
        rollback_directory / "users.json";

    expect(
        reloaded.save(
            rollback_path.string()
        ),
        "create policy rollback fixture"
    );

    policy_changed = false;
    policy_error.clear();

    expect(
        reloaded.set_placement_policy_id_persisted(
            "fp-legacy",
            "before-failure",
            rollback_path.string(),
            &policy_changed,
            &policy_error
        ) &&
        policy_changed,
        "establish rollback baseline"
    );

    std::error_code rollback_error;

    expect(
        fs::remove(
            rollback_path,
            rollback_error
        ) &&
        !rollback_error,
        "remove rollback registry file"
    );

    rollback_error.clear();

    expect(
        fs::remove(
            rollback_directory,
            rollback_error
        ) &&
        !rollback_error,
        "remove rollback registry directory"
    );

    expect(
        write_json(
            rollback_directory,
            json{
                {"blocker", true}
            }
        ),
        "replace rollback directory with a regular file"
    );

    policy_changed = false;
    policy_error.clear();

    expect(
        !reloaded.set_placement_policy_id_persisted(
            "fp-legacy",
            "after-failure",
            rollback_path.string(),
            &policy_changed,
            &policy_error
        ) &&
        !policy_changed,
        "failed persistence must reject policy mutation"
    );

    const auto after_failed_policy_write =
        reloaded.get("fp-legacy");

    expect(
        after_failed_policy_write.has_value() &&
        after_failed_policy_write->
            placement_policy_id ==
                "before-failure",
        "failed persistence must restore previous policy reference"
    );

    const fs::path invalid_string_path =
        directory / "invalid_string.json";

    const json invalid_string_json{
        {
            "users",
            json::array({
                json{
                    {"fingerprint", "fp-bad-string"},
                    {"home_node", "../node"}
                }
            })
        }
    };

    expect(
        write_json(
            invalid_string_path,
            invalid_string_json
        ),
        "write invalid home_node string fixture"
    );

    pqnas::UsersRegistry invalid_string_registry;

    expect(
        !invalid_string_registry.load(
            invalid_string_path.string()
        ),
        "invalid persisted home_node string must fail loading"
    );

    const fs::path invalid_type_path =
        directory / "invalid_type.json";

    const json invalid_type_json{
        {
            "users",
            json::array({
                json{
                    {"fingerprint", "fp-bad-type"},
                    {"home_node", 123}
                }
            })
        }
    };

    expect(
        write_json(
            invalid_type_path,
            invalid_type_json
        ),
        "write invalid home_node type fixture"
    );

    pqnas::UsersRegistry invalid_type_registry;

    expect(
        !invalid_type_registry.load(
            invalid_type_path.string()
        ),
        "non-string persisted home_node must fail loading"
    );

    const fs::path invalid_policy_string_path =
        directory / "invalid_policy_string.json";

    const json invalid_policy_string_json{
        {
            "users",
            json::array({
                json{
                    {"fingerprint", "fp-bad-policy"},
                    {
                        "placement_policy_id",
                        "../business"
                    }
                }
            })
        }
    };

    expect(
        write_json(
            invalid_policy_string_path,
            invalid_policy_string_json
        ),
        "write invalid policy ID fixture"
    );

    pqnas::UsersRegistry
        invalid_policy_string_registry;

    expect(
        !invalid_policy_string_registry.load(
            invalid_policy_string_path.string()
        ),
        "unsafe persisted policy ID must fail loading"
    );

    const fs::path invalid_policy_type_path =
        directory / "invalid_policy_type.json";

    const json invalid_policy_type_json{
        {
            "users",
            json::array({
                json{
                    {"fingerprint", "fp-bad-policy-type"},
                    {"placement_policy_id", 123}
                }
            })
        }
    };

    expect(
        write_json(
            invalid_policy_type_path,
            invalid_policy_type_json
        ),
        "write invalid policy type fixture"
    );

    pqnas::UsersRegistry
        invalid_policy_type_registry;

    expect(
        !invalid_policy_type_registry.load(
            invalid_policy_type_path.string()
        ),
        "non-string persisted policy ID must fail loading"
    );

    cleanup();

    if (failures != 0) {
        std::cerr
            << failures
            << " user home_node test(s) failed\n";
        return 1;
    }

    std::cout
        << "OK: user home_node model tests passed\n";

    return 0;
}
