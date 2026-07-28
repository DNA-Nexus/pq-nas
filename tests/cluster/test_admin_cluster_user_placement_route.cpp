#include "routes/routes_admin_cluster_user_placement.h"

#include "cluster/node_registry.h"
#include "cluster/user_placement_service.h"
#include "httplib.h"
#include "users_registry.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

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
            "pqnas-admin-user-placement-XXXXXX"
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

httplib::Request request_with_body(
    const std::string& body
) {
    httplib::Request request;
    request.body = body;
    request.remote_addr = "192.0.2.10";
    return request;
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

pqnas::UserRec make_user(
    const std::string& fingerprint
) {
    pqnas::UserRec user;
    user.fingerprint = fingerprint;
    user.role = "user";
    user.status = "enabled";
    user.storage_state = "unallocated";
    user.app_prefs_json = "{}";
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
            make_user("user-fingerprint")
        ),
        "create placement route user"
    );

    expect(
        users.save(users_path.string()),
        "persist placement route user"
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

    expect(
        nodes.upsert_configured_node(
            "node-01",
            "https://node-01.internal",
            true,
            "2026-07-28T13:00:00Z",
            &error
        ),
        "add enabled node: " + error
    );

    error.clear();

    expect(
        nodes.upsert_configured_node(
            "node-02",
            "https://node-02.internal",
            false,
            "2026-07-28T13:00:00Z",
            &error
        ),
        "add disabled node: " + error
    );

    std::vector<AuditRecord> audits;

    pqnas::cluster::UserPlacementServiceConfig
        service_config;

    service_config.users = &users;
    service_config.nodes = &nodes;
    service_config.placement_mutex =
        std::make_shared<std::mutex>();
    service_config.users_path =
        users_path.string();

    service_config.audit_emit =
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

    auto service =
        std::make_shared<
            pqnas::cluster::UserPlacementService
        >(
            service_config
        );

    bool auth_allowed = true;
    bool origin_allowed = true;
    int auth_calls = 0;
    int origin_calls = 0;

    pqnas::cluster::
        AdminClusterUserPlacementRoutesContext context;

    context.service = service;

    context.require_admin_auth =
        [&](
            const httplib::Request&,
            httplib::Response& response,
            std::string* actor_fingerprint
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

            if (actor_fingerprint) {
                *actor_fingerprint =
                    "admin-fingerprint";
            }

            return true;
        };

    context.require_same_origin =
        [&](
            const httplib::Request&,
            httplib::Response& response
        ) {
            ++origin_calls;

            if (!origin_allowed) {
                response.status = 403;
                response.set_content(
                    R"({"ok":false,"error":"forbidden"})",
                    "application/json"
                );
                return false;
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

    {
        auth_allowed = false;
        origin_allowed = true;
        auth_calls = 0;
        origin_calls = 0;

        httplib::Request request =
            request_with_body(
                std::string(
                    pqnas::cluster::
                        kAdminClusterUserPlacementBodyMaxBytes +
                    100,
                    'x'
                )
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_user_home_node_assign(
                request,
                response,
                context
            );

        expect(
            response.status == 401,
            "authentication should run before body parsing"
        );

        expect(
            auth_calls == 1 &&
            origin_calls == 0,
            "unauthorized request must not reach origin check"
        );

        expect(
            audits.empty(),
            "unauthorized request must not call placement service"
        );
    }

    {
        auth_allowed = true;
        origin_allowed = false;

        httplib::Request request =
            request_with_body(
                R"({
                    "fingerprint":"user-fingerprint",
                    "node_id":"node-01"
                })"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_user_home_node_assign(
                request,
                response,
                context
            );

        expect(
            response.status == 403,
            "same-origin failure should block placement"
        );

        expect(
            audits.empty(),
            "same-origin failure must not call placement service"
        );
    }

    {
        origin_allowed = true;

        httplib::Request request =
            request_with_body(
                std::string(
                    pqnas::cluster::
                        kAdminClusterUserPlacementBodyMaxBytes +
                    1,
                    'x'
                )
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_user_home_node_assign(
                request,
                response,
                context
            );

        expect(
            response.status == 413,
            "oversized request should return 413"
        );
    }

    {
        httplib::Request request =
            request_with_body(
                R"({
                    "fingerprint":"user-fingerprint",
                    "node_id":"node-01",
                    "extra":true
                })"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_user_home_node_assign(
                request,
                response,
                context
            );

        expect(
            response.status == 400,
            "unknown JSON key should return 400"
        );
    }

    {
        httplib::Request request =
            request_with_body(
                R"({
                    "fingerprint":"user-fingerprint",
                    "node_id":1
                })"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_user_home_node_assign(
                request,
                response,
                context
            );

        expect(
            response.status == 400,
            "non-string node ID should return 400"
        );
    }

    {
        httplib::Request request =
            request_with_body(
                R"({
                    "fingerprint":"user-fingerprint",
                    "node_id":"node-01"
                })"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_user_home_node_assign(
                request,
                response,
                context
            );

        const json body =
            response_json(response);

        expect(
            response.status == 200 &&
            body["ok"] == true &&
            body["changed"] == true &&
            body["home_node"] == "node-01",
            "valid placement should return changed success"
        );

        expect(
            response.headers.find(
                "Cache-Control"
            ) != response.headers.end() &&
            response.headers.find(
                "Cache-Control"
            )->second == "no-store",
            "placement response should disable caching"
        );
    }

    {
        httplib::Request request =
            request_with_body(
                R"({
                    "fingerprint":"user-fingerprint",
                    "node_id":"node-01"
                })"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_user_home_node_assign(
                request,
                response,
                context
            );

        const json body =
            response_json(response);

        expect(
            response.status == 200 &&
            body["changed"] == false,
            "same placement should be idempotent"
        );
    }

    {
        httplib::Request request =
            request_with_body(
                R"({
                    "fingerprint":"user-fingerprint",
                    "node_id":"node-02"
                })"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_user_home_node_assign(
                request,
                response,
                context
            );

        const json body =
            response_json(response);

        expect(
            response.status == 409 &&
            body["reason_code"] ==
                "node_disabled",
            "disabled node should return 409"
        );
    }

    {
        httplib::Request request =
            request_with_body(
                R"({
                    "fingerprint":"user-fingerprint",
                    "node_id":"node-99"
                })"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_user_home_node_assign(
                request,
                response,
                context
            );

        expect(
            response.status == 404,
            "unknown node should return 404"
        );
    }

    {
        httplib::Request request =
            request_with_body(
                R"({
                    "fingerprint":"missing-user",
                    "node_id":"node-01"
                })"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_user_home_node_assign(
                request,
                response,
                context
            );

        expect(
            response.status == 404,
            "unknown user should return 404"
        );
    }

    {
        httplib::Request request =
            request_with_body(
                R"({
                    "fingerprint":"user-fingerprint",
                    "node_id":"../node"
                })"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_user_home_node_assign(
                request,
                response,
                context
            );

        expect(
            response.status == 400,
            "invalid node ID should return 400"
        );
    }

    pqnas::UsersRegistry reloaded;

    expect(
        reloaded.load(users_path.string()),
        "reload users after route placement"
    );

    const auto reloaded_user =
        reloaded.get("user-fingerprint");

    expect(
        reloaded_user.has_value() &&
        reloaded_user->home_node ==
            "node-01",
        "route placement should persist home_node"
    );

    expect(
        audits.size() == 6,
        "only requests reaching placement service should be audited"
    );

    for (const AuditRecord& audit : audits) {
        expect(
            audit.event ==
                "admin.user_home_node.assign",
            "route must use placement service audit event"
        );
    }

    cleanup();

    if (failures != 0) {
        std::cerr
            << failures
            << " admin placement route test(s) failed\n";
        return 1;
    }

    std::cout
        << "OK: admin cluster user placement route tests passed\n";

    return 0;
}
