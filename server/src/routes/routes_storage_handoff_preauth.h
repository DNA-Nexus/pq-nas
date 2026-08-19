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

// Security: this browser-binding secret only bridges the browser's Storage
// preauth step to the short-lived Director-authorized handoff ticket.
inline constexpr long
    kStorageHandoffPreauthTtlSeconds = 120;

inline constexpr std::size_t
    kStorageHandoffPreauthMaxRequestBytes = 256;

// Security: preauth is intentionally unauthenticated, so keep the cheap
// stateless endpoint bounded per transport peer.
inline constexpr std::size_t
    kStorageHandoffPreauthRateLimitRequests = 32;

inline constexpr long
    kStorageHandoffPreauthRateLimitWindowSeconds = 10;

inline constexpr std::size_t
    kStorageHandoffPreauthRateLimitTrackedPeers = 512;

inline constexpr const char*
    kStorageHandoffPreauthCookieName =
        "pqnas_storage_preauth";

inline constexpr const char*
    kStorageHandoffConsumePath =
        "/api/storage/handoff/consume";

struct StorageHandoffPreauthRoutesContext {
    // Must be a canonical HTTPS base authority, for example:
    // https://storage-01.example.com
    //
    // Security: do not derive the trusted origin from Host or forwarded
    // headers supplied by the request.
    std::string expected_origin;

    std::function<bool(
        const std::string& remote_address,
        long* retry_after_seconds
    )> consume_rate_limit_slot;
};

// Exposed for deterministic route tests without opening a listener.
void handle_storage_handoff_preauth_request(
    const httplib::Request& request,
    httplib::Response& response,
    const StorageHandoffPreauthRoutesContext& context
);

void register_storage_handoff_preauth_routes(
    httplib::Server& server,
    const StorageHandoffPreauthRoutesContext& context
);

} // namespace pqnas::cluster
