#include "routes/routes_admin_cluster_user_policy.h"

#include "cluster/user_placement_policy_assignment_service.h"
#include "cluster/user_placement_policy_registry.h"
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
            "pqnas-admin-user-policy-route-XXXXXX"
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

pqnas::UserRec make_user() {
    pqnas::UserRec user;
    user.fingerprint =
        "user-fingerprint";

    user.role = "user";
    user.status = "enabled";
    user.storage_state = "unallocated";
    user.app_prefs_json = "{}";
    user.home_node = "node-01";

    return user;
}

pqnas::cluster::UserPlacementPolicy
make_policy() {
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
            make_user()
        ),
        "create policy route user"
    );

    expect(
        users.save(
            users_path.string()
        ),
        "persist policy route user"
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
            make_policy(),
            &error
        ),
        "create route policy: " + error
    );

    std::vector<AuditRecord> audits;

    pqnas::cluster::
        UserPlacementPolicyAssignmentServiceConfig
            service_config;

    service_config.users = &users;
    service_config.policies = &policies;

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
            pqnas::cluster::
                UserPlacementPolicyAssignmentService
        >(
            service_config
        );

    bool auth_allowed = true;
    bool origin_allowed = true;
    int auth_calls = 0;
    int origin_calls = 0;

    pqnas::cluster::
        AdminClusterUserPolicyRoutesContext
            context;

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
                        kAdminClusterUserPolicyBodyMaxBytes +
                    100,
                    'x'
                )
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_user_policy_assign(
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
            "unauthorized request must not call policy service"
        );
    }

    {
        auth_allowed = true;
        origin_allowed = false;

        httplib::Request request =
            request_with_body(
                R"({
                    "fingerprint":"user-fingerprint",
                    "policy_id":"business-backup"
                })"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_user_policy_assign(
                request,
                response,
                context
            );

        expect(
            response.status == 403,
            "same-origin failure should block policy assignment"
        );

        expect(
            audits.empty(),
            "same-origin failure must not call policy service"
        );
    }

    {
        origin_allowed = true;

        httplib::Request request =
            request_with_body(
                std::string(
                    pqnas::cluster::
                        kAdminClusterUserPolicyBodyMaxBytes +
                    1,
                    'x'
                )
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_user_policy_assign(
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
                    "policy_id":"business-backup",
                    "extra":true
                })"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_user_policy_assign(
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
                    "policy_id":1
                })"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_user_policy_assign(
                request,
                response,
                context
            );

        expect(
            response.status == 400,
            "non-string policy ID should return 400"
        );
    }

    {
        httplib::Request request =
            request_with_body(
                R"({
                    "fingerprint":"user-fingerprint",
                    "policy_id":"business-backup"
                })"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_user_policy_assign(
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
            body["placement_policy_id"] ==
                "business-backup",
            "valid policy should return changed success"
        );

        expect(
            response.headers.find(
                "Cache-Control"
            ) != response.headers.end() &&
            response.headers.find(
                "Cache-Control"
            )->second == "no-store",
            "policy response should disable caching"
        );

        const auto user =
            users.get("user-fingerprint");

        expect(
            user.has_value() &&
            user->home_node == "node-01",
            "policy route must not change home_node"
        );
    }

    {
        httplib::Request request =
            request_with_body(
                R"({
                    "fingerprint":"user-fingerprint",
                    "policy_id":"business-backup"
                })"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_user_policy_assign(
                request,
                response,
                context
            );

        const json body =
            response_json(response);

        expect(
            response.status == 200 &&
            body["changed"] == false,
            "same policy should be idempotent"
        );
    }

    {
        httplib::Request request =
            request_with_body(
                R"({
                    "fingerprint":"user-fingerprint",
                    "policy_id":"missing-policy"
                })"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_user_policy_assign(
                request,
                response,
                context
            );

        const json body =
            response_json(response);

        expect(
            response.status == 404 &&
            body["reason_code"] ==
                "policy_not_found",
            "unknown policy should return 404"
        );
    }

    {
        httplib::Request request =
            request_with_body(
                R"({
                    "fingerprint":"user-fingerprint",
                    "policy_id":"../business"
                })"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_user_policy_assign(
                request,
                response,
                context
            );

        const json body =
            response_json(response);

        expect(
            response.status == 400 &&
            body["reason_code"] ==
                "invalid_policy_id",
            "unsafe policy ID should return 400"
        );
    }

    {
        httplib::Request request =
            request_with_body(
                R"({
                    "fingerprint":"missing-user",
                    "policy_id":"business-backup"
                })"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_user_policy_assign(
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
                    "policy_id":""
                })"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_user_policy_assign(
                request,
                response,
                context
            );

        const json body =
            response_json(response);

        expect(
            response.status == 200 &&
            body["changed"] == true &&
            body["previous_policy_id"] ==
                "business-backup" &&
            body["placement_policy_id"] == "",
            "empty policy ID should clear override"
        );
    }

    pqnas::UsersRegistry reloaded;

    expect(
        reloaded.load(
            users_path.string()
        ),
        "reload users after policy route"
    );

    const auto reloaded_user =
        reloaded.get(
            "user-fingerprint"
        );

    expect(
        reloaded_user.has_value() &&
        reloaded_user->
            placement_policy_id.empty() &&
        reloaded_user->home_node ==
            "node-01",
        "route should persist policy clear without moving user"
    );

    expect(
        audits.size() == 6,
        "only requests reaching policy service should be audited"
    );

    for (const AuditRecord& audit : audits) {
        expect(
            audit.event ==
                "admin.user_placement_policy.assign",
            "route must use policy assignment audit event"
        );

        expect(
            audit.fields.count(
                "node_ids"
            ) == 0 &&
            audit.fields.count(
                "users_path"
            ) == 0 &&
            audit.fields.count(
                "error"
            ) == 0,
            "route audit must exclude topology, paths and raw errors"
        );
    }

    cleanup();

    if (failures != 0) {
        std::cerr
            << failures
            << " admin user policy route test(s) failed\n";
        return 1;
    }

    std::cout
        << "OK: admin cluster user policy route tests passed\n";

    return 0;
}
