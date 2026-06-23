#pragma once

#include <functional>
#include <string>

#include <nlohmann/json.hpp>

namespace httplib {
class Server;
struct Request;
struct Response;
}

struct AdminAuditRetentionRoutesContext {
    std::function<bool(const httplib::Request&, httplib::Response&)> require_admin;
    std::function<bool(const httplib::Request&, httplib::Response&)> require_same_origin;
    std::function<void(httplib::Response&, int, const std::string&)> reply_json;

    std::function<bool(const nlohmann::json&, nlohmann::json*, int*)> preview_prune;
    std::function<bool(const httplib::Request&, nlohmann::json*, int*)> prune;
};

void register_admin_audit_retention_routes(
    httplib::Server& srv,
    const AdminAuditRetentionRoutesContext& ctx
);
