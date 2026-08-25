#include "cluster/storage_user_projection_reconciler.h"

#include "cluster/cluster_identity_validation.h"
#include "cluster/storage_user_projection_audit.h"
#include "cluster/storage_user_projection_builder.h"
#include "cluster/storage_user_projection_sync_state_store.h"
#include "cluster/storage_user_projection_transition_store.h"
#include "storage_metadata_validation.h"
#include "users_registry.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace pqnas::cluster {
namespace {

bool sync_succeeded(
    StorageUserProjectionSyncClientCode code
) noexcept {
    return
        code ==
            StorageUserProjectionSyncClientCode::Applied ||
        code ==
            StorageUserProjectionSyncClientCode::Unchanged;
}

bool valid_digest_hex(
    const std::string& value
) {
    if (value.size() !=
        kStorageUserProjectionDigestHexChars) {
        return false;
    }

    for (unsigned char character : value) {
        const bool valid =
            (character >= '0' && character <= '9') ||
            (character >= 'a' && character <= 'f');

        if (!valid) {
            return false;
        }
    }

    return true;
}

bool fingerprint_in_audit_range(
    const std::string& fingerprint,
    const StorageUserProjectionAuditRequest& request
) {
    return
        (request.range_start.empty() ||
         fingerprint >= request.range_start) &&
        (request.range_end.empty() ||
         fingerprint < request.range_end);
}

const StorageUserProjectionAuditExpected*
find_expected(
    const StorageUserProjectionAuditRequest& request,
    const std::string& fingerprint
) {
    const auto found =
        std::lower_bound(
            request.expected.begin(),
            request.expected.end(),
            fingerprint,
            [](
                const StorageUserProjectionAuditExpected& entry,
                const std::string& value
            ) {
                return entry.fingerprint < value;
            }
        );

    if (found == request.expected.end() ||
        found->fingerprint != fingerprint) {
        return nullptr;
    }

    return &*found;
}

// Security: the production HTTPS parser is strict, but the reconciler accepts
// an injected callback. Re-validate the typed proof so a future callback bug
// cannot turn malformed/incomplete evidence into authorization convergence.
bool audit_result_consistent(
    const StorageUserProjectionAuditRequest& request,
    const StorageUserProjectionAuditResult& result
) {
    if (!result.complete ||
        result.matched > request.expected.size() ||
        result.mismatches.size() >
            kStorageUserProjectionAuditMaxMismatches) {
        return false;
    }

    std::string previous_fingerprint;
    bool have_previous = false;
    std::size_t expected_mismatches = 0;

    for (const auto& mismatch :
         result.mismatches) {
        if (!pqnas::valid_storage_user_fingerprint_hex(
                mismatch.fingerprint) ||
            !fingerprint_in_audit_range(
                mismatch.fingerprint,
                request) ||
            (have_previous &&
             mismatch.fingerprint <=
                 previous_fingerprint)) {
            return false;
        }

        const auto* expected =
            find_expected(
                request,
                mismatch.fingerprint
            );

        switch (mismatch.code) {
            case StorageUserProjectionAuditMismatchCode::Missing:
                if (!expected ||
                    mismatch.expected_revision !=
                        expected->revision ||
                    mismatch.actual_revision != 0) {
                    return false;
                }

                ++expected_mismatches;
                break;

            case StorageUserProjectionAuditMismatchCode::Stale:
                if (!expected ||
                    mismatch.expected_revision !=
                        expected->revision ||
                    mismatch.actual_revision == 0 ||
                    mismatch.actual_revision >=
                        expected->revision) {
                    return false;
                }

                ++expected_mismatches;
                break;

            case StorageUserProjectionAuditMismatchCode::Conflict:
                if (!expected ||
                    mismatch.expected_revision !=
                        expected->revision ||
                    mismatch.actual_revision !=
                        expected->revision) {
                    return false;
                }

                ++expected_mismatches;
                break;

            case StorageUserProjectionAuditMismatchCode::Newer:
                if (!expected ||
                    mismatch.expected_revision !=
                        expected->revision ||
                    mismatch.actual_revision <=
                        expected->revision) {
                    return false;
                }

                ++expected_mismatches;
                break;

            case StorageUserProjectionAuditMismatchCode::WrongNodeState:
                if (mismatch.actual_revision == 0) {
                    return false;
                }

                if (expected) {
                    if (mismatch.expected_revision !=
                        expected->revision) {
                        return false;
                    }

                    ++expected_mismatches;
                } else if (
                    mismatch.expected_revision != 0
                ) {
                    return false;
                }

                break;

            case StorageUserProjectionAuditMismatchCode::
                    UnexpectedAuthorization:
                if (expected ||
                    mismatch.expected_revision != 0 ||
                    mismatch.actual_revision == 0) {
                    return false;
                }

                break;

            default:
                return false;
        }

        previous_fingerprint =
            mismatch.fingerprint;
        have_previous = true;
    }

    return
        result.matched +
            expected_mismatches ==
        request.expected.size();
}

StorageUserProjection previous_node_tombstone(
    const StorageUserProjection& current,
    const std::string& previous_home_node
) {
    StorageUserProjection tombstone;

    tombstone.fingerprint =
        current.fingerprint;

    // Security: a Node that is no longer authoritative receives no usable
    // authorization or storage mapping.
    tombstone.enabled = false;
    tombstone.storage_allocated = false;
    tombstone.quota_bytes = 0;
    tombstone.root_rel.clear();
    tombstone.storage_pool_id.clear();

    tombstone.home_node =
        previous_home_node;

    // Security: use the new Director revision so older delayed writes become
    // stale at the previous Node.
    tombstone.revision =
        current.revision;

    return tombstone;
}

StorageUserProjection unplaced_user_tombstone(
    const pqnas::UserRec& user,
    const std::string& previous_home_node
) {
    StorageUserProjection revision_source;

    revision_source.fingerprint =
        user.fingerprint;

    revision_source.revision =
        user.storage_projection_revision;

    // Security: explicit unplacement uses the Director's newer durable
    // revision while targeting the last Node confirmed to hold authorization.
    // Reuse the exact A->B tombstone shape so no usable storage mapping leaks.
    return previous_node_tombstone(
        revision_source,
        previous_home_node
    );
}


StorageUserProjectionSyncState
projection_state_from_projection(
    const StorageUserProjection& projection
) {
    StorageUserProjectionSyncState state;

    state.fingerprint =
        projection.fingerprint;

    state.home_node =
        projection.home_node;

    state.revision =
        projection.revision;

    return state;
}


bool persist_pending_transition(
    StorageUserProjectionTransitionStore* store,
    const StorageUserProjection& projection,
    StorageUserProjectionReconcileResult* result
) {
    if (!store || !result) {
        if (result) {
            ++result->state_conflicts;
            ++result->failed;
        }

        return false;
    }

    const StorageUserProjectionTransitionRecord
        record =
            projection_state_from_projection(
                projection
            );

    std::string persistence_error;

    const auto persisted =
        store->mark_pending(
            record,
            &persistence_error
        );

    persistence_error.clear();

    switch (persisted) {
        case StorageUserProjectionSyncStateStoreResult::
                Applied:
        case StorageUserProjectionSyncStateStoreResult::
                Unchanged:
            return true;

        case StorageUserProjectionSyncStateStoreResult::
                PersistFailed:
            ++result->persist_failures;
            ++result->failed;
            return false;

        case StorageUserProjectionSyncStateStoreResult::
                Stale:
            ++result->stale_states;
            ++result->failed;
            return false;

        case StorageUserProjectionSyncStateStoreResult::
                Conflict:
        case StorageUserProjectionSyncStateStoreResult::
                InvalidInput:
            ++result->state_conflicts;
            ++result->failed;
            return false;
    }

    ++result->state_conflicts;
    ++result->failed;
    return false;
}


bool projection_equal(
    const StorageUserProjection& left,
    const StorageUserProjection& right
) noexcept {
    return
        left.fingerprint == right.fingerprint &&
        left.enabled == right.enabled &&
        left.storage_allocated ==
            right.storage_allocated &&
        left.quota_bytes == right.quota_bytes &&
        left.root_rel == right.root_rel &&
        left.storage_pool_id ==
            right.storage_pool_id &&
        left.home_node == right.home_node &&
        left.revision == right.revision;
}


bool prepare_pending_authorization_transition(
    const StorageUserProjectionReconcilerConfig& config,
    const StorageUserProjection& projection,
    StorageUserProjectionReconcileResult* result
) {
    if (!config.users ||
        !config.transition_store ||
        !result) {
        if (result) {
            ++result->state_conflicts;
            ++result->failed;
        }

        return false;
    }

    std::unique_lock<std::mutex>
        transition_lock;

    if (config.transition_mutex) {
        transition_lock =
            std::unique_lock<std::mutex>(
                *config.transition_mutex
            );
    }

    // RT-016: this is the narrow shared deletion/reconciliation boundary.
    // Re-read users.json-backed state after acquiring it, then persist RT-009
    // pending knowledge before releasing it. A physical user/Node deletion
    // that runs after this point observes pending state and fails closed.
    const auto authoritative_user =
        config.users->get(
            projection.fingerprint
        );

    if (!authoritative_user.has_value()) {
        ++result->stale_states;
        ++result->failed;
        return false;
    }

    StorageUserProjection
        authoritative_projection;

    std::string build_error;

    if (!build_storage_user_projection(
            *authoritative_user,
            &authoritative_projection,
            &build_error)) {
        build_error.clear();

        ++result->state_conflicts;
        ++result->failed;
        return false;
    }

    if (!projection_equal(
            authoritative_projection,
            projection)) {
        // Director state advanced while this reconciliation pass was working.
        // Never journal or send the stale authorization target.
        ++result->stale_states;
        ++result->failed;
        return false;
    }

    return persist_pending_transition(
        config.transition_store,
        projection,
        result
    );
}


bool erase_pending_transition(
    StorageUserProjectionTransitionStore* store,
    const StorageUserProjectionTransitionRecord& record,
    StorageUserProjectionReconcileResult* result
) {
    if (!store || !result) {
        if (result) {
            ++result->state_conflicts;
            ++result->failed;
        }

        return false;
    }

    bool expectation_matched = false;
    std::string persistence_error;

    if (!store->erase_if_current(
            record.fingerprint,
            record.home_node,
            record.revision,
            &expectation_matched,
            &persistence_error)) {
        persistence_error.clear();

        ++result->persist_failures;
        ++result->failed;
        return false;
    }

    persistence_error.clear();

    if (!expectation_matched) {
        // Security: an unexpected journal replacement must not be erased or
        // treated as completion of this authorization transition.
        ++result->state_conflicts;
        ++result->failed;
        return false;
    }

    return true;
}


bool persist_confirmed_state(
    StorageUserProjectionSyncStateStore* store,
    const StorageUserProjection& projection,
    StorageUserProjectionReconcileResult* result
) {
    const StorageUserProjectionSyncState
        state =
            projection_state_from_projection(
                projection
            );

    std::string persistence_error;

    const auto persisted =
        store->mark_synced(
            state,
            &persistence_error
        );

    persistence_error.clear();

    switch (persisted) {
        case StorageUserProjectionSyncStateStoreResult::
                Applied:
        case StorageUserProjectionSyncStateStoreResult::
                Unchanged:
            return true;

        case StorageUserProjectionSyncStateStoreResult::
                PersistFailed:
            ++result->persist_failures;
            ++result->failed;
            return false;

        case StorageUserProjectionSyncStateStoreResult::
                Stale:
        case StorageUserProjectionSyncStateStoreResult::
                Conflict:
        case StorageUserProjectionSyncStateStoreResult::
                InvalidInput:
            ++result->state_conflicts;
            ++result->failed;
            return false;
    }

    ++result->state_conflicts;
    ++result->failed;
    return false;
}

void emit_reconcile_audit_best_effort(
    const StorageUserProjectionReconcilerConfig& config,
    const std::string& event,
    const std::string& fingerprint,
    std::uint64_t previous_revision,
    std::uint64_t projection_revision,
    const std::string& previous_home_node,
    const std::string& home_node
) noexcept {
    if (!config.audit_emit) {
        return;
    }

    try {
        config.audit_emit(
            event,
            "ok",
            {
                {
                    "fingerprint",
                    fingerprint
                },
                {
                    "previous_revision",
                    std::to_string(
                        previous_revision
                    )
                },
                {
                    "projection_revision",
                    std::to_string(
                        projection_revision
                    )
                },
                {
                    "previous_home_node",
                    previous_home_node
                },
                {
                    "home_node",
                    home_node
                }
            }
        );
    } catch (...) {
        // Security/availability: audit is best-effort after the durable
        // authorization transition. Never turn an already-completed
        // reconciliation into a retry or rollback because audit I/O failed.
    }
}

bool heal_confirmed_state_durability(
    StorageUserProjectionSyncStateStore* store,
    const StorageUserProjectionSyncState& state,
    StorageUserProjectionReconcileResult* result
) {
    std::string persistence_error;

    const auto persisted =
        store->mark_synced(
            state,
            &persistence_error
        );

    persistence_error.clear();

    switch (persisted) {
        case StorageUserProjectionSyncStateStoreResult::
                Applied:
        case StorageUserProjectionSyncStateStoreResult::
                Unchanged:
            return true;

        case StorageUserProjectionSyncStateStoreResult::
                PersistFailed:
            ++result->persist_failures;
            ++result->failed;
            return false;

        case StorageUserProjectionSyncStateStoreResult::
                Stale:
        case StorageUserProjectionSyncStateStoreResult::
                Conflict:
        case StorageUserProjectionSyncStateStoreResult::
                InvalidInput:
            ++result->state_conflicts;
            ++result->failed;
            return false;
    }

    ++result->state_conflicts;
    ++result->failed;
    return false;
}

} // namespace

