#pragma once

#include "cluster/storage_user_projection_sync_client.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace pqnas::cluster {

inline constexpr std::size_t
    kStorageUserProjectionSyncMaxResponseBytes =
        4u * 1024u;

// Strict parser for the Storage Node projection endpoint.
// expected_revision binds a successful response to the exact request.
StorageUserProjectionSyncTransportResponse
parse_storage_user_projection_sync_https_response(
    int http_status,
    const std::string& content_type,
    const std::string& body,
    std::uint64_t expected_revision
);

// Production Director -> Storage HTTPS transport.
//
// Security:
// - canonical HTTPS authority only
// - certificate-chain and hostname verification always enabled
// - redirects, decompression and keep-alive disabled
// - bounded body/response sizes and timeouts
// - short-lived per-node Authorization buffer wiped best-effort
StorageUserProjectionSyncTransport
make_https_storage_user_projection_sync_transport();

} // namespace pqnas::cluster
