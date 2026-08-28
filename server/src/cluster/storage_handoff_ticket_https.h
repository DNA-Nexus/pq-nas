#pragma once

#include "cluster/storage_handoff_ticket_client.h"

#include <cstddef>
#include <string>

namespace pqnas::cluster {

inline constexpr std::size_t
    kStorageHandoffTicketMaxResponseBytes =
        4u * 1024u;

// Strict parser for POST /internal/storage/handoff/ticket responses.
StorageHandoffTicketTransportResponse
parse_storage_handoff_ticket_https_response(
    int http_status,
    const std::string& content_type,
    const std::string& body
);

// Production Director -> Storage handoff-ticket transport.
//
// Security:
// - canonical HTTPS authority only
// - exact durable SPKI pin + normal CA/hostname verification
// - redirects, decompression and keep-alive disabled
// - bounded request/response sizes and timeouts
// - reusable Authorization material wiped best-effort
StorageHandoffTicketTransport
make_https_storage_handoff_ticket_transport();

} // namespace pqnas::cluster
