#include "routes/routes_admin_cluster_user_placement_apply.h"

#include "cluster/node_status.h"
#include "cluster/user_placement_preview_service.h"
#include "cluster/user_placement_recommendation_apply_service.h"
#include "httplib.h"
#include "users_registry.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
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
            "pqnas-placement-apply-route-XXXXXX"
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

    return result
        ? fs::path(result)
        : fs::path{};
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

pqnas::cluster::NodeRegistryRecord make_node() {
    pqnas::cluster::NodeRegistryRecord node;
    node.node_id = "node-business-01";
    node.enabled = true;
    node.health = "online";

    // RT-007: normal placement fixtures represent Director-approved capacity.
    node.capacity_ceiling_bytes = 1000;

    node.protocol_version =
        pqnas::cluster::
            kNodeStatusProtocolVersion;

    node.last_seen_at =
        "2026-07-29T06:00:00Z";

    node.node_reported_at =
        "2026-07-29T06:00:00Z";

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

    pqnas::UsersRegistry users;
    pqnas::UserRec user;

    user.fingerprint =
        "user-fingerprint";

    user.role = "user";
    user.status = "enabled";
    user.storage_state = "unallocated";
    user.app_prefs_json = "{}";
    user.home_node = "node-01";

    user.placement_policy_id =
        "business-backup";

    expect(
        users.upsert(user) &&
        users.save(users_path.string()),
        "create route test user"
    );

    std::vector<
        pqnas::cluster::NodeRegistryRecord
    > nodes{
        make_node()
    };

    pqnas::cluster::
        UserPlacementPreviewServiceConfig
            preview_config;

    preview_config.get_user =
        [&](const std::string& fingerprint)
            -> std::optional<
                pqnas::cluster::
                    UserPlacementPreviewUserSnapshot
            > {
            const auto current =
                users.get(
                    fingerprint
                );

            if (!current.has_value()) {
                return std::nullopt;
            }

            return
                pqnas::cluster::
                    UserPlacementPreviewUserSnapshot{
                        current->home_node,
                        current->placement_policy_id
                    };
        };

    preview_config.node_snapshot =
        [&]() {
            return nodes;
        };

    preview_config.get_policy =
        [](const std::string& policy_id)
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

    preview_config.assigned_user_counts =
        []() {
            return std::unordered_map<
                std::string,
                std::size_t
            >{
                {"node-business-01", 1}
            };
        };

    preview_config.placement_mutex =
        std::make_shared<std::mutex>();

    auto preview_service =
        std::make_shared<
            pqnas::cluster::
                UserPlacementPreviewService
        >(
            preview_config
        );

    pqnas::cluster::
        UserPlacementRecommendationApplyServiceConfig
            apply_config;

    apply_config.users = &users;
    apply_config.preview_service =
        preview_service;

    apply_config.users_path =
        users_path.string();

    apply_config.audit_emit =
        [](
            const std::string&,
            const std::string&,
            const std::map<
                std::string,
                std::string
            >&
        ) {};

    auto apply_service =
        std::make_shared<
            pqnas::cluster::
                UserPlacementRecommendationApplyService
        >(
            apply_config
        );

    bool auth_allowed = true;
    bool origin_allowed = true;
    int auth_calls = 0;
    int origin_calls = 0;

    pqnas::cluster::
        AdminClusterUserPlacementApplyRoutesContext
            context;

    context.service =
        apply_service;

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

    const std::string valid_body =
        R"({
            "fingerprint":"user-fingerprint",
            "expected_current_home_node":"node-01",
            "expected_placement_policy_id":"business-backup",
            "expected_recommended_home_node":"node-business-01"
        })";

    {
        auth_allowed = false;
        origin_allowed = true;
        auth_calls = 0;
        origin_calls = 0;

        httplib::Request request =
            request_with_body(
                std::string(
                    pqnas::cluster::
                        kAdminClusterUserPlacementApplyBodyMaxBytes +
                    100,
                    'x'
                )
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_user_placement_apply(
                request,
                response,
                context
            );

        expect(
            response.status == 401 &&
            auth_calls == 1 &&
            origin_calls == 0,
            "authentication must precede apply body parsing"
        );
    }

    {
        auth_allowed = true;
        origin_allowed = false;

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_user_placement_apply(
                request_with_body(
                    valid_body
                ),
                response,
                context
            );

        expect(
            response.status == 403,
            "same-origin failure should block apply"
        );
    }

    {
        origin_allowed = true;

        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_user_placement_apply(
                request_with_body(
                    std::string(
                        pqnas::cluster::
                            kAdminClusterUserPlacementApplyBodyMaxBytes +
                        1,
                        'x'
                    )
                ),
                response,
                context
            );

        expect(
            response.status == 413,
            "oversized apply body should return 413"
        );
    }

    {
        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_user_placement_apply(
                request_with_body(
                    R"({
                        "fingerprint":"user-fingerprint",
                        "expected_current_home_node":"node-01",
                        "expected_placement_policy_id":"business-backup",
                        "expected_recommended_home_node":"node-business-01",
                        "extra":true
                    })"
                ),
                response,
                context
            );

        expect(
            response.status == 400,
            "unknown apply field should return 400"
        );
    }

    {
        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_user_placement_apply(
                request_with_body(
                    R"({
                        "fingerprint":"user-fingerprint",
                        "expected_current_home_node":"node-01",
                        "expected_placement_policy_id":"business-backup",
                        "expected_recommended_home_node":42
                    })"
                ),
                response,
                context
            );

        expect(
            response.status == 400,
            "non-string apply field should return 400"
        );
    }

    {
        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_user_placement_apply(
                request_with_body(
                    valid_body
                ),
                response,
                context
            );

        const json body =
            response_json(response);

        expect(
            response.status == 200 &&
            body["ok"] == true &&
            body["changed"] == true &&
            body["previous_home_node"] ==
                "node-01" &&
            body["home_node"] ==
                "node-business-01" &&
            body["placement_policy_id"] ==
                "business-backup",
            "valid apply route should persist recommendation"
        );

        expect(
            response.headers.find(
                "Cache-Control"
            ) != response.headers.end() &&
            response.headers.find(
                "Cache-Control"
            )->second == "no-store",
            "apply response should disable caching"
        );
    }

    {
        httplib::Response response;

        pqnas::cluster::
            handle_admin_cluster_user_placement_apply(
                request_with_body(
                    valid_body
                ),
                response,
                context
            );

        const json body =
            response_json(response);

        expect(
            response.status == 409 &&
            body["error"] == "conflict" &&
            body["reason_code"] ==
                "placement_preview_stale",
            "reusing stale preview should return 409"
        );

        expect(
            body.find(
                "recommended_home_node"
            ) == body.end() &&
            body.find(
                "current_home_node"
            ) == body.end(),
            "stale response must not silently return a replacement preview"
        );
    }

    cleanup();

    if (failures != 0) {
        std::cerr
            << failures
            << " placement apply route test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: admin cluster user placement apply route tests passed\n";

    return 0;
}
