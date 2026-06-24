#include "routes_workspace_external_messages.h"

#include "workspace_access_shared.h"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <deque>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace pqnas {
namespace {

using nlohmann::json;

constexpr int k_ext_wsmsg_default_limit = 100;
constexpr int k_ext_wsmsg_max_limit = 200;
constexpr std::size_t k_ext_wsmsg_max_body_bytes = 4000;
constexpr int k_ext_wsmsg_active_cap_per_workspace = 5000;
constexpr sqlite3_int64 k_ext_wsmsg_soft_delete_keep_seconds = 30LL * 24LL * 60LL * 60LL;

constexpr sqlite3_int64 k_ext_wsmsg_rate_window_seconds = 60;
constexpr int k_ext_wsmsg_post_rate_limit = 30;
constexpr int k_ext_wsmsg_read_rate_limit = 120;
constexpr std::size_t k_ext_wsmsg_rate_bucket_cap = 10000;

std::mutex g_ext_wsmsg_schema_mu;
std::mutex g_ext_wsmsg_rate_mu;
std::unordered_map<std::string, std::deque<sqlite3_int64>> g_ext_wsmsg_rate;

struct Db {
    sqlite3* db = nullptr;
    ~Db() { if (db) sqlite3_close(db); }

    Db() = default;
    Db(const Db&) = delete;
    Db& operator=(const Db&) = delete;
};

struct Stmt {
    sqlite3_stmt* st = nullptr;
    ~Stmt() { if (st) sqlite3_finalize(st); }

    Stmt() = default;
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;
};

struct ActorCtx {
    std::string fp;
    std::string name;
    WorkspaceRec workspace;
    WorkspaceMemberRec member;
};

std::string trim_copy_ext_wsmsg(const std::string& s) {
    std::size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;

    std::size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;

    return s.substr(a, b - a);
}

std::string header_value_ext_wsmsg(const httplib::Request& req, const char* key) {
    auto it = req.headers.find(key);
    return it == req.headers.end() ? std::string() : it->second;
}

void reply_json_ext_wsmsg(const WorkspaceFileRouteDeps& deps,
                          httplib::Response& res,
                          int code,
                          const json& body) {
    if (deps.reply_json) {
        deps.reply_json(res, code, body.dump());
        return;
    }

    res.status = code;
    res.set_content(body.dump(), "application/json; charset=utf-8");
}

bool require_same_origin_for_cookie_mutation_ext_wsmsg(
    const httplib::Request& req,
    httplib::Response& res,
    const WorkspaceFileRouteDeps& deps) {

    if (!deps.origin || deps.origin->empty()) {
        reply_json_ext_wsmsg(deps, res, 500, {
            {"ok", false},
            {"error", "server_error"},
            {"message", "origin not configured"}
        });
        return false;
    }

    const std::string authz = header_value_ext_wsmsg(req, "Authorization");
    const bool has_bearer =
        authz.size() > 7 &&
        authz.compare(0, 7, "Bearer ") == 0;

    if (has_bearer) return true;

    const std::string origin = header_value_ext_wsmsg(req, "Origin");
    if (!origin.empty()) {
        if (origin == *deps.origin) return true;

        reply_json_ext_wsmsg(deps, res, 403, {
            {"ok", false},
            {"error", "forbidden"},
            {"message", "origin mismatch"}
        });
        return false;
    }

    const std::string referer = header_value_ext_wsmsg(req, "Referer");
    if (!referer.empty()) {
        const std::string allowed_prefix = *deps.origin + "/";
        if (referer == *deps.origin || referer.rfind(allowed_prefix, 0) == 0) {
            return true;
        }

        reply_json_ext_wsmsg(deps, res, 403, {
            {"ok", false},
            {"error", "forbidden"},
            {"message", "origin mismatch"}
        });
        return false;
    }

    reply_json_ext_wsmsg(deps, res, 403, {
        {"ok", false},
        {"error", "forbidden"},
        {"message", "origin required"}
    });
    return false;
}

sqlite3_int64 now_epoch_ext_wsmsg(const WorkspaceFileRouteDeps& deps) {
    if (deps.now_epoch_sec) return static_cast<sqlite3_int64>(deps.now_epoch_sec());

    return static_cast<sqlite3_int64>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
}

std::string iso_utc_from_epoch_ext_wsmsg(sqlite3_int64 epoch) {
    std::time_t t = static_cast<std::time_t>(epoch);
    std::tm tmv{};
    gmtime_r(&t, &tmv);

    char buf[32]{};
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmv);
    return std::string(buf);
}

