#include "notepad_store.h"

#include <filesystem>
#include <string>
#include <system_error>

#include <sqlite3.h>

namespace pqnas {
namespace {

void set_err(std::string* err, const std::string& msg) {
    if (err) *err = msg;
}

bool exec_sql(sqlite3* db, const char* sql, std::string* err) {
    char* raw = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &raw);
    if (rc == SQLITE_OK) return true;

    std::string msg = raw ? raw : sqlite3_errmsg(db);
    sqlite3_free(raw);
    set_err(err, msg);
    return false;
}

bool bind_text(sqlite3_stmt* st, int idx, const std::string& value) {
    return sqlite3_bind_text(
        st,
        idx,
        value.c_str(),
        static_cast<int>(value.size()),
        SQLITE_TRANSIENT
    ) == SQLITE_OK;
}

std::string col_text(sqlite3_stmt* st, int idx) {
    const unsigned char* p = sqlite3_column_text(st, idx);
    return p ? reinterpret_cast<const char*>(p) : std::string{};
}

NotepadNoteRec row_to_note(sqlite3_stmt* st) {
    NotepadNoteRec r;
    r.owner_fingerprint = col_text(st, 0);
    r.body = col_text(st, 1);
    r.revision = static_cast<std::int64_t>(sqlite3_column_int64(st, 2));
    r.updated_at_epoch = static_cast<std::int64_t>(sqlite3_column_int64(st, 3));
    return r;
}

bool open_db(const std::filesystem::path& db_path, sqlite3** out, std::string* err) {
    if (!out) {
        set_err(err, "missing output db pointer");
        return false;
    }
    *out = nullptr;

    std::error_code ec;
    const auto parent = db_path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            set_err(err, "failed to create notepad db directory: " + ec.message());
            return false;
        }
    }

    sqlite3* db = nullptr;
    if (sqlite3_open(db_path.string().c_str(), &db) != SQLITE_OK) {
        std::string msg = db ? sqlite3_errmsg(db) : "sqlite open failed";
        if (db) sqlite3_close(db);
        set_err(err, msg);
        return false;
    }

    sqlite3_busy_timeout(db, 5000);
    *out = db;
    return true;
}

std::optional<NotepadNoteRec> get_note_open_db(sqlite3* db,
                                               const std::string& owner_fingerprint,
                                               std::string* err) {
    const char* sql =
        "SELECT owner_fingerprint, body, revision, updated_at_epoch "
        "FROM notepad_notes "
        "WHERE owner_fingerprint = ?1 "
        "LIMIT 1;";

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
        set_err(err, sqlite3_errmsg(db));
        return std::nullopt;
    }

    if (!bind_text(st, 1, owner_fingerprint)) {
        set_err(err, sqlite3_errmsg(db));
        sqlite3_finalize(st);
        return std::nullopt;
    }

    const int rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        NotepadNoteRec r = row_to_note(st);
        sqlite3_finalize(st);
        return r;
    }

    if (rc != SQLITE_DONE) {
        set_err(err, sqlite3_errmsg(db));
    }

    sqlite3_finalize(st);
    return std::nullopt;
}

} // namespace

NotepadStore::NotepadStore(std::filesystem::path db_path)
    : db_path_(std::move(db_path)) {}

bool NotepadStore::init(std::string* err) const {
    sqlite3* db = nullptr;
    if (!open_db(db_path_, &db, err)) return false;

    bool ok = true;
    ok = ok && exec_sql(db, "PRAGMA journal_mode=WAL;", err);
    ok = ok && exec_sql(db, "PRAGMA synchronous=NORMAL;", err);
    ok = ok && exec_sql(db,
        "CREATE TABLE IF NOT EXISTS notepad_notes ("
        "  owner_fingerprint TEXT PRIMARY KEY,"
        "  body TEXT NOT NULL DEFAULT '',"
        "  revision INTEGER NOT NULL DEFAULT 0,"
        "  updated_at_epoch INTEGER NOT NULL DEFAULT 0"
        ");",
        err);

    sqlite3_close(db);
    return ok;
}

