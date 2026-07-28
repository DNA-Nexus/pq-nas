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

    friend bool verify_internal_bearer_authorization(
        const std::string&,
        const InternalAuthCredential&
    );

    std::array<unsigned char, kInternalAuthSecretBytes> secret_{};
    bool configured_ = false;
};

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

} // namespace pqnas::cluster
