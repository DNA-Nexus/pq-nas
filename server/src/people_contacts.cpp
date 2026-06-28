
#include "people_contacts.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <sstream>
#include <string>
#include <system_error>

#include <sqlite3.h>

namespace pqnas {
namespace {

std::int64_t now_epoch_seconds_local() {
    return static_cast<std::int64_t>(std::time(nullptr));
}

std::string trim_copy_local(const std::string& s) {
    std::size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;

    std::size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;

    return s.substr(a, b - a);
}

void clamp_local(std::string* s, std::size_t max_len) {
    if (s && s->size() > max_len) s->resize(max_len);
}

std::string sqlite_err_local(sqlite3* db) {
    const char* msg = db ? sqlite3_errmsg(db) : nullptr;
    return msg ? std::string(msg) : std::string("sqlite error");
}

bool exec_sql_local(sqlite3* db, const char* sql, std::string* err) {
    char* msg = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &msg);
    if (rc != SQLITE_OK) {
        if (err) *err = msg ? std::string(msg) : sqlite_err_local(db);
        if (msg) sqlite3_free(msg);
        return false;
    }
    if (msg) sqlite3_free(msg);
    return true;
}

std::string col_text_local(sqlite3_stmt* st, int idx) {
    const unsigned char* p = sqlite3_column_text(st, idx);
    return p ? reinterpret_cast<const char*>(p) : std::string{};
}

void bind_text_local(sqlite3_stmt* st, int idx, const std::string& value) {
    sqlite3_bind_text(st, idx, value.c_str(), -1, SQLITE_TRANSIENT);
}

PeopleContactRecord row_to_contact_local(sqlite3_stmt* st) {
    PeopleContactRecord r;
    int i = 0;
    r.id = static_cast<std::int64_t>(sqlite3_column_int64(st, i++));
    r.owner_fingerprint = col_text_local(st, i++);
    r.subject_user_id = col_text_local(st, i++);
    r.subject_fingerprint = col_text_local(st, i++);
    r.subject_kind = col_text_local(st, i++);
    r.display_name = col_text_local(st, i++);
    r.nickname = col_text_local(st, i++);

    r.contact_type = col_text_local(st, i++);
    r.company = col_text_local(st, i++);
    r.title = col_text_local(st, i++);

    r.email = col_text_local(st, i++);
    r.phone = col_text_local(st, i++);
    r.mobile = col_text_local(st, i++);
    r.website = col_text_local(st, i++);

    r.street = col_text_local(st, i++);
    r.postal_code = col_text_local(st, i++);
    r.city = col_text_local(st, i++);
    r.country = col_text_local(st, i++);

    r.delivery_name = col_text_local(st, i++);
    r.delivery_street = col_text_local(st, i++);
    r.delivery_postal_code = col_text_local(st, i++);
    r.delivery_city = col_text_local(st, i++);
    r.delivery_country = col_text_local(st, i++);

    r.tags = col_text_local(st, i++);
    r.status = col_text_local(st, i++);
    r.notes = col_text_local(st, i++);

    r.created_at_epoch = static_cast<std::int64_t>(sqlite3_column_int64(st, i++));
    r.updated_at_epoch = static_cast<std::int64_t>(sqlite3_column_int64(st, i++));
    return r;
}

bool open_db_local(const std::filesystem::path& db_path, sqlite3** out, std::string* err) {
    *out = nullptr;

    std::error_code ec;
    std::filesystem::create_directories(db_path.parent_path(), ec);
    if (ec) {
        if (err) *err = "failed to create people db directory: " + ec.message();
        return false;
    }

    sqlite3* db = nullptr;
    if (sqlite3_open(db_path.string().c_str(), &db) != SQLITE_OK) {
        if (err) *err = db ? sqlite3_errmsg(db) : "sqlite open failed";
        if (db) sqlite3_close(db);
        return false;
    }

    sqlite3_busy_timeout(db, 5000);
    *out = db;
    return true;
}

bool people_column_exists_local(sqlite3* db, const std::string& column, bool* exists, std::string* err) {
    if (exists) *exists = false;

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, "PRAGMA table_info(people_contacts)", -1, &st, nullptr) != SQLITE_OK) {
        if (err) *err = sqlite_err_local(db);
        return false;
    }

    while (true) {
        const int rc = sqlite3_step(st);
        if (rc == SQLITE_ROW) {
            const std::string name = col_text_local(st, 1);
            if (name == column) {
                if (exists) *exists = true;
                sqlite3_finalize(st);
                return true;
            }
            continue;
        }

        if (rc == SQLITE_DONE) break;

        if (err) *err = sqlite_err_local(db);
        sqlite3_finalize(st);
        return false;
    }

    sqlite3_finalize(st);
    return true;
}

