#include "cluster/storage_user_projection_audit.h"

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

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

std::string hex_fingerprint(
    std::uint64_t value
) {
    std::ostringstream output;

    output
        << std::hex
        << std::nouppercase
        << std::setfill('0')
        << std::setw(128)
        << value;

    return output.str();
}

pqnas::cluster::StorageUserProjection
allocated_projection(
    const std::string& fingerprint,
    std::uint64_t revision,
    const std::string& home_node =
        "storage-01"
) {
    pqnas::cluster::StorageUserProjection
        projection;

    projection.fingerprint =
        fingerprint;
    projection.enabled = true;
    projection.storage_allocated = true;
    projection.quota_bytes = 123456789;

    projection.root_rel =
        "users/" +
        projection.fingerprint;

    projection.storage_pool_id =
        "pool_01";

    projection.home_node =
        home_node;

    projection.revision =
        revision;

    return projection;
}

pqnas::cluster::StorageUserProjection
tombstone_projection(
    const std::string& fingerprint,
    std::uint64_t revision,
    const std::string& home_node =
        "storage-01"
) {
    pqnas::cluster::StorageUserProjection
        projection;

    projection.fingerprint =
        fingerprint;
    projection.enabled = false;
    projection.storage_allocated = false;
    projection.quota_bytes = 0;
    projection.root_rel.clear();
    projection.storage_pool_id.clear();
    projection.home_node =
        home_node;
    projection.revision =
        revision;

    return projection;
}

std::string digest_of(
    const pqnas::cluster::StorageUserProjection&
        projection
) {
    std::string digest;
    std::string error;

    const bool ok =
        pqnas::cluster::
            storage_user_projection_digest_v1(
                projection,
                &digest,
                &error
            );

    expect(
        ok,
        "projection digest succeeds: " +
            error
    );

    return digest;
}

pqnas::cluster::StorageUserProjectionAuditExpected
expected_from(
    const pqnas::cluster::StorageUserProjection&
        projection
) {
    pqnas::cluster::
        StorageUserProjectionAuditExpected
            expected;

    expected.fingerprint =
        projection.fingerprint;
    expected.revision =
        projection.revision;
    expected.digest_hex =
        digest_of(projection);

    return expected;
}

bool audit(
    const std::string& range_start,
    const std::string& range_end,
    const std::vector<
        pqnas::cluster::
            StorageUserProjectionAuditExpected
    >& expected,
    const std::vector<
        pqnas::cluster::StorageUserProjection
    >& snapshot,
    pqnas::cluster::
        StorageUserProjectionAuditResult* result,
    std::string* error
) {
    return pqnas::cluster::
        audit_storage_user_projection_range(
            "storage-01",
            range_start,
            range_end,
            expected,
            snapshot,
            result,
            error
        );
}

void expect_one_mismatch(
    const pqnas::cluster::
        StorageUserProjectionAuditResult& result,
    pqnas::cluster::
        StorageUserProjectionAuditMismatchCode code,
    const std::string& message
) {
    expect(
        result.mismatches.size() == 1,
        message +
            ": exactly one mismatch"
    );

    if (result.mismatches.size() == 1) {
        expect(
            result.mismatches[0].code ==
                code,
            message +
                ": mismatch code"
        );
    }
}

} // namespace

