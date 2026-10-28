#include "user_placement_policy_registry.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pqnas::cluster {
namespace {

using json = nlohmann::json;

constexpr std::size_t
    kMaxPolicyRegistryBytes =
        4 * 1024 * 1024;

constexpr std::size_t
    kMaxPolicyCount = 10000;

std::atomic<std::uint64_t>
    g_policy_temp_counter{0};

class UniqueFd {
public:
    explicit UniqueFd(
        int descriptor = -1
    ) noexcept
        : descriptor_(descriptor) {}

    ~UniqueFd() {
        if (descriptor_ >= 0) {
            ::close(descriptor_);
        }
    }

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) =
        delete;

    UniqueFd(UniqueFd&& other) noexcept
        : descriptor_(other.release()) {}

    UniqueFd& operator=(
        UniqueFd&& other
    ) noexcept {
        if (this != &other) {
            // Security: close the old descriptor before taking ownership so
            // error paths cannot accumulate descriptor leaks.
            if (descriptor_ >= 0) {
                ::close(descriptor_);
            }

            descriptor_ = other.release();
        }

        return *this;
    }

    int get() const noexcept {
        return descriptor_;
    }

    int release() noexcept {
        const int result = descriptor_;
        descriptor_ = -1;
        return result;
    }

private:
    int descriptor_;
};

void set_error(
    std::string* error,
    const std::string& message
) {
    if (error) {
        *error = message;
    }
}

bool valid_stable_identifier(
    const std::string& value
) {
    if (value.empty() ||
        value.size() > 64) {
        return false;
    }

    for (std::size_t index = 0;
         index < value.size();
         ++index) {
        const unsigned char character =
            static_cast<unsigned char>(
                value[index]
            );

        const bool alphanumeric =
            (character >= 'A' &&
             character <= 'Z') ||
            (character >= 'a' &&
             character <= 'z') ||
            (character >= '0' &&
             character <= '9');

        if (index == 0) {
            if (!alphanumeric) {
                return false;
            }

            continue;
        }

        if (!alphanumeric &&
            character != '-' &&
            character != '_' &&
            character != '.') {
            return false;
        }
    }

    return true;
}

bool normalize_registry_path(
    const std::string& raw,
    std::filesystem::path* normalized,
    std::string* error
) {
    if (!normalized) {
        set_error(
            error,
            "missing policy registry path output"
        );
        return false;
    }

    if (raw.empty() ||
        raw.size() > 4096) {
        set_error(
            error,
            "invalid policy registry path"
        );
        return false;
    }

    const std::filesystem::path path(raw);

    if (!path.is_absolute() ||
        !path.has_root_path()) {
        set_error(
            error,
            "policy registry path must be absolute"
        );
        return false;
    }

    for (const auto& component : path) {
        const std::string part =
            component.string();

        if (part == "." ||
            part == "..") {
            set_error(
                error,
                "policy registry path contains traversal"
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
            "policy registry path must name a file"
        );
        return false;
    }

    *normalized = clean;
    return true;
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
        bool accepted = false;

        for (const char* key : allowed) {
            if (iterator.key() == key) {
                accepted = true;
                break;
            }
        }

        if (!accepted) {
            return false;
        }
    }

    return true;
}

bool parse_mode(
    const std::string& value,
    UserPlacementPolicyMode* mode
) {
    if (!mode) {
        return false;
    }

    if (value == "automatic") {
        *mode =
            UserPlacementPolicyMode::Automatic;
        return true;
    }

    if (value == "pinned") {
        *mode =
            UserPlacementPolicyMode::Pinned;
        return true;
    }

    if (value == "allowed_nodes") {
        *mode =
            UserPlacementPolicyMode::AllowedNodes;
        return true;
    }

    return false;
}

bool parse_fallback(
    const std::string& value,
    UserPlacementPolicyFallback* fallback
) {
    if (!fallback) {
        return false;
    }

    if (value == "deny") {
        *fallback =
            UserPlacementPolicyFallback::Deny;
        return true;
    }

    if (value == "automatic") {
        *fallback =
            UserPlacementPolicyFallback::Automatic;
        return true;
    }

    return false;
}

UserPlacementPolicy canonical_policy(
    UserPlacementPolicy policy
) {
    std::sort(
        policy.node_ids.begin(),
        policy.node_ids.end()
    );

    return policy;
}

