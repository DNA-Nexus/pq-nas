#include "circle_stack_routes.h"
#include "storage_resolver.h"

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using json = nlohmann::json;

namespace {

static sqlite3* g_db = nullptr;
static constexpr const char* kCircleStackDbPath = "/srv/pqnas/circlestack.db";

void set_json(httplib::Response& res, const json& body) {
    res.set_content(body.dump(), "application/json; charset=utf-8");
}

static void cs_db_init() {
    if (g_db) return;

    if (sqlite3_open(kCircleStackDbPath, &g_db) != SQLITE_OK) {
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
        "DELETE FROM circle_edges "
        "WHERE user_a_fp IS NULL OR user_b_fp IS NULL "
        "   OR user_a_fp = '' OR user_b_fp = '' "
        "   OR user_a_fp = user_b_fp",
        nullptr, nullptr, nullptr);

    sqlite3_exec(g_db,
        "UPDATE circle_edges "
        "SET user_a_fp = CASE WHEN user_a_fp > user_b_fp THEN user_b_fp ELSE user_a_fp END, "
        "    user_b_fp = CASE WHEN user_a_fp > user_b_fp THEN user_a_fp ELSE user_b_fp END "
        "WHERE user_a_fp > user_b_fp",
        nullptr, nullptr, nullptr);

    sqlite3_exec(g_db,
        "DELETE FROM circle_edges "
        "WHERE id NOT IN ("
        "  SELECT MIN(id) FROM circle_edges GROUP BY user_a_fp, user_b_fp"
        ")",
        nullptr, nullptr, nullptr);

    sqlite3_exec(g_db,
        "DROP INDEX IF EXISTS idx_circle_edges_pair_intro",
        nullptr, nullptr, nullptr);

    sqlite3_exec(g_db,
        "CREATE UNIQUE INDEX IF NOT EXISTS idx_circle_edges_pair "
        "ON circle_edges(user_a_fp, user_b_fp)",
        nullptr, nullptr, nullptr);

    sqlite3_exec(g_db,
        "CREATE INDEX IF NOT EXISTS idx_circle_edges_intro "
        "ON circle_edges(source_intro_id)",
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

    sqlite3_exec(g_db,
        "CREATE TABLE IF NOT EXISTS post_reactions ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "post_id INTEGER NOT NULL,"
        "actor_fp TEXT NOT NULL,"
        "reaction TEXT NOT NULL,"
        "created_epoch INTEGER NOT NULL,"
        "UNIQUE(post_id, actor_fp)"
        ")",
        nullptr, nullptr, nullptr);

    sqlite3_exec(g_db,
        "CREATE INDEX IF NOT EXISTS idx_post_reactions_post_id "
        "ON post_reactions(post_id)",
        nullptr, nullptr, nullptr);

    sqlite3_exec(g_db,
        "CREATE TABLE IF NOT EXISTS post_replies ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "post_id INTEGER NOT NULL,"
        "actor_fp TEXT NOT NULL,"
        "text TEXT NOT NULL DEFAULT '',"
        "media_path TEXT NOT NULL DEFAULT '',"
        "created_epoch INTEGER NOT NULL"
        ")",
        nullptr, nullptr, nullptr);

    sqlite3_exec(g_db,
        "CREATE INDEX IF NOT EXISTS idx_post_replies_post_id "
        "ON post_replies(post_id, created_epoch)",
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


static constexpr const char* kPeopleContactsDbPath =
    "/srv/pqnas/config/people_contacts.sqlite3";

std::string cs_short_fp(const std::string& fp) {
    return fp.size() >= 8 ? fp.substr(0, 8) : fp;
}

std::string cs_display_name_for_fp(
    const pqnas::CircleStackRoutesDeps& deps,
    const std::string& fp
) {
    std::string name = cs_short_fp(fp);

    if (deps.users && !fp.empty()) {
        auto u = deps.users->get(fp);
        if (u.has_value() && !u->name.empty()) {
            name = u->name;
        }
    }

    return name;
}

bool cs_open_people_db(sqlite3** out_db, std::string* err) {
    if (!out_db) return false;
    *out_db = nullptr;

    sqlite3* db = nullptr;
    if (sqlite3_open(kPeopleContactsDbPath, &db) != SQLITE_OK) {
        if (err) {
            err->assign(db ? sqlite3_errmsg(db) : "sqlite3_open failed");
        }
        if (db) sqlite3_close(db);
        return false;
    }

    *out_db = db;
    return true;
}

bool cs_insert_people_contact(
    sqlite3* people_db,
    const pqnas::CircleStackRoutesDeps& deps,
    const std::string& owner_fp,
    const std::string& subject_fp,
    sqlite3_int64 now,
    std::string* err
) {
    if (!people_db || owner_fp.empty() || subject_fp.empty() || owner_fp == subject_fp) {
        if (err) *err = "invalid people contact";
        return false;
    }

    const std::string display_name = cs_display_name_for_fp(deps, subject_fp);

    sqlite3_stmt* st = nullptr;
    const char* sql =
        "INSERT OR IGNORE INTO people_contacts "
        "(owner_fingerprint, subject_user_id, subject_fingerprint, subject_kind, "
        " display_name, nickname, notes, created_at_epoch, updated_at_epoch) "
        "VALUES (?, '', ?, 'local_user', ?, '', 'Accepted contact', ?, ?)";

    if (sqlite3_prepare_v2(people_db, sql, -1, &st, nullptr) != SQLITE_OK) {
        if (err) *err = sqlite3_errmsg(people_db);
        return false;
    }

    sqlite3_bind_text(st, 1, owner_fp.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, subject_fp.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, display_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 4, now);
    sqlite3_bind_int64(st, 5, now);

    const int rc = sqlite3_step(st);
    sqlite3_finalize(st);

    if (rc != SQLITE_DONE) {
        if (err) *err = sqlite3_errmsg(people_db);
        return false;
    }

    return true;
}

bool cs_insert_symmetric_people_contacts(
    const pqnas::CircleStackRoutesDeps& deps,
    const std::string& a_fp,
    const std::string& b_fp,
    sqlite3_int64 now,
    std::string* err
) {
    sqlite3* people_db = nullptr;
    if (!cs_open_people_db(&people_db, err)) {
        return false;
    }

    bool ok = true;

    if (sqlite3_exec(people_db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK) {
        if (err) *err = sqlite3_errmsg(people_db);
        ok = false;
    }

    if (ok) ok = cs_insert_people_contact(people_db, deps, a_fp, b_fp, now, err);
    if (ok) ok = cs_insert_people_contact(people_db, deps, b_fp, a_fp, now, err);

    if (ok) {
        if (sqlite3_exec(people_db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
            if (err) *err = sqlite3_errmsg(people_db);
            ok = false;
        }
    } else {
        sqlite3_exec(people_db, "ROLLBACK", nullptr, nullptr, nullptr);
    }

    sqlite3_close(people_db);
    return ok;
}

bool cs_canonical_circle_pair(
    const std::string& left,
    const std::string& right,
    std::string* out_a,
    std::string* out_b,
    std::string* err
) {
    if (left.empty() || right.empty() || left == right) {
        if (err) *err = "invalid circle pair";
        return false;
    }

    if (left < right) {
        *out_a = left;
        *out_b = right;
    } else {
        *out_a = right;
        *out_b = left;
    }

    return true;
}

bool cs_insert_circle_edge(
    sqlite3* db,
    const std::string& left_fp,
    const std::string& right_fp,
    sqlite3_int64 now,
    sqlite3_int64 source_intro_id,
    std::string* err
) {
    std::string a;
    std::string b;

    if (!cs_canonical_circle_pair(left_fp, right_fp, &a, &b, err)) {
        return false;
    }

    sqlite3_stmt* st = nullptr;
    const char* sql =
        "INSERT OR IGNORE INTO circle_edges "
        "(created_epoch, user_a_fp, user_b_fp, source_intro_id) "
        "VALUES (?, ?, ?, ?)";

    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
        if (err) *err = sqlite3_errmsg(db);
        return false;
    }

    sqlite3_bind_int64(st, 1, now);
    sqlite3_bind_text(st, 2, a.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, b.c_str(), -1, SQLITE_TRANSIENT);

    if (source_intro_id > 0) {
        sqlite3_bind_int64(st, 4, source_intro_id);
    } else {
        sqlite3_bind_null(st, 4);
    }

    const int rc = sqlite3_step(st);
    sqlite3_finalize(st);

    if (rc != SQLITE_DONE) {
        if (err) *err = sqlite3_errmsg(db);
        return false;
    }

    return true;
}


bool cs_is_supported_reaction(const std::string& reaction) {
    return reaction == "👍" ||
           reaction == "❤️" ||
           reaction == "😂" ||
           reaction == "😮" ||
           reaction == "👏" ||
           reaction == "🔥";
}

bool cs_actor_can_see_post(
    sqlite3* db,
    int post_id,
    const std::string& actor_fp,
    std::string* err
) {
    sqlite3_stmt* st = nullptr;

    if (sqlite3_prepare_v2(db,
            "SELECT owner_fp, visibility FROM posts WHERE id = ?",
            -1, &st, nullptr) != SQLITE_OK) {
        if (err) *err = sqlite3_errmsg(db);
        return false;
    }

    sqlite3_bind_int(st, 1, post_id);

    bool found = false;
    std::string owner_fp;
    std::string visibility = "public";

    if (sqlite3_step(st) == SQLITE_ROW) {
        found = true;

        const char* owner = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
        const char* vis = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));

        owner_fp = owner ? owner : "";
        visibility = vis ? vis : "public";
    }

    sqlite3_finalize(st);

    if (!found) {
        if (err) *err = "not_found";
        return false;
    }

    if (owner_fp == actor_fp) return true;
    if (visibility == "public") return true;

    if (visibility == "circle") {
        sqlite3_stmt* cst = nullptr;

        if (sqlite3_prepare_v2(db,
                "SELECT 1 FROM circle_edges WHERE "
                "((user_a_fp = ? AND user_b_fp = ?) OR "
                "(user_a_fp = ? AND user_b_fp = ?)) LIMIT 1",
                -1, &cst, nullptr) != SQLITE_OK) {
            if (err) *err = sqlite3_errmsg(db);
            return false;
        }

        sqlite3_bind_text(cst, 1, owner_fp.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(cst, 2, actor_fp.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(cst, 3, actor_fp.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(cst, 4, owner_fp.c_str(), -1, SQLITE_TRANSIENT);

        const bool ok = sqlite3_step(cst) == SQLITE_ROW;
        sqlite3_finalize(cst);

        if (ok) return true;
    }

    if (err) *err = "forbidden";
    return false;
}


json cs_load_post_replies(
    sqlite3* db,
    int post_id,
    const std::string& viewer_fp,
    const pqnas::CircleStackRoutesDeps& deps
) {
    json replies = json::array();

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "SELECT id, actor_fp, text, media_path, created_epoch "
            "FROM post_replies "
            "WHERE post_id = ? "
            "ORDER BY created_epoch ASC, id ASC",
            -1, &st, nullptr) != SQLITE_OK) {
        return replies;
    }

    sqlite3_bind_int(st, 1, post_id);

    while (sqlite3_step(st) == SQLITE_ROW) {
        const int id = sqlite3_column_int(st, 0);
        const char* actor_raw = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
        const char* text_raw = reinterpret_cast<const char*>(sqlite3_column_text(st, 2));
        const char* media_raw = reinterpret_cast<const char*>(sqlite3_column_text(st, 3));
        const sqlite3_int64 created = sqlite3_column_int64(st, 4);

        const std::string actor_fp = actor_raw ? actor_raw : "";
        const std::string media_path = media_raw ? media_raw : "";

        std::string display_name = cs_short_fp(actor_fp);
        std::string avatar_url;

        if (deps.users) {
            auto u = deps.users->get(actor_fp);
            if (u.has_value()) {
                if (!u->name.empty()) {
                    display_name = u->name;
                }
                avatar_url = u->avatar_url;
            }
        }

        json r = {
            {"id", id},
            {"post_id", post_id},
            {"actor_fp", actor_fp},
            {"actor_fp_short", cs_short_fp(actor_fp)},
            {"actor_display_name", display_name},
            {"actor_avatar_url", avatar_url},
            {"text", text_raw ? text_raw : ""},
            {"created_epoch", created},
            {"is_mine", actor_fp == viewer_fp}
        };

        if (!media_path.empty()) {
            r["media_url"] = "/api/v4/circlestack/reply/media?id=" + std::to_string(id);
        }

        replies.push_back(r);
    }

    sqlite3_finalize(st);
    return replies;
}


json cs_load_post_reactions(
    sqlite3* db,
    int post_id,
    const std::string& actor_fp,
    const pqnas::CircleStackRoutesDeps& deps,
    std::string* out_my_reaction
) {
    if (out_my_reaction) *out_my_reaction = "";

    json by_reaction = json::object();

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "SELECT actor_fp, reaction, created_epoch "
            "FROM post_reactions "
            "WHERE post_id = ? "
            "ORDER BY created_epoch ASC",
            -1, &st, nullptr) != SQLITE_OK) {
        return json::array();
    }

    sqlite3_bind_int(st, 1, post_id);

    while (sqlite3_step(st) == SQLITE_ROW) {
        const char* actor_raw = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
        const char* reaction_raw = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));

        const std::string reactor_fp = actor_raw ? actor_raw : "";
        const std::string reaction = reaction_raw ? reaction_raw : "";

        if (reactor_fp.empty() || !cs_is_supported_reaction(reaction)) {
            continue;
        }

        if (reactor_fp == actor_fp && out_my_reaction) {
            *out_my_reaction = reaction;
        }

        if (!by_reaction.contains(reaction)) {
            by_reaction[reaction] = {
                {"reaction", reaction},
                {"count", 0},
                {"reacted_by_me", false},
                {"people", json::array()}
            };
        }

        by_reaction[reaction]["count"] =
            by_reaction[reaction].value("count", 0) + 1;

        if (reactor_fp == actor_fp) {
            by_reaction[reaction]["reacted_by_me"] = true;
        }

        std::string display_name = cs_short_fp(reactor_fp);
        std::string avatar_url;

        if (deps.users) {
            auto u = deps.users->get(reactor_fp);
            if (u.has_value()) {
                if (!u->name.empty()) {
                    display_name = u->name;
                }
                avatar_url = u->avatar_url;
            }
        }

        by_reaction[reaction]["people"].push_back({
            {"fp", reactor_fp},
            {"fp_short", cs_short_fp(reactor_fp)},
            {"display_name", display_name},
            {"avatar_url", avatar_url}
        });
    }