bool build_storage_user_projection_reconcile_audit_requests(
    const std::string& node_id,
    std::vector<StorageUserProjectionAuditExpected> expected,
    std::vector<StorageUserProjectionAuditRequest>* requests
) {
    if (!requests ||
        !valid_cluster_node_id(node_id) ||
        kStorageUserProjectionAuditMaxExpected == 0) {
        return false;
    }

    std::sort(
        expected.begin(),
        expected.end(),
        [](
            const StorageUserProjectionAuditExpected& left,
            const StorageUserProjectionAuditExpected& right
        ) {
            return
                left.fingerprint <
                right.fingerprint;
        }
    );

    std::string previous_fingerprint;
    bool have_previous = false;

    for (const auto& entry :
         expected) {
        if (!pqnas::valid_storage_user_fingerprint_hex(
                entry.fingerprint) ||
            entry.revision == 0 ||
            !valid_digest_hex(
                entry.digest_hex) ||
            (have_previous &&
             entry.fingerprint ==
                 previous_fingerprint)) {
            return false;
        }

        previous_fingerprint =
            entry.fingerprint;
        have_previous = true;
    }

    std::vector<StorageUserProjectionAuditRequest>
        candidate;

    if (expected.empty()) {
        StorageUserProjectionAuditRequest request;
        request.node_id = node_id;

        candidate.push_back(
            std::move(request)
        );

        *requests =
            std::move(candidate);

        return true;
    }

    for (std::size_t offset = 0;
         offset < expected.size();
         offset +=
            kStorageUserProjectionAuditMaxExpected) {
        const std::size_t next =
            std::min(
                expected.size(),
                offset +
                    kStorageUserProjectionAuditMaxExpected
            );

        StorageUserProjectionAuditRequest request;
        request.node_id = node_id;

        if (offset != 0) {
            request.range_start =
                expected[offset].fingerprint;
        }

        if (next < expected.size()) {
            request.range_end =
                expected[next].fingerprint;
        }

        request.expected.assign(
            expected.begin() +
                static_cast<std::ptrdiff_t>(
                    offset
                ),
            expected.begin() +
                static_cast<std::ptrdiff_t>(
                    next
                )
        );

        candidate.push_back(
            std::move(request)
        );
    }

    *requests =
        std::move(candidate);

    return true;
}

