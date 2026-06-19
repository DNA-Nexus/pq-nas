#include "workspace_messages.h"

/*
 * Workspace message board backend.
 *
 * This module implements the lightweight per-workspace message board used by
 * File Manager. Messages are stored server-side in a small SQLite database
 * next to the normal PQ-NAS config files. The feature is intentionally scoped
 * to workspace members only; the browser UI is treated as untrusted and all
 * membership, moderation, mute and delete decisions are repeated here.
 *
 * Security notes:
 * - No shell commands are executed in this module.
 * - Mutating routes require same-origin checks for cookie-based requests.
 * - Workspace membership is checked for every route.
 * - Full fingerprints remain internal; normal API message responses expose
 *   UI-safe booleans such as is_own/can_delete/can_mute_author instead.
 * - Attachment cards are references to workspace paths, not direct file reads.
 * - Message table growth is bounded by pruning old/deleted rows.
 * - Rate limiting is in-memory and defensive; it resets on service restart.
 */

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace pqnas {
namespace {

// Default and maximum number of messages returned by the list endpoint.
// The API clamps client-provided limits to this range.
constexpr int k_workspace_messages_default_limit = 100;
constexpr int k_workspace_messages_max_limit = 200;
// Retention policy. Active rows above the per-workspace cap are soft-deleted;
// soft-deleted rows are physically removed after the keep window.
constexpr int k_workspace_messages_active_cap_per_workspace = 5000;
constexpr sqlite3_int64 k_workspace_messages_soft_delete_keep_seconds = 30LL * 24LL * 60LL * 60LL;
constexpr std::size_t k_workspace_message_max_bytes = 4000;

// Simple per-user/per-workspace/per-IP sliding-window rate limits.
// These protect the SQLite DB and message table from accidental or malicious spam.
constexpr sqlite3_int64 k_workspace_messages_rate_window_seconds = 60;
constexpr int k_workspace_messages_post_rate_limit = 30;
constexpr int k_workspace_messages_delete_rate_limit = 60;
constexpr int k_workspace_messages_mute_rate_limit = 30;
constexpr int k_workspace_messages_read_rate_limit = 120;
constexpr std::size_t k_workspace_messages_rate_bucket_cap = 10000;

// Schema creation/migration is guarded so concurrent first requests do not try
// to run the same CREATE/ALTER statements at the same time.
std::mutex g_workspace_messages_schema_mu;
// In-memory rate-limit buckets. This is intentionally process-local; it is a
// cheap abuse guard, not a durable security ledger.
std::mutex g_workspace_messages_rate_mu;
std::unordered_map<std::string, std::deque<sqlite3_int64>> g_workspace_messages_rate;

// RAII wrapper for sqlite3*. It keeps error paths simple and prevents leaks
// when a handler returns early.
struct SqliteHandle {
    sqlite3* db = nullptr;
    ~SqliteHandle() {
        if (db) sqlite3_close(db);
    }

    SqliteHandle() = default;
    SqliteHandle(const SqliteHandle&) = delete;
    SqliteHandle& operator=(const SqliteHandle&) = delete;
};

// RAII wrapper for prepared statements.
struct StmtHandle {
    sqlite3_stmt* stmt = nullptr;
    ~StmtHandle() {
        if (stmt) sqlite3_finalize(stmt);
    }

    StmtHandle() = default;
    StmtHandle(const StmtHandle&) = delete;
    StmtHandle& operator=(const StmtHandle&) = delete;
};

// Trim ASCII whitespace from both ends. Used before comparing IDs,
// names and user-provided small text fields.
std::string trim_copy_wsmsg(const std::string& s) {
    std::size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;

    std::size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;

    return s.substr(a, b - a);
}

// Fetch one HTTP header without throwing or creating optional boilerplate.
std::string header_value_wsmsg(const httplib::Request& req, const char* key) {
    auto it = req.headers.find(key);
    return it == req.headers.end() ? std::string() : it->second;
}

// Central JSON reply helper. Prefer the server-provided responder when
// available so route behavior stays consistent with the rest of PQ-NAS.
void reply_json_wsmsg(const WorkspaceFileRouteDeps& deps,
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

// CSRF guard for mutating routes. Cookie-based browser calls must come from
// the configured origin or a same-origin referer. Bearer-token calls are
// allowed to skip this check because they are not ambient browser credentials.
bool require_same_origin_for_cookie_mutation_wsmsg(const httplib::Request& req,
                                                   httplib::Response& res,
                                                   const WorkspaceFileRouteDeps& deps) {
    if (!deps.origin || deps.origin->empty()) {
        reply_json_wsmsg(deps, res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "origin not configured"}
        });
        return false;
    }

    const std::string authz = header_value_wsmsg(req, "Authorization");
    const bool has_bearer =
        authz.size() > 7 &&
        authz.compare(0, 7, "Bearer ") == 0;

    if (has_bearer) return true;

    const std::string origin = header_value_wsmsg(req, "Origin");
    if (!origin.empty()) {
        if (origin == *deps.origin) return true;

        reply_json_wsmsg(deps, res, 403, json{
            {"ok", false},
            {"error", "forbidden"},
            {"message", "origin mismatch"}
        });
        return false;
    }

    const std::string referer = header_value_wsmsg(req, "Referer");
    if (!referer.empty()) {
        const std::string allowed_prefix = *deps.origin + "/";
        if (referer == *deps.origin || referer.rfind(allowed_prefix, 0) == 0) return true;

        reply_json_wsmsg(deps, res, 403, json{
            {"ok", false},
            {"error", "forbidden"},
            {"message", "origin mismatch"}
        });
        return false;
    }

    reply_json_wsmsg(deps, res, 403, json{
        {"ok", false},
        {"error", "forbidden"},
        {"message", "origin required"}
    });
    return false;
}

// Clock abstraction. Tests can provide deps.now_epoch_sec; production falls
// back to system_clock.
std::int64_t now_epoch_wsmsg(const WorkspaceFileRouteDeps& deps) {
    if (deps.now_epoch_sec) {
        return deps.now_epoch_sec();
    }

    return static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
}

// Store a readable UTC timestamp alongside the epoch value for UI display and
// easier DB inspection.
std::string iso_utc_from_epoch_wsmsg(std::int64_t epoch) {
    std::time_t t = static_cast<std::time_t>(epoch);
    std::tm tmv{};
    gmtime_r(&t, &tmv);

    char buf[32]{};
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmv);
    return std::string(buf);
}

// Place the SQLite DB next to the configured workspace/users state files.
// This keeps the message board state with the rest of PQ-NAS runtime config.
std::filesystem::path workspace_messages_db_path(const WorkspaceFileRouteDeps& deps) {
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

// Execute fixed SQL statements used for pragmas, schema creation and simple
// transaction control. This is sqlite3_exec, not shell execution.
bool exec_sql_wsmsg(sqlite3* db, const char* sql, std::string* err) {
    char* emsg = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &emsg);
    if (rc != SQLITE_OK) {
        if (err) {
            *err = emsg ? emsg : sqlite3_errmsg(db);
        }
        if (emsg) sqlite3_free(emsg);
        return false;
    }

    if (emsg) sqlite3_free(emsg);
    return true;
}

// Migration helper for ALTER TABLE ADD COLUMN. SQLite has no IF NOT EXISTS for
// older ALTER TABLE forms, so duplicate-column errors are treated as success.
bool exec_sql_allow_duplicate_column_wsmsg(sqlite3* db, const char* sql, std::string* err) {
    char* emsg = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &emsg);
    if (rc == SQLITE_OK) {
        if (emsg) sqlite3_free(emsg);
        return true;
    }

    const std::string msg = emsg ? emsg : sqlite3_errmsg(db);
    if (emsg) sqlite3_free(emsg);

    if (msg.find("duplicate column") != std::string::npos) {
        return true;
    }

    if (err) *err = msg;
    return false;
}

// Prepare a SQLite statement and normalize error propagation.
bool prepare_wsmsg(sqlite3* db,
                   const char* sql,
                   sqlite3_stmt** out,
                   std::string* err) {
    *out = nullptr;
    const int rc = sqlite3_prepare_v2(db, sql, -1, out, nullptr);
    if (rc != SQLITE_OK) {
        if (err) *err = sqlite3_errmsg(db);
        return false;
    }
    return true;
}

