#include "cluster/node_enrollment_consumption_service.h"

#include "cluster/internal_auth.h"
#include "cluster/node_enrollment_token.h"

#include <sodium.h>

#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

using pqnas::cluster::
    NodeEnrollmentConsumptionRequest;
using pqnas::cluster::
    NodeEnrollmentConsumptionResult;
using pqnas::cluster::
    NodeEnrollmentConsumptionService;
using pqnas::cluster::
    NodeEnrollmentConsumptionServiceConfig;
using pqnas::cluster::
    NodeEnrollmentConsumptionServiceResult;
using pqnas::cluster::
    NodeEnrollmentProvisioningStage;
using pqnas::cluster::
    NodeEnrollmentRecord;
using pqnas::cluster::
    NodeEnrollmentRegistry;
using pqnas::cluster::
    NodeEnrollmentRegistryResult;
using pqnas::cluster::
    NodeCredentialRegistry;
using pqnas::cluster::
    NodeCredentialRegistryResult;
using pqnas::cluster::
    NodeRegistry;
using pqnas::cluster::
    NodeRegistryInsertResult;

int failures = 0;

struct AuditRecord {
    std::string event;
    std::string outcome;
    std::map<std::string, std::string> fields;
};

struct Fixture {
    std::string enrollment_id;
    std::string node_id;
    std::string api_base_url;
    std::uint64_t generation = 0;
    std::string bootstrap_token;
    std::string node_credential;
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
    const NodeEnrollmentConsumptionResult& actual,
    NodeEnrollmentConsumptionServiceResult expected,
    const std::string& message
) {
    expect(
        actual.result == expected,
        message +
            ": expected " +
            pqnas::cluster::
                node_enrollment_consumption_service_result_code(
                    expected
                ) +
            ", got " +
            actual.error_code()
    );
}

fs::path make_test_directory() {
    std::string pattern =
        (
            fs::temp_directory_path() /
            "pqnas-enrollment-consumption-XXXXXX"
        ).string();

    std::vector<char> writable(
        pattern.begin(),
        pattern.end()
    );
    writable.push_back('\0');

    char* result =
        ::mkdtemp(writable.data());

    return result
        ? fs::path(result)
        : fs::path{};
}

bool create_fixture(
    NodeEnrollmentRegistry* registry,
    char enrollment_character,
    const std::string& node_id,
    const std::string& api_base_url,
    const std::string& created_at,
    const std::string& expires_at,
    Fixture* fixture,
    std::string* error
) {
    if (!registry ||
        !fixture) {
        return false;
    }

    fixture->enrollment_id =
        std::string(
            32,
            enrollment_character
        );
    fixture->node_id = node_id;
    fixture->api_base_url = api_base_url;

    pqnas::cluster::
        NodeEnrollmentTokenMaterial material;

    if (pqnas::cluster::
            generate_node_enrollment_token(
                &material) !=
        pqnas::cluster::
            NodeEnrollmentTokenResult::Ok) {
        return false;
    }

    NodeEnrollmentRecord created;

    if (registry->create_or_replace_pending(
            fixture->enrollment_id,
            node_id,
            api_base_url,
            7,
            material.token_hash(),
            created_at,
            expires_at,
            &created,
            error) !=
        NodeEnrollmentRegistryResult::Ok) {
        return false;
    }

    fixture->generation =
        created.generation;

    if (!material.take_token(
            &fixture->bootstrap_token)) {
        return false;
    }

    if (!pqnas::cluster::
            generate_internal_auth_credential_token(
                &fixture->node_credential,
                error)) {
        return false;
    }

    return true;
}

NodeEnrollmentConsumptionRequest request_for(
    const Fixture& fixture,
    const std::string& now_iso
) {
    NodeEnrollmentConsumptionRequest request;
    request.enrollment_id =
        fixture.enrollment_id;
    request.expected_generation =
        fixture.generation;
    request.node_id =
        fixture.node_id;
    request.api_base_url =
        fixture.api_base_url;
    request.protocol_version = 7;
    request.bootstrap_token =
        fixture.bootstrap_token;
    request.node_credential =
        fixture.node_credential;
    request.now_iso = now_iso;
    return request;
}

