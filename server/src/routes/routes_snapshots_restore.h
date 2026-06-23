#pragma once

#include <functional>
#include <string>
#include <vector>

namespace httplib {
class Server;
struct Request;
struct Response;
}

namespace pqnas {
struct AuditEvent;
}

struct SnapshotRestoreVolume {
    std::string name;
    std::string source_subvolume;
    std::string snap_root;
    bool enabled = false;
};

struct SnapshotRestoreRoutesContext {
    std::function<bool(const httplib::Request&, httplib::Response&, std::string*)> require_admin;
    std::function<void(httplib::Response&, int, const std::string&)> reply_json;
    std::function<void(const pqnas::AuditEvent&)> audit_append;

    std::function<bool(std::string*, std::vector<SnapshotRestoreVolume>*, std::string*)> load_volumes;
    std::function<bool(const std::string&, const std::string&)> is_path_under;
    std::function<bool(const std::string&)> is_btrfs_subvolume;

    std::function<std::string(const std::string&)> realpath_str;
    std::function<std::string()> now_iso_utc;
    std::function<std::string()> rand_hex_32;
    std::function<std::string(std::size_t)> random_b64url;

    std::function<void(const std::string&, std::string*, int*)> popen_capture;
};

void register_snapshot_restore_routes(
    httplib::Server& srv,
    const SnapshotRestoreRoutesContext& ctx
);
