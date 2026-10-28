#include "cluster/node_registry.h"
#include "cluster/cluster_identity_validation.h"

#include "cluster/node_status.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <set>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pqnas::cluster {
namespace {

using json = nlohmann::json;

constexpr std::size_t kMaxRegistryBytes = 4 * 1024 * 1024;
constexpr std::size_t kMaxNodeCount = 10000;

std::atomic<std::uint64_t> g_temp_counter{0};

class UniqueFd {
public:
    explicit UniqueFd(int fd = -1) noexcept
        : fd_(fd) {}

    ~UniqueFd() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    UniqueFd(UniqueFd&& other) noexcept
        : fd_(other.release()) {}

    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            // Security: close the previously owned descriptor before taking
            // ownership so descriptor leaks cannot accumulate on error paths.
            if (fd_ >= 0) {
                ::close(fd_);
            }

            fd_ = other.release();
        }

        return *this;
    }

    int get() const noexcept {
        return fd_;
    }

    int release() noexcept {
        const int result = fd_;
        fd_ = -1;
        return result;
    }

private:
    int fd_;
};

void set_error(
    std::string* err,
    const std::string& message
) {
    if (err) {
        *err = message;
    }
}

bool valid_metadata_token(
    const std::string& value,
    std::size_t maximum
) {
    if (value.size() > maximum) {
        return false;
    }

    for (unsigned char ch : value) {
        const bool alphanumeric =
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9');

        if (!alphanumeric &&
            ch != '-' &&
            ch != '_' &&
            ch != '.' &&
            ch != '+') {
            return false;
        }
    }

    return true;
}

bool valid_version_string(const std::string& value) {
    if (value.size() > 64) {
        return false;
    }

    for (unsigned char ch : value) {
        if (ch < 0x21 || ch > 0x7e) {
            return false;
        }
    }

    return true;
}

bool valid_iso_utc_seconds(
    const std::string& value
) {
    if (value.size() != 20) {
        return false;
    }

    constexpr std::array<std::size_t, 6>
        separators{4, 7, 10, 13, 16, 19};

    constexpr std::array<char, 6>
        expected{'-', '-', 'T', ':', ':', 'Z'};

    for (std::size_t index = 0;
         index < separators.size();
         ++index) {
        if (value[separators[index]] != expected[index]) {
            return false;
        }
    }

    for (std::size_t index = 0;
         index < value.size();
         ++index) {
        bool separator = false;

        for (std::size_t position : separators) {
            if (index == position) {
                separator = true;
                break;
            }
        }

        if (separator) {
            continue;
        }

        const unsigned char ch =
            static_cast<unsigned char>(value[index]);

        if (ch < '0' || ch > '9') {
            return false;
        }
    }

    const auto decimal = [&value](
        std::size_t offset,
        std::size_t count
    ) -> int {
        int result = 0;

        for (std::size_t index = 0;
             index < count;
             ++index) {
            result =
                result * 10 +
                static_cast<int>(
                    value[offset + index] - '0'
                );
        }

        return result;
    };

    const int year = decimal(0, 4);
    const int month = decimal(5, 2);
    const int day = decimal(8, 2);
    const int hour = decimal(11, 2);
    const int minute = decimal(14, 2);
    const int second = decimal(17, 2);

    return
        year >= 2000 &&
        year <= 9999 &&
        month >= 1 &&
        month <= 12 &&
        day >= 1 &&
        day <= 31 &&
        hour >= 0 &&
        hour <= 23 &&
        minute >= 0 &&
        minute <= 59 &&
        second >= 0 &&
        second <= 60;
}

bool valid_optional_timestamp(
    const std::string& value
) {
    return
        value.empty() ||
        valid_iso_utc_seconds(value);
}

