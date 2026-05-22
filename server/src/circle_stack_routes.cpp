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

    sqlite3_exec(g_db,
        "ALTER TABLE posts ADD COLUMN visibility TEXT DEFAULT 'public'",
        nullptr, nullptr, nullptr);

    sqlite3_exec(g_db,
        "ALTER TABLE posts ADD COLUMN circle_allow TEXT DEFAULT '[]'",
        nullptr, nullptr, nullptr);

    sqlite3_exec(g_db,
        "CREATE TABLE IF NOT EXISTS circle_edges ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "created_epoch INTEGER,"
        "user_a_fp TEXT,"
        "user_b_fp TEXT,"
        "source_intro_id INTEGER"
        ")",
        nullptr, nullptr, nullptr);

    sqlite3_exec(g_db,
        "CREATE UNIQUE INDEX IF NOT EXISTS idx_circle_edges_pair_intro "
        "ON circle_edges(user_a_fp, user_b_fp, source_intro_id)",
        nullptr, nullptr, nullptr);


    sqlite3_exec(g_db,
        "CREATE TABLE IF NOT EXISTS introductions ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "created_epoch INTEGER,"
        "introducer_fp TEXT,"
        "person_a_fp TEXT,"
        "person_b_fp TEXT,"
        "message TEXT,"
        "status TEXT DEFAULT 'pending'"
        ")",
        nullptr, nullptr, nullptr);

    sqlite3_exec(g_db,
        "CREATE TABLE IF NOT EXISTS contact_requests ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "created_epoch INTEGER NOT NULL,"
        "from_fp TEXT NOT NULL,"
        "to_fp TEXT NOT NULL,"
        "status TEXT NOT NULL DEFAULT 'pending',"
        "message TEXT NOT NULL DEFAULT '',"
        "UNIQUE(from_fp, to_fp)"
        ")",
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

                bool can_see = false;

                if (owner_fp == actor_fp) {
                    can_see = true;
                } else if (visibility == "public") {
                    can_see = true;
                } else if (visibility == "circle") {
                    sqlite3_stmt* cst = nullptr;
                    sqlite3_prepare_v2(g_db,
                        "SELECT 1 FROM circle_edges WHERE "
                        "((user_a_fp = ? AND user_b_fp = ?) OR "
                        "(user_a_fp = ? AND user_b_fp = ?)) LIMIT 1",
                        -1, &cst, nullptr);

                    sqlite3_bind_text(cst, 1, owner_fp.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(cst, 2, actor_fp.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(cst, 3, actor_fp.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(cst, 4, owner_fp.c_str(), -1, SQLITE_TRANSIENT);

                    can_see = sqlite3_step(cst) == SQLITE_ROW;
                    sqlite3_finalize(cst);
                }

                if (!can_see) continue;

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


    server.Get("/api/v4/circlestack/search_users",
        [&](const httplib::Request& req, httplib::Response& res) {
            std::string actor_fp, actor_role;
            if (!deps.require_user_auth_users_actor ||
                !deps.require_user_auth_users_actor(
                    req, res, deps.cookie_key, deps.users, &actor_fp, &actor_role)) {
                return;
            }

            std::string q = req.has_param("q") ? req.get_param_value("q") : "";
            if (q.size() < 2) {
                return set_json(res, {{"ok", true}, {"users", json::array()}});
            }

            std::transform(q.begin(), q.end(), q.begin(),
                [](unsigned char c){ return std::tolower(c); });

            json out;
            out["ok"] = true;
            out["users"] = json::array();

            if (deps.users) {
                auto snap = deps.users->snapshot();
                int count = 0;

                for (const auto& kv : snap) {
                    const auto& u = kv.second;
                    if (u.status != "enabled") continue;
                    if (u.fingerprint == actor_fp) continue;

                    std::string name = u.name;
                    std::string fp = u.fingerprint;

                    std::string hay = name + " " + fp;
                    std::transform(hay.begin(), hay.end(), hay.begin(),
                        [](unsigned char c){ return std::tolower(c); });

                    if (hay.find(q) == std::string::npos) continue;

                    json item;
                    item["fingerprint"] = fp;
                    item["fp_short"] = fp.size() >= 8 ? fp.substr(0, 8) : fp;
                    item["name"] = name.empty() ? item["fp_short"].get<std::string>() : name;
                    item["role"] = u.role;
                    item["avatar_url"] = u.avatar_url;

                    out["users"].push_back(item);
                    if (++count >= 20) break;
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
server.Post("/api/v4/circlestack/introductions/create",
        [&](const httplib::Request& req, httplib::Response& res) {
            std::string actor_fp, actor_role;
            if (!deps.require_user_auth_users_actor ||
                !deps.require_user_auth_users_actor(
                    req, res, deps.cookie_key, deps.users, &actor_fp, &actor_role)) {
                return;
            }

            cs_db_init();

            json body = json::parse(req.body, nullptr, false);
            if (!body.is_object()) {
                return set_json(res, {{"ok", false}, {"error", "invalid json"}});
            }

            const std::string a = body.value("person_a_fp", "");
            const std::string b = body.value("person_b_fp", "");
            const std::string msg = body.value("message", "");

            if (a.empty() || b.empty() || a == b) {
                return set_json(res, {{"ok", false}, {"error", "invalid people"}});
            }

            const std::time_t now = std::time(nullptr);

            sqlite3_stmt* st = nullptr;
            sqlite3_prepare_v2(g_db,
                "INSERT INTO introductions (created_epoch, introducer_fp, person_a_fp, person_b_fp, message) "
                "VALUES (?, ?, ?, ?, ?)",
                -1, &st, nullptr);

            sqlite3_bind_int64(st, 1, (sqlite3_int64)now);
            sqlite3_bind_text(st, 2, actor_fp.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 3, a.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 4, b.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 5, msg.c_str(), -1, SQLITE_TRANSIENT);

            sqlite3_step(st);
            sqlite3_finalize(st);

            set_json(res, {{"ok", true}, {"status", "pending"}});
        });


    server.Get("/api/v4/circlestack/introductions",
        [&](const httplib::Request& req, httplib::Response& res) {
            std::string actor_fp, actor_role;
            if (!deps.require_user_auth_users_actor ||
                !deps.require_user_auth_users_actor(
                    req, res, deps.cookie_key, deps.users, &actor_fp, &actor_role)) {
                return;
            }

            cs_db_init();

            json out = json::array();

            sqlite3_stmt* st = nullptr;
            sqlite3_prepare_v2(g_db,
                "SELECT id, created_epoch, introducer_fp, person_a_fp, person_b_fp, message, status "
                "FROM introductions "
                "WHERE introducer_fp = ? OR person_a_fp = ? OR person_b_fp = ? "
                "ORDER BY created_epoch DESC",
                -1, &st, nullptr);

            sqlite3_bind_text(st, 1, actor_fp.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 2, actor_fp.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 3, actor_fp.c_str(), -1, SQLITE_TRANSIENT);

            while (sqlite3_step(st) == SQLITE_ROW) {
                json row = {
                    {"id", sqlite3_column_int64(st, 0)},
                    {"created_epoch", sqlite3_column_int64(st, 1)},
                    {"introducer_fp", (const char*)sqlite3_column_text(st, 2)},
                    {"person_a_fp", (const char*)sqlite3_column_text(st, 3)},
                    {"person_b_fp", (const char*)sqlite3_column_text(st, 4)},
                    {"message", (const char*)sqlite3_column_text(st, 5)},
                    {"status", (const char*)sqlite3_column_text(st, 6)}
                };
                out.push_back(row);
            }

            sqlite3_finalize(st);

            set_json(res, {{"ok", true}, {"items", out}});
        });


    server.Post("/api/v4/circlestack/introductions/respond",
        [&](const httplib::Request& req, httplib::Response& res) {
            std::string actor_fp, actor_role;
            if (!deps.require_user_auth_users_actor ||
                !deps.require_user_auth_users_actor(
                    req, res, deps.cookie_key, deps.users, &actor_fp, &actor_role)) {
                return;
            }

            cs_db_init();

            json body = json::parse(req.body, nullptr, false);
            if (!body.is_object()) {
                return set_json(res, {{"ok", false}, {"error", "invalid json"}});
            }

            const int id = body.value("id", 0);
            const std::string action = body.value("action", "");

            if (id <= 0 || (action != "accept" && action != "dismiss")) {
                return set_json(res, {{"ok", false}, {"error", "invalid input"}});
            }

            // tarkista että käyttäjä kuuluu tähän introon
            sqlite3_stmt* st = nullptr;
            sqlite3_prepare_v2(g_db,
                "SELECT person_a_fp, person_b_fp FROM introductions WHERE id = ?",
                -1, &st, nullptr);

            sqlite3_bind_int(st, 1, id);

            std::string a, b;
            if (sqlite3_step(st) == SQLITE_ROW) {
                a = (const char*)sqlite3_column_text(st, 0);
                b = (const char*)sqlite3_column_text(st, 1);
            }
            sqlite3_finalize(st);

            if (actor_fp != a && actor_fp != b) {
                return set_json(res, {{"ok", false}, {"error", "not allowed"}});
            }

            if (action == "accept") {
                sqlite3_stmt* st2 = nullptr;
                sqlite3_prepare_v2(g_db,
                    "INSERT OR IGNORE INTO circle_edges (created_epoch, user_a_fp, user_b_fp, source_intro_id) VALUES (?, ?, ?, ?)",
                    -1, &st2, nullptr);
                sqlite3_bind_int64(st2, 1, (sqlite3_int64)std::time(nullptr));
                sqlite3_bind_text(st2, 2, a.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(st2, 3, b.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(st2, 4, id);
                sqlite3_step(st2);
                sqlite3_finalize(st2);
            }

            sqlite3_prepare_v2(g_db,
                "UPDATE introductions SET status = ? WHERE id = ?",
                -1, &st, nullptr);

            sqlite3_bind_text(st, 1, action.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(st, 2, id);

            sqlite3_step(st);
            sqlite3_finalize(st);

            set_json(res, {{"ok", true}});
        });


    server.Get("/api/v4/circlestack/people",
        [&](const httplib::Request& req, httplib::Response& res) {
            std::string actor_fp, actor_role;
            if (!deps.require_user_auth_users_actor ||
                !deps.require_user_auth_users_actor(req, res, deps.cookie_key, deps.users, &actor_fp, &actor_role)) return;

            cs_db_init();
            json out = json::array();

            sqlite3_stmt* st = nullptr;
            sqlite3* people_db = nullptr;
            if (sqlite3_open("/srv/pqnas/config/people_contacts.sqlite3", &people_db) != SQLITE_OK) {
                if (people_db) sqlite3_close(people_db);
                return set_json(res, {{"ok", false}, {"error", "people db open failed"}});
            }

            sqlite3_prepare_v2(people_db,
                "SELECT subject_fingerprint, subject_kind, display_name "
                "FROM people_contacts "
                "WHERE owner_fingerprint = ? "
                "ORDER BY display_name COLLATE NOCASE",
                -1, &st, nullptr);
            sqlite3_bind_text(st, 1, actor_fp.c_str(), -1, SQLITE_TRANSIENT);

            while (sqlite3_step(st) == SQLITE_ROW) {
                const char* fp = (const char*)sqlite3_column_text(st, 0);
                const char* kind = (const char*)sqlite3_column_text(st, 1);
                const char* name = (const char*)sqlite3_column_text(st, 2);
                std::string source = kind ? kind : "";

                out.push_back({
                    {"fp", fp ? fp : ""},
                    {"source", source},
                    {"display_name", name ? name : ""},
                    {"circle_capable", source == "local_user"}
                });
            }

            sqlite3_close(people_db);
            sqlite3_finalize(st);

            set_json(res, {{"ok", true}, {"items", out}});
        });

    server.Post("/api/v4/circlestack/people/add",
        [&](const httplib::Request& req, httplib::Response& res) {
            std::string actor_fp, actor_role;
            if (!deps.require_user_auth_users_actor ||
                !deps.require_user_auth_users_actor(req, res, deps.cookie_key, deps.users, &actor_fp, &actor_role)) return;

            cs_db_init();

            json body = json::parse(req.body, nullptr, false);
            if (!body.is_object()) return set_json(res, {{"ok", false}, {"error", "invalid json"}});

            const std::string other = body.value("fp", "");
            if (other.empty() || other == actor_fp) {
                return set_json(res, {{"ok", false}, {"error", "invalid fp"}});
            }

            sqlite3_stmt* st = nullptr;
            sqlite3_prepare_v2(g_db,
                "INSERT OR IGNORE INTO contact_requests "
                "(created_epoch, from_fp, to_fp, status, message) "
                "VALUES (?, ?, ?, 'pending', '')",
                -1, &st, nullptr);

            sqlite3_bind_int64(st, 1, (sqlite3_int64)std::time(nullptr));
            sqlite3_bind_text(st, 2, actor_fp.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 3, other.c_str(), -1, SQLITE_TRANSIENT);

            sqlite3_step(st);
            sqlite3_finalize(st);

            set_json(res, {{"ok", true}});
        });



    server.Get("/api/v4/circlestack/contact/requests",
        [&](const httplib::Request& req, httplib::Response& res) {
            std::string actor_fp, actor_role;
            if (!deps.require_user_auth_users_actor ||
                !deps.require_user_auth_users_actor(req, res, deps.cookie_key, deps.users, &actor_fp, &actor_role)) return;

            cs_db_init();

            json out;
            out["ok"] = true;
            out["incoming"] = json::array();
            out["outgoing"] = json::array();

            sqlite3_stmt* st = nullptr;
            sqlite3_prepare_v2(g_db,
                "SELECT id, from_fp, to_fp, status, message, created_epoch "
                "FROM contact_requests WHERE from_fp = ? OR to_fp = ? "
                "ORDER BY created_epoch DESC",
                -1, &st, nullptr);

            sqlite3_bind_text(st, 1, actor_fp.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 2, actor_fp.c_str(), -1, SQLITE_TRANSIENT);

            while (sqlite3_step(st) == SQLITE_ROW) {
                std::string from = (const char*)sqlite3_column_text(st, 1);
                std::string to = (const char*)sqlite3_column_text(st, 2);

                json item = {
                    {"id", sqlite3_column_int(st, 0)},
                    {"from_fp", from},
                    {"to_fp", to},
                    {"status", (const char*)sqlite3_column_text(st, 3)},
                    {"message", (const char*)sqlite3_column_text(st, 4)},
                    {"created_epoch", sqlite3_column_int64(st, 5)}
                };

                if (to == actor_fp) out["incoming"].push_back(item);
                else out["outgoing"].push_back(item);
            }

            sqlite3_finalize(st);
            set_json(res, out);
        });

    server.Post("/api/v4/circlestack/contact/respond",
        [&](const httplib::Request& req, httplib::Response& res) {
            std::string actor_fp, actor_role;
            if (!deps.require_user_auth_users_actor ||
                !deps.require_user_auth_users_actor(req, res, deps.cookie_key, deps.users, &actor_fp, &actor_role)) return;

            cs_db_init();

            json body = json::parse(req.body, nullptr, false);
            if (!body.is_object()) return set_json(res, {{"ok", false}, {"error", "invalid json"}});

            const int id = body.value("id", 0);
            const std::string action = body.value("action", "");

            if (id <= 0 || (action != "accept" && action != "reject")) {
                return set_json(res, {{"ok", false}, {"error", "invalid input"}});
            }

            sqlite3_stmt* st = nullptr;
            sqlite3_prepare_v2(g_db,
                "SELECT from_fp, to_fp FROM contact_requests WHERE id = ? AND status = 'pending'",
                -1, &st, nullptr);

            sqlite3_bind_int(st, 1, id);

            std::string from_fp, to_fp;
            if (sqlite3_step(st) == SQLITE_ROW) {
                from_fp = (const char*)sqlite3_column_text(st, 0);
                to_fp = (const char*)sqlite3_column_text(st, 1);
            }
            sqlite3_finalize(st);

            if (from_fp.empty() || to_fp.empty()) {
                return set_json(res, {{"ok", false}, {"error", "not found"}});
            }

            if (actor_fp != to_fp) {
                return set_json(res, {{"ok", false}, {"error", "not allowed"}});
            }

            const sqlite3_int64 now = (sqlite3_int64)std::time(nullptr);

            if (action == "accept") {
                sqlite3* people_db = nullptr;
                if (sqlite3_open("/srv/pqnas/config/people_contacts.sqlite3", &people_db) != SQLITE_OK) {
                    if (people_db) sqlite3_close(people_db);
                    return set_json(res, {{"ok", false}, {"error", "people db open failed"}});
                }

                auto insert_contact = [&](const std::string& owner, const std::string& other) {
                    std::string name = other.size() >= 8 ? other.substr(0, 8) : other;
                    if (deps.users) {
                        auto u = deps.users->get(other);
                        if (u.has_value() && !u->name.empty()) name = u->name;
                    }

                    sqlite3_stmt* st2 = nullptr;
                    sqlite3_prepare_v2(people_db,
                        "INSERT OR IGNORE INTO people_contacts "
                        "(owner_fingerprint, subject_user_id, subject_fingerprint, subject_kind, display_name, nickname, notes, created_at_epoch, updated_at_epoch) "
                        "VALUES (?, '', ?, 'local_user', ?, '', 'Accepted contact', ?, ?)",
                        -1, &st2, nullptr);

                    sqlite3_bind_text(st2, 1, owner.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(st2, 2, other.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(st2, 3, name.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int64(st2, 4, now);
                    sqlite3_bind_int64(st2, 5, now);

                    sqlite3_step(st2);
                    sqlite3_finalize(st2);
                };

                insert_contact(from_fp, to_fp);
                insert_contact(to_fp, from_fp);
                sqlite3_close(people_db);

                sqlite3_prepare_v2(g_db,
                    "INSERT OR IGNORE INTO circle_edges "
                    "(created_epoch, user_a_fp, user_b_fp, source_intro_id) "
                    "VALUES (?, ?, ?, NULL)",
                    -1, &st, nullptr);

                sqlite3_bind_int64(st, 1, now);
                sqlite3_bind_text(st, 2, from_fp.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(st, 3, to_fp.c_str(), -1, SQLITE_TRANSIENT);

                sqlite3_step(st);
                sqlite3_finalize(st);
            }

            sqlite3_prepare_v2(g_db,
                "UPDATE contact_requests SET status = ? WHERE id = ?",
                -1, &st, nullptr);

            sqlite3_bind_text(st, 1, action.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(st, 2, id);

            sqlite3_step(st);
            sqlite3_finalize(st);

            set_json(res, {{"ok", true}});
        });


    server.Get("/api/v4/circlestack/circle",
        [&](const httplib::Request& req, httplib::Response& res) {
            std::string actor_fp, actor_role;
            if (!deps.require_user_auth_users_actor ||
                !deps.require_user_auth_users_actor(
                    req, res, deps.cookie_key, deps.users, &actor_fp, &actor_role)) {
                return;
            }

            cs_db_init();
            json out = json::array();

            sqlite3_stmt* st = nullptr;
            sqlite3_prepare_v2(g_db,
                "SELECT user_a_fp, user_b_fp FROM circle_edges "
                "WHERE user_a_fp = ? OR user_b_fp = ?",
                -1, &st, nullptr);

            sqlite3_bind_text(st, 1, actor_fp.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 2, actor_fp.c_str(), -1, SQLITE_TRANSIENT);

            while (sqlite3_step(st) == SQLITE_ROW) {
                std::string a = (const char*)sqlite3_column_text(st, 0);
                std::string b = (const char*)sqlite3_column_text(st, 1);
                out.push_back({
                    {"fp", a == actor_fp ? b : a},
                    {"source", "circle"}
                });
            }

            sqlite3_finalize(st);
            set_json(res, {{"ok", true}, {"items", out}});
        });

    server.Post("/api/v4/circlestack/circle/remove",
        [&](const httplib::Request& req, httplib::Response& res) {
            std::string actor_fp, actor_role;
            if (!deps.require_user_auth_users_actor ||
                !deps.require_user_auth_users_actor(
                    req, res, deps.cookie_key, deps.users, &actor_fp, &actor_role)) {
                return;
            }

            cs_db_init();

            json body = json::parse(req.body, nullptr, false);
            if (!body.is_object()) {
                return set_json(res, {{"ok", false}, {"error", "invalid json"}});
            }

            const std::string other = body.value("fp", "");
            if (other.empty()) {
                return set_json(res, {{"ok", false}, {"error", "missing fp"}});
            }

            sqlite3_stmt* st = nullptr;
            sqlite3_prepare_v2(g_db,
                "DELETE FROM circle_edges WHERE "
                "(user_a_fp = ? AND user_b_fp = ?) OR "
                "(user_a_fp = ? AND user_b_fp = ?)",
                -1, &st, nullptr);

            sqlite3_bind_text(st, 1, actor_fp.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 2, other.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 3, other.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 4, actor_fp.c_str(), -1, SQLITE_TRANSIENT);

            sqlite3_step(st);
            sqlite3_finalize(st);

            set_json(res, {{"ok", true}});
        });
}
} // namespace pqnas
