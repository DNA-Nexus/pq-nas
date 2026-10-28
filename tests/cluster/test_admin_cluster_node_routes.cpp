#include "routes/routes_admin_cluster_nodes.h"

#include "audit_log.h"
#include "cluster/node_registry.h"
#include "httplib.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
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

    std::string actor_fingerprint =
        "admin-fingerprint";

    std::vector<pqnas::AuditEvent> audits;

    std::unordered_map<
        std::string,
        std::size_t
    > assigned_user_counts;

    pqnas::cluster::AdminClusterNodeRoutesContext
        context;

    context.registry = &registry;

    std::unordered_map<
        std::string,
        std::string
    > tls_trust;

    int tls_trust_bind_calls = 0;

    context.get_node_tls_trust =
        [&](const std::string& node_id,
            std::string* pin,
            std::string* error) {
            if (error) {
                error->clear();
            }

            const auto iterator =
                tls_trust.find(node_id);

            if (iterator ==
                tls_trust.end()) {
                if (pin) {
                    pin->clear();
                }

                return
                    pqnas::cluster::
                        NodeTlsTrustStoreResult::
                            NotFound;
            }

            if (pin) {
                *pin = iterator->second;
            }

            return
                pqnas::cluster::
                    NodeTlsTrustStoreResult::Ok;
        };

    context.bind_node_tls_trust =
        [&](const std::string& node_id,
            const std::string& pin,
            std::string* error) {
            ++tls_trust_bind_calls;

            if (error) {
                error->clear();
            }

            const auto iterator =
                tls_trust.find(node_id);

            if (iterator ==
                tls_trust.end()) {
                tls_trust.emplace(
                    node_id,
                    pin
                );

                return
                    pqnas::cluster::
                        NodeTlsTrustStoreResult::Ok;
            }

            return
                iterator->second == pin
                    ? pqnas::cluster::
                          NodeTlsTrustStoreResult::Ok
                    : pqnas::cluster::
                          NodeTlsTrustStoreResult::
                              TrustConflict;
        };

    bool tls_verify_allowed = true;
    bool tls_verify_throws = false;
    std::function<void()> tls_verify_hook;
    int tls_verify_calls = 0;
    std::string last_tls_verify_url;
    std::string last_tls_verify_pin;

    context.verify_node_tls_endpoint =
        [&](const std::string& api_base_url,
            const std::string& pin,
            std::string* error) {
            ++tls_verify_calls;

            last_tls_verify_url =
                api_base_url;

            last_tls_verify_pin =
                pin;

            if (tls_verify_throws) {
                throw 1;
            }

            if (tls_verify_hook) {
                tls_verify_hook();
            }

            if (!tls_verify_allowed) {
                if (error) {
                    *error =
                        "simulated TLS verification failure";
                }

                return false;
            }

            if (error) {
                error->clear();
            }

            return true;
        };

    context.placement_mutex =
        std::make_shared<std::mutex>();

    context.assigned_user_counts =
        [&]() {
            return assigned_user_counts;
        };

    bool rotation_busy = false;
    bool rotation_query_throws = false;

    context.rotation_in_progress =
        [&](const std::string&) {
            if (rotation_query_throws) {
                throw 1;
            }

            return rotation_busy;
        };

    int rotation_calls = 0;
    bool rotation_callback_throws = false;
    std::string last_rotation_node_id;

    pqnas::cluster::
        NodeCredentialRotationResult
            rotation_result;

    rotation_result.code =
        pqnas::cluster::
            NodeCredentialRotationServiceCode::Ok;

    rotation_result.director_credential_committed =
        true;

    rotation_result.storage_finalized =
        true;

    context.rotate_node_credential =
        [&](const std::string& node_id) {
            ++rotation_calls;
            last_rotation_node_id =
                node_id;

            if (rotation_callback_throws) {
                throw 1;
            }

            return rotation_result;
        };

    bool deletion_provisioning_active = false;
    bool deletion_confirmed_projection_active = false;
    int deletion_calls = 0;
    std::string last_delete_revoked_at;

    context.erase_unassigned_node =
        [&](const std::string& node_id,
            const std::string& revoked_at) {
            ++deletion_calls;

            pqnas::cluster::NodeDeletionResult result;

            last_delete_revoked_at = revoked_at;

            if (deletion_provisioning_active) {
                result.result =
                    pqnas::cluster::
                        NodeDeletionServiceResult::
                            ProvisioningActive;

                return result;
            }

            if (deletion_confirmed_projection_active) {
                result.result =
                    pqnas::cluster::
                        NodeDeletionServiceResult::
                            ConfirmedProjectionActive;

                return result;
            }

            std::string deletion_error;

            if (!registry.erase(
                    node_id,
                    &deletion_error)) {
                result.result =
                    pqnas::cluster::
                        NodeDeletionServiceResult::
                            NotFound;

                return result;
            }

            result.result =
                pqnas::cluster::
                    NodeDeletionServiceResult::Ok;

            result.node_removed = true;

            return result;
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
                *actor_fp =
                    actor_fingerprint;
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
            // Regression: pqnas::now_iso_utc() emits milliseconds.
            return std::string(
                "2026-07-28T07:00:00.123Z"
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
                    "browser_base_url":"https://storage-01.example.test/",
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
                "https://node-01.internal" &&
            body["node"]["browser_base_url"] ==
                "https://storage-01.example.test",
            "created node should use normalized URLs"
        );

        const auto created =
            registry.get("node-01");

        expect(
            created.has_value() &&
            created->added_at ==
                "2026-07-28T07:00:00Z" &&
            created->updated_at ==
                "2026-07-28T07:00:00Z" &&
            created->browser_base_url ==
                "https://storage-01.example.test",
            "created node should persist Director-owned browser routing"
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
                    "capacity_ceiling_bytes":1000
                })"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_nodes_update(
                request,
                response,
                context
            );

        const json body =
            response_json(response);

        expect(
            response.status == 200,
            "capacity ceiling update should return 200"
        );

        bool capacity_ceiling_exposed = false;

        if (body.is_object()) {
            const auto node_iterator =
                body.find("node");

            if (node_iterator != body.end() &&
                node_iterator->is_object()) {
                const auto ceiling_iterator =
                    node_iterator->find(
                        "capacity_ceiling_bytes"
                    );

                capacity_ceiling_exposed =
                    ceiling_iterator !=
                        node_iterator->end() &&
                    *ceiling_iterator == 1000;
            }
        }

        expect(
            capacity_ceiling_exposed,
            "capacity ceiling update should expose Director-owned limit"
        );

        const auto after_capacity =
            registry.get("node-01");

        expect(
            after_capacity.has_value() &&
            after_capacity->browser_base_url ==
                "https://storage-01.example.test",
            "capacity update must preserve browser routing"
        );
    }

    {
        const auto before =
            registry.get("node-01");

        httplib::Request request =
            request_with_body(
                R"({
                    "node_id":"node-01",
                    "browser_base_url":"http://storage-invalid.example.test"
                })"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_nodes_update(
                request,
                response,
                context
            );

        const auto after =
            registry.get("node-01");

        expect(
            response.status == 400,
            "plaintext browser URL update should be rejected"
        );

        expect(
            before.has_value() &&
            after.has_value() &&
            after->browser_base_url ==
                before->browser_base_url,
            "invalid browser URL must preserve node configuration"
        );
    }

    {
        httplib::Request request =
            request_with_body(
                R"({
                    "node_id":"node-01",
                    "browser_base_url":"https://storage-new.example.test/"
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

        const json body =
            response_json(response);

        expect(
            response.status == 200 &&
            updated.has_value() &&
            updated->browser_base_url ==
                "https://storage-new.example.test" &&
            body["node"]["browser_base_url"] ==
                "https://storage-new.example.test",
            "browser-only update should persist normalized authority"
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

    // Security regression: a lifecycle UPDATE must not change the same Node
    // while credential rotation owns its per-node operation marker.
    rotation_busy = true;

    {
        const auto before =
            registry.get(
                "node-01"
            );

        httplib::Request request =
            request_with_body(
                R"({
                    "node_id":"node-01",
                    "api_base_url":"https://blocked-update.internal",
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

        const auto after =
            registry.get(
                "node-01"
            );

        const json body =
            response_json(
                response
            );

        expect(
            response.status == 409 &&
            body["error"] == "conflict",
            "rotation-busy node update should return 409"
        );

        expect(
            before.has_value() &&
            after.has_value() &&
            after->api_base_url ==
                before->api_base_url &&
            after->enabled ==
                before->enabled &&
            after->capacity_ceiling_bytes ==
                before->capacity_ceiling_bytes &&
            after->browser_base_url ==
                before->browser_base_url,
            "rotation-busy update must preserve node configuration"
        );
    }

    rotation_busy = false;

    // Security regression: an uncertain busy-state query fails closed rather
    // than allowing a Node lifecycle mutation to proceed.
    rotation_query_throws = true;

    {
        const auto before =
            registry.get(
                "node-01"
            );

        httplib::Request request =
            request_with_body(
                R"({
                    "node_id":"node-01",
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

        const auto after =
            registry.get(
                "node-01"
            );

        expect(
            response.status == 500,
            "rotation-state query failure should fail update closed"
        );

        expect(
            before.has_value() &&
            after.has_value() &&
            after->api_base_url ==
                before->api_base_url &&
            after->enabled ==
                before->enabled &&
            after->capacity_ceiling_bytes ==
                before->capacity_ceiling_bytes &&
            after->browser_base_url ==
                before->browser_base_url,
            "rotation query failure must preserve node configuration"
        );
    }

    rotation_query_throws = false;

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
            updated->capacity_ceiling_bytes == 1000 &&
            updated->browser_base_url ==
                "https://storage-new.example.test" &&
            !updated->enabled &&
            updated->health == "disabled",
            "update should preserve Director-owned fields with lifecycle state"
        );

        expect(
            !audits.empty() &&
            audits.back().event ==
                "admin.cluster_node.update" &&
            audits.back().outcome == "ok",
            "successful update should be audited"
        );
    }

    // -----------------------------------------------------------------
    // RT014 legacy Node TLS trust migration.
    // -----------------------------------------------------------------

    constexpr char kMigrationPinA[] =
        "sha256/"
        "lAhFeu/Qcc7BJ8H5hTmTCGGtG6lMlA25dclywJ/Gi2g=";

    constexpr char kMigrationPinB[] =
        "sha256/"
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";

    {
        const auto before =
            registry.get("node-01");

        const int binds_before =
            tls_trust_bind_calls;

        tls_verify_allowed = false;

        httplib::Request request =
            request_with_body(
                std::string(
                    R"({"node_id":"node-01","api_base_url":"https://node-01.migrated.internal","tls_spki_sha256_pin":")"
                ) +
                kMigrationPinA +
                R"("})"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_node_tls_trust_migration(
                request,
                response,
                context
            );

        tls_verify_allowed = true;

        const auto after =
            registry.get("node-01");

        expect(
            response.status == 502,
            "failed TLS verification should return 502"
        );

        expect(
            tls_trust.find("node-01") ==
                tls_trust.end() &&
            tls_trust_bind_calls ==
                binds_before,
            "failed TLS verification must not persist trust"
        );

        expect(
            before.has_value() &&
            after.has_value() &&
            before->api_base_url ==
                after->api_base_url,
            "failed TLS verification must preserve node URL"
        );
    }

    {
        const auto before =
            registry.get("node-01");

        expect(
            before.has_value(),
            "TLS migration race fixture should exist"
        );

        tls_verify_hook =
            [&]() {
                const auto current =
                    registry.get("node-01");

                if (!current.has_value()) {
                    expect(
                        false,
                        "TLS migration race Node disappeared unexpectedly"
                    );
                    return;
                }

                std::string race_error;

                expect(
                    registry.erase(
                        "node-01",
                        &race_error
                    ),
                    "erase race fixture Node: " +
                        race_error
                );

                race_error.clear();

                expect(
                    registry.upsert_configured_node(
                        "node-01",
                        current->api_base_url,
                        current->enabled,
                        "2026-08-24T03:39:59Z",
                        &race_error,
                        current->
                            capacity_ceiling_bytes,
                        current->
                            browser_base_url
                    ),
                    "recreate race fixture Node: " +
                        race_error
                );
            };

        httplib::Request request =
            request_with_body(
                std::string(
                    R"({"node_id":"node-01","api_base_url":"https://node-01.migrated.internal","tls_spki_sha256_pin":")"
                ) +
                kMigrationPinA +
                R"("})"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_node_tls_trust_migration(
                request,
                response,
                context
            );

        tls_verify_hook = {};

        expect(
            response.status == 409,
            "delete/recreate during TLS verification should return 409"
        );

        expect(
            tls_trust.find("node-01") ==
                tls_trust.end(),
            "stale Node incarnation must not receive TLS trust"
        );
    }

    {
        const int verifies_before =
            tls_verify_calls;

        httplib::Request request =
            request_with_body(
                std::string(
                    R"({"node_id":"node-01","api_base_url":"https://node-01.migrated.internal/","tls_spki_sha256_pin":")"
                ) +
                kMigrationPinA +
                R"("})"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_node_tls_trust_migration(
                request,
                response,
                context
            );

        const auto migrated =
            registry.get("node-01");

        expect(
            response.status == 200,
            "verified TLS migration should return 200"
        );

        expect(
            tls_verify_calls ==
                verifies_before + 1 &&
            last_tls_verify_url ==
                "https://node-01.migrated.internal" &&
            last_tls_verify_pin ==
                kMigrationPinA,
            "migration should verify normalized URL and exact pin"
        );

        expect(
            tls_trust["node-01"] ==
                kMigrationPinA,
            "migration should persist verified TLS identity"
        );

        expect(
            migrated.has_value() &&
            migrated->api_base_url ==
                "https://node-01.migrated.internal",
            "migration should switch Node API URL"
        );

        expect(
            !audits.empty() &&
            audits.back().event ==
                "admin.cluster_node.tls_trust_migration" &&
            audits.back().outcome == "ok",
            "successful TLS migration should be audited"
        );
    }

    {
        const int verifies_before =
            tls_verify_calls;

        const int binds_before =
            tls_trust_bind_calls;

        httplib::Request request =
            request_with_body(
                std::string(
                    R"({"node_id":"node-01","api_base_url":"https://node-01.migrated.internal","tls_spki_sha256_pin":")"
                ) +
                kMigrationPinB +
                R"("})"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_node_tls_trust_migration(
                request,
                response,
                context
            );

        expect(
            response.status == 409,
            "different existing TLS identity should return 409"
        );

        expect(
            tls_verify_calls ==
                verifies_before &&
            tls_trust_bind_calls ==
                binds_before &&
            tls_trust["node-01"] ==
                kMigrationPinA,
            "trust conflict must fail before network or mutation"
        );
    }

    {
        const int verifies_before =
            tls_verify_calls;

        httplib::Request request =
            request_with_body(
                std::string(
                    R"({"node_id":"node-01","api_base_url":"https://node-01.migrated.internal","tls_spki_sha256_pin":")"
                ) +
                kMigrationPinA +
                R"("})"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_node_tls_trust_migration(
                request,
                response,
                context
            );

        expect(
            response.status == 200 &&
            tls_verify_calls ==
                verifies_before + 1 &&
            tls_trust["node-01"] ==
                kMigrationPinA,
            "exact TLS migration retry should be idempotent"
        );
    }

    // -----------------------------------------------------------------
    // Admin credential rotation trigger.
    // -----------------------------------------------------------------

    actor_fingerprint =
        "rotation-success-admin";

    {
        const int calls_before =
            rotation_calls;

        rotation_result.code =
            pqnas::cluster::
                NodeCredentialRotationServiceCode::Ok;

        rotation_result.director_credential_committed =
            true;

        rotation_result.storage_finalized =
            true;

        httplib::Request request =
            request_with_body(
                R"({"node_id":"node-01"})"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_node_credential_rotation(
                request,
                response,
                context
            );

        const json body =
            response_json(
                response
            );

        expect(
            response.status == 200 &&
            body["ok"] == true &&
            body["rotation"] == "completed" &&
            body["director_credential_committed"] == true &&
            body["storage_finalized"] == true,
            "successful credential rotation trigger should return 200"
        );

        expect(
            rotation_calls ==
                calls_before + 1 &&
            last_rotation_node_id ==
                "node-01",
            "rotation trigger should invoke service exactly once"
        );

        expect(
            !audits.empty() &&
            audits.back().event ==
                "admin.cluster_node.credential_rotation" &&
            audits.back().outcome ==
                "ok",
            "successful credential rotation should be audited"
        );
    }

    actor_fingerprint =
        "rotation-finalize-admin";

    {
        const int calls_before =
            rotation_calls;

        rotation_result.code =
            pqnas::cluster::
                NodeCredentialRotationServiceCode::
                    FinalizePending;

        rotation_result.director_credential_committed =
            true;

        rotation_result.storage_finalized =
            false;

        httplib::Request request =
            request_with_body(
                R"({"node_id":"node-01"})"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_node_credential_rotation(
                request,
                response,
                context
            );

        const json body =
            response_json(
                response
            );

        expect(
            response.status == 502 &&
            body["error"] ==
                "finalize_pending" &&
            body["retryable"] == true &&
            body["director_credential_committed"] == true &&
            body["storage_finalized"] == false,
            "FinalizePending should expose retryable lifecycle state without credentials"
        );

        expect(
            rotation_calls ==
                calls_before + 1,
            "FinalizePending should come from one service invocation"
        );
    }

    actor_fingerprint =
        "rotation-conflict-admin";

    {
        rotation_result.code =
            pqnas::cluster::
                NodeCredentialRotationServiceCode::
                    RotationInProgress;

        httplib::Request request =
            request_with_body(
                R"({"node_id":"node-01"})"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_node_credential_rotation(
                request,
                response,
                context
            );

        expect(
            response.status == 409,
            "concurrent credential rotation should return 409"
        );
    }

    actor_fingerprint =
        "rotation-invalid-admin";

    {
        const int calls_before =
            rotation_calls;

        httplib::Request request =
            request_with_body(
                R"({"node_id":"node-01","extra":true})"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_node_credential_rotation(
                request,
                response,
                context
            );

        expect(
            response.status == 400 &&
            rotation_calls ==
                calls_before,
            "rotation trigger must reject extra JSON fields before service"
        );
    }

    actor_fingerprint =
        "rotation-throw-admin";

    {
        const int calls_before =
            rotation_calls;

        rotation_callback_throws =
            true;

        httplib::Request request =
            request_with_body(
                R"({"node_id":"node-01"})"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_node_credential_rotation(
                request,
                response,
                context
            );

        rotation_callback_throws =
            false;

        expect(
            response.status == 500 &&
            rotation_calls ==
                calls_before + 1,
            "rotation service exception must fail admin trigger closed"
        );
    }

    // Unauthenticated requests must not reach same-origin validation, rate
    // limiting or the rotation service.
    actor_fingerprint =
        "rotation-auth-admin";

    {
        const int calls_before =
            rotation_calls;

        auth_allowed = false;
        origin_allowed = true;

        httplib::Request request =
            request_with_body(
                R"({"node_id":"node-01"})"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_node_credential_rotation(
                request,
                response,
                context
            );

        expect(
            response.status == 401 &&
            rotation_calls ==
                calls_before,
            "unauthenticated rotation trigger must stop before service"
        );

        auth_allowed = true;
    }

    actor_fingerprint =
        "rotation-origin-admin";

    {
        const int calls_before =
            rotation_calls;

        origin_allowed = false;

        httplib::Request request =
            request_with_body(
                R"({"node_id":"node-01"})"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_node_credential_rotation(
                request,
                response,
                context
            );

        expect(
            response.status == 403 &&
            rotation_calls ==
                calls_before,
            "cross-origin rotation trigger must stop before service"
        );

        origin_allowed = true;
    }

    // Security regression: authenticated administrative callers are bounded
    // without giving unauthenticated traffic a way to consume the budget.
    actor_fingerprint =
        "rotation-rate-admin";

    rotation_result.code =
        pqnas::cluster::
            NodeCredentialRotationServiceCode::Ok;

    rotation_result.director_credential_committed =
        true;

    rotation_result.storage_finalized =
        true;

    {
        const int calls_before =
            rotation_calls;

        for (std::size_t index = 0;
             index <
                 pqnas::cluster::
                     kAdminClusterNodeRotationRateLimit;
             ++index) {
            httplib::Request request =
                request_with_body(
                    R"({"node_id":"node-01"})"
                );

            httplib::Response response;

            pqnas::cluster::
                handle_admin_cluster_node_credential_rotation(
                    request,
                    response,
                    context
                );

            expect(
                response.status == 200,
                "rotation requests within admin rate limit should succeed"
            );
        }

        httplib::Request blocked_request =
            request_with_body(
                R"({"node_id":"node-01"})"
            );

        httplib::Response blocked_response;

        pqnas::cluster::
            handle_admin_cluster_node_credential_rotation(
                blocked_request,
                blocked_response,
                context
            );

        expect(
            blocked_response.status == 429 &&
            response_header(
                blocked_response,
                "Retry-After"
            ) != "",
            "rotation request above admin rate limit should return 429"
        );

        expect(
            rotation_calls ==
                calls_before +
                static_cast<int>(
                    pqnas::cluster::
                        kAdminClusterNodeRotationRateLimit
                ),
            "rate-limited rotation must not invoke service"
        );
    }

    actor_fingerprint =
        "admin-fingerprint";

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

    // Security regression: deletion cannot begin after rotation has installed
    // its busy marker under the shared placement coordination mutex.
    rotation_busy = true;

    {
        const int calls_before =
            deletion_calls;

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
            response_json(
                response
            );

        expect(
            response.status == 409 &&
            body["error"] == "conflict",
            "rotation-busy node delete should return 409"
        );

        expect(
            registry.get(
                "node-01"
            ).has_value(),
            "rotation-busy delete must preserve node"
        );

        expect(
            deletion_calls ==
                calls_before,
            "rotation-busy delete must not enter deletion lifecycle"
        );
    }

    rotation_busy = false;

    // Security regression: active provisioning must be surfaced as a
    // conflict and must not remove the configured Node.
    deletion_provisioning_active = true;

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
            response.status == 409,
            "active provisioning delete should return 409"
        );

        expect(
            registry.get("node-01").has_value(),
            "active provisioning conflict must preserve node"
        );
    }

    deletion_provisioning_active = false;

    // RT010 security regression: durable confirmed projection state is a
    // conflict even after authoritative assigned-user count reaches zero.
    deletion_confirmed_projection_active = true;

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
            response_json(
                response
            );

        expect(
            response.status == 409 &&
            body["error"] == "conflict",
            "confirmed projection node delete should return 409"
        );

        expect(
            registry.get(
                "node-01"
            ).has_value(),
            "confirmed projection conflict must preserve node"
        );
    }

    deletion_confirmed_projection_active = false;

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
            last_delete_revoked_at ==
                "2026-07-28T07:00:00Z",
            "delete lifecycle timestamp should be canonical"
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
