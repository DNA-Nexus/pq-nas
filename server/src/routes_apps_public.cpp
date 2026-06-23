#include "routes_apps_public.h"

#include "httplib.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

using json = nlohmann::json;

namespace {

bool context_ok(const AppsPublicRoutesContext& c) {
    return !c.apps_installed_dir.empty() &&
           !c.apps_bundled_dir.empty() &&
           !c.server_version.empty() &&
           c.load_app_launch_policy_json &&
           c.is_admin_cookie &&
           c.serve_file_under_root &&
           c.read_file_to_string &&
           c.rel_to_repo;
}

void reply_context_error(httplib::Response& res) {
    res.status = 500;
    res.set_header("Cache-Control", "no-store");
    res.set_content(
        "{\"ok\":false,\"error\":\"server_error\",\"message\":\"apps public route context incomplete\"}",
        "application/json; charset=utf-8"
    );
}

std::string app_asset_content_type(const std::string& p) {
    if (p.size() >= 5 && p.substr(p.size() - 5) == ".html") return "text/html; charset=utf-8";
    if (p.size() >= 3 && p.substr(p.size() - 3) == ".js") return "application/javascript; charset=utf-8";
    if (p.size() >= 4 && p.substr(p.size() - 4) == ".css") return "text/css; charset=utf-8";
    if (p.size() >= 4 && p.substr(p.size() - 4) == ".png") return "image/png";
    if (p.size() >= 4 && p.substr(p.size() - 4) == ".svg") return "image/svg+xml";
    if (p.size() >= 4 && p.substr(p.size() - 4) == ".jpg") return "image/jpeg";
    if (p.size() >= 5 && p.substr(p.size() - 5) == ".jpeg") return "image/jpeg";
    if (p.size() >= 5 && p.substr(p.size() - 5) == ".webp") return "image/webp";
    return "application/octet-stream";
}

bool app_is_admin_only_from_policy(const json& app_launch_policy_by_id, const std::string& app_id) {
    try {
        if (!app_launch_policy_by_id.contains(app_id) ||
            !app_launch_policy_by_id[app_id].is_object()) {
            return false;
        }

        const json& entry = app_launch_policy_by_id[app_id];
        return entry.contains("admin_only") &&
               entry["admin_only"].is_boolean() &&
               entry["admin_only"].get<bool>();
    } catch (...) {
        return false;
    }
}

bool app_is_admin_only(const AppsPublicRoutesContext& c, const std::string& app_id) {
    try {
        json pol = c.load_app_launch_policy_json();
        if (pol.contains("by_app_id") &&
            pol["by_app_id"].is_object()) {
            return app_is_admin_only_from_policy(pol["by_app_id"], app_id);
        }
    } catch (...) {
    }

    return false;
}

long long app_version_component(const std::string& v, std::size_t& pos) {
    while (pos < v.size() && v[pos] == '.') ++pos;

    long long n = 0;
    bool any = false;

    while (pos < v.size() && v[pos] >= '0' && v[pos] <= '9') {
        any = true;
        n = (n * 10) + (v[pos] - '0');
        ++pos;
    }

    while (pos < v.size() && v[pos] != '.') ++pos;
    if (pos < v.size() && v[pos] == '.') ++pos;

    return any ? n : 0;
}

int compare_app_versions(const std::string& a, const std::string& b) {
    std::size_t ia = 0;
    std::size_t ib = 0;

    for (int i = 0; i < 4; ++i) {
        const long long av = app_version_component(a, ia);
        const long long bv = app_version_component(b, ib);

        if (av < bv) return -1;
        if (av > bv) return 1;
    }

    return 0;
}

std::string app_manifest_min_server_version(const json& mani) {
    try {
        if (mani.is_object() &&
            mani.contains("min_server_version") &&
            mani["min_server_version"].is_string()) {
            return mani["min_server_version"].get<std::string>();
        }
    } catch (...) {
    }

    return "";
}

bool app_server_version_ok(
    const AppsPublicRoutesContext& c,
    const std::string& min_server_version
) {
    if (min_server_version.empty()) return true;
    return compare_app_versions(c.server_version, min_server_version) >= 0;
}

std::string app_compatibility_message(
    const AppsPublicRoutesContext& c,
    const std::string& min_server_version
) {
    if (min_server_version.empty()) return "";

    return std::string("Requires DNA-Nexus Server ") +
           min_server_version +
           " or newer. Current server is " +
           c.server_version +
           ".";
}

void apply_app_compatibility_fields(
    const AppsPublicRoutesContext& c,
    json& item,
    const json& mani
) {
    const std::string min_server_version = app_manifest_min_server_version(mani);
    const bool ok = app_server_version_ok(c, min_server_version);

    item["server_version"] = c.server_version;
    item["min_server_version"] = min_server_version;
    item["compatibility_ok"] = ok;
    item["compatibility_state"] = ok ? "ok" : "server_too_old";
    item["compatibility_message"] = ok ? "" : app_compatibility_message(c, min_server_version);
}

} // namespace

