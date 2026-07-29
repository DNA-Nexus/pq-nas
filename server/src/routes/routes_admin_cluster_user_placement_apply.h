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

class UserPlacementRecommendationApplyService;

inline constexpr std::size_t
    kAdminClusterUserPlacementApplyBodyMaxBytes =
        4096;

struct AdminClusterUserPlacementApplyRoutesContext {
    std::shared_ptr<
        UserPlacementRecommendationApplyService
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

void handle_admin_cluster_user_placement_apply(
    const httplib::Request& request,
    httplib::Response& response,
    const AdminClusterUserPlacementApplyRoutesContext&
        context
);

void register_admin_cluster_user_placement_apply_routes(
    httplib::Server& server,
    const AdminClusterUserPlacementApplyRoutesContext&
        context
);

} // namespace pqnas::cluster
