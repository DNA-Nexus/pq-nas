#pragma once

#include <filesystem>
#include <mutex>
#include <string>

namespace pqnas::cluster {

enum class NodeTlsTrustStoreResult {
    Ok,
    InvalidInput,
    NotFound,
    TrustConflict,
    PinInvalid,
    StorageUnsafe,
    IoFailed,
    PersistFailed
};

const char* node_tls_trust_store_result_code(
    NodeTlsTrustStoreResult result
) noexcept;

class NodeTlsTrustStore {
public:
    // A missing directory is a valid empty trust store.
    //
    // Security: every existing entry is validated before the configured path
    // becomes authoritative.
    bool load(
        const std::string& directory,
        std::string* error
    );

    // Returns the canonical public SPKI pin for one node.
    NodeTlsTrustStoreResult get(
        const std::string& node_id,
        std::string* tls_spki_sha256_pin,
        std::string* error
    ) const;

    // Establishes an immutable first TLS identity binding.
    //
    // Security:
    // - an absent node may be bound exactly once
    // - retrying the exact same pin is idempotent
    // - a different existing pin fails closed with TrustConflict
    // - this API can never rotate or replace an existing TLS identity
    //
    // A future TLS-key rotation lifecycle must use a separate API with stronger
    // authorization and explicit expected-generation semantics.
    NodeTlsTrustStoreResult bind_if_absent(
        const std::string& node_id,
        const std::string& tls_spki_sha256_pin,
        std::string* error
    );

private:
    mutable std::mutex mutex_;
    std::filesystem::path directory_;
};

} // namespace pqnas::cluster
