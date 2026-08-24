#include "cluster/storage_user_projection_audit.h"

#include "cluster/cluster_identity_validation.h"
#include "storage_metadata_validation.h"
#include "storage_path_validation.h"

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <openssl/evp.h>
#include <string>
#include <utility>

namespace pqnas::cluster {
namespace {

void set_error(
    std::string* error,
    const std::string& message
) {
    if (error) {
        *error = message;
    }
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

bool valid_projection_for_audit(
    const StorageUserProjection& projection
) {
    if (!pqnas::valid_storage_user_fingerprint_hex(
            projection.fingerprint) ||
        !valid_cluster_node_id(
            projection.home_node) ||
        projection.revision == 0 ||
        !pqnas::valid_storage_pool_reference(
            projection.storage_pool_id) ||
        projection.root_rel.size() >
            kStorageUserProjectionRootRelMaxBytes) {
        return false;
    }

    if (!projection.storage_allocated) {
        return
            projection.quota_bytes == 0 &&
            projection.root_rel.empty() &&
            projection.storage_pool_id.empty();
    }

    if (projection.root_rel.empty()) {
        return false;
    }

    std::string normalized_root;
    std::string path_error;

    return
        pqnas::normalize_storage_relative_path_strict(
            projection.root_rel,
            &normalized_root,
            &path_error
        ) &&
        normalized_root == projection.root_rel;
}

std::array<unsigned char, 4> encode_u32_be(
    std::uint32_t value
) {
    return {
        static_cast<unsigned char>(
            (value >> 24) & 0xffu
        ),
        static_cast<unsigned char>(
            (value >> 16) & 0xffu
        ),
        static_cast<unsigned char>(
            (value >> 8) & 0xffu
        ),
        static_cast<unsigned char>(
            value & 0xffu
        )
    };
}

std::array<unsigned char, 8> encode_u64_be(
    std::uint64_t value
) {
    return {
        static_cast<unsigned char>(
            (value >> 56) & 0xffu
        ),
        static_cast<unsigned char>(
            (value >> 48) & 0xffu
        ),
        static_cast<unsigned char>(
            (value >> 40) & 0xffu
        ),
        static_cast<unsigned char>(
            (value >> 32) & 0xffu
        ),
        static_cast<unsigned char>(
            (value >> 24) & 0xffu
        ),
        static_cast<unsigned char>(
            (value >> 16) & 0xffu
        ),
        static_cast<unsigned char>(
            (value >> 8) & 0xffu
        ),
        static_cast<unsigned char>(
            value & 0xffu
        )
    };
}

bool digest_update(
    EVP_MD_CTX* context,
    const void* data,
    std::size_t size
) {
    if (!context) {
        return false;
    }

    if (size == 0) {
        return true;
    }

    return
        EVP_DigestUpdate(
            context,
            data,
            size
        ) == 1;
}

bool digest_u64(
    EVP_MD_CTX* context,
    std::uint64_t value
) {
    const auto encoded =
        encode_u64_be(value);

    return digest_update(
        context,
        encoded.data(),
        encoded.size()
    );
}

bool digest_string(
    EVP_MD_CTX* context,
    const std::string& value
) {
    if (value.size() >
        static_cast<std::size_t>(
            std::numeric_limits<
                std::uint32_t
            >::max())) {
        return false;
    }

    const auto length =
        encode_u32_be(
            static_cast<std::uint32_t>(
                value.size()
            )
        );

    return
        digest_update(
            context,
            length.data(),
            length.size()
        ) &&
        digest_update(
            context,
            value.data(),
            value.size()
        );
}

bool fingerprint_in_range(
    const std::string& fingerprint,
    const std::string& range_start,
    const std::string& range_end
) {
    return
        (range_start.empty() ||
         fingerprint >= range_start) &&
        (range_end.empty() ||
         fingerprint < range_end);
}

void append_mismatch(
    StorageUserProjectionAuditResult* result,
    StorageUserProjectionAuditMismatchCode code,
    const std::string& fingerprint,
    std::uint64_t expected_revision,
    std::uint64_t actual_revision
) {
    if (!result) {
        return;
    }

    if (result->mismatches.size() >=
        kStorageUserProjectionAuditMaxMismatches) {
        result->complete = false;
        return;
    }

    StorageUserProjectionAuditMismatch
        mismatch;

    mismatch.code = code;
    mismatch.fingerprint = fingerprint;
    mismatch.expected_revision =
        expected_revision;
    mismatch.actual_revision =
        actual_revision;

    result->mismatches.push_back(
        std::move(mismatch)
    );
}

} // namespace

const char* storage_user_projection_audit_mismatch_code(
    StorageUserProjectionAuditMismatchCode code
) noexcept {
    switch (code) {
        case StorageUserProjectionAuditMismatchCode::
                Missing:
            return "missing";
        case StorageUserProjectionAuditMismatchCode::
                Stale:
            return "stale";
        case StorageUserProjectionAuditMismatchCode::
                Conflict:
            return "conflict";
        case StorageUserProjectionAuditMismatchCode::
                Newer:
            return "newer";
        case StorageUserProjectionAuditMismatchCode::
                WrongNodeState:
            return "wrong_node_state";
        case StorageUserProjectionAuditMismatchCode::
                UnexpectedAuthorization:
            return "unexpected_authorization";
    }

    return "unknown";
}

bool storage_user_projection_digest_v1(
    const StorageUserProjection& projection,
    std::string* digest_hex,
    std::string* error
) {
    if (error) {
        error->clear();
    }

    if (!digest_hex) {
        set_error(
            error,
            "missing storage projection digest output"
        );
        return false;
    }

    digest_hex->clear();

    if (!valid_projection_for_audit(
            projection)) {
        set_error(
            error,
            "invalid storage projection for digest"
        );
        return false;
    }

    using EvpContext =
        std::unique_ptr<
            EVP_MD_CTX,
            decltype(&EVP_MD_CTX_free)
        >;

    EvpContext context(
        EVP_MD_CTX_new(),
        &EVP_MD_CTX_free
    );

    if (!context ||
        EVP_DigestInit_ex(
            context.get(),
            EVP_sha256(),
            nullptr
        ) != 1) {
        set_error(
            error,
            "storage projection digest failed"
        );
        return false;
    }

    // Security: include the terminating NUL in the domain separator so a
    // future encoding cannot share this hash namespace by string-prefix
    // accident.
    static constexpr char domain[] =
        "DNA-NEXUS-STORAGE-PROJECTION-V1";

    const std::array<unsigned char, 2>
        flags{
            static_cast<unsigned char>(
                projection.enabled ? 1 : 0
            ),
            static_cast<unsigned char>(
                projection.storage_allocated
                    ? 1
                    : 0
            )
        };

    const bool updated =
        digest_update(
            context.get(),
            domain,
            sizeof(domain)
        ) &&
        digest_string(
            context.get(),
            projection.fingerprint
        ) &&
        digest_update(
            context.get(),
            flags.data(),
            flags.size()
        ) &&
        digest_u64(
            context.get(),
            projection.quota_bytes
        ) &&
        digest_string(
            context.get(),
            projection.root_rel
        ) &&
        digest_string(
            context.get(),
            projection.storage_pool_id
        ) &&
        digest_string(
            context.get(),
            projection.home_node
        ) &&
        digest_u64(
            context.get(),
            projection.revision
        );

    if (!updated) {
        set_error(
            error,
            "storage projection digest failed"
        );
        return false;
    }

    std::array<unsigned char, 32>
        digest{};

    unsigned int digest_size = 0;

    if (EVP_DigestFinal_ex(
            context.get(),
            digest.data(),
            &digest_size) != 1 ||
        digest_size !=
            static_cast<unsigned int>(
                digest.size()
            )) {
        set_error(
            error,
            "storage projection digest failed"
        );
        return false;
    }

    static constexpr char hex[] =
        "0123456789abcdef";

    std::string encoded;
    encoded.resize(
        digest.size() * 2
    );

    for (std::size_t index = 0;
         index < digest.size();
         ++index) {
        encoded[index * 2] =
            hex[
                (digest[index] >> 4) &
                0x0fu
            ];

        encoded[index * 2 + 1] =
            hex[
                digest[index] &
                0x0fu
            ];
    }

    *digest_hex =
        std::move(encoded);

    return true;
}

bool audit_storage_user_projection_range(
    const std::string& local_node_id,
    const std::string& range_start,
    const std::string& range_end,
    const std::vector<StorageUserProjectionAuditExpected>& expected,
    const std::vector<StorageUserProjection>& snapshot,
    StorageUserProjectionAuditResult* result,
    std::string* error
) {
    if (error) {
        error->clear();
    }

    if (!result) {
        set_error(
            error,
            "missing storage projection audit output"
        );
        return false;
    }

    if (!valid_cluster_node_id(
            local_node_id)) {
        set_error(
            error,
            "invalid storage projection audit node"
        );
        return false;
    }

    if ((!range_start.empty() &&
         !pqnas::valid_storage_user_fingerprint_hex(
             range_start)) ||
        (!range_end.empty() &&
         !pqnas::valid_storage_user_fingerprint_hex(
             range_end)) ||
        (!range_start.empty() &&
         !range_end.empty() &&
         range_start >= range_end)) {
        set_error(
            error,
            "invalid storage projection audit range"
        );
        return false;
    }

    if (expected.size() >
        kStorageUserProjectionAuditMaxExpected) {
        set_error(
            error,
            "too many expected storage projections"
        );
        return false;
    }

    std::string previous_expected;
    bool have_previous_expected = false;

    for (const auto& item : expected) {
        if (!pqnas::valid_storage_user_fingerprint_hex(
                item.fingerprint) ||
            item.revision == 0 ||
            !valid_digest_hex(
                item.digest_hex) ||
            !fingerprint_in_range(
                item.fingerprint,
                range_start,
                range_end) ||
            (have_previous_expected &&
             item.fingerprint <=
                 previous_expected)) {
            set_error(
                error,
                "invalid expected storage projection audit entry"
            );
            return false;
        }

        previous_expected =
            item.fingerprint;
        have_previous_expected = true;
    }

    if (snapshot.size() >
        kStorageUserProjectionStoreMaxEntries) {
        set_error(
            error,
            "storage projection snapshot is too large"
        );
        return false;
    }

    std::string previous_actual;
    bool have_previous_actual = false;

    for (const auto& projection : snapshot) {
        if (!pqnas::valid_storage_user_fingerprint_hex(
                projection.fingerprint) ||
            (have_previous_actual &&
             projection.fingerprint <=
                 previous_actual)) {
            set_error(
                error,
                "invalid storage projection snapshot ordering"
            );
            return false;
        }

        previous_actual =
            projection.fingerprint;
        have_previous_actual = true;
    }

    StorageUserProjectionAuditResult
        candidate;

    std::size_t actual_index = 0;

    while (actual_index <
               snapshot.size() &&
           !range_start.empty() &&
           snapshot[actual_index].fingerprint <
               range_start) {
        ++actual_index;
    }

    std::size_t expected_index = 0;

    while (expected_index <
               expected.size() ||
           (actual_index <
                snapshot.size() &&
            (range_end.empty() ||
             snapshot[actual_index].fingerprint <
                 range_end))) {
        const bool have_expected =
            expected_index <
            expected.size();

        const bool have_actual =
            actual_index <
                snapshot.size() &&
            (range_end.empty() ||
             snapshot[actual_index].fingerprint <
                 range_end);

        if (have_expected &&
            (!have_actual ||
             expected[expected_index].fingerprint <
                 snapshot[actual_index].fingerprint)) {
            append_mismatch(
                &candidate,
                StorageUserProjectionAuditMismatchCode::
                    Missing,
                expected[expected_index].fingerprint,
                expected[expected_index].revision,
                0
            );

            ++expected_index;
            continue;
        }

        if (have_actual &&
            (!have_expected ||
             snapshot[actual_index].fingerprint <
                 expected[expected_index].fingerprint)) {
            const StorageUserProjection& actual =
                snapshot[actual_index];

            if (!valid_projection_for_audit(
                    actual)) {
                set_error(
                    error,
                    "invalid storage projection snapshot entry"
                );
                return false;
            }

            if (actual.home_node !=
                local_node_id) {
                append_mismatch(
                    &candidate,
                    StorageUserProjectionAuditMismatchCode::
                        WrongNodeState,
                    actual.fingerprint,
                    0,
                    actual.revision
                );
            } else if (
                !actual.enabled &&
                !actual.storage_allocated
            ) {
                // Security: a canonical disabling tombstone is intentionally
                // retained as a revision floor and is not unexpected live
                // authorization.
                ++candidate.safe_tombstones_ignored;
            } else {
                append_mismatch(
                    &candidate,
                    StorageUserProjectionAuditMismatchCode::
                        UnexpectedAuthorization,
                    actual.fingerprint,
                    0,
                    actual.revision
                );
            }

            ++actual_index;
            continue;
        }

        if (!have_expected ||
            !have_actual) {
            set_error(
                error,
                "storage projection audit comparison failed"
            );
            return false;
        }

        const auto& wanted =
            expected[expected_index];

        const StorageUserProjection& actual =
            snapshot[actual_index];

        if (!valid_projection_for_audit(
                actual)) {
            set_error(
                error,
                "invalid storage projection snapshot entry"
            );
            return false;
        }

        if (actual.home_node !=
            local_node_id) {
            append_mismatch(
                &candidate,
                StorageUserProjectionAuditMismatchCode::
                    WrongNodeState,
                actual.fingerprint,
                wanted.revision,
                actual.revision
            );
        } else if (
            actual.revision <
                wanted.revision
        ) {
            append_mismatch(
                &candidate,
                StorageUserProjectionAuditMismatchCode::
                    Stale,
                actual.fingerprint,
                wanted.revision,
                actual.revision
            );
        } else if (
            actual.revision >
                wanted.revision
        ) {
            append_mismatch(
                &candidate,
                StorageUserProjectionAuditMismatchCode::
                    Newer,
                actual.fingerprint,
                wanted.revision,
                actual.revision
            );
        } else {
            std::string actual_digest;
            std::string digest_error;

            if (!storage_user_projection_digest_v1(
                    actual,
                    &actual_digest,
                    &digest_error)) {
                digest_error.clear();

                set_error(
                    error,
                    "failed to digest storage projection snapshot"
                );
                return false;
            }

            if (actual_digest !=
                wanted.digest_hex) {
                append_mismatch(
                    &candidate,
                    StorageUserProjectionAuditMismatchCode::
                        Conflict,
                    actual.fingerprint,
                    wanted.revision,
                    actual.revision
                );
            } else {
                ++candidate.matched;
            }
        }

        ++expected_index;
        ++actual_index;
    }

    *result =
        std::move(candidate);

    return true;
}

} // namespace pqnas::cluster
