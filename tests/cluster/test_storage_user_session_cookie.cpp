#include "cluster/storage_user_session_cookie.h"

#include "session_cookie.h"

#include <array>
#include <iostream>
#include <sodium.h>
#include <string>

namespace {

using pqnas::cluster::
    StorageUserSessionCookieVerifyResult;

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
    StorageUserSessionCookieVerifyResult actual,
    StorageUserSessionCookieVerifyResult expected,
    const std::string& message
) {
    expect(
        actual == expected,
        message +
            ": expected " +
            pqnas::cluster::
                storage_user_session_cookie_verify_result_code(
                    expected
                ) +
            ", got " +
            pqnas::cluster::
                storage_user_session_cookie_verify_result_code(
                    actual
                )
    );
}

std::string fingerprint_base64(
    const std::string& fingerprint
) {
    std::array<
        char,
        sodium_base64_ENCODED_LEN(
            128,
            sodium_base64_VARIANT_ORIGINAL
        )
    > encoded{};

    sodium_bin2base64(
        encoded.data(),
        encoded.size(),
        reinterpret_cast<
            const unsigned char*
        >(fingerprint.data()),
        fingerprint.size(),
        sodium_base64_VARIANT_ORIGINAL
    );

    return encoded.data();
}

} // namespace

int main() {
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

    const std::string fingerprint(
        128,
        'a'
    );

    const std::string node_one =
        "storage-01";

    const std::string node_two =
        "storage-02";

    std::string cookie;

    expect(
        pqnas::cluster::
            storage_user_session_cookie_mint(
                base_key.data(),
                node_one,
                fingerprint,
                1000,
                1600,
                &cookie
            ),
        "valid Storage cookie mints"
    );

    expect(
        !cookie.empty(),
        "minted Storage cookie is non-empty"
    );

    std::string verified_fp;
    long verified_exp = 0;

    expect_result(
        pqnas::cluster::
            storage_user_session_cookie_verify(
                base_key.data(),
                node_one,
                cookie,
                1200,
                &verified_fp,
                &verified_exp
            ),
        StorageUserSessionCookieVerifyResult::
            Verified,
        "same Node verifies Storage cookie"
    );

    expect(
        verified_fp == fingerprint &&
        verified_exp == 1600,
        "verified Storage cookie returns identity and expiry"
    );

    verified_fp = "stale";
    verified_exp = 9999;

    expect_result(
        pqnas::cluster::
            storage_user_session_cookie_verify(
                base_key.data(),
                node_two,
                cookie,
                1200,
                &verified_fp,
                &verified_exp
            ),
        StorageUserSessionCookieVerifyResult::
            InvalidCookie,
        "different Storage Node rejects cookie"
    );

    expect(
        verified_fp.empty() &&
        verified_exp == 0,
        "cross-Node failure clears outputs"
    );

    // Security regression: even if Director and Storage accidentally share the
    // same underlying COOKIE_KEY, a normal session signed directly with that
    // key must not authenticate as a domain-separated Storage session.
    std::string director_cookie;

    expect(
        session_cookie_mint(
            base_key.data(),
            fingerprint_base64(
                fingerprint
            ),
            1000,
            1600,
            director_cookie
        ),
        "normal session cookie fixture mints"
    );

    expect_result(
        pqnas::cluster::
            storage_user_session_cookie_verify(
                base_key.data(),
                node_one,
                director_cookie,
                1200,
                &verified_fp,
                &verified_exp
            ),
        StorageUserSessionCookieVerifyResult::
            InvalidCookie,
        "normal Director cookie cannot authenticate as Storage session"
    );

    verified_fp = "stale";
    verified_exp = 9999;

    expect_result(
        pqnas::cluster::
            storage_user_session_cookie_verify(
                base_key.data(),
                node_one,
                cookie,
                1600,
                &verified_fp,
                &verified_exp
            ),
        StorageUserSessionCookieVerifyResult::
            Expired,
        "Storage cookie expires exactly at exp"
    );

    expect(
        verified_fp.empty() &&
        verified_exp == 0,
        "exact-exp failure clears outputs"
    );

    verified_fp = "stale";
    verified_exp = 9999;

    expect_result(
        pqnas::cluster::
            storage_user_session_cookie_verify(
                base_key.data(),
                node_one,
                cookie,
                1601,
                &verified_fp,
                &verified_exp
            ),
        StorageUserSessionCookieVerifyResult::
            Expired,
        "expired Storage cookie fails closed"
    );

    expect(
        verified_fp.empty() &&
        verified_exp == 0,
        "expired cookie clears outputs"
    );

    std::string tampered =
        cookie;

    expect(
        !tampered.empty(),
        "tamper fixture exists"
    );

    if (!tampered.empty()) {
        tampered[0] =
            tampered[0] == 'A'
                ? 'B'
                : 'A';
    }

    expect_result(
        pqnas::cluster::
            storage_user_session_cookie_verify(
                base_key.data(),
                node_one,
                tampered,
                1200,
                &verified_fp,
                &verified_exp
            ),
        StorageUserSessionCookieVerifyResult::
            InvalidCookie,
        "tampered Storage cookie fails closed"
    );

    std::string output = "stale";

    expect(
        !pqnas::cluster::
            storage_user_session_cookie_mint(
                base_key.data(),
                "../storage-01",
                fingerprint,
                1000,
                1600,
                &output
            ),
        "invalid Node ID cannot mint Storage cookie"
    );

    expect(
        output.empty(),
        "invalid Node mint clears output"
    );

    expect(
        !pqnas::cluster::
            storage_user_session_cookie_mint(
                base_key.data(),
                node_one,
                std::string(127, 'a'),
                1000,
                1600,
                &output
            ),
        "malformed fingerprint cannot mint Storage cookie"
    );

    expect(
        !pqnas::cluster::
            storage_user_session_cookie_mint(
                base_key.data(),
                node_one,
                fingerprint,
                1000,
                1000 +
                    pqnas::cluster::
                        kStorageUserSessionCookieMaxTtlSeconds +
                    1,
                &output
            ),
        "Storage cookie lifetime above security ceiling rejected"
    );

    expect(
        !pqnas::cluster::
            storage_user_session_cookie_mint(
                base_key.data(),
                node_one,
                fingerprint,
                2000,
                1999,
                &output
            ),
        "reversed Storage cookie time window rejected"
    );

    std::array<unsigned char, 32>
        zero_key{};

    expect(
        !pqnas::cluster::
            storage_user_session_cookie_mint(
                zero_key.data(),
                node_one,
                fingerprint,
                1000,
                1600,
                &output
            ),
        "obviously uninitialized zero base key fails closed"
    );

    expect_result(
        pqnas::cluster::
            storage_user_session_cookie_verify(
                base_key.data(),
                "../storage-01",
                cookie,
                1200,
                &verified_fp,
                &verified_exp
            ),
        StorageUserSessionCookieVerifyResult::
            InvalidInput,
        "invalid verifier Node ID fails closed"
    );

    const std::string oversized(
        pqnas::cluster::
            kStorageUserSessionCookieMaxBytes +
        1,
        'x'
    );

    expect_result(
        pqnas::cluster::
            storage_user_session_cookie_verify(
                base_key.data(),
                node_one,
                oversized,
                1200,
                &verified_fp,
                &verified_exp
            ),
        StorageUserSessionCookieVerifyResult::
            InvalidInput,
        "oversized Storage cookie rejected before crypto parsing"
    );

    if (failures != 0) {
        std::cerr
            << failures
            << " Storage session cookie test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: Storage user session cookie tests passed\n";

    return 0;
}
