#pragma once

#include <cstddef>
#include <string>

namespace pqnas::cluster {

// Security ceiling, not the eventual configured lifetime.
//
// The production handoff route may choose a shorter configured TTL, but no
// Storage session minted through this primitive may exceed one hour.
inline constexpr long
    kStorageUserSessionCookieMaxTtlSeconds = 3600;

inline constexpr std::size_t
    kStorageUserSessionCookieMaxBytes = 2048;

enum class StorageUserSessionCookieVerifyResult {
    Verified,
    InvalidInput,
    InvalidCookie,
    Expired
};

const char*
storage_user_session_cookie_verify_result_code(
    StorageUserSessionCookieVerifyResult result
) noexcept;

// Mint a Storage-Node-specific session cookie value.
//
// Security:
// - base_cookie_key is never used directly for signing
// - the signing key is domain-separated by purpose + local Node ID
// - fingerprint is strict canonical lowercase hex
// - lifetime is bounded by a security ceiling
bool storage_user_session_cookie_mint(
    const unsigned char base_cookie_key[32],
    const std::string& local_node_id,
    const std::string& fingerprint_hex,
    long issued_at,
    long expires_at,
    std::string* out_cookie
) noexcept;

// Verify only the cryptographic Storage-session envelope.
//
// This does NOT authorize storage access. The returned fingerprint must still
// be checked against StorageUserSessionPolicy on every authenticated request.
StorageUserSessionCookieVerifyResult
storage_user_session_cookie_verify(
    const unsigned char base_cookie_key[32],
    const std::string& local_node_id,
    const std::string& cookie_value,
    long now,
    std::string* out_fingerprint_hex,
    long* out_expires_at = nullptr
) noexcept;

} // namespace pqnas::cluster
