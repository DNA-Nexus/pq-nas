#pragma once

#include <functional>
#include <string>

namespace httplib {
class Server;
struct Request;
struct Response;
}

struct AdminAuditReadRoutesContext {
    std::string static_audit_js;
    std::string audit_jsonl_path;
    std::string audit_state_path;

    std::function<bool(const httplib::Request&, httplib::Response&)> require_admin;
    std::function<void(httplib::Response&, int, const std::string&)> reply_json;
    std::function<std::string(const std::string&)> slurp_file;
    std::function<std::string(const std::string&)> trim_nl;
};

void register_admin_audit_read_routes(
    httplib::Server& srv,
    const AdminAuditReadRoutesContext& ctx
);