std::filesystem::path db_path_ext_wsmsg(const WorkspaceFileRouteDeps& deps) {
    if (!deps.workspaces_path.empty()) {
        std::filesystem::path p(deps.workspaces_path);
        if (p.has_parent_path()) return p.parent_path() / "workspace_messages.sqlite3";
    }

    if (!deps.users_path.empty()) {
        std::filesystem::path p(deps.users_path);
        if (p.has_parent_path()) return p.parent_path() / "workspace_messages.sqlite3";
    }

    return std::filesystem::path("/etc/pqnas/workspace_messages.sqlite3");
}

bool exec_sql_ext_wsmsg(sqlite3* db, const char* sql, std::string* err) {
    char* emsg = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &emsg);
    if (rc != SQLITE_OK) {
        if (err) *err = emsg ? emsg : sqlite3_errmsg(db);
        if (emsg) sqlite3_free(emsg);
        return false;
    }

    if (emsg) sqlite3_free(emsg);
    return true;
}

bool prepare_ext_wsmsg(sqlite3* db, const char* sql, sqlite3_stmt** out, std::string* err) {
    *out = nullptr;
    const int rc = sqlite3_prepare_v2(db, sql, -1, out, nullptr);
    if (rc != SQLITE_OK) {
        if (err) *err = sqlite3_errmsg(db);
        return false;
    }
    return true;
}

std::string sqlite_text_ext_wsmsg(sqlite3_stmt* st, int col) {
    const unsigned char* t = sqlite3_column_text(st, col);
    return t ? reinterpret_cast<const char*>(t) : std::string();
}

bool open_db_ext_wsmsg(const WorkspaceFileRouteDeps& deps, Db* out, std::string* err) {
    if (!out) return false;
    out->db = nullptr;

    const std::filesystem::path path = db_path_ext_wsmsg(deps);

    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            if (err) *err = "failed to create db directory: " + ec.message();
            return false;
        }
    }

    sqlite3* db = nullptr;
    if (sqlite3_open(path.string().c_str(), &db) != SQLITE_OK) {
        if (err) *err = db ? sqlite3_errmsg(db) : "sqlite3_open failed";
        if (db) sqlite3_close(db);
        return false;
    }

    out->db = db;
    sqlite3_busy_timeout(out->db, 3000);

    std::lock_guard<std::mutex> lk(g_ext_wsmsg_schema_mu);

    std::string sqlerr;
    if (!exec_sql_ext_wsmsg(out->db, "PRAGMA busy_timeout=3000;", &sqlerr)) {
        if (err) *err = sqlerr;
        return false;
    }

    if (!exec_sql_ext_wsmsg(out->db, "PRAGMA journal_mode=WAL;", &sqlerr)) {
        if (err) *err = sqlerr;
        return false;
    }

    if (!exec_sql_ext_wsmsg(out->db,
            "CREATE TABLE IF NOT EXISTS workspace_messages ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "workspace_id TEXT NOT NULL,"
            "author_fp TEXT NOT NULL,"
            "author_name TEXT NOT NULL DEFAULT '',"
            "body TEXT NOT NULL,"
            "attachments_json TEXT NOT NULL DEFAULT '[]',"
            "deleted_at_epoch INTEGER NOT NULL DEFAULT 0,"
            "deleted_by_fp TEXT NOT NULL DEFAULT '',"
            "created_at_epoch INTEGER NOT NULL,"
            "created_at TEXT NOT NULL"
            ");",
            &sqlerr)) {
        if (err) *err = sqlerr;
        return false;
    }

    if (!exec_sql_ext_wsmsg(out->db,
            "CREATE INDEX IF NOT EXISTS idx_workspace_messages_ws_id "
            "ON workspace_messages(workspace_id, id);",
            &sqlerr)) {
        if (err) *err = sqlerr;
        return false;
    }

    if (!exec_sql_ext_wsmsg(out->db,
            "CREATE INDEX IF NOT EXISTS idx_workspace_messages_active_ws_id "
            "ON workspace_messages(workspace_id, deleted_at_epoch, id);",
            &sqlerr)) {
        if (err) *err = sqlerr;
        return false;
    }

    if (!exec_sql_ext_wsmsg(out->db,
            "CREATE TABLE IF NOT EXISTS workspace_message_reads ("
            "workspace_id TEXT NOT NULL,"
            "reader_fp TEXT NOT NULL,"
            "last_seen_id INTEGER NOT NULL DEFAULT 0,"
            "last_seen_at_epoch INTEGER NOT NULL DEFAULT 0,"
            "PRIMARY KEY(workspace_id, reader_fp)"
            ");",
            &sqlerr)) {
        if (err) *err = sqlerr;
        return false;
    }

    return true;
}

