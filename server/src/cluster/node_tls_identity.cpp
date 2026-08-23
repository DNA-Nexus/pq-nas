#include "cluster/node_tls_identity.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/x509.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace pqnas::cluster {
namespace {

constexpr char kPinPrefix[] = "sha256/";
constexpr std::size_t kPinPrefixBytes =
    sizeof(kPinPrefix) - 1u;

using EvpPkeyPtr =
    std::unique_ptr<
        EVP_PKEY,
        decltype(&EVP_PKEY_free)
    >;

void set_error(
    std::string* err,
    const std::string& message
) {
    if (err) {
        *err = message;
    }
}

bool ascii_space(
    unsigned char character
) noexcept {
    return
        character == ' ' ||
        character == '\t' ||
        character == '\r' ||
        character == '\n' ||
        character == '\f' ||
        character == '\v';
}

std::string trim_ascii(
    const std::string& value
) {
    std::size_t first = 0;

    while (first < value.size() &&
           ascii_space(
               static_cast<unsigned char>(
                   value[first]
               ))) {
        ++first;
    }

    std::size_t last = value.size();

    while (last > first &&
           ascii_space(
               static_cast<unsigned char>(
                   value[last - 1]
               ))) {
        --last;
    }

    return value.substr(
        first,
        last - first
    );
}

bool base64_character(
    unsigned char character
) noexcept {
    return
        (character >= 'A' &&
         character <= 'Z') ||
        (character >= 'a' &&
         character <= 'z') ||
        (character >= '0' &&
         character <= '9') ||
        character == '+' ||
        character == '/';
}

bool encode_digest(
    const std::array<
        unsigned char,
        kNodeTlsSpkiSha256DigestBytes
    >& digest,
    std::string* output,
    std::string* err
) {
    if (!output) {
        set_error(
            err,
            "missing TLS SPKI pin output"
        );
        return false;
    }

    std::array<
        unsigned char,
        kNodeTlsSpkiSha256Base64Chars + 1u
    > encoded{};

    const int encoded_bytes =
        EVP_EncodeBlock(
            encoded.data(),
            digest.data(),
            static_cast<int>(
                digest.size()
            )
        );

    if (encoded_bytes !=
        static_cast<int>(
            kNodeTlsSpkiSha256Base64Chars
        )) {
        set_error(
            err,
            "failed to encode TLS SPKI pin"
        );
        return false;
    }

    output->assign(
        kPinPrefix,
        kPinPrefixBytes
    );

    output->append(
        reinterpret_cast<const char*>(
            encoded.data()
        ),
        kNodeTlsSpkiSha256Base64Chars
    );

    return true;
}

bool decode_canonical_pin(
    const std::string& value,
    std::array<
        unsigned char,
        kNodeTlsSpkiSha256DigestBytes
    >* digest,
    std::string* err
) {
    if (!digest) {
        set_error(
            err,
            "missing TLS SPKI digest output"
        );
        return false;
    }

    if (value.size() !=
            kPinPrefixBytes +
                kNodeTlsSpkiSha256Base64Chars ||
        value.compare(
            0,
            kPinPrefixBytes,
            kPinPrefix
        ) != 0) {
        set_error(
            err,
            "invalid TLS SPKI SHA-256 pin format"
        );
        return false;
    }

    const std::string payload =
        value.substr(
            kPinPrefixBytes
        );

    // SHA-256 is 32 bytes, therefore canonical standard base64 is exactly
    // 44 characters with one trailing '=' padding character.
    for (std::size_t index = 0;
         index + 1u < payload.size();
         ++index) {
        if (!base64_character(
                static_cast<unsigned char>(
                    payload[index]
                ))) {
            set_error(
                err,
                "invalid TLS SPKI SHA-256 base64"
            );
            return false;
        }
    }

    if (payload.empty() ||
        payload.back() != '=') {
        set_error(
            err,
            "invalid TLS SPKI SHA-256 base64 padding"
        );
        return false;
    }

    std::array<unsigned char, 33u>
        decoded{};

    const int decoded_with_padding =
        EVP_DecodeBlock(
            decoded.data(),
            reinterpret_cast<
                const unsigned char*
            >(
                payload.data()
            ),
            static_cast<int>(
                payload.size()
            )
        );

    // EVP_DecodeBlock includes the padded byte in its reported output length.
    if (decoded_with_padding != 33) {
        set_error(
            err,
            "invalid TLS SPKI SHA-256 digest"
        );
        return false;
    }

    std::copy_n(
        decoded.begin(),
        digest->size(),
        digest->begin()
    );

    // Security: re-encode and require exact equality so non-canonical base64
    // encodings cannot represent the same trusted identity.
    std::string canonical;

    if (!encode_digest(
            *digest,
            &canonical,
            err) ||
        canonical != value) {
        set_error(
            err,
            "non-canonical TLS SPKI SHA-256 pin"
        );
        return false;
    }

    return true;
}

} // namespace

