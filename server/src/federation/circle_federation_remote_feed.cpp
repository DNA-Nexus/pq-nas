#include "federation/circle_federation_remote_feed.h"
#include "federation/circle_federation_limits.h"

#include <sqlite3.h>

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace pqnas::federation {

namespace {

static constexpr const char* kCircleFederationRemoteFeedDbPath =
    "/srv/pqnas/config/circlestack_federation_remote_feed.sqlite3";

std::int64_t now_epoch() {
    return static_cast<std::int64_t>(std::time(nullptr));
}

std::string column_text(sqlite3_stmt* st, int col) {
    const char* raw = reinterpret_cast<const char*>(sqlite3_column_text(st, col));
    return raw ? raw : "";
}

bool open_remote_feed_db(sqlite3** out_db, std::string* err) {
    if (!out_db) return false;
    *out_db = nullptr;

    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(kCircleFederationRemoteFeedDbPath).parent_path(),
        ec);

    if (ec) {
        if (err) *err = "failed to create remote feed directory: " + ec.message();
        return false;
    }

    sqlite3* db = nullptr;
    if (sqlite3_open(kCircleFederationRemoteFeedDbPath, &db) != SQLITE_OK) {
        if (err) *err = db ? sqlite3_errmsg(db) : "sqlite3_open failed";
        if (db) sqlite3_close(db);
        return false;
    }

    sqlite3_busy_timeout(db, 5000);
    *out_db = db;
    return true;
}

bool exec_sql(sqlite3* db, const char* sql, std::string* err) {
    char* msg = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &msg);

    if (rc != SQLITE_OK) {
        if (err) *err = msg ? msg : sqlite3_errmsg(db);
        if (msg) sqlite3_free(msg);
        return false;
    }

    if (msg) sqlite3_free(msg);
    return true;
}

std::int64_t count_sql(sqlite3* db, const char* sql) {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
        return 0;
    }

    std::int64_t out = 0;
    if (sqlite3_step(st) == SQLITE_ROW) {
        out = sqlite3_column_int64(st, 0);
    }

    sqlite3_finalize(st);
    return out;
}

CircleFederationRemoteFeedEvent row_to_event(sqlite3_stmt* st) {
    CircleFederationRemoteFeedEvent ev;
    ev.id = sqlite3_column_int64(st, 0);
    ev.received_epoch = sqlite3_column_int64(st, 1);
    ev.created_epoch = sqlite3_column_int64(st, 2);
    ev.circle_id = column_text(st, 3);
    ev.event_id = column_text(st, 4);
    ev.event_type = column_text(st, 5);
    ev.origin_nas = column_text(st, 6);
    ev.target_type = column_text(st, 7);
    ev.post_id = sqlite3_column_int64(st, 8);
    ev.reply_id = sqlite3_column_int64(st, 9);
    ev.actor_fp = column_text(st, 10);
    ev.reaction = column_text(st, 11);
    ev.event_json = column_text(st, 12);
    return ev;
}

} // namespace

bool ensure_circle_federation_remote_feed(std::string* err) {
    sqlite3* db = nullptr;
    if (!open_remote_feed_db(&db, err)) return false;

    const char* schema_sql =
        "PRAGMA journal_mode=WAL;"
        "PRAGMA busy_timeout=5000;"
        "CREATE TABLE IF NOT EXISTS circle_federation_remote_feed ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "received_epoch INTEGER NOT NULL,"
        "created_epoch INTEGER NOT NULL DEFAULT 0,"
        "circle_id TEXT NOT NULL,"
        "event_id TEXT NOT NULL,"
        "event_type TEXT NOT NULL,"
        "origin_nas TEXT NOT NULL,"
        "target_type TEXT NOT NULL DEFAULT '',"
        "post_id INTEGER NOT NULL DEFAULT 0,"
        "reply_id INTEGER NOT NULL DEFAULT 0,"
        "actor_fp TEXT NOT NULL DEFAULT '',"
        "reaction TEXT NOT NULL DEFAULT '',"
        "event_json TEXT NOT NULL"
        ");"
        "CREATE UNIQUE INDEX IF NOT EXISTS idx_circle_remote_feed_event_id "
        "ON circle_federation_remote_feed(event_id);"
        "CREATE INDEX IF NOT EXISTS idx_circle_remote_feed_circle_created "
        "ON circle_federation_remote_feed(circle_id, created_epoch DESC, id DESC);"
        "CREATE INDEX IF NOT EXISTS idx_circle_remote_feed_origin "
        "ON circle_federation_remote_feed(origin_nas, id);";

    const bool ok = exec_sql(db, schema_sql, err);
    sqlite3_close(db);
    return ok;
}