std::string rate_ip_ext_wsmsg(const httplib::Request& req) {
    return req.remote_addr.empty() ? std::string("unknown") : req.remote_addr;
}

bool rate_allow_ext_wsmsg(const httplib::Request& req,
                          const std::string& action,
                          const std::string& workspace_id,
                          const std::string& actor_fp,
                          sqlite3_int64 now,
                          int limit,
                          sqlite3_int64* retry_after) {
    if (retry_after) *retry_after = 0;
    if (limit <= 0) return false;

    const std::string key =
        action + "|" + rate_ip_ext_wsmsg(req) + "|" + workspace_id + "|" + actor_fp;

    std::lock_guard<std::mutex> lk(g_ext_wsmsg_rate_mu);
    auto& q = g_ext_wsmsg_rate[key];

    while (!q.empty() && q.front() <= now - k_ext_wsmsg_rate_window_seconds) {
        q.pop_front();
    }

    if (static_cast<int>(q.size()) >= limit) {
        if (retry_after) {
            const sqlite3_int64 oldest = q.empty() ? now : q.front();
            const sqlite3_int64 retry = (oldest + k_ext_wsmsg_rate_window_seconds) - now;
            *retry_after = retry > 0 ? retry : 1;
        }
        return false;
    }

    q.push_back(now);

    if (g_ext_wsmsg_rate.size() > k_ext_wsmsg_rate_bucket_cap) {
        for (auto it = g_ext_wsmsg_rate.begin(); it != g_ext_wsmsg_rate.end(); ) {
            auto& hits = it->second;
            while (!hits.empty() && hits.front() <= now - k_ext_wsmsg_rate_window_seconds) {
                hits.pop_front();
            }
            if (hits.empty()) it = g_ext_wsmsg_rate.erase(it);
            else ++it;
        }
    }

    return true;
}

bool load_actor_workspace_ext_wsmsg(const httplib::Request& req,
                                    httplib::Response& res,
                                    const WorkspaceFileRouteDeps& deps,
                                    const std::string& workspace_id,
                                    ActorCtx* out) {
    if (!out) return false;

    if (!deps.users || !deps.workspaces || !deps.cookie_key ||
        !deps.reply_json || !deps.require_user_auth_users_actor) {
        reply_json_ext_wsmsg(deps, res, 500, {
            {"ok", false},
            {"error", "server_error"},
            {"message", "external message routes not fully configured"}
        });
        return false;
    }

    if (!is_valid_workspace_id(workspace_id)) {
        reply_json_ext_wsmsg(deps, res, 400, {
            {"ok", false},
            {"error", "bad_request"},
            {"message", "missing or invalid workspace_id"}
        });
        return false;
    }

    std::string actor_fp;
    std::string actor_name;
    if (!deps.require_user_auth_users_actor(
            req, res, deps.cookie_key, deps.users, &actor_fp, &actor_name)) {
        return false;
    }

    if (!deps.workspaces->load(deps.workspaces_path)) {
        reply_json_ext_wsmsg(deps, res, 500, {
            {"ok", false},
            {"error", "workspaces_reload_failed"},
            {"message", "failed to reload workspaces"}
        });
        return false;
    }

    auto wopt = deps.workspaces->get(workspace_id);
    if (!wopt.has_value() || wopt->status != "enabled") {
        reply_json_ext_wsmsg(deps, res, 404, {
            {"ok", false},
            {"error", "workspace_not_found"},
            {"message", "workspace not found"}
        });
        return false;
    }

    auto mopt = workspace_enabled_member_for_actor(*wopt, actor_fp);
    if (!mopt.has_value()) {
        reply_json_ext_wsmsg(deps, res, 403, {
            {"ok", false},
            {"error", "forbidden"},
            {"message", "not a workspace member"}
        });
        return false;
    }

    out->fp = actor_fp;
    out->name = trim_copy_ext_wsmsg(mopt->display_name);
    if (out->name.empty()) out->name = trim_copy_ext_wsmsg(actor_name);
    if (out->name.empty()) out->name = "Workspace member";
    out->workspace = *wopt;
    out->member = *mopt;
    return true;
}

