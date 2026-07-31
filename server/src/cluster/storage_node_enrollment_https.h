#pragma once

#include "cluster/storage_node_enrollment_client.h"

#include <cstddef>
#include <string>

namespace pqnas::cluster {

inline constexpr std::size_t
    kStorageNodeEnrollmentMaxResponseBytes =
        4 * 1024;

// Strictly classifies the Director bootstrap route response. This helper is
// public so the protocol contract can be regression-tested without weakening
// production TLS verification.
StorageNodeEnrollmentTransportResponse
parse_storage_node_enrollment_https_response(
    int http_status,
    const std::string& content_type,
    const std::string& body
);

// Production storage-node-to-Director enrollment transport.
//
// Security:
// - HTTPS only
// - certificate-chain and hostname verification always enabled
// - redirects, decompression and keep-alive disabled
// - bounded request, response and timeout behavior
// - Authorization and JSON credential buffers are wiped best-effort
StorageNodeEnrollmentTransport
make_https_storage_node_enrollment_transport();

} // namespace pqnas::cluster
