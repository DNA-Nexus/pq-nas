#include "cluster/user_placement_policy_registry.h"

#include <nlohmann/json.hpp>

#include <cerrno>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#if defined(__linux__)

// Test-only durability fault injection.
//
// Security: production code gets no runtime fault hook. The linker wrapper
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
            "pqnas-placement-policy-registry-XXXXXX"
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

    output << contents;
    output.flush();

    return output.good();
}

pqnas::cluster::UserPlacementPolicy
automatic_policy(
    const std::string& policy_id
) {
    pqnas::cluster::UserPlacementPolicy
        policy;

    policy.policy_id = policy_id;

    policy.mode =
        pqnas::cluster::
            UserPlacementPolicyMode::Automatic;

    policy.fallback =
        pqnas::cluster::
            UserPlacementPolicyFallback::Deny;

    return policy;
}

pqnas::cluster::UserPlacementPolicy
business_policy() {
    pqnas::cluster::UserPlacementPolicy
        policy;

    policy.policy_id =
        "business-backup";

    policy.mode =
        pqnas::cluster::
            UserPlacementPolicyMode::AllowedNodes;

    policy.fallback =
        pqnas::cluster::
            UserPlacementPolicyFallback::Deny;

    // Deliberately unsorted. Persistence should canonicalize this set.
    policy.node_ids = {
        "node-business-02",
        "node-business-01"
    };

    return policy;
}

} // namespace

