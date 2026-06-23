#include "routes_file_versions_restore.h"

#include "httplib.h"
#include "audit_fields.h"
#include "file_versions.h"
#include "file_versions_restore.h"
#include "storage_resolver.h"
#include "user_quota.h"
#include "users_registry.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <string>
#include <system_error>

using json = nlohmann::json;

namespace {

bool context_ok(const FileVersionRestoreRoutesContext& c) {
    return c.file_versions &&
           c.users &&
           c.require_user_auth &&
           c.reply_json &&
           c.user_dir_for_fp &&
           c.file_size_u64 &&
           c.file_mtime_epoch;
}

void fallback_reply_json(httplib::Response& res, int status, const std::string& body) {
    res.status = status;
    res.set_content(body, "application/json; charset=utf-8");
}

void reply_json_ctx(
    const FileVersionRestoreRoutesContext& c,
    httplib::Response& res,
    int status,
    const json& body
) {
    if (c.reply_json) c.reply_json(res, status, body.dump());
    else fallback_reply_json(res, status, body.dump());
}

} // namespace

void register_file_version_restore_routes(
    httplib::Server& srv,
    const FileVersionRestoreRoutesContext& ctx
) {
    const FileVersionRestoreRoutesContext c = ctx;

    srv.Post("/api/v4/files/restore_version",
        [c](const httplib::Request& req, httplib::Response& res) {
            auto reply = [&](int status, const json& body) {
                reply_json_ctx(c, res, status, body);
            };

            if (!context_ok(c)) {
                reply(500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "file version restore route context incomplete"}
                });
                return;
            }

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
                    {"message", "invalid path"}
                });
                return;
            }

            const std::filesystem::path user_dir = c.user_dir_for_fp(fp_hex);

            std::filesystem::path abs_path;
            std::string perr;
            if (!pqnas::resolve_user_path_strict(user_dir, rel_norm, &abs_path, &perr)) {
                reply(400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "invalid path"}
                });
                return;
            }

            std::string found_ancestor;
            std::string ancestor_err;
            if (pqnas::any_file_ancestor_exists(*c.users, fp_hex, rel_norm, &found_ancestor, &ancestor_err)) {
                reply(409, json{
                    {"ok", false},
                    {"error", "path_conflict"},
                    {"message", "a parent path is an existing file"},
                    {"ancestor", found_ancestor}
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

            std::error_code ec;
            auto live_st = std::filesystem::symlink_status(abs_path, ec);
            if (!ec &&
                std::filesystem::exists(live_st) &&
                !std::filesystem::is_symlink(live_st) &&
                std::filesystem::is_regular_file(live_st)) {
                pqnas::PreserveCurrentVersionParams vp;
                vp.scope_type = "user";
                vp.scope_id = fp_hex;
                vp.scope_root = user_dir;
                vp.logical_rel_path = rel_norm;
                vp.live_abs_path = abs_path;
                vp.event_kind = "restore_preserve";
                vp.actor_fp = fp_hex;
                vp.users = c.users;
                vp.file_versions = c.file_versions;

                std::string preserve_err;
                std::string ignored_version_id;
                if (!pqnas::preserve_current_file_version(vp, &ignored_version_id, &preserve_err)) {
                    reply(500, json{
                        {"ok", false},
                        {"error", "server_error"},
                        {"message", "failed to preserve current file before restore"},
                        {"detail", pqnas::shorten(preserve_err, 180)}
                    });
                    return;
                }
            } else {
                std::filesystem::create_directories(abs_path.parent_path(), ec);
                if (ec) {
                    reply(500, json{
                        {"ok", false},
                        {"error", "server_error"},
                        {"message", "failed to create destination directory"},
                        {"detail", ec.message()}
                    });
                    return;
                }
            }

            auto rr = pqnas::restore_version_blob_to_path(
                c.file_versions,
                "user",
                fp_hex,
                rel_norm,
                version_id,
                abs_path
            );

            if (!rr.ok) {
                reply(rr.error == "not_found" ? 404 : 500, json{
                    {"ok", false},
                    {"error", rr.error.empty() ? "server_error" : rr.error},
                    {"message", rr.message.empty() ? "restore failed" : rr.message},
                    {"detail", pqnas::shorten(rr.detail, 180)}
                });
                return;
            }

            const std::uint64_t restored_size =
                rr.bytes > 0 ? static_cast<std::uint64_t>(rr.bytes)
                             : c.file_size_u64(abs_path);

            const std::int64_t restored_mtime =
                rr.mtime_epoch > 0
                    ? static_cast<std::int64_t>(rr.mtime_epoch)
                    : c.file_mtime_epoch(abs_path);

            const std::int64_t now_ts = static_cast<std::int64_t>(std::time(nullptr));

            if (c.touch_gallery_file_facts) {
                c.touch_gallery_file_facts(
                    "user",
                    fp_hex,
                    rel_norm,
                    restored_size,
                    restored_mtime,
                    now_ts
                );
            }

            reply(200, json{
                {"ok", true},
                {"scope_type", "user"},
                {"scope_id", fp_hex},
                {"path", rel_norm},
                {"restored_version_id", version_id},
                {"bytes", rr.bytes},
                {"mtime_epoch", rr.mtime_epoch},
                {"sha256_hex", rr.sha256_hex}
            });
        }
    );
}
