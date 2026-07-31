#include "cluster/node_enrollment_registry.h"

#include <nlohmann/json.hpp>
#include <sodium.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

using pqnas::cluster::NodeEnrollmentProvisioningStage;
using pqnas::cluster::NodeEnrollmentRecord;
using pqnas::cluster::NodeEnrollmentRegistry;
using pqnas::cluster::NodeEnrollmentRegistryResult;
using pqnas::cluster::NodeEnrollmentState;

int failures = 0;

const std::string kHashA(43, 'A');

const std::string kHashB =
    std::string(42, 'B') + "A";

const std::string kHashC =
    std::string(42, 'C') + "A";

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
    NodeEnrollmentRegistryResult actual,
    NodeEnrollmentRegistryResult expected,
    const std::string& message
) {
    expect(
        actual == expected,
        message +
            ": expected " +
            pqnas::cluster::
                node_enrollment_registry_result_code(
                    expected
                ) +
            ", got " +
            pqnas::cluster::
                node_enrollment_registry_result_code(
                    actual
                )
    );
}

fs::path make_test_directory() {
    std::string pattern =
        (
            fs::temp_directory_path() /
            "pqnas-enrollment-registry-test-XXXXXX"
        ).string();

    std::vector<char> writable(
        pattern.begin(),
        pattern.end()
    );

    writable.push_back('\0');

    char* result =
        ::mkdtemp(writable.data());

    if (!result) {
        return {};
    }

    return fs::path(result);
}

bool write_text(
    const fs::path& path,
    const std::string& contents
) {
    std::ofstream output(
        path,
        std::ios::binary |
        std::ios::trunc
    );

    if (!output.good()) {
        return false;
    }

    output.write(
        contents.data(),
        static_cast<std::streamsize>(
            contents.size()
        )
    );

    output.flush();
    return output.good();
}

json pending_record_json(
    const std::string& enrollment_id,
    const std::string& node_id,
    const std::string& url,
    std::uint64_t generation
) {
    return json{
        {"enrollment_id", enrollment_id},
        {"node_id", node_id},
        {"api_base_url", url},
        {"required_protocol_version", 1},
        {"token_hash", kHashA},
        {"created_at", "2026-07-30T06:00:00Z"},
        {"expires_at", "2026-07-30T07:00:00Z"},
        {"state", "pending"},
        {"revoked_at", ""},
        {"consumed_at", ""},
        {"generation", generation}
    };
}

} // namespace

