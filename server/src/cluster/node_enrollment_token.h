#pragma once

#include <cstddef>
#include <string>

namespace pqnas::cluster {

inline constexpr std::size_t
    kNodeEnrollmentTokenBytes = 32;

inline constexpr std::size_t
    kNodeEnrollmentTokenChars = 43;

inline constexpr std::size_t
    kNodeEnrollmentTokenHashBytes = 32;

inline constexpr std::size_t
    kNodeEnrollmentTokenHashChars = 43;

enum class NodeEnrollmentTokenResult {
    Ok,
    InvalidInput,
    CryptoUnavailable,
    CryptoFailed,
    EncodingFailed
};

const char* node_enrollment_token_result_code(
    NodeEnrollmentTokenResult result
);

// Securely clears a short-lived plaintext enrollment token held in a string.
// Callers must use this after constructing the one-time HTTP response.
void clear_node_enrollment_token_text(
    std::string* value
) noexcept;

// Owns one generated plaintext bootstrap token and its persisted verifier.
//
// The type is deliberately move-only so accidental copies cannot multiply the
// plaintext secret in memory. The verifier may be read before take_token().
// take_token() transfers the plaintext exactly once and clears this object.
class NodeEnrollmentTokenMaterial {
public:
    NodeEnrollmentTokenMaterial() = default;
    ~NodeEnrollmentTokenMaterial();

    NodeEnrollmentTokenMaterial(
        const NodeEnrollmentTokenMaterial&
    ) = delete;

    NodeEnrollmentTokenMaterial& operator=(
        const NodeEnrollmentTokenMaterial&
    ) = delete;

    NodeEnrollmentTokenMaterial(
        NodeEnrollmentTokenMaterial&& other
    ) noexcept;

    NodeEnrollmentTokenMaterial& operator=(
        NodeEnrollmentTokenMaterial&& other
    ) noexcept;

    bool configured() const noexcept;

    const std::string& token_hash() const noexcept;

    // The caller becomes responsible for clearing the returned plaintext with
    // clear_node_enrollment_token_text(). This succeeds at most once.
    bool take_token(
        std::string* output
    ) noexcept;

    void clear() noexcept;

private:
    friend NodeEnrollmentTokenResult
    generate_node_enrollment_token(
        NodeEnrollmentTokenMaterial*
    );

    std::string token_;
    std::string token_hash_;
    bool configured_ = false;
};

// Generates 32 cryptographically random bytes, encodes them canonically as
// base64url without padding, and computes SHA-256 over the raw random bytes.
//
// On any failure, output is cleared. The plaintext is never logged or persisted
// by this module.
NodeEnrollmentTokenResult
generate_node_enrollment_token(
    NodeEnrollmentTokenMaterial* output
);

// Validates one canonical 32-byte base64url token and computes SHA-256 over its
// decoded raw bytes. The hash is returned as canonical base64url without
// padding. On failure, output_hash is cleared.
NodeEnrollmentTokenResult
hash_node_enrollment_token(
    const std::string& token,
    std::string* output_hash
);

// Validates the exact canonical verifier format accepted by the persistent
// enrollment registry.
bool valid_node_enrollment_token_hash(
    const std::string& value
) noexcept;

} // namespace pqnas::cluster
