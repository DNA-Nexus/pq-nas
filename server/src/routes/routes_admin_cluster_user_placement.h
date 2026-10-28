#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>

namespace httplib {
class Server;
struct Request;
struct Response;
}

namespace pqnas::cluster {

class UserPlacementService;

inline constexpr std::size_t
    kAdminClusterUserPlacementBodyMaxBytes = 4096;

struct AdminClusterUserPlacementRoutesContext {
    // Shared ownership keeps the service alive for registered HTTP callbacks.
    std::shared_ptr<UserPlacementService> service;

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
};

// Exposed separately for deterministic unit testing.
void handle_admin_cluster_user_home_node_assign(
    const httplib::Request& request,
    httplib::Response& response,
    const AdminClusterUserPlacementRoutesContext& context
);

// Exposed separately for deterministic unit testing.
void handle_admin_cluster_user_home_node_unassign(
    const httplib::Request& request,
    httplib::Response& response,
    const AdminClusterUserPlacementRoutesContext& context
);

void register_admin_cluster_user_placement_routes(
    httplib::Server& server,
    const AdminClusterUserPlacementRoutesContext& context
);

} // namespace pqnas::cluster
