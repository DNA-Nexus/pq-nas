#include "cluster/storage_node_enrollment_client.h"

#include "cluster/internal_auth.h"
#include "cluster/node_enrollment_token.h"

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

bool valid_bootstrap_token(
    const std::string& token
) {
    if (token.empty()) {
        return false;
    }

    std::string verifier;

    const NodeEnrollmentTokenResult result =
        hash_node_enrollment_token(
            token,
            &verifier
        );

    clear_node_enrollment_token_text(
        &verifier
    );

    return
        result ==
        NodeEnrollmentTokenResult::Ok;
}

bool current_timestamp(
    const StorageNodeEnrollmentClientConfig&
        config,
    std::string* output
) {
    if (!output ||
        !config.now_iso_utc) {
        return false;
    }

    output->clear();

    try {
        *output =
            config.now_iso_utc();
    } catch (...) {
        output->clear();
    }

    return !output->empty();
}

StorageNodeEnrollmentClientResult finish(
    StorageNodeEnrollmentClientCode code,
    bool recovered = false,
    bool already_enrolled = false
) noexcept {
    StorageNodeEnrollmentClientResult result;

    result.code = code;
    result.ok =
        code ==
        StorageNodeEnrollmentClientCode::Ok;

    result.recovered =
        result.ok && recovered;

    result.already_enrolled =
        result.ok && already_enrolled;

    return result;
}

StorageNodeEnrollmentClientCode
map_store_result(
    StorageNodeProvisioningStoreResult result
) noexcept {
    switch (result) {
        case StorageNodeProvisioningStoreResult::
                Ok:
            return
                StorageNodeEnrollmentClientCode::
                    Ok;

        case StorageNodeProvisioningStoreResult::
                InvalidInput:
            return
                StorageNodeEnrollmentClientCode::
                    InvalidInput;

        case StorageNodeProvisioningStoreResult::
                StateMismatch:
            return
                StorageNodeEnrollmentClientCode::
                    StateConflict;

        case StorageNodeProvisioningStoreResult::
                AlreadyEnrolled:
            return
                StorageNodeEnrollmentClientCode::
                    Ok;

        case StorageNodeProvisioningStoreResult::
                PersistFailed:
            return
                StorageNodeEnrollmentClientCode::
                    PersistFailed;

        case StorageNodeProvisioningStoreResult::
                NotLoaded:
        case StorageNodeProvisioningStoreResult::
                CredentialMissing:
        case StorageNodeProvisioningStoreResult::
                CredentialInvalid:
        case StorageNodeProvisioningStoreResult::
                StorageUnsafe:
        case StorageNodeProvisioningStoreResult::
                IoFailed:
            return
                StorageNodeEnrollmentClientCode::
                    LocalStateUnavailable;
    }

    return
        StorageNodeEnrollmentClientCode::
            LocalStateUnavailable;
}

StorageNodeEnrollmentClientCode
map_transport_result(
    StorageNodeEnrollmentTransportCode result
) noexcept {
    switch (result) {
        case StorageNodeEnrollmentTransportCode::
                Ok:
            return
                StorageNodeEnrollmentClientCode::
                    Ok;

        case StorageNodeEnrollmentTransportCode::
                AuthenticationFailed:
            return
                StorageNodeEnrollmentClientCode::
                    AuthenticationFailed;

        case StorageNodeEnrollmentTransportCode::
                Conflict:
            return
                StorageNodeEnrollmentClientCode::
                    DirectorConflict;

        case StorageNodeEnrollmentTransportCode::
                RateLimited:
            return
                StorageNodeEnrollmentClientCode::
                    RateLimited;

        case StorageNodeEnrollmentTransportCode::
                ServiceUnavailable:
        case StorageNodeEnrollmentTransportCode::
                TransportFailed:
            return
                StorageNodeEnrollmentClientCode::
                    DirectorUnavailable;

        case StorageNodeEnrollmentTransportCode::
                InvalidResponse:
            return
                StorageNodeEnrollmentClientCode::
                    ProtocolError;
    }

    return
        StorageNodeEnrollmentClientCode::
            ProtocolError;
}

} // namespace