sqlite3_int64 latest_message_id_ext_wsmsg(sqlite3* db, const std::string& workspace_id) {
    Stmt st;
    std::string err;
    if (!prepare_ext_wsmsg(db,
            "SELECT COALESCE(MAX(id), 0) "
            "FROM workspace_messages "
            "WHERE workspace_id = ?1 AND deleted_at_epoch = 0;",
            &st.st, &err)) {
        return 0;
    }

    sqlite3_bind_text(st.st, 1, workspace_id.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st.st) == SQLITE_ROW) {
        return sqlite3_column_int64(st.st, 0);
    }
    return 0;
}

sqlite3_int64 last_seen_id_ext_wsmsg(sqlite3* db,
                                     const std::string& workspace_id,
                                     const std::string& actor_fp) {
    Stmt st;
    std::string err;
    if (!prepare_ext_wsmsg(db,
            "SELECT last_seen_id "
            "FROM workspace_message_reads "
            "WHERE workspace_id = ?1 AND reader_fp = ?2;",
            &st.st, &err)) {
        return 0;
    }

    sqlite3_bind_text(st.st, 1, workspace_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st.st, 2, actor_fp.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(st.st) == SQLITE_ROW) {
        return sqlite3_column_int64(st.st, 0);
    }
    return 0;
}

sqlite3_int64 unread_count_ext_wsmsg(sqlite3* db,
                                     const std::string& workspace_id,
                                     sqlite3_int64 last_seen_id) {
    Stmt st;
    std::string err;
    if (!prepare_ext_wsmsg(db,
            "SELECT COUNT(*) "
            "FROM workspace_messages "
            "WHERE workspace_id = ?1 AND deleted_at_epoch = 0 AND id > ?2;",
            &st.st, &err)) {
        return 0;
    }

    sqlite3_bind_text(st.st, 1, workspace_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st.st, 2, last_seen_id);

    if (sqlite3_step(st.st) == SQLITE_ROW) {
        return sqlite3_column_int64(st.st, 0);
    }
    return 0;
}

bool mark_read_ext_wsmsg(sqlite3* db,
                         const std::string& workspace_id,
                         const std::string& actor_fp,
                         sqlite3_int64 last_seen_id,
                         sqlite3_int64 now,
                         std::string* err) {
    Stmt st;
    if (!prepare_ext_wsmsg(db,
            "INSERT INTO workspace_message_reads "
            "(workspace_id, reader_fp, last_seen_id, last_seen_at_epoch) "
            "VALUES (?1, ?2, ?3, ?4) "
            "ON CONFLICT(workspace_id, reader_fp) DO UPDATE SET "
            "last_seen_id = MAX(last_seen_id, excluded.last_seen_id), "
            "last_seen_at_epoch = excluded.last_seen_at_epoch;",
            &st.st, err)) {
        return false;
    }

    sqlite3_bind_text(st.st, 1, workspace_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st.st, 2, actor_fp.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st.st, 3, last_seen_id);
    sqlite3_bind_int64(st.st, 4, now);

    const int rc = sqlite3_step(st.st);
    if (rc != SQLITE_DONE) {
        if (err) *err = sqlite3_errmsg(db);
        return false;
    }

    return true;
}

