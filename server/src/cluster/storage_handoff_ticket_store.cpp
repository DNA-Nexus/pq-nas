#include "cluster/storage_handoff_ticket_store.h"
#include "cluster/cluster_identity_validation.h"

#include <sodium.h>

#include <array>
#include <ctime>
#include <limits>
#include <string>
#include <utility>

namespace pqnas::cluster {
namespace {

inline constexpr std::size_t kEncodedBufferChars =
    sodium_base64_ENCODED_LEN(
        kStorageHandoffTicketBytes,
        sodium_base64_VARIANT_URLSAFE_NO_PADDING
    );

static_assert(
    kEncodedBufferChars ==
        kStorageHandoffTicketChars + 1,
    "unexpected libsodium base64url size"
);

inline constexpr int kGenerationAttempts = 8;

using RawTicket = std::array<
    unsigned char,
    kStorageHandoffTicketBytes
>;

using TicketHash = std::array<
    unsigned char,
    crypto_hash_sha256_BYTES
>;

using EncodedBuffer = std::array<
    char,
    kEncodedBufferChars
>;

template <typename Array>
class SensitiveArrayWiper {
public:
    explicit SensitiveArrayWiper(
        Array& value
    ) noexcept
        : value_(value) {}

    ~SensitiveArrayWiper() {
        sodium_memzero(
            value_.data(),
            value_.size()
        );
    }

    SensitiveArrayWiper(
        const SensitiveArrayWiper&
    ) = delete;

    SensitiveArrayWiper& operator=(
        const SensitiveArrayWiper&
    ) = delete;

private:
    Array& value_;
};

class SensitiveStringWiper {
public:
    explicit SensitiveStringWiper(
        std::string& value
    ) noexcept
        : value_(value) {}

    ~SensitiveStringWiper() {
        clear_storage_handoff_ticket_text(
            &value_
        );
    }

    SensitiveStringWiper(
        const SensitiveStringWiper&
    ) = delete;

