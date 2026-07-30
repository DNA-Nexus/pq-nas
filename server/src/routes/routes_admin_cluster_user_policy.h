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

class UserPlacementPolicyAssignmentService;

inline constexpr std::size_t
    kAdminClusterUserPolicyBodyMaxBytes = 4096;

struct AdminClusterUserPolicyRoutesContext {
    // Shared ownership keeps the service alive for registered callbacks.
    std::shared_ptr<
        UserPlacementPolicyAssignmentService
    > service;

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
void handle_admin_cluster_user_policy_assign(
    const httplib::Request& request,
    httplib::Response& response,
    const AdminClusterUserPolicyRoutesContext&
        context
);

void register_admin_cluster_user_policy_routes(
    httplib::Server& server,
    const AdminClusterUserPolicyRoutesContext&
        context
);

} // namespace pqnas::cluster
