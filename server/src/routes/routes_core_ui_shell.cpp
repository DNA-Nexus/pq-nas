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
           !c.static_handoff_director_js.empty() &&
           !c.static_handoff_storage_bootstrap_js.empty() &&
           !c.static_handoff_storage_finish_js.empty() &&
           c.require_admin &&
           c.require_user &&
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

constexpr const char*
    kHandoffDirectorHtml = R"HTML(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="referrer" content="no-referrer">
  <meta name="robots" content="noindex,nofollow">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>DNA Nexus - Connecting storage</title>
</head>
<body>
  <main>
    <h1>Connecting to storage</h1>
    <p id="handoff-status" role="status" aria-live="polite">Preparing secure storage session...</p>
  </main>
  <script src="/static/handoff/director.js" defer></script>
</body>
</html>
)HTML";

constexpr const char*
    kHandoffStorageBootstrapHtml = R"HTML(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="referrer" content="no-referrer">
  <meta name="robots" content="noindex,nofollow">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>DNA Nexus - Preparing storage</title>
</head>
<body>
  <main>
    <h1>Preparing storage</h1>
    <p id="handoff-status" role="status" aria-live="polite">Preparing secure browser binding...</p>
  </main>
  <script src="/static/handoff/storage_bootstrap.js" defer></script>
</body>
</html>
)HTML";

constexpr const char*
    kHandoffStorageFinishHtml = R"HTML(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="referrer" content="no-referrer">
  <meta name="robots" content="noindex,nofollow">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>DNA Nexus - Opening storage</title>
</head>
<body>
  <main>
    <h1>Opening storage</h1>
    <p id="handoff-status" role="status" aria-live="polite">Creating secure storage session...</p>
  </main>
  <script src="/static/handoff/storage_finish.js" defer></script>
</body>
</html>
)HTML";


void set_handoff_security_headers(
    httplib::Response& res
) {
    res.set_header(
        "Cache-Control",
        "no-store"
    );
    res.set_header(
        "Pragma",
        "no-cache"
    );
    res.set_header(
        "X-Content-Type-Options",
        "nosniff"
    );
    res.set_header(
        "Referrer-Policy",
        "no-referrer"
    );
    res.set_header(
        "X-Frame-Options",
        "DENY"
    );
    res.set_header(
        "Cross-Origin-Opener-Policy",
        "same-origin"
    );
    res.set_header(
        "Cross-Origin-Resource-Policy",
        "same-origin"
    );

    // Security: handoff pages need only same-origin JavaScript and API calls.
    // No inline script, forms, frames, remote assets or base-URL rewriting.
    res.set_header(
        "Content-Security-Policy",
        "default-src 'none'; "
        "script-src 'self'; "
        "connect-src 'self'; "
        "frame-ancestors 'none'; "
        "base-uri 'none'; "
        "form-action 'none'; "
        "object-src 'none'"
    );
}


void reply_handoff_not_found(
    httplib::Response& res
) {
    set_handoff_security_headers(res);
    res.status = 404;
    res.set_content(
        "not found",
        "text/plain; charset=utf-8"
    );
}


void reply_handoff_html(
    httplib::Response& res,
    const char* body
) {
    set_handoff_security_headers(res);
    res.status = 200;
    res.set_content(
        body ? body : "",
        "text/html; charset=utf-8"
    );
}


