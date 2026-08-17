#include "cluster/storage_user_projection_store.h"

#include "cluster/cluster_identity_validation.h"
#include "storage_metadata_validation.h"
#include "storage_path_validation.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <fcntl.h>
#include <initializer_list>
#include <limits>
#include <nlohmann/json.hpp>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace pqnas::cluster {
namespace {

using json = nlohmann::json;

std::atomic<std::uint64_t> g_temp_counter{0};

class UniqueFd {
public:
    UniqueFd() = default;

    explicit UniqueFd(int fd)
        : fd_(fd) {
    }

    ~UniqueFd() {
        if (fd_ >= 0) {
            (void)::close(fd_);
        }
    }

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    UniqueFd(UniqueFd&& other) noexcept
        : fd_(other.release()) {
    }

    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            if (fd_ >= 0) {
                (void)::close(fd_);
            }

            fd_ = other.release();
        }

        return *this;
    }

    int get() const {
        return fd_;
    }

    int release() {
        const int result = fd_;
        fd_ = -1;
        return result;
    }

private:
    int fd_ = -1;
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
    const json& object,
    std::initializer_list<const char*> allowed
) {
    if (!object.is_object()) {
        return false;
    }

    for (auto iterator = object.begin();
         iterator != object.end();
         ++iterator) {
        bool found = false;

        for (const char* key : allowed) {
            if (iterator.key() == key) {
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

bool json_string(
    const json& object,
    const char* key,
    std::string* output
) {
    if (!output) {
        return false;
    }

    const auto iterator = object.find(key);

    if (iterator == object.end() ||
        !iterator->is_string()) {
        return false;
    }

    *output = iterator->get<std::string>();
    return true;
}

bool json_bool(
    const json& object,
    const char* key,
    bool* output
) {
    if (!output) {
        return false;
    }

    const auto iterator = object.find(key);

    if (iterator == object.end() ||
        !iterator->is_boolean()) {
        return false;
    }

    *output = iterator->get<bool>();
    return true;
}

bool json_int(
    const json& object,
    const char* key,
    int minimum,
    int maximum,
    int* output
) {
    if (!output) {
        return false;
    }

    const auto iterator = object.find(key);

    if (iterator == object.end() ||
        !iterator->is_number_integer()) {
        return false;
    }

    const std::int64_t value =
        iterator->get<std::int64_t>();

    if (value < minimum ||
        value > maximum) {
        return false;
    }

    *output = static_cast<int>(value);
    return true;
}

bool json_u64(
    const json& object,
    const char* key,
    std::uint64_t* output
) {
    if (!output) {
        return false;
    }

    const auto iterator = object.find(key);

    if (iterator == object.end()) {
        return false;
    }

    try {
        if (iterator->is_number_unsigned()) {
            *output =
                iterator->get<std::uint64_t>();
            return true;
        }

        if (iterator->is_number_integer()) {
            const std::int64_t value =
                iterator->get<std::int64_t>();

            if (value < 0) {
                return false;
            }

            *output =
                static_cast<std::uint64_t>(
                    value
                );

            return true;
        }
    } catch (...) {
        return false;
    }

    return false;
}

bool normalize_store_path(
    const std::string& raw,
    std::filesystem::path* normalized,
    std::string* error
) {
    if (!normalized) {
        set_error(
            error,
            "missing storage projection path output"
        );
        return false;
    }

    if (raw.empty() || raw.size() > 4096) {
        set_error(
            error,
            "invalid storage projection path"
        );
        return false;
    }

    const std::filesystem::path path(raw);

    if (!path.is_absolute() ||
        !path.has_root_path()) {
        set_error(
            error,
            "storage projection path must be absolute"
        );
        return false;
    }

    for (const auto& component : path) {
        const std::string part =
            component.string();

        if (part == "." || part == "..") {
            set_error(
                error,
                "storage projection path contains traversal"
            );
            return false;
        }
    }

    const std::filesystem::path clean =
        path.lexically_normal();

    if (clean.empty() ||
        clean == clean.root_path() ||
        clean.filename().empty()) {
        set_error(
            error,
            "storage projection path must name a file"
        );
        return false;
    }

    *normalized = clean;
    return true;
}

bool projection_equal(
    const StorageUserProjection& left,
    const StorageUserProjection& right
) {
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

bool validate_projection(
    const StorageUserProjection& projection,
    std::string* error
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
        set_error(
            error,
            "invalid storage user projection metadata"
        );
        return false;
    }

    if (!projection.storage_allocated) {
        // Security: deallocation projections deliberately carry no stale
        // physical mapping or quota metadata to Storage Nodes.
        if (projection.quota_bytes != 0 ||
            !projection.root_rel.empty() ||
            !projection.storage_pool_id.empty()) {
            set_error(
                error,
                "invalid unallocated storage projection"
            );
            return false;
        }

        return true;
    }

    if (projection.root_rel.empty()) {
        set_error(
            error,
            "allocated storage projection has no root"
        );
        return false;
    }

    std::string normalized_root;
    std::string path_error;

    if (!pqnas::normalize_storage_relative_path_strict(
            projection.root_rel,
            &normalized_root,
            &path_error) ||
        normalized_root != projection.root_rel) {
        // Security: control-plane paths must already be canonical. Never
        // silently reinterpret Director metadata into another storage path.
        set_error(
            error,
            "invalid storage projection root"
        );
        return false;
    }

    return true;
}

bool validate_projection_set(
    const std::unordered_map<
        std::string,
        StorageUserProjection
    >& projections,
    std::string* error
) {
    if (projections.size() >
        kStorageUserProjectionStoreMaxEntries) {
        set_error(
            error,
            "too many storage user projections"
        );
        return false;
    }

    for (const auto& entry : projections) {
        if (entry.first !=
                entry.second.fingerprint ||
            !validate_projection(
                entry.second,
                error)) {
            if (error && error->empty()) {
                *error =
                    "invalid storage projection entry";
            }

            return false;
        }
    }

    return true;
}

json projection_to_json(
    const StorageUserProjection& projection
) {
    return json{
        {"fingerprint", projection.fingerprint},
        {"enabled", projection.enabled},
        {
            "storage_allocated",
            projection.storage_allocated
        },
        {"quota_bytes", projection.quota_bytes},
        {"root_rel", projection.root_rel},
        {
            "storage_pool_id",
            projection.storage_pool_id
        },
        {"home_node", projection.home_node},
        {"revision", projection.revision}
    };
}

bool parse_projection(
    const json& value,
    StorageUserProjection* output,
    std::string* error
) {
    if (!output ||
        !has_only_keys(
            value,
            {
                "fingerprint",
                "enabled",
                "storage_allocated",
                "quota_bytes",
                "root_rel",
                "storage_pool_id",
                "home_node",
                "revision"
            })) {
        set_error(
            error,
            "invalid storage projection entry"
        );
        return false;
    }

    StorageUserProjection projection;

    if (!json_string(
            value,
            "fingerprint",
            &projection.fingerprint) ||
        !json_bool(
            value,
            "enabled",
            &projection.enabled) ||
        !json_bool(
            value,
            "storage_allocated",
            &projection.storage_allocated) ||
        !json_u64(
            value,
            "quota_bytes",
            &projection.quota_bytes) ||
        !json_string(
            value,
            "root_rel",
            &projection.root_rel) ||
        !json_string(
            value,
            "storage_pool_id",
            &projection.storage_pool_id) ||
        !json_string(
            value,
            "home_node",
            &projection.home_node) ||
        !json_u64(
            value,
            "revision",
            &projection.revision) ||
        !validate_projection(
            projection,
            error)) {
        if (error && error->empty()) {
            *error =
                "invalid storage projection entry";
        }

        return false;
    }

    *output = std::move(projection);
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
            "failed to inspect storage projection directory"
        );
        return false;
    }

    const uid_t effective_uid =
        ::geteuid();

    // Security: trusted authorization metadata must not live in an
    // attacker-writable directory where target/temp files can be swapped.
    if (!S_ISDIR(stats.st_mode) ||
        (stats.st_mode & 0022) != 0 ||
        (stats.st_uid != effective_uid &&
         stats.st_uid != 0)) {
        set_error(
            error,
            "unsafe storage projection directory"
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
            "failed to inspect storage projection target"
        );
        return false;
    }

    const uid_t effective_uid =
        ::geteuid();

    // Security: reject symlinks, hardlinks, devices and files readable or
    // writable by group/other users before replacing authorization state.
    if (!S_ISREG(stats.st_mode) ||
        stats.st_nlink != 1 ||
        (stats.st_mode & 0077) != 0 ||
        (stats.st_uid != effective_uid &&
         stats.st_uid != 0)) {
        set_error(
            error,
            "unsafe storage projection target"
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
            "missing storage projection buffer"
        );
        return false;
    }

    output->clear();

    std::array<char, 8192> buffer{};

    while (true) {
        const ssize_t count =
            ::read(
                descriptor,
                buffer.data(),
                buffer.size()
            );

        if (count > 0) {
            const std::size_t bytes =
                static_cast<std::size_t>(
                    count
                );

            if (output->size() >
                kStorageUserProjectionStoreMaxBytes ||
                bytes >
                    kStorageUserProjectionStoreMaxBytes -
                        output->size()) {
                set_error(
                    error,
                    "storage projection store is too large"
                );
                return false;
            }

            output->append(
                buffer.data(),
                bytes
            );

            continue;
        }

        if (count == 0) {
            return true;
        }

        if (errno == EINTR) {
            continue;
        }

        set_error(
            error,
            "failed to read storage projection store"
        );
        return false;
    }
}

bool read_store_file(
    const std::filesystem::path& path,
    std::unordered_map<
        std::string,
        StorageUserProjection
    >* output,
    bool* missing,
    std::string* error
) {
    if (!output || !missing) {
        set_error(
            error,
            "missing storage projection load output"
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
            "failed to open storage projection directory"
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
            "failed to open storage projection store"
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
            "failed to inspect storage projection store"
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
            "unsafe storage projection file"
        );
        return false;
    }

    if (stats.st_size < 0 ||
        static_cast<unsigned long long>(
            stats.st_size) >
        static_cast<unsigned long long>(
            kStorageUserProjectionStoreMaxBytes)) {
        set_error(
            error,
            "storage projection file has invalid size"
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
        root = json::parse(contents);
    } catch (...) {
        set_error(
            error,
            "invalid storage projection JSON"
        );
        return false;
    }

    if (!has_only_keys(
            root,
            {"version", "projections"})) {
        set_error(
            error,
            "invalid storage projection root"
        );
        return false;
    }

    int version = 0;

    if (!json_int(
            root,
            "version",
            kStorageUserProjectionStoreSchemaVersion,
            kStorageUserProjectionStoreSchemaVersion,
            &version)) {
        set_error(
            error,
            "unsupported storage projection version"
        );
        return false;
    }

    (void)version;

    const auto projections_iterator =
        root.find("projections");

    if (projections_iterator == root.end() ||
        !projections_iterator->is_array() ||
        projections_iterator->size() >
            kStorageUserProjectionStoreMaxEntries) {
        set_error(
            error,
            "invalid storage projection array"
        );
        return false;
    }

    std::unordered_map<
        std::string,
        StorageUserProjection
    > parsed;

    parsed.reserve(
        projections_iterator->size()
    );

    for (const json& value :
         *projections_iterator) {
        StorageUserProjection projection;

        if (!parse_projection(
                value,
                &projection,
                error)) {
            return false;
        }

        const std::string fingerprint =
            projection.fingerprint;

        if (!parsed.emplace(
                fingerprint,
                std::move(projection))
                .second) {
            set_error(
                error,
                "duplicate storage projection fingerprint"
            );
            return false;
        }
    }

    if (!validate_projection_set(
            parsed,
            error)) {
        return false;
    }

    *output = std::move(parsed);
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

        if (count < 0 && errno == EINTR) {
            continue;
        }

        set_error(
            error,
            "failed to write storage projection store"
        );
        return false;
    }

    return true;
}

bool persist_store_file(
    const std::filesystem::path& path,
    const std::unordered_map<
        std::string,
        StorageUserProjection
    >& projections,
    bool* committed,
    std::string* error
) {
    if (!committed) {
        set_error(
            error,
            "missing storage projection commit output"
        );
        return false;
    }

    *committed = false;

    if (!validate_projection_set(
            projections,
            error)) {
        return false;
    }

    std::vector<std::string> fingerprints;

    fingerprints.reserve(
        projections.size()
    );

    for (const auto& entry : projections) {
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
            kStorageUserProjectionStoreSchemaVersion
        },
        {
            "projections",
            json::array()
        }
    };

    for (const std::string& fingerprint :
         fingerprints) {
        root["projections"].push_back(
            projection_to_json(
                projections.at(fingerprint)
            )
        );
    }

    std::string contents;

    try {
        contents = root.dump(2);
        contents.push_back('\n');
    } catch (...) {
        set_error(
            error,
            "failed to serialize storage projection store"
        );
        return false;
    }

    if (contents.size() >
        kStorageUserProjectionStoreMaxBytes) {
        set_error(
            error,
            "serialized storage projection store is too large"
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
            "failed to create storage projection directory"
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
            "failed to open storage projection directory"
        );
        return false;
    }

    UniqueFd directory_descriptor(
        raw_directory_descriptor
    );

    if (created_directory) {
        struct stat created_stats {};

        // Security: only tighten permissions on the inode that this process
        // just created and owns; never chmod an attacker-substituted directory.
        if (::fstat(
                directory_descriptor.get(),
                &created_stats) != 0 ||
            !S_ISDIR(created_stats.st_mode) ||
            created_stats.st_uid != ::geteuid() ||
            ::fchmod(
                directory_descriptor.get(),
                0700) != 0) {
            set_error(
                error,
                "failed to secure storage projection directory"
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
                "failed to create storage projection temp file"
            );
            return false;
        }
    }

    if (temporary_descriptor.get() < 0) {
        set_error(
            error,
            "failed to allocate storage projection temp file"
        );
        return false;
    }

    const auto remove_temporary = [&]() {
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
        if (error && error->empty()) {
            *error =
                "failed to sync storage projection store";
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
            "failed to close storage projection temp file"
        );
        remove_temporary();
        return false;
    }

    // Security: re-check immediately before commit so a target substituted
    // during serialization cannot bypass inode/type/permission validation.
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
            "failed to commit storage projection store"
        );
        remove_temporary();
        return false;
    }

    // rename() has made the new inode logically visible. Report that before
    // directory fsync so process memory never remains on the old authorization
    // snapshot after an uncertain durability result.
    *committed = true;

    if (::fsync(
            directory_descriptor.get()) != 0) {
        set_error(
            error,
            "failed to sync storage projection directory"
        );
        return false;
    }

    return true;
}

