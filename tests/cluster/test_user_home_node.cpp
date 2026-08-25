#include "users_registry.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

#if defined(__linux__)

// Test-only durability fault injection.
//
// Security: production code gets no runtime fault hook; the linker wrapper
// fails only the next parent-directory fsync in this regression executable.
bool g_fail_next_directory_fsync = false;
int g_injected_directory_fsync_failures = 0;

extern "C" int __real_fsync(int descriptor);

extern "C" int __wrap_fsync(int descriptor) {
    struct stat stats {};

    if (g_fail_next_directory_fsync &&
        ::fstat(
            descriptor,
            &stats
        ) == 0 &&
        S_ISDIR(stats.st_mode)) {
        g_fail_next_directory_fsync = false;
        ++g_injected_directory_fsync_failures;

        errno = EIO;
        return -1;
    }

    return __real_fsync(descriptor);
}

#endif

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

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

fs::path make_test_directory() {
    std::string pattern =
        (
            fs::temp_directory_path() /
            "pqnas-user-home-node-XXXXXX"
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

bool write_json(
    const fs::path& path,
    const json& value
) {
    std::ofstream output(
        path,
        std::ios::binary |
        std::ios::trunc
    );

    if (!output.good()) {
        return false;
    }

    output << value.dump(2) << '\n';
    return output.good();
}

json read_json(
    const fs::path& path
) {
    std::ifstream input(path);

    if (!input.good()) {
        return json();
    }

    json value =
        json::parse(
            input,
            nullptr,
            false
        );

    return value;
}

const json* find_user(
    const json& root,
    const std::string& fingerprint
) {
    if (!root.is_object() ||
        !root.contains("users") ||
        !root["users"].is_array()) {
        return nullptr;
    }

    for (const auto& user : root["users"]) {
        if (user.is_object() &&
            user.value("fingerprint", "") ==
                fingerprint) {
            return &user;
        }
    }

    return nullptr;
}

} // namespace

int main() {
    expect(
        pqnas::valid_user_home_node_id(""),
        "empty home_node should represent local placement"
    );

    expect(
        pqnas::valid_user_home_node_id("node-01") &&
        pqnas::valid_user_home_node_id("Node_02.eu"),
        "stable node IDs should be accepted"
    );

    expect(
        !pqnas::valid_user_home_node_id("-node") &&
        !pqnas::valid_user_home_node_id("../node") &&
        !pqnas::valid_user_home_node_id("node/01") &&
        !pqnas::valid_user_home_node_id(
            "https://node-01.internal"
        ) &&
        !pqnas::valid_user_home_node_id("node 01") &&
        !pqnas::valid_user_home_node_id(
            std::string(65, 'a')
        ),
        "paths, URLs, whitespace and oversized IDs must be rejected"
    );

    expect(
        pqnas::valid_user_placement_policy_id("") &&
        pqnas::valid_user_placement_policy_id(
            "business-backup"
        ) &&
        pqnas::valid_user_placement_policy_id(
            "Business_02.eu"
        ),
        "empty and stable placement policy IDs should be accepted"
    );

    expect(
        !pqnas::valid_user_placement_policy_id(
            "-business"
        ) &&
        !pqnas::valid_user_placement_policy_id(
            "../business"
        ) &&
        !pqnas::valid_user_placement_policy_id(
            "business/backup"
        ) &&
        !pqnas::valid_user_placement_policy_id(
            "https://policy.internal"
        ) &&
        !pqnas::valid_user_placement_policy_id(
            "business backup"
        ) &&
        !pqnas::valid_user_placement_policy_id(
            std::string(65, 'p')
        ),
        "unsafe placement policy IDs must be rejected"
    );

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

    const fs::path legacy_path =
        directory / "legacy_users.json";

    const json legacy_json{
        {
            "users",
            json::array({
                json{
                    {"fingerprint", "fp-legacy"},
                    {"role", "user"},
                    {"status", "disabled"}
                }
            })
        }
    };

    expect(
        write_json(legacy_path, legacy_json),
        "write legacy users registry"
    );

    pqnas::UsersRegistry registry;

    expect(
        registry.load(legacy_path.string()),
        "legacy users registry without home_node should load"
    );

    const auto legacy_user =
        registry.get("fp-legacy");

    expect(
        legacy_user.has_value() &&
        legacy_user->home_node.empty() &&
        legacy_user->placement_policy_id.empty(),
        "missing placement fields should preserve legacy local behavior"
    );

    expect(
        registry.set_home_node(
            "fp-legacy",
            "node-01"
        ),
        "valid home_node should update existing user"
    );

    expect(
        !registry.set_home_node(
            "fp-legacy",
            "../node"
        ),
        "invalid home_node setter must fail"
    );

    const auto after_invalid_set =
        registry.get("fp-legacy");

    expect(
        after_invalid_set.has_value() &&
        after_invalid_set->home_node ==
            "node-01",
        "failed setter must preserve previous placement"
    );

    pqnas::UserRec valid_upsert =
        *after_invalid_set;

    valid_upsert.fingerprint = "fp-valid";
    valid_upsert.home_node = "node-02";
    valid_upsert.placement_policy_id =
        "business-backup";

    expect(
        registry.upsert(valid_upsert),
        "generic upsert should accept valid placement"
    );

    pqnas::UserRec invalid_upsert =
        valid_upsert;

    invalid_upsert.fingerprint = "fp-invalid";
    invalid_upsert.home_node = "node/03";

    expect(
        !registry.upsert(invalid_upsert) &&
        !registry.exists("fp-invalid"),
        "generic upsert must reject invalid placement"
    );

    pqnas::UserRec invalid_policy_upsert =
        valid_upsert;

    invalid_policy_upsert.fingerprint =
        "fp-invalid-policy";

    invalid_policy_upsert.home_node =
        "node-03";

    invalid_policy_upsert.placement_policy_id =
        "../business";

    expect(
        !registry.upsert(
            invalid_policy_upsert
        ) &&
        !registry.exists(
            "fp-invalid-policy"
        ),
        "generic upsert must reject invalid policy references"
    );

    const fs::path saved_path =
        directory / "saved_users.json";

    expect(
        registry.save(saved_path.string()),
        "save users registry with placement metadata"
    );

    const json saved_json =
        read_json(saved_path);

    const json* saved_legacy =
        find_user(
            saved_json,
            "fp-legacy"
        );

    const json* saved_valid =
        find_user(
            saved_json,
            "fp-valid"
        );

    expect(
        saved_legacy &&
        saved_legacy->value(
            "home_node",
            ""
        ) == "node-01" &&
        saved_legacy->value(
            "placement_policy_id",
            "missing"
        ).empty(),
        "saved legacy user should contain local policy intent"
    );

    expect(
        saved_valid &&
        saved_valid->value(
            "home_node",
            ""
        ) == "node-02" &&
        saved_valid->value(
            "placement_policy_id",
            ""
        ) == "business-backup",
        "saved user should contain node and policy IDs"
    );

    pqnas::UsersRegistry reloaded;

    expect(
        reloaded.load(saved_path.string()),
        "saved placement registry should reload"
    );

    const auto reloaded_user =
        reloaded.get("fp-legacy");

    const auto reloaded_valid =
        reloaded.get("fp-valid");

    expect(
        reloaded_user.has_value() &&
        reloaded_user->home_node ==
            "node-01" &&
        reloaded_user->placement_policy_id.empty(),
        "legacy placement metadata should survive save and reload"
    );

    expect(
        reloaded_valid.has_value() &&
        reloaded_valid->home_node ==
            "node-02" &&
        reloaded_valid->placement_policy_id ==
            "business-backup",
        "policy reference should survive save and reload"
    );

    const auto initial_policy_counts =
        reloaded.placement_policy_assignment_counts();

    expect(
        initial_policy_counts.size() == 1 &&
        initial_policy_counts.count(
            "business-backup"
        ) == 1 &&
        initial_policy_counts.at(
            "business-backup"
        ) == 1 &&
        initial_policy_counts.count("") == 0,
        "policy counts should omit empty overrides and count explicit IDs"
    );


    bool cas_changed = false;
    bool cas_expectation_matched = false;
    std::string cas_error;

    expect(
        reloaded.set_home_node_persisted_if_current(
            "fp-legacy",
            "node-01",
            "",
            "node-03",
            saved_path.string(),
            &cas_changed,
            &cas_expectation_matched,
            &cas_error
        ) &&
        cas_expectation_matched &&
        cas_changed,
        "matching placement expectation should persist: " +
            cas_error
    );

    const auto cas_updated_user =
        reloaded.get("fp-legacy");

    expect(
        cas_updated_user.has_value() &&
        cas_updated_user->home_node ==
            "node-03" &&
        cas_updated_user->
            placement_policy_id.empty(),
        "successful CAS should update only home_node"
    );

    const json cas_saved_json =
        read_json(saved_path);

    const json* cas_saved_user =
        find_user(
            cas_saved_json,
            "fp-legacy"
        );

    expect(
        cas_saved_user &&
        cas_saved_user->value(
            "home_node",
            ""
        ) == "node-03" &&
        cas_saved_user->value(
            "placement_policy_id",
            "missing"
        ).empty(),
        "successful CAS should reach users.json"
    );

    cas_changed = true;
    cas_expectation_matched = true;
    cas_error.clear();

    expect(
        reloaded.set_home_node_persisted_if_current(
            "fp-legacy",
            "node-01",
            "",
            "node-04",
            saved_path.string(),
            &cas_changed,
            &cas_expectation_matched,
            &cas_error
        ) &&
        !cas_expectation_matched &&
        !cas_changed &&
        cas_error.empty(),
        "stale home_node expectation should be a clean no-op"
    );

    const auto after_stale_home =
        reloaded.get("fp-legacy");

    expect(
        after_stale_home.has_value() &&
        after_stale_home->home_node ==
            "node-03",
        "stale home_node expectation must preserve placement"
    );

    cas_changed = true;
    cas_expectation_matched = true;
    cas_error.clear();

    expect(
        reloaded.set_home_node_persisted_if_current(
            "fp-legacy",
            "node-03",
            "business-backup",
            "node-04",
            saved_path.string(),
            &cas_changed,
            &cas_expectation_matched,
            &cas_error
        ) &&
        !cas_expectation_matched &&
        !cas_changed &&
        cas_error.empty(),
        "stale policy expectation should be a clean no-op"
    );

    const auto after_stale_policy =
        reloaded.get("fp-legacy");

    expect(
        after_stale_policy.has_value() &&
        after_stale_policy->home_node ==
            "node-03" &&
        after_stale_policy->
            placement_policy_id.empty(),
        "stale policy expectation must preserve user state"
    );

    cas_changed = true;
    cas_expectation_matched = false;
    cas_error.clear();

    expect(
        reloaded.set_home_node_persisted_if_current(
            "fp-legacy",
            "node-03",
            "",
            "node-03",
            saved_path.string(),
            &cas_changed,
            &cas_expectation_matched,
            &cas_error
        ) &&
        cas_expectation_matched &&
        !cas_changed,
        "matching already-applied CAS should be idempotent"
    );

    cas_changed = true;
    cas_expectation_matched = true;
    cas_error.clear();

    expect(
        !reloaded.set_home_node_persisted_if_current(
            "fp-legacy",
            "../node",
            "",
            "node-04",
            saved_path.string(),
            &cas_changed,
            &cas_expectation_matched,
            &cas_error
        ) &&
        !cas_expectation_matched &&
        !cas_changed &&
        !cas_error.empty(),
        "invalid CAS expectation should fail validation"
    );

    const fs::path cas_rollback_directory =
        directory / "cas-rollback";

    const fs::path cas_rollback_path =
        cas_rollback_directory /
        "users.json";

    expect(
        reloaded.save(
            cas_rollback_path.string()
        ),
        "create CAS rollback fixture"
    );

    std::error_code cas_rollback_error;

    expect(
        fs::remove(
            cas_rollback_path,
            cas_rollback_error
        ) &&
        !cas_rollback_error,
        "remove CAS rollback registry file"
    );

    cas_rollback_error.clear();

    expect(
        fs::remove(
            cas_rollback_directory,
            cas_rollback_error
        ) &&
        !cas_rollback_error,
        "remove CAS rollback registry directory"
    );

    expect(
        write_json(
            cas_rollback_directory,
            json{
                {"blocker", true}
            }
        ),
        "replace CAS rollback directory with a file"
    );

    cas_changed = true;
    cas_expectation_matched = false;
    cas_error.clear();

    expect(
        !reloaded.set_home_node_persisted_if_current(
            "fp-legacy",
            "node-03",
            "",
            "node-04",
            cas_rollback_path.string(),
            &cas_changed,
            &cas_expectation_matched,
            &cas_error
        ) &&
        cas_expectation_matched &&
        !cas_changed &&
        !cas_error.empty(),
        "CAS persistence failure should be reported"
    );

    const auto after_failed_cas_write =
        reloaded.get("fp-legacy");

    expect(
        after_failed_cas_write.has_value() &&
        after_failed_cas_write->home_node ==
            "node-03" &&
        after_failed_cas_write->
            placement_policy_id.empty(),
        "failed CAS persistence must restore prior state"
    );

    bool policy_changed = false;
    std::string policy_error;

    expect(
        reloaded.set_placement_policy_id_persisted(
            "fp-legacy",
            "business-backup",
            saved_path.string(),
            &policy_changed,
            &policy_error
        ) &&
        policy_changed,
        "persist policy assignment: " +
            policy_error
    );

    policy_changed = true;
    policy_error.clear();

    expect(
        reloaded.set_placement_policy_id_persisted(
            "fp-legacy",
            "business-backup",
            saved_path.string(),
            &policy_changed,
            &policy_error
        ) &&
        !policy_changed,
        "unchanged policy assignment should be idempotent"
    );

    const json policy_saved_json =
        read_json(saved_path);

    const json* policy_saved_user =
        find_user(
            policy_saved_json,
            "fp-legacy"
        );

    expect(
        policy_saved_user &&
        policy_saved_user->value(
            "placement_policy_id",
            ""
        ) == "business-backup",
        "persisted policy assignment should reach users.json"
    );

    const auto assigned_policy_counts =
        reloaded.placement_policy_assignment_counts();

    expect(
        assigned_policy_counts.size() == 1 &&
        assigned_policy_counts.at(
            "business-backup"
        ) == 2,
        "policy count should increase after assigning another user"
    );

    policy_changed = false;
    policy_error.clear();

    expect(
        reloaded.set_placement_policy_id_persisted(
            "fp-legacy",
            "",
            saved_path.string(),
            &policy_changed,
            &policy_error
        ) &&
        policy_changed,
        "empty policy ID should clear the override"
    );

    const auto cleared_policy_user =
        reloaded.get("fp-legacy");

    expect(
        cleared_policy_user.has_value() &&
        cleared_policy_user->
            placement_policy_id.empty(),
        "cleared policy override should update memory"
    );

    const auto cleared_policy_counts =
        reloaded.placement_policy_assignment_counts();

    expect(
        cleared_policy_counts.size() == 1 &&
        cleared_policy_counts.at(
            "business-backup"
        ) == 1 &&
        cleared_policy_counts.count("") == 0,
        "policy count should decrease when an override is cleared"
    );

    const fs::path rollback_directory =
        directory / "policy-rollback";

    const fs::path rollback_path =
        rollback_directory / "users.json";

    expect(
        reloaded.save(
            rollback_path.string()
        ),
        "create policy rollback fixture"
    );

    policy_changed = false;
    policy_error.clear();

    expect(
        reloaded.set_placement_policy_id_persisted(
            "fp-legacy",
            "before-failure",
            rollback_path.string(),
            &policy_changed,
            &policy_error
        ) &&
        policy_changed,
        "establish rollback baseline"
    );

    std::error_code rollback_error;

    expect(
        fs::remove(
            rollback_path,
            rollback_error
        ) &&
        !rollback_error,
        "remove rollback registry file"
    );

    rollback_error.clear();

    expect(
        fs::remove(
            rollback_directory,
            rollback_error
        ) &&
        !rollback_error,
        "remove rollback registry directory"
    );

    expect(
        write_json(
            rollback_directory,
            json{
                {"blocker", true}
            }
        ),
        "replace rollback directory with a regular file"
    );

    policy_changed = false;
    policy_error.clear();

    expect(
        !reloaded.set_placement_policy_id_persisted(
            "fp-legacy",
            "after-failure",
            rollback_path.string(),
            &policy_changed,
            &policy_error
        ) &&
        !policy_changed,
        "failed persistence must reject policy mutation"
    );

    const auto after_failed_policy_write =
        reloaded.get("fp-legacy");

    expect(
        after_failed_policy_write.has_value() &&
        after_failed_policy_write->
            placement_policy_id ==
                "before-failure",
        "failed persistence must restore previous policy reference"
    );


    // ---------------------------------------------------------------------
    // RT-005 profile-only stale snapshot regression
    // ---------------------------------------------------------------------
    //
    // Security: an admin profile request may have been created from state read
    // before a concurrent placement transition. The profile-only persisted API
    // must apply its fields to the CURRENT UserRec rather than write that stale
    // record back with an old home_node or Storage authorization mapping.
    {
        const fs::path profile_path =
            directory /
            "profile_race_users.json";

        pqnas::UsersRegistry
            profile_registry;

        pqnas::UserRec user;

        user.fingerprint =
            "fp-profile-race";

        user.name =
            "Original";

        user.role =
            "user";

        user.status =
            "enabled";

        user.notes =
            "old notes";

        user.email =
            "old@example.invalid";

        user.group =
            "old-group";

        user.address =
            "old-address";

        user.avatar_url =
            "/avatars/old.png";

        user.storage_state =
            "allocated";

        user.quota_bytes =
            4096;

        user.root_rel =
            "users/fp-profile-race";

        user.storage_pool_id =
            "pool01";

        user.storage_set_at =
            "2026-08-24T00:00:00Z";

        user.storage_set_by =
            "admin-fingerprint";

        user.home_node =
            "node-01";

        user.placement_policy_id =
            "business-backup";

        bool seed_projection_changed = false;
        std::string profile_error;

        expect(
            profile_registry.upsert_persisted(
                user,
                profile_path.string(),
                &seed_projection_changed,
                &profile_error
            ) &&
            seed_projection_changed,
            "RT-005 fixture should persist initial user: " +
                profile_error
        );

        // This copy models the stale request-local UserRec that the historical
        // admin route used to modify and later pass to generic upsert().
        const auto stale_snapshot =
            profile_registry.get(
                user.fingerprint
            );

        expect(
            stale_snapshot.has_value() &&
            stale_snapshot->home_node ==
                "node-01",
            "RT-005 fixture should capture node-01 snapshot"
        );

        const std::uint64_t
            stale_revision =
                stale_snapshot.has_value()
                    ? stale_snapshot->
                        storage_projection_revision
                    : 0;

        bool placement_changed = false;

        profile_error.clear();

        expect(
            profile_registry.set_home_node_persisted(
                user.fingerprint,
                "node-02",
                profile_path.string(),
                &placement_changed,
                &profile_error
            ) &&
            placement_changed,
            "RT-005 concurrent placement should move to node-02: " +
                profile_error
        );

        const auto after_move =
            profile_registry.get(
                user.fingerprint
            );

        expect(
            after_move.has_value() &&
            after_move->home_node ==
                "node-02" &&
            after_move->
                storage_projection_revision >
                stale_revision,
            "RT-005 placement must be newer than stale profile snapshot"
        );

        // NEW-002: the primitive itself must reject a stale full-record
        // write-back. Preserving only the current revision is insufficient:
        // doing so would pair stale node-01 authorization with node-02's newer
        // revision.
        if (stale_snapshot.has_value() &&
            after_move.has_value()) {
          pqnas::UserRec stale_upsert =
              *stale_snapshot;

          stale_upsert.name =
              "must-not-commit";

          expect(
              !profile_registry.upsert(
                  stale_upsert
              ),
              "NEW-002 generic upsert must reject stale projection semantics"
          );

          const auto after_rejected_upsert =
              profile_registry.get(
                  user.fingerprint
              );

          expect(
              after_rejected_upsert.has_value() &&
              after_rejected_upsert->home_node ==
                  "node-02" &&
              after_rejected_upsert->
                  storage_projection_revision ==
                  after_move->
                      storage_projection_revision &&
              after_rejected_upsert->name ==
                  after_move->name,
              "NEW-002 rejected stale upsert must preserve current authoritative state"
          );
        }

        pqnas::UserNonAuthMetadataUpdate
            metadata_update;

        metadata_update.avatar_url =
            "/avatars/current-safe.png";

        metadata_update.app_prefs_json =
            R"({"view":"grid"})";

        metadata_update.vault_master_recovery_status =
            "active";

        metadata_update.vault_master_recovery_public_key_b64 =
            "public-key";

        metadata_update.vault_master_recovery_public_key_sha256 =
            "public-key-sha256";

        metadata_update.vault_master_recovery_created_at =
            123456789LL;

        metadata_update.vault_master_recovery_label =
            "Recovery";

        bool metadata_changed = false;

        pqnas::UserRec
            metadata_updated;

        profile_error.clear();

        expect(
            profile_registry.set_non_auth_metadata_persisted(
                user.fingerprint,
                metadata_update,
                profile_path.string(),
                &metadata_changed,
                &metadata_updated,
                &profile_error
            ) &&
            metadata_changed,
            "NEW-002 partial metadata update should persist: " +
                profile_error
        );

        expect(
            after_move.has_value() &&
            metadata_updated.home_node ==
                after_move->home_node &&
            metadata_updated.status ==
                after_move->status &&
            metadata_updated.storage_state ==
                after_move->storage_state &&
            metadata_updated.quota_bytes ==
                after_move->quota_bytes &&
            metadata_updated.root_rel ==
                after_move->root_rel &&
            metadata_updated.storage_pool_id ==
                after_move->storage_pool_id &&
            metadata_updated.placement_policy_id ==
                after_move->placement_policy_id &&
            metadata_updated.storage_projection_revision ==
                after_move->storage_projection_revision &&
            metadata_updated.avatar_url ==
                "/avatars/current-safe.png" &&
            metadata_updated.app_prefs_json ==
                R"({"view":"grid"})" &&
            metadata_updated.vault_master_recovery_status ==
                "active",
            "NEW-002 partial metadata mutation must preserve CURRENT authorization state"
        );

        pqnas::UserProfileUpdate
            profile_update;

        profile_update.name =
            "Updated";

        profile_update.notes =
            "new notes";

        profile_update.email =
            "new@example.invalid";

        profile_update.address =
            "new-address";

        profile_update.group =
            "new-group";

        profile_update.avatar_url =
            "/avatars/new.png";

        // Simulate self-edit: even a requested role change must not alter role.
        profile_update.update_role =
            false;

        profile_update.role =
            "admin";

        bool profile_changed = false;

        pqnas::UserRec
            updated;

        profile_error.clear();

        expect(
            profile_registry.set_profile_persisted(
                user.fingerprint,
                profile_update,
                profile_path.string(),
                &profile_changed,
                &updated,
                &profile_error
            ) &&
            profile_changed,
            "RT-005 profile-only update should persist: " +
                profile_error
        );

        expect(
            after_move.has_value() &&
            updated.name ==
                "Updated" &&
            updated.notes ==
                "new notes" &&
            updated.email ==
                "new@example.invalid" &&
            updated.address ==
                "new-address" &&
            updated.group ==
                "new-group" &&
            updated.avatar_url ==
                "/avatars/new.png" &&
            updated.role ==
                after_move->role &&
            updated.status ==
                after_move->status &&
            updated.home_node ==
                "node-02" &&
            updated.storage_state ==
                after_move->storage_state &&
            updated.quota_bytes ==
                after_move->quota_bytes &&
            updated.root_rel ==
                after_move->root_rel &&
            updated.storage_pool_id ==
                after_move->storage_pool_id &&
            updated.storage_set_at ==
                after_move->storage_set_at &&
            updated.storage_set_by ==
                after_move->storage_set_by &&
            updated.placement_policy_id ==
                after_move->
                    placement_policy_id &&
            updated.storage_projection_revision ==
                after_move->
                    storage_projection_revision,
            "RT-005 profile update must preserve CURRENT protected state"
        );

        // Idempotent retry must not churn users.json authorization version.
        const std::uint64_t
            revision_after_profile =
                updated.storage_projection_revision;

        profile_changed = true;
        profile_error.clear();

        expect(
            profile_registry.set_profile_persisted(
                user.fingerprint,
                profile_update,
                profile_path.string(),
                &profile_changed,
                &updated,
                &profile_error
            ) &&
            !profile_changed &&
            updated.storage_projection_revision ==
                revision_after_profile,
            "RT-005 idempotent profile retry must not advance projection revision"
        );

        // Reload proves the protected current placement, not the stale snapshot,
        // is what reached durable users.json.
        pqnas::UsersRegistry
            profile_reloaded;

        expect(
            profile_reloaded.load(
                profile_path.string()
            ),
            "RT-005 profile fixture should reload"
        );

        const auto durable =
            profile_reloaded.get(
                user.fingerprint
            );

        expect(
            durable.has_value() &&
            durable->home_node ==
                "node-02" &&
            durable->storage_projection_revision ==
                revision_after_profile &&
            durable->name ==
                "Updated" &&
            durable->role ==
                "user" &&
            durable->placement_policy_id ==
                "business-backup",
            "RT-005 durable reload must retain new placement and profile"
        );
    }


    // ---------------------------------------------------------------------
    // NEW-002 concurrent authorization + metadata regression
    // ---------------------------------------------------------------------
    //
    // Security: these races model request-local metadata work happening while
    // another Director operation changes authorization state. Both successful
    // operations must survive regardless of which thread acquires mu_ first.
    {
        const fs::path race_path =
            directory /
            "new002_concurrent_users.json";

        pqnas::UsersRegistry race_registry;

        pqnas::UserRec race_user;
        race_user.fingerprint =
            "new002-concurrent-user";
        race_user.name =
            "Concurrent";
        race_user.role =
            "user";
        race_user.status =
            "enabled";
        race_user.avatar_url =
            "/avatars/original.png";
        race_user.app_prefs_json =
            R"({"view":"list"})";
        race_user.storage_state =
            "allocated";
        race_user.quota_bytes =
            4096;
        race_user.root_rel =
            "users/new002-concurrent-user";
        race_user.storage_pool_id =
            "pool01";
        race_user.storage_set_at =
            "2026-08-25T00:00:00Z";
        race_user.storage_set_by =
            "admin-fingerprint";
        race_user.home_node =
            "node-01";
        race_user.placement_policy_id =
            "business-backup";

        bool seed_projection_changed = false;
        std::string race_error;

        expect(
            race_registry.upsert_persisted(
                race_user,
                race_path.string(),
                &seed_projection_changed,
                &race_error
            ) &&
            seed_projection_changed,
            "NEW-002 concurrency fixture should persist initial user: " +
                race_error
        );

        const auto run_concurrently =
            [](auto left, auto right) {
                std::atomic<int> ready{0};
                std::atomic<bool> go{false};

                std::thread left_thread(
                    [&]() {
                        ready.fetch_add(1);

                        while (!go.load()) {
                            std::this_thread::yield();
                        }

                        left();
                    }
                );

                std::thread right_thread(
                    [&]() {
                        ready.fetch_add(1);

                        while (!go.load()) {
                            std::this_thread::yield();
                        }

                        right();
                    }
                );

                while (ready.load() != 2) {
                    std::this_thread::yield();
                }

                go.store(true);

                left_thread.join();
                right_thread.join();
            };


        // -------------------------------------------------------------
        // Placement change racing avatar metadata.
        // -------------------------------------------------------------
        {
            const auto before =
                race_registry.get(
                    race_user.fingerprint
                );

            expect(
                before.has_value(),
                "NEW-002 placement race should have current user"
            );

            if (before.has_value()) {
                bool placement_ok = false;
                bool placement_changed = false;
                std::string placement_error;

                bool metadata_ok = false;
                bool metadata_changed = false;
                std::string metadata_error;

                pqnas::UserNonAuthMetadataUpdate
                    metadata_update;

                metadata_update.avatar_url =
                    "/avatars/concurrent-placement.png";

                run_concurrently(
                    [&]() {
                        placement_ok =
                            race_registry.set_home_node_persisted(
                                race_user.fingerprint,
                                "node-02",
                                race_path.string(),
                                &placement_changed,
                                &placement_error
                            );
                    },
                    [&]() {
                        metadata_ok =
                            race_registry.set_non_auth_metadata_persisted(
                                race_user.fingerprint,
                                metadata_update,
                                race_path.string(),
                                &metadata_changed,
                                nullptr,
                                &metadata_error
                            );
                    }
                );

                const auto after =
                    race_registry.get(
                        race_user.fingerprint
                    );

                expect(
                    placement_ok &&
                    placement_changed &&
                    metadata_ok &&
                    metadata_changed &&
                    after.has_value() &&
                    after->home_node ==
                        "node-02" &&
                    after->avatar_url ==
                        "/avatars/concurrent-placement.png" &&
                    after->storage_projection_revision ==
                        before->storage_projection_revision + 1,
                    "NEW-002 concurrent placement/avatar writes must both survive"
                );
            }
        }


        // -------------------------------------------------------------
        // Status change racing app-preference metadata.
        // -------------------------------------------------------------
        {
            const auto before =
                race_registry.get(
                    race_user.fingerprint
                );

            expect(
                before.has_value(),
                "NEW-002 status race should have current user"
            );

            if (before.has_value()) {
                bool status_ok = false;
                bool status_changed = false;
                std::string status_error;

                bool metadata_ok = false;
                bool metadata_changed = false;
                std::string metadata_error;

                pqnas::UserNonAuthMetadataUpdate
                    metadata_update;

                metadata_update.app_prefs_json =
                    R"({"view":"grid","race":true})";

                run_concurrently(
                    [&]() {
                        status_ok =
                            race_registry.set_status_persisted(
                                race_user.fingerprint,
                                "disabled",
                                race_path.string(),
                                &status_changed,
                                &status_error
                            );
                    },
                    [&]() {
                        metadata_ok =
                            race_registry.set_non_auth_metadata_persisted(
                                race_user.fingerprint,
                                metadata_update,
                                race_path.string(),
                                &metadata_changed,
                                nullptr,
                                &metadata_error
                            );
                    }
                );

                const auto after =
                    race_registry.get(
                        race_user.fingerprint
                    );

                expect(
                    status_ok &&
                    status_changed &&
                    metadata_ok &&
                    metadata_changed &&
                    after.has_value() &&
                    after->status ==
                        "disabled" &&
                    after->app_prefs_json ==
                        R"({"view":"grid","race":true})" &&
                    after->storage_projection_revision ==
                        before->storage_projection_revision + 1,
                    "NEW-002 concurrent status/app-prefs writes must both survive"
                );
            }
        }


        // -------------------------------------------------------------
        // Storage projection change racing profile metadata.
        // -------------------------------------------------------------
        {
            const auto before =
                race_registry.get(
                    race_user.fingerprint
                );

            expect(
                before.has_value(),
                "NEW-002 storage race should have current user"
            );

            if (before.has_value()) {
                pqnas::UserStorageMetadataUpdate
                    storage_update;

                storage_update.storage_state =
                    before->storage_state;
                storage_update.quota_bytes =
                    before->quota_bytes + 4096;
                storage_update.root_rel =
                    before->root_rel;
                storage_update.storage_pool_id =
                    before->storage_pool_id;
                storage_update.storage_set_at =
                    "2026-08-25T00:01:00Z";
                storage_update.storage_set_by =
                    "concurrent-admin";

                bool storage_ok = false;
                bool storage_changed = false;
                bool projection_changed = false;
                std::string storage_error;

                bool metadata_ok = false;
                bool metadata_changed = false;
                std::string metadata_error;

                pqnas::UserNonAuthMetadataUpdate
                    metadata_update;

                metadata_update.name =
                    "Concurrent metadata";

                run_concurrently(
                    [&]() {
                        storage_ok =
                            race_registry.set_storage_metadata_persisted(
                                race_user.fingerprint,
                                storage_update,
                                race_path.string(),
                                &storage_changed,
                                &projection_changed,
                                &storage_error
                            );
                    },
                    [&]() {
                        metadata_ok =
                            race_registry.set_non_auth_metadata_persisted(
                                race_user.fingerprint,
                                metadata_update,
                                race_path.string(),
                                &metadata_changed,
                                nullptr,
                                &metadata_error
                            );
                    }
                );

                const auto after =
                    race_registry.get(
                        race_user.fingerprint
                    );

                expect(
                    storage_ok &&
                    storage_changed &&
                    projection_changed &&
                    metadata_ok &&
                    metadata_changed &&
                    after.has_value() &&
                    after->quota_bytes ==
                        before->quota_bytes + 4096 &&
                    after->name ==
                        "Concurrent metadata" &&
                    after->home_node ==
                        before->home_node &&
                    after->status ==
                        before->status &&
                    after->storage_projection_revision ==
                        before->storage_projection_revision + 1,
                    "NEW-002 concurrent storage/profile writes must both survive"
                );
            }
        }


        // A fresh registry proves the combined result, not merely process
        // memory, reached the durable users.json snapshot.
        pqnas::UsersRegistry race_reloaded;

        expect(
            race_reloaded.load(
                race_path.string()
            ),
            "NEW-002 concurrent fixture should reload"
        );

        const auto durable =
            race_reloaded.get(
                race_user.fingerprint
            );

        bool durable_prefs_match = false;

        if (durable.has_value()) {
            try {
                durable_prefs_match =
                    json::parse(
                        durable->app_prefs_json
                    ) ==
                    json{
                        {"view", "grid"},
                        {"race", true}
                    };
            } catch (...) {
                durable_prefs_match = false;
            }
        }

        expect(
            durable.has_value() &&
            durable->home_node ==
                "node-02" &&
            durable->status ==
                "disabled" &&
            durable->quota_bytes ==
                8192 &&
            durable->avatar_url ==
                "/avatars/concurrent-placement.png" &&
            durable_prefs_match &&
            durable->name ==
                "Concurrent metadata" &&
            durable->storage_projection_revision ==
                4,
            "NEW-002 concurrent durable snapshot must contain all three authorization and metadata writes"
        );
    }



#if defined(__linux__)

    {
        const fs::path durability_path =
            directory /
            "new001_durability_users.json";

        const json durability_fixture{
            {
                "users",
                json::array({
                    json{
                        {
                            "fingerprint",
                            "new001-durability-user"
                        },
                        {
                            "status",
                            "disabled"
                        },
                        {
                            "home_node",
                            "node-01"
                        },
                        {
                            "storage_projection_revision",
                            1
                        }
                    }
                })
            }
        };

        expect(
            write_json(
                durability_path,
                durability_fixture
            ),
            "NEW-001 durability fixture should be written"
        );

        pqnas::UsersRegistry durability_registry;

        expect(
            durability_registry.load(
                durability_path.string()
            ),
            "NEW-001 durability fixture should load"
        );

        bool status_changed = false;
        std::string durability_error;

        const int mutation_failures_before =
            g_injected_directory_fsync_failures;

        g_fail_next_directory_fsync = true;

        const bool mutation_durable =
            durability_registry.set_status_persisted(
                "new001-durability-user",
                "enabled",
                durability_path.string(),
                &status_changed,
                &durability_error
            );

        expect(
            !mutation_durable,
            "NEW-001 injected parent fsync failure must leave commit uncertain"
        );

        expect(
            !g_fail_next_directory_fsync &&
            g_injected_directory_fsync_failures ==
                mutation_failures_before + 1,
            "NEW-001 mutation must hit exactly one injected directory fsync failure"
        );

        // Security: re-reading the visible file must not certify the rename as
        // durable. ensure_durable() must still perform a real durability write.
        expect(
            durability_registry.load(
                durability_path.string()
            ),
            "NEW-001 successful reload after uncertain commit should succeed"
        );

        const auto reloaded_user =
            durability_registry.get(
                "new001-durability-user"
            );

        expect(
            reloaded_user.has_value() &&
            reloaded_user->status == "enabled",
            "NEW-001 reload should retain the rename-visible committed snapshot"
        );

        const int healing_failures_before =
            g_injected_directory_fsync_failures;

        g_fail_next_directory_fsync = true;
        durability_error.clear();

        expect(
            !durability_registry.ensure_durable(
                &durability_error
            ),
            "NEW-001 load must preserve durability uncertainty for healing"
        );

        expect(
            !g_fail_next_directory_fsync &&
            g_injected_directory_fsync_failures ==
                healing_failures_before + 1,
            "NEW-001 ensure_durable must perform a real directory fsync after load"
        );

        g_fail_next_directory_fsync = false;
        durability_error.clear();

        expect(
            durability_registry.ensure_durable(
                &durability_error
            ),
            "NEW-001 durability retry should heal the visible snapshot"
        );
    }

#endif

    {
        const fs::path atomic_load_path =
            directory /
            "new001_atomic_load_users.json";

        const json original_snapshot{
            {
                "users",
                json::array({
                    json{
                        {
                            "fingerprint",
                            "new001-preserved-user"
                        },
                        {
                            "name",
                            "Preserved"
                        },
                        {
                            "home_node",
                            "node-01"
                        },
                        {
                            "storage_projection_revision",
                            7
                        }
                    }
                })
            }
        };

        expect(
            write_json(
                atomic_load_path,
                original_snapshot
            ),
            "NEW-001 original atomic-load fixture should be written"
        );

        pqnas::UsersRegistry atomic_load_registry;

        expect(
            atomic_load_registry.load(
                atomic_load_path.string()
            ),
            "NEW-001 original atomic-load fixture should load"
        );

        const json invalid_replacement{
            {
                "users",
                json::array({
                    json{
                        {
                            "fingerprint",
                            "new001-invalid-user"
                        },
                        {
                            "home_node",
                            123
                        }
                    }
                })
            }
        };

        expect(
            write_json(
                atomic_load_path,
                invalid_replacement
            ),
            "NEW-001 invalid replacement fixture should be written"
        );

        expect(
            !atomic_load_registry.load(
                atomic_load_path.string()
            ),
            "NEW-001 invalid replacement must fail loading"
        );

        const auto preserved =
            atomic_load_registry.get(
                "new001-preserved-user"
            );

        expect(
            preserved.has_value() &&
            preserved->name == "Preserved" &&
            preserved->home_node == "node-01" &&
            preserved->storage_projection_revision == 7,
            "NEW-001 failed load must preserve the previous authoritative snapshot"
        );

        expect(
            !atomic_load_registry.get(
                "new001-invalid-user"
            ).has_value(),
            "NEW-001 failed load must not partially publish replacement state"
        );
    }


    const fs::path invalid_string_path =
        directory / "invalid_string.json";

    const json invalid_string_json{
        {
            "users",
            json::array({
                json{
                    {"fingerprint", "fp-bad-string"},
                    {"home_node", "../node"}
                }
            })
        }
    };

    expect(
        write_json(
            invalid_string_path,
            invalid_string_json
        ),
        "write invalid home_node string fixture"
    );

    pqnas::UsersRegistry invalid_string_registry;

    expect(
        !invalid_string_registry.load(
            invalid_string_path.string()
        ),
        "invalid persisted home_node string must fail loading"
    );

    const fs::path invalid_type_path =
        directory / "invalid_type.json";

    const json invalid_type_json{
        {
            "users",
            json::array({
                json{
                    {"fingerprint", "fp-bad-type"},
                    {"home_node", 123}
                }
            })
        }
    };

    expect(
        write_json(
            invalid_type_path,
            invalid_type_json
        ),
        "write invalid home_node type fixture"
    );

    pqnas::UsersRegistry invalid_type_registry;

    expect(
        !invalid_type_registry.load(
            invalid_type_path.string()
        ),
        "non-string persisted home_node must fail loading"
    );

    const fs::path invalid_policy_string_path =
        directory / "invalid_policy_string.json";

    const json invalid_policy_string_json{
        {
            "users",
            json::array({
                json{
                    {"fingerprint", "fp-bad-policy"},
                    {
                        "placement_policy_id",
                        "../business"
                    }
                }
            })
        }
    };

    expect(
        write_json(
            invalid_policy_string_path,
            invalid_policy_string_json
        ),
        "write invalid policy ID fixture"
    );

    pqnas::UsersRegistry
        invalid_policy_string_registry;

    expect(
        !invalid_policy_string_registry.load(
            invalid_policy_string_path.string()
        ),
        "unsafe persisted policy ID must fail loading"
    );

    const fs::path invalid_policy_type_path =
        directory / "invalid_policy_type.json";

    const json invalid_policy_type_json{
        {
            "users",
            json::array({
                json{
                    {"fingerprint", "fp-bad-policy-type"},
                    {"placement_policy_id", 123}
                }
            })
        }
    };

    expect(
        write_json(
            invalid_policy_type_path,
            invalid_policy_type_json
        ),
        "write invalid policy type fixture"
    );

    pqnas::UsersRegistry
        invalid_policy_type_registry;

    expect(
        !invalid_policy_type_registry.load(
            invalid_policy_type_path.string()
        ),
        "non-string persisted policy ID must fail loading"
    );

    cleanup();

    if (failures != 0) {
        std::cerr
            << failures
            << " user home_node test(s) failed\n";
        return 1;
    }

    std::cout
        << "OK: user home_node model tests passed\n";

    return 0;
}
