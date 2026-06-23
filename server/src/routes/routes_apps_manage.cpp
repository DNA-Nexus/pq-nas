#include "routes_apps_manage.h"

#include "httplib.h"
#include "audit_fields.h"
#include "audit_log.h"
#include "users_registry.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

using json = nlohmann::json;

namespace {

AppsManageRoutesContext g_apps_ctx;

bool apps_context_ok() {
    return g_apps_ctx.users &&
           g_apps_ctx.cookie_key &&
           !g_apps_ctx.apps_installed_dir.empty() &&
           !g_apps_ctx.apps_bundled_dir.empty() &&
           !g_apps_ctx.server_version.empty() &&
           g_apps_ctx.require_user_auth &&
           g_apps_ctx.require_admin_cookie &&
           g_apps_ctx.is_admin_cookie &&
           g_apps_ctx.safe_app_id &&
           g_apps_ctx.safe_app_ver &&
           g_apps_ctx.load_app_launch_policy_json &&
           g_apps_ctx.save_app_launch_policy_json &&
           g_apps_ctx.normalize_app_launch_policy_json &&
           g_apps_ctx.normalize_app_launch_policy_entry &&
           g_apps_ctx.app_launch_policy_defaults_json &&
           g_apps_ctx.read_file_to_string &&
           g_apps_ctx.file_size_bytes_safe &&
           g_apps_ctx.sha256_file &&
           g_apps_ctx.apply_app_compatibility_fields &&
           g_apps_ctx.rand_hex_16 &&
           g_apps_ctx.run_cmd_capture &&
           g_apps_ctx.app_manifest_min_server_version &&
           g_apps_ctx.app_server_version_ok &&
           g_apps_ctx.app_compatibility_message &&
           g_apps_ctx.app_launch_value_ok &&
           g_apps_ctx.app_window_profile_ok &&
           g_apps_ctx.rel_to_repo &&
           g_apps_ctx.client_ip &&
           g_apps_ctx.now_iso_utc &&
           g_apps_ctx.audit_append;
}

bool require_user_auth_users_actor(
    const httplib::Request& req,
    httplib::Response& res,
    const unsigned char*,
    pqnas::UsersRegistry*,
    std::string* fp_hex,
    std::string* role
) {
    return g_apps_ctx.require_user_auth(req, res, fp_hex, role);
}

bool require_admin_cookie_users(
    const httplib::Request& req,
    httplib::Response& res,
    const unsigned char*,
    const std::string&,
    pqnas::UsersRegistry*
) {
    return g_apps_ctx.require_admin_cookie(req, res);
}

bool is_admin_cookie_users(
    const httplib::Request& req,
    const unsigned char*,
    pqnas::UsersRegistry*
) {
    return g_apps_ctx.is_admin_cookie(req);
}

bool safe_app_id(const std::string& id) {
    return g_apps_ctx.safe_app_id(id);
}

bool safe_app_ver(const std::string& ver) {
    return g_apps_ctx.safe_app_ver(ver);
}

json load_app_launch_policy_json() {
    return g_apps_ctx.load_app_launch_policy_json();
}

bool save_app_launch_policy_json(const json& j) {
    return g_apps_ctx.save_app_launch_policy_json(j);
}

json normalize_app_launch_policy_json(const json& j) {
    return g_apps_ctx.normalize_app_launch_policy_json(j);
}

json normalize_app_launch_policy_entry(const json& j) {
    return g_apps_ctx.normalize_app_launch_policy_entry(j);
}

json app_launch_policy_defaults_json() {
    return g_apps_ctx.app_launch_policy_defaults_json();
}

bool read_file_to_string(const std::string& path, std::string& out) {
    return g_apps_ctx.read_file_to_string(path, out);
}

long long file_size_bytes_safe(const std::string& path) {
    return g_apps_ctx.file_size_bytes_safe(path);
}

bool sha256_file(const std::filesystem::path& path, std::string* hex, std::string* err) {
    return g_apps_ctx.sha256_file(path, hex, err);
}

void apply_app_compatibility_fields(json& item, const json& manifest) {
    g_apps_ctx.apply_app_compatibility_fields(item, manifest);
}

std::string rand_hex_16() {
    return g_apps_ctx.rand_hex_16();
}

bool run_cmd_capture(const std::string& cmd, std::string* out, int* rc) {
    return g_apps_ctx.run_cmd_capture(cmd, out, rc);
}

std::string app_manifest_min_server_version(const json& mani) {
    return g_apps_ctx.app_manifest_min_server_version(mani);
}

bool app_server_version_ok(const std::string& min_server_version) {
    return g_apps_ctx.app_server_version_ok(min_server_version);
}

std::string app_compatibility_message(const std::string& min_server_version) {
    return g_apps_ctx.app_compatibility_message(min_server_version);
}

bool app_launch_value_ok(const std::string& v) {
    return g_apps_ctx.app_launch_value_ok(v);
}

bool app_window_profile_ok(const std::string& v) {
    return g_apps_ctx.app_window_profile_ok(v);
}

std::string rel_to_repo(const std::string& path) {
    return g_apps_ctx.rel_to_repo(path);
}

std::string client_ip(const httplib::Request& req) {
    return g_apps_ctx.client_ip(req);
}

std::string now_iso_utc() {
    return g_apps_ctx.now_iso_utc();
}

void audit_append(const pqnas::AuditEvent& ev) {
    g_apps_ctx.audit_append(ev);
}

void reply_apps_context_error(httplib::Response& res) {
    res.status = 500;
    res.set_header("Cache-Control", "no-store");
    res.set_content(json{
        {"ok", false},
        {"error", "server_error"},
        {"message", "apps route context incomplete"}
    }.dump(2), "application/json; charset=utf-8");
}

} // namespace

