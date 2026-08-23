#include "cluster/node_credential_rotation_service.h"

#include "cluster/internal_auth.h"
#include "cluster/node_credential_registry.h"
#include "cluster/node_registry.h"
#include "cluster/node_tls_trust_store.h"

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

namespace {

namespace fs = std::filesystem;

using pqnas::cluster::
    NodeCredentialRegistry;

using pqnas::cluster::
    NodeCredentialRegistryResult;

using pqnas::cluster::
    NodeCredentialRotationFinalizeRequest;

using pqnas::cluster::
    NodeCredentialRotationFinalizeResponse;

using pqnas::cluster::
    NodeCredentialRotationPrepareRequest;

using pqnas::cluster::
    NodeCredentialRotationPrepareResponse;

using pqnas::cluster::
    NodeCredentialRotationResult;

using pqnas::cluster::
    NodeCredentialRotationService;

using pqnas::cluster::
    NodeCredentialRotationServiceCode;

using pqnas::cluster::
    NodeCredentialRotationServiceConfig;

using pqnas::cluster::
    NodeCredentialRotationTransportCode;

using pqnas::cluster::
    NodeRegistry;

using pqnas::cluster::
    NodeTlsTrustStore;

using pqnas::cluster::
    NodeTlsTrustStoreResult;

int failures = 0;

constexpr char kTestTlsPin[] =
    "sha256/"
    "lAhFeu/Qcc7BJ8H5hTmTCGGtG6lMlA25dclywJ/Gi2g=";

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
            "pqnas-node-rotation-service-XXXXXX"
        ).string();

    std::vector<char> writable(
        pattern.begin(),
        pattern.end()
    );

    writable.push_back(
        '\0'
    );

    char* created =
        ::mkdtemp(
            writable.data()
        );

    return created
        ? fs::path(created)
        : fs::path{};
}

bool credential_matches(
    NodeCredentialRegistry& registry,
    const std::string& node_id,
    const std::string& token
) {
    bool matched = false;
    std::string error;

    const auto result =
        registry.matches(
            node_id,
            token,
            &matched,
            &error
        );

    return
        result ==
            NodeCredentialRegistryResult::Ok &&
        matched;
}

bool coordination_is_available(
    const std::shared_ptr<std::mutex>& mutex
) {
    std::atomic<bool>
        acquired{false};

    std::thread probe(
        [&]() {
            if (mutex->try_lock()) {
                acquired.store(
                    true
                );

                mutex->unlock();
            }
        }
    );

    probe.join();

    return
        acquired.load();
}

} // namespace

