#include "circle_stack_memory_nodes.h"

#include "storage_resolver.h"

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <system_error>

using json = nlohmann::json;

namespace {

static constexpr const char* kCircleStackDbPath = "/srv/pqnas/circlestack.db";

void csmn_set_json(httplib::Response& res, const json& body) {
    res.set_content(body.dump(), "application/json; charset=utf-8");
}

std::string csmn_trim(const std::string& s) {
    std::size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;

    std::size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;

    return s.substr(a, b - a);
}

std::string csmn_limit(std::string s, std::size_t max_len) {
    s = csmn_trim(s);
    if (s.size() > max_len) s.resize(max_len);
    return s;
}

std::string csmn_lower_ext(const std::filesystem::path& p) {
    std::string ext = p.extension().string();
    for (char& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return ext;
}

bool csmn_is_video_path(const std::string& path) {
    const std::string ext = csmn_lower_ext(std::filesystem::path(path));
    return ext == ".mp4" || ext == ".webm" || ext == ".mov" || ext == ".m4v";
}

bool csmn_is_image_path(const std::string& path) {
    const std::string ext = csmn_lower_ext(std::filesystem::path(path));
    return ext == ".jpg" || ext == ".jpeg" || ext == ".png" ||
           ext == ".webp" || ext == ".gif";
}

bool csmn_is_supported_media_path(const std::string& path) {
    return csmn_is_image_path(path) || csmn_is_video_path(path);
}

std::string csmn_media_kind_for_path(const std::string& path) {
    if (csmn_is_video_path(path)) return "video";
    if (csmn_is_image_path(path)) return "image";
    return "file";
}

std::string csmn_mime_for_path(const std::filesystem::path& p) {
    const std::string ext = csmn_lower_ext(p);

    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".png") return "image/png";
    if (ext == ".webp") return "image/webp";
    if (ext == ".gif") return "image/gif";
    if (ext == ".mp4" || ext == ".m4v") return "video/mp4";
    if (ext == ".webm") return "video/webm";
    if (ext == ".mov") return "video/quicktime";

    return "application/octet-stream";
}

std::string csmn_short_fp(const std::string& fp) {
    return fp.size() >= 8 ? fp.substr(0, 8) : fp;
}

json csmn_user_summary(
    const pqnas::CircleStackRoutesDeps& deps,
    const std::string& fp
) {
    std::string display_name = csmn_short_fp(fp);
    std::string avatar_url;

    if (deps.users && !fp.empty()) {
        auto u = deps.users->get(fp);
        if (u.has_value()) {
            if (!u->name.empty()) display_name = u->name;
            avatar_url = u->avatar_url;
        }
    }

    return {
        {"fp", fp},
        {"fp_short", csmn_short_fp(fp)},
        {"display_name", display_name},
        {"avatar_url", avatar_url}
    };
}

bool csmn_exec(sqlite3* db, const char* sql, std::string* err = nullptr) {
    char* sqlite_err = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &sqlite_err);

    if (rc != SQLITE_OK) {
        if (err) {
            *err = sqlite_err ? sqlite_err : sqlite3_errmsg(db);
        }
        if (sqlite_err) sqlite3_free(sqlite_err);
        return false;
    }

    return true;
}

bool csmn_init_db(sqlite3* db, std::string* err = nullptr) {
    if (!db) {
        if (err) *err = "db unavailable";
        return false;
    }

    if (!csmn_exec(db,
        "CREATE TABLE IF NOT EXISTS memory_nodes ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "post_id INTEGER NOT NULL UNIQUE,"
        "owner_fp TEXT NOT NULL,"
        "title TEXT NOT NULL DEFAULT '',"
        "body TEXT NOT NULL DEFAULT '',"
        "visibility TEXT NOT NULL DEFAULT 'circle',"
        "created_epoch INTEGER NOT NULL,"
        "updated_epoch INTEGER NOT NULL"
        ")",
        err)) return false;

    if (!csmn_exec(db,
        "CREATE INDEX IF NOT EXISTS idx_memory_nodes_post_id "
        "ON memory_nodes(post_id)",
        err)) return false;

    if (!csmn_exec(db,
        "CREATE INDEX IF NOT EXISTS idx_memory_nodes_owner_fp "
        "ON memory_nodes(owner_fp)",
        err)) return false;

    if (!csmn_exec(db,
        "CREATE TABLE IF NOT EXISTS memory_node_items ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "node_id INTEGER NOT NULL,"
        "owner_fp TEXT NOT NULL,"
        "media_path TEXT NOT NULL,"
        "media_kind TEXT NOT NULL DEFAULT 'image',"
        "caption TEXT NOT NULL DEFAULT '',"
        "created_epoch INTEGER NOT NULL"
        ")",
        err)) return false;

    if (!csmn_exec(db,
        "CREATE INDEX IF NOT EXISTS idx_memory_node_items_node_id "
        "ON memory_node_items(node_id, created_epoch)",
        err)) return false;

    if (!csmn_exec(db,
        "CREATE TABLE IF NOT EXISTS memory_node_item_reactions ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "item_id INTEGER NOT NULL,"
        "actor_fp TEXT NOT NULL,"
        "reaction TEXT NOT NULL,"
        "created_epoch INTEGER NOT NULL,"
        "updated_epoch INTEGER NOT NULL,"
        "UNIQUE(item_id, actor_fp)"
        ")",
        err)) return false;

    if (!csmn_exec(db,
        "CREATE INDEX IF NOT EXISTS idx_memory_node_item_reactions_item "
        "ON memory_node_item_reactions(item_id, reaction)",
        err)) return false;

    csmn_exec(db,
        "DELETE FROM memory_node_item_reactions "
        "WHERE item_id NOT IN (SELECT id FROM memory_node_items)");

    csmn_exec(db,
        "DELETE FROM memory_node_items "
        "WHERE node_id IN ("
        "  SELECT id FROM memory_nodes "
        "  WHERE post_id NOT IN (SELECT id FROM posts)"
        ")");

    csmn_exec(db,
        "DELETE FROM memory_nodes "
        "WHERE post_id NOT IN (SELECT id FROM posts)");

    return true;
}

