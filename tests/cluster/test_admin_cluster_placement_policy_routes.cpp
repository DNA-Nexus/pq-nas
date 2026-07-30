#include "routes/routes_admin_cluster_placement_policies.h"

#include "audit_log.h"
#include "cluster/user_placement_policy_registry.h"
#include "httplib.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <unistd.h>
#include <unordered_map>
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
            "pqnas-admin-placement-policy-XXXXXX"
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

std::string response_header(
    const httplib::Response& response,
    const std::string& name
) {
    const auto iterator =
        response.headers.find(name);

    if (iterator == response.headers.end()) {
        return {};
    }

    return iterator->second;
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

    pqnas::cluster::
        UserPlacementPolicyRegistry registry;

    std::string registry_error;

    expect(
        registry.load(
            (
                directory /
                "placement_policies.json"
            ).string(),
            &registry_error
        ),
        "load policy registry: " +
            registry_error
    );

    bool auth_allowed = true;
    bool origin_allowed = true;
    int auth_calls = 0;
    int origin_calls = 0;

    std::unordered_map<
        std::string,
        std::size_t
    > assigned_user_counts;

    std::vector<pqnas::AuditEvent>
        audits;

    pqnas::cluster::
        AdminClusterPlacementPolicyRoutesContext
            context;

    context.registry =
        &registry;

    context.placement_mutex =
        std::make_shared<std::mutex>();

    context.assigned_user_counts =
        [&]() {
            return assigned_user_counts;
        };

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

    context.now_iso_utc =
        []() {
            return std::string(
                "2026-07-29T05:00:00Z"
            );
        };

    context.audit_append =
        [&](const pqnas::AuditEvent& audit) {
            audits.push_back(audit);
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
                        kAdminClusterPlacementPolicyBodyMaxBytes +
                    100,
                    'x'
                )
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_placement_policies_upsert(
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
            "unauthorized mutation must not reach origin check"
        );

        expect(
            registry.snapshot().empty(),
            "unauthorized request must not mutate registry"
        );
    }

    {
        auth_allowed = true;
        origin_allowed = true;
        auth_calls = 0;
        origin_calls = 0;

        httplib::Request request;
        request.remote_addr = "192.0.2.10";

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_placement_policies_list(
                request,
                response,
                context
            );

        const json body =
            response_json(response);

        expect(
            response.status == 200 &&
            body["policies"].is_array() &&
            body["policies"].empty(),
            "empty policy list should return 200"
        );

        expect(
            auth_calls == 1 &&
            origin_calls == 0,
            "GET should require auth but not same-origin"
        );

        expect(
            response_header(
                response,
                "Cache-Control"
            ) == "no-store",
            "policy list should disable caching"
        );
    }

    {
        auth_allowed = true;
        origin_allowed = false;

        httplib::Request request =
            request_with_body(
                R"({
                    "policy_id":"standard",
                    "mode":"automatic",
                    "fallback":"deny",
                    "node_ids":[]
                })"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_placement_policies_upsert(
                request,
                response,
                context
            );

        expect(
            response.status == 403,
            "same-origin failure should block policy mutation"
        );

        expect(
            registry.snapshot().empty(),
            "same-origin failure must preserve registry"
        );
    }

    {
        origin_allowed = true;

        httplib::Request request =
            request_with_body(
                std::string(
                    pqnas::cluster::
                        kAdminClusterPlacementPolicyBodyMaxBytes +
                    1,
                    'x'
                )
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_placement_policies_upsert(
                request,
                response,
                context
            );

        expect(
            response.status == 413,
            "oversized policy body should return 413"
        );
    }

    {
        httplib::Request request =
            request_with_body(
                R"({
                    "policy_id":"standard",
                    "mode":"automatic",
                    "fallback":"deny",
                    "node_ids":[],
                    "extra":true
                })"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_placement_policies_upsert(
                request,
                response,
                context
            );

        expect(
            response.status == 400,
            "unknown policy field should return 400"
        );
    }

    {
        httplib::Request request =
            request_with_body(
                R"({
                    "policy_id":"bad-pinned",
                    "mode":"pinned",
                    "fallback":"deny",
                    "node_ids":[]
                })"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_placement_policies_upsert(
                request,
                response,
                context
            );

        const json body =
            response_json(response);

        expect(
            response.status == 400 &&
            body["reason_code"] ==
                "pinned_policy_requires_one_node",
            "invalid policy combination should expose bounded reason"
        );
    }

    {
        httplib::Request request =
            request_with_body(
                R"({
                    "policy_id":"standard",
                    "mode":"automatic",
                    "fallback":"deny",
                    "node_ids":[]
                })"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_placement_policies_upsert(
                request,
                response,
                context
            );

        const json body =
            response_json(response);

        expect(
            response.status == 201 &&
            body["ok"] == true &&
            body["created"] == true &&
            body["changed"] == true &&
            body["policy"]["policy_id"] ==
                "standard",
            "valid automatic policy should be created"
        );
    }

    {
        httplib::Request request =
            request_with_body(
                R"({
                    "policy_id":"standard",
                    "mode":"automatic",
                    "fallback":"deny",
                    "node_ids":[]
                })"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_placement_policies_upsert(
                request,
                response,
                context
            );

        const json body =
            response_json(response);

        expect(
            response.status == 200 &&
            body["created"] == false &&
            body["changed"] == false,
            "identical upsert should be idempotent"
        );
    }

    {
        httplib::Request request =
            request_with_body(
                R"({
                    "policy_id":"business-backup",
                    "mode":"allowed_nodes",
                    "fallback":"deny",
                    "node_ids":[
                        "node-business-02",
                        "node-business-01"
                    ]
                })"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_placement_policies_upsert(
                request,
                response,
                context
            );

        const json body =
            response_json(response);

        expect(
            response.status == 201 &&
            body["policy"]["node_ids"][0] ==
                "node-business-01" &&
            body["policy"]["node_ids"][1] ==
                "node-business-02",
            "policy node IDs should be returned canonically sorted"
        );
    }

    assigned_user_counts[
        "business-backup"
    ] = 3;

    {
        httplib::Request request;
        request.remote_addr = "192.0.2.10";

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_placement_policies_list(
                request,
                response,
                context
            );

        const json body =
            response_json(response);

        expect(
            response.status == 200 &&
            body["policies"].size() == 2,
            "policy list should return registered policies"
        );

        expect(
            body["policies"][0]["policy_id"] ==
                "business-backup" &&
            body["policies"][0]["assigned_users"] ==
                3 &&
            body["policies"][1]["policy_id"] ==
                "standard" &&
            body["policies"][1]["assigned_users"] ==
                0,
            "policy list should expose sorted aggregate assignments"
        );
    }

    {
        httplib::Request request =
            request_with_body(
                R"({"policy_id":"business-backup"})"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_placement_policies_delete(
                request,
                response,
                context
            );

        const json body =
            response_json(response);

        expect(
            response.status == 409 &&
            body["assigned_users"] == 3,
            "assigned policy delete should return 409"
        );

        expect(
            registry.get(
                "business-backup"
            ).has_value(),
            "assigned policy must remain registered"
        );

        expect(
            body.dump().find(
                "admin-fingerprint"
            ) == std::string::npos,
            "delete conflict must not expose fingerprints"
        );
    }

    assigned_user_counts.erase(
        "business-backup"
    );

    {
        httplib::Request request =
            request_with_body(
                R"({"policy_id":"business-backup"})"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_placement_policies_delete(
                request,
                response,
                context
            );

        expect(
            response.status == 200,
            "unassigned policy should be deleted"
        );

        expect(
            !registry.get(
                "business-backup"
            ).has_value(),
            "deleted policy should leave registry"
        );
    }

    {
        httplib::Request request =
            request_with_body(
                R"({"policy_id":"business-backup"})"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_placement_policies_delete(
                request,
                response,
                context
            );

        expect(
            response.status == 404,
            "deleting missing policy should return 404"
        );
    }

    expect(
        !audits.empty(),
        "policy mutations should be audited"
    );

    for (const pqnas::AuditEvent& audit :
         audits) {
        expect(
            audit.f.find("node_ids") ==
                audit.f.end() &&
            audit.f.find("request_body") ==
                audit.f.end() &&
            audit.f.find("registry_path") ==
                audit.f.end() &&
            audit.f.find("error") ==
                audit.f.end(),
            "policy audit must exclude node sets, bodies, paths and raw errors"
        );
    }

    cleanup();

    if (failures != 0) {
        std::cerr
            << failures
            << " admin placement policy route test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: admin cluster placement policy route tests passed\n";

    return 0;
}
