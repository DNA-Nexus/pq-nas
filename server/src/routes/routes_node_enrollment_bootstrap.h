#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace httplib {
class Request;
class Response;
class Server;
}

namespace pqnas::cluster {

class NodeEnrollmentConsumptionService;

inline constexpr std::size_t
    kNodeEnrollmentBootstrapBodyMaxBytes =
        4 * 1024;

inline constexpr std::size_t
    kNodeEnrollmentBootstrapAuthorizationMaxBytes =
        128;

inline constexpr std::int64_t
    kNodeEnrollmentBootstrapRateWindowSeconds =
        60;

inline constexpr int
    kNodeEnrollmentBootstrapRateLimit =
        20;

struct NodeEnrollmentBootstrapRoutesContext {
    NodeEnrollmentConsumptionService* service =
        nullptr;

    // Security: consume persists credential -> NodeRegistry -> consumed state.
    // Node deletion must not interleave with that durable sequence.
    std::shared_ptr<std::mutex> lifecycle_mutex;

    std::function<void(
        httplib::Response&,
        int,
        const std::string&
    )> reply_json;

    std::function<std::string()>
        now_iso_utc;

    // Injectable for deterministic rate-limit tests.
    std::function<std::int64_t()>
        now_epoch_sec;
};

// Exposed separately so strict parsing, authentication ordering and secret
// response behavior can be tested without starting a network listener.
void handle_node_enrollment_bootstrap_consume(
    const httplib::Request& request,
    httplib::Response& response,
    const NodeEnrollmentBootstrapRoutesContext&
        context
);

void register_node_enrollment_bootstrap_routes(
    httplib::Server& server,
    const NodeEnrollmentBootstrapRoutesContext&
        context
);

} // namespace pqnas::cluster