bool normalize_registry_path(
    const std::string& raw,
    std::filesystem::path* normalized,
    std::string* err
) {
    if (!normalized) {
        set_error(err, "missing registry path output");
        return false;
    }

    if (raw.empty() || raw.size() > 4096) {
        set_error(err, "invalid node registry path");
        return false;
    }

    const std::filesystem::path path(raw);

    if (!path.is_absolute() ||
        !path.has_root_path()) {
        set_error(
            err,
            "node registry path must be absolute"
        );
        return false;
    }

    for (const auto& component : path) {
        const std::string part = component.string();

        if (part == "." || part == "..") {
            set_error(
                err,
                "node registry path contains traversal"
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
            err,
            "node registry path must name a file"
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
    std::string* out
) {
    if (!out) {
        return false;
    }

    const auto iterator = object.find(key);

    if (iterator == object.end() ||
        !iterator->is_string()) {
        return false;
    }

    *out = iterator->get<std::string>();
    return true;
}

bool json_u64(
    const json& object,
    const char* key,
    std::uint64_t* out
) {
    if (!out) {
        return false;
    }

    const auto iterator = object.find(key);

    if (iterator == object.end()) {
        return false;
    }

    if (iterator->is_number_unsigned()) {
        *out = iterator->get<std::uint64_t>();
        return true;
    }

    if (iterator->is_number_integer()) {
        const std::int64_t value =
            iterator->get<std::int64_t>();

        if (value < 0) {
            return false;
        }

        *out = static_cast<std::uint64_t>(value);
        return true;
    }

    return false;
}

bool json_int(
    const json& object,
    const char* key,
    int minimum,
    int maximum,
    int* out
) {
    if (!out) {
        return false;
    }

    const auto iterator = object.find(key);

    if (iterator == object.end() ||
        !iterator->is_number_integer()) {
        return false;
    }

    const std::int64_t value =
        iterator->get<std::int64_t>();

    if (value < minimum || value > maximum) {
        return false;
    }

    *out = static_cast<int>(value);
    return true;
}

json record_to_json(
    const NodeRegistryRecord& record,
    int schema_version
) {
    json value{
        {"node_id", record.node_id},
        {"api_base_url", record.api_base_url},
        {"enabled", record.enabled},
        {
            "capacity_ceiling_bytes",
            record.capacity_ceiling_bytes
        },
        {"added_at", record.added_at},
        {"updated_at", record.updated_at},
        {"health", record.health},
        {"last_seen_at", record.last_seen_at},
        {"node_reported_at", record.node_reported_at},
        {"protocol_version", record.protocol_version},
        {"hostname", record.hostname},
        {"server_version", record.server_version},
        {"uptime_s", record.uptime_s},
        {"storage", {
            {"total_bytes", record.storage_total_bytes},
            {"used_bytes", record.storage_used_bytes},
            {"free_bytes", record.storage_free_bytes},
            {"fstype", record.storage_fstype}
        }},
        {"users", {
            {"total", record.users_total},
            {"enabled", record.users_enabled}
        }}
    };

    if (schema_version >=
        kNodeRegistrySchemaVersion) {
        value["browser_base_url"] =
            record.browser_base_url;
    }

    return value;
}

bool parse_record(
    const json& value,
    int schema_version,
    NodeRegistryRecord* out,
    std::string* err
) {
    if (!out ||
        !has_only_keys(
            value,
            {
                "node_id",
                "api_base_url",
                "browser_base_url",
                "enabled",
                "capacity_ceiling_bytes",
                "added_at",
                "updated_at",
                "health",
                "last_seen_at",
                "node_reported_at",
                "protocol_version",
                "hostname",
                "server_version",
                "uptime_s",
                "storage",
                "users"
            })) {
        set_error(err, "invalid node registry entry");
        return false;
    }

    NodeRegistryRecord record;

    if (!json_string(value, "node_id", &record.node_id) ||
        !valid_cluster_node_id(record.node_id)) {
        set_error(err, "invalid node ID in registry");
        return false;
    }

    std::string raw_url;

    if (!json_string(value, "api_base_url", &raw_url) ||
        !normalize_cluster_https_base_url(
            raw_url,
            &record.api_base_url,
            err)) {
        return false;
    }

    const auto browser_url_iterator =
        value.find("browser_base_url");

    if (schema_version <
        kNodeRegistrySchemaVersion) {
        if (browser_url_iterator != value.end()) {
            set_error(
                err,
                "browser URL is not valid in legacy registry schema"
            );
            return false;
        }
    } else {
        if (browser_url_iterator == value.end() ||
            !browser_url_iterator->is_string()) {
            set_error(
                err,
                "invalid node browser URL"
            );
            return false;
        }

        const std::string raw_browser_url =
            browser_url_iterator->get<std::string>();

        if (!raw_browser_url.empty() &&
            !normalize_cluster_https_base_url(
                raw_browser_url,
                &record.browser_base_url,
                err)) {
            return false;
        }
    }

    const auto enabled_iterator =
        value.find("enabled");

    if (enabled_iterator == value.end() ||
        !enabled_iterator->is_boolean()) {
        set_error(err, "invalid node enabled state");
        return false;
    }

    record.enabled =
        enabled_iterator->get<bool>();

    const auto capacity_ceiling_iterator =
        value.find("capacity_ceiling_bytes");

    // Backward-compatible migration: old registries have no Director-owned
    // ceiling. Keep them fail-closed for placement until an admin configures it.
    if (capacity_ceiling_iterator != value.end() &&
        !json_u64(
            value,
            "capacity_ceiling_bytes",
            &record.capacity_ceiling_bytes)) {
        set_error(
            err,
            "invalid node capacity ceiling"
        );
        return false;
    }

    if (!json_string(value, "added_at", &record.added_at) ||
        !json_string(value, "updated_at", &record.updated_at) ||
        !valid_iso_utc_seconds(record.added_at) ||
        !valid_iso_utc_seconds(record.updated_at)) {
        set_error(err, "invalid node registry timestamp");
        return false;
    }

    if (!json_string(value, "health", &record.health) ||
        (record.health != "unknown" &&
         record.health != "online" &&
         record.health != "offline" &&
         record.health != "disabled")) {
        set_error(err, "invalid node health state");
        return false;
    }

    if ((!record.enabled &&
         record.health != "disabled") ||
        (record.enabled &&
         record.health == "disabled")) {
        set_error(err, "inconsistent node enabled state");
        return false;
    }

    if (!json_string(
            value,
            "last_seen_at",
            &record.last_seen_at) ||
        !json_string(
            value,
            "node_reported_at",
            &record.node_reported_at) ||
        !valid_optional_timestamp(record.last_seen_at) ||
        !valid_optional_timestamp(record.node_reported_at)) {
        set_error(err, "invalid node observation timestamp");
        return false;
    }

    if (!json_int(
            value,
            "protocol_version",
            0,
            1000,
            &record.protocol_version) ||
        !json_string(value, "hostname", &record.hostname) ||
        !json_string(
            value,
            "server_version",
            &record.server_version) ||
        !valid_metadata_token(record.hostname, 253) ||
        !valid_version_string(record.server_version) ||
        !json_u64(value, "uptime_s", &record.uptime_s)) {
        set_error(err, "invalid node observation metadata");
        return false;
    }

    const auto storage_iterator =
        value.find("storage");

    if (storage_iterator == value.end() ||
        !has_only_keys(
            *storage_iterator,
            {
                "total_bytes",
                "used_bytes",
                "free_bytes",
                "fstype"
            }) ||
        !json_u64(
            *storage_iterator,
            "total_bytes",
            &record.storage_total_bytes) ||
        !json_u64(
            *storage_iterator,
            "used_bytes",
            &record.storage_used_bytes) ||
        !json_u64(
            *storage_iterator,
            "free_bytes",
            &record.storage_free_bytes) ||
        !json_string(
            *storage_iterator,
            "fstype",
            &record.storage_fstype) ||
        !valid_metadata_token(record.storage_fstype, 32)) {
        set_error(err, "invalid node storage metadata");
        return false;
    }

    if (record.storage_used_bytes >
            record.storage_total_bytes ||
        record.storage_free_bytes >
            record.storage_total_bytes ||
        record.storage_used_bytes !=
            record.storage_total_bytes -
            record.storage_free_bytes) {
        set_error(err, "inconsistent node storage metadata");
        return false;
    }

    const auto users_iterator =
        value.find("users");

    if (users_iterator == value.end() ||
        !has_only_keys(
            *users_iterator,
            {"total", "enabled"}) ||
        !json_u64(
            *users_iterator,
            "total",
            &record.users_total) ||
        !json_u64(
            *users_iterator,
            "enabled",
            &record.users_enabled) ||
        record.users_enabled > record.users_total) {
        set_error(err, "invalid node user metadata");
        return false;
    }

    *out = std::move(record);
    return true;
}

bool read_all_bounded(
    int fd,
    std::string* out,
    std::string* err
) {
    if (!out) {
        set_error(err, "missing registry read output");
        return false;
    }

    out->clear();

    std::array<char, 8192> buffer{};

    for (;;) {
        const ssize_t count =
            ::read(fd, buffer.data(), buffer.size());

        if (count > 0) {
            const std::size_t addition =
                static_cast<std::size_t>(count);

            if (out->size() >
                kMaxRegistryBytes - addition) {
                set_error(err, "node registry is too large");
                return false;
            }

            out->append(buffer.data(), addition);
            continue;
        }

        if (count == 0) {
            return true;
        }

        if (errno == EINTR) {
            continue;
        }

        set_error(err, "failed to read node registry");
        return false;
    }
}

bool load_registry_file(
    const std::filesystem::path& path,
    std::unordered_map<
        std::string,
        NodeRegistryRecord
    >* parsed,
    bool* missing,
    std::string* err
) {
    if (!parsed || !missing) {
        set_error(err, "missing registry load output");
        return false;
    }

    parsed->clear();
    *missing = false;

    const std::filesystem::path parent =
        path.parent_path();

    const int raw_directory_fd = ::open(
        parent.c_str(),
        O_RDONLY |
        O_DIRECTORY |
        O_CLOEXEC |
        O_NOFOLLOW
    );

    if (raw_directory_fd < 0) {
        if (errno == ENOENT) {
            *missing = true;
            return true;
        }

        set_error(err, "failed to open node registry directory");
        return false;
    }

    UniqueFd directory_fd(raw_directory_fd);

    const std::string filename =
        path.filename().string();

    const int raw_file_fd = ::openat(
        directory_fd.get(),
        filename.c_str(),
        O_RDONLY |
        O_CLOEXEC |
        O_NOFOLLOW
    );

    if (raw_file_fd < 0) {
        if (errno == ENOENT) {
            *missing = true;
            return true;
        }

        set_error(err, "failed to open node registry");
        return false;
    }

    UniqueFd file_fd(raw_file_fd);

    struct stat stats {};

    if (::fstat(file_fd.get(), &stats) != 0) {
        set_error(err, "failed to inspect node registry");
        return false;
    }

    if (!S_ISREG(stats.st_mode) ||
        stats.st_nlink != 1 ||
        (stats.st_mode & 0022) != 0) {
        set_error(err, "unsafe node registry file");
        return false;
    }

    const uid_t effective_uid = ::geteuid();

    if (stats.st_uid != effective_uid &&
        stats.st_uid != 0) {
        set_error(err, "unsafe node registry ownership");
        return false;
    }

    if (stats.st_size < 0 ||
        static_cast<unsigned long long>(stats.st_size) >
            static_cast<unsigned long long>(
                kMaxRegistryBytes)) {
        set_error(err, "node registry is too large");
        return false;
    }

    std::string contents;

    if (!read_all_bounded(
            file_fd.get(),
            &contents,
            err)) {
        return false;
    }

    json root;

    try {
        root = json::parse(contents);
    } catch (...) {
        set_error(err, "invalid node registry JSON");
        return false;
    }

    if (!has_only_keys(root, {"version", "nodes"})) {
        set_error(err, "invalid node registry root");
        return false;
    }

    int version = 0;

    if (!json_int(
            root,
            "version",
            kNodeRegistryLegacySchemaVersion,
            kNodeRegistrySchemaVersion,
            &version)) {
        set_error(err, "unsupported node registry version");
        return false;
    }

    const auto nodes_iterator = root.find("nodes");

    if (nodes_iterator == root.end() ||
        !nodes_iterator->is_array() ||
        nodes_iterator->size() > kMaxNodeCount) {
        set_error(err, "invalid node registry node list");
        return false;
    }

    std::set<std::string> api_urls;
    std::set<std::string> browser_urls;

    for (const auto& value : *nodes_iterator) {
        NodeRegistryRecord record;

        if (!parse_record(
                value,
                version,
                &record,
                err)) {
            return false;
        }

        if (parsed->find(record.node_id) !=
            parsed->end()) {
            set_error(err, "duplicate node ID in registry");
            return false;
        }

        if (browser_urls.find(
                record.api_base_url) !=
            browser_urls.end()) {
            set_error(
                err,
                "node endpoint URL is shared by multiple nodes"
            );
            return false;
        }

        if (!api_urls.insert(
                record.api_base_url).second) {
            set_error(err, "duplicate node API URL in registry");
            return false;
        }

        if (!record.browser_base_url.empty()) {
            if (record.browser_base_url !=
                    record.api_base_url &&
                api_urls.find(
                    record.browser_base_url) !=
                    api_urls.end()) {
                set_error(
                    err,
                    "node endpoint URL is shared by multiple nodes"
                );
                return false;
            }

            if (!browser_urls.insert(
                    record.browser_base_url).second) {
                set_error(
                    err,
                    "duplicate node browser URL in registry"
                );
                return false;
            }
        }

        parsed->emplace(
            record.node_id,
            std::move(record)
        );
    }

    return true;
}

bool write_all(
    int fd,
    const std::string& contents,
    std::string* err
) {
    std::size_t offset = 0;

    while (offset < contents.size()) {
        const ssize_t count = ::write(
            fd,
            contents.data() + offset,
            contents.size() - offset
        );

        if (count > 0) {
            offset +=
                static_cast<std::size_t>(count);
            continue;
        }

        if (count < 0 && errno == EINTR) {
            continue;
        }

        set_error(err, "failed to write node registry");
        return false;
    }

    return true;
}

bool persist_registry_file(
    const std::filesystem::path& path,
    const std::unordered_map<
        std::string,
        NodeRegistryRecord
    >& nodes,
    bool* committed,
    std::string* err
) {
    if (!committed) {
        set_error(
            err,
            "missing node registry commit output"
        );
        return false;
    }

    *committed = false;

    std::vector<std::string> node_ids;
    node_ids.reserve(nodes.size());

    for (const auto& entry : nodes) {
        node_ids.push_back(entry.first);
    }

    std::sort(node_ids.begin(), node_ids.end());

    // Rolling-upgrade compatibility: merely running browser-aware code must
    // not rewrite a v2 registry into rollback-incompatible v3. Emit v3 only
    // after a Director-owned browser URL has actually been configured.
    bool browser_schema_required = false;

    for (const auto& entry : nodes) {
        if (!entry.second.browser_base_url.empty()) {
            browser_schema_required = true;
            break;
        }
    }

    const int persisted_schema_version =
        browser_schema_required
            ? kNodeRegistrySchemaVersion
            : kNodeRegistryPreBrowserSchemaVersion;

    json root{
        {"version", persisted_schema_version},
        {"nodes", json::array()}
    };

    for (const std::string& node_id : node_ids) {
        root["nodes"].push_back(
            record_to_json(
                nodes.at(node_id),
                persisted_schema_version
            )
        );
    }

    std::string contents;

    try {
        contents = root.dump(2);
        contents.push_back('\n');
    } catch (...) {
        set_error(err, "failed to serialize node registry");
        return false;
    }

    std::error_code filesystem_error;

    std::filesystem::create_directories(
        path.parent_path(),
        filesystem_error
    );

    if (filesystem_error) {
        set_error(err, "failed to create node registry directory");
        return false;
    }

    const int raw_directory_fd = ::open(
        path.parent_path().c_str(),
        O_RDONLY |
        O_DIRECTORY |
        O_CLOEXEC |
        O_NOFOLLOW
    );

    if (raw_directory_fd < 0) {
        set_error(err, "failed to open node registry directory");
        return false;
    }

    UniqueFd directory_fd(raw_directory_fd);

    const std::string target_filename =
        path.filename().string();

    std::string temporary_filename;
    UniqueFd temporary_fd;

    for (int attempt = 0; attempt < 16; ++attempt) {
        const std::uint64_t counter =
            g_temp_counter.fetch_add(1);

        temporary_filename =
            "." +
            target_filename +
            ".tmp." +
            std::to_string(
                static_cast<long long>(::getpid())
            ) +
            "." +
            std::to_string(counter);

        const int raw_temporary_fd = ::openat(
            directory_fd.get(),
            temporary_filename.c_str(),
            O_WRONLY |
            O_CREAT |
            O_EXCL |
            O_CLOEXEC |
            O_NOFOLLOW,
            0600
        );

        if (raw_temporary_fd >= 0) {
            temporary_fd =
                UniqueFd(raw_temporary_fd);
            break;
        }

        if (errno != EEXIST) {
            set_error(err, "failed to create node registry temp file");
            return false;
        }
    }

    if (temporary_fd.get() < 0) {
        set_error(err, "failed to allocate node registry temp file");
        return false;
    }

    const auto remove_temp = [&]() {
        if (!temporary_filename.empty()) {
            (void)::unlinkat(
                directory_fd.get(),
                temporary_filename.c_str(),
                0
            );
        }
    };

    if (::fchmod(temporary_fd.get(), 0600) != 0 ||
        !write_all(
            temporary_fd.get(),
            contents,
            err) ||
        ::fsync(temporary_fd.get()) != 0) {
        if (err && err->empty()) {
            *err = "failed to sync node registry";
        }

        remove_temp();
        return false;
    }

    const int raw_temporary_fd =
        temporary_fd.release();

    if (::close(raw_temporary_fd) != 0) {
        set_error(err, "failed to close node registry temp file");
        remove_temp();
        return false;
    }

    if (::renameat(
            directory_fd.get(),
            temporary_filename.c_str(),
            directory_fd.get(),
            target_filename.c_str()) != 0) {
        set_error(err, "failed to commit node registry");
        remove_temp();
        return false;
    }

    // Security: rename has made the new inode logically visible. Record
    // that fact before directory fsync so callers never retain stale in-memory
    // state after an uncertain durability result.
    *committed = true;

    if (::fsync(directory_fd.get()) != 0) {
        set_error(
            err,
            "failed to sync node registry directory"
        );
        return false;
    }

    return true;
}

bool validate_status_snapshot(
    const NodeStatusSnapshot& status,
    std::string* err
) {
    if (!node_status_protocol_version_supported(
            status.protocol_version) ||
        !valid_cluster_node_id(status.node_id) ||
        !valid_metadata_token(status.hostname, 253) ||
        !valid_version_string(status.server_version) ||
        !valid_metadata_token(status.storage.fstype, 32) ||
        !valid_iso_utc_seconds(status.timestamp)) {
        set_error(err, "invalid node status metadata");
        return false;
    }

    if (status.storage.used_bytes >
            status.storage.total_bytes ||
        status.storage.free_bytes >
            status.storage.total_bytes ||
        status.storage.used_bytes !=
            status.storage.total_bytes -
            status.storage.free_bytes ||
        status.users.enabled > status.users.total) {
        set_error(err, "inconsistent node status counters");
        return false;
    }

    return true;
}

} // namespace

const char* node_registry_insert_result_code(
    NodeRegistryInsertResult result
) noexcept {
    switch (result) {
        case NodeRegistryInsertResult::Ok:
            return "ok";
        case NodeRegistryInsertResult::InvalidInput:
            return "node_registry_invalid_input";
        case NodeRegistryInsertResult::DuplicateNodeId:
            return "node_registry_duplicate_node_id";
        case NodeRegistryInsertResult::DuplicateApiBaseUrl:
            return "node_registry_duplicate_api_base_url";
        case NodeRegistryInsertResult::PersistFailed:
            return "node_registry_persist_failed";
    }

    return "node_registry_unknown";
}

bool NodeRegistry::load(
    const std::string& path,
    std::string* err
) {
    if (err) {
        err->clear();
    }

    std::filesystem::path normalized_path;

    if (!normalize_registry_path(
            path,
            &normalized_path,
            err)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    std::unordered_map<
        std::string,
        NodeRegistryRecord
    > parsed;

    bool missing = false;

    if (!load_registry_file(
            normalized_path,
            &parsed,
            &missing,
            err)) {
        return false;
    }

    path_ = std::move(normalized_path);

    if (missing) {
        nodes_.clear();
    } else {
        nodes_ = std::move(parsed);
    }

    return true;
}

std::optional<NodeRegistryRecord> NodeRegistry::get(
    const std::string& node_id
) const {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto iterator = nodes_.find(node_id);

    if (iterator == nodes_.end()) {
        return std::nullopt;
    }

    return iterator->second;
}

std::vector<NodeRegistryRecord>
NodeRegistry::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<NodeRegistryRecord> result;
    result.reserve(nodes_.size());

    for (const auto& entry : nodes_) {
        result.push_back(entry.second);
    }

    std::sort(
        result.begin(),
        result.end(),
        [](
            const NodeRegistryRecord& left,
            const NodeRegistryRecord& right
        ) {
            return left.node_id < right.node_id;
        }
    );

    return result;
}

bool NodeRegistry::commit_candidate_locked(
    std::unordered_map<
        std::string,
        NodeRegistryRecord
    >&& candidate,
    std::string* err
) {
    if (path_.empty()) {
        set_error(err, "node registry is not loaded");
        return false;
    }

    bool committed = false;

    const bool durable =
        persist_registry_file(
            path_,
            candidate,
            &committed,
            err
        );

    // Security: rename may already have replaced the registry even when the
    // following directory fsync reports an error. Keep process memory aligned
    // with the visible inode so retries and recovery cannot use stale state.
    if (committed) {
        nodes_ = std::move(candidate);
    }

    return durable;
}

NodeRegistryInsertResult
NodeRegistry::insert_configured_node_if_absent(
    const std::string& node_id,
    const std::string& api_base_url,
    bool enabled,
    const std::string& now_iso,
    std::string* err
) {
    if (err) {
        err->clear();
    }

    if (!valid_cluster_node_id(node_id) ||
        !valid_iso_utc_seconds(now_iso)) {
        set_error(
            err,
            "invalid configured node insertion metadata"
        );
        return NodeRegistryInsertResult::InvalidInput;
    }

    std::string normalized_url;

    if (!normalize_cluster_https_base_url(
            api_base_url,
            &normalized_url,
            err)) {
        return NodeRegistryInsertResult::InvalidInput;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (nodes_.find(node_id) != nodes_.end()) {
        set_error(
            err,
            "node ID is already registered"
        );
        return NodeRegistryInsertResult::DuplicateNodeId;
    }

    for (const auto& entry : nodes_) {
        if (entry.second.api_base_url ==
            normalized_url) {
            set_error(
                err,
                "node API URL is already registered"
            );
            return NodeRegistryInsertResult::
                DuplicateApiBaseUrl;
        }
    }

    auto candidate = nodes_;

    NodeRegistryRecord record;
    record.node_id = node_id;
    record.api_base_url = normalized_url;
    record.enabled = enabled;
    record.added_at = now_iso;
    record.updated_at = now_iso;
    record.health =
        enabled ? "unknown" : "disabled";

    candidate.emplace(
        node_id,
        std::move(record)
    );

    // Security: duplicate checks and the durable snapshot commit occur under
    // one registry mutex, preventing enrollment races from replacing or
    // aliasing an already trusted node identity.
    if (!commit_candidate_locked(
            std::move(candidate),
            err)) {
        return NodeRegistryInsertResult::PersistFailed;
    }

    return NodeRegistryInsertResult::Ok;
}

bool NodeRegistry::upsert_configured_node(
    const std::string& node_id,
    const std::string& api_base_url,
    bool enabled,
    const std::string& now_iso,
    std::string* err,
    std::optional<std::uint64_t>
        capacity_ceiling_bytes,
    std::optional<std::string>
        browser_base_url
) {
    if (err) {
        err->clear();
    }

    if (!valid_cluster_node_id(node_id) ||
        !valid_iso_utc_seconds(now_iso)) {
        set_error(err, "invalid configured node metadata");
        return false;
    }

    std::string normalized_url;

    if (!normalize_cluster_https_base_url(
            api_base_url,
            &normalized_url,
            err)) {
        return false;
    }

    std::optional<std::string>
        normalized_browser_url;

    if (browser_base_url.has_value()) {
        if (browser_base_url->empty()) {
            normalized_browser_url =
                std::string();
        } else {
            std::string normalized;

            if (!normalize_cluster_https_base_url(
                    *browser_base_url,
                    &normalized,
                    err)) {
                return false;
            }

            normalized_browser_url =
                std::move(normalized);
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);

    auto candidate = nodes_;

    for (const auto& entry : candidate) {
        if (entry.first == node_id) {
            continue;
        }

        if (entry.second.api_base_url == normalized_url) {
            set_error(err, "node API URL is already registered");
            return false;
        }

        // Security: browser and service endpoint namespaces belong to the
        // same Node identity. A different Node may not claim either form
        // of an already-trusted authority.
        if (!entry.second.browser_base_url.empty() &&
            entry.second.browser_base_url == normalized_url) {
            set_error(
                err,
                "node endpoint URL is already registered"
            );
            return false;
        }

        if (normalized_browser_url.has_value() &&
            !normalized_browser_url->empty()) {
            if (entry.second.browser_base_url ==
                    *normalized_browser_url) {
                set_error(
                    err,
                    "node browser URL is already registered"
                );
                return false;
            }

            if (entry.second.api_base_url ==
                    *normalized_browser_url) {
                set_error(
                    err,
                    "node endpoint URL is already registered"
                );
                return false;
            }
        }
    }

    auto iterator = candidate.find(node_id);

    if (iterator == candidate.end()) {
        NodeRegistryRecord record;
        record.node_id = node_id;
        record.api_base_url = normalized_url;
        record.browser_base_url =
            normalized_browser_url.value_or(
                std::string()
            );
        record.enabled = enabled;
        record.capacity_ceiling_bytes =
            capacity_ceiling_bytes.value_or(0);
        record.added_at = now_iso;
        record.updated_at = now_iso;
        record.health =
            enabled ? "unknown" : "disabled";

        candidate.emplace(
            node_id,
            std::move(record)
        );
    } else {
        NodeRegistryRecord& record =
            iterator->second;

        record.api_base_url = normalized_url;
        record.enabled = enabled;
        record.updated_at = now_iso;

        if (capacity_ceiling_bytes.has_value()) {
            // Security: only an explicit Director-side configuration mutation
            // may change the trusted capacity ceiling.
            record.capacity_ceiling_bytes =
                *capacity_ceiling_bytes;
        }

        if (normalized_browser_url.has_value()) {
            // Security: browser routing is Director-owned configuration.
            // Heartbeats and unrelated lifecycle updates must preserve it.
            record.browser_base_url =
                *normalized_browser_url;
        }

        if (!enabled) {
            record.health = "disabled";
            record.last_seen_at.clear();
            record.node_reported_at.clear();
        } else if (record.health == "disabled") {
            record.health = "unknown";
        }
    }

    if (!commit_candidate_locked(
            std::move(candidate),
            err)) {
        return false;
    }
    return true;
}

bool NodeRegistry::set_enabled(
    const std::string& node_id,
    bool enabled,
    const std::string& now_iso,
    std::string* err
) {
    if (err) {
        err->clear();
    }

    if (!valid_cluster_node_id(node_id) ||
        !valid_iso_utc_seconds(now_iso)) {
        set_error(err, "invalid node update metadata");
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    auto candidate = nodes_;
    auto iterator = candidate.find(node_id);

    if (iterator == candidate.end()) {
        set_error(err, "node not found");
        return false;
    }

    NodeRegistryRecord& record =
        iterator->second;

    record.enabled = enabled;
    record.updated_at = now_iso;

    if (!enabled) {
        record.health = "disabled";
        record.last_seen_at.clear();
        record.node_reported_at.clear();
    } else if (record.health == "disabled") {
        record.health = "unknown";
    }

    if (!commit_candidate_locked(
            std::move(candidate),
            err)) {
        return false;
    }
    return true;
}

bool NodeRegistry::erase(
    const std::string& node_id,
    std::string* err
) {
    if (err) {
        err->clear();
    }

    if (!valid_cluster_node_id(node_id)) {
        set_error(err, "invalid node ID");
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    auto candidate = nodes_;

    if (candidate.erase(node_id) != 1) {
        set_error(err, "node not found");
        return false;
    }

    if (!commit_candidate_locked(
            std::move(candidate),
            err)) {
        return false;
    }
    return true;
}

bool NodeRegistry::mark_offline(
    const std::string& node_id,
    std::string* err,
    bool* transitioned_offline
) {
    if (err) {
        err->clear();
    }

    if (transitioned_offline) {
        *transitioned_offline = false;
    }

    if (!valid_cluster_node_id(node_id)) {
        set_error(err, "invalid node ID");
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    auto candidate = nodes_;
    auto iterator = candidate.find(node_id);

    if (iterator == candidate.end()) {
        set_error(err, "node not found");
        return false;
    }

    NodeRegistryRecord& record =
        iterator->second;

    if (!record.enabled) {
        set_error(
            err,
            "disabled node cannot be marked offline"
        );
        return false;
    }

    if (record.health == "offline") {
        return true;
    }

    record.health = "offline";

    if (!commit_candidate_locked(
            std::move(candidate),
            err)) {
        return false;
    }

    if (transitioned_offline) {
        *transitioned_offline = true;
    }

    return true;
}

bool NodeRegistry::apply_status(
    const NodeStatusSnapshot& status,
    const std::string& received_at,
    std::string* err,
    bool* transitioned_online
) {
    if (err) {
        err->clear();
    }

    if (transitioned_online) {
        *transitioned_online = false;
    }

    if (!valid_iso_utc_seconds(received_at) ||
        !validate_status_snapshot(status, err)) {
        if (err && err->empty()) {
            *err = "invalid node observation";
        }

        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    auto candidate = nodes_;
    auto iterator =
        candidate.find(status.node_id);

    if (iterator == candidate.end()) {
        set_error(
            err,
            "status received from unregistered node"
        );
        return false;
    }

    NodeRegistryRecord& record =
        iterator->second;

    if (!record.enabled) {
        set_error(
            err,
            "status received from disabled node"
        );
        return false;
    }

    // Security: once Director has authenticated the RT-008 range-audit
    // capability for this node, a later heartbeat may not silently downgrade
    // it to the legacy POST-only protocol. Explicit administration is required
    // for any rollback of that security capability.
    if (node_status_protocol_supports_projection_audit(
            record.protocol_version) &&
        !node_status_protocol_supports_projection_audit(
            status.protocol_version)) {
        set_error(
            err,
            "node status protocol downgrade rejected"
        );
        return false;
    }

    const bool became_online =
        record.health != "online";

    record.health = "online";
    record.last_seen_at = received_at;
    record.node_reported_at = status.timestamp;

    // RT-008 rolling upgrade: preserve the exact authenticated heartbeat
    // protocol so Director can distinguish legacy POST verification from
    // v2 range-audit capability.
    record.protocol_version =
        status.protocol_version;

    record.hostname = status.hostname;
    record.server_version = status.server_version;
    record.uptime_s = status.uptime_s;

    record.storage_total_bytes =
        status.storage.total_bytes;
    record.storage_used_bytes =
        status.storage.used_bytes;
    record.storage_free_bytes =
        status.storage.free_bytes;
    record.storage_fstype =
        status.storage.fstype;

    record.users_total =
        status.users.total;
    record.users_enabled =
        status.users.enabled;

    if (!commit_candidate_locked(
            std::move(candidate),
            err)) {
        return false;
    }

    if (transitioned_online) {
        *transitioned_online = became_online;
    }

    return true;
}

} // namespace pqnas::cluster
