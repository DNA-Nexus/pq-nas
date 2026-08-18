#include "cluster/storage_user_session_authenticator.h"

#include "cluster/cluster_identity_validation.h"
#include "cluster/storage_user_session_cookie.h"
#include "cluster/storage_user_session_policy.h"

#include <sodium.h>
#include <utility>

namespace pqnas::cluster {
namespace {

void clear_output(
    StorageUserSessionAuthenticatorOutput* output
) noexcept {
    if (!output) {
        return;
    }

    output->fingerprint.clear();
    output->role.clear();
    output->projection_revision = 0;
    output->expires_at = 0;
}

} // namespace

const char*
storage_user_session_authenticator_result_code(
    StorageUserSessionAuthenticatorResult result
) noexcept {
    switch (result) {
        case StorageUserSessionAuthenticatorResult::
                Authenticated:
            return "authenticated";

        case StorageUserSessionAuthenticatorResult::
                InvalidInput:
            return "invalid_input";

        case StorageUserSessionAuthenticatorResult::
                InvalidConfiguration:
            return "invalid_configuration";

        case StorageUserSessionAuthenticatorResult::
                Rejected:
            return "rejected";

        case StorageUserSessionAuthenticatorResult::
                ClockUnavailable:
            return "clock_unavailable";

        case StorageUserSessionAuthenticatorResult::
                InternalError:
            return "internal_error";
    }

    return "internal_error";
}

StorageUserSessionAuthenticator::
StorageUserSessionAuthenticator(
    StorageUserSessionAuthenticatorConfig config
)
    : config_(std::move(config)) {
}

bool StorageUserSessionAuthenticator::
config_valid() const noexcept {
    return
        config_.session_policy != nullptr &&
        config_.base_cookie_key != nullptr &&
        sodium_is_zero(
            config_.base_cookie_key,
            crypto_auth_hmacsha256_KEYBYTES
        ) == 0 &&
        valid_cluster_node_id(
            config_.local_node_id
        ) &&
        static_cast<bool>(
            config_.clock
        );
}

long StorageUserSessionAuthenticator::
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

StorageUserSessionAuthenticatorResult
StorageUserSessionAuthenticator::authenticate(
    const std::string& cookie_value,
    StorageUserSessionAuthenticatorOutput* output
) const noexcept {
    clear_output(output);

    if (!output) {
        return StorageUserSessionAuthenticatorResult::
            InvalidInput;
    }

    if (!config_valid()) {
        return StorageUserSessionAuthenticatorResult::
            InvalidConfiguration;
    }

    const long now =
        now_epoch_safe();

    if (now <= 0) {
        return StorageUserSessionAuthenticatorResult::
            ClockUnavailable;
    }

    try {
        std::string fingerprint;
        std::uint64_t cookie_revision = 0;
        long expires_at = 0;

        const StorageUserSessionCookieVerifyResult
            cookie_result =
                storage_user_session_cookie_verify(
                    config_.base_cookie_key,
                    config_.local_node_id,
                    cookie_value,
                    now,
                    &fingerprint,
                    &cookie_revision,
                    &expires_at
                );

        // Security: do not expose whether rejection came from MAC validity,
        // expiry, Node-domain mismatch or malformed authenticated identity.
        if (cookie_result !=
            StorageUserSessionCookieVerifyResult::
                Verified) {
            return StorageUserSessionAuthenticatorResult::
                Rejected;
        }

        StorageUserSessionAuthorization
            authorization;

        const StorageUserSessionPolicyResult
            authorization_result =
                config_.session_policy->authorize(
                    fingerprint,
                    &authorization
                );

        if (authorization_result !=
            StorageUserSessionPolicyResult::
                Authorized) {
            return StorageUserSessionAuthenticatorResult::
                Rejected;
        }

        // Security: the session authorization epoch must match exactly.
        //
        // This permanently invalidates an old cookie after any projection
        // revision change, including revoke followed by later re-placement.
        if (authorization.fingerprint !=
                fingerprint ||
            authorization.home_node !=
                config_.local_node_id ||
            authorization.projection_revision !=
                cookie_revision ||
            authorization.role !=
                kStorageUserSessionRole) {
            return StorageUserSessionAuthenticatorResult::
                Rejected;
        }

        output->fingerprint =
            authorization.fingerprint;

        output->role =
            authorization.role;

        output->projection_revision =
            authorization.projection_revision;

        output->expires_at =
            expires_at;

        return StorageUserSessionAuthenticatorResult::
            Authenticated;
    } catch (...) {
        clear_output(output);

        return StorageUserSessionAuthenticatorResult::
            InternalError;
    }
}

} // namespace pqnas::cluster
