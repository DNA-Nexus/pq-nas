#include "notepad_store.h"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

#include <fcntl.h>
#include <sqlite3.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

namespace pqnas {
namespace {

using json = nlohmann::json;

void set_err(std::string* err, const std::string& msg) {
    if (err) *err = msg;
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

std::filesystem::path notepad_dir_for_user(const std::filesystem::path& user_dir) {
    return user_dir / ".pqnas" / "apps" / "notepad";
}

std::filesystem::path notepad_note_path_for_user(const std::filesystem::path& user_dir) {
    return notepad_dir_for_user(user_dir) / "note.json";
}

bool ensure_dir_exists_no_symlink(const std::filesystem::path& dir, std::string* err) {
    std::error_code ec;

    const auto st = std::filesystem::symlink_status(dir, ec);
    if (!ec && std::filesystem::is_symlink(st)) {
        // Security: app-private metadata paths must not follow user-created symlinks
        // outside the user's storage root.
        set_err(err, "notepad path contains symlink");
        return false;
    }

    if (ec || !std::filesystem::exists(st)) {
        std::filesystem::create_directory(dir, ec);
        if (ec) {
            set_err(err, "failed to create notepad directory: " + ec.message());
            return false;
        }
    }

    const auto st2 = std::filesystem::symlink_status(dir, ec);
    if (ec || !std::filesystem::is_directory(st2) || std::filesystem::is_symlink(st2)) {
        set_err(err, "notepad path is not a safe directory");
        return false;
    }

    return true;
}

bool ensure_user_notepad_dir(const std::filesystem::path& user_dir, std::string* err) {
    if (user_dir.empty()) {
        set_err(err, "missing user directory");
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::exists(user_dir, ec)) {
        std::filesystem::create_directories(user_dir, ec);
        if (ec) {
            set_err(err, "failed to create user directory: " + ec.message());
            return false;
        }
    }

    if (!ensure_dir_exists_no_symlink(user_dir, err)) return false;
    if (!ensure_dir_exists_no_symlink(user_dir / ".pqnas", err)) return false;
    if (!ensure_dir_exists_no_symlink(user_dir / ".pqnas" / "apps", err)) return false;
    if (!ensure_dir_exists_no_symlink(notepad_dir_for_user(user_dir), err)) return false;

    return true;
}

bool read_small_text_file(const std::filesystem::path& path,
                          std::size_t max_bytes,
                          std::string* out,
                          std::string* err) {
    if (out) out->clear();

    std::error_code ec;
    const auto st = std::filesystem::symlink_status(path, ec);
    if (ec || !std::filesystem::exists(st)) return false;

    if (std::filesystem::is_symlink(st)) {
        // Security: never read app data through a symlink controlled from the
        // user's file tree.
        set_err(err, "notepad note path is a symlink");
        return false;
    }

    if (!std::filesystem::is_regular_file(st)) {
        set_err(err, "notepad note path is not a regular file");
        return false;
    }

    const auto sz = std::filesystem::file_size(path, ec);
    if (ec) {
        set_err(err, "failed to stat notepad note: " + ec.message());
        return false;
    }

    if (sz > max_bytes) {
        set_err(err, "notepad note file too large");
        return false;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        set_err(err, "failed to open notepad note");
        return false;
    }

    std::ostringstream ss;
    ss << in.rdbuf();
    if (!in.good() && !in.eof()) {
        set_err(err, "failed to read notepad note");
        return false;
    }

    if (out) *out = ss.str();
    return true;
}

bool write_text_atomic_no_symlink(const std::filesystem::path& path,
                                  const std::string& text,
                                  std::string* err) {
    const auto dir = path.parent_path();
    if (dir.empty()) {
        set_err(err, "missing note directory");
        return false;
    }

    std::error_code ec;
    const auto existing = std::filesystem::symlink_status(path, ec);
    if (!ec && std::filesystem::exists(existing) && std::filesystem::is_symlink(existing)) {
        // Security: rename replaces the final path, but reject symlinks anyway so
        // the app-data invariant stays clear and audit/debugging remains simple.
        set_err(err, "notepad note path is a symlink");
        return false;
    }

    for (int attempt = 0; attempt < 16; ++attempt) {
        const std::string tmp_name =
            ".note.json.tmp." +
            std::to_string(static_cast<long long>(::getpid())) +
            "." +
            std::to_string(attempt);

        const std::filesystem::path tmp = dir / tmp_name;

        const int fd = ::open(
            tmp.c_str(),
            O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW,
            0600
        );

        if (fd < 0) {
            if (errno == EEXIST) continue;
            set_err(err, std::string("failed to create notepad temp file: ") + std::strerror(errno));
            return false;
        }

        const char* p = text.data();
        std::size_t left = text.size();
        bool ok = true;

        while (left > 0) {
            const ssize_t n = ::write(fd, p, left);
            if (n < 0) {
                if (errno == EINTR) continue;
                ok = false;
                set_err(err, std::string("failed to write notepad temp file: ") + std::strerror(errno));
                break;
            }
            p += n;
            left -= static_cast<std::size_t>(n);
        }

        if (ok && ::fsync(fd) != 0) {
            ok = false;
            set_err(err, std::string("failed to fsync notepad temp file: ") + std::strerror(errno));
        }

        if (::close(fd) != 0 && ok) {
            ok = false;
            set_err(err, std::string("failed to close notepad temp file: ") + std::strerror(errno));
        }

        if (!ok) {
            std::filesystem::remove(tmp, ec);
            return false;
        }

        std::filesystem::rename(tmp, path, ec);
        if (ec) {
            std::filesystem::remove(tmp, ec);
            set_err(err, "failed to replace notepad note: " + ec.message());
            return false;
        }

        return true;
    }

    set_err(err, "failed to allocate unique notepad temp file");
    return false;
}

json parse_marks_json_safe(const std::string& raw) {
    try {
        if (raw.empty()) return json::array();
        const json parsed = json::parse(raw);
        return parsed.is_array() ? parsed : json::array();
    } catch (...) {
        return json::array();
    }
}

std::optional<NotepadNoteRec> parse_note_json(const std::string& raw,
                                              const std::string& expected_owner,
                                              std::string* err) {
    json j;
    try {
        j = json::parse(raw);
    } catch (...) {
        set_err(err, "invalid notepad note json");
        return std::nullopt;
    }

    if (!j.is_object()) {
        set_err(err, "invalid notepad note shape");
        return std::nullopt;
    }

    const std::string owner = j.value("owner_fingerprint", expected_owner);
    if (!expected_owner.empty() && owner != expected_owner) {
        set_err(err, "notepad owner mismatch");
        return std::nullopt;
    }

    NotepadNoteRec r;
    r.owner_fingerprint = owner;
    r.body = j.value("body", "");
    r.revision = j.value("revision", 0LL);
    r.updated_at_epoch = j.value("updated_at", 0LL);

    if (j.contains("marks") && j["marks"].is_array()) {
        r.marks_json = j["marks"].dump();
    } else {
        r.marks_json = "[]";
    }

    if (r.body.size() > kNotepadMaxBodyBytes) {
        set_err(err, "notepad body too large");
        return std::nullopt;
    }

    if (r.marks_json.size() > kNotepadMaxMarksJsonBytes) {
        set_err(err, "notepad marks too large");
        return std::nullopt;
    }

    if (r.revision < 0) r.revision = 0;
    if (r.updated_at_epoch < 0) r.updated_at_epoch = 0;

    return r;
}

bool write_user_note_file(const std::filesystem::path& user_dir,
                          const NotepadNoteRec& note,
                          std::string* err) {
    if (!ensure_user_notepad_dir(user_dir, err)) return false;

    const json out = {
        {"schema", 1},
        {"owner_fingerprint", note.owner_fingerprint},
        {"body", note.body},
        {"marks", parse_marks_json_safe(note.marks_json)},
        {"revision", note.revision},
        {"updated_at", note.updated_at_epoch}
    };

    const std::string text = out.dump(2) + "\n";
    if (text.size() > kNotepadMaxNoteJsonBytes) {
        set_err(err, "notepad note json too large");
        return false;
    }

    return write_text_atomic_no_symlink(notepad_note_path_for_user(user_dir), text, err);
}

std::optional<NotepadNoteRec> read_user_note_file(const std::string& owner_fingerprint,
                                                  const std::filesystem::path& user_dir,
                                                  std::string* err) {
    const auto path = notepad_note_path_for_user(user_dir);
    std::string raw;
    if (!read_small_text_file(path, kNotepadMaxNoteJsonBytes, &raw, err)) {
        return std::nullopt;
    }

    return parse_note_json(raw, owner_fingerprint, err);
}

bool sqlite_table_exists(sqlite3* db, const std::string& table, std::string* err) {
    const char* sql =
        "SELECT 1 FROM sqlite_master "
        "WHERE type = 'table' AND name = ?1 "
        "LIMIT 1;";

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
        set_err(err, sqlite3_errmsg(db));
        return false;
    }

    if (!bind_text(st, 1, table)) {
        set_err(err, sqlite3_errmsg(db));
        sqlite3_finalize(st);
        return false;
    }

    const int rc = sqlite3_step(st);
    const bool found = (rc == SQLITE_ROW);

    if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
        set_err(err, sqlite3_errmsg(db));
    }

