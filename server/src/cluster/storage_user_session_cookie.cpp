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
        "pqnas-storage-session-v2";

constexpr std::size_t
    kFingerprintBytes = 128;

constexpr std::size_t
    kProjectionRevisionBytes = 8;

constexpr std::size_t
    kStorageSessionIdentityBytes =
        kFingerprintBytes +
        kProjectionRevisionBytes;

constexpr std::size_t
    kStorageSessionIdentityBase64Bytes =
        sodium_base64_ENCODED_LEN(
            kStorageSessionIdentityBytes,
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
    std::uint64_t* projection_revision,
    long* expires_at
) noexcept {
    if (fingerprint) {
        fingerprint->clear();
    }

    if (projection_revision) {
        *projection_revision = 0;
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

bool encode_storage_session_identity(
    const std::string& fingerprint_hex,
    std::uint64_t projection_revision,
    std::string* output
) {
    if (!output ||
        projection_revision == 0 ||
        !pqnas::valid_storage_user_fingerprint_hex(
            fingerprint_hex
        )) {
        return false;
    }

    std::array<
        unsigned char,
        kStorageSessionIdentityBytes
    > payload{};

    std::memcpy(
        payload.data(),
        fingerprint_hex.data(),
        kFingerprintBytes
    );

    // Security: fixed-width big-endian encoding gives one canonical binary
    // representation for the Director-owned authorization revision.
    for (std::size_t i = 0;
         i < kProjectionRevisionBytes;
         ++i) {
        const std::size_t shift =
            (kProjectionRevisionBytes - 1 - i) * 8;

        payload[
            kFingerprintBytes + i
        ] = static_cast<unsigned char>(
            (projection_revision >> shift) &
            0xffu
        );
    }

    std::array<
        char,
        kStorageSessionIdentityBase64Bytes
    > encoded{};

    if (!sodium_bin2base64(
            encoded.data(),
            encoded.size(),
            payload.data(),
            payload.size(),
            sodium_base64_VARIANT_ORIGINAL
        )) {
        return false;
    }

    *output = encoded.data();

    return true;
}

bool decode_storage_session_identity(
    const std::string& encoded,
    std::string* fingerprint,
    std::uint64_t* projection_revision
) {
    if (!fingerprint ||
        !projection_revision ||
        encoded.empty() ||
        encoded.size() >=
            kStorageSessionIdentityBase64Bytes) {
        return false;
    }

    fingerprint->clear();
    *projection_revision = 0;

    std::array<
        unsigned char,
        kStorageSessionIdentityBytes
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
            kStorageSessionIdentityBytes ||
        !encoded_end ||
        *encoded_end != '\0') {
        return false;
    }

    // Security: reject alternate textual encodings of the same binary claims.
    std::array<
        char,
        kStorageSessionIdentityBase64Bytes
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

    std::string decoded_fingerprint(
        reinterpret_cast<const char*>(
            decoded.data()
        ),
        kFingerprintBytes
    );

    if (!pqnas::valid_storage_user_fingerprint_hex(
            decoded_fingerprint
        )) {
        return false;
    }

    std::uint64_t decoded_revision = 0;

    for (std::size_t i = 0;
         i < kProjectionRevisionBytes;
         ++i) {
        decoded_revision =
            (decoded_revision << 8) |
            static_cast<std::uint64_t>(
                decoded[
                    kFingerprintBytes + i
                ]
            );
    }

    if (decoded_revision == 0) {
        return false;
    }

    *fingerprint =
        std::move(decoded_fingerprint);

    *projection_revision =
        decoded_revision;

    return true;
}

} // namespace

void clear_storage_user_session_cookie_text(
    std::string* value
) noexcept {
    if (!value) {
        return;
    }

    if (!value->empty()) {
        sodium_memzero(
            value->data(),
            value->size()
        );
    }

    value->clear();
}

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
    std::uint64_t projection_revision,
    long issued_at,
    long expires_at,
    std::string* out_cookie
) noexcept {
    clear_storage_user_session_cookie_text(
        out_cookie
    );

    if (!out_cookie ||
        !base_cookie_key ||
        !valid_session_time_window(
            issued_at,
            expires_at
        ) ||
        !valid_cluster_node_id(
            local_node_id
        ) ||
        projection_revision == 0 ||
        !pqnas::valid_storage_user_fingerprint_hex(
            fingerprint_hex
        )) {
        return false;
    }

    std::string cookie;

    try {
        ScopedDerivedKey derived_key;

        if (!derive_storage_session_key(
                base_cookie_key,
                local_node_id,
                &derived_key
            )) {
            return false;
        }

        std::string identity_b64;

        if (!encode_storage_session_identity(
                fingerprint_hex,
                projection_revision,
                &identity_b64
            )) {
            return false;
        }

        if (!session_cookie_mint(
                derived_key.data(),
                identity_b64,
                issued_at,
                expires_at,
                cookie
            ) ||
            cookie.empty() ||
            cookie.size() >
                kStorageUserSessionCookieMaxBytes) {
            clear_storage_user_session_cookie_text(
                &cookie
            );

            return false;
        }

        *out_cookie =
            std::move(cookie);

        return true;
    } catch (...) {
        clear_storage_user_session_cookie_text(
            &cookie
        );

        clear_storage_user_session_cookie_text(
            out_cookie
        );

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
    std::uint64_t* out_projection_revision,
    long* out_expires_at
) noexcept {
    clear_verify_outputs(
        out_fingerprint_hex,
        out_projection_revision,
        out_expires_at
    );

    if (!out_fingerprint_hex ||
        !out_projection_revision ||
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

        std::string identity_b64;
        long expires_at = 0;

        if (!session_cookie_verify(
                derived_key.data(),
                cookie_value,
                identity_b64,
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
        std::uint64_t projection_revision = 0;

        if (!decode_storage_session_identity(
                identity_b64,
                &fingerprint,
                &projection_revision
            )) {
            return StorageUserSessionCookieVerifyResult::
                InvalidCookie;
        }

        *out_fingerprint_hex =
            std::move(fingerprint);

        *out_projection_revision =
            projection_revision;

        if (out_expires_at) {
            *out_expires_at =
                expires_at;
        }

        return StorageUserSessionCookieVerifyResult::
            Verified;
    } catch (...) {
        clear_verify_outputs(
            out_fingerprint_hex,
            out_projection_revision,
            out_expires_at
        );

        return StorageUserSessionCookieVerifyResult::
            InvalidCookie;
    }
}

} // namespace pqnas::cluster
