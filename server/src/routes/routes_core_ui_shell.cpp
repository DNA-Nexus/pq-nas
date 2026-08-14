#include "routes_core_ui_shell.h"

#include "httplib.h"

#include <string>

namespace {

bool context_ok(const CoreUiShellRoutesContext& c) {
    return !c.static_system_js.empty() &&
           !c.static_audit_html.empty() &&
           !c.static_admin_html.empty() &&
           !c.static_director_admin_html.empty() &&
           !c.static_director_admin_css.empty() &&
           !c.static_director_admin_js.empty() &&
           !c.static_app_html.empty() &&
           !c.static_app_js.empty() &&
           !c.static_app_frame_background_tasks_js.empty() &&
           !c.static_admin_js.empty() &&
           c.require_admin &&
           c.is_admin &&
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

void handle_core_ui_admin(
    const httplib::Request& req,
    httplib::Response& res,
    const CoreUiShellRoutesContext& c
) {
    if (!context_ok(c)) {
        reply_context_error(res);
        return;
    }

    std::string actor_fp;
    if (!c.require_admin(req, res, &actor_fp)) {
        return;
    }

    // Security: the server role selects between fixed server-owned
    // UI paths. Request data never controls the filesystem path.
    const std::string& admin_html =
        c.is_director
            ? c.static_director_admin_html
            : c.static_admin_html;

    const std::string body =
        c.slurp_file(admin_html);

    if (body.empty()) {
        res.status = 404;
        res.set_content(
            "missing admin UI",
            "text/plain; charset=utf-8"
        );
        return;
    }

    res.status = 200;
    res.set_header("Cache-Control", "no-store");
    res.set_header("X-Content-Type-Options", "nosniff");
    res.set_content(
        body,
        "text/html; charset=utf-8"
    );
}

void handle_core_ui_app(
    const httplib::Request& req,
    httplib::Response& res,
    const CoreUiShellRoutesContext& c
) {
    if (!context_ok(c)) {
        reply_context_error(res);
        return;
    }

    if (c.is_director) {
        if (c.is_admin(req)) {
            // Security: redirect only a server-verified administrator.
            // A normal Director user must never be mistaken for an admin.
            res.status = 302;
            res.set_header("Cache-Control", "no-store");
            res.set_header("Location", "/admin");
            return;
        }

        // Security: Director is a control plane and must never fall back to
        // the ordinary NAS desktop. User-to-Storage handoff will replace
        // this temporary fail-closed response when that flow is implemented.
        res.status = 503;
        res.set_header("Cache-Control", "no-store");
        res.set_header("X-Content-Type-Options", "nosniff");
        res.set_content(
            "Storage routing is not available yet.",
            "text/plain; charset=utf-8"
        );
        return;
    }

    const std::string body =
        c.slurp_file(c.static_app_html);

    if (body.empty()) {
        res.status = 404;
        res.set_content(
            "missing app.html",
            "text/plain; charset=utf-8"
        );
        return;
    }

    res.status = 200;
    res.set_header("Cache-Control", "no-store");
    res.set_header("X-Content-Type-Options", "nosniff");
    res.set_content(
        body,
        "text/html; charset=utf-8"
    );
}

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
            handle_core_ui_admin(
                req,
                res,
                c
            );
        }
    );

    srv.Get("/app",
        [c](const httplib::Request& req, httplib::Response& res) {
            handle_core_ui_app(
                req,
                res,
                c
            );
        }
    );

    srv.Get("/static/director/admin.css",
        [c](const httplib::Request&, httplib::Response& res) {
            if (!context_ok(c)) {
                reply_context_error(res);
                return;
            }

            std::string body;

            // Security: serve only the fixed Director stylesheet path from
            // server configuration. Request input never selects a file.
            if (!c.read_file_to_string(
                    c.static_director_admin_css,
                    body
                ) || body.empty()) {
                res.status = 404;
                res.set_header("Cache-Control", "no-store");
                res.set_content(
                    "missing Director stylesheet",
                    "text/plain; charset=utf-8"
                );
                return;
            }

            res.status = 200;
            res.set_header("Cache-Control", "no-store");
            res.set_header("X-Content-Type-Options", "nosniff");
            res.set_content(
                body,
                "text/css; charset=utf-8"
            );
        }
    );

    srv.Get("/static/director/admin.js",
        [c](const httplib::Request&, httplib::Response& res) {
            if (!context_ok(c)) {
                reply_context_error(res);
                return;
            }

            std::string body;

            // Security: serve only the fixed Director script path from
            // server configuration. Request input never selects a file.
            if (!c.read_file_to_string(
                    c.static_director_admin_js,
                    body
                ) || body.empty()) {
                res.status = 404;
                res.set_header("Cache-Control", "no-store");
                res.set_content(
                    "missing Director script",
                    "text/plain; charset=utf-8"
                );
                return;
            }

            res.status = 200;
            res.set_header("Cache-Control", "no-store");
            res.set_header("X-Content-Type-Options", "nosniff");
            res.set_content(
                body,
                "application/javascript; charset=utf-8"
            );
        }
    );

    srv.Get("/static/app_frame_background_tasks.js",
        [c](const httplib::Request&, httplib::Response& res) {
            if (!context_ok(c)) {
                reply_context_error(res);
                return;
            }

            std::string body;

            // Security: serve only the fixed server-owned path from the route
            // context. Request data never selects a filesystem path.
            if (!c.read_file_to_string(
                    c.static_app_frame_background_tasks_js,
                    body
                ) || body.empty()) {
                res.status = 404;
                res.set_header("Cache-Control", "no-store");
                res.set_content(
                    "missing app_frame_background_tasks.js",
                    "text/plain; charset=utf-8"
                );
                return;
            }

            res.status = 200;
            res.set_header("Cache-Control", "no-store");
            res.set_content(
                body,
                "application/javascript; charset=utf-8"
            );
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
