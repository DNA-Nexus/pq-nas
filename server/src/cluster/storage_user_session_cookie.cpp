#include "cluster/storage_user_session_cookie.h"

#include "cluster/cluster_identity_validation.h"
#include "session_cookie.h"
#include "storage_metadata_validation.h"

#include <array>
#include <cstring>
#include <sodium.h>
#include <string>

namespace pqnas::cluster {
namespace {

constexpr char
    kStorageSessionDomain[] =
        "pqnas-storage-session-v1";

constexpr std::size_t
    kFingerprintBytes = 128;

constexpr std::size_t
    kFingerprintBase64Bytes =
        sodium_base64_ENCODED_LEN(
            kFingerprintBytes,
            sodium_base64_VARIANT_ORIGINAL
        );

class ScopedDerivedKey {
public:
    ScopedDerivedKey() = default;

    ~ScopedDerivedKey() {
        sodium_memzero(
            bytes_.data(),
            bytes_.size()
        );
    }

    ScopedDerivedKey(
        const ScopedDerivedKey&
    ) = delete;

    ScopedDerivedKey& operator=(
        const ScopedDerivedKey&
    ) = delete;

    unsigned char* data() noexcept {
        return bytes_.data();
    }

    const unsigned char* data() const noexcept {
        return bytes_.data();
    }

private:
    std::array<
        unsigned char,
        crypto_auth_hmacsha256_BYTES
    > bytes_{};
};

void clear_verify_outputs(
    std::string* fingerprint,
    long* expires_at
) noexcept {
    if (fingerprint) {
        fingerprint->clear();
    }

    if (expires_at) {
        *expires_at = 0;
    }
}

bool valid_session_time_window(
    long issued_at,
    long expires_at
) noexcept {
    if (issued_at < 0 ||
        expires_at <= issued_at) {
        return false;
    }

    // Security: convert only non-negative ordered values before subtracting.
    // This avoids signed-long overflow in adversarial or corrupted inputs.
    const auto issued_unsigned =
        static_cast<unsigned long>(
            issued_at
        );

    const auto expires_unsigned =
        static_cast<unsigned long>(
            expires_at
        );

    return
        expires_unsigned - issued_unsigned <=
        static_cast<unsigned long>(
            kStorageUserSessionCookieMaxTtlSeconds
        );
}

bool derive_storage_session_key(
    const unsigned char* base_cookie_key,
    const std::string& local_node_id,
    ScopedDerivedKey* output
) noexcept {
    if (!base_cookie_key ||
        !output ||
        sodium_is_zero(
            base_cookie_key,
            crypto_auth_hmacsha256_KEYBYTES
        ) != 0 ||
        !valid_cluster_node_id(
            local_node_id
        )) {
        return false;
    }

    crypto_auth_hmacsha256_state state;

    if (crypto_auth_hmacsha256_init(
            &state,
            base_cookie_key,
            crypto_auth_hmacsha256_KEYBYTES
        ) != 0) {
        return false;
    }

    // Security: explicit purpose separator prevents the normal Director/
    // Standalone session key from authenticating a Storage session even when
    // the same underlying COOKIE_KEY were accidentally provisioned to both.
    crypto_auth_hmacsha256_update(
        &state,
        reinterpret_cast<
            const unsigned char*
        >(kStorageSessionDomain),
        sizeof(kStorageSessionDomain) - 1
    );

    constexpr unsigned char separator = 0;

    crypto_auth_hmacsha256_update(
        &state,
        &separator,
        1
    );

    // Security: bind the derived signing key to one canonical Node identity so
    // a cookie from one Storage Node cannot authenticate on another Node.
    crypto_auth_hmacsha256_update(
        &state,
        reinterpret_cast<
            const unsigned char*
        >(local_node_id.data()),
        local_node_id.size()
    );

    crypto_auth_hmacsha256_final(
        &state,
        output->data()
    );

    return true;
}

bool fingerprint_to_base64(
    const std::string& fingerprint_hex,
    std::string* output
) {
    if (!output ||
        !pqnas::valid_storage_user_fingerprint_hex(
            fingerprint_hex
        )) {
        return false;
    }

    std::array<
        char,
        kFingerprintBase64Bytes
    > encoded{};

    if (!sodium_bin2base64(
            encoded.data(),
            encoded.size(),
            reinterpret_cast<
                const unsigned char*
            >(fingerprint_hex.data()),
            fingerprint_hex.size(),
            sodium_base64_VARIANT_ORIGINAL
        )) {
        return false;
    }

    *output = encoded.data();
    return true;
}

bool base64_to_fingerprint(
    const std::string& encoded,
    std::string* output
) {
    if (!output ||
        encoded.empty() ||
        encoded.size() >=
            kFingerprintBase64Bytes) {
        return false;
    }

    std::array<
        unsigned char,
        kFingerprintBytes
    > decoded{};

    std::size_t decoded_size = 0;
    const char* encoded_end = nullptr;

    if (sodium_base642bin(
            decoded.data(),
            decoded.size(),
            encoded.c_str(),
            encoded.size(),
            nullptr,
            &decoded_size,
            &encoded_end,
            sodium_base64_VARIANT_ORIGINAL
        ) != 0 ||
        decoded_size !=
            kFingerprintBytes ||
        !encoded_end ||
        *encoded_end != '\0') {
        return false;
    }

    // Security: require the canonical base64 representation that our own
    // minting path creates; reject alternate encodings of the same bytes.
    std::array<
        char,
        kFingerprintBase64Bytes
    > canonical{};

    if (!sodium_bin2base64(
            canonical.data(),
            canonical.size(),
            decoded.data(),
            decoded_size,
            sodium_base64_VARIANT_ORIGINAL
        ) ||
        encoded != canonical.data()) {
        return false;
    }

    std::string fingerprint(
        reinterpret_cast<const char*>(
            decoded.data()
        ),
        decoded_size
    );

    if (!pqnas::valid_storage_user_fingerprint_hex(
            fingerprint
        )) {
        return false;
    }

    *output = std::move(fingerprint);
    return true;
}

} // namespace

const char*
storage_user_session_cookie_verify_result_code(
    StorageUserSessionCookieVerifyResult result
) noexcept {
    switch (result) {
        case StorageUserSessionCookieVerifyResult::
                Verified:
            return "verified";

        case StorageUserSessionCookieVerifyResult::
                InvalidInput:
            return "invalid_input";

        case StorageUserSessionCookieVerifyResult::
                InvalidCookie:
            return "invalid_cookie";

        case StorageUserSessionCookieVerifyResult::
                Expired:
            return "expired";
    }

    return "invalid_cookie";
}

bool storage_user_session_cookie_mint(
    const unsigned char base_cookie_key[32],
    const std::string& local_node_id,
    const std::string& fingerprint_hex,
    long issued_at,
    long expires_at,
    std::string* out_cookie
) noexcept {
    if (out_cookie) {
        out_cookie->clear();
    }

    if (!out_cookie ||
        !base_cookie_key ||
        !valid_session_time_window(
            issued_at,
            expires_at
        ) ||
        !valid_cluster_node_id(
            local_node_id
        ) ||
        !pqnas::valid_storage_user_fingerprint_hex(
            fingerprint_hex
        )) {
        return false;
    }

    try {
        ScopedDerivedKey derived_key;

        if (!derive_storage_session_key(
                base_cookie_key,
                local_node_id,
                &derived_key
            )) {
            return false;
        }

        std::string fingerprint_b64;

        if (!fingerprint_to_base64(
                fingerprint_hex,
                &fingerprint_b64
            )) {
            return false;
        }

        std::string cookie;

        if (!session_cookie_mint(
                derived_key.data(),
                fingerprint_b64,
                issued_at,
                expires_at,
                cookie
            ) ||
            cookie.empty() ||
            cookie.size() >
                kStorageUserSessionCookieMaxBytes) {
            return false;
        }

        *out_cookie =
            std::move(cookie);

        return true;
    } catch (...) {
        out_cookie->clear();
        return false;
    }
}

StorageUserSessionCookieVerifyResult
storage_user_session_cookie_verify(
    const unsigned char base_cookie_key[32],
    const std::string& local_node_id,
    const std::string& cookie_value,
    long now,
    std::string* out_fingerprint_hex,
    long* out_expires_at
) noexcept {
    clear_verify_outputs(
        out_fingerprint_hex,
        out_expires_at
    );

    if (!out_fingerprint_hex ||
        !base_cookie_key ||
        now < 0 ||
        cookie_value.empty() ||
        cookie_value.size() >
            kStorageUserSessionCookieMaxBytes ||
        !valid_cluster_node_id(
            local_node_id
        )) {
        return StorageUserSessionCookieVerifyResult::
            InvalidInput;
    }

    try {
        ScopedDerivedKey derived_key;

        if (!derive_storage_session_key(
                base_cookie_key,
                local_node_id,
                &derived_key
            )) {
            return StorageUserSessionCookieVerifyResult::
                InvalidInput;
        }

        std::string fingerprint_b64;
        long expires_at = 0;

        if (!session_cookie_verify(
                derived_key.data(),
                cookie_value,
                fingerprint_b64,
                expires_at
            )) {
            return StorageUserSessionCookieVerifyResult::
                InvalidCookie;
        }

        // Security: exp is an exclusive validity boundary. At exp itself,
        // the session is already expired; never grant an extra clock tick.
        if (expires_at <= 0 ||
            now >= expires_at) {
            return StorageUserSessionCookieVerifyResult::
                Expired;
        }

        std::string fingerprint;

        if (!base64_to_fingerprint(
                fingerprint_b64,
                &fingerprint
            )) {
            return StorageUserSessionCookieVerifyResult::
                InvalidCookie;
        }

        *out_fingerprint_hex =
            std::move(fingerprint);

        if (out_expires_at) {
            *out_expires_at =
                expires_at;
        }

        return StorageUserSessionCookieVerifyResult::
            Verified;
    } catch (...) {
        clear_verify_outputs(
            out_fingerprint_hex,
            out_expires_at
        );

        return StorageUserSessionCookieVerifyResult::
            InvalidCookie;
    }
}

} // namespace pqnas::cluster