const char* storage_node_enrollment_transport_code(
    StorageNodeEnrollmentTransportCode code
) noexcept {
    switch (code) {
        case StorageNodeEnrollmentTransportCode::
                Ok:
            return
                "storage_node_enrollment_transport_ok";

        case StorageNodeEnrollmentTransportCode::
                AuthenticationFailed:
            return
                "storage_node_enrollment_authentication_failed";

        case StorageNodeEnrollmentTransportCode::
                Conflict:
            return
                "storage_node_enrollment_conflict";

        case StorageNodeEnrollmentTransportCode::
                RateLimited:
            return
                "storage_node_enrollment_rate_limited";

        case StorageNodeEnrollmentTransportCode::
                ServiceUnavailable:
            return
                "storage_node_enrollment_service_unavailable";

        case StorageNodeEnrollmentTransportCode::
                TransportFailed:
            return
                "storage_node_enrollment_transport_failed";

        case StorageNodeEnrollmentTransportCode::
                InvalidResponse:
            return
                "storage_node_enrollment_invalid_response";
    }

    return
        "storage_node_enrollment_transport_unknown";
}

const char* storage_node_enrollment_client_code(
    StorageNodeEnrollmentClientCode code
) noexcept {
    switch (code) {
        case StorageNodeEnrollmentClientCode::
                Ok:
            return
                "storage_node_enrollment_ok";

        case StorageNodeEnrollmentClientCode::
                InvalidInput:
            return
                "storage_node_enrollment_invalid_input";

        case StorageNodeEnrollmentClientCode::
                ClientUnavailable:
            return
                "storage_node_enrollment_client_unavailable";

        case StorageNodeEnrollmentClientCode::
                LocalStateUnavailable:
            return
                "storage_node_enrollment_local_state_unavailable";

        case StorageNodeEnrollmentClientCode::
                StateConflict:
            return
                "storage_node_enrollment_state_conflict";

        case StorageNodeEnrollmentClientCode::
                AuthenticationFailed:
            return
                "storage_node_enrollment_authentication_failed";

        case StorageNodeEnrollmentClientCode::
                DirectorConflict:
            return
                "storage_node_enrollment_director_conflict";

        case StorageNodeEnrollmentClientCode::
                RateLimited:
            return
                "storage_node_enrollment_rate_limited";

        case StorageNodeEnrollmentClientCode::
                DirectorUnavailable:
            return
                "storage_node_enrollment_director_unavailable";

        case StorageNodeEnrollmentClientCode::
                ProtocolError:
            return
                "storage_node_enrollment_protocol_error";

        case StorageNodeEnrollmentClientCode::
                PersistFailed:
            return
                "storage_node_enrollment_persist_failed";
    }

    return
        "storage_node_enrollment_unknown";
}

const char*
StorageNodeEnrollmentClientResult::error_code()
    const noexcept {
    return storage_node_enrollment_client_code(
        code
    );
}

StorageNodeEnrollmentClient::
StorageNodeEnrollmentClient(
    StorageNodeEnrollmentClientConfig config
)
    : config_(std::move(config)) {}