bool store_circle_federation_remote_feed_event(
    const std::string& circle_id,
    const std::string& event_id,
    const std::string& event_type,
    const std::string& origin_nas,
    std::int64_t created_epoch,
    const std::string& target_type,
    std::int64_t post_id,
    std::int64_t reply_id,
    const std::string& actor_fp,
    const std::string& reaction,
    const std::string& event_json,
    std::string* err) {
    if (circle_id.empty() || event_id.empty() || event_type.empty() ||
        origin_nas.empty() || event_json.empty()) {
        if (err) *err = "missing required remote feed event field";
        return false;
    }

    if (event_json.size() > kMaxCircleFederationEventJsonBytes) {
        if (err) *err = "federation event JSON too large";
        return false;
    }

    if (!ensure_circle_federation_remote_feed(err)) return false;

    sqlite3* db = nullptr;
    if (!open_remote_feed_db(&db, err)) return false;

    sqlite3_stmt* st = nullptr;
    const char* sql =
        "INSERT OR IGNORE INTO circle_federation_remote_feed "
        "(received_epoch, created_epoch, circle_id, event_id, event_type, origin_nas, "
        " target_type, post_id, reply_id, actor_fp, reaction, event_json) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";

    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
        if (err) *err = sqlite3_errmsg(db);
        sqlite3_close(db);
        return false;
    }

    sqlite3_bind_int64(st, 1, now_epoch());
    sqlite3_bind_int64(st, 2, created_epoch);
    sqlite3_bind_text(st, 3, circle_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, event_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, event_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 6, origin_nas.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 7, target_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 8, post_id);
    sqlite3_bind_int64(st, 9, reply_id);
    sqlite3_bind_text(st, 10, actor_fp.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 11, reaction.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 12, event_json.c_str(), -1, SQLITE_TRANSIENT);

    const int rc = sqlite3_step(st);
    sqlite3_finalize(st);

    if (rc != SQLITE_DONE) {
        if (err) *err = sqlite3_errmsg(db);
        sqlite3_close(db);
        return false;
    }

    sqlite3_close(db);

    std::string prune_err;
    if (!prune_circle_federation_remote_feed(
            kMaxCircleFederationRemoteFeedRows,
            &prune_err)) {
        if (err) *err = prune_err;
        return false;
    }

    return true;
}


bool prune_circle_federation_remote_feed(
    int max_rows,
    std::string* err) {
    if (max_rows <= 0) {
        if (err) *err = "invalid remote feed prune max_rows";
        return false;
    }

    if (!ensure_circle_federation_remote_feed(err)) return false;

    sqlite3* db = nullptr;
    if (!open_remote_feed_db(&db, err)) return false;

    sqlite3_stmt* st = nullptr;
    const char* sql =
        "DELETE FROM circle_federation_remote_feed "
        "WHERE id NOT IN ("
        "  SELECT id FROM circle_federation_remote_feed "
        "  ORDER BY id DESC "
        "  LIMIT ?"
        ")";

    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
        if (err) *err = sqlite3_errmsg(db);
        sqlite3_close(db);
        return false;
    }

    sqlite3_bind_int(st, 1, max_rows);

    const int rc = sqlite3_step(st);
    sqlite3_finalize(st);

    if (rc != SQLITE_DONE) {
        if (err) *err = sqlite3_errmsg(db);
        sqlite3_close(db);
        return false;
    }

    sqlite3_close(db);
    return true;
}


std::vector<CircleFederationRemoteFeedEvent> list_circle_federation_remote_feed(
    int limit,
    std::string* err) {
    std::vector<CircleFederationRemoteFeedEvent> out;

    if (!ensure_circle_federation_remote_feed(err)) return out;

    limit = std::clamp(limit, 1, 500);

    sqlite3* db = nullptr;
    if (!open_remote_feed_db(&db, err)) return out;

    sqlite3_stmt* st = nullptr;
    const char* sql =
        "SELECT id, received_epoch, created_epoch, circle_id, event_id, event_type, "
        "origin_nas, target_type, post_id, reply_id, actor_fp, reaction, event_json "
        "FROM circle_federation_remote_feed "
        "ORDER BY created_epoch DESC, id DESC "
        "LIMIT ?";

    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
        if (err) *err = sqlite3_errmsg(db);
        sqlite3_close(db);
        return out;
    }

    sqlite3_bind_int(st, 1, limit);

    while (sqlite3_step(st) == SQLITE_ROW) {
        out.push_back(row_to_event(st));
    }

    sqlite3_finalize(st);
    sqlite3_close(db);
    return out;
}

CircleFederationRemoteFeedStats circle_federation_remote_feed_stats(std::string* err) {
    CircleFederationRemoteFeedStats stats;

    if (!ensure_circle_federation_remote_feed(err)) return stats;

    sqlite3* db = nullptr;
    if (!open_remote_feed_db(&db, err)) return stats;

    stats.total = count_sql(db, "SELECT COUNT(*) FROM circle_federation_remote_feed");
    stats.posts = count_sql(db, "SELECT COUNT(*) FROM circle_federation_remote_feed WHERE event_type = 'circle.post.created'");
    stats.replies = count_sql(db, "SELECT COUNT(*) FROM circle_federation_remote_feed WHERE event_type = 'circle.reply.created'");
    stats.reaction_created = count_sql(db, "SELECT COUNT(*) FROM circle_federation_remote_feed WHERE event_type = 'circle.reaction.created'");
    stats.reaction_removed = count_sql(db, "SELECT COUNT(*) FROM circle_federation_remote_feed WHERE event_type = 'circle.reaction.removed'");

    sqlite3_close(db);
    return stats;
}

} // namespace pqnas::federation
