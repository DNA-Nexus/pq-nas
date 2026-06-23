#include "routes_apps_public.h"

#include "httplib.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

using json = nlohmann::json;

namespace {

bool context_ok(const AppsPublicRoutesContext& c) {
    return !c.apps_installed_dir.empty() &&
           c.load_app_launch_policy_json &&
           c.is_admin_cookie &&
           c.serve_file_under_root;
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

bool app_is_admin_only(const AppsPublicRoutesContext& c, const std::string& app_id) {
    try {
        json pol = c.load_app_launch_policy_json();
        if (pol.contains("by_app_id") &&
            pol["by_app_id"].is_object() &&
            pol["by_app_id"].contains(app_id) &&
            pol["by_app_id"][app_id].is_object()) {
            const json& entry = pol["by_app_id"][app_id];
            return entry.contains("admin_only") &&
                   entry["admin_only"].is_boolean() &&
                   entry["admin_only"].get<bool>();
        }
    } catch (...) {
    }

    return false;
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
                res.status = 500;
                res.set_header("Cache-Control", "no-store");
                res.set_content(
                    "{\"ok\":false,\"error\":\"server_error\",\"message\":\"apps public route context incomplete\"}",
                    "application/json; charset=utf-8"
                );
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
}
