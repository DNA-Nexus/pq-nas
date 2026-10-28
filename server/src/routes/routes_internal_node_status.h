#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include <nlohmann/json.hpp>

namespace httplib {
class Server;
struct Request;
struct Response;
}

namespace pqnas::cluster {

class InternalAuthCredential;

inline constexpr std::int64_t
    kInternalNodeStatusRateWindowSeconds = 60;

inline constexpr int
    kInternalNodeStatusRateLimit = 60;

struct InternalNodeStatusRoutesContext {
    // Security: route code receives only a verifier. Credential rotation
    // can therefore accept active + pending credentials without exposing
    // either secret or coupling HTTP handlers to credential storage.
    std::function<bool(
        const std::string&
    )> verify_authorization;

    // Builds trusted, already-sanitized node status JSON after authentication.
    std::function<bool(
        nlohmann::json*,
        std::string*
    )> build_status_json;

    std::function<void(
        httplib::Response&,
        int,
        const std::string&
    )> reply_json;

    // Injectable for deterministic rate-limit tests.
    std::function<std::int64_t()> now_epoch_sec;
};

// Exposed separately so authentication and collection order can be tested
// without starting a network listener.
void handle_internal_node_status_request(
    const httplib::Request& req,
    httplib::Response& res,
    const InternalNodeStatusRoutesContext& context
);

void register_internal_node_status_routes(
    httplib::Server& server,
    const InternalNodeStatusRoutesContext& context
);

} // namespace pqnas::cluster
