#include "cluster/storage_handoff_consume_service.h"

#include "cluster/cluster_identity_validation.h"
#include "cluster/storage_handoff_ticket_store.h"
#include "cluster/storage_user_session_cookie.h"
#include "cluster/storage_user_session_policy.h"

#include <limits>
#include <utility>

namespace pqnas::cluster {
namespace {

void clear_output(
    StorageHandoffConsumeServiceOutput* output
) noexcept {
    if (!output) {
        return;
    }

    clear_storage_user_session_cookie_text(
        &output->session_cookie_value
    );

    output->expires_at = 0;
}

} // namespace

const char*
storage_handoff_consume_service_result_code(
    StorageHandoffConsumeServiceResult result
) noexcept {
    switch (result) {
        case StorageHandoffConsumeServiceResult::Ok:
            return "ok";

        case StorageHandoffConsumeServiceResult::
                InvalidInput:
            return "invalid_input";

        case StorageHandoffConsumeServiceResult::
                InvalidConfiguration:
            return "invalid_configuration";

        case StorageHandoffConsumeServiceResult::
                Rejected:
            return "rejected";

        case StorageHandoffConsumeServiceResult::
                ClockUnavailable:
            return "clock_unavailable";

        case StorageHandoffConsumeServiceResult::
                SessionMintFailed:
            return "session_mint_failed";

        case StorageHandoffConsumeServiceResult::
                InternalError:
            return "internal_error";
    }

    return "internal_error";
}

StorageHandoffConsumeService::
StorageHandoffConsumeService(
    StorageHandoffConsumeServiceConfig config
)
    : config_(std::move(config)) {
}

bool StorageHandoffConsumeService::
config_valid() const noexcept {
    return
        config_.ticket_store != nullptr &&
        config_.session_policy != nullptr &&
        config_.base_cookie_key != nullptr &&
        valid_cluster_node_id(
            config_.local_node_id
        ) &&
        config_.session_ttl_seconds > 0 &&
        config_.session_ttl_seconds <=
            kStorageUserSessionCookieMaxTtlSeconds &&
        static_cast<bool>(config_.clock);
}

long StorageHandoffConsumeService::
now_epoch_safe() const noexcept {
    try {
        if (!config_.clock) {
            return 0;
        }

        return config_.clock();
    } catch (...) {
        return 0;
    }
}

StorageHandoffConsumeServiceResult
StorageHandoffConsumeService::consume(
    const std::string& ticket,
    StorageHandoffConsumeServiceOutput* output
) const noexcept {
    clear_output(output);

    if (!output) {
        return StorageHandoffConsumeServiceResult::
            InvalidInput;
    }

    // Validate static server configuration before burning a browser ticket.
    // Dynamic failures after successful ticket consumption intentionally do
    // burn the ticket and require a fresh handoff from the Director.
    if (!config_valid()) {
        return StorageHandoffConsumeServiceResult::
            InvalidConfiguration;
    }

    try {
        StorageHandoffTicketClaims claims;

        const StorageHandoffTicketResult
            ticket_result =
                config_.ticket_store->consume(
                    ticket,
                    &claims
                );

        // Security: do not distinguish malformed, missing, expired or failed
        // ticket verification at this service boundary.
        if (ticket_result !=
            StorageHandoffTicketResult::Ok) {
            return StorageHandoffConsumeServiceResult::
                Rejected;
        }

        // From this point onward the bearer is permanently spent.
        //
        // Security: a ticket minted for another Node must never reach local
        // projection authorization or session minting.
        if (claims.home_node !=
                config_.local_node_id ||
            claims.projection_revision == 0) {
            return StorageHandoffConsumeServiceResult::
                Rejected;
        }

        StorageUserSessionAuthorization
            authorization;

        const StorageUserSessionPolicyResult
            authorization_result =
                config_.session_policy->authorize(
                    claims.fingerprint,
                    &authorization
                );

        if (authorization_result !=
            StorageUserSessionPolicyResult::
                Authorized) {
            return StorageHandoffConsumeServiceResult::
                Rejected;
        }

        // Security: require the exact authorization snapshot that justified
        // ticket issuance. A newer OR older projection revision invalidates
        // the ticket; >= is deliberately not sufficient here.
        if (authorization.fingerprint !=
                claims.fingerprint ||
            authorization.home_node !=
                claims.home_node ||
            authorization.home_node !=
                config_.local_node_id ||
            authorization.projection_revision !=
                claims.projection_revision ||
            authorization.role !=
                kStorageUserSessionRole) {
            return StorageHandoffConsumeServiceResult::
                Rejected;
        }

        const long now =
            now_epoch_safe();

        if (now <= 0 ||
            config_.session_ttl_seconds >
                std::numeric_limits<long>::max() -
                    now) {
            return StorageHandoffConsumeServiceResult::
                ClockUnavailable;
        }

        const long expires_at =
            now +
            config_.session_ttl_seconds;

        std::string session_cookie;

        if (!storage_user_session_cookie_mint(
                config_.base_cookie_key,
                config_.local_node_id,
                authorization.fingerprint,
                authorization.projection_revision,
                now,
                expires_at,
                &session_cookie
            )) {
            clear_storage_user_session_cookie_text(
                &session_cookie
            );

            return StorageHandoffConsumeServiceResult::
                SessionMintFailed;
        }

        // Security: this service creates only the cryptographic session
        // envelope. Actual Storage access must still re-run
        // StorageUserSessionPolicy for every authenticated request.
        output->session_cookie_value.swap(
            session_cookie
        );

        output->expires_at =
            expires_at;

        clear_storage_user_session_cookie_text(
            &session_cookie
        );

        return StorageHandoffConsumeServiceResult::
            Ok;
    } catch (...) {
        clear_output(output);

        return StorageHandoffConsumeServiceResult::
            InternalError;
    }
}

} // namespace pqnas::cluster
