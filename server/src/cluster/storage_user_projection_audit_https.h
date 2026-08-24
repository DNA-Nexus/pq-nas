#pragma once

#include "cluster/storage_user_projection_audit_client.h"

#include <cstddef>
#include <string>

namespace pqnas::cluster {

inline constexpr std::size_t
    kStorageUserProjectionAuditHttpsMaxRequestBytes =
        256u * 1024u;

inline constexpr std::size_t
    kStorageUserProjectionAuditHttpsMaxResponseBytes =
        256u * 1024u;

// Strict response parser. The request manifest binds a success response to the
// exact node, lexical range and expected revisions sent by the Director.
StorageUserProjectionAuditTransportResponse
parse_storage_user_projection_audit_https_response(
    int http_status,
    const std::string& content_type,
    const std::string& body,
    const StorageUserProjectionAuditRequest& expected_request
);

// Production Director -> Storage authenticated, SPKI-pinned audit transport.
StorageUserProjectionAuditTransport
make_https_storage_user_projection_audit_transport();

} // namespace pqnas::cluster
