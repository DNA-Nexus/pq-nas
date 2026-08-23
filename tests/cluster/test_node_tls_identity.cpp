#include "cluster/node_tls_identity.h"

#include <openssl/evp.h>
#include <openssl/x509.h>

#include <array>
#include <cstddef>
#include <iostream>
#include <memory>
#include <string>

namespace {

using EvpPkeyPtr =
    std::unique_ptr<
        EVP_PKEY,
        decltype(&EVP_PKEY_free)
    >;

using X509Ptr =
    std::unique_ptr<
        X509,
        decltype(&X509_free)
    >;

bool expect(
    bool condition,
    const char* label
) {
    if (condition) {
        return true;
    }

    std::cerr
        << "FAILED: "
        << label
        << '\n';

    return false;
}

X509Ptr make_known_certificate() {
    std::array<unsigned char, 32u> raw_public_key{};

    for (std::size_t index = 0;
         index < raw_public_key.size();
         ++index) {
        raw_public_key[index] =
            static_cast<unsigned char>(index);
    }

    EvpPkeyPtr public_key(
        EVP_PKEY_new_raw_public_key(
            EVP_PKEY_ED25519,
            nullptr,
            raw_public_key.data(),
            raw_public_key.size()
        ),
        EVP_PKEY_free
    );

    if (!public_key) {
        return X509Ptr(nullptr, X509_free);
    }

    X509Ptr certificate(
        X509_new(),
        X509_free
    );

    if (!certificate ||
        X509_set_pubkey(
            certificate.get(),
            public_key.get()
        ) != 1) {
        return X509Ptr(nullptr, X509_free);
    }

    return certificate;
}

} // namespace

int main() {
    using namespace pqnas::cluster;

    constexpr char kExpectedPin[] =
        "sha256/"
        "lAhFeu/Qcc7BJ8H5hTmTCGGtG6lMlA25dclywJ/Gi2g=";

    constexpr char kDifferentValidPin[] =
        "sha256/"
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";

    int failures = 0;

    const auto check = [&failures](
        bool condition,
        const char* label
    ) {
        if (!expect(condition, label)) {
            ++failures;
        }
    };

    X509Ptr certificate =
        make_known_certificate();

    check(
        static_cast<bool>(certificate),
        "create known certificate"
    );

    std::string normalized;
    std::string error;

    check(
        normalize_node_tls_spki_sha256_pin(
            kExpectedPin,
            &normalized,
            &error
        ),
        "normalize canonical pin"
    );

    check(
        normalized == kExpectedPin,
        "canonical pin unchanged"
    );

    normalized.clear();
    error.clear();

    check(
        normalize_node_tls_spki_sha256_pin(
            std::string(" \t") +
                kExpectedPin +
                "\r\n",
            &normalized,
            &error
        ),
        "normalize surrounding ASCII whitespace"
    );

    check(
        normalized == kExpectedPin,
        "whitespace normalization result"
    );

    check(
        !normalize_node_tls_spki_sha256_pin(
            "SHA256/"
            "lAhFeu/Qcc7BJ8H5hTmTCGGtG6lMlA25dclywJ/Gi2g=",
            &normalized,
            &error
        ),
        "reject wrong prefix case"
    );

    check(
        !normalize_node_tls_spki_sha256_pin(
            "sha256/"
            "lAhFeu_Qcc7BJ8H5hTmTCGGtG6lMlA25dclywJ_Gi2g=",
            &normalized,
            &error
        ),
        "reject URL-safe base64"
    );

    check(
        !normalize_node_tls_spki_sha256_pin(
            "sha256/AAAA",
            &normalized,
            &error
        ),
        "reject wrong digest length"
    );

    std::string calculated_pin;

    check(
        certificate &&
        node_tls_x509_spki_sha256_pin(
            certificate.get(),
            &calculated_pin,
            &error
        ),
        "calculate certificate SPKI pin"
    );

    check(
        calculated_pin == kExpectedPin,
        "known SPKI test vector"
    );

    check(
        certificate &&
        node_tls_x509_matches_spki_sha256_pin(
            certificate.get(),
            kExpectedPin
        ),
        "matching pin accepted"
    );

    check(
        certificate &&
        !node_tls_x509_matches_spki_sha256_pin(
            certificate.get(),
            kDifferentValidPin
        ),
        "different valid pin rejected"
    );

    check(
        certificate &&
        !node_tls_x509_matches_spki_sha256_pin(
            certificate.get(),
            std::string(" ") +
                kExpectedPin
        ),
        "non-canonical trusted pin rejected"
    );

    check(
        !node_tls_x509_matches_spki_sha256_pin(
            nullptr,
            kExpectedPin
        ),
        "missing certificate rejected"
    );

    if (failures == 0) {
        std::cout
            << "node TLS identity tests passed\n";
    }

    return failures == 0 ? 0 : 1;
}
