#include "cluster/storage_user_projection_sync_state_store.h"

#include "cluster/cluster_identity_validation.h"
#include "storage_metadata_validation.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace pqnas::cluster {
namespace {

using json = nlohmann::json;

std::atomic<std::uint64_t> g_temp_counter{0};

class UniqueFd {
public:
    UniqueFd() = default;

    explicit UniqueFd(
        int descriptor
    ) noexcept
        : descriptor_(descriptor) {}

    ~UniqueFd() {
        reset();
    }

    UniqueFd(
        const UniqueFd&
    ) = delete;

    UniqueFd& operator=(
        const UniqueFd&
    ) = delete;

    UniqueFd(
        UniqueFd&& other
    ) noexcept
        : descriptor_(other.release()) {}

    UniqueFd& operator=(
        UniqueFd&& other
    ) noexcept {
        if (this != &other) {
            reset(
                other.release()
            );
        }

        return *this;
    }

    int get() const noexcept {
        return descriptor_;
    }

    int release() noexcept {
        const int descriptor =
            descriptor_;

        descriptor_ = -1;

        return descriptor;
    }

    void reset(
        int descriptor = -1
    ) noexcept {
        if (descriptor_ >= 0) {
            (void)::close(
                descriptor_
            );
        }

        descriptor_ = descriptor;
    }

private:
    int descriptor_ = -1;
};

void set_error(
    std::string* error,
    const std::string& message
) {
    if (error) {
        *error = message;
    }
}

bool has_only_keys(
    const json& value,
    std::initializer_list<const char*> allowed
) {
    if (!value.is_object()) {
        return false;
    }

    for (const auto& entry :
         value.items()) {
        bool found = false;

        for (const char* key :
             allowed) {
            if (entry.key() == key) {
                found = true;
                break;
            }
        }

        if (!found) {
            return false;
        }
    }

    return true;
}

bool json_u64(
    const json& value,
    std::uint64_t* output
) {
    if (!output) {
        return false;
    }

    try {
        if (value.is_number_unsigned()) {
            *output =
                value.get<std::uint64_t>();

            return true;
        }

        if (value.is_number_integer()) {
            const std::int64_t signed_value =
                value.get<std::int64_t>();

            if (signed_value < 0) {
                return false;
            }

            *output =
                static_cast<std::uint64_t>(
                    signed_value
                );

            return true;
        }
    } catch (...) {
        return false;
    }

    return false;
}

bool valid_state(
    const StorageUserProjectionSyncState& state
) {
    return
        pqnas::valid_storage_user_fingerprint_hex(
            state.fingerprint
        ) &&
        valid_cluster_node_id(
            state.home_node
        ) &&
        state.revision != 0;
}

bool state_equal(
    const StorageUserProjectionSyncState& left,
    const StorageUserProjectionSyncState& right
) {
    return
        left.fingerprint ==
            right.fingerprint &&
        left.home_node ==
            right.home_node &&
        left.revision ==
            right.revision;
}

bool normalize_store_path(
    const std::string& raw_path,
    std::filesystem::path* output,
    std::string* error
) {
    if (!output ||
        raw_path.empty()) {
        set_error(
            error,
            "invalid projection sync-state path"
        );

        return false;
    }

    std::filesystem::path path(
        raw_path
    );

    if (!path.is_absolute()) {
        set_error(
            error,
            "projection sync-state path must be absolute"
        );

        return false;
    }

    path =
        path.lexically_normal();

    if (path.empty() ||
        path.filename().empty() ||
        path.parent_path().empty()) {
        set_error(
            error,
            "invalid projection sync-state path"
        );

        return false;
    }

    *output =
        std::move(path);

    return true;
}

bool validate_directory_descriptor(
    int descriptor,
    std::string* error
) {
    struct stat stats {};

    if (::fstat(
            descriptor,
            &stats) != 0) {
        set_error(
            error,
            "failed to inspect projection sync-state directory"
        );

        return false;
    }

    const uid_t effective_uid =
        ::geteuid();

    if (!S_ISDIR(stats.st_mode) ||
        (stats.st_mode & 0022) != 0 ||
        (stats.st_uid != effective_uid &&
         stats.st_uid != 0)) {
        set_error(
            error,
            "unsafe projection sync-state directory"
        );

        return false;
    }

    return true;
}

bool validate_existing_target(
    int directory_descriptor,
    const std::string& filename,
    std::string* error
) {
    struct stat stats {};

    if (::fstatat(
            directory_descriptor,
            filename.c_str(),
            &stats,
            AT_SYMLINK_NOFOLLOW) != 0) {
        if (errno == ENOENT) {
            return true;
        }

        set_error(
            error,
            "failed to inspect projection sync-state file"
        );

        return false;
    }

    const uid_t effective_uid =
        ::geteuid();

    if (!S_ISREG(stats.st_mode) ||
        stats.st_nlink != 1 ||
        (stats.st_mode & 0077) != 0 ||
        (stats.st_uid != effective_uid &&
         stats.st_uid != 0)) {
        set_error(
            error,
            "unsafe projection sync-state file"
        );

        return false;
    }

    return true;
}

bool read_bounded_file(
    int descriptor,
    std::string* output,
    std::string* error
) {
    if (!output) {
        set_error(
            error,
            "missing projection sync-state read output"
        );

        return false;
    }

    output->clear();

    char buffer[8192];

    while (true) {
        const ssize_t count =
            ::read(
                descriptor,
                buffer,
                sizeof(buffer)
            );

        if (count > 0) {
            if (static_cast<std::size_t>(count) >
                kStorageUserProjectionSyncStateMaxBytes -
                    output->size()) {
                set_error(
                    error,
                    "projection sync-state file is too large"
                );

                return false;
            }

            output->append(
                buffer,
                static_cast<std::size_t>(
                    count
                )
            );

            continue;
        }

        if (count == 0) {
            break;
        }

        if (errno == EINTR) {
            continue;
        }

        set_error(
            error,
            "failed to read projection sync-state file"
        );

        return false;
    }

    return true;
}

bool write_all(
    int descriptor,
    const std::string& contents,
    std::string* error
) {
    std::size_t offset = 0;

    while (offset < contents.size()) {
        const ssize_t count =
            ::write(
                descriptor,
                contents.data() + offset,
                contents.size() - offset
            );

        if (count > 0) {
            offset +=
                static_cast<std::size_t>(
                    count
                );

            continue;
        }

        if (count < 0 &&
            errno == EINTR) {
            continue;
        }

        set_error(
            error,
            "failed to write projection sync-state store"
        );

        return false;
    }

    return true;
}

bool validate_state_set(
    const std::unordered_map<
        std::string,
        StorageUserProjectionSyncState
    >& states,
    std::string* error
) {
    if (states.size() >
        kStorageUserProjectionSyncStateMaxEntries) {
        set_error(
            error,
            "too many projection sync-state entries"
        );

        return false;
    }

    for (const auto& entry :
         states) {
        if (entry.first !=
                entry.second.fingerprint ||
            !valid_state(
                entry.second)) {
            set_error(
                error,
                "invalid projection sync-state entry"
            );

            return false;
        }
    }

    return true;
}

json state_to_json(
    const StorageUserProjectionSyncState& state
) {
    return json{
        {"fingerprint", state.fingerprint},
        {"home_node", state.home_node},
        {"revision", state.revision}
    };
}

bool parse_state_json(
    const json& value,
    StorageUserProjectionSyncState* output
) {
    if (!output ||
        !has_only_keys(
            value,
            {
                "fingerprint",
                "home_node",
                "revision"
            }) ||
        value.size() != 3 ||
        !value.contains("fingerprint") ||
        !value.contains("home_node") ||
        !value.contains("revision") ||
        !value["fingerprint"].is_string() ||
        !value["home_node"].is_string()) {
        return false;
    }

    StorageUserProjectionSyncState state;

    try {
        state.fingerprint =
            value["fingerprint"].
                get<std::string>();

        state.home_node =
            value["home_node"].
                get<std::string>();
    } catch (...) {
        return false;
    }

    if (!json_u64(
            value["revision"],
            &state.revision) ||
        !valid_state(state)) {
        return false;
    }

    *output =
        std::move(state);

    return true;
}

bool read_store_file(
    const std::filesystem::path& path,
    std::unordered_map<
        std::string,
        StorageUserProjectionSyncState
    >* output,
    bool* missing,
    std::string* error
) {
    if (!output ||
        !missing) {
        set_error(
            error,
            "missing projection sync-state load output"
        );

        return false;
    }

    *missing = false;

    const int raw_directory_descriptor =
        ::open(
            path.parent_path().c_str(),
            O_RDONLY |
            O_DIRECTORY |
            O_CLOEXEC |
            O_NOFOLLOW
        );

    if (raw_directory_descriptor < 0) {
        if (errno == ENOENT) {
            output->clear();
            *missing = true;
            return true;
        }

        set_error(
            error,
            "failed to open projection sync-state directory"
        );

        return false;
    }

    UniqueFd directory_descriptor(
        raw_directory_descriptor
    );

    if (!validate_directory_descriptor(
            directory_descriptor.get(),
            error)) {
        return false;
    }

    const std::string filename =
        path.filename().string();

    const int raw_file_descriptor =
        ::openat(
            directory_descriptor.get(),
            filename.c_str(),
            O_RDONLY |
            O_CLOEXEC |
            O_NOFOLLOW
        );

    if (raw_file_descriptor < 0) {
        if (errno == ENOENT) {
            output->clear();
            *missing = true;
            return true;
        }

        set_error(
            error,
            "failed to open projection sync-state store"
        );

        return false;
    }

    UniqueFd file_descriptor(
        raw_file_descriptor
    );

    struct stat stats {};

    if (::fstat(
            file_descriptor.get(),
            &stats) != 0) {
        set_error(
            error,
            "failed to inspect projection sync-state store"
        );

        return false;
    }

    const uid_t effective_uid =
        ::geteuid();

    if (!S_ISREG(stats.st_mode) ||
        stats.st_nlink != 1 ||
        (stats.st_mode & 0077) != 0 ||
        (stats.st_uid != effective_uid &&
         stats.st_uid != 0)) {
        set_error(
            error,
            "unsafe projection sync-state file"
        );

        return false;
    }

    if (stats.st_size < 0 ||
        static_cast<unsigned long long>(
            stats.st_size) >
        static_cast<unsigned long long>(
            kStorageUserProjectionSyncStateMaxBytes)) {
        set_error(
            error,
            "projection sync-state file has invalid size"
        );

        return false;
    }

    std::string contents;

    if (!read_bounded_file(
            file_descriptor.get(),
            &contents,
            error)) {
        return false;
    }

    json root;

    try {
        root =
            json::parse(contents);
    } catch (...) {
        set_error(
            error,
            "invalid projection sync-state JSON"
        );

        return false;
    }

    if (!has_only_keys(
            root,
            {"version", "states"}) ||
        root.size() != 2 ||
        !root.contains("version") ||
        !root.contains("states") ||
        !root["version"].is_number_integer() ||
        !root["states"].is_array() ||
        root["states"].size() >
            kStorageUserProjectionSyncStateMaxEntries) {
        set_error(
            error,
            "invalid projection sync-state root"
        );

        return false;
    }

    int version = 0;

    try {
        version =
            root["version"].get<int>();
    } catch (...) {
        set_error(
            error,
            "invalid projection sync-state version"
        );

        return false;
    }

    if (version !=
        kStorageUserProjectionSyncStateSchemaVersion) {
        set_error(
            error,
            "unsupported projection sync-state version"
        );

        return false;
    }

    std::unordered_map<
        std::string,
        StorageUserProjectionSyncState
    > parsed;

    for (const json& value :
         root["states"]) {
        StorageUserProjectionSyncState state;

        if (!parse_state_json(
                value,
                &state)) {
            set_error(
                error,
                "invalid projection sync-state entry"
            );

            return false;
        }

        if (!parsed.emplace(
                state.fingerprint,
                state
            ).second) {
            set_error(
                error,
                "duplicate projection sync-state fingerprint"
            );

            return false;
        }
    }

    *output =
        std::move(parsed);

    return true;
}

bool persist_store_file(
    const std::filesystem::path& path,
    const std::unordered_map<
        std::string,
        StorageUserProjectionSyncState
    >& states,
    bool* committed,
    std::string* error
) {
    if (!committed) {
        set_error(
            error,
            "missing projection sync-state commit output"
        );

        return false;
    }

    *committed = false;

    if (!validate_state_set(
            states,
            error)) {
        return false;
    }

    std::vector<std::string> fingerprints;

    fingerprints.reserve(
        states.size()
    );

    for (const auto& entry :
         states) {
        fingerprints.push_back(
            entry.first
        );
    }

    std::sort(
        fingerprints.begin(),
        fingerprints.end()
    );

    json root{
        {
            "version",
            kStorageUserProjectionSyncStateSchemaVersion
        },
        {
            "states",
            json::array()
        }
    };

    for (const std::string& fingerprint :
         fingerprints) {
        root["states"].push_back(
            state_to_json(
                states.at(fingerprint)
            )
        );
    }

    std::string contents;

    try {
        contents =
            root.dump(2);

        contents.push_back('\n');
    } catch (...) {
        set_error(
            error,
            "failed to serialize projection sync-state store"
        );

        return false;
    }

    if (contents.size() >
        kStorageUserProjectionSyncStateMaxBytes) {
        set_error(
            error,
            "serialized projection sync-state store is too large"
        );

        return false;
    }

    std::error_code filesystem_error;

    const bool created_directory =
        std::filesystem::create_directories(
            path.parent_path(),
            filesystem_error
        );

    if (filesystem_error) {
        set_error(
            error,
            "failed to create projection sync-state directory"
        );

        return false;
    }

    const int raw_directory_descriptor =
        ::open(
            path.parent_path().c_str(),
            O_RDONLY |
            O_DIRECTORY |
            O_CLOEXEC |
            O_NOFOLLOW
        );

    if (raw_directory_descriptor < 0) {
        set_error(
            error,
            "failed to open projection sync-state directory"
        );

        return false;
    }

    UniqueFd directory_descriptor(
        raw_directory_descriptor
    );

    if (created_directory) {
        struct stat created_stats {};

        // Security: tighten permissions only on the directory inode that this
        // process just created and owns.
        if (::fstat(
                directory_descriptor.get(),
                &created_stats) != 0 ||
            !S_ISDIR(created_stats.st_mode) ||
            created_stats.st_uid !=
                ::geteuid() ||
            ::fchmod(
                directory_descriptor.get(),
                0700) != 0) {
            set_error(
                error,
                "failed to secure projection sync-state directory"
            );

            return false;
        }
    }

    if (!validate_directory_descriptor(
            directory_descriptor.get(),
            error)) {
        return false;
    }

    const std::string target_filename =
        path.filename().string();

    if (!validate_existing_target(
            directory_descriptor.get(),
            target_filename,
            error)) {
        return false;
    }

    std::string temporary_filename;
    UniqueFd temporary_descriptor;

    for (int attempt = 0;
         attempt < 16;
         ++attempt) {
        const std::uint64_t counter =
            g_temp_counter.fetch_add(1);

        temporary_filename =
            "." +
            target_filename +
            ".tmp." +
            std::to_string(
                static_cast<long long>(
                    ::getpid()
                )
            ) +
            "." +
            std::to_string(counter);

        const int raw_temporary_descriptor =
            ::openat(
                directory_descriptor.get(),
                temporary_filename.c_str(),
                O_WRONLY |
                O_CREAT |
                O_EXCL |
                O_CLOEXEC |
                O_NOFOLLOW,
                0600
            );

        if (raw_temporary_descriptor >= 0) {
            temporary_descriptor =
                UniqueFd(
                    raw_temporary_descriptor
                );

            break;
        }

        if (errno != EEXIST) {
            set_error(
                error,
                "failed to create projection sync-state temp file"
            );

            return false;
        }
    }

    if (temporary_descriptor.get() < 0) {
        set_error(
            error,
            "failed to allocate projection sync-state temp file"
        );

        return false;
    }

    const auto remove_temporary =
        [&]() {
            if (!temporary_filename.empty()) {
                (void)::unlinkat(
                    directory_descriptor.get(),
                    temporary_filename.c_str(),
                    0
                );
            }
        };

    if (::fchmod(
            temporary_descriptor.get(),
            0600) != 0 ||
        !write_all(
            temporary_descriptor.get(),
            contents,
            error) ||
        ::fsync(
            temporary_descriptor.get()) != 0) {
        if (error &&
            error->empty()) {
            *error =
                "failed to sync projection sync-state store";
        }

        remove_temporary();
        return false;
    }

    const int raw_temporary_descriptor =
        temporary_descriptor.release();

    if (::close(
            raw_temporary_descriptor) != 0) {
        set_error(
            error,
            "failed to close projection sync-state temp file"
        );

        remove_temporary();
        return false;
    }

    // Security: re-check the existing target immediately before rename.
    if (!validate_existing_target(
            directory_descriptor.get(),
            target_filename,
            error)) {
        remove_temporary();
        return false;
    }

    if (::renameat(
            directory_descriptor.get(),
            temporary_filename.c_str(),
            directory_descriptor.get(),
            target_filename.c_str()) != 0) {
        set_error(
            error,
            "failed to commit projection sync-state store"
        );

        remove_temporary();
        return false;
    }

    // rename() has made the new inode logically visible.
    *committed = true;

    if (::fsync(
            directory_descriptor.get()) != 0) {
        set_error(
            error,
            "failed to sync projection sync-state directory"
        );

        return false;
    }

    return true;
}

StorageUserProjectionSyncStateStoreResult
semantic_failure(
    StorageUserProjectionSyncStateStoreResult result,
    std::string* error
) {
    set_error(
        error,
        storage_user_projection_sync_state_store_result_code(
            result
        )
    );

    return result;
}

} // namespace

