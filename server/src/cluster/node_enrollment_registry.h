#pragma once

#include "cluster/node_enrollment_token.h"

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
    kNodeEnrollmentRegistrySchemaVersion = 2;

inline constexpr int
    kNodeEnrollmentRegistryMinimumReadableSchemaVersion = 1;

enum class NodeEnrollmentState {
    Pending,
    Revoked,
    Consumed
};

const char* node_enrollment_state_name(
    NodeEnrollmentState state
);

enum class NodeEnrollmentProvisioningStage {
    None,
    Prepared,
    CredentialPersisted,
    NodeRegistered
};

const char* node_enrollment_provisioning_stage_name(
    NodeEnrollmentProvisioningStage stage
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
    ProvisioningActive,
    ProvisioningStageMismatch,
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

    // Durable, non-secret transaction metadata. The bootstrap verifier remains
    // available until provisioning is finalized or rolled back.
    NodeEnrollmentProvisioningStage provisioning_stage =
        NodeEnrollmentProvisioningStage::None;

    // Records when bootstrap authentication was accepted. This remains on a
    // consumed record so recovery may finalize after the original TTL expires.
    std::string provisioning_started_at;

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

    // Authenticates one pending intent and durably starts provisioning. Every
    // expectation, constant-time verifier comparison, and mutation occurs under
    // the same registry lock.
    NodeEnrollmentRegistryResult
    prepare_provisioning_if_matches(
        const std::string& enrollment_id,
        const std::string& expected_node_id,
        const std::string& expected_api_base_url,
        int presented_protocol_version,
        std::uint64_t expected_generation,
        const std::string& presented_token_hash,
        const std::string& provisioning_started_at,
        NodeEnrollmentRecord* prepared,
        std::string* error
    );

    // Re-authenticates an already prepared transaction without mutating it.
    // Only the persisted stage is returned; the verifier never leaves the
    // registry's constant-time comparison boundary.
    NodeEnrollmentRegistryResult
    authenticate_active_provisioning_if_matches(
        const std::string& enrollment_id,
        const std::string& expected_node_id,
        const std::string& expected_api_base_url,
        int presented_protocol_version,
        std::uint64_t expected_generation,
        const std::string& presented_token_hash,
        NodeEnrollmentProvisioningStage* authenticated_stage,
        std::string* error
    ) const;

    // Advances only the exact expected stage. This compare-and-mutate boundary
    // prevents concurrent bootstrap requests from skipping durable work.
    NodeEnrollmentRegistryResult
    advance_provisioning_stage(
        const std::string& enrollment_id,
        std::uint64_t expected_generation,
        NodeEnrollmentProvisioningStage expected_stage,
        NodeEnrollmentProvisioningStage next_stage,
        NodeEnrollmentRecord* advanced,
        std::string* error
    );

    // Called only after the service has rolled back external state belonging to
    // this transaction. The verifier remains available for a safe retry.
    NodeEnrollmentRegistryResult reset_provisioning(
        const std::string& enrollment_id,
        std::uint64_t expected_generation,
        NodeEnrollmentProvisioningStage expected_stage,
        NodeEnrollmentRecord* reset,
        std::string* error
    );

    // Finalizes only after node registration is durable. Authentication may
    // have occurred before expiry even when recovery completes after expiry.
    NodeEnrollmentRegistryResult finalize_provisioning(
        const std::string& enrollment_id,
        std::uint64_t expected_generation,
        const std::string& consumed_at,
        NodeEnrollmentRecord* consumed,
        std::string* error
    );

private:
    // Persists and installs one candidate while the registry mutex is held.
    // If rename succeeds but directory fsync fails, memory is still advanced to
    // the committed inode before PersistFailed is returned.
    bool commit_candidate_locked(
        std::unordered_map<
            std::string,
            NodeEnrollmentRecord
        >&& candidate,
        std::string* error
    );

    mutable std::mutex mutex_;
    std::filesystem::path path_;

    std::unordered_map<
        std::string,
        NodeEnrollmentRecord
    > records_;
};

} // namespace pqnas::cluster
