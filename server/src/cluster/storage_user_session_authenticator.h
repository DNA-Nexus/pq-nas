#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace pqnas::cluster {

class StorageUserSessionPolicy;

enum class StorageUserSessionAuthenticatorResult {
    Authenticated,
    InvalidInput,
    InvalidConfiguration,

    // Security: malformed, expired, wrong-Node, revoked and stale-revision
    // sessions intentionally collapse into one authorization failure result.
    Rejected,

    ClockUnavailable,
    InternalError
};

const char*
storage_user_session_authenticator_result_code(
    StorageUserSessionAuthenticatorResult result
) noexcept;

using StorageUserSessionAuthenticatorClock =
    std::function<long()>;

struct StorageUserSessionAuthenticatorConfig {
    const StorageUserSessionPolicy*
        session_policy = nullptr;

    // Must point to exactly 32 bytes and outlive this authenticator.
    const unsigned char* base_cookie_key =
        nullptr;

    std::string local_node_id;

    StorageUserSessionAuthenticatorClock
        clock;
};

struct StorageUserSessionAuthenticatorOutput {
    std::string fingerprint;
    std::string role;

    std::uint64_t projection_revision = 0;

    long expires_at = 0;
};

class StorageUserSessionAuthenticator {
public:
    explicit StorageUserSessionAuthenticator(
        StorageUserSessionAuthenticatorConfig config
    );

    // Security:
    // 1. cryptographically verify the Node-domain-separated Storage cookie
    // 2. fetch current authorization through StorageUserSessionPolicy
    // 3. require exact cookie/current projection revision equality
    // 4. authorize only the fixed Storage "user" role
    //
    // No positive authorization is cached here.
    StorageUserSessionAuthenticatorResult
    authenticate(
        const std::string& cookie_value,
        StorageUserSessionAuthenticatorOutput* output
    ) const noexcept;

private:
    bool config_valid() const noexcept;
    long now_epoch_safe() const noexcept;

    StorageUserSessionAuthenticatorConfig
        config_;
};

} // namespace pqnas::cluster
