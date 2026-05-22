#include "circle_stack_routes.h"
#include "storage_resolver.h"

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using json = nlohmann::json;

namespace {

static sqlite3* g_db = nullptr;

void set_json(httplib::Response& res, const json& body) {
    res.set_content(body.dump(), "application/json; charset=utf-8");
}

static void cs_db_init() {
    if (g_db) return;

    if (sqlite3_open("circlestack.db", &g_db) != SQLITE_OK) {
        fprintf(stderr, "CircleStack DB open failed\n");
        return;
    }

    const char* sql =
        "CREATE TABLE IF NOT EXISTS posts ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "text TEXT,"
        "media_path TEXT,"
        "created_epoch INTEGER"
        ");";

    sqlite3_exec(g_db, sql, nullptr, nullptr, nullptr);
    sqlite3_exec(g_db,
        "ALTER TABLE posts ADD COLUMN owner_fp TEXT DEFAULT ''",
        nullptr, nullptr, nullptr);
}

bool cs_path_has_no_symlink_components_below_root(
    const std::filesystem::path& root,
    const std::filesystem::path& target,
    std::string* err
) {
    std::error_code ec;

    const auto root_abs = std::filesystem::weakly_canonical(root, ec);
    if (ec) {
        if (err) *err = "root canonical failed: " + ec.message();
        return false;
    }

    if (!std::filesystem::exists(target, ec)) {
        if (err) *err = "target missing";
        return false;
    }

    std::filesystem::path cur = root_abs;
    const auto rel = std::filesystem::relative(target, root_abs, ec);
    if (ec) {
        if (err) *err = "relative failed: " + ec.message();
        return false;
    }

    for (const auto& part : rel) {
        cur /= part;

        auto st = std::filesystem::symlink_status(cur, ec);
        if (ec) {
            if (err) *err = "symlink_status failed: " + ec.message();
            return false;
        }

        if (std::filesystem::is_symlink(st)) {
            if (err) *err = "symlink component rejected";
            return false;
        }
    }

    return true;
}

std::string cs_mime_for_path(const std::filesystem::path& p) {
    std::string ext = p.extension().string();

    for (auto& c : ext) {
        c = static_cast<char>(std::tolower(c));
    }

    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".png") return "image/png";
    if (ext == ".webp") return "image/webp";
    if (ext == ".gif") return "image/gif";

    return "application/octet-stream";
}

} // namespace

