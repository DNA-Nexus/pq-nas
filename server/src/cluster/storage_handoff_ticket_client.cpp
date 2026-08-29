#include "cluster/storage_handoff_ticket_client.h"

#include "cluster/cluster_identity_validation.h"
#include "cluster/internal_auth.h"
#include "cluster/node_credential_registry.h"
#include "cluster/node_registry.h"
#include "cluster/node_tls_trust_store.h"
#include "cluster/storage_handoff_ticket_store.h"

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

StorageHandoffTicketClientCode
map_transport_code(
    StorageHandoffTicketTransportCode code
) noexcept {
    switch (code) {
        case StorageHandoffTicketTransportCode::Ok:
            return StorageHandoffTicketClientCode::Ok;

        case StorageHandoffTicketTransportCode::Rejected:
            return StorageHandoffTicketClientCode::Rejected;

        case StorageHandoffTicketTransportCode::
                AuthenticationFailed:
            return StorageHandoffTicketClientCode::
                AuthenticationFailed;

        case StorageHandoffTicketTransportCode::
                UserNotAuthorized:
            return StorageHandoffTicketClientCode::
                UserNotAuthorized;

        case StorageHandoffTicketTransportCode::
                RestartRequired:
            return StorageHandoffTicketClientCode::
                RestartRequired;

        case StorageHandoffTicketTransportCode::RateLimited:
            return StorageHandoffTicketClientCode::RateLimited;

        case StorageHandoffTicketTransportCode::
                ServiceUnavailable:
            return StorageHandoffTicketClientCode::
                ServiceUnavailable;

        case StorageHandoffTicketTransportCode::
                InvalidResponse:
            return StorageHandoffTicketClientCode::
                InvalidResponse;

        case StorageHandoffTicketTransportCode::
                TransportFailed:
            return StorageHandoffTicketClientCode::
                TransportFailed;
    }

    return StorageHandoffTicketClientCode::
        InvalidResponse;
}

} // namespace

const char* storage_handoff_ticket_client_code(
    StorageHandoffTicketClientCode code
) noexcept {
    switch (code) {
        case StorageHandoffTicketClientCode::Ok:
            return "ok";
        case StorageHandoffTicketClientCode::InvalidInput:
            return "invalid_input";
        case StorageHandoffTicketClientCode::ClientUnavailable:
            return "client_unavailable";
        case StorageHandoffTicketClientCode::NodeNotFound:
            return "node_not_found";
        case StorageHandoffTicketClientCode::NodeDisabled:
            return "node_disabled";
        case StorageHandoffTicketClientCode::TargetChanged:
            return "target_changed";
        case StorageHandoffTicketClientCode::TlsTrustUnavailable:
            return "tls_trust_unavailable";
        case StorageHandoffTicketClientCode::CredentialUnavailable:
            return "credential_unavailable";
        case StorageHandoffTicketClientCode::Rejected:
            return "rejected";
        case StorageHandoffTicketClientCode::AuthenticationFailed:
            return "authentication_failed";
        case StorageHandoffTicketClientCode::UserNotAuthorized:
            return "user_not_authorized";
        case StorageHandoffTicketClientCode::RestartRequired:
            return "restart_required";
        case StorageHandoffTicketClientCode::RateLimited:
            return "rate_limited";
        case StorageHandoffTicketClientCode::ServiceUnavailable:
            return "service_unavailable";
        case StorageHandoffTicketClientCode::InvalidResponse:
            return "invalid_response";
        case StorageHandoffTicketClientCode::TransportFailed:
            return "transport_failed";
    }

    return "unknown";
}

StorageHandoffTicketClient::
StorageHandoffTicketClient(
    StorageHandoffTicketClientConfig config
)
    : config_(std::move(config)) {}

