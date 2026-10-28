#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

namespace pqnas::cluster {

inline constexpr std::size_t
    kStorageHandoffTicketBytes = 32;

inline constexpr std::size_t
    kStorageHandoffTicketChars = 43;

inline constexpr std::size_t
    kStorageHandoffFingerprintHexChars = 128;

inline constexpr std::size_t
    kStorageHandoffBrowserBindingBytes = 32;

inline constexpr std::size_t
    kStorageHandoffBrowserBindingChars = 43;

inline constexpr std::int64_t
    kStorageHandoffTicketMaxTtlSeconds = 120;

inline constexpr std::size_t
    kStorageHandoffTicketMaxEntries = 4096;

enum class StorageHandoffTicketResult {
    Ok,
    InvalidInput,
    CryptoUnavailable,
    CryptoFailed,
    ClockUnavailable,
    StoreFull,

    // Security: one browser-binding handoff attempt already minted a ticket.
    // The caller must restart preauth instead of minting another bearer.
    BindingAlreadyIssued,

    NotFound,
    Expired,

    // Security: the ticket exists but belongs to another browser binding.
    // This result does NOT consume the ticket.
    BindingMismatch,

    InternalError
};

const char* storage_handoff_ticket_result_code(
    StorageHandoffTicketResult result
) noexcept;

// Canonical validator for the 32-byte unpadded base64url handoff ticket.
//
// Security: every handoff protocol layer must use this shared validator so
// acceptance rules cannot drift between issuance, transport and consume.
bool valid_storage_handoff_ticket_text(
    const std::string& value
) noexcept;

// Security: callers must clear plaintext tickets after transferring them to
// the browser. The store itself retains only a SHA-256 verifier.
void clear_storage_handoff_ticket_text(
    std::string* value
) noexcept;

// Create the short-lived browser secret used to bind a handoff ticket to the
// browser that initiated the Storage flow.
//
// The secret has the same 32-byte / 43-char canonical base64url shape as a
// ticket, but it is a distinct security value and must never be logged.
bool generate_storage_handoff_browser_binding_secret(
    std::string* out_secret
) noexcept;

// Hash a canonical browser-binding secret for safe transport through the
// Director. The Storage ticket retains only this verifier, never the secret.
bool storage_handoff_browser_binding_hash(
    const std::string& secret,
    std::string* out_hash
) noexcept;

bool valid_storage_handoff_browser_binding_hash(
    const std::string& value
) noexcept;

void clear_storage_handoff_browser_binding_secret_text(
    std::string* value
) noexcept;

struct StorageHandoffTicketClaims {
    std::string fingerprint;

    // Security: bind redemption to the exact Storage placement snapshot that
    // authorized ticket issuance. A later consume must compare these claims
    // against the Node's current local projection before creating a session.
    std::string home_node;
    std::uint64_t projection_revision = 0;

    std::int64_t issued_at = 0;
    std::int64_t expires_at = 0;
};

using StorageHandoffTicketClock =
    std::function<std::int64_t()>;

class StorageHandoffTicketStore {
public:
    explicit StorageHandoffTicketStore(
        std::size_t max_entries =
            kStorageHandoffTicketMaxEntries,
        StorageHandoffTicketClock clock = {}
    );

    // Creates a cryptographically random one-time ticket.
    //
    // Security:
    // - plaintext is returned only to the caller
    // - only SHA-256(ticket text) is retained by the store
    // - one browser-binding hash may mint only one ticket until expiry
    // - TTL is strictly bounded
    // - ticket and binding-reservation state are both hard-capped
    StorageHandoffTicketResult issue(
        const std::string& fingerprint,
        const std::string& home_node,
        std::uint64_t projection_revision,
        const std::string& browser_binding_hash,
        std::int64_t ttl_seconds,
        std::string* out_ticket,
        std::int64_t* out_expires_at = nullptr
    );

    // Atomically validates and removes one ticket.
    //
    // Security:
    // - browser_binding_secret must match the verifier bound at issue time
    // - binding comparison is constant-time
    // - a binding mismatch does NOT erase the ticket, preventing an attacker
    //   who stole only the ticket from burning the intended browser's handoff
    // - a successful ticket can never be consumed twice
    StorageHandoffTicketResult consume(
        const std::string& ticket,
        const std::string& browser_binding_secret,
        StorageHandoffTicketClaims* out_claims
    );

    void prune_expired();

    std::size_t size() const;

private:
    struct Entry {
        std::string fingerprint;
        std::string home_node;
        std::uint64_t projection_revision = 0;

        // One-way verifier only. The plaintext browser secret is never stored.
        std::string browser_binding_hash;

        std::int64_t issued_at = 0;
        std::int64_t expires_at = 0;
    };

    std::int64_t now_epoch_safe() const noexcept;

    void prune_expired_locked(
        std::int64_t now
    );

    const std::size_t max_entries_;
    const StorageHandoffTicketClock clock_;

    std::unordered_map<std::string, Entry>
        entries_by_hash_;

    // Security: retain a one-way browser-binding reservation even after a
    // ticket is consumed. This makes response-loss retries fail closed instead
    // of creating two simultaneously valid one-time capabilities.
    std::unordered_map<std::string, std::int64_t>
        binding_reservations_;

    mutable std::mutex mutex_;
};

} // namespace pqnas::cluster