void prune_ext_wsmsg(sqlite3* db, const std::string& workspace_id, sqlite3_int64 now) {
    {
        Stmt st;
        std::string err;
        if (prepare_ext_wsmsg(db,
                "UPDATE workspace_messages "
                "SET deleted_at_epoch = ?2, deleted_by_fp = '' "
                "WHERE id IN ("
                "  SELECT id FROM workspace_messages "
                "  WHERE workspace_id = ?1 AND deleted_at_epoch = 0 "
                "  ORDER BY id DESC "
                "  LIMIT -1 OFFSET ?3"
                ");",
                &st.st, &err)) {
            sqlite3_bind_text(st.st, 1, workspace_id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(st.st, 2, now);
            sqlite3_bind_int(st.st, 3, k_ext_wsmsg_active_cap_per_workspace);
            (void)sqlite3_step(st.st);
        }
    }

    {
        Stmt st;
        std::string err;
        if (prepare_ext_wsmsg(db,
                "DELETE FROM workspace_messages "
                "WHERE deleted_at_epoch > 0 AND deleted_at_epoch < ?1;",
                &st.st, &err)) {
            sqlite3_bind_int64(st.st, 1, now - k_ext_wsmsg_soft_delete_keep_seconds);
            (void)sqlite3_step(st.st);
        }
    }
}

int int_param_ext_wsmsg(const httplib::Request& req, const char* key, int defv) {
    if (!req.has_param(key)) return defv;
    try {
        return std::stoi(req.get_param_value(key));
    } catch (...) {
        return defv;
    }
}

sqlite3_int64 int64_param_ext_wsmsg(const httplib::Request& req, const char* key, sqlite3_int64 defv) {
    if (!req.has_param(key)) return defv;
    try {
        return static_cast<sqlite3_int64>(std::stoll(req.get_param_value(key)));
    } catch (...) {
        return defv;
    }
}

json message_json_ext_wsmsg(sqlite3_stmt* st, const std::string& actor_fp) {
    const sqlite3_int64 id = sqlite3_column_int64(st, 0);
    const std::string author_fp = sqlite_text_ext_wsmsg(st, 1);
    const std::string author_name = sqlite_text_ext_wsmsg(st, 2);
    const std::string body = sqlite_text_ext_wsmsg(st, 3);
    const sqlite3_int64 created_epoch = sqlite3_column_int64(st, 4);
    const std::string created_at = sqlite_text_ext_wsmsg(st, 5);

    return json{
        {"id", id},
        {"author_name", author_name.empty() ? "Workspace member" : author_name},
        {"body", body},
        {"created_at_epoch", created_epoch},
        {"created_at", created_at},
        {"is_own", author_fp == actor_fp}
    };
}

} // namespace