// Open the workspace message database and ensure the current schema exists.
// Schema work is cheap after first run because CREATE IF NOT EXISTS and the
// duplicate-column migration helper are idempotent.
bool open_workspace_messages_db(const WorkspaceFileRouteDeps& deps,
                                SqliteHandle* out,
                                std::string* err) {
    if (!out) return false;
    out->db = nullptr;

    const std::filesystem::path db_path = workspace_messages_db_path(deps);

    std::error_code ec;
    if (db_path.has_parent_path()) {
        std::filesystem::create_directories(db_path.parent_path(), ec);
        if (ec) {
            if (err) *err = "failed to create db directory: " + ec.message();
            return false;
        }
    }

    sqlite3* db = nullptr;
    int rc = sqlite3_open(db_path.string().c_str(), &db);
    if (rc != SQLITE_OK) {
        if (err) *err = db ? sqlite3_errmsg(db) : "sqlite3_open failed";
        if (db) sqlite3_close(db);
        return false;
    }

    out->db = db;
    sqlite3_busy_timeout(out->db, 3000);

    {
        std::lock_guard<std::mutex> lock(g_workspace_messages_schema_mu);

        std::string sqlerr;
        if (!exec_sql_wsmsg(out->db, "PRAGMA busy_timeout=3000;", &sqlerr)) {
            if (err) *err = sqlerr;
            return false;
        }

        if (!exec_sql_wsmsg(out->db, "PRAGMA journal_mode=WAL;", &sqlerr)) {
            if (err) *err = sqlerr;
            return false;
        }

        if (!exec_sql_wsmsg(out->db,
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

        if (!exec_sql_allow_duplicate_column_wsmsg(out->db,
                "ALTER TABLE workspace_messages ADD COLUMN attachments_json TEXT NOT NULL DEFAULT '[]';",
                &sqlerr)) {
            if (err) *err = sqlerr;
            return false;
        }

        if (!exec_sql_allow_duplicate_column_wsmsg(out->db,
                "ALTER TABLE workspace_messages ADD COLUMN deleted_at_epoch INTEGER NOT NULL DEFAULT 0;",
                &sqlerr)) {
            if (err) *err = sqlerr;
            return false;
        }

        if (!exec_sql_allow_duplicate_column_wsmsg(out->db,
                "ALTER TABLE workspace_messages ADD COLUMN deleted_by_fp TEXT NOT NULL DEFAULT '';",
                &sqlerr)) {
            if (err) *err = sqlerr;
            return false;
        }

        if (!exec_sql_wsmsg(out->db,
                "CREATE INDEX IF NOT EXISTS idx_workspace_messages_ws_id "
                "ON workspace_messages(workspace_id, id);",
                &sqlerr)) {
            if (err) *err = sqlerr;
            return false;
        }

        if (!exec_sql_wsmsg(out->db,
                "CREATE INDEX IF NOT EXISTS idx_workspace_messages_active_ws_id "
                "ON workspace_messages(workspace_id, deleted_at_epoch, id);",
                &sqlerr)) {
            if (err) *err = sqlerr;
            return false;
        }

        if (!exec_sql_wsmsg(out->db,
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

        if (!exec_sql_wsmsg(out->db,
                "CREATE TABLE IF NOT EXISTS workspace_message_mutes ("
                "workspace_id TEXT NOT NULL,"
                "target_fp TEXT NOT NULL,"
                "muted_by_fp TEXT NOT NULL,"
                "muted_at_epoch INTEGER NOT NULL,"
                "reason TEXT NOT NULL DEFAULT '',"
                "PRIMARY KEY(workspace_id, target_fp)"
                ");",
                &sqlerr)) {
            if (err) *err = sqlerr;
            return false;
        }
    }

    return true;
}

// Safely read a nullable SQLite TEXT column as std::string.
std::string sqlite_text_col(sqlite3_stmt* st, int col) {
    const unsigned char* t = sqlite3_column_text(st, col);
    return t ? reinterpret_cast<const char*>(t) : std::string();
}

// Last-resort UI display for unknown users. Avoid showing full fingerprints in
// normal message responses.
std::string short_fp_wsmsg(const std::string& fp) {
    const std::string v = trim_copy_wsmsg(fp);
    if (v.empty()) return "Member";
    if (v.size() <= 18) return v;
    return v.substr(0, 18) + "...";
}

// Extract the best human-readable name from a user JSON object. Different
// provisioning paths may use slightly different field names.
std::string preferred_name_from_user_json_wsmsg(const json& j) {
    static const char* keys[] = {
        "nickname",
        "display_name",
        "full_name",
        "real_name",
        "name",
        "username",
        "user_name",
        "login_name"
    };

    for (const char* key : keys) {
        auto it = j.find(key);
        if (it != j.end() && it->is_string()) {
            const std::string v = trim_copy_wsmsg(it->get<std::string>());
            if (!v.empty()) return v;
        }
    }

    return {};
}

// Check whether a user JSON object represents the given fingerprint.
bool user_json_matches_fp_wsmsg(const json& j, const std::string& fp) {
    static const char* fp_keys[] = {
        "fingerprint",
        "fingerprint_b64",
        "owner_fingerprint",
        "user_fingerprint"
    };

    for (const char* key : fp_keys) {
        auto it = j.find(key);
        if (it != j.end() && it->is_string()) {
            if (trim_copy_wsmsg(it->get<std::string>()) == fp) return true;
        }
    }

    return false;
}

// Best-effort lookup from users.json so old messages can still display a
// friendly name even if the workspace member record has no display_name.
std::string lookup_user_preferred_name_wsmsg(const std::string& users_path,
                                             const std::string& fp) {
    if (users_path.empty() || fp.empty()) return {};

    std::ifstream f(users_path);
    if (!f.good()) return {};

    json root = json::parse(f, nullptr, false);
    if (root.is_discarded()) return {};

    auto try_array = [&](const json& arr) -> std::string {
        if (!arr.is_array()) return {};
        for (const auto& item : arr) {
            if (!item.is_object()) continue;
            if (!user_json_matches_fp_wsmsg(item, fp)) continue;

            const std::string name = preferred_name_from_user_json_wsmsg(item);
            if (!name.empty()) return name;
        }
        return {};
    };

    if (root.is_object()) {
        auto it_users = root.find("users");
        if (it_users != root.end()) {
            const std::string v = try_array(*it_users);
            if (!v.empty()) return v;
        }

        auto it_items = root.find("items");
        if (it_items != root.end()) {
            const std::string v = try_array(*it_items);
            if (!v.empty()) return v;
        }
    }

    if (root.is_array()) {
        const std::string v = try_array(root);
        if (!v.empty()) return v;
    }

    return {};
}

// Resolve the author display name in privacy-friendly priority order:
// workspace member name -> users.json name -> stored DB name -> shortened fp.
std::string resolve_message_author_name_wsmsg(const WorkspaceFileRouteDeps& deps,
                                              const WorkspaceRec& workspace,
                                              const std::string& author_fp,
                                              const std::string& author_name_db) {
    const std::string fp = trim_copy_wsmsg(author_fp);

    for (const auto& m : workspace.members) {
        if (m.fingerprint != fp) continue;

        const std::string member_name = trim_copy_wsmsg(m.display_name);
        if (!member_name.empty()) return member_name;
        break;
    }

    const std::string registry_name = lookup_user_preferred_name_wsmsg(deps.users_path, fp);
    if (!registry_name.empty()) return registry_name;

    const std::string from_db = trim_copy_wsmsg(author_name_db);
    if (!from_db.empty() && from_db != fp) return from_db;

    return short_fp_wsmsg(fp);
}

// Normalize an attachment reference path. The message board stores only
// workspace-relative references; it rejects absolute paths, traversal,
// backslashes, empty segments and NUL characters.
bool normalize_attachment_path_wsmsg(const std::string& in,
                                      std::string* out,
                                      std::string* err) {
    std::string p = trim_copy_wsmsg(in);
    while (!p.empty() && p.front() == '/') p.erase(p.begin());

    if (p.empty()) {
        if (err) *err = "empty attachment path";
        return false;
    }

    if (p.size() > 4096) {
        if (err) *err = "attachment path too long";
        return false;
    }

    if (p.find('\\') != std::string::npos || p.find('\0') != std::string::npos) {
        if (err) *err = "invalid attachment path character";
        return false;
    }

    std::vector<std::string> parts;
    std::stringstream ss(p);
    std::string part;
    while (std::getline(ss, part, '/')) {
        part = trim_copy_wsmsg(part);
        if (part.empty() || part == "." || part == "..") {
            if (err) *err = "invalid attachment path segment";
            return false;
        }
        parts.push_back(part);
    }

    if (parts.empty()) {
        if (err) *err = "empty attachment path";
        return false;
    }

    std::string norm;
    for (const auto& x : parts) {
        if (!norm.empty()) norm += "/";
        norm += x;
    }

    if (out) *out = norm;
    return true;
}

// Derive a display name from a normalized path when the client did not provide
// one.
std::string leaf_name_from_path_wsmsg(const std::string& path) {
    const auto slash = path.find_last_of('/');
    if (slash == std::string::npos) return path;
    return path.substr(slash + 1);
}

// Validate and normalize attachment references before storing them. These are
// metadata references only; no file is opened, served or trusted here.
json sanitize_message_attachments_wsmsg(const json& body,
                                        const std::string& workspace_id,
                                        std::string* err) {
    json out = json::array();

    auto it = body.find("attachments");
    if (it == body.end() || it->is_null()) return out;

    if (!it->is_array()) {
        if (err) *err = "attachments must be an array";
        return json();
    }

    if (it->size() > 8) {
        if (err) *err = "too many attachments";
        return json();
    }

    for (const auto& raw : *it) {
        if (!raw.is_object()) {
            if (err) *err = "attachment must be an object";
            return json();
        }

        std::string path_norm;
        std::string perr;
        if (!normalize_attachment_path_wsmsg(raw.value("path", ""), &path_norm, &perr)) {
            if (err) *err = perr;
            return json();
        }

        std::string kind = trim_copy_wsmsg(raw.value("kind", "file"));
        if (kind != "file" && kind != "dir") kind = "file";

        std::string name = trim_copy_wsmsg(raw.value("name", ""));
        if (name.empty()) name = leaf_name_from_path_wsmsg(path_norm);
        if (name.size() > 255) name = name.substr(0, 255);

        json a = json{
            {"type", "workspace_file"},
            {"workspace_id", workspace_id},
            {"path", path_norm},
            {"name", name},
            {"kind", kind}
        };

        if (raw.contains("size_bytes") && raw["size_bytes"].is_number_unsigned()) {
            a["size_bytes"] = raw["size_bytes"].get<std::uint64_t>();
        } else if (raw.contains("size_bytes") && raw["size_bytes"].is_number_integer()) {
            const auto n = raw["size_bytes"].get<long long>();
            if (n >= 0) a["size_bytes"] = static_cast<std::uint64_t>(n);
        }

        out.push_back(std::move(a));
    }

    return out;
}

// Parse stored attachment JSON defensively. Corrupt values are treated as an
// empty attachment list instead of breaking message rendering.
json parse_attachments_json_wsmsg(const std::string& raw) {
    if (raw.empty()) return json::array();

    json j = json::parse(raw, nullptr, false);
    if (j.is_discarded() || !j.is_array()) return json::array();
    return j;
}

// Bind text values using SQLITE_TRANSIENT so SQLite owns its own copy.
void bind_text_wsmsg(sqlite3_stmt* st, int idx, const std::string& value) {
    sqlite3_bind_text(st, idx, value.c_str(), -1, SQLITE_TRANSIENT);
}

// Read and clamp an integer query parameter.
int int_param_wsmsg(const httplib::Request& req,
                    const char* name,
                    int def,
                    int lo,
                    int hi) {
    if (!req.has_param(name)) return def;

    try {
        int v = std::stoi(req.get_param_value(name));
        if (v < lo) v = lo;
        if (v > hi) v = hi;
        return v;
    } catch (...) {
        return def;
    }
}

// Read an int64 query parameter with a safe fallback.
sqlite3_int64 int64_param_wsmsg(const httplib::Request& req,
                                const char* name,
                                sqlite3_int64 def) {
    if (!req.has_param(name)) return def;

    try {
        return static_cast<sqlite3_int64>(std::stoll(req.get_param_value(name)));
    } catch (...) {
        return def;
    }
}

// Authenticated workspace actor resolved for a request. This bundles the
// session fingerprint, workspace role, display name and current workspace record.
struct WorkspaceMessageActor {
    std::string fp;
    std::string role;
    std::string display_name;
    WorkspaceRec workspace;
    WorkspaceMemberRec member;
};

// Rate-limit key. Include action, remote address, workspace and actor so one
// noisy user/workspace does not consume the whole process budget.
std::string workspace_message_rate_key_wsmsg(const httplib::Request& req,
                                             const WorkspaceMessageActor& actor,
                                             const char* action) {
    const std::string remote_addr = req.remote_addr.empty() ? "unknown" : req.remote_addr;

    std::ostringstream oss;
    oss << (action ? action : "unknown")
        << '|'
        << remote_addr
        << '|'
        << actor.workspace.workspace_id
        << '|'
        << actor.fp;

    return oss.str();
}

// Remove expired rate-limit buckets while the rate-limit mutex is held.
void compact_workspace_message_rate_map_locked_wsmsg(sqlite3_int64 now_epoch) {
    const sqlite3_int64 min_epoch = now_epoch - k_workspace_messages_rate_window_seconds;

    for (auto it = g_workspace_messages_rate.begin(); it != g_workspace_messages_rate.end(); ) {
        auto& hits = it->second;
        while (!hits.empty() && hits.front() <= min_epoch) {
            hits.pop_front();
        }

        if (hits.empty()) {
            it = g_workspace_messages_rate.erase(it);
        } else {
            ++it;
        }
    }
}

// Sliding-window limiter. Returns true when the caller should be rejected with
// HTTP 429 and reports the retry-after duration.
bool workspace_message_rate_limited_wsmsg(const httplib::Request& req,
                                          const WorkspaceMessageActor& actor,
                                          const char* action,
                                          int max_hits,
                                          sqlite3_int64 now_epoch,
                                          sqlite3_int64* retry_after_seconds) {
    if (retry_after_seconds) *retry_after_seconds = k_workspace_messages_rate_window_seconds;
    if (max_hits <= 0) return false;

    const std::string key = workspace_message_rate_key_wsmsg(req, actor, action);

    std::lock_guard<std::mutex> lock(g_workspace_messages_rate_mu);

    if (g_workspace_messages_rate.size() >= k_workspace_messages_rate_bucket_cap) {
        compact_workspace_message_rate_map_locked_wsmsg(now_epoch);
    }

    auto found = g_workspace_messages_rate.find(key);
    if (g_workspace_messages_rate.size() >= k_workspace_messages_rate_bucket_cap &&
        found == g_workspace_messages_rate.end()) {
        if (retry_after_seconds) *retry_after_seconds = k_workspace_messages_rate_window_seconds;
        return true;
    }

    // Create the bucket only after the global cap check above.
    auto& hits = g_workspace_messages_rate[key];

    const sqlite3_int64 min_epoch = now_epoch - k_workspace_messages_rate_window_seconds;
    while (!hits.empty() && hits.front() <= min_epoch) {
        hits.pop_front();
    }

    if (hits.size() >= static_cast<std::size_t>(max_hits)) {
        sqlite3_int64 retry = k_workspace_messages_rate_window_seconds;
        if (!hits.empty()) {
            retry = k_workspace_messages_rate_window_seconds - (now_epoch - hits.front());
            if (retry < 1) retry = 1;
        }

        if (retry_after_seconds) *retry_after_seconds = retry;
        return true;
    }

    hits.push_back(now_epoch);
    return false;
}

// Route-facing rate-limit wrapper that emits the standard JSON 429 response.
bool require_workspace_message_rate_limit_wsmsg(const WorkspaceFileRouteDeps& deps,
                                                const httplib::Request& req,
                                                httplib::Response& res,
                                                const WorkspaceMessageActor& actor,
                                                const char* action,
                                                int max_hits,
                                                sqlite3_int64 now_epoch) {
    sqlite3_int64 retry_after = 0;

    if (!workspace_message_rate_limited_wsmsg(
            req,
            actor,
            action,
            max_hits,
            now_epoch,
            &retry_after)) {
        return true;
    }

    res.set_header("Retry-After", std::to_string(retry_after));

    reply_json_wsmsg(deps, res, 429, json{
        {"ok", false},
        {"error", "rate_limited"},
        {"message", "too many requests, try again later"}
    });

    return false;
}

// Bounded-growth guard. Old soft-deleted rows are permanently deleted, and
// active rows beyond the per-workspace cap are soft-deleted by system-prune.
bool prune_workspace_messages_wsmsg(sqlite3* db,
                                    const std::string& workspace_id,
                                    sqlite3_int64 now_epoch,
                                    std::string* err) {
    if (!db || workspace_id.empty()) return true;

    {
        StmtHandle st;
        if (!prepare_wsmsg(db,
                "DELETE FROM workspace_messages "
                "WHERE workspace_id=? "
                "AND deleted_at_epoch>0 "
                "AND deleted_at_epoch<?;",
                &st.stmt,
                err)) {
            return false;
        }

        bind_text_wsmsg(st.stmt, 1, workspace_id);
        sqlite3_bind_int64(st.stmt, 2, now_epoch - k_workspace_messages_soft_delete_keep_seconds);

        const int rc = sqlite3_step(st.stmt);
        if (rc != SQLITE_DONE) {
            if (err) *err = sqlite3_errmsg(db);
            return false;
        }
    }

    {
        StmtHandle st;
        if (!prepare_wsmsg(db,
                "UPDATE workspace_messages "
                "SET deleted_at_epoch=?, deleted_by_fp='system-prune' "
                "WHERE workspace_id=? "
                "AND deleted_at_epoch=0 "
                "AND id IN ("
                "  SELECT id FROM ("
                "    SELECT id FROM workspace_messages "
                "    WHERE workspace_id=? "
                "    AND deleted_at_epoch=0 "
                "    ORDER BY id DESC "
                "    LIMIT -1 OFFSET ?"
                "  )"
                ");",
                &st.stmt,
                err)) {
            return false;
        }

        sqlite3_bind_int64(st.stmt, 1, now_epoch);
        bind_text_wsmsg(st.stmt, 2, workspace_id);
        bind_text_wsmsg(st.stmt, 3, workspace_id);
        sqlite3_bind_int64(st.stmt, 4, k_workspace_messages_active_cap_per_workspace);

        const int rc = sqlite3_step(st.stmt);
        if (rc != SQLITE_DONE) {
            if (err) *err = sqlite3_errmsg(db);
            return false;
        }
    }

    return true;
}

// Authenticate the caller and verify that they are an enabled member of an
// enabled workspace. UI visibility is not trusted as an authorization boundary.
bool require_workspace_message_actor(const WorkspaceFileRouteDeps& deps,
                                     const httplib::Request& req,
                                     httplib::Response& res,
                                     const std::string& workspace_id_raw,
                                     WorkspaceMessageActor* out) {
    if (!deps.users || !deps.workspaces || !deps.cookie_key ||
        !deps.require_user_auth_users_actor) {
        reply_json_wsmsg(deps, res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "workspace message route dependencies missing"}
        });
        return false;
    }

    const std::string workspace_id = trim_copy_wsmsg(workspace_id_raw);
    if (workspace_id.empty() || !is_valid_workspace_id(workspace_id)) {
        reply_json_wsmsg(deps, res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid workspace_id"}
        });
        return false;
    }

    std::string actor_fp;
    std::string actor_role;
    if (!deps.require_user_auth_users_actor(
            req, res, deps.cookie_key, deps.users, &actor_fp, &actor_role)) {
        return false;
    }
    (void)actor_role;

    if (!deps.workspaces->load(deps.workspaces_path)) {
        reply_json_wsmsg(deps, res, 500, json{
            {"ok", false},
            {"error", "workspaces_reload_failed"},
            {"message", "failed to reload workspaces"}
        });
        return false;
    }

    auto wopt = deps.workspaces->get(workspace_id);
    if (!wopt.has_value()) {
        reply_json_wsmsg(deps, res, 404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "workspace not found"}
        });
        return false;
    }

    WorkspaceRec w = *wopt;
    if (w.status != "enabled") {
        reply_json_wsmsg(deps, res, 403, json{
            {"ok", false},
            {"error", "forbidden"},
            {"message", "workspace disabled"}
        });
        return false;
    }

    WorkspaceMemberRec found;
    bool ok = false;
    for (const auto& m : w.members) {
        // Only enabled workspace members may use the message board.
        if (m.fingerprint == actor_fp && m.status == "enabled") {
            found = m;
            ok = true;
            break;
        }
    }

    if (!ok) {
        reply_json_wsmsg(deps, res, 403, json{
            {"ok", false},
            {"error", "forbidden"},
            {"message", "workspace access denied"}
        });
        return false;
    }

    if (out) {
        out->fp = actor_fp;
        out->role = found.role;
        out->display_name = resolve_message_author_name_wsmsg(
            deps,
            w,
            found.fingerprint,
            found.display_name
        );
        out->workspace = std::move(w);
        out->member = std::move(found);
    }

    return true;
}

