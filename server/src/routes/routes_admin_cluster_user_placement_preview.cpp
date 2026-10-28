#include "routes_admin_cluster_user_placement_preview.h"

#include "cluster/user_placement_preview_service.h"
#include "httplib.h"

#include <nlohmann/json.hpp>

#include <initializer_list>
#include <string>

namespace pqnas::cluster {
namespace {

using json = nlohmann::json;

bool context_valid(
    const AdminClusterUserPlacementPreviewRoutesContext&
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
    const AdminClusterUserPlacementPreviewRoutesContext&
        context,
    httplib::Response& response,
    int status,
    const json& body
) {
    // Security: placement previews expose internal account and node topology
    // decisions and must never be cached.
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
    if (error_code == "invalid_fingerprint") {
        return 400;
    }

    if (error_code == "user_not_found") {
        return 404;
    }

    if (error_code == "policy_not_found") {
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

    if (error_code == "user_not_found") {
        return "user not found";
    }

    if (error_code == "policy_not_found") {
        return "assigned placement policy was not found";
    }

    // Security: hide callback exceptions and internal registry state.
    return "failed to evaluate user placement";
}

} // namespace

void handle_admin_cluster_user_placement_preview(
    const httplib::Request& request,
    httplib::Response& response,
    const AdminClusterUserPlacementPreviewRoutesContext&
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
                    "admin cluster placement preview context incomplete"
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
        kAdminClusterUserPlacementPreviewBodyMaxBytes) {
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
                    kAdminClusterUserPlacementPreviewBodyMaxBytes
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
            {"fingerprint"}
        ) ||
        body.size() != 1) {
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

    if (fingerprint_iterator == body.end() ||
        !fingerprint_iterator->is_string()) {
        reply_json_local(
            context,
            response,
            400,
            json{
                {"ok", false},
                {"error", "bad_request"},
                {
                    "message",
                    "fingerprint must be a string"
                }
            }
        );
        return;
    }

    const std::string fingerprint =
        fingerprint_iterator->
            get<std::string>();

    const UserPlacementPreviewResult result =
        context.service->preview(
            fingerprint
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

    json rejected =
        json::object();

    for (const auto& entry :
         result.rejected) {
        rejected[entry.first] =
            entry.second;
    }

    const std::string reason_code =
        result.recommendation_available
            ? "placement_recommended"
            : (
                result.error_code.empty()
                    ? "no_recommendation"
                    : result.error_code
            );

    reply_json_local(
        context,
        response,
        200,
        json{
            {"ok", true},
            {
                "recommendation_available",
                result.recommendation_available
            },
            {"reason_code", reason_code},
            {"fingerprint", fingerprint},
            {
                "placement_policy_id",
                result.placement_policy_id
            },
            {
                "policy_source",
                result.policy_source
            },
            {
                "policy_mode",
                result.policy_mode
            },
            {
                "policy_fallback",
                result.policy_fallback
            },
            {
                "selected_scope",
                result.selected_scope
            },
            {
                "used_fallback",
                result.used_fallback
            },
            {
                "current_home_node",
                result.current_home_node
            },
            {
                "recommended_home_node",
                result.recommended_home_node
            },
            {
                "current_home_node_matches",
                result.current_home_node_matches
            },
            {
                "selection_reason",
                result.selection_reason
            },
            {
                "considered_nodes",
                result.considered_nodes
            },
            {
                "eligible_nodes",
                result.eligible_nodes
            },
            {
                "assigned_users",
                result.assigned_users
            },
            {
                "storage_total_bytes",
                result.storage_total_bytes
            },
            {
                "storage_free_bytes",
                result.storage_free_bytes
            },
            {"rejected", rejected}
        }
    );
}

void register_admin_cluster_user_placement_preview_routes(
    httplib::Server& server,
    const AdminClusterUserPlacementPreviewRoutesContext&
        context
) {
    const AdminClusterUserPlacementPreviewRoutesContext
        captured =
            context;

    server.Post(
        "/api/v4/admin/cluster/users/placement-preview",
        [captured](
            const httplib::Request& request,
            httplib::Response& response
        ) {
            handle_admin_cluster_user_placement_preview(
                request,
                response,
                captured
            );
        }
    );
}

} // namespace pqnas::cluster
