#include "cluster/node_enrollment_consumption_service.h"

#include "cluster/cluster_identity_validation.h"
#include "cluster/internal_auth.h"
#include "cluster/node_enrollment_token.h"

#include <cctype>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pqnas::cluster {
namespace {

class SensitiveHashWiper {
public:
    explicit SensitiveHashWiper(
        std::string& value
    ) noexcept
        : value_(value) {}

    ~SensitiveHashWiper() {
        clear_node_enrollment_token_text(
            &value_
        );
    }

    SensitiveHashWiper(
        const SensitiveHashWiper&
    ) = delete;

    SensitiveHashWiper& operator=(
        const SensitiveHashWiper&
    ) = delete;

private:
    std::string& value_;
};

bool valid_enrollment_id_text(
    const std::string& value
) noexcept {
    if (value.size() < 16 ||
        value.size() > 128) {
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

bool leap_year(int year) noexcept {
    return
        (year % 4 == 0 &&
         year % 100 != 0) ||
        year % 400 == 0;
}

bool parse_two_digits(
    const std::string& value,
    std::size_t offset,
    int* output
) noexcept {
    if (!output ||
        offset + 2 > value.size() ||
        !std::isdigit(
            static_cast<unsigned char>(
                value[offset])) ||
        !std::isdigit(
            static_cast<unsigned char>(
                value[offset + 1]))) {
        return false;
    }

    *output =
        (value[offset] - '0') * 10 +
        (value[offset + 1] - '0');

    return true;
}

bool parse_four_digits(
    const std::string& value,
    std::size_t offset,
    int* output
) noexcept {
    if (!output ||
        offset + 4 > value.size()) {
        return false;
    }

    int result = 0;

    for (std::size_t index = 0;
         index < 4;
         ++index) {
        const unsigned char ch =
            static_cast<unsigned char>(
                value[offset + index]);

        if (!std::isdigit(ch)) {
            return false;
        }

        result =
            result * 10 +
            static_cast<int>(ch - '0');
    }

    *output = result;
    return true;
}

bool valid_iso_utc_seconds_text(
    const std::string& value
) noexcept {
    if (value.size() != 20 ||
        value[4] != '-' ||
        value[7] != '-' ||
        value[10] != 'T' ||
        value[13] != ':' ||
        value[16] != ':' ||
        value[19] != 'Z') {
        return false;
    }

    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;

    if (!parse_four_digits(value, 0, &year) ||
        !parse_two_digits(value, 5, &month) ||
        !parse_two_digits(value, 8, &day) ||
        !parse_two_digits(value, 11, &hour) ||
        !parse_two_digits(value, 14, &minute) ||
        !parse_two_digits(value, 17, &second) ||
        year < 1970 ||
        month < 1 ||
        month > 12 ||
        hour < 0 ||
        hour > 23 ||
        minute < 0 ||
        minute > 59 ||
        second < 0 ||
        second > 59) {
        return false;
    }

    static constexpr int days_per_month[] = {
        0,
        31,
        28,
        31,
        30,
        31,
        30,
        31,
        31,
        30,
        31,
        30,
        31
    };

    int maximum_day =
        days_per_month[month];

    if (month == 2 &&
        leap_year(year)) {
        maximum_day = 29;
    }

    return
        day >= 1 &&
        day <= maximum_day;
}

NodeEnrollmentConsumptionServiceResult
map_enrollment_result(
    NodeEnrollmentRegistryResult result
) noexcept {
    switch (result) {
        case NodeEnrollmentRegistryResult::Ok:
            return NodeEnrollmentConsumptionServiceResult::Ok;

        case NodeEnrollmentRegistryResult::InvalidInput:
            return NodeEnrollmentConsumptionServiceResult::
                InvalidInput;

        case NodeEnrollmentRegistryResult::NotFound:
            return NodeEnrollmentConsumptionServiceResult::
                NotFound;

        case NodeEnrollmentRegistryResult::
                GenerationMismatch:
            return NodeEnrollmentConsumptionServiceResult::
                GenerationMismatch;

        case NodeEnrollmentRegistryResult::Expired:
            return NodeEnrollmentConsumptionServiceResult::
                Expired;

        case NodeEnrollmentRegistryResult::Revoked:
            return NodeEnrollmentConsumptionServiceResult::
                Revoked;

        case NodeEnrollmentRegistryResult::
                AlreadyConsumed:
            return NodeEnrollmentConsumptionServiceResult::
                Consumed;

        case NodeEnrollmentRegistryResult::NodeMismatch:
            return NodeEnrollmentConsumptionServiceResult::
                NodeMismatch;

        case NodeEnrollmentRegistryResult::UrlMismatch:
            return NodeEnrollmentConsumptionServiceResult::
                UrlMismatch;

        case NodeEnrollmentRegistryResult::
                ProtocolIncompatible:
            return NodeEnrollmentConsumptionServiceResult::
                ProtocolIncompatible;

        case NodeEnrollmentRegistryResult::TokenInvalid:
            return NodeEnrollmentConsumptionServiceResult::
                BootstrapTokenInvalid;

        case NodeEnrollmentRegistryResult::
                ProvisioningActive:
        case NodeEnrollmentRegistryResult::
                ProvisioningStageMismatch:
            return NodeEnrollmentConsumptionServiceResult::
                ProvisioningStateConflict;

        case NodeEnrollmentRegistryResult::
                PersistFailed:
            return NodeEnrollmentConsumptionServiceResult::
                PersistFailed;

        case NodeEnrollmentRegistryResult::
                DuplicateEnrollmentId:
        case NodeEnrollmentRegistryResult::
                DuplicateActiveUrl:
            return NodeEnrollmentConsumptionServiceResult::
                RegistryFailed;
    }

    return NodeEnrollmentConsumptionServiceResult::
        RegistryFailed;
}

NodeEnrollmentConsumptionServiceResult
map_credential_result(
    NodeCredentialRegistryResult result
) noexcept {
    switch (result) {
        case NodeCredentialRegistryResult::Ok:
            return NodeEnrollmentConsumptionServiceResult::Ok;

        case NodeCredentialRegistryResult::InvalidInput:
            return NodeEnrollmentConsumptionServiceResult::
                InvalidInput;

        case NodeCredentialRegistryResult::NotFound:
        case NodeCredentialRegistryResult::AlreadyExists:
        case NodeCredentialRegistryResult::
                CredentialInvalid:
            return NodeEnrollmentConsumptionServiceResult::
                CredentialConflict;

        case NodeCredentialRegistryResult::StorageUnsafe:
        case NodeCredentialRegistryResult::IoFailed:
            return NodeEnrollmentConsumptionServiceResult::
                CredentialUnavailable;

        case NodeCredentialRegistryResult::PersistFailed:
            return NodeEnrollmentConsumptionServiceResult::
                PersistFailed;
    }

    return NodeEnrollmentConsumptionServiceResult::
        RegistryFailed;
}

bool configured_node_matches(
    const NodeRegistry& registry,
    const std::string& node_id,
    const std::string& normalized_url
) {
    const std::optional<NodeRegistryRecord> record =
        registry.get(node_id);

    return
        record.has_value() &&
        record->node_id == node_id &&
        record->api_base_url ==
            normalized_url &&
        record->enabled;
}

bool configured_url_exists(
    const NodeRegistry& registry,
    const std::string& normalized_url
) {
    for (const auto& record :
         registry.snapshot()) {
        if (record.api_base_url ==
            normalized_url) {
            return true;
        }
    }

    return false;
}

} // namespace

const char*
node_enrollment_consumption_service_result_code(
    NodeEnrollmentConsumptionServiceResult result
) noexcept {
    switch (result) {
        case NodeEnrollmentConsumptionServiceResult::Ok:
            return "ok";
        case NodeEnrollmentConsumptionServiceResult::
                ServiceUnavailable:
            return "enrollment_consumption_service_unavailable";
        case NodeEnrollmentConsumptionServiceResult::
                InvalidInput:
            return "enrollment_consumption_invalid";
        case NodeEnrollmentConsumptionServiceResult::
                NotFound:
            return "enrollment_consumption_not_found";
        case NodeEnrollmentConsumptionServiceResult::
                GenerationMismatch:
            return "enrollment_consumption_generation_mismatch";
        case NodeEnrollmentConsumptionServiceResult::
                Expired:
            return "enrollment_consumption_expired";
        case NodeEnrollmentConsumptionServiceResult::
                Revoked:
            return "enrollment_consumption_revoked";
        case NodeEnrollmentConsumptionServiceResult::
                Consumed:
            return "enrollment_consumption_already_consumed";
        case NodeEnrollmentConsumptionServiceResult::
                NodeMismatch:
            return "enrollment_consumption_node_mismatch";
        case NodeEnrollmentConsumptionServiceResult::
                UrlMismatch:
            return "enrollment_consumption_url_mismatch";
        case NodeEnrollmentConsumptionServiceResult::
                ProtocolIncompatible:
            return "enrollment_consumption_protocol_incompatible";
        case NodeEnrollmentConsumptionServiceResult::
                BootstrapTokenInvalid:
            return "enrollment_consumption_token_invalid";
        case NodeEnrollmentConsumptionServiceResult::
                ProvisioningStateConflict:
            return "enrollment_consumption_state_conflict";
        case NodeEnrollmentConsumptionServiceResult::
                CredentialConflict:
            return "enrollment_consumption_credential_conflict";
        case NodeEnrollmentConsumptionServiceResult::
                CredentialUnavailable:
            return "enrollment_consumption_credential_unavailable";
        case NodeEnrollmentConsumptionServiceResult::
                NodeConflict:
            return "enrollment_consumption_node_conflict";
        case NodeEnrollmentConsumptionServiceResult::
                PersistFailed:
            return "enrollment_consumption_persist_failed";
        case NodeEnrollmentConsumptionServiceResult::
                RegistryFailed:
            return "enrollment_consumption_registry_failed";
    }

    return "enrollment_consumption_registry_failed";
}

const char*
NodeEnrollmentConsumptionResult::error_code()
    const noexcept {
    return
        node_enrollment_consumption_service_result_code(
            result
        );
}

NodeEnrollmentConsumptionService::
NodeEnrollmentConsumptionService(
    NodeEnrollmentConsumptionServiceConfig config
)
    : config_(std::move(config)) {}

bool NodeEnrollmentConsumptionService::
config_valid() const noexcept {
    return
        config_.enrollment_registry &&
        config_.credential_registry &&
        config_.node_registry &&
        static_cast<bool>(
            config_.audit_emit
        );
}

NodeEnrollmentConsumptionResult
NodeEnrollmentConsumptionService::finish(
    const NodeEnrollmentConsumptionRequest& request,
    NodeEnrollmentConsumptionServiceResult result,
    bool recovered
) const {
    NodeEnrollmentConsumptionResult output;
    output.ok =
        result ==
        NodeEnrollmentConsumptionServiceResult::Ok;
    output.recovered =
        output.ok && recovered;
    output.result = result;

    audit_best_effort(
        request,
        output
    );

    return output;
}

void NodeEnrollmentConsumptionService::
audit_best_effort(
    const NodeEnrollmentConsumptionRequest& request,
    const NodeEnrollmentConsumptionResult& result
) const noexcept {
    if (!config_.audit_emit) {
        return;
    }

    // Security: never audit either plaintext token, verifier, URL, raw error,
    // request body, Authorization value, or registry filesystem path.
    std::map<std::string, std::string> fields{
        {
            "reason_code",
            result.error_code()
        }
    };

    if (valid_cluster_node_id(
            request.node_id)) {
        fields["node_id"] =
            request.node_id;
    }

    if (result.ok &&
        valid_enrollment_id_text(
            request.enrollment_id)) {
        fields["enrollment_id"] =
            request.enrollment_id;

        fields["generation"] =
            std::to_string(
                request.expected_generation
            );

        fields["recovered"] =
            result.recovered
                ? "true"
                : "false";
    }

    try {
        config_.audit_emit(
            "cluster.node_enrollment.consume",
            result.ok ? "ok" : "error",
            fields
        );
    } catch (...) {
        // Provisioning state must never be rolled back by audit failure.
    }
}

NodeEnrollmentConsumptionResult
NodeEnrollmentConsumptionService::consume(
    const NodeEnrollmentConsumptionRequest& request
) {
    const auto fail =
        [&](NodeEnrollmentConsumptionServiceResult result,
            bool recovered = false) {
            return finish(
                request,
                result,
                recovered
            );
        };

    if (!config_valid()) {
        return fail(
            NodeEnrollmentConsumptionServiceResult::
                ServiceUnavailable
        );
    }

    std::string normalized_url;

    if (!valid_enrollment_id_text(
            request.enrollment_id) ||
        request.expected_generation == 0 ||
        !valid_cluster_node_id(
            request.node_id) ||
        request.protocol_version < 1 ||
        request.protocol_version > 1000 ||
        !valid_internal_auth_credential_token(
            request.node_credential) ||
        !valid_iso_utc_seconds_text(
            request.now_iso) ||
        !normalize_cluster_https_base_url(
            request.api_base_url,
            &normalized_url,
            nullptr)) {
        return fail(
            NodeEnrollmentConsumptionServiceResult::
                InvalidInput
        );
    }

    std::string bootstrap_hash;
    SensitiveHashWiper hash_wiper(
        bootstrap_hash
    );

    if (hash_node_enrollment_token(
            request.bootstrap_token,
            &bootstrap_hash) !=
        NodeEnrollmentTokenResult::Ok) {
        return fail(
            NodeEnrollmentConsumptionServiceResult::
                BootstrapTokenInvalid
        );
    }

    NodeEnrollmentProvisioningStage stage =
        NodeEnrollmentProvisioningStage::None;

    std::string internal_error;

    NodeEnrollmentRegistryResult
        enrollment_result =
            config_.enrollment_registry
                ->authenticate_active_provisioning_if_matches(
                    request.enrollment_id,
                    request.node_id,
                    normalized_url,
                    request.protocol_version,
                    request.expected_generation,
                    bootstrap_hash,
                    &stage,
                    &internal_error
                );

    bool recovered =
        enrollment_result ==
        NodeEnrollmentRegistryResult::Ok;

    const bool finalized_retry =
        enrollment_result ==
        NodeEnrollmentRegistryResult::
            AlreadyConsumed;

    if (enrollment_result ==
        NodeEnrollmentRegistryResult::
            ProvisioningStageMismatch) {
        // Security: reject pre-existing external state before owning a new
        // transaction. Exact matches appearing after prepare are treated only
        // as idempotent crash/concurrency recovery.
        bool credential_present = false;
        internal_error.clear();

        const NodeCredentialRegistryResult
            contains_result =
                config_.credential_registry
                    ->contains(
                        request.node_id,
                        &credential_present,
                        &internal_error
                    );

        if (contains_result !=
            NodeCredentialRegistryResult::Ok) {
            return fail(
                map_credential_result(
                    contains_result
                )
            );
        }

        if (credential_present) {
            return fail(
                NodeEnrollmentConsumptionServiceResult::
                    CredentialConflict
            );
        }

        if (config_.node_registry->get(
                request.node_id).has_value() ||
            configured_url_exists(
                *config_.node_registry,
                normalized_url)) {
            return fail(
                NodeEnrollmentConsumptionServiceResult::
                    NodeConflict
            );
        }

        NodeEnrollmentRecord prepared;
        internal_error.clear();

        enrollment_result =
            config_.enrollment_registry
                ->prepare_provisioning_if_matches(
                    request.enrollment_id,
                    request.node_id,
                    normalized_url,
                    request.protocol_version,
                    request.expected_generation,
                    bootstrap_hash,
                    request.now_iso,
                    &prepared,
                    &internal_error
                );

        if (enrollment_result !=
            NodeEnrollmentRegistryResult::Ok) {
            return fail(
                map_enrollment_result(
                    enrollment_result
                )
            );
        }

        stage =
            NodeEnrollmentProvisioningStage::
                Prepared;
    } else if (enrollment_result !=
                   NodeEnrollmentRegistryResult::Ok &&
               !finalized_retry) {
        return fail(
            map_enrollment_result(
                enrollment_result
            )
        );
    }

    const auto verify_credential =
        [&]() -> NodeEnrollmentConsumptionServiceResult {
            bool matched = false;
            internal_error.clear();

            const NodeCredentialRegistryResult
                result =
                    config_.credential_registry
                        ->matches(
                            request.node_id,
                            request.node_credential,
                            &matched,
                            &internal_error
                        );

            if (result !=
                NodeCredentialRegistryResult::Ok) {
                return map_credential_result(
                    result
                );
            }

            return matched
                ? NodeEnrollmentConsumptionServiceResult::Ok
                : NodeEnrollmentConsumptionServiceResult::
                    CredentialConflict;
        };

    const auto verify_final_state =
        [&]() -> NodeEnrollmentConsumptionServiceResult {
            const auto credential_result =
                verify_credential();

            if (credential_result !=
                NodeEnrollmentConsumptionServiceResult::Ok) {
                return credential_result;
            }

            return configured_node_matches(
                *config_.node_registry,
                request.node_id,
                normalized_url)
                ? NodeEnrollmentConsumptionServiceResult::Ok
                : NodeEnrollmentConsumptionServiceResult::
                    NodeConflict;
        };

    if (finalized_retry) {
        const auto consumed_record =
            config_.enrollment_registry->get(
                request.enrollment_id
            );

        // Security: the bootstrap verifier is deliberately erased at
        // finalization. Recover only an exact consumed transaction and require
        // possession of the persisted node credential. Metadata or credential
        // mismatches remain indistinguishable from any other terminal intent.
        if (!consumed_record.has_value() ||
            consumed_record->state !=
                NodeEnrollmentState::Consumed ||
            consumed_record->generation !=
                request.expected_generation ||
            consumed_record->node_id !=
                request.node_id ||
            consumed_record->api_base_url !=
                normalized_url ||
            request.protocol_version !=
                consumed_record
                    ->required_protocol_version) {
            return fail(
                NodeEnrollmentConsumptionServiceResult::
                    Consumed
            );
        }

        const auto credential_result =
            verify_credential();

        if (credential_result ==
            NodeEnrollmentConsumptionServiceResult::
                CredentialConflict) {
            return fail(
                NodeEnrollmentConsumptionServiceResult::
                    Consumed
            );
        }

        if (credential_result !=
            NodeEnrollmentConsumptionServiceResult::Ok) {
            return fail(
                credential_result
            );
        }

        if (!configured_node_matches(
                *config_.node_registry,
                request.node_id,
                normalized_url)) {
            return fail(
                NodeEnrollmentConsumptionServiceResult::
                    NodeConflict,
                true
            );
        }

        return fail(
            NodeEnrollmentConsumptionServiceResult::Ok,
            true
        );
    }

    if (stage ==
        NodeEnrollmentProvisioningStage::Prepared) {
        internal_error.clear();

        const NodeCredentialRegistryResult
            credential_result =
                config_.credential_registry
                    ->create_if_absent(
                        request.node_id,
                        request.node_credential,
                        &internal_error
                    );

        if (credential_result ==
            NodeCredentialRegistryResult::
                AlreadyExists) {
            const auto match_result =
                verify_credential();

            if (match_result !=
                NodeEnrollmentConsumptionServiceResult::Ok) {
                return fail(
                    match_result,
                    true
                );
            }

            recovered = true;
        } else if (credential_result !=
                   NodeCredentialRegistryResult::Ok) {
            return fail(
                map_credential_result(
                    credential_result
                ),
                recovered
            );
        }

        NodeEnrollmentRecord advanced;
        internal_error.clear();

        enrollment_result =
            config_.enrollment_registry
                ->advance_provisioning_stage(
                    request.enrollment_id,
                    request.expected_generation,
                    NodeEnrollmentProvisioningStage::
                        Prepared,
                    NodeEnrollmentProvisioningStage::
                        CredentialPersisted,
                    &advanced,
                    &internal_error
                );

        if (enrollment_result ==
            NodeEnrollmentRegistryResult::Ok) {
            stage =
                NodeEnrollmentProvisioningStage::
                    CredentialPersisted;
        } else if (enrollment_result ==
                  NodeEnrollmentRegistryResult::
                      ProvisioningStageMismatch) {
            internal_error.clear();

            enrollment_result =
                config_.enrollment_registry
                    ->authenticate_active_provisioning_if_matches(
                        request.enrollment_id,
                        request.node_id,
                        normalized_url,
                        request.protocol_version,
                        request.expected_generation,
                        bootstrap_hash,
                        &stage,
                        &internal_error
                    );

            if (enrollment_result !=
                NodeEnrollmentRegistryResult::Ok) {
                return fail(
                    map_enrollment_result(
                        enrollment_result
                    ),
                    true
                );
            }

            recovered = true;
        } else if (enrollment_result ==
                   NodeEnrollmentRegistryResult::
                       AlreadyConsumed) {
            const auto final_state =
                verify_final_state();

            return fail(
                final_state ==
                    NodeEnrollmentConsumptionServiceResult::Ok
                    ? NodeEnrollmentConsumptionServiceResult::Ok
                    : final_state,
                true
            );
        } else {
            return fail(
                map_enrollment_result(
                    enrollment_result
                ),
                recovered
            );
        }
    }

    if (stage ==
        NodeEnrollmentProvisioningStage::
            CredentialPersisted) {
        const auto credential_result =
            verify_credential();

        if (credential_result !=
            NodeEnrollmentConsumptionServiceResult::Ok) {
            return fail(
                credential_result,
                recovered
            );
        }

        internal_error.clear();

        const NodeRegistryInsertResult
            node_result =
                config_.node_registry
                    ->insert_configured_node_if_absent(
                        request.node_id,
                        normalized_url,
                        true,
                        request.now_iso,
                        &internal_error
                    );

        if (node_result ==
            NodeRegistryInsertResult::
                DuplicateNodeId) {
            if (!configured_node_matches(
                    *config_.node_registry,
                    request.node_id,
                    normalized_url)) {
                return fail(
                    NodeEnrollmentConsumptionServiceResult::
                        NodeConflict,
                    true
                );
            }

            recovered = true;
        } else if (node_result ==
                   NodeRegistryInsertResult::
                       DuplicateApiBaseUrl) {
            return fail(
                NodeEnrollmentConsumptionServiceResult::
                    NodeConflict,
                recovered
            );
        } else if (node_result ==
                   NodeRegistryInsertResult::
                       PersistFailed) {
            return fail(
                NodeEnrollmentConsumptionServiceResult::
                    PersistFailed,
                recovered
            );
        } else if (node_result !=
                   NodeRegistryInsertResult::Ok) {
            return fail(
                NodeEnrollmentConsumptionServiceResult::
                    InvalidInput,
                recovered
            );
        }

        NodeEnrollmentRecord advanced;
        internal_error.clear();

        enrollment_result =
            config_.enrollment_registry
                ->advance_provisioning_stage(
                    request.enrollment_id,
                    request.expected_generation,
                    NodeEnrollmentProvisioningStage::
                        CredentialPersisted,
                    NodeEnrollmentProvisioningStage::
                        NodeRegistered,
                    &advanced,
                    &internal_error
                );

        if (enrollment_result ==
            NodeEnrollmentRegistryResult::Ok) {
            stage =
                NodeEnrollmentProvisioningStage::
                    NodeRegistered;
        } else if (enrollment_result ==
                  NodeEnrollmentRegistryResult::
                      ProvisioningStageMismatch) {
            internal_error.clear();

            enrollment_result =
                config_.enrollment_registry
                    ->authenticate_active_provisioning_if_matches(
                        request.enrollment_id,
                        request.node_id,
                        normalized_url,
                        request.protocol_version,
                        request.expected_generation,
                        bootstrap_hash,
                        &stage,
                        &internal_error
                    );

            if (enrollment_result !=
                NodeEnrollmentRegistryResult::Ok) {
                return fail(
                    map_enrollment_result(
                        enrollment_result
                    ),
                    true
                );
            }

            recovered = true;
        } else if (enrollment_result ==
                   NodeEnrollmentRegistryResult::
                       AlreadyConsumed) {
            const auto final_state =
                verify_final_state();

            return fail(
                final_state ==
                    NodeEnrollmentConsumptionServiceResult::Ok
                    ? NodeEnrollmentConsumptionServiceResult::Ok
                    : final_state,
                true
            );
        } else {
            return fail(
                map_enrollment_result(
                    enrollment_result
                ),
                recovered
            );
        }
    }

    if (stage !=
        NodeEnrollmentProvisioningStage::
            NodeRegistered) {
        return fail(
            NodeEnrollmentConsumptionServiceResult::
                ProvisioningStateConflict,
            recovered
        );
    }

    const auto final_state =
        verify_final_state();

    if (final_state !=
        NodeEnrollmentConsumptionServiceResult::Ok) {
        return fail(
            final_state,
            recovered
        );
    }

    NodeEnrollmentRecord consumed;
    internal_error.clear();

    enrollment_result =
        config_.enrollment_registry
            ->finalize_provisioning(
                request.enrollment_id,
                request.expected_generation,
                request.now_iso,
                &consumed,
                &internal_error
            );

    if (enrollment_result ==
        NodeEnrollmentRegistryResult::Ok ||
        enrollment_result ==
        NodeEnrollmentRegistryResult::
            AlreadyConsumed) {
        return fail(
            NodeEnrollmentConsumptionServiceResult::Ok,
            recovered ||
            enrollment_result ==
                NodeEnrollmentRegistryResult::
                    AlreadyConsumed
        );
    }

    return fail(
        map_enrollment_result(
            enrollment_result
        ),
        recovered
    );
}

} // namespace pqnas::cluster
