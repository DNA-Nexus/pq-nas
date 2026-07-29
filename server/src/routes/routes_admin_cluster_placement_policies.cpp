#include "routes_admin_cluster_placement_policies.h"

#include "audit_log.h"
#include "cluster/user_placement_policy.h"
#include "cluster/user_placement_policy_registry.h"
#include "httplib.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace pqnas::cluster {
namespace {

using json = nlohmann::json;

std::mutex g_cluster_policy_mutation_mutex;

bool context_ok(
    const AdminClusterPlacementPolicyRoutesContext&
        context
) {
    return
        context.registry &&
        context.placement_mutex &&
        static_cast<bool>(
            context.assigned_user_counts
        ) &&
        static_cast<bool>(
            context.require_admin_auth
        ) &&
        static_cast<bool>(
            context.require_same_origin
        ) &&
        static_cast<bool>(
            context.reply_json
        ) &&
        static_cast<bool>(
            context.now_iso_utc
        ) &&
        static_cast<bool>(
            context.audit_append
        );
}

void reply_json_local(
    const AdminClusterPlacementPolicyRoutesContext&
        context,
    httplib::Response& response,
    int status,
    const json& body
) {
    // Security: policy definitions expose internal control-plane topology and
    // must not be retained by browser or intermediary caches.
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
        bool accepted = false;

        for (const char* key : allowed) {
            if (iterator.key() == key) {
                accepted = true;
                break;
            }
        }

        if (!accepted) {
            return false;
        }
    }

    return true;
}

bool parse_json_object(
    const AdminClusterPlacementPolicyRoutesContext&
        context,
    const httplib::Request& request,
    httplib::Response& response,
    json* output
) {
    if (!output) {
        reply_json_local(
            context,
            response,
            500,
            json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "request parser unavailable"}
            }
        );
        return false;
    }

    if (request.body.size() >
        kAdminClusterPlacementPolicyBodyMaxBytes) {
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
                    kAdminClusterPlacementPolicyBodyMaxBytes
                }
            }
        );
        return false;
    }

    *output =
        json::parse(
            request.body,
            nullptr,
            false
        );

    if (output->is_discarded() ||
        !output->is_object()) {
        reply_json_local(
            context,
            response,
            400,
            json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "invalid json"}
            }
        );
        return false;
    }

    return true;
}

bool parse_mode(
    const std::string& value,
    UserPlacementPolicyMode* mode
) {
    if (!mode) {
        return false;
    }

    if (value == "automatic") {
        *mode =
            UserPlacementPolicyMode::Automatic;
        return true;
    }

    if (value == "pinned") {
        *mode =
            UserPlacementPolicyMode::Pinned;
        return true;
    }

    if (value == "allowed_nodes") {
        *mode =
            UserPlacementPolicyMode::AllowedNodes;
        return true;
    }

    return false;
}

bool parse_fallback(
    const std::string& value,
    UserPlacementPolicyFallback* fallback
) {
    if (!fallback) {
        return false;
    }

    if (value == "deny") {
        *fallback =
            UserPlacementPolicyFallback::Deny;
        return true;
    }

    if (value == "automatic") {
        *fallback =
            UserPlacementPolicyFallback::Automatic;
        return true;
    }

    return false;
}

