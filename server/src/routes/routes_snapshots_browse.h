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

struct SnapshotBrowseVolume {
    std::string name;
    std::string source_subvolume;
    std::string snap_root;
    bool enabled = false;
};

struct SnapshotBrowseRoutesContext {
    std::function<bool(const httplib::Request&, httplib::Response&, std::string*)> require_admin;
    std::function<void(httplib::Response&, int, const std::string&)> reply_json;
    std::function<void(const pqnas::AuditEvent&)> audit_append;

    std::function<bool(std::string*, std::vector<SnapshotBrowseVolume>*, std::string*)> load_volumes;
    std::function<bool(const std::string&, std::string*)> is_btrfs_subvolume;
    std::function<bool(const std::string&, const std::string&)> is_path_under;
    std::function<void(const std::string&, std::string*, int*)> btrfs_subvolume_show;

    std::function<std::string(const std::string&, std::size_t)> audit_safe_header_value;
};

void register_snapshot_browse_routes(
    httplib::Server& srv,
    const SnapshotBrowseRoutesContext& ctx
);