bool csmn_open_db(sqlite3** out_db, std::string* err = nullptr) {
    if (!out_db) return false;
    *out_db = nullptr;

    sqlite3* db = nullptr;
    if (sqlite3_open(kCircleStackDbPath, &db) != SQLITE_OK) {
        if (err) *err = db ? sqlite3_errmsg(db) : "sqlite3_open failed";
        if (db) sqlite3_close(db);
        return false;
    }

    if (!csmn_init_db(db, err)) {
        sqlite3_close(db);
        return false;
    }

    *out_db = db;
    return true;
}

long long csmn_count(sqlite3* db, const char* sql) {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
        return 0;
    }

    long long out = 0;
    if (sqlite3_step(st) == SQLITE_ROW) {
        out = sqlite3_column_int64(st, 0);
    }

    sqlite3_finalize(st);
    return out;
}


bool csmn_actor_can_see_post(
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

bool csmn_load_node_post_id(sqlite3* db, int node_id, int* out_post_id, std::string* out_owner_fp = nullptr) {
    if (out_post_id) *out_post_id = 0;
    if (out_owner_fp) out_owner_fp->clear();

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "SELECT post_id, owner_fp FROM memory_nodes WHERE id = ?",
            -1, &st, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int(st, 1, node_id);

    bool found = false;
    if (sqlite3_step(st) == SQLITE_ROW) {
        found = true;
        if (out_post_id) *out_post_id = sqlite3_column_int(st, 0);

        if (out_owner_fp) {
            const char* owner = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
            *out_owner_fp = owner ? owner : "";
        }
    }

    sqlite3_finalize(st);
    return found;
}

bool csmn_path_has_no_symlink_components_below_root(
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

    const auto target_abs = std::filesystem::weakly_canonical(target, ec);
    if (ec) {
        if (err) *err = "target canonical failed: " + ec.message();
        return false;
    }

    const auto rel = std::filesystem::relative(target_abs, root_abs, ec);
    if (ec) {
        if (err) *err = "relative failed: " + ec.message();
        return false;
    }

    if (rel.empty() || rel.is_absolute()) {
        if (err) *err = "target outside root";
        return false;
    }

    for (const auto& part : rel) {
        if (part == "..") {
            if (err) *err = "target outside root";
            return false;
        }
    }

    std::filesystem::path cur = root_abs;
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

bool csmn_validate_media_path(
    const pqnas::CircleStackRoutesDeps& deps,
    const std::string& owner_fp,
    const std::string& raw_path,
    std::string* out_norm,
    std::string* out_kind,
    std::string* err
) {
    if (out_norm) out_norm->clear();
    if (out_kind) out_kind->clear();

    std::string rel_norm;
    std::string norm_err;

    if (!pqnas::normalize_user_rel_path_strict(raw_path, &rel_norm, &norm_err)) {
        if (err) *err = "INVALID_MEDIA_PATH";
        return false;
    }

    if (!csmn_is_supported_media_path(rel_norm)) {
        if (err) *err = "UNSUPPORTED_MEDIA_TYPE";
        return false;
    }

    if (!deps.users) {
        if (err) *err = "users_registry_unavailable";
        return false;
    }

    pqnas::ResolvedExistingPath resolved;
    std::string resolve_err;

    if (!pqnas::resolve_existing_user_file_path(
            *deps.users,
            owner_fp,
            rel_norm,
            &resolved,
            &resolve_err)) {
        if (err) *err = resolve_err.empty() ? "MEDIA_NOT_FOUND" : resolve_err;
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::is_regular_file(resolved.abs_path, ec)) {
        if (err) *err = "MEDIA_NOT_FILE";
        return false;
    }

    if (deps.user_dir_for_fp && deps.users) {
        std::filesystem::path owner_root;
        try {
            owner_root = deps.user_dir_for_fp(*deps.users, owner_fp);
        } catch (...) {
            if (err) *err = "owner_root_failed";
            return false;
        }

        std::string symlink_err;
        if (!csmn_path_has_no_symlink_components_below_root(
                owner_root,
                resolved.abs_path,
                &symlink_err)) {
            if (err) *err = symlink_err.empty() ? "SYMLINK_REJECTED" : symlink_err;
            return false;
        }
    }

    if (out_norm) *out_norm = rel_norm;
    if (out_kind) *out_kind = csmn_media_kind_for_path(rel_norm);
    return true;
}

std::uintmax_t csmn_media_file_size(
    const pqnas::CircleStackRoutesDeps& deps,
    const std::string& owner_fp,
    const std::string& media_path
) {
    if (!deps.users || owner_fp.empty() || media_path.empty()) return 0;

    pqnas::ResolvedExistingPath resolved;
    std::string err;

    if (!pqnas::resolve_existing_user_file_path(
            *deps.users,
            owner_fp,
            media_path,
            &resolved,
            &err)) {
        return 0;
    }

    std::error_code ec;
    if (!std::filesystem::is_regular_file(resolved.abs_path, ec) || ec) {
        return 0;
    }

    const auto sz = std::filesystem::file_size(resolved.abs_path, ec);
    return ec ? 0 : sz;
}

bool csmn_valid_reaction(const std::string& reaction) {
    static const char* kAllowed[] = {
        u8"👍",
        u8"❤️",
        u8"😂",
        u8"😮",
        u8"👏",
        u8"🔥"
    };

    for (const char* allowed : kAllowed) {
        if (reaction == allowed) return true;
    }

    return false;
}

std::string csmn_item_my_reaction(
    sqlite3* db,
    int item_id,
    const std::string& viewer_fp
) {
    if (!db || item_id <= 0 || viewer_fp.empty()) return "";

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "SELECT reaction FROM memory_node_item_reactions "
            "WHERE item_id = ? AND actor_fp = ? "
            "LIMIT 1",
            -1, &st, nullptr) != SQLITE_OK) {
        return "";
    }

    sqlite3_bind_int(st, 1, item_id);
    sqlite3_bind_text(st, 2, viewer_fp.c_str(), -1, SQLITE_TRANSIENT);

    std::string out;
    if (sqlite3_step(st) == SQLITE_ROW) {
        const char* raw = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
        out = raw ? raw : "";
    }

    sqlite3_finalize(st);
    return out;
}

