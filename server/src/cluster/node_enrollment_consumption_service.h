#pragma once

#include "cluster/node_credential_registry.h"
#include "cluster/node_enrollment_registry.h"
#include "cluster/node_registry.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>

namespace pqnas::cluster {

using NodeEnrollmentConsumptionAuditEmit =
    std::function<void(
        const std::string& event,
        const std::string& outcome,
        const std::map<
            std::string,
            std::string
        >& fields
    )>;

struct NodeEnrollmentConsumptionRequest {
    std::string enrollment_id;
    std::uint64_t expected_generation = 0;

    std::string node_id;
    std::string api_base_url;
    int protocol_version = 0;

    // The route owns and clears these request strings after the service call.
    // This service never stores or returns either plaintext secret.
    std::string bootstrap_token;
    std::string node_credential;

    std::string now_iso;
};

enum class NodeEnrollmentConsumptionServiceResult {
    Ok,
    ServiceUnavailable,
    InvalidInput,
    NotFound,
    GenerationMismatch,
    Expired,
    Revoked,
    Consumed,
    NodeMismatch,
    UrlMismatch,
    ProtocolIncompatible,
    BootstrapTokenInvalid,
    ProvisioningStateConflict,
    CredentialConflict,
    CredentialUnavailable,
    NodeConflict,
    PersistFailed,
    RegistryFailed
};

const char*
node_enrollment_consumption_service_result_code(
    NodeEnrollmentConsumptionServiceResult result
) noexcept;

struct NodeEnrollmentConsumptionResult {
    bool ok = false;
    bool recovered = false;

    NodeEnrollmentConsumptionServiceResult result =
        NodeEnrollmentConsumptionServiceResult::
            ServiceUnavailable;

    const char* error_code() const noexcept;
};

struct NodeEnrollmentConsumptionServiceConfig {
    NodeEnrollmentRegistry* enrollment_registry =
        nullptr;

    NodeCredentialRegistry* credential_registry =
        nullptr;

    NodeRegistry* node_registry = nullptr;

    NodeEnrollmentConsumptionAuditEmit audit_emit;
};

class NodeEnrollmentConsumptionService {
public:
    explicit NodeEnrollmentConsumptionService(
        NodeEnrollmentConsumptionServiceConfig config
    );

    // Authenticates the bootstrap intent and executes or recovers the durable
    // sequence: credential -> configured node -> consumed enrollment.
    NodeEnrollmentConsumptionResult consume(
        const NodeEnrollmentConsumptionRequest& request
    );

private:
    bool config_valid() const noexcept;

    NodeEnrollmentConsumptionResult finish(
        const NodeEnrollmentConsumptionRequest& request,
        NodeEnrollmentConsumptionServiceResult result,
        bool recovered
    ) const;

    void audit_best_effort(
        const NodeEnrollmentConsumptionRequest& request,
        const NodeEnrollmentConsumptionResult& result
    ) const noexcept;

    NodeEnrollmentConsumptionServiceConfig config_;
};

} // namespace pqnas::cluster
