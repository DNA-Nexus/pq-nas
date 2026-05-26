#include "federation/circle_federation_inbox.h"
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

static constexpr const char* kCircleFederationInboxDbPath =
    "/srv/pqnas/config/circlestack_federation_inbox.sqlite3";

std::int64_t now_epoch() {
    return static_cast<std::int64_t>(std::time(nullptr));
}

std::string column_text(sqlite3_stmt* st, int col) {
    const char* raw = reinterpret_cast<const char*>(sqlite3_column_text(st, col));
    return raw ? raw : "";
}

bool open_inbox_db(sqlite3** out_db, std::string* err) {
    if (!out_db) return false;
    *out_db = nullptr;

    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(kCircleFederationInboxDbPath).parent_path(),
        ec);

    if (ec) {
        if (err) *err = "failed to create inbox directory: " + ec.message();
        return false;
    }

    sqlite3* db = nullptr;
    if (sqlite3_open(kCircleFederationInboxDbPath, &db) != SQLITE_OK) {
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

CircleFederationInboxEvent row_to_event(sqlite3_stmt* st) {
    CircleFederationInboxEvent ev;
    ev.id = sqlite3_column_int64(st, 0);
    ev.received_epoch = sqlite3_column_int64(st, 1);
    ev.created_epoch = sqlite3_column_int64(st, 2);
    ev.status = column_text(st, 3);
    ev.circle_id = column_text(st, 4);
    ev.event_id = column_text(st, 5);
    ev.event_type = column_text(st, 6);
    ev.origin_nas = column_text(st, 7);
    ev.event_key = column_text(st, 8);
    ev.event_json = column_text(st, 9);
    ev.last_error = column_text(st, 10);
    return ev;
}

} // namespace

bool ensure_circle_federation_inbox(std::string* err) {
    sqlite3* db = nullptr;
    if (!open_inbox_db(&db, err)) return false;

    const char* schema_sql =
        "PRAGMA journal_mode=WAL;"
        "PRAGMA busy_timeout=5000;"
        "CREATE TABLE IF NOT EXISTS circle_federation_inbox ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "received_epoch INTEGER NOT NULL,"
        "created_epoch INTEGER NOT NULL DEFAULT 0,"
        "status TEXT NOT NULL DEFAULT 'pending',"
        "circle_id TEXT NOT NULL,"
        "event_id TEXT NOT NULL,"
        "event_type TEXT NOT NULL,"
        "origin_nas TEXT NOT NULL,"
        "event_key TEXT NOT NULL,"
        "event_json TEXT NOT NULL,"
        "last_error TEXT NOT NULL DEFAULT ''"
        ");"
        "CREATE UNIQUE INDEX IF NOT EXISTS idx_circle_fed_inbox_event_id "
        "ON circle_federation_inbox(event_id);"
        "CREATE INDEX IF NOT EXISTS idx_circle_fed_inbox_status "
        "ON circle_federation_inbox(status, id);"
        "CREATE INDEX IF NOT EXISTS idx_circle_fed_inbox_circle "
        "ON circle_federation_inbox(circle_id, id);";

    const bool ok = exec_sql(db, schema_sql, err);
    sqlite3_close(db);
    return ok;
}

bool store_circle_federation_inbox_event(
    const std::string& circle_id,
    const std::string& event_id,
    const std::string& event_type,
    const std::string& origin_nas,
    std::int64_t created_epoch,
    const std::string& event_key,
    const std::string& event_json,
    std::string* err) {
    if (circle_id.empty() || event_id.empty() || event_type.empty() ||
        origin_nas.empty() || event_key.empty() || event_json.empty()) {
        if (err) *err = "missing required inbox event field";
        return false;
    }

    if (event_json.size() > kMaxCircleFederationEventJsonBytes) {
        if (err) *err = "federation event JSON too large";
        return false;
    }

    if (!ensure_circle_federation_inbox(err)) return false;

    sqlite3* db = nullptr;
    if (!open_inbox_db(&db, err)) return false;

    sqlite3_stmt* st = nullptr;
    const char* sql =
        "INSERT OR IGNORE INTO circle_federation_inbox "
        "(received_epoch, created_epoch, status, circle_id, event_id, event_type, "
        " origin_nas, event_key, event_json, last_error) "
        "VALUES (?, ?, 'pending', ?, ?, ?, ?, ?, ?, '')";

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
    sqlite3_bind_text(st, 7, event_key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 8, event_json.c_str(), -1, SQLITE_TRANSIENT);

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

std::vector<CircleFederationInboxEvent> list_circle_federation_inbox(
    int limit,
    std::string* err) {
    std::vector<CircleFederationInboxEvent> out;

    if (!ensure_circle_federation_inbox(err)) return out;

    limit = std::clamp(limit, 1, 500);

    sqlite3* db = nullptr;
    if (!open_inbox_db(&db, err)) return out;

    sqlite3_stmt* st = nullptr;
    const char* sql =
        "SELECT id, received_epoch, created_epoch, status, circle_id, event_id, "
        "event_type, origin_nas, event_key, event_json, last_error "
        "FROM circle_federation_inbox "
        "ORDER BY id DESC "
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

CircleFederationInboxStats circle_federation_inbox_stats(std::string* err) {
    CircleFederationInboxStats stats;

    if (!ensure_circle_federation_inbox(err)) return stats;

    sqlite3* db = nullptr;
    if (!open_inbox_db(&db, err)) return stats;

    stats.total = count_sql(db, "SELECT COUNT(*) FROM circle_federation_inbox");
    stats.pending = count_sql(db, "SELECT COUNT(*) FROM circle_federation_inbox WHERE status = 'pending'");
    stats.applied = count_sql(db, "SELECT COUNT(*) FROM circle_federation_inbox WHERE status = 'applied'");
    stats.ignored = count_sql(db, "SELECT COUNT(*) FROM circle_federation_inbox WHERE status = 'ignored'");
    stats.failed = count_sql(db, "SELECT COUNT(*) FROM circle_federation_inbox WHERE status = 'failed'");

    sqlite3_close(db);
    return stats;
}

std::string clamp_inbox_error(std::string value) {
    constexpr std::size_t kMax = 2048;
    if (value.size() > kMax) {
        value.resize(kMax);
        value += "...[truncated]";
    }
    return value;
}

bool update_circle_federation_inbox_status(
    std::int64_t id,
    const std::string& status,
    const std::string& last_error,
    std::string* err) {
    if (!ensure_circle_federation_inbox(err)) return false;

    sqlite3* db = nullptr;
    if (!open_inbox_db(&db, err)) return false;

    sqlite3_stmt* st = nullptr;
    const char* sql =
        "UPDATE circle_federation_inbox "
        "SET status = ?, last_error = ? "
        "WHERE id = ?";

    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
        if (err) *err = sqlite3_errmsg(db);
        sqlite3_close(db);
        return false;
    }

    const std::string clamped = clamp_inbox_error(last_error);

    sqlite3_bind_text(st, 1, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, clamped.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 3, id);

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

bool mark_circle_federation_inbox_applied(
    std::int64_t id,
    std::string* err) {
    return update_circle_federation_inbox_status(id, "applied", "", err);
}

bool mark_circle_federation_inbox_ignored(
    std::int64_t id,
    const std::string& reason,
    std::string* err) {
    return update_circle_federation_inbox_status(id, "ignored", reason, err);
}

bool mark_circle_federation_inbox_failed(
    std::int64_t id,
    const std::string& reason,
    std::string* err) {
    return update_circle_federation_inbox_status(id, "failed", reason, err);
}


} // namespace pqnas::federation
