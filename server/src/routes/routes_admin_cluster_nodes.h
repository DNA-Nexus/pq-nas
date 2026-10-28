#pragma once

#include "cluster/node_deletion_service.h"
#include "cluster/node_credential_rotation_service.h"
#include "cluster/node_tls_trust_store.h"

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

class NodeRegistry;

inline constexpr std::size_t
    kAdminClusterNodeRequestBodyMaxBytes = 4096;

// Security: credential rotation performs remote privileged state changes.
// Limit only authenticated admin actors so unauthenticated traffic cannot
// consume the mutation budget.
inline constexpr std::size_t
    kAdminClusterNodeRotationRateLimit = 8;

inline constexpr long
    kAdminClusterNodeRotationRateWindowSeconds = 60;

inline constexpr std::size_t
    kAdminClusterNodeRotationTrackedActors = 256;

struct AdminClusterNodeRoutesContext {
    NodeRegistry* registry = nullptr;

    // Security: legacy RT014 migration verifies the candidate endpoint before
    // establishing the immutable first TLS identity binding.
    std::function<NodeTlsTrustStoreResult(
        const std::string&,
        std::string*,
        std::string*
    )> get_node_tls_trust;

    std::function<NodeTlsTrustStoreResult(
        const std::string&,
        const std::string&,
        std::string*
    )> bind_node_tls_trust;

    // Security: this callback performs network I/O and must never be invoked
    // while placement/lifecycle locks are held.
    std::function<bool(
        const std::string&,
        const std::string&,
        std::string*
    )> verify_node_tls_endpoint;

    // Shared with UserPlacementService to prevent placement from racing node
    // disable or deletion.
    std::shared_ptr<std::mutex>
        placement_mutex;

    // Aggregate assignment information only.
    std::function<std::unordered_map<
        std::string,
        std::size_t
    >()> assigned_user_counts;

    // Security: UPDATE and DELETE query this while holding placement_mutex.
    // Rotation establishes its busy marker under the same mutex before
    // releasing it for network I/O, closing the lifecycle race.
    std::function<bool(
        const std::string&
    )> rotation_in_progress;

    // Security: this callback may perform network I/O. Callers must never hold
    // placement_mutex or another global lifecycle lock while invoking it.
    std::function<NodeCredentialRotationResult(
        const std::string&
    )> rotate_node_credential;

    // Security: node deletion is a coordinated lifecycle transaction that
    // revokes pending bootstrap state and the long-lived bearer before the
    // trusted NodeRegistry entry disappears.
    std::function<NodeDeletionResult(
        const std::string&,
        const std::string&
    )> erase_unassigned_node;

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

void handle_admin_cluster_node_tls_trust_migration(
    const httplib::Request& req,
    httplib::Response& res,
    const AdminClusterNodeRoutesContext& context
);

void handle_admin_cluster_node_credential_rotation(
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
