#include "circle_stack_routes.h"

#include <nlohmann/json.hpp>

#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace {

struct CircleStackPost {
    int id = 0;
    std::string text;
    std::string media_path;
    std::time_t created_epoch = 0;
};


#include <sqlite3.h>

static sqlite3* g_db = nullptr;

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
}

std::vector<CircleStackPost> g_circle_stack_posts;
int g_circle_stack_next_id = 1;

void set_json(httplib::Response& res, const json& body) {
    res.set_content(body.dump(), "application/json; charset=utf-8");
}

} // namespace

void register_circle_stack_routes(httplib::Server& server) {
    server.Get("/api/v4/circlestack/feed",
        [](const httplib::Request&, httplib::Response& res) {
            json out;
            out["ok"] = true;
            out["posts"] = json::array();

            
cs_db_init();

sqlite3_stmt* stmt = nullptr;
sqlite3_prepare_v2(g_db,
    "SELECT id, text, media_path, created_epoch FROM posts ORDER BY id DESC",
    -1, &stmt, nullptr);

while (sqlite3_step(stmt) == SQLITE_ROW) {
    json p;

    int id = sqlite3_column_int(stmt, 0);
    const char* text = (const char*)sqlite3_column_text(stmt, 1);
    const char* media = (const char*)sqlite3_column_text(stmt, 2);
    long long created = sqlite3_column_int64(stmt, 3);

    p["id"] = id;
    p["text"] = text ? text : "";
    p["created_epoch"] = created;

    if (media && media[0]) {
        p["media_url"] = "/api/v4/circlestack/media?id=" + std::to_string(id);
    }

    out["posts"].push_back(p);
}

sqlite3_finalize(stmt);

            set_json(res, out);
        });

    server.Post("/api/v4/circlestack/posts/create",
        [](const httplib::Request& req, httplib::Response& res) {
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

            if (text.empty() && media_path.empty()) {
                res.status = 400;
                set_json(res, {{"ok", false}, {"error", "empty_post"}});
                return;
            }

            CircleStackPost post;
            post.id = g_circle_stack_next_id++;
            post.text = text;
            post.media_path = media_path;
            post.created_epoch = std::time(nullptr);

            g_circle_stack_posts.push_back(post);

cs_db_init();

sqlite3_stmt* stmt = nullptr;
sqlite3_prepare_v2(g_db,
    "INSERT INTO posts(text, media_path, created_epoch) VALUES(?,?,?)",
    -1, &stmt, nullptr);

sqlite3_bind_text(stmt, 1, text.c_str(), -1, SQLITE_TRANSIENT);
sqlite3_bind_text(stmt, 2, media_path.c_str(), -1, SQLITE_TRANSIENT);
sqlite3_bind_int64(stmt, 3, post.created_epoch);

sqlite3_step(stmt);
sqlite3_finalize(stmt);


            set_json(res, {{"ok", true}, {"id", post.id}});
        });

    server.Get("/api/v4/circlestack/media",
        [](const httplib::Request& req, httplib::Response& res) {
            if (!req.has_param("id")) {
                res.status = 400;
                return;
            }

            const int id = std::atoi(req.get_param_value("id").c_str());

            const CircleStackPost* found = nullptr;
            for (const auto& p : g_circle_stack_posts) {
                if (p.id == id) {
                    found = &p;
                    break;
                }
            }

            if (!found || found->media_path.empty()) {
                res.status = 404;
                return;
            }

            // MVP only. Later: resolve via user's storage root + ACL check.
            std::ifstream f(found->media_path, std::ios::binary);
            if (!f) {
                res.status = 404;
                return;
            }

            std::stringstream ss;
            ss << f.rdbuf();

            res.set_content(ss.str(), "image/jpeg");
        });
}