bool add_people_column_if_missing_local(sqlite3* db,
                                        const std::string& column,
                                        const std::string& declaration,
                                        std::string* err) {
    bool exists = false;
    if (!people_column_exists_local(db, column, &exists, err)) return false;
    if (exists) return true;

    const std::string sql = "ALTER TABLE people_contacts ADD COLUMN " + declaration + ";";
    return exec_sql_local(db, sql.c_str(), err);
}

bool ensure_schema_local(sqlite3* db, std::string* err) {
    if (!exec_sql_local(db, "PRAGMA journal_mode=WAL;", err)) return false;
    if (!exec_sql_local(db, "PRAGMA busy_timeout=5000;", err)) return false;

    static const char* kSchema = R"SQL(
CREATE TABLE IF NOT EXISTS people_contacts (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    owner_fingerprint TEXT NOT NULL,
    subject_user_id TEXT NOT NULL DEFAULT '',
    subject_fingerprint TEXT NOT NULL,
    subject_kind TEXT NOT NULL DEFAULT 'manual_contact',
    display_name TEXT NOT NULL,
    nickname TEXT NOT NULL DEFAULT '',

    contact_type TEXT NOT NULL DEFAULT 'person',
    company TEXT NOT NULL DEFAULT '',
    title TEXT NOT NULL DEFAULT '',

    email TEXT NOT NULL DEFAULT '',
    phone TEXT NOT NULL DEFAULT '',
    mobile TEXT NOT NULL DEFAULT '',
    website TEXT NOT NULL DEFAULT '',

    street TEXT NOT NULL DEFAULT '',
    postal_code TEXT NOT NULL DEFAULT '',
    city TEXT NOT NULL DEFAULT '',
    country TEXT NOT NULL DEFAULT '',

    delivery_name TEXT NOT NULL DEFAULT '',
    delivery_street TEXT NOT NULL DEFAULT '',
    delivery_postal_code TEXT NOT NULL DEFAULT '',
    delivery_city TEXT NOT NULL DEFAULT '',
    delivery_country TEXT NOT NULL DEFAULT '',

    tags TEXT NOT NULL DEFAULT '',
    status TEXT NOT NULL DEFAULT 'active',
    notes TEXT NOT NULL DEFAULT '',

    created_at_epoch INTEGER NOT NULL,
    updated_at_epoch INTEGER NOT NULL,
    UNIQUE(owner_fingerprint, subject_fingerprint)
);

CREATE INDEX IF NOT EXISTS idx_people_contacts_owner_name
ON people_contacts(owner_fingerprint, display_name COLLATE NOCASE);

CREATE INDEX IF NOT EXISTS idx_people_contacts_owner_kind
ON people_contacts(owner_fingerprint, subject_kind);

)SQL";

    if (!exec_sql_local(db, kSchema, err)) return false;

    struct ColumnDef {
        const char* name;
        const char* decl;
    };

    const ColumnDef cols[] = {
        {"contact_type", "contact_type TEXT NOT NULL DEFAULT 'person'"},
        {"company", "company TEXT NOT NULL DEFAULT ''"},
        {"title", "title TEXT NOT NULL DEFAULT ''"},
        {"email", "email TEXT NOT NULL DEFAULT ''"},
        {"phone", "phone TEXT NOT NULL DEFAULT ''"},
        {"mobile", "mobile TEXT NOT NULL DEFAULT ''"},
        {"website", "website TEXT NOT NULL DEFAULT ''"},
        {"street", "street TEXT NOT NULL DEFAULT ''"},
        {"postal_code", "postal_code TEXT NOT NULL DEFAULT ''"},
        {"city", "city TEXT NOT NULL DEFAULT ''"},
        {"country", "country TEXT NOT NULL DEFAULT ''"},
        {"delivery_name", "delivery_name TEXT NOT NULL DEFAULT ''"},
        {"delivery_street", "delivery_street TEXT NOT NULL DEFAULT ''"},
        {"delivery_postal_code", "delivery_postal_code TEXT NOT NULL DEFAULT ''"},
        {"delivery_city", "delivery_city TEXT NOT NULL DEFAULT ''"},
        {"delivery_country", "delivery_country TEXT NOT NULL DEFAULT ''"},
        {"tags", "tags TEXT NOT NULL DEFAULT ''"},
        {"status", "status TEXT NOT NULL DEFAULT 'active'"}
    };

    for (const auto& c : cols) {
        if (!add_people_column_if_missing_local(db, c.name, c.decl, err)) return false;
    }

    // These indexes depend on migrated columns, so create them only after
    // ALTER TABLE has completed for older existing databases.
    if (!exec_sql_local(db,
        "CREATE INDEX IF NOT EXISTS idx_people_contacts_owner_type "
        "ON people_contacts(owner_fingerprint, contact_type);",
        err)) {
        return false;
    }

    if (!exec_sql_local(db,
        "CREATE INDEX IF NOT EXISTS idx_people_contacts_owner_status "
        "ON people_contacts(owner_fingerprint, status);",
        err)) {
        return false;
    }

    return true;
}

} // namespace

