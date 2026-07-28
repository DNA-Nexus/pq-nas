#include "cluster/node_status.h"

#include "storage_info.h"
#include "users_registry.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <limits>
#include <string>

#include <sys/statvfs.h>
#include <unistd.h>

namespace pqnas::cluster {
namespace {

void set_error(std::string* err, const std::string& message) {
    if (err) {
        *err = message;
    }
}

bool valid_node_id(const std::string& value) {
    if (value.empty() || value.size() > 64) {
        return false;
    }

    for (std::size_t i = 0; i < value.size(); ++i) {
        const unsigned char ch =
            static_cast<unsigned char>(value[i]);

        const bool alphanumeric =
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9');

        if (i == 0) {
            if (!alphanumeric) {
                return false;
            }
            continue;
        }

        if (!alphanumeric &&
            ch != '-' &&
            ch != '_' &&
            ch != '.') {
            return false;
        }
    }

    return true;
}

bool valid_version(const std::string& value) {
    if (value.empty() || value.size() > 64) {
        return false;
    }

    for (unsigned char ch : value) {
        if (ch < 0x21 || ch > 0x7e) {
            return false;
        }
    }

    return true;
}

std::string safe_hostname() {
    char buffer[256] = {};

    if (::gethostname(buffer, sizeof(buffer) - 1) != 0) {
        return "unknown";
    }

    const std::string value(buffer);

    if (value.empty() || value.size() > 253) {
        return "unknown";
    }

    // Security: local host state is still treated as untrusted metadata.
    // Restrict it before forwarding the value to another server.
    for (unsigned char ch : value) {
        const bool alphanumeric =
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9');

        if (!alphanumeric &&
            ch != '-' &&
            ch != '_' &&
            ch != '.') {
            return "unknown";
        }
    }

    return value;
}

std::string safe_fstype(const std::string& value) {
    if (value.empty() || value.size() > 32) {
        return "unknown";
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
            return "unknown";
        }
    }

    return value;
}

bool checked_multiply(
    std::uint64_t left,
    std::uint64_t right,
    std::uint64_t* out
) {
    if (!out) {
        return false;
    }

    if (left != 0 &&
        right > std::numeric_limits<std::uint64_t>::max() / left) {
        return false;
    }

    *out = left * right;
    return true;
}

bool collect_storage_bytes(
    const std::string& data_root,
    NodeStorageStatus* out,
    std::string* err
) {
    if (!out) {
        set_error(err, "missing storage status output");
        return false;
    }

    if (data_root.empty()) {
        set_error(err, "storage root is not configured");
        return false;
    }

    struct statvfs stats {};

    if (::statvfs(data_root.c_str(), &stats) != 0) {
        // Do not return the configured path or errno details to callers.
        set_error(err, "storage statistics unavailable");
        return false;
    }

    const std::uint64_t block_size =
        stats.f_frsize != 0
            ? static_cast<std::uint64_t>(stats.f_frsize)
            : static_cast<std::uint64_t>(stats.f_bsize);

    std::uint64_t total = 0;
    std::uint64_t free = 0;

    if (!checked_multiply(
            static_cast<std::uint64_t>(stats.f_blocks),
            block_size,
            &total) ||
        !checked_multiply(
            static_cast<std::uint64_t>(stats.f_bavail),
            block_size,
            &free)) {
        set_error(err, "storage statistics overflow");
        return false;
    }

    free = std::min(free, total);

    out->total_bytes = total;
    out->free_bytes = free;
    out->used_bytes = total - free;

    StorageInfo storage_info;
    std::string storage_info_err;

    if (get_storage_info(
            data_root,
            &storage_info,
            &storage_info_err)) {
        out->fstype = safe_fstype(storage_info.fstype);
    } else {
        out->fstype = "unknown";
    }

    return true;
}

bool read_uptime_seconds(std::uint64_t* out) {
    if (!out) {
        return false;
    }

    std::ifstream file("/proc/uptime");

    double value = 0.0;

    if (!(file >> value) ||
        !std::isfinite(value) ||
        value < 0.0 ||
        value >
            static_cast<double>(
                std::numeric_limits<std::uint64_t>::max())) {
        return false;
    }

    *out = static_cast<std::uint64_t>(value);
    return true;
}

std::string now_iso_utc() {
    using namespace std::chrono;

    const auto now = system_clock::now();
    const std::time_t timestamp =
        system_clock::to_time_t(now);

    std::tm utc {};

    if (::gmtime_r(&timestamp, &utc) == nullptr) {
        return {};
    }

    char buffer[32] = {};

    const int written = std::snprintf(
        buffer,
        sizeof(buffer),
        "%04d-%02d-%02dT%02d:%02d:%02dZ",
        utc.tm_year + 1900,
        utc.tm_mon + 1,
        utc.tm_mday,
        utc.tm_hour,
        utc.tm_min,
        utc.tm_sec
    );

    if (written <= 0 ||
        static_cast<std::size_t>(written) >= sizeof(buffer)) {
        return {};
    }

    return std::string(buffer);
}

} // namespace

bool collect_node_status_snapshot(
    const std::string& node_id,
    const std::string& server_version,
    const std::string& data_root,
    const UsersRegistry& users,
    NodeStatusSnapshot* out,
    std::string* err
) {
    if (err) {
        err->clear();
    }

    if (!out) {
        set_error(err, "missing node status output");
        return false;
    }

    *out = NodeStatusSnapshot{};

    // Defense in depth: validate again at the boundary that constructs
    // remotely transmitted control-plane data.
    if (!valid_node_id(node_id)) {
        set_error(err, "invalid node identity");
        return false;
    }

    if (!valid_version(server_version)) {
        set_error(err, "invalid server version");
        return false;
    }

    NodeStorageStatus storage;

    if (!collect_storage_bytes(data_root, &storage, err)) {
        return false;
    }

    std::uint64_t uptime_s = 0;

    if (!read_uptime_seconds(&uptime_s)) {
        set_error(err, "system uptime unavailable");
        return false;
    }

    const std::string timestamp = now_iso_utc();

    if (timestamp.empty()) {
        set_error(err, "system timestamp unavailable");
        return false;
    }

    NodeUserStatus user_status;

    const auto user_snapshot = users.snapshot();

    user_status.total =
        static_cast<std::uint64_t>(user_snapshot.size());

    for (const auto& entry : user_snapshot) {
        if (entry.second.status == "enabled") {
            ++user_status.enabled;
        }
    }

    out->node_id = node_id;
    out->hostname = safe_hostname();
    out->server_version = server_version;
    out->uptime_s = uptime_s;
    out->storage = storage;
    out->users = user_status;
    out->timestamp = timestamp;

    return true;
}

nlohmann::json node_status_to_json(
    const NodeStatusSnapshot& status
) {
    return nlohmann::json{
        {"ok", true},
        {"protocol_version", kNodeStatusProtocolVersion},
        {"node_id", status.node_id},
        {"role", "node"},
        {"hostname", status.hostname},
        {"server_version", status.server_version},
        {"uptime_s", status.uptime_s},
        {"storage", {
            {"total_bytes", status.storage.total_bytes},
            {"used_bytes", status.storage.used_bytes},
            {"free_bytes", status.storage.free_bytes},
            {"fstype", status.storage.fstype}
        }},
        {"users", {
            {"total", status.users.total},
            {"enabled", status.users.enabled}
        }},
        {"timestamp", status.timestamp}
    };
}

} // namespace pqnas::cluster
