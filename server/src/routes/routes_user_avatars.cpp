#include "routes_user_avatars.h"

#include "httplib.h"
#include "audit_log.h"
#include "runtime_paths.h"
#include "users_registry.h"
#include "workspaces.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

using json = nlohmann::json;

namespace {

void fallback_reply_json(httplib::Response& res, int status, const std::string& body) {
    res.status = status;
    res.set_content(body, "application/json; charset=utf-8");
}

void reply_json_ctx(
    const UserAvatarRoutesContext& c,
    httplib::Response& res,
    int status,
    const json& body
) {
    if (c.reply_json) c.reply_json(res, status, body.dump());
    else fallback_reply_json(res, status, body.dump());
}

bool context_ok(const UserAvatarRoutesContext& c) {
    return c.users &&
           c.workspaces &&
           c.require_admin_cookie &&
           c.require_user_cookie &&
           c.require_user_auth &&
           c.require_same_origin &&
           c.reply_json &&
           c.b64std_decode_to_bytes &&
           c.is_valid_fingerprint_hex &&
           c.audit_append;
}

bool avatar_ext_from_mime(const std::string& mime, std::string* ext) {
    if (!ext) return false;

    if (mime == "image/png") {
        *ext = ".png";
        return true;
    }
    if (mime == "image/jpeg") {
        *ext = ".jpg";
        return true;
    }
    if (mime == "image/webp") {
        *ext = ".webp";
        return true;
    }

    return false;
}

std::filesystem::path avatar_dir() {
    return std::filesystem::path(pqnas::data_root_dir()) / "avatars";
}

bool write_avatar_file(
    const std::string& fp,
    const std::string& ext,
    const std::string& bytes,
    std::string* err
) {
    std::error_code ec;
    const std::filesystem::path dir = avatar_dir();
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        if (err) *err = "mkdir failed";
        return false;
    }

    const std::filesystem::path out = dir / (fp + ext);
    std::ofstream o(out.string(), std::ios::binary | std::ios::trunc);
    if (!o.good()) {
        if (err) *err = "write failed";
        return false;
    }

    o.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!o.good()) {
        if (err) *err = "write failed";
        return false;
    }

    return true;
}

void remove_stale_avatar_variants(const std::string& fp, const std::string& keep_ext) {
    std::error_code ec;
    const std::filesystem::path dir = avatar_dir();

    if (keep_ext != ".png") {
        std::filesystem::remove(dir / (fp + ".png"), ec);
        ec.clear();
    }
    if (keep_ext != ".jpg") {
        std::filesystem::remove(dir / (fp + ".jpg"), ec);
        ec.clear();
    }
    if (keep_ext != ".webp") {
        std::filesystem::remove(dir / (fp + ".webp"), ec);
        ec.clear();
    }
}

void remove_all_avatar_variants(const std::string& fp) {
    std::error_code ec;
    const std::filesystem::path dir = avatar_dir();

    std::filesystem::remove(dir / (fp + ".png"), ec);
    ec.clear();
    std::filesystem::remove(dir / (fp + ".jpg"), ec);
    ec.clear();
    std::filesystem::remove(dir / (fp + ".webp"), ec);
}

void serve_avatar_for_fingerprint(
    const UserAvatarRoutesContext& c,
    const std::string& fp,
    httplib::Response& res
) {
    const std::filesystem::path dir = avatar_dir();
    const std::filesystem::path p_png = dir / (fp + ".png");
    const std::filesystem::path p_jpg = dir / (fp + ".jpg");
    const std::filesystem::path p_webp = dir / (fp + ".webp");

    std::filesystem::path p;
    std::string ct;

    if (std::filesystem::exists(p_png)) {
        p = p_png;
        ct = "image/png";
    } else if (std::filesystem::exists(p_jpg)) {
        p = p_jpg;
        ct = "image/jpeg";
    } else if (std::filesystem::exists(p_webp)) {
        p = p_webp;
        ct = "image/webp";
    } else {
        reply_json_ctx(c, res, 404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "file missing"}
        });
        return;
    }

    std::ifstream f(p, std::ios::binary);
    if (!f.good()) {
        reply_json_ctx(c, res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "failed to open avatar"}
        });
        return;
    }

    std::string bytes(
        (std::istreambuf_iterator<char>(f)),
        std::istreambuf_iterator<char>()
    );

    if (!f.good() && !f.eof()) {
        reply_json_ctx(c, res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "failed to read avatar"}
        });
        return;
    }

    res.set_header("Cache-Control", "no-store");
    res.set_content(bytes, ct.c_str());
}


