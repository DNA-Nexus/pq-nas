#include "routes/routes_admin_users_overview.h"

#include "httplib.h"
#include "users_registry.h"

#include <nlohmann/json.hpp>

#include <filesystem>
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
            "pqnas-admin-users-overview-XXXXXX"
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
    const std::string& fingerprint,
    const std::string& home_node,
    const std::string& placement_policy_id
) {
    pqnas::UserRec user;
    user.fingerprint = fingerprint;
    user.name = fingerprint;
    user.role = "user";
    user.status = "enabled";
    user.storage_state = "unallocated";
    user.app_prefs_json = "{}";
    user.home_node = home_node;
    user.placement_policy_id =
        placement_policy_id;

    return user;
}

json response_json(
    const httplib::Response& response
) {
    return json::parse(
        response.body,
        nullptr,
        false
    );
}

const json* find_user(
    const json& users,
    const std::string& fingerprint
) {
    if (!users.is_array()) {
        return nullptr;
    }

    for (const json& user : users) {
        if (user.is_object() &&
            user.value(
                "fingerprint",
                ""
            ) == fingerprint) {
            return &user;
        }
    }

    return nullptr;
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
            make_user(
                "local-user",
                "",
                ""
            )
        ),
        "create standalone user"
    );

    expect(
        users.upsert(
            make_user(
                "node-user",
                "node-01",
                "business-backup"
            )
        ),
        "create node-assigned user"
    );

    expect(
        users.save(
            users_path.string()
        ),
        "save users overview fixture"
    );

    bool auth_allowed = true;
    int auth_calls = 0;

    AdminUsersOverviewRoutesContext context;
    context.users = &users;
    context.users_path =
        users_path.string();

    context.require_admin_auth =
        [&](
            const httplib::Request&,
            httplib::Response& response,
            std::string* actor_fp
        ) {
            ++auth_calls;

            if (!auth_allowed) {
                response.status = 401;
                response.set_content(
                    R"({"ok":false,"error":"unauthorized"})",
                    "application/json"
                );
                return false;
            }

            if (actor_fp) {
                *actor_fp =
                    "admin-fingerprint";
            }

            return true;
        };

    context.reply_json =
        [](
            httplib::Response& response,
            int status,
            const std::string& body
        ) {
            response.status = status;
            response.set_content(
                body,
                "application/json; charset=utf-8"
            );
        };

    context.user_dir_for_fp =
        [&](const std::string& fingerprint) {
            return
                directory /
                fingerprint;
        };

    context.dir_size_bytes_best_effort =
        [](const fs::path&) {
            return std::uint64_t{0};
        };

    {
        auth_allowed = false;
        auth_calls = 0;

        httplib::Request request;
        httplib::Response response;

        handle_admin_users_overview(
            request,
            response,
            context
        );

        expect(
            response.status == 401,
            "unauthorized request should return 401"
        );

        expect(
            auth_calls == 1,
            "users overview should authenticate first"
        );
    }

    {
        auth_allowed = true;
        auth_calls = 0;

        httplib::Request request;
        httplib::Response response;

        handle_admin_users_overview(
            request,
            response,
            context
        );

        const json body =
            response_json(response);

        expect(
            response.status == 200 &&
            body.is_object() &&
            body["users"].is_array() &&
            body["users"].size() == 2,
            "valid users overview response"
        );

        expect(
            response.headers.find(
                "Cache-Control"
            ) != response.headers.end() &&
            response.headers.find(
                "Cache-Control"
            )->second == "no-store",
            "users overview should disable caching"
        );

        const json* local_user =
            find_user(
                body["users"],
                "local-user"
            );

        const json* node_user =
            find_user(
                body["users"],
                "node-user"
            );

        expect(
            local_user &&
            local_user->contains(
                "home_node"
            ) &&
            (*local_user)["home_node"] == "" &&
            local_user->contains(
                "placement_policy_id"
            ) &&
            (*local_user)["placement_policy_id"] == "",
            "standalone user should expose empty placement metadata"
        );

        expect(
            node_user &&
            (*node_user)["home_node"] ==
                "node-01" &&
            (*node_user)["placement_policy_id"] ==
                "business-backup",
            "assigned user should expose node and policy IDs separately"
        );
    }

    cleanup();

    if (failures != 0) {
        std::cerr
            << failures
            << " admin users overview test(s) failed\n";
        return 1;
    }

    std::cout
        << "OK: admin users overview route tests passed\n";

    return 0;
}
