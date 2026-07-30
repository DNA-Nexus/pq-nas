#include "routes_admin_cluster_nodes.h"
#include "cluster/cluster_identity_validation.h"

#include "audit_log.h"
#include "cluster/node_registry.h"
#include "httplib.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <initializer_list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace pqnas::cluster {
namespace {

using json = nlohmann::json;

std::mutex g_cluster_node_mutation_mutex;

bool context_ok(
    const AdminClusterNodeRoutesContext& context
) {
    return
        context.registry &&
        context.placement_mutex &&
        static_cast<bool>(context.assigned_user_counts) &&
        static_cast<bool>(context.require_admin_auth) &&
        static_cast<bool>(context.require_same_origin) &&
        static_cast<bool>(context.reply_json) &&
        static_cast<bool>(context.now_iso_utc) &&
        static_cast<bool>(context.audit_append);
}

void reply_json_local(
    const AdminClusterNodeRoutesContext& context,
    httplib::Response& res,
    int status,
    const json& body
) {
    res.set_header("Cache-Control", "no-store");

    if (context.reply_json) {
        context.reply_json(
            res,
            status,
            body.dump()
        );
        return;
    }

    res.status = status;
    res.set_content(
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
        bool found = false;

        for (const char* allowed_key : allowed) {
            if (iterator.key() == allowed_key) {
                found = true;
                break;
            }
        }

        if (!found) {
            return false;
        }
    }

    return true;
}

bool parse_json_object(
    const AdminClusterNodeRoutesContext& context,
    const httplib::Request& req,
    httplib::Response& res,
    json* out
) {
    if (!out) {
        reply_json_local(
            context,
            res,
            500,
            json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "request parser unavailable"}
            }
        );
        return false;
    }

    if (req.body.size() >
        kAdminClusterNodeRequestBodyMaxBytes) {
        reply_json_local(
            context,
            res,
            413,
            json{
                {"ok", false},
                {"error", "payload_too_large"},
                {"message", "request body is too large"},
                {
                    "max_bytes",
                    kAdminClusterNodeRequestBodyMaxBytes
                }
            }
        );
        return false;
    }

    *out = json::parse(
        req.body,
        nullptr,
        false
    );

    if (out->is_discarded() ||
        !out->is_object()) {
        reply_json_local(
            context,
            res,
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

json node_record_to_json(
    const NodeRegistryRecord& record
) {
    return json{
        {"node_id", record.node_id},
        {"api_base_url", record.api_base_url},
        {"enabled", record.enabled},
        {"added_at", record.added_at},
        {"updated_at", record.updated_at},
        {"health", record.health},
        {"last_seen_at", record.last_seen_at},
        {
            "node_reported_at",
            record.node_reported_at
        },
        {
            "protocol_version",
            record.protocol_version
        },
        {"hostname", record.hostname},
        {"server_version", record.server_version},
        {"uptime_s", record.uptime_s},
        {
            "storage",
            {
                {
                    "total_bytes",
                    record.storage_total_bytes
                },
                {
                    "used_bytes",
                    record.storage_used_bytes
                },
                {
                    "free_bytes",
                    record.storage_free_bytes
                },
                {
                    "fstype",
                    record.storage_fstype
                }
            }
        },
        {
            "users",
            {
                {"total", record.users_total},
                {"enabled", record.users_enabled}
            }
        }
    };
}

std::string audit_ip(
    const httplib::Request& req
) {
    if (req.remote_addr.empty()) {
        return "?";
    }

    if (req.remote_addr.size() <= 128) {
        return req.remote_addr;
    }

    return req.remote_addr.substr(0, 128);
}

bool load_assigned_user_counts(
    const AdminClusterNodeRoutesContext& context,
    std::unordered_map<
        std::string,
        std::size_t
    >* counts
) {
    if (!counts ||
        !context.assigned_user_counts) {
        return false;
    }

    counts->clear();

    try {
        *counts =
            context.assigned_user_counts();
    } catch (...) {
        counts->clear();
        return false;
    }

    return true;
}

void audit_mutation(
    const AdminClusterNodeRoutesContext& context,
    const httplib::Request& req,
    const std::string& actor_fp,
    const std::string& event,
    const std::string& node_id,
    const std::string& outcome
) {
    if (!context.audit_append) {
        return;
    }

    pqnas::AuditEvent audit;
    audit.event = event;
    audit.outcome = outcome;
    audit.f["actor_fp"] = actor_fp;
    audit.f["node_id"] = node_id;
    audit.f["ip"] = audit_ip(req);
    audit.f["ts"] = context.now_iso_utc();

    // Security: the internal API URL and request body are intentionally not
    // copied into the audit event. This avoids unnecessary topology and
    // credential-adjacent metadata in general audit output.
    context.audit_append(audit);
}

bool authenticate_admin(
    const AdminClusterNodeRoutesContext& context,
    const httplib::Request& req,
    httplib::Response& res,
    std::string* actor_fp
) {
    res.set_header("Cache-Control", "no-store");

    if (!context_ok(context)) {
        reply_json_local(
            context,
            res,
            500,
            json{
                {"ok", false},
                {"error", "server_error"},
                {
                    "message",
                    "admin cluster route context incomplete"
                }
            }
        );
        return false;
    }

    return context.require_admin_auth(
        req,
        res,
        actor_fp
    );
}

bool authorize_mutation(
    const AdminClusterNodeRoutesContext& context,
    const httplib::Request& req,
    httplib::Response& res,
    std::string* actor_fp
) {
    if (!authenticate_admin(
            context,
            req,
            res,
            actor_fp)) {
        return false;
    }

    return context.require_same_origin(req, res);
}

} // namespace

void handle_admin_cluster_nodes_list(
    const httplib::Request& req,
    httplib::Response& res,
    const AdminClusterNodeRoutesContext& context
) {
    std::string actor_fp;

    if (!authenticate_admin(
            context,
            req,
            res,
            &actor_fp)) {
        return;
    }

    std::unordered_map<
        std::string,
        std::size_t
    > assigned_user_counts;

    if (!load_assigned_user_counts(
            context,
            &assigned_user_counts)) {
        reply_json_local(
            context,
            res,
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

    const std::vector<NodeRegistryRecord> nodes =
        context.registry->snapshot();

    json output_nodes = json::array();

    for (const NodeRegistryRecord& node : nodes) {
        json output_node =
            node_record_to_json(node);

        const auto assigned_iterator =
            assigned_user_counts.find(
                node.node_id
            );

        output_node["assigned_users"] =
            assigned_iterator ==
                assigned_user_counts.end()
                    ? 0
                    : assigned_iterator->second;

        output_nodes.push_back(
            std::move(output_node)
        );
    }

    reply_json_local(
        context,
        res,
        200,
        json{
            {"ok", true},
            {
                "schema_version",
                kNodeRegistrySchemaVersion
            },
            {"nodes", std::move(output_nodes)}
        }
    );
}

void handle_admin_cluster_nodes_create(
    const httplib::Request& req,
    httplib::Response& res,
    const AdminClusterNodeRoutesContext& context
) {
    std::string actor_fp;

    if (!authorize_mutation(
            context,
            req,
            res,
            &actor_fp)) {
        return;
    }

    json body;

    if (!parse_json_object(
            context,
            req,
            res,
            &body)) {
        audit_mutation(
            context,
            req,
            actor_fp,
            "admin.cluster_node.create",
            "",
            "fail"
        );
        return;
    }

    if (!has_only_keys(
            body,
            {
                "node_id",
                "api_base_url",
                "enabled"
            })) {
        audit_mutation(
            context,
            req,
            actor_fp,
            "admin.cluster_node.create",
            "",
            "fail"
        );

        reply_json_local(
            context,
            res,
            400,
            json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "unsupported request field"}
            }
        );
        return;
    }

    const auto node_id_iterator =
        body.find("node_id");

    const auto url_iterator =
        body.find("api_base_url");

    if (node_id_iterator == body.end() ||
        !node_id_iterator->is_string() ||
        url_iterator == body.end() ||
        !url_iterator->is_string()) {
        audit_mutation(
            context,
            req,
            actor_fp,
            "admin.cluster_node.create",
            "",
            "fail"
        );

        reply_json_local(
            context,
            res,
            400,
            json{
                {"ok", false},
                {"error", "bad_request"},
                {
                    "message",
                    "node_id and api_base_url are required"
                }
            }
        );
        return;
    }

    const std::string node_id =
        node_id_iterator->get<std::string>();

    std::string normalized_url;

    if (!valid_cluster_node_id(node_id) ||
        !normalize_cluster_https_base_url(
            url_iterator->get<std::string>(),
            &normalized_url,
            nullptr)) {
        audit_mutation(
            context,
            req,
            actor_fp,
            "admin.cluster_node.create",
            valid_cluster_node_id(node_id) ? node_id : "",
            "fail"
        );

        reply_json_local(
            context,
            res,
            400,
            json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "invalid node configuration"}
            }
        );
        return;
    }

    bool enabled = true;

    const auto enabled_iterator =
        body.find("enabled");

    if (enabled_iterator != body.end()) {
        if (!enabled_iterator->is_boolean()) {
            audit_mutation(
                context,
                req,
                actor_fp,
                "admin.cluster_node.create",
                node_id,
                "fail"
            );

            reply_json_local(
                context,
                res,
                400,
                json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "enabled must be boolean"}
                }
            );
            return;
        }

        enabled =
            enabled_iterator->get<bool>();
    }

    std::lock_guard<std::mutex> lock(
        g_cluster_node_mutation_mutex
    );

    if (context.registry->get(node_id).has_value()) {
        audit_mutation(
            context,
            req,
            actor_fp,
            "admin.cluster_node.create",
            node_id,
            "fail"
        );

        reply_json_local(
            context,
            res,
            409,
            json{
                {"ok", false},
                {"error", "conflict"},
                {"message", "node is already registered"}
            }
        );
        return;
    }

    std::string registry_error;

    if (!context.registry->upsert_configured_node(
            node_id,
            normalized_url,
            enabled,
            context.now_iso_utc(),
            &registry_error)) {
        audit_mutation(
            context,
            req,
            actor_fp,
            "admin.cluster_node.create",
            node_id,
            "fail"
        );

        // Do not expose persistence paths or filesystem errors.
        reply_json_local(
            context,
            res,
            500,
            json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to create node"}
            }
        );
        return;
    }

    const auto created =
        context.registry->get(node_id);

    if (!created.has_value()) {
        audit_mutation(
            context,
            req,
            actor_fp,
            "admin.cluster_node.create",
            node_id,
            "fail"
        );

        reply_json_local(
            context,
            res,
            500,
            json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "created node is unavailable"}
            }
        );
        return;
    }

    audit_mutation(
        context,
        req,
        actor_fp,
        "admin.cluster_node.create",
        node_id,
        "ok"
    );

    reply_json_local(
        context,
        res,
        201,
        json{
            {"ok", true},
            {"created", true},
            {"node", node_record_to_json(*created)}
        }
    );
}