PeopleContactsStore::PeopleContactsStore(std::filesystem::path db_path)
    : db_path_(std::move(db_path)) {}

bool PeopleContactsStore::init(std::string* err) const {
    sqlite3* db = nullptr;
    if (!open_db_local(db_path_, &db, err)) return false;

    const bool ok = ensure_schema_local(db, err);
    sqlite3_close(db);
    return ok;
}

bool PeopleContactsStore::list_for_owner(const std::string& owner_fp,
                                         std::vector<PeopleContactRecord>* out,
                                         std::string* err) const {
    if (!out) return false;
    out->clear();

    sqlite3* db = nullptr;
    if (!open_db_local(db_path_, &db, err)) return false;
    if (!ensure_schema_local(db, err)) {
        sqlite3_close(db);
        return false;
    }

    static const char* kSql = R"SQL(
SELECT id, owner_fingerprint, subject_user_id, subject_fingerprint, subject_kind,
       display_name, nickname,
       contact_type, company, title,
       email, phone, mobile, website,
       street, postal_code, city, country,
       delivery_name, delivery_street, delivery_postal_code, delivery_city, delivery_country,
       tags, status, notes,
       created_at_epoch, updated_at_epoch
FROM people_contacts
WHERE owner_fingerprint = ?
ORDER BY display_name COLLATE NOCASE ASC, subject_fingerprint ASC
LIMIT 1000
)SQL";

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, kSql, -1, &st, nullptr) != SQLITE_OK) {
        if (err) *err = sqlite_err_local(db);
        sqlite3_close(db);
        return false;
    }

    bind_text_local(st, 1, owner_fp);

    while (true) {
        const int rc = sqlite3_step(st);
        if (rc == SQLITE_ROW) {
            out->push_back(row_to_contact_local(st));
            continue;
        }
        if (rc == SQLITE_DONE) break;

        if (err) *err = sqlite_err_local(db);
        sqlite3_finalize(st);
        sqlite3_close(db);
        return false;
    }

    sqlite3_finalize(st);
    sqlite3_close(db);
    return true;
}

bool PeopleContactsStore::find_for_owner(const std::string& owner_fp,
                                         const std::string& subject_fp,
                                         std::optional<PeopleContactRecord>* out,
                                         std::string* err) const {
    if (!out) return false;
    *out = std::nullopt;

    sqlite3* db = nullptr;
    if (!open_db_local(db_path_, &db, err)) return false;
    if (!ensure_schema_local(db, err)) {
        sqlite3_close(db);
        return false;
    }

    static const char* kSql = R"SQL(
SELECT id, owner_fingerprint, subject_user_id, subject_fingerprint, subject_kind,
       display_name, nickname,
       contact_type, company, title,
       email, phone, mobile, website,
       street, postal_code, city, country,
       delivery_name, delivery_street, delivery_postal_code, delivery_city, delivery_country,
       tags, status, notes,
       created_at_epoch, updated_at_epoch
FROM people_contacts
WHERE owner_fingerprint = ? AND subject_fingerprint = ?
LIMIT 1
)SQL";

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, kSql, -1, &st, nullptr) != SQLITE_OK) {
        if (err) *err = sqlite_err_local(db);
        sqlite3_close(db);
        return false;
    }

    bind_text_local(st, 1, owner_fp);
    bind_text_local(st, 2, subject_fp);

    const int rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        *out = row_to_contact_local(st);
    } else if (rc != SQLITE_DONE) {
        if (err) *err = sqlite_err_local(db);
        sqlite3_finalize(st);
        sqlite3_close(db);
        return false;
    }

    sqlite3_finalize(st);
    sqlite3_close(db);
    return true;
}