json csmn_item_reactions_json(
    sqlite3* db,
    int item_id,
    const std::string& viewer_fp,
    const pqnas::CircleStackRoutesDeps& deps
) {
    json out = json::array();
    if (!db || item_id <= 0) return out;

    const std::string my_reaction = csmn_item_my_reaction(db, item_id, viewer_fp);

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "SELECT reaction, COUNT(*) "
            "FROM memory_node_item_reactions "
            "WHERE item_id = ? "
            "GROUP BY reaction "
            "ORDER BY COUNT(*) DESC, reaction ASC",
            -1, &st, nullptr) != SQLITE_OK) {
        return out;
    }

    sqlite3_bind_int(st, 1, item_id);

    while (sqlite3_step(st) == SQLITE_ROW) {
        const char* reaction_raw = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
        const std::string reaction = reaction_raw ? reaction_raw : "";
        const int count = sqlite3_column_int(st, 1);

        json people = json::array();

        sqlite3_stmt* pst = nullptr;
        if (sqlite3_prepare_v2(db,
                "SELECT actor_fp "
                "FROM memory_node_item_reactions "
                "WHERE item_id = ? AND reaction = ? "
                "ORDER BY updated_epoch ASC, id ASC "
                "LIMIT 16",
                -1, &pst, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(pst, 1, item_id);
            sqlite3_bind_text(pst, 2, reaction.c_str(), -1, SQLITE_TRANSIENT);

            while (sqlite3_step(pst) == SQLITE_ROW) {
                const char* fp_raw = reinterpret_cast<const char*>(sqlite3_column_text(pst, 0));
                const std::string fp = fp_raw ? fp_raw : "";
                people.push_back(csmn_user_summary(deps, fp));
            }

            sqlite3_finalize(pst);
        }

        out.push_back({
            {"reaction", reaction},
            {"count", count},
            {"reacted_by_me", !my_reaction.empty() && my_reaction == reaction},
            {"people", people}
        });
    }

    sqlite3_finalize(st);
    return out;
}

json csmn_load_items(
    sqlite3* db,
    int node_id,
    const std::string& viewer_fp,
    const pqnas::CircleStackRoutesDeps& deps
) {
    json items = json::array();

    std::string node_owner_fp;
    int post_id = 0;
    (void)csmn_load_node_post_id(db, node_id, &post_id, &node_owner_fp);

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "SELECT id, owner_fp, media_path, media_kind, caption, created_epoch "
            "FROM memory_node_items "
            "WHERE node_id = ? "
            "ORDER BY created_epoch ASC, id ASC",
            -1, &st, nullptr) != SQLITE_OK) {
        return items;
    }

    sqlite3_bind_int(st, 1, node_id);

    while (sqlite3_step(st) == SQLITE_ROW) {
        const int id = sqlite3_column_int(st, 0);

        const char* owner_raw = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
        const char* path_raw = reinterpret_cast<const char*>(sqlite3_column_text(st, 2));
        const char* kind_raw = reinterpret_cast<const char*>(sqlite3_column_text(st, 3));
        const char* caption_raw = reinterpret_cast<const char*>(sqlite3_column_text(st, 4));

        const std::string owner_fp = owner_raw ? owner_raw : "";
        const std::string media_path = path_raw ? path_raw : "";
        const std::string media_kind = kind_raw ? kind_raw : csmn_media_kind_for_path(media_path);
        const std::uintmax_t media_bytes = csmn_media_file_size(deps, owner_fp, media_path);

        json owner = csmn_user_summary(deps, owner_fp);

        items.push_back({
            {"id", id},
            {"node_id", node_id},
            {"owner_fp", owner_fp},
            {"owner_fp_short", owner.value("fp_short", "")},
            {"owner_display_name", owner.value("display_name", "")},
            {"owner_avatar_url", owner.value("avatar_url", "")},
            {"media_kind", media_kind},
            {"media_bytes", media_bytes},
            {"caption", caption_raw ? caption_raw : ""},
            {"my_reaction", csmn_item_my_reaction(db, id, viewer_fp)},
            {"reactions", csmn_item_reactions_json(db, id, viewer_fp, deps)},
            {"created_epoch", sqlite3_column_int64(st, 5)},
            {"media_url", "/api/v4/circlestack/memory-nodes/items/media?id=" + std::to_string(id)},
            {"can_delete", owner_fp == viewer_fp || node_owner_fp == viewer_fp}
        });
    }

    sqlite3_finalize(st);
    return items;
}

