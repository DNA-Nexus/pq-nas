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
class StorageHandoffTicketStore;

inline constexpr std::size_t
    kInternalStorageHandoffMaxRequestBytes = 1024;

inline constexpr long
    kInternalStorageHandoffTicketTtlSeconds = 30;

// Security: keep the internal handoff endpoint from becoming an unbounded
// ticket-minting or request-parsing surface if an authenticated peer loops.
inline constexpr std::size_t
    kInternalStorageHandoffRateLimitRequests = 256;

inline constexpr long
    kInternalStorageHandoffRateLimitWindowSeconds = 10;

inline constexpr std::size_t
    kInternalStorageHandoffRateLimitTrackedPeers = 256;

struct InternalStorageHandoffRoutesContext {
    const InternalAuthCredential* credential = nullptr;

    StorageHandoffTicketStore* ticket_store =
        nullptr;

    std::function<bool(
        const std::string& remote_address,
        long* retry_after_seconds
    )> consume_rate_limit_slot;

    // Called only after successful service-to-service authentication and
    // strict request validation.
    //
    // The route deliberately does not know how cluster user projections are
    // represented. That policy is supplied by the Storage runtime.
    std::function<bool(
        const std::string& fingerprint
    )> authorize_user;

    std::function<void(
        httplib::Response&,
        int,
        const std::string&
    )> reply_json;
};

// Exposed for deterministic route tests without opening a listener.
void handle_internal_storage_handoff_ticket_request(
    const httplib::Request& request,
    httplib::Response& response,
    const InternalStorageHandoffRoutesContext& context
);

void register_internal_storage_handoff_routes(
    httplib::Server& server,
    const InternalStorageHandoffRoutesContext& context
);

} // namespace pqnas::cluster
