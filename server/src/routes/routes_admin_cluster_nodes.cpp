#include "routes_admin_cluster_nodes.h"
#include "cluster/cluster_identity_validation.h"
#include "cluster/cluster_timestamp.h"
#include "cluster/node_tls_identity.h"

#include "audit_log.h"
#include "cluster/node_registry.h"
#include "httplib.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace pqnas::cluster {
namespace {

using json = nlohmann::json;

std::mutex g_cluster_node_mutation_mutex;

struct AdminRotationRateState {
    std::chrono::steady_clock::time_point
        window_started;

    std::chrono::steady_clock::time_point
        last_seen;

    std::size_t count = 0;
};

std::mutex
    g_admin_rotation_rate_mutex;

std::unordered_map<
    std::string,
    AdminRotationRateState
> g_admin_rotation_rate_states;

bool consume_admin_rotation_rate_limit(
    const std::string& actor_fp,
    long* retry_after_seconds
) {
    if (retry_after_seconds) {
        *retry_after_seconds =
            kAdminClusterNodeRotationRateWindowSeconds;
    }

    if (actor_fp.empty() ||
        actor_fp.size() > 512) {
        return false;
    }

    using Clock =
        std::chrono::steady_clock;

    const auto now =
        Clock::now();

    const auto window =
        std::chrono::seconds(
            kAdminClusterNodeRotationRateWindowSeconds
        );

    std::lock_guard<std::mutex>
        lock(
            g_admin_rotation_rate_mutex
        );

    auto iterator =
        g_admin_rotation_rate_states.find(
            actor_fp
        );

    if (iterator ==
        g_admin_rotation_rate_states.end()) {
        if (g_admin_rotation_rate_states.size() >=
            kAdminClusterNodeRotationTrackedActors) {
            auto oldest =
                g_admin_rotation_rate_states.begin();

            for (auto current =
                     g_admin_rotation_rate_states.begin();
                 current !=
                     g_admin_rotation_rate_states.end();
                 ++current) {
                if (current->second.last_seen <
                    oldest->second.last_seen) {
                    oldest = current;
                }
            }

            // Security: keep attacker-controlled actor cardinality bounded.
            g_admin_rotation_rate_states.erase(
                oldest
            );
        }

        AdminRotationRateState state;
        state.window_started = now;
        state.last_seen = now;
        state.count = 1;

        g_admin_rotation_rate_states.emplace(
            actor_fp,
            state
        );

        if (retry_after_seconds) {
            *retry_after_seconds = 0;
        }

        return true;
    }

    AdminRotationRateState& state =
        iterator->second;

    state.last_seen = now;

    const auto elapsed =
        now - state.window_started;

    if (elapsed >= window) {
        state.window_started = now;
        state.count = 1;

        if (retry_after_seconds) {
            *retry_after_seconds = 0;
        }

        return true;
    }

    if (state.count >=
        kAdminClusterNodeRotationRateLimit) {
        if (retry_after_seconds) {
            const auto remaining =
                window - elapsed;

            const auto milliseconds =
                std::chrono::duration_cast<
                    std::chrono::milliseconds
                >(
                    remaining
                ).count();

            long seconds =
                static_cast<long>(
                    (milliseconds + 999) /
                    1000
                );

            if (seconds < 1) {
                seconds = 1;
            }

            *retry_after_seconds =
                seconds;
        }

        return false;
    }

    ++state.count;

    if (retry_after_seconds) {
        *retry_after_seconds = 0;
    }

    return true;
}

bool context_ok(
    const AdminClusterNodeRoutesContext& context
) {
    return
        context.registry &&
        context.placement_mutex &&
        static_cast<bool>(context.get_node_tls_trust) &&
        static_cast<bool>(context.bind_node_tls_trust) &&
        static_cast<bool>(context.verify_node_tls_endpoint) &&
        static_cast<bool>(context.assigned_user_counts) &&
        static_cast<bool>(context.rotation_in_progress) &&
        static_cast<bool>(context.rotate_node_credential) &&
        static_cast<bool>(context.erase_unassigned_node) &&
        static_cast<bool>(context.require_admin_auth) &&
        static_cast<bool>(context.require_same_origin) &&
        static_cast<bool>(context.reply_json) &&
        static_cast<bool>(context.now_iso_utc) &&
        static_cast<bool>(context.audit_append);
}

bool query_rotation_in_progress(
    const AdminClusterNodeRoutesContext& context,
    const std::string& node_id,
    bool* in_progress
) noexcept {
    if (!in_progress ||
        !context.rotation_in_progress) {
        return false;
    }

    *in_progress = true;

    try {
        *in_progress =
            context.rotation_in_progress(
                node_id
            );

        return true;
    } catch (...) {
        // Security: an uncertain rotation state must fail closed. A lifecycle
        // mutation must never proceed merely because the busy-state provider
        // failed.
        *in_progress = true;
        return false;
    }
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
        {"browser_base_url", record.browser_base_url},
        {"enabled", record.enabled},
        {
            "capacity_ceiling_bytes",
            record.capacity_ceiling_bytes
        },
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
                "browser_base_url",
                "enabled",
                "capacity_ceiling_bytes"
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

    std::optional<std::uint64_t>
        capacity_ceiling_bytes;

    const auto capacity_ceiling_iterator =
        body.find("capacity_ceiling_bytes");

    if (capacity_ceiling_iterator != body.end()) {
        if (!capacity_ceiling_iterator->
                is_number_unsigned()) {
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
                    {
                        "message",
                        "capacity_ceiling_bytes must be an unsigned integer"
                    }
                }
            );
            return;
        }