bool parse_policy_body(
    const json& body,
    UserPlacementPolicy* output,
    std::string* reason_code
) {
    if (reason_code) {
        reason_code->clear();
    }

    if (!output ||
        !has_only_keys(
            body,
            {
                "policy_id",
                "mode",
                "fallback",
                "node_ids"
            }) ||
        body.size() != 4) {
        if (reason_code) {
            *reason_code =
                "invalid_request_body";
        }
        return false;
    }

    const auto policy_id_iterator =
        body.find("policy_id");

    const auto mode_iterator =
        body.find("mode");

    const auto fallback_iterator =
        body.find("fallback");

    const auto nodes_iterator =
        body.find("node_ids");

    if (policy_id_iterator == body.end() ||
        !policy_id_iterator->is_string() ||
        mode_iterator == body.end() ||
        !mode_iterator->is_string() ||
        fallback_iterator == body.end() ||
        !fallback_iterator->is_string() ||
        nodes_iterator == body.end() ||
        !nodes_iterator->is_array()) {
        if (reason_code) {
            *reason_code =
                "invalid_policy_fields";
        }
        return false;
    }

    UserPlacementPolicy policy;

    policy.policy_id =
        policy_id_iterator->
            get<std::string>();

    if (!parse_mode(
            mode_iterator->get<std::string>(),
            &policy.mode)) {
        if (reason_code) {
            *reason_code =
                "invalid_policy_mode";
        }
        return false;
    }

    if (!parse_fallback(
            fallback_iterator->get<std::string>(),
            &policy.fallback)) {
        if (reason_code) {
            *reason_code =
                "invalid_policy_fallback";
        }
        return false;
    }

    if (nodes_iterator->size() >
        kUserPlacementPolicyMaxNodeCount) {
        if (reason_code) {
            *reason_code =
                "too_many_policy_nodes";
        }
        return false;
    }

    policy.node_ids.reserve(
        nodes_iterator->size()
    );

    for (const json& node_id :
         *nodes_iterator) {
        if (!node_id.is_string()) {
            if (reason_code) {
                *reason_code =
                    "invalid_policy_node_id";
            }
            return false;
        }

        policy.node_ids.push_back(
            node_id.get<std::string>()
        );
    }

    std::string validation_error;

    if (!validate_user_placement_policy(
            policy,
            &validation_error)) {
        if (reason_code) {
            *reason_code =
                validation_error.empty()
                    ? "invalid_policy"
                    : validation_error;
        }
        return false;
    }

    std::sort(
        policy.node_ids.begin(),
        policy.node_ids.end()
    );

    *output =
        std::move(policy);

    return true;
}

bool policies_equal(
    const UserPlacementPolicy& left,
    const UserPlacementPolicy& right
) {
    return
        left.policy_id ==
            right.policy_id &&
        left.mode ==
            right.mode &&
        left.fallback ==
            right.fallback &&
        left.node_ids ==
            right.node_ids;
}

json policy_to_json(
    const UserPlacementPolicy& policy,
    std::size_t assigned_users
) {
    return json{
        {"policy_id", policy.policy_id},
        {
            "mode",
            user_placement_policy_mode_name(
                policy.mode
            )
        },
        {
            "fallback",
            user_placement_policy_fallback_name(
                policy.fallback
            )
        },
        {"node_ids", policy.node_ids},
        {"assigned_users", assigned_users}
    };
}

std::string audit_ip(
    const httplib::Request& request
) {
    if (request.remote_addr.empty()) {
        return "?";
    }

    if (request.remote_addr.size() <= 128) {
        return request.remote_addr;
    }

    return request.remote_addr.substr(
        0,
        128
    );
}

void audit_mutation(
    const AdminClusterPlacementPolicyRoutesContext&
        context,
    const httplib::Request& request,
    const std::string& actor_fingerprint,
    const std::string& event,
    const std::string& policy_id,
    const std::string& outcome,
    const std::string& reason_code
) {
    if (!context.audit_append) {
        return;
    }

    pqnas::AuditEvent audit;
    audit.event = event;
    audit.outcome = outcome;

    audit.f["actor_fp"] =
        actor_fingerprint;

    audit.f["policy_id"] =
        policy_id;

    audit.f["reason_code"] =
        reason_code;

    audit.f["ip"] =
        audit_ip(request);

    audit.f["ts"] =
        context.now_iso_utc();

    // Security: never copy node sets, request bodies, persistence paths or raw
    // registry errors into the audit log.
    try {
        context.audit_append(
            audit
        );
    } catch (...) {
        // Registry persistence must not be rolled back by audit failure.
    }
}

bool authenticate_admin(
    const AdminClusterPlacementPolicyRoutesContext&
        context,
    const httplib::Request& request,
    httplib::Response& response,
    std::string* actor_fingerprint
) {
    response.set_header(
        "Cache-Control",
        "no-store"
    );

    if (!context_ok(context)) {
        reply_json_local(
            context,
            response,
            500,
            json{
                {"ok", false},
                {"error", "server_error"},
                {
                    "message",
                    "admin placement policy route context incomplete"
                }
            }
        );
        return false;
    }

    return context.require_admin_auth(
        request,
        response,
        actor_fingerprint
    );
}

