#include "routes/routes_admin_cluster_user_placement_preview.h"

#include "cluster/node_status.h"
#include "cluster/user_placement_preview_service.h"
#include "httplib.h"

#include <nlohmann/json.hpp>

#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

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

pqnas::cluster::NodeRegistryRecord make_node(
    const std::string& node_id
) {
    pqnas::cluster::NodeRegistryRecord node;
    node.node_id = node_id;
    node.enabled = true;
    node.health = "online";

    node.protocol_version =
        pqnas::cluster::
            kNodeStatusProtocolVersion;

    node.last_seen_at =
        "2026-07-28T14:00:00Z";

    node.node_reported_at =
        "2026-07-28T14:00:00Z";

    node.storage_total_bytes = 1000;
    node.storage_used_bytes = 400;
    node.storage_free_bytes = 600;
    node.storage_fstype = "ext4";

    return node;
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
    using UserSnapshot =
        pqnas::cluster::
            UserPlacementPreviewUserSnapshot;

    UserSnapshot user{
        "node-01",
        "business-backup"
    };

    bool user_exists = true;

    std::vector<
        pqnas::cluster::NodeRegistryRecord
    > nodes{
        make_node(
            "node-business-01"
        )
    };

    pqnas::cluster::
        UserPlacementPreviewServiceConfig
            service_config;

    service_config.get_user =
        [&](const std::string& fingerprint)
            -> std::optional<UserSnapshot> {
            if (!user_exists ||
                fingerprint !=
                    "user-fingerprint") {
                return std::nullopt;
            }

            return user;
        };

    service_config.node_snapshot =
        [&]() {
            return nodes;
        };

    service_config.get_policy =
        [&](const std::string& policy_id)
            -> std::optional<
                pqnas::cluster::
                    UserPlacementPolicy
            > {
            if (policy_id !=
                "business-backup") {
                return std::nullopt;
            }

            return make_policy();
        };

    service_config.assigned_user_counts =
        []() {
            return std::unordered_map<
                std::string,
                std::size_t
            >{
                {"node-business-01", 3}
            };
        };

    service_config.placement_mutex =
        std::make_shared<std::mutex>();

    auto service =
        std::make_shared<
            pqnas::cluster::
                UserPlacementPreviewService
        >(
            service_config
        );

    bool auth_allowed = true;
    bool origin_allowed = true;
    int auth_calls = 0;
    int origin_calls = 0;

    pqnas::cluster::
        AdminClusterUserPlacementPreviewRoutesContext
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
                        kAdminClusterUserPlacementPreviewBodyMaxBytes +
                    100,
                    'x'
                )
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_user_placement_preview(
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
    }

    {
        auth_allowed = true;
        origin_allowed = false;

        httplib::Request request =
            request_with_body(
                R"({"fingerprint":"user-fingerprint"})"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_user_placement_preview(
                request,
                response,
                context
            );

        expect(
            response.status == 403,
            "same-origin failure should block preview"
        );
    }

    {
        origin_allowed = true;

        httplib::Request request =
            request_with_body(
                std::string(
                    pqnas::cluster::
                        kAdminClusterUserPlacementPreviewBodyMaxBytes +
                    1,
                    'x'
                )
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_user_placement_preview(
                request,
                response,
                context
            );

        expect(
            response.status == 413,
            "oversized preview request should return 413"
        );
    }

    {
        httplib::Request request =
            request_with_body(
                R"({
                    "fingerprint":"user-fingerprint",
                    "extra":true
                })"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_user_placement_preview(
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
                R"({"fingerprint":1})"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_user_placement_preview(
                request,
                response,
                context
            );

        expect(
            response.status == 400,
            "non-string fingerprint should return 400"
        );
    }

    {
        httplib::Request request =
            request_with_body(
                R"({"fingerprint":"user-fingerprint"})"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_user_placement_preview(
                request,
                response,
                context
            );

        const json body =
            response_json(response);

        expect(
            response.status == 200 &&
            body["ok"] == true &&
            body["recommendation_available"] ==
                true &&
            body["placement_policy_id"] ==
                "business-backup" &&
            body["current_home_node"] ==
                "node-01" &&
            body["recommended_home_node"] ==
                "node-business-01" &&
            body["current_home_node_matches"] ==
                false &&
            body["selected_scope"] ==
                "policy",
            "valid preview should expose policy recommendation"
        );

        expect(
            body["assigned_users"] == 3 &&
            body["considered_nodes"] == 1 &&
            body["eligible_nodes"] == 1,
            "preview should expose aggregate selector data"
        );

        expect(
            response.headers.find(
                "Cache-Control"
            ) != response.headers.end() &&
            response.headers.find(
                "Cache-Control"
            )->second == "no-store",
            "preview response should disable caching"
        );

        expect(
            user.home_node == "node-01" &&
            user.placement_policy_id ==
                "business-backup",
            "preview route must not mutate placement state"
        );
    }

    {
        nodes[0].health = "offline";

        httplib::Request request =
            request_with_body(
                R"({"fingerprint":"user-fingerprint"})"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_user_placement_preview(
                request,
                response,
                context
            );

        const json body =
            response_json(response);

        expect(
            response.status == 200 &&
            body["ok"] == true &&
            body["recommendation_available"] ==
                false &&
            body["reason_code"] ==
                "policy_target_unavailable" &&
            body["rejected"]["not_online"] ==
                1,
            "unavailable target should return diagnostic preview"
        );

        nodes[0].health = "online";
    }

    {
        user.placement_policy_id =
            "missing-policy";

        httplib::Request request =
            request_with_body(
                R"({"fingerprint":"user-fingerprint"})"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_user_placement_preview(
                request,
                response,
                context
            );

        const json body =
            response_json(response);

        expect(
            response.status == 409 &&
            body["reason_code"] ==
                "policy_not_found",
            "missing assigned policy should return conflict"
        );

        user.placement_policy_id =
            "business-backup";
    }

    {
        user_exists = false;

        httplib::Request request =
            request_with_body(
                R"({"fingerprint":"user-fingerprint"})"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_user_placement_preview(
                request,
                response,
                context
            );

        expect(
            response.status == 404,
            "unknown user should return 404"
        );
    }

    if (failures != 0) {
        std::cerr
            << failures
            << " admin placement preview route test(s) failed\n";
        return 1;
    }

    std::cout
        << "OK: admin cluster user placement preview route tests passed\n";

    return 0;
}