const char*
storage_user_projection_sync_state_store_result_code(
    StorageUserProjectionSyncStateStoreResult result
) noexcept {
    switch (result) {
        case StorageUserProjectionSyncStateStoreResult::
                Applied:
            return "applied";

        case StorageUserProjectionSyncStateStoreResult::
                Unchanged:
            return "unchanged";

        case StorageUserProjectionSyncStateStoreResult::
                Stale:
            return "stale";

        case StorageUserProjectionSyncStateStoreResult::
                Conflict:
            return "conflict";

        case StorageUserProjectionSyncStateStoreResult::
                InvalidInput:
            return "invalid_input";

        case StorageUserProjectionSyncStateStoreResult::
                PersistFailed:
            return "persist_failed";
    }

    return "unknown";
}

bool StorageUserProjectionSyncStateStore::load(
    const std::string& path,
    std::string* error
) {
    if (error) {
        error->clear();
    }

    std::filesystem::path normalized_path;

    if (!normalize_store_path(
            path,
            &normalized_path,
            error)) {
        return false;
    }

    std::unordered_map<
        std::string,
        StorageUserProjectionSyncState
    > parsed;

    bool missing = false;

    if (!read_store_file(
            normalized_path,
            &parsed,
            &missing,
            error)) {
        return false;
    }

    (void)missing;

    std::lock_guard<std::mutex> lock(
        mutex_
    );

    path_ =
        std::move(normalized_path);

    states_ =
        std::move(parsed);

    durability_uncertain_ = false;

    return true;
}

