#include "routes_admin_audit_rotate.h"

#include "httplib.h"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {

bool context_ok(const AdminAuditRotateRoutesContext& c) {
    return c.require_admin &&
           c.require_same_origin &&
           c.rotate_audit;
}

void reply_context_error(httplib::Response& res) {
    res.status = 500;
    res.set_header("Cache-Control", "no-store");
    res.set_content(
        "{\"ok\":false,\"error\":\"server_error\",\"message\":\"admin audit rotate route context incomplete\"}",
        "application/json; charset=utf-8"
    );
}

} // namespace

void register_admin_audit_rotate_routes(
    httplib::Server& srv,
    const AdminAuditRotateRoutesContext& ctx
) {
    const AdminAuditRotateRoutesContext c = ctx;

    srv.Post("/api/v4/admin/rotate-audit",
        [c](const httplib::Request& req, httplib::Response& res) {
            if (!context_ok(c)) {
                reply_context_error(res);
                return;
            }

            if (!c.require_admin(req, res)) return;
            if (!c.require_same_origin(req, res)) return;

            json j;
            if (!c.rotate_audit(&j)) {
                j["ok"] = false;
                j["error"] = "rotate_failed";
                res.status = 500;
                res.set_content(j.dump(2), "application/json; charset=utf-8");
                return;
            }

            res.set_content(j.dump(2), "application/json; charset=utf-8");
        }
    );
}
