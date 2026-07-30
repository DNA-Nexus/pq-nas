#include "cluster/node_enrollment_service.h"

#include "cluster/cluster_identity_validation.h"

#include <sodium.h>

#include <array>
#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace pqnas::cluster {
namespace {

inline constexpr std::size_t
    kEnrollmentIdBufferChars =
        sodium_base64_ENCODED_LEN(
            kNodeEnrollmentIdBytes,
            sodium_base64_VARIANT_URLSAFE_NO_PADDING
        );

static_assert(
    kEnrollmentIdBufferChars ==
        kNodeEnrollmentIdChars + 1,
    "unexpected enrollment ID base64url size"
);

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
        clear_node_enrollment_token_text(
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

bool valid_generated_enrollment_id(
    const std::string& value
) noexcept {
    if (value.size() !=
        kNodeEnrollmentIdChars) {
        return false;
    }

    for (unsigned char ch : value) {
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

bool secure_generate_enrollment_id(
    std::string* output
) {
    if (!output) {
        return false;
    }

    output->clear();

    if (sodium_init() < 0) {
        return false;
    }

    std::array<
        unsigned char,
        kNodeEnrollmentIdBytes
    > random_bytes{};

    SensitiveArrayWiper<
        decltype(random_bytes)
    > random_wiper(random_bytes);

    std::array<
        char,
        kEnrollmentIdBufferChars
    > encoded{};

    SensitiveArrayWiper<
        decltype(encoded)
    > encoded_wiper(encoded);

    randombytes_buf(
        random_bytes.data(),
        random_bytes.size()
    );

    const char* result =
        sodium_bin2base64(
            encoded.data(),
            encoded.size(),
            random_bytes.data(),
            random_bytes.size(),
            sodium_base64_VARIANT_URLSAFE_NO_PADDING
        );

    if (!result ||
        encoded[
            kNodeEnrollmentIdChars
        ] != '\0') {
        return false;
    }

    try {
        output->assign(
            encoded.data(),
            kNodeEnrollmentIdChars
        );
    } catch (...) {
        output->clear();
        return false;
    }

    return valid_generated_enrollment_id(
        *output
    );
}

NodeEnrollmentServiceResult
map_registry_failure(
    NodeEnrollmentRegistryResult result
) {
    switch (result) {
        case NodeEnrollmentRegistryResult::Ok:
            return NodeEnrollmentServiceResult::Ok;

        case NodeEnrollmentRegistryResult::
                InvalidInput:
            return NodeEnrollmentServiceResult::
                InvalidInput;

        case NodeEnrollmentRegistryResult::
                DuplicateEnrollmentId:
            return NodeEnrollmentServiceResult::
                EnrollmentIdConflict;

        case NodeEnrollmentRegistryResult::
                DuplicateActiveUrl:
            return NodeEnrollmentServiceResult::
                UrlConflict;

        case NodeEnrollmentRegistryResult::
                PersistFailed:
            return NodeEnrollmentServiceResult::
                PersistFailed;

        case NodeEnrollmentRegistryResult::NotFound:
        case NodeEnrollmentRegistryResult::
                GenerationMismatch:
        case NodeEnrollmentRegistryResult::Expired:
        case NodeEnrollmentRegistryResult::Revoked:
        case NodeEnrollmentRegistryResult::
                AlreadyConsumed:
        case NodeEnrollmentRegistryResult::
                NodeMismatch:
        case NodeEnrollmentRegistryResult::
                UrlMismatch:
        case NodeEnrollmentRegistryResult::
                ProtocolIncompatible:
        case NodeEnrollmentRegistryResult::
                TokenInvalid:
            return NodeEnrollmentServiceResult::
                RegistryFailed;
    }

    return NodeEnrollmentServiceResult::
        RegistryFailed;
}

} // namespace

const char* node_enrollment_service_result_code(
    NodeEnrollmentServiceResult result
) {
    switch (result) {
        case NodeEnrollmentServiceResult::Ok:
            return "ok";

        case NodeEnrollmentServiceResult::
                ServiceUnavailable:
            return "service_unavailable";

        case NodeEnrollmentServiceResult::
                InvalidInput:
            return "enrollment_invalid";

        case NodeEnrollmentServiceResult::
                IdGenerationFailed:
            return "enrollment_id_generation_failed";

        case NodeEnrollmentServiceResult::
                TokenGenerationFailed:
            return "enrollment_token_generation_failed";

        case NodeEnrollmentServiceResult::
                EnrollmentIdConflict:
            return "enrollment_id_conflict";

        case NodeEnrollmentServiceResult::
                UrlConflict:
            return "enrollment_url_conflict";

        case NodeEnrollmentServiceResult::
                PersistFailed:
            return "enrollment_persist_failed";

        case NodeEnrollmentServiceResult::
                RegistryFailed:
            return "enrollment_registry_failed";
    }

    return "enrollment_registry_failed";
}

NodeEnrollmentView redact_node_enrollment_record(
    const NodeEnrollmentRecord& record
) {
    NodeEnrollmentView output;

    output.enrollment_id =
        record.enrollment_id;

    output.node_id =
        record.node_id;

    output.api_base_url =
        record.api_base_url;

    output.required_protocol_version =
        record.required_protocol_version;

    output.created_at =
        record.created_at;

    output.expires_at =
        record.expires_at;

    output.state =
        node_enrollment_state_name(
            record.state
        );

    output.revoked_at =
        record.revoked_at;

    output.consumed_at =
        record.consumed_at;

    output.generation =
        record.generation;

    return output;
}

NodeEnrollmentCreateResult::
~NodeEnrollmentCreateResult() {
    clear_secret();
}

NodeEnrollmentCreateResult::
NodeEnrollmentCreateResult(
    NodeEnrollmentCreateResult&& other
) noexcept
    : ok_(other.ok_),
      result_(other.result_),
      enrollment_(
          std::move(other.enrollment_)),
      bootstrap_token_(
          std::move(other.bootstrap_token_)) {
    other.ok_ = false;

    other.result_ =
        NodeEnrollmentServiceResult::
            ServiceUnavailable;

    other.clear_secret();
}

NodeEnrollmentCreateResult&
NodeEnrollmentCreateResult::operator=(
    NodeEnrollmentCreateResult&& other
) noexcept {
    if (this == &other) {
        return *this;
    }

    clear_secret();

    ok_ = other.ok_;
    result_ = other.result_;

    enrollment_ =
        std::move(other.enrollment_);

    bootstrap_token_ =
        std::move(other.bootstrap_token_);

    other.ok_ = false;

    other.result_ =
        NodeEnrollmentServiceResult::
            ServiceUnavailable;

    other.clear_secret();

    return *this;
}

bool NodeEnrollmentCreateResult::ok()
    const noexcept {
    return ok_;
}

NodeEnrollmentServiceResult
NodeEnrollmentCreateResult::result()
    const noexcept {
    return result_;
}

const char*
NodeEnrollmentCreateResult::error_code()
    const noexcept {
    return node_enrollment_service_result_code(
        result_
    );
}

const NodeEnrollmentView&
NodeEnrollmentCreateResult::enrollment()
    const noexcept {
    return enrollment_;
}

bool NodeEnrollmentCreateResult::
take_bootstrap_token(
    std::string* output
) noexcept {
    if (!output ||
        !ok_ ||
        bootstrap_token_.size() !=
            kNodeEnrollmentTokenChars) {
        return false;
    }

    clear_node_enrollment_token_text(
        output
    );

    output->swap(
        bootstrap_token_
    );

    clear_node_enrollment_token_text(
        &bootstrap_token_
    );

    return true;
}

void NodeEnrollmentCreateResult::set_failure(
    NodeEnrollmentServiceResult result
) {
    clear_secret();

    ok_ = false;
    result_ = result;
    enrollment_ = {};
}

void NodeEnrollmentCreateResult::set_success(
    NodeEnrollmentView enrollment,
    std::string* bootstrap_token
) {
    clear_secret();

    enrollment_ =
        std::move(enrollment);

    if (bootstrap_token) {
        bootstrap_token_.swap(
            *bootstrap_token
        );
    }

    ok_ =
        bootstrap_token_.size() ==
            kNodeEnrollmentTokenChars;

    result_ = ok_
        ? NodeEnrollmentServiceResult::Ok
        : NodeEnrollmentServiceResult::
            TokenGenerationFailed;

    if (!ok_) {
        clear_secret();
        enrollment_ = {};
    }
}

void NodeEnrollmentCreateResult::
clear_secret() noexcept {
    clear_node_enrollment_token_text(
        &bootstrap_token_
    );
}

NodeEnrollmentService::NodeEnrollmentService(
    NodeEnrollmentServiceConfig config
)
    : config_(std::move(config)) {
    if (!config_.generate_enrollment_id) {
        config_.generate_enrollment_id =
            secure_generate_enrollment_id;
    }

    if (!config_.generate_token) {
        config_.generate_token =
            generate_node_enrollment_token;
    }
}

NodeEnrollmentCreateResult
NodeEnrollmentService::failure_result(
    NodeEnrollmentServiceResult result
) {
    NodeEnrollmentCreateResult output;
    output.set_failure(result);
    return output;
}

bool NodeEnrollmentService::config_valid()
    const {
    return
        config_.registry &&
        static_cast<bool>(
            config_.generate_enrollment_id
        ) &&
        static_cast<bool>(
            config_.generate_token
        ) &&
        static_cast<bool>(
            config_.audit_emit
        );
}

void NodeEnrollmentService::audit_best_effort(
    const std::string& outcome,
    const std::string& node_id,
    const std::string& actor_fingerprint,
    const std::string& reason_code,
    const std::string& enrollment_id,
    std::uint64_t generation
) const {
    if (!config_.audit_emit) {
        return;
    }

    // Security: never audit the URL, plaintext token, verifier, raw error,
    // request body or registry path.
    std::map<std::string, std::string> fields{
        {"reason_code", reason_code}
    };

    if (valid_cluster_node_id(node_id)) {
        fields["node_id"] = node_id;
    }

    if (!actor_fingerprint.empty()) {
        fields["actor_fp"] =
            actor_fingerprint;
    }

    if (outcome == "ok" &&
        valid_generated_enrollment_id(
            enrollment_id)) {
        fields["enrollment_id"] =
            enrollment_id;

        fields["generation"] =
            std::to_string(generation);
    }

    try {
        config_.audit_emit(
            "admin.cluster_node_enrollment.create",
            outcome,
            fields
        );
    } catch (...) {
        // Enrollment persistence must not be rolled back by audit failure.
    }
}

NodeEnrollmentCreateResult
NodeEnrollmentService::create_pending(
    const std::string& node_id,
    const std::string& api_base_url,
    int required_protocol_version,
    const std::string& created_at,
    const std::string& expires_at,
    const std::string& actor_fingerprint
) {
    if (!config_valid()) {
        return failure_result(
            NodeEnrollmentServiceResult::
                ServiceUnavailable
        );
    }

    std::string normalized_url;
    std::string validation_error;

    if (!valid_cluster_node_id(node_id) ||
        required_protocol_version < 1 ||
        required_protocol_version > 1000 ||
        created_at.empty() ||
        expires_at.empty() ||
        created_at >= expires_at ||
        !normalize_cluster_https_base_url(
            api_base_url,
            &normalized_url,
            &validation_error)) {
        audit_best_effort(
            "fail",
            node_id,
            actor_fingerprint,
            node_enrollment_service_result_code(
                NodeEnrollmentServiceResult::
                    InvalidInput
            ),
            "",
            0
        );

        return failure_result(
            NodeEnrollmentServiceResult::
                InvalidInput
        );
    }

    NodeEnrollmentTokenMaterial material;

    const NodeEnrollmentTokenResult
        token_result =
            config_.generate_token(
                &material
            );

    if (token_result !=
            NodeEnrollmentTokenResult::Ok ||
        !material.configured() ||
        !valid_node_enrollment_token_hash(
            material.token_hash())) {
        material.clear();

        audit_best_effort(
            "fail",
            node_id,
            actor_fingerprint,
            node_enrollment_service_result_code(
                NodeEnrollmentServiceResult::
                    TokenGenerationFailed
            ),
            "",
            0
        );

        return failure_result(
            NodeEnrollmentServiceResult::
                TokenGenerationFailed
        );
    }

    std::string token_hash =
        material.token_hash();

    SensitiveStringWiper
        hash_wiper(token_hash);

    std::string bootstrap_token;

    SensitiveStringWiper
        token_wiper(bootstrap_token);

    // Security: transfer plaintext out of the generator before persistence.
    // If any later operation fails, the local wiper destroys it and no caller
    // receives an unpersisted or unusable token.
    if (!material.take_token(
            &bootstrap_token)) {
        audit_best_effort(
            "fail",
            node_id,
            actor_fingerprint,
            node_enrollment_service_result_code(
                NodeEnrollmentServiceResult::
                    TokenGenerationFailed
            ),
            "",
            0
        );

        return failure_result(
            NodeEnrollmentServiceResult::
                TokenGenerationFailed
        );
    }

    for (int attempt = 0;
         attempt <
            kNodeEnrollmentIdGenerationAttempts;
         ++attempt) {
        std::string enrollment_id;

        if (!config_.generate_enrollment_id(
                &enrollment_id) ||
            !valid_generated_enrollment_id(
                enrollment_id)) {
            audit_best_effort(
                "fail",
                node_id,
                actor_fingerprint,
                node_enrollment_service_result_code(
                    NodeEnrollmentServiceResult::
                        IdGenerationFailed
                ),
                "",
                0
            );

            return failure_result(
                NodeEnrollmentServiceResult::
                    IdGenerationFailed
            );
        }

        NodeEnrollmentRecord created;
        std::string registry_error;

        const NodeEnrollmentRegistryResult
            registry_result =
                config_.registry->
                    create_or_replace_pending(
                        enrollment_id,
                        node_id,
                        normalized_url,
                        required_protocol_version,
                        token_hash,
                        created_at,
                        expires_at,
                        &created,
                        &registry_error
                    );

        if (registry_result ==
            NodeEnrollmentRegistryResult::Ok) {
            NodeEnrollmentCreateResult result;

            result.set_success(
                redact_node_enrollment_record(
                    created
                ),
                &bootstrap_token
            );

            if (!result.ok()) {
                // This cannot normally fail because the token was validated
                // before persistence. Fail closed if the invariant breaks.
                audit_best_effort(
                    "fail",
                    node_id,
                    actor_fingerprint,
                    node_enrollment_service_result_code(
                        NodeEnrollmentServiceResult::
                            TokenGenerationFailed
                    ),
                    "",
                    0
                );

                return failure_result(
                    NodeEnrollmentServiceResult::
                        TokenGenerationFailed
                );
            }

            audit_best_effort(
                "ok",
                node_id,
                actor_fingerprint,
                node_enrollment_service_result_code(
                    NodeEnrollmentServiceResult::Ok
                ),
                created.enrollment_id,
                created.generation
            );

            return result;
        }

        if (registry_result ==
            NodeEnrollmentRegistryResult::
                DuplicateEnrollmentId) {
            continue;
        }

        const NodeEnrollmentServiceResult
            service_result =
                map_registry_failure(
                    registry_result
                );

        audit_best_effort(
            "fail",
            node_id,
            actor_fingerprint,
            node_enrollment_service_result_code(
                service_result
            ),
            "",
            0
        );

        return failure_result(
            service_result
        );
    }

    audit_best_effort(
        "fail",
        node_id,
        actor_fingerprint,
        node_enrollment_service_result_code(
            NodeEnrollmentServiceResult::
                EnrollmentIdConflict
        ),
        "",
        0
    );

    return failure_result(
        NodeEnrollmentServiceResult::
            EnrollmentIdConflict
    );
}

std::vector<NodeEnrollmentView>
NodeEnrollmentService::redacted_snapshot()
    const {
    std::vector<NodeEnrollmentView> result;

    if (!config_.registry) {
        return result;
    }

    const std::vector<NodeEnrollmentRecord>
        records =
            config_.registry->snapshot();

    result.reserve(records.size());

    for (const NodeEnrollmentRecord& record :
         records) {
        result.push_back(
            redact_node_enrollment_record(
                record
            )
        );
    }

    return result;
}

} // namespace pqnas::cluster
