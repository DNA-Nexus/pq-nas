#include "routes_file_versions_read.h"

#include "httplib.h"
#include "audit_fields.h"
#include "file_versions.h"
#include "file_versions_present.h"
#include "file_versions_read.h"
#include "storage_resolver.h"
#include "users_registry.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using json = nlohmann::json;

namespace {

bool context_ok(const FileVersionReadRoutesContext& c) {
    return c.file_versions &&
           c.users &&
           c.require_user_auth &&
           c.reply_json &&
           c.user_dir_for_fp;
}

void fallback_reply_json(httplib::Response& res, int status, const std::string& body) {
    res.status = status;
    res.set_content(body, "application/json; charset=utf-8");
}

void reply_json_ctx(
    const FileVersionReadRoutesContext& c,
    httplib::Response& res,
    int status,
    const json& body
) {
    if (c.reply_json) c.reply_json(res, status, body.dump());
    else fallback_reply_json(res, status, body.dump());
}

std::string safe_download_name(std::string name) {
    for (char& ch : name) {
        unsigned char uc = static_cast<unsigned char>(ch);
        if (uc < 32 || uc == 127 || ch == '"' || ch == '\\' || ch == '/' || ch == ';') {
            ch = '_';
        }
    }

    if (name.empty()) name = "download";
    return name;
}

} // namespace

