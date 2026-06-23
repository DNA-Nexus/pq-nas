#pragma once

#include <cstddef>
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

struct AdminUserStatusRoutesContext {
    pqnas::UsersRegistry* users = nullptr;
    std::string users_path;

    std::function<bool(const httplib::Request&, httplib::Response&, std::string*)> require_admin_auth;
    std::function<bool(const httplib::Request&, httplib::Response&)> require_same_origin;
    std::function<void(httplib::Response&, int, const std::string&)> reply_json;

    std::function<bool(const std::string&)> admin_would_remove_last_enabled_admin;
    std::function<bool(const std::string&, std::size_t*, std::string*)> invalidate_opaque_enrollment_tokens_for_revoke;
    std::function<void(const std::string&)> revoke_devices_for_fingerprint;

    std::function<std::string()> now_iso_utc;
    std::function<void(const pqnas::AuditEvent&)> audit_append;
};

void register_admin_user_status_routes(
    httplib::Server& srv,
    const AdminUserStatusRoutesContext& ctx
);
