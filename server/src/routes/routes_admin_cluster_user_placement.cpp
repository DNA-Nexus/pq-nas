#include "routes_admin_cluster_user_placement.h"

#include "cluster/user_placement_service.h"
#include "httplib.h"

#include <nlohmann/json.hpp>

#include <initializer_list>
#include <string>

namespace pqnas::cluster {
namespace {

using json = nlohmann::json;

bool context_valid(
    const AdminClusterUserPlacementRoutesContext& context
) {
    return
        context.service &&
        static_cast<bool>(context.require_admin_auth) &&
        static_cast<bool>(context.require_same_origin) &&
        static_cast<bool>(context.reply_json);
}

void reply_json_local(
    const AdminClusterUserPlacementRoutesContext& context,
    httplib::Response& response,
    int status,
    const json& body
) {
    // Security: placement responses can reveal internal account topology and
    // must not be retained by intermediary or browser caches.
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
        error_code == "invalid_node_id" ||
        error_code == "invalid_expected_home_node") {
        return 400;
    }

    if (error_code == "user_not_found" ||
        error_code == "node_not_found") {
        return 404;
    }

    if (error_code == "node_disabled" ||
        error_code == "placement_stale") {
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

    if (error_code == "invalid_node_id") {
        return "invalid node ID";
    }

    if (error_code == "user_not_found") {
        return "user not found";
    }

    if (error_code == "node_not_found") {
        return "node not found";
    }

    if (error_code == "node_disabled") {
        return "node is disabled";
    }

    if (error_code == "invalid_expected_home_node") {
        return "invalid expected home node";
    }

    if (error_code == "placement_stale") {
        return "user placement changed";
    }

    // Security: never expose persistence paths or raw filesystem failures.
    return "failed to update user home node";
}

} // namespace

void handle_admin_cluster_user_home_node_assign(
    const httplib::Request& request,
    httplib::Response& response,
    const AdminClusterUserPlacementRoutesContext& context
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
                    "admin cluster user placement route context incomplete"
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
        kAdminClusterUserPlacementBodyMaxBytes) {
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
                    kAdminClusterUserPlacementBodyMaxBytes
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
                "node_id"
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

    const auto node_id_iterator =
        body.find("node_id");

    if (fingerprint_iterator == body.end() ||
        !fingerprint_iterator->is_string() ||
        node_id_iterator == body.end() ||
        !node_id_iterator->is_string()) {
        reply_json_local(
            context,
            response,
            400,
            json{
                {"ok", false},
                {"error", "bad_request"},
                {
                    "message",
                    "fingerprint and node_id must be strings"
                }
            }
        );
        return;
    }

    const std::string fingerprint =
        fingerprint_iterator->get<std::string>();

    const std::string node_id =
        node_id_iterator->get<std::string>();

    const UserPlacementResult result =
        context.service->assign(
            fingerprint,
            node_id,
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
            {"home_node", result.home_node}
        }
    );
}

void handle_admin_cluster_user_home_node_unassign(
    const httplib::Request& request,
    httplib::Response& response,
    const AdminClusterUserPlacementRoutesContext& context
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
                    "admin cluster user placement route context incomplete"
                }
            }
        );
        return;
    }

    std::string actor_fingerprint;

    // Security: authenticate before body parsing so malformed or oversized
    // requests cannot bypass the normal admin trust boundary.
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
        kAdminClusterUserPlacementBodyMaxBytes) {
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
                    kAdminClusterUserPlacementBodyMaxBytes
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
                "expected_home_node"
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

    const auto expected_home_node_iterator =
        body.find("expected_home_node");

    if (fingerprint_iterator == body.end() ||
        !fingerprint_iterator->is_string() ||
        expected_home_node_iterator == body.end() ||
        !expected_home_node_iterator->is_string()) {
        reply_json_local(
            context,
            response,
            400,
            json{
                {"ok", false},
                {"error", "bad_request"},
                {
                    "message",
                    "fingerprint and expected_home_node must be strings"
                }
            }
        );
        return;
    }

    const std::string fingerprint =
        fingerprint_iterator->get<std::string>();

    const std::string expected_home_node =
        expected_home_node_iterator->
            get<std::string>();

    const UserPlacementResult result =
        context.service->unassign(
            fingerprint,
            expected_home_node,
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
            {"home_node", result.home_node}
        }
    );
}


void register_admin_cluster_user_placement_routes(
    httplib::Server& server,
    const AdminClusterUserPlacementRoutesContext& context
) {
    const AdminClusterUserPlacementRoutesContext captured =
        context;

    server.Post(
        "/api/v4/admin/cluster/users/home-node",
        [captured](
            const httplib::Request& request,
            httplib::Response& response
        ) {
            handle_admin_cluster_user_home_node_assign(
                request,
                response,
                captured
            );
        }
    );

    server.Delete(
        "/api/v4/admin/cluster/users/home-node",
        [captured](
            const httplib::Request& request,
            httplib::Response& response
        ) {
            handle_admin_cluster_user_home_node_unassign(
                request,
                response,
                captured
            );
        }
    );
}

} // namespace pqnas::cluster
