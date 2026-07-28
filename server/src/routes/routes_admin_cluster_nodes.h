#pragma once

#include <cstddef>
#include <functional>
#include <string>

namespace httplib {
class Server;
struct Request;
struct Response;
}

namespace pqnas {

struct AuditEvent;

namespace cluster {

class NodeRegistry;

inline constexpr std::size_t
    kAdminClusterNodeRequestBodyMaxBytes = 4096;

struct AdminClusterNodeRoutesContext {
    NodeRegistry* registry = nullptr;

    std::function<bool(
        const httplib::Request&,
        httplib::Response&,
        std::string*
    )> require_admin_auth;

    std::function<bool(
        const httplib::Request&,
        httplib::Response&
    )> require_same_origin;

    std::function<void(
        httplib::Response&,
        int,
        const std::string&
    )> reply_json;

    std::function<std::string()> now_iso_utc;

    std::function<void(
        const pqnas::AuditEvent&
    )> audit_append;
};

// Handlers are exposed separately so auth, validation, persistence and audit
// behavior can be tested without starting an HTTP listener.
void handle_admin_cluster_nodes_list(
    const httplib::Request& req,
    httplib::Response& res,
    const AdminClusterNodeRoutesContext& context
);

void handle_admin_cluster_nodes_create(
    const httplib::Request& req,
    httplib::Response& res,
    const AdminClusterNodeRoutesContext& context
);

void handle_admin_cluster_nodes_update(
    const httplib::Request& req,
    httplib::Response& res,
    const AdminClusterNodeRoutesContext& context
);

void handle_admin_cluster_nodes_delete(
    const httplib::Request& req,
    httplib::Response& res,
    const AdminClusterNodeRoutesContext& context
);

void register_admin_cluster_node_routes(
    httplib::Server& server,
    const AdminClusterNodeRoutesContext& context
);

} // namespace cluster
} // namespace pqnas