void register_apps_public_routes(
    httplib::Server& srv,
    const AppsPublicRoutesContext& ctx
) {
    const AppsPublicRoutesContext c = ctx;

    srv.Get(R"(/apps/([A-Za-z0-9_.-]+)/([A-Za-z0-9_.-]+)/(.*))",
        [c](const httplib::Request& req, httplib::Response& res) {
            if (!context_ok(c)) {
                reply_context_error(res);
                return;
            }

            const std::string app_id = req.matches[1];
            const std::string ver = req.matches[2];
            const std::string tail = req.matches[3];

            if (app_is_admin_only(c, app_id) && !c.is_admin_cookie(req)) {
                res.status = 403;
                res.set_header("Cache-Control", "no-store");
                res.set_content(
                    R"({"ok":false,"error":"forbidden","message":"Admin-only app"})",
                    "application/json; charset=utf-8"
                );
                return;
            }

            const std::string root =
                (std::filesystem::path(c.apps_installed_dir) / app_id / ver).string();

            c.serve_file_under_root(
                root,
                tail,
                app_asset_content_type(tail),
                res,
                true
            );
        }
    );

    srv.Get("/api/v4/apps",
        [c](const httplib::Request& req, httplib::Response& res) {
            if (!context_ok(c)) {
                reply_context_error(res);
                return;
            }

            namespace fs = std::filesystem;

            json out;
            out["ok"] = true;
            out["bundled"] = json::array();
            out["installed"] = json::array();
            out["launch_policy_by_app_id"] = json::object();

            const bool is_admin = c.is_admin_cookie(req);

            json app_launch_policy = c.load_app_launch_policy_json();
            json app_launch_policy_by_id = json::object();
            if (app_launch_policy.contains("by_app_id") &&
                app_launch_policy["by_app_id"].is_object()) {
                app_launch_policy_by_id = app_launch_policy["by_app_id"];
            }

            auto app_admin_only = [&](const std::string& app_id) -> bool {
                return app_is_admin_only_from_policy(app_launch_policy_by_id, app_id);
            };

            if (is_admin) {
                std::error_code ec;
                fs::path bundled(c.apps_bundled_dir);
                if (fs::exists(bundled, ec) && fs::is_directory(bundled, ec)) {
                    for (auto& de : fs::directory_iterator(bundled, ec)) {
                        if (ec) break;
                        if (!de.is_directory(ec) || ec) continue;

                        const std::string app_id = de.path().filename().string();

                        for (auto& f : fs::directory_iterator(de.path(), ec)) {
                            if (ec) break;
                            if (!f.is_regular_file(ec) || ec) continue;
                            if (f.path().extension() != ".zip") continue;

                            json item;
                            item["id"] = app_id;
                            item["zip"] = c.rel_to_repo(f.path().string());
                            out["bundled"].push_back(item);
                        }
                    }
                }
            }

            {
                std::error_code ec;
                fs::path installed(c.apps_installed_dir);
                if (fs::exists(installed, ec) && fs::is_directory(installed, ec)) {
                    for (auto& de_app : fs::directory_iterator(installed, ec)) {
                        if (ec) break;
                        if (!de_app.is_directory(ec) || ec) continue;

                        const std::string app_id = de_app.path().filename().string();

                        if (app_admin_only(app_id) && !is_admin) {
                            continue;
                        }

                        for (auto& de_ver : fs::directory_iterator(de_app.path(), ec)) {
                            if (ec) break;
                            if (!de_ver.is_directory(ec) || ec) continue;

                            const std::string ver = de_ver.path().filename().string();
                            fs::path root = de_ver.path();

                            fs::path manifest = root / "manifest.json";
                            fs::path default_entry = root / "www" / "index.html";

                            if (!fs::exists(manifest, ec) && !fs::exists(default_entry, ec)) {
                                continue;
                            }

                            const bool has_manifest = fs::exists(manifest, ec) && !ec;

                            json mj = json::object();
                            if (has_manifest) {
                                std::string mb;
                                if (c.read_file_to_string(manifest.string(), mb) && !mb.empty()) {
                                    try {
                                        mj = json::parse(mb);
                                    } catch (...) {
                                        mj = json::object();
                                    }
                                }
                            }

                            json item;
                            item["id"] = app_id;
                            item["version"] = ver;
                            item["root"] = c.rel_to_repo(root.string());
                            item["has_manifest"] = has_manifest;
                            apply_app_compatibility_fields(c, item, mj);
                            out["installed"].push_back(item);
                        }
                    }
                }
            }

            for (auto it = app_launch_policy_by_id.begin();
                 it != app_launch_policy_by_id.end();
                 ++it) {
                if (is_admin || !app_admin_only(it.key())) {
                    out["launch_policy_by_app_id"][it.key()] = it.value();
                }
            }

            res.set_header("Cache-Control", "no-store");
            res.set_content(out.dump(2), "application/json; charset=utf-8");
        }
    );
}