json csmn_load_single_item(
    sqlite3* db,
    int item_id,
    const std::string& viewer_fp,
    const pqnas::CircleStackRoutesDeps& deps
) {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "SELECT node_id FROM memory_node_items WHERE id = ?",
            -1, &st, nullptr) != SQLITE_OK) {
        return json();
    }

    sqlite3_bind_int(st, 1, item_id);

    int node_id = 0;
    if (sqlite3_step(st) == SQLITE_ROW) {
        node_id = sqlite3_column_int(st, 0);
    }

    sqlite3_finalize(st);

    if (node_id <= 0) return json();

    json items = csmn_load_items(db, node_id, viewer_fp, deps);
    for (const auto& item : items) {
        if (item.value("id", 0) == item_id) return item;
    }

    return json();
}

void csmn_annotate_one_post(
    sqlite3* db,
    json& post,
    const std::string& viewer_fp,
    const pqnas::CircleStackRoutesDeps& deps
) {
    const int post_id = post.value("id", 0);
    if (post_id <= 0) return;

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "SELECT id, owner_fp, title, body, visibility, created_epoch, updated_epoch "
            "FROM memory_nodes WHERE post_id = ?",
            -1, &st, nullptr) != SQLITE_OK) {
        return;
    }

    sqlite3_bind_int(st, 1, post_id);

    if (sqlite3_step(st) == SQLITE_ROW) {
        const int node_id = sqlite3_column_int(st, 0);

        const char* owner_raw = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
        const char* title_raw = reinterpret_cast<const char*>(sqlite3_column_text(st, 2));
        const char* body_raw = reinterpret_cast<const char*>(sqlite3_column_text(st, 3));
        const char* vis_raw = reinterpret_cast<const char*>(sqlite3_column_text(st, 4));

        const std::string owner_fp = owner_raw ? owner_raw : "";
        json owner = csmn_user_summary(deps, owner_fp);

        const long long item_count = csmn_count(db,
            ("SELECT COUNT(*) FROM memory_node_items WHERE node_id = " + std::to_string(node_id)).c_str());

        const long long contributor_count = csmn_count(db,
            ("SELECT COUNT(DISTINCT owner_fp) FROM memory_node_items WHERE node_id = " + std::to_string(node_id)).c_str());

        post["post_kind"] = "memory_node";
        post["memory_node"] = {
            {"id", node_id},
            {"post_id", post_id},
            {"owner_fp", owner_fp},
            {"owner_fp_short", owner.value("fp_short", "")},
            {"owner_display_name", owner.value("display_name", "")},
            {"owner_avatar_url", owner.value("avatar_url", "")},
            {"title", title_raw ? title_raw : ""},
            {"body", body_raw ? body_raw : ""},
            {"visibility", vis_raw ? vis_raw : "circle"},
            {"created_epoch", sqlite3_column_int64(st, 5)},
            {"updated_epoch", sqlite3_column_int64(st, 6)},
            {"item_count", item_count},
            {"contributors_count", contributor_count},
            {"can_contribute", true},
            {"items", csmn_load_items(db, node_id, viewer_fp, deps)}
        };
    }

    sqlite3_finalize(st);
}

} // namespace

