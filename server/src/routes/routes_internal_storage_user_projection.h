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

// Security: an authenticated but buggy/compromised Director must not be able
// to turn projection synchronization into an unbounded mutation/parser loop.
inline constexpr std::size_t
    kInternalStorageUserProjectionRateLimitRequests = 256;

inline constexpr long
    kInternalStorageUserProjectionRateLimitWindowSeconds = 10;

inline constexpr std::size_t
    kInternalStorageUserProjectionRateLimitTrackedPeers = 256;

struct InternalStorageUserProjectionRoutesContext {
    const InternalAuthCredential* credential = nullptr;

    StorageUserProjectionStore* projection_store =
        nullptr;

    // Stable configured identity of this Storage Node.
    std::string local_node_id;

    std::function<bool(
        const std::string& remote_address,
        long* retry_after_seconds
    )> consume_rate_limit_slot;

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

void register_internal_storage_user_projection_routes(
    httplib::Server& server,
    const InternalStorageUserProjectionRoutesContext& context
);

} // namespace pqnas::cluster