void handle_admin_cluster_nodes_update(
    const httplib::Request& req,
    httplib::Response& res,
    const AdminClusterNodeRoutesContext& context
) {
    std::string actor_fp;

    if (!authorize_mutation(
            context,
            req,
            res,
            &actor_fp)) {
        return;
    }

    json body;

    if (!parse_json_object(
            context,
            req,
            res,
            &body)) {
        audit_mutation(
            context,
            req,
            actor_fp,
            "admin.cluster_node.update",
            "",
            "fail"
        );
        return;
    }

    if (!has_only_keys(
            body,
            {
                "node_id",
                "api_base_url",
                "enabled"
            })) {
        audit_mutation(
            context,
            req,
            actor_fp,
            "admin.cluster_node.update",
            "",
            "fail"
        );

        reply_json_local(
            context,
            res,
            400,
            json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "unsupported request field"}
            }
        );
        return;
    }

    const auto node_id_iterator =
        body.find("node_id");

    if (node_id_iterator == body.end() ||
        !node_id_iterator->is_string()) {
        audit_mutation(
            context,
            req,
            actor_fp,
            "admin.cluster_node.update",
            "",
            "fail"
        );

        reply_json_local(
            context,
            res,
            400,
            json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "node_id is required"}
            }
        );
        return;
    }

    const std::string node_id =
        node_id_iterator->get<std::string>();

    if (!valid_cluster_node_id(node_id)) {
        audit_mutation(
            context,
            req,
            actor_fp,
            "admin.cluster_node.update",
            "",
            "fail"
        );

        reply_json_local(
            context,
            res,
            400,
            json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "invalid node ID"}
            }
        );
        return;
    }

    const auto url_iterator =
        body.find("api_base_url");

    const auto enabled_iterator =
        body.find("enabled");

    const bool has_url =
        url_iterator != body.end();

    const bool has_enabled =
        enabled_iterator != body.end();

    if (!has_url && !has_enabled) {
        audit_mutation(
            context,
            req,
            actor_fp,
            "admin.cluster_node.update",
            node_id,
            "fail"
        );

        reply_json_local(
            context,
            res,
            400,
            json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "no node fields to update"}
            }
        );
        return;
    }

    std::string normalized_url;

    if (has_url) {
        if (!url_iterator->is_string() ||
            !normalize_cluster_https_base_url(
                url_iterator->get<std::string>(),
                &normalized_url,
                nullptr)) {
            audit_mutation(
                context,
                req,
                actor_fp,
                "admin.cluster_node.update",
                node_id,
                "fail"
            );

            reply_json_local(
                context,
                res,
                400,
                json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "invalid node API URL"}
                }
            );
            return;
        }
    }

    bool requested_enabled = false;

    if (has_enabled) {
        if (!enabled_iterator->is_boolean()) {
            audit_mutation(
                context,
                req,
                actor_fp,
                "admin.cluster_node.update",
                node_id,
                "fail"
            );

            reply_json_local(
                context,
                res,
                400,
                json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "enabled must be boolean"}
                }
            );
            return;
        }

        requested_enabled =
            enabled_iterator->get<bool>();
    }

    std::scoped_lock lock(
        *context.placement_mutex,
        g_cluster_node_mutation_mutex
    );

    const auto current =
        context.registry->get(node_id);

    if (!current.has_value()) {
        audit_mutation(
            context,
            req,
            actor_fp,
            "admin.cluster_node.update",
            node_id,
            "fail"
        );

        reply_json_local(
            context,
            res,
            404,
            json{
                {"ok", false},
                {"error", "not_found"},
                {"message", "node not found"}
            }
        );
        return;
    }

    const std::string final_url =
        has_url
            ? normalized_url
            : current->api_base_url;

    const bool final_enabled =
        has_enabled
            ? requested_enabled
            : current->enabled;

    std::string registry_error;

    if (!context.registry->upsert_configured_node(
            node_id,
            final_url,
            final_enabled,
            context.now_iso_utc(),
            &registry_error)) {
        audit_mutation(
            context,
            req,
            actor_fp,
            "admin.cluster_node.update",
            node_id,
            "fail"
        );

        reply_json_local(
            context,
            res,
            500,
            json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to update node"}
            }
        );
        return;
    }

    const auto updated =
        context.registry->get(node_id);

    if (!updated.has_value()) {
        audit_mutation(
            context,
            req,
            actor_fp,
            "admin.cluster_node.update",
            node_id,
            "fail"
        );

        reply_json_local(
            context,
            res,
            500,
            json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "updated node is unavailable"}
            }
        );
        return;
    }

    audit_mutation(
        context,
        req,
        actor_fp,
        "admin.cluster_node.update",
        node_id,
        "ok"
    );

    reply_json_local(
        context,
        res,
        200,
        json{
            {"ok", true},
            {"node", node_record_to_json(*updated)}
        }
    );
}