bool authorize_mutation(
    const AdminClusterPlacementPolicyRoutesContext&
        context,
    const httplib::Request& request,
    httplib::Response& response,
    std::string* actor_fingerprint
) {
    if (!authenticate_admin(
            context,
            request,
            response,
            actor_fingerprint)) {
        return false;
    }

    return context.require_same_origin(
        request,
        response
    );
}

bool load_assignment_counts(
    const AdminClusterPlacementPolicyRoutesContext&
        context,
    std::unordered_map<
        std::string,
        std::size_t
    >* output
) {
    if (!output ||
        !context.assigned_user_counts) {
        return false;
    }

    output->clear();

    try {
        *output =
            context.assigned_user_counts();
    } catch (...) {
        output->clear();
        return false;
    }

    return true;
}

std::size_t assigned_users_for(
    const std::unordered_map<
        std::string,
        std::size_t
    >& counts,
    const std::string& policy_id
) {
    const auto iterator =
        counts.find(policy_id);

    if (iterator == counts.end()) {
        return 0;
    }

    return iterator->second;
}

} // namespace

void handle_admin_cluster_placement_policies_list(
    const httplib::Request& request,
    httplib::Response& response,
    const AdminClusterPlacementPolicyRoutesContext&
        context
) {
    std::string actor_fingerprint;

    if (!authenticate_admin(
            context,
            request,
            response,
            &actor_fingerprint)) {
        return;
    }

    std::lock_guard<std::mutex> placement_lock(
        *context.placement_mutex
    );

    std::unordered_map<
        std::string,
        std::size_t
    > assigned_user_counts;

    if (!load_assignment_counts(
            context,
            &assigned_user_counts)) {
        reply_json_local(
            context,
            response,
            500,
            json{
                {"ok", false},
                {"error", "server_error"},
                {
                    "message",
                    "failed to count assigned users"
                }
            }
        );
        return;
    }

    const std::vector<UserPlacementPolicy>
        policies =
            context.registry->snapshot();

    json output =
        json::array();

    for (const UserPlacementPolicy& policy :
         policies) {
        output.push_back(
            policy_to_json(
                policy,
                assigned_users_for(
                    assigned_user_counts,
                    policy.policy_id
                )
            )
        );
    }

    reply_json_local(
        context,
        response,
        200,
        json{
            {"ok", true},
            {
                "schema_version",
                kUserPlacementPolicyRegistrySchemaVersion
            },
            {"policies", std::move(output)}
        }
    );
}