void register_workspace_external_message_routes(
    httplib::Server& srv,
    const WorkspaceFileRouteDeps& deps) {

    // httplib stores route lambdas and calls them long after this registration
    // function has returned. Never capture the stack reference parameter here.
    // Keep a route-owned copy of the dependency bundle instead.
    auto route_deps = std::make_shared<WorkspaceFileRouteDeps>(deps);

    srv.Get("/api/v4/workspaces/external-messages/list",
            [route_deps](const httplib::Request& req, httplib::Response& res) {
        const WorkspaceFileRouteDeps& deps = *route_deps;
        const std::string workspace_id = trim_copy_ext_wsmsg(req.get_param_value("workspace_id"));

        ActorCtx actor;
        if (!load_actor_workspace_ext_wsmsg(req, res, deps, workspace_id, &actor)) return;

        const sqlite3_int64 now = now_epoch_ext_wsmsg(deps);
        sqlite3_int64 retry_after = 0;
        if (!rate_allow_ext_wsmsg(req, "list", workspace_id, actor.fp, now,
                                  k_ext_wsmsg_read_rate_limit, &retry_after)) {
            reply_json_ext_wsmsg(deps, res, 429, {
                {"ok", false},
                {"error", "rate_limited"},
                {"message", "too many message list requests"},
                {"retry_after_seconds", retry_after}
            });
            return;
        }

        Db db;
        std::string err;
        if (!open_db_ext_wsmsg(deps, &db, &err)) {
            reply_json_ext_wsmsg(deps, res, 500, {
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to open workspace message database"}
            });
            return;
        }

        int limit = int_param_ext_wsmsg(req, "limit", k_ext_wsmsg_default_limit);
        if (limit < 1) limit = 1;
        if (limit > k_ext_wsmsg_max_limit) limit = k_ext_wsmsg_max_limit;

        sqlite3_int64 after_id = int64_param_ext_wsmsg(req, "after_id", 0);
        if (after_id < 0) after_id = 0;

        Stmt st;
        if (!prepare_ext_wsmsg(db.db,
                "SELECT id, author_fp, author_name, body, created_at_epoch, created_at "
                "FROM workspace_messages "
                "WHERE workspace_id = ?1 AND deleted_at_epoch = 0 AND id > ?2 "
                "ORDER BY id ASC "
                "LIMIT ?3;",
                &st.st, &err)) {
            reply_json_ext_wsmsg(deps, res, 500, {
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to prepare message list"}
            });
            return;
        }

        sqlite3_bind_text(st.st, 1, workspace_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st.st, 2, after_id);
        sqlite3_bind_int(st.st, 3, limit);

        json messages = json::array();
        while (sqlite3_step(st.st) == SQLITE_ROW) {
            messages.push_back(message_json_ext_wsmsg(st.st, actor.fp));
        }

        const sqlite3_int64 latest_id = latest_message_id_ext_wsmsg(db.db, workspace_id);
        const sqlite3_int64 seen_id = last_seen_id_ext_wsmsg(db.db, workspace_id, actor.fp);

        reply_json_ext_wsmsg(deps, res, 200, {
            {"ok", true},
            {"workspace_id", workspace_id},
            {"messages", messages},
            {"latest_id", latest_id},
            {"last_seen_id", seen_id},
            {"unread_count", unread_count_ext_wsmsg(db.db, workspace_id, seen_id)}
        });
    });

    srv.Post("/api/v4/workspaces/external-messages/post",
             [route_deps](const httplib::Request& req, httplib::Response& res) {
        const WorkspaceFileRouteDeps& deps = *route_deps;
        if (!require_same_origin_for_cookie_mutation_ext_wsmsg(req, res, deps)) return;

        json body_json;
        try {
            body_json = json::parse(req.body.empty() ? "{}" : req.body);
        } catch (...) {
            reply_json_ext_wsmsg(deps, res, 400, {
                {"ok", false},
                {"error", "bad_request"},
                {"message", "invalid json"}
            });
            return;
        }

        const std::string workspace_id = trim_copy_ext_wsmsg(body_json.value("workspace_id", ""));

        ActorCtx actor;
        if (!load_actor_workspace_ext_wsmsg(req, res, deps, workspace_id, &actor)) return;

        const sqlite3_int64 now = now_epoch_ext_wsmsg(deps);
        sqlite3_int64 retry_after = 0;
        if (!rate_allow_ext_wsmsg(req, "post", workspace_id, actor.fp, now,
                                  k_ext_wsmsg_post_rate_limit, &retry_after)) {
            reply_json_ext_wsmsg(deps, res, 429, {
                {"ok", false},
                {"error", "rate_limited"},
                {"message", "too many message posts"},
                {"retry_after_seconds", retry_after}
            });
            return;
        }

        std::string msg = trim_copy_ext_wsmsg(body_json.value("body", ""));
        if (msg.empty()) {
            reply_json_ext_wsmsg(deps, res, 400, {
                {"ok", false},
                {"error", "bad_request"},
                {"message", "message is empty"}
            });
            return;
        }

        if (msg.size() > k_ext_wsmsg_max_body_bytes) {
            reply_json_ext_wsmsg(deps, res, 413, {
                {"ok", false},
                {"error", "payload_too_large"},
                {"message", "message is too large"}
            });
            return;
        }

        Db db;
        std::string err;
        if (!open_db_ext_wsmsg(deps, &db, &err)) {
            reply_json_ext_wsmsg(deps, res, 500, {
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to open workspace message database"}
            });
            return;
        }

        const std::string created_at = iso_utc_from_epoch_ext_wsmsg(now);

        Stmt st;
        if (!prepare_ext_wsmsg(db.db,
                "INSERT INTO workspace_messages "
                "(workspace_id, author_fp, author_name, body, attachments_json, "
                " deleted_at_epoch, deleted_by_fp, created_at_epoch, created_at) "
                "VALUES (?1, ?2, ?3, ?4, '[]', 0, '', ?5, ?6);",
                &st.st, &err)) {
            reply_json_ext_wsmsg(deps, res, 500, {
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to prepare message insert"}
            });
            return;
        }

        sqlite3_bind_text(st.st, 1, workspace_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st.st, 2, actor.fp.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st.st, 3, actor.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st.st, 4, msg.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st.st, 5, now);
        sqlite3_bind_text(st.st, 6, created_at.c_str(), -1, SQLITE_TRANSIENT);

        const int rc = sqlite3_step(st.st);
        if (rc != SQLITE_DONE) {
            reply_json_ext_wsmsg(deps, res, 500, {
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to insert message"}
            });
            return;
        }

        const sqlite3_int64 new_id = sqlite3_last_insert_rowid(db.db);
        prune_ext_wsmsg(db.db, workspace_id, now);
        (void)mark_read_ext_wsmsg(db.db, workspace_id, actor.fp, new_id, now, nullptr);

        if (deps.audit_emit) {
            deps.audit_emit("workspace.external_message_posted", "ok", {
                {"workspace_id", workspace_id},
                {"member_kind", actor.member.member_kind},
                {"role", actor.member.role}
            });
        }

        reply_json_ext_wsmsg(deps, res, 200, {
            {"ok", true},
            {"message", {
                {"id", new_id},
                {"author_name", actor.name},
                {"body", msg},
                {"created_at_epoch", now},
                {"created_at", created_at},
                {"is_own", true}
            }}
        });
    });

    srv.Post("/api/v4/workspaces/external-messages/read",
             [route_deps](const httplib::Request& req, httplib::Response& res) {
        const WorkspaceFileRouteDeps& deps = *route_deps;
        if (!require_same_origin_for_cookie_mutation_ext_wsmsg(req, res, deps)) return;

        json body_json;
        try {
            body_json = json::parse(req.body.empty() ? "{}" : req.body);
        } catch (...) {
            reply_json_ext_wsmsg(deps, res, 400, {
                {"ok", false},
                {"error", "bad_request"},
                {"message", "invalid json"}
            });
            return;
        }

        const std::string workspace_id = trim_copy_ext_wsmsg(body_json.value("workspace_id", ""));

        ActorCtx actor;
        if (!load_actor_workspace_ext_wsmsg(req, res, deps, workspace_id, &actor)) return;

        Db db;
        std::string err;
        if (!open_db_ext_wsmsg(deps, &db, &err)) {
            reply_json_ext_wsmsg(deps, res, 500, {
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to open workspace message database"}
            });
            return;
        }

        sqlite3_int64 last_seen_id = 0;
        try {
            last_seen_id = body_json.value("last_seen_id", 0LL);
        } catch (...) {
            last_seen_id = 0;
        }

        if (last_seen_id <= 0) {
            last_seen_id = latest_message_id_ext_wsmsg(db.db, workspace_id);
        }

        const sqlite3_int64 now = now_epoch_ext_wsmsg(deps);
        if (!mark_read_ext_wsmsg(db.db, workspace_id, actor.fp, last_seen_id, now, &err)) {
            reply_json_ext_wsmsg(deps, res, 500, {
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to mark messages read"}
            });
            return;
        }

        reply_json_ext_wsmsg(deps, res, 200, {
            {"ok", true},
            {"last_seen_id", last_seen_id}
        });
    });
}

} // namespace pqnas