int main() {
    using pqnas::cluster::
        StorageUserProjectionAuditMismatchCode;
    using pqnas::cluster::
        StorageUserProjectionAuditResult;

    const std::string fingerprint_a(
        128,
        'a'
    );

    const std::string fingerprint_b(
        128,
        'b'
    );

    const std::string fingerprint_c(
        128,
        'c'
    );

    const auto base =
        allocated_projection(
            fingerprint_a,
            42
        );

    {
        const std::string digest =
            digest_of(base);

        expect(
            digest ==
                "1602f3735bc7bbbf974fe282700f114c"
                "3853ab8b3e126111798ed6aa12cec4d7",
            "canonical digest fixed vector"
        );
    }

    {
        const std::string base_digest =
            digest_of(base);

        auto changed =
            base;

        changed.enabled = false;

        expect(
            digest_of(changed) !=
                base_digest,
            "enabled participates in digest"
        );

        changed =
            tombstone_projection(
                fingerprint_a,
                42
            );

        expect(
            digest_of(changed) !=
                base_digest,
            "allocation state participates in digest"
        );

        changed =
            base;
        ++changed.quota_bytes;

        expect(
            digest_of(changed) !=
                base_digest,
            "quota participates in digest"
        );

        changed =
            base;
        changed.root_rel +=
            "/data";

        expect(
            digest_of(changed) !=
                base_digest,
            "root participates in digest"
        );

        changed =
            base;
        changed.storage_pool_id =
            "pool_02";

        expect(
            digest_of(changed) !=
                base_digest,
            "storage pool participates in digest"
        );

        changed =
            base;
        changed.home_node =
            "storage-02";

        expect(
            digest_of(changed) !=
                base_digest,
            "home node participates in digest"
        );

        changed =
            base;
        ++changed.revision;

        expect(
            digest_of(changed) !=
                base_digest,
            "revision participates in digest"
        );

        changed =
            allocated_projection(
                fingerprint_b,
                42
            );

        expect(
            digest_of(changed) !=
                base_digest,
            "fingerprint participates in digest"
        );
    }

    {
        std::string digest;
        std::string error;

        auto invalid =
            base;
        invalid.revision = 0;

        expect(
            !pqnas::cluster::
                storage_user_projection_digest_v1(
                    invalid,
                    &digest,
                    &error
                ),
            "zero revision digest fails closed"
        );

        invalid =
            base;
        invalid.fingerprint[0] = 'A';
        error.clear();

        expect(
            !pqnas::cluster::
                storage_user_projection_digest_v1(
                    invalid,
                    &digest,
                    &error
                ),
            "non-canonical fingerprint digest fails closed"
        );

        invalid =
            tombstone_projection(
                fingerprint_a,
                42
            );

        invalid.quota_bytes = 1;
        error.clear();

        expect(
            !pqnas::cluster::
                storage_user_projection_digest_v1(
                    invalid,
                    &digest,
                    &error
                ),
            "non-canonical tombstone digest fails closed"
        );
    }

    {
        const auto second =
            allocated_projection(
                fingerprint_b,
                17
            );

        const std::vector<
            pqnas::cluster::
                StorageUserProjectionAuditExpected
        > expected{
            expected_from(base),
            expected_from(second)
        };

        const std::vector<
            pqnas::cluster::StorageUserProjection
        > snapshot{
            base,
            second
        };

        StorageUserProjectionAuditResult
            result;

        std::string error;

        expect(
            audit(
                "",
                "",
                expected,
                snapshot,
                &result,
                &error
            ),
            "matching audit succeeds: " +
                error
        );

        expect(
            result.complete &&
            result.matched == 2 &&
            result.mismatches.empty(),
            "matching audit verifies entire range"
        );
    }

    {
        const auto second =
            allocated_projection(
                fingerprint_b,
                17
            );

        const std::vector<
            pqnas::cluster::
                StorageUserProjectionAuditExpected
        > expected{
            expected_from(base),
            expected_from(second)
        };

        const std::vector<
            pqnas::cluster::StorageUserProjection
        > snapshot{
            base
        };

        StorageUserProjectionAuditResult
            result;

        std::string error;

        expect(
            audit(
                "",
                "",
                expected,
                snapshot,
                &result,
                &error
            ),
            "missing projection audit succeeds: " +
                error
        );

        expect_one_mismatch(
            result,
            StorageUserProjectionAuditMismatchCode::
                Missing,
            "missing projection detected"
        );
    }

    {
        const auto expected_projection =
            allocated_projection(
                fingerprint_a,
                42
            );

        const auto stale =
            allocated_projection(
                fingerprint_a,
                41
            );

        StorageUserProjectionAuditResult
            result;

        std::string error;

        expect(
            audit(
                "",
                "",
                {
                    expected_from(
                        expected_projection
                    )
                },
                {stale},
                &result,
                &error
            ),
            "stale projection audit succeeds: " +
                error
        );

        expect_one_mismatch(
            result,
            StorageUserProjectionAuditMismatchCode::
                Stale,
            "stale projection detected"
        );
    }

    {
        const auto expected_projection =
            allocated_projection(
                fingerprint_a,
                42
            );

        const auto newer =
            allocated_projection(
                fingerprint_a,
                43
            );

        StorageUserProjectionAuditResult
            result;

        std::string error;

        expect(
            audit(
                "",
                "",
                {
                    expected_from(
                        expected_projection
                    )
                },
                {newer},
                &result,
                &error
            ),
            "newer projection audit succeeds: " +
                error
        );

        expect_one_mismatch(
            result,
            StorageUserProjectionAuditMismatchCode::
                Newer,
            "newer Storage revision detected"
        );
    }

    {
        const auto expected_projection =
            allocated_projection(
                fingerprint_a,
                42
            );

        auto conflict =
            expected_projection;
        ++conflict.quota_bytes;

        StorageUserProjectionAuditResult
            result;

        std::string error;

        expect(
            audit(
                "",
                "",
                {
                    expected_from(
                        expected_projection
                    )
                },
                {conflict},
                &result,
                &error
            ),
            "conflicting projection audit succeeds: " +
                error
        );

        expect_one_mismatch(
            result,
            StorageUserProjectionAuditMismatchCode::
                Conflict,
            "same revision content conflict detected"
        );
    }

    {
        const auto expected_projection =
            allocated_projection(
                fingerprint_a,
                42
            );

        const auto wrong_node =
            allocated_projection(
                fingerprint_a,
                42,
                "storage-02"
            );

        StorageUserProjectionAuditResult
            result;

        std::string error;

        expect(
            audit(
                "",
                "",
                {
                    expected_from(
                        expected_projection
                    )
                },
                {wrong_node},
                &result,
                &error
            ),
            "wrong-node projection audit succeeds: " +
                error
        );

        expect_one_mismatch(
            result,
            StorageUserProjectionAuditMismatchCode::
                WrongNodeState,
            "wrong-node projection detected"
        );
    }

    {
        const auto unexpected =
            allocated_projection(
                fingerprint_a,
                41
            );

        StorageUserProjectionAuditResult
            result;

        std::string error;

        expect(
            audit(
                "",
                "",
                {},
                {unexpected},
                &result,
                &error
            ),
            "unexpected live projection audit succeeds: " +
                error
        );

        expect_one_mismatch(
            result,
            StorageUserProjectionAuditMismatchCode::
                UnexpectedAuthorization,
            "unexpected live authorization detected"
        );
    }

    {
        const auto tombstone =
            tombstone_projection(
                fingerprint_a,
                42
            );

        StorageUserProjectionAuditResult
            result;

        std::string error;

        expect(
            audit(
                "",
                "",
                {},
                {tombstone},
                &result,
                &error
            ),
            "retained tombstone audit succeeds: " +
                error
        );

        expect(
            result.complete &&
            result.mismatches.empty() &&
            result.safe_tombstones_ignored == 1,
            "canonical retained tombstone is safe"
        );
    }

    {
        const auto outside =
            allocated_projection(
                fingerprint_a,
                1
            );

        const auto inside =
            allocated_projection(
                fingerprint_c,
                2
            );

        StorageUserProjectionAuditResult
            result;

        std::string error;

        expect(
            audit(
                fingerprint_b,
                "",
                {
                    expected_from(
                        inside
                    )
                },
                {
                    outside,
                    inside
                },
                &result,
                &error
            ),
            "bounded lexical range audit succeeds: " +
                error
        );

        expect(
            result.complete &&
            result.matched == 1 &&
            result.mismatches.empty(),
            "entries below range_start are ignored"
        );
    }

    {
        StorageUserProjectionAuditResult
            result;

        std::string error;

        const auto first =
            allocated_projection(
                fingerprint_a,
                1
            );

        const auto second =
            allocated_projection(
                fingerprint_b,
                1
            );

        expect(
            !audit(
                "",
                "",
                {
                    expected_from(second),
                    expected_from(first)
                },
                {
                    first,
                    second
                },
                &result,
                &error
            ),
            "unsorted expected list fails closed"
        );
    }

    {
        std::vector<
            pqnas::cluster::StorageUserProjection
        > snapshot;

        snapshot.reserve(
            pqnas::cluster::
                kStorageUserProjectionAuditMaxMismatches +
            1
        );

        for (std::size_t index = 0;
             index <
                 pqnas::cluster::
                     kStorageUserProjectionAuditMaxMismatches +
                 1;
             ++index) {
            snapshot.push_back(
                allocated_projection(
                    hex_fingerprint(
                        static_cast<std::uint64_t>(
                            index
                        )
                    ),
                    1
                )
            );
        }

        StorageUserProjectionAuditResult
            result;

        std::string error;

        expect(
            audit(
                "",
                "",
                {},
                snapshot,
                &result,
                &error
            ),
            "bounded mismatch audit succeeds: " +
                error
        );

        expect(
            !result.complete,
            "mismatch overflow marks range incomplete"
        );

        expect(
            result.mismatches.size() ==
                pqnas::cluster::
                    kStorageUserProjectionAuditMaxMismatches,
            "mismatch response remains bounded"
        );
    }

    if (failures != 0) {
        std::cerr
            << "FAIL: "
            << failures
            << " storage user projection audit test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: storage user projection audit tests passed\n";

    return 0;
}
