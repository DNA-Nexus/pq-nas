#include "cluster/storage_user_projection_reconciler.h"

#include "cluster/storage_user_projection_sync_state_store.h"
#include "cluster/storage_user_projection_transition_store.h"
#include "users_registry.h"

#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

int failures = 0;

struct AuditRecord {
    std::string event;
    std::string outcome;
    std::map<std::string, std::string> fields;
};

bool audit_field_equals(
    const AuditRecord& audit,
    const std::string& key,
    const std::string& expected
) {
    const auto it =
        audit.fields.find(key);

    return
        it != audit.fields.end() &&
        it->second == expected;
}

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
            "pqnas-projection-reconciler-XXXXXX"
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

std::unique_ptr<
    pqnas::cluster::StorageUserProjectionTransitionStore
>
load_transition_store(
    const fs::path& path,
    std::string* error
) {
    auto store =
        std::make_unique<
            pqnas::cluster::
                StorageUserProjectionTransitionStore
        >();

    if (!store->load(
            path.string(),
            error)) {
        return nullptr;
    }

    return store;
}


pqnas::UserRec make_user(
    char fingerprint_character,
    const std::string& home_node
) {
    pqnas::UserRec user;

    user.fingerprint =
        std::string(
            128,
            fingerprint_character
        );

    user.name =
        "projection reconciler";

    user.role =
        "user";

    user.status =
        "enabled";

    user.storage_state =
        "unallocated";

    user.quota_bytes = 0;
    user.root_rel.clear();
    user.storage_pool_id.clear();

    user.home_node =
        home_node;

    return user;
}

pqnas::cluster::
StorageUserProjectionSyncResult
sync_success(
    const pqnas::cluster::
        StorageUserProjection& projection
) {
    pqnas::cluster::
        StorageUserProjectionSyncResult result;

    result.code =
        pqnas::cluster::
            StorageUserProjectionSyncClientCode::
                Applied;

    result.http_status = 200;
    result.revision =
        projection.revision;

    return result;
}

std::string fingerprint_for_index(
    std::size_t value
) {
    static constexpr char hex[] =
        "0123456789abcdef";

    std::string fingerprint(
        128,
        '0'
    );

    std::size_t position =
        fingerprint.size();

    do {
        --position;

        fingerprint[position] =
            hex[value & 0x0fU];

        value >>= 4U;
    } while (value != 0 &&
             position != 0);

    return fingerprint;
}

pqnas::cluster::
StorageUserProjectionAuditClientResult
audit_match(
    const pqnas::cluster::
        StorageUserProjectionAuditRequest& request
) {
    pqnas::cluster::
        StorageUserProjectionAuditClientResult
            result;

    result.code =
        pqnas::cluster::
            StorageUserProjectionAuditClientCode::Ok;

    result.http_status = 200;
    result.result.complete = true;
    result.result.matched =
        request.expected.size();

    return result;
}

} // namespace