StorageHandoffTicketClientResult
StorageHandoffTicketClient::issue(
    const std::string& target_node_id,
    const std::string& fingerprint,
    const std::string& browser_binding_hash
) const {
    const auto finish =
        [](
            StorageHandoffTicketClientCode code,
            int http_status = 0,
            std::string ticket = {},
            std::int64_t expires_at = 0
        ) {
            StorageHandoffTicketClientResult result;

            result.code = code;
            result.http_status = http_status;
            result.ticket = std::move(ticket);
            result.expires_at = expires_at;

            return result;
        };

    if (!config_.node_registry ||
        !config_.credential_registry ||
        !config_.tls_trust_store ||
        !config_.transport) {
        return finish(
            StorageHandoffTicketClientCode::
                ClientUnavailable
        );
    }

    if (!valid_cluster_node_id(
            target_node_id) ||
        !pqnas::valid_storage_user_fingerprint_hex(
            fingerprint) ||
        !valid_storage_handoff_browser_binding_hash(
            browser_binding_hash)) {
        return finish(
            StorageHandoffTicketClientCode::
                InvalidInput
        );
    }

    const auto node =
        config_.node_registry->get(
            target_node_id
        );

    if (!node.has_value()) {
        return finish(
            StorageHandoffTicketClientCode::
                NodeNotFound
        );
    }

    if (node->node_id !=
            target_node_id) {
        return finish(
            StorageHandoffTicketClientCode::
                InvalidInput
        );
    }

    if (!node->enabled) {
        return finish(
            StorageHandoffTicketClientCode::
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
            StorageHandoffTicketClientCode::
                InvalidInput
        );
    }

    StorageHandoffTicketTransportRequest
        request;

    request.api_base_url =
        node->api_base_url;

    request.fingerprint =
        fingerprint;

    request.browser_binding_hash =
        browser_binding_hash;

    std::string tls_trust_error;

    const NodeTlsTrustStoreResult
        tls_trust_result =
            config_.tls_trust_store->get(
                target_node_id,
                &request.tls_spki_sha256_pin,
                &tls_trust_error
            );

    // Security: never expose trust-store filesystem/detail errors at this
    // protocol boundary.
    tls_trust_error.clear();

    if (tls_trust_result !=
        NodeTlsTrustStoreResult::Ok) {
        return finish(
            StorageHandoffTicketClientCode::
                TlsTrustUnavailable
        );
    }

    // Security: Node configuration and TLS trust are mutable control-plane
    // state. Re-read them immediately before materializing reusable bearer
    // material and performing the authenticated external side effect.
    const auto current_node =
        config_.node_registry->get(
            target_node_id
        );

    if (!current_node.has_value()) {
        return finish(
            StorageHandoffTicketClientCode::
                NodeNotFound
        );
    }

    if (!current_node->enabled) {
        return finish(
            StorageHandoffTicketClientCode::
                NodeDisabled
        );
    }

    if (current_node->node_id !=
            target_node_id ||
        current_node->api_base_url !=
            request.api_base_url) {
        return finish(
            StorageHandoffTicketClientCode::
                TargetChanged
        );
    }

    std::string current_tls_pin;
    std::string current_tls_error;

    const NodeTlsTrustStoreResult
        current_tls_result =
            config_.tls_trust_store->get(
                target_node_id,
                &current_tls_pin,
                &current_tls_error
            );

    current_tls_error.clear();

    if (current_tls_result !=
        NodeTlsTrustStoreResult::Ok) {
        return finish(
            StorageHandoffTicketClientCode::
                TlsTrustUnavailable
        );
    }

    if (current_tls_pin !=
        request.tls_spki_sha256_pin) {
        return finish(
            StorageHandoffTicketClientCode::
                TargetChanged
        );
    }

    // Security: reusable bearer material is obtained only after the exact
    // target Node and its TLS identity have been revalidated.
    std::string credential_error;

    const NodeCredentialRegistryResult
        credential_result =
            config_.credential_registry->
                make_bearer_authorization(
                    target_node_id,
                    &request.authorization,
                    &credential_error
                );

    SensitiveStringWiper
        authorization_wiper(
            request.authorization
        );

    credential_error.clear();

    if (credential_result !=
        NodeCredentialRegistryResult::Ok) {
        return finish(
            StorageHandoffTicketClientCode::
                CredentialUnavailable
        );
    }

    StorageHandoffTicketTransportResponse
        response;

    try {
        response =
            config_.transport(request);
    } catch (...) {
        return finish(
            StorageHandoffTicketClientCode::
                TransportFailed
        );
    }

    const StorageHandoffTicketClientCode
        mapped_code =
            map_transport_code(
                response.code
            );

    if (mapped_code !=
        StorageHandoffTicketClientCode::Ok) {
        // Security: an error transport result must never leak a stray ticket.
        clear_storage_handoff_ticket_text(
            &response.ticket
        );

        return finish(
            mapped_code,
            response.http_status
        );
    }

    // Defense in depth: custom/test transports must satisfy the same success
    // contract as the production HTTPS parser.
    if (!valid_storage_handoff_ticket_text(
            response.ticket) ||
        response.expires_at <= 0) {
        clear_storage_handoff_ticket_text(
            &response.ticket
        );

        return finish(
            StorageHandoffTicketClientCode::
                InvalidResponse,
            response.http_status
        );
    }

    return finish(
        StorageHandoffTicketClientCode::Ok,
        response.http_status,
        std::move(response.ticket),
        response.expires_at
    );
}

} // namespace pqnas::cluster