// Latest non-deleted message id for unread/read-state calculations.
sqlite3_int64 latest_message_id_wsmsg(sqlite3* db,
                                      const std::string& workspace_id,
                                      std::string* err) {
    StmtHandle st;
    if (!prepare_wsmsg(db,
            "SELECT COALESCE(MAX(id), 0) FROM workspace_messages WHERE workspace_id=? AND deleted_at_epoch=0;",
            &st.stmt,
            err)) {
        return 0;
    }

    bind_text_wsmsg(st.stmt, 1, workspace_id);

    const int rc = sqlite3_step(st.stmt);
    if (rc == SQLITE_ROW) {
        return sqlite3_column_int64(st.stmt, 0);
    }

    if (rc != SQLITE_DONE && err) *err = sqlite3_errmsg(db);
    return 0;
}

// Last message id that this actor has marked as seen in this workspace.
sqlite3_int64 last_seen_id_wsmsg(sqlite3* db,
                                 const std::string& workspace_id,
                                 const std::string& actor_fp,
                                 std::string* err) {
    StmtHandle st;
    if (!prepare_wsmsg(db,
            "SELECT last_seen_id FROM workspace_message_reads "
            "WHERE workspace_id=? AND reader_fp=?;",
            &st.stmt,
            err)) {
        return 0;
    }

    bind_text_wsmsg(st.stmt, 1, workspace_id);
    bind_text_wsmsg(st.stmt, 2, actor_fp);

    const int rc = sqlite3_step(st.stmt);
    if (rc == SQLITE_ROW) {
        return sqlite3_column_int64(st.stmt, 0);
    }

    if (rc != SQLITE_DONE && err) *err = sqlite3_errmsg(db);
    return 0;
}

