#include "cluster/storage_user_session_authenticator.h"

#include "cluster/storage_user_session_cookie.h"
#include "cluster/storage_user_session_policy.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

using pqnas::cluster::
    StorageUserProjection;

using pqnas::cluster::
    StorageUserSessionAuthenticator;

using pqnas::cluster::
    StorageUserSessionAuthenticatorConfig;

using pqnas::cluster::
    StorageUserSessionAuthenticatorOutput;

using pqnas::cluster::
    StorageUserSessionAuthenticatorResult;

using pqnas::cluster::
    StorageUserSessionPolicy;

int failures = 0;

void expect(
    bool condition,
    const std::string& message
) {
    if (!condition) {
        ++failures;

        std::cerr
            << "FAIL: "
            << message
            << '\n';
    }
}

void expect_result(
    StorageUserSessionAuthenticatorResult actual,
    StorageUserSessionAuthenticatorResult expected,
    const std::string& message
) {
    expect(
        actual == expected,
        message +
            ": expected " +
            pqnas::cluster::
                storage_user_session_authenticator_result_code(
                    expected
                ) +
            ", got " +
            pqnas::cluster::
                storage_user_session_authenticator_result_code(
                    actual
                )
    );
}

StorageUserProjection valid_projection(
    const std::string& fingerprint,
    std::uint64_t revision
) {
    StorageUserProjection projection;

    projection.fingerprint =
        fingerprint;

    projection.enabled = true;
    projection.storage_allocated = true;
    projection.quota_bytes = 1024;

    projection.root_rel =
        "users/example";

    projection.storage_pool_id.clear();

    projection.home_node =
        "storage-01";

    projection.revision =
        revision;

    return projection;
}

} // namespace

