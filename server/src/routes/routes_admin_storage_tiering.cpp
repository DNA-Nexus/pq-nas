#include "routes_admin_storage_tiering.h"

#include "httplib.h"
#include "audit_log.h"
#include "audit_fields.h"
#include "storage_resolver.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <string>

using json = nlohmann::json;

namespace {

std::string trim_copy_local(std::string s) {
    auto is_ws = [](unsigned char c) {
        return std::isspace(c) != 0;
    };

    auto first = std::find_if_not(s.begin(), s.end(), [&](char c) {
        return is_ws(static_cast<unsigned char>(c));
    });

    auto last = std::find_if_not(s.rbegin(), s.rend(), [&](char c) {
        return is_ws(static_cast<unsigned char>(c));
    }).base();

    if (first >= last) return {};
    return std::string(first, last);
}

} // namespace

void register_admin_storage_tiering_routes(
    httplib::Server& srv,
    const AdminStorageTieringRoutesContext& ctx
) {
    srv.Post("/api/v4/admin/storage/tiering/migrate_one",
        [ctx](const httplib::Request& req, httplib::Response& res) {
            auto reply = [&](int status, const json& body) {
                if (ctx.reply_json) {
                    ctx.reply_json(res, status, body.dump());
                } else {
                    res.status = status;
                    res.set_content(body.dump(), "application/json; charset=utf-8");
                }
            };

            if (!ctx.require_admin ||
                !ctx.require_same_origin ||
                !ctx.migrate_one_landing_file ||
                !ctx.audit_append) {
                reply(500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "admin storage tiering route context incomplete"}
                });
                return;
            }

            std::string actor_fp;
            if (!ctx.require_admin(req, res, &actor_fp)) return;
            if (!ctx.require_same_origin(req, res)) return;

            json j;
            try {
                j = json::parse(req.body.empty() ? "{}" : req.body);
            } catch (...) {
                reply(400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "invalid json"}
                });
                return;
            }

            const std::string fp = trim_copy_local(j.value("fingerprint", ""));
            const std::string rel_path = trim_copy_local(j.value("path", ""));

            if (fp.empty() || rel_path.empty()) {
                reply(400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "missing fingerprint or path"}
                });
                return;
            }

            std::string rel_norm;
            std::string nerr;
            if (!pqnas::normalize_user_rel_path_strict(rel_path, &rel_norm, &nerr)) {
                reply(400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "invalid path"},
                    {"detail", nerr}
                });
                return;
            }

            std::string merr;
            if (!ctx.migrate_one_landing_file(fp, rel_norm, &merr)) {
                reply(500, json{
                    {"ok", false},
                    {"error", "migration_failed"},
                    {"message", "tiering migration failed"},
                    {"detail", pqnas::shorten(merr, 180)}
                });
                return;
            }

            pqnas::AuditEvent ev;
            ev.event = "admin.storage_tiering_migrate_one";
            ev.outcome = "ok";
            ev.f["actor_fp"] = actor_fp;
            ev.f["fingerprint"] = fp;
            ev.f["path"] = pqnas::shorten(rel_norm, 200);
            ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
            ctx.audit_append(ev);

            reply(200, json{
                {"ok", true},
                {"fingerprint", fp},
                {"path", rel_norm}
            });
        }
    );
    srv.Get("/api/v4/admin/storage/tiering/status",
        [ctx](const httplib::Request& req, httplib::Response& res) {
            auto reply = [&](int status, const json& body) {
                if (ctx.reply_json) {
                    ctx.reply_json(res, status, body.dump());
                } else {
                    res.status = status;
                    res.set_content(body.dump(), "application/json; charset=utf-8");
                }
            };

            if (!ctx.require_admin ||
                !ctx.reply_json ||
                !ctx.tiering_status_json) {
                reply(500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "admin storage tiering status route context incomplete"}
                });
                return;
            }

            std::string actor_fp;
            if (!ctx.require_admin(req, res, &actor_fp)) return;

            json out;
            std::string err;
            if (!ctx.tiering_status_json(&out, &err)) {
                reply(500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", err.empty() ? "failed to read tiering status" : err}
                });
                return;
            }

            reply(200, out);
        }
    );


}