StorageUserProjectionReconciler::
StorageUserProjectionReconciler(
    StorageUserProjectionReconcilerConfig config
)
    : config_(std::move(config)) {}

StorageUserProjectionReconcileResult
StorageUserProjectionReconciler::reconcile_once() const {
    StorageUserProjectionReconcileResult result;

    if (!config_.users ||
        !config_.state_store ||
        !config_.transition_store ||
        !config_.sync_projection) {
        result.failed = 1;
        return result;
    }

    const bool has_audit_node_list =
        static_cast<bool>(
            config_.list_audit_capable_nodes
        );

    const bool has_audit_invoke =
        static_cast<bool>(
            config_.audit_projection_range
        );

    if (has_audit_node_list !=
        has_audit_invoke) {
        // Security: never partially enable RT-008 proof mode.
        ++result.state_conflicts;
        ++result.failed;
        return result;
    }

    std::string durability_error;

    // Security: users.json is authoritative Director state. Never derive or
    // send Storage authorization while its latest visible commit has uncertain
    // crash durability.
    if (!config_.users->ensure_durable(
            &durability_error)) {
        durability_error.clear();

        ++result.persist_failures;
        ++result.failed;

        return result;
    }

    durability_error.clear();

    // Security: confirmed Node synchronization state must also be durable
    // before another Storage authorization transition is attempted.
    if (!config_.state_store->ensure_durable(
            &durability_error)) {
        durability_error.clear();

        ++result.persist_failures;
        ++result.failed;

        return result;
    }

    durability_error.clear();

    // Security: pending transition knowledge is authorization state too.
    // Never perform another Storage write until its visible snapshot is
    // known durable.
    if (!config_.transition_store->ensure_durable(
            &durability_error)) {
        durability_error.clear();

        ++result.persist_failures;
        ++result.failed;

        return result;
    }

    const auto users =
        config_.users->snapshot();

    const auto states =
        config_.state_store->snapshot();

    const auto transitions =
        config_.transition_store->snapshot();

    std::vector<std::string>
        audit_nodes;

    std::unordered_set<std::string>
        audit_capable_nodes;

    if (has_audit_node_list) {
        try {
            audit_nodes =
                config_.list_audit_capable_nodes();
        } catch (...) {
            ++result.failed;
            return result;
        }

        std::sort(
            audit_nodes.begin(),
            audit_nodes.end()
        );

        for (const auto& node_id :
             audit_nodes) {
            if (!valid_cluster_node_id(
                    node_id) ||
                !audit_capable_nodes.insert(
                    node_id
                ).second) {
                ++result.state_conflicts;
                ++result.failed;
                return result;
            }
        }
    }

    // Equal confirmed/current entries on audit-capable Nodes are intentionally
    // deferred until full-range proof succeeds. Legacy Nodes are verified
    // immediately with an idempotent current projection POST.
    std::unordered_map<
        std::string,
        StorageUserProjection
    > deferred_equal;

    std::unordered_set<std::string>
        current_fingerprints;

    current_fingerprints.reserve(
        users.size()
    );

    std::vector<std::string>
        fingerprints;

    fingerprints.reserve(
        users.size()
    );

    for (const auto& entry :
         users) {
        current_fingerprints.insert(
            entry.first
        );

        fingerprints.push_back(
            entry.first
        );
    }

    std::sort(
        fingerprints.begin(),
        fingerprints.end()
    );

    // Security: a state entry whose Director user no longer exists is never
    // silently discarded. Physical cluster deletion remains blocked until a
    // dedicated newer-revision revoke protocol exists.
    for (const auto& state :
         states) {
        if (current_fingerprints.find(
                state.fingerprint) ==
            current_fingerprints.end()) {
            ++result.orphaned_states;
            ++result.failed;
        }
    }

    // Security: an orphaned pending transition can identify a Storage
    // Node that may still hold authorization. Never silently forget it.
    for (const auto& transition :
         transitions) {
        if (current_fingerprints.find(
                transition.fingerprint) ==
            current_fingerprints.end()) {
            ++result.orphaned_states;
            ++result.failed;
        }
    }

    for (const std::string& fingerprint :
         fingerprints) {
        ++result.users_seen;

        const auto user_iterator =
            users.find(
                fingerprint
            );

        if (user_iterator ==
            users.end()) {
            ++result.failed;
            continue;
        }

        const pqnas::UserRec& user =
            user_iterator->second;

        const auto confirmed =
            config_.state_store->get(
                fingerprint
            );

        const auto pending =
            config_.transition_store->get(
                fingerprint
            );

        if (pending.has_value()) {
            // A confirmed state at least as new as this pending record proves
            // that the same Node is already represented durably. The journal
            // can be cleared without another authorization write.
            if (confirmed.has_value() &&
                confirmed->home_node ==
                    pending->home_node &&
                confirmed->revision >=
                    pending->revision) {
                if (!erase_pending_transition(
                        config_.transition_store,
                        *pending,
                        &result)) {
                    continue;
                }
            } else if (
                pending->revision >
                    user.storage_projection_revision
            ) {
                // Security: Director state is older than possibly-authorized
                // journal knowledge. Never roll the Node backwards.
                ++result.stale_states;
                ++result.failed;
                continue;
            } else if (
                !user.home_node.empty() &&
                user.home_node ==
                    pending->home_node
            ) {
                // The pending Node is still authoritative. Retry the live
                // projection idempotently, durably confirm it, then clear
                // the write-ahead journal.
                StorageUserProjection recovered;
                std::string build_error;

                if (!build_storage_user_projection(
                        user,
                        &recovered,
                        &build_error)) {
                    build_error.clear();

                    ++result.failed;
                    continue;
                }

                // Security: pending recovery must preserve the same rollback
                // and equal-revision conflict invariants as normal
                // reconciliation before any authorization reaches Storage.
                if (confirmed.has_value() &&
                    confirmed->revision >
                        recovered.revision) {
                    ++result.stale_states;
                    ++result.failed;
                    continue;
                }

                if (confirmed.has_value() &&
                    confirmed->revision ==
                        recovered.revision &&
                    confirmed->home_node !=
                        recovered.home_node) {
                    // Security: the Director has two different targets for
                    // one revision. Never authorize either interpretation
                    // further until the durable state is repaired.
                    ++result.state_conflicts;
                    ++result.failed;
                    continue;
                }

                StorageUserProjectionSyncResult
                    recovered_result;

                try {
                    recovered_result =
                        config_.sync_projection(
                            recovered
                        );
                } catch (...) {
                    ++result.failed;
                    continue;
                }

                if (!sync_succeeded(
                        recovered_result.code)) {
                    ++result.failed;
                    continue;
                }

                if (!persist_confirmed_state(
                        config_.state_store,
                        recovered,
                        &result)) {
                    continue;
                }

                if (!erase_pending_transition(
                        config_.transition_store,
                        *pending,
                        &result)) {
                    continue;
                }

                if (confirmed.has_value() &&
                    confirmed->home_node !=
                        recovered.home_node) {
                    emit_reconcile_audit_best_effort(
                        config_,
                        "cluster.storage_projection.moved",
                        fingerprint,
                        confirmed->revision,
                        recovered.revision,
                        confirmed->home_node,
                        recovered.home_node
                    );

                    ++result.moved;
                } else {
                    emit_reconcile_audit_best_effort(
                        config_,
                        "cluster.storage_projection.synced",
                        fingerprint,
                        confirmed.has_value()
                            ? confirmed->revision
                            : 0,
                        recovered.revision,
                        confirmed.has_value()
                            ? confirmed->home_node
                            : "",
                        recovered.home_node
                    );

                    ++result.synced_current;
                }

                continue;
            } else if (
                pending->revision ==
                    user.storage_projection_revision
            ) {
                // Same revision cannot safely mean two different targets or
                // both live authorization and a disabling tombstone.
                ++result.state_conflicts;
                ++result.failed;
                continue;
            } else {
                // Director state advanced beyond an intermediate pending Node.
                // Revoke that possibly-authorized Node first with the newer
                // authoritative revision, then forget the journal record.
                const StorageUserProjection
                    pending_tombstone =
                        unplaced_user_tombstone(
                            user,
                            pending->home_node
                        );

                StorageUserProjectionSyncResult
                    pending_revoke_result;

                try {
                    pending_revoke_result =
                        config_.sync_projection(
                            pending_tombstone
                        );
                } catch (...) {
                    ++result.failed;
                    continue;
                }

                if (!sync_succeeded(
                        pending_revoke_result.code)) {
                    ++result.failed;
                    continue;
                }

                if (!erase_pending_transition(
                        config_.transition_store,
                        *pending,
                        &result)) {
                    continue;
                }

                // Continue with ordinary confirmed-state reconciliation.
            }
        }

        if (user.home_node.empty()) {
            if (!confirmed.has_value()) {
                ++result.no_placement;
                continue;
            }

            if (confirmed->revision >
                user.storage_projection_revision) {
                // Security: persistent knowledge newer than Director state
                // cannot be revoked using an older revision.
                ++result.stale_states;
                ++result.failed;
                continue;
            }

            if (confirmed->revision ==
                user.storage_projection_revision) {
                // Security: equal revision cannot prove whether the Node holds
                // the old live projection or an already-applied tombstone.
                // Never send conflicting same-revision authorization content.
                ++result.state_conflicts;
                ++result.failed;
                continue;
            }

            const StorageUserProjection tombstone =
                unplaced_user_tombstone(
                    user,
                    confirmed->home_node
                );

            StorageUserProjectionSyncResult
                revoke_result;

            try {
                revoke_result =
                    config_.sync_projection(
                        tombstone
                    );
            } catch (...) {
                ++result.failed;
                continue;
            }

            if (!sync_succeeded(
                    revoke_result.code)) {
                // Security: retain confirmed state until the last known Node
                // has accepted the newer disabling tombstone.
                ++result.failed;
                continue;
            }

            bool expectation_matched = false;
            std::string persistence_error;

            if (!config_.state_store->
                    erase_if_current(
                        fingerprint,
                        confirmed->home_node,
                        confirmed->revision,
                        &expectation_matched,
                        &persistence_error)) {
                persistence_error.clear();

                // Security: a revoke is not considered reconciled until the
                // Director has durably removed the old confirmed placement.
                ++result.persist_failures;
                ++result.failed;
                continue;
            }

            persistence_error.clear();

            if (!expectation_matched) {
                // Another state transition won the CAS. Never erase or claim
                // success against newer confirmed synchronization knowledge.
                ++result.state_conflicts;
                ++result.failed;
                continue;
            }

            emit_reconcile_audit_best_effort(
                config_,
                "cluster.storage_projection.revoked",
                fingerprint,
                confirmed->revision,
                user.storage_projection_revision,
                confirmed->home_node,
                ""
            );

            ++result.revoked_unplaced;
            continue;
        }

        StorageUserProjection current;
        std::string build_error;

        if (!build_storage_user_projection(
                user,
                &current,
                &build_error)) {
            build_error.clear();

            ++result.failed;
            continue;
        }

        if (!confirmed.has_value()) {
            // RT009: establish durable knowledge of the new target before the
            // Node can receive usable authorization.
            if (!prepare_pending_authorization_transition(
                    config_,
                    current,
                    &result)) {
                continue;
            }

            StorageUserProjectionSyncResult
                sync_result;

            try {
                sync_result =
                    config_.sync_projection(
                        current
                    );
            } catch (...) {
                ++result.failed;
                continue;
            }

            if (!sync_succeeded(
                    sync_result.code)) {
                // Keep pending state. The request may have reached Storage
                // even when the response was lost.
                ++result.failed;
                continue;
            }

            if (!persist_confirmed_state(
                    config_.state_store,
                    current,
                    &result)) {
                // Keep pending state so restart recovery remembers this Node.
                continue;
            }

            const auto pending_current =
                projection_state_from_projection(
                    current
                );

            if (!erase_pending_transition(
                    config_.transition_store,
                    pending_current,
                    &result)) {
                continue;
            }

            emit_reconcile_audit_best_effort(
                config_,
                "cluster.storage_projection.synced",
                fingerprint,
                0,
                current.revision,
                "",
                current.home_node
            );

            ++result.synced_current;
            continue;
        }

        if (confirmed->revision >
            current.revision) {
            // Security: never roll Storage authorization backwards if the
            // persisted sync state proves a newer revision was already sent.
            ++result.stale_states;
            ++result.failed;
            continue;
        }

        if (confirmed->revision ==
            current.revision) {
            if (confirmed->home_node !=
                current.home_node) {
                // Same Director revision cannot legitimately represent two
                // different authoritative Nodes.
                ++result.state_conflicts;
                ++result.failed;
                continue;
            }

            if (audit_capable_nodes.find(
                    current.home_node) !=
                audit_capable_nodes.end()) {
                // RT-008: Director durable state is not proof that Storage
                // currently has this projection. Defer success until the
                // authenticated full-range audit at the end of this pass.
                if (!deferred_equal.emplace(
                        fingerprint,
                        current
                    ).second) {
                    ++result.state_conflicts;
                    ++result.failed;
                }

                continue;
            }

            // Rolling-upgrade fallback: legacy Storage has no readback proof,
            // so verify the exact current authorization with an idempotent
            // authenticated POST. Applied or Unchanged is current Storage
            // evidence; local confirmed state alone is never sufficient.
            StorageUserProjectionSyncResult verification_result;

            try {
                verification_result =
                    config_.sync_projection(
                        current
                    );
            } catch (...) {
                ++result.failed;
                continue;
            }

            if (!sync_succeeded(
                    verification_result.code)) {
                ++result.failed;
                continue;
            }

            if (heal_confirmed_state_durability(
                    config_.state_store,
                    *confirmed,
                    &result)) {
                ++result.already_synced;
            }

            continue;
        }

        // confirmed revision is older than current Director revision.
        if (confirmed->home_node !=
            current.home_node) {
            const StorageUserProjection tombstone =
                previous_node_tombstone(
                    current,
                    confirmed->home_node
                );

            StorageUserProjectionSyncResult
                revoke_result;

            try {
                revoke_result =
                    config_.sync_projection(
                        tombstone
                    );
            } catch (...) {
                ++result.failed;
                continue;
            }

            if (!sync_succeeded(
                    revoke_result.code)) {
                // Security: do not establish the new Node projection while the
                // last confirmed Node remains unreconciled.
                ++result.failed;
                continue;
            }

            // RT009: the previous Node has accepted its tombstone. Persist B
            // before B can receive authorization, so B survives a Director
            // crash or confirmed-state persistence failure.
            if (!prepare_pending_authorization_transition(
                    config_,
                    current,
                    &result)) {
                continue;
            }

            StorageUserProjectionSyncResult
                current_result;

            try {
                current_result =
                    config_.sync_projection(
                        current
                    );
            } catch (...) {
                ++result.failed;
                continue;
            }

            if (!sync_succeeded(
                    current_result.code)) {
                // Keep pending B: transport failure cannot prove that B did
                // not accept the authorization.
                ++result.failed;
                continue;
            }

            if (!persist_confirmed_state(
                    config_.state_store,
                    current,
                    &result)) {
                // Keep pending B for restart recovery.
                continue;
            }

            const auto pending_current =
                projection_state_from_projection(
                    current
                );

            if (!erase_pending_transition(
                    config_.transition_store,
                    pending_current,
                    &result)) {
                continue;
            }

            emit_reconcile_audit_best_effort(
                config_,
                "cluster.storage_projection.moved",
                fingerprint,
                confirmed->revision,
                current.revision,
                confirmed->home_node,
                current.home_node
            );

            ++result.moved;
            continue;
        }

        StorageUserProjectionSyncResult
            sync_result;

        try {
            sync_result =
                config_.sync_projection(
                    current
                );
        } catch (...) {
            ++result.failed;
            continue;
        }

        if (!sync_succeeded(
                sync_result.code)) {
            ++result.failed;
            continue;
        }

        if (persist_confirmed_state(
                config_.state_store,
                current,
                &result)) {
            emit_reconcile_audit_best_effort(
                config_,
                "cluster.storage_projection.synced",
                fingerprint,
                confirmed->revision,
                current.revision,
                confirmed->home_node,
                current.home_node
            );

            ++result.synced_current;
        }
    }

    // RT-008 range proof runs only after ordinary RT-009 recovery/move/revoke
    // work completed successfully. Pre-auditing could mistake a known pending
    // authorization for an orphan and block the revoke needed to converge it.
    if (result.failed == 0 &&
        !audit_nodes.empty()) {
        std::map<
            std::string,
            std::vector<
                StorageUserProjectionAuditExpected
            >
        > expected_by_node;

        for (const auto& node_id :
             audit_nodes) {
            expected_by_node.emplace(
                node_id,
                std::vector<
                    StorageUserProjectionAuditExpected
                >{}
            );
        }

        bool expected_build_failed = false;

        for (const auto& entry :
             users) {
            const pqnas::UserRec& user =
                entry.second;

            if (user.home_node.empty() ||
                audit_capable_nodes.find(
                    user.home_node) ==
                    audit_capable_nodes.end()) {
                continue;
            }

            StorageUserProjection projection;
            std::string build_error;

            if (!build_storage_user_projection(
                    user,
                    &projection,
                    &build_error)) {
                build_error.clear();
                ++result.state_conflicts;
                ++result.failed;
                expected_build_failed = true;
                break;
            }

            std::string digest;
            std::string digest_error;

            if (!storage_user_projection_digest_v1(
                    projection,
                    &digest,
                    &digest_error)) {
                digest_error.clear();
                ++result.state_conflicts;
                ++result.failed;
                expected_build_failed = true;
                break;
            }

            StorageUserProjectionAuditExpected expected;
            expected.fingerprint =
                projection.fingerprint;
            expected.revision =
                projection.revision;
            expected.digest_hex =
                std::move(digest);

            expected_by_node[
                projection.home_node
            ].push_back(
                std::move(expected)
            );
        }

        struct AuditObservation {
            StorageUserProjectionAuditRequest request;
            StorageUserProjectionAuditResult result;
        };

        std::vector<AuditObservation>
            observations;

        bool audit_failed =
            expected_build_failed;

        if (!audit_failed) {
            for (const auto& node_id :
                 audit_nodes) {
                std::vector<
                    StorageUserProjectionAuditRequest
                > requests;

                if (!build_storage_user_projection_reconcile_audit_requests(
                        node_id,
                        expected_by_node[node_id],
                        &requests)) {
                    ++result.state_conflicts;
                    ++result.failed;
                    audit_failed = true;
                    break;
                }

                for (const auto& request :
                     requests) {
                    StorageUserProjectionAuditClientResult
                        audit_result;

                    try {
                        audit_result =
                            config_.audit_projection_range(
                                request
                            );
                    } catch (...) {
                        ++result.failed;
                        audit_failed = true;
                        break;
                    }

                    if (audit_result.code !=
                            StorageUserProjectionAuditClientCode::Ok ||
                        !audit_result.result.complete) {
                        ++result.failed;
                        audit_failed = true;
                        break;
                    }

                    if (!audit_result_consistent(
                            request,
                            audit_result.result)) {
                        ++result.state_conflicts;
                        ++result.failed;
                        audit_failed = true;
                        break;
                    }

                    observations.push_back(
                        AuditObservation{
                            request,
                            std::move(
                                audit_result.result
                            )
                        }
                    );
                }

                if (audit_failed) {
                    break;
                }
            }
        }

        std::unordered_set<std::string>
            repair_fingerprints;

        if (!audit_failed) {
            for (const auto& observation :
                 observations) {
                for (const auto& mismatch :
                     observation.result.mismatches) {
                    switch (mismatch.code) {
                        case StorageUserProjectionAuditMismatchCode::Missing:
                        case StorageUserProjectionAuditMismatchCode::Stale: {
                            const auto deferred =
                                deferred_equal.find(
                                    mismatch.fingerprint
                                );

                            if (deferred ==
                                    deferred_equal.end() ||
                                deferred->second.home_node !=
                                    observation.request.node_id ||
                                deferred->second.revision !=
                                    mismatch.expected_revision ||
                                !repair_fingerprints.insert(
                                    mismatch.fingerprint
                                ).second) {
                                ++result.state_conflicts;
                                ++result.failed;
                                audit_failed = true;
                            }

                            break;
                        }

                        case StorageUserProjectionAuditMismatchCode::Conflict:
                        case StorageUserProjectionAuditMismatchCode::
                                WrongNodeState:
                            ++result.state_conflicts;
                            ++result.failed;
                            audit_failed = true;
                            break;

                        case StorageUserProjectionAuditMismatchCode::Newer:
                            ++result.stale_states;
                            ++result.failed;
                            audit_failed = true;
                            break;

                        case StorageUserProjectionAuditMismatchCode::
                                UnexpectedAuthorization:
                            if (current_fingerprints.find(
                                    mismatch.fingerprint) ==
                                current_fingerprints.end()) {
                                ++result.orphaned_states;
                            } else {
                                ++result.state_conflicts;
                            }

                            ++result.failed;
                            audit_failed = true;
                            break;

                        default:
                            ++result.state_conflicts;
                            ++result.failed;
                            audit_failed = true;
                            break;
                    }

                    if (audit_failed) {
                        break;
                    }
                }

                if (audit_failed) {
                    break;
                }
            }
        }

        // Missing/stale is auto-healed only for a projection that was already
        // confirmed equal on this exact Node/revision before the audit. This is
        // the original RT-008 rollback case and does not authorize a new Node.
        if (!audit_failed &&
            !repair_fingerprints.empty()) {
            std::vector<std::string>
                repairs(
                    repair_fingerprints.begin(),
                    repair_fingerprints.end()
                );

            std::sort(
                repairs.begin(),
                repairs.end()
            );

            for (const auto& fingerprint :
                 repairs) {
                const auto deferred =
                    deferred_equal.find(
                        fingerprint
                    );

                if (deferred ==
                    deferred_equal.end()) {
                    ++result.state_conflicts;
                    ++result.failed;
                    audit_failed = true;
                    break;
                }

                StorageUserProjectionSyncResult
                    repair_result;

                try {
                    repair_result =
                        config_.sync_projection(
                            deferred->second
                        );
                } catch (...) {
                    ++result.failed;
                    audit_failed = true;
                    break;
                }

                if (!sync_succeeded(
                        repair_result.code)) {
                    ++result.failed;
                    audit_failed = true;
                    break;
                }

                const auto state =
                    projection_state_from_projection(
                        deferred->second
                    );

                if (!heal_confirmed_state_durability(
                        config_.state_store,
                        state,
                        &result)) {
                    audit_failed = true;
                    break;
                }

                emit_reconcile_audit_best_effort(
                    config_,
                    "cluster.storage_projection.synced",
                    fingerprint,
                    deferred->second.revision,
                    deferred->second.revision,
                    deferred->second.home_node,
                    deferred->second.home_node
                );

                ++result.synced_current;
            }
        }

        // Equal/current projections with no mismatch are already synchronized
        // only now: the Storage range proof, not Director durable memory,
        // establishes their current authorization state.
        if (!audit_failed) {
            std::vector<std::string>
                deferred_fingerprints;

            deferred_fingerprints.reserve(
                deferred_equal.size()
            );

            for (const auto& entry :
                 deferred_equal) {
                deferred_fingerprints.push_back(
                    entry.first
                );
            }

            std::sort(
                deferred_fingerprints.begin(),
                deferred_fingerprints.end()
            );

            for (const auto& fingerprint :
                 deferred_fingerprints) {
                if (repair_fingerprints.find(
                        fingerprint) !=
                    repair_fingerprints.end()) {
                    continue;
                }

                const auto deferred =
                    deferred_equal.find(
                        fingerprint
                    );

                if (deferred ==
                    deferred_equal.end()) {
                    ++result.state_conflicts;
                    ++result.failed;
                    break;
                }

                const auto state =
                    projection_state_from_projection(
                        deferred->second
                    );

                if (heal_confirmed_state_durability(
                        config_.state_store,
                        state,
                        &result)) {
                    ++result.already_synced;
                }
            }
        }
    }

    result.ok =
        result.failed == 0;

    return result;
}

} // namespace pqnas::cluster
