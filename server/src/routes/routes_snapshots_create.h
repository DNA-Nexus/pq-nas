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

struct SnapshotCreateVolume {
    std::string name;
    std::string source_subvolume;
    std::string snap_root;
    bool enabled = false;
};

struct SnapshotCreateRoutesContext {
    std::function<bool(const httplib::Request&, httplib::Response&, std::string*)> require_admin;
    std::function<bool(const httplib::Request&, httplib::Response&)> require_same_origin;
    std::function<void(httplib::Response&, int, const std::string&)> reply_json;
    std::function<void(const pqnas::AuditEvent&)> audit_append;

    std::function<bool(std::string*, std::vector<SnapshotCreateVolume>*, std::string*)> load_volumes;
    std::function<bool(const std::string&, std::string*)> is_btrfs_subvolume;

    std::function<std::string(const std::string&, std::size_t)> audit_safe_header_value;
};

void register_snapshot_create_routes(
    httplib::Server& srv,
    const SnapshotCreateRoutesContext& ctx
);
