#include "cluster/node_enrollment_service.h"

#include <sodium.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

using pqnas::cluster::
    NodeEnrollmentMutationResult;

using pqnas::cluster::
    NodeEnrollmentServiceResult;

int failures = 0;

struct AuditRecord {
    std::string event;
    std::string outcome;

    std::map<
        std::string,
        std::string
    > fields;
};

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

void expect_result(
    const NodeEnrollmentMutationResult& result,
    NodeEnrollmentServiceResult expected,
    const std::string& message
) {
    expect(
        result.result == expected,
        message +
            ": expected " +
            pqnas::cluster::
                node_enrollment_service_result_code(
                    expected
                ) +
            ", got " +
            result.error_code()
    );
}

fs::path make_test_directory() {
    std::string pattern =
        (
            fs::temp_directory_path() /
            "pqnas-enrollment-revoke-XXXXXX"
        ).string();

    std::vector<char> writable(
        pattern.begin(),
        pattern.end()
    );

    writable.push_back('\0');

    char* result =
        ::mkdtemp(
            writable.data()
        );

    if (!result) {
        return {};
    }

    return fs::path(result);
}

void clear_secret(
    std::string* value
) {
    pqnas::cluster::
        clear_node_enrollment_token_text(
            value
        );
}

} // namespace

