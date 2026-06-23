#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace httplib {
class Server;
struct Request;
struct Response;
}

namespace pqnas {
class UsersRegistry;
class WorkspacesRegistry;
}

struct AdminUserStoragePreviewRoutesContext {
    pqnas::UsersRegistry* users = nullptr;
    pqnas::WorkspacesRegistry* workspaces = nullptr;

    std::string users_path;
    std::string workspaces_path;

    std::function<bool(const httplib::Request&, httplib::Response&, std::string*)> require_admin_cookie;
    std::function<void(httplib::Response&, int, const std::string&)> reply_json;

    std::function<std::string(const std::string&)> trim_copy;
    std::function<bool(const std::string&)> is_valid_fingerprint_hex;
    std::function<std::string(const std::string&)> normalize_storage_pool_id;

    std::function<bool(const std::string&, std::string*, std::string*)> storage_pool_mount_by_id;
    std::function<std::string()> data_root_dir;
    std::function<std::string(const std::string&)> default_root_rel_for_fp;
    std::function<bool(const std::string&)> is_safe_rel_path;

    std::function<bool(const std::string&, std::uint64_t*, std::uint64_t*)> statvfs_path;
    std::function<std::uint64_t(const std::filesystem::path&)> dir_size_bytes_best_effort;
    std::function<std::uint64_t(const std::string&, const std::string&)> sum_allocated_workspace_quota_on_pool;
};

void register_admin_user_storage_preview_routes(
    httplib::Server& srv,
    const AdminUserStoragePreviewRoutesContext& ctx
);
