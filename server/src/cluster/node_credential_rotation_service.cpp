#include "cluster/node_credential_rotation_service.h"

#include "cluster/cluster_identity_validation.h"
#include "cluster/internal_auth.h"
#include "cluster/node_credential_registry.h"
#include "cluster/node_registry.h"
#include "cluster/node_tls_trust_store.h"

#include <string>
#include <utility>

namespace pqnas::cluster {
namespace {

class SensitiveStringWiper {
public:
    explicit SensitiveStringWiper(
        std::string& value
    ) noexcept
        : value_(value) {}

    ~SensitiveStringWiper() {
        clear_internal_auth_token_text(
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

template <typename Function>
class ScopeExit {
public:
    explicit ScopeExit(
        Function function
    )
        : function_(
              std::move(function)
          ) {}

    ~ScopeExit() noexcept {
        try {
            function_();
        } catch (...) {
            // Security: cleanup is best effort during stack unwinding. The
            // cleanup function itself is noexcept, but never let a future
            // implementation turn a destructor into a termination path.
        }
    }

    ScopeExit(
        const ScopeExit&
    ) = delete;

    ScopeExit& operator=(
        const ScopeExit&
    ) = delete;

private:
    Function function_;
};

template <typename Function>
ScopeExit<Function> make_scope_exit(
    Function function
) {
    return
        ScopeExit<Function>(
            std::move(function)
        );
}

NodeCredentialRotationResult finish(
    NodeCredentialRotationServiceCode code,
    int http_status = 0,
    bool director_committed = false,
    bool storage_finalized = false
) {
    NodeCredentialRotationResult result;

    result.code =
        code;

    result.http_status =
        http_status;

    result.director_credential_committed =
        director_committed;

    result.storage_finalized =
        storage_finalized;

    return result;
}

bool authorization_to_token_in_place(
    std::string* authorization
) {
    if (!authorization) {
        return false;
    }

    constexpr char prefix[] =
        "Bearer ";

    constexpr std::size_t prefix_bytes =
        sizeof(prefix) - 1;

    if (authorization->size() !=
            prefix_bytes +
                kInternalAuthTokenChars ||
        authorization->compare(
            0,
            prefix_bytes,
            prefix
        ) != 0) {
        clear_internal_auth_token_text(
            authorization
        );

        return false;
    }

    // Reuse the same wipeable allocation instead of creating another plaintext
    // credential string.
    authorization->erase(
        0,
        prefix_bytes
    );

    return
        valid_internal_auth_credential_token(
            *authorization
        );
}

bool same_rotation_target(
    const NodeRegistryRecord& expected,
    const NodeRegistryRecord& current
) noexcept {
    // Heartbeat/status fields may legitimately change during network I/O.
    // Only Director configuration relevant to this operation is compared.
    return
        current.node_id ==
            expected.node_id &&
        current.api_base_url ==
            expected.api_base_url &&
        current.enabled ==
            expected.enabled;
}

} // namespace

const char*
node_credential_rotation_service_code(
    NodeCredentialRotationServiceCode code
) noexcept {
    switch (code) {
        case NodeCredentialRotationServiceCode::Ok:
            return "ok";

        case NodeCredentialRotationServiceCode::
                InvalidInput:
            return "invalid_input";

        case NodeCredentialRotationServiceCode::
                ServiceUnavailable:
            return "service_unavailable";

        case NodeCredentialRotationServiceCode::
                NodeNotFound:
            return "node_not_found";

        case NodeCredentialRotationServiceCode::
                NodeDisabled:
            return "node_disabled";

        case NodeCredentialRotationServiceCode::
                RotationInProgress:
            return "rotation_in_progress";

        case NodeCredentialRotationServiceCode::
                NodeChanged:
            return "node_changed";

        case NodeCredentialRotationServiceCode::
                TlsTrustUnavailable:
            return "tls_trust_unavailable";

        case NodeCredentialRotationServiceCode::
                CredentialUnavailable:
            return "credential_unavailable";

        case NodeCredentialRotationServiceCode::
                PrepareFailed:
            return "prepare_failed";

        case NodeCredentialRotationServiceCode::
                CredentialStateConflict:
            return "credential_state_conflict";

        case NodeCredentialRotationServiceCode::
                CredentialCommitFailed:
            return "credential_commit_failed";

        case NodeCredentialRotationServiceCode::
                FinalizePending:
            return "finalize_pending";
    }

    return "service_unavailable";
}

NodeCredentialRotationService::
NodeCredentialRotationService(
    NodeCredentialRotationServiceConfig config
)
    : config_(
          std::move(config)
      ) {}

bool
NodeCredentialRotationService::
config_valid() const noexcept {
    return
        config_.node_registry &&
        config_.credential_registry &&
        config_.tls_trust_store &&
        config_.coordination_mutex &&
        static_cast<bool>(
            config_.prepare_transport
        ) &&
        static_cast<bool>(
            config_.finalize_transport
        );
}

bool
NodeCredentialRotationService::
rotation_in_progress(
    const std::string& node_id
) const {
    std::lock_guard<std::mutex>
        lock(busy_mutex_);

    return
        busy_nodes_.find(
            node_id
        ) !=
        busy_nodes_.end();
}

void
NodeCredentialRotationService::
clear_busy(
    const std::string& node_id
) noexcept {
    try {
        std::lock_guard<std::mutex>
            lock(busy_mutex_);

        busy_nodes_.erase(
            node_id
        );
    } catch (...) {
        // Security: do not throw from cleanup. In the exceptional mutex failure
        // case, remaining busy state fails closed rather than enabling overlap.
    }
}

NodeCredentialRotationResult
NodeCredentialRotationService::rotate(
    const std::string& node_id
) {
    if (!config_valid()) {
        return finish(
            NodeCredentialRotationServiceCode::
                ServiceUnavailable
        );
    }

    if (!valid_cluster_node_id(
            node_id)) {
        return finish(
            NodeCredentialRotationServiceCode::
                InvalidInput
        );
    }

    NodeRegistryRecord expected_node;

    NodeCredentialRotationPrepareRequest
        prepare_request;

    SensitiveStringWiper
        prepare_authorization_wiper(
            prepare_request.authorization
        );

    {
        // Security: establish the per-node busy marker under the same Director
        // coordination mutex used by node lifecycle administration. Network I/O
        // begins only after this scope releases the mutex.
        std::lock_guard<std::mutex>
            coordination_lock(
                *config_.coordination_mutex
            );

        {
            std::lock_guard<std::mutex>
                busy_lock(
                    busy_mutex_
                );

            if (busy_nodes_.find(
                    node_id) !=
                busy_nodes_.end()) {
                return finish(
                    NodeCredentialRotationServiceCode::
                        RotationInProgress
                );
            }
        }

        const auto node =
            config_.node_registry->get(
                node_id
            );

        if (!node.has_value()) {
            return finish(
                NodeCredentialRotationServiceCode::
                    NodeNotFound
            );
        }

        if (!node->enabled) {
            return finish(
                NodeCredentialRotationServiceCode::
                    NodeDisabled
            );
        }

        std::string normalized_url;

        if (!normalize_cluster_https_base_url(
                node->api_base_url,
                &normalized_url,
                nullptr) ||
            normalized_url !=
                node->api_base_url) {
            return finish(
                NodeCredentialRotationServiceCode::
                    InvalidInput
            );
        }

        expected_node =
            *node;

        prepare_request.api_base_url =
            expected_node.api_base_url;

        std::string tls_trust_error;

        const NodeTlsTrustStoreResult
            tls_trust_result =
                config_.tls_trust_store->get(
                    node_id,
                    &prepare_request.tls_spki_sha256_pin,
                    &tls_trust_error
                );

        // Security: never disclose trust-store filesystem details through the
        // credential-rotation API or audit surface.
        tls_trust_error.clear();

        if (tls_trust_result !=
            NodeTlsTrustStoreResult::Ok) {
            return finish(
                NodeCredentialRotationServiceCode::
                    TlsTrustUnavailable
            );
        }

        // Security: TLS trust is resolved before reusable OLD bearer material.
        std::string credential_error;

        const NodeCredentialRegistryResult
            credential_result =
                config_.credential_registry
                    ->make_bearer_authorization(
                        node_id,
                        &prepare_request.authorization,
                        &credential_error
                    );

        credential_error.clear();

        if (credential_result !=
            NodeCredentialRegistryResult::
                Ok) {
            return finish(
                NodeCredentialRotationServiceCode::
                    CredentialUnavailable
            );
        }

        try {
            std::lock_guard<std::mutex>
                busy_lock(
                    busy_mutex_
                );

            busy_nodes_.insert(
                node_id
            );
        } catch (...) {
            return finish(
                NodeCredentialRotationServiceCode::
                    ServiceUnavailable
            );
        }
    }

    auto busy_guard =
        make_scope_exit(
            [this, node_id]() noexcept {
                clear_busy(
                    node_id
                );
            }
        );

    // ---------------------------------------------------------------------
    // Phase 1: establish OLD + NEW overlap on Storage.
    // No Director lifecycle/placement mutex is held over this network call.
    // ---------------------------------------------------------------------

    NodeCredentialRotationPrepareResponse
        prepare_response;

    try {
        prepare_response =
            config_.prepare_transport(
                prepare_request
            );
    } catch (...) {
        return finish(
            NodeCredentialRotationServiceCode::
                PrepareFailed
        );
    }

    SensitiveStringWiper
        pending_credential_wiper(
            prepare_response.pending_credential
        );

    if (prepare_response.code !=
            NodeCredentialRotationTransportCode::
                Ok ||
        !valid_internal_auth_credential_token(
            prepare_response.pending_credential)) {
        return finish(
            NodeCredentialRotationServiceCode::
                PrepareFailed,
            prepare_response.http_status
        );
    }

    // The prepare Authorization is no longer needed as a wire value. Convert
    // it in place to the exact OLD plaintext expected by compare-and-replace.
    if (!authorization_to_token_in_place(
            &prepare_request.authorization)) {
        return finish(
            NodeCredentialRotationServiceCode::
                CredentialUnavailable
        );
    }

    NodeCredentialRotationFinalizeRequest
        finalize_request;

    // Security: bearer rotation never changes node identity. Finalize uses
    // exactly the same immutable TLS pin captured before prepare.
    finalize_request.tls_spki_sha256_pin =
        prepare_request.tls_spki_sha256_pin;

    SensitiveStringWiper
        finalize_authorization_wiper(
            finalize_request.authorization
        );

    bool director_committed = false;

    {
        // -----------------------------------------------------------------
        // Phase 2: atomically confirm target configuration and durably adopt
        // NEW on Director. Still no network I/O inside this lock.
        // -----------------------------------------------------------------
        std::lock_guard<std::mutex>
            coordination_lock(
                *config_.coordination_mutex
            );

        const auto current_node =
            config_.node_registry->get(
                node_id
            );

        if (!current_node.has_value() ||
            !same_rotation_target(
                expected_node,
                *current_node)) {
            return finish(
                NodeCredentialRotationServiceCode::
                    NodeChanged
            );
        }

        std::string credential_error;

        const NodeCredentialRegistryResult
            replace_result =
                config_.credential_registry
                    ->replace_if_matches(
                        node_id,
                        prepare_request.authorization,
                        prepare_response.pending_credential,
                        &credential_error
                    );

        credential_error.clear();

        if (replace_result ==
            NodeCredentialRegistryResult::
                StateMismatch) {
            return finish(
                NodeCredentialRotationServiceCode::
                    CredentialStateConflict
            );
        }

        if (replace_result !=
            NodeCredentialRegistryResult::
                Ok) {
            return finish(
                NodeCredentialRotationServiceCode::
                    CredentialCommitFailed
            );
        }

        director_committed = true;

        // Security: obtain finalize Authorization from the now-authoritative
        // Director registry rather than trusting the transport response copy.
        const NodeCredentialRegistryResult
            authorization_result =
                config_.credential_registry
                    ->make_bearer_authorization(
                        node_id,
                        &finalize_request.authorization,
                        &credential_error
                    );

        credential_error.clear();

        if (authorization_result !=
            NodeCredentialRegistryResult::
                Ok) {
            // NEW is already durable. Never roll back OLD.
            return finish(
                NodeCredentialRotationServiceCode::
                    FinalizePending,
                0,
                true,
                false
            );
        }

        finalize_request.api_base_url =
            expected_node.api_base_url;
    }

    // ---------------------------------------------------------------------
    // Phase 3: retire OLD on Storage, authenticated specifically with NEW.
    // No Director lifecycle/placement mutex is held over this network call.
    // ---------------------------------------------------------------------

    NodeCredentialRotationFinalizeResponse
        finalize_response;

    try {
        finalize_response =
            config_.finalize_transport(
                finalize_request
            );
    } catch (...) {
        return finish(
            NodeCredentialRotationServiceCode::
                FinalizePending,
            0,
            director_committed,
            false
        );
    }

    if (finalize_response.code !=
        NodeCredentialRotationTransportCode::
            Ok) {
        // Director NEW is already authoritative and durable. OLD must never be
        // restored just because the final network acknowledgement is missing.
        return finish(
            NodeCredentialRotationServiceCode::
                FinalizePending,
            finalize_response.http_status,
            true,
            false
        );
    }

    return finish(
        NodeCredentialRotationServiceCode::Ok,
        finalize_response.http_status,
        true,
        true
    );
}

} // namespace pqnas::cluster