void register_file_version_read_routes(
    httplib::Server& srv,
    const FileVersionReadRoutesContext& ctx
) {
    const FileVersionReadRoutesContext c = ctx;

    srv.Get("/api/v4/files/versions/list",
        [c](const httplib::Request& req, httplib::Response& res) {
            auto reply = [&](int status, const json& body) {
                reply_json_ctx(c, res, status, body);
            };

            if (!context_ok(c)) {
                reply(500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "file version read route context incomplete"}
                });
                return;
            }

            std::string fp_hex;
            std::string role;
            if (!c.require_user_auth(req, res, &fp_hex, &role)) return;
            (void)role;

            std::string path_rel;
            if (req.has_param("path")) path_rel = req.get_param_value("path");

            if (path_rel.empty()) {
                reply(400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "missing path"}
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

            std::size_t limit = 100;
            if (req.has_param("limit")) {
                try {
                    long long v = std::stoll(req.get_param_value("limit"));
                    if (v > 0) limit = static_cast<std::size_t>(std::min<long long>(v, 500));
                } catch (...) {
                }
            }

            std::string verr;
            auto rows = c.file_versions->list_versions_for_path("user", fp_hex, rel_norm, limit, &verr);
            if (!verr.empty()) {
                reply(500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "failed to list file versions"},
                    {"detail", pqnas::shorten(verr, 180)}
                });
                return;
            }

            json out;
            out["ok"] = true;
            out["scope_type"] = "user";
            out["scope_id"] = fp_hex;
            out["path"] = rel_norm;
            out["versions"] = json::array();

            for (const auto& r : rows) {
                json item = json::object();
                item["version_id"] = r.version_id;
                item["logical_rel_path"] = r.logical_rel_path;
                item["event_kind"] = r.event_kind;
                item["created_at"] = r.created_at;
                item["created_epoch"] = r.created_epoch;
                item["actor_fp"] = r.actor_fp;
                item["actor_name_snapshot"] = r.actor_name_snapshot;
                item["actor_display"] = pqnas::version_actor_display(r.actor_name_snapshot, r.actor_fp);
                item["bytes"] = r.bytes;
                item["sha256_hex"] = r.sha256_hex;
                item["is_deleted_event"] = r.is_deleted_event;

                std::string ferr;
                auto fs = c.file_versions->flags_for_version("user", fp_hex, rel_norm, r.version_id, fp_hex, &ferr);
                item["flag_count"] = fs.flag_count;
                item["flagged_by_me"] = fs.flagged_by_me;
                item["flags"] = json::array();

                for (const auto& fl : fs.flags) {
                    item["flags"].push_back(json{
                        {"actor_fp", fl.actor_fp},
                        {"actor_name_snapshot", fl.actor_name_snapshot},
                        {"actor_display", pqnas::version_actor_display(fl.actor_name_snapshot, fl.actor_fp)},
                        {"created_at", fl.created_at},
                        {"created_epoch", fl.created_epoch},
                        {"note", fl.note}
                    });
                }

                out["versions"].push_back(std::move(item));
            }

            reply(200, out);
        }
    );

    srv.Get("/api/v4/files/versions/read_text",
        [c](const httplib::Request& req, httplib::Response& res) {
            auto reply = [&](int status, const json& body) {
                reply_json_ctx(c, res, status, body);
            };

            if (!context_ok(c)) {
                reply(500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "file version read route context incomplete"}
                });
                return;
            }

            std::string fp_hex;
            std::string role;
            if (!c.require_user_auth(req, res, &fp_hex, &role)) return;
            (void)role;

            res.set_header("Cache-Control", "no-store");

            std::string path_rel;
            if (req.has_param("path")) path_rel = req.get_param_value("path");

            std::string version_id;
            if (req.has_param("version_id")) version_id = req.get_param_value("version_id");

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

            auto rr = pqnas::read_version_blob_as_text(
                c.file_versions,
                "user",
                fp_hex,
                rel_norm,
                version_id,
                user_dir,
                2 * 1024 * 1024
            );

            if (!rr.ok) {
                const int http =
                    (rr.error == "bad_request") ? 400 :
                    (rr.error == "not_found") ? 404 :
                    (rr.error == "too_large") ? 413 :
                    (rr.error == "unsupported") ? 415 : 500;

                reply(http, json{
                    {"ok", false},
                    {"error", rr.error.empty() ? "server_error" : rr.error},
                    {"message", rr.message.empty() ? "failed to read version text" : rr.message},
                    {"detail", pqnas::shorten(rr.detail, 180)},
                    {"bytes", rr.bytes}
                });
                return;
            }

            reply(200, json{
                {"ok", true},
                {"scope_type", "user"},
                {"scope_id", fp_hex},
                {"path", rr.path},
                {"version_id", rr.version_id},
                {"created_at", rr.created_at},
                {"bytes", rr.bytes},
                {"sha256", rr.sha256_hex},
                {"sha256_hex", rr.sha256_hex},
                {"encoding", rr.encoding},
                {"had_utf8_bom", rr.had_utf8_bom},
                {"text", rr.text}
            });
        }
    );

    srv.Get("/api/v4/files/versions/download",
        [c](const httplib::Request& req, httplib::Response& res) {
            auto reply = [&](int status, const json& body) {
                reply_json_ctx(c, res, status, body);
            };

            if (!context_ok(c)) {
                reply(500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "file version read route context incomplete"}
                });
                return;
            }

            std::string fp_hex;
            std::string role;
            if (!c.require_user_auth(req, res, &fp_hex, &role)) return;
            (void)role;

            res.set_header("Cache-Control", "no-store");

            std::string path_rel;
            if (req.has_param("path")) path_rel = req.get_param_value("path");

            std::string version_id;
            if (req.has_param("version_id")) version_id = req.get_param_value("version_id");

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

            auto rr = pqnas::resolve_version_blob_for_download(
                c.file_versions,
                "user",
                fp_hex,
                rel_norm,
                version_id,
                user_dir
            );

            if (!rr.ok) {
                const int http =
                    (rr.error == "bad_request") ? 400 :
                    (rr.error == "not_found") ? 404 :
                    (rr.error == "unsupported") ? 415 : 500;

                reply(http, json{
                    {"ok", false},
                    {"error", rr.error.empty() ? "server_error" : rr.error},
                    {"message", rr.message.empty() ? "failed to download version" : rr.message},
                    {"detail", pqnas::shorten(rr.detail, 180)}
                });
                return;
            }

            std::ifstream f(rr.blob_abs_path, std::ios::binary);
            if (!f.good()) {
                reply(500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "failed to open version blob"}
                });
                return;
            }

            std::string body(
                (std::istreambuf_iterator<char>(f)),
                std::istreambuf_iterator<char>()
            );

            if (!f.good() && !f.eof()) {
                reply(500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "failed to read version blob"}
                });
                return;
            }

            std::string leaf = rel_norm;
            const auto slash = leaf.find_last_of('/');
            if (slash != std::string::npos) leaf = leaf.substr(slash + 1);

            const std::string filename = safe_download_name(leaf + ".version-" + version_id);

            res.set_header("Content-Type", "application/octet-stream");
            res.set_header("Content-Disposition", "attachment; filename=\"" + filename + "\"");
            res.set_header("X-PQNAS-Version-Id", rr.version_id);
            res.set_header("X-PQNAS-SHA256", rr.sha256_hex);
            res.body = std::move(body);
        }
    );
}
