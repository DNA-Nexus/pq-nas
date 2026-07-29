#include "routes_admin_cluster_user_placement_apply.h"

#include "cluster/user_placement_recommendation_apply_service.h"
#include "httplib.h"

#include <nlohmann/json.hpp>

#include <initializer_list>
#include <string>

namespace pqnas::cluster {
namespace {

using json = nlohmann::json;

bool context_valid(
    const AdminClusterUserPlacementApplyRoutesContext&
        context
) {
    return
        context.service &&
        static_cast<bool>(
            context.require_admin_auth
        ) &&
        static_cast<bool>(
            context.require_same_origin
        ) &&
        static_cast<bool>(
            context.reply_json
        );
}

void reply_json_local(
    const AdminClusterUserPlacementApplyRoutesContext&
        context,
    httplib::Response& response,
    int status,
    const json& body
) {
    // Security: placement state and topology decisions must not be cached.
    response.set_header(
        "Cache-Control",
        "no-store"
    );

    if (context.reply_json) {
        context.reply_json(
            response,
            status,
            body.dump()
        );
        return;
    }

    response.status = status;
    response.set_content(
        body.dump(),
        "application/json; charset=utf-8"
    );
}

bool has_only_keys(
    const json& object,
    std::initializer_list<const char*> allowed
) {
    if (!object.is_object()) {
        return false;
    }

    for (auto iterator = object.begin();
         iterator != object.end();
         ++iterator) {
        bool known = false;

        for (const char* key : allowed) {
            if (iterator.key() == key) {
                known = true;
                break;
            }
        }

        if (!known) {
            return false;
        }
    }

    return true;
}

int status_for_error(
    const std::string& error_code
) {
    if (error_code == "invalid_fingerprint" ||
        error_code == "invalid_expected_home_node" ||
        error_code == "invalid_expected_policy_id" ||
        error_code ==
            "invalid_expected_recommended_home_node") {
        return 400;
    }

    if (error_code == "user_not_found") {
        return 404;
    }

    if (error_code ==
        "placement_preview_stale") {
        return 409;
    }

    return 500;
}

std::string public_error_name(
    int status
) {
    if (status == 400) {
        return "bad_request";
    }

    if (status == 404) {
        return "not_found";
    }

    if (status == 409) {
        return "conflict";
    }

    return "server_error";
}

std::string public_error_message(
    const std::string& error_code
) {
    if (error_code == "invalid_fingerprint") {
        return "invalid fingerprint";
    }

    if (error_code ==
        "invalid_expected_home_node") {
        return "invalid expected home node";
    }

    if (error_code ==
        "invalid_expected_policy_id") {
        return "invalid expected placement policy";
    }

    if (error_code ==
        "invalid_expected_recommended_home_node") {
        return "invalid expected recommendation";
    }

    if (error_code == "user_not_found") {
        return "user not found";
    }

    if (error_code ==
        "placement_preview_stale") {
        return "placement preview is stale";
    }

    // Security: hide resolver callbacks, filesystem paths and raw failures.
    return "failed to apply user placement recommendation";
}

} // namespace

void handle_admin_cluster_user_placement_apply(
    const httplib::Request& request,
    httplib::Response& response,
    const AdminClusterUserPlacementApplyRoutesContext&
        context
) {
    if (!context_valid(context)) {
        reply_json_local(
            context,
            response,
            500,
            json{
                {"ok", false},
                {"error", "server_error"},
                {
                    "message",
                    "admin placement apply context incomplete"
                }
            }
        );
        return;
    }

    std::string actor_fingerprint;

    // Security: authentication and origin checks precede body processing.
    if (!context.require_admin_auth(
            request,
            response,
            &actor_fingerprint)) {
        return;
    }

    if (!context.require_same_origin(
            request,
            response)) {
        return;
    }

    if (request.body.size() >
        kAdminClusterUserPlacementApplyBodyMaxBytes) {
        reply_json_local(
            context,
            response,
            413,
            json{
                {"ok", false},
                {"error", "payload_too_large"},
                {"message", "request body is too large"},
                {
                    "max_bytes",
                    kAdminClusterUserPlacementApplyBodyMaxBytes
                }
            }
        );
        return;
    }

    const json body =
        json::parse(
            request.body,
            nullptr,
            false
        );

    if (body.is_discarded() ||
        !has_only_keys(
            body,
            {
                "fingerprint",
                "expected_current_home_node",
                "expected_placement_policy_id",
                "expected_recommended_home_node"
            }) ||
        body.size() != 4) {
        reply_json_local(
            context,
            response,
            400,
            json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "invalid request body"}
            }
        );
        return;
    }

    for (const char* key : {
             "fingerprint",
             "expected_current_home_node",
             "expected_placement_policy_id",
             "expected_recommended_home_node"
         }) {
        const auto iterator =
            body.find(key);

        if (iterator == body.end() ||
            !iterator->is_string()) {
            reply_json_local(
                context,
                response,
                400,
                json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {
                        "message",
                        "placement apply fields must be strings"
                    }
                }
            );
            return;
        }
    }

    const std::string fingerprint =
        body["fingerprint"].
            get<std::string>();

    const std::string expected_current_home_node =
        body["expected_current_home_node"].
            get<std::string>();

    const std::string expected_placement_policy_id =
        body["expected_placement_policy_id"].
            get<std::string>();

    const std::string
        expected_recommended_home_node =
            body["expected_recommended_home_node"].
                get<std::string>();

    const UserPlacementRecommendationApplyResult
        result =
            context.service->apply(
                fingerprint,
                expected_current_home_node,
                expected_placement_policy_id,
                expected_recommended_home_node,
                actor_fingerprint
            );

    if (!result.ok) {
        const int status =
            status_for_error(
                result.error_code
            );

        reply_json_local(
            context,
            response,
            status,
            json{
                {"ok", false},
                {
                    "error",
                    public_error_name(status)
                },
                {
                    "message",
                    public_error_message(
                        result.error_code
                    )
                },
                {
                    "reason_code",
                    result.error_code.empty()
                        ? "service_unavailable"
                        : result.error_code
                }
            }
        );
        return;
    }

    reply_json_local(
        context,
        response,
        200,
        json{
            {"ok", true},
            {"changed", result.changed},
            {"fingerprint", fingerprint},
            {
                "previous_home_node",
                result.previous_home_node
            },
            {"home_node", result.home_node},
            {
                "placement_policy_id",
                result.placement_policy_id
            }
        }
    );
}

void register_admin_cluster_user_placement_apply_routes(
    httplib::Server& server,
    const AdminClusterUserPlacementApplyRoutesContext&
        context
) {
    const AdminClusterUserPlacementApplyRoutesContext
        captured =
            context;

    server.Post(
        "/api/v4/admin/cluster/users/placement-apply",
        [captured](
            const httplib::Request& request,
            httplib::Response& response
        ) {
            handle_admin_cluster_user_placement_apply(
                request,
                response,
                captured
            );
        }
    );
}

} // namespace pqnas::cluster