bool app_pref_app_id_ok(const std::string& id) {
    if (id.empty() || id.size() > 96) return false;

    for (unsigned char ch : id) {
        if (std::isalnum(ch) || ch == '_' || ch == '-' || ch == '.') continue;
        return false;
    }

    return true;
}

bool app_pref_launch_ok(const std::string& v) {
    return v == "auto" || v == "embedded" || v == "detached";
}

bool app_pref_window_ok(const std::string& v) {
    return v == "auto" || v == "small" || v == "normal" || v == "large" || v == "full";
}

json sanitize_app_prefs_json(const json& in) {
    json out = json::object();
    if (!in.is_object()) return out;

    std::size_t count = 0;

    for (auto it = in.begin(); it != in.end(); ++it) {
        if (count >= 128) break;

        const std::string app_id = it.key();
        if (!app_pref_app_id_ok(app_id)) continue;
        if (!it.value().is_object()) continue;

        const json& src = it.value();
        json one = json::object();

        if (src.contains("show_in_sidebar") && src["show_in_sidebar"].is_boolean()) {
            one["show_in_sidebar"] = src["show_in_sidebar"].get<bool>();
        }

        if (src.contains("default_launch") && src["default_launch"].is_string()) {
            const std::string v = src["default_launch"].get<std::string>();
            if (app_pref_launch_ok(v)) one["default_launch"] = v;
        }

        if (src.contains("window_profile") && src["window_profile"].is_string()) {
            const std::string v = src["window_profile"].get<std::string>();
            if (app_pref_window_ok(v)) one["window_profile"] = v;
        }

        if (!one.empty()) {
            out[app_id] = one;
            ++count;
        }
    }

    return out;
}

json parse_stored_app_prefs_json(const std::string& raw) {
    try {
        if (raw.empty()) return json::object();
        const json parsed = json::parse(raw);
        return sanitize_app_prefs_json(parsed);
    } catch (...) {
        return json::object();
    }
}

} // namespace

