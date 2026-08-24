#include "cluster/node_deletion_service.h"

#include "cluster/internal_auth.h"
#include "cluster/node_credential_registry.h"
#include "cluster/node_enrollment_registry.h"
#include "cluster/node_enrollment_token.h"
#include "cluster/node_registry.h"
#include "cluster/node_tls_trust_store.h"
#include "cluster/storage_user_projection_sync_state_store.h"

#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

namespace fs = std::filesystem;

int failures = 0;

void expect(
    bool condition,
    const std::string& message
) {
    if (!condition) {
        ++failures;

        std::cerr
            << "FAIL: "
            << message
            << '\n';
    }
}

fs::path make_test_directory() {
    std::string pattern =
        (
            fs::temp_directory_path() /
            "pqnas-node-deletion-XXXXXX"
        ).string();

    std::vector<char> writable(
        pattern.begin(),
        pattern.end()
    );

    writable.push_back('\0');

    char* result =
        ::mkdtemp(writable.data());

    return
        result
            ? fs::path(result)
            : fs::path{};
}

bool create_node(
    pqnas::cluster::NodeRegistry& registry,
    const std::string& node_id,
    const std::string& url,
    std::string* error
) {
    return
        registry.insert_configured_node_if_absent(
            node_id,
            url,
            true,
            "2026-08-23T05:00:00Z",
            error
        ) ==
        pqnas::cluster::
            NodeRegistryInsertResult::Ok;
}

struct PendingFixture {
    pqnas::cluster::NodeEnrollmentRecord record;
    std::string token_hash;
};

PendingFixture create_pending(
    pqnas::cluster::NodeEnrollmentRegistry& registry,
    const std::string& enrollment_id,
    const std::string& node_id,
    const std::string& url,
    char,
    std::string* error
) {
    PendingFixture fixture;

    // Canonical 32-byte base64url test vector used by the token regression
    // tests. Arbitrary repeated characters are not necessarily canonical.
    std::string token =
        "AAECAwQFBgcICQoLDA0ODxAREhMUFRYX"
        "GBkaGxwdHh8";

    expect(
        pqnas::cluster::hash_node_enrollment_token(
            token,
            &fixture.token_hash
        ) ==
            pqnas::cluster::
                NodeEnrollmentTokenResult::Ok,
        "hash pending enrollment token"
    );

    expect(
        registry.create_or_replace_pending(
            enrollment_id,
            node_id,
            url,
            1,
            fixture.token_hash,
            "2026-08-23T05:00:00Z",
            "2026-08-23T06:00:00Z",
            &fixture.record,
            error
        ) ==
            pqnas::cluster::
                NodeEnrollmentRegistryResult::Ok,
        "create pending enrollment: " +
            (error ? *error : std::string())
    );

    pqnas::cluster::
        clear_node_enrollment_token_text(
            &token
        );

    return fixture;
}

} // namespace