    sqlite3_finalize(st);

    json out = json::array();

    const char* order[] = {"👍", "❤️", "😂", "😮", "👏", "🔥"};
    for (const char* r : order) {
        if (by_reaction.contains(r)) {
            out.push_back(by_reaction[r]);
        }
    }

    return out;
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
                std::string owner_avatar_url;

                if (deps.users && !owner_fp.empty()) {
                    auto u = deps.users->get(owner_fp);
                    if (u.has_value()) {
                        if (!u->name.empty()) {
                            owner_display = u->name;
                        }
                        owner_avatar_url = u->avatar_url;
                    }
                }

                p["id"] = id;
                p["text"] = text ? text : "";
                p["created_epoch"] = created;
                p["owner_fp"] = owner_fp;
                p["owner_display_name"] = owner_display;
                p["owner_fp_short"] = owner_fp.size() >= 8
                    ? owner_fp.substr(0, 8)
                    : owner_fp;
                p["owner_avatar_url"] = owner_avatar_url;
                p["visibility"] = visibility;

                std::string my_reaction;
                p["reactions"] = cs_load_post_reactions(
                    g_db, id, actor_fp, deps, &my_reaction);
                p["my_reaction"] = my_reaction;
                p["replies"] = cs_load_post_replies(g_db, id, actor_fp, deps);

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