namespace pqnas {

void circle_stack_memory_nodes_annotate_feed_posts(
    json& posts,
    const std::string& viewer_fp,
    const CircleStackRoutesDeps& deps
) {
    if (!posts.is_array()) return;

    sqlite3* db = nullptr;
    std::string err;

    if (!csmn_open_db(&db, &err)) {
        return;
    }

    for (auto& post : posts) {
        if (post.is_object()) {
            csmn_annotate_one_post(db, post, viewer_fp, deps);
        }
    }

    sqlite3_close(db);
}


void register_circle_stack_memory_node_routes(
    httplib::Server& server,
    const CircleStackRoutesDeps& deps
) {
    server.Post("/api/v4/circlestack/memory-nodes/create",
        [&](const httplib::Request& req, httplib::Response& res) {
            std::string actor_fp;
            std::string actor_role;

            if (!deps.require_user_auth_users_actor ||
                !deps.require_user_auth_users_actor(
                    req, res, deps.cookie_key, deps.users, &actor_fp, &actor_role)) {
                return;
            }

            json body = json::parse(req.body, nullptr, false);
            if (!body.is_object()) {
                res.status = 400;
                return csmn_set_json(res, {{"ok", false}, {"error", "invalid_json"}});
            }

            std::string title = csmn_limit(body.value("title", ""), 160);
            std::string node_body = csmn_limit(body.value("body", ""), 5000);
            std::string visibility = body.value("visibility", "circle");
            std::string media_path = csmn_trim(body.value("media_path", ""));
            std::string caption = csmn_limit(body.value("caption", ""), 512);

            if (title.empty()) title = "Memory Node";

            if (visibility != "public" && visibility != "private" && visibility != "circle") {
                res.status = 400;
                return csmn_set_json(res, {{"ok", false}, {"error", "invalid_visibility"}});
            }

            std::string media_norm;
            std::string media_kind;
            if (!media_path.empty()) {
                std::string media_err;
                if (!csmn_validate_media_path(
                        deps,
                        actor_fp,
                        media_path,
                        &media_norm,
                        &media_kind,
                        &media_err)) {
                    res.status = 400;
                    return csmn_set_json(res, {
                        {"ok", false},
                        {"error", media_err.empty() ? "invalid_media" : media_err}
                    });
                }
            }

            sqlite3* db = nullptr;
            std::string db_err;
            if (!csmn_open_db(&db, &db_err)) {
                res.status = 500;
                return csmn_set_json(res, {
                    {"ok", false},
                    {"error", "db_open_failed"},
                    {"detail", db_err}
                });
            }

            const sqlite3_int64 now = (sqlite3_int64)std::time(nullptr);
            const std::string post_text = node_body.empty()
                ? ("Opened Memory Node: " + title)
                : node_body;

            sqlite3_stmt* st = nullptr;

            if (sqlite3_exec(db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK) {
                const std::string detail = sqlite3_errmsg(db);
                sqlite3_close(db);
                res.status = 500;
                return csmn_set_json(res, {
                    {"ok", false},
                    {"error", "db_begin_failed"},
                    {"detail", detail}
                });
            }

            if (sqlite3_prepare_v2(db,
                    "INSERT INTO posts(text, media_path, created_epoch, owner_fp, visibility, circle_allow) "
                    "VALUES(?,?,?,?,?,?)",
                    -1, &st, nullptr) != SQLITE_OK) {
                sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
                const std::string detail = sqlite3_errmsg(db);
                sqlite3_close(db);
                res.status = 500;
                return csmn_set_json(res, {{"ok", false}, {"error", "post_prepare_failed"}, {"detail", detail}});
            }

            sqlite3_bind_text(st, 1, post_text.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 2, "", -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(st, 3, now);
            sqlite3_bind_text(st, 4, actor_fp.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 5, visibility.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 6, "[]", -1, SQLITE_TRANSIENT);

            if (sqlite3_step(st) != SQLITE_DONE) {
                sqlite3_finalize(st);
                sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
                const std::string detail = sqlite3_errmsg(db);
                sqlite3_close(db);
                res.status = 500;
                return csmn_set_json(res, {{"ok", false}, {"error", "post_insert_failed"}, {"detail", detail}});
            }

            const sqlite3_int64 post_id = sqlite3_last_insert_rowid(db);
            sqlite3_finalize(st);
            st = nullptr;

            if (sqlite3_prepare_v2(db,
                    "INSERT INTO memory_nodes(post_id, owner_fp, title, body, visibility, created_epoch, updated_epoch) "
                    "VALUES(?,?,?,?,?,?,?)",
                    -1, &st, nullptr) != SQLITE_OK) {
                sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
                const std::string detail = sqlite3_errmsg(db);
                sqlite3_close(db);
                res.status = 500;
                return csmn_set_json(res, {{"ok", false}, {"error", "node_prepare_failed"}, {"detail", detail}});
            }

            sqlite3_bind_int64(st, 1, post_id);
            sqlite3_bind_text(st, 2, actor_fp.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 3, title.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 4, node_body.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 5, visibility.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(st, 6, now);
            sqlite3_bind_int64(st, 7, now);

            if (sqlite3_step(st) != SQLITE_DONE) {
                sqlite3_finalize(st);
                sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
                const std::string detail = sqlite3_errmsg(db);
                sqlite3_close(db);
                res.status = 500;
                return csmn_set_json(res, {{"ok", false}, {"error", "node_insert_failed"}, {"detail", detail}});
            }

            const sqlite3_int64 node_id = sqlite3_last_insert_rowid(db);
            sqlite3_finalize(st);
            st = nullptr;

            if (!media_norm.empty()) {
                if (sqlite3_prepare_v2(db,
                        "INSERT INTO memory_node_items(node_id, owner_fp, media_path, media_kind, caption, created_epoch) "
                        "VALUES(?,?,?,?,?,?)",
                        -1, &st, nullptr) != SQLITE_OK) {
                    sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
                    const std::string detail = sqlite3_errmsg(db);
                    sqlite3_close(db);
                    res.status = 500;
                    return csmn_set_json(res, {{"ok", false}, {"error", "item_prepare_failed"}, {"detail", detail}});
                }

                sqlite3_bind_int64(st, 1, node_id);
                sqlite3_bind_text(st, 2, actor_fp.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(st, 3, media_norm.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(st, 4, media_kind.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(st, 5, caption.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(st, 6, now);

                if (sqlite3_step(st) != SQLITE_DONE) {
                    sqlite3_finalize(st);
                    sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
                    const std::string detail = sqlite3_errmsg(db);
                    sqlite3_close(db);
                    res.status = 500;
                    return csmn_set_json(res, {{"ok", false}, {"error", "item_insert_failed"}, {"detail", detail}});
                }

                sqlite3_finalize(st);
                st = nullptr;
            }

            if (sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
                sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
                const std::string detail = sqlite3_errmsg(db);
                sqlite3_close(db);
                res.status = 500;
                return csmn_set_json(res, {{"ok", false}, {"error", "db_commit_failed"}, {"detail", detail}});
            }

            sqlite3_close(db);

            csmn_set_json(res, {
                {"ok", true},
                {"id", node_id},
                {"post_id", post_id}
            });
        });

    server.Post("/api/v4/circlestack/memory-nodes/items/add",
        [&](const httplib::Request& req, httplib::Response& res) {
            std::string actor_fp;
            std::string actor_role;

            if (!deps.require_user_auth_users_actor ||
                !deps.require_user_auth_users_actor(
                    req, res, deps.cookie_key, deps.users, &actor_fp, &actor_role)) {
                return;
            }

            json body = json::parse(req.body, nullptr, false);
            if (!body.is_object()) {
                res.status = 400;
                return csmn_set_json(res, {{"ok", false}, {"error", "invalid_json"}});
            }

            const int node_id = body.value("node_id", 0);
            const std::string raw_media_path = csmn_trim(body.value("media_path", ""));
            const std::string caption = csmn_limit(body.value("caption", ""), 512);

            if (node_id <= 0 || raw_media_path.empty()) {
                res.status = 400;
                return csmn_set_json(res, {{"ok", false}, {"error", "invalid_input"}});
            }

            std::string media_norm;
            std::string media_kind;
            std::string media_err;
            if (!csmn_validate_media_path(
                    deps,
                    actor_fp,
                    raw_media_path,
                    &media_norm,
                    &media_kind,
                    &media_err)) {
                res.status = 400;
                return csmn_set_json(res, {
                    {"ok", false},
                    {"error", media_err.empty() ? "invalid_media" : media_err}
                });
            }

            sqlite3* db = nullptr;
            std::string db_err;
            if (!csmn_open_db(&db, &db_err)) {
                res.status = 500;
                return csmn_set_json(res, {{"ok", false}, {"error", "db_open_failed"}, {"detail", db_err}});
            }

            int post_id = 0;
            if (!csmn_load_node_post_id(db, node_id, &post_id) || post_id <= 0) {
                sqlite3_close(db);
                res.status = 404;
                return csmn_set_json(res, {{"ok", false}, {"error", "node_not_found"}});
            }

            std::string visibility_err;
            if (!csmn_actor_can_see_post(db, post_id, actor_fp, &visibility_err)) {
                sqlite3_close(db);
                res.status = visibility_err == "not_found" ? 404 : 403;
                return csmn_set_json(res, {{"ok", false}, {"error", visibility_err}});
            }

            sqlite3_stmt* st = nullptr;
            if (sqlite3_prepare_v2(db,
                    "INSERT INTO memory_node_items(node_id, owner_fp, media_path, media_kind, caption, created_epoch) "
                    "VALUES(?,?,?,?,?,?)",
                    -1, &st, nullptr) != SQLITE_OK) {
                const std::string detail = sqlite3_errmsg(db);
                sqlite3_close(db);
                res.status = 500;
                return csmn_set_json(res, {{"ok", false}, {"error", "db_prepare_failed"}, {"detail", detail}});
            }

            sqlite3_bind_int(st, 1, node_id);
            sqlite3_bind_text(st, 2, actor_fp.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 3, media_norm.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 4, media_kind.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 5, caption.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(st, 6, (sqlite3_int64)std::time(nullptr));

            if (sqlite3_step(st) != SQLITE_DONE) {
                sqlite3_finalize(st);
                const std::string detail = sqlite3_errmsg(db);
                sqlite3_close(db);
                res.status = 500;
                return csmn_set_json(res, {{"ok", false}, {"error", "db_insert_failed"}, {"detail", detail}});
            }

            const int item_id = (int)sqlite3_last_insert_rowid(db);
            sqlite3_finalize(st);

            json item = csmn_load_single_item(db, item_id, actor_fp, deps);
            sqlite3_close(db);

            csmn_set_json(res, {
                {"ok", true},
                {"node_id", node_id},
                {"item", item}
            });
        });

    server.Post("/api/v4/circlestack/memory-nodes/items/react",
        [&](const httplib::Request& req, httplib::Response& res) {
            std::string actor_fp;
            std::string actor_role;

            if (!deps.require_user_auth_users_actor ||
                !deps.require_user_auth_users_actor(
                    req, res, deps.cookie_key, deps.users, &actor_fp, &actor_role)) {
                return;
            }

            json body = json::parse(req.body, nullptr, false);
            if (!body.is_object()) {
                res.status = 400;
                return csmn_set_json(res, {{"ok", false}, {"error", "invalid_json"}});
            }

            const int item_id = body.value("item_id", body.value("id", 0));
            std::string reaction = csmn_limit(body.value("reaction", ""), 32);

            if (item_id <= 0) {
                res.status = 400;
                return csmn_set_json(res, {{"ok", false}, {"error", "invalid_item_id"}});
            }

            if (!reaction.empty() && !csmn_valid_reaction(reaction)) {
                res.status = 400;
                return csmn_set_json(res, {{"ok", false}, {"error", "invalid_reaction"}});
            }

            sqlite3* db = nullptr;
            std::string db_err;
            if (!csmn_open_db(&db, &db_err)) {
                res.status = 500;
                return csmn_set_json(res, {{"ok", false}, {"error", "db_open_failed"}, {"detail", db_err}});
            }

            sqlite3_stmt* st = nullptr;
            if (sqlite3_prepare_v2(db,
                    "SELECT i.node_id, n.post_id "
                    "FROM memory_node_items i "
                    "JOIN memory_nodes n ON n.id = i.node_id "
                    "WHERE i.id = ?",
                    -1, &st, nullptr) != SQLITE_OK) {
                const std::string detail = sqlite3_errmsg(db);
                sqlite3_close(db);
                res.status = 500;
                return csmn_set_json(res, {{"ok", false}, {"error", "db_prepare_failed"}, {"detail", detail}});
            }

            sqlite3_bind_int(st, 1, item_id);

            int post_id = 0;
            if (sqlite3_step(st) == SQLITE_ROW) {
                post_id = sqlite3_column_int(st, 1);
            }

            sqlite3_finalize(st);
            st = nullptr;

            if (post_id <= 0) {
                sqlite3_close(db);
                res.status = 404;
                return csmn_set_json(res, {{"ok", false}, {"error", "item_not_found"}});
            }

            std::string visibility_err;
            if (!csmn_actor_can_see_post(db, post_id, actor_fp, &visibility_err)) {
                sqlite3_close(db);
                res.status = visibility_err == "not_found" ? 404 : 403;
                return csmn_set_json(res, {{"ok", false}, {"error", visibility_err}});
            }

            if (sqlite3_exec(db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK) {
                const std::string detail = sqlite3_errmsg(db);
                sqlite3_close(db);
                res.status = 500;
                return csmn_set_json(res, {{"ok", false}, {"error", "db_begin_failed"}, {"detail", detail}});
            }

            if (sqlite3_prepare_v2(db,
                    "DELETE FROM memory_node_item_reactions "
                    "WHERE item_id = ? AND actor_fp = ?",
                    -1, &st, nullptr) != SQLITE_OK) {
                sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
                const std::string detail = sqlite3_errmsg(db);
                sqlite3_close(db);
                res.status = 500;
                return csmn_set_json(res, {{"ok", false}, {"error", "db_prepare_failed"}, {"detail", detail}});
            }

            sqlite3_bind_int(st, 1, item_id);
            sqlite3_bind_text(st, 2, actor_fp.c_str(), -1, SQLITE_TRANSIENT);

            if (sqlite3_step(st) != SQLITE_DONE) {
                sqlite3_finalize(st);
                sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
                const std::string detail = sqlite3_errmsg(db);
                sqlite3_close(db);
                res.status = 500;
                return csmn_set_json(res, {{"ok", false}, {"error", "db_delete_failed"}, {"detail", detail}});
            }

            sqlite3_finalize(st);
            st = nullptr;

            if (!reaction.empty()) {
                const sqlite3_int64 now = (sqlite3_int64)std::time(nullptr);

                if (sqlite3_prepare_v2(db,
                        "INSERT INTO memory_node_item_reactions"
                        "(item_id, actor_fp, reaction, created_epoch, updated_epoch) "
                        "VALUES(?,?,?,?,?)",
                        -1, &st, nullptr) != SQLITE_OK) {
                    sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
                    const std::string detail = sqlite3_errmsg(db);
                    sqlite3_close(db);
                    res.status = 500;
                    return csmn_set_json(res, {{"ok", false}, {"error", "db_prepare_failed"}, {"detail", detail}});
                }

                sqlite3_bind_int(st, 1, item_id);
                sqlite3_bind_text(st, 2, actor_fp.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(st, 3, reaction.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(st, 4, now);
                sqlite3_bind_int64(st, 5, now);

                if (sqlite3_step(st) != SQLITE_DONE) {
                    sqlite3_finalize(st);
                    sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
                    const std::string detail = sqlite3_errmsg(db);
                    sqlite3_close(db);
                    res.status = 500;
                    return csmn_set_json(res, {{"ok", false}, {"error", "db_insert_failed"}, {"detail", detail}});
                }

                sqlite3_finalize(st);
                st = nullptr;
            }

            if (sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
                sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
                const std::string detail = sqlite3_errmsg(db);
                sqlite3_close(db);
                res.status = 500;
                return csmn_set_json(res, {{"ok", false}, {"error", "db_commit_failed"}, {"detail", detail}});
            }

            json item = csmn_load_single_item(db, item_id, actor_fp, deps);
            sqlite3_close(db);

            csmn_set_json(res, {
                {"ok", true},
                {"item", item}
            });
        });

    server.Delete("/api/v4/circlestack/memory-nodes/items",
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
                return csmn_set_json(res, {{"ok", false}, {"error", "missing_id"}});
            }

            const int item_id = std::atoi(req.get_param_value("id").c_str());

            sqlite3* db = nullptr;
            std::string db_err;
            if (!csmn_open_db(&db, &db_err)) {
                res.status = 500;
                return csmn_set_json(res, {{"ok", false}, {"error", "db_open_failed"}, {"detail", db_err}});
            }

            sqlite3_stmt* st = nullptr;
            if (sqlite3_prepare_v2(db,
                    "SELECT i.node_id, i.owner_fp, n.owner_fp "
                    "FROM memory_node_items i "
                    "JOIN memory_nodes n ON n.id = i.node_id "
                    "WHERE i.id = ?",
                    -1, &st, nullptr) != SQLITE_OK) {
                const std::string detail = sqlite3_errmsg(db);
                sqlite3_close(db);
                res.status = 500;
                return csmn_set_json(res, {{"ok", false}, {"error", "db_prepare_failed"}, {"detail", detail}});
            }

            sqlite3_bind_int(st, 1, item_id);

            int node_id = 0;
            std::string item_owner_fp;
            std::string node_owner_fp;

            if (sqlite3_step(st) == SQLITE_ROW) {
                node_id = sqlite3_column_int(st, 0);

                const char* item_owner = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
                const char* node_owner = reinterpret_cast<const char*>(sqlite3_column_text(st, 2));

                item_owner_fp = item_owner ? item_owner : "";
                node_owner_fp = node_owner ? node_owner : "";
            }

            sqlite3_finalize(st);
            st = nullptr;

            if (node_id <= 0) {
                sqlite3_close(db);
                res.status = 404;
                return csmn_set_json(res, {{"ok", false}, {"error", "not_found"}});
            }

            if (item_owner_fp != actor_fp && node_owner_fp != actor_fp) {
                sqlite3_close(db);
                res.status = 403;
                return csmn_set_json(res, {{"ok", false}, {"error", "forbidden"}});
            }

            csmn_exec(db,
                ("DELETE FROM memory_node_item_reactions WHERE item_id = " + std::to_string(item_id)).c_str());

            if (sqlite3_prepare_v2(db,
                    "DELETE FROM memory_node_items WHERE id = ?",
                    -1, &st, nullptr) != SQLITE_OK) {
                const std::string detail = sqlite3_errmsg(db);
                sqlite3_close(db);
                res.status = 500;
                return csmn_set_json(res, {{"ok", false}, {"error", "db_prepare_failed"}, {"detail", detail}});
            }

            sqlite3_bind_int(st, 1, item_id);

            if (sqlite3_step(st) != SQLITE_DONE) {
                sqlite3_finalize(st);
                const std::string detail = sqlite3_errmsg(db);
                sqlite3_close(db);
                res.status = 500;
                return csmn_set_json(res, {{"ok", false}, {"error", "db_delete_failed"}, {"detail", detail}});
            }

            sqlite3_finalize(st);
            sqlite3_close(db);

            csmn_set_json(res, {
                {"ok", true},
                {"id", item_id},
                {"node_id", node_id}
            });
        });

    server.Get("/api/v4/circlestack/memory-nodes/items/media",
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

            const int item_id = std::atoi(req.get_param_value("id").c_str());

            sqlite3* db = nullptr;
            std::string db_err;
            if (!csmn_open_db(&db, &db_err)) {
                res.status = 500;
                return;
            }

            sqlite3_stmt* st = nullptr;
            if (sqlite3_prepare_v2(db,
                    "SELECT i.node_id, i.owner_fp, i.media_path, n.post_id "
                    "FROM memory_node_items i "
                    "JOIN memory_nodes n ON n.id = i.node_id "
                    "WHERE i.id = ?",
                    -1, &st, nullptr) != SQLITE_OK) {
                sqlite3_close(db);
                res.status = 500;
                return;
            }

            sqlite3_bind_int(st, 1, item_id);

            int post_id = 0;
            std::string owner_fp;
            std::string media_path;

            if (sqlite3_step(st) == SQLITE_ROW) {
                const char* owner_raw = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
                const char* path_raw = reinterpret_cast<const char*>(sqlite3_column_text(st, 2));

                owner_fp = owner_raw ? owner_raw : "";
                media_path = path_raw ? path_raw : "";
                post_id = sqlite3_column_int(st, 3);
            }

            sqlite3_finalize(st);

            if (post_id <= 0 || owner_fp.empty() || media_path.empty()) {
                sqlite3_close(db);
                res.status = 404;
                return;
            }

            std::string visibility_err;
            if (!csmn_actor_can_see_post(db, post_id, actor_fp, &visibility_err)) {
                sqlite3_close(db);
                res.status = visibility_err == "not_found" ? 404 : 403;
                return;
            }

            sqlite3_close(db);

            if (!deps.users) {
                res.status = 500;
                return;
            }

            pqnas::ResolvedExistingPath resolved;
            std::string resolve_err;
            if (!pqnas::resolve_existing_user_file_path(
                    *deps.users,
                    owner_fp,
                    media_path,
                    &resolved,
                    &resolve_err)) {
                res.status = 404;
                return;
            }

            std::error_code ec;
            if (!std::filesystem::is_regular_file(resolved.abs_path, ec)) {
                res.status = 404;
                return;
            }

            if (deps.user_dir_for_fp && deps.users) {
                std::filesystem::path owner_root;

                try {
                    owner_root = deps.user_dir_for_fp(*deps.users, owner_fp);
                } catch (...) {
                    res.status = 500;
                    return;
                }

                std::string symlink_err;
                if (!csmn_path_has_no_symlink_components_below_root(
                        owner_root,
                        resolved.abs_path,
                        &symlink_err)) {
                    res.status = 403;
                    csmn_set_json(res, {{"ok", false}, {"error", "SYMLINK_REJECTED"}});
                    return;
                }
            }

            std::ifstream f(resolved.abs_path, std::ios::binary);
            if (!f) {
                res.status = 404;
                return;
            }

            std::stringstream ss;
            ss << f.rdbuf();

            const std::string mime = csmn_mime_for_path(resolved.abs_path);
            res.set_content(ss.str(), mime.c_str());
        });
}

} // namespace pqnas
