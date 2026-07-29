#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace httplib {
class Server;
struct Request;
struct Response;
}

namespace pqnas {

struct AuditEvent;

namespace cluster {

class UserPlacementPolicyRegistry;

inline constexpr std::size_t
    kAdminClusterPlacementPolicyBodyMaxBytes =
        4096;

struct AdminClusterPlacementPolicyRoutesContext {
    UserPlacementPolicyRegistry*
        registry = nullptr;

    // Shared with user policy assignment, placement preview, home-node
    // placement and node administration.
    std::shared_ptr<std::mutex>
        placement_mutex;

    // Aggregate policy references only. No fingerprints are exposed.
    std::function<std::unordered_map<
        std::string,
        std::size_t
    >()> assigned_user_counts;

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

    std::function<std::string()>
        now_iso_utc;

    std::function<void(
        const pqnas::AuditEvent&
    )> audit_append;
};

void handle_admin_cluster_placement_policies_list(
    const httplib::Request& request,
    httplib::Response& response,
    const AdminClusterPlacementPolicyRoutesContext&
        context
);

void handle_admin_cluster_placement_policies_upsert(
    const httplib::Request& request,
    httplib::Response& response,
    const AdminClusterPlacementPolicyRoutesContext&
        context
);

void handle_admin_cluster_placement_policies_delete(
    const httplib::Request& request,
    httplib::Response& response,
    const AdminClusterPlacementPolicyRoutesContext&
        context
);

void register_admin_cluster_placement_policy_routes(
    httplib::Server& server,
    const AdminClusterPlacementPolicyRoutesContext&
        context
);

} // namespace cluster
} // namespace pqnas