std::optional<StorageUserProjectionSyncState>
StorageUserProjectionSyncStateStore::get(
    const std::string& fingerprint
) const {
    if (!pqnas::valid_storage_user_fingerprint_hex(
            fingerprint)) {
        return std::nullopt;
    }

    std::lock_guard<std::mutex> lock(
        mutex_
    );

    const auto iterator =
        states_.find(
            fingerprint
        );

    if (iterator ==
        states_.end()) {
        return std::nullopt;
    }

    return iterator->second;
}

std::vector<StorageUserProjectionSyncState>
StorageUserProjectionSyncStateStore::snapshot() const {
    std::lock_guard<std::mutex> lock(
        mutex_
    );

    std::vector<
        StorageUserProjectionSyncState
    > result;

    result.reserve(
        states_.size()
    );

    for (const auto& entry :
         states_) {
        result.push_back(
            entry.second
        );
    }

    std::sort(
        result.begin(),
        result.end(),
        [](
            const StorageUserProjectionSyncState& left,
            const StorageUserProjectionSyncState& right
        ) {
            return
                left.fingerprint <
                right.fingerprint;
        }
    );

    return result;
}

bool StorageUserProjectionSyncStateStore::
commit_candidate_locked(
    std::unordered_map<
        std::string,
        StorageUserProjectionSyncState
    >&& candidate,
    std::string* error
) {
    if (path_.empty()) {
        set_error(
            error,
            "projection sync-state store is not loaded"
        );

        return false;
    }

    bool committed = false;

    const bool durable =
        persist_store_file(
            path_,
            candidate,
            &committed,
            error
        );

    // Security/correctness: if rename committed the new snapshot, process
    // memory follows the visible inode even when parent-directory fsync fails.
    //
    // A failure before rename must preserve any earlier uncertainty.
    if (committed) {
        states_ =
            std::move(candidate);

        durability_uncertain_ =
            !durable;
    }

    return durable;
}

