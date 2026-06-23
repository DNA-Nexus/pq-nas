#include "routes_admin_user_storage_jobs.h"

#include "httplib.h"
#include "audit_log.h"
#include "audit_fields.h"
#include "users_registry.h"
#include "user_storage_migration.h"

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

bool is_sha256_hex_lower_local(const std::string& s) {
    if (s.size() != 64) return false;
    for (unsigned char c : s) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return true;
}

void fallback_reply_json(httplib::Response& res, int status, const std::string& body) {
    res.status = status;
    res.set_content(body, "application/json; charset=utf-8");
}

bool context_ok(const AdminUserStorageJobsRoutesContext& c) {
    return c.users &&
           c.require_admin &&
           c.require_same_origin &&
           c.reply_json &&
           c.audit_append &&
           c.enqueue_migration_job &&
           c.read_migration_record &&
           c.enqueue_cleanup_job &&
           c.read_cleanup_record;
}

void reply_json_ctx(
    const AdminUserStorageJobsRoutesContext& c,
    httplib::Response& res,
    int status,
    const json& body
) {
    if (c.reply_json) c.reply_json(res, status, body.dump());
    else fallback_reply_json(res, status, body.dump());
}

} // namespace

void register_admin_user_storage_jobs_routes(
    httplib::Server& srv,
    const AdminUserStorageJobsRoutesContext& ctx
) {
    const AdminUserStorageJobsRoutesContext c = ctx;

    srv.Post("/api/v4/admin/users/migrate_storage",
        [c](const httplib::Request& req, httplib::Response& res) {
            auto reply = [&](int status, const json& body) {
                reply_json_ctx(c, res, status, body);
            };

            if (!context_ok(c)) {
                reply(500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "admin user storage job route context incomplete"}
                });
                return;
            }

            std::string actor_fp;
            if (!c.require_admin(req, res, &actor_fp)) return;
            if (!c.require_same_origin(req, res)) return;

            res.set_header("Cache-Control", "no-store");

            json j;
            try {
                j = json::parse(req.body);
            } catch (...) {
                reply(400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "invalid json"}
                });
                return;
            }

            const std::string fp = trim_copy_local(j.value("fingerprint", ""));
            std::string pool_id = trim_copy_local(j.value("pool_id", ""));
            if (pool_id == "DEFAULT" || pool_id == "Default" || pool_id == "default") {
                pool_id = "default";
            }

            if (fp.empty() || pool_id.empty()) {
                reply(400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "missing fingerprint or pool_id"}
                });
                return;
            }

            pqnas::UserStorageMigrationPlan plan;
            std::string resolve_err;
            if (!pqnas::resolve_user_storage_migration(*c.users, c.users_path, fp, pool_id, &plan, &resolve_err)) {
                pqnas::AuditEvent ev;
                ev.event = "admin.user_storage_migration_rejected";
                ev.outcome = "fail";
                ev.f["fingerprint"] = fp;
                ev.f["actor_fp"] = actor_fp;
                ev.f["to_pool_id"] = pool_id;
                ev.f["reason"] = "resolve_failed";
                if (!resolve_err.empty()) ev.f["detail"] = pqnas::shorten(resolve_err, 180);
                ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
                c.audit_append(ev);

                int http = 400;
                if (resolve_err == "user_missing" || resolve_err == "storage_unallocated") http = 404;

                reply(http, json{
                    {"ok", false},
                    {"error", "resolve_failed"},
                    {"message", "user storage migration rejected"},
                    {"detail", resolve_err}
                });
                return;
            }

            if (plan.from_pool_id == plan.to_pool_id) {
                pqnas::AuditEvent ev;
                ev.event = "admin.user_storage_migration_rejected";
                ev.outcome = "fail";
                ev.f["fingerprint"] = fp;
                ev.f["actor_fp"] = actor_fp;
                ev.f["from_pool_id"] = plan.from_pool_id;
                ev.f["to_pool_id"] = plan.to_pool_id;
                ev.f["reason"] = "same_pool";
                ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
                c.audit_append(ev);

                reply(409, json{
                    {"ok", false},
                    {"error", "same_pool"},
                    {"message", "source and destination pool are the same"}
                });
                return;
            }

            try {
                json out = c.enqueue_migration_job(actor_fp, fp, pool_id, req.remote_addr);

                pqnas::AuditEvent ev;
                ev.event = "admin.user_storage_migration_job_created";
                ev.outcome = "ok";
                ev.f["fingerprint"] = fp;
                ev.f["actor_fp"] = actor_fp;
                ev.f["from_pool_id"] = plan.from_pool_id;
                ev.f["to_pool_id"] = plan.to_pool_id;
                ev.f["job_id"] = out.value("job_id", "");
                ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
                c.audit_append(ev);

                if (c.reply_json) c.reply_json(res, 200, out.dump());
                else fallback_reply_json(res, 200, out.dump());
                return;
            } catch (const std::exception& e) {
                pqnas::AuditEvent ev;
                ev.event = "admin.user_storage_migration_rejected";
                ev.outcome = "fail";
                ev.f["fingerprint"] = fp;
                ev.f["actor_fp"] = actor_fp;
                ev.f["to_pool_id"] = pool_id;
                ev.f["reason"] = "enqueue_failed";
                ev.f["detail"] = pqnas::shorten(e.what(), 180);
                ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
                c.audit_append(ev);

                reply(500, json{
                    {"ok", false},
                    {"error", "enqueue_failed"},
                    {"message", "failed to create migration job"}
                });
                return;
            }
        }
    );

    srv.Get("/api/v4/admin/users/migrate_storage_status",
        [c](const httplib::Request& req, httplib::Response& res) {
            auto reply = [&](int status, const json& body) {
                reply_json_ctx(c, res, status, body);
            };

            if (!context_ok(c)) {
                reply(500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "admin user storage job route context incomplete"}
                });
                return;
            }

            std::string actor_fp;
            if (!c.require_admin(req, res, &actor_fp)) return;
            (void)actor_fp;

            res.set_header("Cache-Control", "no-store");

            const std::string job_id = trim_copy_local(req.get_param_value("job_id"));
            if (!is_sha256_hex_lower_local(job_id)) {
                reply(400, json{
                    {"ok", false},
                    {"error", "bad_job_id"},
                    {"message", "invalid job_id"}
                });
                return;
            }

            json rec;
            std::string err;
            if (!c.read_migration_record(job_id, &rec, &err)) {
                reply(404, json{
                    {"ok", false},
                    {"error", err.empty() ? "not_found" : err},
                    {"message", "migration job not found"}
                });
                return;
            }

            reply(200, json{
                {"ok", true},
                {"job", rec}
            });
        }
    );

    srv.Post("/api/v4/admin/users/cleanup_old_storage",
        [c](const httplib::Request& req, httplib::Response& res) {
            auto reply = [&](int status, const json& body) {
                reply_json_ctx(c, res, status, body);
            };

            if (!context_ok(c)) {
                reply(500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "admin user storage job route context incomplete"}
                });
                return;
            }

            std::string actor_fp;
            if (!c.require_admin(req, res, &actor_fp)) return;
            if (!c.require_same_origin(req, res)) return;

            res.set_header("Cache-Control", "no-store");

            json j;
            try {
                j = json::parse(req.body);
            } catch (...) {
                reply(400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "invalid json"}
                });
                return;
            }

            const std::string fp = trim_copy_local(j.value("fingerprint", ""));
            const std::string expected_active_pool_id = trim_copy_local(j.value("expected_active_pool_id", ""));
            const std::string old_pool_id = trim_copy_local(j.value("old_pool_id", ""));

            if (fp.empty() || expected_active_pool_id.empty() || old_pool_id.empty()) {
                reply(400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "missing fingerprint, expected_active_pool_id or old_pool_id"}
                });
                return;
            }

            if (expected_active_pool_id == old_pool_id) {
                pqnas::AuditEvent ev;
                ev.event = "admin.user_storage_cleanup_rejected";
                ev.outcome = "fail";
                ev.f["fingerprint"] = fp;
                ev.f["actor_fp"] = actor_fp;
                ev.f["expected_active_pool_id"] = expected_active_pool_id;
                ev.f["old_pool_id"] = old_pool_id;
                ev.f["reason"] = "same_pool";
                ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
                c.audit_append(ev);

                reply(409, json{
                    {"ok", false},
                    {"error", "same_pool"},
                    {"message", "expected active pool and old pool must differ"}
                });
                return;
            }

            try {
                json out = c.enqueue_cleanup_job(
                    actor_fp,
                    fp,
                    expected_active_pool_id,
                    old_pool_id,
                    req.remote_addr
                );

                pqnas::AuditEvent ev;
                ev.event = "admin.user_storage_cleanup_job_created";
                ev.outcome = "ok";
                ev.f["fingerprint"] = fp;
                ev.f["actor_fp"] = actor_fp;
                ev.f["expected_active_pool_id"] = expected_active_pool_id;
                ev.f["old_pool_id"] = old_pool_id;
                ev.f["job_id"] = out.value("job_id", "");
                ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
                c.audit_append(ev);

                if (c.reply_json) c.reply_json(res, 200, out.dump());
                else fallback_reply_json(res, 200, out.dump());
            } catch (const std::exception& e) {
                pqnas::AuditEvent ev;
                ev.event = "admin.user_storage_cleanup_rejected";
                ev.outcome = "fail";
                ev.f["fingerprint"] = fp;
                ev.f["actor_fp"] = actor_fp;
                ev.f["expected_active_pool_id"] = expected_active_pool_id;
                ev.f["old_pool_id"] = old_pool_id;
                ev.f["reason"] = "enqueue_failed";
                ev.f["detail"] = pqnas::shorten(e.what(), 180);
                ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
                c.audit_append(ev);

                reply(500, json{
                    {"ok", false},
                    {"error", "enqueue_failed"},
                    {"message", e.what()}
                });
            }
        }
    );

    srv.Get("/api/v4/admin/users/cleanup_old_storage_status",
        [c](const httplib::Request& req, httplib::Response& res) {
            auto reply = [&](int status, const json& body) {
                reply_json_ctx(c, res, status, body);
            };

            if (!context_ok(c)) {
                reply(500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "admin user storage job route context incomplete"}
                });
                return;
            }

            std::string actor_fp;
            if (!c.require_admin(req, res, &actor_fp)) return;
            (void)actor_fp;

            res.set_header("Cache-Control", "no-store");

            const std::string job_id = trim_copy_local(req.get_param_value("job_id"));
            if (!is_sha256_hex_lower_local(job_id)) {
                reply(400, json{
                    {"ok", false},
                    {"error", "bad_job_id"},
                    {"message", "invalid job_id"}
                });
                return;
            }

            json rec;
            std::string err;
            if (!c.read_cleanup_record(job_id, &rec, &err)) {
                reply(404, json{
                    {"ok", false},
                    {"error", err.empty() ? "not_found" : err},
                    {"message", "cleanup job record not found"}
                });
                return;
            }

            reply(200, json{
                {"ok", true},
                {"job", rec}
            });
        }
    );
}