StorageUserProjectionStoreResult semantic_failure(
    StorageUserProjectionStoreResult result,
    std::string* error
) {
    set_error(
        error,
        storage_user_projection_store_result_code(
            result
        )
    );

    return result;
}

} // namespace

const char* storage_user_projection_store_result_code(
    StorageUserProjectionStoreResult result
) {
    switch (result) {
        case StorageUserProjectionStoreResult::Applied:
            return "storage_projection_applied";

        case StorageUserProjectionStoreResult::Unchanged:
            return "storage_projection_unchanged";

        case StorageUserProjectionStoreResult::Stale:
            return "storage_projection_stale";

        case StorageUserProjectionStoreResult::Conflict:
            return "storage_projection_conflict";

        case StorageUserProjectionStoreResult::InvalidInput:
            return "storage_projection_invalid_input";

        case StorageUserProjectionStoreResult::PersistFailed:
            return "storage_projection_persist_failed";
    }

    return "storage_projection_invalid";
}

bool StorageUserProjectionStore::load(
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

    std::lock_guard<std::mutex> lock(
        mutex_
    );

    std::unordered_map<
        std::string,
        StorageUserProjection
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

    path_ = std::move(normalized_path);
    projections_ = std::move(parsed);
    durability_uncertain_ = false;

    return true;
}