int main() {
    const std::string fingerprint(
        128,
        'a'
    );

    const std::string local_node =
        "storage-01";

    std::array<unsigned char, 32>
        base_key{};

    for (std::size_t i = 0;
         i < base_key.size();
         ++i) {
        base_key[i] =
            static_cast<unsigned char>(
                i + 1
            );
    }

    std::optional<StorageUserProjection>
        current_projection =
            valid_projection(
                fingerprint,
                17
            );

    int lookup_calls = 0;

    StorageUserSessionPolicy::Config
        policy_config;

    policy_config.local_node_id =
        local_node;

    policy_config.lookup_projection =
        [&](const std::string& candidate)
            -> std::optional<StorageUserProjection> {
            ++lookup_calls;

            if (!current_projection.has_value() ||
                current_projection->fingerprint !=
                    candidate) {
                return std::nullopt;
            }

            return current_projection;
        };

    StorageUserSessionPolicy policy(
        std::move(policy_config)
    );

    long now = 1200;

    StorageUserSessionAuthenticatorConfig
        config;

    config.session_policy =
        &policy;

    config.base_cookie_key =
        base_key.data();

    config.local_node_id =
        local_node;

    config.clock =
        [&]() -> long {
            return now;
        };

    StorageUserSessionAuthenticator
        authenticator(
            config
        );

    std::string cookie;

    expect(
        pqnas::cluster::
            storage_user_session_cookie_mint(
                base_key.data(),
                local_node,
                fingerprint,
                17,
                1000,
                1600,
                &cookie
            ),
        "rev17 Storage cookie fixture mints"
    );

    StorageUserSessionAuthenticatorOutput
        output;

    // ------------------------------------------------------------------
    // Valid cookie + current rev17 projection.
    // ------------------------------------------------------------------
    expect_result(
        authenticator.authenticate(
            cookie,
            &output
        ),
        StorageUserSessionAuthenticatorResult::
            Authenticated,
        "matching rev17 Storage session authenticates"
    );

    expect(
        output.fingerprint ==
            fingerprint &&
        output.role == "user" &&
        output.projection_revision == 17 &&
        output.expires_at == 1600,
        "successful authenticator output is minimal and revision-bound"
    );


    // ------------------------------------------------------------------
    // Newer projection invalidates old cookie.
    // ------------------------------------------------------------------
    current_projection =
        valid_projection(
            fingerprint,
            18
        );

    output.fingerprint =
        "stale";

    output.role =
        "admin";

    output.projection_revision =
        9999;

    output.expires_at =
        9999;

    expect_result(
        authenticator.authenticate(
            cookie,
            &output
        ),
        StorageUserSessionAuthenticatorResult::
            Rejected,
        "rev17 cookie rejects current rev18 projection"
    );

    expect(
        output.fingerprint.empty() &&
        output.role.empty() &&
        output.projection_revision == 0 &&
        output.expires_at == 0,
        "stale authorization output is cleared on rejection"
    );


    // ------------------------------------------------------------------
    // Security regression: revoked session must not resurrect after a later
    // re-placement on the same Node.
    // ------------------------------------------------------------------
    current_projection =
        valid_projection(
            fingerprint,
            19
        );

    expect_result(
        authenticator.authenticate(
            cookie,
            &output
        ),
        StorageUserSessionAuthenticatorResult::
            Rejected,
        "rev17 cookie stays invalid after rev19 re-placement"
    );


    // ------------------------------------------------------------------
    // Disabled projection.
    // ------------------------------------------------------------------
    current_projection =
        valid_projection(
            fingerprint,
            17
        );

    current_projection->enabled =
        false;

    expect_result(
        authenticator.authenticate(
            cookie,
            &output
        ),
        StorageUserSessionAuthenticatorResult::
            Rejected,
        "disabled current projection rejects session"
    );


    // ------------------------------------------------------------------
    // Unallocated projection.
    // ------------------------------------------------------------------
    current_projection =
        valid_projection(
            fingerprint,
            17
        );

    current_projection->
        storage_allocated = false;

    expect_result(
        authenticator.authenticate(
            cookie,
            &output
        ),
        StorageUserSessionAuthenticatorResult::
            Rejected,
        "unallocated current projection rejects session"
    );


    // ------------------------------------------------------------------
    // Wrong current home Node.
    // ------------------------------------------------------------------
    current_projection =
        valid_projection(
            fingerprint,
            17
        );

    current_projection->home_node =
        "storage-02";

    expect_result(
        authenticator.authenticate(
            cookie,
            &output
        ),
        StorageUserSessionAuthenticatorResult::
            Rejected,
        "foreign current home Node rejects session"
    );


    // ------------------------------------------------------------------
    // Missing projection.
    // ------------------------------------------------------------------
    current_projection.reset();

    expect_result(
        authenticator.authenticate(
            cookie,
            &output
        ),
        StorageUserSessionAuthenticatorResult::
            Rejected,
        "missing current projection rejects session"
    );


    // ------------------------------------------------------------------
    // Invalid cookie must be rejected before projection lookup.
    // ------------------------------------------------------------------
    current_projection =
        valid_projection(
            fingerprint,
            17
        );

    std::string tampered =
        cookie;

    if (!tampered.empty()) {
        tampered[0] =
            tampered[0] == 'A'
                ? 'B'
                : 'A';
    }

    const int lookups_before_tamper =
        lookup_calls;

    expect_result(
        authenticator.authenticate(
            tampered,
            &output
        ),
        StorageUserSessionAuthenticatorResult::
            Rejected,
        "tampered cookie rejects"
    );

    expect(
        lookup_calls ==
            lookups_before_tamper,
        "tampered cookie performs no projection authorization"
    );


    // ------------------------------------------------------------------
    // Cookie minted for another Node must fail before policy.
    // ------------------------------------------------------------------
    std::string foreign_cookie;

    expect(
        pqnas::cluster::
            storage_user_session_cookie_mint(
                base_key.data(),
                "storage-02",
                fingerprint,
                17,
                1000,
                1600,
                &foreign_cookie
            ),
        "foreign Node cookie fixture mints"
    );

    const int lookups_before_foreign =
        lookup_calls;

    expect_result(
        authenticator.authenticate(
            foreign_cookie,
            &output
        ),
        StorageUserSessionAuthenticatorResult::
            Rejected,
        "foreign Node cookie rejects locally"
    );

    expect(
        lookup_calls ==
            lookups_before_foreign,
        "foreign Node cookie performs no projection authorization"
    );


    // ------------------------------------------------------------------
    // Exact expiration boundary.
    // ------------------------------------------------------------------
    now = 1600;

    const int lookups_before_expired =
        lookup_calls;

    expect_result(
        authenticator.authenticate(
            cookie,
            &output
        ),
        StorageUserSessionAuthenticatorResult::
            Rejected,
        "cookie rejects exactly at exp"
    );

    expect(
        lookup_calls ==
            lookups_before_expired,
        "expired cookie performs no projection authorization"
    );

    now = 1200;


    // ------------------------------------------------------------------
    // Projection lookup failure is fail-closed.
    // ------------------------------------------------------------------
    StorageUserSessionPolicy::Config
        throwing_policy_config;

    throwing_policy_config.local_node_id =
        local_node;

    throwing_policy_config.lookup_projection =
        [](const std::string&)
            -> std::optional<StorageUserProjection> {
            throw std::runtime_error(
                "synthetic lookup failure"
            );
        };

    StorageUserSessionPolicy
        throwing_policy(
            std::move(
                throwing_policy_config
            )
        );

    StorageUserSessionAuthenticatorConfig
        throwing_config =
            config;

    throwing_config.session_policy =
        &throwing_policy;

    StorageUserSessionAuthenticator
        throwing_authenticator(
            throwing_config
        );

    expect_result(
        throwing_authenticator.authenticate(
            cookie,
            &output
        ),
        StorageUserSessionAuthenticatorResult::
            Rejected,
        "projection lookup failure rejects session"
    );


    // ------------------------------------------------------------------
    // Clock failure occurs before cookie/policy evaluation.
    // ------------------------------------------------------------------
    StorageUserSessionAuthenticatorConfig
        bad_clock_config =
            config;

    bad_clock_config.clock =
        []() -> long {
            return 0;
        };

    StorageUserSessionAuthenticator
        bad_clock_authenticator(
            bad_clock_config
        );

    const int lookups_before_bad_clock =
        lookup_calls;

    expect_result(
        bad_clock_authenticator.authenticate(
            cookie,
            &output
        ),
        StorageUserSessionAuthenticatorResult::
            ClockUnavailable,
        "unavailable session clock fails closed"
    );

    expect(
        lookup_calls ==
            lookups_before_bad_clock,
        "clock failure performs no projection authorization"
    );


    // ------------------------------------------------------------------
    // Static configuration errors fail before authentication.
    // ------------------------------------------------------------------
    std::array<unsigned char, 32>
        zero_key{};

    StorageUserSessionAuthenticatorConfig
        bad_key_config =
            config;

    bad_key_config.base_cookie_key =
        zero_key.data();

    StorageUserSessionAuthenticator
        bad_key_authenticator(
            bad_key_config
        );

    const int lookups_before_bad_key =
        lookup_calls;

    expect_result(
        bad_key_authenticator.authenticate(
            cookie,
            &output
        ),
        StorageUserSessionAuthenticatorResult::
            InvalidConfiguration,
        "zero base cookie key is invalid configuration"
    );

    expect(
        lookup_calls ==
            lookups_before_bad_key,
        "invalid key configuration performs no projection authorization"
    );


    expect_result(
        authenticator.authenticate(
            cookie,
            nullptr
        ),
        StorageUserSessionAuthenticatorResult::
            InvalidInput,
        "missing output fails closed"
    );


    if (failures != 0) {
        std::cerr
            << failures
            << " Storage session authenticator test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: Storage user session authenticator tests passed\n";

    return 0;
}
