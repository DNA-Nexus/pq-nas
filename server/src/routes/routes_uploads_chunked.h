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
class FileVersionsIndex;
class UsersRegistry;
struct AuditEvent;
struct QuotaCheckResult;
}

struct ChunkedUploadTieringConfig {
    bool enabled = false;
    std::string landing_pool_id;
};

struct ChunkedUploadRoutesContext {
    pqnas::UsersRegistry* users = nullptr;
    pqnas::FileVersionsIndex* file_versions = nullptr;

    std::filesystem::path users_path;
    const unsigned char* cookie_key = nullptr;

    std::function<bool(const httplib::Request&, httplib::Response&, std::string*, std::string*)> require_user_auth;
    std::function<bool(const httplib::Request&, httplib::Response&)> require_same_origin;
    std::function<bool(httplib::Response&, const std::string&, const std::string&, const std::string&, bool)> require_no_live_lock_for_write;

    std::function<void(httplib::Response&, int, const std::string&)> reply_json;
    std::function<bool(httplib::Response&, const std::string&, const pqnas::QuotaCheckResult&)> reply_quota_error;

    std::function<std::filesystem::path(const std::string&)> user_dir_for_fp;
    std::function<std::string(std::size_t)> random_b64url;
    std::function<std::int64_t()> now_epoch_sec;

    std::function<void(const pqnas::AuditEvent&)> audit_append;

    std::function<ChunkedUploadTieringConfig()> upload_tiering_config;
    std::function<bool(const std::string&, const std::string&, const std::string&, std::filesystem::path*, std::string*)> build_landing_abs_path;
    std::function<bool(const std::filesystem::path&, std::string*)> ensure_no_symlink_in_existing_path_prefix;

    std::function<void(const std::string& fp_hex,
                       const std::string& event_name,
                       const std::string& logical_rel_path,
                       const std::string& item_type,
                       const std::string& aux,
                       std::uint64_t bytes,
                       int count,
                       const httplib::Request* req)> record_user_file_activity;
};

void register_chunked_upload_routes(
    httplib::Server& srv,
    const ChunkedUploadRoutesContext& ctx
);
