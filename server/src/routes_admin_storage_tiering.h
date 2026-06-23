#pragma once

#include <functional>
#include <string>

namespace httplib {
class Server;
struct Request;
struct Response;
}

namespace pqnas {
struct AuditEvent;
}

struct AdminStorageTieringRoutesContext {
    std::function<bool(const httplib::Request&, httplib::Response&, std::string*)> require_admin;
    std::function<bool(const httplib::Request&, httplib::Response&)> require_same_origin;
    std::function<void(httplib::Response&, int, const std::string&)> reply_json;
    std::function<bool(const std::string&, const std::string&, std::string*)> migrate_one_landing_file;
    std::function<void(const pqnas::AuditEvent&)> audit_append;
};

void register_admin_storage_tiering_routes(
    httplib::Server& srv,
    const AdminStorageTieringRoutesContext& ctx
);
