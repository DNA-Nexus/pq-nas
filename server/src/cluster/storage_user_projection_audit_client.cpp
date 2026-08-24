#include "cluster/storage_user_projection_audit_client.h"

#include "cluster/cluster_identity_validation.h"
#include "cluster/internal_auth.h"
#include "cluster/node_credential_registry.h"
#include "cluster/node_registry.h"
#include "cluster/node_tls_trust_store.h"

#include "storage_metadata_validation.h"

#include <string>
#include <utility>

namespace pqnas::cluster {
namespace {

class SensitiveStringWiper {
public:
    explicit SensitiveStringWiper(
        std::string& value
    ) noexcept
        : value_(value) {}

    ~SensitiveStringWiper() {
        clear_internal_auth_token_text(
            &value_
        );
    }

    SensitiveStringWiper(
        const SensitiveStringWiper&
    ) = delete;

    SensitiveStringWiper& operator=(
        const SensitiveStringWiper&
    ) = delete;

private:
    std::string& value_;
};

bool valid_digest_hex(
    const std::string& value
) {
    if (value.size() !=
        kStorageUserProjectionDigestHexChars) {
        return false;
    }

    for (unsigned char character : value) {
        const bool valid =
            (character >= '0' && character <= '9') ||
            (character >= 'a' && character <= 'f');

        if (!valid) {
            return false;
        }
    }

    return true;
}

bool fingerprint_in_range(
    const std::string& fingerprint,
    const std::string& range_start,
    const std::string& range_end
) {
    return
        (range_start.empty() ||
         fingerprint >= range_start) &&
        (range_end.empty() ||
         fingerprint < range_end);
}

StorageUserProjectionAuditClientCode
map_transport_code(
    StorageUserProjectionAuditTransportCode code
) noexcept {
    switch (code) {
        case StorageUserProjectionAuditTransportCode::Ok:
            return
                StorageUserProjectionAuditClientCode::Ok;

        case StorageUserProjectionAuditTransportCode::Rejected:
            return
                StorageUserProjectionAuditClientCode::Rejected;

        case StorageUserProjectionAuditTransportCode::
                AuthenticationFailed:
            return
                StorageUserProjectionAuditClientCode::
                    AuthenticationFailed;

        case StorageUserProjectionAuditTransportCode::WrongNode:
            return
                StorageUserProjectionAuditClientCode::WrongNode;

        case StorageUserProjectionAuditTransportCode::RateLimited:
            return
                StorageUserProjectionAuditClientCode::RateLimited;

        case StorageUserProjectionAuditTransportCode::
                ServiceUnavailable:
            return
                StorageUserProjectionAuditClientCode::
                    ServiceUnavailable;

        case StorageUserProjectionAuditTransportCode::
                InvalidResponse:
            return
                StorageUserProjectionAuditClientCode::
                    InvalidResponse;

        case StorageUserProjectionAuditTransportCode::
                TransportFailed:
            return
                StorageUserProjectionAuditClientCode::
                    TransportFailed;
    }

    return
        StorageUserProjectionAuditClientCode::
            InvalidResponse;
}

} // namespace

bool valid_storage_user_projection_audit_request(
    const StorageUserProjectionAuditRequest& request
) {
    if (!valid_cluster_node_id(
            request.node_id) ||
        (!request.range_start.empty() &&
         !pqnas::valid_storage_user_fingerprint_hex(
             request.range_start)) ||
        (!request.range_end.empty() &&
         !pqnas::valid_storage_user_fingerprint_hex(
             request.range_end)) ||
        (!request.range_start.empty() &&
         !request.range_end.empty() &&
         request.range_start >=
             request.range_end) ||
        request.expected.size() >
            kStorageUserProjectionAuditMaxExpected) {
        return false;
    }

    std::string previous_fingerprint;
    bool have_previous = false;

    for (const auto& entry :
         request.expected) {
        if (!pqnas::valid_storage_user_fingerprint_hex(
                entry.fingerprint) ||
            entry.revision == 0 ||
            !valid_digest_hex(
                entry.digest_hex) ||
            !fingerprint_in_range(
                entry.fingerprint,
                request.range_start,
                request.range_end) ||
            (have_previous &&
             entry.fingerprint <=
                 previous_fingerprint)) {
            return false;
        }

        previous_fingerprint =
            entry.fingerprint;
        have_previous = true;
    }

    return true;
}

const char* storage_user_projection_audit_client_code(
    StorageUserProjectionAuditClientCode code
) noexcept {
    switch (code) {
        case StorageUserProjectionAuditClientCode::Ok:
            return "ok";
        case StorageUserProjectionAuditClientCode::InvalidInput:
            return "invalid_input";
        case StorageUserProjectionAuditClientCode::ClientUnavailable:
            return "client_unavailable";
        case StorageUserProjectionAuditClientCode::NodeNotFound:
            return "node_not_found";
        case StorageUserProjectionAuditClientCode::NodeDisabled:
            return "node_disabled";
        case StorageUserProjectionAuditClientCode::TlsTrustUnavailable:
            return "tls_trust_unavailable";
        case StorageUserProjectionAuditClientCode::CredentialUnavailable:
            return "credential_unavailable";
        case StorageUserProjectionAuditClientCode::Rejected:
            return "rejected";
        case StorageUserProjectionAuditClientCode::AuthenticationFailed:
            return "authentication_failed";
        case StorageUserProjectionAuditClientCode::WrongNode:
            return "wrong_node";
        case StorageUserProjectionAuditClientCode::RateLimited:
            return "rate_limited";
        case StorageUserProjectionAuditClientCode::ServiceUnavailable:
            return "service_unavailable";
        case StorageUserProjectionAuditClientCode::InvalidResponse:
            return "invalid_response";
        case StorageUserProjectionAuditClientCode::TransportFailed:
            return "transport_failed";
    }

    return "unknown";
}

StorageUserProjectionAuditClient::
StorageUserProjectionAuditClient(
    StorageUserProjectionAuditClientConfig config
)
    : config_(std::move(config)) {}

StorageUserProjectionAuditClientResult
StorageUserProjectionAuditClient::audit(
    const StorageUserProjectionAuditRequest& audit_request
) const {
    const auto finish =
        [](
            StorageUserProjectionAuditClientCode code,
            int http_status = 0,
            StorageUserProjectionAuditResult result = {}
        ) {
            StorageUserProjectionAuditClientResult output;
            output.code = code;
            output.http_status = http_status;

            // Security: a failed transport/client state must never surface a
            // default complete=true audit result to a future caller.
            if (code !=
                StorageUserProjectionAuditClientCode::Ok) {
                result.complete = false;
            }

            output.result = std::move(result);
            return output;
        };

    if (!config_.node_registry ||
        !config_.credential_registry ||
        !config_.tls_trust_store ||
        !config_.transport) {
        return finish(
            StorageUserProjectionAuditClientCode::
                ClientUnavailable
        );
    }

    if (!valid_storage_user_projection_audit_request(
            audit_request)) {
        return finish(
            StorageUserProjectionAuditClientCode::
                InvalidInput
        );
    }

    const auto node =
        config_.node_registry->get(
            audit_request.node_id
        );

    if (!node.has_value()) {
        return finish(
            StorageUserProjectionAuditClientCode::
                NodeNotFound
        );
    }

    if (node->node_id !=
            audit_request.node_id ||
        !node->enabled) {
        return finish(
            node->enabled
                ? StorageUserProjectionAuditClientCode::
                      InvalidInput
                : StorageUserProjectionAuditClientCode::
                      NodeDisabled
        );
    }

    std::string normalized_url;

    if (!normalize_cluster_https_base_url(
            node->api_base_url,
            &normalized_url,
            nullptr) ||
        normalized_url !=
            node->api_base_url) {
        return finish(
            StorageUserProjectionAuditClientCode::
                InvalidInput
        );
    }

    StorageUserProjectionAuditTransportRequest
        transport_request;

    transport_request.api_base_url =
        node->api_base_url;

    transport_request.audit =
        audit_request;

    std::string tls_trust_error;

    const NodeTlsTrustStoreResult
        trust_result =
            config_.tls_trust_store->get(
                audit_request.node_id,
                &transport_request.tls_spki_sha256_pin,
                &tls_trust_error
            );

    tls_trust_error.clear();

    if (trust_result !=
        NodeTlsTrustStoreResult::Ok) {
        return finish(
            StorageUserProjectionAuditClientCode::
                TlsTrustUnavailable
        );
    }

    // Security: only after immutable TLS trust has been resolved may the
    // reusable per-node bearer credential be materialized.
    std::string credential_error;

    const NodeCredentialRegistryResult
        credential_result =
            config_.credential_registry
                ->make_bearer_authorization(
                    audit_request.node_id,
                    &transport_request.authorization,
                    &credential_error
                );

    SensitiveStringWiper authorization_wiper(
        transport_request.authorization
    );

    credential_error.clear();

    if (credential_result !=
        NodeCredentialRegistryResult::Ok) {
        return finish(
            StorageUserProjectionAuditClientCode::
                CredentialUnavailable
        );
    }

    StorageUserProjectionAuditTransportResponse
        transport_response;

    try {
        transport_response =
            config_.transport(
                transport_request
            );
    } catch (...) {
        return finish(
            StorageUserProjectionAuditClientCode::
                TransportFailed
        );
    }

    return finish(
        map_transport_code(
            transport_response.code
        ),
        transport_response.http_status,
        std::move(
            transport_response.result
        )
    );
}

} // namespace pqnas::cluster