void handle_admin_cluster_placement_policies_upsert(
    const httplib::Request& request,
    httplib::Response& response,
    const AdminClusterPlacementPolicyRoutesContext&
        context
) {
    std::string actor_fingerprint;

    if (!authorize_mutation(
            context,
            request,
            response,
            &actor_fingerprint)) {
        return;
    }

    json body;

    if (!parse_json_object(
            context,
            request,
            response,
            &body)) {
        audit_mutation(
            context,
            request,
            actor_fingerprint,
            "admin.cluster_placement_policy.upsert",
            "",
            "fail",
            "invalid_request_body"
        );
        return;
    }

    UserPlacementPolicy policy;
    std::string reason_code;

    if (!parse_policy_body(
            body,
            &policy,
            &reason_code)) {
        audit_mutation(
            context,
            request,
            actor_fingerprint,
            "admin.cluster_placement_policy.upsert",
            "",
            "fail",
            reason_code
        );

        reply_json_local(
            context,
            response,
            400,
            json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "invalid placement policy"},
                {
                    "reason_code",
                    reason_code.empty()
                        ? "invalid_policy"
                        : reason_code
                }
            }
        );
        return;
    }

    std::scoped_lock lock(
        *context.placement_mutex,
        g_cluster_policy_mutation_mutex
    );

    const auto current =
        context.registry->get(
            policy.policy_id
        );

    const bool created =
        !current.has_value();

    const bool changed =
        created ||
        !policies_equal(
            *current,
            policy
        );

    if (changed) {
        std::string registry_error;

        if (!context.registry->upsert(
                policy,
                &registry_error)) {
            audit_mutation(
                context,
                request,
                actor_fingerprint,
                "admin.cluster_placement_policy.upsert",
                policy.policy_id,
                "fail",
                "persistence_failed"
            );

            // Security: do not expose persistence paths or raw errors.
            reply_json_local(
                context,
                response,
                500,
                json{
                    {"ok", false},
                    {"error", "server_error"},
                    {
                        "message",
                        "failed to persist placement policy"
                    }
                }
            );
            return;
        }
    }

    const auto stored =
        context.registry->get(
            policy.policy_id
        );

    if (!stored.has_value()) {
        audit_mutation(
            context,
            request,
            actor_fingerprint,
            "admin.cluster_placement_policy.upsert",
            policy.policy_id,
            "fail",
            "stored_policy_unavailable"
        );

        reply_json_local(
            context,
            response,
            500,
            json{
                {"ok", false},
                {"error", "server_error"},
                {
                    "message",
                    "stored placement policy is unavailable"
                }
            }
        );
        return;
    }

    std::unordered_map<
        std::string,
        std::size_t
    > assigned_user_counts;

    if (!load_assignment_counts(
            context,
            &assigned_user_counts)) {
        audit_mutation(
            context,
            request,
            actor_fingerprint,
            "admin.cluster_placement_policy.upsert",
            policy.policy_id,
            "fail",
            "assignment_count_failed"
        );

        reply_json_local(
            context,
            response,
            500,
            json{
                {"ok", false},
                {"error", "server_error"},
                {
                    "message",
                    "failed to count assigned users"
                }
            }
        );
        return;
    }

    audit_mutation(
        context,
        request,
        actor_fingerprint,
        "admin.cluster_placement_policy.upsert",
        policy.policy_id,
        "ok",
        created
            ? "policy_created"
            : (
                changed
                    ? "policy_updated"
                    : "policy_unchanged"
            )
    );

    reply_json_local(
        context,
        response,
        created ? 201 : 200,
        json{
            {"ok", true},
            {"created", created},
            {"changed", changed},
            {
                "policy",
                policy_to_json(
                    *stored,
                    assigned_users_for(
                        assigned_user_counts,
                        stored->policy_id
                    )
                )
            }
        }
    );
}