int main() {
    // RT-008 range planner: 513 expected identities become 512 + 1 with
    // contiguous lexical boundaries. Zero identities still audit all Storage.
    {
        std::vector<
            pqnas::cluster::
                StorageUserProjectionAuditExpected
        > expected;

        expected.reserve(
            pqnas::cluster::
                kStorageUserProjectionAuditMaxExpected +
            1
        );

        for (std::size_t index = 0;
             index <
                pqnas::cluster::
                    kStorageUserProjectionAuditMaxExpected +
                    1;
             ++index) {
            pqnas::cluster::
                StorageUserProjectionAuditExpected
                    entry;

            entry.fingerprint =
                fingerprint_for_index(
                    index + 1
                );

            entry.revision = 7;

            entry.digest_hex =
                std::string(
                    pqnas::cluster::
                        kStorageUserProjectionDigestHexChars,
                    'a'
                );

            expected.push_back(
                std::move(entry)
            );
        }

        std::vector<
            pqnas::cluster::
                StorageUserProjectionAuditRequest
        > requests;

        expect(
            pqnas::cluster::
                build_storage_user_projection_reconcile_audit_requests(
                    "storage-01",
                    expected,
                    &requests
                ) &&
            requests.size() == 2 &&
            requests[0].range_start.empty() &&
            requests[0].range_end ==
                expected[
                    pqnas::cluster::
                        kStorageUserProjectionAuditMaxExpected
                ].fingerprint &&
            requests[0].expected.size() ==
                pqnas::cluster::
                    kStorageUserProjectionAuditMaxExpected &&
            requests[1].range_start ==
                requests[0].range_end &&
            requests[1].range_end.empty() &&
            requests[1].expected.size() == 1,
            "RT-008 range planner partitions 513 identities as contiguous 512 + 1"
        );

        requests.clear();

        expect(
            pqnas::cluster::
                build_storage_user_projection_reconcile_audit_requests(
                    "storage-empty",
                    {},
                    &requests
                ) &&
            requests.size() == 1 &&
            requests[0].range_start.empty() &&
            requests[0].range_end.empty() &&
            requests[0].expected.empty(),
            "RT-008 zero-user node receives one full-namespace audit"
        );
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

    const fs::path users_path =
        directory / "users.json";

    const fs::path state_path =
        directory /
        "storage_projection_sync_state.json";

    pqnas::UsersRegistry users;

    pqnas::cluster::
        StorageUserProjectionSyncStateStore
            state_store;

    std::string error;

    expect(
        state_store.load(
            state_path.string(),
            &error
        ),
        "sync-state store loads"
    );

    bool projection_changed = false;

    const auto placed =
        make_user(
            'a',
            "storage-01"
        );

    expect(
        users.upsert_persisted(
            placed,
            users_path.string(),
            &projection_changed,
            &error
        ),
        "placed user persisted"
    );

    const auto unplaced =
        make_user(
            'b',
            ""
        );

    expect(
        users.upsert_persisted(
            unplaced,
            users_path.string(),
            &projection_changed,
            &error
        ),
        "unplaced user persisted"
    );

    std::vector<
        pqnas::cluster::
            StorageUserProjection
    > sent;

    bool fail_next = false;

    std::vector<AuditRecord> audits;

    // RT-016: production shares this boundary with physical user deletion and
    // Node deletion. The network callback below probes it from another thread.
    const auto rt016_transition_mutex =
        std::make_shared<std::mutex>();

    bool rt016_probe_first_authorization = true;
    bool rt016_pending_visible_before_sync = false;
    bool rt016_boundary_available_during_sync = false;

    auto transition_store =
        load_transition_store(
            directory /
                "storage_projection_transition_journal.json",
            &error
        );

    expect(
        transition_store != nullptr,
        "base transition journal loads: " +
            error
    );

    pqnas::cluster::
        StorageUserProjectionReconcilerConfig
            config;

    config.users =
        &users;

    config.state_store =
        &state_store;

    config.transition_store =
        transition_store.get();

    config.transition_mutex =
        rt016_transition_mutex;

    config.audit_emit =
        [&](const std::string& event,
            const std::string& outcome,
            const std::map<std::string, std::string>& fields) {
            audits.push_back(
                AuditRecord{
                    event,
                    outcome,
                    fields
                }
            );
        };

    config.sync_projection =
        [&](const pqnas::cluster::
                StorageUserProjection& projection) {
            if (rt016_probe_first_authorization) {
                rt016_probe_first_authorization = false;

                const auto pending =
                    transition_store->get(
                        projection.fingerprint
                    );

                rt016_pending_visible_before_sync =
                    pending.has_value() &&
                    pending->home_node ==
                        projection.home_node &&
                    pending->revision ==
                        projection.revision;

                bool boundary_acquired = false;

                std::thread boundary_probe(
                    [&]() {
                        boundary_acquired =
                            rt016_transition_mutex->
                                try_lock();

                        if (boundary_acquired) {
                            rt016_transition_mutex->
                                unlock();
                        }
                    }
                );

                boundary_probe.join();

                rt016_boundary_available_during_sync =
                    boundary_acquired;
            }

            sent.push_back(
                projection
            );

            if (fail_next) {
                fail_next = false;

                pqnas::cluster::
                    StorageUserProjectionSyncResult
                        failed;

                failed.code =
                    pqnas::cluster::
                        StorageUserProjectionSyncClientCode::
                            TransportFailed;

                return failed;
            }

            return sync_success(
                projection
            );
        };

    pqnas::cluster::
        StorageUserProjectionReconciler
            reconciler(config);

    // First reconciliation sends the current placed projection and records the
    // exact Node/revision that accepted it.
    {
        sent.clear();

        const auto result =
            reconciler.reconcile_once();

        const auto state =
            state_store.get(
                placed.fingerprint
            );

        expect(
            result.ok &&
            result.synced_current == 1 &&
            result.no_placement == 1 &&
            rt016_pending_visible_before_sync &&
            rt016_boundary_available_during_sync &&
            sent.size() == 1 &&
            sent[0].home_node ==
                "storage-01" &&
            state.has_value() &&
            state->home_node ==
                "storage-01" &&
            state->revision ==
                sent[0].revision &&
            audits.size() == 1 &&
            audits[0].event ==
                "cluster.storage_projection.synced" &&
            audits[0].outcome == "ok" &&
            audit_field_equals(
                audits[0],
                "fingerprint",
                placed.fingerprint
            ) &&
            audit_field_equals(
                audits[0],
                "previous_revision",
                "0"
            ) &&
            audit_field_equals(
                audits[0],
                "projection_revision",
                std::to_string(sent[0].revision)
            ) &&
            audit_field_equals(
                audits[0],
                "previous_home_node",
                ""
            ) &&
            audit_field_equals(
                audits[0],
                "home_node",
                "storage-01"
            ),
            "RT-016 initial reconciliation journals pending before unlocked network I/O"
        );
    }

    // RT-008 rolling-upgrade fallback: without audit capability, equal
    // confirmed/current state is verified by an idempotent Storage POST.
    {
        sent.clear();

        const auto result =
            reconciler.reconcile_once();

        const auto current_user =
            users.get(
                placed.fingerprint
            );

        expect(
            result.ok &&
            result.already_synced == 1 &&
            result.no_placement == 1 &&
            sent.size() == 1 &&
            current_user.has_value() &&
            sent[0].home_node ==
                "storage-01" &&
            sent[0].revision ==
                current_user->
                    storage_projection_revision &&
            audits.size() == 1,
            "legacy equal-confirmed revision is verified by idempotent Storage POST"
        );
    }

    // RT-008 audit-capable Nodes prove equal/current state by full range
    // readback. A zero-user Node is still checked with an empty full range.
    {
        auto audit_config =
            config;

        audit_config.audit_emit = {};

        std::vector<
            pqnas::cluster::
                StorageUserProjectionAuditRequest
        > proof_requests;

        audit_config.list_audit_capable_nodes =
            []() {
                return std::vector<std::string>{
                    "storage-01",
                    "storage-empty"
                };
            };

        audit_config.audit_projection_range =
            [&](const pqnas::cluster::
                    StorageUserProjectionAuditRequest& request) {
                proof_requests.push_back(
                    request
                );

                return audit_match(
                    request
                );
            };

        pqnas::cluster::
            StorageUserProjectionReconciler
                audit_reconciler(
                    audit_config
                );

        sent.clear();

        const auto result =
            audit_reconciler.reconcile_once();

        expect(
            result.ok &&
            result.already_synced == 1 &&
            result.no_placement == 1 &&
            sent.empty() &&
            proof_requests.size() == 2 &&
            proof_requests[0].node_id ==
                "storage-01" &&
            proof_requests[0].range_start.empty() &&
            proof_requests[0].range_end.empty() &&
            proof_requests[0].expected.size() == 1 &&
            proof_requests[1].node_id ==
                "storage-empty" &&
            proof_requests[1].range_start.empty() &&
            proof_requests[1].range_end.empty() &&
            proof_requests[1].expected.empty(),
            "audit-capable equal state requires current full-range Storage proof"
        );
    }

    // Original RT-008 regression: Director says A@rev is confirmed, but
    // Storage lost the projection. Missing readback replays the exact same
    // current projection and only then reports convergence.
    {
        auto missing_config =
            config;

        missing_config.audit_emit = {};

        missing_config.list_audit_capable_nodes =
            []() {
                return std::vector<std::string>{
                    "storage-01"
                };
            };

        missing_config.audit_projection_range =
            [&](const pqnas::cluster::
                    StorageUserProjectionAuditRequest& request) {
                auto result =
                    audit_match(
                        request
                    );

                expect(
                    request.expected.size() == 1,
                    "RT-008 missing proof carries one expected projection"
                );

                result.result.matched = 0;

                pqnas::cluster::
                    StorageUserProjectionAuditMismatch
                        mismatch;

                mismatch.code =
                    pqnas::cluster::
                        StorageUserProjectionAuditMismatchCode::
                            Missing;

                mismatch.fingerprint =
                    request.expected[0].fingerprint;

                mismatch.expected_revision =
                    request.expected[0].revision;

                mismatch.actual_revision = 0;

                result.result.mismatches.push_back(
                    std::move(mismatch)
                );

                return result;
            };

        pqnas::cluster::
            StorageUserProjectionReconciler
                missing_reconciler(
                    missing_config
                );

        sent.clear();

        const auto result =
            missing_reconciler.reconcile_once();

        const auto current_user =
            users.get(
                placed.fingerprint
            );

        expect(
            result.ok &&
            result.already_synced == 0 &&
            result.synced_current == 1 &&
            sent.size() == 1 &&
            current_user.has_value() &&
            sent[0].fingerprint ==
                placed.fingerprint &&
            sent[0].home_node ==
                "storage-01" &&
            sent[0].revision ==
                current_user->
                    storage_projection_revision,
            "RT-008 missing Storage projection is replayed before convergence"
        );
    }

    // Equal-revision content disagreement is never overwritten automatically.
    {
        auto conflict_config =
            config;

        conflict_config.audit_emit = {};

        conflict_config.list_audit_capable_nodes =
            []() {
                return std::vector<std::string>{
                    "storage-01"
                };
            };

        conflict_config.audit_projection_range =
            [](const pqnas::cluster::
                    StorageUserProjectionAuditRequest& request) {
                auto result =
                    audit_match(
                        request
                    );

                result.result.matched = 0;

                pqnas::cluster::
                    StorageUserProjectionAuditMismatch
                        mismatch;

                mismatch.code =
                    pqnas::cluster::
                        StorageUserProjectionAuditMismatchCode::
                            Conflict;

                mismatch.fingerprint =
                    request.expected[0].fingerprint;

                mismatch.expected_revision =
                    request.expected[0].revision;

                mismatch.actual_revision =
                    request.expected[0].revision;

                result.result.mismatches.push_back(
                    std::move(mismatch)
                );

                return result;
            };

        pqnas::cluster::
            StorageUserProjectionReconciler
                conflict_reconciler(
                    conflict_config
                );

        sent.clear();

        const auto result =
            conflict_reconciler.reconcile_once();

        expect(
            !result.ok &&
            result.state_conflicts == 1 &&
            result.already_synced == 0 &&
            sent.empty(),
            "RT-008 equal-revision audit conflict fails closed without repair"
        );
    }

    // A Storage authorization unknown to current Director users is an orphan
    // signal. Never invent stored_revision+1 or perform destructive repair.
    {
        auto orphan_audit_config =
            config;

        orphan_audit_config.audit_emit = {};

        orphan_audit_config.list_audit_capable_nodes =
            []() {
                return std::vector<std::string>{
                    "storage-01",
                    "storage-empty"
                };
            };

        orphan_audit_config.audit_projection_range =
            [](const pqnas::cluster::
                    StorageUserProjectionAuditRequest& request) {
                if (request.node_id ==
                    "storage-01") {
                    return audit_match(
                        request
                    );
                }

                auto result =
                    audit_match(
                        request
                    );

                pqnas::cluster::
                    StorageUserProjectionAuditMismatch
                        mismatch;

                mismatch.code =
                    pqnas::cluster::
                        StorageUserProjectionAuditMismatchCode::
                            UnexpectedAuthorization;

                mismatch.fingerprint =
                    std::string(
                        128,
                        'c'
                    );

                mismatch.expected_revision = 0;
                mismatch.actual_revision = 9;

                result.result.mismatches.push_back(
                    std::move(mismatch)
                );

                return result;
            };

        pqnas::cluster::
            StorageUserProjectionReconciler
                orphan_audit_reconciler(
                    orphan_audit_config
                );

        sent.clear();

        const auto result =
            orphan_audit_reconciler.
                reconcile_once();

        expect(
            !result.ok &&
            result.orphaned_states == 1 &&
            result.already_synced == 0 &&
            sent.empty(),
            "RT-008 unexpected orphan authorization fails closed without mutation"
        );
    }

    // Projection semantic change on the same Node advances revision and sends
    // only the new current projection.
    {
        bool changed = false;

        expect(
            users.set_status_persisted(
                placed.fingerprint,
                "disabled",
                users_path.string(),
                &changed,
                &error
            ) &&
            changed,
            "status mutation persisted"
        );

        sent.clear();

        const auto result =
            reconciler.reconcile_once();

        const auto state =
            state_store.get(
                placed.fingerprint
            );

        expect(
            result.ok &&
            result.synced_current == 1 &&
            sent.size() == 1 &&
            sent[0].home_node ==
                "storage-01" &&
            !sent[0].enabled &&
            state.has_value() &&
            state->revision ==
                sent[0].revision &&
            audits.size() == 2 &&
            audits[1].event ==
                "cluster.storage_projection.synced" &&
            audits[1].outcome == "ok" &&
            audit_field_equals(
                audits[1],
                "fingerprint",
                placed.fingerprint
            ) &&
            audit_field_equals(
                audits[1],
                "previous_revision",
                std::to_string(
                    sent[0].revision - 1
                )
            ) &&
            audit_field_equals(
                audits[1],
                "projection_revision",
                std::to_string(sent[0].revision)
            ) &&
            audit_field_equals(
                audits[1],
                "previous_home_node",
                "storage-01"
            ) &&
            audit_field_equals(
                audits[1],
                "home_node",
                "storage-01"
            ),
            "same-Node newer revision synchronizes and audits current projection"
        );
    }

    // NEW-005 regression: a long reconciliation pass must not re-push a
    // superseded same-Node authorization from its initial UsersRegistry snapshot.
    //
    // The first network callback mutates whichever OTHER user has not yet been
    // sent. This makes the test independent of unordered_map iteration order.
    {
        const fs::path stale_users_path =
            directory /
            "new005_stale_users.json";

        const fs::path stale_state_path =
            directory /
            "new005_stale_sync_state.json";

        const fs::path stale_transition_path =
            directory /
            "new005_stale_transition_journal.json";

        pqnas::UsersRegistry
            stale_users;

        pqnas::cluster::
            StorageUserProjectionSyncStateStore
                stale_store;

        error.clear();

        expect(
            stale_store.load(
                stale_state_path.string(),
                &error
            ),
            "NEW-005 sync-state store loads: " +
                error
        );

        auto stale_transition_store =
            load_transition_store(
                stale_transition_path,
                &error
            );

        expect(
            stale_transition_store != nullptr,
            "NEW-005 transition journal loads: " +
                error
        );

        const auto stale_user_a =
            make_user(
                'c',
                "storage-01"
            );

        const auto stale_user_b =
            make_user(
                'd',
                "storage-01"
            );

        bool stale_projection_changed = false;

        error.clear();

        expect(
            stale_users.upsert_persisted(
                stale_user_a,
                stale_users_path.string(),
                &stale_projection_changed,
                &error
            ) &&
            stale_projection_changed,
            "NEW-005 user A persists"
        );

        stale_projection_changed = false;
        error.clear();

        expect(
            stale_users.upsert_persisted(
                stale_user_b,
                stale_users_path.string(),
                &stale_projection_changed,
                &error
            ) &&
            stale_projection_changed,
            "NEW-005 user B persists"
        );

        std::vector<
            pqnas::cluster::
                StorageUserProjection
        > stale_sent;

        pqnas::cluster::
            StorageUserProjectionReconcilerConfig
                stale_config;

        stale_config.users =
            &stale_users;

        stale_config.state_store =
            &stale_store;

        stale_config.transition_store =
            stale_transition_store.get();

        stale_config.sync_projection =
            [&](const pqnas::cluster::
                    StorageUserProjection& projection) {
                stale_sent.push_back(
                    projection
                );

                return sync_success(
                    projection
                );
            };

        pqnas::cluster::
            StorageUserProjectionReconciler
                stale_initial_reconciler(
                    stale_config
                );

        const auto stale_initial_result =
            stale_initial_reconciler.
                reconcile_once();

        expect(
            stale_initial_result.ok &&
            stale_initial_result.synced_current == 2 &&
            stale_sent.size() == 2,
            "NEW-005 fixture initially confirms both users"
        );

        bool stale_a_changed = false;
        bool stale_b_changed = false;

        error.clear();

        expect(
            stale_users.set_status_persisted(
                stale_user_a.fingerprint,
                "disabled",
                stale_users_path.string(),
                &stale_a_changed,
                &error
            ) &&
            stale_a_changed,
            "NEW-005 user A advances before stale pass"
        );

        error.clear();

        expect(
            stale_users.set_status_persisted(
                stale_user_b.fingerprint,
                "disabled",
                stale_users_path.string(),
                &stale_b_changed,
                &error
            ) &&
            stale_b_changed,
            "NEW-005 user B advances before stale pass"
        );

        stale_sent.clear();

        bool mutation_attempted = false;
        bool mutation_succeeded = false;
        std::string mutated_fingerprint;

        auto stale_race_config =
            stale_config;

        stale_race_config.sync_projection =
            [&](const pqnas::cluster::
                    StorageUserProjection& projection) {
                stale_sent.push_back(
                    projection
                );

                if (!mutation_attempted) {
                    mutation_attempted = true;

                    mutated_fingerprint =
                        projection.fingerprint ==
                                stale_user_a.fingerprint
                            ? stale_user_b.fingerprint
                            : stale_user_a.fingerprint;

                    bool changed = false;
                    std::string mutation_error;

                    mutation_succeeded =
                        stale_users.set_status_persisted(
                            mutated_fingerprint,
                            "enabled",
                            stale_users_path.string(),
                            &changed,
                            &mutation_error
                        ) &&
                        changed;
                }

                return sync_success(
                    projection
                );
            };

        pqnas::cluster::
            StorageUserProjectionReconciler
                stale_race_reconciler(
                    stale_race_config
                );

        const auto stale_result =
            stale_race_reconciler.
                reconcile_once();

        const auto authoritative_mutated_user =
            stale_users.get(
                mutated_fingerprint
            );

        const auto confirmed_mutated_state =
            stale_store.get(
                mutated_fingerprint
            );

        bool stale_projection_was_sent = false;

        for (const auto& projection :
             stale_sent) {
            if (projection.fingerprint ==
                mutated_fingerprint) {
                stale_projection_was_sent = true;
            }
        }

        expect(
            mutation_attempted &&
            mutation_succeeded &&
            !stale_result.ok &&
            stale_result.stale_states == 1 &&
            stale_result.failed == 1 &&
            stale_result.synced_current == 1 &&
            stale_sent.size() == 1 &&
            !stale_projection_was_sent &&
            authoritative_mutated_user.has_value() &&
            authoritative_mutated_user->status ==
                "enabled" &&
            confirmed_mutated_state.has_value() &&
            confirmed_mutated_state->revision <
                authoritative_mutated_user->
                    storage_projection_revision,
            "NEW-005 stale same-Node projection is skipped after authoritative revision advances"
        );

        // Converge both users so confirmed state again exactly matches the
        // authoritative Director revisions. This prepares the second NEW-005
        // branch: equal-revision legacy verification.
        stale_sent.clear();

        auto equal_converge_config =
            stale_config;

        equal_converge_config.sync_projection =
            [&](const pqnas::cluster::
                    StorageUserProjection& projection) {
                stale_sent.push_back(
                    projection
                );

                return sync_success(
                    projection
                );
            };

        pqnas::cluster::
            StorageUserProjectionReconciler
                equal_converge_reconciler(
                    equal_converge_config
                );

        const auto equal_converge_result =
            equal_converge_reconciler.
                reconcile_once();

        const auto converged_user_a =
            stale_users.get(
                stale_user_a.fingerprint
            );

        const auto converged_user_b =
            stale_users.get(
                stale_user_b.fingerprint
            );

        const auto converged_state_a =
            stale_store.get(
                stale_user_a.fingerprint
            );

        const auto converged_state_b =
            stale_store.get(
                stale_user_b.fingerprint
            );

        expect(
            equal_converge_result.ok &&
            stale_sent.size() == 2 &&
            converged_user_a.has_value() &&
            converged_user_b.has_value() &&
            converged_state_a.has_value() &&
            converged_state_b.has_value() &&
            converged_state_a->revision ==
                converged_user_a->
                    storage_projection_revision &&
            converged_state_b->revision ==
                converged_user_b->
                    storage_projection_revision,
            "NEW-005 fixture reconverges before equal-revision stale race"
        );

        // NEW-005 second affected branch: both users enter the pass with
        // confirmed == current. The first legacy verification POST mutates the
        // OTHER authoritative user. Its stale equal-revision snapshot must be
        // rejected before a second POST can leave the Director.
        stale_sent.clear();

        bool equal_mutation_attempted = false;
        bool equal_mutation_succeeded = false;
        std::string equal_mutated_fingerprint;

        auto equal_race_config =
            stale_config;

        equal_race_config.sync_projection =
            [&](const pqnas::cluster::
                    StorageUserProjection& projection) {
                stale_sent.push_back(
                    projection
                );

                if (!equal_mutation_attempted) {
                    equal_mutation_attempted = true;

                    equal_mutated_fingerprint =
                        projection.fingerprint ==
                                stale_user_a.fingerprint
                            ? stale_user_b.fingerprint
                            : stale_user_a.fingerprint;

                    const auto target =
                        stale_users.get(
                            equal_mutated_fingerprint
                        );

                    bool changed = false;
                    std::string mutation_error;

                    if (target.has_value()) {
                        const std::string next_status =
                            target->status == "enabled"
                                ? "disabled"
                                : "enabled";

                        equal_mutation_succeeded =
                            stale_users.set_status_persisted(
                                equal_mutated_fingerprint,
                                next_status,
                                stale_users_path.string(),
                                &changed,
                                &mutation_error
                            ) &&
                            changed;
                    }
                }

                return sync_success(
                    projection
                );
            };

        pqnas::cluster::
            StorageUserProjectionReconciler
                equal_race_reconciler(
                    equal_race_config
                );

        const auto equal_race_result =
            equal_race_reconciler.
                reconcile_once();

        const auto equal_authoritative_user =
            stale_users.get(
                equal_mutated_fingerprint
            );

        const auto equal_confirmed_state =
            stale_store.get(
                equal_mutated_fingerprint
            );

        bool equal_stale_projection_was_sent = false;

        for (const auto& projection :
             stale_sent) {
            if (projection.fingerprint ==
                equal_mutated_fingerprint) {
                equal_stale_projection_was_sent = true;
            }
        }

        expect(
            equal_mutation_attempted &&
            equal_mutation_succeeded &&
            !equal_race_result.ok &&
            equal_race_result.stale_states == 1 &&
            equal_race_result.failed == 1 &&
            equal_race_result.already_synced == 1 &&
            stale_sent.size() == 1 &&
            !equal_stale_projection_was_sent &&
            equal_authoritative_user.has_value() &&
            equal_confirmed_state.has_value() &&
            equal_confirmed_state->revision <
                equal_authoritative_user->
                    storage_projection_revision,
            "NEW-005 stale equal-revision verification is skipped after authoritative revision advances"
        );

        // Reconverge after the equal-revision race so the audit-capable pass
        // begins with confirmed == authoritative for both fixture users.
        stale_sent.clear();

        auto audit_reconverge_config =
            stale_config;

        audit_reconverge_config.sync_projection =
            [&](const pqnas::cluster::
                    StorageUserProjection& projection) {
                stale_sent.push_back(
                    projection
                );

                return sync_success(
                    projection
                );
            };

        pqnas::cluster::
            StorageUserProjectionReconciler
                audit_reconverge_reconciler(
                    audit_reconverge_config
                );

        const auto audit_reconverge_result =
            audit_reconverge_reconciler.
                reconcile_once();

        const auto audit_ready_user_a =
            stale_users.get(
                stale_user_a.fingerprint
            );

        const auto audit_ready_user_b =
            stale_users.get(
                stale_user_b.fingerprint
            );

        const auto audit_ready_state_a =
            stale_store.get(
                stale_user_a.fingerprint
            );

        const auto audit_ready_state_b =
            stale_store.get(
                stale_user_b.fingerprint
            );

        expect(
            audit_reconverge_result.ok &&
            audit_ready_user_a.has_value() &&
            audit_ready_user_b.has_value() &&
            audit_ready_state_a.has_value() &&
            audit_ready_state_b.has_value() &&
            audit_ready_state_a->revision ==
                audit_ready_user_a->
                    storage_projection_revision &&
            audit_ready_state_b->revision ==
                audit_ready_user_b->
                    storage_projection_revision,
            "NEW-005 fixture reconverges before stale audit repair race"
        );

        stale_sent.clear();

        bool audit_mutation_attempted = false;
        bool audit_mutation_succeeded = false;
        bool audit_target_found = false;

        auto stale_audit_config =
            stale_config;

        stale_audit_config.list_audit_capable_nodes =
            []() {
                return std::vector<std::string>{
                    "storage-01"
                };
            };

        stale_audit_config.audit_projection_range =
            [&](const pqnas::cluster::
                    StorageUserProjectionAuditRequest& request) {
                auto result =
                    audit_match(
                        request
                    );

                std::uint64_t expected_revision = 0;

                for (const auto& expected :
                     request.expected) {
                    if (expected.fingerprint ==
                        stale_user_a.fingerprint) {
                        audit_target_found = true;
                        expected_revision =
                            expected.revision;
                        break;
                    }
                }

                if (audit_target_found &&
                    !audit_mutation_attempted) {
                    audit_mutation_attempted = true;

                    const auto target =
                        stale_users.get(
                            stale_user_a.fingerprint
                        );

                    bool changed = false;
                    std::string mutation_error;

                    if (target.has_value()) {
                        const std::string next_status =
                            target->status == "enabled"
                                ? "disabled"
                                : "enabled";

                        audit_mutation_succeeded =
                            stale_users.set_status_persisted(
                                stale_user_a.fingerprint,
                                next_status,
                                stale_users_path.string(),
                                &changed,
                                &mutation_error
                            ) &&
                            changed;
                    }
                }

                if (audit_target_found) {
                    if (result.result.matched > 0) {
                        --result.result.matched;
                    }

                    pqnas::cluster::
                        StorageUserProjectionAuditMismatch
                            mismatch;

                    mismatch.code =
                        pqnas::cluster::
                            StorageUserProjectionAuditMismatchCode::
                                Missing;

                    mismatch.fingerprint =
                        stale_user_a.fingerprint;

                    mismatch.expected_revision =
                        expected_revision;

                    mismatch.actual_revision = 0;

                    result.result.mismatches.push_back(
                        std::move(mismatch)
                    );
                }

                return result;
            };

        stale_audit_config.sync_projection =
            [&](const pqnas::cluster::
                    StorageUserProjection& projection) {
                stale_sent.push_back(
                    projection
                );

                return sync_success(
                    projection
                );
            };

        pqnas::cluster::
            StorageUserProjectionReconciler
                stale_audit_reconciler(
                    stale_audit_config
                );

        const auto stale_audit_result =
            stale_audit_reconciler.
                reconcile_once();

        const auto audit_authoritative_user =
            stale_users.get(
                stale_user_a.fingerprint
            );

        const auto audit_confirmed_state =
            stale_store.get(
                stale_user_a.fingerprint
            );

        expect(
            audit_target_found &&
            audit_mutation_attempted &&
            audit_mutation_succeeded &&
            !stale_audit_result.ok &&
            stale_audit_result.stale_states == 1 &&
            stale_audit_result.failed == 1 &&
            stale_sent.empty() &&
            audit_authoritative_user.has_value() &&
            audit_confirmed_state.has_value() &&
            audit_confirmed_state->revision <
                audit_authoritative_user->
                    storage_projection_revision,
            "NEW-005 stale audit repair is skipped after authoritative revision advances"
        );
    }

    const auto before_move_state =
        state_store.get(
            placed.fingerprint
        );

    expect(
        before_move_state.has_value(),
        "pre-move state exists"
    );

    // Placement change: failed old-Node revoke blocks the new Node and does not
    // advance the persistent confirmed state.
    {
        bool changed = false;

        expect(
            users.set_home_node_persisted(
                placed.fingerprint,
                "storage-02",
                users_path.string(),
                &changed,
                &error
            ) &&
            changed,
            "placement mutation persisted"
        );

        sent.clear();
        fail_next = true;

        const auto result =
            reconciler.reconcile_once();

        const auto state =
            state_store.get(
                placed.fingerprint
            );

        expect(
            !result.ok &&
            result.failed == 1 &&
            sent.size() == 1 &&
            sent[0].home_node ==
                "storage-01" &&
            !sent[0].enabled &&
            !sent[0].storage_allocated &&
            state.has_value() &&
            before_move_state.has_value() &&
            state->home_node ==
                before_move_state->home_node &&
            state->revision ==
                before_move_state->revision &&
            audits.size() == 2,
            "failed previous-Node revoke blocks move, preserves state and emits no success audit"
        );
    }

    // Retry: previous Node tombstone first, then current Node projection, then
    // persistent state advances to the new Node/revision.
    {
        sent.clear();

        const auto result =
            reconciler.reconcile_once();

        const auto state =
            state_store.get(
                placed.fingerprint
            );

        expect(
            result.ok &&
            result.moved == 1 &&
            sent.size() == 2 &&
            sent[0].home_node ==
                "storage-01" &&
            !sent[0].enabled &&
            !sent[0].storage_allocated &&
            sent[1].home_node ==
                "storage-02" &&
            sent[0].revision ==
                sent[1].revision &&
            state.has_value() &&
            state->home_node ==
                "storage-02" &&
            state->revision ==
                sent[1].revision &&
            before_move_state.has_value() &&
            audits.size() == 3 &&
            audits[2].event ==
                "cluster.storage_projection.moved" &&
            audits[2].outcome == "ok" &&
            audit_field_equals(
                audits[2],
                "fingerprint",
                placed.fingerprint
            ) &&
            audit_field_equals(
                audits[2],
                "previous_revision",
                std::to_string(
                    before_move_state->revision
                )
            ) &&
            audit_field_equals(
                audits[2],
                "projection_revision",
                std::to_string(sent[1].revision)
            ) &&
            audit_field_equals(
                audits[2],
                "previous_home_node",
                "storage-01"
            ) &&
            audit_field_equals(
                audits[2],
                "home_node",
                "storage-02"
            ),
            "placement retry revokes old Node, synchronizes new Node and audits move"
        );
    }

    // Persistent state newer than Director is a fail-closed rollback signal.
    {
        const auto current =
            state_store.get(
                placed.fingerprint
            );

        expect(
            current.has_value(),
            "current state available for stale-state test"
        );

        if (current.has_value()) {
            pqnas::cluster::
                StorageUserProjectionSyncState
                    future = *current;

            future.revision += 10;

            expect(
                state_store.mark_synced(
                    future,
                    &error
                ) ==
                    pqnas::cluster::
                        StorageUserProjectionSyncStateStoreResult::
                            Applied,
                "future sync-state fixture persisted"
            );
        }

        sent.clear();

        const auto result =
            reconciler.reconcile_once();

        expect(
            !result.ok &&
            result.stale_states == 1 &&
            sent.empty(),
            "newer confirmed state prevents Director revision rollback"
        );
    }

    // Same revision on a different Node is an invariant conflict and must not
    // trigger network traffic.
    {
        const fs::path conflict_state_path =
            directory /
            "conflict_state.json";

        pqnas::cluster::
            StorageUserProjectionSyncStateStore
                conflict_store;

        expect(
            conflict_store.load(
                conflict_state_path.string(),
                &error
            ),
            "conflict state store loads"
        );

        const auto conflict_user =
            make_user(
                'c',
                "storage-02"
            );

        pqnas::UsersRegistry
            conflict_users;

        const fs::path conflict_users_path =
            directory /
            "conflict_users.json";

        expect(
            conflict_users.upsert_persisted(
                conflict_user,
                conflict_users_path.string(),
                &projection_changed,
                &error
            ),
            "conflict user persisted"
        );

        const auto persisted_conflict_user =
            conflict_users.get(
                conflict_user.fingerprint
            );

        expect(
            persisted_conflict_user.has_value(),
            "conflict user readable"
        );

        if (persisted_conflict_user.has_value()) {
            pqnas::cluster::
                StorageUserProjectionSyncState
                    conflicting_state;

            conflicting_state.fingerprint =
                conflict_user.fingerprint;

            conflicting_state.home_node =
                "storage-01";

            conflicting_state.revision =
                persisted_conflict_user->
                    storage_projection_revision;

            expect(
                conflict_store.mark_synced(
                    conflicting_state,
                    &error
                ) ==
                    pqnas::cluster::
                        StorageUserProjectionSyncStateStoreResult::
                            Applied,
                "same-revision different-Node fixture persisted"
            );
        }

        std::size_t network_calls = 0;

        auto conflict_transition_store =
            load_transition_store(
                directory /
                    "conflict_transition_journal.json",
                &error
            );

        expect(
            conflict_transition_store != nullptr,
            "conflict transition journal loads: " +
                error
        );

        pqnas::cluster::
            StorageUserProjectionReconcilerConfig
                conflict_config;

        conflict_config.users =
            &conflict_users;

        conflict_config.state_store =
            &conflict_store;

        conflict_config.transition_store =
            conflict_transition_store.get();

        conflict_config.sync_projection =
            [&](const pqnas::cluster::
                    StorageUserProjection& projection) {
                ++network_calls;

                return sync_success(
                    projection
                );
            };

        pqnas::cluster::
            StorageUserProjectionReconciler
                conflict_reconciler(
                    conflict_config
                );

        const auto result =
            conflict_reconciler.reconcile_once();

        expect(
            !result.ok &&
            result.state_conflicts == 1 &&
            network_calls == 0,
            "same revision cannot silently migrate between Nodes"
        );
    }

    // RT009 regression: pending B at the authoritative revision does not
    // override conflicting confirmed A at that same revision. The conflict
    // must fail closed before any live authorization reaches B.
    {
        const fs::path pending_conflict_users_path =
            directory /
            "pending_conflict_users.json";

        const fs::path pending_conflict_state_path =
            directory /
            "pending_conflict_state.json";

        const fs::path pending_conflict_transition_path =
            directory /
            "pending_conflict_transition_journal.json";

        pqnas::UsersRegistry
            pending_conflict_users;

        pqnas::cluster::
            StorageUserProjectionSyncStateStore
                pending_conflict_store;

        error.clear();

        expect(
            pending_conflict_store.load(
                pending_conflict_state_path.string(),
                &error
            ),
            "pending-conflict confirmed state loads: " +
                error
        );

        const auto pending_conflict_user =
            make_user(
                'd',
                "storage-02"
            );

        bool pending_conflict_changed = false;

        error.clear();

        expect(
            pending_conflict_users.upsert_persisted(
                pending_conflict_user,
                pending_conflict_users_path.string(),
                &pending_conflict_changed,
                &error
            ) &&
            pending_conflict_changed,
            "pending-conflict Director user persists"
        );

        const auto persisted_pending_conflict_user =
            pending_conflict_users.get(
                pending_conflict_user.fingerprint
            );

        expect(
            persisted_pending_conflict_user.has_value(),
            "pending-conflict Director user reloads"
        );

        pqnas::cluster::
            StorageUserProjectionSyncState
                conflicting_confirmed;

        if (persisted_pending_conflict_user.has_value()) {
            conflicting_confirmed.fingerprint =
                pending_conflict_user.fingerprint;

            conflicting_confirmed.home_node =
                "storage-01";

            conflicting_confirmed.revision =
                persisted_pending_conflict_user->
                    storage_projection_revision;
        }

        error.clear();

        expect(
            pending_conflict_store.mark_synced(
                conflicting_confirmed,
                &error
            ) ==
                pqnas::cluster::
                    StorageUserProjectionSyncStateStoreResult::
                        Applied,
            "pending-conflict confirmed A persists: " +
                error
        );

        auto pending_conflict_transition_store =
            load_transition_store(
                pending_conflict_transition_path,
                &error
            );

        expect(
            pending_conflict_transition_store != nullptr,
            "pending-conflict journal loads: " +
                error
        );

        pqnas::cluster::
            StorageUserProjectionTransitionRecord
                conflicting_pending;

        if (persisted_pending_conflict_user.has_value()) {
            conflicting_pending.fingerprint =
                pending_conflict_user.fingerprint;

            conflicting_pending.home_node =
                "storage-02";

            conflicting_pending.revision =
                persisted_pending_conflict_user->
                    storage_projection_revision;
        }

        error.clear();

        expect(
            pending_conflict_transition_store->
                mark_pending(
                    conflicting_pending,
                    &error
                ) ==
                    pqnas::cluster::
                        StorageUserProjectionSyncStateStoreResult::
                            Applied,
            "pending-conflict pending B persists: " +
                error
        );

        std::size_t network_calls = 0;

        pqnas::cluster::
            StorageUserProjectionReconcilerConfig
                pending_conflict_config;

        pending_conflict_config.users =
            &pending_conflict_users;

        pending_conflict_config.state_store =
            &pending_conflict_store;

        pending_conflict_config.transition_store =
            pending_conflict_transition_store.get();

        pending_conflict_config.sync_projection =
            [&](const pqnas::cluster::
                    StorageUserProjection& projection) {
                ++network_calls;

                return sync_success(
                    projection
                );
            };

        pqnas::cluster::
            StorageUserProjectionReconciler
                pending_conflict_reconciler(
                    pending_conflict_config
                );

        const auto result =
            pending_conflict_reconciler.
                reconcile_once();

        const auto confirmed_after =
            pending_conflict_store.get(
                pending_conflict_user.fingerprint
            );

        const auto pending_after =
            pending_conflict_transition_store->get(
                pending_conflict_user.fingerprint
            );

        expect(
            !result.ok &&
            result.state_conflicts == 1 &&
            result.failed == 1 &&
            network_calls == 0 &&
            confirmed_after.has_value() &&
            confirmed_after->home_node ==
                "storage-01" &&
            pending_after.has_value() &&
            pending_after->home_node ==
                "storage-02" &&
            persisted_pending_conflict_user.has_value() &&
            confirmed_after->revision ==
                persisted_pending_conflict_user->
                    storage_projection_revision &&
            pending_after->revision ==
                persisted_pending_conflict_user->
                    storage_projection_revision,
            "equal-revision confirmed A plus pending B fails before network"
        );
    }


    // Explicit unplacement revokes the last confirmed Node at the newer
    // Director revision before removing persistent confirmed state.
    {
        const fs::path unplace_state_path =
            directory /
            "unplace_state.json";

        const fs::path unplace_users_path =
            directory /
            "unplace_users.json";

        pqnas::cluster::
            StorageUserProjectionSyncStateStore
                unplace_store;

        expect(
            unplace_store.load(
                unplace_state_path.string(),
                &error
            ),
            "unplacement sync-state store loads"
        );

        pqnas::UsersRegistry
            unplace_users;

        const auto unplace_user =
            make_user(
                'e',
                "storage-01"
            );

        bool local_projection_changed = false;

        expect(
            unplace_users.upsert_persisted(
                unplace_user,
                unplace_users_path.string(),
                &local_projection_changed,
                &error
            ) &&
            local_projection_changed,
            "unplacement fixture user persisted"
        );

        std::vector<
            pqnas::cluster::
                StorageUserProjection
        > unplace_sent;

        bool unplace_fail_next = false;

        std::vector<AuditRecord>
            unplace_audits;

        auto unplace_transition_store =
            load_transition_store(
                directory /
                    "unplace_transition_journal.json",
                &error
            );

        expect(
            unplace_transition_store != nullptr,
            "unplacement transition journal loads: " +
                error
        );

        pqnas::cluster::
            StorageUserProjectionReconcilerConfig
                unplace_config;

        unplace_config.users =
            &unplace_users;

        unplace_config.state_store =
            &unplace_store;

        unplace_config.transition_store =
            unplace_transition_store.get();

        unplace_config.audit_emit =
            [&](const std::string& event,
                const std::string& outcome,
                const std::map<std::string, std::string>& fields) {
                unplace_audits.push_back(
                    AuditRecord{
                        event,
                        outcome,
                        fields
                    }
                );
            };

        unplace_config.sync_projection =
            [&](const pqnas::cluster::
                    StorageUserProjection& projection) {
                unplace_sent.push_back(
                    projection
                );

                if (unplace_fail_next) {
                    unplace_fail_next = false;

                    pqnas::cluster::
                        StorageUserProjectionSyncResult
                            failed;

                    failed.code =
                        pqnas::cluster::
                            StorageUserProjectionSyncClientCode::
                                TransportFailed;

                    return failed;
                }

                return sync_success(
                    projection
                );
            };

        pqnas::cluster::
            StorageUserProjectionReconciler
                unplace_reconciler(
                    unplace_config
                );

        unplace_sent.clear();

        const auto initial_result =
            unplace_reconciler.reconcile_once();

        const auto before_unplace_user =
            unplace_users.get(
                unplace_user.fingerprint
            );

        const auto before_unplace_state =
            unplace_store.get(
                unplace_user.fingerprint
            );

        expect(
            initial_result.ok &&
            initial_result.synced_current == 1 &&
            unplace_sent.size() == 1 &&
            before_unplace_user.has_value() &&
            before_unplace_state.has_value() &&
            before_unplace_state->home_node ==
                "storage-01" &&
            before_unplace_state->revision ==
                before_unplace_user->
                    storage_projection_revision &&
            unplace_audits.size() == 1 &&
            unplace_audits[0].event ==
                "cluster.storage_projection.synced" &&
            audit_field_equals(
                unplace_audits[0],
                "previous_revision",
                "0"
            ) &&
            audit_field_equals(
                unplace_audits[0],
                "projection_revision",
                std::to_string(
                    before_unplace_state->revision
                )
            ) &&
            audit_field_equals(
                unplace_audits[0],
                "home_node",
                "storage-01"
            ),
            "unplacement fixture first confirms and audits placed projection"
        );

        bool clear_changed = false;
        bool clear_expectation_matched = false;

        expect(
            unplace_users.clear_home_node_persisted_if_current(
                unplace_user.fingerprint,
                "storage-01",
                "",
                unplace_users_path.string(),
                &clear_changed,
                &clear_expectation_matched,
                &error
            ) &&
            clear_changed &&
            clear_expectation_matched,
            "explicit unplacement Director commit persists"
        );

        const auto after_unplace_user =
            unplace_users.get(
                unplace_user.fingerprint
            );

        expect(
            after_unplace_user.has_value() &&
            after_unplace_user->home_node.empty() &&
            before_unplace_user.has_value() &&
            after_unplace_user->
                storage_projection_revision ==
                    before_unplace_user->
                        storage_projection_revision + 1,
            "explicit unplacement advances Director projection revision"
        );

        // First revoke attempt fails. The old confirmed state must remain so a
        // later reconciliation still knows exactly which Node to revoke.
        unplace_sent.clear();
        unplace_fail_next = true;

        const auto failed_revoke =
            unplace_reconciler.reconcile_once();

        const auto after_failed_revoke_state =
            unplace_store.get(
                unplace_user.fingerprint
            );

        expect(
            !failed_revoke.ok &&
            failed_revoke.failed == 1 &&
            failed_revoke.revoked_unplaced == 0 &&
            unplace_sent.size() == 1 &&
            unplace_sent[0].home_node ==
                "storage-01" &&
            !unplace_sent[0].enabled &&
            !unplace_sent[0].storage_allocated &&
            unplace_sent[0].quota_bytes == 0 &&
            unplace_sent[0].root_rel.empty() &&
            unplace_sent[0].storage_pool_id.empty() &&
            after_unplace_user.has_value() &&
            unplace_sent[0].revision ==
                after_unplace_user->
                    storage_projection_revision &&
            after_failed_revoke_state.has_value() &&
            before_unplace_state.has_value() &&
            after_failed_revoke_state->home_node ==
                before_unplace_state->home_node &&
            after_failed_revoke_state->revision ==
                before_unplace_state->revision &&
            unplace_audits.size() == 1,
            "failed unplacement revoke retains last confirmed Node and emits no success audit"
        );

        // Retry accepts the same newer tombstone, then CAS-erases exactly the
        // old confirmed Node/revision.
        unplace_sent.clear();

        const auto successful_revoke =
            unplace_reconciler.reconcile_once();

        const auto after_success_state =
            unplace_store.get(
                unplace_user.fingerprint
            );

        expect(
            successful_revoke.ok &&
            successful_revoke.revoked_unplaced == 1 &&
            successful_revoke.failed == 0 &&
            unplace_sent.size() == 1 &&
            unplace_sent[0].home_node ==
                "storage-01" &&
            !unplace_sent[0].enabled &&
            !unplace_sent[0].storage_allocated &&
            after_unplace_user.has_value() &&
            unplace_sent[0].revision ==
                after_unplace_user->
                    storage_projection_revision &&
            !after_success_state.has_value() &&
            before_unplace_state.has_value() &&
            unplace_audits.size() == 2 &&
            unplace_audits[1].event ==
                "cluster.storage_projection.revoked" &&
            unplace_audits[1].outcome == "ok" &&
            audit_field_equals(
                unplace_audits[1],
                "fingerprint",
                unplace_user.fingerprint
            ) &&
            audit_field_equals(
                unplace_audits[1],
                "previous_revision",
                std::to_string(
                    before_unplace_state->revision
                )
            ) &&
            audit_field_equals(
                unplace_audits[1],
                "projection_revision",
                std::to_string(
                    after_unplace_user->
                        storage_projection_revision
                )
            ) &&
            audit_field_equals(
                unplace_audits[1],
                "previous_home_node",
                "storage-01"
            ) &&
            audit_field_equals(
                unplace_audits[1],
                "home_node",
                ""
            ),
            "successful unplacement revokes old Node, erases confirmed state and audits revoke"
        );

        // Once revoke + durable erase are complete, an unplaced user is a
        // stable no-network state.
        unplace_sent.clear();

        const auto steady_unplaced =
            unplace_reconciler.reconcile_once();

        expect(
            steady_unplaced.ok &&
            steady_unplaced.no_placement == 1 &&
            steady_unplaced.revoked_unplaced == 0 &&
            unplace_sent.empty() &&
            unplace_audits.size() == 2,
            "reconciled unplaced user remains a network and audit no-op"
        );
    }

    // Security/restart invariant: if the previous Node accepted its
    // tombstone but synchronization to the new Node failed, a Director restart
    // must reload the last confirmed Node and safely retry revoke-before-move.
    {
        const fs::path restart_move_users_path =
            directory /
            "restart_move_users.json";

        const fs::path restart_move_state_path =
            directory /
            "restart_move_state.json";

        const fs::path restart_move_transition_path =
            directory /
            "restart_move_transition_journal.json";

        pqnas::UsersRegistry
            before_restart_users;

        pqnas::cluster::
            StorageUserProjectionSyncStateStore
                before_restart_store;

        expect(
            before_restart_store.load(
                restart_move_state_path.string(),
                &error
            ),
            "restart move state store loads"
        );

        const auto restart_move_user =
            make_user(
                '1',
                "storage-01"
            );

        bool restart_move_projection_changed = false;

        expect(
            before_restart_users.upsert_persisted(
                restart_move_user,
                restart_move_users_path.string(),
                &restart_move_projection_changed,
                &error
            ) &&
            restart_move_projection_changed,
            "restart move fixture user persists"
        );

        std::vector<
            pqnas::cluster::
                StorageUserProjection
        > before_restart_sent;

        bool fail_new_node_sync = false;

        auto before_restart_transition_store =
            load_transition_store(
                restart_move_transition_path,
                &error
            );

        expect(
            before_restart_transition_store != nullptr,
            "restart move transition journal loads: " +
                error
        );

        pqnas::cluster::
            StorageUserProjectionReconcilerConfig
                before_restart_config;

        before_restart_config.users =
            &before_restart_users;

        before_restart_config.state_store =
            &before_restart_store;

        before_restart_config.transition_store =
            before_restart_transition_store.get();

        before_restart_config.sync_projection =
            [&](const pqnas::cluster::
                    StorageUserProjection& projection) {
                before_restart_sent.push_back(
                    projection
                );

                if (fail_new_node_sync &&
                    projection.home_node ==
                        "storage-02" &&
                    projection.enabled) {
                    pqnas::cluster::
                        StorageUserProjectionSyncResult
                            failed;

                    failed.code =
                        pqnas::cluster::
                            StorageUserProjectionSyncClientCode::
                                TransportFailed;

                    return failed;
                }

                return sync_success(
                    projection
                );
            };

        pqnas::cluster::
            StorageUserProjectionReconciler
                before_restart_reconciler(
                    before_restart_config
                );

        expect(
            before_restart_reconciler.
                reconcile_once().ok,
            "restart move fixture initially synchronizes"
        );

        const auto confirmed_before_move =
            before_restart_store.get(
                restart_move_user.fingerprint
            );

        bool move_changed = false;

        expect(
            before_restart_users.set_home_node_persisted(
                restart_move_user.fingerprint,
                "storage-02",
                restart_move_users_path.string(),
                &move_changed,
                &error
            ) &&
            move_changed,
            "restart move Director commit persists"
        );

        before_restart_sent.clear();
        fail_new_node_sync = true;

        const auto partial_move =
            before_restart_reconciler.
                reconcile_once();

        const auto state_after_partial_move =
            before_restart_store.get(
                restart_move_user.fingerprint
            );

        const auto transition_after_partial_move =
            before_restart_transition_store->get(
                restart_move_user.fingerprint
            );

        expect(
            !partial_move.ok &&
            partial_move.failed == 1 &&
            partial_move.moved == 0 &&
            before_restart_sent.size() == 2 &&
            before_restart_sent[0].home_node ==
                "storage-01" &&
            !before_restart_sent[0].enabled &&
            !before_restart_sent[0].storage_allocated &&
            before_restart_sent[1].home_node ==
                "storage-02" &&
            confirmed_before_move.has_value() &&
            state_after_partial_move.has_value() &&
            state_after_partial_move->home_node ==
                confirmed_before_move->home_node &&
            state_after_partial_move->revision ==
                confirmed_before_move->revision &&
            transition_after_partial_move.has_value() &&
            transition_after_partial_move->home_node ==
                "storage-02" &&
            transition_after_partial_move->revision ==
                before_restart_sent[1].revision,
            "failed new-Node sync preserves confirmed A plus durable pending B"
        );

        // Simulate process loss: reconstruct both authoritative stores solely
        // from their durable on-disk state.
        pqnas::UsersRegistry
            restarted_users;

        expect(
            restarted_users.load(
                restart_move_users_path.string()
            ),
            "restart move users reload"
        );

        pqnas::cluster::
            StorageUserProjectionSyncStateStore
                restarted_store;

        expect(
            restarted_store.load(
                restart_move_state_path.string(),
                &error
            ),
            "restart move sync-state reload"
        );

        auto restarted_transition_store =
            load_transition_store(
                restart_move_transition_path,
                &error
            );

        expect(
            restarted_transition_store != nullptr,
            "restart move transition journal reloads: " +
                error
        );

        const auto restarted_user =
            restarted_users.get(
                restart_move_user.fingerprint
            );

        const auto restarted_state =
            restarted_store.get(
                restart_move_user.fingerprint
            );

        const auto restarted_transition =
            restarted_transition_store->get(
                restart_move_user.fingerprint
            );

        expect(
            restarted_user.has_value() &&
            restarted_user->home_node ==
                "storage-02" &&
            restarted_state.has_value() &&
            restarted_state->home_node ==
                "storage-01" &&
            restarted_state->revision <
                restarted_user->
                    storage_projection_revision &&
            restarted_transition.has_value() &&
            restarted_transition->home_node ==
                "storage-02" &&
            restarted_transition->revision ==
                restarted_user->
                    storage_projection_revision,
            "restart reload retains confirmed A plus pending B"
        );

        std::vector<
            pqnas::cluster::
                StorageUserProjection
        > after_restart_sent;

        pqnas::cluster::
            StorageUserProjectionReconcilerConfig
                after_restart_config;

        after_restart_config.users =
            &restarted_users;

        after_restart_config.state_store =
            &restarted_store;

        after_restart_config.transition_store =
            restarted_transition_store.get();

        after_restart_config.sync_projection =
            [&](const pqnas::cluster::
                    StorageUserProjection& projection) {
                after_restart_sent.push_back(
                    projection
                );

                return sync_success(
                    projection
                );
            };

        pqnas::cluster::
            StorageUserProjectionReconciler
                after_restart_reconciler(
                    after_restart_config
                );

        const auto completed_move =
            after_restart_reconciler.
                reconcile_once();

        const auto final_move_state =
            restarted_store.get(
                restart_move_user.fingerprint
            );

        expect(
            completed_move.ok &&
            completed_move.moved == 1 &&
            after_restart_sent.size() == 1 &&
            after_restart_sent[0].home_node ==
                "storage-02" &&
            after_restart_sent[0].enabled &&
            restarted_user.has_value() &&
            after_restart_sent[0].revision ==
                restarted_user->
                    storage_projection_revision &&
            final_move_state.has_value() &&
            final_move_state->home_node ==
                "storage-02" &&
            final_move_state->revision ==
                restarted_user->
                    storage_projection_revision &&
            !restarted_transition_store->get(
                restart_move_user.fingerprint
            ).has_value(),
            "restart restores pending B, confirms it and clears the journal"
        );
    }


    // RT009 regression: B accepts the new live projection, but Director
    // confirmed-state persistence fails afterwards. The durable pending
    // journal must remember B across restart so the intermediate authorization
    // target cannot be stranded or forgotten.
    {
        const fs::path persist_fail_users_path =
            directory /
            "persist_fail_move_users.json";

        const fs::path persist_fail_state_path =
            directory /
            "persist_fail_move_state.json";

        const fs::path persist_fail_transition_path =
            directory /
            "persist_fail_move_transition_journal.json";

        pqnas::UsersRegistry
            before_restart_users;

        pqnas::cluster::
            StorageUserProjectionSyncStateStore
                before_restart_store;

        error.clear();

        expect(
            before_restart_store.load(
                persist_fail_state_path.string(),
                &error
            ),
            "RT009 confirmed state store loads: " +
                error
        );

        auto before_restart_transition_store =
            load_transition_store(
                persist_fail_transition_path,
                &error
            );

        expect(
            before_restart_transition_store != nullptr,
            "RT009 transition journal loads: " +
                error
        );

        const auto persist_fail_user =
            make_user(
                '9',
                "storage-01"
            );

        bool projection_changed = false;

        error.clear();

        expect(
            before_restart_users.upsert_persisted(
                persist_fail_user,
                persist_fail_users_path.string(),
                &projection_changed,
                &error
            ) &&
            projection_changed,
            "RT009 initial Director user persists"
        );

        std::vector<
            pqnas::cluster::
                StorageUserProjection
        > before_restart_sent;

        pqnas::cluster::
            StorageUserProjectionReconcilerConfig
                before_restart_config;

        before_restart_config.users =
            &before_restart_users;

        before_restart_config.state_store =
            &before_restart_store;

        before_restart_config.transition_store =
            before_restart_transition_store.get();

        before_restart_config.sync_projection =
            [&](const pqnas::cluster::
                    StorageUserProjection& projection) {
                before_restart_sent.push_back(
                    projection
                );

                return sync_success(
                    projection
                );
            };

        pqnas::cluster::
            StorageUserProjectionReconciler
                before_restart_reconciler(
                    before_restart_config
                );

        const auto initial_result =
            before_restart_reconciler.
                reconcile_once();

        const auto confirmed_before_move =
            before_restart_store.get(
                persist_fail_user.fingerprint
            );

        expect(
            initial_result.ok &&
            initial_result.synced_current == 1 &&
            confirmed_before_move.has_value() &&
            confirmed_before_move->home_node ==
                "storage-01" &&
            !before_restart_transition_store->get(
                persist_fail_user.fingerprint
            ).has_value(),
            "RT009 fixture starts with confirmed A and no pending journal"
        );

        bool move_changed = false;

        error.clear();

        expect(
            before_restart_users.set_home_node_persisted(
                persist_fail_user.fingerprint,
                "storage-02",
                persist_fail_users_path.string(),
                &move_changed,
                &error
            ) &&
            move_changed,
            "RT009 A-to-B Director placement persists"
        );

        // Security test injection: make only the confirmed-state target fail
        // private-file validation. The separate pending journal remains valid.
        const bool made_confirmed_target_unsafe =
            ::chmod(
                persist_fail_state_path.c_str(),
                0644
            ) == 0;

        expect(
            made_confirmed_target_unsafe,
            "RT009 confirmed target made unsafe for persistence injection"
        );

        before_restart_sent.clear();

        const auto failed_confirm =
            before_restart_reconciler.
                reconcile_once();

        const auto confirmed_after_failure =
            before_restart_store.get(
                persist_fail_user.fingerprint
            );

        const auto pending_after_failure =
            before_restart_transition_store->get(
                persist_fail_user.fingerprint
            );

        // Restore private permissions before simulating process restart so the
        // persisted old confirmed snapshot can be loaded normally.
        const bool restored_confirmed_target =
            ::chmod(
                persist_fail_state_path.c_str(),
                0600
            ) == 0;

        expect(
            restored_confirmed_target,
            "RT009 confirmed target permissions restored"
        );

        expect(
            !failed_confirm.ok &&
            failed_confirm.persist_failures == 1 &&
            failed_confirm.failed == 1 &&
            failed_confirm.moved == 0 &&
            before_restart_sent.size() == 2 &&
            before_restart_sent[0].home_node ==
                "storage-01" &&
            !before_restart_sent[0].enabled &&
            !before_restart_sent[0].storage_allocated &&
            before_restart_sent[1].home_node ==
                "storage-02" &&
            before_restart_sent[1].enabled &&
            confirmed_before_move.has_value() &&
            confirmed_after_failure.has_value() &&
            confirmed_after_failure->home_node ==
                confirmed_before_move->home_node &&
            confirmed_after_failure->revision ==
                confirmed_before_move->revision &&
            pending_after_failure.has_value() &&
            pending_after_failure->home_node ==
                "storage-02" &&
            pending_after_failure->revision ==
                before_restart_sent[1].revision,
            "RT009 B acceptance plus confirmed persist failure retains durable pending B"
        );

        // Simulate Director process loss. Reconstruct all three authoritative
        // inputs only from durable files.
        pqnas::UsersRegistry
            restarted_users;

        expect(
            restarted_users.load(
                persist_fail_users_path.string()
            ),
            "RT009 users reload after simulated restart"
        );

        pqnas::cluster::
            StorageUserProjectionSyncStateStore
                restarted_store;

        error.clear();

        expect(
            restarted_store.load(
                persist_fail_state_path.string(),
                &error
            ),
            "RT009 confirmed state reloads: " +
                error
        );

        auto restarted_transition_store =
            load_transition_store(
                persist_fail_transition_path,
                &error
            );

        expect(
            restarted_transition_store != nullptr,
            "RT009 pending journal reloads: " +
                error
        );

        const auto restarted_user =
            restarted_users.get(
                persist_fail_user.fingerprint
            );

        const auto restarted_confirmed =
            restarted_store.get(
                persist_fail_user.fingerprint
            );

        const auto restarted_pending =
            restarted_transition_store->get(
                persist_fail_user.fingerprint
            );

        expect(
            restarted_user.has_value() &&
            restarted_user->home_node ==
                "storage-02" &&
            restarted_confirmed.has_value() &&
            restarted_confirmed->home_node ==
                "storage-01" &&
            restarted_pending.has_value() &&
            restarted_pending->home_node ==
                "storage-02" &&
            restarted_pending->revision ==
                restarted_user->
                    storage_projection_revision,
            "RT009 restart reconstructs confirmed A plus pending B"
        );

        // Reproduce the historical stranded-intermediate-Node window exactly:
        // authoritative Director placement advances again before B has been
        // durably promoted from pending to confirmed.
        bool advance_to_c_changed = false;

        error.clear();

        expect(
            restarted_users.set_home_node_persisted(
                persist_fail_user.fingerprint,
                "storage-03",
                persist_fail_users_path.string(),
                &advance_to_c_changed,
                &error
            ) &&
            advance_to_c_changed,
            "RT009 authoritative placement advances B-to-C"
        );

        const auto advanced_user =
            restarted_users.get(
                persist_fail_user.fingerprint
            );

        expect(
            advanced_user.has_value() &&
            advanced_user->home_node ==
                "storage-03" &&
            restarted_pending.has_value() &&
            advanced_user->
                storage_projection_revision >
                    restarted_pending->revision,
            "RT009 C revision is newer than pending B"
        );

        std::vector<
            pqnas::cluster::
                StorageUserProjection
        > after_restart_sent;

        pqnas::cluster::
            StorageUserProjectionReconcilerConfig
                after_restart_config;

        after_restart_config.users =
            &restarted_users;

        after_restart_config.state_store =
            &restarted_store;

        after_restart_config.transition_store =
            restarted_transition_store.get();

        after_restart_config.sync_projection =
            [&](const pqnas::cluster::
                    StorageUserProjection& projection) {
                after_restart_sent.push_back(
                    projection
                );

                return sync_success(
                    projection
                );
            };

        pqnas::cluster::
            StorageUserProjectionReconciler
                after_restart_reconciler(
                    after_restart_config
                );

        const auto recovered =
            after_restart_reconciler.
                reconcile_once();

        const auto final_confirmed =
            restarted_store.get(
                persist_fail_user.fingerprint
            );

        const auto final_pending =
            restarted_transition_store->get(
                persist_fail_user.fingerprint
            );

        expect(
            recovered.ok &&
            recovered.moved == 1 &&
            recovered.failed == 0 &&
            after_restart_sent.size() == 3 &&

            // First remove the possibly-authorized intermediate B.
            after_restart_sent[0].home_node ==
                "storage-02" &&
            !after_restart_sent[0].enabled &&
            !after_restart_sent[0].storage_allocated &&

            // Then reconcile the older confirmed A knowledge.
            after_restart_sent[1].home_node ==
                "storage-01" &&
            !after_restart_sent[1].enabled &&
            !after_restart_sent[1].storage_allocated &&

            // Only after both possible old authorization targets are disabled
            // may the new authoritative C projection be established.
            after_restart_sent[2].home_node ==
                "storage-03" &&
            after_restart_sent[2].enabled &&
            advanced_user.has_value() &&

            after_restart_sent[0].revision ==
                advanced_user->
                    storage_projection_revision &&
            after_restart_sent[1].revision ==
                advanced_user->
                    storage_projection_revision &&
            after_restart_sent[2].revision ==
                advanced_user->
                    storage_projection_revision &&

            final_confirmed.has_value() &&
            final_confirmed->home_node ==
                "storage-03" &&
            final_confirmed->revision ==
                advanced_user->
                    storage_projection_revision &&
            !final_pending.has_value(),
            "RT009 restart revokes pending B and confirmed A before authorizing C"
        );
    }


    // Security/restart invariant: an explicit unplacement must retain the last
    // confirmed Node across restart when its disabling tombstone has not yet
    // been accepted.
    {
        const fs::path restart_unplace_users_path =
            directory /
            "restart_unplace_users.json";

        const fs::path restart_unplace_state_path =
            directory /
            "restart_unplace_state.json";

        const fs::path restart_unplace_transition_path =
            directory /
            "restart_unplace_transition_journal.json";

        pqnas::UsersRegistry
            before_restart_users;

        pqnas::cluster::
            StorageUserProjectionSyncStateStore
                before_restart_store;

        expect(
            before_restart_store.load(
                restart_unplace_state_path.string(),
                &error
            ),
            "restart unplacement state store loads"
        );

        const auto restart_unplace_user =
            make_user(
                '2',
                "storage-01"
            );

        bool restart_unplace_projection_changed = false;

        expect(
            before_restart_users.upsert_persisted(
                restart_unplace_user,
                restart_unplace_users_path.string(),
                &restart_unplace_projection_changed,
                &error
            ) &&
            restart_unplace_projection_changed,
            "restart unplacement fixture user persists"
        );

        std::vector<
            pqnas::cluster::
                StorageUserProjection
        > before_restart_sent;

        bool fail_revoke = false;

        auto before_restart_unplace_transition_store =
            load_transition_store(
                restart_unplace_transition_path,
                &error
            );

        expect(
            before_restart_unplace_transition_store != nullptr,
            "restart unplacement transition journal loads: " +
                error
        );

        pqnas::cluster::
            StorageUserProjectionReconcilerConfig
                before_restart_config;

        before_restart_config.users =
            &before_restart_users;

        before_restart_config.state_store =
            &before_restart_store;

        before_restart_config.transition_store =
            before_restart_unplace_transition_store.get();

        before_restart_config.sync_projection =
            [&](const pqnas::cluster::
                    StorageUserProjection& projection) {
                before_restart_sent.push_back(
                    projection
                );

                if (fail_revoke &&
                    !projection.enabled) {
                    pqnas::cluster::
                        StorageUserProjectionSyncResult
                            failed;

                    failed.code =
                        pqnas::cluster::
                            StorageUserProjectionSyncClientCode::
                                TransportFailed;

                    return failed;
                }

                return sync_success(
                    projection
                );
            };

        pqnas::cluster::
            StorageUserProjectionReconciler
                before_restart_reconciler(
                    before_restart_config
                );

        expect(
            before_restart_reconciler.
                reconcile_once().ok,
            "restart unplacement fixture initially synchronizes"
        );

        const auto confirmed_before_unplace =
            before_restart_store.get(
                restart_unplace_user.fingerprint
            );

        bool clear_changed = false;
        bool clear_matched = false;

        expect(
            before_restart_users.
                clear_home_node_persisted_if_current(
                    restart_unplace_user.fingerprint,
                    "storage-01",
                    "",
                    restart_unplace_users_path.string(),
                    &clear_changed,
                    &clear_matched,
                    &error
                ) &&
            clear_changed &&
            clear_matched,
            "restart unplacement Director commit persists"
        );

        before_restart_sent.clear();
        fail_revoke = true;

        const auto failed_revoke =
            before_restart_reconciler.
                reconcile_once();

        const auto state_after_failed_revoke =
            before_restart_store.get(
                restart_unplace_user.fingerprint
            );

        expect(
            !failed_revoke.ok &&
            failed_revoke.failed == 1 &&
            failed_revoke.revoked_unplaced == 0 &&
            before_restart_sent.size() == 1 &&
            before_restart_sent[0].home_node ==
                "storage-01" &&
            !before_restart_sent[0].enabled &&
            !before_restart_sent[0].storage_allocated &&
            confirmed_before_unplace.has_value() &&
            state_after_failed_revoke.has_value() &&
            state_after_failed_revoke->home_node ==
                confirmed_before_unplace->home_node &&
            state_after_failed_revoke->revision ==
                confirmed_before_unplace->revision,
            "failed revoke preserves confirmed Node for restart"
        );

        // Simulate Director restart by discarding all in-memory placement
        // knowledge and reloading both durable stores.
        pqnas::UsersRegistry
            restarted_users;

        expect(
            restarted_users.load(
                restart_unplace_users_path.string()
            ),
            "restart unplacement users reload"
        );

        pqnas::cluster::
            StorageUserProjectionSyncStateStore
                restarted_store;

        expect(
            restarted_store.load(
                restart_unplace_state_path.string(),
                &error
            ),
            "restart unplacement sync-state reload"
        );

        auto restarted_unplace_transition_store =
            load_transition_store(
                restart_unplace_transition_path,
                &error
            );

        expect(
            restarted_unplace_transition_store != nullptr,
            "restart unplacement transition journal reloads: " +
                error
        );

        const auto restarted_user =
            restarted_users.get(
                restart_unplace_user.fingerprint
            );

        const auto restarted_state =
            restarted_store.get(
                restart_unplace_user.fingerprint
            );

        expect(
            restarted_user.has_value() &&
            restarted_user->home_node.empty() &&
            restarted_state.has_value() &&
            restarted_state->home_node ==
                "storage-01" &&
            restarted_state->revision <
                restarted_user->
                    storage_projection_revision,
            "restart reload retains unplacement plus previous confirmed Node"
        );

        std::vector<
            pqnas::cluster::
                StorageUserProjection
        > after_restart_sent;

        pqnas::cluster::
            StorageUserProjectionReconcilerConfig
                after_restart_config;

        after_restart_config.users =
            &restarted_users;

        after_restart_config.state_store =
            &restarted_store;

        after_restart_config.transition_store =
            restarted_unplace_transition_store.get();

        after_restart_config.sync_projection =
            [&](const pqnas::cluster::
                    StorageUserProjection& projection) {
                after_restart_sent.push_back(
                    projection
                );

                return sync_success(
                    projection
                );
            };

        pqnas::cluster::
            StorageUserProjectionReconciler
                after_restart_reconciler(
                    after_restart_config
                );

        const auto completed_revoke =
            after_restart_reconciler.
                reconcile_once();

        expect(
            completed_revoke.ok &&
            completed_revoke.revoked_unplaced == 1 &&
            completed_revoke.failed == 0 &&
            after_restart_sent.size() == 1 &&
            after_restart_sent[0].home_node ==
                "storage-01" &&
            !after_restart_sent[0].enabled &&
            !after_restart_sent[0].storage_allocated &&
            restarted_user.has_value() &&
            after_restart_sent[0].revision ==
                restarted_user->
                    storage_projection_revision &&
            !restarted_store.get(
                restart_unplace_user.fingerprint
            ).has_value(),
            "restart reload revokes previous Node and durably completes unplacement"
        );
    }


    // Equal Director/confirmed revision is deliberately ambiguous for an
    // unplaced user: the Node could still hold live same-revision content.
    // Never try to replace it with a different tombstone at that revision.
    {
        const fs::path ambiguous_state_path =
            directory /
            "ambiguous_unplace_state.json";

        const fs::path ambiguous_users_path =
            directory /
            "ambiguous_unplace_users.json";

        pqnas::cluster::
            StorageUserProjectionSyncStateStore
                ambiguous_store;

        expect(
            ambiguous_store.load(
                ambiguous_state_path.string(),
                &error
            ),
            "ambiguous unplacement sync-state store loads"
        );

        pqnas::UsersRegistry
            ambiguous_users;

        const auto ambiguous_user =
            make_user(
                'f',
                "storage-01"
            );

        bool ambiguous_projection_changed = false;

        expect(
            ambiguous_users.upsert_persisted(
                ambiguous_user,
                ambiguous_users_path.string(),
                &ambiguous_projection_changed,
                &error
            ) &&
            ambiguous_projection_changed,
            "ambiguous unplacement user persisted"
        );

        std::vector<
            pqnas::cluster::
                StorageUserProjection
        > ambiguous_sent;

        auto ambiguous_transition_store =
            load_transition_store(
                directory /
                    "ambiguous_unplace_transition_journal.json",
                &error
            );

        expect(
            ambiguous_transition_store != nullptr,
            "ambiguous transition journal loads: " +
                error
        );

        pqnas::cluster::
            StorageUserProjectionReconcilerConfig
                ambiguous_config;

        ambiguous_config.users =
            &ambiguous_users;

        ambiguous_config.state_store =
            &ambiguous_store;

        ambiguous_config.transition_store =
            ambiguous_transition_store.get();

        ambiguous_config.sync_projection =
            [&](const pqnas::cluster::
                    StorageUserProjection& projection) {
                ambiguous_sent.push_back(
                    projection
                );

                return sync_success(
                    projection
                );
            };

        pqnas::cluster::
            StorageUserProjectionReconciler
                ambiguous_reconciler(
                    ambiguous_config
                );

        expect(
            ambiguous_reconciler.reconcile_once().ok,
            "ambiguous unplacement fixture initially synchronizes"
        );

        bool ambiguous_clear_changed = false;
        bool ambiguous_clear_matched = false;

        expect(
            ambiguous_users.clear_home_node_persisted_if_current(
                ambiguous_user.fingerprint,
                "storage-01",
                "",
                ambiguous_users_path.string(),
                &ambiguous_clear_changed,
                &ambiguous_clear_matched,
                &error
            ) &&
            ambiguous_clear_changed &&
            ambiguous_clear_matched,
            "ambiguous unplacement Director commit persists"
        );

        const auto ambiguous_current_user =
            ambiguous_users.get(
                ambiguous_user.fingerprint
            );

        expect(
            ambiguous_current_user.has_value(),
            "ambiguous unplaced user remains readable"
        );

        if (ambiguous_current_user.has_value()) {
            pqnas::cluster::
                StorageUserProjectionSyncState
                    equal_revision_state;

            equal_revision_state.fingerprint =
                ambiguous_user.fingerprint;

            equal_revision_state.home_node =
                "storage-01";

            equal_revision_state.revision =
                ambiguous_current_user->
                    storage_projection_revision;

            expect(
                ambiguous_store.mark_synced(
                    equal_revision_state,
                    &error
                ) ==
                    pqnas::cluster::
                        StorageUserProjectionSyncStateStoreResult::
                            Applied,
                "ambiguous equal-revision state fixture persisted"
            );
        }

        ambiguous_sent.clear();

        const auto ambiguous_result =
            ambiguous_reconciler.reconcile_once();

        expect(
            !ambiguous_result.ok &&
            ambiguous_result.state_conflicts == 1 &&
            ambiguous_result.failed == 1 &&
            ambiguous_result.revoked_unplaced == 0 &&
            ambiguous_sent.empty() &&
            ambiguous_store.get(
                ambiguous_user.fingerprint
            ).has_value(),
            "equal-revision unplacement fails closed without network traffic"
        );
    }


    // State with no Director user is retained and reported, never erased.
    {
        const fs::path orphan_state_path =
            directory /
            "orphan_state.json";

        pqnas::cluster::
            StorageUserProjectionSyncStateStore
                orphan_store;

        expect(
            orphan_store.load(
                orphan_state_path.string(),
                &error
            ),
            "orphan state store loads"
        );

        pqnas::cluster::
            StorageUserProjectionSyncState
                orphan;

        orphan.fingerprint =
            std::string(
                128,
                'd'
            );

        orphan.home_node =
            "storage-01";

        orphan.revision = 4;

        expect(
            orphan_store.mark_synced(
                orphan,
                &error
            ) ==
                pqnas::cluster::
                    StorageUserProjectionSyncStateStoreResult::
                        Applied,
            "orphan fixture persisted"
        );

        pqnas::UsersRegistry
            empty_users;

        std::size_t network_calls = 0;

        auto orphan_transition_store =
            load_transition_store(
                directory /
                    "orphan_transition_journal.json",
                &error
            );

        expect(
            orphan_transition_store != nullptr,
            "orphan transition journal loads: " +
                error
        );

        pqnas::cluster::
            StorageUserProjectionReconcilerConfig
                orphan_config;

        orphan_config.users =
            &empty_users;

        orphan_config.state_store =
            &orphan_store;

        orphan_config.transition_store =
            orphan_transition_store.get();

        orphan_config.sync_projection =
            [&](const pqnas::cluster::
                    StorageUserProjection& projection) {
                ++network_calls;

                return sync_success(
                    projection
                );
            };

        pqnas::cluster::
            StorageUserProjectionReconciler
                orphan_reconciler(
                    orphan_config
                );

        const auto result =
            orphan_reconciler.reconcile_once();

        expect(
            !result.ok &&
            result.orphaned_states == 1 &&
            network_calls == 0 &&
            orphan_store.get(
                orphan.fingerprint
            ).has_value(),
            "orphan state fails closed and remains persisted"
        );
    }

    cleanup();

    if (failures != 0) {
        std::cerr
            << "FAIL: "
            << failures
            << " projection reconciler test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: storage user projection reconciler tests passed\n";

    return 0;
}
