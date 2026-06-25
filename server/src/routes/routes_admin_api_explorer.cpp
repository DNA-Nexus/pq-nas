#include "routes_admin_api_explorer.h"

#include "httplib.h"

#include <nlohmann/json.hpp>

#include <string>

using json = nlohmann::json;

namespace {

bool context_ok(const AdminApiExplorerRoutesContext& c) {
    return !c.html_path.empty() &&
           !c.js_path.empty() &&
           !c.catalog_path.empty() &&
           c.require_admin &&
           c.slurp_file &&
           c.reply_json;
}

void reply_context_error(const AdminApiExplorerRoutesContext& c, httplib::Response& res) {
    if (c.reply_json) {
        c.reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "admin api explorer route context incomplete"}
        }.dump());
        return;
    }

    res.status = 500;
    res.set_header("Cache-Control", "no-store");
    res.set_content(
        "{\"ok\":false,\"error\":\"server_error\",\"message\":\"admin api explorer route context incomplete\"}",
        "application/json; charset=utf-8"
    );
}

} // namespace

void register_admin_api_explorer_routes(
    httplib::Server& srv,
    const AdminApiExplorerRoutesContext& ctx
) {
    const AdminApiExplorerRoutesContext c = ctx;

    srv.Get("/admin/api-explorer",
        [c](const httplib::Request& req, httplib::Response& res) {
            if (!context_ok(c)) {
                reply_context_error(c, res);
                return;
            }

            std::string actor_fp;
            if (!c.require_admin(req, res, &actor_fp)) return;

            const std::string body = c.slurp_file(c.html_path);
            if (body.empty()) {
                res.status = 404;
                res.set_header("Cache-Control", "no-store");
                res.set_content("missing admin_api_explorer.html", "text/plain; charset=utf-8");
                return;
            }

            res.set_header("Cache-Control", "no-store");
            res.set_content(body, "text/html; charset=utf-8");
        }
    );

    srv.Get("/static/admin_api_explorer.js",
        [c](const httplib::Request& req, httplib::Response& res) {
            if (!context_ok(c)) {
                reply_context_error(c, res);
                return;
            }

            std::string actor_fp;
            if (!c.require_admin(req, res, &actor_fp)) return;

            const std::string body = c.slurp_file(c.js_path);
            if (body.empty()) {
                res.status = 404;
                res.set_header("Cache-Control", "no-store");
                res.set_content("missing admin_api_explorer.js", "text/plain; charset=utf-8");
                return;
            }

            res.set_header("Cache-Control", "no-store");
            res.set_content(body, "application/javascript; charset=utf-8");
        }
    );

    srv.Get("/api/v4/admin/api-explorer/routes",
        [c](const httplib::Request& req, httplib::Response& res) {
            if (!context_ok(c)) {
                reply_context_error(c, res);
                return;
            }

            std::string actor_fp;
            if (!c.require_admin(req, res, &actor_fp)) return;

            const std::string body = c.slurp_file(c.catalog_path);
            if (body.empty()) {
                c.reply_json(res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "missing admin API catalog"}
                }.dump());
                return;
            }

            json parsed = json::parse(body, nullptr, false);
            if (parsed.is_discarded() || !parsed.is_object()) {
                c.reply_json(res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "invalid admin API catalog JSON"}
                }.dump());
                return;
            }

            res.set_header("Cache-Control", "no-store");
            res.set_content(body, "application/json; charset=utf-8");
        }
    );
}
