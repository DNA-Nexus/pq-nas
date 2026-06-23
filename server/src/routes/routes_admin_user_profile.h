#pragma once

#include <functional>
#include <string>

namespace httplib {
class Server;
struct Request;
struct Response;
}

namespace pqnas {
class UsersRegistry;
struct AuditEvent;
}

struct AdminUserProfileRoutesContext {
    pqnas::UsersRegistry* users = nullptr;
    std::string users_path;

    std::function<bool(const httplib::Request&, httplib::Response&, std::string*)> require_admin_cookie;
    std::function<bool(const httplib::Request&, httplib::Response&, std::string*)> require_admin_auth;
    std::function<bool(const httplib::Request&, httplib::Response&)> require_same_origin;
    std::function<void(httplib::Response&, int, const std::string&)> reply_json;
    std::function<std::string()> now_iso_utc;
    std::function<void(const pqnas::AuditEvent&)> audit_append;
};

void register_admin_user_profile_routes(
    httplib::Server& srv,
    const AdminUserProfileRoutesContext& ctx
);