namespace pqnas {

void register_circle_stack_routes(httplib::Server& server, const CircleStackRoutesDeps& deps) {
    server.Get("/api/v4/circlestack/feed",
        [&](const httplib::Request& req, httplib::Response& res) {
            std::string actor_fp;
            std::string actor_role;

            if (!deps.require_user_auth_users_actor ||
                !deps.require_user_auth_users_actor(
                    req, res, deps.cookie_key, deps.users, &actor_fp, &actor_role)) {
                return;
            }

            cs_db_init();

            json out;
            out["ok"] = true;
            out["posts"] = json::array();

            sqlite3_stmt* stmt = nullptr;
            sqlite3_prepare_v2(g_db,
                "SELECT id, text, media_path, created_epoch, owner_fp, visibility, circle_allow "
                "FROM posts ORDER BY id DESC",
                -1, &stmt, nullptr);

            while (sqlite3_step(stmt) == SQLITE_ROW) {
                json p;

                const int id = sqlite3_column_int(stmt, 0);
                const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                const char* media = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                const long long created = sqlite3_column_int64(stmt, 3);
                const char* owner = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
                const char* vis = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
                const std::string owner_fp = owner ? owner : "";
                const std::string visibility = vis ? vis : "public";

                std::string owner_display = owner_fp.size() >= 8 ? owner_fp.substr(0, 8) : owner_fp;

                if (deps.users && !owner_fp.empty()) {
                    auto u = deps.users->get(owner_fp);
                    if (u.has_value() && !u->name.empty()) {
                        owner_display = u->name;
                    }
                }

                p["id"] = id;
                p["text"] = text ? text : "";
                p["created_epoch"] = created;
                p["owner_display_name"] = owner_display;
                p["owner_fp_short"] = owner_fp.size() >= 8
                    ? owner_fp.substr(0, 8)
                    : owner_fp;
                p["visibility"] = visibility;

                if (media && media[0]) {
                    p["media_url"] = "/api/v4/circlestack/media?id=" + std::to_string(id);
                }

                out["posts"].push_back(p);
            }

            sqlite3_finalize(stmt);
            set_json(res, out);
        });


    server.Get("/api/v4/circlestack/users",
        [&](const httplib::Request& req, httplib::Response& res) {
            std::string actor_fp;
            std::string actor_role;
            if (!deps.require_user_auth_users_actor ||
                !deps.require_user_auth_users_actor(
                    req, res, deps.cookie_key, deps.users, &actor_fp, &actor_role)) {
                return;
            }

            json out;
            out["ok"] = true;
            out["users"] = json::array();

            if (deps.users) {
                auto snap = deps.users->snapshot();
                for (const auto& kv : snap) {
                    const auto& u = kv.second;
                    if (u.status != "enabled") continue;

                    json item;
                    item["fingerprint"] = u.fingerprint;
                    item["fp_short"] = u.fingerprint.size() >= 8 ? u.fingerprint.substr(0, 8) : u.fingerprint;
                    item["name"] = u.name.empty() ? item["fp_short"].get<std::string>() : u.name;
                    item["role"] = u.role;
                    item["avatar_url"] = u.avatar_url;
                    item["is_me"] = (u.fingerprint == actor_fp);
                    out["users"].push_back(item);
                }
            }

            set_json(res, out);
        });

    server.Post("/api/v4/circlestack/posts/create",
        [&](const httplib::Request& req, httplib::Response& res) {
            std::string actor_fp;
            std::string actor_role;

            if (!deps.require_user_auth_users_actor ||
                !deps.require_user_auth_users_actor(
                    req, res, deps.cookie_key, deps.users, &actor_fp, &actor_role)) {
                return;
            }

            json body;
            try {
                body = json::parse(req.body);
            } catch (...) {
                res.status = 400;
                set_json(res, {{"ok", false}, {"error", "invalid_json"}});
                return;
            }

            const std::string text = body.value("text", "");
            const std::string media_path = body.value("media_path", "");
const std::string visibility = body.value("visibility", "public");
            const std::string circle_allow = body.value("circle_allow", "[]");

            if (!media_path.empty()) {
                std::string rel_norm;
                std::string norm_err;

                if (!normalize_user_rel_path_strict(media_path, &rel_norm, &norm_err)) {
                    res.status = 400;
                    set_json(res, {{"ok", false}, {"error", "INVALID_MEDIA_PATH"}});
                    return;
                }
            }

            if (visibility != "public" && visibility != "private" && visibility != "circle") {
    res.status = 400;
    set_json(res, {{"ok", false}, {"error", "invalid_visibility"}});
    return;
}

if (text.empty() && media_path.empty()) {
                res.status = 400;
                set_json(res, {{"ok", false}, {"error", "empty_post"}});
                return;
            }

            const auto created_epoch = static_cast<long long>(std::time(nullptr));

            cs_db_init();

            sqlite3_stmt* stmt = nullptr;
            sqlite3_prepare_v2(g_db,
                "INSERT INTO posts(text, media_path, created_epoch, owner_fp, visibility, circle_allow) "
                "VALUES(?,?,?,?,?,?)",
                -1, &stmt, nullptr);

            sqlite3_bind_text(stmt, 1, text.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, media_path.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 3, created_epoch);
            sqlite3_bind_text(stmt, 4, actor_fp.c_str(), -1, SQLITE_TRANSIENT);
sqlite3_bind_text(stmt, 5, visibility.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 6, circle_allow.c_str(), -1, SQLITE_TRANSIENT);

            if (sqlite3_step(stmt) != SQLITE_DONE) {
                sqlite3_finalize(stmt);
                res.status = 500;
                set_json(res, {{"ok", false}, {"error", "db_insert_failed"}});
                return;
            }

            const long long id = sqlite3_last_insert_rowid(g_db);
            sqlite3_finalize(stmt);

            set_json(res, {{"ok", true}, {"id", id}});
        });

    server.Delete("/api/v4/circlestack/posts",
        [&](const httplib::Request& req, httplib::Response& res) {
            std::string actor_fp;
            std::string actor_role;

            if (!deps.require_user_auth_users_actor ||
                !deps.require_user_auth_users_actor(
                    req, res, deps.cookie_key, deps.users, &actor_fp, &actor_role)) {
                return;
            }

            if (!req.has_param("id")) {
                res.status = 400;
                set_json(res, {{"ok", false}, {"error", "missing_id"}});
                return;
            }

            const int id = std::atoi(req.get_param_value("id").c_str());

            cs_db_init();

            sqlite3_stmt* stmt = nullptr;
            sqlite3_prepare_v2(g_db,
                "SELECT owner_fp FROM posts WHERE id=?",
                -1, &stmt, nullptr);

            sqlite3_bind_int(stmt, 1, id);

            std::string owner_fp;
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                const char* o = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                if (o) owner_fp = o;
            }

            sqlite3_finalize(stmt);

            if (owner_fp.empty()) {
                res.status = 404;
                set_json(res, {{"ok", false}, {"error", "not_found"}});
                return;
            }

            if (owner_fp != actor_fp) {
                res.status = 403;
                set_json(res, {{"ok", false}, {"error", "forbidden"}});
                return;
            }

            sqlite3_prepare_v2(g_db,
                "DELETE FROM posts WHERE id=?",
                -1, &stmt, nullptr);

            sqlite3_bind_int(stmt, 1, id);

            if (sqlite3_step(stmt) != SQLITE_DONE) {
                sqlite3_finalize(stmt);
                res.status = 500;
                set_json(res, {{"ok", false}, {"error", "db_delete_failed"}});
                return;
            }

            sqlite3_finalize(stmt);

            set_json(res, {{"ok", true}, {"deleted_id", id}});
        });

