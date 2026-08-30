#pragma once

#include "cluster/storage_handoff_ticket_client.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace httplib {
class Server;
struct Request;
struct Response;
}

namespace pqnas::cluster {

inline constexpr const char*
    kDirectorStorageHandoffStartPath =
        "/api/storage/handoff/start";

inline constexpr std::size_t
    kDirectorStorageHandoffStartMaxRequestBytes = 512;

// Handoff minting is an authenticated capability endpoint. Normal browser
// usage needs one request, so a deliberately low per-user ceiling is enough
// while still allowing occasional manual retries.
inline constexpr std::size_t
    kDirectorStorageHandoffStartRateLimitRequests = 8;

inline constexpr long
    kDirectorStorageHandoffStartRateLimitWindowSeconds = 60;

inline constexpr std::size_t
    kDirectorStorageHandoffStartRateLimitTrackedUsers = 4096;


// Snapshot of all Director-owned authority that must remain stable across
// the external Storage ticket-minting side effect.
struct DirectorStorageHandoffTarget {
    std::string home_node;
    std::uint64_t projection_revision = 0;

    // Internal Director -> Storage service endpoint.
    std::string api_base_url;

    // Public browser -> Storage endpoint.
    std::string browser_base_url;
};


enum class DirectorStorageHandoffTargetCode {
    Ok,
    UserNotAuthorized,
    Unavailable
};


struct DirectorStorageHandoffTargetResult {
    DirectorStorageHandoffTargetCode code =
        DirectorStorageHandoffTargetCode::Unavailable;

    DirectorStorageHandoffTarget target;
};


struct DirectorStorageHandoffStartRoutesContext {
    // Director's own browser origin. This is trusted configuration and must
    // never be derived from Host/X-Forwarded-* request headers.
    std::string expected_director_origin;

    // Browser handoff is intentionally cookie-only. Production must wire this
    // to require_user_cookie_users_actor(), never the mixed Bearer/cookie gate.
    std::function<bool(
        const httplib::Request&,
        httplib::Response&,
        std::string* fingerprint,
        std::string* role
    )> require_user_cookie;

    // Per authenticated user. The route supplies the canonical fingerprint.
    std::function<bool(
        const std::string& fingerprint,
        long* retry_after_seconds
    )> consume_rate_limit_slot;

    // Must re-read Director authorization, confirmed Storage projection and
    // Node browser/service routing from their authoritative stores.
    std::function<DirectorStorageHandoffTargetResult(
        const std::string& fingerprint
    )> resolve_authoritative_target;

    // Production wiring delegates to StorageHandoffTicketClient::issue().
    std::function<StorageHandoffTicketClientResult(
        const std::string& home_node,
        const std::string& fingerprint,
        const std::string& browser_binding_hash
    )> issue_ticket;
};


// Production-friendly bounded per-user limiter.
std::function<bool(
    const std::string& fingerprint,
    long* retry_after_seconds
)>
make_default_director_storage_handoff_start_rate_limiter();


// Exposed for deterministic tests without opening a listener.
void handle_director_storage_handoff_start_request(
    const httplib::Request& request,
    httplib::Response& response,
    const DirectorStorageHandoffStartRoutesContext& context
);


void register_director_storage_handoff_start_routes(
    httplib::Server& server,
    const DirectorStorageHandoffStartRoutesContext& context
);

} // namespace pqnas::cluster
