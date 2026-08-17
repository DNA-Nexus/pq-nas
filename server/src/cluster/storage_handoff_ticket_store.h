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
    NotFound,
    Expired,
    InternalError
};

const char* storage_handoff_ticket_result_code(
    StorageHandoffTicketResult result
) noexcept;

// Security: callers must clear plaintext tickets after transferring them to
// the browser. The store itself retains only a SHA-256 verifier.
void clear_storage_handoff_ticket_text(
    std::string* value
) noexcept;

struct StorageHandoffTicketClaims {
    std::string fingerprint;
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
    // - TTL is strictly bounded
    // - the in-memory store has a hard entry cap
    StorageHandoffTicketResult issue(
        const std::string& fingerprint,
        std::int64_t ttl_seconds,
        std::string* out_ticket,
        std::int64_t* out_expires_at = nullptr
    );

    // Atomically validates and removes one ticket.
    // A successful ticket can never be consumed twice.
    StorageHandoffTicketResult consume(
        const std::string& ticket,
        StorageHandoffTicketClaims* out_claims
    );

    void prune_expired();

    std::size_t size() const;

private:
    struct Entry {
        std::string fingerprint;
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

    mutable std::mutex mutex_;
};

} // namespace pqnas::cluster