bool normalize_node_tls_spki_sha256_pin(
    const std::string& raw,
    std::string* normalized,
    std::string* err
) {
    if (normalized) {
        normalized->clear();
    }

    if (err) {
        err->clear();
    }

    const std::string trimmed =
        trim_ascii(raw);

    std::array<
        unsigned char,
        kNodeTlsSpkiSha256DigestBytes
    > digest{};

    if (!decode_canonical_pin(
            trimmed,
            &digest,
            err)) {
        return false;
    }

    return encode_digest(
        digest,
        normalized,
        err
    );
}

bool node_tls_x509_spki_sha256_digest(
    X509* certificate,
    std::array<
        unsigned char,
        kNodeTlsSpkiSha256DigestBytes
    >* digest,
    std::string* err
) {
    if (err) {
        err->clear();
    }

    if (!certificate || !digest) {
        set_error(
            err,
            "missing TLS certificate or digest output"
        );
        return false;
    }

    EvpPkeyPtr public_key(
        X509_get_pubkey(
            certificate
        ),
        EVP_PKEY_free
    );

    if (!public_key) {
        set_error(
            err,
            "TLS certificate has no public key"
        );
        return false;
    }

    const int der_bytes =
        i2d_PUBKEY(
            public_key.get(),
            nullptr
        );

    if (der_bytes <= 0 ||
        der_bytes > 1024 * 1024) {
        set_error(
            err,
            "invalid TLS certificate public key"
        );
        return false;
    }

    std::vector<unsigned char> der(
        static_cast<std::size_t>(
            der_bytes
        )
    );

    unsigned char* cursor =
        der.data();

    if (i2d_PUBKEY(
            public_key.get(),
            &cursor
        ) != der_bytes) {
        set_error(
            err,
            "failed to encode TLS certificate SPKI"
        );
        return false;
    }

    unsigned int digest_bytes = 0;

    if (EVP_Digest(
            der.data(),
            der.size(),
            digest->data(),
            &digest_bytes,
            EVP_sha256(),
            nullptr
        ) != 1 ||
        digest_bytes != digest->size()) {
        set_error(
            err,
            "failed to hash TLS certificate SPKI"
        );
        return false;
    }

    return true;
}

bool node_tls_x509_spki_sha256_pin(
    X509* certificate,
    std::string* pin,
    std::string* err
) {
    if (pin) {
        pin->clear();
    }

    std::array<
        unsigned char,
        kNodeTlsSpkiSha256DigestBytes
    > digest{};

    if (!node_tls_x509_spki_sha256_digest(
            certificate,
            &digest,
            err)) {
        return false;
    }

    return encode_digest(
        digest,
        pin,
        err
    );
}

bool node_tls_x509_matches_spki_sha256_pin(
    X509* certificate,
    const std::string& expected_pin
) noexcept {
    try {
        std::string normalized;

        if (!normalize_node_tls_spki_sha256_pin(
                expected_pin,
                &normalized,
                nullptr) ||
            normalized != expected_pin) {
            // Security: production trust state must already be canonical.
            return false;
        }

        std::array<
            unsigned char,
            kNodeTlsSpkiSha256DigestBytes
        > expected_digest{};

        if (!decode_canonical_pin(
                expected_pin,
                &expected_digest,
                nullptr)) {
            return false;
        }

        std::array<
            unsigned char,
            kNodeTlsSpkiSha256DigestBytes
        > actual_digest{};

        if (!node_tls_x509_spki_sha256_digest(
                certificate,
                &actual_digest,
                nullptr)) {
            return false;
        }

        return
            CRYPTO_memcmp(
                expected_digest.data(),
                actual_digest.data(),
                actual_digest.size()
            ) == 0;
    } catch (...) {
        // Security: TLS identity verification must fail closed on every
        // allocation or OpenSSL-adjacent exceptional path.
        return false;
    }
}

} // namespace pqnas::cluster