StorageNodeEnrollmentClientResult
StorageNodeEnrollmentClient::enroll(
    const StorageNodeProvisioningIntent& intent,
    const std::string& bootstrap_token
) {
    if (!config_.store ||
        !config_.transport ||
        !config_.now_iso_utc) {
        return finish(
            StorageNodeEnrollmentClientCode::
                ClientUnavailable
        );
    }

    const auto prior =
        config_.store->snapshot();

    const bool persisted_credential_retry =
        prior.has_value() &&
        (
            prior->stage ==
                StorageNodeProvisioningStage::
                    CredentialPersisted ||
            prior->stage ==
                StorageNodeProvisioningStage::
                    Enrolled
        );

    if (bootstrap_token.empty() &&
        !persisted_credential_retry) {
        return finish(
            StorageNodeEnrollmentClientCode::
                InvalidInput
        );
    }

    if (!bootstrap_token.empty() &&
        !valid_bootstrap_token(
            bootstrap_token)) {
        return finish(
            StorageNodeEnrollmentClientCode::
                InvalidInput
        );
    }

    std::string prepare_time;

    if (!current_timestamp(
            config_,
            &prepare_time)) {
        return finish(
            StorageNodeEnrollmentClientCode::
                ClientUnavailable
        );
    }

    std::string node_credential;
    SensitiveStringWiper credential_wiper(
        node_credential
    );

    StorageNodeProvisioningRecord prepared;
    std::string internal_error;

    const StorageNodeProvisioningStoreResult
        prepare_result =
            config_.store
                ->prepare_for_bootstrap(
                    intent,
                    prepare_time,
                    &node_credential,
                    &prepared,
                    &internal_error
                );

    internal_error.clear();

    if (prepare_result ==
        StorageNodeProvisioningStoreResult::
            AlreadyEnrolled) {
        return finish(
            StorageNodeEnrollmentClientCode::Ok,
            true,
            true
        );
    }

    if (prepare_result !=
        StorageNodeProvisioningStoreResult::Ok) {
        return finish(
            map_store_result(
                prepare_result
            )
        );
    }

    if (prepared.stage !=
            StorageNodeProvisioningStage::
                CredentialPersisted ||
        !valid_internal_auth_credential_token(
            node_credential)) {
        return finish(
            StorageNodeEnrollmentClientCode::
                LocalStateUnavailable
        );
    }

    const auto invoke =
        [&](const std::string& authorization_token) {
            StorageNodeEnrollmentTransportRequest
                request;

            request.director_url =
                prepared.director_url;

            request.authorization_token =
                authorization_token;

            SensitiveStringWiper authorization_wiper(
                request.authorization_token
            );

            request.enrollment_id =
                prepared.enrollment_id;

            request.expected_generation =
                prepared.expected_generation;

            request.node_id =
                prepared.node_id;

            request.api_base_url =
                prepared.api_base_url;

            request.protocol_version =
                prepared.protocol_version;

            request.node_credential =
                node_credential;

            SensitiveStringWiper request_credential_wiper(
                request.node_credential
            );

            try {
                return config_.transport(
                    request
                );
            } catch (...) {
                return
                    StorageNodeEnrollmentTransportResponse{
                        StorageNodeEnrollmentTransportCode::
                            TransportFailed,
                        false,
                        0
                    };
            }
        };

    StorageNodeEnrollmentTransportResponse
        response;

    if (!bootstrap_token.empty()) {
        response =
            invoke(
                bootstrap_token
            );

        if (response.code ==
            StorageNodeEnrollmentTransportCode::
                AuthenticationFailed) {
            // Security: retry only the generic authentication failure. This
            // closes the crash window after Director commit without treating
            // conflicts, throttling or server errors as credential challenges.
            response =
                invoke(
                    node_credential
                );
        }
    } else {
        response =
            invoke(
                node_credential
            );
    }

    if (response.code !=
        StorageNodeEnrollmentTransportCode::Ok) {
        return finish(
            map_transport_result(
                response.code
            )
        );
    }

    std::string completion_time;

    if (!current_timestamp(
            config_,
            &completion_time)) {
        return finish(
            StorageNodeEnrollmentClientCode::
                ClientUnavailable
        );
    }

    StorageNodeProvisioningRecord enrolled;

    const StorageNodeProvisioningStoreResult
        completion_result =
            config_.store->mark_enrolled(
                intent,
                completion_time,
                &enrolled,
                &internal_error
            );

    internal_error.clear();

    if (completion_result !=
        StorageNodeProvisioningStoreResult::Ok) {
        return finish(
            map_store_result(
                completion_result
            )
        );
    }

    return finish(
        StorageNodeEnrollmentClientCode::Ok,
        response.recovered,
        false
    );
}

} // namespace pqnas::cluster