    sqlite3_finalize(st);
    return found;
}

bool sqlite_column_exists(sqlite3* db, const char* column, std::string* err) {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, "PRAGMA table_info(notepad_notes);", -1, &st, nullptr) != SQLITE_OK) {
        set_err(err, sqlite3_errmsg(db));
        return false;
    }

    bool found = false;
    int rc = SQLITE_OK;
    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
        if (col_text(st, 1) == column) {
            found = true;
            break;
        }
    }

    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        set_err(err, sqlite3_errmsg(db));
    }

    sqlite3_finalize(st);
    return found;
}

std::optional<NotepadNoteRec> read_legacy_sqlite_note(const std::filesystem::path& legacy_db_path,
                                                      const std::string& owner_fingerprint,
                                                      std::string* err) {
    if (legacy_db_path.empty()) return std::nullopt;

    std::error_code ec;
    if (!std::filesystem::exists(legacy_db_path, ec) || ec) return std::nullopt;

    sqlite3* db = nullptr;
    if (sqlite3_open_v2(legacy_db_path.string().c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return std::nullopt;
    }

    sqlite3_busy_timeout(db, 5000);

    std::string table_err;
    const bool has_table = sqlite_table_exists(db, "notepad_notes", &table_err);
    if (!table_err.empty() || !has_table) {
        sqlite3_close(db);
        if (!table_err.empty()) set_err(err, table_err);
        return std::nullopt;
    }

    std::string col_err;
    const bool has_marks = sqlite_column_exists(db, "marks_json", &col_err);
    if (!col_err.empty()) {
        sqlite3_close(db);
        set_err(err, col_err);
        return std::nullopt;
    }

    const std::string sql =
        has_marks
            ? "SELECT owner_fingerprint, body, marks_json, revision, updated_at_epoch "
              "FROM notepad_notes WHERE owner_fingerprint = ?1 LIMIT 1;"
            : "SELECT owner_fingerprint, body, '[]', revision, updated_at_epoch "
              "FROM notepad_notes WHERE owner_fingerprint = ?1 LIMIT 1;";

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return std::nullopt;
    }

    if (!bind_text(st, 1, owner_fingerprint)) {
        set_err(err, sqlite3_errmsg(db));
        sqlite3_finalize(st);
        sqlite3_close(db);
        return std::nullopt;
    }

    const int rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        NotepadNoteRec r;
        r.owner_fingerprint = col_text(st, 0);
        r.body = col_text(st, 1);
        r.marks_json = col_text(st, 2);
        if (r.marks_json.empty()) r.marks_json = "[]";
        r.revision = static_cast<std::int64_t>(sqlite3_column_int64(st, 3));
        r.updated_at_epoch = static_cast<std::int64_t>(sqlite3_column_int64(st, 4));

        sqlite3_finalize(st);
        sqlite3_close(db);
        return r;
    }

    if (rc != SQLITE_DONE) {
        set_err(err, sqlite3_errmsg(db));
    }

    sqlite3_finalize(st);
    sqlite3_close(db);
    return std::nullopt;
}

} // namespace