bool parse_policy(
    const json& value,
    UserPlacementPolicy* output,
    std::string* error
) {
    if (!output ||
        !has_only_keys(
            value,
            {
                "policy_id",
                "mode",
                "fallback",
                "node_ids"
            })) {
        set_error(
            error,
            "invalid placement policy object"
        );
        return false;
    }

    if (!value.contains("policy_id") ||
        !value["policy_id"].is_string() ||
        !value.contains("mode") ||
        !value["mode"].is_string() ||
        !value.contains("fallback") ||
        !value["fallback"].is_string() ||
        !value.contains("node_ids") ||
        !value["node_ids"].is_array()) {
        set_error(
            error,
            "invalid placement policy fields"
        );
        return false;
    }

    UserPlacementPolicy policy;
    policy.policy_id =
        value["policy_id"].get<std::string>();

    if (!parse_mode(
            value["mode"].get<std::string>(),
            &policy.mode)) {
        set_error(
            error,
            "invalid placement policy mode"
        );
        return false;
    }

    if (!parse_fallback(
            value["fallback"].get<std::string>(),
            &policy.fallback)) {
        set_error(
            error,
            "invalid placement policy fallback"
        );
        return false;
    }

    if (value["node_ids"].size() >
        kUserPlacementPolicyMaxNodeCount) {
        set_error(
            error,
            "too many placement policy nodes"
        );
        return false;
    }

    policy.node_ids.reserve(
        value["node_ids"].size()
    );

    for (const json& node_id :
         value["node_ids"]) {
        if (!node_id.is_string()) {
            set_error(
                error,
                "invalid placement policy node ID"
            );
            return false;
        }

        policy.node_ids.push_back(
            node_id.get<std::string>()
        );
    }

    std::string validation_error;

    if (!validate_user_placement_policy(
            policy,
            &validation_error)) {
        set_error(
            error,
            "invalid placement policy: " +
                validation_error
        );
        return false;
    }

    *output =
        canonical_policy(
            std::move(policy)
        );

    return true;
}

bool read_all_bounded(
    int descriptor,
    std::string* output,
    std::string* error
) {
    if (!output) {
        set_error(
            error,
            "missing policy registry read output"
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

        if (count == 0) {
            return true;
        }

        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }

            set_error(
                error,
                "failed to read policy registry"
            );
            return false;
        }

        if (output->size() +
                static_cast<std::size_t>(count) >
            kMaxPolicyRegistryBytes) {
            set_error(
                error,
                "policy registry is too large"
            );
            return false;
        }

        output->append(
            buffer,
            static_cast<std::size_t>(count)
        );
    }
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

        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }

            set_error(
                error,
                "failed to write policy registry"
            );
            return false;
        }

        if (count == 0) {
            set_error(
                error,
                "short policy registry write"
            );
            return false;
        }

        offset +=
            static_cast<std::size_t>(count);
    }

    return true;
}

bool load_registry_file(
    const std::filesystem::path& path,
    std::unordered_map<
        std::string,
        UserPlacementPolicy
    >* parsed,
    bool* missing,
    std::string* error
) {
    if (!parsed || !missing) {
        set_error(
            error,
            "missing policy registry load output"
        );
        return false;
    }

    parsed->clear();
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
            *missing = true;
            return true;
        }

        set_error(
            error,
            "failed to open policy registry directory"
        );
        return false;
    }

    UniqueFd directory_descriptor(
        raw_directory_descriptor
    );

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
            *missing = true;
            return true;
        }

        set_error(
            error,
            "failed to open policy registry"
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
            "failed to inspect policy registry"
        );
        return false;
    }

    // Security: trusted control-plane policy must be a private, singly linked
    // regular file owned by root or the running service account.
    if (!S_ISREG(stats.st_mode) ||
        stats.st_nlink != 1 ||
        (stats.st_mode & 0077) != 0) {
        set_error(
            error,
            "unsafe policy registry file"
        );
        return false;
    }

    const uid_t effective_uid =
        ::geteuid();

    if (stats.st_uid != effective_uid &&
        stats.st_uid != 0) {
        set_error(
            error,
            "unsafe policy registry ownership"
        );
        return false;
    }

    if (stats.st_size < 0 ||
        static_cast<unsigned long long>(
            stats.st_size
        ) >
        static_cast<unsigned long long>(
            kMaxPolicyRegistryBytes
        )) {
        set_error(
            error,
            "policy registry is too large"
        );
        return false;
    }

    std::string contents;

    if (!read_all_bounded(
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
            "invalid policy registry JSON"
        );
        return false;
    }

    if (!has_only_keys(
            root,
            {
                "version",
                "policies"
            }) ||
        !root.contains("version") ||
        root["version"] !=
            kUserPlacementPolicyRegistrySchemaVersion ||
        !root.contains("policies") ||
        !root["policies"].is_array()) {
        set_error(
            error,
            "invalid policy registry schema"
        );
        return false;
    }

    if (root["policies"].size() >
        kMaxPolicyCount) {
        set_error(
            error,
            "too many placement policies"
        );
        return false;
    }

    for (const json& value :
         root["policies"]) {
        UserPlacementPolicy policy;

        if (!parse_policy(
                value,
                &policy,
                error)) {
            return false;
        }

        if (!parsed->emplace(
                policy.policy_id,
                std::move(policy)
            ).second) {
            set_error(
                error,
                "duplicate placement policy ID"
            );
            return false;
        }
    }

    return true;
}