void handle_admin_cluster_placement_policies_delete(
    const httplib::Request& request,
    httplib::Response& response,
    const AdminClusterPlacementPolicyRoutesContext&
        context
) {
    std::string actor_fingerprint;

    if (!authorize_mutation(
            context,
            request,
            response,
            &actor_fingerprint)) {
        return;
    }

    json body;

    if (!parse_json_object(
            context,
            request,
            response,
            &body)) {
        audit_mutation(
            context,
            request,
            actor_fingerprint,
            "admin.cluster_placement_policy.delete",
            "",
            "fail",
            "invalid_request_body"
        );
        return;
    }

    if (!has_only_keys(
            body,
            {"policy_id"}) ||
        body.size() != 1 ||
        !body.contains("policy_id") ||
        !body["policy_id"].is_string()) {
        audit_mutation(
            context,
            request,
            actor_fingerprint,
            "admin.cluster_placement_policy.delete",
            "",
            "fail",
            "invalid_request_body"
        );

        reply_json_local(
            context,
            response,
            400,
            json{
                {"ok", false},
                {"error", "bad_request"},
                {
                    "message",
                    "policy_id must be the only request field"
                }
            }
        );
        return;
    }

    const std::string policy_id =
        body["policy_id"].get<std::string>();

    UserPlacementPolicy identifier_check;
    identifier_check.policy_id =
        policy_id;

    identifier_check.mode =
        UserPlacementPolicyMode::Automatic;

    identifier_check.fallback =
        UserPlacementPolicyFallback::Deny;

    std::string validation_error;

    if (!validate_user_placement_policy(
            identifier_check,
            &validation_error)) {
        audit_mutation(
            context,
            request,
            actor_fingerprint,
            "admin.cluster_placement_policy.delete",
            "",
            "fail",
            "invalid_policy_id"
        );

        reply_json_local(
            context,
            response,
            400,
            json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "invalid placement policy ID"}
            }
        );
        return;
    }

    std::scoped_lock lock(
        *context.placement_mutex,
        g_cluster_policy_mutation_mutex
    );

    if (!context.registry->get(
            policy_id).has_value()) {
        audit_mutation(
            context,
            request,
            actor_fingerprint,
            "admin.cluster_placement_policy.delete",
            policy_id,
            "fail",
            "policy_not_found"
        );

        reply_json_local(
            context,
            response,
            404,
            json{
                {"ok", false},
                {"error", "not_found"},
                {"message", "placement policy not found"}
            }
        );
        return;
    }

    std::unordered_map<
        std::string,
        std::size_t
    > assigned_user_counts;

    if (!load_assignment_counts(
            context,
            &assigned_user_counts)) {
        audit_mutation(
            context,
            request,
            actor_fingerprint,
            "admin.cluster_placement_policy.delete",
            policy_id,
            "fail",
            "assignment_count_failed"
        );

        reply_json_local(
            context,
            response,
            500,
            json{
                {"ok", false},
                {"error", "server_error"},
                {
                    "message",
                    "failed to count assigned users"
                }
            }
        );
        return;
    }

    const std::size_t assigned_users =
        assigned_users_for(
            assigned_user_counts,
            policy_id
        );

    if (assigned_users > 0) {
        audit_mutation(
            context,
            request,
            actor_fingerprint,
            "admin.cluster_placement_policy.delete",
            policy_id,
            "fail",
            "policy_has_assigned_users"
        );

        // Security: expose only the aggregate count, never fingerprints.
        reply_json_local(
            context,
            response,
            409,
            json{
                {"ok", false},
                {"error", "conflict"},
                {
                    "message",
                    "placement policy still has assigned users"
                },
                {"policy_id", policy_id},
                {"assigned_users", assigned_users}
            }
        );
        return;
    }

    std::string registry_error;

    if (!context.registry->erase(
            policy_id,
            &registry_error)) {
        audit_mutation(
            context,
            request,
            actor_fingerprint,
            "admin.cluster_placement_policy.delete",
            policy_id,
            "fail",
            "persistence_failed"
        );

        reply_json_local(
            context,
            response,
            500,
            json{
                {"ok", false},
                {"error", "server_error"},
                {
                    "message",
                    "failed to delete placement policy"
                }
            }
        );
        return;
    }

    audit_mutation(
        context,
        request,
        actor_fingerprint,
        "admin.cluster_placement_policy.delete",
        policy_id,
        "ok",
        "policy_deleted"
    );

    reply_json_local(
        context,
        response,
        200,
        json{
            {"ok", true},
            {"removed", true},
            {"policy_id", policy_id}
        }
    );
}

void register_admin_cluster_placement_policy_routes(
    httplib::Server& server,
    const AdminClusterPlacementPolicyRoutesContext&
        context
) {
    const AdminClusterPlacementPolicyRoutesContext
        captured =
            context;

    server.Get(
        "/api/v4/admin/cluster/placement-policies",
        [captured](
            const httplib::Request& request,
            httplib::Response& response
        ) {
            handle_admin_cluster_placement_policies_list(
                request,
                response,
                captured
            );
        }
    );

    server.Post(
        "/api/v4/admin/cluster/placement-policies",
        [captured](
            const httplib::Request& request,
            httplib::Response& response
        ) {
            handle_admin_cluster_placement_policies_upsert(
                request,
                response,
                captured
            );
        }
    );

    server.Delete(
        "/api/v4/admin/cluster/placement-policies",
        [captured](
            const httplib::Request& request,
            httplib::Response& response
        ) {
            handle_admin_cluster_placement_policies_delete(
                request,
                response,
                captured
            );
        }
    );
}

} // namespace pqnas::cluster