bool PeopleContactsStore::upsert_for_owner(const std::string& owner_fp,
                                           const PeopleContactRecord& input,
                                           PeopleContactRecord* out,
                                           std::string* err) const {
    const std::string subject_fp = people_canonical_fingerprint(input.subject_fingerprint);
    if (!people_valid_fingerprint(owner_fp) || !people_valid_fingerprint(subject_fp)) {
        if (err) *err = "invalid fingerprint";
        return false;
    }

    const std::string subject_kind = people_normalize_subject_kind(input.subject_kind);
    std::string subject_user_id = trim_copy_local(input.subject_user_id);

    std::string contact_type = people_normalize_contact_type(input.contact_type);
    std::string status = people_normalize_status(input.status);

    std::string display_name = trim_copy_local(input.display_name);
    std::string nickname = trim_copy_local(input.nickname);
    std::string company = trim_copy_local(input.company);
    std::string title = trim_copy_local(input.title);

    std::string email = trim_copy_local(input.email);
    std::string phone = trim_copy_local(input.phone);
    std::string mobile = trim_copy_local(input.mobile);
    std::string website = trim_copy_local(input.website);

    std::string street = trim_copy_local(input.street);
    std::string postal_code = trim_copy_local(input.postal_code);
    std::string city = trim_copy_local(input.city);
    std::string country = trim_copy_local(input.country);

    std::string delivery_name = trim_copy_local(input.delivery_name);
    std::string delivery_street = trim_copy_local(input.delivery_street);
    std::string delivery_postal_code = trim_copy_local(input.delivery_postal_code);
    std::string delivery_city = trim_copy_local(input.delivery_city);
    std::string delivery_country = trim_copy_local(input.delivery_country);

    std::string tags = trim_copy_local(input.tags);
    std::string notes = trim_copy_local(input.notes);

    if (display_name.empty()) {
        display_name = people_fingerprint_short(subject_fp);
    }

    clamp_local(&display_name, 160);
    clamp_local(&nickname, 120);
    clamp_local(&subject_user_id, 160);
    clamp_local(&company, 180);
    clamp_local(&title, 140);

    clamp_local(&email, 254);
    clamp_local(&phone, 80);
    clamp_local(&mobile, 80);
    clamp_local(&website, 240);

    clamp_local(&street, 260);
    clamp_local(&postal_code, 40);
    clamp_local(&city, 120);
    clamp_local(&country, 120);

    clamp_local(&delivery_name, 180);
    clamp_local(&delivery_street, 260);
    clamp_local(&delivery_postal_code, 40);
    clamp_local(&delivery_city, 120);
    clamp_local(&delivery_country, 120);

    clamp_local(&tags, 500);
    clamp_local(&notes, 4000);

    sqlite3* db = nullptr;
    if (!open_db_local(db_path_, &db, err)) return false;
    if (!ensure_schema_local(db, err)) {
        sqlite3_close(db);
        return false;
    }

    const std::int64_t now = now_epoch_seconds_local();

    static const char* kSql = R"SQL(
INSERT INTO people_contacts (
    owner_fingerprint, subject_user_id, subject_fingerprint, subject_kind,
    display_name, nickname,
    contact_type, company, title,
    email, phone, mobile, website,
    street, postal_code, city, country,
    delivery_name, delivery_street, delivery_postal_code, delivery_city, delivery_country,
    tags, status, notes,
    created_at_epoch, updated_at_epoch
) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
ON CONFLICT(owner_fingerprint, subject_fingerprint) DO UPDATE SET
    subject_user_id = excluded.subject_user_id,
    subject_kind = excluded.subject_kind,
    display_name = excluded.display_name,
    nickname = excluded.nickname,
    contact_type = excluded.contact_type,
    company = excluded.company,
    title = excluded.title,
    email = excluded.email,
    phone = excluded.phone,
    mobile = excluded.mobile,
    website = excluded.website,
    street = excluded.street,
    postal_code = excluded.postal_code,
    city = excluded.city,
    country = excluded.country,
    delivery_name = excluded.delivery_name,
    delivery_street = excluded.delivery_street,
    delivery_postal_code = excluded.delivery_postal_code,
    delivery_city = excluded.delivery_city,
    delivery_country = excluded.delivery_country,
    tags = excluded.tags,
    status = excluded.status,
    notes = excluded.notes,
    updated_at_epoch = excluded.updated_at_epoch
)SQL";

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, kSql, -1, &st, nullptr) != SQLITE_OK) {
        if (err) *err = sqlite_err_local(db);
        sqlite3_close(db);
        return false;
    }

    int i = 1;
    bind_text_local(st, i++, owner_fp);
    bind_text_local(st, i++, subject_user_id);
    bind_text_local(st, i++, subject_fp);
    bind_text_local(st, i++, subject_kind);
    bind_text_local(st, i++, display_name);
    bind_text_local(st, i++, nickname);
    bind_text_local(st, i++, contact_type);
    bind_text_local(st, i++, company);
    bind_text_local(st, i++, title);
    bind_text_local(st, i++, email);
    bind_text_local(st, i++, phone);
    bind_text_local(st, i++, mobile);
    bind_text_local(st, i++, website);
    bind_text_local(st, i++, street);
    bind_text_local(st, i++, postal_code);
    bind_text_local(st, i++, city);
    bind_text_local(st, i++, country);
    bind_text_local(st, i++, delivery_name);
    bind_text_local(st, i++, delivery_street);
    bind_text_local(st, i++, delivery_postal_code);
    bind_text_local(st, i++, delivery_city);
    bind_text_local(st, i++, delivery_country);
    bind_text_local(st, i++, tags);
    bind_text_local(st, i++, status);
    bind_text_local(st, i++, notes);
    sqlite3_bind_int64(st, i++, static_cast<sqlite3_int64>(now));
    sqlite3_bind_int64(st, i++, static_cast<sqlite3_int64>(now));

    if (sqlite3_step(st) != SQLITE_DONE) {
        if (err) *err = sqlite_err_local(db);
        sqlite3_finalize(st);
        sqlite3_close(db);
        return false;
    }

    sqlite3_finalize(st);
    sqlite3_close(db);

    if (out) {
        std::optional<PeopleContactRecord> found;
        if (!find_for_owner(owner_fp, subject_fp, &found, err)) return false;
        if (found.has_value()) *out = *found;
    }

    return true;
}

