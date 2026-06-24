#include "routes_file_versions_manage.h"

#include "httplib.h"
#include "audit_fields.h"
#include "file_versions.h"
#include "storage_resolver.h"
#include "users_registry.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

using json = nlohmann::json;

namespace {

bool context_ok(const FileVersionManageRoutesContext& c) {
    return c.file_versions &&
           c.users &&
           c.require_same_origin &&
           c.require_user_auth &&
           c.reply_json &&
           c.user_dir_for_fp;
}

void fallback_reply_json(httplib::Response& res, int status, const std::string& body) {
    res.status = status;
    res.set_content(body, "application/json; charset=utf-8");
}

void reply_json_ctx(
    const FileVersionManageRoutesContext& c,
    httplib::Response& res,
    int status,
    const json& body
) {
    if (c.reply_json) c.reply_json(res, status, body.dump());
    else fallback_reply_json(res, status, body.dump());
}

} // namespace

void register_file_version_manage_routes(
    httplib::Server& srv,
    const FileVersionManageRoutesContext& ctx
) {
    const FileVersionManageRoutesContext c = ctx;

    auto files_versions_flag_handler =
        [c](const httplib::Request& req, httplib::Response& res, bool want_flag) {
            auto reply = [&](int status, const json& body) {
                reply_json_ctx(c, res, status, body);
            };

            if (!context_ok(c)) {
                reply(500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "file version manage route context incomplete"}
                });
                return;
            }

            if (!c.require_same_origin(req, res)) return;

            std::string fp_hex;
            std::string role;
            if (!c.require_user_auth(req, res, &fp_hex, &role)) return;
            (void)role;

            json body = json::parse(req.body, nullptr, false);
            if (body.is_discarded() || !body.is_object()) {
                reply(400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "invalid json"}
                });
                return;
            }

            const std::string path_rel = body.value("path", "");
            const std::string version_id = body.value("version_id", "");
            const std::string note = body.value("note", "");

            if (path_rel.empty() || version_id.empty()) {
                reply(400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "missing path or version_id"}
                });
                return;
            }

            std::string rel_norm;
            std::string nerr;
            if (!pqnas::normalize_user_rel_path_strict(path_rel, &rel_norm, &nerr)) {
                reply(400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "invalid path"}
                });
                return;
            }

            std::string ferr;
            bool ok = false;
            if (want_flag) {
                ok = c.file_versions->flag_version(
                    "user",
                    fp_hex,
                    rel_norm,
                    version_id,
                    fp_hex,
                    c.users,
                    note,
                    &ferr
                );
            } else {
                ok = c.file_versions->unflag_version(
                    "user",
                    fp_hex,
                    rel_norm,
                    version_id,
                    fp_hex,
                    &ferr
                );
            }

            if (!ok) {
                const int http = (ferr == "version not found") ? 404 : 500;
                reply(http, json{
                    {"ok", false},
                    {"error", http == 404 ? "not_found" : "server_error"},
                    {"message", want_flag ? "failed to flag version" : "failed to unflag version"},
                    {"detail", pqnas::shorten(ferr, 180)}
                });
                return;
            }

            auto fs = c.file_versions->flags_for_version(
                "user",
                fp_hex,
                rel_norm,
                version_id,
                fp_hex,
                &ferr
            );

            reply(200, json{
                {"ok", true},
                {"flagged", want_flag},
                {"flag_count", fs.flag_count},
                {"flagged_by_me", fs.flagged_by_me}
            });
        };

    srv.Post("/api/v4/files/versions/flag",
        [files_versions_flag_handler](const httplib::Request& req, httplib::Response& res) {
            files_versions_flag_handler(req, res, true);
        }
    );

    srv.Post("/api/v4/files/versions/unflag",
        [files_versions_flag_handler](const httplib::Request& req, httplib::Response& res) {
            files_versions_flag_handler(req, res, false);
        }
    );

    srv.Get("/api/v4/files/versions/summary",
        [c](const httplib::Request& req, httplib::Response& res) {
            auto reply = [&](int status, const json& body) {
                reply_json_ctx(c, res, status, body);
            };

            if (!context_ok(c)) {
                reply(500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "file version manage route context incomplete"}
                });
                return;
            }

            std::string fp_hex;
            std::string role;
            if (!c.require_user_auth(req, res, &fp_hex, &role)) return;
            (void)role;

            res.set_header("Cache-Control", "no-store");

            pqnas::FileVersionsScopeStats stats;
            std::string serr;
            if (!c.file_versions->scope_stats("user", fp_hex, &stats, &serr)) {
                reply(500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "failed to summarize file versions"},
                    {"detail", pqnas::shorten(serr, 180)}
                });
                return;
            }

            reply(200, json{
                {"ok", true},
                {"scope_type", "user"},
                {"scope_id", fp_hex},
                {"versions_count", stats.versions_count},
                {"versions_bytes", stats.versions_bytes}
            });
        }
    );

    srv.Post("/api/v4/files/versions/delete",
        [c](const httplib::Request& req, httplib::Response& res) {
            auto reply = [&](int status, const json& body) {
                reply_json_ctx(c, res, status, body);
            };

            if (!context_ok(c)) {
                reply(500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "file version manage route context incomplete"}
                });
                return;
            }

            if (!c.require_same_origin(req, res)) return;

            std::string fp_hex;
            std::string role;
            if (!c.require_user_auth(req, res, &fp_hex, &role)) return;
            (void)role;

            json body = json::parse(req.body, nullptr, false);
            if (body.is_discarded() || !body.is_object()) {
                reply(400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "invalid json"}
                });
                return;
            }

            const std::string path_rel = body.value("path", "");
            const std::string version_id = body.value("version_id", "");

            if (path_rel.empty() || version_id.empty()) {
                reply(400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "missing path or version_id"}
                });
                return;
            }

            std::string rel_norm;
            std::string nerr;
            if (!pqnas::normalize_user_rel_path_strict(path_rel, &rel_norm, &nerr)) {
                reply(400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "invalid path"},
                    {"detail", pqnas::shorten(nerr, 180)}
                });
                return;
            }

            auto uopt = c.users->get(fp_hex);
            if (!uopt.has_value() || uopt->storage_state != "allocated") {
                reply(403, json{
                    {"ok", false},
                    {"error", "storage_unallocated"},
                    {"message", "Storage not allocated"}
                });
                return;
            }

            const std::filesystem::path user_dir = c.user_dir_for_fp(fp_hex);

            pqnas::FileVersionsDeleteResult dr;
            std::string derr;
            if (!c.file_versions->delete_single_version("user", fp_hex, user_dir, rel_norm, version_id, &dr, &derr)) {
                const bool not_found = derr.find("not found") != std::string::npos;
                reply(not_found ? 404 : 500, json{
                    {"ok", false},
                    {"error", not_found ? "not_found" : "server_error"},
                    {"message", not_found ? "version not found" : "failed to delete version"},
                    {"detail", pqnas::shorten(derr, 180)}
                });
                return;
            }

            reply(200, json{
                {"ok", true},
                {"scope_type", "user"},
                {"scope_id", fp_hex},
                {"path", rel_norm},
                {"version_id", version_id},
                {"versions_deleted", dr.versions_deleted},
                {"version_bytes_deleted", dr.bytes_deleted},
                {"version_blobs_missing", dr.blobs_missing}
            });
        }
    );
}
