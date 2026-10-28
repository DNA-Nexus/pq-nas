#pragma once

#include <array>
#include <cstddef>
#include <string>

#include <openssl/types.h>

namespace pqnas::cluster {

inline constexpr std::size_t
    kNodeTlsSpkiSha256DigestBytes = 32u;

inline constexpr std::size_t
    kNodeTlsSpkiSha256Base64Chars = 44u;

// Normalizes the public, non-secret TLS SPKI pin format:
//
//     sha256/<standard-base64 SHA-256 digest>
//
// Security: callers loading authoritative persisted state should additionally
// require that the persisted value already equals the normalized value.
bool normalize_node_tls_spki_sha256_pin(
    const std::string& raw,
    std::string* normalized,
    std::string* err
);

// Calculates SHA-256 over DER SubjectPublicKeyInfo (SPKI), not over the whole
// certificate. This allows normal certificate renewal while the same TLS key
// remains trusted.
bool node_tls_x509_spki_sha256_digest(
    X509* certificate,
    std::array<
        unsigned char,
        kNodeTlsSpkiSha256DigestBytes
    >* digest,
    std::string* err
);

bool node_tls_x509_spki_sha256_pin(
    X509* certificate,
    std::string* pin,
    std::string* err
);

// Exact canonical pin comparison.
//
// Security: comparison is performed on the decoded 32-byte SHA-256 digests
// using OpenSSL's constant-time comparison primitive.
bool node_tls_x509_matches_spki_sha256_pin(
    X509* certificate,
    const std::string& expected_pin
) noexcept;

} // namespace pqnas::cluster
