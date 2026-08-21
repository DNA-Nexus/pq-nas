#include "routes/routes_admin_user_lifecycle.h"

#include "httplib.h"
#include "users_registry.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

namespace {

using json = nlohmann::json;

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

pqnas::UserRec make_disabled_user(const std::string& fp) {
    pqnas::UserRec user;
    user.fingerprint = fp;
    user.name = "Delete test user";
    user.role = "user";
    user.status = "disabled";
    user.storage_state = "unallocated";
    user.app_prefs_json = "{}";
    return user;
}

struct TestState {
    bool physical_ok = true;
    bool physical_erased = true;
    bool placement_blocked = false;
    bool cluster_revoke_pending = false;
    std::string physical_error;

    bool cleanup_ok = true;
    std::string cleanup_error;

    int physical_calls = 0;
    int cleanup_calls = 0;
};

json parse_body(const httplib::Result& result) {
    if (!result) return json{};
    return json::parse(result->body, nullptr, false);
}

bool run_case(
    const std::string& name,
    bool create_user,
    TestState& state,
    int expected_status,
    const std::string& expected_error,
    bool expected_already_deleted,
    int expected_physical_calls,
    int expected_cleanup_calls
) {
    pqnas::UsersRegistry users;

    const std::string target_fp = "target-delete-fingerprint";
    const std::string actor_fp = "admin-delete-fingerprint";

    if (create_user) {
        if (!users.upsert(make_disabled_user(target_fp))) {
            ++failures;
            std::cerr << "FAIL: " << name << ": fixture user creation failed\n";
            return false;
        }
    }

    httplib::Server server;

    AdminUserLifecycleRoutesContext ctx;
    ctx.users = &users;
    ctx.users_path =
        (std::filesystem::temp_directory_path() /
         "pqnas-delete-auth-cleanup-unused-users.json").string();

    ctx.require_admin =
        [&](const httplib::Request&,
            httplib::Response&,
            std::string* out_actor_fp) {
            if (out_actor_fp) *out_actor_fp = actor_fp;
            return true;
        };

    ctx.require_same_origin =
        [](const httplib::Request&, httplib::Response&) {
            return true;
        };

    ctx.reply_json =
        [](httplib::Response& response,
           int status,
           const std::string& body) {
            response.status = status;
            response.set_content(
                body,
                "application/json; charset=utf-8"
            );
        };

    ctx.admin_would_remove_last_enabled_admin =
        [](const std::string&) {
            return false;
        };

    ctx.revoke_devices_for_fingerprint =
        [](const std::string&) {};

    ctx.cleanup_auth_state_for_fingerprint =
        [&](const std::string& fp, std::string* err) {
            ++state.cleanup_calls;

            expect(
                fp == target_fp,
                name + ": cleanup fingerprint must match target"
            );

            if (err) *err = state.cleanup_error;
            return state.cleanup_ok;
        };

    ctx.now_iso_utc =
        []() {
            return std::string("2026-08-21T10:00:00Z");
        };

    ctx.audit_append =
        [](const pqnas::AuditEvent&) {};

    ctx.erase_user_persisted_cluster_safe =
        [&](const std::string& fp,
            bool* erased,
            bool* placement_blocked,
            bool* cluster_revoke_pending,
            std::string* error) {
            ++state.physical_calls;

            expect(
                fp == target_fp,
                name + ": physical delete fingerprint must match target"
            );

            if (erased) *erased = state.physical_erased;
            if (placement_blocked) {
                *placement_blocked = state.placement_blocked;
            }
            if (cluster_revoke_pending) {
                *cluster_revoke_pending =
                    state.cluster_revoke_pending;
            }
            if (error) *error = state.physical_error;

            return state.physical_ok;
        };

    register_admin_user_lifecycle_routes(server, ctx);

    const int port =
        server.bind_to_any_port("127.0.0.1");

    if (port <= 0) {
        ++failures;
        std::cerr << "FAIL: " << name << ": bind failed\n";
        return false;
    }

    std::thread server_thread([&]() {
        server.listen_after_bind();
    });

    std::this_thread::sleep_for(
        std::chrono::milliseconds(20)
    );

    httplib::Client client("127.0.0.1", port);
    client.set_connection_timeout(2);
    client.set_read_timeout(2);

    const auto result =
        client.Post(
            "/api/v4/admin/users/delete",
            json{{"fingerprint", target_fp}}.dump(),
            "application/json"
        );

    server.stop();
    server_thread.join();

    expect(
        static_cast<bool>(result),
        name + ": request must produce a response"
    );

    if (!result) return false;

    const json body = parse_body(result);

    expect(
        result->status == expected_status,
        name + ": unexpected HTTP status"
    );

    if (!expected_error.empty()) {
        expect(
            body.is_object() &&
            body.value("error", "") == expected_error,
            name + ": unexpected error code"
        );
    } else {
        expect(
            body.is_object() &&
            body.value("ok", false),
            name + ": expected ok=true"
        );

        expect(
            body.value("already_deleted", false) ==
                expected_already_deleted,
            name + ": unexpected already_deleted value"
        );
    }

    expect(
        state.physical_calls == expected_physical_calls,
        name + ": unexpected physical delete call count"
    );

    expect(
        state.cleanup_calls == expected_cleanup_calls,
        name + ": unexpected auth cleanup call count"
    );

    return true;
}

} // namespace

int main() {
    {
        TestState state;
        state.physical_erased = false;
        state.placement_blocked = true;

        run_case(
            "placement blocked",
            true,
            state,
            409,
            "cluster_revoke_required",
            false,
            1,
            0
        );
    }

    {
        TestState state;

        run_case(
            "successful physical delete",
            true,
            state,
            200,
            "",
            false,
            1,
            1
        );
    }

    {
        TestState state;
        state.cleanup_ok = false;
        state.cleanup_error =
            "opaque_credentials_save_failed";

        run_case(
            "auth cleanup pending",
            true,
            state,
            500,
            "auth_cleanup_pending",
            false,
            1,
            1
        );
    }

    {
        TestState state;

        run_case(
            "missing user cleanup retry",
            false,
            state,
            200,
            "",
            true,
            0,
            1
        );
    }

    if (failures != 0) {
        std::cerr
            << "FAIL: admin user delete auth-cleanup regression failures="
            << failures
            << '\n';
        return 1;
    }

    std::cout
        << "OK: admin user delete auth-cleanup regression tests passed\n";

    return 0;
}