bool PeopleContactsStore::delete_for_owner(const std::string& owner_fp,
                                           const std::string& subject_fp_in,
                                           bool* deleted,
                                           std::string* err) const {
    if (deleted) *deleted = false;

    const std::string subject_fp = people_canonical_fingerprint(subject_fp_in);
    if (!people_valid_fingerprint(owner_fp) || !people_valid_fingerprint(subject_fp)) {
        if (err) *err = "invalid fingerprint";
        return false;
    }

    sqlite3* db = nullptr;
    if (!open_db_local(db_path_, &db, err)) return false;
    if (!ensure_schema_local(db, err)) {
        sqlite3_close(db);
        return false;
    }

    static const char* kSql = R"SQL(
DELETE FROM people_contacts
WHERE owner_fingerprint = ? AND subject_fingerprint = ?
)SQL";

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, kSql, -1, &st, nullptr) != SQLITE_OK) {
        if (err) *err = sqlite_err_local(db);
        sqlite3_close(db);
        return false;
    }

    bind_text_local(st, 1, owner_fp);
    bind_text_local(st, 2, subject_fp);

    if (sqlite3_step(st) != SQLITE_DONE) {
        if (err) *err = sqlite_err_local(db);
        sqlite3_finalize(st);
        sqlite3_close(db);
        return false;
    }

    if (deleted) *deleted = sqlite3_changes(db) > 0;

    sqlite3_finalize(st);
    sqlite3_close(db);
    return true;
}

std::string people_canonical_fingerprint(const std::string& input) {
    std::string s = trim_copy_local(input);

    std::string out;
    out.reserve(s.size());
    for (unsigned char ch : s) {
        if (std::isspace(ch)) continue;
        if (ch == ':' || ch == '-') continue;
        out.push_back(static_cast<char>(std::tolower(ch)));
    }
    return out;
}

bool people_valid_fingerprint(const std::string& fp) {
    if (fp.size() < 16 || fp.size() > 256) return false;

    for (unsigned char ch : fp) {
        if (!std::isxdigit(ch)) return false;
    }

    return true;
}

std::string people_normalize_subject_kind(const std::string& input) {
    std::string s = trim_copy_local(input);
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (s == "manual_contact") return s;
    if (s == "local_user") return s;
    if (s == "external_dna") return s;
    if (s == "fingerprint") return s;
    return "manual_contact";
}

std::string people_normalize_contact_type(const std::string& input) {
    std::string s = trim_copy_local(input);
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (s == "person") return s;
    if (s == "company") return s;
    if (s == "customer") return s;
    if (s == "supplier") return s;
    if (s == "family") return s;
    if (s == "other") return s;
    return "person";
}

std::string people_normalize_status(const std::string& input) {
    std::string s = trim_copy_local(input);
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (s == "active") return s;
    if (s == "inactive") return s;
    if (s == "archived") return s;
    return "active";
}

std::string people_fingerprint_short(const std::string& fp) {
    if (fp.size() <= 16) return fp;
    return fp.substr(0, 8) + "…" + fp.substr(fp.size() - 6);
}

} // namespace pqnas
