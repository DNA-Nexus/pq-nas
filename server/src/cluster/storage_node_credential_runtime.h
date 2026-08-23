#pragma once

#include "cluster/internal_auth.h"

#include <filesystem>
#include <mutex>
#include <string>

namespace pqnas::cluster {

enum class StorageNodeCredentialRuntimeResult {
    Ok,
    InvalidInput,
    NotLoaded,
    AuthenticationFailed,
    NoPending,
    StateMismatch,
    CredentialInvalid,
    StorageUnsafe,
    IoFailed,
    PersistFailed
};

const char* storage_node_credential_runtime_result_code(
    StorageNodeCredentialRuntimeResult result
) noexcept;

// Owns the Storage Node's service-to-service authentication state.
//
// Security model:
//
//   active only:
//       accept ACTIVE
//
//   rotation pending:
//       accept ACTIVE + PENDING
//
//   finalized:
//       accept new ACTIVE only
//
// The pending credential is persisted before it becomes acceptable in memory.
// Finalization replaces the active credential using:
//
//   write temp -> fsync(temp) -> rename -> fsync(parent)
//
// The pending recovery anchor is removed only after the new active credential
// has become durably authoritative.
class StorageNodeCredentialRuntime {
public:
    StorageNodeCredentialRuntime() = default;
    ~StorageNodeCredentialRuntime();

    StorageNodeCredentialRuntime(
        const StorageNodeCredentialRuntime&
    ) = delete;

    StorageNodeCredentialRuntime& operator=(
        const StorageNodeCredentialRuntime&
    ) = delete;

    // Loads the active credential and, if present, its fixed sibling:
    //
    //     <active filename>.pending
    //
    // A pending credential is therefore recovered across restart without a
    // separately configurable path.
    bool load(
        const std::string& active_credential_file,
        std::string* error
    );

    // Thread-safe verifier used by Storage internal HTTP routes.
    //
    // Both comparisons are attempted so route code does not need to know
    // whether a rotation is currently pending.
    bool verify_authorization(
        const std::string& authorization
    ) const;

    // Creates and durably persists a new pending credential.
    //
    // Repeating this call while the same pending file exists returns the
    // existing pending token, making prepare crash/retry safe.
    //
    // The plaintext output is security-sensitive and must be cleared with
    // clear_internal_auth_token_text() by the caller.
    StorageNodeCredentialRuntimeResult
    prepare_rotation_authorized(
        const std::string& authorization,
        std::string* pending_token,
        std::string* error
    );

    // Promotes the pending credential to active and durably removes the
    // recovery anchor.
    //
    // Security:
    // - while PENDING exists, only PENDING may authorize finalization
    // - OLD therefore cannot retire itself before Director proves it is
    //   already using NEW
    // - after completion, the current ACTIVE credential may retry and receives
    //   NoPending, making a lost finalize response recoverable
    StorageNodeCredentialRuntimeResult
    finalize_rotation_authorized(
        const std::string& authorization,
        std::string* error
    );

    bool rotation_pending() const;

private:
    void reset_unlocked() noexcept;

    StorageNodeCredentialRuntimeResult
    prepare_rotation_unlocked(
        std::string* pending_token,
        std::string* error
    );

    StorageNodeCredentialRuntimeResult
    finalize_rotation_unlocked(
        std::string* error
    );

    mutable std::mutex mutex_;

    int directory_descriptor_ = -1;

    std::filesystem::path directory_;

    std::string active_filename_;
    std::string pending_filename_;
    std::string temporary_filename_;

    InternalAuthCredential active_;
    InternalAuthCredential pending_;

    bool loaded_ = false;

    // True only after the active file is known durably to contain PENDING.
    bool pending_matches_active_ = false;

    // Tracks unlink(pending) succeeding before parent-directory fsync.
    bool pending_unlink_uncertain_ = false;
};

} // namespace pqnas::cluster