int main() {
    const fs::path directory =
        make_test_directory();

    if (directory.empty()) {
        std::cerr
            << "FAIL: test directory creation failed\n";
        return 1;
    }

    const auto cleanup = [&]() {
        std::error_code error;
        fs::remove_all(directory, error);
    };

    pqnas::cluster::NodeRegistry node_registry;

    pqnas::cluster::NodeEnrollmentRegistry
        enrollment_registry;

    pqnas::cluster::NodeCredentialRegistry
        credential_registry;

    std::string error;

    expect(
        node_registry.load(
            (
                directory /
                "node_registry.json"
            ).string(),
            &error
        ),
        "load node registry: " + error
    );

    error.clear();

    expect(
        enrollment_registry.load(
            (
                directory /
                "node_enrollments.json"
            ).string(),
            &error
        ),
        "load enrollment registry: " + error
    );

    error.clear();

    expect(
        credential_registry.load(
            (
                directory /
                "node_credentials"
            ).string(),
            &error
        ),
        "load credential registry: " + error
    );

    pqnas::cluster::NodeTlsTrustStore
        tls_trust_store;

    error.clear();

    expect(
        tls_trust_store.load(
            (
                directory /
                "node_tls_trust"
            ).string(),
            &error
        ),
        "load TLS trust store: " + error
    );

    pqnas::cluster::
        StorageUserProjectionSyncStateStore
            projection_sync_state_store;

    error.clear();

    expect(
        projection_sync_state_store.load(
            (
                directory /
                "storage_projection_sync_state.json"
            ).string(),
            &error
        ),
        "load projection sync-state store: " +
            error
    );

    auto projection_transition_mutex =
        std::make_shared<std::mutex>();

    pqnas::cluster::NodeDeletionServiceConfig
        config;

    config.node_registry = &node_registry;
    config.enrollment_registry =
        &enrollment_registry;
    config.credential_registry =
        &credential_registry;
    config.tls_trust_store =
        &tls_trust_store;
    config.projection_sync_state_store =
        &projection_sync_state_store;
    config.projection_transition_mutex =
        projection_transition_mutex;

    pqnas::cluster::NodeDeletionService
        service(config);

    // Complete deletion: pending bootstrap verifier -> bearer -> NodeRegistry.
    {
        error.clear();

        expect(
            create_node(
                node_registry,
                "node-01",
                "https://node-01.internal",
                &error
            ),
            "create deletion node: " + error
        );

        error.clear();

        expect(
            tls_trust_store.bind_if_absent(
                "node-01",
                "sha256/lAhFeu/Qcc7BJ8H5hTmTCGGtG6lMlA25dclywJ/Gi2g=",
                &error
            ) ==
                pqnas::cluster::
                    NodeTlsTrustStoreResult::Ok,
            "create deletion TLS trust: " +
                error
        );

        const std::string credential(
            pqnas::cluster::
                kInternalAuthTokenChars,
            'A'
        );

        error.clear();

        expect(
            credential_registry.create_if_absent(
                "node-01",
                credential,
                &error
            ) ==
                pqnas::cluster::
                    NodeCredentialRegistryResult::Ok,
            "create deletion credential: " +
                error
        );

        error.clear();

        const PendingFixture pending =
            create_pending(
                enrollment_registry,
                std::string(32, 'A'),
                "node-01",
                "https://node-01.internal",
                'C',
                &error
            );

        const auto result =
            service.erase_unassigned_node_locked(
                "node-01",
                "2026-08-23T05:10:00Z"
            );

        expect(
            result.ok() &&
            result.revoked_pending_enrollments == 1 &&
            result.credential_removed &&
            result.tls_trust_removed &&
            result.node_removed,
            "complete node deletion result"
        );

        expect(
            !node_registry.get(
                "node-01"
            ).has_value(),
            "deleted node must leave NodeRegistry"
        );

        std::string persisted_tls_pin;
        error.clear();

        expect(
            tls_trust_store.get(
                "node-01",
                &persisted_tls_pin,
                &error
            ) ==
                pqnas::cluster::
                    NodeTlsTrustStoreResult::NotFound,
            "deleted node TLS trust must be absent"
        );

        bool credential_present = true;
        error.clear();

        expect(
            credential_registry.contains(
                "node-01",
                &credential_present,
                &error
            ) ==
                pqnas::cluster::
                    NodeCredentialRegistryResult::Ok &&
            !credential_present,
            "deleted node credential must be absent"
        );

        const auto revoked =
            enrollment_registry.get(
                pending.record.enrollment_id
            );

        expect(
            revoked.has_value() &&
            revoked->state ==
                pqnas::cluster::
                    NodeEnrollmentState::Revoked &&
            revoked->token_hash.empty(),
            "deleted node pending enrollment must be durably revoked"
        );
    }

    // RT010 security regression: durable confirmed Storage authorization
    // blocks Node deletion before bootstrap, bearer, TLS or registry mutation.
    {
        error.clear();

        expect(
            create_node(
                node_registry,
                "node-confirmed",
                "https://node-confirmed.internal",
                &error
            ),
            "create confirmed-state node: " +
                error
        );

        error.clear();

        expect(
            tls_trust_store.bind_if_absent(
                "node-confirmed",
                "sha256/lAhFeu/Qcc7BJ8H5hTmTCGGtG6lMlA25dclywJ/Gi2g=",
                &error
            ) ==
                pqnas::cluster::
                    NodeTlsTrustStoreResult::Ok,
            "create confirmed-state TLS trust: " +
                error
        );

        const std::string credential(
            pqnas::cluster::
                kInternalAuthTokenChars,
            'A'
        );

        error.clear();

        expect(
            credential_registry.create_if_absent(
                "node-confirmed",
                credential,
                &error
            ) ==
                pqnas::cluster::
                    NodeCredentialRegistryResult::Ok,
            "create confirmed-state credential: " +
                error
        );

        error.clear();

        const PendingFixture pending =
            create_pending(
                enrollment_registry,
                std::string(32, 'I'),
                "node-confirmed",
                "https://node-confirmed.internal",
                'J',
                &error
            );

        pqnas::cluster::
            StorageUserProjectionSyncState
                confirmed_state;

        confirmed_state.fingerprint =
            std::string(128, 'a');

        confirmed_state.home_node =
            "node-confirmed";

        confirmed_state.revision = 42;

        error.clear();

        expect(
            projection_sync_state_store.mark_synced(
                confirmed_state,
                &error
            ) ==
                pqnas::cluster::
                    StorageUserProjectionSyncStateStoreResult::
                        Applied,
            "persist confirmed projection state: " +
                error
        );

        const auto result =
            service.erase_unassigned_node_locked(
                "node-confirmed",
                "2026-08-23T05:11:00Z"
            );

        expect(
            result.result ==
                pqnas::cluster::
                    NodeDeletionServiceResult::
                        ConfirmedProjectionActive &&
            result.revoked_pending_enrollments == 0 &&
            !result.credential_removed &&
            !result.tls_trust_removed &&
            !result.node_removed,
            "confirmed projection must block deletion before mutation"
        );

        expect(
            node_registry.get(
                "node-confirmed"
            ).has_value(),
            "confirmed projection must preserve NodeRegistry"
        );

        bool credential_present = false;
        error.clear();

        expect(
            credential_registry.contains(
                "node-confirmed",
                &credential_present,
                &error
            ) ==
                pqnas::cluster::
                    NodeCredentialRegistryResult::Ok &&
            credential_present,
            "confirmed projection must preserve credential"
        );

        std::string persisted_tls_pin;
        error.clear();

        expect(
            tls_trust_store.get(
                "node-confirmed",
                &persisted_tls_pin,
                &error
            ) ==
                pqnas::cluster::
                    NodeTlsTrustStoreResult::Ok,
            "confirmed projection must preserve TLS trust"
        );

        const auto still_pending =
            enrollment_registry.get(
                pending.record.enrollment_id
            );

        expect(
            still_pending.has_value() &&
            still_pending->state ==
                pqnas::cluster::
                    NodeEnrollmentState::Pending &&
            !still_pending->token_hash.empty(),
            "confirmed projection must preserve pending enrollment"
        );
    }

    // RT010 fail-closed regression: if durable confirmed-state cannot be
    // established, deletion must not touch any Node lifecycle state.
    {
        error.clear();

        expect(
            create_node(
                node_registry,
                "node-state-fail",
                "https://node-state-fail.internal",
                &error
            ),
            "create state-check-failure node: " +
                error
        );

        error.clear();

        expect(
            tls_trust_store.bind_if_absent(
                "node-state-fail",
                "sha256/lAhFeu/Qcc7BJ8H5hTmTCGGtG6lMlA25dclywJ/Gi2g=",
                &error
            ) ==
                pqnas::cluster::
                    NodeTlsTrustStoreResult::Ok,
            "create state-check-failure TLS trust: " +
                error
        );

        const std::string credential(
            pqnas::cluster::
                kInternalAuthTokenChars,
            'A'
        );

        error.clear();

        expect(
            credential_registry.create_if_absent(
                "node-state-fail",
                credential,
                &error
            ) ==
                pqnas::cluster::
                    NodeCredentialRegistryResult::Ok,
            "create state-check-failure credential: " +
                error
        );

        error.clear();

        const PendingFixture pending =
            create_pending(
                enrollment_registry,
                std::string(32, 'K'),
                "node-state-fail",
                "https://node-state-fail.internal",
                'L',
                &error
            );

        pqnas::cluster::
            StorageUserProjectionSyncStateStore
                unloaded_projection_state_store;

        auto unavailable_config =
            config;

        unavailable_config.projection_sync_state_store =
            &unloaded_projection_state_store;

        pqnas::cluster::NodeDeletionService
            unavailable_service(
                unavailable_config
            );

        const auto result =
            unavailable_service
                .erase_unassigned_node_locked(
                    "node-state-fail",
                    "2026-08-23T05:12:00Z"
                );

        expect(
            result.result ==
                pqnas::cluster::
                    NodeDeletionServiceResult::
                        ProjectionStateCheckFailed &&
            result.revoked_pending_enrollments == 0 &&
            !result.credential_removed &&
            !result.tls_trust_removed &&
            !result.node_removed,
            "projection durability uncertainty must fail closed"
        );

        expect(
            node_registry.get(
                "node-state-fail"
            ).has_value(),
            "state-check failure must preserve NodeRegistry"
        );

        bool credential_present = false;
        error.clear();

        expect(
            credential_registry.contains(
                "node-state-fail",
                &credential_present,
                &error
            ) ==
                pqnas::cluster::
                    NodeCredentialRegistryResult::Ok &&
            credential_present,
            "state-check failure must preserve credential"
        );

        std::string persisted_tls_pin;
        error.clear();

        expect(
            tls_trust_store.get(
                "node-state-fail",
                &persisted_tls_pin,
                &error
            ) ==
                pqnas::cluster::
                    NodeTlsTrustStoreResult::Ok,
            "state-check failure must preserve TLS trust"
        );

        const auto still_pending =
            enrollment_registry.get(
                pending.record.enrollment_id
            );

        expect(
            still_pending.has_value() &&
            still_pending->state ==
                pqnas::cluster::
                    NodeEnrollmentState::Pending &&
            !still_pending->token_hash.empty(),
            "state-check failure must preserve pending enrollment"
        );
    }

    // Active provisioning is a hard conflict and must be detected before any
    // credential or node state is removed.
    {
        error.clear();

        expect(
            create_node(
                node_registry,
                "node-active",
                "https://node-active.internal",
                &error
            ),
            "create active node: " + error
        );

        const std::string credential(
            pqnas::cluster::
                kInternalAuthTokenChars,
            'A'
        );

        error.clear();

        expect(
            credential_registry.create_if_absent(
                "node-active",
                credential,
                &error
            ) ==
                pqnas::cluster::
                    NodeCredentialRegistryResult::Ok,
            "create active credential: " +
                error
        );

        error.clear();

        const PendingFixture pending =
            create_pending(
                enrollment_registry,
                std::string(32, 'E'),
                "node-active",
                "https://node-active.internal",
                'F',
                &error
            );

        pqnas::cluster::NodeEnrollmentRecord
            prepared;

        error.clear();

        expect(
            enrollment_registry
                .prepare_provisioning_if_matches(
                    pending.record.enrollment_id,
                    pending.record.node_id,
                    pending.record.api_base_url,
                    pending.record
                        .required_protocol_version,
                    pending.record.generation,
                    pending.token_hash,
                    "2026-08-23T05:05:00Z",
                    &prepared,
                    &error
                ) ==
                pqnas::cluster::
                    NodeEnrollmentRegistryResult::Ok,
            "prepare active provisioning: " +
                error
        );

        const auto result =
            service.erase_unassigned_node_locked(
                "node-active",
                "2026-08-23T05:10:00Z"
            );

        expect(
            result.result ==
                pqnas::cluster::
                    NodeDeletionServiceResult::
                        ProvisioningActive &&
            result.revoked_pending_enrollments == 0 &&
            !result.credential_removed &&
            !result.node_removed,
            "active provisioning must block deletion before mutation"
        );

        expect(
            node_registry.get(
                "node-active"
            ).has_value(),
            "active provisioning must preserve NodeRegistry"
        );

        bool credential_present = false;
        error.clear();

        expect(
            credential_registry.contains(
                "node-active",
                &credential_present,
                &error
            ) ==
                pqnas::cluster::
                    NodeCredentialRegistryResult::Ok &&
            credential_present,
            "active provisioning must preserve credential"
        );

        const auto still_active =
            enrollment_registry.get(
                pending.record.enrollment_id
            );

        expect(
            still_active.has_value() &&
            still_active->state ==
                pqnas::cluster::
                    NodeEnrollmentState::Pending &&
            still_active->provisioning_stage ==
                pqnas::cluster::
                    NodeEnrollmentProvisioningStage::
                        Prepared &&
            !still_active->token_hash.empty(),
            "active provisioning must preserve bootstrap transaction"
        );
    }

    // Crash/retry invariant: a missing credential is accepted because an
    // earlier delete attempt may already have durably removed it before the
    // final NodeRegistry commit.
    {
        error.clear();

        expect(
            create_node(
                node_registry,
                "node-retry",
                "https://node-retry.internal",
                &error
            ),
            "create retry node: " + error
        );

        error.clear();

        const PendingFixture pending =
            create_pending(
                enrollment_registry,
                std::string(32, 'G'),
                "node-retry",
                "https://node-retry.internal",
                'H',
                &error
            );

        const auto result =
            service.erase_unassigned_node_locked(
                "node-retry",
                "2026-08-23T05:15:00Z"
            );

        expect(
            result.ok() &&
            result.revoked_pending_enrollments == 1 &&
            !result.credential_removed &&
            result.node_removed,
            "missing credential must be accepted as deletion retry state"
        );

        const auto revoked =
            enrollment_registry.get(
                pending.record.enrollment_id
            );

        expect(
            revoked.has_value() &&
            revoked->state ==
                pqnas::cluster::
                    NodeEnrollmentState::Revoked &&
            revoked->token_hash.empty(),
            "retry deletion must still revoke pending enrollment"
        );
    }

    {
        const auto result =
            service.erase_unassigned_node_locked(
                "node-missing",
                "2026-08-23T05:20:00Z"
            );

        expect(
            result.result ==
                pqnas::cluster::
                    NodeDeletionServiceResult::
                        NotFound,
            "missing node result"
        );
    }

    cleanup();

    if (failures != 0) {
        std::cerr
            << "FAIL: "
            << failures
            << " node deletion service test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: node deletion service tests passed\n";

    return 0;
}
