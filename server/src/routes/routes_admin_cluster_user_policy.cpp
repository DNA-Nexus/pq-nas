#include "routes_admin_cluster_user_policy.h"

#include "cluster/user_placement_policy_assignment_service.h"
#include "httplib.h"

#include <nlohmann/json.hpp>

#include <initializer_list>
#include <string>

namespace pqnas::cluster {
namespace {

using json = nlohmann::json;

bool context_valid(
    const AdminClusterUserPolicyRoutesContext&
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
    const AdminClusterUserPolicyRoutesContext&
        context,
    httplib::Response& response,
    int status,
    const json& body
) {
    // Security: policy assignment exposes internal account control-plane state
    // and must not be retained by intermediary or browser caches.
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
        error_code == "invalid_policy_id") {
        return 400;
    }

    if (error_code == "user_not_found" ||
        error_code == "policy_not_found") {
        return 404;
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

    return "server_error";
}

std::string public_error_message(
    const std::string& error_code
) {
    if (error_code == "invalid_fingerprint") {
        return "invalid fingerprint";
    }

    if (error_code == "invalid_policy_id") {
        return "invalid placement policy ID";
    }

    if (error_code == "user_not_found") {
        return "user not found";
    }

    if (error_code == "policy_not_found") {
        return "placement policy not found";
    }

    // Security: never expose users.json paths or raw persistence failures.
    return "failed to update user placement policy";
}

} // namespace

void handle_admin_cluster_user_policy_assign(
    const httplib::Request& request,
    httplib::Response& response,
    const AdminClusterUserPolicyRoutesContext&
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
                    "admin cluster user policy route context incomplete"
                }
            }
        );
        return;
    }

    std::string actor_fingerprint;

    // Security: authenticate before parsing or otherwise processing the body.
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
        kAdminClusterUserPolicyBodyMaxBytes) {
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
                    kAdminClusterUserPolicyBodyMaxBytes
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
                "policy_id"
            }) ||
        body.size() != 2) {
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

    const auto fingerprint_iterator =
        body.find("fingerprint");

    const auto policy_id_iterator =
        body.find("policy_id");

    if (fingerprint_iterator == body.end() ||
        !fingerprint_iterator->is_string() ||
        policy_id_iterator == body.end() ||
        !policy_id_iterator->is_string()) {
        reply_json_local(
            context,
            response,
            400,
            json{
                {"ok", false},
                {"error", "bad_request"},
                {
                    "message",
                    "fingerprint and policy_id must be strings"
                }
            }
        );
        return;
    }

    const std::string fingerprint =
        fingerprint_iterator->
            get<std::string>();

    const std::string policy_id =
        policy_id_iterator->
            get<std::string>();

    const UserPlacementPolicyAssignmentResult
        result =
            context.service->assign(
                fingerprint,
                policy_id,
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
                "previous_policy_id",
                result.previous_policy_id
            },
            {
                "placement_policy_id",
                result.placement_policy_id
            }
        }
    );
}

void register_admin_cluster_user_policy_routes(
    httplib::Server& server,
    const AdminClusterUserPolicyRoutesContext&
        context
) {
    const AdminClusterUserPolicyRoutesContext captured =
        context;

    server.Post(
        "/api/v4/admin/cluster/users/placement-policy",
        [captured](
            const httplib::Request& request,
            httplib::Response& response
        ) {
            handle_admin_cluster_user_policy_assign(
                request,
                response,
                captured
            );
        }
    );
}

} // namespace pqnas::cluster
