#pragma once

#include "cluster/node_enrollment_registry.h"
#include "cluster/node_enrollment_token.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace pqnas::cluster {

inline constexpr std::size_t
    kNodeEnrollmentIdBytes = 24;

inline constexpr std::size_t
    kNodeEnrollmentIdChars = 32;

inline constexpr int
    kNodeEnrollmentIdGenerationAttempts = 8;

using NodeEnrollmentAuditEmit =
    std::function<void(
        const std::string& event,
        const std::string& outcome,
        const std::map<
            std::string,
            std::string
        >& fields
    )>;

using NodeEnrollmentIdGenerate =
    std::function<bool(std::string*)>;

using NodeEnrollmentTokenGenerate =
    std::function<NodeEnrollmentTokenResult(
        NodeEnrollmentTokenMaterial*
    )>;

struct NodeEnrollmentServiceConfig {
    NodeEnrollmentRegistry* registry = nullptr;

    // Empty generators are replaced with secure libsodium-backed defaults.
    // Injection exists only to make collision and crypto-failure paths
    // deterministic in tests.
    NodeEnrollmentIdGenerate
        generate_enrollment_id;

    NodeEnrollmentTokenGenerate
        generate_token;

    NodeEnrollmentAuditEmit audit_emit;
};

enum class NodeEnrollmentServiceResult {
    Ok,
    ServiceUnavailable,
    InvalidInput,
    IdGenerationFailed,
    TokenGenerationFailed,
    EnrollmentIdConflict,
    UrlConflict,
    PersistFailed,
    RegistryFailed
};

const char* node_enrollment_service_result_code(
    NodeEnrollmentServiceResult result
);

// Deliberately excludes token_hash. This is the only enrollment record shape
// that admin routes should serialize.
struct NodeEnrollmentView {
    std::string enrollment_id;
    std::string node_id;
    std::string api_base_url;

    int required_protocol_version = 0;

    std::string created_at;
    std::string expires_at;
    std::string state;
    std::string revoked_at;
    std::string consumed_at;

    std::uint64_t generation = 0;
};

NodeEnrollmentView redact_node_enrollment_record(
    const NodeEnrollmentRecord& record
);

// Move-only result that owns the plaintext bootstrap token until the caller
// transfers it exactly once. Destruction securely clears an untaken token.
class NodeEnrollmentCreateResult {
public:
    NodeEnrollmentCreateResult() = default;
    ~NodeEnrollmentCreateResult();

    NodeEnrollmentCreateResult(
        const NodeEnrollmentCreateResult&
    ) = delete;

    NodeEnrollmentCreateResult& operator=(
        const NodeEnrollmentCreateResult&
    ) = delete;

    NodeEnrollmentCreateResult(
        NodeEnrollmentCreateResult&& other
    ) noexcept;

    NodeEnrollmentCreateResult& operator=(
        NodeEnrollmentCreateResult&& other
    ) noexcept;

    bool ok() const noexcept;

    NodeEnrollmentServiceResult
    result() const noexcept;

    const char* error_code() const noexcept;

    const NodeEnrollmentView&
    enrollment() const noexcept;

    // Clears any previous output content before transferring the token.
    // Returns false after the first successful transfer.
    bool take_bootstrap_token(
        std::string* output
    ) noexcept;

private:
    friend class NodeEnrollmentService;

    void set_failure(
        NodeEnrollmentServiceResult result
    );

    void set_success(
        NodeEnrollmentView enrollment,
        std::string* bootstrap_token
    );

    void clear_secret() noexcept;

    bool ok_ = false;

    NodeEnrollmentServiceResult result_ =
        NodeEnrollmentServiceResult::
            ServiceUnavailable;

    NodeEnrollmentView enrollment_;
    std::string bootstrap_token_;
};

class NodeEnrollmentService {
public:
    explicit NodeEnrollmentService(
        NodeEnrollmentServiceConfig config
    );

    // created_at and expires_at are supplied by the caller so this service is
    // deterministic and does not introduce a second cluster clock source.
    NodeEnrollmentCreateResult create_pending(
        const std::string& node_id,
        const std::string& api_base_url,
        int required_protocol_version,
        const std::string& created_at,
        const std::string& expires_at,
        const std::string& actor_fingerprint
    );

    // Redacts every verifier before returning the snapshot.
    std::vector<NodeEnrollmentView>
    redacted_snapshot() const;

private:
    static NodeEnrollmentCreateResult
    failure_result(
        NodeEnrollmentServiceResult result
    );

    bool config_valid() const;

    void audit_best_effort(
        const std::string& outcome,
        const std::string& node_id,
        const std::string& actor_fingerprint,
        const std::string& reason_code,
        const std::string& enrollment_id,
        std::uint64_t generation
    ) const;

    NodeEnrollmentServiceConfig config_;
};

} // namespace pqnas::cluster