    server.Post("/api/v4/circlestack/posts/reply",
        [&](const httplib::Request& req, httplib::Response& res) {
            std::string actor_fp;
            std::string actor_role;

            if (!deps.require_user_auth_users_actor ||
                !deps.require_user_auth_users_actor(
                    req, res, deps.cookie_key, deps.users, &actor_fp, &actor_role)) {
                return;
            }

            cs_db_init();

            json body = json::parse(req.body, nullptr, false);
            if (!body.is_object()) {
                res.status = 400;
                return set_json(res, {{"ok", false}, {"error", "invalid_json"}});
            }

            const int post_id = body.value("post_id", 0);
            const std::string text = body.value("text", "");
            const std::string media_path = body.value("media_path", "");

            if (post_id <= 0) {
                res.status = 400;
                return set_json(res, {{"ok", false}, {"error", "invalid_post_id"}});
            }

            if (text.empty() && media_path.empty()) {
                res.status = 400;
                return set_json(res, {{"ok", false}, {"error", "empty_reply"}});
            }

            if (!media_path.empty()) {
                std::string rel_norm;
                std::string norm_err;

                if (!normalize_user_rel_path_strict(media_path, &rel_norm, &norm_err)) {
                    res.status = 400;
                    return set_json(res, {{"ok", false}, {"error", "INVALID_MEDIA_PATH"}});
                }
            }

            std::string visibility_err;
            if (!cs_actor_can_see_post(g_db, post_id, actor_fp, &visibility_err)) {
                res.status = visibility_err == "not_found" ? 404 : 403;
                return set_json(res, {
                    {"ok", false},
                    {"error", visibility_err}
                });
            }

            sqlite3_stmt* st = nullptr;
            if (sqlite3_prepare_v2(g_db,
                    "INSERT INTO post_replies "
                    "(post_id, actor_fp, text, media_path, created_epoch) "
                    "VALUES (?, ?, ?, ?, ?)",
                    -1, &st, nullptr) != SQLITE_OK) {
                res.status = 500;
                return set_json(res, {{"ok", false}, {"error", "db_prepare_failed"}});
            }

            sqlite3_bind_int(st, 1, post_id);
            sqlite3_bind_text(st, 2, actor_fp.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 3, text.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 4, media_path.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(st, 5, (sqlite3_int64)std::time(nullptr));

            if (sqlite3_step(st) != SQLITE_DONE) {
                sqlite3_finalize(st);
                res.status = 500;
                return set_json(res, {{"ok", false}, {"error", "db_insert_failed"}});
            }

            const sqlite3_int64 id = sqlite3_last_insert_rowid(g_db);
            sqlite3_finalize(st);

            std::string actor_display = cs_short_fp(actor_fp);
            std::string actor_avatar_url;

            if (deps.users) {
                auto u = deps.users->get(actor_fp);
                if (u.has_value()) {
                    if (!u->name.empty()) {
                        actor_display = u->name;
                    }
                    actor_avatar_url = u->avatar_url;
                }
            }

            json reply = {
                {"id", id},
                {"post_id", post_id},
                {"actor_fp", actor_fp},
                {"actor_fp_short", cs_short_fp(actor_fp)},
                {"actor_display_name", actor_display},
                {"actor_avatar_url", actor_avatar_url},
                {"text", text},
                {"created_epoch", (sqlite3_int64)std::time(nullptr)},
                {"is_mine", true}
            };

            if (!media_path.empty()) {
                reply["media_url"] = "/api/v4/circlestack/reply/media?id=" + std::to_string(id);
            }

            set_json(res, {
                {"ok", true},
                {"id", id},
                {"post_id", post_id},
                {"reply", reply}
            });
        });


    server.Post("/api/v4/circlestack/replies/update",
        [&](const httplib::Request& req, httplib::Response& res) {
            std::string actor_fp;
            std::string actor_role;

            if (!deps.require_user_auth_users_actor ||
                !deps.require_user_auth_users_actor(
                    req, res, deps.cookie_key, deps.users, &actor_fp, &actor_role)) {
                return;
            }

            cs_db_init();

            json body = json::parse(req.body, nullptr, false);
            if (!body.is_object()) {
                res.status = 400;
                return set_json(res, {{"ok", false}, {"error", "invalid_json"}});
            }

            const int id = body.value("id", 0);
            const std::string text = body.value("text", "");
            const std::string media_path = body.value("media_path", "");

            if (id <= 0) {
                res.status = 400;
                return set_json(res, {{"ok", false}, {"error", "invalid_reply_id"}});
            }

            if (text.empty() && media_path.empty()) {
                res.status = 400;
                return set_json(res, {{"ok", false}, {"error", "empty_reply"}});
            }

            if (!media_path.empty()) {
                std::string rel_norm;
                std::string norm_err;

                if (!normalize_user_rel_path_strict(media_path, &rel_norm, &norm_err)) {
                    res.status = 400;
                    return set_json(res, {{"ok", false}, {"error", "INVALID_MEDIA_PATH"}});
                }
            }

            sqlite3_stmt* st = nullptr;
            if (sqlite3_prepare_v2(g_db,
                    "SELECT post_id, actor_fp, created_epoch "
                    "FROM post_replies WHERE id = ?",
                    -1, &st, nullptr) != SQLITE_OK) {
                res.status = 500;
                return set_json(res, {{"ok", false}, {"error", "db_prepare_failed"}});
            }

            sqlite3_bind_int(st, 1, id);

            int post_id = 0;
            std::string reply_owner_fp;
            sqlite3_int64 created_epoch = 0;

            if (sqlite3_step(st) == SQLITE_ROW) {
                post_id = sqlite3_column_int(st, 0);

                const char* owner_raw =
                    reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
                reply_owner_fp = owner_raw ? owner_raw : "";

                created_epoch = sqlite3_column_int64(st, 2);
            }

            sqlite3_finalize(st);

            if (post_id <= 0 || reply_owner_fp.empty()) {
                res.status = 404;
                return set_json(res, {{"ok", false}, {"error", "not_found"}});
            }

            if (reply_owner_fp != actor_fp) {
                res.status = 403;
                return set_json(res, {{"ok", false}, {"error", "forbidden"}});
            }

            std::string visibility_err;
            if (!cs_actor_can_see_post(g_db, post_id, actor_fp, &visibility_err)) {
                res.status = visibility_err == "not_found" ? 404 : 403;
                return set_json(res, {{"ok", false}, {"error", visibility_err}});
            }

            if (sqlite3_prepare_v2(g_db,
                    "UPDATE post_replies "
                    "SET text = ?, media_path = ? "
                    "WHERE id = ? AND actor_fp = ?",
                    -1, &st, nullptr) != SQLITE_OK) {
                res.status = 500;
                return set_json(res, {{"ok", false}, {"error", "db_prepare_failed"}});
            }

            sqlite3_bind_text(st, 1, text.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 2, media_path.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(st, 3, id);
            sqlite3_bind_text(st, 4, actor_fp.c_str(), -1, SQLITE_TRANSIENT);

            if (sqlite3_step(st) != SQLITE_DONE) {
                sqlite3_finalize(st);
                res.status = 500;
                return set_json(res, {{"ok", false}, {"error", "db_update_failed"}});
            }

            sqlite3_finalize(st);

            std::string actor_display = cs_short_fp(actor_fp);
            std::string actor_avatar_url;

            if (deps.users) {
                auto u = deps.users->get(actor_fp);
                if (u.has_value()) {
                    if (!u->name.empty()) {
                        actor_display = u->name;
                    }
                    actor_avatar_url = u->avatar_url;
                }
            }

            json reply = {
                {"id", id},
                {"post_id", post_id},
                {"actor_fp", actor_fp},
                {"actor_fp_short", cs_short_fp(actor_fp)},
                {"actor_display_name", actor_display},
                {"actor_avatar_url", actor_avatar_url},
                {"text", text},
                {"created_epoch", created_epoch},
                {"is_mine", true}
            };

            if (!media_path.empty()) {
                reply["media_url"] =
                    "/api/v4/circlestack/reply/media?id=" + std::to_string(id);
            }

            set_json(res, {
                {"ok", true},
                {"id", id},
                {"post_id", post_id},
                {"reply", reply}
            });
        });


    server.Post("/api/v4/circlestack/replies/delete",
        [&](const httplib::Request& req, httplib::Response& res) {
            std::string actor_fp;
            std::string actor_role;

            if (!deps.require_user_auth_users_actor ||
                !deps.require_user_auth_users_actor(
                    req, res, deps.cookie_key, deps.users, &actor_fp, &actor_role)) {
                return;
            }

            cs_db_init();

            json body = json::parse(req.body, nullptr, false);
            if (!body.is_object()) {
                res.status = 400;
                return set_json(res, {{"ok", false}, {"error", "invalid_json"}});
            }

            const int id = body.value("id", 0);

            if (id <= 0) {
                res.status = 400;
                return set_json(res, {{"ok", false}, {"error", "invalid_reply_id"}});
            }

            sqlite3_stmt* st = nullptr;
            if (sqlite3_prepare_v2(g_db,
                    "SELECT post_id, actor_fp FROM post_replies WHERE id = ?",
                    -1, &st, nullptr) != SQLITE_OK) {
                res.status = 500;
                return set_json(res, {{"ok", false}, {"error", "db_prepare_failed"}});
            }

            sqlite3_bind_int(st, 1, id);

            int post_id = 0;
            std::string reply_owner_fp;

            if (sqlite3_step(st) == SQLITE_ROW) {
                post_id = sqlite3_column_int(st, 0);

                const char* owner_raw =
                    reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
                reply_owner_fp = owner_raw ? owner_raw : "";
            }

            sqlite3_finalize(st);

            if (post_id <= 0 || reply_owner_fp.empty()) {
                res.status = 404;
                return set_json(res, {{"ok", false}, {"error", "not_found"}});
            }

            if (reply_owner_fp != actor_fp) {
                res.status = 403;
                return set_json(res, {{"ok", false}, {"error", "forbidden"}});
            }

            if (sqlite3_prepare_v2(g_db,
                    "DELETE FROM post_replies WHERE id = ? AND actor_fp = ?",
                    -1, &st, nullptr) != SQLITE_OK) {
                res.status = 500;
                return set_json(res, {{"ok", false}, {"error", "db_prepare_failed"}});
            }

            sqlite3_bind_int(st, 1, id);
            sqlite3_bind_text(st, 2, actor_fp.c_str(), -1, SQLITE_TRANSIENT);

            if (sqlite3_step(st) != SQLITE_DONE) {
                sqlite3_finalize(st);
                res.status = 500;
                return set_json(res, {{"ok", false}, {"error", "db_delete_failed"}});
            }

            sqlite3_finalize(st);

            set_json(res, {
                {"ok", true},
                {"id", id},
                {"post_id", post_id}
            });
        });


    server.Post("/api/v4/circlestack/posts/react",
        [&](const httplib::Request& req, httplib::Response& res) {
            std::string actor_fp;
            std::string actor_role;

            if (!deps.require_user_auth_users_actor ||
                !deps.require_user_auth_users_actor(
                    req, res, deps.cookie_key, deps.users, &actor_fp, &actor_role)) {
                return;
            }

            cs_db_init();

            json body = json::parse(req.body, nullptr, false);
            if (!body.is_object()) {
                res.status = 400;
                return set_json(res, {{"ok", false}, {"error", "invalid_json"}});
            }

            const int post_id = body.value("post_id", 0);
            const std::string reaction = body.value("reaction", "");

            if (post_id <= 0) {
                res.status = 400;
                return set_json(res, {{"ok", false}, {"error", "invalid_post_id"}});
            }

            std::string visibility_err;
            if (!cs_actor_can_see_post(g_db, post_id, actor_fp, &visibility_err)) {
                res.status = visibility_err == "not_found" ? 404 : 403;
                return set_json(res, {
                    {"ok", false},
                    {"error", visibility_err}
                });
            }

            sqlite3_stmt* st = nullptr;

            if (reaction.empty()) {
                if (sqlite3_prepare_v2(g_db,
                        "DELETE FROM post_reactions "
                        "WHERE post_id = ? AND actor_fp = ?",
                        -1, &st, nullptr) != SQLITE_OK) {
                    res.status = 500;
                    return set_json(res, {{"ok", false}, {"error", "db_prepare_failed"}});
                }

                sqlite3_bind_int(st, 1, post_id);
                sqlite3_bind_text(st, 2, actor_fp.c_str(), -1, SQLITE_TRANSIENT);

                const int rc = sqlite3_step(st);
                sqlite3_finalize(st);

                if (rc != SQLITE_DONE) {
                    res.status = 500;
                    return set_json(res, {{"ok", false}, {"error", "db_delete_failed"}});
                }

                return set_json(res, {
                    {"ok", true},
                    {"post_id", post_id},
                    {"reaction", ""}
                });
            }

            if (!cs_is_supported_reaction(reaction)) {
                res.status = 400;
                return set_json(res, {{"ok", false}, {"error", "unsupported_reaction"}});
            }

            sqlite3_exec(g_db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr);

            if (sqlite3_prepare_v2(g_db,
                    "DELETE FROM post_reactions "
                    "WHERE post_id = ? AND actor_fp = ?",
                    -1, &st, nullptr) != SQLITE_OK) {
                sqlite3_exec(g_db, "ROLLBACK", nullptr, nullptr, nullptr);
                res.status = 500;
                return set_json(res, {{"ok", false}, {"error", "db_prepare_failed"}});
            }

            sqlite3_bind_int(st, 1, post_id);
            sqlite3_bind_text(st, 2, actor_fp.c_str(), -1, SQLITE_TRANSIENT);

            if (sqlite3_step(st) != SQLITE_DONE) {
                sqlite3_finalize(st);
                sqlite3_exec(g_db, "ROLLBACK", nullptr, nullptr, nullptr);
                res.status = 500;
                return set_json(res, {{"ok", false}, {"error", "db_delete_failed"}});
            }

            sqlite3_finalize(st);
            st = nullptr;

            if (sqlite3_prepare_v2(g_db,
                    "INSERT INTO post_reactions "
                    "(post_id, actor_fp, reaction, created_epoch) "
                    "VALUES (?, ?, ?, ?)",
                    -1, &st, nullptr) != SQLITE_OK) {
                sqlite3_exec(g_db, "ROLLBACK", nullptr, nullptr, nullptr);
                res.status = 500;
                return set_json(res, {{"ok", false}, {"error", "db_prepare_failed"}});
            }

            sqlite3_bind_int(st, 1, post_id);
            sqlite3_bind_text(st, 2, actor_fp.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 3, reaction.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(st, 4, (sqlite3_int64)std::time(nullptr));

            if (sqlite3_step(st) != SQLITE_DONE) {
                sqlite3_finalize(st);
                sqlite3_exec(g_db, "ROLLBACK", nullptr, nullptr, nullptr);
                res.status = 500;
                return set_json(res, {{"ok", false}, {"error", "db_insert_failed"}});
            }

            sqlite3_finalize(st);

            if (sqlite3_exec(g_db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
                sqlite3_exec(g_db, "ROLLBACK", nullptr, nullptr, nullptr);
                res.status = 500;
                return set_json(res, {{"ok", false}, {"error", "db_commit_failed"}});
            }

            set_json(res, {
                {"ok", true},
                {"post_id", post_id},
                {"reaction", reaction}
            });
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
                "DELETE FROM post_replies WHERE post_id=?",
                -1, &stmt, nullptr);

            sqlite3_bind_int(stmt, 1, id);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            stmt = nullptr;

            sqlite3_prepare_v2(g_db,
                "DELETE FROM post_reactions WHERE post_id=?",
                -1, &stmt, nullptr);

            sqlite3_bind_int(stmt, 1, id);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            stmt = nullptr;

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

    server.Get("/api/v4/circlestack/reply/media",
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
                "SELECT post_id, media_path, actor_fp FROM post_replies WHERE id=?",
                -1, &stmt, nullptr);

            sqlite3_bind_int(stmt, 1, id);

            int post_id = 0;
            std::string media_path;
            std::string reply_owner_fp;

            if (sqlite3_step(stmt) == SQLITE_ROW) {
                post_id = sqlite3_column_int(stmt, 0);

                const char* m = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                const char* o = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));

                if (m) media_path = m;
                if (o) reply_owner_fp = o;
            }

            sqlite3_finalize(stmt);

            if (post_id <= 0 || media_path.empty() || reply_owner_fp.empty()) {
                res.status = 404;
                return;
            }

            std::string visibility_err;
            if (!cs_actor_can_see_post(g_db, post_id, actor_fp, &visibility_err)) {
                res.status = visibility_err == "not_found" ? 404 : 403;
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
                deps.user_dir_for_fp(*deps.users, reply_owner_fp);

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
                std::string edge_err;
                if (!cs_insert_circle_edge(
                        g_db,
                        a,
                        b,
                        (sqlite3_int64)std::time(nullptr),
                        id,
                        &edge_err)) {
                    res.status = 500;
                    return set_json(res, {
                        {"ok", false},
                        {"error", "circle_edge_insert_failed"},
                        {"detail", edge_err}
                    });
                }
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
            if (sqlite3_open(kPeopleContactsDbPath, &people_db) != SQLITE_OK) {
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

            sqlite3_finalize(st);
            sqlite3_close(people_db);

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
                std::string contact_err;
                if (!cs_insert_symmetric_people_contacts(
                        deps,
                        from_fp,
                        to_fp,
                        now,
                        &contact_err)) {
                    res.status = 500;
                    return set_json(res, {
                        {"ok", false},
                        {"error", "people_contacts_insert_failed"},
                        {"detail", contact_err}
                    });
                }

                std::string edge_err;
                if (!cs_insert_circle_edge(
                        g_db,
                        from_fp,
                        to_fp,
                        now,
                        0,
                        &edge_err)) {
                    res.status = 500;
                    return set_json(res, {
                        {"ok", false},
                        {"error", "circle_edge_insert_failed"},
                        {"detail", edge_err}
                    });
                }
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



    server.Get("/api/v4/circlestack/notifications",
        [&](const httplib::Request& req, httplib::Response& res) {
            std::string actor_fp, actor_role;
            if (!deps.require_user_auth_users_actor ||
                !deps.require_user_auth_users_actor(
                    req, res, deps.cookie_key, deps.users, &actor_fp, &actor_role)) {
                return;
            }

            cs_db_init();

            json items = json::array();

            sqlite3_stmt* st = nullptr;
            sqlite3_prepare_v2(g_db,
                "SELECT id, created_epoch, from_fp, message "
                "FROM contact_requests "
                "WHERE to_fp = ? AND status = 'pending' "
                "ORDER BY created_epoch DESC",
                -1, &st, nullptr);

            sqlite3_bind_text(st, 1, actor_fp.c_str(), -1, SQLITE_TRANSIENT);

            while (sqlite3_step(st) == SQLITE_ROW) {
                const std::string from_fp =
                    reinterpret_cast<const char*>(sqlite3_column_text(st, 2));

                items.push_back({
                    {"type", "contact_request"},
                    {"id", sqlite3_column_int64(st, 0)},
                    {"created_epoch", sqlite3_column_int64(st, 1)},
                    {"from_fp", from_fp},
                    {"from_display_name", cs_display_name_for_fp(deps, from_fp)},
                    {"message", reinterpret_cast<const char*>(sqlite3_column_text(st, 3))},
                    {"action_endpoint", "/api/v4/circlestack/contact/respond"}
                });
            }

            sqlite3_finalize(st);
            st = nullptr;

            sqlite3_prepare_v2(g_db,
                "SELECT id, created_epoch, introducer_fp, person_a_fp, person_b_fp, message "
                "FROM introductions "
                "WHERE status = 'pending' "
                "  AND (person_a_fp = ? OR person_b_fp = ?) "
                "ORDER BY created_epoch DESC",
                -1, &st, nullptr);

            sqlite3_bind_text(st, 1, actor_fp.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 2, actor_fp.c_str(), -1, SQLITE_TRANSIENT);

            while (sqlite3_step(st) == SQLITE_ROW) {
                const std::string introducer_fp =
                    reinterpret_cast<const char*>(sqlite3_column_text(st, 2));
                const std::string a_fp =
                    reinterpret_cast<const char*>(sqlite3_column_text(st, 3));
                const std::string b_fp =
                    reinterpret_cast<const char*>(sqlite3_column_text(st, 4));

                const std::string other_fp = (actor_fp == a_fp) ? b_fp : a_fp;

                items.push_back({
                    {"type", "introduction"},
                    {"id", sqlite3_column_int64(st, 0)},
                    {"created_epoch", sqlite3_column_int64(st, 1)},
                    {"introducer_fp", introducer_fp},
                    {"introducer_display_name", cs_display_name_for_fp(deps, introducer_fp)},
                    {"other_fp", other_fp},
                    {"other_display_name", cs_display_name_for_fp(deps, other_fp)},
                    {"person_a_fp", a_fp},
                    {"person_b_fp", b_fp},
                    {"message", reinterpret_cast<const char*>(sqlite3_column_text(st, 5))},
                    {"action_endpoint", "/api/v4/circlestack/introductions/respond"}
                });
            }

            sqlite3_finalize(st);

            set_json(res, {
                {"ok", true},
                {"count", items.size()},
                {"items", items}
            });
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