// Count active messages newer than last_seen_id.
sqlite3_int64 unread_count_wsmsg(sqlite3* db,
                                 const std::string& workspace_id,
                                 sqlite3_int64 last_seen_id,
                                 std::string* err) {
    StmtHandle st;
    if (!prepare_wsmsg(db,
            "SELECT COUNT(*) FROM workspace_messages WHERE workspace_id=? AND id>? AND deleted_at_epoch=0;",
            &st.stmt,
            err)) {
        return 0;
    }

    bind_text_wsmsg(st.stmt, 1, workspace_id);
    sqlite3_bind_int64(st.stmt, 2, last_seen_id);

    const int rc = sqlite3_step(st.stmt);
    if (rc == SQLITE_ROW) {
        return sqlite3_column_int64(st.stmt, 0);
    }

    if (rc != SQLITE_DONE && err) *err = sqlite3_errmsg(db);
    return 0;
}

// Convert a DB row to the public API message shape. Full author_fp is used only
// internally to resolve a friendly name and is not returned to the browser.
json message_row_to_json(const WorkspaceFileRouteDeps& deps,
                         const WorkspaceRec& workspace,
                         sqlite3_stmt* st) {
    const std::string author_fp = sqlite_text_col(st, 2);
    const std::string author_name_db = sqlite_text_col(st, 3);
    const json attachments = parse_attachments_json_wsmsg(sqlite_text_col(st, 5));

    return json{
        {"id", sqlite3_column_int64(st, 0)},
        {"workspace_id", sqlite_text_col(st, 1)},
        {"author_name", resolve_message_author_name_wsmsg(deps, workspace, author_fp, author_name_db)},
        {"body", sqlite_text_col(st, 4)},
        {"attachments", attachments},
        {"created_at_epoch", sqlite3_column_int64(st, 6)},
        {"created_at", sqlite_text_col(st, 7)}
    };
}

