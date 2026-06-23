#pragma once

#include <functional>
#include <string>

#include <nlohmann/json.hpp>

namespace httplib {
class Server;
struct Request;
struct Response;
}

struct AdminAuditRotateRoutesContext {
    std::function<bool(const httplib::Request&, httplib::Response&)> require_admin;
    std::function<bool(const httplib::Request&, httplib::Response&)> require_same_origin;
    std::function<bool(nlohmann::json*)> rotate_audit;
};

void register_admin_audit_rotate_routes(
    httplib::Server& srv,
    const AdminAuditRotateRoutesContext& ctx
);
