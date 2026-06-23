#include "routes_admin_approvals_ui.h"

#include "httplib.h"

#include <string>

namespace {

bool context_ok(const AdminApprovalsUiRoutesContext& c) {
    return !c.approvals_html_path.empty() &&
           !c.approvals_js_path.empty() &&
           c.require_admin &&
           c.slurp_file;
}

void reply_context_error(httplib::Response& res) {
    res.status = 500;
    res.set_header("Cache-Control", "no-store");
    res.set_content(
        "{\"ok\":false,\"error\":\"server_error\",\"message\":\"admin approvals ui route context incomplete\"}",
        "application/json; charset=utf-8"
    );
}

} // namespace

void register_admin_approvals_ui_routes(
    httplib::Server& srv,
    const AdminApprovalsUiRoutesContext& ctx
) {
    const AdminApprovalsUiRoutesContext c = ctx;

    srv.Get("/admin/approvals",
        [c](const httplib::Request& req, httplib::Response& res) {
            if (!context_ok(c)) {
                reply_context_error(res);
                return;
            }

            std::string actor_fp;
            if (!c.require_admin(req, res, &actor_fp)) return;

            const std::string body = c.slurp_file(c.approvals_html_path);
            if (body.empty()) {
                res.status = 404;
                res.set_content("missing admin_approvals.html", "text/plain");
                return;
            }

            res.set_header("Cache-Control", "no-store");
            res.set_content(body, "text/html; charset=utf-8");
        }
    );

    srv.Get("/static/admin_approvals.js",
        [c](const httplib::Request&, httplib::Response& res) {
            if (!context_ok(c)) {
                reply_context_error(res);
                return;
            }

            const std::string body = c.slurp_file(c.approvals_js_path);
            if (body.empty()) {
                res.status = 404;
                res.set_content("missing admin_approvals.js", "text/plain");
                return;
            }

            res.set_header("Cache-Control", "no-store");
            res.set_content(body, "application/javascript; charset=utf-8");
        }
    );
}