bool prepare_fixture(
    NodeEnrollmentRegistry* registry,
    const Fixture& fixture,
    const std::string& started_at,
    NodeEnrollmentRecord* record,
    std::string* error
) {
    std::string token_hash;

    const auto hash_result =
        pqnas::cluster::
            hash_node_enrollment_token(
                fixture.bootstrap_token,
                &token_hash
            );

    const auto result =
        hash_result ==
            pqnas::cluster::
                NodeEnrollmentTokenResult::Ok
        ? registry->
            prepare_provisioning_if_matches(
                fixture.enrollment_id,
                fixture.node_id,
                fixture.api_base_url,
                7,
                fixture.generation,
                token_hash,
                started_at,
                record,
                error
            )
        : NodeEnrollmentRegistryResult::
            TokenInvalid;

    pqnas::cluster::
        clear_node_enrollment_token_text(
            &token_hash
        );

    return
        result ==
        NodeEnrollmentRegistryResult::Ok;
}

void clear_fixture_secrets(
    Fixture* fixture
) {
    if (!fixture) {
        return;
    }

    pqnas::cluster::
        clear_node_enrollment_token_text(
            &fixture->bootstrap_token
        );

    pqnas::cluster::
        clear_internal_auth_token_text(
            &fixture->node_credential
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

    NodeEnrollmentRegistry
        enrollment_registry;
    NodeCredentialRegistry
        credential_registry;
    NodeRegistry node_registry;

    std::string error;

    expect(
        enrollment_registry.load(
            (
                directory /
                "enrollments.json"
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
                "credentials"
            ).string(),
            &error
        ),
        "load credential registry: " + error
    );

    error.clear();

    expect(
        node_registry.load(
            (
                directory /
                "nodes.json"
            ).string(),
            &error
        ),
        "load node registry: " + error
    );

    std::vector<AuditRecord> audits;

    NodeEnrollmentConsumptionServiceConfig config;
    config.enrollment_registry =
        &enrollment_registry;
    config.credential_registry =
        &credential_registry;
    config.node_registry =
        &node_registry;
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

    NodeEnrollmentConsumptionService
        service(config);

    std::vector<Fixture> fixtures;

    Fixture fresh;
    error.clear();

    expect(
        create_fixture(
            &enrollment_registry,
            'A',
            "node-consume-fresh",
            "https://node-consume-fresh.internal/",
            "2026-07-31T04:00:00Z",
            "2026-07-31T05:00:00Z",
            &fresh,
            &error
        ),
        "create fresh fixture: " + error
    );

    fixtures.push_back(fresh);

    const auto fresh_result =
        service.consume(
            request_for(
                fresh,
                "2026-07-31T04:10:00Z"
            )
        );

    expect_result(
        fresh_result,
        NodeEnrollmentConsumptionServiceResult::Ok,
        "fresh enrollment consumption"
    );

    expect(
        fresh_result.ok &&
        !fresh_result.recovered,
        "fresh consumption result flags"
    );

    const auto fresh_record =
        enrollment_registry.get(
            fresh.enrollment_id
        );

    expect(
        fresh_record.has_value() &&
        fresh_record->state ==
            pqnas::cluster::
                NodeEnrollmentState::Consumed &&
        fresh_record->token_hash.empty(),
        "fresh enrollment must finalize and clear verifier"
    );

    bool credential_matched = false;
    error.clear();

    expect(
        credential_registry.matches(
            fresh.node_id,
            fresh.node_credential,
            &credential_matched,
            &error
        ) ==
            NodeCredentialRegistryResult::Ok &&
        credential_matched,
        "fresh credential persisted"
    );

    const auto fresh_node =
        node_registry.get(
            fresh.node_id
        );

    expect(
        fresh_node.has_value() &&
        fresh_node->api_base_url ==
            "https://node-consume-fresh.internal" &&
        fresh_node->enabled,
        "fresh node configured canonically"
    );

    expect(
        fresh_node.has_value() &&
        fresh_node->capacity_ceiling_bytes == 0,
        "enrollment must not establish trusted node capacity"
    );

    Fixture prepared;
    error.clear();

    expect(
        create_fixture(
            &enrollment_registry,
            'B',
            "node-consume-prepared",
            "https://node-consume-prepared.internal",
            "2026-07-31T04:00:00Z",
            "2026-07-31T05:00:00Z",
            &prepared,
            &error
        ),
        "create prepared recovery fixture: " +
            error
    );

    fixtures.push_back(prepared);

    NodeEnrollmentRecord transition_record;
    error.clear();

    expect(
        prepare_fixture(
            &enrollment_registry,
            prepared,
            "2026-07-31T04:20:00Z",
            &transition_record,
            &error
        ),
        "prepare recovery fixture: " + error
    );

    error.clear();

    expect(
        credential_registry.create_if_absent(
            prepared.node_id,
            prepared.node_credential,
            &error
        ) ==
            NodeCredentialRegistryResult::Ok,
        "persist prepared recovery credential: " +
            error
    );

    const auto prepared_result =
        service.consume(
            request_for(
                prepared,
                "2026-07-31T06:00:00Z"
            )
        );

    expect_result(
        prepared_result,
        NodeEnrollmentConsumptionServiceResult::Ok,
        "recover from Prepared after TTL"
    );

    expect(
        prepared_result.recovered,
        "Prepared recovery flag"
    );

    Fixture credential_stage;
    error.clear();

    expect(
        create_fixture(
            &enrollment_registry,
            'C',
            "node-consume-credential",
            "https://node-consume-credential.internal",
            "2026-07-31T04:00:00Z",
            "2026-07-31T05:00:00Z",
            &credential_stage,
            &error
        ),
        "create credential-stage fixture: " +
            error
    );

    fixtures.push_back(credential_stage);

    error.clear();

    expect(
        prepare_fixture(
            &enrollment_registry,
            credential_stage,
            "2026-07-31T04:25:00Z",
            &transition_record,
            &error
        ),
        "prepare credential-stage fixture: " +
            error
    );

    error.clear();

    expect(
        credential_registry.create_if_absent(
            credential_stage.node_id,
            credential_stage.node_credential,
            &error
        ) ==
            NodeCredentialRegistryResult::Ok,
        "persist credential-stage credential: " +
            error
    );

    error.clear();

    expect(
        enrollment_registry.advance_provisioning_stage(
            credential_stage.enrollment_id,
            credential_stage.generation,
            NodeEnrollmentProvisioningStage::
                Prepared,
            NodeEnrollmentProvisioningStage::
                CredentialPersisted,
            &transition_record,
            &error
        ) ==
            NodeEnrollmentRegistryResult::Ok,
        "advance credential-stage fixture: " +
            error
    );

    error.clear();

    expect(
        node_registry.insert_configured_node_if_absent(
            credential_stage.node_id,
            credential_stage.api_base_url,
            true,
            "2026-07-31T04:26:00Z",
            &error
        ) ==
            NodeRegistryInsertResult::Ok,
        "persist node before credential-stage crash: " +
            error
    );

    const auto credential_stage_result =
        service.consume(
            request_for(
                credential_stage,
                "2026-07-31T06:05:00Z"
            )
        );

    expect_result(
        credential_stage_result,
        NodeEnrollmentConsumptionServiceResult::Ok,
        "recover from CredentialPersisted"
    );

    expect(
        credential_stage_result.recovered,
        "CredentialPersisted recovery flag"
    );

    Fixture node_stage;
    error.clear();

    expect(
        create_fixture(
            &enrollment_registry,
            'D',
            "node-consume-registered",
            "https://node-consume-registered.internal",
            "2026-07-31T04:00:00Z",
            "2026-07-31T05:00:00Z",
            &node_stage,
            &error
        ),
        "create node-stage fixture: " + error
    );

    fixtures.push_back(node_stage);

    error.clear();

    expect(
        prepare_fixture(
            &enrollment_registry,
            node_stage,
            "2026-07-31T04:30:00Z",
            &transition_record,
            &error
        ),
        "prepare node-stage fixture: " + error
    );

    error.clear();

    expect(
        credential_registry.create_if_absent(
            node_stage.node_id,
            node_stage.node_credential,
            &error
        ) ==
            NodeCredentialRegistryResult::Ok,
        "persist node-stage credential: " + error
    );

    error.clear();

    expect(
        enrollment_registry.advance_provisioning_stage(
            node_stage.enrollment_id,
            node_stage.generation,
            NodeEnrollmentProvisioningStage::
                Prepared,
            NodeEnrollmentProvisioningStage::
                CredentialPersisted,
            &transition_record,
            &error
        ) ==
            NodeEnrollmentRegistryResult::Ok,
        "advance node-stage credential: " + error
    );

    error.clear();

    expect(
        node_registry.insert_configured_node_if_absent(
            node_stage.node_id,
            node_stage.api_base_url,
            true,
            "2026-07-31T04:31:00Z",
            &error
        ) ==
            NodeRegistryInsertResult::Ok,
        "persist node-stage node: " + error
    );

    error.clear();

    expect(
        enrollment_registry.advance_provisioning_stage(
            node_stage.enrollment_id,
            node_stage.generation,
            NodeEnrollmentProvisioningStage::
                CredentialPersisted,
            NodeEnrollmentProvisioningStage::
                NodeRegistered,
            &transition_record,
            &error
        ) ==
            NodeEnrollmentRegistryResult::Ok,
        "advance node-stage registration: " + error
    );

    const auto node_stage_result =
        service.consume(
            request_for(
                node_stage,
                "2026-07-31T06:10:00Z"
            )
        );

    expect_result(
        node_stage_result,
        NodeEnrollmentConsumptionServiceResult::Ok,
        "recover from NodeRegistered"
    );

    expect(
        node_stage_result.recovered,
        "NodeRegistered recovery flag"
    );

    Fixture credential_conflict;
    error.clear();

    expect(
        create_fixture(
            &enrollment_registry,
            'E',
            "node-consume-existing-credential",
            "https://node-consume-existing-credential.internal",
            "2026-07-31T07:00:00Z",
            "2026-07-31T08:00:00Z",
            &credential_conflict,
            &error
        ),
        "create credential-conflict fixture: " +
            error
    );

    fixtures.push_back(credential_conflict);

    error.clear();

    expect(
        credential_registry.create_if_absent(
            credential_conflict.node_id,
            credential_conflict.node_credential,
            &error
        ) ==
            NodeCredentialRegistryResult::Ok,
        "precreate conflicting credential: " +
            error
    );

    const auto credential_conflict_result =
        service.consume(
            request_for(
                credential_conflict,
                "2026-07-31T07:10:00Z"
            )
        );

    expect_result(
        credential_conflict_result,
        NodeEnrollmentConsumptionServiceResult::
            CredentialConflict,
        "fresh transaction rejects existing credential"
    );

    const auto credential_conflict_record =
        enrollment_registry.get(
            credential_conflict.enrollment_id
        );

    expect(
        credential_conflict_record.has_value() &&
        credential_conflict_record
            ->provisioning_stage ==
                NodeEnrollmentProvisioningStage::
                    None,
        "preflight credential conflict must not prepare enrollment"
    );

    Fixture node_conflict;
    error.clear();

    expect(
        create_fixture(
            &enrollment_registry,
            'F',
            "node-consume-existing-node",
            "https://node-consume-existing-node.internal",
            "2026-07-31T07:00:00Z",
            "2026-07-31T08:00:00Z",
            &node_conflict,
            &error
        ),
        "create node-conflict fixture: " + error
    );

    fixtures.push_back(node_conflict);

    error.clear();

    expect(
        node_registry.insert_configured_node_if_absent(
            node_conflict.node_id,
            node_conflict.api_base_url,
            true,
            "2026-07-31T07:05:00Z",
            &error
        ) ==
            NodeRegistryInsertResult::Ok,
        "precreate conflicting node: " + error
    );

    const auto node_conflict_result =
        service.consume(
            request_for(
                node_conflict,
                "2026-07-31T07:10:00Z"
            )
        );

    expect_result(
        node_conflict_result,
        NodeEnrollmentConsumptionServiceResult::
            NodeConflict,
        "fresh transaction rejects existing node"
    );

    bool absent_credential = true;
    error.clear();

    expect(
        credential_registry.contains(
            node_conflict.node_id,
            &absent_credential,
            &error
        ) ==
            NodeCredentialRegistryResult::Ok &&
        !absent_credential,
        "node preflight conflict must not create credential"
    );

    Fixture active_mismatch;
    error.clear();

    expect(
        create_fixture(
            &enrollment_registry,
            'G',
            "node-consume-mismatch",
            "https://node-consume-mismatch.internal",
            "2026-07-31T07:00:00Z",
            "2026-07-31T08:00:00Z",
            &active_mismatch,
            &error
        ),
        "create active mismatch fixture: " +
            error
    );

    fixtures.push_back(active_mismatch);

    error.clear();

    expect(
        prepare_fixture(
            &enrollment_registry,
            active_mismatch,
            "2026-07-31T07:10:00Z",
            &transition_record,
            &error
        ),
        "prepare active mismatch fixture: " +
            error
    );

    std::string different_credential;
    error.clear();

    expect(
        pqnas::cluster::
            generate_internal_auth_credential_token(
                &different_credential,
                &error
            ),
        "generate mismatching credential: " +
            error
    );

    error.clear();

    expect(
        credential_registry.create_if_absent(
            active_mismatch.node_id,
            different_credential,
            &error
        ) ==
            NodeCredentialRegistryResult::Ok,
        "persist mismatching credential: " +
            error
    );

    const auto active_mismatch_result =
        service.consume(
            request_for(
                active_mismatch,
                "2026-07-31T07:20:00Z"
            )
        );

    expect_result(
        active_mismatch_result,
        NodeEnrollmentConsumptionServiceResult::
            CredentialConflict,
        "active transaction rejects foreign credential"
    );

    const auto active_mismatch_record =
        enrollment_registry.get(
            active_mismatch.enrollment_id
        );

    expect(
        active_mismatch_record.has_value() &&
        active_mismatch_record
            ->provisioning_stage ==
                NodeEnrollmentProvisioningStage::
                    Prepared,
        "credential mismatch remains fail-closed in Prepared"
    );

    pqnas::cluster::
        clear_internal_auth_token_text(
            &different_credential
        );

    Fixture wrong_token;
    error.clear();

    expect(
        create_fixture(
            &enrollment_registry,
            'H',
            "node-consume-wrong-token",
            "https://node-consume-wrong-token.internal",
            "2026-07-31T07:00:00Z",
            "2026-07-31T08:00:00Z",
            &wrong_token,
            &error
        ),
        "create wrong-token fixture: " +
            error
    );

    fixtures.push_back(wrong_token);

    NodeEnrollmentConsumptionRequest
        wrong_token_request =
            request_for(
                wrong_token,
                "2026-07-31T07:15:00Z"
            );

    wrong_token_request.bootstrap_token =
        fresh.bootstrap_token;

    const auto wrong_token_result =
        service.consume(
            wrong_token_request
        );

    expect_result(
        wrong_token_result,
        NodeEnrollmentConsumptionServiceResult::
            BootstrapTokenInvalid,
        "wrong bootstrap token"
    );

    NodeEnrollmentConsumptionRequest
        finalized_recovery_request =
            request_for(
                fresh,
                "2026-07-31T07:30:00Z"
            );

    pqnas::cluster::
        clear_node_enrollment_token_text(
            &finalized_recovery_request
                 .bootstrap_token
        );

    // Recovery after a node crash must not depend on retaining the one-time
    // bootstrap token after the Director has already consumed the intent.
    finalized_recovery_request.bootstrap_token =
        fresh.node_credential;

    const auto repeated_result =
        service.consume(
            finalized_recovery_request
        );

    expect_result(
        repeated_result,
        NodeEnrollmentConsumptionServiceResult::
            Ok,
        "recover after finalized consumption"
    );

    expect(
        repeated_result.ok &&
        repeated_result.recovered,
        "finalized recovery result flags"
    );

    std::string finalized_foreign_credential;

    error.clear();

    expect(
        pqnas::cluster::
            generate_internal_auth_credential_token(
                &finalized_foreign_credential,
                &error
            ),
        "generate finalized foreign credential: " +
            error
    );

    NodeEnrollmentConsumptionRequest
        finalized_foreign_request =
            request_for(
                fresh,
                "2026-07-31T07:31:00Z"
            );

    pqnas::cluster::
        clear_node_enrollment_token_text(
            &finalized_foreign_request
                 .bootstrap_token
        );

    pqnas::cluster::
        clear_internal_auth_token_text(
            &finalized_foreign_request
                 .node_credential
        );

    finalized_foreign_request.bootstrap_token =
        finalized_foreign_credential;

    finalized_foreign_request.node_credential =
        finalized_foreign_credential;

    const auto finalized_foreign_result =
        service.consume(
            finalized_foreign_request
        );

    expect_result(
        finalized_foreign_result,
        NodeEnrollmentConsumptionServiceResult::
            Consumed,
        "finalized recovery rejects foreign credential generically"
    );

    NodeEnrollmentConsumptionRequest
        finalized_metadata_request =
            request_for(
                fresh,
                "2026-07-31T07:32:00Z"
            );

    pqnas::cluster::
        clear_node_enrollment_token_text(
            &finalized_metadata_request
                 .bootstrap_token
        );

    finalized_metadata_request.bootstrap_token =
        fresh.node_credential;

    finalized_metadata_request.api_base_url =
        "https://different-finalized-node.internal";

    const auto finalized_metadata_result =
        service.consume(
            finalized_metadata_request
        );

    expect_result(
        finalized_metadata_result,
        NodeEnrollmentConsumptionServiceResult::
            Consumed,
        "finalized recovery rejects metadata mismatch generically"
    );

    pqnas::cluster::
        clear_node_enrollment_token_text(
            &finalized_recovery_request
                 .bootstrap_token
        );

    pqnas::cluster::
        clear_internal_auth_token_text(
            &finalized_recovery_request
                 .node_credential
        );

    pqnas::cluster::
        clear_internal_auth_token_text(
            &finalized_foreign_credential
        );

    pqnas::cluster::
        clear_node_enrollment_token_text(
            &finalized_foreign_request
                 .bootstrap_token
        );

    pqnas::cluster::
        clear_internal_auth_token_text(
            &finalized_foreign_request
                 .node_credential
        );

    pqnas::cluster::
        clear_node_enrollment_token_text(
            &finalized_metadata_request
                 .bootstrap_token
        );

    pqnas::cluster::
        clear_internal_auth_token_text(
            &finalized_metadata_request
                 .node_credential
        );

    const auto invalid_time_result =
        service.consume(
            request_for(
                wrong_token,
                "2026-02-30T07:00:00Z"
            )
        );

    expect_result(
        invalid_time_result,
        NodeEnrollmentConsumptionServiceResult::
            InvalidInput,
        "invalid calendar timestamp"
    );

    expect(
        !audits.empty(),
        "consumption service emits audit records"
    );

    for (const auto& audit : audits) {
        expect(
            audit.event ==
                "cluster.node_enrollment.consume",
            "consumption audit event name"
        );

        for (const auto& field :
             audit.fields) {
            expect(
                field.second !=
                    fresh.bootstrap_token &&
                field.second !=
                    fresh.node_credential &&
                field.second !=
                    fresh.api_base_url,
                "audit must not contain plaintext secrets or URL"
            );
        }
    }

    for (auto& fixture : fixtures) {
        clear_fixture_secrets(
            &fixture
        );
    }

    clear_fixture_secrets(&fresh);
    clear_fixture_secrets(&prepared);
    clear_fixture_secrets(&credential_stage);
    clear_fixture_secrets(&node_stage);
    clear_fixture_secrets(&credential_conflict);
    clear_fixture_secrets(&node_conflict);
    clear_fixture_secrets(&active_mismatch);
    clear_fixture_secrets(&wrong_token);

    pqnas::cluster::
        clear_node_enrollment_token_text(
            &wrong_token_request.bootstrap_token
        );

    pqnas::cluster::
        clear_internal_auth_token_text(
            &wrong_token_request.node_credential
        );

    cleanup();

    if (failures != 0) {
        std::cerr
            << failures
            << " enrollment consumption service test(s) failed\n";
        return 1;
    }

    std::cout
        << "OK: node enrollment consumption service tests passed\n";
    return 0;
}
