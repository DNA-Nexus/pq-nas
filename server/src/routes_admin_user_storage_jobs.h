#pragma once

#include <functional>
#include <string>

#include <nlohmann/json.hpp>

namespace httplib {
class Server;
struct Request;
struct Response;
}

namespace pqnas {
class UsersRegistry;
struct AuditEvent;
}

struct AdminUserStorageJobsRoutesContext {
    pqnas::UsersRegistry* users = nullptr;
    std::string users_path;

    std::function<bool(const httplib::Request&, httplib::Response&, std::string*)> require_admin;
    std::function<bool(const httplib::Request&, httplib::Response&)> require_same_origin;
    std::function<void(httplib::Response&, int, const std::string&)> reply_json;
    std::function<void(const pqnas::AuditEvent&)> audit_append;

    std::function<nlohmann::json(
        const std::string& actor_fp,
        const std::string& fp,
        const std::string& pool_id,
        const std::string& remote_addr
    )> enqueue_migration_job;

    std::function<bool(
        const std::string& job_id,
        nlohmann::json* out,
        std::string* err
    )> read_migration_record;

    std::function<nlohmann::json(
        const std::string& actor_fp,
        const std::string& fp,
        const std::string& expected_active_pool_id,
        const std::string& old_pool_id,
        const std::string& remote_addr
    )> enqueue_cleanup_job;

    std::function<bool(
        const std::string& job_id,
        nlohmann::json* out,
        std::string* err
    )> read_cleanup_record;
};

void register_admin_user_storage_jobs_routes(
    httplib::Server& srv,
    const AdminUserStorageJobsRoutesContext& ctx
);
