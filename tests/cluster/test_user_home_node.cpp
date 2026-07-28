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
        legacy_user->home_node.empty(),
        "missing home_node should migrate to local placement"
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
        ) == "node-01",
        "saved legacy user should contain node-01"
    );

    expect(
        saved_valid &&
        saved_valid->value(
            "home_node",
            ""
        ) == "node-02",
        "saved upserted user should contain node-02"
    );

    pqnas::UsersRegistry reloaded;

    expect(
        reloaded.load(saved_path.string()),
        "saved placement registry should reload"
    );

    const auto reloaded_user =
        reloaded.get("fp-legacy");

    expect(
        reloaded_user.has_value() &&
        reloaded_user->home_node ==
            "node-01",
        "home_node should survive save and reload"
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