NotepadStore::NotepadStore(std::filesystem::path legacy_db_path)
    : legacy_db_path_(std::move(legacy_db_path)) {}

bool NotepadStore::init(std::string* /*err*/) const {
    // File-backed Notepad data is initialized per user directory. The legacy
    // SQLite path is only read during migration and is never written here.
    return true;
}

std::filesystem::path NotepadStore::note_path_for_user(const std::filesystem::path& user_dir) const {
    return notepad_note_path_for_user(user_dir);
}

std::optional<NotepadNoteRec> NotepadStore::get_note(const std::string& owner_fingerprint,
                                                     const std::filesystem::path& user_dir,
                                                     std::string* err) const {
    if (owner_fingerprint.empty()) {
        set_err(err, "missing owner fingerprint");
        return std::nullopt;
    }

    std::string local_err;
    auto local = read_user_note_file(owner_fingerprint, user_dir, &local_err);
    if (!local_err.empty()) {
        set_err(err, local_err);
        return std::nullopt;
    }

    if (local) return local;

    // One-time migration path: if the first Notepad backend saved this user into
    // the central SQLite DB, materialize that note into the user's own storage root.
    std::string legacy_err;
    auto legacy = read_legacy_sqlite_note(legacy_db_path_, owner_fingerprint, &legacy_err);
    if (!legacy_err.empty()) {
        set_err(err, legacy_err);
        return std::nullopt;
    }

    if (legacy) {
        std::string write_err;
        if (!write_user_note_file(user_dir, *legacy, &write_err)) {
            set_err(err, write_err);
            return std::nullopt;
        }
        return legacy;
    }

    return std::nullopt;
}

bool NotepadStore::save_note(const std::string& owner_fingerprint,
                             const std::filesystem::path& user_dir,
                             const std::string& body,
                             const std::string& marks_json,
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

    if (marks_json.size() > kNotepadMaxMarksJsonBytes) {
        set_err(err, "notepad marks too large");
        return false;
    }

    if (expected_revision < 0) {
        set_err(err, "invalid revision");
        return false;
    }

    std::string cur_err;
    auto cur = get_note(owner_fingerprint, user_dir, &cur_err);
    if (!cur_err.empty()) {
        set_err(err, cur_err);
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
                out->marks_json = "[]";
                out->revision = 0;
                out->updated_at_epoch = 0;
            }
        }
        return false;
    }

    NotepadNoteRec next;
    next.owner_fingerprint = owner_fingerprint;
    next.body = body;
    next.marks_json = marks_json.empty() ? "[]" : marks_json;
    next.revision = current_revision + 1;
    next.updated_at_epoch = now_epoch;

    if (!write_user_note_file(user_dir, next, err)) {
        return false;
    }

    if (out) *out = next;
    return true;
}

} // namespace pqnas