    server.Get("/api/v4/circlestack/media",
        [&](const httplib::Request& req, httplib::Response& res) {
            std::string actor_fp;
            std::string actor_role;

            if (!deps.require_user_auth_users_actor ||
                !deps.require_user_auth_users_actor(
                    req, res, deps.cookie_key, deps.users, &actor_fp, &actor_role)) {
                return;
            }

            if (!req.has_param("id")) {
                res.status = 400;
                return;
            }

            const int id = std::atoi(req.get_param_value("id").c_str());

            cs_db_init();

            sqlite3_stmt* stmt = nullptr;
            sqlite3_prepare_v2(g_db,
                "SELECT media_path, owner_fp FROM posts WHERE id=?",
                -1, &stmt, nullptr);

            sqlite3_bind_int(stmt, 1, id);

            std::string media_path;
            std::string owner_fp;

            if (sqlite3_step(stmt) == SQLITE_ROW) {
                const char* m = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                const char* o = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));

                if (m) media_path = m;
                if (o) owner_fp = o;
            }

            sqlite3_finalize(stmt);

            if (media_path.empty()) {
                res.status = 404;
                return;
            }

            if (owner_fp.empty()) {
                res.status = 403;
                return;
            }

            std::string rel_norm;
            std::string norm_err;

            if (!normalize_user_rel_path_strict(media_path, &rel_norm, &norm_err)) {
                res.status = 400;
                set_json(res, {{"ok", false}, {"error", "INVALID_MEDIA_PATH"}});
                return;
            }

            if (!deps.users || !deps.user_dir_for_fp) {
                res.status = 500;
                return;
            }

            const std::filesystem::path owner_root =
                deps.user_dir_for_fp(*deps.users, owner_fp);

            const std::filesystem::path requested = owner_root / rel_norm;

            if (!std::filesystem::exists(requested)) {
                res.status = 404;
                return;
            }

            std::string symlink_err;
            if (!cs_path_has_no_symlink_components_below_root(
                    owner_root, requested, &symlink_err)) {
                res.status = 403;
                set_json(res, {{"ok", false}, {"error", "SYMLINK_REJECTED"}});
                return;
            }

            std::ifstream f(requested, std::ios::binary);
            if (!f) {
                res.status = 404;
                return;
            }

            std::stringstream ss;
            ss << f.rdbuf();

            const std::string mime = cs_mime_for_path(requested);
            res.set_content(ss.str(), mime.c_str());
        });
}

} // namespace pqnas