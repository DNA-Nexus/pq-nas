#pragma once

#include "cluster/node_credential_rotation_service.h"

#include <cstddef>
#include <string>

namespace pqnas::cluster {

inline constexpr std::size_t
    kNodeCredentialRotationHttpsMaxResponseBytes =
        1024u;

// Strict parsers are exposed for deterministic protocol regression tests.
// Successful prepare responses contain plaintext credential material and the
// caller must securely clear pending_credential after use.
NodeCredentialRotationPrepareResponse
parse_node_credential_rotation_prepare_https_response(
    int http_status,
    const std::string& content_type,
    const std::string& body
);

NodeCredentialRotationFinalizeResponse
parse_node_credential_rotation_finalize_https_response(
    int http_status,
    const std::string& content_type,
    const std::string& body
);

// Production Director -> Storage credential-rotation transports.
//
// Security:
// - canonical HTTPS authority only
// - certificate-chain and hostname verification always enabled
// - redirects, decompression and keep-alive disabled
// - bounded response size and network timeouts
// - Authorization and response-body buffers wiped best-effort
NodeCredentialRotationPrepareTransport
make_https_node_credential_rotation_prepare_transport();

NodeCredentialRotationFinalizeTransport
make_https_node_credential_rotation_finalize_transport();

} // namespace pqnas::cluster