bool StorageUserProjectionSyncStateStore::
ensure_durable(
    std::string* error
) {
    if (error) {
        error->clear();
    }

    std::lock_guard<std::mutex> lock(
        mutex_
    );

    if (path_.empty()) {
        set_error(
            error,
            "projection sync-state store is not loaded"
        );

        return false;
    }

    if (!durability_uncertain_) {
        return true;
    }

    // Security/correctness: persist the exact current snapshot. This also
    // heals an erase whose rename committed but whose parent-directory fsync
    // failed after the erased entry disappeared from process memory.
    auto candidate =
        states_;

    return commit_candidate_locked(
        std::move(candidate),
        error
    );
}


StorageUserProjectionSyncStateStoreResult
StorageUserProjectionSyncStateStore::mark_synced(
    const StorageUserProjectionSyncState& state,
    std::string* error
) {
    if (error) {
        error->clear();
    }

    if (!valid_state(state)) {
        return semantic_failure(
            StorageUserProjectionSyncStateStoreResult::
                InvalidInput,
            error
        );
    }

    std::lock_guard<std::mutex> lock(
        mutex_
    );

    if (path_.empty()) {
        return semantic_failure(
            StorageUserProjectionSyncStateStoreResult::
                PersistFailed,
            error
        );
    }

    const auto iterator =
        states_.find(
            state.fingerprint
        );

    if (iterator !=
        states_.end()) {
        const StorageUserProjectionSyncState&
            existing =
                iterator->second;

        if (state.revision <
            existing.revision) {
            return semantic_failure(
                StorageUserProjectionSyncStateStoreResult::
                    Stale,
                error
            );
        }

        if (state.revision ==
            existing.revision) {
            if (!state_equal(
                    state,
                    existing)) {
                return semantic_failure(
                    StorageUserProjectionSyncStateStoreResult::
                        Conflict,
                    error
                );
            }

            if (!durability_uncertain_) {
                return
                    StorageUserProjectionSyncStateStoreResult::
                        Unchanged;
            }

            // A prior rename may have succeeded while directory fsync failed.
            // Exact retry re-persists the complete current snapshot.
            auto retry_candidate =
                states_;

            if (!commit_candidate_locked(
                    std::move(
                        retry_candidate
                    ),
                    error)) {
                return
                    StorageUserProjectionSyncStateStoreResult::
                        PersistFailed;
            }

            return
                StorageUserProjectionSyncStateStoreResult::
                    Unchanged;
        }
    }

    auto candidate =
        states_;

    candidate[state.fingerprint] =
        state;

    if (!commit_candidate_locked(
            std::move(candidate),
            error)) {
        return
            StorageUserProjectionSyncStateStoreResult::
                PersistFailed;
    }

    return
        StorageUserProjectionSyncStateStoreResult::
            Applied;
}