std::optional<StorageUserProjection>
StorageUserProjectionStore::get(
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
        projections_.find(fingerprint);

    if (iterator == projections_.end()) {
        return std::nullopt;
    }

    return iterator->second;
}

std::vector<StorageUserProjection>
StorageUserProjectionStore::snapshot() const {
    std::lock_guard<std::mutex> lock(
        mutex_
    );

    std::vector<StorageUserProjection>
        result;

    result.reserve(
        projections_.size()
    );

    for (const auto& entry : projections_) {
        result.push_back(
            entry.second
        );
    }

    std::sort(
        result.begin(),
        result.end(),
        [](
            const StorageUserProjection& left,
            const StorageUserProjection& right
        ) {
            return left.fingerprint <
                right.fingerprint;
        }
    );

    return result;
}

bool StorageUserProjectionStore::
commit_candidate_locked(
    std::unordered_map<
        std::string,
        StorageUserProjection
    >&& candidate,
    std::string* error
) {
    if (path_.empty()) {
        set_error(
            error,
            "storage projection store is not loaded"
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

    // Security: rename may already have replaced authorization state even when
    // parent-directory fsync fails. Keep memory aligned with the visible inode.
    //
    // A failure before rename must NOT clear an earlier durability uncertainty:
    // the previously visible inode may still need an identical retry so its
    // parent-directory fsync can be re-attempted.
    if (committed) {
        projections_ =
            std::move(candidate);

        durability_uncertain_ =
            !durable;
    }

    return durable;
}

StorageUserProjectionStoreResult
StorageUserProjectionStore::apply(
    const StorageUserProjection& projection,
    std::string* error
) {
    if (error) {
        error->clear();
    }

    std::string validation_error;

    if (!validate_projection(
            projection,
            &validation_error)) {
        return semantic_failure(
            StorageUserProjectionStoreResult::
                InvalidInput,
            error
        );
    }

    std::lock_guard<std::mutex> lock(
        mutex_
    );

    if (path_.empty()) {
        return semantic_failure(
            StorageUserProjectionStoreResult::
                PersistFailed,
            error
        );
    }

    const auto iterator =
        projections_.find(
            projection.fingerprint
        );

    if (iterator != projections_.end()) {
        const StorageUserProjection& existing =
            iterator->second;

        if (projection.revision <
            existing.revision) {
            return semantic_failure(
                StorageUserProjectionStoreResult::
                    Stale,
                error
            );
        }

        if (projection.revision ==
            existing.revision) {
            if (!projection_equal(
                    projection,
                    existing)) {
                return semantic_failure(
                    StorageUserProjectionStoreResult::
                        Conflict,
                    error
                );
            }

            if (!durability_uncertain_) {
                return StorageUserProjectionStoreResult::
                    Unchanged;
            }

            // Security/correctness: a previous rename may have succeeded while
            // directory fsync failed. An identical retry re-attempts durable
            // persistence instead of falsely claiming a durable no-op.
            auto retry_candidate =
                projections_;

            if (!commit_candidate_locked(
                    std::move(retry_candidate),
                    error)) {
                return StorageUserProjectionStoreResult::
                    PersistFailed;
            }

            return StorageUserProjectionStoreResult::
                Unchanged;
        }
    }

    auto candidate =
        projections_;

    candidate[projection.fingerprint] =
        projection;

    if (!commit_candidate_locked(
            std::move(candidate),
            error)) {
        return StorageUserProjectionStoreResult::
            PersistFailed;
    }

    return StorageUserProjectionStoreResult::
        Applied;
}

} // namespace pqnas::cluster