// Upsert read-state. The MAX() keeps read state monotonic so older client calls
// cannot move last_seen_id backwards.
bool mark_read_wsmsg(sqlite3* db,
                     const std::string& workspace_id,
                     const std::string& actor_fp,
                     sqlite3_int64 last_seen_id,
                     std::int64_t now_epoch,
                     std::string* err) {
    StmtHandle st;
    if (!prepare_wsmsg(db,
            "INSERT INTO workspace_message_reads("
            "workspace_id, reader_fp, last_seen_id, last_seen_at_epoch"
            ") VALUES(?,?,?,?) "
            "ON CONFLICT(workspace_id, reader_fp) DO UPDATE SET "
            "last_seen_id=MAX(workspace_message_reads.last_seen_id, excluded.last_seen_id), "
            "last_seen_at_epoch=excluded.last_seen_at_epoch;",
            &st.stmt,
            err)) {
        return false;
    }

    bind_text_wsmsg(st.stmt, 1, workspace_id);
    bind_text_wsmsg(st.stmt, 2, actor_fp);
    sqlite3_bind_int64(st.stmt, 3, last_seen_id);
    sqlite3_bind_int64(st.stmt, 4, now_epoch);

    const int rc = sqlite3_step(st.stmt);
    if (rc != SQLITE_DONE) {
        if (err) *err = sqlite3_errmsg(db);
        return false;
    }

    return true;
}

// Owner check used for moderation actions.
bool is_workspace_owner_wsmsg(const WorkspaceMessageActor& actor) {
    return actor.role == "owner";
}

// Verify that a target fingerprint is still an enabled member of the workspace.
bool workspace_has_enabled_member_fp_wsmsg(const WorkspaceRec& workspace,
                                           const std::string& fp) {
    if (fp.empty()) return false;
    for (const auto& m : workspace.members) {
        if (m.fingerprint == fp && m.status == "enabled") return true;
    }
    return false;
}

// Used to prevent muting owners.
bool workspace_member_is_owner_wsmsg(const WorkspaceRec& workspace,
                                     const std::string& fp) {
    if (fp.empty()) return false;
    for (const auto& m : workspace.members) {
        if (m.fingerprint == fp && m.status == "enabled" && m.role == "owner") return true;
    }
    return false;
}

// Check whether one exact target is muted. "*" is stored as the workspace-wide
// non-owner mute marker and is checked explicitly by callers.
bool message_target_is_muted_wsmsg(sqlite3* db,
                                    const std::string& workspace_id,
                                    const std::string& target_fp,
                                    std::string* err) {
    if (workspace_id.empty() || target_fp.empty()) return false;

    StmtHandle st;
    if (!prepare_wsmsg(db,
            "SELECT 1 FROM workspace_message_mutes "
            "WHERE workspace_id=? AND target_fp=? "
            "LIMIT 1;",
            &st.stmt,
            err)) {
        return false;
    }

    bind_text_wsmsg(st.stmt, 1, workspace_id);
    bind_text_wsmsg(st.stmt, 2, target_fp);

    const int rc = sqlite3_step(st.stmt);
    if (rc == SQLITE_ROW) return true;
    if (rc != SQLITE_DONE && err) *err = sqlite3_errmsg(db);
    return false;
}

// Check whether the current actor is muted. Owners are never muted, even when
// the workspace-wide "*" mute exists.
bool message_actor_is_muted_wsmsg(sqlite3* db,
                                  const WorkspaceMessageActor& actor,
                                  std::string* err) {
    if (is_workspace_owner_wsmsg(actor)) return false;

    StmtHandle st;
    if (!prepare_wsmsg(db,
            "SELECT 1 FROM workspace_message_mutes "
            "WHERE workspace_id=? AND (target_fp='*' OR target_fp=?) "
            "LIMIT 1;",
            &st.stmt,
            err)) {
        return false;
    }

    bind_text_wsmsg(st.stmt, 1, actor.workspace.workspace_id);
    bind_text_wsmsg(st.stmt, 2, actor.fp);

    const int rc = sqlite3_step(st.stmt);
    if (rc == SQLITE_ROW) return true;
    if (rc != SQLITE_DONE && err) *err = sqlite3_errmsg(db);
    return false;
}

// Create or update a mute record. target_fp may be "*" for the workspace-wide
// "mute all non-owners" state.
bool upsert_message_mute_wsmsg(sqlite3* db,
                               const std::string& workspace_id,
                               const std::string& target_fp,
                               const std::string& muted_by_fp,
                               std::int64_t now_epoch,
                               const std::string& reason,
                               std::string* err) {
    StmtHandle st;
    if (!prepare_wsmsg(db,
            "INSERT INTO workspace_message_mutes("
            "workspace_id, target_fp, muted_by_fp, muted_at_epoch, reason"
            ") VALUES(?,?,?,?,?) "
            "ON CONFLICT(workspace_id, target_fp) DO UPDATE SET "
            "muted_by_fp=excluded.muted_by_fp, "
            "muted_at_epoch=excluded.muted_at_epoch, "
            "reason=excluded.reason;",
            &st.stmt,
            err)) {
        return false;
    }

    bind_text_wsmsg(st.stmt, 1, workspace_id);
    bind_text_wsmsg(st.stmt, 2, target_fp);
    bind_text_wsmsg(st.stmt, 3, muted_by_fp);
    sqlite3_bind_int64(st.stmt, 4, now_epoch);
    bind_text_wsmsg(st.stmt, 5, reason);

    const int rc = sqlite3_step(st.stmt);
    if (rc != SQLITE_DONE) {
        if (err) *err = sqlite3_errmsg(db);
        return false;
    }

    return true;
}

// Remove a mute record.
bool delete_message_mute_wsmsg(sqlite3* db,
                               const std::string& workspace_id,
                               const std::string& target_fp,
                               std::string* err) {
    StmtHandle st;
    if (!prepare_wsmsg(db,
            "DELETE FROM workspace_message_mutes WHERE workspace_id=? AND target_fp=?;",
            &st.stmt,
            err)) {
        return false;
    }

    bind_text_wsmsg(st.stmt, 1, workspace_id);
    bind_text_wsmsg(st.stmt, 2, target_fp);

    const int rc = sqlite3_step(st.stmt);
    if (rc != SQLITE_DONE) {
        if (err) *err = sqlite3_errmsg(db);
        return false;
    }

    return true;
}

// Resolve the author fingerprint for an existing non-deleted message. The
// frontend sends message_id for muting/deleting; the server resolves target fp.
sqlite3_int64 workspace_message_mute_count_wsmsg(sqlite3* db,
                                                  const std::string& workspace_id,
                                                  std::string* err) {
    StmtHandle st;
    if (!prepare_wsmsg(db,
            "SELECT COUNT(*) FROM workspace_message_mutes WHERE workspace_id=?;",
            &st.stmt,
            err)) {
        return 0;
    }

    bind_text_wsmsg(st.stmt, 1, workspace_id);

    const int rc = sqlite3_step(st.stmt);
    if (rc == SQLITE_ROW) {
        return sqlite3_column_int64(st.stmt, 0);
    }

    if (rc != SQLITE_DONE && err) *err = sqlite3_errmsg(db);
    return 0;
}

bool delete_all_workspace_message_mutes_wsmsg(sqlite3* db,
                                              const std::string& workspace_id,
                                              std::string* err) {
    StmtHandle st;
    if (!prepare_wsmsg(db,
            "DELETE FROM workspace_message_mutes WHERE workspace_id=?;",
            &st.stmt,
            err)) {
        return false;
    }

    bind_text_wsmsg(st.stmt, 1, workspace_id);

    const int rc = sqlite3_step(st.stmt);
    if (rc != SQLITE_DONE) {
        if (err) *err = sqlite3_errmsg(db);
        return false;
    }

    return true;
}

