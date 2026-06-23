#include "routes_auth_debug_approvals.h"

#include "httplib.h"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {

bool context_ok(const AuthDebugApprovalsRoutesContext& c) {
    return c.auth_debug_enabled &&
           c.require_admin_cookie &&
           c.approvals_count &&
           c.reply_json;
}

void reply_json_local(
    const AuthDebugApprovalsRoutesContext& c,
    httplib::Response& res,
    int status,
    const json& body
) {
    if (c.reply_json) {
        c.reply_json(res, status, body.dump());
        return;
    }

    res.status = status;
    res.set_content(body.dump(), "application/json; charset=utf-8");
}

} // namespace

void register_auth_debug_approvals_routes(
    httplib::Server& srv,
    const AuthDebugApprovalsRoutesContext& ctx
) {
    const AuthDebugApprovalsRoutesContext c = ctx;

    srv.Get("/api/debug/auth/approvals",
        [c](const httplib::Request& req, httplib::Response& res) {
            if (!context_ok(c)) {
                reply_json_local(c, res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "auth debug approvals route context incomplete"}
                });
                return;
            }

            if (!c.auth_debug_enabled()) {
                reply_json_local(c, res, 404, json{
                    {"ok", false},
                    {"error", "not_found"}
                });
                return;
            }

            if (!c.require_admin_cookie(req, res)) return;

            res.set_header("Cache-Control", "no-store");

            json out;
            out["ok"] = true;
            out["count"] = c.approvals_count();
            out["items_redacted"] = true;
            out["message"] = "Auth debug approval TTL details are redacted.";

            reply_json_local(c, res, 200, out);
        }
    );
}