void reply_handoff_js(
    httplib::Response& res,
    const CoreUiShellRoutesContext& c,
    const std::string& path
) {
    std::string body;

    if (!c.read_file_to_string(
            path,
            body) ||
        body.empty()) {
        set_handoff_security_headers(res);
        res.status = 404;
        res.set_content(
            "not found",
            "text/plain; charset=utf-8"
        );
        return;
    }

    set_handoff_security_headers(res);
    res.status = 200;
    res.set_content(
        body,
        "application/javascript; charset=utf-8"
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

        // Security: the Director remains a control plane. A normal user must
        // authenticate before entering the browser handoff and must never
        // receive the local NAS desktop from the Director.
        std::string actor_fp;

        if (!c.require_user(
                req,
                res,
                &actor_fp)) {
            return;
        }

        res.status = 302;
        res.set_header("Cache-Control", "no-store");
        res.set_header("Location", "/storage/handoff/director");
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


void handle_core_ui_handoff_director(
    const httplib::Request& req,
    httplib::Response& res,
    const CoreUiShellRoutesContext& c
) {
    if (!context_ok(c)) {
        reply_context_error(res);
        return;
    }

    if (!c.is_director) {
        reply_handoff_not_found(res);
        return;
    }

    std::string actor_fp;

    if (!c.require_user(
            req,
            res,
            &actor_fp)) {
        return;
    }

    reply_handoff_html(
        res,
        kHandoffDirectorHtml
    );
}


void handle_core_ui_handoff_storage_bootstrap(
    const httplib::Request&,
    httplib::Response& res,
    const CoreUiShellRoutesContext& c
) {
    if (!context_ok(c)) {
        reply_context_error(res);
        return;
    }

    if (!c.is_storage_node) {
        reply_handoff_not_found(res);
        return;
    }

    reply_handoff_html(
        res,
        kHandoffStorageBootstrapHtml
    );
}


void handle_core_ui_handoff_storage_finish(
    const httplib::Request&,
    httplib::Response& res,
    const CoreUiShellRoutesContext& c
) {
    if (!context_ok(c)) {
        reply_context_error(res);
        return;
    }

    if (!c.is_storage_node) {
        reply_handoff_not_found(res);
        return;
    }

    reply_handoff_html(
        res,
        kHandoffStorageFinishHtml
    );
}


void register_core_ui_shell_routes(
    httplib::Server& srv,
    const CoreUiShellRoutesContext& ctx
) {
    const CoreUiShellRoutesContext c = ctx;

    srv.Get(
        "/storage/handoff/director",
        [c](
            const httplib::Request& req,
            httplib::Response& res
        ) {
            handle_core_ui_handoff_director(
                req,
                res,
                c
            );
        }
    );

    srv.Get(
        "/storage/handoff/bootstrap",
        [c](
            const httplib::Request& req,
            httplib::Response& res
        ) {
            handle_core_ui_handoff_storage_bootstrap(
                req,
                res,
                c
            );
        }
    );

    srv.Get(
        "/storage/handoff/finish",
        [c](
            const httplib::Request& req,
            httplib::Response& res
        ) {
            handle_core_ui_handoff_storage_finish(
                req,
                res,
                c
            );
        }
    );

    srv.Get(
        "/static/handoff/director.js",
        [c](
            const httplib::Request&,
            httplib::Response& res
        ) {
            if (!context_ok(c)) {
                reply_context_error(res);
                return;
            }

            if (!c.is_director) {
                reply_handoff_not_found(res);
                return;
            }

            reply_handoff_js(
                res,
                c,
                c.static_handoff_director_js
            );
        }
    );

    srv.Get(
        "/static/handoff/storage_bootstrap.js",
        [c](
            const httplib::Request&,
            httplib::Response& res
        ) {
            if (!context_ok(c)) {
                reply_context_error(res);
                return;
            }

            if (!c.is_storage_node) {
                reply_handoff_not_found(res);
                return;
            }

            reply_handoff_js(
                res,
                c,
                c.static_handoff_storage_bootstrap_js
            );
        }
    );

    srv.Get(
        "/static/handoff/storage_finish.js",
        [c](
            const httplib::Request&,
            httplib::Response& res
        ) {
            if (!context_ok(c)) {
                reply_context_error(res);
                return;
            }

            if (!c.is_storage_node) {
                reply_handoff_not_found(res);
                return;
            }

            reply_handoff_js(
                res,
                c,
                c.static_handoff_storage_finish_js
            );
        }
    );

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