bool get_message_author_wsmsg(sqlite3* db,
                              const std::string& workspace_id,
                              sqlite3_int64 message_id,
                              std::string* author_fp,
                              std::string* err) {
    if (author_fp) author_fp->clear();

    StmtHandle st;
    if (!prepare_wsmsg(db,
            "SELECT author_fp FROM workspace_messages "
            "WHERE workspace_id=? AND id=? AND deleted_at_epoch=0;",
            &st.stmt,
            err)) {
        return false;
    }

    bind_text_wsmsg(st.stmt, 1, workspace_id);
    sqlite3_bind_int64(st.stmt, 2, message_id);

    const int rc = sqlite3_step(st.stmt);
    if (rc == SQLITE_ROW) {
        if (author_fp) *author_fp = sqlite_text_col(st.stmt, 0);
        return true;
    }

    if (rc != SQLITE_DONE && err) *err = sqlite3_errmsg(db);
    return false;
}

// Soft-delete a message instead of removing it immediately. This keeps audit
// and read-state behavior stable; pruning removes old deleted rows later.
bool soft_delete_message_wsmsg(sqlite3* db,
                               const std::string& workspace_id,
                               sqlite3_int64 message_id,
                               const std::string& actor_fp,
                               std::int64_t now_epoch,
                               std::string* err) {
    StmtHandle st;
    if (!prepare_wsmsg(db,
            "UPDATE workspace_messages "
            "SET deleted_at_epoch=?, deleted_by_fp=? "
            "WHERE workspace_id=? AND id=? AND deleted_at_epoch=0;",
            &st.stmt,
            err)) {
        return false;
    }

    sqlite3_bind_int64(st.stmt, 1, now_epoch);
    bind_text_wsmsg(st.stmt, 2, actor_fp);
    bind_text_wsmsg(st.stmt, 3, workspace_id);
    sqlite3_bind_int64(st.stmt, 4, message_id);

    const int rc = sqlite3_step(st.stmt);
    if (rc != SQLITE_DONE) {
        if (err) *err = sqlite3_errmsg(db);
        return false;
    }

    return sqlite3_changes(db) > 0;
}

// Best-effort security audit event. Failures here must not break user actions.
void audit_workspace_message_best_effort(const WorkspaceFileRouteDeps& deps,
                                         const httplib::Request& req,
                                         const std::string& event,
                                         const std::string& outcome,
                                         const WorkspaceMessageActor& actor,
                                         sqlite3_int64 message_id) {
    if (!deps.audit_emit) return;

    std::map<std::string, std::string> f;
    f["actor_fp"] = actor.fp;
    f["workspace_id"] = actor.workspace.workspace_id;
    f["role"] = actor.role;
    if (message_id > 0) f["message_id"] = std::to_string(static_cast<long long>(message_id));
    f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

    auto it_cf = req.headers.find("CF-Connecting-IP");
    if (it_cf != req.headers.end()) f["cf_ip"] = it_cf->second;

    auto it_xff = req.headers.find("X-Forwarded-For");
    if (it_xff != req.headers.end()) f["xff"] = it_xff->second;

    deps.audit_emit(event, outcome, f);
}

} // namespace

