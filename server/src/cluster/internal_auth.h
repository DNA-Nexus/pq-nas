#pragma once

#include <array>
#include <cstddef>
#include <string>

namespace pqnas::cluster {

inline constexpr std::size_t kInternalAuthSecretBytes = 32;
inline constexpr std::size_t kInternalAuthTokenChars = 43;

class InternalAuthCredential {
public:
    InternalAuthCredential() = default;
    ~InternalAuthCredential();

    InternalAuthCredential(const InternalAuthCredential&) = delete;
    InternalAuthCredential& operator=(const InternalAuthCredential&) = delete;

    InternalAuthCredential(InternalAuthCredential&& other) noexcept;
    InternalAuthCredential& operator=(InternalAuthCredential&& other) noexcept;

    bool configured() const noexcept;
    void clear() noexcept;

private:
    friend bool load_internal_auth_credential_file(
        const std::string&,
        InternalAuthCredential*,
        std::string*
    );

    friend bool parse_internal_auth_credential_token(
        const std::string&,
        InternalAuthCredential*,
        std::string*
    );

    friend bool verify_internal_bearer_authorization(
        const std::string&,
        const InternalAuthCredential&
    );

    friend bool make_internal_bearer_authorization(
        const InternalAuthCredential&,
        std::string*
    );

    std::array<unsigned char, kInternalAuthSecretBytes> secret_{};
    bool configured_ = false;
};

// Securely clears the active characters before releasing the logical string.
// Callers should use this for every temporary plaintext credential token.
void clear_internal_auth_token_text(
    std::string* token
) noexcept;

// Accepts exactly one canonical 32-byte base64url token without padding.
bool valid_internal_auth_credential_token(
    const std::string& token
) noexcept;

// Generates a cryptographically random long-lived credential token.
// Any previous output content is securely cleared before replacement.
bool generate_internal_auth_credential_token(
    std::string* token,
    std::string* err
);

// Parses one plaintext token into the move-only credential object. The active
// credential is replaced only after complete validation and decoding.
bool parse_internal_auth_credential_token(
    const std::string& token,
    InternalAuthCredential* out,
    std::string* err
);

// Loads one 32-byte secret encoded as 43-character base64url without padding.
//
// Security properties:
// - opens with O_NOFOLLOW
// - accepts regular files only
// - rejects group/other permissions
// - bounds the input size
// - validates and decodes before replacing the active credential
bool load_internal_auth_credential_file(
    const std::string& path,
    InternalAuthCredential* out,
    std::string* err
);

// Verifies an exact "Bearer <base64url-token>" value using constant-time
// comparison. It deliberately returns only true/false so authentication
// responses do not reveal parsing or credential details.
bool verify_internal_bearer_authorization(
    const std::string& authorization_header,
    const InternalAuthCredential& credential
);

// Builds the exact Authorization value used for Director-to-node requests.
//
// Security:
// - returns false for an unconfigured credential
// - exposes the token only through this narrow protocol helper
// - callers must keep the returned string short-lived and must never log it
bool make_internal_bearer_authorization(
    const InternalAuthCredential& credential,
    std::string* out
);

} // namespace pqnas::cluster
