#pragma once

#include "cluster/storage_handoff_consume_service.h"

#include <cstddef>
#include <functional>
#include <string>

namespace httplib {
class Server;
struct Request;
struct Response;
}

namespace pqnas::cluster {

inline constexpr std::size_t
    kStorageHandoffConsumeMaxRequestBytes = 64;

inline constexpr std::size_t
    kStorageHandoffConsumeMaxCookieHeaderBytes = 4096;

inline constexpr std::size_t
    kStorageHandoffConsumeRateLimitRequests = 32;

inline constexpr long
    kStorageHandoffConsumeRateLimitWindowSeconds = 10;

inline constexpr std::size_t
    kStorageHandoffConsumeRateLimitTrackedPeers = 512;

inline constexpr const char*
    kStorageUserSessionCookieName =
        "pqnas_storage_session";

using StorageHandoffConsumeRouteService =
    std::function<
        StorageHandoffConsumeServiceResult(
            const std::string& ticket,
            const std::string& browser_binding_secret,
            StorageHandoffConsumeServiceOutput* output
        )
    >;

struct StorageHandoffConsumeRoutesContext {
    std::string expected_director_origin;

    std::function<bool(
        const std::string& remote_address,
        long* retry_after_seconds
    )> consume_rate_limit_slot;

    StorageHandoffConsumeRouteService consume;

    std::function<long()> now_epoch;
};

void handle_storage_handoff_consume_request(
    const httplib::Request& request,
    httplib::Response& response,
    const StorageHandoffConsumeRoutesContext& context
);

void register_storage_handoff_consume_routes(
    httplib::Server& server,
    const StorageHandoffConsumeRoutesContext& context
);

} // namespace pqnas::cluster