bool StorageUserProjectionSyncStateStore::
erase_if_current(
    const std::string& fingerprint,
    const std::string& expected_home_node,
    std::uint64_t expected_revision,
    bool* expectation_matched,
    std::string* error
) {
    if (expectation_matched) {
        *expectation_matched = false;
    }

    if (error) {
        error->clear();
    }

    if (!pqnas::valid_storage_user_fingerprint_hex(
            fingerprint) ||
        !valid_cluster_node_id(
            expected_home_node) ||
        expected_revision == 0) {
        set_error(
            error,
            "invalid projection sync-state erase input"
        );

        return false;
    }

    std::lock_guard<std::mutex> lock(
        mutex_
    );

    if (path_.empty()) {
        set_error(
            error,
            "projection sync-state store is not loaded"
        );

        return false;
    }

    const auto iterator =
        states_.find(
            fingerprint
        );

    if (iterator ==
        states_.end()) {
        // If a previous erase rename succeeded but directory fsync failed,
        // retry the now-current empty/updated snapshot for durability.
        if (durability_uncertain_) {
            auto retry_candidate =
                states_;

            if (!commit_candidate_locked(
                    std::move(
                        retry_candidate
                    ),
                    error)) {
                return false;
            }
        }

        return true;
    }

    if (iterator->second.home_node !=
            expected_home_node ||
        iterator->second.revision !=
            expected_revision) {
        return true;
    }

    if (expectation_matched) {
        *expectation_matched = true;
    }

    auto candidate =
        states_;

    candidate.erase(
        fingerprint
    );

    if (!commit_candidate_locked(
            std::move(candidate),
            error)) {
        return false;
    }

    return true;
}

} // namespace pqnas::cluster
