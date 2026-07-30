#include "routes/routes_admin_cluster_nodes.h"

#include "audit_log.h"
#include "cluster/node_registry.h"
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
        std::cerr << "FAIL: " << message << '\n';
    }
}

fs::path make_test_directory() {
    std::string pattern =
        (
            fs::temp_directory_path() /
            "pqnas-admin-cluster-routes-XXXXXX"
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

httplib::Request request_with_body(
    const std::string& body
) {
    httplib::Request request;
    request.body = body;
    request.remote_addr = "192.0.2.10";
    return request;
}

} // namespace

int main() {
    const fs::path directory =
        make_test_directory();

    if (directory.empty()) {
        std::cerr
            << "FAIL: could not create test directory\n";
        return 1;
    }

    const auto cleanup = [&]() {
        std::error_code error;
        fs::remove_all(directory, error);
    };

    pqnas::cluster::NodeRegistry registry;
    std::string registry_error;

    expect(
        registry.load(
            (
                directory /
                "node_registry.json"
            ).string(),
            &registry_error
        ),
        "load empty node registry: " +
            registry_error
    );

    bool auth_allowed = true;
    bool origin_allowed = true;
    int auth_calls = 0;
    int origin_calls = 0;

    std::vector<pqnas::AuditEvent> audits;

    std::unordered_map<
        std::string,
        std::size_t
    > assigned_user_counts;

    pqnas::cluster::AdminClusterNodeRoutesContext
        context;

    context.registry = &registry;

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
                *actor_fp = "admin-fingerprint";
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
                "2026-07-28T07:00:00Z"
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
                        kAdminClusterNodeRequestBodyMaxBytes +
                    100,
                    'x'
                )
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_nodes_create(
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
            handle_admin_cluster_nodes_list(
                request,
                response,
                context
            );

        const json body =
            response_json(response);

        expect(
            response.status == 200,
            "empty node list should return 200"
        );

        expect(
            body.is_object() &&
            body["nodes"].is_array() &&
            body["nodes"].empty(),
            "empty node list response"
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
            "GET response should disable caching"
        );
    }

    {
        auth_allowed = true;
        origin_allowed = false;

        httplib::Request request =
            request_with_body(
                R"({
                    "node_id":"node-01",
                    "api_base_url":"https://node-01.internal"
                })"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_nodes_create(
                request,
                response,
                context
            );

        expect(
            response.status == 403,
            "same-origin failure should block mutation"
        );

        expect(
            registry.snapshot().empty(),
            "same-origin failure must not mutate registry"
        );
    }

    {
        auth_allowed = true;
        origin_allowed = true;

        httplib::Request request =
            request_with_body(
                std::string(
                    pqnas::cluster::
                        kAdminClusterNodeRequestBodyMaxBytes +
                    1,
                    'x'
                )
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_nodes_create(
                request,
                response,
                context
            );

        expect(
            response.status == 413,
            "oversized body should return 413"
        );

        expect(
            registry.snapshot().empty(),
            "oversized request must not mutate registry"
        );
    }

    {
        httplib::Request request =
            request_with_body(
                R"({
                    "node_id":"node-01",
                    "api_base_url":"https://node-01.internal/",
                    "enabled":true
                })"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_nodes_create(
                request,
                response,
                context
            );

        const json body =
            response_json(response);

        expect(
            response.status == 201,
            "valid node create should return 201"
        );

        expect(
            body.is_object() &&
            body["node"]["node_id"] == "node-01" &&
            body["node"]["api_base_url"] ==
                "https://node-01.internal",
            "created node should use normalized URL"
        );

        expect(
            registry.get("node-01").has_value(),
            "created node should persist"
        );

        expect(
            !audits.empty() &&
            audits.back().event ==
                "admin.cluster_node.create" &&
            audits.back().outcome == "ok",
            "successful create should be audited"
        );
    }

    {
        httplib::Request request =
            request_with_body(
                R"({
                    "node_id":"node-01",
                    "api_base_url":"https://duplicate.internal"
                })"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_nodes_create(
                request,
                response,
                context
            );

        expect(
            response.status == 409,
            "duplicate node create should return 409"
        );
    }

    {
        httplib::Request request =
            request_with_body(
                R"({
                    "node_id":"node-99",
                    "enabled":false
                })"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_nodes_update(
                request,
                response,
                context
            );

        expect(
            response.status == 404,
            "updating missing node should return 404"
        );
    }

    {
        httplib::Request request =
            request_with_body(
                R"({
                    "node_id":"node-01",
                    "api_base_url":"https://node-01.internal:8443",
                    "enabled":false
                })"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_nodes_update(
                request,
                response,
                context
            );

        const auto updated =
            registry.get("node-01");

        expect(
            response.status == 200,
            "valid node update should return 200"
        );

        expect(
            updated.has_value() &&
            updated->api_base_url ==
                "https://node-01.internal:8443" &&
            !updated->enabled &&
            updated->health == "disabled",
            "update should persist URL and enabled state"
        );

        expect(
            !audits.empty() &&
            audits.back().event ==
                "admin.cluster_node.update" &&
            audits.back().outcome == "ok",
            "successful update should be audited"
        );
    }

    assigned_user_counts["node-01"] = 2;

    {
        httplib::Request request;
        request.remote_addr = "192.0.2.10";

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_nodes_list(
                request,
                response,
                context
            );

        const json body =
            response_json(response);

        expect(
            response.status == 200 &&
            body["nodes"].size() == 1,
            "list should return configured node"
        );

        expect(
            body["nodes"][0]["health"] ==
                "disabled",
            "list should expose Director health state"
        );

        expect(
            body["nodes"][0]["assigned_users"] == 2,
            "list should expose aggregate assigned user count"
        );
    }

    {
        httplib::Request request =
            request_with_body(
                R"({"node_id":"node-01"})"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_nodes_delete(
                request,
                response,
                context
            );

        const json body =
            response_json(response);

        expect(
            response.status == 409 &&
            body["assigned_users"] == 2,
            "assigned node delete should return 409"
        );

        expect(
            registry.get("node-01").has_value(),
            "assigned node must remain registered"
        );
    }

    assigned_user_counts.erase("node-01");

    {
        httplib::Request request =
            request_with_body(
                R"({"node_id":"node-01"})"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_nodes_delete(
                request,
                response,
                context
            );

        expect(
            response.status == 200,
            "valid delete should return 200"
        );

        expect(
            registry.snapshot().empty(),
            "deleted node should be removed"
        );

        expect(
            !audits.empty() &&
            audits.back().event ==
                "admin.cluster_node.delete" &&
            audits.back().outcome == "ok",
            "successful delete should be audited"
        );
    }

    {
        httplib::Request request =
            request_with_body(
                R"({"node_id":"node-01"})"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_nodes_delete(
                request,
                response,
                context
            );

        expect(
            response.status == 404,
            "deleting missing node should return 404"
        );
    }

    cleanup();

    if (failures != 0) {
        std::cerr
            << failures
            << " admin cluster route test(s) failed\n";
        return 1;
    }

    std::cout
        << "OK: admin cluster node route tests passed\n";
    return 0;
}
