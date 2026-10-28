#pragma once

#include <cstddef>

namespace httplib {
class Server;
struct Request;
struct Response;
}

namespace pqnas::cluster {

class StorageNodeCredentialRuntime;

// Security: credential mutation endpoints are deliberately much more tightly
// throttled than normal internal control-plane traffic.
inline constexpr std::size_t
    kInternalNodeCredentialRotationRateLimitRequests = 16;

inline constexpr long
    kInternalNodeCredentialRotationRateLimitWindowSeconds = 60;

inline constexpr std::size_t
    kInternalNodeCredentialRotationRateLimitTrackedPeers = 256;

struct InternalNodeCredentialRotationRoutesContext {
    // Security: the HTTP layer must call the runtime's atomic
    // authenticate+mutate operations. Do not split authorization from
    // prepare/finalize into separate route-level state transitions.
    StorageNodeCredentialRuntime* runtime = nullptr;
};

void handle_internal_node_credential_rotation_prepare_request(
    const httplib::Request& request,
    httplib::Response& response,
    const InternalNodeCredentialRotationRoutesContext& context
);

void handle_internal_node_credential_rotation_finalize_request(
    const httplib::Request& request,
    httplib::Response& response,
    const InternalNodeCredentialRotationRoutesContext& context
);

void register_internal_node_credential_rotation_routes(
    httplib::Server& server,
    const InternalNodeCredentialRotationRoutesContext& context
);

} // namespace pqnas::cluster