    SensitiveStringWiper& operator=(
        const SensitiveStringWiper&
    ) = delete;

private:
    std::string& value_;
};

bool sodium_ready() noexcept {
    return sodium_init() >= 0;
}

bool valid_fingerprint(
    const std::string& fingerprint
) noexcept {
    if (fingerprint.size() !=
        kStorageHandoffFingerprintHexChars) {
        return false;
    }

    for (unsigned char ch : fingerprint) {
        const bool hex =
            (ch >= '0' && ch <= '9') ||
            (ch >= 'a' && ch <= 'f') ||
            (ch >= 'A' && ch <= 'F');

        if (!hex) {
            return false;
        }
    }

    return true;
}

bool valid_ticket_text(
    const std::string& ticket
) noexcept {
    if (ticket.size() !=
        kStorageHandoffTicketChars) {
        return false;
    }

    for (unsigned char ch : ticket) {
        const bool alphanumeric =
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9');

        if (!alphanumeric &&
            ch != '-' &&
            ch != '_') {
            return false;
        }
    }

    return true;
}

bool encode_32(
    const unsigned char* raw,
    std::size_t raw_size,
    EncodedBuffer* output
) noexcept {
    if (!raw ||
        !output ||
        raw_size != kStorageHandoffTicketBytes) {
        return false;
    }

    output->fill('\0');

    const char* result =
        sodium_bin2base64(
            output->data(),
            output->size(),
            raw,
            raw_size,
            sodium_base64_VARIANT_URLSAFE_NO_PADDING
        );

    return
        result != nullptr &&
        (*output)[kStorageHandoffTicketChars] ==
            '\0';
}

bool generate_ticket(
    std::string* output
) {
    if (!output) {
        return false;
    }

    clear_storage_handoff_ticket_text(
        output
    );

    RawTicket raw{};
    SensitiveArrayWiper<RawTicket>
        raw_wiper(raw);

    EncodedBuffer encoded{};
    SensitiveArrayWiper<EncodedBuffer>
        encoded_wiper(encoded);

    randombytes_buf(
        raw.data(),
        raw.size()
    );

    if (!encode_32(
            raw.data(),
            raw.size(),
            &encoded)) {
        return false;
    }

    try {
        output->assign(
            encoded.data(),
            kStorageHandoffTicketChars
        );
    } catch (...) {
        clear_storage_handoff_ticket_text(
            output
        );
        return false;
    }

    return true;
}

bool hash_ticket(
    const std::string& ticket,
    std::string* output_hash
) {
    if (!output_hash ||
        !valid_ticket_text(ticket)) {
        return false;
    }

    output_hash->clear();

    TicketHash digest{};
    SensitiveArrayWiper<TicketHash>
        digest_wiper(digest);

    if (crypto_hash_sha256(
            digest.data(),
            reinterpret_cast<
                const unsigned char*
            >(ticket.data()),
            ticket.size()) != 0) {
        return false;
    }

    EncodedBuffer encoded{};
    SensitiveArrayWiper<EncodedBuffer>
        encoded_wiper(encoded);

    if (!encode_32(
            digest.data(),
            digest.size(),
            &encoded)) {
        return false;
    }

    try {
        output_hash->assign(
            encoded.data(),
            kStorageHandoffTicketChars
        );
    } catch (...) {
        output_hash->clear();
        return false;
    }

    return true;
}

} // namespace

const char* storage_handoff_ticket_result_code(
    StorageHandoffTicketResult result
) noexcept {
    switch (result) {
        case StorageHandoffTicketResult::Ok:
            return "ok";

        case StorageHandoffTicketResult::InvalidInput:
            return "handoff_ticket_invalid";

        case StorageHandoffTicketResult::
                CryptoUnavailable:
            return "handoff_ticket_crypto_unavailable";

        case StorageHandoffTicketResult::CryptoFailed:
            return "handoff_ticket_crypto_failed";

        case StorageHandoffTicketResult::
                ClockUnavailable:
            return "handoff_ticket_clock_unavailable";

        case StorageHandoffTicketResult::StoreFull:
            return "handoff_ticket_store_full";

        case StorageHandoffTicketResult::NotFound:
            return "handoff_ticket_not_found";

        case StorageHandoffTicketResult::Expired:
            return "handoff_ticket_expired";

        case StorageHandoffTicketResult::InternalError:
            return "handoff_ticket_internal_error";
    }

    return "handoff_ticket_internal_error";
}

void clear_storage_handoff_ticket_text(
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

StorageHandoffTicketStore::
StorageHandoffTicketStore(
    std::size_t max_entries,
    StorageHandoffTicketClock clock
)
    : max_entries_(max_entries),
      clock_(std::move(clock)) {}

std::int64_t
StorageHandoffTicketStore::
now_epoch_safe() const noexcept {
    try {
        if (clock_) {
            return clock_();
        }

        return static_cast<std::int64_t>(
            std::time(nullptr)
        );
    } catch (...) {
        return 0;
    }
}

void StorageHandoffTicketStore::
prune_expired_locked(
    std::int64_t now
) {
    for (auto iterator =
             entries_by_hash_.begin();
         iterator != entries_by_hash_.end();) {
        if (iterator->second.expires_at <= now) {
            iterator =
                entries_by_hash_.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

StorageHandoffTicketResult
StorageHandoffTicketStore::issue(
    const std::string& fingerprint,
    const std::string& home_node,
    std::uint64_t projection_revision,
    std::int64_t ttl_seconds,
    std::string* out_ticket,
    std::int64_t* out_expires_at
) {
    if (out_expires_at) {
        *out_expires_at = 0;
    }

    if (!out_ticket) {
        return StorageHandoffTicketResult::
            InvalidInput;
    }

    clear_storage_handoff_ticket_text(
        out_ticket
    );

    // Security: never create an unbound handoff bearer. The ticket must
    // identify both the Storage Node and the Director-owned projection revision
    // whose authorization decision allowed issuance.
    if (!valid_fingerprint(fingerprint) ||
        !valid_cluster_node_id(home_node) ||
        projection_revision == 0 ||
        ttl_seconds <= 0 ||
        ttl_seconds >
            kStorageHandoffTicketMaxTtlSeconds) {
        return StorageHandoffTicketResult::
            InvalidInput;
    }

    if (!sodium_ready()) {
        return StorageHandoffTicketResult::
            CryptoUnavailable;
    }

    const std::int64_t now =
        now_epoch_safe();

    if (now <= 0 ||
        now >
            std::numeric_limits<
                std::int64_t
            >::max() - ttl_seconds) {
        return StorageHandoffTicketResult::
            ClockUnavailable;
    }

    const std::int64_t expires_at =
        now + ttl_seconds;

    for (int attempt = 0;
         attempt < kGenerationAttempts;
         ++attempt) {
        std::string ticket;
        SensitiveStringWiper
            ticket_wiper(ticket);

        if (!generate_ticket(&ticket)) {
            return StorageHandoffTicketResult::
                CryptoFailed;
        }

        std::string ticket_hash;

        if (!hash_ticket(
                ticket,
                &ticket_hash)) {
            return StorageHandoffTicketResult::
                CryptoFailed;
        }

        bool inserted = false;

        try {
            std::lock_guard<std::mutex>
                lock(mutex_);

            prune_expired_locked(now);

            if (entries_by_hash_.size() >=
                max_entries_) {
                return StorageHandoffTicketResult::
                    StoreFull;
            }

            Entry entry;
            entry.fingerprint = fingerprint;
            entry.home_node = home_node;
            entry.projection_revision =
                projection_revision;
            entry.issued_at = now;
            entry.expires_at = expires_at;

            inserted =
                entries_by_hash_.emplace(
                    ticket_hash,
                    std::move(entry)
                ).second;
        } catch (...) {
            return StorageHandoffTicketResult::
                InternalError;
        }

        if (!inserted) {
            // Cryptographic collision is extraordinarily unlikely,
            // but never replace an existing one-time ticket.
            continue;
        }

        out_ticket->swap(ticket);

        if (out_expires_at) {
            *out_expires_at = expires_at;
        }

        return StorageHandoffTicketResult::Ok;
    }

    return StorageHandoffTicketResult::
        InternalError;
}

StorageHandoffTicketResult
StorageHandoffTicketStore::consume(
    const std::string& ticket,
    StorageHandoffTicketClaims* out_claims
) {
    if (!out_claims) {
        return StorageHandoffTicketResult::
            InvalidInput;
    }

    *out_claims =
        StorageHandoffTicketClaims{};

    if (!valid_ticket_text(ticket)) {
        return StorageHandoffTicketResult::
            InvalidInput;
    }

    if (!sodium_ready()) {
        return StorageHandoffTicketResult::
            CryptoUnavailable;
    }

    std::string ticket_hash;

    if (!hash_ticket(
            ticket,
            &ticket_hash)) {
        return StorageHandoffTicketResult::
            CryptoFailed;
    }

    const std::int64_t now =
        now_epoch_safe();

    if (now <= 0) {
        return StorageHandoffTicketResult::
            ClockUnavailable;
    }

    std::lock_guard<std::mutex>
        lock(mutex_);

    auto iterator =
        entries_by_hash_.find(ticket_hash);

    if (iterator ==
        entries_by_hash_.end()) {
        return StorageHandoffTicketResult::
            NotFound;
    }

    if (iterator->second.expires_at <= now) {
        entries_by_hash_.erase(iterator);

        return StorageHandoffTicketResult::
            Expired;
    }

    StorageHandoffTicketClaims claims;

    try {
        claims.fingerprint =
            iterator->second.fingerprint;

        claims.home_node =
            iterator->second.home_node;

        claims.projection_revision =
            iterator->second.projection_revision;

        claims.issued_at =
            iterator->second.issued_at;

        claims.expires_at =
            iterator->second.expires_at;
    } catch (...) {
        return StorageHandoffTicketResult::
            InternalError;
    }

    // Security: erase under the same mutex as lookup. Two simultaneous
    // consumers can never successfully redeem the same ticket.
    entries_by_hash_.erase(iterator);

    *out_claims = std::move(claims);

    return StorageHandoffTicketResult::Ok;
}

void StorageHandoffTicketStore::
prune_expired() {
    const std::int64_t now =
        now_epoch_safe();

    if (now <= 0) {
        return;
    }

    std::lock_guard<std::mutex>
        lock(mutex_);

    prune_expired_locked(now);
}

std::size_t StorageHandoffTicketStore::
size() const {
    std::lock_guard<std::mutex>
        lock(mutex_);

    return entries_by_hash_.size();
}

} // namespace pqnas::cluster