bool persist_registry_file(
    const std::filesystem::path& path,
    const std::unordered_map<
        std::string,
        UserPlacementPolicy
    >& policies,
    bool* committed,
    std::string* error
) {
    if (!committed) {
        set_error(
            error,
            "missing policy registry commit output"
        );
        return false;
    }

    *committed = false;
    if (policies.size() >
        kMaxPolicyCount) {
        set_error(
            error,
            "too many placement policies"
        );
        return false;
    }

    std::vector<std::string> policy_ids;
    policy_ids.reserve(policies.size());

    for (const auto& entry : policies) {
        policy_ids.push_back(
            entry.first
        );
    }

    std::sort(
        policy_ids.begin(),
        policy_ids.end()
    );

    json root;
    root["version"] =
        kUserPlacementPolicyRegistrySchemaVersion;

    root["policies"] =
        json::array();

    for (const std::string& policy_id :
         policy_ids) {
        const auto iterator =
            policies.find(policy_id);

        if (iterator == policies.end()) {
            set_error(
                error,
                "placement policy snapshot changed"
            );
            return false;
        }

        UserPlacementPolicy policy =
            canonical_policy(
                iterator->second
            );

        std::string validation_error;

        if (policy.policy_id != policy_id ||
            !validate_user_placement_policy(
                policy,
                &validation_error)) {
            set_error(
                error,
                "invalid placement policy snapshot"
            );
            return false;
        }

        root["policies"].push_back({
            {"policy_id", policy.policy_id},
            {
                "mode",
                user_placement_policy_mode_name(
                    policy.mode
                )
            },
            {
                "fallback",
                user_placement_policy_fallback_name(
                    policy.fallback
                )
            },
            {"node_ids", policy.node_ids}
        });
    }

    std::string contents;

    try {
        contents = root.dump(2);
        contents.push_back('\n');
    } catch (...) {
        set_error(
            error,
            "failed to serialize policy registry"
        );
        return false;
    }

    if (contents.size() >
        kMaxPolicyRegistryBytes) {
        set_error(
            error,
            "policy registry is too large"
        );
        return false;
    }

    std::error_code filesystem_error;

    std::filesystem::create_directories(
        path.parent_path(),
        filesystem_error
    );

    if (filesystem_error) {
        set_error(
            error,
            "failed to create policy registry directory"
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
            "failed to open policy registry directory"
        );
        return false;
    }

    UniqueFd directory_descriptor(
        raw_directory_descriptor
    );

    const std::string target_filename =
        path.filename().string();

    std::string temporary_filename;
    UniqueFd temporary_descriptor;

    for (int attempt = 0;
         attempt < 16;
         ++attempt) {
        const std::uint64_t counter =
            g_policy_temp_counter.fetch_add(1);

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
                "failed to create policy registry temp file"
            );
            return false;
        }
    }

    if (temporary_descriptor.get() < 0) {
        set_error(
            error,
            "failed to allocate policy registry temp file"
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
                "failed to sync policy registry";
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
            "failed to close policy registry temp file"
        );

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
            "failed to commit policy registry"
        );

        remove_temporary();
        return false;
    }

    // Security: rename has made the new policy snapshot logically visible.
    // Report that before directory fsync so process memory never rolls back
    // to stale control-plane state after an uncertain durability result.
    *committed = true;

    if (::fsync(
            directory_descriptor.get()) != 0) {
        set_error(
            error,
            "failed to sync policy registry directory"
        );
        return false;
    }

    return true;
}

} // namespace