int main() {
    const fs::path root =
        make_test_directory();

    if (root.empty()) {
        std::cerr
            << "FAIL: test directory creation failed\n";

        return 1;
    }

    const auto cleanup =
        [&]() {
            std::error_code error;

            fs::remove_all(
                root,
                error
            );
        };

    const std::string token_a(
        43,
        'A'
    );

    const std::string token_b =
        std::string(
            42,
            'B'
        ) +
        "A";

    const std::string token_c =
        std::string(
            42,
            'C'
        ) +
        "A";

    const std::string token_d =
        std::string(
            42,
            'D'
        ) +
        "A";

    expect(
        pqnas::cluster::
            valid_internal_auth_credential_token(
                token_a
            ) &&
        pqnas::cluster::
            valid_internal_auth_credential_token(
                token_b
            ) &&
        pqnas::cluster::
            valid_internal_auth_credential_token(
                token_c
            ) &&
        pqnas::cluster::
            valid_internal_auth_credential_token(
                token_d
            ),
        "synthetic credentials are canonical"
    );

    NodeRegistry
        node_registry;

    NodeCredentialRegistry
        credential_registry;

    NodeTlsTrustStore
        tls_trust_store;

    std::string error;

    expect(
        node_registry.load(
            (
                root /
                "node_registry.json"
            ).string(),
            &error
        ),
        "load empty node registry: " +
            error
    );

    error.clear();

    expect(
        credential_registry.load(
            (
                root /
                "node_credentials"
            ).string(),
            &error
        ),
        "load empty credential registry: " +
            error
    );

    error.clear();

    expect(
        tls_trust_store.load(
            (
                root /
                "node_tls_trust"
            ).string(),
            &error
        ),
        "load empty TLS trust store: " +
            error
    );

    const auto add_node =
        [&](const std::string& node_id,
            const std::string& url,
            const std::string& token) {
            error.clear();

            expect(
                node_registry
                    .upsert_configured_node(
                        node_id,
                        url,
                        true,
                        "2026-08-23T10:00:00Z",
                        &error
                    ),
                "add node " +
                    node_id +
                    ": " +
                    error
            );

            error.clear();

            expect(
                credential_registry
                    .create_if_absent(
                        node_id,
                        token,
                        &error
                    ) ==
                    NodeCredentialRegistryResult::
                        Ok,
                "add credential " +
                    node_id +
                    ": " +
                    error
            );

            error.clear();

            expect(
                tls_trust_store.bind_if_absent(
                    node_id,
                    kTestTlsPin,
                    &error
                ) ==
                    NodeTlsTrustStoreResult::Ok,
                "bind TLS trust " +
                    node_id +
                    ": " +
                    error
            );
        };

    add_node(
        "node-success",
        "https://node-success.internal",
        token_a
    );

    add_node(
        "node-prepare-fail",
        "https://node-prepare-fail.internal",
        token_a
    );

    add_node(
        "node-state-conflict",
        "https://node-state-conflict.internal",
        token_a
    );

    add_node(
        "node-finalize-retry",
        "https://node-finalize-retry.internal",
        token_a
    );

    add_node(
        "node-concurrent",
        "https://node-concurrent.internal",
        token_a
    );

    error.clear();

    expect(
        node_registry.upsert_configured_node(
            "node-no-trust",
            "https://node-no-trust.internal",
            true,
            "2026-08-23T10:00:00Z",
            &error
        ),
        "add node without TLS trust: " +
            error
    );

    error.clear();

    expect(
        credential_registry.create_if_absent(
            "node-no-trust",
            token_a,
            &error
        ) ==
            NodeCredentialRegistryResult::Ok,
        "add credential for node without TLS trust: " +
            error
    );

    auto coordination_mutex =
        std::make_shared<std::mutex>();

    std::mutex concurrent_gate_mutex;
    std::condition_variable
        concurrent_gate_condition;

    bool concurrent_prepare_entered = false;
    bool concurrent_prepare_release = false;

    int finalize_retry_calls = 0;
    int state_conflict_finalize_calls = 0;
    int prepare_fail_finalize_calls = 0;

    NodeCredentialRotationServiceConfig
        config;

    config.node_registry =
        &node_registry;

    config.credential_registry =
        &credential_registry;

    config.tls_trust_store =
        &tls_trust_store;

    config.coordination_mutex =
        coordination_mutex;

    config.prepare_transport =
        [&](const NodeCredentialRotationPrepareRequest&
                request) {
            expect(
                coordination_is_available(
                    coordination_mutex
                ),
                "prepare transport runs outside coordination mutex"
            );

            expect(
                request.tls_spki_sha256_pin ==
                    kTestTlsPin,
                "prepare uses exact enrolled TLS pin"
            );

            NodeCredentialRotationPrepareResponse
                response;

            response.http_status = 200;

            if (request.api_base_url ==
                "https://node-prepare-fail.internal") {
                response.code =
                    NodeCredentialRotationTransportCode::
                        TransportFailed;

                response.http_status = 0;

                return response;
            }

            if (request.api_base_url ==
                "https://node-state-conflict.internal") {
                expect(
                    request.authorization ==
                        "Bearer " +
                            token_a,
                    "state conflict prepare uses OLD"
                );

                std::string mutation_error;

                expect(
                    credential_registry.upsert(
                        "node-state-conflict",
                        token_c,
                        &mutation_error
                    ) ==
                        NodeCredentialRegistryResult::
                            Ok,
                    "inject concurrent credential generation"
                );

                response.code =
                    NodeCredentialRotationTransportCode::
                        Ok;

                response.pending_credential =
                    token_b;

                return response;
            }

            if (request.api_base_url ==
                "https://node-finalize-retry.internal") {
                response.code =
                    NodeCredentialRotationTransportCode::
                        Ok;

                // First call: OLD -> NEW. Retry after finalize uncertainty:
                // NEW -> the same still-pending NEW.
                response.pending_credential =
                    token_b;

                return response;
            }

            if (request.api_base_url ==
                "https://node-concurrent.internal") {
                {
                    std::unique_lock<std::mutex>
                        lock(
                            concurrent_gate_mutex
                        );

                    concurrent_prepare_entered =
                        true;

                    concurrent_gate_condition
                        .notify_all();

                    concurrent_gate_condition
                        .wait(
                            lock,
                            [&]() {
                                return
                                    concurrent_prepare_release;
                            }
                        );
                }

                response.code =
                    NodeCredentialRotationTransportCode::
                        Ok;

                response.pending_credential =
                    token_d;

                return response;
            }

            response.code =
                NodeCredentialRotationTransportCode::
                    Ok;

            response.pending_credential =
                token_b;

            return response;
        };

    config.finalize_transport =
        [&](const NodeCredentialRotationFinalizeRequest&
                request) {
            expect(
                coordination_is_available(
                    coordination_mutex
                ),
                "finalize transport runs outside coordination mutex"
            );

            expect(
                request.tls_spki_sha256_pin ==
                    kTestTlsPin,
                "finalize preserves exact prepare TLS pin"
            );

            NodeCredentialRotationFinalizeResponse
                response;

            response.http_status = 200;

            if (request.api_base_url ==
                "https://node-prepare-fail.internal") {
                ++prepare_fail_finalize_calls;
            }

            if (request.api_base_url ==
                "https://node-state-conflict.internal") {
                ++state_conflict_finalize_calls;
            }

            if (request.api_base_url ==
                "https://node-finalize-retry.internal") {
                ++finalize_retry_calls;

                expect(
                    request.authorization ==
                        "Bearer " +
                            token_b,
                    "finalize uses Director-authoritative NEW"
                );

                if (finalize_retry_calls == 1) {
                    response.code =
                        NodeCredentialRotationTransportCode::
                            TransportFailed;

                    response.http_status = 0;

                    return response;
                }
            }

            if (request.api_base_url ==
                "https://node-concurrent.internal") {
                expect(
                    request.authorization ==
                        "Bearer " +
                            token_d,
                    "concurrent rotation finalizes with NEW"
                );
            }

            response.code =
                NodeCredentialRotationTransportCode::
                    Ok;

            return response;
        };

    NodeCredentialRotationService
        service(
            config
        );

    // ---------------------------------------------------------------------
    // TLS trust is mandatory before OLD bearer material or transport.
    // ---------------------------------------------------------------------
    {
        const NodeCredentialRotationResult result =
            service.rotate(
                "node-no-trust"
            );

        expect(
            result.code ==
                NodeCredentialRotationServiceCode::
                    TlsTrustUnavailable &&
            !result.director_credential_committed &&
            !result.storage_finalized,
            "node without TLS trust fails closed before rotation"
        );

        expect(
            credential_matches(
                credential_registry,
                "node-no-trust",
                token_a
            ),
            "TLS trust failure leaves OLD credential unchanged"
        );
    }

    {
        auto invalid_config =
            config;

        invalid_config.tls_trust_store =
            nullptr;

        NodeCredentialRotationService
            invalid_service(
                invalid_config
            );

        const auto result =
            invalid_service.rotate(
                "node-success"
            );

        expect(
            result.code ==
                NodeCredentialRotationServiceCode::
                    ServiceUnavailable,
            "missing TLS trust-store configuration fails closed"
        );
    }

    // ---------------------------------------------------------------------
    // Normal state-machine order:
    // prepare OLD -> durable Director CAS -> finalize NEW.
    // ---------------------------------------------------------------------
    {
        const NodeCredentialRotationResult result =
            service.rotate(
                "node-success"
            );

        expect(
            result.code ==
                NodeCredentialRotationServiceCode::
                    Ok &&
            result.director_credential_committed &&
            result.storage_finalized,
            "successful rotation completes both sides"
        );

        expect(
            !credential_matches(
                credential_registry,
                "node-success",
                token_a
            ) &&
            credential_matches(
                credential_registry,
                "node-success",
                token_b
            ),
            "successful rotation leaves only NEW authoritative on Director"
        );

        expect(
            !service.rotation_in_progress(
                "node-success"
            ),
            "successful rotation clears busy marker"
        );
    }

    // ---------------------------------------------------------------------
    // Prepare failure: no Director mutation and no finalize.
    // ---------------------------------------------------------------------
    {
        const NodeCredentialRotationResult result =
            service.rotate(
                "node-prepare-fail"
            );

        expect(
            result.code ==
                NodeCredentialRotationServiceCode::
                    PrepareFailed,
            "prepare failure is reported"
        );

        expect(
            credential_matches(
                credential_registry,
                "node-prepare-fail",
                token_a
            ),
            "prepare failure preserves OLD"
        );

        expect(
            prepare_fail_finalize_calls == 0,
            "prepare failure never calls finalize"
        );
    }

    // ---------------------------------------------------------------------
    // Credential changes while prepare is on the wire:
    // compare-and-replace rejects stale OLD and finalize is forbidden.
    // ---------------------------------------------------------------------
    {
        const NodeCredentialRotationResult result =
            service.rotate(
                "node-state-conflict"
            );

        expect(
            result.code ==
                NodeCredentialRotationServiceCode::
                    CredentialStateConflict,
            "stale prepared generation is rejected"
        );

        expect(
            credential_matches(
                credential_registry,
                "node-state-conflict",
                token_c
            ),
            "state conflict preserves newer authoritative credential"
        );

        expect(
            state_conflict_finalize_calls == 0,
            "state conflict never calls finalize"
        );
    }

    // ---------------------------------------------------------------------
    // Finalize uncertainty: Director NEW is never rolled back.
    // Retry with NEW can complete the existing Storage pending generation.
    // ---------------------------------------------------------------------
    {
        const NodeCredentialRotationResult first =
            service.rotate(
                "node-finalize-retry"
            );

        expect(
            first.code ==
                NodeCredentialRotationServiceCode::
                    FinalizePending &&
            first.director_credential_committed &&
            !first.storage_finalized,
            "lost finalize leaves explicit pending result"
        );

        expect(
            credential_matches(
                credential_registry,
                "node-finalize-retry",
                token_b
            ),
            "finalize failure keeps durable NEW on Director"
        );

        const NodeCredentialRotationResult retry =
            service.rotate(
                "node-finalize-retry"
            );

        expect(
            retry.code ==
                NodeCredentialRotationServiceCode::
                    Ok &&
            retry.director_credential_committed &&
            retry.storage_finalized,
            "retry with Director NEW completes pending finalize"
        );

        expect(
            finalize_retry_calls == 2,
            "finalize retry reaches transport exactly twice"
        );

        expect(
            credential_matches(
                credential_registry,
                "node-finalize-retry",
                token_b
            ),
            "retry preserves NEW generation"
        );
    }

    // ---------------------------------------------------------------------
    // Same-node rotations cannot overlap. The busy marker exists during
    // network I/O without holding coordination_mutex.
    // ---------------------------------------------------------------------
    {
        NodeCredentialRotationResult
            first_result;

        std::thread first(
            [&]() {
                first_result =
                    service.rotate(
                        "node-concurrent"
                    );
            }
        );

        {
            std::unique_lock<std::mutex>
                lock(
                    concurrent_gate_mutex
                );

            concurrent_gate_condition.wait(
                lock,
                [&]() {
                    return
                        concurrent_prepare_entered;
                }
            );
        }

        expect(
            service.rotation_in_progress(
                "node-concurrent"
            ),
            "busy marker remains set during prepare network I/O"
        );

        expect(
            coordination_is_available(
                coordination_mutex
            ),
            "coordination mutex is free while prepare is blocked"
        );

        const NodeCredentialRotationResult second =
            service.rotate(
                "node-concurrent"
            );

        expect(
            second.code ==
                NodeCredentialRotationServiceCode::
                    RotationInProgress,
            "second same-node rotation is rejected"
        );

        {
            std::lock_guard<std::mutex>
                lock(
                    concurrent_gate_mutex
                );

            concurrent_prepare_release =
                true;
        }

        concurrent_gate_condition.notify_all();

        first.join();

        expect(
            first_result.code ==
                NodeCredentialRotationServiceCode::
                    Ok,
            "first concurrent rotation completes normally"
        );

        expect(
            credential_matches(
                credential_registry,
                "node-concurrent",
                token_d
            ),
            "concurrent fixture commits its NEW generation"
        );

        expect(
            !service.rotation_in_progress(
                "node-concurrent"
            ),
            "busy marker clears after concurrent rotation"
        );
    }

    cleanup();

    if (failures == 0) {
        std::cout
            << "OK: node credential rotation service tests passed\n";

        return 0;
    }

    std::cerr
        << "FAIL: "
        << failures
        << " node credential rotation service test(s) failed\n";

    return 1;
}