        capacity_ceiling_bytes =
            capacity_ceiling_iterator->
                get<std::uint64_t>();
    }

    std::optional<std::string>
        normalized_browser_base_url;

    const auto browser_url_iterator =
        body.find("browser_base_url");

    if (browser_url_iterator != body.end()) {
        if (!browser_url_iterator->is_string()) {
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
                    {
                        "message",
                        "browser_base_url must be a string"
                    }
                }
            );
            return;
        }

        const std::string requested_browser_url =
            browser_url_iterator->get<std::string>();

        if (requested_browser_url.empty()) {
            normalized_browser_base_url =
                std::string();
        } else {
            std::string normalized_browser_url;

            if (!normalize_cluster_https_base_url(
                    requested_browser_url,
                    &normalized_browser_url,
                    nullptr)) {
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
                        {
                            "message",
                            "invalid node browser URL"
                        }
                    }
                );
                return;
            }

            normalized_browser_base_url =
                std::move(normalized_browser_url);
        }
    }

    // Security: direct administrative Node creation shares the Director
    // lifecycle boundary with enrollment consumption. Otherwise bootstrap
    // provisioning could persist a credential and race a second NodeRegistry
    // insertion for the same node ID.
    std::scoped_lock lock(
        *context.placement_mutex,
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
            canonicalize_cluster_runtime_timestamp(
                context.now_iso_utc()
            ),
            &registry_error,
            capacity_ceiling_bytes,
            normalized_browser_base_url)) {
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
                "browser_base_url",
                "enabled",
                "capacity_ceiling_bytes"
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

    const auto capacity_ceiling_iterator =
        body.find("capacity_ceiling_bytes");

    const auto browser_url_iterator =
        body.find("browser_base_url");

    const bool has_url =
        url_iterator != body.end();

    const bool has_enabled =
        enabled_iterator != body.end();

    const bool has_capacity_ceiling =
        capacity_ceiling_iterator != body.end();

    const bool has_browser_url =
        browser_url_iterator != body.end();

    if (!has_url &&
        !has_enabled &&
        !has_capacity_ceiling &&
        !has_browser_url) {
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

    std::optional<std::uint64_t>
        requested_capacity_ceiling_bytes;

    if (has_capacity_ceiling) {
        if (!capacity_ceiling_iterator->
                is_number_unsigned()) {
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
                    {
                        "message",
                        "capacity_ceiling_bytes must be an unsigned integer"
                    }
                }
            );
            return;
        }

        requested_capacity_ceiling_bytes =
            capacity_ceiling_iterator->
                get<std::uint64_t>();
    }

    std::optional<std::string>
        requested_browser_base_url;

    if (has_browser_url) {
        if (!browser_url_iterator->is_string()) {
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
                    {
                        "message",
                        "browser_base_url must be a string"
                    }
                }
            );
            return;
        }

        const std::string requested_browser_url =
            browser_url_iterator->get<std::string>();

        if (requested_browser_url.empty()) {
            requested_browser_base_url =
                std::string();
        } else {
            std::string normalized_browser_url;

            if (!normalize_cluster_https_base_url(
                    requested_browser_url,
                    &normalized_browser_url,
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
                        {
                            "message",
                            "invalid node browser URL"
                        }
                    }
                );
                return;
            }

            requested_browser_base_url =
                std::move(normalized_browser_url);
        }
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

    bool rotation_busy = false;

    if (!query_rotation_in_progress(
            context,
            node_id,
            &rotation_busy)) {
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
                {
                    "message",
                    "failed to check node credential rotation state"
                }
            }
        );
        return;
    }

    if (rotation_busy) {
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
            409,
            json{
                {"ok", false},
                {"error", "conflict"},
                {
                    "message",
                    "node credential rotation is in progress"
                }
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
            canonicalize_cluster_runtime_timestamp(
                context.now_iso_utc()
            ),
            &registry_error,
            requested_capacity_ceiling_bytes,
            requested_browser_base_url)) {
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

void handle_admin_cluster_node_tls_trust_migration(
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
            "admin.cluster_node.tls_trust_migration",
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
                "tls_spki_sha256_pin"
            })) {
        audit_mutation(
            context,
            req,
            actor_fp,
            "admin.cluster_node.tls_trust_migration",
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
            "admin.cluster_node.tls_trust_migration",
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

    const auto fail =
        [&](int status,
            const char* code,
            const char* message) {
            audit_mutation(
                context,
                req,
                actor_fp,
                "admin.cluster_node.tls_trust_migration",
                node_id,
                "fail"
            );

            reply_json_local(
                context,
                res,
                status,
                json{
                    {"ok", false},
                    {"error", code},
                    {"message", message}
                }
            );
        };

    if (!valid_cluster_node_id(node_id)) {
        fail(
            400,
            "bad_request",
            "invalid node ID"
        );
        return;
    }

    const auto url_iterator =
        body.find("api_base_url");

    if (url_iterator == body.end() ||
        !url_iterator->is_string()) {
        fail(
            400,
            "bad_request",
            "api_base_url is required"
        );
        return;
    }

    std::string normalized_url;

    if (!normalize_cluster_https_base_url(
            url_iterator->get<std::string>(),
            &normalized_url,
            nullptr)) {
        fail(
            400,
            "bad_request",
            "invalid node API URL"
        );
        return;
    }

    const auto pin_iterator =
        body.find("tls_spki_sha256_pin");

    if (pin_iterator == body.end() ||
        !pin_iterator->is_string()) {
        fail(
            400,
            "bad_request",
            "tls_spki_sha256_pin is required"
        );
        return;
    }

    const std::string requested_pin =
        pin_iterator->get<std::string>();

    std::string normalized_pin;

    if (!normalize_node_tls_spki_sha256_pin(
            requested_pin,
            &normalized_pin,
            nullptr) ||
        normalized_pin != requested_pin) {
        fail(
            400,
            "bad_request",
            "invalid node TLS SPKI pin"
        );
        return;
    }

    NodeRegistryRecord snapshot;

    {
        std::scoped_lock lock(
            *context.placement_mutex,
            g_cluster_node_mutation_mutex
        );

        const auto current =
            context.registry->get(node_id);

        if (!current.has_value()) {
            fail(
                404,
                "not_found",
                "node not found"
            );
            return;
        }

        bool rotation_busy = false;

        if (!query_rotation_in_progress(
                context,
                node_id,
                &rotation_busy)) {
            fail(
                500,
                "server_error",
                "failed to check node credential rotation state"
            );
            return;
        }

        if (rotation_busy) {
            fail(
                409,
                "conflict",
                "node credential rotation is in progress"
            );
            return;
        }

        std::string existing_pin;
        std::string trust_error;

        NodeTlsTrustStoreResult trust_result =
            NodeTlsTrustStoreResult::IoFailed;

        try {
            trust_result =
                context.get_node_tls_trust(
                    node_id,
                    &existing_pin,
                    &trust_error
                );
        } catch (...) {
            fail(
                503,
                "service_unavailable",
                "node TLS trust store is unavailable"
            );
            return;
        }

        if (trust_result ==
                NodeTlsTrustStoreResult::Ok &&
            existing_pin != requested_pin) {
            fail(
                409,
                "conflict",
                "node TLS identity is already bound"
            );
            return;
        }

        if (trust_result !=
                NodeTlsTrustStoreResult::Ok &&
            trust_result !=
                NodeTlsTrustStoreResult::NotFound) {
            fail(
                503,
                "service_unavailable",
                "node TLS trust store is unavailable"
            );
            return;
        }

        snapshot = *current;
    }

    // Security: never hold lifecycle/placement locks during network I/O.
    // No bearer credential is attached to this verification probe.
    std::string verification_error;
    bool verified = false;

    try {
        verified =
            context.verify_node_tls_endpoint(
                normalized_url,
                requested_pin,
                &verification_error
            );
    } catch (...) {
        verified = false;
    }

    if (!verified) {
        fail(
            502,
            "bad_gateway",
            "node TLS identity verification failed"
        );
        return;
    }

    std::optional<NodeRegistryRecord>
        updated;

    {
        std::scoped_lock lock(
            *context.placement_mutex,
            g_cluster_node_mutation_mutex
        );

        const auto current =
            context.registry->get(node_id);

        if (!current.has_value()) {
            fail(
                409,
                "conflict",
                "node changed during TLS verification"
            );
            return;
        }

        bool rotation_busy = false;

        if (!query_rotation_in_progress(
                context,
                node_id,
                &rotation_busy) ||
            rotation_busy) {
            fail(
                409,
                "conflict",
                "node lifecycle changed during TLS verification"
            );
            return;
        }

        // Security: added_at identifies this registered Node incarnation,
        // while updated_at plus the Director-owned configuration fields detect
        // concurrent administrative mutation. Heartbeat observation fields may
        // legitimately change while the network probe is in progress.
        if (current->added_at !=
                snapshot.added_at ||
            current->updated_at !=
                snapshot.updated_at ||
            current->api_base_url !=
                snapshot.api_base_url ||
            current->browser_base_url !=
                snapshot.browser_base_url ||
            current->enabled !=
                snapshot.enabled ||
            current->capacity_ceiling_bytes !=
                snapshot.capacity_ceiling_bytes) {
            fail(
                409,
                "conflict",
                "node configuration changed during TLS verification"
            );
            return;
        }

        std::string existing_pin;
        std::string trust_error;

        NodeTlsTrustStoreResult trust_result =
            NodeTlsTrustStoreResult::IoFailed;

        try {
            trust_result =
                context.get_node_tls_trust(
                    node_id,
                    &existing_pin,
                    &trust_error
                );
        } catch (...) {
            fail(
                503,
                "service_unavailable",
                "node TLS trust store is unavailable"
            );
            return;
        }

        if (trust_result ==
                NodeTlsTrustStoreResult::Ok &&
            existing_pin != requested_pin) {
            fail(
                409,
                "conflict",
                "node TLS identity changed during verification"
            );
            return;
        }

        if (trust_result ==
            NodeTlsTrustStoreResult::NotFound) {
            NodeTlsTrustStoreResult bind_result =
                NodeTlsTrustStoreResult::IoFailed;

            try {
                bind_result =
                    context.bind_node_tls_trust(
                        node_id,
                        requested_pin,
                        &trust_error
                    );
            } catch (...) {
                fail(
                    503,
                    "service_unavailable",
                    "failed to persist node TLS identity"
                );
                return;
            }

            if (bind_result ==
                NodeTlsTrustStoreResult::TrustConflict) {
                fail(
                    409,
                    "conflict",
                    "node TLS identity changed during verification"
                );
                return;
            }

            if (bind_result !=
                NodeTlsTrustStoreResult::Ok) {
                fail(
                    503,
                    "service_unavailable",
                    "failed to persist node TLS identity"
                );
                return;
            }
        } else if (
            trust_result !=
                NodeTlsTrustStoreResult::Ok) {
            fail(
                503,
                "service_unavailable",
                "node TLS trust store is unavailable"
            );
            return;
        }

        std::string registry_error;

        // Security: if registry persistence fails after the immutable binding
        // becomes durable, the same verified pin remains safe and the admin
        // operation can be retried idempotently.
        if (!context.registry->upsert_configured_node(
                node_id,
                normalized_url,
                current->enabled,
                canonicalize_cluster_runtime_timestamp(
                    context.now_iso_utc()
                ),
                &registry_error,
                std::nullopt)) {
            fail(
                500,
                "server_error",
                "failed to update node API URL"
            );
            return;
        }

        updated =
            context.registry->get(node_id);

        if (!updated.has_value()) {
            fail(
                500,
                "server_error",
                "updated node is unavailable"
            );
            return;
        }
    }

    audit_mutation(
        context,
        req,
        actor_fp,
        "admin.cluster_node.tls_trust_migration",
        node_id,
        "ok"
    );

    reply_json_local(
        context,
        res,
        200,
        json{
            {"ok", true},
            {"tls_trust_bound", true},
            {"node", node_record_to_json(*updated)}
        }
    );
}

void handle_admin_cluster_node_credential_rotation(
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
            "admin.cluster_node.credential_rotation",
            "",
            "fail"
        );
        return;
    }

    if (!has_only_keys(
            body,
            {"node_id"}) ||
        body.find("node_id") ==
            body.end() ||
        !body["node_id"].is_string()) {
        audit_mutation(
            context,
            req,
            actor_fp,
            "admin.cluster_node.credential_rotation",
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
                    "request must contain only node_id"
                }
            }
        );
        return;
    }

    const std::string node_id =
        body["node_id"].get<std::string>();

    if (!valid_cluster_node_id(
            node_id)) {
        audit_mutation(
            context,
            req,
            actor_fp,
            "admin.cluster_node.credential_rotation",
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
                {"message", "invalid node_id"}
            }
        );
        return;
    }

    long retry_after_seconds = 0;

    if (!consume_admin_rotation_rate_limit(
            actor_fp,
            &retry_after_seconds)) {
        if (retry_after_seconds < 1) {
            retry_after_seconds =
                kAdminClusterNodeRotationRateWindowSeconds;
        }

        res.set_header(
            "Retry-After",
            std::to_string(
                retry_after_seconds
            )
        );

        audit_mutation(
            context,
            req,
            actor_fp,
            "admin.cluster_node.credential_rotation",
            node_id,
            "fail"
        );

        reply_json_local(
            context,
            res,
            429,
            json{
                {"ok", false},
                {"error", "rate_limited"},
                {
                    "message",
                    "credential rotation rate limit exceeded"
                }
            }
        );
        return;
    }

    NodeCredentialRotationResult result;

    try {
        // Security: do not acquire placement_mutex here. rotate() establishes
        // its own short coordination boundaries and performs prepare/finalize
        // HTTPS only after releasing them.
        result =
            context.rotate_node_credential(
                node_id
            );
    } catch (...) {
        audit_mutation(
            context,
            req,
            actor_fp,
            "admin.cluster_node.credential_rotation",
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
                {"message", "credential rotation failed"}
            }
        );
        return;
    }

    int status = 500;

    json response{
        {"ok", false}
    };

    switch (result.code) {
        case NodeCredentialRotationServiceCode::Ok:
            status = 200;
            response = json{
                {"ok", true},
                {"node_id", node_id},
                {"rotation", "completed"},
                {
                    "director_credential_committed",
                    result.director_credential_committed
                },
                {
                    "storage_finalized",
                    result.storage_finalized
                }
            };
            break;

        case NodeCredentialRotationServiceCode::
                InvalidInput:
            status = 400;
            response["error"] =
                "bad_request";
            response["message"] =
                "node configuration is invalid for credential rotation";
            break;

        case NodeCredentialRotationServiceCode::
                NodeNotFound:
            status = 404;
            response["error"] =
                "not_found";
            response["message"] =
                "node not found";
            break;

        case NodeCredentialRotationServiceCode::
                NodeDisabled:
            status = 409;
            response["error"] =
                "conflict";
            response["message"] =
                "node is disabled";
            break;

        case NodeCredentialRotationServiceCode::
                RotationInProgress:
            status = 409;
            response["error"] =
                "conflict";
            response["message"] =
                "node credential rotation is already in progress";
            break;

        case NodeCredentialRotationServiceCode::
                NodeChanged:
            status = 409;
            response["error"] =
                "conflict";
            response["message"] =
                "node configuration changed during credential rotation";
            break;

        case NodeCredentialRotationServiceCode::
                CredentialStateConflict:
            status = 409;
            response["error"] =
                "conflict";
            response["message"] =
                "node credential state changed during rotation";
            break;

        case NodeCredentialRotationServiceCode::
                TlsTrustUnavailable:
            // Security: expose only the stable trust-state category. Never
            // return trust-store paths or underlying filesystem diagnostics.
            status = 503;
            response["error"] =
                "service_unavailable";
            response["message"] =
                "node TLS identity trust is unavailable";
            break;

        case NodeCredentialRotationServiceCode::
                CredentialUnavailable:
            status = 503;
            response["error"] =
                "service_unavailable";
            response["message"] =
                "node credential is unavailable";
            break;

        case NodeCredentialRotationServiceCode::
                PrepareFailed:
            status = 502;
            response["error"] =
                "upstream_error";
            response["message"] =
                "Storage credential rotation prepare failed";
            break;

        case NodeCredentialRotationServiceCode::
                CredentialCommitFailed:
            status = 500;
            response["error"] =
                "server_error";
            response["message"] =
                "failed to commit rotated credential";
            break;

        case NodeCredentialRotationServiceCode::
                FinalizePending:
            // Director NEW is already authoritative. Expose only lifecycle
            // state, never credential material or upstream diagnostics.
            status = 502;
            response["error"] =
                "finalize_pending";
            response["message"] =
                "Storage credential rotation finalization is unconfirmed";
            response["retryable"] = true;
            response["director_credential_committed"] =
                result.director_credential_committed;
            response["storage_finalized"] =
                result.storage_finalized;
            break;

        case NodeCredentialRotationServiceCode::
                ServiceUnavailable:
            status = 503;
            response["error"] =
                "service_unavailable";
            response["message"] =
                "credential rotation service is unavailable";
            break;
    }

    audit_mutation(
        context,
        req,
        actor_fp,
        "admin.cluster_node.credential_rotation",
        node_id,
        result.code ==
                NodeCredentialRotationServiceCode::Ok
            ? "ok"
            : "fail"
    );

    reply_json_local(
        context,
        res,
        status,
        response
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

    bool rotation_busy = false;

    if (!query_rotation_in_progress(
            context,
            node_id,
            &rotation_busy)) {
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
                    "failed to check node credential rotation state"
                }
            }
        );
        return;
    }

    if (rotation_busy) {
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
            409,
            json{
                {"ok", false},
                {"error", "conflict"},
                {
                    "message",
                    "node credential rotation is in progress"
                }
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

    NodeDeletionResult deletion_result;

    try {
        const std::string revoked_at =
            canonicalize_cluster_runtime_timestamp(
                context.now_iso_utc()
            );

        deletion_result =
            context.erase_unassigned_node(
                node_id,
                revoked_at
            );
    } catch (...) {
        deletion_result.result =
            NodeDeletionServiceResult::
                RegistryCleanupFailed;
    }

    if (!deletion_result.ok()) {
        audit_mutation(
            context,
            req,
            actor_fp,
            "admin.cluster_node.delete",
            node_id,
            "fail"
        );

        if (deletion_result.result ==
            NodeDeletionServiceResult::
                ProvisioningActive) {
            // Security: never tear down credentials underneath an authenticated
            // provisioning transaction that must finish or recover atomically.
            reply_json_local(
                context,
                res,
                409,
                json{
                    {"ok", false},
                    {"error", "conflict"},
                    {
                        "message",
                        "node provisioning is active"
                    }
                }
            );

            return;
        }

        if (deletion_result.result ==
            NodeDeletionServiceResult::
                ConfirmedProjectionActive) {
            // Security: never destroy a Node lifecycle while durable
            // confirmed Storage authorization still references that Node.
            // Do not expose user fingerprints.
            reply_json_local(
                context,
                res,
                409,
                json{
                    {"ok", false},
                    {"error", "conflict"},
                    {
                        "message",
                        "node still has confirmed Storage projection state"
                    }
                }
            );

            return;
        }

        if (deletion_result.result ==
            NodeDeletionServiceResult::NotFound) {
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

        // Security: do not expose credential, enrollment or persistence
        // diagnostics from the coordinated deletion transaction.
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

    server.Post(
        "/api/v4/admin/cluster/nodes/tls-trust/migration",
        [captured](
            const httplib::Request& req,
            httplib::Response& res
        ) {
            handle_admin_cluster_node_tls_trust_migration(
                req,
                res,
                captured
            );
        }
    );

    server.Post(
        "/api/v4/admin/cluster/nodes/credential/rotation",
        [captured](
            const httplib::Request& req,
            httplib::Response& res
        ) {
            handle_admin_cluster_node_credential_rotation(
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
