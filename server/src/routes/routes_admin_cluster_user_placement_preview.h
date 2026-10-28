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

class UserPlacementPreviewService;

inline constexpr std::size_t
    kAdminClusterUserPlacementPreviewBodyMaxBytes =
        4096;

struct AdminClusterUserPlacementPreviewRoutesContext {
    std::shared_ptr<
        UserPlacementPreviewService
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
void handle_admin_cluster_user_placement_preview(
    const httplib::Request& request,
    httplib::Response& response,
    const AdminClusterUserPlacementPreviewRoutesContext&
        context
);

void register_admin_cluster_user_placement_preview_routes(
    httplib::Server& server,
    const AdminClusterUserPlacementPreviewRoutesContext&
        context
);

} // namespace pqnas::cluster