int main() {
    if (sodium_init() < 0) {
        std::cerr
            << "FAIL: sodium initialization failed\n";
        return 1;
    }

    const fs::path test_directory =
        make_test_directory();

    if (test_directory.empty()) {
        std::cerr
            << "FAIL: could not create test directory\n";
        return 1;
    }

    const auto cleanup = [&]() {
        std::error_code error;
        fs::remove_all(
            test_directory,
            error
        );
    };

    const fs::path registry_path =
        test_directory /
        "node_enrollments.json";

    NodeEnrollmentRegistry registry;
    std::string error;

    expect(
        registry.load(
            registry_path.string(),
            &error
        ),
        "missing registry should load as empty: " +
            error
    );

    expect(
        registry.snapshot().empty(),
        "missing registry should produce empty state"
    );

    error.clear();

    expect_result(
        registry.create_or_replace_pending(
            "EnrollmentInvalidHash01",
            "node-invalid-hash",
            "https://node-invalid-hash.internal",
            1,
            std::string(43, 'B'),
            "2026-07-30T05:00:00Z",
            "2026-07-30T06:00:00Z",
            nullptr,
            &error
        ),
        NodeEnrollmentRegistryResult::
            InvalidInput,
        "non-canonical token hash"
    );

    expect(
        registry.snapshot().empty(),
        "invalid verifier must not mutate registry"
    );

    NodeEnrollmentRecord first;

    expect_result(
        registry.create_or_replace_pending(
            "EnrollmentNodeOne0001",
            "node-01",
            "https://node-01.internal/",
            1,
            kHashA,
            "2026-07-30T06:00:00Z",
            "2026-07-30T07:00:00Z",
            &first,
            &error
        ),
        NodeEnrollmentRegistryResult::Ok,
        "create first pending enrollment"
    );

    expect(
        first.state ==
            NodeEnrollmentState::Pending &&
        first.generation == 1 &&
        first.api_base_url ==
            "https://node-01.internal" &&
        first.token_hash == kHashA &&
        first.provisioning_stage ==
            NodeEnrollmentProvisioningStage::None &&
        first.provisioning_started_at.empty(),
        "created record should be canonical pending generation one"
    );

    struct stat registry_stats {};

    expect(
        ::stat(
            registry_path.c_str(),
            &registry_stats
        ) == 0,
        "registry file should exist"
    );

    expect(
        (registry_stats.st_mode & 0077) == 0,
        "registry file must be private"
    );

    json persisted;

    try {
        std::ifstream input(
            registry_path
        );

        input >> persisted;
    } catch (...) {
        expect(
            false,
            "persisted registry should contain JSON"
        );
    }

    expect(
        persisted["version"] ==
            pqnas::cluster::
                kNodeEnrollmentRegistrySchemaVersion,
        "persisted schema version"
    );

    expect(
        persisted["enrollments"].is_array() &&
        persisted["enrollments"].size() == 1 &&
        persisted["enrollments"][0]
            ["token_hash"] == kHashA,
        "pending verifier should persist without plaintext token"
    );

    const auto before_duplicate_id =
        registry.snapshot();

    error.clear();

    expect_result(
        registry.create_or_replace_pending(
            "EnrollmentNodeOne0001",
            "node-99",
            "https://node-99.internal",
            1,
            kHashB,
            "2026-07-30T06:05:00Z",
            "2026-07-30T07:05:00Z",
            nullptr,
            &error
        ),
        NodeEnrollmentRegistryResult::
            DuplicateEnrollmentId,
        "duplicate enrollment ID"
    );

    expect(
        registry.snapshot().size() ==
            before_duplicate_id.size(),
        "duplicate ID must not mutate registry"
    );

    error.clear();

    expect_result(
        registry.create_or_replace_pending(
            "EnrollmentNodeTwo0001",
            "node-02",
            "https://node-01.internal",
            1,
            kHashB,
            "2026-07-30T06:10:00Z",
            "2026-07-30T07:10:00Z",
            nullptr,
            &error
        ),
        NodeEnrollmentRegistryResult::
            DuplicateActiveUrl,
        "active URL must not belong to another node"
    );

    NodeEnrollmentRecord replacement;

    error.clear();

    expect_result(
        registry.create_or_replace_pending(
            "EnrollmentNodeOne0002",
            "node-01",
            "https://node-01.internal",
            1,
            kHashB,
            "2026-07-30T06:15:00Z",
            "2026-07-30T07:15:00Z",
            &replacement,
            &error
        ),
        NodeEnrollmentRegistryResult::Ok,
        "replace pending enrollment"
    );

    expect(
        replacement.generation == 2 &&
        replacement.state ==
            NodeEnrollmentState::Pending,
        "replacement should use generation two"
    );

    const auto revoked_first =
        registry.get(
            "EnrollmentNodeOne0001"
        );

    expect(
        revoked_first.has_value() &&
        revoked_first->state ==
            NodeEnrollmentState::Revoked &&
        revoked_first->revoked_at ==
            "2026-07-30T06:15:00Z" &&
        revoked_first->token_hash.empty(),
        "replacement should atomically revoke and clear old verifier"
    );

    error.clear();

    expect_result(
        registry.prepare_provisioning_if_matches(
            "EnrollmentNodeOne0002",
            "node-01",
            "https://node-01.internal",
            1,
            2,
            kHashA,
            "2026-07-30T06:20:00Z",
            nullptr,
            &error
        ),
        NodeEnrollmentRegistryResult::
            TokenInvalid,
        "wrong token hash"
    );

    error.clear();

    expect_result(
        registry.prepare_provisioning_if_matches(
            "EnrollmentNodeOne0002",
            "node-01",
            "https://node-01.internal",
            1,
            1,
            kHashB,
            "2026-07-30T06:20:00Z",
            nullptr,
            &error
        ),
        NodeEnrollmentRegistryResult::
            GenerationMismatch,
        "stale generation"
    );

    error.clear();

    expect_result(
        registry.prepare_provisioning_if_matches(
            "EnrollmentNodeOne0002",
            "node-other",
            "https://node-01.internal",
            1,
            2,
            kHashB,
            "2026-07-30T06:20:00Z",
            nullptr,
            &error
        ),
        NodeEnrollmentRegistryResult::
            NodeMismatch,
        "node mismatch"
    );

    error.clear();

    expect_result(
        registry.prepare_provisioning_if_matches(
            "EnrollmentNodeOne0002",
            "node-01",
            "https://other.internal",
            1,
            2,
            kHashB,
            "2026-07-30T06:20:00Z",
            nullptr,
            &error
        ),
        NodeEnrollmentRegistryResult::
            UrlMismatch,
        "URL mismatch"
    );

    error.clear();

    expect_result(
        registry.prepare_provisioning_if_matches(
            "EnrollmentNodeOne0002",
            "node-01",
            "https://node-01.internal",
            2,
            2,
            kHashB,
            "2026-07-30T06:20:00Z",
            nullptr,
            &error
        ),
        NodeEnrollmentRegistryResult::
            ProtocolIncompatible,
        "protocol mismatch"
    );

    NodeEnrollmentRecord prepared;

    error.clear();

    expect_result(
        registry.prepare_provisioning_if_matches(
            "EnrollmentNodeOne0002",
            "node-01",
            "https://node-01.internal/",
            1,
            2,
            kHashB,
            "2026-07-30T06:20:00Z",
            &prepared,
            &error
        ),
        NodeEnrollmentRegistryResult::Ok,
        "successful provisioning prepare"
    );

    expect(
        prepared.state ==
            NodeEnrollmentState::Pending &&
        prepared.provisioning_stage ==
            NodeEnrollmentProvisioningStage::
                Prepared &&
        prepared.provisioning_started_at ==
            "2026-07-30T06:20:00Z" &&
        prepared.token_hash == kHashB,
        "prepare should retain verifier and persist authenticated start"
    );

    error.clear();

    expect_result(
        registry.prepare_provisioning_if_matches(
            "EnrollmentNodeOne0002",
            "node-01",
            "https://node-01.internal",
            1,
            2,
            kHashB,
            "2026-07-30T06:20:00Z",
            nullptr,
            &error
        ),
        NodeEnrollmentRegistryResult::
            ProvisioningActive,
        "parallel prepare must fail closed"
    );

    error.clear();

    expect_result(
        registry.create_or_replace_pending(
            "EnrollmentBlocked0001",
            "node-01",
            "https://node-01.internal",
            1,
            kHashC,
            "2026-07-30T06:21:00Z",
            "2026-07-30T07:21:00Z",
            nullptr,
            &error
        ),
        NodeEnrollmentRegistryResult::
            ProvisioningActive,
        "replacement must not revoke active provisioning"
    );

    error.clear();

    expect_result(
        registry.revoke_pending(
            "EnrollmentNodeOne0002",
            2,
            "2026-07-30T06:21:00Z",
            nullptr,
            &error
        ),
        NodeEnrollmentRegistryResult::
            ProvisioningActive,
        "revocation must not interrupt active provisioning"
    );

    error.clear();

    expect_result(
        registry.advance_provisioning_stage(
            "EnrollmentNodeOne0002",
            2,
            NodeEnrollmentProvisioningStage::
                CredentialPersisted,
            NodeEnrollmentProvisioningStage::
                NodeRegistered,
            nullptr,
            &error
        ),
        NodeEnrollmentRegistryResult::
            ProvisioningStageMismatch,
        "stage skipping must fail closed"
    );

    NodeEnrollmentRecord advanced;

    error.clear();

    expect_result(
        registry.advance_provisioning_stage(
            "EnrollmentNodeOne0002",
            2,
            NodeEnrollmentProvisioningStage::
                Prepared,
            NodeEnrollmentProvisioningStage::
                CredentialPersisted,
            &advanced,
            &error
        ),
        NodeEnrollmentRegistryResult::Ok,
        "mark credential persisted"
    );

    expect(
        advanced.provisioning_stage ==
            NodeEnrollmentProvisioningStage::
                CredentialPersisted &&
        advanced.provisioning_started_at ==
            "2026-07-30T06:20:00Z",
        "credential stage should preserve authenticated start"
    );

    error.clear();

    expect_result(
        registry.reset_provisioning(
            "EnrollmentNodeOne0002",
            2,
            NodeEnrollmentProvisioningStage::
                Prepared,
            nullptr,
            &error
        ),
        NodeEnrollmentRegistryResult::
            ProvisioningStageMismatch,
        "rollback must compare current stage"
    );

    NodeEnrollmentRecord reset;

    error.clear();

    expect_result(
        registry.reset_provisioning(
            "EnrollmentNodeOne0002",
            2,
            NodeEnrollmentProvisioningStage::
                CredentialPersisted,
            &reset,
            &error
        ),
        NodeEnrollmentRegistryResult::Ok,
        "reset provisioning after external rollback"
    );

    expect(
        reset.provisioning_stage ==
            NodeEnrollmentProvisioningStage::None &&
        reset.provisioning_started_at.empty() &&
        reset.token_hash == kHashB,
        "reset should retain verifier for a safe retry"
    );

    error.clear();

    expect_result(
        registry.prepare_provisioning_if_matches(
            "EnrollmentNodeOne0002",
            "node-01",
            "https://node-01.internal",
            1,
            2,
            kHashB,
            "2026-07-30T06:30:00Z",
            nullptr,
            &error
        ),
        NodeEnrollmentRegistryResult::Ok,
        "prepare provisioning retry"
    );

    error.clear();

    expect_result(
        registry.advance_provisioning_stage(
            "EnrollmentNodeOne0002",
            2,
            NodeEnrollmentProvisioningStage::
                Prepared,
            NodeEnrollmentProvisioningStage::
                CredentialPersisted,
            nullptr,
            &error
        ),
        NodeEnrollmentRegistryResult::Ok,
        "retry credential stage"
    );

    error.clear();

    expect_result(
        registry.advance_provisioning_stage(
            "EnrollmentNodeOne0002",
            2,
            NodeEnrollmentProvisioningStage::
                CredentialPersisted,
            NodeEnrollmentProvisioningStage::
                NodeRegistered,
            nullptr,
            &error
        ),
        NodeEnrollmentRegistryResult::Ok,
        "mark node registered"
    );

    NodeEnrollmentRecord consumed;

    error.clear();

    expect_result(
        registry.finalize_provisioning(
            "EnrollmentNodeOne0002",
            2,
            "2026-07-30T07:05:00Z",
            &consumed,
            &error
        ),
        NodeEnrollmentRegistryResult::Ok,
        "finalize after authenticated start and later recovery"
    );

    expect(
        consumed.state ==
            NodeEnrollmentState::Consumed &&
        consumed.consumed_at ==
            "2026-07-30T07:05:00Z" &&
        consumed.token_hash.empty() &&
        consumed.provisioning_stage ==
            NodeEnrollmentProvisioningStage::None &&
        consumed.provisioning_started_at ==
            "2026-07-30T06:30:00Z",
        "finalize should clear verifier only after durable stages"
    );

    error.clear();

    expect_result(
        registry.finalize_provisioning(
            "EnrollmentNodeOne0002",
            2,
            "2026-07-30T07:06:00Z",
            nullptr,
            &error
        ),
        NodeEnrollmentRegistryResult::
            AlreadyConsumed,
        "finalize replay"
    );

    NodeEnrollmentRecord expired;

    error.clear();

    expect_result(
        registry.create_or_replace_pending(
            "EnrollmentExpired0001",
            "node-expired",
            "https://node-expired.internal",
            1,
            kHashC,
            "2026-07-30T06:00:00Z",
            "2026-07-30T06:05:00Z",
            &expired,
            &error
        ),
        NodeEnrollmentRegistryResult::Ok,
        "create expiring enrollment"
    );

    error.clear();

    expect_result(
        registry.prepare_provisioning_if_matches(
            expired.enrollment_id,
            expired.node_id,
            expired.api_base_url,
            1,
            expired.generation,
            kHashC,
            "2026-07-30T06:05:00Z",
            nullptr,
            &error
        ),
        NodeEnrollmentRegistryResult::Expired,
        "expiry boundary"
    );

    NodeEnrollmentRecord revoked;

    error.clear();

    expect_result(
        registry.revoke_pending(
            expired.enrollment_id,
            expired.generation,
            "2026-07-30T06:06:00Z",
            &revoked,
            &error
        ),
        NodeEnrollmentRegistryResult::Ok,
        "revoke expired pending intent"
    );

    expect(
        revoked.state ==
            NodeEnrollmentState::Revoked &&
        revoked.token_hash.empty(),
        "revocation should clear verifier"
    );

    error.clear();

    expect_result(
        registry.prepare_provisioning_if_matches(
            expired.enrollment_id,
            expired.node_id,
            expired.api_base_url,
            1,
            expired.generation,
            kHashC,
            "2026-07-30T06:04:00Z",
            nullptr,
            &error
        ),
        NodeEnrollmentRegistryResult::Revoked,
        "revoked enrollment consume"
    );

    NodeEnrollmentRecord old_url_holder;

    error.clear();

    expect_result(
        registry.create_or_replace_pending(
            "EnrollmentOldUrl0001",
            "node-old-url",
            "https://reassigned.internal",
            1,
            kHashA,
            "2026-07-30T08:00:00Z",
            "2026-07-30T08:05:00Z",
            &old_url_holder,
            &error
        ),
        NodeEnrollmentRegistryResult::Ok,
        "create expired URL holder"
    );

    NodeEnrollmentRecord new_url_holder;

    error.clear();

    expect_result(
        registry.create_or_replace_pending(
            "EnrollmentNewUrl0001",
            "node-new-url",
            "https://reassigned.internal",
            1,
            kHashB,
            "2026-07-30T08:06:00Z",
            "2026-07-30T09:06:00Z",
            &new_url_holder,
            &error
        ),
        NodeEnrollmentRegistryResult::Ok,
        "reassign URL after old intent expiry"
    );

    const auto old_url_after =
        registry.get(
            old_url_holder.enrollment_id
        );

    expect(
        old_url_after.has_value() &&
        old_url_after->state ==
            NodeEnrollmentState::Revoked &&
        old_url_after->token_hash.empty(),
        "expired URL holder should be revoked during reassignment"
    );

    NodeEnrollmentRegistry reloaded;

    error.clear();

    expect(
        reloaded.load(
            registry_path.string(),
            &error
        ),
        "reload persisted enrollment registry: " +
            error
    );

    const auto reloaded_consumed =
        reloaded.get(
            "EnrollmentNodeOne0002"
        );

    expect(
        reloaded_consumed.has_value() &&
        reloaded_consumed->state ==
            NodeEnrollmentState::Consumed &&
        reloaded_consumed->token_hash.empty() &&
        reloaded_consumed->provisioning_stage ==
            NodeEnrollmentProvisioningStage::None &&
        reloaded_consumed->provisioning_started_at ==
            consumed.provisioning_started_at,
        "terminal state should survive reload"
    );

    const auto before_failed_load =
        reloaded.snapshot();

    expect(
        write_text(
            registry_path,
            R"({"version":1,"enrollments":[],"unknown":true})"
        ),
        "write unknown root field"
    );

    expect(
        ::chmod(
            registry_path.c_str(),
            0600
        ) == 0,
        "restore private permissions"
    );

    error.clear();

    expect(
        !reloaded.load(
            registry_path.string(),
            &error
        ),
        "unknown registry root field must fail"
    );

    expect(
        reloaded.snapshot().size() ==
            before_failed_load.size(),
        "failed load must preserve memory"
    );

    {
        const fs::path malformed_path =
            test_directory /
            "malformed_enrollment.json";

        expect(
            write_text(
                malformed_path,
                "{ definitely not JSON\n"
            ),
            "write malformed enrollment registry JSON"
        );

        expect(
            ::chmod(
                malformed_path.c_str(),
                0600
            ) == 0,
            "set malformed fixture permissions"
        );

        NodeEnrollmentRegistry malformed_registry;

        error.clear();

        expect(
            !malformed_registry.load(
                malformed_path.string(),
                &error
            ),
            "malformed enrollment JSON must fail"
        );

        expect(
            malformed_registry.snapshot().empty(),
            "malformed load must not install partial state"
        );
    }


    {
        const fs::path legacy_v1_path =
            test_directory /
            "legacy_v1_consumed.json";

        const json legacy_v1_record{
            {"enrollment_id", "LegacyConsumed0001"},
            {"node_id", "node-legacy"},
            {
                "api_base_url",
                "https://node-legacy.internal"
            },
            {"required_protocol_version", 1},
            {"token_hash", ""},
            {"created_at", "2026-07-30T09:00:00Z"},
            {"expires_at", "2026-07-30T10:00:00Z"},
            {"state", "consumed"},
            {"revoked_at", ""},
            {"consumed_at", "2026-07-30T09:30:00Z"},
            {"generation", 1}
        };

        const json legacy_v1_root{
            {"version", 1},
            {
                "enrollments",
                json::array({legacy_v1_record})
            }
        };

        expect(
            write_text(
                legacy_v1_path,
                legacy_v1_root.dump()
            ),
            "write legacy v1 consumed fixture"
        );

        expect(
            ::chmod(
                legacy_v1_path.c_str(),
                0600
            ) == 0,
            "set legacy v1 fixture permissions"
        );

        NodeEnrollmentRegistry legacy_v1_registry;

        error.clear();

        expect(
            legacy_v1_registry.load(
                legacy_v1_path.string(),
                &error
            ),
            "legacy v1 consumed registry should migrate: " +
                error
        );

        const auto legacy_consumed =
            legacy_v1_registry.get(
                "LegacyConsumed0001"
            );

        expect(
            legacy_consumed.has_value() &&
            legacy_consumed->state ==
                NodeEnrollmentState::Consumed &&
            legacy_consumed->provisioning_stage ==
                NodeEnrollmentProvisioningStage::None &&
            legacy_consumed->provisioning_started_at ==
                legacy_consumed->consumed_at,
            "legacy v1 consumed record should receive deterministic provisioning metadata"
        );
    }

    {
        const fs::path unknown_entry_path =
            test_directory /
            "unknown_enrollment_field.json";

        json unknown_entry =
            pending_record_json(
                "UnknownFieldEnroll01",
                "node-unknown-field",
                "https://node-unknown-field.internal",
                1
            );

        unknown_entry["unknown"] = true;

        const json unknown_entry_root{
            {"version", 1},
            {
                "enrollments",
                json::array({unknown_entry})
            }
        };

        expect(
            write_text(
                unknown_entry_path,
                unknown_entry_root.dump()
            ),
            "write unknown enrollment entry field"
        );

        expect(
            ::chmod(
                unknown_entry_path.c_str(),
                0600
            ) == 0,
            "set unknown-field fixture permissions"
        );

        NodeEnrollmentRegistry
            unknown_entry_registry;

        error.clear();

        expect(
            !unknown_entry_registry.load(
                unknown_entry_path.string(),
                &error
            ),
            "unknown enrollment entry field must fail"
        );

        expect(
            unknown_entry_registry.snapshot().empty(),
            "unknown entry field must not install state"
        );
    }

    {
        const fs::path duplicate_path =
            test_directory /
            "duplicate_enrollment.json";

        json duplicate_root{
            {"version", 1},
            {
                "enrollments",
                json::array({
                    pending_record_json(
                        "DuplicateEnroll0001",
                        "node-a",
                        "https://node-a.internal",
                        1
                    ),
                    pending_record_json(
                        "DuplicateEnroll0001",
                        "node-b",
                        "https://node-b.internal",
                        1
                    )
                })
            }
        };

        expect(
            write_text(
                duplicate_path,
                duplicate_root.dump()
            ),
            "write duplicate enrollment fixture"
        );

        expect(
            ::chmod(
                duplicate_path.c_str(),
                0600
            ) == 0,
            "set duplicate fixture permissions"
        );

        NodeEnrollmentRegistry duplicate_registry;

        error.clear();

        expect(
            !duplicate_registry.load(
                duplicate_path.string(),
                &error
            ),
            "duplicate enrollment IDs must fail load"
        );
    }

    {
        const fs::path unsafe_path =
            test_directory /
            "unsafe_permissions.json";

        expect(
            write_text(
                unsafe_path,
                R"({"version":1,"enrollments":[]})"
            ),
            "write unsafe permission fixture"
        );

        expect(
            ::chmod(
                unsafe_path.c_str(),
                0644
            ) == 0,
            "set unsafe permissions"
        );

        NodeEnrollmentRegistry unsafe_registry;

        error.clear();

        expect(
            !unsafe_registry.load(
                unsafe_path.string(),
                &error
            ),
            "group-readable enrollment registry must fail"
        );
    }

    {
        const fs::path real_path =
            test_directory /
            "symlink_target.json";

        const fs::path symlink_path =
            test_directory /
            "enrollment_symlink.json";

        expect(
            write_text(
                real_path,
                R"({"version":1,"enrollments":[]})"
            ),
            "write symlink target"
        );

        expect(
            ::chmod(
                real_path.c_str(),
                0600
            ) == 0,
            "set symlink target permissions"
        );

        expect(
            ::symlink(
                real_path.c_str(),
                symlink_path.c_str()
            ) == 0,
            "create enrollment registry symlink"
        );

        NodeEnrollmentRegistry symlink_registry;

        error.clear();

        expect(
            !symlink_registry.load(
                symlink_path.string(),
                &error
            ),
            "enrollment registry symlink must fail"
        );
    }

    {
        const fs::path hardlink_target =
            test_directory /
            "hardlink_target.json";

        const fs::path hardlink_path =
            test_directory /
            "enrollment_hardlink.json";

        expect(
            write_text(
                hardlink_target,
                R"({"version":1,"enrollments":[]})"
            ),
            "write hardlink target"
        );

        expect(
            ::chmod(
                hardlink_target.c_str(),
                0600
            ) == 0,
            "set hardlink target permissions"
        );

        std::error_code hardlink_error;

        fs::create_hard_link(
            hardlink_target,
            hardlink_path,
            hardlink_error
        );

        expect(
            !hardlink_error,
            "create enrollment registry hardlink"
        );

        if (!hardlink_error) {
            NodeEnrollmentRegistry hardlink_registry;

            error.clear();

            expect(
                !hardlink_registry.load(
                    hardlink_path.string(),
                    &error
                ),
                "hardlinked enrollment registry must fail"
            );
        }
    }

    {
        const fs::path oversized_path =
            test_directory /
            "oversized_enrollment.json";

        expect(
            write_text(
                oversized_path,
                std::string(
                    4 * 1024 * 1024 + 1,
                    'x'
                )
            ),
            "write oversized registry fixture"
        );

        expect(
            ::chmod(
                oversized_path.c_str(),
                0600
            ) == 0,
            "set oversized fixture permissions"
        );

        NodeEnrollmentRegistry oversized_registry;

        error.clear();

        expect(
            !oversized_registry.load(
                oversized_path.string(),
                &error
            ),
            "oversized registry must fail"
        );
    }

    {
        const fs::path rollback_directory =
            test_directory /
            "rollback";

        const fs::path rollback_path =
            rollback_directory /
            "node_enrollments.json";

        NodeEnrollmentRegistry rollback_registry;

        error.clear();

        expect(
            rollback_registry.load(
                rollback_path.string(),
                &error
            ),
            "load rollback registry: " +
                error
        );

        NodeEnrollmentRecord before_record;

        expect_result(
            rollback_registry.create_or_replace_pending(
                "EnrollmentRollback01",
                "node-rollback",
                "https://node-rollback.internal",
                1,
                kHashA,
                "2026-07-30T10:00:00Z",
                "2026-07-30T11:00:00Z",
                &before_record,
                &error
            ),
            NodeEnrollmentRegistryResult::Ok,
            "create rollback fixture"
        );

        struct stat rollback_directory_stats {};

        expect(
            ::stat(
                rollback_directory.c_str(),
                &rollback_directory_stats
            ) == 0,
            "created enrollment directory should exist"
        );

        expect(
            (rollback_directory_stats.st_mode & 0077) == 0,
            "created enrollment directory must be private"
        );

        const auto before_failed_write =
            rollback_registry.snapshot();

        std::error_code filesystem_error;

        expect(
            fs::remove(
                rollback_path,
                filesystem_error
            ) &&
            !filesystem_error,
            "remove rollback registry file"
        );

        filesystem_error.clear();

        expect(
            fs::remove(
                rollback_directory,
                filesystem_error
            ) &&
            !filesystem_error,
            "remove rollback directory"
        );

        expect(
            write_text(
                rollback_directory,
                "not a directory\n"
            ),
            "create deterministic persistence blocker"
        );

        error.clear();

        expect_result(
            rollback_registry.create_or_replace_pending(
                "EnrollmentRollback02",
                "node-rollback",
                "https://node-rollback.internal",
                1,
                kHashB,
                "2026-07-30T10:10:00Z",
                "2026-07-30T11:10:00Z",
                nullptr,
                &error
            ),
            NodeEnrollmentRegistryResult::
                PersistFailed,
            "failed persistence result"
        );

        const auto after_failed_write =
            rollback_registry.snapshot();

        expect(
            after_failed_write.size() ==
                before_failed_write.size() &&
            after_failed_write.size() == 1 &&
            after_failed_write[0].enrollment_id ==
                before_failed_write[0].enrollment_id &&
            after_failed_write[0].state ==
                NodeEnrollmentState::Pending &&
            after_failed_write[0].token_hash ==
                kHashA,
            "failed persistence must preserve pending verifier state"
        );
    }

    expect(
        std::string(
            pqnas::cluster::
                node_enrollment_registry_result_code(
                    NodeEnrollmentRegistryResult::
                        TokenInvalid
                )
        ) == "enrollment_token_invalid",
        "stable token-invalid reason code"
    );

    expect(
        std::string(
            pqnas::cluster::
                node_enrollment_registry_result_code(
                    NodeEnrollmentRegistryResult::
                        ProvisioningActive
                )
        ) == "enrollment_provisioning_active",
        "stable provisioning-active reason code"
    );

    expect(
        std::string(
            pqnas::cluster::
                node_enrollment_registry_result_code(
                    NodeEnrollmentRegistryResult::
                        ProvisioningStageMismatch
                )
        ) == "enrollment_provisioning_stage_mismatch",
        "stable provisioning-stage reason code"
    );

    {
        const fs::path recovery_auth_path =
            test_directory /
            "recovery_auth_registry.json";

        NodeEnrollmentRegistry
            recovery_auth_registry;

        error.clear();

        expect(
            recovery_auth_registry.load(
                recovery_auth_path.string(),
                &error
            ),
            "load recovery authentication registry: " +
                error
        );

        const std::string recovery_id(
            32,
            'Q'
        );

        NodeEnrollmentRecord recovery_record;

        error.clear();

        expect_result(
            recovery_auth_registry.
                create_or_replace_pending(
                    recovery_id,
                    "node-recovery-auth",
                    "https://node-recovery-auth.internal/",
                    7,
                    kHashA,
                    "2026-07-30T06:00:00Z",
                    "2026-07-30T07:00:00Z",
                    &recovery_record,
                    &error
                ),
            NodeEnrollmentRegistryResult::Ok,
            "create recovery authentication fixture"
        );

        pqnas::cluster::
            NodeEnrollmentProvisioningStage
                authenticated_stage =
                    pqnas::cluster::
                        NodeEnrollmentProvisioningStage::
                            NodeRegistered;

        error.clear();

        expect_result(
            recovery_auth_registry.
                authenticate_active_provisioning_if_matches(
                    recovery_id,
                    "node-recovery-auth",
                    "https://node-recovery-auth.internal",
                    7,
                    recovery_record.generation,
                    kHashA,
                    &authenticated_stage,
                    &error
                ),
            NodeEnrollmentRegistryResult::
                ProvisioningStageMismatch,
            "pending non-provisioning record must not authenticate as recovery"
        );

        expect(
            authenticated_stage ==
                pqnas::cluster::
                    NodeEnrollmentProvisioningStage::
                        None,
            "failed recovery authentication must clear stage output"
        );

        error.clear();

        expect_result(
            recovery_auth_registry.
                prepare_provisioning_if_matches(
                    recovery_id,
                    "node-recovery-auth",
                    "https://node-recovery-auth.internal",
                    7,
                    recovery_record.generation,
                    kHashA,
                    "2026-07-30T06:30:00Z",
                    &recovery_record,
                    &error
                ),
            NodeEnrollmentRegistryResult::Ok,
            "prepare recovery authentication fixture"
        );

        const auto before_authentication =
            recovery_auth_registry.get(
                recovery_id
            );

        error.clear();

        expect_result(
            recovery_auth_registry.
                authenticate_active_provisioning_if_matches(
                    recovery_id,
                    "node-recovery-auth",
                    "https://node-recovery-auth.internal/",
                    7,
                    recovery_record.generation,
                    kHashA,
                    &authenticated_stage,
                    &error
                ),
            NodeEnrollmentRegistryResult::Ok,
            "authenticate prepared recovery transaction"
        );

        expect(
            authenticated_stage ==
                pqnas::cluster::
                    NodeEnrollmentProvisioningStage::
                        Prepared,
            "prepared recovery authentication must return persisted stage"
        );

        const auto after_authentication =
            recovery_auth_registry.get(
                recovery_id
            );

        expect(
            before_authentication.has_value() &&
            after_authentication.has_value() &&
            before_authentication->state ==
                after_authentication->state &&
            before_authentication->provisioning_stage ==
                after_authentication->provisioning_stage &&
            before_authentication->provisioning_started_at ==
                after_authentication->provisioning_started_at &&
            before_authentication->token_hash ==
                after_authentication->token_hash,
            "recovery authentication must be read-only"
        );

        error.clear();

        expect_result(
            recovery_auth_registry.
                authenticate_active_provisioning_if_matches(
                    recovery_id,
                    "node-recovery-auth",
                    "https://node-recovery-auth.internal",
                    7,
                    recovery_record.generation,
                    kHashB,
                    &authenticated_stage,
                    &error
                ),
            NodeEnrollmentRegistryResult::
                TokenInvalid,
            "recovery authentication must reject wrong verifier"
        );

        error.clear();

        expect_result(
            recovery_auth_registry.
                authenticate_active_provisioning_if_matches(
                    recovery_id,
                    "node-other",
                    "https://node-recovery-auth.internal",
                    7,
                    recovery_record.generation,
                    kHashA,
                    &authenticated_stage,
                    &error
                ),
            NodeEnrollmentRegistryResult::
                NodeMismatch,
            "recovery authentication node mismatch"
        );

        error.clear();

        expect_result(
            recovery_auth_registry.
                authenticate_active_provisioning_if_matches(
                    recovery_id,
                    "node-recovery-auth",
                    "https://node-other.internal",
                    7,
                    recovery_record.generation,
                    kHashA,
                    &authenticated_stage,
                    &error
                ),
            NodeEnrollmentRegistryResult::
                UrlMismatch,
            "recovery authentication URL mismatch"
        );

        error.clear();

        expect_result(
            recovery_auth_registry.
                authenticate_active_provisioning_if_matches(
                    recovery_id,
                    "node-recovery-auth",
                    "https://node-recovery-auth.internal",
                    8,
                    recovery_record.generation,
                    kHashA,
                    &authenticated_stage,
                    &error
                ),
            NodeEnrollmentRegistryResult::
                ProtocolIncompatible,
            "recovery authentication protocol mismatch"
        );

        error.clear();

        expect_result(
            recovery_auth_registry.
                authenticate_active_provisioning_if_matches(
                    recovery_id,
                    "node-recovery-auth",
                    "https://node-recovery-auth.internal",
                    7,
                    recovery_record.generation + 1,
                    kHashA,
                    &authenticated_stage,
                    &error
                ),
            NodeEnrollmentRegistryResult::
                GenerationMismatch,
            "recovery authentication generation mismatch"
        );

        error.clear();

        expect_result(
            recovery_auth_registry.
                advance_provisioning_stage(
                    recovery_id,
                    recovery_record.generation,
                    pqnas::cluster::
                        NodeEnrollmentProvisioningStage::
                            Prepared,
                    pqnas::cluster::
                        NodeEnrollmentProvisioningStage::
                            CredentialPersisted,
                    &recovery_record,
                    &error
                ),
            NodeEnrollmentRegistryResult::Ok,
            "advance recovery fixture to credential persisted"
        );

        error.clear();

        expect_result(
            recovery_auth_registry.
                authenticate_active_provisioning_if_matches(
                    recovery_id,
                    "node-recovery-auth",
                    "https://node-recovery-auth.internal",
                    7,
                    recovery_record.generation,
                    kHashA,
                    &authenticated_stage,
                    &error
                ),
            NodeEnrollmentRegistryResult::Ok,
            "authenticate credential-persisted recovery transaction"
        );

        expect(
            authenticated_stage ==
                pqnas::cluster::
                    NodeEnrollmentProvisioningStage::
                        CredentialPersisted,
            "credential-persisted recovery stage"
        );

        error.clear();

        expect_result(
            recovery_auth_registry.
                advance_provisioning_stage(
                    recovery_id,
                    recovery_record.generation,
                    pqnas::cluster::
                        NodeEnrollmentProvisioningStage::
                            CredentialPersisted,
                    pqnas::cluster::
                        NodeEnrollmentProvisioningStage::
                            NodeRegistered,
                    &recovery_record,
                    &error
                ),
            NodeEnrollmentRegistryResult::Ok,
            "advance recovery fixture to node registered"
        );

        NodeEnrollmentRegistry
            recovery_auth_reloaded;

        error.clear();

        expect(
            recovery_auth_reloaded.load(
                recovery_auth_path.string(),
                &error
            ),
            "reload active recovery transaction: " +
                error
        );

        error.clear();

        expect_result(
            recovery_auth_reloaded.
                authenticate_active_provisioning_if_matches(
                    recovery_id,
                    "node-recovery-auth",
                    "https://node-recovery-auth.internal",
                    7,
                    recovery_record.generation,
                    kHashA,
                    &authenticated_stage,
                    &error
                ),
            NodeEnrollmentRegistryResult::Ok,
            "authenticate reloaded node-registered transaction"
        );

        expect(
            authenticated_stage ==
                pqnas::cluster::
                    NodeEnrollmentProvisioningStage::
                        NodeRegistered,
            "node-registered recovery stage must survive reload"
        );

        // Recovery remains valid after the original TTL because the persisted
        // prepare time proves authentication happened before expiry.
        error.clear();

        expect_result(
            recovery_auth_reloaded.
                finalize_provisioning(
                    recovery_id,
                    recovery_record.generation,
                    "2026-07-30T08:30:00Z",
                    &recovery_record,
                    &error
                ),
            NodeEnrollmentRegistryResult::Ok,
            "finalize authenticated recovery after original TTL"
        );

        error.clear();

        expect_result(
            recovery_auth_reloaded.
                authenticate_active_provisioning_if_matches(
                    recovery_id,
                    "node-recovery-auth",
                    "https://node-recovery-auth.internal",
                    7,
                    recovery_record.generation,
                    kHashA,
                    &authenticated_stage,
                    &error
                ),
            NodeEnrollmentRegistryResult::
                AlreadyConsumed,
            "consumed transaction must not authenticate for recovery"
        );

        const std::string revoked_id(
            32,
            'V'
        );

        NodeEnrollmentRecord revoked_record;

        error.clear();

        expect_result(
            recovery_auth_reloaded.
                create_or_replace_pending(
                    revoked_id,
                    "node-recovery-revoked",
                    "https://node-recovery-revoked.internal",
                    7,
                    kHashB,
                    "2026-07-30T09:00:00Z",
                    "2026-07-30T10:00:00Z",
                    &revoked_record,
                    &error
                ),
            NodeEnrollmentRegistryResult::Ok,
            "create revoked recovery fixture"
        );

        error.clear();

        expect_result(
            recovery_auth_reloaded.
                revoke_pending(
                    revoked_id,
                    revoked_record.generation,
                    "2026-07-30T09:10:00Z",
                    &revoked_record,
                    &error
                ),
            NodeEnrollmentRegistryResult::Ok,
            "revoke recovery authentication fixture"
        );

        error.clear();

        expect_result(
            recovery_auth_reloaded.
                authenticate_active_provisioning_if_matches(
                    revoked_id,
                    "node-recovery-revoked",
                    "https://node-recovery-revoked.internal",
                    7,
                    revoked_record.generation,
                    kHashB,
                    &authenticated_stage,
                    &error
                ),
            NodeEnrollmentRegistryResult::
                Revoked,
            "revoked transaction must not authenticate for recovery"
        );

        error.clear();

        expect_result(
            recovery_auth_reloaded.
                authenticate_active_provisioning_if_matches(
                    std::string(32, 'Z'),
                    "node-recovery-missing",
                    "https://node-recovery-missing.internal",
                    7,
                    1,
                    kHashC,
                    &authenticated_stage,
                    &error
                ),
            NodeEnrollmentRegistryResult::
                NotFound,
            "missing recovery authentication transaction"
        );

        error.clear();

        expect_result(
            recovery_auth_reloaded.
                authenticate_active_provisioning_if_matches(
                    recovery_id,
                    "node-recovery-auth",
                    "https://node-recovery-auth.internal",
                    7,
                    recovery_record.generation,
                    kHashA,
                    nullptr,
                    &error
                ),
            NodeEnrollmentRegistryResult::
                InvalidInput,
            "recovery authentication requires stage output"
        );
    }

    cleanup();

    if (failures != 0) {
        std::cerr
            << failures
            << " node enrollment registry test(s) failed\n";
        return 1;
    }

    std::cout
        << "OK: node enrollment registry tests passed\n";

    return 0;
}