// Register all workspace message HTTP routes.
void register_workspace_message_routes(httplib::Server& srv,
                                       const WorkspaceFileRouteDeps& deps) {
    // List messages and moderation state for the current workspace.
    // This route returns UI-safe authorization booleans per message instead of
    // exposing full fingerprints to the browser.
    srv.Get("/api/v4/workspaces/messages",
            [&deps](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Cache-Control", "no-store");

        const std::string workspace_id =
            req.has_param("workspace_id") ? req.get_param_value("workspace_id") : "";

        WorkspaceMessageActor actor;
        if (!require_workspace_message_actor(deps, req, res, workspace_id, &actor)) return;

        SqliteHandle db;
        std::string dberr;
        if (!open_workspace_messages_db(deps, &db, &dberr)) {
            reply_json_wsmsg(deps, res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to open workspace messages db"},
                {"detail", dberr}
            });
            return;
        }

        std::string moderr;
        const bool can_moderate = is_workspace_owner_wsmsg(actor);
        const bool actor_muted = message_actor_is_muted_wsmsg(db.db, actor, &moderr);
        if (!moderr.empty()) {
            reply_json_wsmsg(deps, res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to read workspace message mute state"},
                {"detail", moderr}
            });
            return;
        }

        const bool message_board_muted_all = message_target_is_muted_wsmsg(
            db.db,
            actor.workspace.workspace_id,
            "*",
            &moderr
        );
        if (!moderr.empty()) {
            reply_json_wsmsg(deps, res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to read workspace message board mute state"},
                {"detail", moderr}
            });
            return;
        }

        const sqlite3_int64 mute_count = workspace_message_mute_count_wsmsg(
            db.db,
            actor.workspace.workspace_id,
            &moderr
        );
        if (!moderr.empty()) {
            reply_json_wsmsg(deps, res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to read workspace message mute count"},
                {"detail", moderr}
            });
            return;
        }

        const int limit = int_param_wsmsg(
            req,
            "limit",
            k_workspace_messages_default_limit,
            1,
            k_workspace_messages_max_limit
        );
        const sqlite3_int64 after_id = int64_param_wsmsg(req, "after_id", 0);

        json messages = json::array();
        StmtHandle st;

        if (after_id > 0) {
            if (!prepare_wsmsg(db.db,
                    "SELECT id, workspace_id, author_fp, author_name, body, attachments_json, created_at_epoch, created_at "
                    "FROM workspace_messages "
                    "WHERE workspace_id=? AND id>? AND deleted_at_epoch=0 "
                    "ORDER BY id ASC "
                    "LIMIT ?;",
                    &st.stmt,
                    &dberr)) {
                reply_json_wsmsg(deps, res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "failed to prepare messages query"},
                    {"detail", dberr}
                });
                return;
            }

            bind_text_wsmsg(st.stmt, 1, actor.workspace.workspace_id);
            sqlite3_bind_int64(st.stmt, 2, after_id);
            sqlite3_bind_int(st.stmt, 3, limit);
        } else {
            if (!prepare_wsmsg(db.db,
                    "SELECT id, workspace_id, author_fp, author_name, body, attachments_json, created_at_epoch, created_at "
                    "FROM ("
                    "  SELECT id, workspace_id, author_fp, author_name, body, attachments_json, created_at_epoch, created_at "
                    "  FROM workspace_messages "
                    "  WHERE workspace_id=? AND deleted_at_epoch=0 "
                    "  ORDER BY id DESC "
                    "  LIMIT ?"
                    ") ORDER BY id ASC;",
                    &st.stmt,
                    &dberr)) {
                reply_json_wsmsg(deps, res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "failed to prepare messages query"},
                    {"detail", dberr}
                });
                return;
            }

            bind_text_wsmsg(st.stmt, 1, actor.workspace.workspace_id);
            sqlite3_bind_int(st.stmt, 2, limit);
        }

        while (true) {
            const int rc = sqlite3_step(st.stmt);
            if (rc == SQLITE_ROW) {
                const std::string row_author_fp = sqlite_text_col(st.stmt, 2);
                json msg = message_row_to_json(deps, actor.workspace, st.stmt);

                const bool is_own = row_author_fp == actor.fp;
                const bool author_is_owner = workspace_member_is_owner_wsmsg(actor.workspace, row_author_fp);
                const bool author_muted = message_target_is_muted_wsmsg(
                    db.db,
                    actor.workspace.workspace_id,
                    row_author_fp,
                    &moderr
                );

                if (!moderr.empty()) {
                    reply_json_wsmsg(deps, res, 500, json{
                        {"ok", false},
                        {"error", "server_error"},
                        {"message", "failed to read message mute state"},
                        {"detail", moderr}
                    });
                    return;
                }

                msg["is_own"] = is_own;
                // The browser receives permission decisions, not raw fingerprints.
                msg["can_delete"] = is_own || can_moderate;
                msg["can_mute_author"] = can_moderate && !is_own && !author_is_owner;
                msg["author_muted"] = author_muted;

                messages.push_back(std::move(msg));
                continue;
            }

            if (rc == SQLITE_DONE) break;

            reply_json_wsmsg(deps, res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to read workspace messages"},
                {"detail", sqlite3_errmsg(db.db)}
            });
            return;
        }

        std::string err;
        const sqlite3_int64 latest_id =
            latest_message_id_wsmsg(db.db, actor.workspace.workspace_id, &err);
        const sqlite3_int64 seen_id =
            last_seen_id_wsmsg(db.db, actor.workspace.workspace_id, actor.fp, &err);
        const sqlite3_int64 unread =
            unread_count_wsmsg(db.db, actor.workspace.workspace_id, seen_id, &err);

        reply_json_wsmsg(deps, res, 200, json{
            {"ok", true},
            {"workspace_id", actor.workspace.workspace_id},
            {"can_moderate_messages", can_moderate},
            {"actor_muted", actor_muted},
            {"message_board_muted_all", message_board_muted_all},
            {"workspace_message_mute_count", mute_count},
            {"messages", messages},
            {"latest_id", latest_id},
            {"last_seen_id", seen_id},
            {"unread_count", unread}
        });
    });

    // Create a new message. The server validates workspace membership, mute
    // state, message size, attachment references, rate limits and SQLite writes.
    srv.Post("/api/v4/workspaces/messages/post",
             [&deps](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Cache-Control", "no-store");
        if (!require_same_origin_for_cookie_mutation_wsmsg(req, res, deps)) return;

        json body = json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.is_object()) {
            reply_json_wsmsg(deps, res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "invalid json"}
            });
            return;
        }

        const std::string workspace_id = body.value("workspace_id", "");
        std::string text = trim_copy_wsmsg(body.value("body", ""));

        if (text.empty()) {
            reply_json_wsmsg(deps, res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "message is empty"}
            });
            return;
        }

        if (text.size() > k_workspace_message_max_bytes) {
            reply_json_wsmsg(deps, res, 413, json{
                {"ok", false},
                {"error", "too_large"},
                {"message", "message too large"},
                {"max_bytes", k_workspace_message_max_bytes}
            });
            return;
        }

        WorkspaceMessageActor actor;
        if (!require_workspace_message_actor(deps, req, res, workspace_id, &actor)) return;

        if (!require_workspace_message_rate_limit_wsmsg(
                deps,
                req,
                res,
                actor,
                "post",
                k_workspace_messages_post_rate_limit,
                now_epoch_wsmsg(deps))) {
            return;
        }

        std::string attachments_err;
        json attachments = sanitize_message_attachments_wsmsg(
            body,
            actor.workspace.workspace_id,
            &attachments_err
        );
        if (!attachments_err.empty() || !attachments.is_array()) {
            reply_json_wsmsg(deps, res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", attachments_err.empty() ? "invalid attachments" : attachments_err}
            });
            return;
        }

        if (text.empty() && attachments.empty()) {
            reply_json_wsmsg(deps, res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "message is empty"}
            });
            return;
        }

        const std::string attachments_json = attachments.dump();

        SqliteHandle db;
        std::string dberr;
        if (!open_workspace_messages_db(deps, &db, &dberr)) {
            reply_json_wsmsg(deps, res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to open workspace messages db"},
                {"detail", dberr}
            });
            return;
        }

        std::string mute_err;
        const bool muted = message_actor_is_muted_wsmsg(db.db, actor, &mute_err);
        if (!mute_err.empty()) {
            reply_json_wsmsg(deps, res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to read workspace message mute state"},
                {"detail", mute_err}
            });
            return;
        }

        if (muted) {
            reply_json_wsmsg(deps, res, 403, json{
                {"ok", false},
                {"error", "muted"},
                {"message", "you are muted in this workspace message board"}
            });
            return;
        }

        const std::int64_t now = now_epoch_wsmsg(deps);
        const std::string created_at = iso_utc_from_epoch_wsmsg(now);

        // BEGIN IMMEDIATE reserves the write lock up front so insert + read-state
        // update either commit together or fail together.
        if (!exec_sql_wsmsg(db.db, "BEGIN IMMEDIATE;", &dberr)) {
            reply_json_wsmsg(deps, res, 503, json{
                {"ok", false},
                {"error", "busy"},
                {"message", "workspace messages db busy"},
                {"detail", dberr}
            });
            return;
        }

        sqlite3_int64 new_id = 0;
        bool ok = false;

        {
            StmtHandle st;
            if (prepare_wsmsg(db.db,
                    "INSERT INTO workspace_messages("
                    "workspace_id, author_fp, author_name, body, attachments_json, created_at_epoch, created_at"
                    ") VALUES(?,?,?,?,?,?,?);",
                    &st.stmt,
                    &dberr)) {
                bind_text_wsmsg(st.stmt, 1, actor.workspace.workspace_id);
                bind_text_wsmsg(st.stmt, 2, actor.fp);
                bind_text_wsmsg(st.stmt, 3, actor.display_name);
                bind_text_wsmsg(st.stmt, 4, text);
                bind_text_wsmsg(st.stmt, 5, attachments_json);
                sqlite3_bind_int64(st.stmt, 6, now);
                bind_text_wsmsg(st.stmt, 7, created_at);

                const int rc = sqlite3_step(st.stmt);
                if (rc == SQLITE_DONE) {
                    new_id = sqlite3_last_insert_rowid(db.db);
                    ok = mark_read_wsmsg(
                        db.db,
                        actor.workspace.workspace_id,
                        actor.fp,
                        new_id,
                        now,
                        &dberr
                    );
                } else {
                    dberr = sqlite3_errmsg(db.db);
                }
            }
        }

        if (!ok) {
            std::string rollback_err;
            (void)exec_sql_wsmsg(db.db, "ROLLBACK;", &rollback_err);

            audit_workspace_message_best_effort(
                deps,
                req,
                "workspace.messages_post_fail",
                "fail",
                actor,
                new_id
            );

            reply_json_wsmsg(deps, res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to write workspace message"},
                {"detail", dberr}
            });
            return;
        }

        // Commit only after the message insert and mark-read update both succeed.
        if (!exec_sql_wsmsg(db.db, "COMMIT;", &dberr)) {
            std::string rollback_err;
            (void)exec_sql_wsmsg(db.db, "ROLLBACK;", &rollback_err);

            reply_json_wsmsg(deps, res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to commit workspace message"},
                {"detail", dberr}
            });
            return;
        }

        std::string prune_err;
        if (!prune_workspace_messages_wsmsg(
                db.db,
                actor.workspace.workspace_id,
                now,
                &prune_err)) {
            audit_workspace_message_best_effort(
                deps,
                req,
                "workspace.messages_prune_failed",
                "warn",
                actor,
                0
            );
        }

        audit_workspace_message_best_effort(
            deps,
            req,
            "workspace.messages_post_ok",
            "ok",
            actor,
            new_id
        );

        reply_json_wsmsg(deps, res, 200, json{
            {"ok", true},
            {"workspace_id", actor.workspace.workspace_id},
            {"latest_id", new_id},
            {"message", json{
                {"id", new_id},
                {"workspace_id", actor.workspace.workspace_id},
                {"author_name", actor.display_name},
                {"body", text},
                {"attachments", attachments},
                {"created_at_epoch", now},
                {"created_at", created_at},
                {"is_own", true},
                {"can_delete", true},
                {"can_mute_author", false},
                {"author_muted", false}
            }}
        });
    });

    // Soft-delete a message. Authors can delete their own messages; workspace
    // owners can delete any message in the workspace.
    srv.Post("/api/v4/workspaces/messages/delete",
             [&deps](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Cache-Control", "no-store");
        if (!require_same_origin_for_cookie_mutation_wsmsg(req, res, deps)) return;

        json body = json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.is_object()) {
            reply_json_wsmsg(deps, res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "invalid json"}
            });
            return;
        }

        const std::string workspace_id = body.value("workspace_id", "");
        sqlite3_int64 message_id = 0;
        try {
            message_id = static_cast<sqlite3_int64>(body.value("message_id", 0LL));
        } catch (...) {
            message_id = 0;
        }

        if (message_id <= 0) {
            reply_json_wsmsg(deps, res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "invalid message_id"}
            });
            return;
        }

        WorkspaceMessageActor actor;
        if (!require_workspace_message_actor(deps, req, res, workspace_id, &actor)) return;

        if (!require_workspace_message_rate_limit_wsmsg(
                deps,
                req,
                res,
                actor,
                "delete",
                k_workspace_messages_delete_rate_limit,
                now_epoch_wsmsg(deps))) {
            return;
        }

        SqliteHandle db;
        std::string dberr;
        if (!open_workspace_messages_db(deps, &db, &dberr)) {
            reply_json_wsmsg(deps, res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to open workspace messages db"},
                {"detail", dberr}
            });
            return;
        }

        std::string author_fp;
        if (!get_message_author_wsmsg(db.db, actor.workspace.workspace_id, message_id, &author_fp, &dberr)) {
            reply_json_wsmsg(deps, res, 404, json{
                {"ok", false},
                {"error", "not_found"},
                {"message", "message not found"}
            });
            return;
        }

        const bool can_delete = is_workspace_owner_wsmsg(actor) || author_fp == actor.fp;
        if (!can_delete) {
            reply_json_wsmsg(deps, res, 403, json{
                {"ok", false},
                {"error", "forbidden"},
                {"message", "only the author or workspace owner can delete this message"}
            });
            return;
        }

        const std::int64_t now = now_epoch_wsmsg(deps);
        if (!soft_delete_message_wsmsg(db.db, actor.workspace.workspace_id, message_id, actor.fp, now, &dberr)) {
            reply_json_wsmsg(deps, res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to delete workspace message"},
                {"detail", dberr}
            });
            return;
        }

        audit_workspace_message_best_effort(
            deps,
            req,
            "workspace.messages_delete_ok",
            "ok",
            actor,
            message_id
        );

        reply_json_wsmsg(deps, res, 200, json{
            {"ok", true},
            {"workspace_id", actor.workspace.workspace_id},
            {"message_id", message_id}
        });
    });

    // Owner-only moderation endpoint. The browser sends message_id/target_all;
    // the server resolves the actual target fingerprint internally.
    srv.Post("/api/v4/workspaces/messages/mute",
             [&deps](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Cache-Control", "no-store");
        if (!require_same_origin_for_cookie_mutation_wsmsg(req, res, deps)) return;

        json body = json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.is_object()) {
            reply_json_wsmsg(deps, res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "invalid json"}
            });
            return;
        }

        const std::string workspace_id = body.value("workspace_id", "");
        const bool target_all = body.value("target_all", false);
        const bool muted = body.value("muted", true);
        std::string reason = trim_copy_wsmsg(body.value("reason", ""));

        sqlite3_int64 message_id = 0;
        try {
            message_id = static_cast<sqlite3_int64>(body.value("message_id", 0LL));
        } catch (...) {
            message_id = 0;
        }

        if (reason.size() > 500) reason = reason.substr(0, 500);

        if (!target_all && message_id <= 0) {
            reply_json_wsmsg(deps, res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "target_all or message_id is required"}
            });
            return;
        }

        WorkspaceMessageActor actor;
        if (!require_workspace_message_actor(deps, req, res, workspace_id, &actor)) return;

        if (!is_workspace_owner_wsmsg(actor)) {
            reply_json_wsmsg(deps, res, 403, json{
                {"ok", false},
                {"error", "forbidden"},
                {"message", "only workspace owner can mute members"}
            });
            return;
        }

        if (!require_workspace_message_rate_limit_wsmsg(
                deps,
                req,
                res,
                actor,
                "mute",
                k_workspace_messages_mute_rate_limit,
                now_epoch_wsmsg(deps))) {
            return;
        }

        SqliteHandle db;
        std::string dberr;
        if (!open_workspace_messages_db(deps, &db, &dberr)) {
            reply_json_wsmsg(deps, res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to open workspace messages db"},
                {"detail", dberr}
            });
            return;
        }

        std::string target_fp;
        if (target_all) {
            target_fp = "*";
        } else {
            if (!get_message_author_wsmsg(
                    db.db,
                    actor.workspace.workspace_id,
                    message_id,
                    &target_fp,
                    &dberr)) {
                reply_json_wsmsg(deps, res, 404, json{
                    {"ok", false},
                    {"error", "not_found"},
                    {"message", "message not found"}
                });
                return;
            }
        }

        // For individual mutes, make sure the message author is still a valid
        // non-owner member before creating the mute.
        if (target_fp != "*") {
            if (target_fp == actor.fp) {
                reply_json_wsmsg(deps, res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "cannot mute yourself"}
                });
                return;
            }

            if (!workspace_has_enabled_member_fp_wsmsg(actor.workspace, target_fp)) {
                reply_json_wsmsg(deps, res, 404, json{
                    {"ok", false},
                    {"error", "not_found"},
                    {"message", "target member not found"}
                });
                return;
            }

            if (workspace_member_is_owner_wsmsg(actor.workspace, target_fp)) {
                reply_json_wsmsg(deps, res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "workspace owner cannot be muted"}
                });
                return;
            }
        }

        bool ok = false;
        if (muted) {
            ok = upsert_message_mute_wsmsg(
                db.db,
                actor.workspace.workspace_id,
                target_fp,
                actor.fp,
                now_epoch_wsmsg(deps),
                reason,
                &dberr
            );
        } else if (target_fp == "*") {
            ok = delete_all_workspace_message_mutes_wsmsg(
                db.db,
                actor.workspace.workspace_id,
                &dberr
            );
        } else {
            ok = delete_message_mute_wsmsg(
                db.db,
                actor.workspace.workspace_id,
                target_fp,
                &dberr
            );
        }

        if (!ok) {
            reply_json_wsmsg(deps, res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to update workspace message mute state"},
                {"detail", dberr}
            });
            return;
        }

        audit_workspace_message_best_effort(
            deps,
            req,
            muted ? "workspace.messages_mute_ok" : "workspace.messages_unmute_ok",
            "ok",
            actor,
            0
        );

        reply_json_wsmsg(deps, res, 200, json{
            {"ok", true},
            {"workspace_id", actor.workspace.workspace_id},
            {"target_all", target_fp == "*"},
            {"message_id", message_id},
            {"muted", muted}
        });
    });

    // Mark messages as read for the actor. This is a mutation because it writes
    // workspace_message_reads, so it keeps the same-origin and rate-limit guards.
    srv.Post("/api/v4/workspaces/messages/read",
             [&deps](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Cache-Control", "no-store");
        if (!require_same_origin_for_cookie_mutation_wsmsg(req, res, deps)) return;

        json body = json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.is_object()) {
            reply_json_wsmsg(deps, res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "invalid json"}
            });
            return;
        }

        const std::string workspace_id = body.value("workspace_id", "");

        WorkspaceMessageActor actor;
        if (!require_workspace_message_actor(deps, req, res, workspace_id, &actor)) return;

        if (!require_workspace_message_rate_limit_wsmsg(
                deps,
                req,
                res,
                actor,
                "read",
                k_workspace_messages_read_rate_limit,
                now_epoch_wsmsg(deps))) {
            return;
        }

        SqliteHandle db;
        std::string dberr;
        if (!open_workspace_messages_db(deps, &db, &dberr)) {
            reply_json_wsmsg(deps, res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to open workspace messages db"},
                {"detail", dberr}
            });
            return;
        }

        sqlite3_int64 last_seen_id = 0;
        try {
            last_seen_id = static_cast<sqlite3_int64>(body.value("last_seen_id", 0LL));
        } catch (...) {
            last_seen_id = 0;
        }

        if (last_seen_id <= 0) {
            last_seen_id = latest_message_id_wsmsg(
                db.db,
                actor.workspace.workspace_id,
                &dberr
            );
        }

        const std::int64_t now = now_epoch_wsmsg(deps);
        if (!mark_read_wsmsg(
                db.db,
                actor.workspace.workspace_id,
                actor.fp,
                last_seen_id,
                now,
                &dberr)) {
            reply_json_wsmsg(deps, res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to mark workspace messages read"},
                {"detail", dberr}
            });
            return;
        }

        reply_json_wsmsg(deps, res, 200, json{
            {"ok", true},
            {"workspace_id", actor.workspace.workspace_id},
            {"last_seen_id", last_seen_id},
            {"unread_count", 0}
        });
    });
}

} // namespace pqnas
