#include "cluster/node_enrollment_service.h"

#include <sodium.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <type_traits>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

using pqnas::cluster::
    NodeEnrollmentCreateResult;

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
    const NodeEnrollmentCreateResult& result,
    NodeEnrollmentServiceResult expected,
    const std::string& message
) {
    expect(
        result.result() == expected,
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
            "pqnas-enrollment-service-XXXXXX"
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

const std::string kIdA(32, 'A');
const std::string kIdB(32, 'B');
const std::string kIdC(32, 'C');
const std::string kIdD(32, 'D');

} // namespace

static_assert(
    !std::is_copy_constructible_v<
        NodeEnrollmentCreateResult
    >
);

static_assert(
    !std::is_copy_assignable_v<
        NodeEnrollmentCreateResult
    >
);

static_assert(
    std::is_move_constructible_v<
        NodeEnrollmentCreateResult
    >
);

static_assert(
    std::is_move_assignable_v<
        NodeEnrollmentCreateResult
    >
);

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

        fs::remove_all(
            directory,
            error
        );
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
        "load service registry: " + error
    );

    std::vector<AuditRecord> audits;

    std::vector<std::string> ids{
        kIdA,
        kIdB,
        kIdC,
        kIdD
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

    auto first =
        service.create_pending(
            "node-a",
            "https://node-a.internal",
            1,
            "2026-07-30T07:00:00Z",
            "2026-07-30T08:00:00Z",
            "admin-fp"
        );

    expect_result(
        first,
        NodeEnrollmentServiceResult::Ok,
        "create first intent"
    );

    expect(
        first.ok() &&
        first.enrollment().enrollment_id ==
            kIdA &&
        first.enrollment().node_id ==
            "node-a" &&
        first.enrollment().api_base_url ==
            "https://node-a.internal" &&
        first.enrollment().state ==
            "pending" &&
        first.enrollment().generation == 1,
        "first redacted enrollment metadata"
    );

    const auto persisted_first =
        registry.get(kIdA);

    expect(
        persisted_first.has_value() &&
        !persisted_first->token_hash.empty(),
        "registry should persist only verifier"
    );

    std::string first_plaintext =
        "old-output-secret";

    expect(
        first.take_bootstrap_token(
            &first_plaintext
        ),
        "first plaintext transfer"
    );

    expect(
        first_plaintext.size() ==
            pqnas::cluster::
                kNodeEnrollmentTokenChars,
        "first plaintext token format"
    );

    std::string first_recomputed_hash;

    expect(
        pqnas::cluster::
            hash_node_enrollment_token(
                first_plaintext,
                &first_recomputed_hash
            ) ==
                pqnas::cluster::
                    NodeEnrollmentTokenResult::Ok &&
        persisted_first.has_value() &&
        first_recomputed_hash ==
            persisted_first->token_hash,
        "plaintext must match persisted verifier"
    );

    expect(
        !first.take_bootstrap_token(
            &first_recomputed_hash
        ),
        "plaintext must transfer only once"
    );

    pqnas::cluster::
        clear_node_enrollment_token_text(
            &first_plaintext
        );

    auto replacement =
        service.create_pending(
            "node-a",
            "https://node-a.internal",
            1,
            "2026-07-30T07:10:00Z",
            "2026-07-30T08:10:00Z",
            "admin-fp"
        );

    expect_result(
        replacement,
        NodeEnrollmentServiceResult::Ok,
        "replace node-a intent"
    );

    expect(
        replacement.enrollment()
            .enrollment_id == kIdB &&
        replacement.enrollment()
            .generation == 2,
        "replacement generation"
    );

    const auto revoked_first =
        registry.get(kIdA);

    expect(
        revoked_first.has_value() &&
        revoked_first->state ==
            pqnas::cluster::
                NodeEnrollmentState::Revoked &&
        revoked_first->token_hash.empty(),
        "replacement must atomically clear old verifier"
    );

    std::string replacement_plaintext;

    expect(
        replacement.take_bootstrap_token(
            &replacement_plaintext
        ),
        "replacement plaintext transfer"
    );

    pqnas::cluster::
        clear_node_enrollment_token_text(
            &replacement_plaintext
        );

    auto duplicate_url =
        service.create_pending(
            "node-other",
            "https://node-a.internal",
            1,
            "2026-07-30T07:20:00Z",
            "2026-07-30T08:20:00Z",
            "admin-fp"
        );

    expect_result(
        duplicate_url,
        NodeEnrollmentServiceResult::
            UrlConflict,
        "active URL conflict"
    );

    std::string unavailable_secret;

    expect(
        !duplicate_url.take_bootstrap_token(
            &unavailable_secret
        ),
        "failed create must not expose token"
    );

    expect(
        registry.snapshot().size() == 2,
        "URL conflict must not mutate registry"
    );

    // The first generated ID collides with the existing first record. The
    // service must retry the ID without regenerating the plaintext token.
    int collision_id_calls = 0;
    int collision_token_calls = 0;

    pqnas::cluster::
        NodeEnrollmentServiceConfig
            collision_config;

    collision_config.registry = &registry;

    collision_config.generate_enrollment_id =
        [&](std::string* output) {
            ++collision_id_calls;

            if (!output) {
                return false;
            }

            *output =
                collision_id_calls == 1
                    ? kIdA
                    : kIdD;

            return true;
        };

    collision_config.generate_token =
        [&](pqnas::cluster::
                NodeEnrollmentTokenMaterial*
                    output) {
            ++collision_token_calls;

            return pqnas::cluster::
                generate_node_enrollment_token(
                    output
                );
        };

    collision_config.audit_emit =
        config.audit_emit;

    pqnas::cluster::NodeEnrollmentService
        collision_service(
            collision_config
        );

    auto collision_retry =
        collision_service.create_pending(
            "node-b",
            "https://node-b.internal",
            1,
            "2026-07-30T07:30:00Z",
            "2026-07-30T08:30:00Z",
            "admin-fp"
        );

    expect_result(
        collision_retry,
        NodeEnrollmentServiceResult::Ok,
        "ID collision retry"
    );

    expect(
        collision_id_calls == 2 &&
        collision_token_calls == 1 &&
        collision_retry.enrollment()
            .enrollment_id == kIdD,
        "ID collision must not regenerate token"
    );

    std::string collision_plaintext;

    expect(
        collision_retry.take_bootstrap_token(
            &collision_plaintext
        ),
        "collision retry plaintext"
    );

    pqnas::cluster::
        clear_node_enrollment_token_text(
            &collision_plaintext
        );

    pqnas::cluster::
        NodeEnrollmentServiceConfig
            exhausted_config;

    exhausted_config.registry = &registry;

    int exhausted_calls = 0;

    exhausted_config.generate_enrollment_id =
        [&](std::string* output) {
            ++exhausted_calls;

            if (!output) {
                return false;
            }

            *output = kIdA;
            return true;
        };

    exhausted_config.audit_emit =
        config.audit_emit;

    pqnas::cluster::NodeEnrollmentService
        exhausted_service(
            exhausted_config
        );

    const std::size_t before_exhaustion =
        registry.snapshot().size();

    auto exhausted =
        exhausted_service.create_pending(
            "node-c",
            "https://node-c.internal",
            1,
            "2026-07-30T07:40:00Z",
            "2026-07-30T08:40:00Z",
            "admin-fp"
        );

    expect_result(
        exhausted,
        NodeEnrollmentServiceResult::
            EnrollmentIdConflict,
        "exhausted ID collisions"
    );

    expect(
        exhausted_calls ==
            pqnas::cluster::
                kNodeEnrollmentIdGenerationAttempts &&
        registry.snapshot().size() ==
            before_exhaustion,
        "exhausted collision retries must not mutate"
    );

    pqnas::cluster::
        NodeEnrollmentServiceConfig
            crypto_failure_config;

    crypto_failure_config.registry =
        &registry;

    int crypto_failure_id_calls = 0;

    crypto_failure_config
        .generate_enrollment_id =
            [&](std::string*) {
                ++crypto_failure_id_calls;
                return true;
            };

    crypto_failure_config.generate_token =
        [](pqnas::cluster::
               NodeEnrollmentTokenMaterial*) {
            return pqnas::cluster::
                NodeEnrollmentTokenResult::
                    CryptoFailed;
        };

    crypto_failure_config.audit_emit =
        config.audit_emit;

    pqnas::cluster::NodeEnrollmentService
        crypto_failure_service(
            crypto_failure_config
        );

    auto crypto_failure =
        crypto_failure_service.create_pending(
            "node-d",
            "https://node-d.internal",
            1,
            "2026-07-30T07:50:00Z",
            "2026-07-30T08:50:00Z",
            "admin-fp"
        );

    expect_result(
        crypto_failure,
        NodeEnrollmentServiceResult::
            TokenGenerationFailed,
        "crypto generation failure"
    );

    expect(
        crypto_failure_id_calls == 0,
        "ID generation should not run after token failure"
    );

    auto invalid =
        service.create_pending(
            "../node",
            "http://not-https.internal",
            0,
            "2026-07-30T09:00:00Z",
            "2026-07-30T08:00:00Z",
            "admin-fp"
        );

    expect_result(
        invalid,
        NodeEnrollmentServiceResult::
            InvalidInput,
        "invalid enrollment request"
    );

    const auto redacted =
        service.redacted_snapshot();

    expect(
        redacted.size() ==
            registry.snapshot().size(),
        "redacted snapshot size"
    );

    for (const auto& view : redacted) {
        expect(
            !view.enrollment_id.empty() &&
            !view.node_id.empty() &&
            !view.state.empty(),
            "redacted snapshot metadata"
        );
    }

    {
        pqnas::cluster::
            NodeEnrollmentServiceConfig
                throwing_audit_config;

        throwing_audit_config.registry =
            &registry;

        throwing_audit_config
            .generate_enrollment_id =
                [](std::string* output) {
                    if (!output) {
                        return false;
                    }

                    *output =
                        std::string(32, 'M');

                    return true;
                };

        throwing_audit_config.audit_emit =
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
            throwing_audit_service(
                throwing_audit_config
            );

        auto before_move =
            throwing_audit_service.create_pending(
                "node-move",
                "https://node-move.internal",
                1,
                "2026-07-30T09:10:00Z",
                "2026-07-30T10:10:00Z",
                "admin-fp"
            );

        expect_result(
            before_move,
            NodeEnrollmentServiceResult::Ok,
            "audit exception after persistence"
        );

        const std::string move_enrollment_id =
            before_move.enrollment()
                .enrollment_id;

        const auto persisted_move =
            registry.get(
                move_enrollment_id
            );

        expect(
            persisted_move.has_value() &&
            persisted_move->state ==
                pqnas::cluster::
                    NodeEnrollmentState::Pending &&
            !persisted_move->token_hash.empty(),
            "audit failure must not roll back persisted enrollment"
        );

        NodeEnrollmentCreateResult after_move(
            std::move(before_move)
        );

        std::string moved_from_output;

        expect(
            !before_move.take_bootstrap_token(
                &moved_from_output
            ),
            "moved-from result must not retain plaintext"
        );

        expect(
            after_move.ok() &&
            after_move.enrollment()
                .enrollment_id ==
                    move_enrollment_id,
            "move construction must preserve enrollment metadata"
        );

        std::string moved_plaintext;

        expect(
            after_move.take_bootstrap_token(
                &moved_plaintext
            ),
            "moved result must retain one-time plaintext"
        );

        std::string moved_hash;

        expect(
            pqnas::cluster::
                hash_node_enrollment_token(
                    moved_plaintext,
                    &moved_hash
                ) ==
                    pqnas::cluster::
                        NodeEnrollmentTokenResult::Ok &&
            persisted_move.has_value() &&
            moved_hash ==
                persisted_move->token_hash,
            "moved plaintext must match persisted verifier"
        );

        expect(
            !after_move.take_bootstrap_token(
                &moved_hash
            ),
            "moved plaintext must remain one-time"
        );

        pqnas::cluster::
            clear_node_enrollment_token_text(
                &moved_plaintext
            );
    }

    // Separate persistence-failure registry so the main audit fixtures remain
    // intact after replacing its directory with a blocker file.
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

        int failure_id_calls = 0;

        failure_config.generate_enrollment_id =
            [&](std::string* output) {
                if (!output) {
                    return false;
                }

                ++failure_id_calls;

                *output =
                    std::string(
                        32,
                        failure_id_calls == 1
                            ? 'P'
                            : 'Q'
                    );

                return true;
            };

        failure_config.audit_emit =
            config.audit_emit;

        pqnas::cluster::NodeEnrollmentService
            failure_service(
                failure_config
            );

        auto fixture =
            failure_service.create_pending(
                "node-persist",
                "https://node-persist.internal",
                1,
                "2026-07-30T10:00:00Z",
                "2026-07-30T11:00:00Z",
                "admin-fp"
            );

        expect_result(
            fixture,
            NodeEnrollmentServiceResult::Ok,
            "create persistence fixture"
        );

        std::string fixture_plaintext;

        expect(
            fixture.take_bootstrap_token(
                &fixture_plaintext
            ),
            "take persistence fixture token"
        );

        pqnas::cluster::
            clear_node_enrollment_token_text(
                &fixture_plaintext
            );

        const auto before_failure =
            failure_registry.snapshot();

        std::error_code filesystem_error;

        fs::remove(
            failure_path,
            filesystem_error
        );

        expect(
            !filesystem_error,
            "remove persistence fixture file"
        );

        filesystem_error.clear();

        fs::remove(
            failure_directory,
            filesystem_error
        );

        expect(
            !filesystem_error,
            "remove persistence fixture directory"
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

        auto failed_persist =
            failure_service.create_pending(
                "node-persist-two",
                "https://node-persist-two.internal",
                1,
                "2026-07-30T10:10:00Z",
                "2026-07-30T11:10:00Z",
                "admin-fp"
            );

        expect_result(
            failed_persist,
            NodeEnrollmentServiceResult::
                PersistFailed,
            "service persistence failure"
        );

        expect(
            failure_id_calls == 2,
            "persistence fixture should use distinct enrollment IDs"
        );

        expect(
            !failed_persist.take_bootstrap_token(
                &fixture_plaintext
            ),
            "persistence failure must not expose token"
        );

        const auto after_failure =
            failure_registry.snapshot();

        expect(
            after_failure.size() ==
                before_failure.size() &&
            after_failure[0].enrollment_id ==
                before_failure[0].enrollment_id &&
            !after_failure[0].token_hash.empty(),
            "persistence failure must preserve registry memory"
        );
    }

    pqnas::cluster::
        NodeEnrollmentServiceConfig
            unavailable_config;

    unavailable_config.audit_emit =
        config.audit_emit;

    pqnas::cluster::NodeEnrollmentService
        unavailable_service(
            unavailable_config
        );

    auto unavailable =
        unavailable_service.create_pending(
            "node-unavailable",
            "https://node-unavailable.internal",
            1,
            "2026-07-30T12:00:00Z",
            "2026-07-30T13:00:00Z",
            "admin-fp"
        );

    expect_result(
        unavailable,
        NodeEnrollmentServiceResult::
            ServiceUnavailable,
        "missing registry configuration"
    );

    expect(
        !audits.empty(),
        "service operations should emit audit results"
    );

    for (const AuditRecord& audit : audits) {
        expect(
            audit.event ==
                "admin.cluster_node_enrollment.create",
            "enrollment audit event"
        );

        expect(
            audit.fields.count(
                "reason_code"
            ) == 1,
            "audit reason code"
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
            "audit must exclude URL, secrets, raw errors and paths"
        );
    }

    expect(
        std::string(
            pqnas::cluster::
                node_enrollment_service_result_code(
                    NodeEnrollmentServiceResult::
                        TokenGenerationFailed
                )
        ) ==
            "enrollment_token_generation_failed",
        "stable token-generation reason code"
    );

    cleanup();

    if (failures != 0) {
        std::cerr
            << failures
            << " node enrollment service test(s) failed\n";
        return 1;
    }

    std::cout
        << "OK: node enrollment service tests passed\n";

    return 0;
}