std::optional<NotepadNoteRec> NotepadStore::get_note(const std::string& owner_fingerprint,
                                                     std::string* err) const {
    if (owner_fingerprint.empty()) {
        set_err(err, "missing owner fingerprint");
        return std::nullopt;
    }

    if (!init(err)) return std::nullopt;

    sqlite3* db = nullptr;
    if (!open_db(db_path_, &db, err)) return std::nullopt;

    auto out = get_note_open_db(db, owner_fingerprint, err);
    sqlite3_close(db);
    return out;
}

bool NotepadStore::save_note(const std::string& owner_fingerprint,
                             const std::string& body,
                             std::int64_t expected_revision,
                             std::int64_t now_epoch,
                             NotepadNoteRec* out,
                             bool* revision_mismatch,
                             std::string* err) const {
    if (out) *out = NotepadNoteRec{};
    if (revision_mismatch) *revision_mismatch = false;

    if (owner_fingerprint.empty()) {
        set_err(err, "missing owner fingerprint");
        return false;
    }

    if (body.size() > kNotepadMaxBodyBytes) {
        set_err(err, "notepad body too large");
        return false;
    }

    if (expected_revision < 0) {
        set_err(err, "invalid revision");
        return false;
    }

    if (!init(err)) return false;

    sqlite3* db = nullptr;
    if (!open_db(db_path_, &db, err)) return false;

    if (!exec_sql(db, "BEGIN IMMEDIATE;", err)) {
        sqlite3_close(db);
        return false;
    }

    std::string read_err;
    auto cur = get_note_open_db(db, owner_fingerprint, &read_err);
    if (!read_err.empty()) {
        exec_sql(db, "ROLLBACK;", nullptr);
        sqlite3_close(db);
        set_err(err, read_err);
        return false;
    }

    const std::int64_t current_revision = cur ? cur->revision : 0;
    if (expected_revision != current_revision) {
        if (revision_mismatch) *revision_mismatch = true;
        if (out) {
            if (cur) {
                *out = *cur;
            } else {
                out->owner_fingerprint = owner_fingerprint;
                out->body.clear();
                out->revision = 0;
                out->updated_at_epoch = 0;
            }
        }

        exec_sql(db, "ROLLBACK;", nullptr);
        sqlite3_close(db);
        return false;
    }

    const std::int64_t next_revision = current_revision + 1;

    const char* sql =
        "INSERT INTO notepad_notes(owner_fingerprint, body, revision, updated_at_epoch) "
        "VALUES(?1, ?2, ?3, ?4) "
        "ON CONFLICT(owner_fingerprint) DO UPDATE SET "
        "  body = excluded.body,"
        "  revision = excluded.revision,"
        "  updated_at_epoch = excluded.updated_at_epoch;";

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
        set_err(err, sqlite3_errmsg(db));
        exec_sql(db, "ROLLBACK;", nullptr);
        sqlite3_close(db);
        return false;
    }

    bool ok = true;
    ok = ok && bind_text(st, 1, owner_fingerprint);
    ok = ok && bind_text(st, 2, body);
    ok = ok && sqlite3_bind_int64(st, 3, static_cast<sqlite3_int64>(next_revision)) == SQLITE_OK;
    ok = ok && sqlite3_bind_int64(st, 4, static_cast<sqlite3_int64>(now_epoch)) == SQLITE_OK;

    if (!ok) {
        set_err(err, sqlite3_errmsg(db));
        sqlite3_finalize(st);
        exec_sql(db, "ROLLBACK;", nullptr);
        sqlite3_close(db);
        return false;
    }

    const int rc = sqlite3_step(st);
    if (rc != SQLITE_DONE) {
        set_err(err, sqlite3_errmsg(db));
        sqlite3_finalize(st);
        exec_sql(db, "ROLLBACK;", nullptr);
        sqlite3_close(db);
        return false;
    }

    sqlite3_finalize(st);

    if (!exec_sql(db, "COMMIT;", err)) {
        exec_sql(db, "ROLLBACK;", nullptr);
        sqlite3_close(db);
        return false;
    }

    sqlite3_close(db);

    if (out) {
        out->owner_fingerprint = owner_fingerprint;
        out->body = body;
        out->revision = next_revision;
        out->updated_at_epoch = now_epoch;
    }

    return true;
}

} // namespace pqnas