void register_apps_manage_routes(
    httplib::Server& srv,
    const AppsManageRoutesContext& ctx
) {
    g_apps_ctx = ctx;

    if (!apps_context_ok()) {
        srv.Get("/api/v4/apps/has", [](const httplib::Request&, httplib::Response& res) {
            reply_apps_context_error(res);
        });
        return;
    }

srv.Get("/api/v4/apps/has", [=](const httplib::Request& req, httplib::Response& res) {
    res.set_header("Cache-Control", "no-store");

    auto reply = [&](int status, const json& j) {
        res.status = status;
        res.set_content(j.dump(), "application/json; charset=utf-8");
    };

    std::string actor_fp, actor_role;
    if (!require_user_auth_users_actor(req, res, g_apps_ctx.cookie_key, g_apps_ctx.users, &actor_fp, &actor_role)) return;

    const std::string id = req.has_param("id") ? req.get_param_value("id") : "";

    auto valid_capability_app_id = [](const std::string& v) -> bool {
        if (v.empty() || v.size() > 64) return false;

        for (char c : v) {
            const bool ok =
                (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') ||
                c == '_' ||
                c == '-';

            if (!ok) return false;
        }

        return true;
    };

    if (!valid_capability_app_id(id) || !safe_app_id(id)) {
        reply(400, json{
            {"ok", false},
            {"error", "invalid_app_id"}
        });
        return;
    }

    json appLaunchPolicy = load_app_launch_policy_json();
    json appLaunchPolicyById = json::object();
    if (appLaunchPolicy.contains("by_app_id") && appLaunchPolicy["by_app_id"].is_object()) {
        appLaunchPolicyById = appLaunchPolicy["by_app_id"];
    }

    auto app_admin_only = [&](const std::string& appId) -> bool {
        try {
            if (!appLaunchPolicyById.contains(appId) || !appLaunchPolicyById[appId].is_object()) return false;
            const json& entry = appLaunchPolicyById[appId];
            return entry.contains("admin_only") && entry["admin_only"].is_boolean()
                ? entry["admin_only"].get<bool>()
                : false;
        } catch (...) {
            return false;
        }
    };

    const bool actor_is_admin = (actor_role == "admin");
    const bool allowed_by_role = !app_admin_only(id) || actor_is_admin;

    namespace fs = std::filesystem;
    std::error_code ec;

    bool installed = false;
    bool mobile = false;

    const fs::path app_root = fs::path(g_apps_ctx.apps_installed_dir) / id;

    if (fs::exists(app_root, ec) && fs::is_directory(app_root, ec) && !ec) {
        for (auto& de_ver : fs::directory_iterator(app_root, ec)) {
            if (ec) break;
            if (!de_ver.is_directory()) continue;

            const std::string ver = de_ver.path().filename().string();
            if (!safe_app_ver(ver)) continue;

            const fs::path manifest = de_ver.path() / "manifest.json";
            if (!fs::exists(manifest, ec) || ec) continue;

            installed = true;

            std::string body;
            json mj;
            if (read_file_to_string(manifest.string(), body) && !body.empty()) {
                try {
                    mj = json::parse(body);
                } catch (...) {
                    mj = json::object();
                }
            }

            // Future manifest-friendly mobile/surfaces handling.
            // Default to true for installed apps so older manifests can still be used by mobile.
            bool manifest_mobile = true;

            try {
                if (mj.is_object()) {
                    if (mj.contains("mobile") && mj["mobile"].is_boolean()) {
                        manifest_mobile = mj["mobile"].get<bool>();
                    } else if (mj.contains("surfaces") && mj["surfaces"].is_object()) {
                        const json& surfaces = mj["surfaces"];
                        if (surfaces.contains("mobile") && surfaces["mobile"].is_boolean()) {
                            manifest_mobile = surfaces["mobile"].get<bool>();
                        }
                    }
                }
            } catch (...) {
                manifest_mobile = true;
            }

            if (manifest_mobile) {
                mobile = true;
            }

            break;
        }
    }

    const bool available = installed && mobile && allowed_by_role;

    {
        pqnas::AuditEvent ev;
        ev.event = "v4.apps_has";
        ev.outcome = "ok";
        ev.f["actor_fp"] = actor_fp;
        ev.f["role"] = actor_role;
        ev.f["app_id"] = id;
        ev.f["available"] = available ? "true" : "false";
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
        audit_append(ev);
    }

    reply(200, json{
        {"ok", true},
        {"id", id},
        {"available", available},
        {"mobile", available}
    });
});

    srv.Get("/api/v4/apps/list", [=](const httplib::Request& req, httplib::Response& res) {
    json out;
    out["ok"] = true;
    out["installed"] = json::array();
    out["bundled"] = json::array();

    const bool isAdmin = is_admin_cookie_users(req, g_apps_ctx.cookie_key, g_apps_ctx.users);

    json appLaunchPolicy = load_app_launch_policy_json();
    json appLaunchPolicyById = json::object();
    if (appLaunchPolicy.contains("by_app_id") && appLaunchPolicy["by_app_id"].is_object()) {
        appLaunchPolicyById = appLaunchPolicy["by_app_id"];
    }

    auto app_admin_only = [&](const std::string& appId) -> bool {
        try {
            if (!appLaunchPolicyById.contains(appId) || !appLaunchPolicyById[appId].is_object()) return false;
            const json& entry = appLaunchPolicyById[appId];
            return entry.contains("admin_only") && entry["admin_only"].is_boolean()
                ? entry["admin_only"].get<bool>()
                : false;
        } catch (...) {
            return false;
        }
    };

    namespace fs = std::filesystem;
    std::error_code ec;

    // ---------------- installed: g_apps_ctx.apps_installed_dir/<id>/<ver>/manifest.json
    fs::path installed_root(g_apps_ctx.apps_installed_dir);
    if (fs::exists(installed_root, ec) && fs::is_directory(installed_root, ec) && !ec) {
        for (auto& de_id : fs::directory_iterator(installed_root, ec)) {
            if (ec) break;
            if (!de_id.is_directory()) continue;

            const std::string id = de_id.path().filename().string();
            if (!safe_app_id(id)) continue;

            if (app_admin_only(id) && !isAdmin) {
                continue;
            }

            for (auto& de_ver : fs::directory_iterator(de_id.path(), ec)) {
                if (ec) break;
                if (!de_ver.is_directory()) continue;

                const std::string ver = de_ver.path().filename().string();
                if (!safe_app_ver(ver)) continue;

                const fs::path manifest = de_ver.path() / "manifest.json";
                if (!fs::exists(manifest, ec) || ec) continue;

                std::string body;
                if (!read_file_to_string(manifest.string(), body) || body.empty()) continue;
                json mj;
                try {
                    mj = json::parse(body);
                } catch (...) {
                    continue; // skip invalid manifest
                }

                json item;
                item["id"] = id;
                item["ver"] = ver;

                // optional fields from manifest (don’t assume)
                if (mj.is_object()) {
                    if (mj.contains("name")) item["name"] = mj["name"];
                    if (mj.contains("title")) item["title"] = mj["title"];
                    if (mj.contains("description")) item["description"] = mj["description"];
                    if (mj.contains("entry")) item["entry"] = mj["entry"];
                    if (mj.contains("icon")) item["icon"] = mj["icon"];
                }

                apply_app_compatibility_fields(item, mj);

                // convenience: where it is on disk + what URL it should be served from
                item["path"] = de_ver.path().string();
                item["base_url"] = std::string("/apps/") + id + "/" + ver + "/";

                out["installed"].push_back(item);
            }
        }
    }

    // ---------------- bundled: g_apps_ctx.apps_bundled_dir/<id>/*.zip
    fs::path bundled_root(g_apps_ctx.apps_bundled_dir);
    if (fs::exists(bundled_root, ec) && fs::is_directory(bundled_root, ec) && !ec) {
        for (auto& de_id : fs::directory_iterator(bundled_root, ec)) {
            if (ec) break;
            if (!de_id.is_directory()) continue;

            const std::string id = de_id.path().filename().string();
            if (!safe_app_id(id)) continue;

            for (auto& de_zip : fs::directory_iterator(de_id.path(), ec)) {
                if (ec) break;
                if (!de_zip.is_regular_file()) continue;

                const fs::path p = de_zip.path();
                const std::string ext = p.extension().string();
                if (ext != ".zip") continue;

                json item;
                item["id"] = id;
                item["zip"] = p.filename().string();
                item["path"] = p.string();

                // size + sha256 best-effort (you already have sha256_file + hex helper)
                long long sz = file_size_bytes_safe(p.string());
                if (sz >= 0) item["size_bytes"] = sz;

                std::string hex, err;
                if (sha256_file(p, &hex, &err)) item["sha256"] = hex;

                out["bundled"].push_back(item);
            }
        }
    }

    res.status = 200;
    res.set_header("Cache-Control", "no-store");
    res.set_header("Content-Type", "application/json");
    res.body = out.dump(2);
});


    srv.Post("/api/v4/apps/upload_install", [=](const httplib::Request& req, httplib::Response& res) {
        if (!require_admin_cookie_users(req, res, g_apps_ctx.cookie_key, std::string{}, g_apps_ctx.users)) return;

        auto reply = [&](int status, const json& j) {
            res.status = status;
            res.set_header("Cache-Control", "no-store");
            res.set_content(j.dump(2), "application/json; charset=utf-8");
        };

        const std::string ct = req.get_header_value("Content-Type");
        const std::string origName = req.get_header_value("X-PQNAS-Filename");

        auto audit_fail = [&](const std::string& why) {
            pqnas::AuditEvent ev;
            ev.event = "admin.apps_upload_install";
            ev.outcome = "fail";
            if (!origName.empty()) ev.f["src"] = pqnas::shorten(origName, 160);
            ev.f["why"] = pqnas::shorten(why, 180);
            ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
            ev.f["ts"] = now_iso_utc();
            audit_append(ev);
        };

        if (ct.find("application/zip") == std::string::npos &&
            ct.find("application/octet-stream") == std::string::npos) {
            audit_fail("expected application/zip");
            reply(400, {{"ok", false}, {"error", "bad_request"}, {"message", "expected Content-Type: application/zip"}});
            return;
        }

    std::error_code ec;

    // Write uploaded zip to temp file
    const std::filesystem::path tmpZip =
        std::filesystem::path(g_apps_ctx.apps_installed_dir) / (".tmp_upload_" + rand_hex_16() + ".zip");

    {
        std::filesystem::create_directories(tmpZip.parent_path(), ec);
        if (ec) {
            reply(500, {{"ok", false}, {"error", "server_error"}, {"message", "failed to create temp dir"}});
            return;
        }

        std::ofstream f(tmpZip, std::ios::binary);
        if (!f.good()) {
            audit_fail("failed to open temp zip for write");
            reply(500, {{"ok", false}, {"error", "server_error"}, {"message", "failed to open temp zip for write"}});
            return;
        }
        f.write(req.body.data(), (std::streamsize)req.body.size());
        f.close();
        if (!f.good()) {
            std::filesystem::remove(tmpZip, ec);
            audit_fail("failed to write temp zip");
            reply(500, {{"ok", false}, {"error", "server_error"}, {"message", "failed to write temp zip"}});
            return;
        }
    }

    auto cleanupZip = [&]() {
        std::filesystem::remove(tmpZip, ec);
    };

    // Zip-slip defense: list entries and validate names
    {
        std::string listing;
        int rc = -1;
        const std::string cmd = "unzip -Z1 \"" + tmpZip.string() + "\" 2>/dev/null";
        if (!run_cmd_capture(cmd, &listing, &rc) || rc != 0 || listing.empty()) {
            audit_fail("zip unreadable or empty");
            cleanupZip();
            reply(400, {{"ok", false}, {"error", "bad_request"}, {"message", "zip unreadable or empty"}});
            return;
        }


        std::istringstream iss(listing);
        std::string line;
        int count = 0;
        while (std::getline(iss, line)) {
            if (line.empty()) continue;
            count++;
            if (count > 2000) { // sanity limit
                audit_fail("zip has too many entries");
                cleanupZip();
                reply(400, {{"ok", false}, {"error", "bad_request"}, {"message", "zip has too many entries"}});
                return;
            }

            // Reject absolute paths or Windows-style
            if (!line.empty() && (line[0] == '/' || line[0] == '\\')) {
                audit_fail("zip contains unsafe paths");
                cleanupZip();
                reply(400, {{"ok", false}, {"error", "bad_request"}, {"message", "zip contains unsafe paths"}});
                return;
            }
            if (line.find('\\') != std::string::npos) {
                audit_fail("zip contains unsafe paths");
                cleanupZip();
                reply(400, {{"ok", false}, {"error", "bad_request"}, {"message", "zip contains unsafe paths"}});
                return;
            }

            // Reject .. segments
            // (handles "../x", "a/../b", etc.)
            if (line == ".." || line.rfind("../", 0) == 0 || line.find("/../") != std::string::npos ||
                (line.size() >= 3 && line.compare(line.size()-3, 3, "/..") == 0)) {
                audit_fail("zip contains path traversal");
                cleanupZip();
                reply(400, {{"ok", false}, {"error", "bad_request"}, {"message", "zip contains path traversal"}});
                return;
            }
        }
    }

    // Read manifest.json from zip
    std::string manifest_txt;
    {
        std::string out;
        int rc = -1;
        const std::string cmd = "unzip -p \"" + tmpZip.string() + "\" manifest.json 2>/dev/null";
        if (!run_cmd_capture(cmd, &out, &rc) || rc != 0 || out.empty()) {
            cleanupZip();
            reply(400, {{"ok", false}, {"error", "bad_request"}, {"message", "manifest.json missing or unreadable in zip"}});
            return;
        }
        manifest_txt = out;
    }

    json mani;
    try { mani = json::parse(manifest_txt); }
    catch (...) {
        cleanupZip();
        audit_fail("manifest.json is not valid json");
        reply(400, {{"ok", false}, {"error", "bad_request"}, {"message", "manifest.json is not valid json"}});
        return;
    }

    const std::string id  = mani.value("id", "");
    const std::string ver = mani.value("version", "");
    if (!safe_app_id(id) || ver.empty() || ver.size() > 64) {
        cleanupZip();
        reply(400, {{"ok", false}, {"error", "bad_request"}, {"message", "manifest id/version invalid"}});
        return;
    }

    {
        const std::string minServerVersion = app_manifest_min_server_version(mani);
        if (!app_server_version_ok(minServerVersion)) {
            cleanupZip();
            const std::string msg = app_compatibility_message(minServerVersion);
            audit_fail(msg);
            reply(400, {
                {"ok", false},
                {"error", "incompatible_server"},
                {"message", msg},
                {"server_version", g_apps_ctx.server_version},
                {"min_server_version", minServerVersion}
            });
            return;
        }
    }

    {
        const std::string minServerVersion = app_manifest_min_server_version(mani);
        if (!app_server_version_ok(minServerVersion)) {
            const std::string msg = app_compatibility_message(minServerVersion);
            audit_fail(msg);
            reply(400, {
                {"ok", false},
                {"error", "incompatible_server"},
                {"message", msg},
                {"server_version", g_apps_ctx.server_version},
                {"min_server_version", minServerVersion}
            });
            return;
        }
    }

    const std::filesystem::path dst = std::filesystem::path(g_apps_ctx.apps_installed_dir) / id / ver;
    if (std::filesystem::exists(dst, ec) && !ec) {
        cleanupZip();
        audit_fail("version already installed");
        reply(409, {{"ok", false}, {"error", "conflict"}, {"message", "version already installed (remove first)"}});
        return;
    }

    // Extract to temp dir under g_apps_ctx.apps_installed_dir (runtime install area)
    const std::filesystem::path tmp =
        std::filesystem::path(g_apps_ctx.apps_installed_dir) / (".tmp_install_" + id + "_" + rand_hex_16());

    std::filesystem::create_directories(tmp, ec);
    if (ec) {
        audit_fail("failed to create temp dir");
        cleanupZip();
        reply(500, {{"ok", false}, {"error", "server_error"}, {"message", "failed to create temp dir"}});
        return;
    }

    // unzip into temp
    {
        std::string out;
        int rc = -1;
        const std::string cmd = "unzip -q \"" + tmpZip.string() + "\" -d \"" + tmp.string() + "\" 2>/dev/null";
        if (!run_cmd_capture(cmd, &out, &rc) || rc != 0) {
            std::filesystem::remove_all(tmp, ec);
            cleanupZip();
            reply(400, {{"ok", false}, {"error", "bad_request"}, {"message", "failed to extract zip"}});
            return;
        }
    }

    // Required structure
    if (!std::filesystem::exists(tmp / "manifest.json", ec) || ec ||
        !std::filesystem::exists(tmp / "www" / "index.html", ec) || ec) {
        std::filesystem::remove_all(tmp, ec);
        cleanupZip();
        audit_fail("zip missing required files");
        reply(400, {{"ok", false}, {"error", "bad_request"}, {"message", "zip missing required files (manifest.json, www/index.html)"}});
        return;
    }

    std::filesystem::create_directories(dst.parent_path(), ec);
    if (ec) {
        std::filesystem::remove_all(tmp, ec);
        cleanupZip();
        reply(500, {{"ok", false}, {"error", "server_error"}, {"message", "failed to create destination dir"}});
        return;
    }

    std::filesystem::rename(tmp, dst, ec);
    if (ec) {
        std::filesystem::remove_all(tmp, ec);
        cleanupZip();
        audit_fail("failed to finalize install");
        reply(500, {{"ok", false}, {"error", "server_error"}, {"message", "failed to finalize install"}});
        return;
    }

    cleanupZip();
        {
            pqnas::AuditEvent ev;
            ev.event = "admin.apps_upload_install";
            ev.outcome = "ok";
            ev.f["id"] = id;
            ev.f["version"] = ver;
            if (!origName.empty()) ev.f["src"] = pqnas::shorten(origName, 160);
            ev.f["bytes"] = std::to_string(req.body.size());
            ev.f["ip"] = client_ip(req);
            ev.f["ts"] = now_iso_utc();
            audit_append(ev);
        }

    reply(200, {{"ok", true}, {"id", id}, {"version", ver}, {"root", rel_to_repo(dst.string())}, {"src", origName}});
});

srv.Post("/api/v4/apps/install_bundled", [=](const httplib::Request& req, httplib::Response& res) {
    auto reply = [&](int status, const json& j) {
        res.status = status;
        res.set_header("Cache-Control", "no-store");
        res.set_content(j.dump(2), "application/json; charset=utf-8");
    };
    //only admins can install apps
    if (!require_admin_cookie_users(req, res, g_apps_ctx.cookie_key, std::string{}, g_apps_ctx.users)) return;

    auto audit_fail = [&](const std::string& why) {
        pqnas::AuditEvent ev;
        ev.event = "admin.apps_install_bundled";
        ev.outcome = "fail";
        ev.f["why"] = pqnas::shorten(why, 180);
        ev.f["ip"] = client_ip(req);
        ev.f["ts"] = now_iso_utc();
        audit_append(ev);
    };

    json in;
    try { in = json::parse(req.body); }
    catch (...) {
        reply(400, {{"ok", false}, {"error", "bad_request"}, {"message", "invalid json"}});
        return;
    }

    const std::string id  = in.value("id", "");
    const std::string zip = in.value("zip", "");

    if (!safe_app_id(id) || zip.empty() ||
        zip.find('/') != std::string::npos || zip.find('\\') != std::string::npos) {
        reply(400, {{"ok", false}, {"error", "bad_request"}, {"message", "bad id or zip"}});
        return;
    }

    std::error_code ec;
    const std::filesystem::path zip_path = std::filesystem::path(g_apps_ctx.apps_bundled_dir) / id / zip;

    if (!std::filesystem::exists(zip_path, ec) || ec) {
        reply(404, {{"ok", false}, {"error", "not_found"}, {"message", "bundled zip not found"}});
        return;
    }

    // Read manifest.json from zip
    std::string manifest_txt;
    int code = -1;
    {
        const std::string cmd = "unzip -p \"" + zip_path.string() + "\" manifest.json 2>/dev/null";
        if (!run_cmd_capture(cmd, &manifest_txt, &code) || code != 0 || manifest_txt.empty()) {
            audit_fail("manifest.json missing or unreadable in zip");
            reply(400, {{"ok", false}, {"error", "bad_request"}, {"message", "manifest.json missing or unreadable in zip"}});
            return;
        }
    }

    json mani;
    try { mani = json::parse(manifest_txt); }
    catch (...) {
        reply(400, {{"ok", false}, {"error", "bad_request"}, {"message", "manifest.json is not valid json"}});
        return;
    }

    const std::string mid = mani.value("id", "");
    const std::string ver = mani.value("version", "");

    if (mid != id || !safe_app_id(mid) || ver.empty() || ver.size() > 64) {
        audit_fail("manifest id/version invalid");
        reply(400, {{"ok", false}, {"error", "bad_request"}, {"message", "manifest id/version invalid or mismatch"}});
        return;
    }

    const std::filesystem::path dst = std::filesystem::path(g_apps_ctx.apps_installed_dir) / id / ver;
    if (std::filesystem::exists(dst, ec) && !ec) {
        reply(409, {{"ok", false}, {"error", "conflict"}, {"message", "version already installed (remove first)"}});
        return;
    }

    // Extract to temp dir under g_apps_ctx.apps_installed_dir (runtime install area)
    const std::filesystem::path tmp =
        std::filesystem::path(g_apps_ctx.apps_installed_dir) / (".tmp_install_" + id + "_" + rand_hex_16());

    std::filesystem::create_directories(tmp, ec);
    if (ec) {
        audit_fail("failed to create temp dir");
        reply(500, {{"ok", false}, {"error", "server_error"}, {"message", "failed to create temp dir"}});
        return;
    }

    // unzip into temp
    {
        const std::string cmd = "unzip -q \"" + zip_path.string() + "\" -d \"" + tmp.string() + "\" 2>/dev/null";
        std::string out;
        int rc = -1;
        if (!run_cmd_capture(cmd, &out, &rc) || rc != 0) {
            std::filesystem::remove_all(tmp, ec);
            audit_fail("failed to extract zip");
            reply(400, {{"ok", false}, {"error", "bad_request"}, {"message", "failed to extract zip"}});
            return;
        }
    }

    // Required structure
    if (!std::filesystem::exists(tmp / "manifest.json", ec) || ec ||
        !std::filesystem::exists(tmp / "www" / "index.html", ec) || ec) {
        std::filesystem::remove_all(tmp, ec);
        reply(400, {{"ok", false}, {"error", "bad_request"}, {"message", "zip missing required files (manifest.json, www/index.html)"}});
        return;
    }

    std::filesystem::create_directories(dst.parent_path(), ec);
    if (ec) {
        std::filesystem::remove_all(tmp, ec);
        audit_fail("failed to create destination dir");
        reply(500, {{"ok", false}, {"error", "server_error"}, {"message", "failed to create destination dir"}});
        return;
    }

    std::filesystem::rename(tmp, dst, ec);
    if (ec) {
        std::filesystem::remove_all(tmp, ec);
        reply(500, {{"ok", false}, {"error", "server_error"}, {"message", "failed to finalize install"}});
        return;
    }

    const std::string adminOnlyHeader = req.get_header_value("X-PQNAS-Admin-Only");
    const bool installAdminOnly =
        adminOnlyHeader == "1" ||
        adminOnlyHeader == "true" ||
        adminOnlyHeader == "TRUE" ||
        adminOnlyHeader == "yes" ||
        adminOnlyHeader == "YES";

    {
        json pol = load_app_launch_policy_json();

        if (!pol.contains("by_app_id") || !pol["by_app_id"].is_object()) {
            pol["by_app_id"] = json::object();
        }

        json existing = json::object();
        if (pol["by_app_id"].contains(id) && pol["by_app_id"][id].is_object()) {
            existing = pol["by_app_id"][id];
        }

        json merged = app_launch_policy_defaults_json();
        json normalizedExisting = normalize_app_launch_policy_entry(existing);
        for (auto it = normalizedExisting.begin(); it != normalizedExisting.end(); ++it) {
            merged[it.key()] = it.value();
        }

        merged["admin_only"] = installAdminOnly;

        pol["by_app_id"][id] = merged;
        pol = normalize_app_launch_policy_json(pol);

        if (!save_app_launch_policy_json(pol)) {
            reply(500, {{"ok", false}, {"error", "save_failed"}, {"message", "installed app, but failed to save app visibility policy"}});
            return;
        }
    }

    reply(200, json{
        {"ok", true},
        {"id", id},
        {"version", ver},
        {"root", rel_to_repo(dst.string())},
        {"admin_only", installAdminOnly}
    });
});

srv.Post("/api/v4/apps/launch_policy", [=](const httplib::Request& req, httplib::Response& res) {
    auto reply = [&](int status, const json& j) {
        res.status = status;
        res.set_header("Cache-Control", "no-store");
        res.set_content(j.dump(2), "application/json; charset=utf-8");
    };

    if (!is_admin_cookie_users(req, g_apps_ctx.cookie_key, g_apps_ctx.users)) {
        reply(403, {
            {"ok", false},
            {"error", "forbidden"},
            {"message", "Admin access required"}
        });
        return;
    }

    json body = json::object();
    try {
        if (!req.body.empty()) body = json::parse(req.body);
    } catch (...) {
        reply(400, {
            {"ok", false},
            {"error", "bad_json"},
            {"message", "Request body must be valid JSON"}
        });
        return;
    }

    const std::string appId =
        (body.contains("id") && body["id"].is_string())
            ? body["id"].get<std::string>()
            : std::string{};

    const std::string defaultLaunch =
        (body.contains("default_launch") && body["default_launch"].is_string())
            ? body["default_launch"].get<std::string>()
            : std::string{};

    const std::string windowProfile =
        (body.contains("window_profile") && body["window_profile"].is_string())
            ? body["window_profile"].get<std::string>()
            : std::string{};

    const bool allowUserOverride =
        (body.contains("allow_user_override") && body["allow_user_override"].is_boolean())
            ? body["allow_user_override"].get<bool>()
            : true;

    const bool adminOnly =
        (body.contains("admin_only") && body["admin_only"].is_boolean())
            ? body["admin_only"].get<bool>()
            : false;

    if (appId.empty()) {
        reply(400, {
            {"ok", false},
            {"error", "missing_id"},
            {"message", "App id is required"}
        });
        return;
    }

    if (!app_launch_value_ok(defaultLaunch)) {
        reply(400, {
            {"ok", false},
            {"error", "invalid_default_launch"},
            {"message", "default_launch must be one of: auto, embedded, detached"}
        });
        return;
    }

    if (!app_window_profile_ok(windowProfile)) {
        reply(400, {
            {"ok", false},
            {"error", "invalid_window_profile"},
            {"message", "window_profile must be one of: auto, small, normal, large, full"}
        });
        return;
    }

    json pol = load_app_launch_policy_json();
    if (!pol.contains("by_app_id") || !pol["by_app_id"].is_object()) {
        pol["by_app_id"] = json::object();
    }

    pol["by_app_id"][appId] = json{
        {"default_launch", defaultLaunch},
        {"window_profile", windowProfile},
        {"allow_user_override", allowUserOverride},
        {"admin_only", adminOnly}
    };

    pol = normalize_app_launch_policy_json(pol);

    if (!save_app_launch_policy_json(pol)) {
        reply(500, {
            {"ok", false},
            {"error", "save_failed"},
            {"message", "Failed to save launch policy"}
        });
        return;
    }

    reply(200, {
        {"ok", true},
        {"id", appId},
        {"policy", pol["by_app_id"][appId]}
    });
});

srv.Post("/api/v4/apps/uninstall", [=](const httplib::Request& req, httplib::Response& res) {
    auto reply = [&](int status, const json& j) {
        res.status = status;
        res.set_header("Cache-Control", "no-store");
        res.set_content(j.dump(2), "application/json; charset=utf-8");
    };

    // only admins can uninstall apps
    if (!require_admin_cookie_users(req, res, g_apps_ctx.cookie_key, std::string{}, g_apps_ctx.users)) return;

    json in;
    try { in = json::parse(req.body); }
    catch (...) {
        // audit (fail)
        {
            pqnas::AuditEvent ev;
            ev.event = "admin.apps_uninstall";
            ev.outcome = "fail";
            ev.f["why"] = "invalid json";
            ev.f["ip"] = client_ip(req);
            ev.f["ts"] = now_iso_utc();
            audit_append(ev);
        }
        reply(400, {{"ok", false}, {"error", "bad_request"}, {"message", "invalid json"}});
        return;
    }

    const std::string id  = in.value("id", "");
    const std::string ver = in.value("version", "");

    auto audit_fail = [&](const std::string& why) {
        pqnas::AuditEvent ev;
        ev.event = "admin.apps_uninstall";
        ev.outcome = "fail";
        if (!id.empty())  ev.f["id"] = id;
        if (!ver.empty()) ev.f["version"] = ver;
        ev.f["why"] = pqnas::shorten(why, 180);
        ev.f["ip"] = client_ip(req);
        ev.f["ts"] = now_iso_utc();
        audit_append(ev);
    };

    auto audit_ok = [&]() {
        pqnas::AuditEvent ev;
        ev.event = "admin.apps_uninstall";
        ev.outcome = "ok";
        ev.f["id"] = id;
        ev.f["version"] = ver;
        ev.f["ip"] = client_ip(req);
        ev.f["ts"] = now_iso_utc();
        audit_append(ev);
    };

    if (!safe_app_id(id) || ver.empty() || ver.size() > 64) {
        audit_fail("bad id or version");
        reply(400, {{"ok", false}, {"error", "bad_request"}, {"message", "bad id or version"}});
        return;
    }

    std::error_code ec;
    const std::filesystem::path dst = std::filesystem::path(g_apps_ctx.apps_installed_dir) / id / ver;

    if (!std::filesystem::exists(dst, ec) || ec) {
        audit_fail("not installed");
        reply(404, {{"ok", false}, {"error", "not_found"}, {"message", "not installed"}});
        return;
    }

    std::filesystem::remove_all(dst, ec);
    if (ec) {
        audit_fail(std::string("failed to remove app: ") + ec.message());
        reply(500, {{"ok", false}, {"error", "server_error"}, {"message", "failed to remove app"}});
        return;
    }

    // Optional: remove empty appId dir
    const std::filesystem::path appDir = std::filesystem::path(g_apps_ctx.apps_installed_dir) / id;
    if (std::filesystem::exists(appDir, ec) && std::filesystem::is_directory(appDir, ec)) {
        bool empty = (std::filesystem::directory_iterator(appDir, ec) == std::filesystem::directory_iterator());
        if (!ec && empty) std::filesystem::remove(appDir, ec);
    }

    audit_ok();
    reply(200, {{"ok", true}, {"id", id}, {"version", ver}});
});



}