int main() {
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
        "placement_policies.json";

    pqnas::cluster::
        UserPlacementPolicyRegistry registry;

    std::string error;

    expect(
        registry.load(
            registry_path.string(),
            &error
        ),
        "missing registry should load empty: " +
            error
    );

    expect(
        registry.snapshot().empty(),
        "missing registry should create empty state"
    );

    error.clear();

    expect(
        registry.upsert(
            business_policy(),
            &error
        ),
        "create business policy: " +
            error
    );

    error.clear();

    expect(
        registry.upsert(
            automatic_policy("standard"),
            &error
        ),
        "create automatic policy: " +
            error
    );

    const auto snapshot =
        registry.snapshot();

    expect(
        snapshot.size() == 2,
        "registry should contain two policies"
    );

    expect(
        snapshot.size() == 2 &&
        snapshot[0].policy_id ==
            "business-backup" &&
        snapshot[1].policy_id ==
            "standard",
        "snapshot should be sorted by policy ID"
    );

    const auto business =
        registry.get(
            "business-backup"
        );

    expect(
        business.has_value() &&
        business->node_ids.size() == 2 &&
        business->node_ids[0] ==
            "node-business-01" &&
        business->node_ids[1] ==
            "node-business-02",
        "policy node set should be canonicalized"
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
        "registry must be private to owner"
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
                kUserPlacementPolicyRegistrySchemaVersion,
        "persisted schema version"
    );

    expect(
        persisted["policies"].is_array() &&
        persisted["policies"].size() == 2,
        "persisted policy array"
    );

    expect(
        persisted["policies"][0]["policy_id"] ==
            "business-backup" &&
        persisted["policies"][1]["policy_id"] ==
            "standard",
        "persisted policies should be deterministic"
    );

    expect(
        persisted["policies"][0]["node_ids"][0] ==
            "node-business-01" &&
        persisted["policies"][0]["node_ids"][1] ==
            "node-business-02",
        "persisted node IDs should be deterministic"
    );

    pqnas::cluster::
        UserPlacementPolicyRegistry reloaded;

    error.clear();

    expect(
        reloaded.load(
            registry_path.string(),
            &error
        ),
        "reload persisted registry: " +
            error
    );

    const auto reloaded_business =
        reloaded.get(
            "business-backup"
        );

    expect(
        reloaded_business.has_value() &&
        reloaded_business->mode ==
            pqnas::cluster::
                UserPlacementPolicyMode::AllowedNodes &&
        reloaded_business->fallback ==
            pqnas::cluster::
                UserPlacementPolicyFallback::Deny,
        "reload business policy"
    );

    {
        auto invalid =
            automatic_policy(
                "../business"
            );

        error.clear();

        expect(
            !reloaded.upsert(
                invalid,
                &error
            ),
            "path-like policy ID must be rejected"
        );

        expect(
            reloaded.snapshot().size() == 2,
            "invalid mutation must preserve memory"
        );
    }

    {
        const auto before_bad_load =
            reloaded.snapshot();

        expect(
            write_text(
                registry_path,
                R"({"version":1,"policies":[{"policy_id":"x","mode":"automatic","fallback":"deny","node_ids":[],"unknown":true}]})"
            ),
            "write unknown-field registry"
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
            "unknown policy fields must reject load"
        );

        const auto after_bad_load =
            reloaded.snapshot();

        expect(
            after_bad_load.size() ==
                before_bad_load.size() &&
            after_bad_load.size() == 2 &&
            after_bad_load[0].policy_id ==
                before_bad_load[0].policy_id &&
            after_bad_load[1].policy_id ==
                before_bad_load[1].policy_id,
            "failed load must preserve memory snapshot"
        );
    }

    {
        const fs::path duplicate_path =
            test_directory /
            "duplicate.json";

        expect(
            write_text(
                duplicate_path,
                R"({"version":1,"policies":[{"policy_id":"duplicate","mode":"automatic","fallback":"deny","node_ids":[]},{"policy_id":"duplicate","mode":"automatic","fallback":"deny","node_ids":[]}]})"
            ),
            "write duplicate-policy registry"
        );

        expect(
            ::chmod(
                duplicate_path.c_str(),
                0600
            ) == 0,
            "set duplicate registry permissions"
        );

        pqnas::cluster::
            UserPlacementPolicyRegistry
                duplicate_registry;

        error.clear();

        expect(
            !duplicate_registry.load(
                duplicate_path.string(),
                &error
            ),
            "duplicate policy IDs must reject load"
        );
    }

    {
        const fs::path unsafe_path =
            test_directory /
            "unsafe.json";

        expect(
            write_text(
                unsafe_path,
                R"({"version":1,"policies":[]})"
            ),
            "write unsafe-permission registry"
        );

        expect(
            ::chmod(
                unsafe_path.c_str(),
                0644
            ) == 0,
            "set unsafe registry permissions"
        );

        pqnas::cluster::
            UserPlacementPolicyRegistry
                unsafe_registry;

        error.clear();

        expect(
            !unsafe_registry.load(
                unsafe_path.string(),
                &error
            ),
            "group/other-readable policy registry must be rejected"
        );
    }

    {
        const fs::path symlink_target =
            test_directory /
            "symlink-target.json";

        const fs::path symlink_path =
            test_directory /
            "symlink.json";

        expect(
            write_text(
                symlink_target,
                R"({"version":1,"policies":[]})"
            ),
            "write symlink target"
        );

        expect(
            ::chmod(
                symlink_target.c_str(),
                0600
            ) == 0,
            "set symlink target permissions"
        );

        expect(
            ::symlink(
                symlink_target.c_str(),
                symlink_path.c_str()
            ) == 0,
            "create registry symlink"
        );

        pqnas::cluster::
            UserPlacementPolicyRegistry
                symlink_registry;

        error.clear();

        expect(
            !symlink_registry.load(
                symlink_path.string(),
                &error
            ),
            "policy registry symlink must be rejected"
        );
    }

    {
        const fs::path rollback_directory =
            test_directory /
            "rollback";

        const fs::path rollback_path =
            rollback_directory /
            "placement_policies.json";

        pqnas::cluster::
            UserPlacementPolicyRegistry
                rollback_registry;

        error.clear();

        expect(
            rollback_registry.load(
                rollback_path.string(),
                &error
            ),
            "load rollback registry: " +
                error
        );

        error.clear();

        expect(
            rollback_registry.upsert(
                automatic_policy(
                    "before-failure"
                ),
                &error
            ),
            "create rollback policy: " +
                error
        );

        const auto before_failed_write =
            rollback_registry.snapshot();

        std::error_code filesystem_error;

        const bool removed_registry =
            fs::remove(
                rollback_path,
                filesystem_error
            );

        expect(
            removed_registry &&
            !filesystem_error,
            "remove rollback registry file"
        );

        filesystem_error.clear();

        const bool removed_directory =
            fs::remove(
                rollback_directory,
                filesystem_error
            );

        expect(
            removed_directory &&
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

        expect(
            !rollback_registry.upsert(
                automatic_policy(
                    "after-failure"
                ),
                &error
            ),
            "failed persistence must reject mutation"
        );

        const auto after_failed_write =
            rollback_registry.snapshot();

        expect(
            after_failed_write.size() ==
                before_failed_write.size() &&
            after_failed_write.size() == 1 &&
            after_failed_write[0].policy_id ==
                "before-failure",
            "failed persistence must preserve memory"
        );
    }

    {
        const fs::path erase_path =
            test_directory /
            "erase.json";

        pqnas::cluster::
            UserPlacementPolicyRegistry
                erase_registry;

        error.clear();

        expect(
            erase_registry.load(
                erase_path.string(),
                &error
            ),
            "load erase registry: " +
                error
        );

        expect(
            erase_registry.upsert(
                automatic_policy(
                    "temporary"
                ),
                &error
            ),
            "create policy to erase: " +
                error
        );

        error.clear();

        expect(
            erase_registry.erase(
                "temporary",
                &error
            ),
            "erase existing policy: " +
                error
        );

        expect(
            erase_registry.snapshot().empty(),
            "erased policy should leave empty registry"
        );

        error.clear();

        expect(
            !erase_registry.erase(
                "temporary",
                &error
            ),
            "erasing missing policy should fail"
        );
    }


#if defined(__linux__)
    {
        const fs::path durability_path =
            test_directory /
            "durability-fsync.json";

        pqnas::cluster::
            UserPlacementPolicyRegistry
                durability_registry;

        error.clear();

        expect(
            durability_registry.load(
                durability_path.string(),
                &error
            ),
            "load durability regression registry: " +
                error
        );

        const int failures_before =
            g_injected_directory_fsync_failures;

        g_fail_next_directory_fsync = true;
        error.clear();

        expect(
            !durability_registry.upsert(
                automatic_policy(
                    "post-rename-visible"
                ),
                &error
            ),
            "parent-directory fsync failure must reject durable success"
        );

        expect(
            g_injected_directory_fsync_failures ==
                failures_before + 1,
            "durability regression must inject exactly one directory fsync failure"
        );

        expect(
            error ==
                "failed to sync policy registry directory",
            "directory fsync failure must be reported explicitly"
        );

        // rename() already made the new inode visible. Even though durability
        // confirmation failed, process memory must not roll back to the stale
        // pre-mutation policy snapshot.
        expect(
            durability_registry.get(
                "post-rename-visible"
            ).has_value(),
            "post-rename fsync failure must keep memory aligned with visible policy snapshot"
        );

        // Exact retry must be able to establish the missing durability
        // boundary without requiring a process restart.
        error.clear();

        expect(
            durability_registry.upsert(
                automatic_policy(
                    "post-rename-visible"
                ),
                &error
            ),
            "retry must durably persist the visible policy snapshot: " +
                error
        );

        pqnas::cluster::
            UserPlacementPolicyRegistry
                durability_reloaded;

        error.clear();

        expect(
            durability_reloaded.load(
                durability_path.string(),
                &error
            ),
            "reload durability regression registry: " +
                error
        );

        expect(
            durability_reloaded.get(
                "post-rename-visible"
            ).has_value(),
            "durable retry must survive registry reload"
        );
    }

    {
        const fs::path erase_durability_path =
            test_directory /
            "erase-durability-fsync.json";

        pqnas::cluster::
            UserPlacementPolicyRegistry
                erase_durability_registry;

        error.clear();

        expect(
            erase_durability_registry.load(
                erase_durability_path.string(),
                &error
            ),
            "load erase durability registry: " +
                error
        );

        error.clear();

        expect(
            erase_durability_registry.upsert(
                automatic_policy(
                    "erase-post-rename"
                ),
                &error
            ),
            "seed erase durability policy: " +
                error
        );

        const int failures_before_erase =
            g_injected_directory_fsync_failures;

        g_fail_next_directory_fsync = true;
        error.clear();

        expect(
            !erase_durability_registry.erase(
                "erase-post-rename",
                &error
            ),
            "erase parent-directory fsync failure must reject durable success"
        );

        expect(
            g_injected_directory_fsync_failures ==
                failures_before_erase + 1,
            "erase must inject exactly one directory fsync failure"
        );

        expect(
            erase_durability_registry.snapshot().empty(),
            "post-rename erase failure must keep memory aligned with visible snapshot"
        );

        // Retry the already-visible erase. Because the policy is now absent
        // from memory, this specifically verifies that the uncertainty marker
        // causes the current snapshot to be re-persisted before NotFound.
        g_fail_next_directory_fsync = true;
        error.clear();

        expect(
            !erase_durability_registry.erase(
                "erase-post-rename",
                &error
            ),
            "uncertain erase retry should still report failure"
        );

        expect(
            g_injected_directory_fsync_failures ==
                failures_before_erase + 2,
            "uncertain erase retry must re-attempt directory fsync"
        );

        expect(
            error ==
                "failed to sync policy registry directory",
            "uncertain erase retry must expose durability failure before not-found"
        );

        // One more retry without injected failure heals the uncertainty.
        error.clear();

        expect(
            !erase_durability_registry.erase(
                "erase-post-rename",
                &error
            ),
            "healed missing erase retains existing not-found API semantics"
        );

        expect(
            error ==
                "placement policy not found",
            "successful durability healing must restore normal not-found result"
        );

        pqnas::cluster::
            UserPlacementPolicyRegistry
                erase_durability_reloaded;

        error.clear();

        expect(
            erase_durability_reloaded.load(
                erase_durability_path.string(),
                &error
            ),
            "reload healed erase registry: " +
                error
        );

        expect(
            !erase_durability_reloaded.get(
                "erase-post-rename"
            ).has_value(),
            "healed erase must remain absent after reload"
        );
    }
#endif

    cleanup();

    if (failures != 0) {
        std::cerr
            << failures
            << " placement policy registry test(s) failed\n";
        return 1;
    }

    std::cout
        << "OK: placement policy registry tests passed\n";

    return 0;
}
