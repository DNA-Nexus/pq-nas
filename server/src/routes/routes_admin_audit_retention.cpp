#include "routes_admin_audit_retention.h"

#include "httplib.h"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {

bool context_ok(const AdminAuditRetentionRoutesContext& c) {
    return c.require_admin &&
           c.require_same_origin &&
           c.reply_json &&
           c.preview_prune &&
           c.prune;
}

void reply_context_error(httplib::Response& res) {
    res.status = 500;
    res.set_header("Cache-Control", "no-store");
    res.set_content(
        "{\"ok\":false,\"error\":\"server_error\",\"message\":\"admin audit retention route context incomplete\"}",
        "application/json; charset=utf-8"
    );
}

void reply_callback_failed(
    const AdminAuditRetentionRoutesContext& c,
    httplib::Response& res,
    int status
) {
    if (status <= 0) status = 500;

    c.reply_json(res, status, json{
        {"ok", false},
        {"error", "server_error"},
        {"message", "audit retention operation failed"}
    }.dump());
}

} // namespace

void register_admin_audit_retention_routes(
    httplib::Server& srv,
    const AdminAuditRetentionRoutesContext& ctx
) {
    const AdminAuditRetentionRoutesContext c = ctx;

    srv.Post("/api/v4/admin/audit/preview-prune",
        [c](const httplib::Request& req, httplib::Response& res) {
            if (!context_ok(c)) {
                reply_context_error(res);
                return;
            }

            if (!c.require_admin(req, res)) return;
            if (!c.require_same_origin(req, res)) return;

            json in = json::object();
            if (!req.body.empty()) {
                in = json::parse(req.body, nullptr, false);
                if (in.is_discarded() || !in.is_object()) {
                    in = json::object();
                }
            }

            json out = json::object();
            int status = 200;

            if (!c.preview_prune(in, &out, &status)) {
                reply_callback_failed(c, res, status);
                return;
            }

            if (status <= 0) status = 200;
            c.reply_json(res, status, out.dump());
        }
    );

    srv.Post("/api/v4/admin/audit/prune",
        [c](const httplib::Request& req, httplib::Response& res) {
            if (!context_ok(c)) {
                reply_context_error(res);
                return;
            }

            if (!c.require_admin(req, res)) return;
            if (!c.require_same_origin(req, res)) return;

            json out = json::object();
            int status = 200;

            if (!c.prune(req, &out, &status)) {
                reply_callback_failed(c, res, status);
                return;
            }

            if (status <= 0) status = 200;
            c.reply_json(res, status, out.dump());
        }
    );
}