void register_user_avatar_routes(
    httplib::Server& srv,
    const UserAvatarRoutesContext& ctx
) {
    const UserAvatarRoutesContext c = ctx;

    srv.Post("/api/v4/admin/users/avatar_upload",
        [c](const httplib::Request& req, httplib::Response& res) {
            auto reply = [&](int status, const json& body) {
                reply_json_ctx(c, res, status, body);
            };

            if (!context_ok(c)) {
                reply(500, json{{"ok", false}, {"error", "server_error"}, {"message", "avatar route context incomplete"}});
                return;
            }

            std::string actor_fp;
            if (!c.require_admin_cookie(req, res, &actor_fp)) return;
            if (!c.require_same_origin(req, res)) return;

            json j;
            try {
                j = json::parse(req.body);
            } catch (...) {
                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid json"}});
                return;
            }

            const std::string fp = j.value("fingerprint", "");
            const std::string mime = j.value("mime", "");
            const std::string b64 = j.value("data_b64", "");

            if (fp.empty() || b64.empty()) {
                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "missing fingerprint or data"}});
                return;
            }

            std::string ext;
            if (!avatar_ext_from_mime(mime, &ext)) {
                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "unsupported image type"}});
                return;
            }

            std::string bytes;
            if (!c.b64std_decode_to_bytes(b64, bytes)) {
                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "base64 decode failed"}});
                return;
            }

            if (bytes.size() > 256 * 1024) {
                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "file too large"}});
                return;
            }

            std::string err;
            if (!write_avatar_file(fp, ext, bytes, &err)) {
                const std::string msg = err.empty() ? "write failed" : err;
                reply(500, json{{"ok", false}, {"error", "server_error"}, {"message", msg}});
                return;
            }

            const std::string url = std::string("/api/v4/users/avatar?fingerprint=") + fp;
            reply(200, json{{"ok", true}, {"avatar_url", url}});
        }
    );

    srv.Get("/api/v4/admin/users/avatar",
        [c](const httplib::Request& req, httplib::Response& res) {
            auto reply = [&](int status, const json& body) {
                reply_json_ctx(c, res, status, body);
            };

            if (!context_ok(c)) {
                reply(500, json{{"ok", false}, {"error", "server_error"}, {"message", "avatar route context incomplete"}});
                return;
            }

            std::string actor_fp;
            if (!c.require_admin_cookie(req, res, &actor_fp)) return;

            const std::string fp =
                req.has_param("fingerprint") ? req.get_param_value("fingerprint") : "";

            if (fp.empty()) {
                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "missing fingerprint"}});
                return;
            }

            serve_avatar_for_fingerprint(c, fp, res);
        }
    );

    srv.Get("/api/v4/users/avatar",
        [c](const httplib::Request& req, httplib::Response& res) {
            auto reply = [&](int status, const json& body) {
                reply_json_ctx(c, res, status, body);
            };

            if (!context_ok(c)) {
                reply(500, json{{"ok", false}, {"error", "server_error"}, {"message", "avatar route context incomplete"}});
                return;
            }

            std::string actor_fp;
            std::string actor_role;
            if (!c.require_user_cookie(req, res, &actor_fp, &actor_role)) return;
            (void)actor_role;

            const std::string target_fp =
                req.has_param("fingerprint") ? req.get_param_value("fingerprint") : "";

            if (target_fp.empty()) {
                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "missing fingerprint"}});
                return;
            }

            if (!c.users->load(c.users_path)) {
                reply(500, json{{"ok", false}, {"error", "users_reload_failed"}, {"message", "failed to reload users"}});
                return;
            }

            if (!c.workspaces->load(c.workspaces_path)) {
                reply(500, json{{"ok", false}, {"error", "workspaces_reload_failed"}, {"message", "failed to reload workspaces"}});
                return;
            }

            bool allowed = false;

            if (target_fp == actor_fp) {
                allowed = true;
            }

            if (!allowed && c.users->is_admin_enabled(actor_fp)) {
                allowed = true;
            }

            if (!allowed) {
                for (const auto& kv : c.workspaces->snapshot()) {
                    const auto& w = kv.second;
                    if (w.status != "enabled") continue;

                    bool actor_in_same_workspace = false;
                    bool target_in_same_workspace = false;

                    for (const auto& m : w.members) {
                        if (m.status != "enabled") continue;

                        if (m.fingerprint == actor_fp) actor_in_same_workspace = true;
                        if (m.fingerprint == target_fp) target_in_same_workspace = true;

                        if (actor_in_same_workspace && target_in_same_workspace) {
                            allowed = true;
                            break;
                        }
                    }

                    if (allowed) break;
                }
            }

            if (!allowed) {
                reply(403, json{{"ok", false}, {"error", "forbidden"}, {"message", "avatar access denied"}});
                return;
            }

            serve_avatar_for_fingerprint(c, target_fp, res);
        }
    );

    srv.Post("/api/v4/admin/users/avatar_remove",
        [c](const httplib::Request& req, httplib::Response& res) {
            auto reply = [&](int status, const json& body) {
                reply_json_ctx(c, res, status, body);
            };

            if (!context_ok(c)) {
                reply(500, json{{"ok", false}, {"error", "server_error"}, {"message", "avatar route context incomplete"}});
                return;
            }

            std::string actor_fp;
            if (!c.require_admin_cookie(req, res, &actor_fp)) return;
            if (!c.require_same_origin(req, res)) return;

            json j;
            try {
                j = json::parse(req.body);
            } catch (...) {
                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid json"}});
                return;
            }

            const std::string fp = j.value("fingerprint", "");
            if (fp.empty()) {
                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "missing fingerprint"}});
                return;
            }

            auto cur = c.users->get(fp);
            if (!cur.has_value()) {
                reply(404, json{{"ok", false}, {"error", "not_found"}, {"message", "user not found"}});
                return;
            }

            remove_all_avatar_variants(fp);

            pqnas::UserRec u = *cur;
            u.avatar_url.clear();

            const bool ok_upsert = c.users->upsert(u);
            const bool ok_save = ok_upsert ? c.users->save(c.users_path) : false;

            if (!ok_upsert || !ok_save) {
                reply(500, json{{"ok", false}, {"error", "server_error"}, {"message", "save failed"}});
                return;
            }

            reply(200, json{{"ok", true}});
        }
    );

    srv.Get("/api/v4/user/app_prefs",
        [c](const httplib::Request& req, httplib::Response& res) {
            auto reply = [&](int status, const json& body) {
                reply_json_ctx(c, res, status, body);
            };

            if (!context_ok(c)) {
                reply(500, json{{"ok", false}, {"error", "server_error"}, {"message", "app prefs route context incomplete"}});
                return;
            }

            std::string actor_fp;
            std::string actor_role;
            if (!c.require_user_auth(req, res, &actor_fp, &actor_role)) return;
            (void)actor_role;

            if (!c.users->load(c.users_path)) {
                reply(500, json{{"ok", false}, {"error", "users_reload_failed"}, {"message", "failed to reload users"}});
                return;
            }

            auto cur = c.users->get(actor_fp);
            if (!cur.has_value()) {
                reply(404, json{{"ok", false}, {"error", "not_found"}, {"message", "user not found"}});
                return;
            }

            reply(200, json{
                {"ok", true},
                {"app_prefs", parse_stored_app_prefs_json(cur->app_prefs_json)}
            });
        }
    );

    srv.Post("/api/v4/user/app_prefs",
        [c](const httplib::Request& req, httplib::Response& res) {
            auto reply = [&](int status, const json& body) {
                reply_json_ctx(c, res, status, body);
            };

            if (!context_ok(c)) {
                reply(500, json{{"ok", false}, {"error", "server_error"}, {"message", "app prefs route context incomplete"}});
                return;
            }

            std::string actor_fp;
            std::string actor_role;
            if (!c.require_user_auth(req, res, &actor_fp, &actor_role)) return;
            if (!c.require_same_origin(req, res)) return;
            (void)actor_role;

            if (req.body.size() > 64 * 1024) {
                reply(413, json{{"ok", false}, {"error", "too_large"}, {"message", "app prefs payload too large"}});
                return;
            }

            json j;
            try {
                j = json::parse(req.body);
            } catch (...) {
                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid json"}});
                return;
            }

            const json raw = (j.is_object() && j.contains("app_prefs")) ? j["app_prefs"] : j;
            if (!raw.is_object()) {
                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "app_prefs must be an object"}});
                return;
            }

            const json prefs = sanitize_app_prefs_json(raw);

            if (!c.users->load(c.users_path)) {
                reply(500, json{{"ok", false}, {"error", "users_reload_failed"}, {"message", "failed to reload users"}});
                return;
            }

            auto cur = c.users->get(actor_fp);
            if (!cur.has_value()) {
                reply(404, json{{"ok", false}, {"error", "not_found"}, {"message", "user not found"}});
                return;
            }

            pqnas::UserRec u = *cur;
            u.app_prefs_json = prefs.dump();

            const bool ok_upsert = c.users->upsert(u);
            const bool ok_save = ok_upsert ? c.users->save(c.users_path) : false;

            {
                pqnas::AuditEvent ev;
                ev.event = "user.app_prefs_update";
                ev.outcome = (ok_upsert && ok_save) ? "ok" : "fail";
                ev.f["actor_fp"] = actor_fp;
                ev.f["app_count"] = std::to_string(prefs.size());
                ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
                c.audit_append(ev);
            }

            if (!ok_upsert || !ok_save) {
                reply(500, json{{"ok", false}, {"error", "server_error"}, {"message", "app prefs save failed"}});
                return;
            }

            reply(200, json{
                {"ok", true},
                {"app_prefs", prefs}
            });
        }
    );

    srv.Post("/api/v4/user/profile/avatar_upload",
        [c](const httplib::Request& req, httplib::Response& res) {
            auto reply = [&](int status, const json& body) {
                reply_json_ctx(c, res, status, body);
            };

            if (!context_ok(c)) {
                reply(500, json{{"ok", false}, {"error", "server_error"}, {"message", "avatar route context incomplete"}});
                return;
            }

            std::string actor_fp;
            std::string actor_role;
            if (!c.require_user_auth(req, res, &actor_fp, &actor_role)) return;
            if (!c.require_same_origin(req, res)) return;
            (void)actor_role;

            if (!c.is_valid_fingerprint_hex(actor_fp)) {
                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid actor fingerprint"}});
                return;
            }

            json j;
            try {
                j = json::parse(req.body);
            } catch (...) {
                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid json"}});
                return;
            }

            const std::string mime = j.value("mime", "");
            const std::string b64 = j.value("data_b64", "");

            if (b64.empty()) {
                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "missing data"}});
                return;
            }

            std::string ext;
            if (!avatar_ext_from_mime(mime, &ext)) {
                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "unsupported image type"}});
                return;
            }

            std::string bytes;
            if (!c.b64std_decode_to_bytes(b64, bytes)) {
                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "base64 decode failed"}});
                return;
            }

            if (bytes.size() > 256 * 1024) {
                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "file too large"}});
                return;
            }

            if (!c.users->load(c.users_path)) {
                reply(500, json{{"ok", false}, {"error", "users_reload_failed"}, {"message", "failed to reload users"}});
                return;
            }

            auto cur = c.users->get(actor_fp);
            if (!cur.has_value()) {
                reply(404, json{{"ok", false}, {"error", "not_found"}, {"message", "user not found"}});
                return;
            }

            std::string err;
            if (!write_avatar_file(actor_fp, ext, bytes, &err)) {
                reply(500, json{{"ok", false}, {"error", "server_error"}, {"message", err.empty() ? "avatar write failed" : err}});
                return;
            }

            remove_stale_avatar_variants(actor_fp, ext);

            const std::string url =
                std::string("/api/v4/users/avatar?fingerprint=") + actor_fp;

            pqnas::UserRec u = *cur;
            u.avatar_url = url;

            const bool ok_upsert = c.users->upsert(u);
            const bool ok_save = ok_upsert ? c.users->save(c.users_path) : false;

            {
                pqnas::AuditEvent ev;
                ev.event = "user.avatar_upload";
                ev.outcome = (ok_upsert && ok_save) ? "ok" : "fail";
                ev.f["actor_fp"] = actor_fp;
                ev.f["mime"] = mime;
                ev.f["bytes"] = std::to_string(bytes.size());
                ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
                c.audit_append(ev);
            }

            if (!ok_upsert || !ok_save) {
                reply(500, json{{"ok", false}, {"error", "server_error"}, {"message", "avatar metadata save failed"}});
                return;
            }

            reply(200, json{{"ok", true}, {"avatar_url", url}});
        }
    );
    srv.Post("/api/v4/user/profile/avatar_remove",
        [c](const httplib::Request& req, httplib::Response& res) {
            auto reply = [&](int status, const json& body) {
                reply_json_ctx(c, res, status, body);
            };

            if (!context_ok(c)) {
                reply(500, json{{"ok", false}, {"error", "server_error"}, {"message", "avatar route context incomplete"}});
                return;
            }

            std::string actor_fp;
            std::string actor_role;
            if (!c.require_user_auth(req, res, &actor_fp, &actor_role)) return;
            if (!c.require_same_origin(req, res)) return;
            (void)actor_role;

            if (!c.users->load(c.users_path)) {
                reply(500, json{
                    {"ok", false},
                    {"error", "users_reload_failed"},
                    {"message", "failed to reload users"}
                });
                return;
            }

            auto cur = c.users->get(actor_fp);
            if (!cur.has_value()) {
                reply(404, json{
                    {"ok", false},
                    {"error", "not_found"},
                    {"message", "user not found"}
                });
                return;
            }

            remove_all_avatar_variants(actor_fp);

            pqnas::UserRec u = *cur;
            u.avatar_url.clear();

            const bool ok_upsert = c.users->upsert(u);
            const bool ok_save = ok_upsert ? c.users->save(c.users_path) : false;

            {
                pqnas::AuditEvent ev;
                ev.event = "user.avatar_remove";
                ev.outcome = (ok_upsert && ok_save) ? "ok" : "fail";
                ev.f["actor_fp"] = actor_fp;
                ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
                c.audit_append(ev);
            }

            if (!ok_upsert || !ok_save) {
                reply(500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "avatar metadata save failed"}
                });
                return;
            }

            reply(200, json{
                {"ok", true}
            });
        }
    );


}