bool UserPlacementPolicyRegistry::load(
    const std::string& path,
    std::string* error
) {
    if (error) {
        error->clear();
    }

    std::filesystem::path normalized_path;

    if (!normalize_registry_path(
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
        UserPlacementPolicy
    > parsed;

    bool missing = false;

    if (!load_registry_file(
            normalized_path,
            &parsed,
            &missing,
            error)) {
        return false;
    }

    path_ =
        std::move(normalized_path);

    if (missing) {
        policies_.clear();
    } else {
        policies_ =
            std::move(parsed);
    }
    durability_uncertain_ = false;

    return true;
}

std::optional<UserPlacementPolicy>
UserPlacementPolicyRegistry::get(
    const std::string& policy_id
) const {
    if (!valid_stable_identifier(
            policy_id)) {
        return std::nullopt;
    }

    std::lock_guard<std::mutex> lock(
        mutex_
    );

    const auto iterator =
        policies_.find(policy_id);

    if (iterator == policies_.end()) {
        return std::nullopt;
    }

    return iterator->second;
}

std::vector<UserPlacementPolicy>
UserPlacementPolicyRegistry::snapshot() const {
    std::lock_guard<std::mutex> lock(
        mutex_
    );

    std::vector<UserPlacementPolicy> result;
    result.reserve(policies_.size());

    for (const auto& entry : policies_) {
        result.push_back(
            entry.second
        );
    }

    std::sort(
        result.begin(),
        result.end(),
        [](
            const UserPlacementPolicy& left,
            const UserPlacementPolicy& right
        ) {
            return
                left.policy_id <
                right.policy_id;
        }
    );

    return result;
}

bool UserPlacementPolicyRegistry::
persist_candidate_locked(
    std::unordered_map<
        std::string,
        UserPlacementPolicy
    >&& candidate,
    std::string* error
) {
    if (path_.empty()) {
        set_error(
            error,
            "policy registry is not loaded"
        );
        return false;
    }

    bool committed = false;

    const bool durable =
        persist_registry_file(
            path_,
            candidate,
            &committed,
            error
        );

    // Security: rename may already have replaced the authoritative policy
    // snapshot even when the following parent-directory fsync fails.
    // Keep process memory aligned with the visible inode.
    if (committed) {
        policies_ =
            std::move(candidate);

        durability_uncertain_ =
            !durable;
    }

    return durable;
}

bool UserPlacementPolicyRegistry::
ensure_durable_locked(
    std::string* error
) {
    if (!durability_uncertain_) {
        return true;
    }

    // Security/correctness: re-persist the exact currently visible snapshot.
    // This heals a prior rename whose parent-directory fsync failed without
    // resurrecting or rolling back policy state.
    auto candidate =
        policies_;

    return persist_candidate_locked(
        std::move(candidate),
        error
    );
}

bool UserPlacementPolicyRegistry::upsert(
    const UserPlacementPolicy& input,
    std::string* error
) {
    if (error) {
        error->clear();
    }

    std::string validation_error;

    if (!validate_user_placement_policy(
            input,
            &validation_error)) {
        set_error(
            error,
            "invalid placement policy: " +
                validation_error
        );
        return false;
    }

    UserPlacementPolicy policy =
        canonical_policy(input);

    std::lock_guard<std::mutex> lock(
        mutex_
    );

    auto candidate =
        policies_;

    candidate[policy.policy_id] =
        std::move(policy);

    if (!persist_candidate_locked(
            std::move(candidate),
            error)) {
        return false;
    }

    return true;
}

bool UserPlacementPolicyRegistry::erase(
    const std::string& policy_id,
    std::string* error
) {
    if (error) {
        error->clear();
    }

    if (!valid_stable_identifier(
            policy_id)) {
        set_error(
            error,
            "invalid placement policy ID"
        );
        return false;
    }

    std::lock_guard<std::mutex> lock(
        mutex_
    );

    auto candidate =
        policies_;

    if (candidate.erase(policy_id) != 1) {
        if (durability_uncertain_ &&
            !ensure_durable_locked(error)) {
            return false;
        }

        set_error(
            error,
            "placement policy not found"
        );
        return false;
    }

    if (!persist_candidate_locked(
            std::move(candidate),
            error)) {
        return false;
    }

    return true;
}

} // namespace pqnas::cluster
