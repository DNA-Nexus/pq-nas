#pragma once

#include <cstddef>
#include <functional>
#include <string>

namespace httplib {
class Server;
struct Request;
struct Response;
}

namespace pqnas::cluster {

class InternalAuthCredential;
class StorageUserProjectionStore;

inline constexpr std::size_t
    kInternalStorageUserProjectionMaxRequestBytes =
        8u * 1024u;

// RT-008 readback carries up to 512 compact expected identities. Bound the
// body before JSON parsing so authenticated control-plane traffic cannot turn
// verification into an unbounded parser/allocation path.
inline constexpr std::size_t
    kInternalStorageUserProjectionAuditMaxRequestBytes =
        256u * 1024u;

inline constexpr std::size_t
    kInternalStorageUserProjectionAuditRateLimitRequests = 64;

inline constexpr long
    kInternalStorageUserProjectionAuditRateLimitWindowSeconds = 10;

inline constexpr std::size_t
    kInternalStorageUserProjectionAuditRateLimitTrackedPeers = 64;

// Security: an authenticated but buggy/compromised Director must not be able
// to turn projection synchronization into an unbounded mutation/parser loop.
inline constexpr std::size_t
    kInternalStorageUserProjectionRateLimitRequests = 256;

inline constexpr long
    kInternalStorageUserProjectionRateLimitWindowSeconds = 10;

inline constexpr std::size_t
    kInternalStorageUserProjectionRateLimitTrackedPeers = 256;

struct InternalStorageUserProjectionRoutesContext {
    // Security: route code receives only a verifier. Credential rotation
    // can therefore accept active + pending credentials without exposing
    // either secret or coupling HTTP handlers to credential storage.
    std::function<bool(
        const std::string&
    )> verify_authorization;

    StorageUserProjectionStore* projection_store =
        nullptr;

    // Stable configured identity of this Storage Node.
    std::string local_node_id;

    std::function<bool(
        const std::string& remote_address,
        long* retry_after_seconds
    )> consume_rate_limit_slot;

    // Readback is separately throttled so a large audit cannot starve
    // authorization mutation traffic.
    std::function<bool(
        const std::string& remote_address,
        long* retry_after_seconds
    )> consume_audit_rate_limit_slot;

    std::function<void(
        httplib::Response&,
        int,
        const std::string&
    )> reply_json;
};

// Exposed for deterministic tests without opening a listener.
void handle_internal_storage_user_projection_request(
    const httplib::Request& request,
    httplib::Response& response,
    const InternalStorageUserProjectionRoutesContext& context
);

// RT-008 authenticated read-only proof endpoint. Exposed separately for
// deterministic tests without opening a listener.
void handle_internal_storage_user_projection_audit_request(
    const httplib::Request& request,
    httplib::Response& response,
    const InternalStorageUserProjectionRoutesContext& context
);

void register_internal_storage_user_projection_routes(
    httplib::Server& server,
    const InternalStorageUserProjectionRoutesContext& context
);

} // namespace pqnas::cluster