void handle_admin_cluster_nodes_delete(
    const httplib::Request& req,
    httplib::Response& res,
    const AdminClusterNodeRoutesContext& context
) {
    std::string actor_fp;

    if (!authorize_mutation(
            context,
            req,
            res,
            &actor_fp)) {
        return;
    }

    json body;

    if (!parse_json_object(
            context,
            req,
            res,
            &body)) {
        audit_mutation(
            context,
            req,
            actor_fp,
            "admin.cluster_node.delete",
            "",
            "fail"
        );
        return;
    }

    if (!has_only_keys(body, {"node_id"})) {
        audit_mutation(
            context,
            req,
            actor_fp,
            "admin.cluster_node.delete",
            "",
            "fail"
        );

        reply_json_local(
            context,
            res,
            400,
            json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "unsupported request field"}
            }
        );
        return;
    }

    const auto node_id_iterator =
        body.find("node_id");

    if (node_id_iterator == body.end() ||
        !node_id_iterator->is_string() ||
        !valid_cluster_node_id(
            node_id_iterator->get<std::string>())) {
        audit_mutation(
            context,
            req,
            actor_fp,
            "admin.cluster_node.delete",
            "",
            "fail"
        );

        reply_json_local(
            context,
            res,
            400,
            json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "invalid node ID"}
            }
        );
        return;
    }

    const std::string node_id =
        node_id_iterator->get<std::string>();

    std::scoped_lock lock(
        *context.placement_mutex,
        g_cluster_node_mutation_mutex
    );

    if (!context.registry->get(node_id).has_value()) {
        audit_mutation(
            context,
            req,
            actor_fp,
            "admin.cluster_node.delete",
            node_id,
            "fail"
        );

        reply_json_local(
            context,
            res,
            404,
            json{
                {"ok", false},
                {"error", "not_found"},
                {"message", "node not found"}
            }
        );
        return;
    }

    std::unordered_map<
        std::string,
        std::size_t
    > assigned_user_counts;

    if (!load_assigned_user_counts(
            context,
            &assigned_user_counts)) {
        audit_mutation(
            context,
            req,
            actor_fp,
            "admin.cluster_node.delete",
            node_id,
            "fail"
        );

        reply_json_local(
            context,
            res,
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

    const auto assigned_iterator =
        assigned_user_counts.find(node_id);

    const std::size_t assigned_users =
        assigned_iterator ==
            assigned_user_counts.end()
                ? 0
                : assigned_iterator->second;

    if (assigned_users > 0) {
        audit_mutation(
            context,
            req,
            actor_fp,
            "admin.cluster_node.delete",
            node_id,
            "fail"
        );

        // Security: return only the aggregate count, never user fingerprints.
        reply_json_local(
            context,
            res,
            409,
            json{
                {"ok", false},
                {"error", "conflict"},
                {
                    "message",
                    "node still has assigned users"
                },
                {"node_id", node_id},
                {"assigned_users", assigned_users}
            }
        );
        return;
    }

    std::string registry_error;

    if (!context.registry->erase(
            node_id,
            &registry_error)) {
        audit_mutation(
            context,
            req,
            actor_fp,
            "admin.cluster_node.delete",
            node_id,
            "fail"
        );

        reply_json_local(
            context,
            res,
            500,
            json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to delete node"}
            }
        );
        return;
    }

    audit_mutation(
        context,
        req,
        actor_fp,
        "admin.cluster_node.delete",
        node_id,
        "ok"
    );

    reply_json_local(
        context,
        res,
        200,
        json{
            {"ok", true},
            {"removed", true},
            {"node_id", node_id}
        }
    );
}

void register_admin_cluster_node_routes(
    httplib::Server& server,
    const AdminClusterNodeRoutesContext& context
) {
    const AdminClusterNodeRoutesContext captured =
        context;

    server.Get(
        "/api/v4/admin/cluster/nodes",
        [captured](
            const httplib::Request& req,
            httplib::Response& res
        ) {
            handle_admin_cluster_nodes_list(
                req,
                res,
                captured
            );
        }
    );

    server.Post(
        "/api/v4/admin/cluster/nodes",
        [captured](
            const httplib::Request& req,
            httplib::Response& res
        ) {
            handle_admin_cluster_nodes_create(
                req,
                res,
                captured
            );
        }
    );

    server.Patch(
        "/api/v4/admin/cluster/nodes",
        [captured](
            const httplib::Request& req,
            httplib::Response& res
        ) {
            handle_admin_cluster_nodes_update(
                req,
                res,
                captured
            );
        }
    );

    server.Delete(
        "/api/v4/admin/cluster/nodes",
        [captured](
            const httplib::Request& req,
            httplib::Response& res
        ) {
            handle_admin_cluster_nodes_delete(
                req,
                res,
                captured
            );
        }
    );
}

} // namespace pqnas::cluster
