#pragma once

#include "cluster/storage_user_projection_store.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pqnas::cluster {

inline constexpr std::size_t
    kStorageUserProjectionAuditMaxExpected = 512;

inline constexpr std::size_t
    kStorageUserProjectionAuditMaxMismatches = 512;

inline constexpr std::size_t
    kStorageUserProjectionDigestHexChars = 64;

enum class StorageUserProjectionAuditMismatchCode {
    Missing,
    Stale,
    Conflict,
    Newer,
    WrongNodeState,
    UnexpectedAuthorization
};

const char* storage_user_projection_audit_mismatch_code(
    StorageUserProjectionAuditMismatchCode code
) noexcept;

struct StorageUserProjectionAuditExpected {
    std::string fingerprint;
    std::uint64_t revision = 0;
    std::string digest_hex;
};

struct StorageUserProjectionAuditMismatch {
    StorageUserProjectionAuditMismatchCode code =
        StorageUserProjectionAuditMismatchCode::Conflict;

    std::string fingerprint;
    std::uint64_t expected_revision = 0;
    std::uint64_t actual_revision = 0;
};

struct StorageUserProjectionAuditResult {
    // False means the mismatch list hit its bounded response cap. Callers must
    // never treat an incomplete range as verified.
    bool complete = true;

    std::size_t matched = 0;
    std::size_t safe_tombstones_ignored = 0;

    std::vector<StorageUserProjectionAuditMismatch>
        mismatches;
};

// Canonical SHA-256 identity for one Storage authorization projection.
//
// Security: the digest is over a versioned binary encoding with explicit
// big-endian lengths and integers. It does not depend on JSON formatting.
bool storage_user_projection_digest_v1(
    const StorageUserProjection& projection,
    std::string* digest_hex,
    std::string* error
);

// Compare one complete lexical fingerprint range from the Storage snapshot
// against Director-provided expected projection identities.
//
// range_start is inclusive; range_end is exclusive. Empty boundaries mean the
// beginning/end of the full fingerprint namespace.
//
// Security: this function is read-only. It never mutates projection state and
// returns actual projection contents only as bounded mismatch metadata.
bool audit_storage_user_projection_range(
    const std::string& local_node_id,
    const std::string& range_start,
    const std::string& range_end,
    const std::vector<StorageUserProjectionAuditExpected>& expected,
    const std::vector<StorageUserProjection>& snapshot,
    StorageUserProjectionAuditResult* result,
    std::string* error
);

} // namespace pqnas::cluster
