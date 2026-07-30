#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace pqnas::cluster {

inline constexpr int
    kNodeEnrollmentRegistrySchemaVersion = 1;

inline constexpr std::size_t
    kNodeEnrollmentTokenHashBytes = 32;

inline constexpr std::size_t
    kNodeEnrollmentTokenHashChars = 43;

enum class NodeEnrollmentState {
    Pending,
    Revoked,
    Consumed
};

const char* node_enrollment_state_name(
    NodeEnrollmentState state
);

enum class NodeEnrollmentRegistryResult {
    Ok,
    InvalidInput,
    NotFound,
    DuplicateEnrollmentId,
    DuplicateActiveUrl,
    GenerationMismatch,
    Expired,
    Revoked,
    AlreadyConsumed,
    NodeMismatch,
    UrlMismatch,
    ProtocolIncompatible,
    TokenInvalid,
    PersistFailed
};

// Stable internal reason code. HTTP routes may deliberately collapse these
// into less detailed external errors where disclosure would be undesirable.
const char* node_enrollment_registry_result_code(
    NodeEnrollmentRegistryResult result
);

struct NodeEnrollmentRecord {
    std::string enrollment_id;
    std::string node_id;
    std::string api_base_url;

    int required_protocol_version = 0;

    // This is a fixed-size hash/verifier, never the plaintext bootstrap token.
    // Terminal records clear it so revoked or consumed intents retain no
    // reusable authentication material.
    std::string token_hash;

    std::string created_at;
    std::string expires_at;

    NodeEnrollmentState state =
        NodeEnrollmentState::Pending;

    std::string revoked_at;
    std::string consumed_at;

    // Monotonically increasing for each node ID. Consumers provide the
    // expected generation so replacement races fail closed.
    std::uint64_t generation = 0;
};

class NodeEnrollmentRegistry {
public:
    // Missing file means a valid empty registry. Malformed, unexpectedly
    // linked, incorrectly owned, or insufficiently private files fail closed
    // without replacing the current in-memory snapshot.
    bool load(
        const std::string& path,
        std::string* error
    );

    std::optional<NodeEnrollmentRecord> get(
        const std::string& enrollment_id
    ) const;

    // Internal snapshot: includes pending token hashes. Admin responses must
    // use a redacted service-layer representation instead of serializing this
    // structure directly.
    std::vector<NodeEnrollmentRecord> snapshot() const;

    // Creates a pending intent and atomically revokes any previous pending
    // intent for the same node. An expired intent holding the same URL for a
    // different node is also revoked before the URL is reassigned.
    //
    // The registry assigns the next node-specific generation while holding
    // its mutex. The plaintext bootstrap token is never accepted here.
    NodeEnrollmentRegistryResult create_or_replace_pending(
        const std::string& enrollment_id,
        const std::string& node_id,
        const std::string& api_base_url,
        int required_protocol_version,
        const std::string& token_hash,
        const std::string& created_at,
        const std::string& expires_at,
        NodeEnrollmentRecord* created,
        std::string* error
    );

    NodeEnrollmentRegistryResult revoke_pending(
        const std::string& enrollment_id,
        std::uint64_t expected_generation,
        const std::string& revoked_at,
        NodeEnrollmentRecord* revoked,
        std::string* error
    );

    // Compare-and-consume operation used later by the bootstrap endpoint.
    // Every expectation and the state mutation occur under one registry lock.
    //
    // presented_token_hash must be computed by the service layer before this
    // call. It is compared in constant time against the persisted verifier.
    NodeEnrollmentRegistryResult consume_pending_if_matches(
        const std::string& enrollment_id,
        const std::string& expected_node_id,
        const std::string& expected_api_base_url,
        int presented_protocol_version,
        std::uint64_t expected_generation,
        const std::string& presented_token_hash,
        const std::string& consumed_at,
        NodeEnrollmentRecord* consumed,
        std::string* error
    );

private:
    bool persist_candidate_locked(
        const std::unordered_map<
            std::string,
            NodeEnrollmentRecord
        >& candidate,
        std::string* error
    ) const;

    mutable std::mutex mutex_;
    std::filesystem::path path_;

    std::unordered_map<
        std::string,
        NodeEnrollmentRecord
    > records_;
};

} // namespace pqnas::cluster
