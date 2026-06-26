#pragma once

#include <filesystem>
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

struct AppsManageRoutesContext {
    pqnas::UsersRegistry* users = nullptr;
    const unsigned char* cookie_key = nullptr;

    std::string apps_installed_dir;
    std::string apps_bundled_dir;
    std::string server_version;

    std::function<bool(const httplib::Request&, httplib::Response&, std::string*, std::string*)> require_user_auth;
    std::function<bool(const httplib::Request&, httplib::Response&)> require_admin_cookie;
    std::function<bool(const httplib::Request&, httplib::Response&)> require_same_origin;
    std::function<bool(const httplib::Request&)> is_admin_cookie;

    std::function<bool(const std::string&)> safe_app_id;
    std::function<bool(const std::string&)> safe_app_ver;

    std::function<nlohmann::json()> load_app_launch_policy_json;
    std::function<bool(const nlohmann::json&)> save_app_launch_policy_json;
    std::function<nlohmann::json(const nlohmann::json&)> normalize_app_launch_policy_json;
    std::function<nlohmann::json(const nlohmann::json&)> normalize_app_launch_policy_entry;
    std::function<nlohmann::json()> app_launch_policy_defaults_json;

    std::function<bool(const std::string&, std::string&)> read_file_to_string;
    std::function<long long(const std::string&)> file_size_bytes_safe;
    std::function<bool(const std::filesystem::path&, std::string*, std::string*)> sha256_file;
    std::function<void(nlohmann::json&, const nlohmann::json&)> apply_app_compatibility_fields;

    std::function<std::string()> rand_hex_16;
    std::function<bool(const std::string&, std::string*, int*)> run_cmd_capture;

    std::function<std::string(const nlohmann::json&)> app_manifest_min_server_version;
    std::function<bool(const std::string&)> app_server_version_ok;
    std::function<std::string(const std::string&)> app_compatibility_message;

    std::function<bool(const std::string&)> app_launch_value_ok;
    std::function<bool(const std::string&)> app_window_profile_ok;

    std::function<std::string(const std::string&)> rel_to_repo;
    std::function<std::string(const httplib::Request&)> client_ip;
    std::function<std::string()> now_iso_utc;
    std::function<void(const pqnas::AuditEvent&)> audit_append;
};

void register_apps_manage_routes(
    httplib::Server& srv,
    const AppsManageRoutesContext& ctx
);