int main() {
    if (sodium_init() < 0) {
        std::cerr
            << "FAIL: sodium initialization failed\n";
        return 1;
    }

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

    const fs::path registry_path =
        directory /
        "node_enrollments.json";

    pqnas::cluster::NodeEnrollmentRegistry
        registry;

    std::string error;

    expect(
        registry.load(
            registry_path.string(),
            &error
        ),
        "load revocation registry: " + error
    );

    std::vector<AuditRecord> audits;

    std::vector<std::string> ids{
        std::string(32, 'R'),
        std::string(32, 'S')
    };

    std::size_t id_index = 0;

    pqnas::cluster::
        NodeEnrollmentServiceConfig config;

    config.registry = &registry;

    config.generate_enrollment_id =
        [&](std::string* output) {
            if (!output ||
                id_index >= ids.size()) {
                return false;
            }

            *output = ids[id_index++];
            return true;
        };

    config.audit_emit =
        [&](const std::string& event,
            const std::string& outcome,
            const std::map<
                std::string,
                std::string
            >& fields) {
            audits.push_back(
                AuditRecord{
                    event,
                    outcome,
                    fields
                }
            );
        };

    pqnas::cluster::NodeEnrollmentService
        service(config);

    auto pending =
        service.create_pending(
            "node-revoke",
            "https://node-revoke.internal",
            1,
            "2026-07-30T07:00:00Z",
            "2026-07-30T08:00:00Z",
            "admin-fp"
        );

    expect(
        pending.ok(),
        "create revocation fixture"
    );

    std::string pending_plaintext;

    expect(
        pending.take_bootstrap_token(
            &pending_plaintext
        ),
        "take revocation fixture token"
    );

    clear_secret(&pending_plaintext);

    const std::string pending_id =
        pending.enrollment()
            .enrollment_id;

    const std::uint64_t pending_generation =
        pending.enrollment()
            .generation;

    const auto stale =
        service.revoke_pending(
            pending_id,
            pending_generation + 1,
            "2026-07-30T07:05:00Z",
            "admin-fp"
        );

    expect_result(
        stale,
        NodeEnrollmentServiceResult::
            GenerationMismatch,
        "revocation service stale generation"
    );

    const auto after_stale =
        registry.get(pending_id);

    expect(
        after_stale.has_value() &&
        after_stale->state ==
            pqnas::cluster::
                NodeEnrollmentState::Pending &&
        !after_stale->token_hash.empty(),
        "stale revoke must preserve pending verifier"
    );

    const auto missing =
        service.revoke_pending(
            std::string(32, 'Z'),
            1,
            "2026-07-30T07:05:00Z",
            "admin-fp"
        );

    expect_result(
        missing,
        NodeEnrollmentServiceResult::NotFound,
        "revocation service missing enrollment"
    );

    const auto invalid =
        service.revoke_pending(
            "../invalid",
            0,
            "not-a-time",
            "admin-fp"
        );

    expect_result(
        invalid,
        NodeEnrollmentServiceResult::
            InvalidInput,
        "invalid revocation request"
    );

    const auto revoked =
        service.revoke_pending(
            pending_id,
            pending_generation,
            "2026-07-30T07:05:00Z",
            "admin-fp"
        );

    expect_result(
        revoked,
        NodeEnrollmentServiceResult::Ok,
        "successful service revocation"
    );

    expect(
        revoked.ok &&
        revoked.enrollment.state ==
            "revoked" &&
        revoked.enrollment.revoked_at ==
            "2026-07-30T07:05:00Z",
        "successful revoke returns redacted terminal view"
    );

    const auto persisted_revoked =
        registry.get(pending_id);

    expect(
        persisted_revoked.has_value() &&
        persisted_revoked->state ==
            pqnas::cluster::
                NodeEnrollmentState::Revoked &&
        persisted_revoked->token_hash.empty(),
        "successful revoke must clear persisted verifier"
    );

    const auto repeated =
        service.revoke_pending(
            pending_id,
            pending_generation,
            "2026-07-30T07:06:00Z",
            "admin-fp"
        );

    expect_result(
        repeated,
        NodeEnrollmentServiceResult::Revoked,
        "repeated service revocation"
    );

    auto consume_fixture =
        service.create_pending(
            "node-consumed",
            "https://node-consumed.internal",
            1,
            "2026-07-30T08:00:00Z",
            "2026-07-30T09:00:00Z",
            "admin-fp"
        );

    expect(
        consume_fixture.ok(),
        "create consumed fixture"
    );

    std::string consumed_plaintext;

    expect(
        consume_fixture.take_bootstrap_token(
            &consumed_plaintext
        ),
        "take consumed fixture token"
    );

    std::string consumed_hash;

    expect(
        pqnas::cluster::
            hash_node_enrollment_token(
                consumed_plaintext,
                &consumed_hash
            ) ==
                pqnas::cluster::
                    NodeEnrollmentTokenResult::Ok,
        "hash consumed fixture token"
    );

    clear_secret(&consumed_plaintext);

    pqnas::cluster::NodeEnrollmentRecord
        consumed_record;

    error.clear();

    expect(
        registry.prepare_provisioning_if_matches(
            consume_fixture
                .enrollment()
                .enrollment_id,
            consume_fixture
                .enrollment()
                .node_id,
            consume_fixture
                .enrollment()
                .api_base_url,
            consume_fixture
                .enrollment()
                .required_protocol_version,
            consume_fixture
                .enrollment()
                .generation,
            consumed_hash,
            "2026-07-30T08:05:00Z",
            &consumed_record,
            &error
        ) ==
            pqnas::cluster::
                NodeEnrollmentRegistryResult::Ok,
        "prepare consumed fixture: " + error
    );

    expect(
        registry.advance_provisioning_stage(
            consume_fixture
                .enrollment()
                .enrollment_id,
            consume_fixture
                .enrollment()
                .generation,
            pqnas::cluster::
                NodeEnrollmentProvisioningStage::
                    Prepared,
            pqnas::cluster::
                NodeEnrollmentProvisioningStage::
                    CredentialPersisted,
            &consumed_record,
            &error
        ) ==
            pqnas::cluster::
                NodeEnrollmentRegistryResult::Ok,
        "advance consumed credential fixture: " +
            error
    );

    expect(
        registry.advance_provisioning_stage(
            consume_fixture
                .enrollment()
                .enrollment_id,
            consume_fixture
                .enrollment()
                .generation,
            pqnas::cluster::
                NodeEnrollmentProvisioningStage::
                    CredentialPersisted,
            pqnas::cluster::
                NodeEnrollmentProvisioningStage::
                    TlsTrustBound,
            &consumed_record,
            &error
        ) ==
            pqnas::cluster::
                NodeEnrollmentRegistryResult::Ok,
        "advance consumed TLS fixture: " +
            error
    );

    expect(
        registry.advance_provisioning_stage(
            consume_fixture
                .enrollment()
                .enrollment_id,
            consume_fixture
                .enrollment()
                .generation,
            pqnas::cluster::
                NodeEnrollmentProvisioningStage::
                    TlsTrustBound,
            pqnas::cluster::
                NodeEnrollmentProvisioningStage::
                    NodeRegistered,
            &consumed_record,
            &error
        ) ==
            pqnas::cluster::
                NodeEnrollmentRegistryResult::Ok,
        "advance consumed node fixture: " +
            error
    );

    expect(
        registry.finalize_provisioning(
            consume_fixture
                .enrollment()
                .enrollment_id,
            consume_fixture
                .enrollment()
                .generation,
            "2026-07-30T08:05:00Z",
            &consumed_record,
            &error
        ) ==
            pqnas::cluster::
                NodeEnrollmentRegistryResult::Ok,
        "finalize consumed fixture: " + error
    );

    clear_secret(&consumed_hash);

    const auto consumed_revoke =
        service.revoke_pending(
            consumed_record.enrollment_id,
            consumed_record.generation,
            "2026-07-30T08:06:00Z",
            "admin-fp"
        );

    expect_result(
        consumed_revoke,
        NodeEnrollmentServiceResult::Consumed,
        "consumed enrollment cannot be revoked"
    );

    int revoke_audit_count = 0;

    for (const AuditRecord& audit : audits) {
        if (audit.event !=
            "admin.cluster_node_enrollment.revoke") {
            continue;
        }

        ++revoke_audit_count;

        expect(
            audit.fields.count(
                "reason_code"
            ) == 1,
            "revoke audit reason code"
        );

        expect(
            audit.fields.count(
                "api_base_url"
            ) == 0 &&
            audit.fields.count(
                "bootstrap_token"
            ) == 0 &&
            audit.fields.count(
                "token"
            ) == 0 &&
            audit.fields.count(
                "token_hash"
            ) == 0 &&
            audit.fields.count(
                "error"
            ) == 0 &&
            audit.fields.count(
                "registry_path"
            ) == 0,
            "revoke audit excludes secrets, URL, errors and paths"
        );
    }

    expect(
        revoke_audit_count == 6,
        "every normal revoke operation emits one audit result"
    );

    // Audit exceptions must not undo an already persisted revocation.
    {
        const fs::path throwing_path =
            directory /
            "throwing" /
            "node_enrollments.json";

        pqnas::cluster::NodeEnrollmentRegistry
            throwing_registry;

        error.clear();

        expect(
            throwing_registry.load(
                throwing_path.string(),
                &error
            ),
            "load throwing registry: " + error
        );

        pqnas::cluster::
            NodeEnrollmentServiceConfig
                throwing_config;

        throwing_config.registry =
            &throwing_registry;

        throwing_config.generate_enrollment_id =
            [](std::string* output) {
                if (!output) {
                    return false;
                }

                *output = std::string(32, 'T');
                return true;
            };

        throwing_config.audit_emit =
            [](
                const std::string&,
                const std::string&,
                const std::map<
                    std::string,
                    std::string
                >&) {
                throw 42;
            };

        pqnas::cluster::NodeEnrollmentService
            throwing_service(
                throwing_config
            );

        auto fixture =
            throwing_service.create_pending(
                "node-audit-revoke",
                "https://node-audit-revoke.internal",
                1,
                "2026-07-30T09:00:00Z",
                "2026-07-30T10:00:00Z",
                "admin-fp"
            );

        expect(
            fixture.ok(),
            "create throwing audit fixture"
        );

        std::string plaintext;

        expect(
            fixture.take_bootstrap_token(
                &plaintext
            ),
            "take throwing audit token"
        );

        clear_secret(&plaintext);

        const auto result =
            throwing_service.revoke_pending(
                fixture.enrollment()
                    .enrollment_id,
                fixture.enrollment()
                    .generation,
                "2026-07-30T09:05:00Z",
                "admin-fp"
            );

        expect_result(
            result,
            NodeEnrollmentServiceResult::Ok,
            "audit exception after persisted revocation"
        );

        const auto persisted =
            throwing_registry.get(
                fixture.enrollment()
                    .enrollment_id
            );

        expect(
            persisted.has_value() &&
            persisted->state ==
                pqnas::cluster::
                    NodeEnrollmentState::Revoked &&
            persisted->token_hash.empty(),
            "audit exception must not roll back revocation"
        );
    }

    // A failed atomic write must preserve the pending verifier in memory.
    {
        const fs::path failure_directory =
            directory /
            "persist-failure";

        const fs::path failure_path =
            failure_directory /
            "node_enrollments.json";

        pqnas::cluster::NodeEnrollmentRegistry
            failure_registry;

        error.clear();

        expect(
            failure_registry.load(
                failure_path.string(),
                &error
            ),
            "load failure registry: " + error
        );

        pqnas::cluster::
            NodeEnrollmentServiceConfig
                failure_config;

        failure_config.registry =
            &failure_registry;

        failure_config.generate_enrollment_id =
            [](std::string* output) {
                if (!output) {
                    return false;
                }

                *output = std::string(32, 'P');
                return true;
            };

        failure_config.audit_emit =
            [](const std::string&,
               const std::string&,
               const std::map<
                   std::string,
                   std::string
               >&) {};

        pqnas::cluster::NodeEnrollmentService
            failure_service(
                failure_config
            );

        auto fixture =
            failure_service.create_pending(
                "node-persist-revoke",
                "https://node-persist-revoke.internal",
                1,
                "2026-07-30T10:00:00Z",
                "2026-07-30T11:00:00Z",
                "admin-fp"
            );

        expect(
            fixture.ok(),
            "create revoke persistence fixture"
        );

        std::string plaintext;

        expect(
            fixture.take_bootstrap_token(
                &plaintext
            ),
            "take revoke persistence token"
        );

        clear_secret(&plaintext);

        const auto before =
            failure_registry.get(
                fixture.enrollment()
                    .enrollment_id
            );

        std::error_code filesystem_error;

        fs::remove(
            failure_path,
            filesystem_error
        );

        expect(
            !filesystem_error,
            "remove failure registry file"
        );

        filesystem_error.clear();

        fs::remove(
            failure_directory,
            filesystem_error
        );

        expect(
            !filesystem_error,
            "remove failure registry directory"
        );

        {
            std::ofstream blocker(
                failure_directory,
                std::ios::binary |
                std::ios::trunc
            );

            expect(
                blocker.good(),
                "create persistence blocker"
            );

            blocker << "not a directory\n";
        }

        const auto failed =
            failure_service.revoke_pending(
                fixture.enrollment()
                    .enrollment_id,
                fixture.enrollment()
                    .generation,
                "2026-07-30T10:05:00Z",
                "admin-fp"
            );

        expect_result(
            failed,
            NodeEnrollmentServiceResult::
                PersistFailed,
            "revocation persistence failure"
        );

        const auto after =
            failure_registry.get(
                fixture.enrollment()
                    .enrollment_id
            );

        expect(
            before.has_value() &&
            after.has_value() &&
            after->state ==
                pqnas::cluster::
                    NodeEnrollmentState::Pending &&
            after->token_hash ==
                before->token_hash &&
            !after->token_hash.empty(),
            "failed revoke must preserve pending verifier"
        );
    }

    pqnas::cluster::
        NodeEnrollmentServiceConfig
            unavailable_config;

    unavailable_config.audit_emit =
        [](const std::string&,
           const std::string&,
           const std::map<
               std::string,
               std::string
           >&) {};

    pqnas::cluster::NodeEnrollmentService
        unavailable_service(
            unavailable_config
        );

    const auto unavailable =
        unavailable_service.revoke_pending(
            std::string(32, 'U'),
            1,
            "2026-07-30T12:00:00Z",
            "admin-fp"
        );

    expect_result(
        unavailable,
        NodeEnrollmentServiceResult::
            ServiceUnavailable,
        "missing registry configuration"
    );

    expect(
        std::string(
            pqnas::cluster::
                node_enrollment_service_result_code(
                    NodeEnrollmentServiceResult::
                        GenerationMismatch
                )
        ) ==
            "enrollment_generation_mismatch" &&
        std::string(
            pqnas::cluster::
                node_enrollment_service_result_code(
                    NodeEnrollmentServiceResult::
                        Consumed
                )
        ) ==
            "enrollment_consumed",
        "stable revocation reason codes"
    );

    cleanup();

    if (failures != 0) {
        std::cerr
            << failures
            << " enrollment revocation service test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: node enrollment revocation service tests passed\n";

    return 0;
}
