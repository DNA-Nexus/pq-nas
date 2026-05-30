#include "federation/circle_federation_outbox.h"

#include <sqlite3.h>

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace pqnas::federation {

namespace {

static constexpr const char* kCircleFederationOutboxDbPath =
    "/srv/pqnas/config/circlestack_federation_outbox.sqlite3";

std::int64_t now_epoch() {
    return static_cast<std::int64_t>(std::time(nullptr));
}

std::string column_text(sqlite3_stmt* st, int col) {
    const char* raw = reinterpret_cast<const char*>(sqlite3_column_text(st, col));
    return raw ? raw : "";
}

bool open_outbox_db(sqlite3** out_db, std::string* err) {
    if (!out_db) return false;
    *out_db = nullptr;

    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(kCircleFederationOutboxDbPath).parent_path(),
        ec);

    if (ec) {
        if (err) *err = "failed to create outbox directory: " + ec.message();
        return false;
    }

    sqlite3* db = nullptr;
    if (sqlite3_open(kCircleFederationOutboxDbPath, &db) != SQLITE_OK) {
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


std::string clamp_error(std::string value) {
    constexpr std::size_t kMax = 2048;
    if (value.size() > kMax) {
        value.resize(kMax);
        value += "...[truncated]";
    }
    return value;
}

CircleFederationOutboxEvent row_to_event(sqlite3_stmt* st) {
    CircleFederationOutboxEvent ev;
    ev.id = sqlite3_column_int64(st, 0);
    ev.created_epoch = sqlite3_column_int64(st, 1);
    ev.updated_epoch = sqlite3_column_int64(st, 2);
    ev.next_attempt_epoch = sqlite3_column_int64(st, 3);
    ev.attempts = sqlite3_column_int(st, 4);
    ev.status = column_text(st, 5);
    ev.event_type = column_text(st, 6);
    ev.circle_id = column_text(st, 7);
    ev.event_id = column_text(st, 8);
    ev.event_key = column_text(st, 9);
    ev.head_key = column_text(st, 10);
    ev.event_json = column_text(st, 11);
    ev.last_error = column_text(st, 12);
    return ev;
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

} // namespace

bool ensure_circle_federation_outbox(std::string* err) {
    sqlite3* db = nullptr;
    if (!open_outbox_db(&db, err)) return false;

    const char* schema_sql =
        "PRAGMA journal_mode=WAL;"
        "PRAGMA busy_timeout=5000;"
        "CREATE TABLE IF NOT EXISTS circle_federation_outbox ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "created_epoch INTEGER NOT NULL,"
        "updated_epoch INTEGER NOT NULL,"
        "next_attempt_epoch INTEGER NOT NULL,"
        "attempts INTEGER NOT NULL DEFAULT 0,"
        "status TEXT NOT NULL DEFAULT 'pending',"
        "event_type TEXT NOT NULL,"
        "circle_id TEXT NOT NULL,"
        "event_id TEXT NOT NULL,"
        "event_key TEXT NOT NULL,"
        "head_key TEXT NOT NULL,"
        "event_json TEXT NOT NULL,"
        "last_error TEXT NOT NULL DEFAULT ''"
        ");"
        "CREATE UNIQUE INDEX IF NOT EXISTS idx_circle_fed_outbox_event_id "
        "ON circle_federation_outbox(event_id);"
        "CREATE INDEX IF NOT EXISTS idx_circle_fed_outbox_status_next "
        "ON circle_federation_outbox(status, next_attempt_epoch, id);"
        "CREATE INDEX IF NOT EXISTS idx_circle_fed_outbox_circle "
        "ON circle_federation_outbox(circle_id, id);";

    const bool ok = exec_sql(db, schema_sql, err);
    sqlite3_close(db);
    return ok;
}

bool enqueue_circle_federation_event(
    const std::string& event_type,
    const std::string& circle_id,
    const std::string& event_id,
    const std::string& event_key,
    const std::string& head_key,
    const std::string& event_json,
    std::string* err) {
    if (event_type.empty() || circle_id.empty() || event_id.empty() ||
        event_key.empty() || head_key.empty() || event_json.empty()) {
        if (err) *err = "missing required outbox event field";
        return false;
    }

    if (!ensure_circle_federation_outbox(err)) return false;

    sqlite3* db = nullptr;
    if (!open_outbox_db(&db, err)) return false;

    sqlite3_stmt* st = nullptr;
    const char* sql =
        "INSERT OR IGNORE INTO circle_federation_outbox "
        "(created_epoch, updated_epoch, next_attempt_epoch, attempts, status, "
        " event_type, circle_id, event_id, event_key, head_key, event_json, last_error) "
        "VALUES (?, ?, ?, 0, 'pending', ?, ?, ?, ?, ?, ?, '')";

    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
        if (err) *err = sqlite3_errmsg(db);
        sqlite3_close(db);
        return false;
    }

    const std::int64_t now = now_epoch();

    sqlite3_bind_int64(st, 1, now);
    sqlite3_bind_int64(st, 2, now);
    sqlite3_bind_int64(st, 3, now);
    sqlite3_bind_text(st, 4, event_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, circle_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 6, event_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 7, event_key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 8, head_key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 9, event_json.c_str(), -1, SQLITE_TRANSIENT);

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

std::vector<CircleFederationOutboxEvent> list_circle_federation_outbox(
    int limit,
    std::string* err) {
    std::vector<CircleFederationOutboxEvent> out;

    if (!ensure_circle_federation_outbox(err)) return out;

    limit = std::clamp(limit, 1, 500);

    sqlite3* db = nullptr;
    if (!open_outbox_db(&db, err)) return out;

    sqlite3_stmt* st = nullptr;
    const char* sql =
        "SELECT id, created_epoch, updated_epoch, next_attempt_epoch, attempts, "
        "status, event_type, circle_id, event_id, event_key, head_key, event_json, last_error "
        "FROM circle_federation_outbox "
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

CircleFederationOutboxStats circle_federation_outbox_stats(std::string* err) {
    CircleFederationOutboxStats stats;

    if (!ensure_circle_federation_outbox(err)) return stats;

    sqlite3* db = nullptr;
    if (!open_outbox_db(&db, err)) return stats;

    stats.total = count_sql(db, "SELECT COUNT(*) FROM circle_federation_outbox");
    stats.pending = count_sql(db, "SELECT COUNT(*) FROM circle_federation_outbox WHERE status = 'pending'");
    stats.publishing = count_sql(db, "SELECT COUNT(*) FROM circle_federation_outbox WHERE status = 'publishing'");
    stats.done = count_sql(db, "SELECT COUNT(*) FROM circle_federation_outbox WHERE status = 'done'");
    stats.failed = count_sql(db, "SELECT COUNT(*) FROM circle_federation_outbox WHERE status = 'failed'");
    stats.retry_wait = count_sql(db,
        "SELECT COUNT(*) FROM circle_federation_outbox "
        "WHERE status = 'pending' AND next_attempt_epoch > strftime('%s','now')");

    sqlite3_close(db);
    return stats;
}

std::vector<CircleFederationOutboxEvent> claim_circle_federation_outbox_pending(
    int limit,
    int lease_seconds,
    std::string* err) {
    std::vector<CircleFederationOutboxEvent> out;

    if (!ensure_circle_federation_outbox(err)) return out;

    limit = std::clamp(limit, 1, 50);
    lease_seconds = std::clamp(lease_seconds, 10, 3600);

    sqlite3* db = nullptr;
    if (!open_outbox_db(&db, err)) return out;

    if (!exec_sql(db, "BEGIN IMMEDIATE", err)) {
        sqlite3_close(db);
        return out;
    }

    if (!exec_sql(db,
            "UPDATE circle_federation_outbox "
            "SET status = 'pending', "
            "    updated_epoch = strftime('%s','now'), "
            "    last_error = 'Recovered stale publishing lease' "
            "WHERE status = 'publishing' "
            "  AND next_attempt_epoch <= strftime('%s','now')",
            err)) {
        exec_sql(db, "ROLLBACK", nullptr);
        sqlite3_close(db);
        return out;
    }

    std::vector<std::int64_t> ids;
    sqlite3_stmt* select_st = nullptr;

    const char* select_sql =
        "SELECT id FROM circle_federation_outbox "
        "WHERE status = 'pending' AND next_attempt_epoch <= strftime('%s','now') "
        "ORDER BY id ASC "
        "LIMIT ?";

    if (sqlite3_prepare_v2(db, select_sql, -1, &select_st, nullptr) != SQLITE_OK) {
        if (err) *err = sqlite3_errmsg(db);
        exec_sql(db, "ROLLBACK", nullptr);
        sqlite3_close(db);
        return out;
    }

    sqlite3_bind_int(select_st, 1, limit);

    while (sqlite3_step(select_st) == SQLITE_ROW) {
        ids.push_back(sqlite3_column_int64(select_st, 0));
    }

    sqlite3_finalize(select_st);

    sqlite3_stmt* update_st = nullptr;
    const char* update_sql =
        "UPDATE circle_federation_outbox "
        "SET status = 'publishing', "
        "    attempts = attempts + 1, "
        "    updated_epoch = strftime('%s','now'), "
        "    next_attempt_epoch = strftime('%s','now') + ? "
        "WHERE id = ? AND status = 'pending'";

    if (sqlite3_prepare_v2(db, update_sql, -1, &update_st, nullptr) != SQLITE_OK) {
        if (err) *err = sqlite3_errmsg(db);
        exec_sql(db, "ROLLBACK", nullptr);
        sqlite3_close(db);
        return out;
    }

    for (const auto id : ids) {
        sqlite3_reset(update_st);
        sqlite3_clear_bindings(update_st);
        sqlite3_bind_int(update_st, 1, lease_seconds);
        sqlite3_bind_int64(update_st, 2, id);

        if (sqlite3_step(update_st) != SQLITE_DONE) {
            if (err) *err = sqlite3_errmsg(db);
            sqlite3_finalize(update_st);
            exec_sql(db, "ROLLBACK", nullptr);
            sqlite3_close(db);
            return out;
        }
    }

    sqlite3_finalize(update_st);

    if (!exec_sql(db, "COMMIT", err)) {
        sqlite3_close(db);
        return out;
    }

    sqlite3_stmt* row_st = nullptr;
    const char* row_sql =
        "SELECT id, created_epoch, updated_epoch, next_attempt_epoch, attempts, "
        "status, event_type, circle_id, event_id, event_key, head_key, event_json, last_error "
        "FROM circle_federation_outbox "
        "WHERE id = ?";

    if (sqlite3_prepare_v2(db, row_sql, -1, &row_st, nullptr) != SQLITE_OK) {
        if (err) *err = sqlite3_errmsg(db);
        sqlite3_close(db);
        return out;
    }

    for (const auto id : ids) {
        sqlite3_reset(row_st);
        sqlite3_clear_bindings(row_st);
        sqlite3_bind_int64(row_st, 1, id);

        if (sqlite3_step(row_st) == SQLITE_ROW) {
            out.push_back(row_to_event(row_st));
        }
    }

    sqlite3_finalize(row_st);
    sqlite3_close(db);
    return out;
}

bool mark_circle_federation_outbox_done(std::int64_t id, std::string* err) {
    if (!ensure_circle_federation_outbox(err)) return false;

    sqlite3* db = nullptr;
    if (!open_outbox_db(&db, err)) return false;

    sqlite3_stmt* st = nullptr;
    const char* sql =
        "UPDATE circle_federation_outbox "
        "SET status = 'done', updated_epoch = strftime('%s','now'), "
        "    next_attempt_epoch = strftime('%s','now'), last_error = '' "
        "WHERE id = ?";

    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
        if (err) *err = sqlite3_errmsg(db);
        sqlite3_close(db);
        return false;
    }

    sqlite3_bind_int64(st, 1, id);
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

bool mark_circle_federation_outbox_retry(
    std::int64_t id,
    const std::string& last_error,
    int retry_delay_seconds,
    std::string* err) {
    if (!ensure_circle_federation_outbox(err)) return false;

    retry_delay_seconds = std::clamp(retry_delay_seconds, 1, 86400);
    const std::string clamped_error = clamp_error(last_error);

    sqlite3* db = nullptr;
    if (!open_outbox_db(&db, err)) return false;

    sqlite3_stmt* st = nullptr;
    const char* sql =
        "UPDATE circle_federation_outbox "
        "SET status = 'pending', updated_epoch = strftime('%s','now'), "
        "    next_attempt_epoch = strftime('%s','now') + ?, last_error = ? "
        "WHERE id = ?";

    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
        if (err) *err = sqlite3_errmsg(db);
        sqlite3_close(db);
        return false;
    }

    sqlite3_bind_int(st, 1, retry_delay_seconds);
    sqlite3_bind_text(st, 2, clamped_error.c_str(), -1, SQLITE_TRANSIENT);
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

bool mark_circle_federation_outbox_failed(
    std::int64_t id,
    const std::string& last_error,
    std::string* err) {
    if (!ensure_circle_federation_outbox(err)) return false;

    const std::string clamped_error = clamp_error(last_error);

    sqlite3* db = nullptr;
    if (!open_outbox_db(&db, err)) return false;

    sqlite3_stmt* st = nullptr;
    const char* sql =
        "UPDATE circle_federation_outbox "
        "SET status = 'failed', updated_epoch = strftime('%s','now'), "
        "    next_attempt_epoch = strftime('%s','now'), last_error = ? "
        "WHERE id = ?";

    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
        if (err) *err = sqlite3_errmsg(db);
        sqlite3_close(db);
        return false;
    }

    sqlite3_bind_text(st, 1, clamped_error.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, id);

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


int recover_stale_circle_federation_outbox_leases(std::string* err) {
    if (!ensure_circle_federation_outbox(err)) return 0;

    sqlite3* db = nullptr;
    if (!open_outbox_db(&db, err)) return 0;

    sqlite3_stmt* st = nullptr;
    const char* sql =
        "UPDATE circle_federation_outbox "
        "SET status = 'pending', "
        "    updated_epoch = strftime('%s','now'), "
        "    last_error = 'Recovered stale publishing lease' "
        "WHERE status = 'publishing' "
        "  AND next_attempt_epoch <= strftime('%s','now')";

    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
        if (err) *err = sqlite3_errmsg(db);
        sqlite3_close(db);
        return 0;
    }

    const int rc = sqlite3_step(st);
    const int changed = sqlite3_changes(db);
    sqlite3_finalize(st);

    if (rc != SQLITE_DONE) {
        if (err) *err = sqlite3_errmsg(db);
        sqlite3_close(db);
        return 0;
    }

    sqlite3_close(db);
    return changed;
}


} // namespace pqnas::federation
