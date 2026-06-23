#include "routes_core_ui_shell.h"

#include "httplib.h"

#include <string>

namespace {

bool context_ok(const CoreUiShellRoutesContext& c) {
    return !c.static_system_js.empty() &&
           !c.static_audit_html.empty() &&
           !c.static_admin_html.empty() &&
           !c.static_app_js.empty() &&
           !c.static_admin_js.empty() &&
           c.require_admin &&
           c.read_file_to_string &&
           c.slurp_file;
}

void reply_context_error(httplib::Response& res) {
    res.status = 500;
    res.set_header("Cache-Control", "no-store");
    res.set_content(
        "{\"ok\":false,\"error\":\"server_error\",\"message\":\"core UI shell route context incomplete\"}",
        "application/json; charset=utf-8"
    );
}

} // namespace

void register_core_ui_shell_routes(
    httplib::Server& srv,
    const CoreUiShellRoutesContext& ctx
) {
    const CoreUiShellRoutesContext c = ctx;

    srv.Get("/static/system.js",
        [c](const httplib::Request&, httplib::Response& res) {
            if (!context_ok(c)) {
                reply_context_error(res);
                return;
            }

            std::string body;
            if (!c.read_file_to_string(c.static_system_js, body) || body.empty()) {
                res.status = 404;
                res.set_header("Content-Type", "text/plain");
                res.body = "Missing static file: " + c.static_system_js;
                return;
            }

            res.status = 200;
            res.set_header("Content-Type", "application/javascript; charset=utf-8");
            res.body = body;
        }
    );

    srv.Get("/admin/audit",
        [c](const httplib::Request& req, httplib::Response& res) {
            if (!context_ok(c)) {
                reply_context_error(res);
                return;
            }

            std::string actor_fp;
            if (!c.require_admin(req, res, &actor_fp)) return;

            res.set_header("Cache-Control", "no-store");
            res.set_content(c.slurp_file(c.static_audit_html), "text/html; charset=utf-8");
        }
    );

    srv.Get("/admin",
        [c](const httplib::Request& req, httplib::Response& res) {
            if (!context_ok(c)) {
                reply_context_error(res);
                return;
            }

            std::string actor_fp;
            if (!c.require_admin(req, res, &actor_fp)) return;

            const std::string body = c.slurp_file(c.static_admin_html);
            if (body.empty()) {
                res.status = 404;
                res.set_content("missing admin.html", "text/plain");
                return;
            }

            res.set_header("Cache-Control", "no-store");
            res.set_content(body, "text/html; charset=utf-8");
        }
    );

    srv.Get("/static/app.js",
        [c](const httplib::Request&, httplib::Response& res) {
            if (!context_ok(c)) {
                reply_context_error(res);
                return;
            }

            const std::string body = c.slurp_file(c.static_app_js);
            if (body.empty()) {
                res.status = 404;
                res.set_content("missing app.js", "text/plain");
                return;
            }

            res.set_content(body, "application/javascript; charset=utf-8");
        }
    );

    srv.Get("/static/admin.js",
        [c](const httplib::Request&, httplib::Response& res) {
            if (!context_ok(c)) {
                reply_context_error(res);
                return;
            }

            const std::string body = c.slurp_file(c.static_admin_js);
            if (body.empty()) {
                res.status = 404;
                res.set_content("missing admin.js", "text/plain");
                return;
            }

            res.set_header("Cache-Control", "no-store");
            res.set_content(body, "application/javascript; charset=utf-8");
        }
    );
}
