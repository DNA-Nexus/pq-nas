#include "users_registry.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <unistd.h>
#include <vector>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

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

fs::path make_test_directory() {
    std::string pattern =
        (
            fs::temp_directory_path() /
            "pqnas-user-projection-revision-test-XXXXXX"
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

    output
        << value.dump(2)
        << '\n';

    output.flush();

    return output.good();
}

pqnas::UserRec make_user(
    const std::string& fingerprint
) {
    pqnas::UserRec user;

    user.fingerprint =
        fingerprint;

    user.name =
        "Revision Test";

    user.role =
        "user";

    user.status =
        "disabled";

    user.storage_state =
        "unallocated";

    user.quota_bytes = 0;
    user.root_rel.clear();
    user.storage_pool_id.clear();

    user.home_node.clear();
    user.placement_policy_id.clear();

    return user;
}

} // namespace

int main() {
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
        directory /
        "users.json";

    pqnas::UsersRegistry users;

    bool projection_changed = false;
    std::string error;

    auto user =
        make_user(
            "revision-user"
        );

    // A write-through insertion establishes the first authoritative snapshot.
    expect(
        users.upsert_persisted(
            user,
            users_path.string(),
            &projection_changed,
            &error
        ),
        "initial persisted upsert succeeds: " +
            error
    );

    auto stored =
        users.get(
            user.fingerprint
        );

    expect(
        stored.has_value() &&
        stored->storage_projection_revision == 1 &&
        projection_changed,
        "new persisted user starts at projection revision 1"
    );

    // Profile-only changes persist but do not churn Storage authorization.
    {
        auto candidate =
            *stored;

        candidate.name =
            "Profile-only change";

        projection_changed = true;
        error.clear();

        expect(
            users.upsert_persisted(
                candidate,
                users_path.string(),
                &projection_changed,
                &error
            ),
            "profile-only persisted update succeeds"
        );

        stored =
            users.get(
                user.fingerprint
            );

        expect(
            stored &&
            stored->storage_projection_revision == 1 &&
            !projection_changed,
            "profile-only update keeps projection revision"
        );
    }

    // enabled=false -> enabled=true changes the Node projection.
    {
        bool changed = false;
        error.clear();

        expect(
            users.set_status_persisted(
                user.fingerprint,
                "enabled",
                users_path.string(),
                &changed,
                &error
            ),
            "persisted enable succeeds"
        );

        stored =
            users.get(
                user.fingerprint
            );

        expect(
            stored &&
            stored->status == "enabled" &&
            stored->storage_projection_revision == 2 &&
            changed,
            "enable increments projection revision"
        );

        changed = true;

        expect(
            users.set_status_persisted(
                user.fingerprint,
                "enabled",
                users_path.string(),
                &changed,
                &error
            ),
            "idempotent persisted enable succeeds"
        );

        stored =
            users.get(
                user.fingerprint
            );

        expect(
            stored &&
            stored->storage_projection_revision == 2 &&
            !changed,
            "idempotent status does not increment revision"
        );
    }

    // disabled and revoked both project enabled=false. The first transition
    // enabled -> disabled changes projection; disabled -> revoked does not.
    {
        bool changed = false;

        expect(
            users.set_status_persisted(
                user.fingerprint,
                "disabled",
                users_path.string(),
                &changed,
                &error
            ),
            "persisted disable succeeds"
        );

        stored =
            users.get(
                user.fingerprint
            );

        expect(
            stored &&
            stored->storage_projection_revision == 3,
            "enabled to disabled increments revision"
        );

        expect(
            users.set_status_persisted(
                user.fingerprint,
                "revoked",
                users_path.string(),
                &changed,
                &error
            ),
            "persisted revoke succeeds"
        );

        stored =
            users.get(
                user.fingerprint
            );

        expect(
            stored &&
            stored->status == "revoked" &&
            stored->storage_projection_revision == 3,
            "disabled to revoked does not churn Node projection revision"
        );
    }

    // Stale storage metadata is intentionally ignored while unallocated.
    {
        auto candidate =
            *stored;

        candidate.quota_bytes = 12345;
        candidate.root_rel =
            "stale/local/value";
        candidate.storage_pool_id =
            "pool_01";

        projection_changed = true;

        expect(
            users.upsert_persisted(
                candidate,
                users_path.string(),
                &projection_changed,
                &error
            ),
            "unallocated local metadata update persists"
        );

        stored =
            users.get(
                user.fingerprint
            );

        expect(
            stored &&
            stored->storage_projection_revision == 3 &&
            !projection_changed,
            "unallocated quota/root/pool are not projection semantics"
        );
    }

    // Allocation makes quota/root/pool part of the Storage projection.
    {
        auto candidate =
            *stored;

        candidate.storage_state =
            "allocated";

        candidate.quota_bytes =
            1024;

        candidate.root_rel =
            "users/revision-user";

        candidate.storage_pool_id =
            "pool_01";

        projection_changed = false;

        expect(
            users.upsert_persisted(
                candidate,
                users_path.string(),
                &projection_changed,
                &error
            ),
            "storage allocation persisted update succeeds"
        );

        stored =
            users.get(
                user.fingerprint
            );

        expect(
            stored &&
            stored->storage_projection_revision == 4 &&
            projection_changed,
            "storage allocation increments projection revision"
        );
    }

    // Realized placement is projection state.
    {
        bool changed = false;

        expect(
            users.set_home_node_persisted(
                user.fingerprint,
                "node-01",
                users_path.string(),
                &changed,
                &error
            ),
            "persisted home-node assignment succeeds"
        );

        stored =
            users.get(
                user.fingerprint
            );

        expect(
            stored &&
            stored->home_node == "node-01" &&
            stored->storage_projection_revision == 5 &&
            changed,
            "home-node assignment increments projection revision"
        );
    }

    // Placement policy is Director intent only and must not churn Node state.
    {
        bool changed = false;

        expect(
            users.set_placement_policy_id_persisted(
                user.fingerprint,
                "business-backup",
                users_path.string(),
                &changed,
                &error
            ),
            "placement policy persistence succeeds"
        );

        stored =
            users.get(
                user.fingerprint
            );

        expect(
            stored &&
            stored->placement_policy_id ==
                "business-backup" &&
            stored->storage_projection_revision == 5,
            "placement-policy intent does not increment projection revision"
        );
    }

    // Generic upsert cannot forge or rewind registry-owned revisions.
    {
        auto candidate =
            *stored;

        candidate.storage_projection_revision =
            999999;

        candidate.name =
            "Caller cannot own revision";

        expect(
            users.upsert(candidate),
            "generic profile upsert succeeds"
        );

        stored =
            users.get(
                user.fingerprint
            );

        expect(
            stored &&
            stored->storage_projection_revision == 5,
            "generic caller cannot overwrite projection revision"
        );

        expect(
            users.save(
                users_path.string()
            ),
            "generic profile update save succeeds"
        );
    }

    // Revision survives a normal users.json save/reload.
    {
        pqnas::UsersRegistry reloaded;

        expect(
            reloaded.load(
                users_path.string()
            ),
            "revision-bearing users.json reloads"
        );

        const auto reloaded_user =
            reloaded.get(
                user.fingerprint
            );

        expect(
            reloaded_user &&
            reloaded_user->storage_projection_revision == 5,
            "projection revision survives reload"
        );
    }

    // Older files without the field remain backward-compatible.
    {
        const fs::path legacy_path =
            directory /
            "legacy-users.json";

        const json legacy{
            {
                "users",
                json::array(
                    {
                        json{
                            {"fingerprint", "legacy-user"},
                            {"name", ""},
                            {"role", "user"},
                            {"status", "disabled"}
                        }
                    }
                )
            }
        };

        expect(
            write_json(
                legacy_path,
                legacy
            ),
            "legacy users fixture written"
        );

        pqnas::UsersRegistry legacy_registry;

        expect(
            legacy_registry.load(
                legacy_path.string()
            ),
            "users.json without revision remains loadable"
        );

        const auto legacy_user =
            legacy_registry.get(
                "legacy-user"
            );

        expect(
            legacy_user &&
            legacy_user->storage_projection_revision == 0,
            "missing revision loads as legacy revision zero"
        );
    }

    // Malformed explicit revisions fail closed.
    {
        const fs::path invalid_path =
            directory /
            "invalid-revision.json";

        const json invalid{
            {
                "users",
                json::array(
                    {
                        json{
                            {"fingerprint", "invalid-user"},
                            {"name", ""},
                            {"role", "user"},
                            {"status", "disabled"},
                            {
                                "storage_projection_revision",
                                "not-a-number"
                            }
                        }
                    }
                )
            }
        };

        expect(
            write_json(
                invalid_path,
                invalid
            ),
            "invalid revision fixture written"
        );

        pqnas::UsersRegistry invalid_registry;

        expect(
            !invalid_registry.load(
                invalid_path.string()
            ),
            "non-numeric persisted revision fails closed"
        );
    }

    // Save failure rolls back both projection data and revision.
    {
        const auto before =
            users.get(
                user.fingerprint
            );

        bool changed = false;
        error.clear();

        // Passing an existing directory as the target filename makes the
        // atomic file rename fail without relying on permission tricks.
        expect(
            !users.set_status_persisted(
                user.fingerprint,
                "enabled",
                directory.string(),
                &changed,
                &error
            ),
            "failed users.json replacement reports failure"
        );

        const auto after =
            users.get(
                user.fingerprint
            );

        expect(
            before &&
            after &&
            after->status == before->status &&
            after->storage_projection_revision ==
                before->storage_projection_revision &&
            !changed,
            "failed persistence rolls back data and revision together"
        );
    }

    // UINT64_MAX cannot wrap to zero or otherwise reuse an old revision.
    {
        const fs::path max_path =
            directory /
            "max-revision.json";

        const json maximum{
            {
                "users",
                json::array(
                    {
                        json{
                            {"fingerprint", "max-user"},
                            {"name", ""},
                            {"role", "user"},
                            {"status", "disabled"},
                            {"storage_state", "unallocated"},
                            {"quota_bytes", 0},
                            {"root_rel", ""},
                            {"storage_pool_id", ""},
                            {"home_node", "node-01"},
                            {"placement_policy_id", ""},
                            {
                                "storage_projection_revision",
                                std::numeric_limits<
                                    std::uint64_t
                                >::max()
                            }
                        }
                    }
                )
            }
        };

        expect(
            write_json(
                max_path,
                maximum
            ),
            "max revision fixture written"
        );

        pqnas::UsersRegistry max_registry;

        expect(
            max_registry.load(
                max_path.string()
            ),
            "max revision fixture loads"
        );

        bool changed = false;
        error.clear();

        expect(
            !max_registry.set_status_persisted(
                "max-user",
                "enabled",
                max_path.string(),
                &changed,
                &error
            ),
            "revision exhaustion fails closed"
        );

        const auto max_user =
            max_registry.get(
                "max-user"
            );

        expect(
            max_user &&
            max_user->status == "disabled" &&
            max_user->storage_projection_revision ==
                std::numeric_limits<
                    std::uint64_t
                >::max(),
            "revision exhaustion cannot mutate authorization state"
        );
    }

    cleanup();

    if (failures != 0) {
        std::cerr
            << "FAIL: "
            << failures
            << " user storage projection revision test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: user storage projection revision tests passed\n";

    return 0;
}
