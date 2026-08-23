#include "cluster/node_deletion_service.h"

#include "cluster/internal_auth.h"
#include "cluster/node_credential_registry.h"
#include "cluster/node_enrollment_registry.h"
#include "cluster/node_enrollment_token.h"
#include "cluster/node_registry.h"

#include <filesystem>
#include <iostream>
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

    pqnas::cluster::NodeDeletionServiceConfig
        config;

    config.node_registry = &node_registry;
    config.enrollment_registry =
        &enrollment_registry;
    config.credential_registry =
        &credential_registry;

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
            result.node_removed,
            "complete node deletion result"
        );

        expect(
            !node_registry.get(
                "node-01"
            ).has_value(),
            "deleted node must leave NodeRegistry"
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
