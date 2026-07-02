/*
DNA-Nexus Server Main
==============================

This file is the main server entry point and legacy integration host for
DNA-Nexus. It wires together process-wide configuration, core helper functions,
authentication flows, audit logging, storage helpers, background workers, and
route registration.

The file is being gradually split into smaller route/helper modules. Large
transitional route blocks may live in routes_*.inc files and are included from
inside main() so they can still access the existing local server context while
the refactor continues.

## Major responsibilities

* Server startup: environment/config loading, paths, indexes, stores, and workers.
* Authentication: v4/v5 session, approval, cookie, and authorization helpers.
* Policy: user/admin role checks and endpoint authorization helpers.
* Audit: append-only hash-chained JSONL logging for security-sensitive actions.
* Storage: pool, disk, btrfs, RAID, quota, migration, cleanup, and tiering helpers.
* Routes: registration of API/static/admin/file/share/gallery/app routes.

## v4 QR authentication model

The v4 QR flow is device-mediated:

* Browser requests a v4 session token (st) signed by this server.
* Mobile app approves by producing an ML-DSA-87 signature over a canonical payload
  binding the session token hash, fingerprint/public key, origin, relying party ID,
  challenge, and timestamps.
* Server verifies the bindings and mints a short-lived browser session cookie.

## Security goals for the v4 QR flow

1. No shared secrets in the browser: browser login completes via one-time approval
   and a short-lived session cookie.
2. Cryptographic approval: ML-DSA-87 proves possession of the user's private key.
3. Strong binding:

   * Approval is bound to the exact session token via SHA-256(st) = st_hash.
   * Identity is bound via fingerprint <-> public key.
   * Login is bound to origin / relying-party context to reduce token reuse risk.
4. Replay resistance:

   * Session tokens expire.
   * Approval/consume paths enforce freshness and one-time use where applicable.
5. Auditability:

   * Security-sensitive decisions and operations should be written to the audit log.

## Explicit limitations

* This does not hide metadata from Cloudflare Tunnel, reverse proxies, or hosting
  infrastructure.
* This does not replace WebAuthn; it provides a QR-mediated login flow with a
  different UX and deployment model.
* If local authorization policy files or user state are compromised, authorization
  may be bypassed even when cryptographic checks still work.
* This file is not yet fully separated by responsibility; ongoing refactoring should
  continue moving coherent route/helper groups into dedicated modules.

Verification should fail closed: parse, signature, binding, freshness, or policy
failures must return an error and should emit an audit event when security-relevant.
*/

#include "archive_zip_manifest.h"
#include "version.h"
#include <iostream>
#include <string>
#include <ctime>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <cctype>
#include <dlfcn.h>
#include <unistd.h>
#include <limits.h>
#include <sodium.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <mutex>
#include <qrencode.h>
#include <filesystem>
#include <deque>
#include <algorithm>
#include <array>
#include <iomanip>
#include "verify_v4_crypto.h"
#include <functional>
#include "audit_log.h"
#include "audit_fields.h"
#include <limits>
#include <cstdint>
#include <thread>
#include <atomic>
#include <random>
#include <fcntl.h>
#include <pwd.h>
#include <cmath>
#include <cerrno>
#include <errno.h>
#include <sys/stat.h>
#include <condition_variable>
#include <set>
#include <map>
#include <csignal>
#include <sys/wait.h>
#include <sqlite3.h>
#include <unordered_set>

#include "routes_v5.h"
#include "verify_login_common.h"

#include <chrono>
#include <cstdio>

#include "pqnas_util.h"
#include "authz.h"
#include "session_cookie.h"
#include "runtime_paths.h"
#include "policy.h"
#include "routes/routes_storage_raid.h"
#include "routes/routes_admin_storage_tiering.h"
#include "routes/routes_service_notices.h"
#include "routes/routes_admin_user_lifecycle.h"
#include "routes/routes_admin_user_status.h"
#include "routes/routes_admin_user_storage_preview.h"
#include "routes/routes_admin_user_storage.h"
#include "routes/routes_auth_debug_approvals.h"
#include "routes/routes_admin_approvals_ui.h"
#include "routes/routes_admin_api_explorer.h"
#include "routes/routes_admin_users_overview.h"
#include "routes/routes_admin_user_storage_jobs.h"
#include "routes/routes_admin_user_profile.h"
#include "routes/routes_user_avatars.h"
#include "routes/routes_apps_manage.h"
#include "routes/routes_apps_public.h"
#include "routes/routes_core_ui_shell.h"
#include "routes/routes_admin_audit_read.h"
#include "routes/routes_admin_audit_rotate.h"
#include "routes/routes_admin_audit_retention.h"
#include "routes/routes_snapshots_browse.h"
#include "routes/routes_snapshots_create.h"
#include "routes/routes_snapshots_restore.h"
#include "routes/routes_uploads_chunked.h"
#include "routes/routes_file_versions_archive_blob.h"
#include "routes/routes_file_versions_read.h"
#include "routes/routes_file_versions_manage.h"
#include "routes/routes_file_versions_restore.h"

// header-only HTTP server
#include "httplib.h"
#include "allowlist.h"
#include "v4_verify_shared.h"
#include "users_registry.h"
#include "storage_info.h"
#include "user_quota.h"
#include "storage_resolver.h"
#include "file_location_index.h"
#include "workspaces.h"
#include "workspace_external_invites.h"
#include "workspace_external_sessions.h"
#include "routes/routes_workspace_external_sessions.h"
#include "routes/routes_workspace_external_invites.h"
#include "routes/routes_workspace_external_messages.h"
#include "routes/routes_workspaces_files.h"
#include "routes/routes_workspace_links.h"
//storage health
#include "drive_health.h"
#include "drive_health_monitor.h"
#include "routes/routes_drive_locate.h"

//sharing
#include "share_links.h"
#include "share_pq_v1.h"
#include "share_pq_crypto_v1.h"
#include "share_pq_mlkem_v1.h"
#include <openssl/rand.h>
#include "workspace_access_shared.h"

//pool migration
#include "user_storage_migration.h"
#include "storage_pools.h"

// snapshots
#include "storage/snapshots/snapshot_scheduler.h"
#include "file_versions.h"
#include "file_versions_present.h"
#include "file_versions_restore.h"
#include "file_versions_read.h"

//favorites
#include "file_favorites.h"
#include <sys/file.h>

// Reel Stack user metadata is path-keyed in /config/reelstack_meta.sqlite3.
// These helpers are intentionally best-effort hooks for File Manager move/delete
// paths, matching the existing favorites/gallery metadata lifecycle.
static std::mutex g_reelstack_meta_path_hooks_mu;

static std::filesystem::path reelstack_meta_db_path_hooks_local() {
    return std::filesystem::path(pqnas::data_root_dir()).parent_path()
         / "config"
         / "reelstack_meta.sqlite3";
}

static std::string reelstack_like_escape_path_hooks_local(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (char c : in) {
        if (c == '\\' || c == '%' || c == '_') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

static bool reelstack_meta_open_existing_db_path_hooks_local(sqlite3** out_db,
                                                            std::string* err) {
    if (out_db) *out_db = nullptr;
    if (err) err->clear();

    const std::filesystem::path db_path = reelstack_meta_db_path_hooks_local();

    std::error_code ec;
    if (!std::filesystem::exists(db_path, ec)) {
        return true; // Reel Stack metadata DB has not been created yet; no-op.
    }
    if (ec) {
        if (err) *err = "stat reelstack meta db failed: " + ec.message();
        return false;
    }

    sqlite3* db = nullptr;
    if (sqlite3_open(db_path.string().c_str(), &db) != SQLITE_OK) {
        if (err) *err = db ? sqlite3_errmsg(db) : "sqlite open failed";
        if (db) sqlite3_close(db);
        return false;
    }

    sqlite3_busy_timeout(db, 5000);
    if (out_db) *out_db = db;
    return true;
}

static bool reelstack_meta_exec_sql_path_hooks_local(sqlite3* db,
                                                    const char* sql,
                                                    std::string* err) {
    char* errmsg = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        if (err) *err = errmsg ? errmsg : sqlite3_errmsg(db);
        if (errmsg) sqlite3_free(errmsg);
        return false;
    }
    return true;
}

static bool reelstack_meta_rename_one_path_local(const std::string& scope_type,
                                                 const std::string& scope_id,
                                                 const std::string& from_rel,
                                                 const std::string& to_rel,
                                                 std::int64_t now_epoch,
                                                 std::string* err) {
    if (err) err->clear();
    if (scope_type.empty() || scope_id.empty() || from_rel.empty() || to_rel.empty()) return true;
    if (from_rel == to_rel) return true;

    std::lock_guard<std::mutex> lk(g_reelstack_meta_path_hooks_mu);

    sqlite3* db = nullptr;
    if (!reelstack_meta_open_existing_db_path_hooks_local(&db, err)) return false;
    if (!db) return true;

    auto rollback = [&]() {
        std::string ignored;
        (void)reelstack_meta_exec_sql_path_hooks_local(db, "ROLLBACK;", &ignored);
    };

    if (!reelstack_meta_exec_sql_path_hooks_local(db, "BEGIN IMMEDIATE;", err)) {
        sqlite3_close(db);
        return false;
    }

    {
        const char* sql =
            "DELETE FROM reelstack_meta "
            "WHERE scope_type = ?1 AND scope_id = ?2 AND logical_rel_path = ?3";

        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
            if (err) *err = sqlite3_errmsg(db);
            rollback();
            sqlite3_close(db);
            return false;
        }

        sqlite3_bind_text(st, 1, scope_type.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, scope_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 3, to_rel.c_str(), -1, SQLITE_TRANSIENT);

        const int rc = sqlite3_step(st);
        if (rc != SQLITE_DONE) {
            if (err) *err = sqlite3_errmsg(db);
            sqlite3_finalize(st);
            rollback();
            sqlite3_close(db);
            return false;
        }

        sqlite3_finalize(st);
    }

    {
        const char* sql =
            "UPDATE reelstack_meta "
            "SET logical_rel_path = ?4, updated_epoch = ?5 "
            "WHERE scope_type = ?1 AND scope_id = ?2 AND logical_rel_path = ?3";

        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
            if (err) *err = sqlite3_errmsg(db);
            rollback();
            sqlite3_close(db);
            return false;
        }

        sqlite3_bind_text(st, 1, scope_type.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, scope_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 3, from_rel.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 4, to_rel.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 5, static_cast<sqlite3_int64>(now_epoch));

        const int rc = sqlite3_step(st);
        if (rc != SQLITE_DONE) {
            if (err) *err = sqlite3_errmsg(db);
            sqlite3_finalize(st);
            rollback();
            sqlite3_close(db);
            return false;
        }

        sqlite3_finalize(st);
    }

    if (!reelstack_meta_exec_sql_path_hooks_local(db, "COMMIT;", err)) {
        rollback();
        sqlite3_close(db);
        return false;
    }

    sqlite3_close(db);
    return true;
}

static bool reelstack_meta_rename_subtree_path_local(const std::string& scope_type,
                                                     const std::string& scope_id,
                                                     const std::string& from_rel,
                                                     const std::string& to_rel,
                                                     std::int64_t now_epoch,
                                                     std::string* err) {
    if (err) err->clear();
    if (scope_type.empty() || scope_id.empty() || from_rel.empty() || to_rel.empty()) return true;
    if (from_rel == to_rel) return true;

    std::lock_guard<std::mutex> lk(g_reelstack_meta_path_hooks_mu);

    sqlite3* db = nullptr;
    if (!reelstack_meta_open_existing_db_path_hooks_local(&db, err)) return false;
    if (!db) return true;

    const std::string from_pattern = reelstack_like_escape_path_hooks_local(from_rel + "/") + "%";
    const std::string to_pattern = reelstack_like_escape_path_hooks_local(to_rel + "/") + "%";
    const int tail_start = static_cast<int>(from_rel.size()) + 1; // SQLite substr is 1-indexed.

    auto rollback = [&]() {
        std::string ignored;
        (void)reelstack_meta_exec_sql_path_hooks_local(db, "ROLLBACK;", &ignored);
    };

    if (!reelstack_meta_exec_sql_path_hooks_local(db, "BEGIN IMMEDIATE;", err)) {
        sqlite3_close(db);
        return false;
    }

    {
        const char* sql =
            "DELETE FROM reelstack_meta "
            "WHERE scope_type = ?1 AND scope_id = ?2 "
            "  AND (logical_rel_path = ?3 OR logical_rel_path LIKE ?4 ESCAPE '\\')";

        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
            if (err) *err = sqlite3_errmsg(db);
            rollback();
            sqlite3_close(db);
            return false;
        }

        sqlite3_bind_text(st, 1, scope_type.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, scope_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 3, to_rel.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 4, to_pattern.c_str(), -1, SQLITE_TRANSIENT);

        const int rc = sqlite3_step(st);
        if (rc != SQLITE_DONE) {
            if (err) *err = sqlite3_errmsg(db);
            sqlite3_finalize(st);
            rollback();
            sqlite3_close(db);
            return false;
        }

        sqlite3_finalize(st);
    }

    {
        const char* sql =
            "UPDATE reelstack_meta "
            "SET logical_rel_path = CASE "
            "    WHEN logical_rel_path = ?3 THEN ?4 "
            "    ELSE ?4 || substr(logical_rel_path, ?5) "
            "  END, "
            "  updated_epoch = ?6 "
            "WHERE scope_type = ?1 AND scope_id = ?2 "
            "  AND (logical_rel_path = ?3 OR logical_rel_path LIKE ?7 ESCAPE '\\')";

        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
            if (err) *err = sqlite3_errmsg(db);
            rollback();
            sqlite3_close(db);
            return false;
        }

        sqlite3_bind_text(st, 1, scope_type.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, scope_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 3, from_rel.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 4, to_rel.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(st, 5, tail_start);
        sqlite3_bind_int64(st, 6, static_cast<sqlite3_int64>(now_epoch));
        sqlite3_bind_text(st, 7, from_pattern.c_str(), -1, SQLITE_TRANSIENT);

        const int rc = sqlite3_step(st);
        if (rc != SQLITE_DONE) {
            if (err) *err = sqlite3_errmsg(db);
            sqlite3_finalize(st);
            rollback();
            sqlite3_close(db);
            return false;
        }

        sqlite3_finalize(st);
    }

    if (!reelstack_meta_exec_sql_path_hooks_local(db, "COMMIT;", err)) {
        rollback();
        sqlite3_close(db);
        return false;
    }

    sqlite3_close(db);
    return true;
}

static bool reelstack_meta_remove_under_prefix_path_local(const std::string& scope_type,
                                                          const std::string& scope_id,
                                                          const std::string& rel,
                                                          const std::string& item_type,
                                                          std::string* err) {
    if (err) err->clear();
    if (scope_type.empty() || scope_id.empty() || rel.empty()) return true;

    std::lock_guard<std::mutex> lk(g_reelstack_meta_path_hooks_mu);

    sqlite3* db = nullptr;
    if (!reelstack_meta_open_existing_db_path_hooks_local(&db, err)) return false;
    if (!db) return true;

    const bool is_dir = (item_type == "dir" || item_type == "folder");
    const std::string pattern = reelstack_like_escape_path_hooks_local(rel + "/") + "%";

    const char* sql_dir =
        "DELETE FROM reelstack_meta "
        "WHERE scope_type = ?1 AND scope_id = ?2 "
        "  AND (logical_rel_path = ?3 OR logical_rel_path LIKE ?4 ESCAPE '\\')";

    const char* sql_file =
        "DELETE FROM reelstack_meta "
        "WHERE scope_type = ?1 AND scope_id = ?2 AND logical_rel_path = ?3";

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, is_dir ? sql_dir : sql_file, -1, &st, nullptr) != SQLITE_OK) {
        if (err) *err = sqlite3_errmsg(db);
        sqlite3_close(db);
        return false;
    }

    sqlite3_bind_text(st, 1, scope_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, scope_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, rel.c_str(), -1, SQLITE_TRANSIENT);
    if (is_dir) sqlite3_bind_text(st, 4, pattern.c_str(), -1, SQLITE_TRANSIENT);

    const int rc = sqlite3_step(st);
    if (rc != SQLITE_DONE) {
        if (err) *err = sqlite3_errmsg(db);
        sqlite3_finalize(st);
        sqlite3_close(db);
        return false;
    }

    sqlite3_finalize(st);
    sqlite3_close(db);
    return true;
}


#include "storage_resolver.h"

//apps
#include "static_serve.h"
#include "path_lock_manager.h"
#include "system_metrics.h"
// JSON (header-only)
#include <nlohmann/json.hpp>

#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <sys/wait.h>

// for mobile app
#include "app_tokens.h"
#include "app_pairing.h"

// image gallery
#include "gallery_meta.h"
#include "gallery_albums_routes.h"
#include "gallery_albums.h"
#include "image_embedded_meta.h"

// trash
#include "trash_index.h"
#include "trash_service.h"
#include "trash_routes.h"

// drop zone
#include "dropzone_routes.h"
#include "dropzone_index.h"

//Echo stack
#include "echo_stack_routes.h"
#include "circle_stack_routes.h"
#include "echo_stack_index.h"

// activity
#include "routes/routes_activity.h"
#include "backups/system_backup_worker.h"
#include "backups/system_backup_routes.h"
#include "notifications/notification_routes.h"
#include "updates/update_center_routes.h"
#include "routes/routes_people.h"
#include "routes/routes_file_annotations.h"
#include "routes/routes_file_locks.h"
#include "file_locks.h"
#include "activity_log.h"

using json = nlohmann::json;

static void reply_json(httplib::Response& res, int code, const std::string& body_json);

// ---- config ----
static unsigned char SERVER_PK[32];
static unsigned char SERVER_SK[64];
static unsigned char COOKIE_KEY[32];

static std::string exe_dir();

// REPO_ROOT is derived from the running binary location:
// build/bin/pqnas_server  -> REPO_ROOT = build/bin/../../ = repo root
const std::string REPO_ROOT = std::filesystem::weakly_canonical(
    std::filesystem::path(exe_dir()) / ".." / ".."
).string();


// -------------------- Runtime roots (env-first, dev fallback) --------------------
//
// In production installs these are set via /etc/pqnas/pqnas.env:
//   PQNAS_STATIC_ROOT=/opt/pqnas/static
//   PQNAS_APPS_ROOT=/srv/pqnas/apps
//
// In dev (run from repo) they fall back to REPO_ROOT paths.
//

// ============================================================================
//  STATIC / CONFIG PATH HELPERS
// ============================================================================

static std::string getenv_str(const char* k) {
    const char* v = std::getenv(k);
    return (v && *v) ? std::string(v) : std::string();
}

[[maybe_unused]] static std::string env_or(const char* k, const std::string& fallback) {

    const std::string v = getenv_str(k);
    return v.empty() ? fallback : v;
}

static bool dir_exists(const std::string& p) {
    std::error_code ec;
    return std::filesystem::exists(p, ec) && !ec && std::filesystem::is_directory(p, ec);
}

static std::string static_root_dir() {
    // 1) explicit override
    const std::string env = getenv_str("PQNAS_STATIC_ROOT");
    if (!env.empty()) return env;

    // 2) service-friendly default
    const std::string opt = "/opt/pqnas/static";
    if (dir_exists(opt)) return opt;

    // 3) dev fallback
    return (std::filesystem::path(REPO_ROOT) / "server/src/static").string();
}

static std::string apps_root_dir() {
    const std::string env = getenv_str("PQNAS_APPS_ROOT");
    if (!env.empty()) return env;

    const std::string srv = "/srv/pqnas/apps";
    if (dir_exists(srv)) return srv;

    return (std::filesystem::path(REPO_ROOT) / "apps").string();
}

static std::string config_root_dir() {
    const std::string env = getenv_str("PQNAS_CONFIG_ROOT");
    if (!env.empty()) return env;

    const std::string srv = "/srv/pqnas/config";
    if (dir_exists(srv)) return srv;

    return (std::filesystem::path(REPO_ROOT) / "config").string();
}

static std::string app_launch_policy_path() {
    return (std::filesystem::path(config_root_dir()) / "app_launch_policy.json").string();
}

static std::string static_path(const char* rel) {
    return (std::filesystem::path(static_root_dir()) / rel).string();
}

// ---- Static assets (env-first) ----
const std::string STATIC_AUDIT_HTML          = static_path("admin_audit.html");
const std::string STATIC_AUDIT_JS            = static_path("admin_audit.js");
const std::string STATIC_ADMIN_HTML          = static_path("admin.html");
const std::string STATIC_ADMIN_JS            = static_path("admin.js");
const std::string STATIC_ADMIN_APPS_HTML     = static_path("admin_apps.html");
const std::string STATIC_ADMIN_UPDATES_HTML  = static_path("admin_updates.html");
const std::string STATIC_ADMIN_APPS_JS       = static_path("admin_apps.js");
const std::string STATIC_APP_HTML            = static_path("app.html");
const std::string STATIC_APP_JS              = static_path("app.js");
const std::string STATIC_USERS_HTML          = static_path("admin_users.html");
const std::string STATIC_ADMIN_STATS_HTML      = static_path("admin_stats.html");
const std::string STATIC_ADMIN_STATS_JS        = static_path("admin_stats.js");
const std::string STATIC_USERS_JS            = static_path("admin_users.js");
const std::string STATIC_WAIT_APPROVAL_HTML  = static_path("wait_approval.html");
const std::string STATIC_WAIT_APPROVAL_JS    = static_path("wait_approval.js");
const std::string STATIC_SYSTEM_HTML         = static_path("system.html");
const std::string STATIC_SYSTEM_JS           = static_path("system.js");
const std::string STATIC_LOGIN               = static_path("login.html");
const std::string STATIC_V5_JS               = static_path("pqnas_v5.js");
const std::string STATIC_ADMIN_SETTINGS_HTML = static_path("admin_settings.html");
const std::string STATIC_ADMIN_SETTINGS_JS   = static_path("admin_settings.js");
const std::string STATIC_ADMIN_SERVICE_NOTICES_HTML = static_path("admin_service_notices.html");
const std::string STATIC_APPROVALS_HTML      = static_path("admin_approvals.html");
const std::string STATIC_APPROVALS_JS        = static_path("admin_approvals.js");
const std::string STATIC_BADGES_JS           = static_path("admin_badges.js");
const std::string STATIC_THEME_CSS           = static_path("theme.css");
const std::string STATIC_THEME_JS            = static_path("theme.js");

// ---- Apps dirs (env-first) ----
const std::string APPS_DIR           = apps_root_dir();
const std::string APPS_BUNDLED_DIR   = (std::filesystem::path(APPS_DIR) / "bundled").string();
const std::string APPS_INSTALLED_DIR = (std::filesystem::path(APPS_DIR) / "installed").string();
const std::string APPS_USERS_DIR     = (std::filesystem::path(APPS_DIR) / "users").string();

// for mobile app
static pqnas::AppTokenStore g_app_tokens;
static pqnas::AppPairingStore g_app_pairing;

static std::string ORIGIN   = "https://nas.example.com";
static std::string TLS_SPKI_SHA256_PIN;
static std::string ISS      = "pq-nas";
static std::string AUD      = "dna-messenger";
static std::string SCOPE    = "pqnas.login";
static std::string APP_NAME = "PQ-NAS";

// v4 app requires rp binding inside st payload
static std::string RP_ID    = "nas.example.com";  // relying party id (domain)

static int REQ_TTL      = 60;
static int SESS_TTL     = 8 * 3600;
static int LISTEN_PORT  = 8081; // use 8081 to avoid conflicts

struct ApprovalEntry {
    std::string cookie_val;   // pqnas_session cookie value (b64url.claims + "." + b64url.mac)
    std::string fingerprint;  // computed_fp (hex)
    long expires_at = 0;      // epoch seconds
};

static std::unordered_map<std::string, ApprovalEntry> g_approvals;
static std::mutex g_approvals_mu;

// --- pending admin approval / browser-bound preauth ---
struct PendingEntry {
    std::string reason;
    std::string fingerprint_hex;             // e.g. "user_disabled"
    long expires_at = 0;            // unix epoch seconds
    std::string browser_bind_hash;  // SHA-256 hex of preauth cookie minted by /api/v5/session
};

static std::unordered_map<std::string, PendingEntry> g_pending;
static std::mutex g_pending_mu;


// ============================================================================
//  PENDING LOGIN / APPROVAL STATE
// ============================================================================

static void pending_prune(long now) {
    std::lock_guard<std::mutex> lk(g_pending_mu);
    for (auto it = g_pending.begin(); it != g_pending.end(); ) {
        if (now > it->second.expires_at)
            it = g_pending.erase(it);
        else
            ++it;
    }
}

static void pending_put(const std::string& sid, const PendingEntry& e) {
    std::lock_guard<std::mutex> lk(g_pending_mu);
    g_pending[sid] = e;
}

static bool pending_get(const std::string& sid, PendingEntry& out) {
    std::lock_guard<std::mutex> lk(g_pending_mu);
    auto it = g_pending.find(sid);
    if (it == g_pending.end()) return false;
    out = it->second;
    return true;
}


// ============================================================================
//  APP LAUNCH POLICY HELPERS
// ============================================================================

static json app_launch_policy_defaults_json() {
    return json{
        {"default_launch", "embedded"},
        {"window_profile", "auto"},
        {"allow_user_override", true},
        {"admin_only", false},
        {"show_in_sidebar", true}
    };
}

static bool app_launch_value_ok(const std::string& v) {
    return v == "auto" || v == "embedded" || v == "detached";
}

static bool app_window_profile_ok(const std::string& v) {
    return v == "auto" || v == "small" || v == "normal" || v == "large" || v == "full";
}

static json normalize_app_launch_policy_entry(const json& in) {
    json out = json::object();

    const std::string default_launch =
        (in.is_object() && in.contains("default_launch") && in["default_launch"].is_string())
            ? in["default_launch"].get<std::string>()
            : "";

    const std::string window_profile =
        (in.is_object() && in.contains("window_profile") && in["window_profile"].is_string())
            ? in["window_profile"].get<std::string>()
            : "";

    const bool allow_user_override =
        (in.is_object() && in.contains("allow_user_override") && in["allow_user_override"].is_boolean())
            ? in["allow_user_override"].get<bool>()
            : true;

    const bool admin_only =
        (in.is_object() && in.contains("admin_only") && in["admin_only"].is_boolean())
            ? in["admin_only"].get<bool>()
            : false;

    const bool show_in_sidebar =
        (in.is_object() && in.contains("show_in_sidebar") && in["show_in_sidebar"].is_boolean())
            ? in["show_in_sidebar"].get<bool>()
            : true;

    if (app_launch_value_ok(default_launch)) out["default_launch"] = default_launch;
    if (app_window_profile_ok(window_profile)) out["window_profile"] = window_profile;
    out["allow_user_override"] = allow_user_override;
    out["admin_only"] = admin_only;
    out["show_in_sidebar"] = show_in_sidebar;

    return out;
}

static std::string content_disposition_ascii_fallback(const std::string& in) {
    std::string out;
    out.reserve(in.size());

    for (unsigned char c : in) {
        // Block header injection + quoted-string breakage + awkward path-ish chars.
        if (c <= 31 || c == 127 ||
            c == '"' || c == '\\' ||
            c == '\r' || c == '\n' ||
            c == ';' || c == '/') {
            out.push_back('_');
            continue;
            }

        // Keep visible ASCII only in plain filename= fallback.
        if (c >= 32 && c <= 126) out.push_back(static_cast<char>(c));
        else out.push_back('_');
    }

    if (out.empty()) out = "download";
    return out;
}

static std::string content_disposition_rfc5987(const std::string& in) {
    auto is_attr_char = [](unsigned char c) -> bool {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
            return true;
        switch (c) {
        case '!': case '#': case '$': case '&': case '+': case '-': case '.':
        case '^': case '_': case '`': case '|': case '~':
            return true;
        default:
            return false;
        }
    };

    static const char hex[] = "0123456789ABCDEF";

    std::string out;
    out.reserve(in.size() * 3);

    for (unsigned char c : in) {
        if (is_attr_char(c)) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[(c >> 4) & 0xF]);
            out.push_back(hex[c & 0xF]);
        }
    }
    return out;
}

static std::string build_content_disposition(const std::string& kind, const std::string& filename) {
    const std::string fallback = content_disposition_ascii_fallback(filename);
    const std::string encoded  = content_disposition_rfc5987(filename);
    return kind + "; filename=\"" + fallback + "\"; filename*=UTF-8''" + encoded;
}



// ============================================================================
//  PUBLIC SHARE HELPER FUNCTIONS
// ============================================================================

static std::string public_share_ext_lower_local(const std::filesystem::path& p) {
    std::string ext = p.extension().string();
    for (char& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return ext;
}

static std::string public_share_guess_mime_local(const std::filesystem::path& p) {
    const std::string ext = public_share_ext_lower_local(p);

    if (ext == ".mp4" || ext == ".m4v") return "video/mp4";
    if (ext == ".webm") return "video/webm";
    if (ext == ".ogv" || ext == ".ogg") return "video/ogg";
    if (ext == ".mov") return "video/quicktime";

    if (ext == ".mp3") return "audio/mpeg";
    if (ext == ".m4a") return "audio/mp4";
    if (ext == ".wav") return "audio/wav";
    if (ext == ".flac") return "audio/flac";

    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".png") return "image/png";
    if (ext == ".gif") return "image/gif";
    if (ext == ".webp") return "image/webp";

    if (ext == ".pdf") return "application/pdf";

    return "application/octet-stream";
}

static bool public_share_is_video_mime_local(const std::string& mime) {
    return mime.rfind("video/", 0) == 0;
}

static bool public_share_is_previewable_mime_local(const std::string& mime) {
    return mime.rfind("video/", 0) == 0 ||
           mime.rfind("audio/", 0) == 0 ||
           mime.rfind("image/", 0) == 0 ||
           mime == "application/pdf";
}

static std::string public_share_html_escape_local(const std::string& s) {
    std::string out;
    out.reserve(s.size());

    for (char c : s) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&#39;"; break;
        default: out.push_back(c); break;
        }
    }

    return out;
}
static bool public_share_parse_range_local(const std::string& h,
                                           std::uint64_t size,
                                           std::uint64_t* start,
                                           std::uint64_t* end) {
    if (!start || !end) return false;
    if (size == 0) return false;
    if (h.rfind("bytes=", 0) != 0) return false;

    const std::string spec = h.substr(6);
    const auto dash = spec.find('-');
    if (dash == std::string::npos) return false;

    const std::string a = spec.substr(0, dash);
    const std::string b = spec.substr(dash + 1);

    try {
        if (a.empty()) {
            if (b.empty()) return false;

            const std::uint64_t suffix = std::stoull(b);
            if (suffix == 0) return false;

            *start = suffix >= size ? 0 : size - suffix;
            *end = size - 1;
            return true;
        }

        std::uint64_t s = std::stoull(a);
        std::uint64_t e = b.empty() ? size - 1 : std::stoull(b);

        if (s >= size) return false;
        if (e < s) return false;
        if (e >= size) e = size - 1;

        *start = s;
        *end = e;
        return true;
    } catch (...) {
        return false;
    }
}
static void public_share_send_file_stream_local(const httplib::Request& req,
                                                httplib::Response& res,
                                                const std::filesystem::path& abs_path,
                                                const std::string& filename,
                                                bool force_download) {
    std::error_code ec;
    const std::uint64_t size = std::filesystem::file_size(abs_path, ec);
    if (ec) {
        res.status = 404;
        res.set_content(R"({"ok":false,"error":"file_not_found"})", "application/json");
        return;
    }

    const std::string mime = public_share_guess_mime_local(abs_path);
    const bool previewable = public_share_is_previewable_mime_local(mime);

    res.set_header("Accept-Ranges", "bytes");

    res.set_header(
        "Content-Disposition",
        build_content_disposition(
            (force_download || !previewable) ? "attachment" : "inline",
            filename.empty() ? "download" : filename
        )
    );

    const std::string range = req.get_header_value("Range");

    if (!range.empty()) {
        // cpp-httplib parsed the Range header into req.ranges earlier.
        // Since this route handles Range manually, clear the parsed ranges
        // or httplib will apply Range a second time to our 1024-byte body
        // and emit a bogus duplicate:
        // Content-Range: bytes 0-1023/1024
        auto& mutable_req = const_cast<httplib::Request&>(req);
        mutable_req.ranges.clear();

        std::uint64_t start = 0;
        std::uint64_t end = 0;

        if (!public_share_parse_range_local(range, size, &start, &end)) {
            res.status = 416;
            res.set_header("Content-Range", ("bytes */" + std::to_string(size)).c_str());
            res.set_content("", "text/plain; charset=utf-8");
            return;
        }

        // Safety cap: do not read huge requested ranges into RAM.
        // Browsers are fine receiving a smaller satisfiable range and asking again.
        static constexpr std::uint64_t kMaxManualRangeBytes = 8ull * 1024ull * 1024ull;

        if (end >= start && (end - start + 1) > kMaxManualRangeBytes) {
            end = start + kMaxManualRangeBytes - 1;
            if (end >= size) end = size - 1;
        }

        const std::uint64_t len64 = end - start + 1;

        std::string body;
        body.resize(static_cast<size_t>(len64));

        std::ifstream in(abs_path, std::ios::binary);
        if (!in.good()) {
            res.status = 404;
            res.set_content(R"({"ok":false,"error":"file_not_found"})", "application/json");
            return;
        }

        in.seekg(static_cast<std::streamoff>(start), std::ios::beg);
        if (!in.good()) {
            res.status = 500;
            res.set_content("Server error\n", "text/plain; charset=utf-8");
            return;
        }

        in.read(body.data(), static_cast<std::streamsize>(body.size()));
        const size_t got = static_cast<size_t>(in.gcount());
        if (got != body.size()) {
            body.resize(got);
            if (body.empty()) {
                res.status = 500;
                res.set_content("Server error\n", "text/plain; charset=utf-8");
                return;
            }
            end = start + static_cast<std::uint64_t>(body.size()) - 1;
        }

        res.status = 206;
        res.set_header(
            "Content-Range",
            (
                "bytes " +
                std::to_string(start) +
                "-" +
                std::to_string(end) +
                "/" +
                std::to_string(size)
            ).c_str()
        );

        res.set_content(std::move(body), mime.c_str());
        return;
    }

    res.status = 200;

    // Full non-range response. This is fine for normal GET / ?raw=1.
    res.set_content_provider(
        static_cast<size_t>(size),
        mime.c_str(),
        [abs_path](size_t offset, size_t length, httplib::DataSink& sink) {
            std::ifstream in(abs_path, std::ios::binary);
            if (!in.good()) return false;

            in.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
            if (!in.good()) return false;

            std::array<char, 64 * 1024> buf{};
            size_t remaining = length;

            while (remaining > 0 && in.good()) {
                const size_t want = std::min(remaining, buf.size());
                in.read(buf.data(), static_cast<std::streamsize>(want));

                const size_t got = static_cast<size_t>(in.gcount());
                if (got == 0) break;

                if (!sink.write(buf.data(), got)) return false;
                remaining -= got;
            }

            return remaining == 0;
        }
    );
}

static std::string public_share_safe_cache_key_local(const std::string& token) {
    std::string out;
    out.reserve(token.size());

    for (char c : token) {
        if ((c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_') {
            out += c;
        } else {
            out += '_';
        }
    }

    return out.empty() ? "share" : out;
}

static std::string public_share_shell_quote_local(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out += '\'';

    for (char c : s) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out += c;
        }
    }

    out += '\'';
    return out;
}

static bool public_share_read_binary_file_local(const std::filesystem::path& p,
                                                std::string* out,
                                                std::string* err) {
    if (!out) return false;
    out->clear();

    std::error_code ec;
    const auto size = std::filesystem::file_size(p, ec);
    if (ec || size == 0) {
        if (err) *err = ec ? ec.message() : "empty file";
        return false;
    }

    if (size > 20ull * 1024ull * 1024ull) {
        if (err) *err = "poster too large";
        return false;
    }

    std::ifstream in(p, std::ios::binary);
    if (!in) {
        if (err) *err = "failed to open file";
        return false;
    }

    out->resize(static_cast<std::size_t>(size));
    in.read(out->data(), static_cast<std::streamsize>(out->size()));

    if (!in) {
        if (err) *err = "failed to read file";
        out->clear();
        return false;
    }

    return true;
}

static bool public_share_generate_video_poster_local(const std::filesystem::path& video_abs,
                                                     const std::filesystem::path& poster_abs,
                                                     std::string* err) {
    const std::filesystem::path ffmpeg = "/usr/bin/ffmpeg";

    std::error_code ec;
    if (!std::filesystem::exists(ffmpeg, ec)) {
        if (err) *err = "ffmpeg not found at /usr/bin/ffmpeg";
        return false;
    }

    std::filesystem::create_directories(poster_abs.parent_path(), ec);
    if (ec) {
        if (err) *err = "failed to create poster cache directory: " + ec.message();
        return false;
    }

    std::filesystem::path tmp = poster_abs;
    // Keep a real .jpg suffix so ffmpeg can infer the image muxer/encoder.
    tmp += ".tmp.jpg";

    std::filesystem::remove(tmp, ec);

    auto run_ffmpeg = [&](bool seek) -> int {
        std::ostringstream cmd;
        cmd
            << public_share_shell_quote_local(ffmpeg.string())
            << " -hide_banner -loglevel error -y ";

        if (seek) {
            cmd << "-ss 1 ";
        }

        cmd
            << "-i " << public_share_shell_quote_local(video_abs.string()) << " "
            << "-frames:v 1 "
            << "-vf " << public_share_shell_quote_local("scale=1200:-2:force_original_aspect_ratio=decrease") << " "
            << "-q:v 4 "
            << public_share_shell_quote_local(tmp.string());

        return std::system(cmd.str().c_str());
    };

    int rc = run_ffmpeg(true);

    if (rc != 0 || !std::filesystem::exists(tmp, ec) || std::filesystem::file_size(tmp, ec) == 0) {
        std::filesystem::remove(tmp, ec);
        rc = run_ffmpeg(false);
    }

    if (rc != 0 || !std::filesystem::exists(tmp, ec) || std::filesystem::file_size(tmp, ec) == 0) {
        std::filesystem::remove(tmp, ec);
        if (err) *err = "ffmpeg failed to generate poster";
        return false;
    }

    std::filesystem::remove(poster_abs, ec);
    ec.clear();
    std::filesystem::rename(tmp, poster_abs, ec);

    if (ec) {
        std::filesystem::remove(tmp, ec);
        if (err) *err = "failed to move poster into cache";
        return false;
    }

    return true;
}

static std::string public_share_now_iso_utc_local() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);

    std::tm tm{};
    gmtime_r(&tt, &tm);

    char buf[32]{};
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buf);
}

static std::string public_share_json_string_local(const json& obj,
                                                  const char* key) {
    if (!obj.is_object() || !obj.contains(key) || !obj.at(key).is_string()) {
        return std::string();
    }

    return obj.at(key).get<std::string>();
}

static bool public_share_json_bool_local(const json& obj,
                                         const char* key) {
    if (!obj.is_object() || !obj.contains(key)) return false;

    const auto& v = obj.at(key);

    if (v.is_boolean()) return v.get<bool>();

    if (v.is_number_integer()) return v.get<int>() != 0;

    if (v.is_string()) {
        std::string s = v.get<std::string>();
        for (char& c : s) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }

        return s == "true" || s == "1" || s == "yes" || s == "revoked" || s == "disabled";
    }

    return false;
}

static bool public_share_cache_share_inactive_local(const json& share,
                                                    const std::string& now_iso) {
    if (public_share_json_bool_local(share, "disabled")) return true;
    if (public_share_json_bool_local(share, "revoked")) return true;

    const std::string state = public_share_json_string_local(share, "state");
    if (state == "disabled" || state == "revoked" || state == "deleted") {
        return true;
    }

    const std::string expires_at = public_share_json_string_local(share, "expires_at");

    // shares.json stores UTC timestamps in ISO-8601 form like:
    // 2026-05-24T02:28:15Z
    // With that fixed-width UTC format, lexical comparison is safe.
    if (!expires_at.empty() && expires_at <= now_iso) {
        return true;
    }

    return false;
}

static bool public_share_load_active_tokens_for_poster_cache_local(std::set<std::string>* active_tokens,
                                                                  std::string* err) {
    if (!active_tokens) return false;
    active_tokens->clear();

    const std::filesystem::path shares_path = "/srv/pqnas/config/shares.json";

    std::ifstream in(shares_path);
    if (!in) {
        if (err) *err = "failed to open shares.json";
        return false;
    }

    json root;
    try {
        in >> root;
    } catch (const std::exception& e) {
        if (err) *err = std::string("failed to parse shares.json: ") + e.what();
        return false;
    }

    const json* items = nullptr;

    if (root.is_array()) {
        items = &root;
    } else if (root.is_object() && root.contains("shares") && root.at("shares").is_array()) {
        items = &root.at("shares");
    } else {
        if (err) *err = "shares.json has unsupported shape";
        return false;
    }

    const std::string now_iso = public_share_now_iso_utc_local();

    for (const auto& share : *items) {
        if (!share.is_object()) continue;

        const std::string token = public_share_json_string_local(share, "token").empty()
            ? public_share_json_string_local(share, "id")
            : public_share_json_string_local(share, "token");

        if (token.empty()) continue;
        if (public_share_cache_share_inactive_local(share, now_iso)) continue;

        active_tokens->insert(token);
    }

    return true;
}

static void public_share_cleanup_video_poster_cache_local() {
    const std::filesystem::path cache_dir = "/srv/pqnas/cache/share_posters";

    std::error_code ec;
    if (!std::filesystem::exists(cache_dir, ec) || ec) return;
    if (!std::filesystem::is_directory(cache_dir, ec) || ec) return;

    std::set<std::string> active_tokens;
    std::string load_err;

    if (!public_share_load_active_tokens_for_poster_cache_local(&active_tokens, &load_err)) {
        // Fail closed: if shares.json cannot be read, do not delete cache files.
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(cache_dir, ec)) {
        if (ec) return;

        std::error_code item_ec;
        if (!entry.is_regular_file(item_ec) || item_ec) continue;

        const auto p = entry.path();
        if (p.extension() != ".jpg") continue;

        const std::string token = p.stem().string();
        if (token.empty()) continue;

        if (active_tokens.find(token) == active_tokens.end()) {
            std::filesystem::remove(p, item_ec);
        }
    }
}

static void public_share_maybe_cleanup_video_poster_cache_daily_local() {
    static std::mutex mu;
    static std::chrono::system_clock::time_point last_attempt{};

    const auto now = std::chrono::system_clock::now();

    {
        std::lock_guard<std::mutex> lock(mu);

        if (last_attempt.time_since_epoch().count() != 0 &&
            now - last_attempt < std::chrono::hours(24)) {
            return;
        }

        // Set before cleanup so repeated failing requests do not hammer disk.
        last_attempt = now;
    }

    public_share_cleanup_video_poster_cache_local();
}


static void public_share_send_video_poster_local(httplib::Response& res,
                                                 const std::filesystem::path& video_abs,
                                                 const std::string& token) {
    public_share_maybe_cleanup_video_poster_cache_daily_local();

    const std::filesystem::path cache_dir = "/srv/pqnas/cache/share_posters";
    const std::string key = public_share_safe_cache_key_local(token);
    const std::filesystem::path poster_abs = cache_dir / (key + ".jpg");

    std::error_code ec;
    bool have_poster =
        std::filesystem::exists(poster_abs, ec) &&
        !ec &&
        std::filesystem::file_size(poster_abs, ec) > 0 &&
        !ec;

    if (!have_poster) {
        std::string gen_err;
        have_poster = public_share_generate_video_poster_local(video_abs, poster_abs, &gen_err);
    }

    if (have_poster) {
        std::string bytes;
        std::string read_err;

        if (public_share_read_binary_file_local(poster_abs, &bytes, &read_err)) {
            res.status = 200;
            res.set_header("Cache-Control", "public, max-age=3600");
            res.set_header("Content-Disposition", "inline; filename=\"poster.jpg\"");
            res.set_content(bytes, "image/jpeg");
            return;
        }
    }

    // Safe fallback: keep public previews working even when ffmpeg is missing
    // or the video has no decodable frame.
    res.status = 302;
    res.set_header("Cache-Control", "no-store");
    res.set_header("Location", "/static/video-share-fallback.svg");
}


static std::string public_share_video_page_local(const std::string& token,
                                                 const std::string& filename,
                                                 const std::string& mime,
                                                 const httplib::Request& req) {
    // Reuse the shared public share HTML escape helper.
    const std::string title = filename.empty() ? "Shared video" : filename;
    const std::string description = "Open shared Reel Stack video";

    std::string proto = req.get_header_value("X-Forwarded-Proto");
    if (proto.empty()) proto = "https";

    std::string host = req.get_header_value("X-Forwarded-Host");
    if (host.empty()) host = req.get_header_value("Host");

    const std::string origin = host.empty() ? std::string() : proto + "://" + host;
    const std::string page_url = origin.empty()
        ? "/s/" + token
        : origin + "/s/" + token;
    const std::string raw_url = page_url + "?raw=1";
    const std::string image_url = page_url + "?poster=1";

    const std::string video_mime = mime.empty() ? "video/mp4" : mime;

    const std::string h_title = public_share_html_escape_local(title);
    const std::string h_description = public_share_html_escape_local(description);
    const std::string h_page_url = public_share_html_escape_local(page_url);
    const std::string h_raw_url = public_share_html_escape_local(raw_url);
    const std::string h_image_url = public_share_html_escape_local(image_url);
    const std::string h_video_mime = public_share_html_escape_local(video_mime);

    std::ostringstream html;
    html
        << "<!doctype html>\n"
        << "<html lang=\"en\">\n"
        << "<head>\n"
        << "  <meta charset=\"utf-8\">\n"
        << "  <meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
        << "  <title>" << h_title << "</title>\n"
        << "  <meta name=\"description\" content=\"" << h_description << "\">\n"
        << "  <meta name=\"application-name\" content=\"Reel Stack\">\n"

        << "  <meta property=\"og:type\" content=\"video.other\">\n"
        << "  <meta property=\"og:site_name\" content=\"DNA-Nexus / Reel Stack\">\n"
        << "  <meta property=\"og:title\" content=\"" << h_title << "\">\n"
        << "  <meta property=\"og:description\" content=\"" << h_description << "\">\n"
        << "  <meta property=\"og:url\" content=\"" << h_page_url << "\">\n"
        << "  <meta property=\"og:image\" content=\"" << h_image_url << "\">\n"
        << "  <meta property=\"og:image:width\" content=\"1200\">\n"
        << "  <meta property=\"og:image:height\" content=\"630\">\n"
        << "  <meta property=\"og:video\" content=\"" << h_raw_url << "\">\n"
        << "  <meta property=\"og:video:url\" content=\"" << h_raw_url << "\">\n"
        << "  <meta property=\"og:video:secure_url\" content=\"" << h_raw_url << "\">\n"
        << "  <meta property=\"og:video:type\" content=\"" << h_video_mime << "\">\n"

        << "  <meta name=\"twitter:card\" content=\"player\">\n"
        << "  <meta name=\"twitter:title\" content=\"" << h_title << "\">\n"
        << "  <meta name=\"twitter:description\" content=\"" << h_description << "\">\n"
        << "  <meta name=\"twitter:image\" content=\"" << h_image_url << "\">\n"
        << "  <meta name=\"twitter:player\" content=\"" << h_page_url << "\">\n"
        << "  <meta name=\"twitter:player:stream\" content=\"" << h_raw_url << "\">\n"
        << "  <meta name=\"twitter:player:stream:content_type\" content=\"" << h_video_mime << "\">\n"
        << "  <meta name=\"twitter:player:width\" content=\"1280\">\n"
        << "  <meta name=\"twitter:player:height\" content=\"720\">\n"

        << "  <style>\n"
        << "    :root{color-scheme:dark;background:#070b12;color:#e8edf7;font-family:Inter,system-ui,-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;}\n"
        << "    body{margin:0;min-height:100vh;display:grid;place-items:center;background:radial-gradient(circle at top,#182643 0,#070b12 55%);}\n"
        << "    main{width:min(1120px,94vw);padding:32px 0;}\n"
        << "    .card{border:1px solid rgba(255,255,255,.12);border-radius:24px;background:rgba(10,16,28,.82);box-shadow:0 24px 80px rgba(0,0,0,.45);overflow:hidden;}\n"
        << "    video{display:block;width:100%;max-height:76vh;background:#000;}\n"
        << "    .meta{padding:20px 24px 24px;}\n"
        << "    .eyebrow{font-size:11px;font-weight:800;letter-spacing:.18em;text-transform:uppercase;color:#93c5fd;margin-bottom:8px;}\n"
        << "    h1{font-size:20px;line-height:1.25;margin:0 0 8px;}\n"
        << "    p{margin:0;color:#aeb8cc;}\n"
        << "    a{color:#9ec5ff;}\n"
        << "  </style>\n"
        << "</head>\n"
        << "<body>\n"
        << "  <main>\n"
        << "    <section class=\"card\">\n"
        << "      <video controls preload=\"metadata\" playsinline poster=\"" << h_image_url << "\" src=\"" << h_raw_url << "\"></video>\n"
        << "      <div class=\"meta\">\n"
        << "        <div class=\"eyebrow\">Reel Stack video share</div>\n"
        << "        <h1>" << h_title << "</h1>\n"
        << "        <p>Shared from DNA-Nexus / Reel Stack. <a href=\"" << h_raw_url << "\">Open raw video</a></p>\n"
        << "      </div>\n"
        << "    </section>\n"
        << "  </main>\n"
        << "</body>\n"
        << "</html>\n";

    return html.str();
}

static json normalize_app_launch_policy_json(const json& in) {
    json out = json::object();
    out["schema"] = 1;
    out["defaults"] = app_launch_policy_defaults_json();
    out["by_app_id"] = json::object();

    if (!in.is_object()) return out;

    if (in.contains("defaults") && in["defaults"].is_object()) {
        json d = normalize_app_launch_policy_entry(in["defaults"]);
        json merged = app_launch_policy_defaults_json();
        for (auto it = d.begin(); it != d.end(); ++it) {
            merged[it.key()] = it.value();
        }
        out["defaults"] = merged;
    }

    if (in.contains("by_app_id") && in["by_app_id"].is_object()) {
        for (auto it = in["by_app_id"].begin(); it != in["by_app_id"].end(); ++it) {
            if (!it.value().is_object()) continue;
            out["by_app_id"][it.key()] = normalize_app_launch_policy_entry(it.value());
        }
    }

    return out;
}

static json load_app_launch_policy_json() {
    const std::string path = app_launch_policy_path();
    std::ifstream f(path);
    if (!f.good()) {
        return normalize_app_launch_policy_json(json::object());
    }

    try {
        json j;
        f >> j;
        return normalize_app_launch_policy_json(j);
    } catch (...) {
        return normalize_app_launch_policy_json(json::object());
    }
}

static bool save_app_launch_policy_json(const json& j) {
    namespace fs = std::filesystem;

    std::error_code ec;
    fs::path p(app_launch_policy_path());
    fs::create_directories(p.parent_path(), ec);

    const fs::path tmp = p.string() + ".tmp";

    try {
        {
            std::ofstream f(tmp);
            if (!f.good()) return false;
            f << normalize_app_launch_policy_json(j).dump(2);
            f.flush();
            if (!f.good()) return false;
        }

        fs::rename(tmp, p, ec);
        if (ec) {
            fs::remove(p, ec);
            ec.clear();
            fs::rename(tmp, p, ec);
            if (ec) {
                fs::remove(tmp, ec);
                return false;
            }
        }
        return true;
    } catch (...) {
        fs::remove(tmp, ec);
        return false;
    }
}


// ===================== Network sampling helpers (/proc/net/dev) =====================

#include <unordered_map>
#include <mutex>



// for audit log checking

// ============================================================================
//  USER STORAGE / POOL HELPERS
// ============================================================================

static long long file_size_bytes_safe(const std::string& path) {
    std::error_code ec;
    auto sz = std::filesystem::file_size(path, ec);
    if (ec) return -1;
    return (long long)sz;
}


static bool is_hex_lower_or_upper(char c) {
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

#include <filesystem>

// Reject absolute paths and ".." traversal. Also reject empty.
static bool is_safe_rel_path(const std::string& rel) {
    if (rel.empty()) return false;
    std::filesystem::path p(rel);
    if (p.is_absolute()) return false;

    for (const auto& part : p) {
        const std::string s = part.string();
        if (s == "..") return false;
    }
    return true;
}

// Best-effort recursive size; skips symlinks; ignores errors.
static unsigned long long dir_size_bytes_best_effort(const std::filesystem::path& root) {
    std::error_code ec;

    if (!std::filesystem::exists(root, ec) || ec) return 0;
    if (!std::filesystem::is_directory(root, ec) || ec) return 0;

    unsigned long long total = 0;

    auto it = std::filesystem::recursive_directory_iterator(
        root,
        std::filesystem::directory_options::skip_permission_denied,
        ec
    );

    for (auto end = std::filesystem::recursive_directory_iterator(); it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }

        std::error_code ec2;
        const auto st = it->symlink_status(ec2);
        if (ec2) continue;

        // skip symlinks entirely
        if (std::filesystem::is_symlink(st)) continue;

        if (std::filesystem::is_regular_file(st)) {
            std::error_code ec3;
            const auto sz = std::filesystem::file_size(it->path(), ec3);
            if (!ec3) total += (unsigned long long)sz;
        }
    }

    return total;
}


// Conservative validation: fingerprint must be hex, reasonable length.
static bool is_valid_fingerprint_hex(const std::string& fp) {
    if (fp.size() < 16) return false;
    if (fp.size() > 256) return false;
    for (char c : fp) {
        if (!is_hex_lower_or_upper(c)) return false;
    }
    return true;
}

// Ensure a directory exists (mkdir -p behavior). Returns true if exists/created.
static bool ensure_dir_exists(const std::filesystem::path& p, std::string* err = nullptr) {
    try {
        std::error_code ec;
        if (std::filesystem::exists(p, ec)) {
            if (ec) {
                if (err) *err = "exists() error: " + ec.message();
                return false;
            }
            if (!std::filesystem::is_directory(p, ec) || ec) {
                if (err) *err = "path exists but is not directory";
                return false;
            }
            return true;
        }

        if (!std::filesystem::create_directories(p, ec) || ec) {
            if (err) *err = "create_directories failed: " + ec.message();
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        if (err) *err = std::string("exception: ") + e.what();
        return false;
    } catch (...) {
        if (err) *err = "unknown exception";
        return false;
    }
}




static void pool_mounts_init_default_only();

static std::mutex& pool_mu() {
    static std::mutex mu;
    return mu;
}

static std::unordered_map<std::string, std::string>& pool_mount_by_id() {
    static std::unordered_map<std::string, std::string> m;
    return m;
}

static std::string& default_pool_id() {
    static std::string id = "default";
    return id;
}

static std::filesystem::path data_root_for_pool_id(const std::string& pool_id) {
    // default / legacy pool
    if (pool_id.empty() || pool_id == "default") {
        return std::filesystem::path(pqnas::data_root_dir());
    }

    std::string pool_mount;
    {
        std::lock_guard<std::mutex> lk(pool_mu());
        auto& m = pool_mount_by_id();
        auto it = m.find(pool_id);
        if (it != m.end()) pool_mount = it->second;
    }

    // named pool missing from runtime map -> safest compatibility fallback
    if (pool_mount.empty()) {
        return std::filesystem::path(pqnas::data_root_dir());
    }

    // canonical PQ-NAS data root inside pool
    return std::filesystem::path(pool_mount) / "data";
}

static std::string default_root_rel_for_fp(const std::string& fp_hex) {
    return std::string("users/") + fp_hex;
}

static std::string effective_root_rel_for_user(const pqnas::UserRec& u,
                                               const std::string& fp_hex) {
    if (!u.root_rel.empty() && is_safe_rel_path(u.root_rel)) {
        return u.root_rel;
    }
    return default_root_rel_for_fp(fp_hex);
}

[[maybe_unused]] static std::filesystem::path user_dir_for_fp(const pqnas::UsersRegistry& users,
                                             const std::string& fp_hex) {
    auto uopt = users.get(fp_hex);

    if (!uopt.has_value()) {
        return std::filesystem::path(pqnas::data_root_dir()) / default_root_rel_for_fp(fp_hex);
    }

    const auto& u = *uopt;
    const std::string pool_id = u.storage_pool_id.empty() ? "default" : u.storage_pool_id;
    const std::filesystem::path data_root = data_root_for_pool_id(pool_id);
    const std::string root_rel = effective_root_rel_for_user(u, fp_hex);

    return data_root / root_rel;
}

static std::filesystem::path user_dir_for_fp(pqnas::UsersRegistry& users, const std::string& fp_hex)
{
    auto uopt = users.get(fp_hex);

    // Default/simple install root.
    std::filesystem::path data_root = std::filesystem::path(pqnas::data_root_dir());

    if (uopt.has_value()) {
        const auto& u = *uopt;

        if (!u.storage_pool_id.empty()) {
            std::string pool_mount;

            // 1) Prefer runtime pool map if present
            {
                std::lock_guard<std::mutex> lk(pool_mu());
                auto& m = pool_mount_by_id();
                auto it = m.find(u.storage_pool_id);
                if (it != m.end()) {
                    pool_mount = it->second;
                }
            }

            // 2) Fallback: resolve from persisted pools.json
            if (pool_mount.empty()) {
                try {
                    const std::filesystem::path pools_path =
                        std::filesystem::path(pqnas::data_root_dir()).parent_path() / "config" / "pools.json";

                    std::ifstream f(pools_path);
                    if (f.good()) {
                        nlohmann::json j;
                        f >> j;

                        if (j.is_object() && j.contains("pools") && j["pools"].is_object()) {
                            for (auto it = j["pools"].begin(); it != j["pools"].end(); ++it) {
                                const std::string mount = it.key();
                                const auto& meta = it.value();
                                if (!meta.is_object()) continue;

                                const std::string pid = meta.value("pool_id", "");
                                if (pid == u.storage_pool_id) {
                                    pool_mount = mount;
                                    break;
                                }
                            }
                        }
                    }
                } catch (...) {
                    // fail closed to default root fallback below
                }
            }

            if (!pool_mount.empty()) {
                data_root = std::filesystem::path(pool_mount) / "data";
            }
        }

        if (u.storage_state == "allocated" &&
            !u.root_rel.empty() &&
            is_safe_rel_path(u.root_rel))
        {
            return data_root / u.root_rel;
        }
    }

    // Legacy / fallback layout
    return data_root / "users" / fp_hex;
}

//bridge wrapper
std::filesystem::path pqnas_user_dir_for_fp(pqnas::UsersRegistry& users,
                                            const std::string& fp_hex) {
    return user_dir_for_fp(users, fp_hex);
}

static std::string activity_user_display_name_local(pqnas::UsersRegistry& users,
                                                    const std::string& fp_hex) {
    auto rec = users.get(fp_hex);
    if (!rec.has_value()) return "";
    return rec->name;
}

static pqnas::activity::ActivityActor activity_actor_for_request_local(
    const httplib::Request* req,
    pqnas::UsersRegistry& users,
    const std::string& actor_fp
) {
    pqnas::activity::ActivityActor actor;
    actor.user_id = actor_fp;
    actor.display_name = activity_user_display_name_local(users, actor_fp);
    actor.kind = "user";

    if (!req) {
        return actor;
    }

    auto it = req->headers.find("Authorization");
    if (it == req->headers.end()) {
        return actor;
    }

    const std::string& hdr = it->second;
    const std::string prefix = "Bearer ";
    if (hdr.size() <= prefix.size() || hdr.compare(0, prefix.size(), prefix) != 0) {
        return actor;
    }

    const std::string raw_token = hdr.substr(prefix.size());
    if (raw_token.empty()) {
        return actor;
    }

    std::string token_fp;
    std::string token_role;
    std::string device_id;
    std::string terr;

    if (!g_app_tokens.verify_access_token(
            raw_token,
            &token_fp,
            &token_role,
            &device_id,
            &terr)) {
        return actor;
    }

    if (token_fp != actor_fp || device_id.empty()) {
        return actor;
    }

    actor.kind = "device";

    pqnas::TrustedAppDevice d;
    if (g_app_tokens.get_device(device_id, &d)) {
        actor.device_name = d.device_name;

        if (actor.device_name.empty()) {
            std::string model = d.device_model;
            std::string manufacturer = d.device_manufacturer;

            if (!manufacturer.empty() && !model.empty()) {
                actor.device_name = manufacturer + " " + model;
            } else if (!model.empty()) {
                actor.device_name = model;
            } else if (!manufacturer.empty()) {
                actor.device_name = manufacturer;
            } else if (!d.platform.empty()) {
                actor.device_name = d.platform + " device";
            }
        }
    }

    if (actor.device_name.empty()) {
        actor.device_name = "Mobile app";
    }

    return actor;
}

static std::string activity_basename_local(const std::string& rel_path,
                                           const std::string& fallback) {
    try {
        const std::filesystem::path p(rel_path);
        const std::string name = p.filename().string();
        if (!name.empty()) return name;
    } catch (...) {
    }

    if (!fallback.empty()) return fallback;
    return "item";
}

static void record_user_file_activity_best_effort_local(pqnas::UsersRegistry& users,
                                                        const std::string& actor_fp,
                                                        const std::string& event_type,
                                                        const std::string& rel_path,
                                                        const std::string& item_type,
                                                        const std::string& trash_id,
                                                        std::uint64_t size_bytes,
                                                        std::uint64_t file_count,
                                                        const httplib::Request* req = nullptr) {
    if (actor_fp.empty() || event_type.empty()) return;

    std::filesystem::path user_root;
    try {
        user_root = user_dir_for_fp(users, actor_fp);
    } catch (...) {
        return;
    }

    if (user_root.empty()) return;

    pqnas::activity::ActivityEvent ev;
    ev.owner_user_id = actor_fp;

    ev.actor = activity_actor_for_request_local(req, users, actor_fp);

    ev.event_type = event_type;
    ev.scope_type = "user";
    ev.scope_id = actor_fp;

    ev.target_kind = item_type.empty() ? "item" : item_type;
    ev.target_path = rel_path;
    ev.target_name = activity_basename_local(rel_path, ev.target_kind);

    ev.details = json{
        {"scope_type", "user"},
        {"original_rel_path", rel_path},
        {"item_type", ev.target_kind},
        {"size_bytes", size_bytes},
        {"file_count", file_count}
    };

    if (!trash_id.empty()) {
        ev.details["trash_id"] = trash_id;
    }

    std::string activity_err;
    (void)pqnas::activity::record_user_activity(user_root, ev, &activity_err);
}




// ============================================================================
//  ACTIVITY / AUDIT SUPPORT HELPERS
// ============================================================================

static std::string user_file_lock_fp_short_local(const std::string& fp) {
    if (fp.size() <= 18) return fp;
    return fp.substr(0, 8) + "…" + fp.substr(fp.size() - 8);
}

static bool require_user_no_live_lock_for_write_local(pqnas::UsersRegistry& users,
                                                      const std::string& users_path,
                                                      httplib::Response& res,
                                                      const std::string& fp_hex,
                                                      const std::string& rel_norm,
                                                      const std::string& action_label,
                                                      bool allow_lock_owner = true) {
    if (fp_hex.empty()) return true;

    const std::filesystem::path locks_db_path =
        std::filesystem::path(users_path).parent_path() / "file_locks.sqlite3";

    pqnas::FileLocksStore store(locks_db_path);
    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));

    (void)store.delete_expired(now, nullptr);

    std::string lerr;
    auto conflict = store.find_live_conflict("user", fp_hex, rel_norm, now, &lerr);

    if (!lerr.empty()) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "failed to check file lock"},
            {"detail", pqnas::shorten(lerr, 180)}
        }.dump());
        return false;
    }

    if (!conflict.has_value()) return true;

    if (allow_lock_owner &&
        !conflict->locked_by_fp.empty() &&
        conflict->locked_by_fp == fp_hex) {
        return true;
    }

    std::string label = activity_user_display_name_local(users, conflict->locked_by_fp);
    if (label.empty()) label = user_file_lock_fp_short_local(conflict->locked_by_fp);

    reply_json(res, 409, json{
        {"ok", false},
        {"error", "locked"},
        {"message", action_label.empty()
            ? "item is locked"
            : action_label + " blocked because this item is locked"},
        {"lock", json{
            {"scope_type", conflict->scope_type},
            {"scope_id", conflict->scope_id},
            {"logical_rel_path", conflict->logical_rel_path},
            {"item_kind", conflict->item_kind},
            {"locked_by_fp_short", user_file_lock_fp_short_local(conflict->locked_by_fp)},
            {"locked_by_label", label},
            {"note", conflict->note},
            {"created_at_epoch", conflict->created_at_epoch},
            {"updated_at_epoch", conflict->updated_at_epoch},
            {"expires_at_epoch", conflict->expires_at_epoch}
        }}
    }.dump());

    return false;
}




static std::string activity_security_target_name_local(const std::string& name,
                                                       const std::string& platform,
                                                       const std::string& fallback) {
    if (!name.empty()) return name;
    if (!platform.empty()) return platform + " device";
    if (!fallback.empty()) return fallback;
    return "device";
}

static nlohmann::json activity_security_device_details_local(const std::string& device_name,
                                                             const std::string& platform,
                                                             const std::string& app_version,
                                                             const std::string& device_model,
                                                             const std::string& device_manufacturer,
                                                             const std::string& os_version) {
    nlohmann::json details = nlohmann::json::object();

    details["scope_type"] = "security";

    if (!device_name.empty()) details["device_name"] = device_name;
    if (!platform.empty()) details["platform"] = platform;
    if (!app_version.empty()) details["app_version"] = app_version;
    if (!device_model.empty()) details["device_model"] = device_model;
    if (!device_manufacturer.empty()) details["device_manufacturer"] = device_manufacturer;
    if (!os_version.empty()) details["os_version"] = os_version;

    return details;
}

static nlohmann::json activity_security_device_details_local(const pqnas::TrustedAppDevice& d) {
    return activity_security_device_details_local(
        d.device_name,
        d.platform,
        d.app_version,
        d.device_model,
        d.device_manufacturer,
        d.os_version
    );
}

static void record_security_activity_best_effort_local(pqnas::UsersRegistry& users,
                                                       const std::string& owner_fp,
                                                       const std::string& actor_fp,
                                                       const std::string& event_type,
                                                       const std::string& target_kind,
                                                       const std::string& target_name,
                                                       const nlohmann::json& details) {
    if (owner_fp.empty() || event_type.empty()) return;

    std::filesystem::path user_root;
    try {
        user_root = user_dir_for_fp(users, owner_fp);
    } catch (...) {
        return;
    }

    if (user_root.empty()) return;

    pqnas::activity::ActivityEvent ev;
    ev.owner_user_id = owner_fp;

    ev.actor.user_id = actor_fp.empty() ? owner_fp : actor_fp;
    ev.actor.display_name = activity_user_display_name_local(users, ev.actor.user_id);
    ev.actor.kind = "user";

    ev.event_type = event_type;
    ev.scope_type = "security";

    // Deliberately do not store device_id/session_id/tokens/full fingerprints in Activity.
    ev.scope_id = "";

    ev.target_kind = target_kind.empty() ? "security" : target_kind;
    ev.target_name = target_name.empty() ? ev.target_kind : target_name;
    ev.target_path = "";

    ev.details = details.is_object() ? details : nlohmann::json::object();
    ev.details["scope_type"] = "security";

    std::string activity_err;
    (void)pqnas::activity::record_user_activity(user_root, ev, &activity_err);
}

static std::string activity_move_item_kind_local(const std::string& item_type) {
    if (item_type == "dir") return "folder";
    if (item_type == "folder") return "folder";
    if (item_type == "file") return "file";
    if (!item_type.empty()) return item_type;
    return "item";
}

static void record_user_file_moved_activity_best_effort_local(pqnas::UsersRegistry& users,
                                                              const std::string& actor_fp,
                                                              const std::string& from_rel_path,
                                                              const std::string& to_rel_path,
                                                              const std::string& item_type,
                                                              std::uint64_t size_bytes,
                                                              const httplib::Request* req = nullptr) {
    if (actor_fp.empty() || from_rel_path.empty() || to_rel_path.empty()) return;

    std::filesystem::path user_root;
    try {
        user_root = user_dir_for_fp(users, actor_fp);
    } catch (...) {
        return;
    }

    if (user_root.empty()) return;

    const std::string kind = activity_move_item_kind_local(item_type);

    pqnas::activity::ActivityEvent ev;
    ev.owner_user_id = actor_fp;

    ev.actor = activity_actor_for_request_local(req, users, actor_fp);

    ev.event_type = "file.moved";
    ev.scope_type = "user";
    ev.scope_id = actor_fp;

    ev.target_kind = kind;
    ev.target_path = to_rel_path;
    ev.target_name = activity_basename_local(to_rel_path, kind);

    ev.details = json{
        {"scope_type", "user"},
        {"from_path", from_rel_path},
        {"to_path", to_rel_path},
        {"item_type", kind},
        {"size_bytes", size_bytes}
    };

    std::string activity_err;
    (void)pqnas::activity::record_user_activity(user_root, ev, &activity_err);
}

static void record_user_file_copied_activity_best_effort_local(pqnas::UsersRegistry& users,
                                                               const std::string& actor_fp,
                                                               const std::string& from_rel_path,
                                                               const std::string& to_rel_path,
                                                               const std::string& item_type,
                                                               std::uint64_t size_bytes,
                                                               const httplib::Request* req = nullptr) {
    if (actor_fp.empty() || from_rel_path.empty() || to_rel_path.empty()) return;

    std::filesystem::path user_root;
    try {
        user_root = user_dir_for_fp(users, actor_fp);
    } catch (...) {
        return;
    }

    if (user_root.empty()) return;

    const std::string kind = activity_move_item_kind_local(item_type);

    pqnas::activity::ActivityEvent ev;
    ev.owner_user_id = actor_fp;

    ev.actor = activity_actor_for_request_local(req, users, actor_fp);

    ev.event_type = "file.copied";
    ev.scope_type = "user";
    ev.scope_id = actor_fp;

    ev.target_kind = kind;
    ev.target_path = to_rel_path;
    ev.target_name = activity_basename_local(to_rel_path, kind);

    ev.details = json{
        {"scope_type", "user"},
        {"from_path", from_rel_path},
        {"to_path", to_rel_path},
        {"item_type", kind},
        {"size_bytes", size_bytes}
    };

    std::string activity_err;
    (void)pqnas::activity::record_user_activity(user_root, ev, &activity_err);
}

static std::string activity_share_kind_local(const std::string& share_type) {
    if (share_type == "dir") return "folder";
    if (share_type == "file") return "file";
    if (share_type == "album") return "album";
    if (!share_type.empty()) return share_type;
    return "share";
}

static void record_share_activity_best_effort_local(pqnas::UsersRegistry& users,
                                                    const std::string& actor_fp,
                                                    const std::string& event_type,
                                                    const pqnas::ShareLink& share) {
    if (actor_fp.empty() || event_type.empty() || share.owner_fp.empty()) return;

    std::filesystem::path user_root;
    try {
        user_root = user_dir_for_fp(users, share.owner_fp);
    } catch (...) {
        return;
    }

    if (user_root.empty()) return;

    const std::string share_kind = activity_share_kind_local(share.type);
    const std::string scope_kind = share.scope_kind.empty() ? "user" : share.scope_kind;

    pqnas::activity::ActivityEvent ev;
    ev.owner_user_id = share.owner_fp;

    ev.actor.user_id = actor_fp;
    ev.actor.display_name = activity_user_display_name_local(users, actor_fp);
    ev.actor.kind = "user";

    ev.event_type = event_type;

    // Deliberately do not store token/share_id/workspace_id in Activity.
    ev.scope_type = "share";
    ev.scope_id = "";

    ev.target_kind = share_kind;
    ev.target_path = share.path;
    ev.target_name = activity_basename_local(share.path, share_kind);

    ev.details = json{
            {"scope_type", scope_kind},
            {"share_kind", share_kind},
            {"path", share.path}
    };

    if (!share.expires_at.empty()) {
        ev.details["expires_at"] = share.expires_at;
    }

    std::string activity_err;
    (void)pqnas::activity::record_user_activity(user_root, ev, &activity_err);
}

namespace {


static std::string iso_utc_from_filetime(const std::filesystem::file_time_type& ft) {
    try {
        using namespace std::chrono;

        auto sctp = time_point_cast<system_clock::duration>(
            ft - std::filesystem::file_time_type::clock::now()
            + system_clock::now()
        );

        std::time_t tt = system_clock::to_time_t(sctp);

        std::tm tm{};
#if defined(_WIN32)
        gmtime_s(&tm, &tt);
#else
        gmtime_r(&tt, &tm);
#endif

        char buf[64];
        const int n = std::snprintf(
            buf,
            sizeof(buf),
            "%04d-%02d-%02dT%02d:%02d:%02dZ",
            tm.tm_year + 1900,
            tm.tm_mon + 1,
            tm.tm_mday,
            tm.tm_hour,
            tm.tm_min,
            tm.tm_sec
        );

        if (n <= 0) {
            return "—";
        }
        if (n >= (int)sizeof(buf)) {
            return std::string(buf, buf + (sizeof(buf) - 1));
        }

        return std::string(buf, buf + n);
    } catch (...) {
        return "—";
    }
}



struct ArchivePair {
    std::string jsonl_path;
    std::string state_path; // optional
    std::string name;       // filename
    long long size_bytes = 0; // jsonl + state (if present)
    std::filesystem::file_time_type mtime{};
};

static std::vector<ArchivePair> list_rotated_archives_local(const std::string& audit_jsonl_path) {
    std::vector<ArchivePair> out;

    const std::filesystem::path active(audit_jsonl_path);
    const std::filesystem::path dir = active.parent_path();

    const std::string active_name = active.filename().string(); // pqnas_audit.jsonl
    const std::string prefix = "pqnas_audit-";
    const std::string jsonl_ext = ".jsonl";
    const std::string state_ext = ".state";

    std::error_code ec;
    for (auto& de : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!de.is_regular_file()) continue;

        const auto p = de.path();
        const std::string fn = p.filename().string();
        if (fn == active_name) continue;

        if (fn.rfind(prefix, 0) != 0) continue;
        if (fn.size() <= prefix.size() + jsonl_ext.size()) continue;
        if (fn.substr(fn.size() - jsonl_ext.size()) != jsonl_ext) continue;

        const std::string id = fn.substr(prefix.size(), fn.size() - prefix.size() - jsonl_ext.size());
        if (id.empty()) continue;

        ArchivePair ap;
        ap.jsonl_path = p.string();
        ap.name = fn;

        ap.size_bytes = file_size_bytes_safe(ap.jsonl_path);
        if (ap.size_bytes < 0) ap.size_bytes = 0;

        const std::filesystem::path st = dir / (prefix + id + state_ext);
        if (std::filesystem::exists(st)) {
            ap.state_path = st.string();
            long long s2 = file_size_bytes_safe(ap.state_path);
            if (s2 > 0) ap.size_bytes += s2;
        }

        std::error_code ec2;
        ap.mtime = std::filesystem::last_write_time(p, ec2);
        if (ec2) ap.mtime = std::filesystem::file_time_type::clock::now();

        out.push_back(std::move(ap));
    }

    std::sort(out.begin(), out.end(), [](const ArchivePair& a, const ArchivePair& b) {
        return a.mtime > b.mtime; // newest first
    });

    return out;
}

    static bool reply_quota_error_v1(httplib::Response& res,
                                    const std::string& fp_hex,
                                    const pqnas::QuotaCheckResult& qc) {
    if (qc.ok) return false;

    if (qc.error == "storage_unallocated") {
        reply_json(res, 403, json{
            {"ok", false},
            {"error", "storage_unallocated"},
            {"message", "Storage not allocated"},
            {"fingerprint_hex", fp_hex},
            {"quota_bytes", qc.quota_bytes},
            {"incoming_bytes", qc.incoming_bytes}
        }.dump());
        return true;
    }
    if (qc.error == "invalid_path") {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid path"}
        }.dump());
        return true;
    }
    if (qc.error == "quota_exceeded") {
        reply_json(res, 413, json{
            {"ok", false},
            {"error", "quota_exceeded"},
            {"message", "User quota exceeded"},
            {"fingerprint_hex", fp_hex},
            {"used_bytes", qc.used_bytes},
            {"quota_bytes", qc.quota_bytes},
            {"incoming_bytes", qc.incoming_bytes},
            {"existing_bytes", qc.existing_bytes},
            {"would_used_bytes", qc.would_used_bytes},
            {"free_bytes", (qc.quota_bytes > qc.used_bytes ? (qc.quota_bytes - qc.used_bytes) : 0)}
        }.dump());
        return true;
    }

    reply_json(res, 403, json{
        {"ok", false},
        {"error", "forbidden"},
        {"message", "policy denied"}
    }.dump());
    return true;
}

static nlohmann::json normalize_retention_or_default_local(const nlohmann::json& in_ret) {
    nlohmann::json ret = nlohmann::json::object();
    if (in_ret.is_object()) ret = in_ret;

    auto get_mode = [&]() -> std::string {
        if (ret.contains("mode") && ret["mode"].is_string()) return ret["mode"].get<std::string>();
        return "never";
    };
    auto clamp_int = [&](const char* k, int def, int lo, int hi) -> int {
        if (!ret.contains(k) || ret[k].is_null()) return def;
        if (!ret[k].is_number_integer()) return def;
        int v = ret[k].get<int>();
        if (v < lo) v = lo;
        if (v > hi) v = hi;
        return v;
    };

    std::string mode = get_mode();
    if (!(mode == "never" || mode == "days" || mode == "files" || mode == "size_mb")) mode = "never";

    const int days = clamp_int("days", 90, 1, 3650);
    const int max_files = clamp_int("max_files", 50, 1, 50000);
    const int max_total_mb = clamp_int("max_total_mb", 20480, 1, 10000000);

    return nlohmann::json{
        {"mode", mode},
        {"days", days},
        {"max_files", max_files},
        {"max_total_mb", max_total_mb},
    };
}

static nlohmann::json build_preview_local(const std::vector<ArchivePair>& archives, const nlohmann::json& policy) {
    const std::string mode = policy.value("mode", "never");
    const int days = policy.value("days", 90);
    const int max_files = policy.value("max_files", 50);
    const long long max_bytes = (long long)policy.value("max_total_mb", 20480) * 1024LL * 1024LL;

    long long total_bytes = 0;
    for (const auto& a : archives) total_bytes += std::max(0LL, a.size_bytes);

    std::vector<nlohmann::json> candidates;
    long long cand_bytes = 0;

    if (mode == "never") {
        // nothing
    } else if (mode == "files") {
        for (size_t i = 0; i < archives.size(); i++) {
            if ((int)i < max_files) continue;
            const auto& a = archives[i];
            candidates.push_back({
                {"name", a.name},
                {"size_bytes", a.size_bytes},
                {"mtime_iso", iso_utc_from_filetime(a.mtime)},
                {"reason", "exceeds max_files"}
            });
            cand_bytes += std::max(0LL, a.size_bytes);
        }
    } else if (mode == "days") {
        using namespace std::chrono;
        const auto now = std::filesystem::file_time_type::clock::now();
        const auto cutoff = now - hours(24 * days);

        for (const auto& a : archives) {
            if (a.mtime >= cutoff) continue;
            candidates.push_back({
                {"name", a.name},
                {"size_bytes", a.size_bytes},
                {"mtime_iso", iso_utc_from_filetime(a.mtime)},
                {"reason", "older than days"}
            });
            cand_bytes += std::max(0LL, a.size_bytes);
        }
    } else if (mode == "size_mb") {
        long long kept = 0;
        for (const auto& a : archives) {
            const long long sz = std::max(0LL, a.size_bytes);
            if (kept + sz <= max_bytes) {
                kept += sz;
                continue;
            }
            candidates.push_back({
                {"name", a.name},
                {"size_bytes", a.size_bytes},
                {"mtime_iso", iso_utc_from_filetime(a.mtime)},
                {"reason", "exceeds max_total_mb"}
            });
            cand_bytes += sz;
        }
    }

    nlohmann::json summary = {
        {"candidate_files", (int)candidates.size()},
        {"candidate_bytes", cand_bytes},
        {"total_archives", (int)archives.size()},
        {"total_bytes", total_bytes},
    };

    return nlohmann::json{
        {"ok", true},
        {"candidates", candidates},
        {"summary", summary}
    };
}

} // namespace


static void pending_pop(const std::string& sid) {
    std::lock_guard<std::mutex> lk(g_pending_mu);
    g_pending.erase(sid);
}

static void approvals_prune(long now) {
    std::lock_guard<std::mutex> lk(g_approvals_mu);
    for (auto it = g_approvals.begin(); it != g_approvals.end();) {
        if (now > it->second.expires_at) it = g_approvals.erase(it);
        else ++it;
    }
}

static void approvals_put(const std::string& sid, const ApprovalEntry& e) {
    std::lock_guard<std::mutex> lk(g_approvals_mu);
    g_approvals[sid] = e;
}

static bool approvals_get(const std::string& sid, ApprovalEntry& out) {
    std::lock_guard<std::mutex> lk(g_approvals_mu);
    auto it = g_approvals.find(sid);
    if (it == g_approvals.end()) return false;
    out = it->second;
    return true;
}

static void approvals_pop(const std::string& sid) {
    std::lock_guard<std::mutex> lk(g_approvals_mu);
    g_approvals.erase(sid);
}

// Return directory that contains the running executable
static std::string exe_dir() {
    char buf[PATH_MAX] = {0};
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return ".";
    std::string p(buf, (size_t)n);
    return std::filesystem::path(p).parent_path().string();
}

// Decode standard base64 (with padding) -> bytes

// ============================================================================
//  AUTH / SESSION / COOKIE HELPERS
// ============================================================================

static bool b64std_decode_to_bytes(const std::string& in, std::string& out) {
    out.clear();
    out.resize(in.size() * 3 / 4 + 8);
    size_t out_len = 0;
    if (sodium_base642bin(reinterpret_cast<unsigned char*>(out.data()), out.size(),
                          in.c_str(), in.size(),
                          nullptr, &out_len, nullptr,
                          sodium_base64_VARIANT_ORIGINAL) != 0) {
        return false;
    }
    out.resize(out_len);
    return true;
}

// -----------------------------------------------------------------------------
// Shared helpers needed by verify_v4_shared.cc
// These MUST be link-visible (not static) and stable.
// -----------------------------------------------------------------------------
namespace pqnas {

// URL-safe base64 without padding
[[maybe_unused]] std::string b64url_enc_local(const unsigned char* data, size_t len) {
    size_t outLen = sodium_base64_encoded_len(len, sodium_base64_VARIANT_URLSAFE_NO_PADDING);
    std::string out(outLen, '\0');
    sodium_bin2base64(out.data(), out.size(), data, len, sodium_base64_VARIANT_URLSAFE_NO_PADDING);
    out.resize(std::strlen(out.c_str()));
    return out;
}

// Native PQ verifier loader symbol signature (libdna_lib.so)
using qgp_dsa87_verify_fn = int (*)(const uint8_t* sig, size_t siglen,
                                   const uint8_t* msg, size_t msglen,
                                   const uint8_t* pk);

} // namespace pqnas


static bool ensure_no_symlink_in_existing_path_prefix(const std::filesystem::path& abs_path,
                                                      std::string* err = nullptr) {
    if (err) err->clear();

    const std::filesystem::path p = abs_path.lexically_normal();
    if (p.empty()) {
        if (err) *err = "empty path";
        return false;
    }

    std::filesystem::path cur;
    for (const auto& part : p) {
        cur /= part;

        std::error_code ec;
        auto st = std::filesystem::symlink_status(cur, ec);

        if (ec) {
            if (ec == std::make_error_code(std::errc::no_such_file_or_directory)) {
                return true;
            }
            if (err) *err = "symlink_status failed: " + ec.message();
            return false;
        }

        if (!std::filesystem::exists(st)) {
            return true;
        }

        if (std::filesystem::is_symlink(st)) {
            if (err) *err = "symlink not allowed: " + cur.string();
            return false;
        }
    }

    return true;
}
// -----------------------------------------------------------------------------
// Server-local helpers
// -----------------------------------------------------------------------------
static bool read_file_to_string(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::in | std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// --- Static files (generic) --------------------------------------------------
// Serve arbitrary assets from server/src/static at /static/<file>.
// This is intentionally scoped + safe:
// - No path traversal
// - Fail-closed: only allows known extensions
// - No impact on crypto/auth/audit logic
static bool is_safe_static_relpath(const std::string& rel) {
    if (rel.empty()) return false;
    if (rel.find('\0') != std::string::npos) return false;

    // No absolute paths, no traversal, no backslashes (Windows), no "//"
    if (rel[0] == '/' || rel.find("..") != std::string::npos) return false;
    if (rel.find('\\') != std::string::npos) return false;
    if (rel.find("//") != std::string::npos) return false;

    // Only allow plain filenames or subdirs (static/img/foo.png etc)
    // Keep it simple: only [A-Za-z0-9._-/]
    for (char c : rel) {
        const bool ok =
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '.' || c == '_' || c == '-' || c == '/' ;
        if (!ok) return false;
    }

    return true;
}

static std::string mime_for_ext(std::string ext) {
    // ext must be lowercase and include dot (".png")
    if (ext == ".html")  return "text/html; charset=utf-8";
    if (ext == ".js")    return "application/javascript; charset=utf-8";
    if (ext == ".wasm")  return "application/wasm";
    if (ext == ".css")   return "text/css; charset=utf-8";
    if (ext == ".json")  return "application/json; charset=utf-8";
    if (ext == ".svg")   return "image/svg+xml; charset=utf-8";
    if (ext == ".png")   return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".webp")  return "image/webp";
    if (ext == ".gif")   return "image/gif";
    if (ext == ".ico")   return "image/x-icon";
    if (ext == ".woff")  return "font/woff";
    if (ext == ".woff2") return "font/woff2";
    if (ext == ".ttf")   return "font/ttf";
    return "";
}

static bool has_allowed_static_ext(const std::filesystem::path& p) {
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return (char)std::tolower(c); });
    return !mime_for_ext(ext).empty();
}


static std::string b64url_enc(const unsigned char* data, size_t len) {
    size_t outLen = sodium_base64_encoded_len(len, sodium_base64_VARIANT_URLSAFE_NO_PADDING);
    std::string out(outLen, '\0');
    sodium_bin2base64(out.data(), out.size(), data, len, sodium_base64_VARIANT_URLSAFE_NO_PADDING);
    out.resize(std::strlen(out.c_str()));
    return out;
}

static std::string url_encode(const std::string& s) {
    static const char *hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back((char)c);
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 15]);
        }
    }
    return out;
}

[[maybe_unused]] static std::string trim_nl(std::string s) {
    while (!s.empty() && (s.back()=='\n' || s.back()=='\r')) s.pop_back();
    return s;
}

[[maybe_unused]] static std::string to_lower_copy(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}


static std::string trim_ws(std::string s) {
    while (!s.empty() && (s.front()==' ' || s.front()=='\t')) s.erase(s.begin());
    while (!s.empty() && (s.back()==' ' || s.back()=='\t')) s.pop_back();
    return s;
}

static std::string header_value(const httplib::Request& req, const char* name) {
    auto it = req.headers.find(name);
    return (it == req.headers.end()) ? "" : it->second;
}

static std::string audit_safe_header_value(const std::string& raw, std::size_t max_len = 512) {
    std::string out;
    out.reserve(std::min(raw.size(), max_len));

    for (unsigned char c : raw) {
        if (out.size() >= max_len) break;

        if (c < 0x20 || c == 0x7f) {
            out.push_back(' ');
        } else {
            out.push_back(static_cast<char>(c));
        }
    }

    return out;
}

static bool parse_ipv4_u32(const std::string& ip_raw, std::uint32_t& out) {
    std::string ip = trim_ws(ip_raw);

    const std::string mapped = "::ffff:";
    if (ip.rfind(mapped, 0) == 0) {
        ip = ip.substr(mapped.size());
    }

    std::uint32_t parts[4] = {0, 0, 0, 0};
    std::size_t start = 0;

    for (int i = 0; i < 4; ++i) {
        const std::size_t dot = (i == 3) ? std::string::npos : ip.find('.', start);
        const std::size_t end = (i == 3) ? ip.size() : dot;

        if (end == std::string::npos || end <= start) return false;

        std::uint32_t v = 0;
        for (std::size_t p = start; p < end; ++p) {
            const char c = ip[p];
            if (c < '0' || c > '9') return false;
            v = (v * 10u) + static_cast<std::uint32_t>(c - '0');
            if (v > 255u) return false;
        }

        parts[i] = v;

        if (i < 3) {
            if (dot == std::string::npos) return false;
            start = dot + 1;
        }
    }

    out = (parts[0] << 24u) | (parts[1] << 16u) | (parts[2] << 8u) | parts[3];
    return true;
}

static bool ipv4_in_cidr(const std::string& ip, const std::string& cidr) {
    const std::size_t slash = cidr.find('/');
    if (slash == std::string::npos) return false;

    const std::string net_s = trim_ws(cidr.substr(0, slash));
    const std::string bits_s = trim_ws(cidr.substr(slash + 1));
    if (bits_s.empty()) return false;

    int bits = 0;
    for (char c : bits_s) {
        if (c < '0' || c > '9') return false;
        bits = (bits * 10) + (c - '0');
        if (bits > 32) return false;
    }

    std::uint32_t ip_u = 0;
    std::uint32_t net_u = 0;
    if (!parse_ipv4_u32(ip, ip_u) || !parse_ipv4_u32(net_s, net_u)) return false;

    const std::uint32_t mask =
        (bits == 0) ? 0u : (0xffffffffu << static_cast<unsigned>(32 - bits));

    return (ip_u & mask) == (net_u & mask);
}

static bool trusted_proxy_addr(const std::string& remote_raw) {
    const char* env = std::getenv("PQNAS_TRUSTED_PROXIES");
    if (!env || !*env) return false;

    const std::string remote = trim_ws(remote_raw);
    const std::string list(env);

    std::uint32_t remote_u = 0;
    const bool remote_is_v4 = parse_ipv4_u32(remote, remote_u);

    std::size_t start = 0;
    while (start < list.size()) {
        std::size_t end = list.find(',', start);
        if (end == std::string::npos) end = list.size();

        const std::string rule = trim_ws(list.substr(start, end - start));
        if (!rule.empty()) {
            if (remote == rule) return true;

            std::uint32_t rule_u = 0;
            if (remote_is_v4 && parse_ipv4_u32(rule, rule_u) && remote_u == rule_u) {
                return true;
            }

            if (ipv4_in_cidr(remote, rule)) return true;
        }

        start = end + 1;
    }

    return false;
}

static std::string first_xff_ip(const std::string& xff) {
    const std::size_t comma = xff.find(',');
    return trim_ws((comma == std::string::npos) ? xff : xff.substr(0, comma));
}

static std::string client_ip(const httplib::Request& req) {
    const std::string remote = req.remote_addr.empty() ? "?" : trim_ws(req.remote_addr);

    if (trusted_proxy_addr(remote)) {
        const std::string cf = trim_ws(header_value(req, "CF-Connecting-IP"));
        if (!cf.empty()) return cf;

        const std::string xff = first_xff_ip(header_value(req, "X-Forwarded-For"));
        if (!xff.empty()) return xff;
    }

    return remote;
}

static bool simple_ip_rate_limit_allow(
    const std::string& bucket,
    const std::string& ip,
    int max_hits,
    std::chrono::seconds window
) {
    using Clock = std::chrono::steady_clock;

    static std::mutex mu;
    static std::unordered_map<std::string, std::deque<Clock::time_point>> hits;

    const auto now = Clock::now();
    const auto cutoff = now - window;
    const std::string key = bucket + "\n" + (ip.empty() ? "?" : ip);

    std::lock_guard<std::mutex> lock(mu);

    auto& q = hits[key];
    while (!q.empty() && q.front() < cutoff) {
        q.pop_front();
    }

    if ((int)q.size() >= max_hits) {
        return false;
    }

    q.push_back(now);

    // Opportunistic cleanup to avoid unbounded map growth.
    if (hits.size() > 8192) {
        for (auto it = hits.begin(); it != hits.end(); ) {
            auto& v = it->second;
            while (!v.empty() && v.front() < cutoff) {
                v.pop_front();
            }
            if (v.empty()) {
                it = hits.erase(it);
            } else {
                ++it;
            }
        }
    }

    return true;
}

static void set_rate_limited_json(httplib::Response& res, const std::string& error) {
    res.status = 429;
    res.set_header("Retry-After", "60");
    res.set_content(json{
        {"ok", false},
        {"error", error}
    }.dump(), "application/json; charset=utf-8");
}
static std::string extract_named_cookie_value(const std::string& cookie_header,
                                              const std::string& name) {
    if (cookie_header.empty() || name.empty()) return {};

    std::size_t pos = 0;

    while (pos < cookie_header.size()) {
        while (pos < cookie_header.size() &&
               (cookie_header[pos] == ' ' ||
                cookie_header[pos] == '\t' ||
                cookie_header[pos] == ';')) {
                ++pos;
                }

        const std::size_t eq = cookie_header.find('=', pos);
        if (eq == std::string::npos) break;

        std::size_t key_end = eq;
        while (key_end > pos &&
               (cookie_header[key_end - 1] == ' ' ||
                cookie_header[key_end - 1] == '\t')) {
                --key_end;
                }

        const std::string key = cookie_header.substr(pos, key_end - pos);

        const std::size_t value_start = eq + 1;
        const std::size_t semi = cookie_header.find(';', value_start);

        const std::string value =
            (semi == std::string::npos)
                ? cookie_header.substr(value_start)
                : cookie_header.substr(value_start, semi - value_start);

        if (key == name) return value;

        if (semi == std::string::npos) break;
        pos = semi + 1;
    }

    return {};
}
// ----- Cookie gate: user OR admin (UsersRegistry policy) ---------------------
// Mirrors /api/v4/me logic, but reusable for page + API gating.
// Returns actor_fp_hex + role ("admin"|"user") on success.
static bool require_user_cookie_users_actor(
    const httplib::Request& req,
    httplib::Response& res,
    const unsigned char cookie_key[32],
    pqnas::UsersRegistry* users,
    std::string* out_fp_hex,
    std::string* out_role
) {
    if (out_fp_hex) out_fp_hex->clear();
    if (out_role) out_role->clear();

    auto it = req.headers.find("Cookie");
    if (it == req.headers.end()) {
        reply_json(res, 401, json({{"ok",false},{"error","unauthorized"},{"message","missing cookie"}}).dump());
        return false;
    }

    const std::string& hdr = it->second;
    const std::string cookieVal = extract_named_cookie_value(hdr, "pqnas_session");
    if (cookieVal.empty()) {
        reply_json(res, 401, json({{"ok",false},{"error","unauthorized"},{"message","missing pqnas_session"}}).dump());
        return false;
    }

    std::string fp_b64;
    long exp = 0;
    if (!session_cookie_verify(cookie_key, cookieVal, fp_b64, exp)) {
        reply_json(res, 401, json({{"ok",false},{"error","unauthorized"},{"message","invalid session"}}).dump());
        return false;
    }

    long now = pqnas::now_epoch();
    if (now > exp) {
        reply_json(res, 401, json({{"ok",false},{"error","unauthorized"},{"message","session expired"}}).dump());
        return false;
    }

    // Cookie stores *standard* base64 of UTF-8 fingerprint hex string
    std::string fp_hex;
    {
        std::string raw;
        if (!b64std_decode_to_bytes(fp_b64, raw)) {
            reply_json(res, 401, json({{"ok",false},{"error","unauthorized"},{"message","invalid session"}}).dump());
            return false;
        }
        fp_hex.assign(raw.begin(), raw.end());
    }

    // Users policy (fail-closed)
    const bool is_admin = users && users->is_admin_enabled(fp_hex);
    const bool is_user  = users && (users->is_enabled_user(fp_hex) || is_admin);

    if (!is_user) {
        reply_json(res, 403, json({{"ok",false},{"error","forbidden"},{"message","policy denied"}}).dump());
        return false;
    }

    if (out_fp_hex) *out_fp_hex = fp_hex;
    if (out_role) *out_role = is_admin ? "admin" : "user";
    return true;
}

// ----- Bearer gate: user OR admin (AppTokenStore + UsersRegistry policy) -----
// Mobile/API auth path. Accepts Authorization: Bearer <access_token>.
// Returns false silently if there is no usable bearer auth, so caller can
// fall back to cookie auth.
// Returns actor_fp_hex + role ("admin"|"user") on success.
static bool try_user_bearer_users_actor(
    const httplib::Request& req,
    httplib::Response& res,
    pqnas::UsersRegistry* users,
    std::string* out_fp_hex,
    std::string* out_role
) {
    if (out_fp_hex) out_fp_hex->clear();
    if (out_role) out_role->clear();

    auto it = req.headers.find("Authorization");
    if (it == req.headers.end()) {
        return false;
    }

    const std::string& hdr = it->second;
    const std::string prefix = "Bearer ";
    if (hdr.size() <= prefix.size() || hdr.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }

    const std::string raw_token = hdr.substr(prefix.size());
    if (raw_token.empty()) {
        return false;
    }

    std::string fp_hex;
    std::string token_role;
    std::string device_id;
    std::string terr;
    if (!g_app_tokens.verify_access_token(raw_token, &fp_hex, &token_role, &device_id, &terr)) {
        std::cerr << "[bearer-auth] verify_access_token failed: "
                  << (terr.empty() ? "unknown" : terr) << "\n";
        reply_json(res, 401, json({
            {"ok", false},
            {"error", "unauthorized"},
            {"message", terr.empty() ? "invalid access token" : terr}
        }).dump());
        return false;
    }

    if (!users) {
        reply_json(res, 403, json({{"ok",false},{"error","forbidden"},{"message","policy denied"}}).dump());
        return false;
    }

    auto uopt = users->get(fp_hex);
    if (!uopt.has_value()) {
        reply_json(res, 403, json({{"ok",false},{"error","forbidden"},{"message","policy denied"}}).dump());
        return false;
    }

    const auto& u = *uopt;
    const bool is_admin = (u.role == "admin" && u.status == "enabled");
    const bool is_user  = (u.status == "enabled") || is_admin;

    if (!is_user) {
        reply_json(res, 403, json({{"ok",false},{"error","forbidden"},{"message","policy denied"}}).dump());
        return false;
    }

    if (out_fp_hex) *out_fp_hex = fp_hex;
    if (out_role) *out_role = is_admin ? "admin" : "user";
    return true;
}

// ----- Mixed gate: Bearer first, then cookie -------------------------------
// For routes that should accept either mobile bearer auth or browser cookie auth.
static bool require_user_auth_users_actor(
    const httplib::Request& req,
    httplib::Response& res,
    const unsigned char cookie_key[32],
    pqnas::UsersRegistry* users,
    std::string* out_fp_hex,
    std::string* out_role
) {
    if (out_fp_hex) out_fp_hex->clear();
    if (out_role) out_role->clear();

    auto it = req.headers.find("Authorization");
    const bool has_authz = (it != req.headers.end());
    const bool has_bearer =
        has_authz &&
        it->second.size() > 7 &&
        it->second.compare(0, 7, "Bearer ") == 0;

    if (has_bearer) {
        return try_user_bearer_users_actor(req, res, users, out_fp_hex, out_role);
    }

    return require_user_cookie_users_actor(req, res, cookie_key, users, out_fp_hex, out_role);
}

// ----- Mixed admin gate: Bearer first, then browser cookie ------------------
// Android/mobile uses Authorization: Bearer.
// Browser admin pages use pqnas_session cookie.
// This keeps browser behavior unchanged while allowing enabled admin app tokens.
static bool require_admin_auth_users_actor(
    const httplib::Request& req,
    httplib::Response& res,
    const unsigned char cookie_key[32],
    const std::string& users_path,
    pqnas::UsersRegistry* users,
    std::string* out_fp_hex
) {
    if (out_fp_hex) out_fp_hex->clear();

    auto it = req.headers.find("Authorization");
    const bool has_bearer =
        it != req.headers.end() &&
        it->second.size() > 7 &&
        it->second.compare(0, 7, "Bearer ") == 0;

    if (has_bearer) {
        std::string fp_hex;
        std::string role;

        if (!try_user_bearer_users_actor(req, res, users, &fp_hex, &role)) {
            return false;
        }

        if (role != "admin") {
            reply_json(
                res,
                403,
                json({
                    {"ok", false},
                    {"error", "forbidden"},
                    {"message", "admin required"}
                }).dump()
            );
            return false;
        }

        if (out_fp_hex) *out_fp_hex = fp_hex;
        return true;
    }

    return require_admin_cookie_users_actor(
        req,
        res,
        cookie_key,
        users_path,
        users,
        out_fp_hex
    );
}

static std::string random_b64url(size_t nbytes) {
    std::string b(nbytes, '\0');
    randombytes_buf(b.data(), b.size());
    return b64url_enc(reinterpret_cast<const unsigned char*>(b.data()), b.size());
}

static void reply_json(httplib::Response& res, int code, const std::string& body_json) {
    res.status = code;
    res.set_header("Content-Type", "application/json");
    res.body = body_json;
}

static std::string qr_svg_from_text(const std::string& text,
                                   int module_px = 6,
                                   int margin_modules = 4) {
    QRcode* qr = QRcode_encodeString8bit(text.c_str(), 0, QR_ECLEVEL_M);
    if (!qr) throw std::runtime_error("QRcode_encodeString8bit failed");

    const int w = qr->width;
    const unsigned char* d = qr->data;

    const int size = (w + margin_modules * 2) * module_px;
    std::string svg;
    svg.reserve((size_t)size * 10);

    svg += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    svg += "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\"";
    svg += " width=\"" + std::to_string(size) + "\" height=\"" + std::to_string(size) + "\"";
    svg += " viewBox=\"0 0 " + std::to_string(size) + " " + std::to_string(size) + "\">\n";
    svg += "<rect width=\"100%\" height=\"100%\" fill=\"white\"/>\n";

    for (int y = 0; y < w; y++) {
        for (int x = 0; x < w; x++) {
            const int idx = y * w + x;
            const bool dark = (d[idx] & 1) != 0;
            if (!dark) continue;

            const int xx = (x + margin_modules) * module_px;
            const int yy = (y + margin_modules) * module_px;

            svg += "<rect x=\"" + std::to_string(xx) + "\" y=\"" + std::to_string(yy) + "\"";
            svg += " width=\"" + std::to_string(module_px) + "\" height=\"" + std::to_string(module_px) + "\"";
            svg += " fill=\"black\"/>\n";
        }
    }

    svg += "</svg>\n";
    QRcode_free(qr);
    return svg;
}

static bool load_env_key(const char* name, unsigned char* out, size_t outLenExpected) {
    const char* s = std::getenv(name);
    if (!s) return false;
    size_t out_len = 0;
    if (sodium_base642bin(out, outLenExpected, s, std::strlen(s),
                          nullptr, &out_len, nullptr,
                          sodium_base64_VARIANT_URLSAFE_NO_PADDING) != 0) return false;
    return out_len == outLenExpected;
}

// -----------------------------------------------------------------------------
// Build ST payload canonical JSON (string-built to lock order, matching Python)
// -----------------------------------------------------------------------------
static std::string build_req_payload_canonical(
    const std::string& sid,
    const std::string& chal,
    const std::string& nonce,
    long issued_at,
    long expires_at
) {
    std::string rp = pqnas::lower_ascii(RP_ID);
    std::string rp_id_hash = pqnas::sha256_b64_std_str(rp);

    return std::string("{")
        + "\"aud\":\"" + AUD + "\","
        + "\"chal\":\"" + chal + "\","
        + "\"expires_at\":" + std::to_string(expires_at) + ","
        + "\"issued_at\":" + std::to_string(issued_at) + ","
        + "\"iss\":\"" + ISS + "\","
        + "\"nonce\":\"" + nonce + "\","
        + "\"origin\":\"" + ORIGIN + "\","
        + "\"rp_id\":\"" + RP_ID + "\","
        + "\"rp_id_hash\":\"" + rp_id_hash + "\","
        + "\"scope\":\"" + SCOPE + "\","
        + "\"sid\":\"" + sid + "\","
        + "\"typ\":\"st\","
        + "\"v\":4"
        + "}";
}


static std::string slurp_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.good()) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::string trim_copy_for_tls_pin(const std::string& s) {
    std::size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;

    std::size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;

    return s.substr(a, b - a);
}

static bool normalize_tls_spki_sha256_pin_for_qr(
    const std::string& raw,
    std::string* out,
    std::string* err
) {
    if (out) out->clear();
    if (err) err->clear();

    const std::string v = trim_copy_for_tls_pin(raw);
    static constexpr const char* kPrefix = "sha256/";

    if (v.rfind(kPrefix, 0) != 0) {
        if (err) *err = "TLS pin must start with sha256/";
        return false;
    }

    const std::string b64 = v.substr(std::strlen(kPrefix));
    if (b64.empty()) {
        if (err) *err = "TLS pin base64 payload is empty";
        return false;
    }

    unsigned char decoded[32]{};
    size_t decoded_len = 0;

    if (sodium_base642bin(
            decoded,
            sizeof(decoded),
            b64.c_str(),
            b64.size(),
            nullptr,
            &decoded_len,
            nullptr,
            sodium_base64_VARIANT_ORIGINAL
        ) != 0 ||
        decoded_len != sizeof(decoded)) {
        if (err) *err = "TLS pin must be standard base64 SHA-256 digest";
        return false;
    }

    std::string normalized_b64(
        sodium_base64_encoded_len(sizeof(decoded), sodium_base64_VARIANT_ORIGINAL),
        '\0'
    );

    sodium_bin2base64(
        normalized_b64.data(),
        normalized_b64.size(),
        decoded,
        sizeof(decoded),
        sodium_base64_VARIANT_ORIGINAL
    );

    if (out) *out = std::string(kPrefix) + std::string(normalized_b64.c_str());
    return true;
}

static std::string sign_req_token(const std::string& payload_json) {
    unsigned char sig[crypto_sign_BYTES];
    unsigned long long siglen = 0;

    crypto_sign_detached(
        sig, &siglen,
        reinterpret_cast<const unsigned char*>(payload_json.data()),
        (unsigned long long)payload_json.size(),
        SERVER_SK
    );

    std::string payload_b64 = b64url_enc(reinterpret_cast<const unsigned char*>(payload_json.data()), payload_json.size());
    std::string sig_b64     = b64url_enc(sig, crypto_sign_BYTES);
    return "v4." + payload_b64 + "." + sig_b64;
}

static std::string rel_to_repo(const std::string& abs) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path p = fs::weakly_canonical(fs::path(abs), ec);
    fs::path r = fs::weakly_canonical(fs::path(REPO_ROOT), ec);
    if (ec) return abs;

    auto ps = p.string();
    auto rs = r.string();
    if (ps.size() >= rs.size() && ps.compare(0, rs.size(), rs) == 0) {
        if (ps.size() == rs.size()) return ".";
        if (ps[rs.size()] == '/') return ps.substr(rs.size() + 1);
    }
    return abs; // fallback
}


static bool serve_file_under_root(const std::string& root_dir,
                                  const std::string& rel,
                                  const std::string& content_type,
                                  httplib::Response& res,
                                  bool no_store = true) {
    namespace fs = std::filesystem;

    // Build full path then canonicalize.
    std::error_code ec;
    fs::path root = fs::weakly_canonical(fs::path(root_dir), ec);
    if (ec) {
        res.status = 500;
        res.set_content("static root unavailable", "text/plain; charset=utf-8");
        return false;
    }

    fs::path full = fs::weakly_canonical(root / rel, ec);
    if (ec) {
        res.status = 404;
        res.set_content("not found", "text/plain; charset=utf-8");
        return false;
    }

    // Enforce: full must be under root
    auto root_s = root.string();
    auto full_s = full.string();
    if (full_s.size() < root_s.size() ||
        full_s.compare(0, root_s.size(), root_s) != 0 ||
        (full_s.size() > root_s.size() && full_s[root_s.size()] != '/')) {
        res.status = 403;
        res.set_content("forbidden", "text/plain; charset=utf-8");
        return false;
        }

    // Only serve regular files
    if (!fs::is_regular_file(full, ec) || ec) {
        res.status = 404;
        res.set_content("not found", "text/plain; charset=utf-8");
        return false;
    }

    const std::string body = slurp_file(full_s);
    if (body.empty()) {
        res.status = 404;
        res.set_content("not found", "text/plain; charset=utf-8");
        return false;
    }

    res.set_header("X-Content-Type-Options", "nosniff");
    if (no_store) res.set_header("Cache-Control", "no-store");
    res.set_content(body, content_type);
    return true;
}



static std::string sign_token_v4_ed25519(const json& payload_obj, const unsigned char sk[64]) {
    std::string payload = payload_obj.dump(-1, ' ', false, nlohmann::json::error_handler_t::strict);

    unsigned char sig[crypto_sign_BYTES];
    crypto_sign_detached(sig, nullptr,
                         reinterpret_cast<const unsigned char*>(payload.data()),
                         (unsigned long long)payload.size(),
                         sk);

    std::string p64 = b64url_enc(reinterpret_cast<const unsigned char*>(payload.data()), payload.size());
    std::string s64 = b64url_enc(sig, sizeof(sig));

    return std::string("v4.") + p64 + "." + s64;
}


// Small helper: bytes -> hex
static std::string hex_encode_lower(const unsigned char* data, size_t len) {
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.resize(len * 2);
    for (size_t i = 0; i < len; i++) {
        out[i * 2 + 0] = kHex[(data[i] >> 4) & 0xF];
        out[i * 2 + 1] = kHex[(data[i] >> 0) & 0xF];
    }
    return out;
}

#include <cstdint>
#include <vector>
#include <string>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <algorithm>

// ----------------------------- ZIP streaming (store, no compression) ----------
// We create a ZIP with local headers + data descriptors + central directory.
// No external libs required. CRC32 is computed per-file while streaming.
//
// Limitations:
// - Uses ZIP32 fields (no Zip64). Good for typical sizes; if you need >4GiB single file
//   or very large archives, we can extend to Zip64 later.
// ------------------------------------------------------------------------------

namespace {


// ============================================================================
//  ZIP STREAMING HELPERS
// ============================================================================

static inline void zip_u16(std::string& out, std::uint16_t v) {
    out.push_back((char)(v & 0xff));
    out.push_back((char)((v >> 8) & 0xff));
}
static inline void zip_u32(std::string& out, std::uint32_t v) {
    out.push_back((char)(v & 0xff));
    out.push_back((char)((v >> 8) & 0xff));
    out.push_back((char)((v >> 16) & 0xff));
    out.push_back((char)((v >> 24) & 0xff));
}

static std::uint32_t crc32_update(std::uint32_t crc, const unsigned char* data, size_t len) {
    static std::uint32_t table[256];
    static bool inited = false;
    if (!inited) {
        for (std::uint32_t i = 0; i < 256; i++) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        inited = true;
    }
    crc = crc ^ 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

static inline std::string zip_sanitize_relpath(std::string p) {
    // ZIP paths use forward slashes and must not be absolute.
    for (auto& ch : p) if (ch == '\\') ch = '/';
    while (!p.empty() && p.front() == '/') p.erase(p.begin());
    // Remove any empty segments.
    while (p.find("//") != std::string::npos) p = std::string(p).replace(p.find("//"), 2, "/");
    return p;
}

static inline std::string zip_basename(const std::filesystem::path& p) {
    auto s = p.filename().string();
    if (s.empty()) s = "folder";
    return s;
}

static inline void zip_dos_time_date(std::filesystem::file_time_type ft, std::uint16_t& dos_time, std::uint16_t& dos_date) {
    // Best-effort conversion.
    // DOS date starts at 1980-01-01.
    using namespace std::chrono;
    auto sctp = time_point_cast<system_clock::duration>(ft - std::filesystem::file_time_type::clock::now()
                                                       + system_clock::now());
    std::time_t tt = system_clock::to_time_t(sctp);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    int year = tm.tm_year + 1900;
    if (year < 1980) year = 1980;
    int mon = tm.tm_mon + 1;
    int day = tm.tm_mday;
    int hour = tm.tm_hour;
    int min = tm.tm_min;
    int sec = tm.tm_sec;

    dos_time = (std::uint16_t)(((hour & 31) << 11) | ((min & 63) << 5) | ((sec / 2) & 31));
    dos_date = (std::uint16_t)((((year - 1980) & 127) << 9) | ((mon & 15) << 5) | (day & 31));
}

struct ZipFileItem {
    std::filesystem::path abs_path; // full path on disk
    std::string zip_name;           // relative name inside zip (forward slashes)
    std::uint64_t size_u64 = 0;     // file size
    std::uint16_t dos_time = 0;
    std::uint16_t dos_date = 0;

    // Filled during streaming:
    std::uint32_t crc32 = 0;
    std::uint32_t size32 = 0;
    std::uint32_t local_header_off = 0;
};

struct ZipTotals {
    std::uint64_t total_bytes = 0;
    std::uint64_t central_dir_bytes = 0;
    std::uint64_t central_dir_off = 0;
};

// Compute exact archive size for "store + data descriptor".
static ZipTotals zip_compute_totals(const std::vector<ZipFileItem>& items) {
    ZipTotals t{};
    std::uint64_t off = 0;

    // Local file header layout:
    // 30 bytes fixed + filename + extra
    // file data
    // data descriptor 16 bytes (signature + crc + csize + usize)
    for (const auto& it : items) {
        off += 30;
        off += it.zip_name.size();
        off += 0; // extra
        off += it.size_u64;
        off += 16; // data descriptor (we include signature)
    }

    t.central_dir_off = off;

    // Central dir file header:
    // 46 bytes fixed + filename + extra + comment
    std::uint64_t cd = 0;
    for (const auto& it : items) {
        cd += 46;
        cd += it.zip_name.size();
        cd += 0; // extra
        cd += 0; // comment
    }

    // End of central dir: 22 bytes + comment(0)
    cd += 22;

    t.central_dir_bytes = cd;
    t.total_bytes = off + cd;
    return t;
}

// Streaming state machine that emits the full ZIP in-order.
class ZipStreamer {
public:
    ZipStreamer(std::vector<ZipFileItem> items, ZipTotals totals)
        : items_(std::move(items)), totals_(totals) {}


    // Called by httplib content provider; must emit sequential bytes.
    bool emit(size_t offset, size_t max_len, httplib::DataSink& sink) {
        if (finished_) return false;
        if (offset != (size_t)cur_off_) {
            // httplib usually calls sequentially. If not, fail safe.
            return false;
        }

        size_t remaining = max_len;
        while (remaining > 0 && !finished_) {
            // Drain pending buffer first
            if (buf_pos_ < buf_.size()) {
                size_t n = std::min(remaining, buf_.size() - buf_pos_);
                sink.write(buf_.data() + buf_pos_, n);
                buf_pos_ += n;
                cur_off_ += n;
                remaining -= n;
                continue;
            }

            // Buffer empty; produce next chunk based on stage
            buf_.clear();
            buf_pos_ = 0;

            if (stage_ == Stage::LocalHeader) {
                if (idx_ >= items_.size()) {
                    stage_ = Stage::CentralDir;
                    continue;
                }
                auto& it = items_[idx_];
                it.local_header_off = (std::uint32_t)cur_off_; // zip32 offset
                make_local_header(it);
                stage_ = Stage::FileData;
                open_file(it);
                continue;
            }

            if (stage_ == Stage::FileData) {
                auto& it = items_[idx_];
                if (!fp_.is_open()) {
                    // nothing to read -> write descriptor
                    make_data_descriptor(it);
                    stage_ = Stage::NextFile;
                    continue;
                }

                // Read a chunk from file directly into sink (avoids extra copies)
                const size_t chunk = std::min<size_t>(remaining, 64 * 1024);
                tmp_.resize(chunk);

                fp_.read(tmp_.data(), (std::streamsize)chunk);
                std::streamsize got = fp_.gcount();
                if (got > 0) {
                    // update CRC
                    it.crc32 = crc32_update(it.crc32, (const unsigned char*)tmp_.data(), (size_t)got);
                    sink.write(tmp_.data(), (size_t)got);
                    cur_off_ += (size_t)got;
                    remaining -= (size_t)got;
                    continue;
                }

                // EOF
                fp_.close();
                // finalize sizes for central dir
                it.size32 = (std::uint32_t)std::min<std::uint64_t>(it.size_u64, 0xFFFFFFFFu);
                make_data_descriptor(it);
                stage_ = Stage::NextFile;
                continue;
            }

            if (stage_ == Stage::NextFile) {
                idx_++;
                stage_ = Stage::LocalHeader;
                continue;
            }

            if (stage_ == Stage::CentralDir) {
                if (!central_built_) {
                    build_central_directory();
                    central_built_ = true;
                }
                if (central_pos_ < central_.size()) {
                    // serve central dir bytes in chunks via buf_
                    const size_t n = std::min(remaining, central_.size() - central_pos_);
                    sink.write(central_.data() + central_pos_, n);
                    central_pos_ += n;
                    cur_off_ += n;
                    remaining -= n;
                    continue;
                }
                finished_ = true;
                break;
            }
        }

        return !finished_;
    }

    const std::vector<ZipFileItem>& items() const { return items_; }

private:
    enum class Stage { LocalHeader, FileData, NextFile, CentralDir };

    void make_local_header(const ZipFileItem& it) {
        // Local file header signature 0x04034b50
        zip_u32(buf_, 0x04034b50u);
        zip_u16(buf_, 20);             // version needed
        zip_u16(buf_, 0x0008u);        // general purpose bit flag: bit3 => data descriptor
        zip_u16(buf_, 0);              // compression method 0=store
        zip_u16(buf_, it.dos_time);
        zip_u16(buf_, it.dos_date);
        zip_u32(buf_, 0);              // crc32 (0 for now, in descriptor)
        zip_u32(buf_, 0);              // compressed size (0 for now)
        zip_u32(buf_, 0);              // uncompressed size (0 for now)
        zip_u16(buf_, (std::uint16_t)it.zip_name.size());
        zip_u16(buf_, 0);              // extra length
        buf_ += it.zip_name;           // filename bytes
    }

    void make_data_descriptor(const ZipFileItem& it) {
        // Data descriptor signature 0x08074b50 + crc + csize + usize
        zip_u32(buf_, 0x08074b50u);
        zip_u32(buf_, it.crc32);
        // store => compressed size == uncompressed size
        std::uint32_t sz = (std::uint32_t)std::min<std::uint64_t>(it.size_u64, 0xFFFFFFFFu);
        zip_u32(buf_, sz);
        zip_u32(buf_, sz);
    }

    void build_central_directory() {
        central_.clear();
        central_.reserve((size_t)totals_.central_dir_bytes);

        const std::uint32_t cd_start = (std::uint32_t)totals_.central_dir_off;
        std::uint32_t cd_size = 0;

        for (const auto& it : items_) {
            // Central directory header signature 0x02014b50
            zip_u32(central_, 0x02014b50u);
            zip_u16(central_, 0x031Eu);   // version made by (arbitrary)
            zip_u16(central_, 20);        // version needed
            zip_u16(central_, 0x0008u);   // flags: data descriptor used
            zip_u16(central_, 0);         // method: store
            zip_u16(central_, it.dos_time);
            zip_u16(central_, it.dos_date);
            zip_u32(central_, it.crc32);
            std::uint32_t sz = (std::uint32_t)std::min<std::uint64_t>(it.size_u64, 0xFFFFFFFFu);
            zip_u32(central_, sz);        // compressed
            zip_u32(central_, sz);        // uncompressed
            zip_u16(central_, (std::uint16_t)it.zip_name.size());
            zip_u16(central_, 0);         // extra len
            zip_u16(central_, 0);         // comment len
            zip_u16(central_, 0);         // disk number
            zip_u16(central_, 0);         // internal attrs
            zip_u32(central_, 0);         // external attrs
            zip_u32(central_, it.local_header_off);
            central_ += it.zip_name;

            cd_size += (std::uint32_t)(46 + it.zip_name.size());
        }

        // End of central directory record signature 0x06054b50
        zip_u32(central_, 0x06054b50u);
        zip_u16(central_, 0); // disk
        zip_u16(central_, 0); // disk start
        zip_u16(central_, (std::uint16_t)items_.size());
        zip_u16(central_, (std::uint16_t)items_.size());
        zip_u32(central_, cd_size);
        zip_u32(central_, cd_start);
        zip_u16(central_, 0); // comment len
    }

    void open_file(const ZipFileItem& it) {
        fp_.open(it.abs_path, std::ios::binary);
        // If open fails, we still proceed; read will yield 0 and descriptor will be written.
    }

private:
    std::vector<ZipFileItem> items_;
    ZipTotals totals_;

    Stage stage_ = Stage::LocalHeader;
    size_t idx_ = 0;

    std::ifstream fp_;
    std::string buf_;
    size_t buf_pos_ = 0;

    std::string central_;
    size_t central_pos_ = 0;
    bool central_built_ = false;

    std::vector<char> tmp_;

    std::uint64_t cur_off_ = 0;
    bool finished_ = false;
};

} // namespace

// Stream SHA-256 for a file. Returns false + err on failure.
static bool sha256_file(const std::filesystem::path& p, std::string* out_hex, std::string* err) {
    std::ifstream f(p, std::ios::binary);
    if (!f.good()) {
        if (err) *err = "cannot open file";
        return false;
    }

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        if (err) *err = "EVP_MD_CTX_new failed";
        return false;
    }

    // ensure free on all exits
    struct CtxGuard {
        EVP_MD_CTX* c;
        ~CtxGuard() { if (c) EVP_MD_CTX_free(c); }
    } guard{ctx};

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
        if (err) *err = "EVP_DigestInit_ex failed";
        return false;
    }

    std::array<char, 64 * 1024> buf{};
    while (f.good()) {
        f.read(buf.data(), (std::streamsize)buf.size());
        std::streamsize n = f.gcount();
        if (n > 0) {
            if (EVP_DigestUpdate(ctx, buf.data(), (size_t)n) != 1) {
                if (err) *err = "EVP_DigestUpdate failed";
                return false;
            }
        }
    }
    if (!f.eof()) {
        if (err) *err = "read failed";
        return false;
    }

    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int md_len = 0;
    if (EVP_DigestFinal_ex(ctx, md, &md_len) != 1) {
        if (err) *err = "EVP_DigestFinal_ex failed";
        return false;
    }

    if (out_hex) *out_hex = hex_encode_lower(md, (size_t)md_len);
    return true;
}


// ============================================================================
//  COMMAND / SHELL / STORAGE PROBE HELPERS
// ============================================================================

static bool run_cmd_capture(const std::string& cmd, std::string* out, int* exit_code) {
    if (out) out->clear();
    if (exit_code) *exit_code = 127; // default like "command failed"

    // Always capture stderr too.
    std::string cmd2 = cmd;
    if (cmd2.find("2>&1") == std::string::npos) {
        cmd2 += " 2>&1";
    }

    FILE* fp = popen(cmd2.c_str(), "r");
    if (!fp) {
        return false; // popen failed
    }

    std::string s;
    char buf[4096];
    while (true) {
        size_t n = fread(buf, 1, sizeof(buf), fp);
        if (n == 0) break;
        s.append(buf, n);
    }

    const int rc = pclose(fp);

    if (out) *out = s;

    if (rc == -1) {
        if (exit_code) *exit_code = 127;
        return false;
    }

    if (WIFEXITED(rc)) {
        if (exit_code) *exit_code = WEXITSTATUS(rc);
        return true;
    }

    if (WIFSIGNALED(rc)) {
        if (exit_code) *exit_code = 128 + WTERMSIG(rc);
        return true;
    }

    if (exit_code) *exit_code = 127;
    return false;
}

static std::string shell_quote_posix(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('\'');
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

static bool run_cmd_capture_argv(const std::vector<std::string>& argv, std::string* out, int* exit_code) {
    std::string cmd;
    bool first = true;
    for (const auto& a : argv) {
        if (!first) cmd.push_back(' ');
        first = false;
        cmd += shell_quote_posix(a);
    }
    return run_cmd_capture(cmd, out, exit_code);
}
static std::string rand_hex_16() {
    static const char* k = "0123456789abcdef";
    std::array<unsigned char, 8> b{};
    randombytes_buf(b.data(), b.size());
    std::string s;
    s.reserve(16);
    for (unsigned char c : b) { s.push_back(k[c >> 4]); s.push_back(k[c & 0x0f]); }
    return s;
}

static bool safe_app_id(const std::string& s) {
    if (s.empty() || s.size() > 64) return false;
    for (char c : s) {
        if (!(std::isalnum((unsigned char)c) || c=='_' || c=='-' || c=='.')) return false;
    }
    return true;
}

static bool safe_app_ver(const std::string& s) {
    if (s.empty() || s.size() > 64) return false;
    for (char c : s) {
        if (!(std::isalnum((unsigned char)c) || c=='_' || c=='-' || c=='.')) return false;
    }
    return true;
}
// ---- Snapshot Manager helpers (v1) -----------------------------------------
namespace {

struct SnapVol {
    std::string name;
    std::string source_subvolume; // absolute
    std::string snap_root;        // absolute
    bool enabled{false};
};

static std::string rand_hex_32() {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    static const char* h = "0123456789abcdef";
    std::string out;
    out.resize(32);
    for (int i = 0; i < 16; i++) {
        uint8_t b = (uint8_t)(rng() & 0xFF);
        out[i*2+0] = h[(b >> 4) & 0xF];
        out[i*2+1] = h[b & 0xF];
    }
    return out;
}

static bool popen_capture(const std::string& cmd, std::string* out, int* rc) {
    if (out) out->clear();

    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) { if (rc) *rc = -1; return false; }

    std::string buf;
    char tmp[4096];
    while (true) {
        size_t n = fread(tmp, 1, sizeof(tmp), fp);
        if (n > 0) buf.append(tmp, tmp + n);
        if (n < sizeof(tmp)) break;
    }

    int st = pclose(fp);

    int code = -1;
    if (st == -1) {
        code = -1; // pclose failed
    } else if (WIFEXITED(st)) {
        code = WEXITSTATUS(st); // normal exit => 0..255
    } else if (WIFSIGNALED(st)) {
        code = 128 + WTERMSIG(st); // like bash convention
    } else {
        code = st; // fallback (shouldn't happen often)
    }

    if (rc) *rc = code;
    if (out) *out = buf;
    return true;
}


static std::string realpath_str(const std::string& p) {
    std::error_code ec;
    auto rp = std::filesystem::weakly_canonical(std::filesystem::path(p), ec);
    if (ec) return p;
    return rp.string();
}

static bool is_path_under(const std::string& child, const std::string& parent) {
    // Canonical-ish containment check
    const std::string c = realpath_str(child);
    std::string p = realpath_str(parent);
    if (!p.empty() && p.back() != '/') p.push_back('/');
    return (c.size() >= p.size() && c.compare(0, p.size(), p) == 0);
}

struct RestoreRootProbeResult {
    int exit_code = 127;
    std::string output;
};

static RestoreRootProbeResult run_restore_root_probe_argv(
    const std::vector<std::string>& args,
    std::size_t max_bytes = 16 * 1024
) {
    RestoreRootProbeResult result;

    std::vector<std::string> argv_s = {
        "/usr/bin/sudo",
        "-n",
        "/usr/local/sbin/pqnas-restore-root"
    };
    argv_s.insert(argv_s.end(), args.begin(), args.end());

    int pipefd[2] = {-1, -1};
    if (::pipe(pipefd) != 0) {
        result.output = std::string("pipe failed: ") + std::strerror(errno);
        return result;
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        const int saved = errno;
        ::close(pipefd[0]);
        ::close(pipefd[1]);
        result.output = std::string("fork failed: ") + std::strerror(saved);
        return result;
    }

    if (pid == 0) {
        ::dup2(pipefd[1], STDOUT_FILENO);
        ::dup2(pipefd[1], STDERR_FILENO);
        ::close(pipefd[0]);
        ::close(pipefd[1]);

        std::vector<char*> argv;
        argv.reserve(argv_s.size() + 1);
        for (const auto& a : argv_s) {
            argv.push_back(const_cast<char*>(a.c_str()));
        }
        argv.push_back(nullptr);

        ::execv("/usr/bin/sudo", argv.data());
        _exit(127);
    }

    ::close(pipefd[1]);

    char buf[4096];
    bool truncated = false;

    for (;;) {
        const ssize_t n = ::read(pipefd[0], buf, sizeof(buf));
        if (n > 0) {
            const std::size_t have = result.output.size();
            if (have < max_bytes) {
                const std::size_t room = max_bytes - have;
                const std::size_t take =
                    static_cast<std::size_t>(n) < room
                        ? static_cast<std::size_t>(n)
                        : room;
                result.output.append(buf, take);
            }
            if (result.output.size() >= max_bytes) truncated = true;
            continue;
        }
        if (n == 0) break;
        if (errno == EINTR) continue;
        break;
    }

    ::close(pipefd[0]);

    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR) continue;
        result.output += "\nwaitpid failed: ";
        result.output += std::strerror(errno);
        result.exit_code = 127;
        return result;
    }

    if (truncated) result.output += "\n[output truncated]\n";

    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exit_code = 128 + WTERMSIG(status);
    } else {
        result.exit_code = 127;
    }

    return result;
}

static bool is_btrfs_subvolume_sudo_n(const std::string& abs_path, std::string* detail = nullptr) {
    // Security: probing uses a guarded helper with path allowlists, not direct btrfs sudo.
    const RestoreRootProbeResult r =
        run_restore_root_probe_argv({"subvolume-show", abs_path});

    if (detail) *detail = pqnas::shorten(r.output, 300);
    return r.exit_code == 0;
}




static bool load_snapshot_volumes_from_admin_settings(const std::string& admin_settings_path,
                                                     std::string* backend_out,
                                                     std::vector<SnapVol>* vols_out,
                                                     std::string* err_out) {
    if (backend_out) backend_out->clear();
    if (vols_out) vols_out->clear();
    if (err_out) err_out->clear();

    json j;
    try {
        std::ifstream f(admin_settings_path);
        if (!f.good()) {
            if (err_out) *err_out = "admin_settings not readable";
            return false;
        }
        f >> j;
    } catch (const std::exception& e) {
        if (err_out) *err_out = std::string("parse failed: ") + e.what();
        return false;
    } catch (...) {
        if (err_out) *err_out = "parse failed";
        return false;
    }

    auto s = j.value("snapshots", json::object());
    const bool enabled = s.value("enabled", false);
    const std::string backend = s.value("backend", "btrfs");
    if (backend_out) *backend_out = backend;

    std::vector<SnapVol> vols;
    auto arr = s.value("volumes", json::array());
    if (!arr.is_array()) arr = json::array();

    for (const auto& v : arr) {
        if (!v.is_object()) continue;
        SnapVol sv;
        sv.name = v.value("name", "");
        sv.source_subvolume = v.value("source_subvolume", "");
        sv.snap_root = v.value("snap_root", "");
        sv.enabled = enabled; // global enabled gates volumes in v1
        if (sv.name.empty() || sv.source_subvolume.empty() || sv.snap_root.empty()) continue;
        vols.push_back(sv);
    }

    if (vols_out) *vols_out = vols;
    return true;
}

// confirm cache

} // namespace

static std::string shell_escape_single_quotes(std::string s) {
    size_t pos = 0;
    while ((pos = s.find("'", pos)) != std::string::npos) {
        s.replace(pos, 1, "'\\''");
        pos += 4;
    }
    return s;
}
static std::string sh_quote(const std::string& s) {
    // Wrap in single quotes and escape any embedded single quotes safely.
    return "'" + shell_escape_single_quotes(s) + "'";
}


namespace pqnas { struct AuditEvent; }

#include <functional>


// ============================================================================
//  GLOBAL AUDIT BRIDGE
// ============================================================================

// Global audit bridge: endpoints can call audit_append(ev) anywhere in main.cpp.
// It becomes active once we bind it after AuditLog audit(...) is constructed.
static std::function<void(const pqnas::AuditEvent&)> g_audit_append;

static void audit_append(const pqnas::AuditEvent& ev) {
    if (g_audit_append) g_audit_append(ev);
}

// ============================================================================

// ============================================================================
//  STORAGE MANAGER / BTRFS HELPERS
// ============================================================================

// Storage Manager v1 (read-only): disk + btrfs status helpers
// ============================================================================

// Cap string size to prevent huge JSON responses or memory abuse
static inline void cap_string(std::string& s, size_t cap_bytes) {
    if (s.size() > cap_bytes) {
        s.resize(cap_bytes);
    }
}

// SHA-256 hex (lowercase), EVP-based. Returns empty string on failure.
static std::string sha256_hex_lower_evp(const std::string& s) {
    EVP_MD_CTX* c = EVP_MD_CTX_new();
    if (!c) return std::string{};
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int mdlen = 0;

    if (EVP_DigestInit_ex(c, EVP_sha256(), nullptr) != 1) { EVP_MD_CTX_free(c); return std::string{}; }
    if (!s.empty()) {
        if (EVP_DigestUpdate(c, s.data(), s.size()) != 1) { EVP_MD_CTX_free(c); return std::string{}; }
    }
    if (EVP_DigestFinal_ex(c, md, &mdlen) != 1) { EVP_MD_CTX_free(c); return std::string{}; }
    EVP_MD_CTX_free(c);

    static const char* hex = "0123456789abcdef";
    std::string out;
    out.resize(mdlen * 2);
    for (unsigned int i = 0; i < mdlen; ++i) {
        out[i*2 + 0] = hex[(md[i] >> 4) & 0xF];
        out[i*2 + 1] = hex[(md[i]     ) & 0xF];
    }
    return out;
}

[[maybe_unused]] static std::string btrfs_membership_fingerprint(const json& btrfs_j) {
    // Stable across "used bytes" changes etc.
    // Fingerprint = sha256("uuid=<uuid>\n<sorted device paths>\n")
    std::string uuid = btrfs_j.value("uuid", "");
    std::vector<std::string> paths;

    if (btrfs_j.contains("devices") && btrfs_j["devices"].is_array()) {
        for (const auto& dev : btrfs_j["devices"]) {
            if (!dev.is_object()) continue;
            const std::string p = dev.value("path", "");
            if (!p.empty()) paths.push_back(p);
        }
    }

    std::sort(paths.begin(), paths.end());

    std::string material = "uuid=" + uuid + "\n";
    for (const auto& p : paths) material += p + "\n";

    return sha256_hex_lower_evp(material);
}

[[maybe_unused]] static std::string join_commands_for_hash(const json& commands_arr) {
    if (!commands_arr.is_array()) return "";
    std::string s;
    for (size_t i = 0; i < commands_arr.size(); ++i) {
        if (!commands_arr[i].is_string()) continue;
        if (!s.empty()) s.push_back('\n');
        s += commands_arr[i].get<std::string>();
    }
    return s;
}

static std::string iso8601_now() {
    using namespace std::chrono;
    auto now = system_clock::now();
    std::time_t tt = system_clock::to_time_t(now);

    std::tm tm{};
    gmtime_r(&tt, &tm); // UTC

    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buf);
}

static bool read_text_file(const std::string& path, std::string* out) {
    if (out) out->clear();

    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    constexpr size_t kMax = 16u * 1024u * 1024u; // 16 MiB hard cap

    std::string s;
    f.seekg(0, std::ios::end);
    std::streampos n = f.tellg();

    if (n > 0 && (size_t)n < kMax) {
        s.resize((size_t)n);
        f.seekg(0, std::ios::beg);
        f.read(&s[0], (std::streamsize)s.size());
        if (!f) return false;
    } else {
        f.clear();
        f.seekg(0, std::ios::beg);
        char buf[4096];
        while (f) {
            f.read(buf, sizeof(buf));
            std::streamsize got = f.gcount();
            if (got > 0) {
                if (s.size() + (size_t)got > kMax) {
                    s.append(buf, (size_t)(kMax - s.size()));
                    break;
                }
                s.append(buf, (size_t)got);
            }
        }
    }

    if (out) *out = s;
    return true;
}

static bool write_text_file_atomic(const std::string& path, const std::string& content) {
    const std::string tmp = path + ".tmp";
    std::ofstream f(tmp, std::ios::binary);
    if (!f) return false;
    f.write(content.data(), (std::streamsize)content.size());
    f.close();
    if (!f) return false;
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp);
        return false;
    }
    return true;
}

// ----------------------------------------------------------------------------
// Pools display-name config (raidmgr / storage UI metadata)
// Stored as JSON next to users.json so it ships with config and survives upgrades.
// Path: <dir_of_users.json>/pools.json
// ----------------------------------------------------------------------------

static std::filesystem::path pools_cfg_path_from_users_path(const std::string& users_path) {
    // Mutable config should live under PQNAS_STORAGE_ROOT/config (default /srv/pqnas/config)
    std::string root = getenv_str("PQNAS_STORAGE_ROOT");
    if (root.empty()) root = "/srv/pqnas";

    std::filesystem::path p = std::filesystem::path(root) / "config" / "pools.json";

    // If that doesn't exist yet, still return it (so load_or_init can create it).
    // Fall back to sibling of users.json only if root looks unusable.
    std::error_code ec;
    auto st = std::filesystem::status(std::filesystem::path(root) / "config", ec);
    if (!ec && std::filesystem::is_directory(st)) return p;

    return std::filesystem::path(users_path).parent_path() / "pools.json";
}

static void pool_mounts_init_default_only() {
    std::lock_guard<std::mutex> lk(pool_mu());
    auto& m = pool_mount_by_id();
    m.clear();
    m[default_pool_id()] = pqnas::data_root_dir();
}

static std::string pool_id_from_mount_best_effort(const std::string& mount) {
    // Preferred: /srv/pqnas/pools/<pool_id>
    // Return basename for anything else (sanitized).
    std::string m = mount;
    while (!m.empty() && m.back() == '/') m.pop_back();

    auto base = [&]() -> std::string {
        auto pos = m.find_last_of('/');
        if (pos == std::string::npos) return m;
        return m.substr(pos + 1);
    }();

    // If it matches /pools/<id>, return <id> (same as basename anyway)
    std::string id = base;

    // sanitize to [a-z0-9_-], max 32 (server-side)
    std::string out;
    out.reserve(id.size());
    for (char c : id) {
        char lc = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
        if ((lc >= 'a' && lc <= 'z') || (lc >= '0' && lc <= '9') || lc == '_' || lc == '-') out.push_back(lc);
    }
    if (out.empty()) out = "pool";
    if (out.size() > 32) out.resize(32);
    return out;
}


static std::filesystem::path pools_cfg_path_from_users_path(const std::string& users_path);
static bool write_text_file_atomic(const std::string&, const std::string&);
static bool read_text_file(const std::string&, std::string*);

static bool is_pool_id_safe(const std::string& s) {
    if (s.size() < 2 || s.size() > 24) return false;
    for (char c : s) {
        const bool ok =
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            (c == '_');
        if (!ok) return false;
    }
    return true;
}

static std::string upper_ascii_copy(std::string s) {
    for (char& c : s) {
        if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    }
    return s;
}

static std::string btrfs_label_for_pool_id(const std::string& pool_id) {
    return "PQNAS_" + upper_ascii_copy(pool_id);
}
static json load_or_init_pools_cfg(const std::string& users_path) {
    const auto cfg_path = pools_cfg_path_from_users_path(users_path);

    std::string txt;
    json j;

    if (read_text_file(cfg_path.string(), &txt)) {
        try {
            j = json::parse(txt);
        } catch (...) {
            j = json::object();
        }
    }

    if (!j.is_object())
        j = json::object();

    int version = j.value("version", 0);

    // ---------- INIT ----------
    if (version == 0) {
        j["version"] = 2;
        j["pools"] = json::object();
    }

    // ---------- MIGRATE v1 → v2 ----------
    if (version == 1) {
        json pools = json::object();
        const auto& names = j.value("names_by_mount", json::object());

        for (auto it = names.begin(); it != names.end(); ++it) {
            const std::string mount = it.key();
            const std::string display = it.value().get<std::string>();

            pools[mount] = {
                {"display_name", display},
                {"created_ts", iso8601_now()},
                {"managed", false}
            };
        }

        j.clear();
        j["version"] = 2;
        j["pools"] = pools;

        write_text_file_atomic(cfg_path.string(), j.dump(2) + "\n");
        return j;
    }

    // ---------- Ensure structure ----------
    if (j.value("version", 0) != 2)
        j["version"] = 2;

    if (!j.contains("pools") || !j["pools"].is_object())
        j["pools"] = json::object();

    return j;
}

// returns true if "btrfs filesystem show <mount>" mentions the given device path

static bool getenv_bool(const char* k, bool defv) {
    const char* v = std::getenv(k);
    if (!v) return defv;
    std::string s(v);
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    if (s == "1" || s == "true" || s == "yes" || s == "on") return true;
    if (s == "0" || s == "false" || s == "no" || s == "off") return false;
    return defv;
}

// Security: run startup pool-restore probes/root-helper calls via argv.
// This avoids shell parsing while keeping the old restore flow isolated.
[[maybe_unused]] static int main_run_argv_capture_no_shell(
    const std::vector<std::string>& argv_s,
    std::string* out,
    std::size_t max_bytes = 1024u * 1024u) {
    if (out) out->clear();

    if (argv_s.empty()) {
        if (out) *out = "err: empty argv\\n";
        return 127;
    }

    int pipefd[2] = {-1, -1};
    if (::pipe(pipefd) != 0) {
        if (out) *out = "err: pipe failed\\n";
        return 127;
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(pipefd[0]);
        ::close(pipefd[1]);
        if (out) *out = "err: fork failed\\n";
        return 127;
    }

    if (pid == 0) {
        ::close(pipefd[0]);
        (void)::dup2(pipefd[1], STDOUT_FILENO);
        (void)::dup2(pipefd[1], STDERR_FILENO);
        ::close(pipefd[1]);

        std::vector<char*> argv;
        argv.reserve(argv_s.size() + 1);
        for (const auto& a : argv_s) {
            argv.push_back(const_cast<char*>(a.c_str()));
        }
        argv.push_back(nullptr);

        ::execv(argv_s[0].c_str(), argv.data());
        _exit(127);
    }

    ::close(pipefd[1]);

    std::array<char, 8192> buf{};
    while (true) {
        const ssize_t n = ::read(pipefd[0], buf.data(), buf.size());
        if (n > 0) {
            if (out && out->size() < max_bytes) {
                const std::size_t remaining = max_bytes - out->size();
                const std::size_t take = std::min<std::size_t>(
                    remaining,
                    static_cast<std::size_t>(n)
                );
                out->append(buf.data(), take);
            }
            continue;
        }
        break;
    }

    ::close(pipefd[0]);

    int st = 0;
    if (::waitpid(pid, &st, 0) < 0) {
        return 127;
    }

    if (WIFEXITED(st)) {
        return WEXITSTATUS(st);
    }
    if (WIFSIGNALED(st)) {
        return 128 + WTERMSIG(st);
    }
    return 127;
}


[[maybe_unused]] static bool is_abs_path_safe(const std::string& p) {
    if (p.empty()) return false;
    if (p[0] != '/') return false;
    // crude hardening against shell injection + traversal
    if (p.find("..") != std::string::npos) return false;
    if (p.find(';') != std::string::npos) return false;
    if (p.find('|') != std::string::npos) return false;
    if (p.find('&') != std::string::npos) return false;
    if (p.find('`') != std::string::npos) return false;
    if (p.find('$') != std::string::npos) return false;
    if (p.find('\n') != std::string::npos) return false;
    if (p.find('\r') != std::string::npos) return false;
    return true;
}


// trim trailing whitespace/newlines (for command outputs)
static inline void rtrim_inplace(std::string& s) {
    while (!s.empty()) {
        char c = s.back();
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t') s.pop_back();
        else break;
    }
}

// Return string value from json, capped to max_len bytes (safe for firmware junk)
static inline std::string jstr_cap(const json& o, const char* k, size_t max_len = 256) {
    auto it = o.find(k);
    if (it == o.end() || it->is_null()) return "";

    std::string s;
    try {
        if (it->is_string()) s = it->get<std::string>();
        else s = it->dump();
    } catch (...) {
        return "";
    }

    if (s.size() > max_len) s.resize(max_len);
    return s;
}

// Returns ok=true and list of mountpoints for any descendants of a disk.
// Uses full path /usr/bin/lsblk for consistency with your other code.

// Convert lsblk JSON into a safer, smaller disk list.
// - keeps only TYPE=="disk"
// - by default excludes /dev/loop* (snap loops), unless PQNAS_STORAGE_ALLOW_LOOP=1

static inline bool str_contains(const std::string& s, const char* needle) {
    return s.find(needle) != std::string::npos;
}

// Parse human size like "20.27MiB", "238.47GiB", "0.00B" into bytes (double->uint64).
// Returns true on success.
static inline bool parse_human_bytes(const std::string& tok, uint64_t* out_bytes) {
    if (!out_bytes) return false;
    *out_bytes = 0;

    std::string s = tok;
    // trim whitespace
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\n' || s.back() == '\r')) s.pop_back();
    if (s.empty()) return false;

    // split numeric prefix and suffix
    size_t i = 0;
    bool seen_digit = false;
    while (i < s.size()) {
        char c = s[i];
        if ((c >= '0' && c <= '9') || c == '.') { seen_digit = true; i++; continue; }
        break;
    }
    if (!seen_digit) return false;

    const std::string num_str = s.substr(0, i);
    const std::string unit = s.substr(i);

    char* endp = nullptr;
    const double v = std::strtod(num_str.c_str(), &endp);
    if (!endp || endp == num_str.c_str()) return false;

    double mul = 1.0;
    if (unit == "B" || unit.empty()) mul = 1.0;
    else if (unit == "KiB") mul = 1024.0;
    else if (unit == "MiB") mul = 1024.0 * 1024.0;
    else if (unit == "GiB") mul = 1024.0 * 1024.0 * 1024.0;
    else if (unit == "TiB") mul = 1024.0 * 1024.0 * 1024.0 * 1024.0;
    else if (unit == "PiB") mul = 1024.0 * 1024.0 * 1024.0 * 1024.0 * 1024.0;
    else return false;

    const double bytes = v * mul;
    if (bytes < 0) return false;
    *out_bytes = static_cast<uint64_t>(bytes + 0.5);
    return true;
}
// Parent disk from a /dev path:
//  - /dev/nvme0n1p1 -> /dev/nvme0n1
//  - /dev/sda1      -> /dev/sda
//  - /dev/mmcblk0p2 -> /dev/mmcblk0
//  - /dev/loop32p1  -> /dev/loop32
static inline std::string parent_disk_from_dev(const std::string& dev_in) {
    // Trim whitespace defensively (lsblk/findmnt output can include \n)
    std::string dev = dev_in;
    while (!dev.empty() && (dev.back() == '\n' || dev.back() == '\r' || dev.back() == ' ' || dev.back() == '\t'))
        dev.pop_back();
    size_t start_ws = 0;
    while (start_ws < dev.size() && (dev[start_ws] == ' ' || dev[start_ws] == '\t'))
        start_ws++;
    if (start_ws > 0) dev.erase(0, start_ws);

    if (dev.rfind("/dev/", 0) != 0) return "";

    auto is_digit = [](char c) { return (c >= '0' && c <= '9'); };

    // nvme: /dev/nvme0n1p1 -> /dev/nvme0n1
    if (dev.rfind("/dev/nvme", 0) == 0) {
        // Only strip a trailing "p<digits>" if it exists
        size_t p = dev.rfind('p');
        if (p != std::string::npos && p + 1 < dev.size()) {
            bool all_digits = true;
            for (size_t i = p + 1; i < dev.size(); ++i) {
                if (!is_digit(dev[i])) { all_digits = false; break; }
            }
            if (all_digits) return dev.substr(0, p);
        }
        return dev;
    }

    // mmcblk: /dev/mmcblk0p2 -> /dev/mmcblk0
    if (dev.rfind("/dev/mmcblk", 0) == 0) {
        size_t p = dev.rfind('p');
        if (p != std::string::npos && p + 1 < dev.size()) {
            bool all_digits = true;
            for (size_t i = p + 1; i < dev.size(); ++i) {
                if (!is_digit(dev[i])) { all_digits = false; break; }
            }
            if (all_digits) return dev.substr(0, p);
        }
        return dev;
    }

    // loop: /dev/loop32p1 -> /dev/loop32 (handle explicitly, no heuristic fallback)
    if (dev.rfind("/dev/loop", 0) == 0) {
        const size_t base = std::string("/dev/loop").size(); // 9
        size_t i = base;

        // require at least one digit after /dev/loop
        if (i >= dev.size() || !is_digit(dev[i])) return dev;

        while (i < dev.size() && is_digit(dev[i])) i++; // consume loop number digits

        // exact disk form: /dev/loop<digits>
        if (i == dev.size()) return dev;

        // partition form: /dev/loop<digits>p<digits>
        if (dev[i] == 'p') {
            size_t ppos = i;
            size_t j = i + 1;
            if (j >= dev.size() || !is_digit(dev[j])) {
                // weird case like /dev/loop32p (no partition digits) -> treat as disk
                return dev.substr(0, ppos);
            }
            while (j < dev.size() && is_digit(dev[j])) j++;
            if (j == dev.size()) {
                // clean match -> return parent disk
                return dev.substr(0, ppos);
            }
        }

        // Anything else: don't guess
        return dev;
    }

    // sdX / vdX / xvdX / etc: strip trailing digits
    size_t end = dev.size();
    while (end > 0 && is_digit(dev[end - 1])) end--;
    if (end > 5 && end < dev.size()) return dev.substr(0, end);

    return dev;
}

// Helper: compute partition path for a whole-disk device (/dev/nvmeXnY -> /dev/nvmeXnYp1, /dev/sdX -> /dev/sdX1)
[[maybe_unused]] static std::string part1_path_from_disk(const std::string& disk) {
    if (disk.rfind("/dev/", 0) != 0) return "";
    if (disk.find("/dev/nvme") == 0)   return disk + "p1";
    if (disk.find("/dev/mmcblk") == 0) return disk + "p1";
    if (disk.find("/dev/loop") == 0)   return disk + "p1";
    return disk + "1";
}


// Very small validator: require /dev/... and no whitespace
static bool is_dev_path_basic_safe(const std::string& s) {
    if (s.rfind("/dev/", 0) != 0) return false;
    for (char c : s) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') return false;
    }
    if (s.find("..") != std::string::npos) return false;
    return true;
}

// Parse a "btrfs filesystem df" line like:
// "Data, single: total=2.01GiB, used=19.12MiB"
// Returns true and fills (name, total_bytes, used_bytes) on success.
static inline bool parse_btrfs_df_line(const std::string& line,
                                      std::string* out_name,
                                      uint64_t* out_total_bytes,
                                      uint64_t* out_used_bytes,
                                      std::string* out_total_str,
                                      std::string* out_used_str) {
    if (out_name) out_name->clear();
    if (out_total_bytes) *out_total_bytes = 0;
    if (out_used_bytes) *out_used_bytes = 0;
    if (out_total_str) out_total_str->clear();
    if (out_used_str) out_used_str->clear();

    // name is before the first comma or colon
    size_t name_end = line.find(',');
    if (name_end == std::string::npos) name_end = line.find(':');
    if (name_end == std::string::npos || name_end == 0) return false;

    std::string name = line.substr(0, name_end);
    // trim
    while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) name.erase(name.begin());
    while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.pop_back();
    if (name.empty()) return false;

    // find total=... and used=...
    size_t pt = line.find("total=");
    size_t pu = line.find("used=");
    if (pt == std::string::npos || pu == std::string::npos) return false;

    pt += 6;
    pu += 5;

    size_t pt_end = line.find_first_of(", \t\r\n", pt);
    if (pt_end == std::string::npos) pt_end = line.size();
    size_t pu_end = line.find_first_of(", \t\r\n", pu);
    if (pu_end == std::string::npos) pu_end = line.size();

    if (pt_end <= pt || pu_end <= pu) return false;

    std::string total_tok = line.substr(pt, pt_end - pt);
    std::string used_tok  = line.substr(pu, pu_end - pu);

    uint64_t total_b = 0, used_b = 0;
    if (!parse_human_bytes(total_tok, &total_b)) return false;
    if (!parse_human_bytes(used_tok, &used_b)) return false;

    if (out_name) *out_name = name;
    if (out_total_bytes) *out_total_bytes = total_b;
    if (out_used_bytes) *out_used_bytes = used_b;
    if (out_total_str) *out_total_str = total_tok;
    if (out_used_str) *out_used_str = used_tok;
    return true;
}

// Round double to N decimal places (safe, deterministic)
static inline double round_dp(double value, int decimals) {
    if (decimals <= 0) {
        return std::round(value);
    }
    const double scale = std::pow(10.0, decimals);
    return std::round(value * scale) / scale;
}

static inline std::string trim_copy(std::string s) {
        rtrim_inplace(s);
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) i++;
    if (i > 0) s.erase(0, i);
    return s;
}

static uint64_t parse_btrfs_human_bytes_to_u64(const std::string& s_in);

[[maybe_unused]] static json parse_btrfs_scrub_status_best_effort(const std::string& raw) {
    // Best-effort only. We do NOT assume exact formatting across btrfs-progs versions.
    // Typical outputs:
    // - "scrub status for <mp>\nno stats available\n" (never run)
    // - "scrub status for <mp>\nscrub started at ...\nstatus: running\n..."
    // - "scrub status for <mp>\nscrub started at ...\nscrub done at ...\nstatus: finished\nerrors: 0\n..."
    json j = json::object();
    j["raw"] = raw;

    const std::string s = raw; // already capped by caller

    auto has = [&](const char* needle)->bool{ return str_contains(s, needle); };

    // running/finished hints
    bool running = false;
    bool finished = false;

    // Common keywords
    if (has("status: running") || (has("running") && has("scrub started"))) running = true;
    if (has("status: finished") || (has("finished") && has("scrub started"))) finished = true;


    // "no stats available" usually means never run (idle)
    bool no_stats = has("no stats available");

    std::string state = "unknown";
    if (running) state = "running";
    else if (finished) state = "finished";
    else if (no_stats) state = "never";
    else if (has("scrub started") || has("scrub done")) state = "idle"; // ran before but not running now

    j["state"] = state;
    j["running"] = running;

    // Parse "errors: N" if present
    {
        const std::string key = "errors:";
        size_t p = s.find(key);
        if (p != std::string::npos) {
            p += key.size();
            while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) p++;
            size_t p2 = p;
            while (p2 < s.size() && (s[p2] >= '0' && s[p2] <= '9')) p2++;
            if (p2 > p) {
                j["errors"] = std::atoi(s.substr(p, p2 - p).c_str());
            }
        }
    }
// UUID:
{
    const std::string key = "UUID:";
    size_t p = s.find(key);
    if (p != std::string::npos) {
        size_t a = p + key.size();
        while (a < s.size() && (s[a] == ' ' || s[a] == '\t')) a++;
        size_t b = a;
        while (b < s.size() && s[b] != '\n' && s[b] != '\r') b++;
        if (b > a) j["uuid"] = trim_copy(s.substr(a, b - a));
    }
}

// no stats available
j["no_stats_available"] = has("no stats available");

// Total to scrub:
{
    const std::string key = "Total to scrub:";
    size_t p = s.find(key);
    if (p != std::string::npos) {
        size_t a = p + key.size();
        while (a < s.size() && (s[a] == ' ' || s[a] == '\t')) a++;
        size_t b = a;
        while (b < s.size() && s[b] != '\n' && s[b] != '\r') b++;
        if (b > a) {
            std::string tok = trim_copy(s.substr(a, b - a));
            j["total_to_scrub"] = tok;
            uint64_t bytes = parse_btrfs_human_bytes_to_u64(tok);
            if (bytes) j["total_to_scrub_bytes"] = bytes;
        }
    }
}

// Rate:
{
    const std::string key = "Rate:";
    size_t p = s.find(key);
    if (p != std::string::npos) {
        size_t a = p + key.size();
        while (a < s.size() && (s[a] == ' ' || s[a] == '\t')) a++;
        size_t b = a;
        while (b < s.size() && s[b] != '\n' && s[b] != '\r') b++;
        if (b > a) {
            std::string tok = trim_copy(s.substr(a, b - a));
            j["rate"] = tok; // e.g. "0.00B/s"
            // parse "XUNIT/s"
            if (tok.size() > 2 && tok.rfind("/s") == tok.size() - 2) {
                std::string numu = tok.substr(0, tok.size() - 2);
                uint64_t bps = parse_btrfs_human_bytes_to_u64(numu);
                j["rate_bps"] = bps;
            }
        }
    }
}

// Error summary:
{
    const std::string key = "Error summary:";
    size_t p = s.find(key);
    if (p != std::string::npos) {
        size_t a = p + key.size();
        while (a < s.size() && (s[a] == ' ' || s[a] == '\t')) a++;
        size_t b = a;
        while (b < s.size() && s[b] != '\n' && s[b] != '\r') b++;
        if (b > a) j["error_summary"] = trim_copy(s.substr(a, b - a));
    }
}

    return j;
}

// ============================ RAID / Btrfs discovery helpers ============================

static bool ensure_dir_fail_closed(const std::string& dir, std::string* err) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        if (err) *err = "create_directories failed: " + ec.message();
        return false;
    }

    // Verify it exists and is a directory (fail-closed)
    ec.clear();
    const bool exists = std::filesystem::exists(dir, ec);
    if (ec || !exists) {
        if (err) *err = "dir does not exist after create_directories";
        return false;
    }

    ec.clear();
    const bool isdir = std::filesystem::is_directory(dir, ec);
    if (ec || !isdir) {
        if (err) *err = "path is not a directory";
        return false;
    }

    return true;
}
#include <unordered_set>
#include <algorithm>
#include <cctype>


namespace {

const auto pqnas_server_started_at_local = std::chrono::system_clock::now();

long long pqnas_server_started_at_epoch_local() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        pqnas_server_started_at_local.time_since_epoch()
    ).count();
}

long long pqnas_server_uptime_seconds_local() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now() - pqnas_server_started_at_local
    ).count();
}

std::string pqnas_server_started_at_iso_local() {
    const std::time_t t = static_cast<std::time_t>(pqnas_server_started_at_epoch_local());
    std::tm tm{};

#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif

    std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return os.str();
}

} // namespace

static int open_excl_lockfile(const std::string& path, std::string* err) {
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0640);
    if (fd < 0) {
        if (err) *err = std::string("open(O_EXCL) failed: ") + std::strerror(errno);
        return -1;
    }
    return fd;
}

[[maybe_unused]] static bool write_fd_all(int fd, const std::string& s) {
    const char* p = s.data();
    size_t n = s.size();
    while (n > 0) {
        ssize_t w = ::write(fd, p, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        p += (size_t)w;
        n -= (size_t)w;
    }

    // Best-effort flush; if it fails we still return false (caller may decide to fail-closed)
    if (::fsync(fd) != 0) {
        // Some filesystems may not support fsync meaningfully, but /run is typically tmpfs and should.
        return false;
    }
    return true;
}


// ============================================================================
//  RAID / USER STORAGE JOB HELPERS
// ============================================================================

static std::string raid_exec_record_path(const std::string& plan_id) {
    return std::string("/run/pqnas/raid/") + plan_id + ".json";
}
[[maybe_unused]] static void ensure_dir_best_effort(const std::string& p) {
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
}
[[maybe_unused]] static std::string raid_mount_lock_path(const std::string& resolved_mount) {
    const std::string h = sha256_hex_lower_evp(resolved_mount);
    // Keep filename deterministic and safe even if hashing fails (shouldn't).
    return std::string("/run/pqnas/raid/lock-mount-") + (h.empty() ? "bad" : h) + ".lock";
}

[[maybe_unused]] static bool is_hex_64_lower_or_upper(const std::string& s) {
    if (s.size() != 64) return false;
    for (char c : s) {
        if (!is_hex_lower_or_upper(c)) return false;  // uses your char helper at line ~313
    }
    return true;
}

// Strict validator: 64 lowercase hex characters.
static bool is_sha256_hex_lower(const std::string& s) {
    if (s.size() != 64) return false;
    for (char c : s) {
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!ok) return false;
    }
    return true;
}

static uint64_t parse_btrfs_human_bytes_to_u64(const std::string& s_in) {
    // Best-effort parser for tokens like "123.45GiB", "931.51MiB", "1024.00KiB", "123B"
    // Returns 0 on failure. Never throws.
    std::string s = trim_copy(s_in);
    if (s.empty()) return 0;

    // Split numeric prefix and unit suffix
    size_t i = 0;
    bool seen_digit = false;
    while (i < s.size()) {
        const char c = s[i];
        if ((c >= '0' && c <= '9') || c == '.') { seen_digit = true; i++; continue; }
        break;
    }
    if (!seen_digit) return 0;

    std::string num = s.substr(0, i);
    std::string unit = trim_copy(s.substr(i));

    // If unit is empty, assume bytes
    if (unit.empty()) unit = "B";

    // Normalize unit (strip spaces)
    {
        std::string u2;
        for (char c : unit) if (c != ' ' && c != '\t') u2.push_back(c);
        unit = u2;
    }

    double val = 0.0;
    try {
        val = std::stod(num);
    } catch (...) {
        return 0;
    }

    uint64_t mul = 1;
    if (unit == "B") mul = 1ULL;
    else if (unit == "KiB") mul = 1024ULL;
    else if (unit == "MiB") mul = 1024ULL * 1024ULL;
    else if (unit == "GiB") mul = 1024ULL * 1024ULL * 1024ULL;
    else if (unit == "TiB") mul = 1024ULL * 1024ULL * 1024ULL * 1024ULL;
    else if (unit == "PiB") mul = 1024ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL;
    else {
        // Unknown unit -> fail safe
        return 0;
    }

    const long double bytes_ld = (long double)val * (long double)mul;
    if (bytes_ld <= 0.0L) return 0;
    if (bytes_ld > (long double)std::numeric_limits<uint64_t>::max()) return 0;
    return (uint64_t)(bytes_ld + 0.5L); // round to nearest
}

struct BtrfsShowDevice {
    int devid = -1;
    std::string path;           // capped
    uint64_t size_bytes = 0;
    uint64_t used_bytes = 0;
    std::string parent_disk;    // derived (e.g. /dev/nvme0n1)
};

struct BtrfsShowParsed {
    std::string label;          // capped
    std::string uuid;           // capped
    int total_devices = -1;
    uint64_t fs_bytes_used_bytes = 0;
    std::vector<BtrfsShowDevice> devices;
};

[[maybe_unused]] static BtrfsShowParsed parse_btrfs_filesystem_show(const std::string& raw) {
    // Parses output of: btrfs filesystem show <mount>
    // Best-effort; ignores unknown lines. Never throws.
    BtrfsShowParsed out;

    std::istringstream iss(raw);
    std::string line;

    while (std::getline(iss, line)) {
        rtrim_inplace(line);

        // IMPORTANT: btrfs show output is often tab-indented.
        // Use a trimmed copy for matching/parsing.
        std::string tline = trim_copy(line);
        if (tline.empty()) continue;

        // Example header line:
        // Label: 'pqnas'  uuid: <uuid>
        if (tline.rfind("Label:", 0) == 0) {
            // use tline everywhere below in this block
            const size_t pos_uuid = tline.find("uuid:");
            std::string left  = (pos_uuid == std::string::npos) ? tline : tline.substr(0, pos_uuid);
            std::string right = (pos_uuid == std::string::npos) ? ""    : tline.substr(pos_uuid);

            // Extract label from left side
            // left like: "Label: 'pqnas'  "
            std::string lbl = left;
            // remove "Label:"
            if (lbl.rfind("Label:", 0) == 0) lbl.erase(0, std::string("Label:").size());
            lbl = trim_copy(lbl);
            // strip surrounding quotes if present
            if (!lbl.empty() && (lbl.front() == '\'' || lbl.front() == '"')) {
                char q = lbl.front();
                if (lbl.size() >= 2 && lbl.back() == q) {
                    lbl = lbl.substr(1, lbl.size() - 2);
                } else {
                    lbl.erase(0, 1);
                }
            }
            cap_string(lbl, 256);
            out.label = lbl;

            // Extract uuid from right side
            // right like: "uuid: XXXXX"
            if (!right.empty()) {
                std::string uu = right;
                const size_t p = uu.find("uuid:");
                if (p != std::string::npos) uu.erase(0, p + 5);
                uu = trim_copy(uu);
                cap_string(uu, 256);
                out.uuid = uu;
            }
            continue;
        }

	// Example:
	// Total devices 2 FS bytes used 123.45GiB
	if (tline.rfind("Total devices", 0) == 0) {
	    // total_devices: token 3
    	{
        	std::istringstream t(tline);
        	std::string tok;
	        t >> tok; // Total
    	    t >> tok; // devices
        	int n = -1;
	        if (t >> n) out.total_devices = n;
    	}

	    // robust: locate "FS bytes used" then parse the following token
    	const std::string key = "FS bytes used";
	    const size_t k = tline.find(key);
    	if (k != std::string::npos) {
        	std::string rest = tline.substr(k + key.size());
	        rest = trim_copy(rest);
    	    // next token
        	std::string tok;
	        {
    	        std::istringstream t2(rest);
        	    t2 >> tok;
	        }
    	    // strip trailing punctuation that sometimes appears
        	while (!tok.empty()) {
            	char c = tok.back();
	            if (c == ',' || c == ')' || c == ';') tok.pop_back();
    	        else break;
        	}
	        out.fs_bytes_used_bytes = parse_btrfs_human_bytes_to_u64(tok);
    	}
    	continue;
	}


        // Example device line:
        // devid    1 size 931.51GiB used 120.03GiB path /dev/nvme0n1p1
        if (tline.find("devid") != std::string::npos && tline.find(" path ") != std::string::npos) {
            std::istringstream t(tline);
            std::string tok;
            BtrfsShowDevice dev;

            while (t >> tok) {
                if (tok == "devid") {
                    int id = -1;
                    if (t >> id) dev.devid = id;
                } else if (tok == "size") {
                    std::string x; if (t >> x) dev.size_bytes = parse_btrfs_human_bytes_to_u64(x);
                } else if (tok == "used") {
                    std::string x; if (t >> x) dev.used_bytes = parse_btrfs_human_bytes_to_u64(x);
                } else if (tok == "path") {
                    std::string p; if (t >> p) {
                        // Only accept /dev/... paths (fail-safe)
                        if (p.rfind("/dev/", 0) == 0) {
                            cap_string(p, 256);
                            dev.path = p;
                            dev.parent_disk = parent_disk_from_dev(p);
                            if (!dev.parent_disk.empty()) cap_string(dev.parent_disk, 256);
                        }
                    }
                }
            }

            if (dev.devid >= 0 && !dev.path.empty()) {
                out.devices.push_back(dev);
            }
            continue;
        }
    }

    // Safety cap: don't allow pathological outputs to create huge JSON
    if (out.devices.size() > 128) out.devices.resize(128);

    return out;
}
// Convert parsed show -> JSON object (UI-friendly)
[[maybe_unused]]static json btrfs_show_parsed_to_json(const BtrfsShowParsed& p,
                                     const json& by_path,
                                     const json& by_name) {
    json out;
    out["label"] = p.label;
    out["uuid"]  = p.uuid;
    if (p.total_devices >= 0) out["total_devices"] = p.total_devices;
    out["fs_bytes_used_bytes"] = p.fs_bytes_used_bytes;

    json devices = json::array();
    for (const auto& d : p.devices) {
        json jd;

        // Path (trimmed)
        std::string path = d.path;
        rtrim_inplace(path);

        jd["devid"]      = d.devid;
        jd["path"]       = path;
        jd["size_bytes"] = d.size_bytes;
        jd["used_bytes"] = d.used_bytes;

        // IMPORTANT: compute parent_disk from path (do NOT trust parsed parent_disk)
        std::string parent = parent_disk_from_dev(path);
        if (!parent.empty()) jd["parent_disk"] = parent;

        // Best-effort mapping to lsblk disk index
        int idx = -1;

        if (!parent.empty() && by_path.is_object()) {
            auto it = by_path.find(parent);
            if (it != by_path.end() && it->is_number_integer()) {
                idx = it->get<int>();
            }
        }

        if (idx < 0 && !parent.empty() && by_name.is_object()) {
            // try basename: /dev/nvme0n1 -> nvme0n1
            std::string name = parent;
            const size_t slash = name.rfind('/');
            if (slash != std::string::npos) name = name.substr(slash + 1);

            auto it2 = by_name.find(name);
            if (it2 != by_name.end() && it2->is_number_integer()) {
                idx = it2->get<int>();
            }
        }

        if (idx >= 0) jd["lsblk_disk_index"] = idx;

        devices.push_back(jd);
    }

    out["devices"] = devices;
    return out;
}

// ============================ GET /api/v4/raid/exec-record ============================
// ---- forward decls (only needed if the implementations are below this block) ----
static bool is_sha256_hex_lower(const std::string& s);
static std::string raid_exec_record_path(const std::string& plan_id);




[[maybe_unused]] static void raid_exec_record_append_step(json* rec,
                                        int step_index_1based,
                                        int step_total,
                                        const std::string& cmd,
                                        bool ok,
                                        int rc,
                                        const std::string& out) {
    if (!rec || !rec->is_object()) return;

    if (!rec->contains("results") || !(*rec)["results"].is_array()) {
        (*rec)["results"] = json::array();
    }

    json row;
    row["i"]   = step_index_1based - 1; // 0-based index for UI consistency
    row["cmd"] = cmd;
    row["ok"]  = ok;
    row["rc"]  = rc;
    row["out"] = out;

    (*rec)["results"].push_back(row);

    (*rec)["ts_last"]    = pqnas::now_iso_utc();
    (*rec)["step_index"] = step_index_1based;
    (*rec)["step_total"] = step_total;
    (*rec)["busy"]       = true;
    (*rec)["state"]      = "running";
}



// ============================================================================
// RAID ASYNC JOB ENGINE moved to routes_storage_raid.cpp.
// Legacy main.cpp worker/queue removed after storage/RAID route split.
// ============================================================================

[[maybe_unused]] static bool raid_exec_record_write_atomic(const std::string& plan_id, const json& rec) {
    if (!is_sha256_hex_lower(plan_id)) return false;
    const std::string path = raid_exec_record_path(plan_id);
    return write_text_file_atomic(path, rec.dump(2) + "\n");
}

[[maybe_unused]] static bool raid_exec_record_read(const std::string& plan_id, json* out_rec, std::string* err) {
    if (out_rec) *out_rec = json::object();
    if (err) err->clear();

    if (!is_sha256_hex_lower(plan_id)) {
        if (err) *err = "bad plan_id";
        return false;
    }

    const std::string path = raid_exec_record_path(plan_id);

    std::string text;
    if (!read_text_file(path, &text)) {
        if (err) *err = "record_not_found";
        return false;
    }

    cap_string(text, 1024 * 1024);

    json j;
    try {
        j = json::parse(text.empty() ? "{}" : text);
    } catch (...) {
        if (err) *err = "record_parse_failed";
        return false;
    }

    if (!j.is_object()) j = json::object();
    if (out_rec) *out_rec = j;
    return true;
}

// ============================================================================
// USER STORAGE MIGRATION ASYNC JOB ENGINE (v1 scaffold)
// - submit route enqueues job + writes canonical record state=queued
// - dedicated worker family, separate from RAID worker
// - phase-based progress record, not command-step-based
// - actual phase execution added in next patch
// ============================================================================

static void user_mig_finalize_record(const std::string& job_id, json* rec, bool ok, const std::string& err_msg);

static std::uint64_t user_mig_compute_tree_bytes_best_effort(const std::filesystem::path& root) {
    std::uint64_t total = 0;

    std::error_code ec;
    if (!std::filesystem::exists(root, ec) || ec) return 0;

    const auto opts = std::filesystem::directory_options::skip_permission_denied;
    std::filesystem::recursive_directory_iterator it(root, opts, ec);
    std::filesystem::recursive_directory_iterator end;

    while (!ec && it != end) {
        std::error_code st_ec;
        const auto st = it->symlink_status(st_ec);
        if (!st_ec && std::filesystem::is_regular_file(st)) {
            std::error_code sz_ec;
            const auto sz = std::filesystem::file_size(it->path(), sz_ec);
            if (!sz_ec) {
                if (std::numeric_limits<std::uint64_t>::max() - total < sz) {
                    total = std::numeric_limits<std::uint64_t>::max();
                } else {
                    total += static_cast<std::uint64_t>(sz);
                }
            }
        }

        it.increment(ec);
    }

    return total;
}

static std::string user_mig_format_bytes_short(std::uint64_t bytes) {
    static constexpr const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    double value = static_cast<double>(bytes);
    std::size_t unit = 0;

    while (value >= 1024.0 && unit + 1 < (sizeof(units) / sizeof(units[0]))) {
        value /= 1024.0;
        ++unit;
    }

    std::ostringstream oss;
    if (unit == 0) {
        oss << static_cast<unsigned long long>(bytes) << " " << units[unit];
    } else {
        oss << std::fixed << std::setprecision(2) << value << " " << units[unit];
    }
    return oss.str();
}

static int user_mig_copy_absolute_percent(std::uint64_t done, std::uint64_t total) {
    if (total == 0) return 20;

    const double ratio = std::max(
        0.0,
        std::min(1.0, static_cast<double>(done) / static_cast<double>(total))
    );

    // Overall migration progress:
    //  0..20  = resolving/validation/destination creation
    // 20..79  = copy bytes
    // 80..100 = verify/metadata/done
    int pct = 20 + static_cast<int>(std::floor(ratio * 59.0));
    if (pct < 20) pct = 20;
    if (pct > 79) pct = 79;
    return pct;
}

static void user_mig_record_set_phase(json* rec, const std::string& phase, int percent, const std::string& message);
static void user_cleanup_finalize_record(const std::string& job_id, json* rec, bool ok, const std::string& err_msg);
static void user_cleanup_record_set_phase(json* rec, const std::string& phase, int percent, const std::string& message);

struct UserStorageMigrationJob {
    std::string job_id;
    std::string actor_fp;
    std::string user_fp;
    std::string requested_target_pool_id;
    std::string remote_ip;

    json record; // canonical durable record we mutate + write
};

static std::mutex g_user_mig_jobs_mu;
static std::condition_variable g_user_mig_jobs_cv;
static std::deque<UserStorageMigrationJob> g_user_mig_jobs_q;
static std::unordered_map<std::string, json> g_user_mig_job_meta; // job_id -> small status/meta
static std::atomic<bool> g_user_mig_worker_stop{false};
// Shared users_path for background user storage workers.
// Historical name kept temporarily after RAID worker moved to routes_storage_raid.cpp.
static std::string g_users_path_for_user_workers;

static std::thread g_user_mig_worker;

struct UserStorageCleanupJob {
    std::string job_id;
    std::string actor_fp;
    std::string user_fp;

    std::string expected_active_pool_id;
    std::string old_pool_id;

    json record;
};

static std::mutex g_user_cleanup_jobs_mu;
static std::condition_variable g_user_cleanup_jobs_cv;
static std::deque<UserStorageCleanupJob> g_user_cleanup_jobs_q;
static std::unordered_map<std::string, json> g_user_cleanup_job_meta;

static std::atomic<bool> g_user_cleanup_worker_stop{false};
static std::thread g_user_cleanup_worker;

static std::string user_mig_new_job_id() {
    std::string seed = pqnas::now_iso_utc();
    seed += "|pid=" + std::to_string((int)getpid());
    seed += "|rnd=" + std::to_string((uint64_t)std::rand());
    seed += "|kind=user_storage_migration";
    const std::string h = sha256_hex_lower_evp(seed);
    return is_sha256_hex_lower(h) ? h : sha256_hex_lower_evp(seed + "|fallback");
}

static std::string user_mig_record_path(const std::string& job_id) {
    return std::string("/run/pqnas/user-storage-migration/") + job_id + ".json";
}

static bool user_mig_record_write_atomic(const std::string& job_id, const json& rec) {
    if (!is_sha256_hex_lower(job_id)) return false;
    const std::string path = user_mig_record_path(job_id);
    return write_text_file_atomic(path, rec.dump(2) + "\n");
}

static bool user_mig_record_read(const std::string& job_id, json* out, std::string* err) {
    if (out) *out = json::object();
    if (err) err->clear();

    if (!is_sha256_hex_lower(job_id)) {
        if (err) *err = "bad_job_id";
        return false;
    }

    const std::string path = user_mig_record_path(job_id);

    std::string text;
    if (!read_text_file(path, &text)) {
        if (err) *err = "record_not_found";
        return false;
    }

    cap_string(text, 1024 * 1024);

    json j;
    try {
        j = json::parse(text.empty() ? "{}" : text);
    } catch (...) {
        if (err) *err = "record_parse_failed";
        return false;
    }

    if (!j.is_object()) j = json::object();
    if (out) *out = j;
    return true;
}

static std::string user_cleanup_record_path(const std::string& job_id) {
    return std::string("/run/pqnas/user-storage-cleanup/") + job_id + ".json";
}

static bool user_cleanup_record_write_atomic(const std::string& job_id, const json& rec) {
    if (!is_sha256_hex_lower(job_id)) return false;
    return write_text_file_atomic(user_cleanup_record_path(job_id), rec.dump(2) + "\n");
}

static bool user_cleanup_record_read(const std::string& job_id, json* out_rec, std::string* err) {
    if (out_rec) *out_rec = json::object();
    if (err) err->clear();

    if (!is_sha256_hex_lower(job_id)) {
        if (err) *err = "bad_job_id";
        return false;
    }

    std::string text;
    if (!read_text_file(user_cleanup_record_path(job_id), &text)) {
        if (err) *err = "record_not_found";
        return false;
    }

    cap_string(text, 1024 * 1024);

    try {
        json j = json::parse(text.empty() ? "{}" : text);
        if (!j.is_object()) j = json::object();
        if (out_rec) *out_rec = j;
        return true;
    } catch (...) {
        if (err) *err = "record_parse_failed";
        return false;
    }
}

static void user_cleanup_record_set_phase(json* rec,
                                          const std::string& phase,
                                          int percent,
                                          const std::string& message) {
    if (!rec) return;

    const std::string ts = pqnas::now_iso_utc();

    (*rec)["phase"] = phase;
    (*rec)["percent"] = percent;
    (*rec)["message"] = message;
    (*rec)["ts_last"] = ts;

    if (!rec->contains("events") || !(*rec)["events"].is_array()) {
        (*rec)["events"] = json::array();
    }

    (*rec)["events"].push_back(json{
        {"ts", ts},
        {"phase", phase},
        {"percent", percent},
        {"message", message}
    });
}

static void user_cleanup_finalize_record(const std::string& job_id,
                                         json* rec,
                                         bool ok,
                                         const std::string& err_msg) {
    if (!rec) return;

    const std::string ts_end = pqnas::now_iso_utc();
    (*rec)["ts_end"] = ts_end;
    (*rec)["ts_last"] = ts_end;
    (*rec)["busy"] = false;
    (*rec)["state"] = ok ? "done" : "failed";
    (*rec)["ok"] = ok;

    if (ok) {
        if (rec->contains("error")) rec->erase("error");
    } else {
        (*rec)["error"] = err_msg.empty() ? "cleanup_failed" : err_msg;
        (*rec)["message"] = err_msg.empty() ? "cleanup failed" : err_msg;
    }

    (void)user_cleanup_record_write_atomic(job_id, *rec);
}

static void user_mig_record_set_phase(json* rec,
                                      const std::string& phase,
                                      int percent,
                                      const std::string& message) {
    if (!rec) return;
    (*rec)["phase"] = phase;
    (*rec)["percent"] = percent;
    (*rec)["message"] = message;
    (*rec)["ts_last"] = pqnas::now_iso_utc();

    if (!rec->contains("events") || !(*rec)["events"].is_array()) {
        (*rec)["events"] = json::array();
    }

    (*rec)["events"].push_back(json{
        {"ts", pqnas::now_iso_utc()},
        {"phase", phase},
        {"percent", percent},
        {"message", message}
    });
}

static void user_mig_finalize_record(const std::string& job_id,
                                     json* rec,
                                     bool ok,
                                     const std::string& err_msg) {
    if (!rec) return;

    const std::string ts_end = pqnas::now_iso_utc();

    (*rec)["ts_end"] = ts_end;
    (*rec)["ts_last"] = ts_end;
    (*rec)["busy"] = false;
    (*rec)["state"] = ok ? "done" : "failed";
    (*rec)["ok"] = ok;

    if (ok) {
        (*rec)["phase"] = "done";
        (*rec)["percent"] = 100;
        (*rec)["message"] = "migration completed";
        if (rec->contains("error")) rec->erase("error");
    } else {
        (*rec)["message"] = err_msg.empty() ? "migration failed" : err_msg;
        (*rec)["error"] = err_msg.empty() ? "migration_failed" : err_msg;
    }

    (void)user_mig_record_write_atomic(job_id, *rec);
}

static std::string user_mig_lock_path(const std::string& user_fp) {
    return std::string("/run/pqnas/locks/user-storage-migrate-") + user_fp + ".lock";
}

static void user_storage_migration_worker_main(std::string users_path) {

    auto user_mig_worker_audit = [&](const std::string& event,
                                     const std::string& outcome,
                                     const UserStorageMigrationJob& job,
                                     const json& extra = json::object()) {
        try {
            pqnas::AuditEvent ev;
            ev.event = event;
            ev.outcome = outcome;

            if (!job.actor_fp.empty()) ev.f["actor_fp"] = job.actor_fp;
            if (!job.user_fp.empty()) ev.f["fingerprint"] = job.user_fp;
            if (!job.requested_target_pool_id.empty()) ev.f["to_pool_id"] = job.requested_target_pool_id;
            ev.f["job_id"] = job.job_id;
            ev.f["ip"] = "local";

            if (extra.is_object()) {
                for (auto it = extra.begin(); it != extra.end(); ++it) {
                    const std::string k = "x_" + pqnas::shorten(it.key(), 64);
                    if (it.value().is_string()) ev.f[k] = pqnas::shorten(it.value().get<std::string>(), 220);
                    else if (it.value().is_boolean()) ev.f[k] = it.value().get<bool>() ? "true" : "false";
                    else ev.f[k] = pqnas::shorten(it.value().dump(), 220);
                }
            }

            audit_append(ev);
        } catch (...) {
        }
    };

    for (;;) {
        UserStorageMigrationJob job;

        {
            std::unique_lock<std::mutex> lk(g_user_mig_jobs_mu);
            g_user_mig_jobs_cv.wait(lk, [&] {
                return g_user_mig_worker_stop.load() || !g_user_mig_jobs_q.empty();
            });
            if (g_user_mig_worker_stop.load()) return;

            job = std::move(g_user_mig_jobs_q.front());
            g_user_mig_jobs_q.pop_front();

            g_user_mig_job_meta[job.job_id]["state"] = "running";
            g_user_mig_job_meta[job.job_id]["ts_started"] = pqnas::now_iso_utc();
        }

        job.record["state"] = "running";
        job.record["busy"] = true;
        if (!job.record.contains("ts_started") || job.record["ts_started"].is_null()) {
            job.record["ts_started"] = pqnas::now_iso_utc();
        }
        user_mig_record_set_phase(&job.record, "starting", 2, "worker started");
        (void)user_mig_record_write_atomic(job.job_id, job.record);

        int fd_user_lock = -1;
        std::string user_lockp;

        auto close_user_lock = [&]() {
            if (fd_user_lock >= 0) {
                ::close(fd_user_lock);
                fd_user_lock = -1;
            }
            if (!user_lockp.empty()) {
                (void)std::filesystem::remove(user_lockp);
            }
        };

        bool ok = false;
        std::string fail_phase = "starting";
        std::string fail_reason;
        pqnas::UserStorageMigrationPlan plan;
        pqnas::UsersRegistry users_local;
        std::string err;

        do {
            user_mig_worker_audit("admin.user_storage_migration_started", "ok", job, json{
                {"phase", "starting"}
            });

            fail_phase = "acquiring_lock";
            user_mig_record_set_phase(&job.record, "acquiring_lock", 5, "acquiring per-user lock");
            (void)user_mig_record_write_atomic(job.job_id, job.record);

            user_lockp = user_mig_lock_path(job.user_fp);
            {
                std::string lock_dir_err;
                if (!ensure_dir_fail_closed("/run/pqnas/locks", &lock_dir_err)) {
                    fail_reason = "lock_dir_failed: " + lock_dir_err;
                    break;
                }
            }
            {
                std::string lock_err;
                fd_user_lock = open_excl_lockfile(user_lockp, &lock_err);
                if (fd_user_lock < 0) {
                    if (lock_err.find("File exists") != std::string::npos) {
                        fail_reason = "user_migration_busy: another migration is already in progress for this user";
                    } else {
                        fail_reason = "user_migration_lock_failed";
                    }
                    if (!lock_err.empty()) fail_reason += ": " + lock_err;
                    break;
                }
            }

            fail_phase = "resolving_paths";
            user_mig_record_set_phase(&job.record, "resolving_paths", 10, "resolving source and destination paths");
            (void)user_mig_record_write_atomic(job.job_id, job.record);

            if (!users_local.load(users_path)) {
                fail_reason = "users.load failed";
                break;
            }

            if (!pqnas::resolve_user_storage_migration(users_local,
                                                      users_path,
                                                      job.user_fp,
                                                      job.requested_target_pool_id,
                                                      &plan,
                                                      &err)) {
                fail_reason = "resolve_failed: " + err;
                break;
            }

            if (plan.from_pool_id == plan.to_pool_id) {
                fail_reason = "same_pool";
                break;
            }
            fail_phase = "validating_destination_capacity";
            user_mig_record_set_phase(&job.record,
                                      "validating_destination_capacity",
                                      16,
                                      "validating destination free space and quota capacity");
            (void)user_mig_record_write_atomic(job.job_id, job.record);

            {
                pqnas::UserStorageMigrationCapacityCheck cap;
                if (!pqnas::validate_user_storage_migration_destination_capacity(users_local,
                                                                                 users_path,
                                                                                 plan,
                                                                                 &cap,
                                                                                 &err)) {
                    job.record["result"]["source_used_bytes"] = cap.source_used_bytes;
                    job.record["result"]["dest_total_bytes"] = cap.dest_total_bytes;
                    job.record["result"]["dest_free_bytes"] = cap.dest_free_bytes;
                    job.record["result"]["required_free_bytes"] = cap.required_free_bytes;
                    job.record["result"]["dest_allocated_other_bytes"] = cap.dest_allocated_other_bytes;
                    job.record["result"]["dest_would_total_quota_bytes"] = cap.dest_would_total_quota_bytes;
                    job.record["result"]["user_quota_bytes"] = cap.user_quota_bytes;

                    fail_reason = err.empty() ? "destination_capacity_validation_failed" : err;
                    break;
                }

                job.record["result"]["source_used_bytes"] = cap.source_used_bytes;
                job.record["result"]["dest_total_bytes"] = cap.dest_total_bytes;
                job.record["result"]["dest_free_bytes"] = cap.dest_free_bytes;
                job.record["result"]["required_free_bytes"] = cap.required_free_bytes;
                job.record["result"]["dest_allocated_other_bytes"] = cap.dest_allocated_other_bytes;
                job.record["result"]["dest_would_total_quota_bytes"] = cap.dest_would_total_quota_bytes;
                job.record["result"]["user_quota_bytes"] = cap.user_quota_bytes;
            }
            job.record["resolved_source_pool_id"] = plan.from_pool_id;
            job.record["resolved_dest_pool_id"] = plan.to_pool_id;
            job.record["resolved_source_root"] = plan.src_data_root.string();
            job.record["resolved_dest_root"] = plan.dst_data_root.string();

            fail_phase = "creating_destination";
            user_mig_record_set_phase(&job.record, "creating_destination", 20, "creating destination directory");
            (void)user_mig_record_write_atomic(job.job_id, job.record);

            if (!pqnas::ensure_user_storage_migration_destination(plan, &err)) {
                fail_reason = "mkdir_failed: " + err;
                break;
            }

            fail_phase = "copying";

            const std::uint64_t copy_total_bytes = user_mig_compute_tree_bytes_best_effort(plan.src_user_dir);

            auto update_copy_progress_record = [&](bool force_write) {
                const std::uint64_t copy_done_raw =
                    user_mig_compute_tree_bytes_best_effort(plan.dst_user_dir);

                const std::uint64_t copy_done_bytes =
                    (copy_total_bytes > 0 && copy_done_raw > copy_total_bytes)
                        ? copy_total_bytes
                        : copy_done_raw;

                const int copy_percent =
                    user_mig_copy_absolute_percent(copy_done_bytes, copy_total_bytes);

                job.record["phase"] = "copying";
                job.record["percent"] = copy_percent;
                job.record["bytes_total"] = copy_total_bytes;
                job.record["bytes_done"] = copy_done_bytes;
                job.record["copy_percent"] =
                    copy_total_bytes > 0
                        ? std::round((static_cast<double>(copy_done_bytes) * 1000.0) /
                                     static_cast<double>(copy_total_bytes)) / 10.0
                        : 0.0;
                job.record["message"] =
                    user_mig_format_bytes_short(copy_done_bytes) + " / " +
                    user_mig_format_bytes_short(copy_total_bytes) + " copied";
                job.record["updated_at"] = pqnas::now_iso_utc();

                if (force_write || copy_total_bytes == 0 || copy_done_bytes <= copy_total_bytes) {
                    (void)user_mig_record_write_atomic(job.job_id, job.record);
                }
            };

            update_copy_progress_record(true);

            std::atomic<bool> copy_progress_stop{false};
            std::thread copy_progress_thread([&]() {
                while (!copy_progress_stop.load(std::memory_order_relaxed)) {
                    try {
                        update_copy_progress_record(false);
                    } catch (...) {
                        // Progress reporting must never affect migration correctness.
                    }

                    for (int i = 0; i < 10; ++i) {
                        if (copy_progress_stop.load(std::memory_order_relaxed)) break;
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    }
                }
            });

            bool copy_ok = false;
            try {
                copy_ok = pqnas::run_user_storage_migration_copy(plan, &err);
            } catch (...) {
                copy_progress_stop.store(true, std::memory_order_relaxed);
                if (copy_progress_thread.joinable()) {
                    copy_progress_thread.join();
                }
                throw;
            }

            copy_progress_stop.store(true, std::memory_order_relaxed);
            if (copy_progress_thread.joinable()) {
                copy_progress_thread.join();
            }

            update_copy_progress_record(true);

            if (!copy_ok) {
                fail_reason = "copy_failed: " + err;
                break;
            }

            job.record["result"]["copied"] = true;

            fail_phase = "verifying";
            user_mig_record_set_phase(&job.record, "verifying", 80, "verifying copied data");
            (void)user_mig_record_write_atomic(job.job_id, job.record);

            if (!pqnas::verify_user_storage_migration_destination(plan, &err)) {
                fail_reason = "verify_failed: " + err;
                break;
            }

            job.record["result"]["verified"] = true;

            fail_phase = "switching_metadata";
            user_mig_record_set_phase(&job.record, "switching_metadata", 92, "switching user metadata");
            (void)user_mig_record_write_atomic(job.job_id, job.record);

            // Re-load users here so compare-before-commit sees latest registry state.
            pqnas::UsersRegistry users_reload;
            if (!users_reload.load(users_path)) {
                fail_reason = "users.load failed before metadata switch";
                break;
            }

            if (!pqnas::switch_user_storage_migration_metadata(users_reload,
                                                               users_path,
                                                               job.actor_fp,
                                                               plan,
                                                               &err)) {
                fail_reason = "metadata_switch_failed: " + err;
                break;
            }

            job.record["result"]["metadata_updated"] = true;
            job.record["result"]["from_pool_id"] = plan.from_pool_id;
            job.record["result"]["to_pool_id"] = plan.to_pool_id;
            job.record["result"]["root_rel"] = plan.root_rel;
            job.record["result"]["src_user_dir"] = plan.src_user_dir.string();
            job.record["result"]["dst_user_dir"] = plan.dst_user_dir.string();

            user_mig_record_set_phase(&job.record, "done", 100, "migration completed");
            ok = true;
        } while (false);

        close_user_lock();

        if (ok) {
            user_mig_finalize_record(job.job_id, &job.record, true, "");
            user_mig_worker_audit("admin.user_storage_migration_succeeded", "ok", job, json{
                {"phase", "done"},
                {"source_pool_id", plan.from_pool_id},
                {"dest_pool_id", plan.to_pool_id}
            });

            std::lock_guard<std::mutex> lk(g_user_mig_jobs_mu);
            g_user_mig_job_meta[job.job_id]["state"] = "done";
            g_user_mig_job_meta[job.job_id]["ts_finished"] = pqnas::now_iso_utc();
        } else {
            job.record["result"]["copied"] = job.record["result"].value("copied", false);
            job.record["result"]["verified"] = job.record["result"].value("verified", false);
            job.record["result"]["metadata_updated"] = job.record["result"].value("metadata_updated", false);

            if (!plan.from_pool_id.empty()) job.record["result"]["from_pool_id"] = plan.from_pool_id;
            if (!plan.to_pool_id.empty()) job.record["result"]["to_pool_id"] = plan.to_pool_id;
            if (!plan.root_rel.empty()) job.record["result"]["root_rel"] = plan.root_rel;
            if (!plan.src_user_dir.empty()) job.record["result"]["src_user_dir"] = plan.src_user_dir.string();
            if (!plan.dst_user_dir.empty()) job.record["result"]["dst_user_dir"] = plan.dst_user_dir.string();

            user_mig_finalize_record(job.job_id, &job.record, false, fail_reason.empty() ? "migration_failed" : fail_reason);

            user_mig_worker_audit("admin.user_storage_migration_failed", "fail", job, json{
                {"phase", fail_phase},
                {"reason", fail_reason.empty() ? "migration_failed" : fail_reason},
                {"source_pool_id", plan.from_pool_id},
                {"dest_pool_id", plan.to_pool_id}
            });

            std::lock_guard<std::mutex> lk(g_user_mig_jobs_mu);
            g_user_mig_job_meta[job.job_id]["state"] = "failed";
            g_user_mig_job_meta[job.job_id]["error"] = fail_reason.empty() ? "migration_failed" : fail_reason;
            g_user_mig_job_meta[job.job_id]["ts_finished"] = pqnas::now_iso_utc();
        }
    }
}

static void user_storage_cleanup_worker_main(std::string users_path) {
	(void)users_path;
    auto user_cleanup_worker_audit = [&](const std::string& event,
                                         const std::string& outcome,
                                         const UserStorageCleanupJob& job,
                                         const json& extra = json::object()) {
        try {
            pqnas::AuditEvent ev;
            ev.event = event;
            ev.outcome = outcome;

            if (!job.actor_fp.empty()) ev.f["actor_fp"] = job.actor_fp;
            if (!job.user_fp.empty()) ev.f["fingerprint"] = job.user_fp;
            if (!job.expected_active_pool_id.empty()) ev.f["expected_active_pool_id"] = job.expected_active_pool_id;
            if (!job.old_pool_id.empty()) ev.f["old_pool_id"] = job.old_pool_id;
            ev.f["job_id"] = job.job_id;
            ev.f["ip"] = "local";

            if (extra.is_object()) {
                for (auto it = extra.begin(); it != extra.end(); ++it) {
                    const std::string k = "x_" + pqnas::shorten(it.key(), 64);
                    if (it.value().is_string()) ev.f[k] = pqnas::shorten(it.value().get<std::string>(), 220);
                    else if (it.value().is_boolean()) ev.f[k] = it.value().get<bool>() ? "true" : "false";
                    else ev.f[k] = pqnas::shorten(it.value().dump(), 220);
                }
            }

            audit_append(ev);
        } catch (...) {
        }
    };

    for (;;) {
        UserStorageCleanupJob job;

        {
            std::unique_lock<std::mutex> lk(g_user_cleanup_jobs_mu);
            g_user_cleanup_jobs_cv.wait(lk, [&] {
                return g_user_cleanup_worker_stop.load() || !g_user_cleanup_jobs_q.empty();
            });
            if (g_user_cleanup_worker_stop.load()) return;

            job = std::move(g_user_cleanup_jobs_q.front());
            g_user_cleanup_jobs_q.pop_front();

            g_user_cleanup_job_meta[job.job_id]["state"] = "running";
            g_user_cleanup_job_meta[job.job_id]["ts_started"] = pqnas::now_iso_utc();
        }

        job.record["state"] = "running";
        job.record["busy"] = true;
        if (!job.record.contains("ts_started") || job.record["ts_started"].is_null()) {
            job.record["ts_started"] = pqnas::now_iso_utc();
        }
        user_cleanup_record_set_phase(&job.record, "starting", 2, "worker started");
        (void)user_cleanup_record_write_atomic(job.job_id, job.record);

        user_cleanup_worker_audit("admin.user_storage_cleanup_started", "ok", job, json{
            {"phase", "starting"}
        });

        int fd_user_lock = -1;
        std::string user_lockp;

        auto close_user_lock = [&]() {
            if (fd_user_lock >= 0) {
                ::close(fd_user_lock);
                fd_user_lock = -1;
            }
            if (!user_lockp.empty()) {
                (void)std::filesystem::remove(user_lockp);
            }
        };

        bool ok = false;
        std::string fail_phase = "starting";
        std::string fail_reason;
        pqnas::UserStorageCleanupPlan plan;
        pqnas::UsersRegistry users_local;
        std::string err;
        std::uint64_t removed_entries = 0;

        do {
            fail_phase = "acquiring_lock";
            user_cleanup_record_set_phase(&job.record, "acquiring_lock", 5, "acquiring per-user lock");
            (void)user_cleanup_record_write_atomic(job.job_id, job.record);

            user_lockp = user_mig_lock_path(job.user_fp);
            {
                std::string lock_dir_err;
                if (!ensure_dir_fail_closed("/run/pqnas/locks", &lock_dir_err)) {
                    fail_reason = "lock_dir_failed: " + lock_dir_err;
                    break;
                }
            }
            {
                std::string lock_err;
                fd_user_lock = open_excl_lockfile(user_lockp, &lock_err);
                if (fd_user_lock < 0) {
                    if (lock_err.find("File exists") != std::string::npos) {
                        fail_reason = "user_storage_cleanup_busy: another storage operation is already in progress for this user";
                    } else {
                        fail_reason = "user_storage_cleanup_lock_failed";
                    }
                    if (!lock_err.empty()) fail_reason += ": " + lock_err;
                    break;
                }
            }

            fail_phase = "reloading_metadata";
            user_cleanup_record_set_phase(&job.record, "reloading_metadata", 10, "reloading latest user metadata");
            (void)user_cleanup_record_write_atomic(job.job_id, job.record);

            if (!users_local.load(users_path)) {
                fail_reason = "users.load failed";
                break;
            }

            fail_phase = "resolving_paths";
            user_cleanup_record_set_phase(&job.record, "resolving_paths", 20, "resolving active and old storage paths");
            (void)user_cleanup_record_write_atomic(job.job_id, job.record);

            if (!pqnas::resolve_user_storage_cleanup(users_local,
                                                     users_path,
                                                     job.user_fp,
                                                     job.expected_active_pool_id,
                                                     job.old_pool_id,
                                                     &plan,
                                                     &err)) {
                fail_reason = "resolve_failed: " + err;
                break;
            }

            job.record["resolved_active_pool_id"] = plan.active_pool_id;
            job.record["resolved_old_pool_id"] = plan.old_pool_id;
            job.record["resolved_active_root"] = plan.active_data_root.string();
            job.record["resolved_old_root"] = plan.old_data_root.string();
            job.record["resolved_active_user_dir"] = plan.active_user_dir.string();
            job.record["resolved_old_user_dir"] = plan.old_user_dir.string();
            (void)user_cleanup_record_write_atomic(job.job_id, job.record);

            fail_phase = "validating_active_mapping";
            user_cleanup_record_set_phase(&job.record, "validating_active_mapping", 35, "validating active pool mapping");
            (void)user_cleanup_record_write_atomic(job.job_id, job.record);

            fail_phase = "validating_old_copy";
            user_cleanup_record_set_phase(&job.record, "validating_old_copy", 50, "validating old inactive copy");
            (void)user_cleanup_record_write_atomic(job.job_id, job.record);

            if (!pqnas::validate_user_storage_cleanup(plan, &err)) {
                fail_reason = "validation_failed: " + err;
                break;
            }

            fail_phase = "deleting_old_copy";
            user_cleanup_record_set_phase(&job.record, "deleting_old_copy", 80, "deleting old inactive copy");
            (void)user_cleanup_record_write_atomic(job.job_id, job.record);

            if (!pqnas::delete_user_storage_old_copy(plan, &removed_entries, &err)) {
                fail_reason = "delete_failed: " + err;
                break;
            }

            job.record["result"]["removed_entries"] = removed_entries;
            job.record["result"]["old_pool_id"] = plan.old_pool_id;
            job.record["result"]["active_pool_id"] = plan.active_pool_id;
            job.record["result"]["root_rel"] = plan.root_rel;
            job.record["result"]["old_user_dir"] = plan.old_user_dir.string();
            job.record["result"]["active_user_dir"] = plan.active_user_dir.string();

            user_cleanup_record_set_phase(&job.record, "done", 100, "old inactive copy deleted");
            ok = true;
        } while (false);

        close_user_lock();

        if (ok) {
            user_cleanup_finalize_record(job.job_id, &job.record, true, "");

            user_cleanup_worker_audit("admin.user_storage_cleanup_succeeded", "ok", job, json{
                {"phase", "done"},
                {"active_pool_id", plan.active_pool_id},
                {"old_pool_id", plan.old_pool_id},
                {"removed_entries", removed_entries}
            });

            std::lock_guard<std::mutex> lk(g_user_cleanup_jobs_mu);
            g_user_cleanup_job_meta[job.job_id]["state"] = "done";
            g_user_cleanup_job_meta[job.job_id]["ts_finished"] = pqnas::now_iso_utc();
        } else {
            if (!plan.active_pool_id.empty()) job.record["result"]["active_pool_id"] = plan.active_pool_id;
            if (!plan.old_pool_id.empty()) job.record["result"]["old_pool_id"] = plan.old_pool_id;
            if (!plan.root_rel.empty()) job.record["result"]["root_rel"] = plan.root_rel;
            if (!plan.old_user_dir.empty()) job.record["result"]["old_user_dir"] = plan.old_user_dir.string();
            if (!plan.active_user_dir.empty()) job.record["result"]["active_user_dir"] = plan.active_user_dir.string();

            user_cleanup_finalize_record(job.job_id, &job.record, false, fail_reason.empty() ? "cleanup_failed" : fail_reason);

            user_cleanup_worker_audit("admin.user_storage_cleanup_failed", "fail", job, json{
                {"phase", fail_phase},
                {"reason", fail_reason.empty() ? "cleanup_failed" : fail_reason},
                {"active_pool_id", plan.active_pool_id},
                {"old_pool_id", plan.old_pool_id}
            });

            std::lock_guard<std::mutex> lk(g_user_cleanup_jobs_mu);
            g_user_cleanup_job_meta[job.job_id]["state"] = "failed";
            g_user_cleanup_job_meta[job.job_id]["error"] = fail_reason.empty() ? "cleanup_failed" : fail_reason;
            g_user_cleanup_job_meta[job.job_id]["ts_finished"] = pqnas::now_iso_utc();
        }
	}
}

static void user_mig_worker_start_once() {
    static std::atomic<bool> started{false};
    bool expected = false;
    if (!started.compare_exchange_strong(expected, true)) return;
    g_user_mig_worker_stop.store(false);
    g_user_mig_worker = std::thread(user_storage_migration_worker_main, g_users_path_for_user_workers);
}

static void user_mig_worker_stop_and_join() {
    g_user_mig_worker_stop.store(true);
    g_user_mig_jobs_cv.notify_all();
    if (g_user_mig_worker.joinable()) g_user_mig_worker.join();
}

static json user_mig_enqueue_job_fail_closed(const std::string& actor_fp,
                                             const std::string& user_fp,
                                             const std::string& target_pool_id,
                                             const std::string& remote_ip) {
    std::string state_dir_err;
    if (!ensure_dir_fail_closed("/run/pqnas/user-storage-migration", &state_dir_err)) {
        throw std::runtime_error("user_mig_state_dir_failed: " + state_dir_err);
    }

    const std::string job_id = user_mig_new_job_id();
    if (!is_sha256_hex_lower(job_id)) {
        throw std::runtime_error("job_id_generation_failed");
    }

    const std::string now = pqnas::now_iso_utc();

    json rec = {
        {"job_id", job_id},
        {"type", "user_storage_migration"},
        {"operation", "user_storage_migration"},
        {"actor_fp", actor_fp},
        {"user_fp", user_fp},
        {"requested_target_pool_id", target_pool_id},
        {"resolved_source_pool_id", nullptr},
        {"resolved_dest_pool_id", nullptr},
        {"resolved_source_root", nullptr},
        {"resolved_dest_root", nullptr},
        {"phase", "queued"},
        {"percent", 0},
        {"message", "queued"},
        {"events", json::array()},
        {"error", nullptr},
        {"result", json::object()},
        {"state", "queued"},
        {"busy", true},
        {"ok", true},
        {"ts_created", now},
        {"ts_started", nullptr},
        {"ts_end", nullptr},
        {"ts_last", now}
    };

    rec["events"].push_back(json{
        {"ts", now},
        {"phase", "queued"},
        {"percent", 0},
        {"message", "job created"}
    });

    if (!user_mig_record_write_atomic(job_id, rec)) {
        throw std::runtime_error("user_mig_record_write_failed");
    }

    user_mig_worker_start_once();

    UserStorageMigrationJob job;
    job.job_id = job_id;
    job.actor_fp = actor_fp;
    job.user_fp = user_fp;
    job.requested_target_pool_id = target_pool_id;
    job.remote_ip = remote_ip;
    job.record = rec;

    {
        std::lock_guard<std::mutex> lk(g_user_mig_jobs_mu);
        g_user_mig_jobs_q.push_back(job);
        g_user_mig_job_meta[job.job_id] = json{
            {"job_id", job.job_id},
            {"user_fp", user_fp},
            {"requested_target_pool_id", target_pool_id},
            {"record_path", user_mig_record_path(job.job_id)},
            {"state", "queued"},
            {"ts_created", now}
        };
    }
    g_user_mig_jobs_cv.notify_one();

    return json{
        {"ok", true},
        {"job_id", job_id},
        {"state", "queued"}
    };
}

static void user_cleanup_worker_start_once() {
    static std::atomic<bool> started{false};
    bool expected = false;
    if (!started.compare_exchange_strong(expected, true)) return;
    g_user_cleanup_worker_stop.store(false);
    g_user_cleanup_worker = std::thread(user_storage_cleanup_worker_main, g_users_path_for_user_workers);
}

static void user_cleanup_worker_stop_and_join() {
    g_user_cleanup_worker_stop.store(true);
    g_user_cleanup_jobs_cv.notify_all();
    if (g_user_cleanup_worker.joinable()) g_user_cleanup_worker.join();
}

static json user_cleanup_enqueue_job_fail_closed(const std::string& actor_fp,
                                                 const std::string& user_fp,
                                                 const std::string& expected_active_pool_id,
                                                 const std::string& old_pool_id,
                                                 const std::string& remote_ip) {
	(void)remote_ip;
    std::string dir_err;
    if (!ensure_dir_fail_closed("/run/pqnas/user-storage-cleanup", &dir_err)) {
        throw std::runtime_error("cleanup_state_dir_failed: " + dir_err);
    }

    user_cleanup_worker_start_once();

    const std::string job_id = sha256_hex_lower_evp(
        pqnas::now_iso_utc() + "|" +
        actor_fp + "|" +
        user_fp + "|" +
        expected_active_pool_id + "|" +
        old_pool_id + "|" +
        std::to_string((int)getpid()) + "|" +
        std::to_string((uint64_t)std::rand())
    );

    json rec = {
        {"job_id", job_id},
        {"type", "user_storage_cleanup"},
        {"operation", "user_storage_cleanup"},
        {"actor_fp", actor_fp},
        {"user_fp", user_fp},
        {"expected_active_pool_id", expected_active_pool_id},
        {"old_pool_id", old_pool_id},
        {"state", "queued"},
        {"busy", true},
        {"ok", true},
        {"phase", "queued"},
        {"percent", 0},
        {"message", "job created"},
        {"events", json::array()},
        {"result", json::object()},
        {"ts_created", pqnas::now_iso_utc()},
        {"ts_started", nullptr},
        {"ts_last", pqnas::now_iso_utc()},
        {"ts_end", nullptr}
    };

    user_cleanup_record_set_phase(&rec, "queued", 0, "job created");

    if (!user_cleanup_record_write_atomic(job_id, rec)) {
        throw std::runtime_error("cleanup_record_write_failed");
    }

    UserStorageCleanupJob job;
    job.job_id = job_id;
    job.actor_fp = actor_fp;
    job.user_fp = user_fp;
    job.expected_active_pool_id = expected_active_pool_id;
    job.old_pool_id = old_pool_id;
    job.record = rec;

    {
        std::lock_guard<std::mutex> lk(g_user_cleanup_jobs_mu);
        g_user_cleanup_jobs_q.push_back(job);
        g_user_cleanup_job_meta[job.job_id] = json{
            {"job_id", job.job_id},
            {"state", "queued"},
            {"ts_created", pqnas::now_iso_utc()}
        };
    }

    g_user_cleanup_jobs_cv.notify_one();

    return json{
        {"ok", true},
        {"job_id", job_id},
        {"state", "queued"}
    };
}
// ------------------------- storage/pools helpers -------------------------

static inline std::vector<std::string> split_lines(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == '\n') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

static inline bool starts_with(const std::string& s, const std::string& pfx) {
    return s.rfind(pfx, 0) == 0;
}

static inline std::string to_lower_ascii_copy(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    }
    return s;
}

[[maybe_unused]] static std::string upper_ascii(std::string s) {
    for (char& c : s) {
        if (c >= 'a' && c <= 'z')
            c = (char)(c - ('a' - 'A'));
    }
    return s;
}

// Parse btrfs filesystem show output for label/uuid/devices.
// Works with lines like: "Label: 'PQNAS_DATA'  uuid: 26a5..."
// and "Total devices 2 FS bytes used ..."
static inline void parse_btrfs_filesystem_show(const std::string& out,
                                               std::string* label,
                                               std::string* uuid,
                                               int* devices) {
    if (label) label->clear();
    if (uuid) uuid->clear();
    if (devices) *devices = -1;

    for (const std::string& raw : split_lines(out)) {
        const std::string line = trim_copy(raw);

        // Label + uuid on same line
        // Label: 'PQNAS_DATA'  uuid: 26a57d77-...
        if (starts_with(line, "Label:")) {
            // label between single quotes if present
            auto q1 = line.find('\'');
            if (q1 != std::string::npos) {
                auto q2 = line.find('\'', q1 + 1);
                if (q2 != std::string::npos && label) {
                    *label = line.substr(q1 + 1, q2 - (q1 + 1));
                }
            }
            // uuid: token
            auto up = line.find("uuid:");
            if (up != std::string::npos && uuid) {
                std::string u = trim_copy(line.substr(up + 5));
                // uuid may be followed by more text; take first token
                auto sp = u.find_first_of(" \t\r\n");
                if (sp != std::string::npos) u = u.substr(0, sp);
                *uuid = u;
            }
            continue;
        }

        if (starts_with(line, "Total devices")) {
            // "Total devices N ..."
            std::string rest = trim_copy(line.substr(std::string("Total devices").size()));
            // first token
            auto sp = rest.find_first_of(" \t\r\n");
            std::string n = (sp == std::string::npos) ? rest : rest.substr(0, sp);
            if (devices) {
                try { *devices = std::stoi(n); } catch (...) { /* ignore */ }
            }
            continue;
        }

        // fallback: if a line contains "uuid:" alone
        if (uuid && uuid->empty()) {
            auto up = line.find("uuid:");
            if (up != std::string::npos) {
                std::string u = trim_copy(line.substr(up + 5));
                auto sp = u.find_first_of(" \t\r\n");
                if (sp != std::string::npos) u = u.substr(0, sp);
                *uuid = u;
            }
        }
    }
}

struct ManagedPoolRef {
    std::string mount;
    std::string pool_id;
    std::string fs_label;
    std::string fs_uuid;
    bool managed{false};
};

static std::vector<ManagedPoolRef> managed_pools_from_cfg(const json& cfg) {
    std::vector<ManagedPoolRef> out;
    std::set<std::string> seen_mounts;

    // Prefer v2 pools object
    if (cfg.contains("pools") && cfg["pools"].is_object()) {
        for (auto it = cfg["pools"].begin(); it != cfg["pools"].end(); ++it) {
            const std::string mount = it.key();
            if (!it.value().is_object()) continue;

            const json& one = it.value();
            const bool managed = one.value("managed", false);
            if (!managed) continue;

            std::string pool_id = one.value("pool_id", "");
            if (pool_id.empty()) pool_id = pool_id_from_mount_best_effort(mount);

            if (!is_pool_id_safe(pool_id)) continue;

            ManagedPoolRef r;
            r.mount = mount;
            r.pool_id = pool_id;
            r.fs_label = one.value("fs_label", "");
            r.fs_uuid = one.value("fs_uuid", "");
            r.managed = true;

            out.push_back(r);
            seen_mounts.insert(mount);
        }
    }

    // Backward-compatible fallback to names_by_mount
    if (cfg.contains("names_by_mount") && cfg["names_by_mount"].is_object()) {
        for (auto it = cfg["names_by_mount"].begin(); it != cfg["names_by_mount"].end(); ++it) {
            const std::string mount = it.key();
            if (seen_mounts.count(mount)) continue;

            std::string pool_id;
            try {
                pool_id = it.value().is_string() ? it.value().get<std::string>() : "";
            } catch (...) {
                pool_id.clear();
            }

            if (!is_pool_id_safe(pool_id)) continue;

            ManagedPoolRef r;
            r.mount = mount;
            r.pool_id = pool_id;
            r.fs_label = btrfs_label_for_pool_id(pool_id);
            r.fs_uuid.clear();
            r.managed = true;

            out.push_back(r);
            seen_mounts.insert(mount);
        }
    }

    return out;
}

static bool is_btrfs_mount_active_at(const std::string& mount) {
    std::string mounts_out;
    // Security: call findmnt via argv, not a shell string, during startup restore.
    int rc = main_run_argv_capture_no_shell({
        "/usr/bin/findmnt", "-rn", "-t", "btrfs", "-o", "TARGET"
    }, &mounts_out, 1024u * 1024u);
    if (rc != 0) return false;

    rtrim_inplace(mounts_out);
    for (const std::string& raw : split_lines(mounts_out)) {
        const std::string line = trim_copy(raw);
        if (line == mount) return true;
    }
    return false;
}

static void pool_mounts_restore_managed(const std::string& users_path) {
	json cfg = load_or_init_pools_cfg(users_path);
	const auto managed = managed_pools_from_cfg(cfg);

	if (managed.empty()) {
    	std::cerr << "[pools] restore: no managed pools configured" << std::endl;
    	return;
	}
    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";
    const std::string pools_prefix = allowed_prefix + "/pools";

	for (const auto& mp : managed) {
    	const std::string& mount = mp.mount;
    	const std::string& pool_id = mp.pool_id;

        if (!is_pool_id_safe(pool_id)) {
            std::cerr << "[pools] restore: skip invalid pool_id for mount=" << mount << std::endl;
            continue;
        }

        if (mount.rfind(pools_prefix + "/", 0) != 0) {
            std::cerr << "[pools] restore: skip mount outside pools prefix: " << mount << std::endl;
            continue;
        }

        if (is_btrfs_mount_active_at(mount)) {
            std::lock_guard<std::mutex> lk(pool_mu());
            pool_mount_by_id()[pool_id] = mount;
            std::cerr << "[pools] restore: already mounted pool_id=" << pool_id
                      << " mount=" << mount << std::endl;
            continue;
        }

		std::string mount_spec;
		std::string log_spec;

		if (!mp.fs_uuid.empty()) {
    		mount_spec = "UUID=" + mp.fs_uuid;
		    log_spec = mount_spec;
		} else {
	    	const std::string label = !mp.fs_label.empty()
        		? mp.fs_label
	        	: btrfs_label_for_pool_id(pool_id);
    		mount_spec = "LABEL=" + label;
    		log_spec = mount_spec;
		}

        std::string out;
        int rc = 0;

        // Security: call pqnas-raid-root via sudo argv, not through shell quoting.
        rc = main_run_argv_capture_no_shell({
            "/usr/bin/sudo", "-n", "/usr/local/sbin/pqnas-raid-root", "mkdir-p", mount
        }, &out, 64u * 1024u);
        if (rc != 0) {
            std::cerr << "[pools] restore: mkdir failed pool_id=" << pool_id
                      << " mount=" << mount
                      << " out=" << pqnas::shorten(out, 300)
                      << std::endl;
            continue;
        }

        out.clear();
		rc = main_run_argv_capture_no_shell({
            "/usr/bin/sudo", "-n", "/usr/local/sbin/pqnas-raid-root",
            "mount-spec", mount_spec, mount
        }, &out, 64u * 1024u);
        if (rc != 0) {
		std::cerr << "[pools] restore: mount failed pool_id=" << pool_id
		          << " via=" << log_spec
        		  << " mount=" << mount
		          << " out=" << pqnas::shorten(out, 300)
          		<< std::endl;
            continue;
        }

        (void)main_run_argv_capture_no_shell({
            "/usr/bin/sudo", "-n", "/usr/local/sbin/pqnas-raid-root", "udev-settle"
        }, &out, 64u * 1024u);
        (void)main_run_argv_capture_no_shell({
            "/usr/bin/sudo", "-n", "/usr/local/sbin/pqnas-raid-root", "btrfs-device-scan"
        }, &out, 64u * 1024u);

        if (is_btrfs_mount_active_at(mount)) {
            std::lock_guard<std::mutex> lk(pool_mu());
            pool_mount_by_id()[pool_id] = mount;
		std::cerr << "[pools] restore: mounted pool_id=" << pool_id
        		  << " mount=" << mount
		          << " via=" << log_spec
        		  << std::endl;
        } else {
            std::cerr << "[pools] restore: mount command returned ok but mount not active"
                      << " pool_id=" << pool_id
                      << " mount=" << mount
                      << std::endl;
        }
    }
}


// Parse "btrfs filesystem df -b <mount>" output.
// We just want profile names for Data and Metadata.
// Example lines:
//   Data, RAID1: total=..., used=...
//   Metadata, RAID1: total=..., used=...
static inline void parse_btrfs_df_profiles(const std::string& out,
                                           std::string* profile_data,
                                           std::string* profile_metadata) {
    if (profile_data) profile_data->clear();
    if (profile_metadata) profile_metadata->clear();

    for (const std::string& raw : split_lines(out)) {
        const std::string line = trim_copy(raw);

        auto parse_line = [&](const std::string& prefix, std::string* dst) {
            if (!dst || !dst->empty()) return;
            if (!starts_with(line, prefix)) return;

            // Strip "Data," or "Metadata,"
            std::string rest = trim_copy(line.substr(prefix.size()));
            // rest starts with profile, then ":".
            auto colon = rest.find(':');
            if (colon == std::string::npos) return;
            std::string prof = trim_copy(rest.substr(0, colon));
            // Normalize to lower
            prof = to_lower_ascii_copy(prof);
            // Some outputs may have "raid1" already or "RAID1"
            *dst = prof;
        };

        parse_line("Data,", profile_data);
        parse_line("Metadata,", profile_metadata);
    }
}

// Parse "btrfs filesystem usage -b <mount>" output for size/used.
// Typical:
//   Device size:           296.00GiB
//   Used:                  21.50MiB
// With -b, it should be bytes on those lines (integers).
static inline void parse_btrfs_usage_bytes(const std::string& out,
                                           int64_t* size_bytes,
                                           int64_t* used_bytes,
                                           int64_t* free_estimated_bytes) {
    if (size_bytes) *size_bytes = -1;
    if (used_bytes) *used_bytes = -1;
    if (free_estimated_bytes) *free_estimated_bytes = -1;

    auto parse_int_after_key = [&](const std::string& line, const std::string& key, int64_t* dst) {
        if (!dst || *dst >= 0) return;

        auto pos = line.find(key);
        if (pos == std::string::npos) return;

        auto colon = line.find(':', pos + key.size());
        if (colon == std::string::npos) return;

        std::string rest = line.substr(colon + 1);

        size_t i = 0;
        while (i < rest.size() &&
               (rest[i] == ' ' || rest[i] == '\t' || rest[i] == '\r' || rest[i] == '\n')) {
            i++;
               }
        if (i) rest.erase(0, i);

        auto sp = rest.find_first_of(" \t\r\n");
        std::string tok = (sp == std::string::npos) ? rest : rest.substr(0, sp);

        try {
            *dst = std::stoll(tok);
        } catch (...) {
        }
    };

    for (const std::string& raw : split_lines(out)) {
        if (raw.empty()) continue;

        parse_int_after_key(raw, "Device size", size_bytes);
        parse_int_after_key(raw, "Used", used_bytes);
        parse_int_after_key(raw, "Free (estimated)", free_estimated_bytes);

        if (size_bytes && used_bytes && free_estimated_bytes &&
            *size_bytes >= 0 && *used_bytes >= 0 && *free_estimated_bytes >= 0) {
            break;
            }
    }
}

static std::string normalize_storage_pool_id(std::string v) {
    v = trim_copy(v);
    return v.empty() ? "default" : v;
}

static bool storage_pool_mount_by_id_adminonly(
    const std::string& users_path,
    const std::string& pool_id,
    std::string* out_mount,
    std::string* out_err)
{
    if (out_mount) out_mount->clear();
    if (out_err) out_err->clear();

    // Load pools.json for display names / stable IDs (same as endpoint)
    const json pools_cfg = load_or_init_pools_cfg(users_path);

    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";
    const std::string test_prefix  = "/srv/pqnas-test";
    const std::string test_prefix2 = "/srv/pqnas-test-btrfs";

    std::string mounts_out;
    // Security: call findmnt via argv, not a shell string, when locating managed pools.
    int rc = main_run_argv_capture_no_shell({
        "/usr/bin/findmnt", "-rn", "-t", "btrfs", "-o", "TARGET,SOURCE,FSTYPE"
    }, &mounts_out, 1024u * 1024u);
    cap_string(mounts_out, 1024 * 1024);
    rtrim_inplace(mounts_out);

    if (rc != 0) {
        if (out_err) *out_err = "findmnt_failed";
        return false;
    }

    for (const std::string& raw : split_lines(mounts_out)) {
        std::string line = trim_copy(raw);
        if (line.empty()) continue;

        // split into 3 tokens
        std::vector<std::string> toks;
        {
            std::string cur;
            for (char c : line) {
                if (c == ' ' || c == '\t') {
                    if (!cur.empty()) { toks.push_back(cur); cur.clear(); }
                } else {
                    cur.push_back(c);
                }
            }
            if (!cur.empty()) toks.push_back(cur);
        }
        if (toks.size() < 3) continue;

        const std::string target = toks[0];
        const std::string fstype = toks[2];

        if (fstype != "btrfs") continue;
        if (target.empty() || target[0] != '/') continue;

        const bool allowed =
            starts_with(target, allowed_prefix) ||
            starts_with(target, test_prefix) ||
            starts_with(target, test_prefix2);

        if (!allowed) continue;

        const std::string pid = pool_id_from_mount_best_effort(target);
        if (pid == pool_id) {
            if (out_mount) *out_mount = target;
            return true;
        }
    }

    if (out_err) *out_err = "pool_id_not_found";
    return false;
}

struct LandingPoolCandidate {
    std::string pool_id;
    std::string mount_path;
    std::uint64_t total_bytes = 0;
    std::uint64_t free_bytes = 0;
    bool mounted = false;
    bool writable = false;
    bool transport_usb = false;
    bool removable = false;
    bool eligible = false;
    std::vector<std::string> warnings;
};

static constexpr std::uint64_t kMinLandingFreeBytes = 5ull * 1024ull * 1024ull * 1024ull; // 5 GiB

static bool statvfs_path(const std::string& path, std::uint64_t* total_bytes, std::uint64_t* free_bytes) {
    if (total_bytes) *total_bytes = 0;
    if (free_bytes) *free_bytes = 0;

    struct statvfs sv {};
    if (::statvfs(path.c_str(), &sv) != 0) return false;

    const std::uint64_t frsize = (std::uint64_t)sv.f_frsize;
    if (total_bytes) *total_bytes = frsize * (std::uint64_t)sv.f_blocks;
    if (free_bytes)  *free_bytes  = frsize * (std::uint64_t)sv.f_bavail;
    return true;
}

static bool is_path_writable_dir(const std::string& path) {
    if (path.empty()) return false;
    if (::access(path.c_str(), W_OK) != 0) return false;

    struct stat st {};
    if (::stat(path.c_str(), &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

static std::string mounted_device_for_path(const std::string& mount_path) {
    try {
        const std::string target = std::filesystem::weakly_canonical(mount_path).string();

        std::ifstream f("/proc/mounts");
        if (!f.good()) return "";

        std::string best_dev;
        std::string best_mnt;

        std::string dev, mnt, fstype, opts;
        while (f >> dev >> mnt >> fstype >> opts) {
            std::string rest;
            std::getline(f, rest);

            std::string mnt_norm = mnt;
            try {
                mnt_norm = std::filesystem::weakly_canonical(mnt).string();
            } catch (...) {}

            const bool prefix_ok =
                (target == mnt_norm) ||
                (target.size() > mnt_norm.size() &&
                 target.compare(0, mnt_norm.size(), mnt_norm) == 0 &&
                 target[mnt_norm.size()] == '/');

            if (!prefix_ok) continue;

            if (mnt_norm.size() > best_mnt.size()) {
                best_mnt = mnt_norm;
                best_dev = dev;
            }
        }

        return best_dev;
    } catch (...) {}
    return "";
}

static std::string canonical_block_base_name(const std::string& dev_path) {
    if (dev_path.empty()) return "";
    std::string b = std::filesystem::path(dev_path).filename().string();
    if (b.empty()) return "";

    // nvme0n1p1 -> nvme0n1
    if (b.rfind("nvme", 0) == 0) {
        auto p = b.rfind('p');
        if (p != std::string::npos && p + 1 < b.size()) {
            bool tail_digits = true;
            for (size_t i = p + 1; i < b.size(); ++i) {
                if (!std::isdigit((unsigned char)b[i])) { tail_digits = false; break; }
            }
            if (tail_digits) return b.substr(0, p);
        }
        return b;
    }

    // mmcblk0p1 -> mmcblk0
    if (b.rfind("mmcblk", 0) == 0) {
        auto p = b.rfind('p');
        if (p != std::string::npos && p + 1 < b.size()) {
            bool tail_digits = true;
            for (size_t i = p + 1; i < b.size(); ++i) {
                if (!std::isdigit((unsigned char)b[i])) { tail_digits = false; break; }
            }
            if (tail_digits) return b.substr(0, p);
        }
        return b;
    }

    // sda1 -> sda, vda2 -> vda
    while (!b.empty() && std::isdigit((unsigned char)b.back())) b.pop_back();
    return b;
}

static bool read_small_text_file_trimmed(const std::string& path, std::string* out) {
    if (out) out->clear();
    try {
        std::ifstream f(path);
        if (!f.good()) return false;
        std::string s;
        std::getline(f, s);
        if (out) *out = trim_copy(s);
        return true;
    } catch (...) {
        return false;
    }
}

static bool detect_block_usb_or_removable(const std::string& dev_path, bool* is_usb, bool* is_removable) {
    if (is_usb) *is_usb = false;
    if (is_removable) *is_removable = false;

    if (dev_path.empty()) return false;
    if (dev_path.rfind("/dev/", 0) != 0) return false;

    const std::string base = canonical_block_base_name(dev_path);
    if (base.empty()) return false;

    try {
        const std::filesystem::path sys_block = std::filesystem::path("/sys/class/block") / base;

        // removable
        {
            std::string s;
            if (read_small_text_file_trimmed((sys_block / "removable").string(), &s)) {
                if (is_removable) *is_removable = (s == "1");
            }
        }

        // USB heuristic from resolved sysfs path
        std::error_code ec;
        const auto canon = std::filesystem::weakly_canonical(sys_block, ec);
        if (!ec) {
            const std::string p = canon.string();
            if (p.find("/usb") != std::string::npos || p.find("/usb/") != std::string::npos) {
                if (is_usb) *is_usb = true;
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

static json landing_pool_candidate_to_json(const LandingPoolCandidate& c) {
    return json{
        {"pool_id", c.pool_id},
        {"mount_path", c.mount_path},
        {"total_bytes", c.total_bytes},
        {"free_bytes", c.free_bytes},
        {"mounted", c.mounted},
        {"writable", c.writable},
        {"transport_usb", c.transport_usb},
        {"removable", c.removable},
        {"eligible", c.eligible},
        {"warnings", c.warnings}
    };
}

static LandingPoolCandidate inspect_landing_pool_candidate(const std::string& pool_id,
                                                           const std::string& mount_path) {
    LandingPoolCandidate c;
    c.pool_id = pool_id;
    c.mount_path = mount_path;

    if (pool_id.empty() || mount_path.empty()) {
        c.warnings.push_back("missing_mount");
        return c;
    }

    c.mounted = !mounted_device_for_path(mount_path).empty();
    if (!c.mounted) c.warnings.push_back("not_mounted");

    c.writable = is_path_writable_dir(mount_path);
    if (!c.writable) c.warnings.push_back("not_writable");

    if (!statvfs_path(mount_path, &c.total_bytes, &c.free_bytes)) {
        c.warnings.push_back("statvfs_failed");
    } else {
        if (c.free_bytes < kMinLandingFreeBytes) c.warnings.push_back("low_free_space");
    }

    {
        const std::string dev = mounted_device_for_path(mount_path);
        std::cerr << "[tiering-admin] pool_id=" << pool_id
          << " mount_path=" << mount_path
          << " resolved_dev=" << dev << "\n";
        bool is_usb = false, is_rem = false;
        if (!dev.empty()) {
            detect_block_usb_or_removable(dev, &is_usb, &is_rem);
        }
        c.transport_usb = is_usb;
        c.removable = is_rem;

        if (c.transport_usb) c.warnings.push_back("usb_blocked");
        if (c.removable) c.warnings.push_back("removable_blocked");
    }

    c.eligible =
        c.mounted &&
        c.writable &&
        c.total_bytes > 0 &&
        c.free_bytes >= kMinLandingFreeBytes &&
        !c.transport_usb &&
        !c.removable;

    return c;
}

static json build_upload_tiering_candidates_json() {
    json arr = json::array();

    // default pool first
    {
        auto c = inspect_landing_pool_candidate("default", pqnas::data_root_dir());
        arr.push_back(landing_pool_candidate_to_json(c));
    }

    {
        std::lock_guard<std::mutex> lk(pool_mu());
        for (const auto& kv : pool_mount_by_id()) {
            const std::string& pid = kv.first;
            const std::string& mount = kv.second;
            if (pid.empty() || pid == "default") continue;

            auto c = inspect_landing_pool_candidate(pid, mount);
            arr.push_back(landing_pool_candidate_to_json(c));
        }
    }

    return arr;
}

static bool find_upload_tiering_candidate_by_pool_id(const std::string& pool_id, json* out) {
    if (out) *out = json();
    const json arr = build_upload_tiering_candidates_json();
    if (!arr.is_array()) return false;

    for (const auto& it : arr) {
        if (!it.is_object()) continue;

        std::string pid;
        auto p = it.find("pool_id");
        if (p != it.end() && p->is_string()) pid = p->get<std::string>();

        if (pid == pool_id) {
            if (out) *out = it;
            return true;
        }
    }
    return false;
}
static bool validate_upload_tiering_pool_id(const std::string& pool_id, std::string* reason, json* candidate_out) {
    if (reason) reason->clear();
    if (candidate_out) *candidate_out = json();

    if (pool_id.empty()) {
        if (reason) *reason = "empty_pool_id";
        return false;
    }

    json c;
    if (!find_upload_tiering_candidate_by_pool_id(pool_id, &c) || !c.is_object()) {
        if (reason) *reason = "pool_not_found";
        return false;
    }

    if (candidate_out) *candidate_out = c;

    auto get_bool = [&](const char* key, bool defval) -> bool {
        auto it = c.find(key);
        if (it == c.end() || !it->is_boolean()) return defval;
        return it->get<bool>();
    };

    auto get_u64 = [&](const char* key, std::uint64_t defval) -> std::uint64_t {
        auto it = c.find(key);
        if (it == c.end()) return defval;
        try {
            if (it->is_number_unsigned()) return it->get<std::uint64_t>();
            if (it->is_number_integer()) {
                long long v = it->get<long long>();
                return v > 0 ? (std::uint64_t)v : defval;
            }
        } catch (...) {}
        return defval;
    };

    if (get_bool("transport_usb", false)) {
        if (reason) *reason = "usb_blocked";
        return false;
    }
    if (get_bool("removable", false)) {
        if (reason) *reason = "removable_blocked";
        return false;
    }
    if (!get_bool("mounted", false)) {
        if (reason) *reason = "not_mounted";
        return false;
    }
    if (!get_bool("writable", false)) {
        if (reason) *reason = "not_writable";
        return false;
    }
    if (get_u64("free_bytes", 0) < kMinLandingFreeBytes) {
        if (reason) *reason = "low_free_space";
        return false;
    }
    if (!get_bool("eligible", false)) {
        if (reason) *reason = "not_eligible";
        return false;
    }

    return true;
}

static std::string admin_settings_path_for_helpers() {
    if (const char* p_admin = std::getenv("PQNAS_ADMIN_SETTINGS_PATH")) {
        if (*p_admin) return std::string(p_admin);
    }

    if (const char* p_cfg = std::getenv("PQNAS_CONFIG")) {
        if (*p_cfg) {
            return (std::filesystem::path(p_cfg) / "admin_settings.json").string();
        }
    }

    return "/etc/pqnas/admin_settings.json";
}
// -----------------------------------------------------------------------------
// DNA Connect alerts helpers
// -----------------------------------------------------------------------------

static json load_admin_settings_json_safe() {
    try {
        const std::string path = admin_settings_path_for_helpers();
        std::ifstream f(path);
        if (!f.good()) return json::object();
        json j;
        f >> j;
        if (!j.is_object()) return json::object();
        return j;
    } catch (...) {
        return json::object();
    }
}

static json load_tiering_settings_json_safe() {
    try {
        json s = load_admin_settings_json_safe();
        if (s.contains("tiering") && s["tiering"].is_object()) {
            return s["tiering"];
        }
    } catch (...) {}
    return json::object();
}

static bool json_bool_or(const json& j, const char* key, bool def) {
    try {
        auto it = j.find(key);
        if (it != j.end() && it->is_boolean()) return it->get<bool>();
    } catch (...) {}
    return def;
}

static std::string json_string_or(const json& j, const char* key, const std::string& def) {
    try {
        auto it = j.find(key);
        if (it != j.end() && it->is_string()) return it->get<std::string>();
    } catch (...) {}
    return def;
}

static int json_int_or_clamped(const json& j, const char* key, int def, int lo, int hi) {
    try {
        auto it = j.find(key);
        if (it != j.end() && it->is_number_integer()) {
            int v = it->get<int>();
            if (v < lo) v = lo;
            if (v > hi) v = hi;
            return v;
        }
    } catch (...) {}
    return def;
}

struct UploadTieringConfig {
    bool enabled = false;
    std::string landing_pool_id;
};

static UploadTieringConfig upload_tiering_config() {
    UploadTieringConfig cfg;

    // Safe defaults
    cfg.enabled = false;
    cfg.landing_pool_id.clear();

    auto apply_and_validate = [&](bool enabled_in, const std::string& pool_in) -> bool {
        cfg.enabled = enabled_in;
        cfg.landing_pool_id = trim_copy(pool_in);

        if (!cfg.enabled) {
            cfg.landing_pool_id.clear();
            return true;
        }

        if (cfg.landing_pool_id.empty()) {
            std::cerr << "[tiering] enabled but landing_pool_id empty; disabling effective tiering\n";
            cfg.enabled = false;
            cfg.landing_pool_id.clear();
            return false;
        }

        std::string reason;
        json candidate;
        if (!validate_upload_tiering_pool_id(cfg.landing_pool_id, &reason, &candidate)) {
            std::cerr << "[tiering] landing pool invalid; disabling effective tiering"
                      << " pool_id=" << cfg.landing_pool_id
                      << " reason=" << reason << "\n";
            cfg.enabled = false;
            cfg.landing_pool_id.clear();
            return false;
        }

        return true;
    };

    // 1) Admin settings are source of truth
    // If settings file exists, but tiering key is absent/invalid, treat as disabled.
    try {
        json root = load_admin_settings_json_safe();
        if (root.is_object() && !root.empty()) {
            if (root.contains("tiering")) {
                const json& t = root["tiering"];
                if (t.is_object()) {
                    const bool enabled = json_bool_or(t, "enabled", false);
                    const std::string lp = json_string_or(t, "landing_pool_id", "");
                    apply_and_validate(enabled, lp);
                    return cfg;
                } else {
                    std::cerr << "[tiering] settings.tiering is not an object; treating tiering as disabled\n";
                    cfg.enabled = false;
                    cfg.landing_pool_id.clear();
                    return cfg;
                }
            }

            // Settings file exists, but no tiering block => disabled
            cfg.enabled = false;
            cfg.landing_pool_id.clear();
            return cfg;
        }
    } catch (...) {
        // fall through to env bootstrap fallback
    }

    // 2) Fallback to env vars only when settings could not be loaded
    bool enabled = false;
    std::string landing_pool_id;

    if (const char* en = std::getenv("PQNAS_TIERING_ENABLE")) {
        if (std::string(en) == "1") enabled = true;
    }

    if (const char* lp = std::getenv("PQNAS_TIERING_LANDING_POOL")) {
        if (*lp) landing_pool_id = lp;
    }

    apply_and_validate(enabled, landing_pool_id);
    return cfg;
}

static bool landing_root_for_pool_id(const std::string& pool_id,
                                     std::filesystem::path* out,
                                     std::string* err) {
    if (err) err->clear();
    if (!out) {
        if (err) *err = "null out";
        return false;
    }

    // Default / non-pooled landing area is still allowed.
    if (pool_id.empty() || pool_id == "default") {
        *out = std::filesystem::path(pqnas::data_root_dir()).parent_path() / "landing";
        return true;
    }

    std::string pool_mount;
    {
        std::lock_guard<std::mutex> lk(pool_mu());
        auto& m = pool_mount_by_id();
        auto it = m.find(pool_id);
        if (it != m.end()) pool_mount = it->second;
    }

    if (pool_mount.empty()) {
        if (err) *err = "landing_pool_not_found";
        return false;
    }

    *out = std::filesystem::path(pool_mount) / "landing";
    return true;
}

static std::int64_t now_epoch_sec() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

static bool build_landing_abs_path(const std::string& landing_pool_id,
                                   const std::string& fp_hex,
                                   const std::string& rel_norm,
                                   std::filesystem::path* out,
                                   std::string* err) {
    if (err) err->clear();
    if (!out) {
        if (err) *err = "null out";
        return false;
    }

    std::filesystem::path landing_root;
    if (!landing_root_for_pool_id(landing_pool_id, &landing_root, err)) {
        return false;
    }

    *out = landing_root / fp_hex / std::filesystem::path(rel_norm);
    return true;
}

static bool build_capacity_abs_path(pqnas::UsersRegistry& users,
                                    const std::string& fp_hex,
                                    const std::string& rel_norm,
                                    std::filesystem::path* out,
                                    std::string* err) {
    if (err) err->clear();
    if (!out) {
        if (err) *err = "null out";
        return false;
    }

    const std::filesystem::path user_dir = user_dir_for_fp(users, fp_hex);

    std::filesystem::path abs;
    if (!pqnas::resolve_user_path_strict(user_dir, rel_norm, &abs, err)) {
        return false;
    }

    *out = std::move(abs);
    return true;
}

static bool migrate_one_landing_file(pqnas::UsersRegistry& users,
                                     const std::string& fp_hex,
                                     const std::string& rel_norm,
                                     std::string* err) {
    if (err) err->clear();

    auto* idx = pqnas::get_file_location_index();
    if (!idx) {
        if (err) *err = "file location index not initialized";
        return false;
    }

    std::string gerr;
    auto rec_opt = idx->get(fp_hex, rel_norm, &gerr);
    if (!rec_opt.has_value()) {
        if (err) *err = gerr.empty() ? "metadata_not_found" : gerr;
        return false;
    }

    const auto& rec = *rec_opt;

    if (rec.tier_state != "landing") {
        if (err) *err = "not_in_landing_state";
        return false;
    }

    const std::filesystem::path src = rec.physical_path;

    std::error_code ec;
    auto st = std::filesystem::status(src, ec);
    if (ec || !std::filesystem::exists(st) || !std::filesystem::is_regular_file(st)) {
        if (err) *err = "source_missing";
        return false;
    }

    // Mark metadata as migrating before any copy work begins.
    {
        std::string merr;
        if (!idx->mark_migrating(fp_hex, rel_norm, rec.physical_path, &merr)) {
            if (err) *err = "mark_migrating_failed: " + merr;
            return false;
        }
    }

    auto revert_to_landing = [&]() {
        std::string rerr;
        (void)idx->mark_landing_again(fp_hex, rel_norm, rec.physical_path, &rerr);
    };

    std::filesystem::path dst_final;
    if (!build_capacity_abs_path(users, fp_hex, rel_norm, &dst_final, err)) {
        revert_to_landing();
        return false;
    }

    std::filesystem::create_directories(dst_final.parent_path(), ec);
    if (ec) {
        revert_to_landing();
        if (err) *err = "create_capacity_dirs_failed: " + ec.message();
        return false;
    }

    const std::filesystem::path dst_tmp =
        dst_final.parent_path() /
        (dst_final.filename().string() + ".tiercopy." + random_b64url(8) + ".tmp");

    // Copy source -> temp
    {
        std::ifstream in(src, std::ios::binary);
        if (!in.good()) {
            revert_to_landing();
            if (err) *err = "open_source_failed";
            return false;
        }

        std::ofstream out(dst_tmp, std::ios::binary | std::ios::trunc);
        if (!out.good()) {
            revert_to_landing();
            if (err) *err = "open_dest_tmp_failed";
            return false;
        }

        std::array<char, 256 * 1024> buf{};
        while (in.good()) {
            in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
            const std::streamsize n = in.gcount();
            if (n > 0) out.write(buf.data(), n);
        }

        out.flush();
        if (!out.good()) {
            std::filesystem::remove(dst_tmp, ec);
            revert_to_landing();
            if (err) *err = "write_dest_tmp_failed";
            return false;
        }
    }

    // Verify size
    const std::uint64_t src_sz = pqnas::file_size_u64_safe(src);
    const std::uint64_t dst_sz = pqnas::file_size_u64_safe(dst_tmp);
    if (src_sz != dst_sz) {
        std::filesystem::remove(dst_tmp, ec);
        revert_to_landing();
        if (err) *err = "verify_size_mismatch";
        return false;
    }

    // Promote temp -> final
    std::filesystem::rename(dst_tmp, dst_final, ec);
    if (ec) {
        std::filesystem::remove(dst_tmp, ec);
        revert_to_landing();
        if (err) *err = "rename_dest_tmp_failed: " + ec.message();
        return false;
    }

    const std::int64_t now_ts = now_epoch_sec();

    std::string serr;
    if (!idx->switch_to_capacity(fp_hex,
                                 rel_norm,
                                 rec.physical_path,
                                 rec.current_pool,
                                 dst_final.string(),
                                 now_ts,
                                 &serr)) {
        // Metadata switch failed; keep dst_final for inspection,
        // but move row back to landing so the file remains logically recoverable.
        revert_to_landing();
        if (err) *err = "metadata_switch_failed: " + serr;
        return false;
    }

    // Delete source only after metadata switch.
    // If this fails, keep capacity as authoritative and report failure.
    const bool removed = std::filesystem::remove(src, ec);
    if (ec || !removed) {
        if (err) *err = "source_delete_failed_after_switch";
        return false;
    }

    return true;
}


// ============================================================================
//  PHOTO / GALLERY STATS HELPERS
// ============================================================================

struct PhotoStatsRow {
    std::string rel_path;
    std::uint64_t size_bytes = 0;
    std::int64_t mtime_epoch = 0;

    std::string make;
    std::string model;
    std::string lens_model;
    std::int64_t iso = 0;
    double f_number = 0.0;
    double exposure_time = 0.0;
    double focal_length = 0.0;

    std::string taken_at;
    std::int64_t taken_epoch = 0;
    std::string taken_month;
    bool exif_ok = false;

    double gps_latitude = 0.0;
    double gps_longitude = 0.0;
    double gps_altitude = 0.0;
    bool gps_altitude_ok = false;
    bool gps_ok = false;
};

std::string shell_quote_single(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    out.push_back('\'');
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

bool is_supported_photo_ext(const std::filesystem::path& p) {
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    return ext == ".jpg"  || ext == ".jpeg" ||
           ext == ".png"  || ext == ".webp" ||
           ext == ".tif"  || ext == ".tiff" ||
           ext == ".heic" || ext == ".heif" ||
           ext == ".cr2"  || ext == ".cr3"  ||
           ext == ".nef"  || ext == ".arw"  ||
           ext == ".raf"  || ext == ".dng"  ||
           ext == ".rw2"  || ext == ".orf";
}

std::int64_t file_mtime_epoch_safe_local(const std::filesystem::path& p) {
    std::error_code ec;
    const auto ft = std::filesystem::last_write_time(p, ec);
    if (ec) return 0;

    using namespace std::chrono;
    const auto sctp = time_point_cast<system_clock::duration>(
        ft - decltype(ft)::clock::now() + system_clock::now());

    return static_cast<std::int64_t>(system_clock::to_time_t(sctp));
}

std::uint64_t file_size_u64_safe_local(const std::filesystem::path& p) {
    std::error_code ec;
    const auto sz = std::filesystem::file_size(p, ec);
    if (ec) return 0;
    return static_cast<std::uint64_t>(sz);
}

std::string normalize_rel_subpath_for_stats(const std::string& raw, bool* ok_out) {
    bool ok = true;

    std::string s = trim_copy(raw);
    std::replace(s.begin(), s.end(), '\\', '/');

    while (!s.empty() && s.front() == '/') s.erase(s.begin());
    while (!s.empty() && s.back() == '/') s.pop_back();

    if (s.empty()) {
        if (ok_out) *ok_out = true;
        return "";
    }

    std::filesystem::path p(s);
    if (p.is_absolute()) {
        if (ok_out) *ok_out = false;
        return "";
    }

    std::filesystem::path norm = p.lexically_normal();
    std::string out = norm.generic_string();

    if (out == "." || out.empty()) {
        if (ok_out) *ok_out = true;
        return "";
    }

    if (out == ".." || out.rfind("../", 0) == 0 || out.find("/../") != std::string::npos) {
        if (ok_out) *ok_out = false;
        return "";
    }

    if (ok_out) *ok_out = ok;
    return out;
}

std::string trim_trailing_zeroes(std::string s) {
    if (s.find('.') == std::string::npos) return s;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

std::string sql_col_text(sqlite3_stmt* st, int col) {
    const unsigned char* p = sqlite3_column_text(st, col);
    return p ? std::string(reinterpret_cast<const char*>(p)) : std::string();
}

bool sqlite_table_has_column(sqlite3* db, const char* table_name, const char* column_name) {
    if (!db || !table_name || !column_name) return false;

    sqlite3_stmt* st = nullptr;
    const std::string sql = "PRAGMA table_info(" + std::string(table_name) + ")";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) {
        return false;
    }

    bool found = false;
    while (sqlite3_step(st) == SQLITE_ROW) {
        const unsigned char* p = sqlite3_column_text(st, 1); // column name
        if (p && std::string(reinterpret_cast<const char*>(p)) == column_name) {
            found = true;
            break;
        }
    }

    sqlite3_finalize(st);
    return found;
}

bool ensure_photo_stats_schema(sqlite3* db, std::string* err) {
    const char* sql = R"SQL(
CREATE TABLE IF NOT EXISTS photo_exif_index (
    rel_path TEXT PRIMARY KEY,
    size_bytes INTEGER NOT NULL,
    mtime_epoch INTEGER NOT NULL,
    make TEXT,
    model TEXT,
    lens_model TEXT,
    iso INTEGER,
    f_number REAL,
    exposure_time REAL,
    focal_length REAL,
    taken_at TEXT,
    taken_epoch INTEGER NOT NULL DEFAULT 0,
    taken_month TEXT,
    gps_latitude REAL NOT NULL DEFAULT 0,
    gps_longitude REAL NOT NULL DEFAULT 0,
    gps_altitude REAL NOT NULL DEFAULT 0,
    gps_altitude_ok INTEGER NOT NULL DEFAULT 0,
    gps_ok INTEGER NOT NULL DEFAULT 0,
    exif_ok INTEGER NOT NULL DEFAULT 0,
    indexed_at INTEGER NOT NULL DEFAULT 0
);

CREATE INDEX IF NOT EXISTS idx_photo_exif_index_taken_month
    ON photo_exif_index(taken_month);

CREATE INDEX IF NOT EXISTS idx_photo_exif_index_model
    ON photo_exif_index(model);

CREATE INDEX IF NOT EXISTS idx_photo_exif_index_lens_model
    ON photo_exif_index(lens_model);

CREATE INDEX IF NOT EXISTS idx_photo_exif_index_iso
    ON photo_exif_index(iso);
)SQL";

    char* zerr = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &zerr);
    if (rc != SQLITE_OK) {
        if (err) *err = zerr ? zerr : "sqlite schema error";
        if (zerr) sqlite3_free(zerr);
        return false;
    }

    // Migration for older installs that already have photo_exif_index without taken_epoch.
    if (!sqlite_table_has_column(db, "photo_exif_index", "taken_epoch")) {
        const char* alter_sql =
            "ALTER TABLE photo_exif_index "
            "ADD COLUMN taken_epoch INTEGER NOT NULL DEFAULT 0;";

        char* alter_err = nullptr;
        const int alter_rc = sqlite3_exec(db, alter_sql, nullptr, nullptr, &alter_err);
        if (alter_rc != SQLITE_OK) {
            if (err) *err = alter_err ? alter_err : "failed to add taken_epoch column";
            if (alter_err) sqlite3_free(alter_err);
            return false;
        }
    }

    if (!sqlite_table_has_column(db, "photo_exif_index", "gps_latitude")) {
        const char* alter_sql =
            "ALTER TABLE photo_exif_index "
            "ADD COLUMN gps_latitude REAL NOT NULL DEFAULT 0;";
        char* alter_err = nullptr;
        const int alter_rc = sqlite3_exec(db, alter_sql, nullptr, nullptr, &alter_err);
        if (alter_rc != SQLITE_OK) {
            if (err) *err = alter_err ? alter_err : "failed to add gps_latitude column";
            if (alter_err) sqlite3_free(alter_err);
            return false;
        }
    }

    if (!sqlite_table_has_column(db, "photo_exif_index", "gps_longitude")) {
        const char* alter_sql =
            "ALTER TABLE photo_exif_index "
            "ADD COLUMN gps_longitude REAL NOT NULL DEFAULT 0;";
        char* alter_err = nullptr;
        const int alter_rc = sqlite3_exec(db, alter_sql, nullptr, nullptr, &alter_err);
        if (alter_rc != SQLITE_OK) {
            if (err) *err = alter_err ? alter_err : "failed to add gps_longitude column";
            if (alter_err) sqlite3_free(alter_err);
            return false;
        }
    }

    if (!sqlite_table_has_column(db, "photo_exif_index", "gps_altitude")) {
        const char* alter_sql =
            "ALTER TABLE photo_exif_index "
            "ADD COLUMN gps_altitude REAL NOT NULL DEFAULT 0;";
        char* alter_err = nullptr;
        const int alter_rc = sqlite3_exec(db, alter_sql, nullptr, nullptr, &alter_err);
        if (alter_rc != SQLITE_OK) {
            if (err) *err = alter_err ? alter_err : "failed to add gps_altitude column";
            if (alter_err) sqlite3_free(alter_err);
            return false;
        }
    }

    if (!sqlite_table_has_column(db, "photo_exif_index", "gps_ok")) {
        const char* alter_sql =
            "ALTER TABLE photo_exif_index "
            "ADD COLUMN gps_ok INTEGER NOT NULL DEFAULT 0;";
        char* alter_err = nullptr;
        const int alter_rc = sqlite3_exec(db, alter_sql, nullptr, nullptr, &alter_err);
        if (alter_rc != SQLITE_OK) {
            if (err) *err = alter_err ? alter_err : "failed to add gps_ok column";
            if (alter_err) sqlite3_free(alter_err);
            return false;
        }
    }

    if (!sqlite_table_has_column(db, "photo_exif_index", "gps_altitude_ok")) {
        const char* alter_sql =
            "ALTER TABLE photo_exif_index "
            "ADD COLUMN gps_altitude_ok INTEGER NOT NULL DEFAULT 0;";
        char* alter_err = nullptr;
        const int alter_rc = sqlite3_exec(db, alter_sql, nullptr, nullptr, &alter_err);
        if (alter_rc != SQLITE_OK) {
            if (err) *err = alter_err ? alter_err : "failed to add gps_altitude_ok column";
            if (alter_err) sqlite3_free(alter_err);
            return false;
        }
    }
    return true;
}

std::int64_t exif_taken_epoch_from_string(const std::string& s) {
    // EXIF DateTimeOriginal / CreateDate usually comes as:
    // YYYY:MM:DD HH:MM:SS
    // For burst grouping v1, treat it as naive local camera time.
    if (s.size() < 19) return 0;

    auto is_digit_local = [](char c) -> bool {
        return c >= '0' && c <= '9';
    };

    if (!(is_digit_local(s[0]) && is_digit_local(s[1]) && is_digit_local(s[2]) && is_digit_local(s[3]) &&
          s[4] == ':' &&
          is_digit_local(s[5]) && is_digit_local(s[6]) &&
          s[7] == ':' &&
          is_digit_local(s[8]) && is_digit_local(s[9]) &&
          s[10] == ' ' &&
          is_digit_local(s[11]) && is_digit_local(s[12]) &&
          s[13] == ':' &&
          is_digit_local(s[14]) && is_digit_local(s[15]) &&
          s[16] == ':' &&
          is_digit_local(s[17]) && is_digit_local(s[18]))) {
        return 0;
          }

    try {
        std::tm tm{};
        tm.tm_year = std::stoi(s.substr(0, 4)) - 1900;
        tm.tm_mon  = std::stoi(s.substr(5, 2)) - 1;
        tm.tm_mday = std::stoi(s.substr(8, 2));
        tm.tm_hour = std::stoi(s.substr(11, 2));
        tm.tm_min  = std::stoi(s.substr(14, 2));
        tm.tm_sec  = std::stoi(s.substr(17, 2));
        tm.tm_isdst = -1;

        const std::time_t tt = std::mktime(&tm);
        if (tt <= 0) return 0;
        return static_cast<std::int64_t>(tt);
    } catch (...) {
        return 0;
    }
}

bool load_cached_photo_stats_row(sqlite3* db,
                                 const std::string& rel_path,
                                 PhotoStatsRow* out) {
    if (!db || !out) return false;

    sqlite3_stmt* st = nullptr;
    const char* sql = R"SQL(
SELECT rel_path, size_bytes, mtime_epoch,
       make, model, lens_model,
       iso, f_number, exposure_time, focal_length,
        taken_at, taken_epoch, taken_month,
        gps_latitude, gps_longitude, gps_altitude, gps_altitude_ok, gps_ok,
        exif_ok
FROM photo_exif_index
WHERE rel_path = ?1
LIMIT 1
)SQL";

    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(st, 1, rel_path.c_str(), -1, SQLITE_TRANSIENT);

    const int rc = sqlite3_step(st);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(st);
        return false;
    }

    out->rel_path = sql_col_text(st, 0);
    out->size_bytes = static_cast<std::uint64_t>(sqlite3_column_int64(st, 1));
    out->mtime_epoch = static_cast<std::int64_t>(sqlite3_column_int64(st, 2));
    out->make = sql_col_text(st, 3);
    out->model = sql_col_text(st, 4);
    out->lens_model = sql_col_text(st, 5);
    out->iso = static_cast<std::int64_t>(sqlite3_column_int64(st, 6));
    out->f_number = sqlite3_column_double(st, 7);
    out->exposure_time = sqlite3_column_double(st, 8);
    out->focal_length = sqlite3_column_double(st, 9);
    out->taken_at = sql_col_text(st, 10);
    out->taken_epoch = static_cast<std::int64_t>(sqlite3_column_int64(st, 11));
    out->taken_month = sql_col_text(st, 12);
    out->gps_latitude = sqlite3_column_double(st, 13);
    out->gps_longitude = sqlite3_column_double(st, 14);
    out->gps_altitude = sqlite3_column_double(st, 15);
    out->gps_altitude_ok = sqlite3_column_int(st, 16) != 0;
    out->gps_ok = sqlite3_column_int(st, 17) != 0;
    out->exif_ok = sqlite3_column_int(st, 18) != 0;

    sqlite3_finalize(st);

    // Compatibility for old rows that already had taken_at but not taken_epoch yet.
    if (out->taken_epoch == 0 && !out->taken_at.empty()) {
        out->taken_epoch = exif_taken_epoch_from_string(out->taken_at);
    }

    return true;
}
bool upsert_photo_stats_row(sqlite3* db,
                            const PhotoStatsRow& row,
                            std::int64_t indexed_at_epoch,
                            std::string* err) {
    if (!db) return false;

    sqlite3_stmt* st = nullptr;
    const char* sql = R"SQL(
INSERT INTO photo_exif_index (
    rel_path, size_bytes, mtime_epoch,
    make, model, lens_model,
    iso, f_number, exposure_time, focal_length,
    taken_at, taken_epoch, taken_month,
    gps_latitude, gps_longitude, gps_altitude, gps_altitude_ok, gps_ok,
    exif_ok, indexed_at
) VALUES (
    ?1, ?2, ?3,
    ?4, ?5, ?6,
    ?7, ?8, ?9, ?10,
    ?11, ?12, ?13,
    ?14, ?15, ?16, ?17, ?18,
    ?19, ?20
)
ON CONFLICT(rel_path) DO UPDATE SET
    size_bytes = excluded.size_bytes,
    mtime_epoch = excluded.mtime_epoch,
    make = excluded.make,
    model = excluded.model,
    lens_model = excluded.lens_model,
    iso = excluded.iso,
    f_number = excluded.f_number,
    exposure_time = excluded.exposure_time,
    focal_length = excluded.focal_length,
    taken_at = excluded.taken_at,
    taken_epoch = excluded.taken_epoch,
    taken_month = excluded.taken_month,
    gps_latitude = excluded.gps_latitude,
    gps_longitude = excluded.gps_longitude,
    gps_altitude = excluded.gps_altitude,
    gps_altitude_ok = excluded.gps_altitude_ok,
    gps_ok = excluded.gps_ok,
    exif_ok = excluded.exif_ok,
    indexed_at = excluded.indexed_at
)SQL";

    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
        if (err) *err = sqlite3_errmsg(db);
        return false;
    }

    sqlite3_bind_text(st, 1, row.rel_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, static_cast<sqlite3_int64>(row.size_bytes));
    sqlite3_bind_int64(st, 3, static_cast<sqlite3_int64>(row.mtime_epoch));
    sqlite3_bind_text(st, 4, row.make.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, row.model.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 6, row.lens_model.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 7, static_cast<sqlite3_int64>(row.iso));
    sqlite3_bind_double(st, 8, row.f_number);
    sqlite3_bind_double(st, 9, row.exposure_time);
    sqlite3_bind_double(st, 10, row.focal_length);
    sqlite3_bind_text(st, 11, row.taken_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 12, static_cast<sqlite3_int64>(row.taken_epoch));
    sqlite3_bind_text(st, 13, row.taken_month.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(st, 14, row.gps_latitude);
    sqlite3_bind_double(st, 15, row.gps_longitude);
    sqlite3_bind_double(st, 16, row.gps_altitude);
    sqlite3_bind_int(st, 17, row.gps_altitude_ok ? 1 : 0);
    sqlite3_bind_int(st, 18, row.gps_ok ? 1 : 0);
    sqlite3_bind_int(st, 19, row.exif_ok ? 1 : 0);
    sqlite3_bind_int64(st, 20, static_cast<sqlite3_int64>(indexed_at_epoch));

    const int rc = sqlite3_step(st);
    if (rc != SQLITE_DONE) {
        if (err) *err = sqlite3_errmsg(db);
        sqlite3_finalize(st);
        return false;
    }

    sqlite3_finalize(st);
    return true;
}

std::string exif_taken_month_from_string(const std::string& s) {
    if (s.size() >= 7 && std::isdigit(static_cast<unsigned char>(s[0]))) {
        if (s[4] == ':' || s[4] == '-') {
            if (std::isdigit(static_cast<unsigned char>(s[5])) &&
                std::isdigit(static_cast<unsigned char>(s[6]))) {
                std::string out;
                out.reserve(7);
                out.append(s, 0, 4);
                out.push_back('-');
                out.append(s, 5, 2);
                return out;
                }
        }
    }
    return "";
}

std::string read_command_stdout(const std::string& cmd) {
    std::string out;
    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) return out;

    char buf[4096];
    while (true) {
        const std::size_t n = std::fread(buf, 1, sizeof(buf), fp);
        if (n == 0) break;
        out.append(buf, n);
    }

    pclose(fp);
    return out;
}

PhotoStatsRow exif_row_from_file(const std::string& rel_path,
                                 const std::filesystem::path& abs_path,
                                 std::uint64_t size_bytes,
                                 std::int64_t mtime_epoch) {
    PhotoStatsRow row;
    row.rel_path = rel_path;
    row.size_bytes = size_bytes;
    row.mtime_epoch = mtime_epoch;

    const std::string cmd =
        "exiftool -json -n "
        "-Make -Model -LensModel -ISO -FNumber -ExposureTime -FocalLength "
        "-DateTimeOriginal -CreateDate "
        "-GPSLatitude -GPSLongitude -GPSAltitude -GPSLatitudeRef -GPSLongitudeRef "
        + shell_quote_single(abs_path.string()) + " 2>/dev/null";

    const std::string raw = read_command_stdout(cmd);
    if (raw.empty()) {
        return row;
    }

    try {
        const json j = json::parse(raw);
        if (!j.is_array() || j.empty() || !j[0].is_object()) {
            return row;
        }

        const json& o = j[0];

        row.make = o.value("Make", "");
        row.model = o.value("Model", "");
        row.lens_model = o.value("LensModel", "");
        row.iso = o.contains("ISO") ? static_cast<std::int64_t>(o.value("ISO", 0.0)) : 0;
        row.f_number = o.value("FNumber", 0.0);
        row.exposure_time = o.value("ExposureTime", 0.0);
        row.focal_length = o.value("FocalLength", 0.0);

        row.taken_at = o.value("DateTimeOriginal", "");
        if (row.taken_at.empty()) {
            row.taken_at = o.value("CreateDate", "");
        }

        row.taken_month = exif_taken_month_from_string(row.taken_at);

        const bool has_gps_lat = o.contains("GPSLatitude") && o["GPSLatitude"].is_number();
        const bool has_gps_lon = o.contains("GPSLongitude") && o["GPSLongitude"].is_number();
        const bool has_gps_alt = o.contains("GPSAltitude") && o["GPSAltitude"].is_number();

        if (has_gps_lat) row.gps_latitude = o["GPSLatitude"].get<double>();
        if (has_gps_lon) row.gps_longitude = o["GPSLongitude"].get<double>();
        if (has_gps_alt) {
            row.gps_altitude = o["GPSAltitude"].get<double>();
            row.gps_altitude_ok = true;
        }

        const std::string lat_ref = trim_copy(o.value("GPSLatitudeRef", ""));
        const std::string lon_ref = trim_copy(o.value("GPSLongitudeRef", ""));

        if (!lat_ref.empty()) {
            const char c = (char)std::toupper((unsigned char)lat_ref[0]);
            if (c == 'S') row.gps_latitude = -std::abs(row.gps_latitude);
            else if (c == 'N') row.gps_latitude = std::abs(row.gps_latitude);
        }

        if (!lon_ref.empty()) {
            const char c = (char)std::toupper((unsigned char)lon_ref[0]);
            if (c == 'W') row.gps_longitude = -std::abs(row.gps_longitude);
            else if (c == 'E') row.gps_longitude = std::abs(row.gps_longitude);
        }

        row.gps_ok = has_gps_lat && has_gps_lon;

        row.exif_ok =
            !row.make.empty() ||
            !row.model.empty() ||
            !row.lens_model.empty() ||
            row.iso > 0 ||
            row.f_number > 0.0 ||
            row.exposure_time > 0.0 ||
            row.focal_length > 0.0 ||
            !row.taken_at.empty() ||
            row.gps_ok;
    } catch (...) {
        return row;
    }

    return row;
}

bool refresh_one_photo_exif_row(sqlite3* db,
                                const std::string& rel_path,
                                const std::filesystem::path& abs_path,
                                std::uint64_t size_bytes,
                                std::int64_t mtime_epoch,
                                PhotoStatsRow* out,
                                std::string* err) {
    if (!db) {
        if (err) *err = "db_null";
        return false;
    }

    PhotoStatsRow row = exif_row_from_file(rel_path, abs_path, size_bytes, mtime_epoch);
    const std::int64_t indexed_now = static_cast<std::int64_t>(std::time(nullptr));

    std::string upsert_err;
    if (!upsert_photo_stats_row(db, row, indexed_now, &upsert_err)) {
        if (err) *err = upsert_err;
        return false;
    }

    if (out) *out = std::move(row);
    return true;
}

std::string lens_label_from_row(const PhotoStatsRow& row) {
    return trim_copy(row.lens_model);
}

std::string camera_label_from_row(const PhotoStatsRow& row) {
    const std::string make = trim_copy(row.make);
    const std::string model = trim_copy(row.model);

    if (!make.empty() && !model.empty()) {
        if (model.rfind(make, 0) == 0) return model;
        return make + " " + model;
    }
    if (!model.empty()) return model;
    if (!make.empty()) return make;
    return "";
}
std::string iso_bucket_from_row(const PhotoStatsRow& row) {
    if (row.iso <= 0) return "";
    return std::to_string(row.iso);
}

std::string aperture_bucket_from_row(const PhotoStatsRow& row) {
    if (row.f_number <= 0.0) return "";
    std::ostringstream oss;
    oss << "f/" << trim_trailing_zeroes(std::to_string(row.f_number));
    return oss.str();
}

std::string shutter_bucket_from_row(const PhotoStatsRow& row) {
    const double t = row.exposure_time;
    if (!(t > 0.0)) return "";

    if (t >= 1.0) {
        std::ostringstream oss;
        oss << trim_trailing_zeroes(std::to_string(t)) << "s";
        return oss.str();
    }

    const long long den = static_cast<long long>(std::llround(1.0 / t));
    if (den > 0) return "1/" + std::to_string(den);

    return "";
}

std::string focal_bucket_from_row(const PhotoStatsRow& row) {
    if (row.focal_length <= 0.0) return "";

    const double rounded = std::round(row.focal_length * 10.0) / 10.0;
    std::ostringstream oss;
    oss << trim_trailing_zeroes(std::to_string(rounded)) << "mm";
    return oss.str();
}

void bump_bucket(std::map<std::string, int>& m, const std::string& key) {
    if (!key.empty()) ++m[key];
}

json top_bucket_array(const std::map<std::string, int>& m, std::size_t limit) {
    std::vector<std::pair<std::string, int>> v(m.begin(), m.end());

    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
    });

    json out = json::array();
    std::size_t n = 0;
    for (const auto& kv : v) {
        if (n++ >= limit) break;
        out.push_back({
            {"label", kv.first},
            {"count", kv.second}
        });
    }
    return out;
}

struct TieringWorkerConfig {
    bool enabled = false;
    int interval_sec = 60;
    int min_age_sec = 60;
    std::size_t max_candidates_per_pass = 8;
};

static TieringWorkerConfig tiering_worker_config() {
    TieringWorkerConfig cfg;

    // Defaults
    cfg.enabled = false;
    cfg.interval_sec = 60;
    cfg.min_age_sec = 60;
    cfg.max_candidates_per_pass = 8;

    // 1) Admin settings first
    {
        const json t = load_tiering_settings_json_safe();
        if (t.is_object() && !t.empty()) {
            cfg.enabled = json_bool_or(t, "enabled", false);

            cfg.interval_sec = json_int_or_clamped(t, "worker_interval_sec", 60, 5, 3600);
            cfg.min_age_sec = json_int_or_clamped(t, "min_age_sec", 60, 0, 86400);

            {
                const int v = json_int_or_clamped(t, "max_candidates_per_pass", 8, 1, 1000);
                cfg.max_candidates_per_pass = static_cast<std::size_t>(v);
            }

            if (t.contains("worker_enabled") && t["worker_enabled"].is_boolean()) {
                cfg.enabled = t["worker_enabled"].get<bool>() && json_bool_or(t, "enabled", false);
            }

            return cfg;
        }
    }

    // 2) Env fallback
    if (const char* en = std::getenv("PQNAS_TIERING_ENABLE")) {
        if (std::string(en) == "1") cfg.enabled = true;
    }

    if (const char* p = std::getenv("PQNAS_TIERING_WORKER_INTERVAL_SEC")) {
        try { cfg.interval_sec = std::max(5, std::stoi(p)); } catch (...) {}
    }

    if (const char* p = std::getenv("PQNAS_TIERING_MIN_AGE_SEC")) {
        try { cfg.min_age_sec = std::max(0, std::stoi(p)); } catch (...) {}
    }

    if (const char* p = std::getenv("PQNAS_TIERING_MAX_CANDIDATES")) {
        try {
            const int v = std::max(1, std::stoi(p));
            cfg.max_candidates_per_pass = static_cast<std::size_t>(v);
        } catch (...) {}
    }

    return cfg;
}

static bool tiering_candidate_old_enough(const pqnas::FileLocationRecord& rec,
                                         int min_age_sec,
                                         std::int64_t now_ts) {
    if (min_age_sec <= 0) return true;
    if (rec.updated_epoch <= 0) return true;
    return (now_ts - rec.updated_epoch) >= min_age_sec;
}


static void tiering_recover_stuck_migrating_files() {
    auto* idx = pqnas::get_file_location_index();
    if (!idx) return;

    const std::int64_t now_ts = now_epoch_sec();
    const std::int64_t cutoff = now_ts - 120; // phase-1 simple threshold

    std::string lerr;
    auto stuck = idx->list_stuck_migrating_candidates(cutoff, &lerr);
    if (!lerr.empty()) {
        std::cerr << "[tiering-worker] stuck-migrating query failed: " << lerr << "\n";
        return;
    }

    for (const auto& rec : stuck) {
        std::error_code ec;
        auto st = std::filesystem::status(rec.physical_path, ec);
        const bool src_exists =
            !ec &&
            std::filesystem::exists(st) &&
            std::filesystem::is_regular_file(st);

        if (!src_exists) {
            // Do not blindly revert if source is gone.
            continue;
        }

        std::string rerr;
        if (idx->mark_landing_again(rec.fp, rec.logical_rel_path, rec.physical_path, &rerr)) {
            pqnas::AuditEvent ev;
            ev.event = "storage.tiering_recover_to_landing";
            ev.outcome = "ok";
            ev.f["fingerprint"] = rec.fp;
            ev.f["path"] = pqnas::shorten(rec.logical_rel_path, 200);
            audit_append(ev);
        } else {
            std::cerr << "[tiering-worker] recover_to_landing failed path="
                      << rec.logical_rel_path << " err=" << rerr << "\n";
        }
    }
}
static std::string guess_download_mime_from_name(const std::string& name) {
    auto lower = [](std::string s) {
        for (char& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    };

    const std::string n = lower(name);
    const auto dot = n.rfind('.');
    const std::string ext = (dot == std::string::npos) ? "" : n.substr(dot + 1);

    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif") return "image/gif";
    if (ext == "webp") return "image/webp";
    if (ext == "svg") return "image/svg+xml";
    if (ext == "bmp") return "image/bmp";
    if (ext == "ico") return "image/x-icon";

    if (ext == "mp4" || ext == "m4v") return "video/mp4";
    if (ext == "mov") return "video/quicktime";
    if (ext == "webm") return "video/webm";
    if (ext == "mkv") return "video/x-matroska";
    if (ext == "avi") return "video/x-msvideo";
    if (ext == "3gp" || ext == "3gpp") return "video/3gpp";

    if (ext == "mp3") return "audio/mpeg";
    if (ext == "m4a") return "audio/mp4";
    if (ext == "aac") return "audio/aac";
    if (ext == "wav") return "audio/wav";
    if (ext == "ogg" || ext == "oga") return "audio/ogg";
    if (ext == "opus") return "audio/opus";
    if (ext == "flac") return "audio/flac";

    if (ext == "txt" || ext == "log" || ext == "md") return "text/plain; charset=utf-8";
    if (ext == "json") return "application/json; charset=utf-8";
    if (ext == "html" || ext == "htm") return "text/html; charset=utf-8";
    if (ext == "css") return "text/css; charset=utf-8";
    if (ext == "js") return "application/javascript; charset=utf-8";
    if (ext == "xml") return "application/xml; charset=utf-8";
    if (ext == "csv") return "text/csv; charset=utf-8";
    if (ext == "pdf") return "application/pdf";

    return "application/octet-stream";
}

static bool is_inline_preview_mime(const std::string& mime) {
    return mime == "image/png" ||
           mime == "image/jpeg" ||
           mime == "image/gif" ||
           mime == "image/webp" ||
           mime == "image/svg+xml" ||
           mime == "image/bmp" ||
           mime == "image/x-icon" ||

           mime == "video/mp4" ||
           mime == "video/quicktime" ||
           mime == "video/webm" ||
           mime == "video/x-matroska" ||
           mime == "video/x-msvideo" ||
           mime == "video/3gpp" ||

           mime == "audio/mpeg" ||
           mime == "audio/mp4" ||
           mime == "audio/aac" ||
           mime == "audio/wav" ||
           mime == "audio/ogg" ||
           mime == "audio/opus" ||
           mime == "audio/flac";
}

static void tiering_worker_loop(pqnas::UsersRegistry* users,
                                std::atomic<bool>* stop_flag) {
	std::cerr << "[tiering-worker] started\n";
    if (!users || !stop_flag) return;

    while (!stop_flag->load()) {
        const TieringWorkerConfig cfg = tiering_worker_config();

        if (!cfg.enabled) {
            for (int i = 0; i < 5 && !stop_flag->load(); ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            continue;
        }

        auto* idx = pqnas::get_file_location_index();
        if (!idx) {
            std::cerr << "[tiering-worker] file location index not initialized\n";
            for (int i = 0; i < cfg.interval_sec && !stop_flag->load(); ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            continue;
        }

		tiering_recover_stuck_migrating_files();

        std::string lerr;
        auto candidates = idx->list_landing_candidates(cfg.max_candidates_per_pass, &lerr);
        if (!lerr.empty()) {
            std::cerr << "[tiering-worker] list_landing_candidates failed: " << lerr << "\n";
        } else {
            const std::int64_t now_ts = now_epoch_sec();

            for (const auto& rec : candidates) {
                if (stop_flag->load()) break;
                if (!tiering_candidate_old_enough(rec, cfg.min_age_sec, now_ts)) continue;

                std::string merr;
                const bool ok = migrate_one_landing_file(*users, rec.fp, rec.logical_rel_path, &merr);

                pqnas::AuditEvent ev;
                ev.event = ok ? "storage.tiering_auto_migrate_ok"
                              : "storage.tiering_auto_migrate_fail";
                ev.outcome = ok ? "ok" : "fail";
                ev.f["fingerprint"] = rec.fp;
                ev.f["path"] = pqnas::shorten(rec.logical_rel_path, 200);
                ev.f["from_pool"] = rec.current_pool;
                ev.f["tier_state"] = rec.tier_state;
                if (!ok && !merr.empty()) ev.f["detail"] = pqnas::shorten(merr, 180);
                audit_append(ev);

                if (!ok) {
                    std::cerr << "[tiering-worker] migrate failed path="
                              << rec.logical_rel_path << " err=" << merr << "\n";
                }
            }
        }

        for (int i = 0; i < cfg.interval_sec && !stop_flag->load(); ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}
namespace {

// Keep MVP conservative.
static constexpr std::uint64_t k_text_edit_max_bytes = 1024 * 1024; // 1 MiB

bool looks_like_text_no_nul_prefix(const std::filesystem::path& p,
                                   std::string* err) {
    if (err) err->clear();

    std::ifstream f(p, std::ios::binary);
    if (!f.good()) {
        if (err) *err = "open failed";
        return false;
    }

    char buf[4096];
    f.read(buf, sizeof(buf));
    const std::streamsize n = f.gcount();

    for (std::streamsize i = 0; i < n; ++i) {
        if (buf[i] == '\0') {
            if (err) *err = "binary file";
            return false;
        }
    }
    return true;
}

bool read_file_bytes_all(const std::filesystem::path& p,
                         std::string* out,
                         std::string* err) {
    if (err) err->clear();
    if (!out) {
        if (err) *err = "null out";
        return false;
    }

    std::ifstream f(p, std::ios::binary);
    if (!f.good()) {
        if (err) *err = "open failed";
        return false;
    }

    std::ostringstream ss;
    ss << f.rdbuf();
    if (!f.good() && !f.eof()) {
        if (err) *err = "read failed";
        return false;
    }

    *out = ss.str();
    return true;
}

// Minimal strict UTF-8 validator.
// Accepts plain UTF-8, rejects malformed sequences.
bool is_valid_utf8(const std::string& s) {
    const auto* p = reinterpret_cast<const unsigned char*>(s.data());
    const size_t n = s.size();
    size_t i = 0;

    while (i < n) {
        const unsigned char c = p[i];

        if (c <= 0x7F) {
            ++i;
            continue;
        }

        if ((c >> 5) == 0x6) { // 110xxxxx 10xxxxxx
            if (i + 1 >= n) return false;
            const unsigned char c1 = p[i + 1];
            if ((c1 >> 6) != 0x2) return false;
            const unsigned int cp = ((c & 0x1F) << 6) | (c1 & 0x3F);
            if (cp < 0x80) return false; // overlong
            i += 2;
            continue;
        }

        if ((c >> 4) == 0xE) { // 1110xxxx 10xxxxxx 10xxxxxx
            if (i + 2 >= n) return false;
            const unsigned char c1 = p[i + 1];
            const unsigned char c2 = p[i + 2];
            if ((c1 >> 6) != 0x2 || (c2 >> 6) != 0x2) return false;
            const unsigned int cp =
                ((c & 0x0F) << 12) |
                ((c1 & 0x3F) << 6) |
                (c2 & 0x3F);
            if (cp < 0x800) return false; // overlong
            if (cp >= 0xD800 && cp <= 0xDFFF) return false; // surrogate range
            i += 3;
            continue;
        }

        if ((c >> 3) == 0x1E) { // 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
            if (i + 3 >= n) return false;
            const unsigned char c1 = p[i + 1];
            const unsigned char c2 = p[i + 2];
            const unsigned char c3 = p[i + 3];
            if ((c1 >> 6) != 0x2 || (c2 >> 6) != 0x2 || (c3 >> 6) != 0x2) return false;
            const unsigned int cp =
                ((c & 0x07) << 18) |
                ((c1 & 0x3F) << 12) |
                ((c2 & 0x3F) << 6) |
                (c3 & 0x3F);
            if (cp < 0x10000) return false; // overlong
            if (cp > 0x10FFFF) return false;
            i += 4;
            continue;
        }

        return false;
    }

    return true;
}

std::string strip_utf8_bom(const std::string& s, bool* had_bom = nullptr) {
    const bool bom = s.size() >= 3 &&
                     (unsigned char)s[0] == 0xEF &&
                     (unsigned char)s[1] == 0xBB &&
                     (unsigned char)s[2] == 0xBF;
    if (had_bom) *had_bom = bom;
    return bom ? s.substr(3) : s;
}

std::uint64_t file_mtime_epoch_safe(const std::filesystem::path& p) {
    std::error_code ec;
    auto ftime = std::filesystem::last_write_time(p, ec);
    if (ec) return 0;

    using namespace std::chrono;
    auto sctp = time_point_cast<system_clock::duration>(
        ftime - std::filesystem::file_time_type::clock::now() + system_clock::now()
    );
    const auto sec = duration_cast<seconds>(sctp.time_since_epoch()).count();
    return sec > 0 ? (std::uint64_t)sec : 0;
}

std::string guess_text_mime_from_name(const std::string& name) {
    auto lower = [](std::string s) {
        for (char& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    };

    const std::string n = lower(name);
    const auto dot = n.rfind('.');
    const std::string ext = (dot == std::string::npos) ? "" : n.substr(dot + 1);

    if (ext == "txt" || ext == "log" || ext == "md" || ext == "ini" || ext == "conf")
        return "text/plain";
    if (ext == "json") return "application/json";
    if (ext == "html" || ext == "htm") return "text/html";
    if (ext == "css") return "text/css";
    if (ext == "js") return "application/javascript";
    if (ext == "xml") return "application/xml";
    if (ext == "csv") return "text/csv";
    if (ext == "yml" || ext == "yaml") return "text/yaml";
    if (ext == "sh" || ext == "bash" || ext == "zsh") return "text/x-shellscript";
    if (ext == "c" || ext == "cc" || ext == "cpp" || ext == "h" || ext == "hpp")
        return "text/plain";
    if (ext == "py") return "text/x-python";
    if (ext == "sql") return "text/plain";

    return "text/plain";
}
// -----------------------------------------------------------------------------
// DNA Connect alert sender
// Uses dna-connect-cli to send a real message from a dedicated PQ-NAS DNA identity
// -----------------------------------------------------------------------------

struct DnaAlertSendResult {
    bool ok = false;
    int exit_code = -1;
    std::string output;
    std::string detail;
};



// ============================================================================
//  DNA-CONNECT / ALERT HELPERS
// ============================================================================

static json build_dna_connect_identity_status_json() {
    const json persisted = load_admin_settings_json_safe();

    json cfg = json::object();
    if (persisted.contains("dna_connect_alerts") && persisted["dna_connect_alerts"].is_object()) {
        cfg = persisted["dna_connect_alerts"];
    }

    const std::string cli_path = trim_copy(cfg.value("cli_path", std::string("/usr/local/bin/dna-connect-cli")));
    const std::string data_dir = trim_copy(cfg.value("data_dir", std::string("/var/lib/pqnas/dna-alerts")));

    json out = {
        {"exists", false},
        {"fingerprint", ""},
        {"name", ""},
        {"data_dir", data_dir},
        {"cli_path", cli_path}
    };

    if (cli_path.empty() || data_dir.empty()) return out;

    const std::filesystem::path fp_path =
        std::filesystem::path(data_dir) / "identity" / "fingerprint.txt";

    const std::filesystem::path keys_dir =
        std::filesystem::path(data_dir) / "keys";

    if (!std::filesystem::exists(keys_dir)) {
        return out;
    }

    out["exists"] = true;

    try {
        if (std::filesystem::exists(fp_path)) {
            std::ifstream f(fp_path.string());
            std::string fp;
            std::getline(f, fp);
            out["fingerprint"] = trim_copy(fp);
        }
    } catch (...) {}

    // optional friendly name
    out["name"] = "pqnasalerts";
    return out;
}
bool write_text_file_atomic_utf8(const std::filesystem::path& target_abs,
                                 const std::string& text_utf8,
                                 std::string* err) {
    if (err) err->clear();

    const auto parent = target_abs.parent_path();
    const auto name = target_abs.filename().string();

    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) {
        if (err) *err = "create parent failed: " + ec.message();
        return false;
    }

    const std::string tmp_name =
        "." + name + ".textedit." + std::to_string((unsigned long long)std::time(nullptr)) + ".tmp";
    const std::filesystem::path tmp_abs = parent / tmp_name;

    {
        std::ofstream f(tmp_abs, std::ios::binary | std::ios::trunc);
        if (!f.good()) {
            if (err) *err = "open tmp failed";
            return false;
        }

        f.write(text_utf8.data(), (std::streamsize)text_utf8.size());
        f.flush();

        if (!f.good()) {
            f.close();
            std::filesystem::remove(tmp_abs, ec);
            if (err) *err = "write tmp failed";
            return false;
        }
    }

    std::filesystem::rename(tmp_abs, target_abs, ec);
    if (ec) {
        std::filesystem::remove(tmp_abs, ec);
        if (err) *err = "rename failed: " + ec.message();
        return false;
    }

    return true;
}
    static bool header_key_equal_ci(const std::string& a, const char* b) {
    if (!b) return false;

    std::size_t n = 0;
    while (b[n] != '\0') ++n;

    if (a.size() != n) return false;

    for (std::size_t i = 0; i < n; ++i) {
        const unsigned char ca = static_cast<unsigned char>(a[i]);
        const unsigned char cb = static_cast<unsigned char>(b[i]);

        if (std::tolower(ca) != std::tolower(cb)) {
            return false;
        }
    }

    return true;
}

    static std::string header_value(const httplib::Request& req, const char* key) {
    auto it = req.headers.find(key);
    if (it != req.headers.end()) {
        return it->second;
    }

    for (const auto& kv : req.headers) {
        if (header_key_equal_ci(kv.first, key)) {
            return kv.second;
        }
    }

    return std::string();
}

    // CSRF defense for browser cookie-auth mutation routes.
    // - If request uses Bearer auth, skip this check (mobile/API clients).
    // - If request relies on cookie auth, require same-origin Origin header.
    // - Fallback to Referer prefix check only if Origin is absent.
    static bool require_same_origin_for_cookie_mutation(
        const httplib::Request& req,
        httplib::Response& res)
{
    const std::string authz = header_value(req, "Authorization");
    const bool has_bearer =
        authz.size() > 7 &&
        authz.compare(0, 7, "Bearer ") == 0;

    if (has_bearer) {
        return true;
    }

    const std::string origin = header_value(req, "Origin");
    if (!origin.empty()) {
        if (origin == ORIGIN) return true;

        reply_json(res, 403, json{
            {"ok", false},
            {"error", "forbidden"},
            {"message", "origin mismatch"}
        }.dump());
        return false;
    }

    const std::string referer = header_value(req, "Referer");
    if (!referer.empty()) {
        const std::string allowed_prefix = ORIGIN + "/";
        if (referer == ORIGIN || referer.rfind(allowed_prefix, 0) == 0) {
            return true;
        }

        reply_json(res, 403, json{
            {"ok", false},
            {"error", "forbidden"},
            {"message", "origin mismatch"}
        }.dump());
        return false;
    }

    reply_json(res, 403, json{
        {"ok", false},
        {"error", "forbidden"},
        {"message", "origin required"}
    }.dump());
    return false;
}
} // namespace



// ============================================================================
//  MAIN ENTRY POINT
// ============================================================================

int main()
{
    if (sodium_init() < 0) {
        std::cerr << "sodium_init failed" << std::endl;
        return 1;
    }

    if (!load_env_key("PQNAS_SERVER_PK_B64URL", SERVER_PK, 32) ||
        !load_env_key("PQNAS_SERVER_SK_B64URL", SERVER_SK, 64) ||
        !load_env_key("PQNAS_COOKIE_KEY_B64URL", COOKIE_KEY, 32)) {
        std::cerr << "Missing/invalid env keys. Run ./build/bin/pqnas_keygen > .env.pqnas then: source .env.pqnas" << std::endl;
        return 2;
        }

    if (const char* v = std::getenv("PQNAS_ORIGIN")) ORIGIN = v;

    if (const char* v = std::getenv("PQNAS_TLS_SPKI_SHA256_PIN")) {
        std::string pin_err;
        std::string pin_norm;
        if (!normalize_tls_spki_sha256_pin_for_qr(v, &pin_norm, &pin_err)) {
            std::cerr << "[cfg] FATAL: invalid PQNAS_TLS_SPKI_SHA256_PIN: "
                      << pin_err << std::endl;
            return 2;
        }

        TLS_SPKI_SHA256_PIN = pin_norm;
    }
    if (const char* v = std::getenv("PQNAS_ISS")) ISS = v;
    if (const char* v = std::getenv("PQNAS_AUD")) AUD = v;
    if (const char* v = std::getenv("PQNAS_SCOPE")) SCOPE = v;
    if (const char* v = std::getenv("PQNAS_APP_NAME")) APP_NAME = v;
    if (const char* v = std::getenv("PQNAS_RP_ID")) RP_ID = v;
    if (const char* v = std::getenv("PQNAS_REQ_TTL")) REQ_TTL = std::atoi(v);
    if (const char* v = std::getenv("PQNAS_SESS_TTL")) SESS_TTL = std::atoi(v);
    if (const char* v = std::getenv("PQNAS_LISTEN_PORT")) LISTEN_PORT = std::atoi(v);

	std::string AUTH_MODE = "v4";
	if (const char* v = std::getenv("PQNAS_AUTH_MODE")) AUTH_MODE = v;

	// normalize + clamp
	AUTH_MODE = pqnas::lower_ascii(AUTH_MODE);
	if (AUTH_MODE != "v4" && AUTH_MODE != "v5" && AUTH_MODE != "auto") {
	    std::cerr << "Invalid PQNAS_AUTH_MODE='" << AUTH_MODE
        	      << "' (expected v4|v5|auto). Defaulting to 'auto'.\n";
    	AUTH_MODE = "auto";
	}

    const std::filesystem::path storage_meta_db =
        std::filesystem::path(pqnas::data_root_dir()).parent_path() / "config" / "storage_meta.db";

    pqnas::FileLocationIndex file_location_index(storage_meta_db);
    {
        std::string ferr;
        if (!file_location_index.open(&ferr) || !file_location_index.init_schema(&ferr)) {
            std::cerr << "storage metadata init failed: " << ferr << std::endl;
            return 1;
        }
    }
    pqnas::set_file_location_index(&file_location_index);

    const std::filesystem::path file_versions_db =
    std::filesystem::path(pqnas::data_root_dir()).parent_path() / "config" / "file_versions.db";

    pqnas::FileVersionsIndex file_versions_index(file_versions_db);
    {
        std::string verr;
        if (!file_versions_index.open(&verr) || !file_versions_index.init_schema(&verr)) {
            std::cerr << "file versions init failed: " << verr << std::endl;
            return 1;
        }
    }

    const std::filesystem::path gallery_meta_db =
        std::filesystem::path(pqnas::data_root_dir()).parent_path() / "config" / "gallery_meta.db";

    pqnas::GalleryMetaIndex gallery_meta_index(gallery_meta_db);
    {
        std::string gerr;
        if (!gallery_meta_index.open(&gerr) || !gallery_meta_index.init_schema(&gerr)) {
            std::cerr << "gallery metadata init failed: " << gerr << std::endl;
            return 1;
        }
    }

    pqnas::set_gallery_meta_index(&gallery_meta_index);

    const std::filesystem::path gallery_albums_db =
        std::filesystem::path(pqnas::data_root_dir()).parent_path() / "config" / "gallery_albums.db";

    pqnas::GalleryAlbumsIndex gallery_albums_index(gallery_albums_db);
    {
        std::string aerr;
        if (!gallery_albums_index.open(&aerr) || !gallery_albums_index.init_schema(&aerr)) {
            std::cerr << "[gallery_albums] WARNING: failed to open/init gallery albums db: "
                      << aerr << std::endl;
        }
    }
    pqnas::set_gallery_albums_index(&gallery_albums_index);
    const std::filesystem::path trash_db =
    std::filesystem::path(pqnas::data_root_dir()).parent_path() / "config" / "trash.db";

    pqnas::TrashIndex trash_index(trash_db);
    {
        std::string terr;
        if (!trash_index.open(&terr) || !trash_index.init_schema(&terr)) {
            std::cerr << "trash index init failed: " << terr << std::endl;
            return 1;
        }
    }
    pqnas::TrashService trash_service(&trash_index);
    // ---- Audit log (hash-chained JSONL) ----
    std::string audit_dir = exe_dir() + "/audit";
	if (const char* p = std::getenv("PQNAS_AUDIT_DIR")) {
    	audit_dir = p;
	}

    try {
        std::filesystem::create_directories(audit_dir);
    } catch (const std::exception& e) {
        std::cerr << "[audit] WARNING: create_directories failed: " << e.what() << std::endl;
    }
	g_app_pairing.set_now_epoch_fn([]() -> long {
    	return now_epoch_sec();
	});

	g_app_pairing.set_random_b64url_fn([](size_t nbytes) -> std::string {
    	return random_b64url(nbytes);
	});
    const std::string audit_jsonl_path = audit_dir + "/pqnas_audit.jsonl";
	std::cerr << "[pqnas] audit_jsonl_path=" << audit_jsonl_path << std::endl;

    const std::string audit_state_path = audit_dir + "/pqnas_audit.state";
    pqnas::AuditLog audit(audit_jsonl_path, audit_state_path);
    // declare early so routes can call it

    // ---- Admin settings path (must exist before any helpers use it) ----
    auto getenv_str = [](const char* k) -> std::string {
        const char* v = std::getenv(k);
        return v ? std::string(v) : std::string();
    };

    // Prefer explicit config root/env, then fall back to the service WorkingDirectory (/srv/pqnas),
    // and finally fall back to REPO_ROOT for dev runs.
    std::string config_root = getenv_str("PQNAS_CONFIG_ROOT");
    if (config_root.empty()) {
        config_root = getenv_str("PQNAS_ROOT"); // optional if you already use it elsewhere
    }
    if (config_root.empty()) {
        // If systemd sets WorkingDirectory=/srv/pqnas, CWD is /srv/pqnas.
        // In dev, CWD is usually repo root.
        config_root = (std::filesystem::path(std::filesystem::current_path()) / "config").string();
    }

    // Final: admin settings path
    std::string admin_settings_path =
        (std::filesystem::path(config_root) / "admin_settings.json").string();



    // If running installed (static root set), require PQNAS_DATA_ROOT explicitly.
    if (!getenv_str("PQNAS_STATIC_ROOT").empty() && getenv_str("PQNAS_DATA_ROOT").empty()) {
        std::cerr << "PQNAS_DATA_ROOT is required when PQNAS_STATIC_ROOT is set (installed mode)." << std::endl;
        return 2;
    }
    std::atomic<bool> snapshots_stop{false};
    std::thread snapshots_thread = pqnas::snapshots::start_snapshot_scheduler(admin_settings_path, snapshots_stop);

    // ---------------------------
    // Auto-rotation (checked before every audit.append)
    // ---------------------------

    // in-memory day marker helper (UTC)
    auto utc_day_yyyymmdd_local = [&]() -> std::string {
        try {
            std::time_t tt = std::time(nullptr);
            std::tm tm{};
#if defined(_WIN32)
            gmtime_s(&tm, &tt);
#else
            gmtime_r(&tt, &tm);
#endif
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                          tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
            return std::string(buf);
        } catch (...) {
            return "1970-01-01";
        }
    };

    // cache admin_settings.json reads so we don't hit disk on every audit line
    auto load_admin_settings_cached = [&](const std::string& path) -> json {
        using clock = std::chrono::steady_clock;

        static clock::time_point last_check = clock::now() - std::chrono::seconds(60);
        static std::filesystem::file_time_type last_mtime{};
        static bool last_mtime_valid = false;
        static json cached = json::object();

        const auto now = clock::now();
        if (now - last_check < std::chrono::seconds(2)) {
            return cached;
        }
        last_check = now;

        std::error_code ec;
        const auto mt = std::filesystem::last_write_time(path, ec);
        const bool mt_ok = !ec;

        const bool changed =
            !last_mtime_valid ||
            !mt_ok ||
            (mt != last_mtime);

        if (!changed) {
            return cached;
        }

        // reload
        json j = json::object();
        try {
            std::ifstream f(path);
            if (f.good()) {
                f >> j;
                if (!j.is_object()) j = json::object();
            }
        } catch (...) {
            j = json::object();
        }

        cached = j;
        last_mtime_valid = mt_ok;
        if (mt_ok) last_mtime = mt;
        return cached;
    };

    // rotate implementation (single place) — IMPORTANT: no recursion here
    auto rotate_audit_now_internal = [&](const std::string& reason_tag) -> bool {
        try {
            pqnas::AuditLog::RotateOptions opt;
            pqnas::AuditLog::RotateResult rr;
            const bool ok = audit.rotate(opt, &rr);
            if (!ok) return false;

            // Optional: log the rotation itself (best-effort) WITHOUT calling maybe_auto_rotate_before_append()
            try {
                pqnas::AuditEvent ev;
                ev.event = "audit.auto_rotated";
                ev.outcome = "ok";
                ev.f["reason"] = reason_tag;
                ev.f["rotated_jsonl_path"] = rr.rotated_jsonl_path;
                ev.f["ip"] = "local";
                audit.append(ev);
            } catch (...) {}

            return true;
        } catch (...) {
            return false;
        }
    };

static std::uint64_t g_transport_max_upload_bytes = 0; // 0 => use payload max
static constexpr std::uint64_t k_payload_max_upload_bytes = 1024ull * 1024ull * 1024ull; // 1 GiB (must match set_payload_max_length)

// the actual policy check: call this before audit.append(ev)
// Uses admin_settings.json schema from /api/v4/admin/settings:
//   audit_rotation: { mode: manual|daily|size_mb|daily_or_size_mb, max_active_mb: int, rotate_utc_day: string }
auto maybe_auto_rotate_before_append = [&]() {
    json settings = load_admin_settings_cached(admin_settings_path);

	// Transport upload limit (server-controlled). 0 or missing => payload max.
	// Clamp to payload max so httplib does not reject before we can JSON it.
	try {
    	std::uint64_t v = 0;
	    if (settings.contains("transport_max_upload_bytes")) {
    	    const auto& t = settings["transport_max_upload_bytes"];
        	if (t.is_number_integer()) {
            	long long vv = t.get<long long>();
	            if (vv > 0) v = (std::uint64_t)vv;
    	    } else if (t.is_string()) {
        	    // allow string numbers too (optional)
            	long long vv = std::stoll(t.get<std::string>());
	            if (vv > 0) v = (std::uint64_t)vv;
    	    }
    	}
	    if (v == 0) v = k_payload_max_upload_bytes;
    	if (v > k_payload_max_upload_bytes) v = k_payload_max_upload_bytes;
	    g_transport_max_upload_bytes = v;
	} catch (...) {
    	g_transport_max_upload_bytes = k_payload_max_upload_bytes;
	}

    json rot = json::object();
    if (settings.contains("audit_rotation") && settings["audit_rotation"].is_object()) {
        rot = settings["audit_rotation"];
    }

    const std::string mode = rot.value("mode", "manual");
    const int max_mb = rot.value("max_active_mb", 256);

    // manual => never auto-rotate
    if (mode == "manual") return;

    static std::string last_rotated_day = utc_day_yyyymmdd_local();

    // daily trigger (UTC day change)
    if (mode == "daily" || mode == "daily_or_size_mb") {
        const std::string today = utc_day_yyyymmdd_local();
        if (today != last_rotated_day) {
            if (rotate_audit_now_internal("daily")) {
                last_rotated_day = today;
                return;
            }
        }
    }

    // size trigger
    if (mode == "size_mb" || mode == "daily_or_size_mb") {
        const long long bytes = file_size_bytes_safe(audit_jsonl_path);
        const long long limit = (long long)max_mb * 1024LL * 1024LL;
        if (bytes >= 0 && bytes >= limit) {
            (void)rotate_audit_now_internal("size_mb");
        }
    }
};



    // ---- audit bridge: bind global audit_append() to this AuditLog instance ----
    g_audit_append = [&](const pqnas::AuditEvent& ev) {
        // Centralized rotation policy: endpoints should NOT call maybe_auto_rotate_before_append().
        maybe_auto_rotate_before_append();

        // Append to hash-chained JSONL audit log
        audit.append(ev);
    };


	pqnas::drive_health_monitor_start(
    	[&](const pqnas::DriveHealthInfo& d,
        	const std::string& prev_status,
	        const std::string& new_status) {

    	    pqnas::AuditEvent ev;

        	if (new_status == "fail") {
            	ev.event = "system.drive_failure";
	            ev.outcome = "fail";
    	    } else if (new_status == "warn") {
        	    ev.event = "system.drive_warning";
            	ev.outcome = "fail";
	        } else if (new_status == "ok") {
    	        ev.event = "system.drive_recovered";
        	    ev.outcome = "ok";
	        } else {
    	        ev.event = "system.drive_status_changed";
        	    ev.outcome = "ok";
        	}

	        ev.f["device"] = d.dev;
    	    ev.f["model"] = pqnas::shorten(d.model, 120);
        	ev.f["prev_status"] = prev_status;
	        ev.f["new_status"] = new_status;
    	    ev.f["health_text"] = pqnas::shorten(d.health_text, 120);

	        if (d.temperature_c >= 0) ev.f["temperature_c"] = std::to_string(d.temperature_c);
    	    if (d.percentage_used >= 0) ev.f["percentage_used"] = std::to_string(d.percentage_used);
        	if (d.available_spare >= 0) ev.f["available_spare"] = std::to_string(d.available_spare);
	        if (d.media_errors >= 0) ev.f["media_errors"] = std::to_string(d.media_errors);
    	    if (d.power_on_hours >= 0) ev.f["power_on_hours"] = std::to_string(d.power_on_hours);
        	if (!d.warning.empty()) ev.f["warning"] = pqnas::shorten(d.warning, 160);
	        if (!d.selftest_text.empty()) ev.f["selftest"] = pqnas::shorten(d.selftest_text, 160);

    	    audit_append(ev);
    	}
	);

    httplib::Server srv;

    pqnas::ServiceNoticesStore service_notices_store(
        std::filesystem::path(config_root_dir()) / "service_notices.json"
    );

    pqnas::backups::SystemBackupWorker system_backup_worker(
        pqnas::backups::SystemBackupWorker::default_config()
    );
    system_backup_worker.start_scheduler();

    const bool hsts_enabled = (ORIGIN.rfind("https://", 0) == 0);
    if (hsts_enabled) {
        std::cerr << "[cfg] hsts=enabled origin=" << ORIGIN << std::endl;
    } else {
        std::cerr << "[cfg] hsts=disabled origin=" << ORIGIN << std::endl;
    }

    srv.set_post_routing_handler([hsts_enabled](const httplib::Request&, httplib::Response& res) {
        if (!hsts_enabled) return;
        res.set_header("Strict-Transport-Security", "max-age=31536000; includeSubDomains");
    });

	srv.set_payload_max_length(k_payload_max_upload_bytes);
	srv.set_error_handler([&](const httplib::Request& req, httplib::Response& res) {
    	// Only force JSON for API endpoints
	    const std::string& p = req.path;
    	const bool is_api = (p.size() >= 5 && p.compare(0, 5, "/api/") == 0);
	    if (!is_api) return;

	    // If someone already set a JSON body, don't override it
    	if (!res.body.empty()) return;

    	if (res.status == 413) {
        	// Note: body may already be rejected by httplib payload max.
	        reply_json(res, 413, json{
    	        {"ok", false},
        	    {"error", "transport_limit_exceeded"},
            	{"message", "Upload rejected before reaching handler (payload limit)"},
	            {"max_bytes", (g_transport_max_upload_bytes ? g_transport_max_upload_bytes : k_payload_max_upload_bytes)}
    	    }.dump());
        	return;
	    }

	    if (res.status == 400) {
    	    reply_json(res, 400, json{
        	    {"ok", false},
	            {"error", "bad_request"},
    	        {"message", "Request rejected before reaching handler"}
        	}.dump());
        	return;
    	}
	});
	// RAID async engine startup repair (Option A restart policy)
	// RAID startup cleanup moved to split RAID route worker/module.
    // ---- Load admin settings once at startup (audit min level) ----
    try {
        std::ifstream f(admin_settings_path);
        if (f.good()) {
            json j = json::parse(f, nullptr, true);

            std::string lvl;
            auto it = j.find("audit_min_level");
            if (it != j.end() && it->is_string()) {
                lvl = it->get<std::string>();
            }

            if (!lvl.empty()) {
                if (!audit.set_min_level_str(lvl)) {
                    std::cerr << "[settings] WARNING: invalid audit_min_level in "
                              << admin_settings_path << std::endl;
                } else {
                    std::cerr << "[settings] audit_min_level=" << audit.min_level_str() << std::endl;
                }
            }
        } else {
            std::cerr << "[settings] no admin_settings.json, default audit_min_level="
                      << audit.min_level_str() << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "[settings] WARNING: failed to load " << admin_settings_path
                  << ": " << e.what() << std::endl;
    }



    // Policy (allowlist) path resolution:
    //  1) PQNAS_POLICY_PATH (explicit override)
    //  2) PQNAS_CONFIG_ROOT/policy.json (installed default)
    //  3) REPO_ROOT/config/policy.json (dev fallback)
    std::string allowlist_path;
    if (const char* p = std::getenv("PQNAS_POLICY_PATH")) {
        allowlist_path = p;
    } else {
        const std::string config_root = getenv_str("PQNAS_CONFIG_ROOT");
        if (!config_root.empty()) {
            allowlist_path = (std::filesystem::path(config_root) / "policy.json").string();
        } else {
            allowlist_path = (std::filesystem::path(REPO_ROOT) / "config" / "policy.json").string();
        }
    }

    pqnas::Allowlist allowlist;
    if (!allowlist.load(allowlist_path)) {
        std::cerr << "[policy] FATAL: failed to load allowlist: " << allowlist_path << std::endl;
        return 3;
    }

    // Users registry path resolution:
    //  1) PQNAS_USERS_PATH (explicit override)
    //  2) PQNAS_CONFIG_ROOT/users.json (installed default)
    //  3) REPO_ROOT/config/users.json (dev fallback)
    std::string users_path;
    if (const char* p = std::getenv("PQNAS_USERS_PATH")) {
        users_path = p;
    } else {
        const std::string config_root = getenv_str("PQNAS_CONFIG_ROOT");
        if (!config_root.empty()) {
            users_path = (std::filesystem::path(config_root) / "users.json").string();
        } else {
            users_path = (std::filesystem::path(REPO_ROOT) / "config" / "users.json").string();
        }
    }

    std::string app_auth_path;
    if (const char* p = std::getenv("PQNAS_APP_AUTH_PATH")) {
        app_auth_path = p;
    } else {
        const std::string config_root = getenv_str("PQNAS_CONFIG_ROOT");
        if (!config_root.empty()) {
            app_auth_path = (std::filesystem::path(config_root) / "app_auth.json").string();
        } else {
            app_auth_path = (std::filesystem::path(REPO_ROOT) / "config" / "app_auth.json").string();
        }
    }

    std::cerr << "[cfg] allowlist_path=" << allowlist_path << std::endl;
    std::cerr << "[cfg] users_path=" << users_path << std::endl;
	std::cerr << "[cfg] app_auth_path=" << app_auth_path << std::endl;

    pqnas::UsersRegistry users;
    if (!users.load(users_path)) {
        std::cerr << "[users] FATAL: failed to load users registry: " << users_path << std::endl;
        return 4;
    }

    pqnas::WorkspacesRegistry workspaces;
    const std::string workspaces_path =
        (std::filesystem::path(users_path).parent_path() / "workspaces.json").string();
    (void)workspaces.load(workspaces_path);



pqnas::WorkspaceExternalSessionsStore workspace_external_sessions;
pqnas::WorkspaceExternalInvitesRegistry workspace_external_invites;
const std::string workspace_external_invites_path =
    (std::filesystem::path(users_path).parent_path() / "workspace_external_invites.json").string();
(void)workspace_external_invites.load(workspace_external_invites_path);
std::cerr << "[cfg] workspace_external_invites_path=" << workspace_external_invites_path << std::endl;

    std::cerr << "[cfg] users_path=" << users_path << std::endl;

    std::string shares_path = (std::filesystem::path(users_path).parent_path() / "shares.json").string();
    if (const char* p = std::getenv("PQNAS_SHARES_PATH")) {
        shares_path = p;
    }
    std::cerr << "[cfg] shares_path=" << shares_path << std::endl;

    std::filesystem::path dropzone_db_path =
    std::filesystem::path(users_path).parent_path() / "dropzones.sqlite3";

    if (const char* p = std::getenv("PQNAS_DROPZONE_DB_PATH")) {
        dropzone_db_path = p;
    }

    std::cerr << "[cfg] dropzone_db_path=" << dropzone_db_path.string() << std::endl;

    pqnas::DropZoneIndex dropzone_index(dropzone_db_path);

    {
        std::string dzerr;
        if (!dropzone_index.open(&dzerr) || !dropzone_index.init_schema(&dzerr)) {
            std::cerr << "[dropzone] FATAL: failed to initialize dropzone index: "
                      << dzerr << std::endl;
            return 6;
        }
    }

    const std::filesystem::path echo_stack_db_path =
        dropzone_db_path.parent_path() / "echo_stack.sqlite3";

    pqnas::EchoStackIndex echo_stack_index(echo_stack_db_path);

    {
        std::string eserr;
        if (!echo_stack_index.open(&eserr) || !echo_stack_index.init_schema(&eserr)) {
            std::cerr << "[echostack] failed to open/init database: "
                      << eserr << std::endl;
            return 1;
        }
    }

    if (TLS_SPKI_SHA256_PIN.empty()) {
        std::cerr << "[cfg] WARNING: PQNAS_TLS_SPKI_SHA256_PIN is not configured; Android app pairing QR will fail closed." << std::endl;
    } else {
        std::cerr << "[cfg] mobile_tls_spki_pin=configured" << std::endl;
    }

    pqnas::ShareRegistry shares(shares_path);
    {
        std::string err;
        if (!shares.load(&err)) {
            std::cerr << "[shares] WARNING: " << err << "\n";
        }
    }

    pqnas::SharePqStoreV1 share_pq(std::filesystem::path(shares_path).parent_path());

	// for mobile app
	g_app_tokens.set_now_epoch_fn([]() { return pqnas::now_epoch(); });
	g_app_tokens.set_now_iso_utc_fn([]() { return pqnas::now_iso_utc(); });
	g_app_tokens.set_random_b64url_fn([](size_t n) { return random_b64url(n); });

	{
    	std::string app_auth_err;
	    if (!g_app_tokens.load(app_auth_path, &app_auth_err)) {
    	    std::cerr << "[app_tokens] FATAL: failed to load app auth store: "
        	          << app_auth_path
	                  << " err=" << app_auth_err
    	              << std::endl;
        	return 5;
    	}
	}

	g_users_path_for_user_workers = users_path;
	pool_mounts_init_default_only();
	pool_mounts_restore_managed(users_path);

	{
    	std::lock_guard<std::mutex> lk(pool_mu());
    	auto& m = pool_mount_by_id();
    	auto it = m.find(default_pool_id());
	    std::cerr << "[cfg] default_pool_id=" << default_pool_id()
    	          << " mount=" << (it == m.end() ? "(missing)" : it->second)
        	      << std::endl;

	    for (const auto& kv : m) {
    	    if (kv.first == default_pool_id()) continue;
        	std::cerr << "[cfg] managed_pool_id=" << kv.first
            	      << " mount=" << kv.second
                	  << std::endl;
    	}
	}

	RoutesV5Context v5;
	v5.origin = &ORIGIN;
	v5.rp_id  = &RP_ID;
	v5.app    = &APP_NAME;
    v5.tls_spki_sha256_pin = &TLS_SPKI_SHA256_PIN;

	v5.req_ttl  = &REQ_TTL;
	v5.sess_ttl = &SESS_TTL;

	v5.server_pk  = SERVER_PK;
	v5.server_sk  = SERVER_SK;
	v5.cookie_key = COOKIE_KEY;

	v5.allowlist = &allowlist;
	v5.users     = &users;

	v5.allowlist_path = &allowlist_path;
	v5.users_path     = &users_path;

	// ---- hook existing helpers (these already exist in main.cpp today) ----
	v5.now_epoch = []() { return pqnas::now_epoch(); };
	v5.now_iso_utc = []() { return pqnas::now_iso_utc(); };


	v5.random_b64url = [&](int n) { return random_b64url(n); };
	v5.url_encode    = [&](const std::string& s) { return url_encode(s); };

v5.consume_app_mint =
    [&](const std::string& fingerprint_hex,
        const std::string& device_name,
        const std::string& platform,
        const std::string& app_version,
        const std::string& device_model,
        const std::string& device_manufacturer,
        const std::string& os_version,
        const std::string& client_ip,
        RoutesV5Context::ConsumeAppResult& out,
        std::string& err) -> bool {
        err.clear();

        auto uopt = users.get(fingerprint_hex);
        if (!uopt.has_value()) {
            err = "policy denied";
            return false;
        }

        const auto& u = *uopt;
        const bool is_admin = (u.role == "admin" && u.status == "enabled");
        const bool is_user  = (u.status == "enabled") || is_admin;
        if (!is_user) {
            err = "policy denied";
            return false;
        }

        std::string device_id;
        std::string access_token;
        long access_exp = 0;
        std::string refresh_token;
        long refresh_exp = 0;

        if (!g_app_tokens.mint_from_approved_fingerprint(
                fingerprint_hex,
                is_admin ? "admin" : "user",
                device_name,
                platform,
                app_version,
                device_model,
                device_manufacturer,
                os_version,
                client_ip,
                &device_id,
                &access_token,
                &access_exp,
                &refresh_token,
                &refresh_exp,
                &err)) {
            return false;
        }

        out.device_id = device_id;
        out.access_token = access_token;
        out.access_exp = access_exp;
        out.refresh_token = refresh_token;
        out.refresh_exp = refresh_exp;
        out.fingerprint_hex = fingerprint_hex;
        out.role = is_admin ? "admin" : "user";

        record_security_activity_best_effort_local(
            users,
            fingerprint_hex,
            fingerprint_hex,
            "security.device_paired",
            "device",
            activity_security_target_name_local(device_name, platform, "device"),
            activity_security_device_details_local(
                device_name,
                platform.empty() ? "android" : platform,
                app_version,
                device_model,
                device_manufacturer,
                os_version
            )
        );

        return true;
    };

	v5.refresh_app_token =
	    [&](const std::string& refresh_token,
	        const std::string& device_id,
	        const std::string& client_ip,
	        RoutesV5Context::RefreshAppResult& out,
	        std::string& err) -> bool {
	        err.clear();

	        std::string fingerprint_hex;
	        std::string token_role;
	        std::string access_token;
	        long access_exp = 0;

	        if (!g_app_tokens.refresh_access_token(
	                refresh_token,
	                device_id,
	                client_ip,
	                &fingerprint_hex,
	                &token_role,
	                &access_token,
	                &access_exp,
	                &err)) {
	            return false;
	        }

	        auto uopt = users.get(fingerprint_hex);
	        if (!uopt.has_value()) {
	            err = "policy denied";
	            return false;
	        }

	        const auto& u = *uopt;
	        const bool is_admin = (u.role == "admin" && u.status == "enabled");
	        const bool is_user  = (u.status == "enabled") || is_admin;
	        if (!is_user) {
	            err = "policy denied";
	            return false;
	        }

	        out.access_token = access_token;
	        out.access_exp = access_exp;
	        out.fingerprint_hex = fingerprint_hex;
	        out.role = is_admin ? "admin" : "user";
	        out.device_id = device_id;
	        return true;
	    };
    v5.revoke_app_token = [&](
        const std::string& refresh_token,
        const std::string& device_id,
        const std::string& client_ip,
        std::string& err) -> bool
    {
        (void)client_ip;

        err.clear();

        pqnas::TrustedAppDevice device_before_revoke;
        const bool had_device_before_revoke =
            g_app_tokens.get_device(device_id, &device_before_revoke);

        const bool was_already_revoked =
            had_device_before_revoke && device_before_revoke.revoked;

        if (!g_app_tokens.revoke_refresh_token(refresh_token, device_id, &err)) {
            return false;
        }

        if (had_device_before_revoke && !was_already_revoked && !device_before_revoke.fingerprint_hex.empty()) {
            const std::string target_name = activity_security_target_name_local(
                device_before_revoke.device_name,
                device_before_revoke.platform,
                "session"
            );

            record_security_activity_best_effort_local(
                users,
                device_before_revoke.fingerprint_hex,
                device_before_revoke.fingerprint_hex,
                "security.session_revoked",
                "session",
                target_name,
                activity_security_device_details_local(device_before_revoke)
            );
        }

        return true;
    };
	v5.build_req_payload_canonical = [&](const std::string& sid,
                                     const std::string& chal,
                                     const std::string& nonce,
                                     long iat,
                                     long exp) {
    	return build_req_payload_canonical(sid, chal, nonce, iat, exp);
	};

	v5.sign_req_token = [&](const std::string& payload) { return sign_req_token(payload); };
	v5.qr_svg_from_text = [&](const std::string& t, int sc, int b) { return qr_svg_from_text(t, sc, b); };

    // v5 stateless-ready correlation key (k):
    // Must match verify_login_common.cc v5 approval_key = vr.st_hash_b64.
    // We define: k = b64_std(SHA256(st_token)).
    v5.st_hash_b64_from_st = [&](const std::string& st_token) -> std::string {
        unsigned char md[EVP_MAX_MD_SIZE];
        unsigned int md_len = 0;

        EVP_MD_CTX* c = EVP_MD_CTX_new();
        if (!c) return std::string{};
        struct Guard { EVP_MD_CTX* p; ~Guard(){ if(p) EVP_MD_CTX_free(p); } } g{c};

        if (EVP_DigestInit_ex(c, EVP_sha256(), nullptr) != 1) return std::string{};
        if (!st_token.empty()) {
            if (EVP_DigestUpdate(c, st_token.data(), st_token.size()) != 1) return std::string{};
        }
        if (EVP_DigestFinal_ex(c, md, &md_len) != 1) return std::string{};

        // st_hash_b64 in your verify path is "b64" (standard base64), so reuse pqnas::b64_std.
        return pqnas::b64_std(md, (size_t)md_len);
    };


	// approvals/pending
	v5.approvals_prune = [&](long now) { approvals_prune(now); };
	v5.pending_prune   = [&](long now) { pending_prune(now); };

	v5.approvals_get = [&](const std::string& sid, RoutesV5Context::ApprovalEntry& out) {
    	ApprovalEntry e;
	    if (!approvals_get(sid, e)) return false;
    	out.cookie_val  = e.cookie_val;
	    out.fingerprint = e.fingerprint;
    	out.expires_at  = e.expires_at;
    	return true;
	};
	v5.approvals_put = [&](const std::string& sid, const RoutesV5Context::ApprovalEntry& in) {
    	ApprovalEntry e;
	    e.cookie_val  = in.cookie_val;
    	e.fingerprint = in.fingerprint;
	    e.expires_at  = in.expires_at;
    	approvals_put(sid, e);
	};
	v5.approvals_pop = [&](const std::string& sid) { approvals_pop(sid); };

    v5.pending_get = [&](const std::string& sid, RoutesV5Context::PendingEntry& out) {
        PendingEntry p;
        if (!pending_get(sid, p)) return false;
        out.expires_at = p.expires_at;
        out.reason = p.reason;
        out.browser_bind_hash = p.browser_bind_hash;
        out.fingerprint_hex = p.fingerprint_hex;
        return true;
    };

    v5.pending_put = [&](const std::string& sid, const RoutesV5Context::PendingEntry& in) {
        PendingEntry p;
        p.expires_at = in.expires_at;
        p.reason = in.reason;

        // Preserve browser binding and fingerprint independently.
        // A pending-admin update often has a new fingerprint but wants to keep
        // the browser binding minted earlier by /api/v5/session.
        PendingEntry oldp;
        const bool had_old_pending = pending_get(sid, oldp);

        if (!in.browser_bind_hash.empty()) {
            p.browser_bind_hash = in.browser_bind_hash;
        } else if (had_old_pending) {
            p.browser_bind_hash = oldp.browser_bind_hash;
        }

        if (!in.fingerprint_hex.empty()) {
            p.fingerprint_hex = in.fingerprint_hex;
        } else if (had_old_pending) {
            p.fingerprint_hex = oldp.fingerprint_hex;
        }

        pending_put(sid, p);
    };

	v5.pending_pop = [&](const std::string& sid) { pending_pop(sid); };

	v5.app_device_refresh_expiry =
    	[&](const std::string& device_id, long& out_expires_at) -> bool {
        	return g_app_tokens.get_refresh_expiry_for_device(device_id, &out_expires_at);
    	};
	v5.app_devices_list_for_fingerprint =
    	[&](const std::string& fingerprint_hex) -> std::vector<pqnas::TrustedAppDevice> {
        	return g_app_tokens.list_devices_for_fingerprint(fingerprint_hex);
    	};

	v5.app_device_get =
    	[&](const std::string& device_id, pqnas::TrustedAppDevice& out) -> bool {
        	return g_app_tokens.get_device(device_id, &out);
    	};

	v5.app_device_revoke =
    	[&](const std::string& device_id, std::string& err) -> bool {
            pqnas::TrustedAppDevice device_before_revoke;
            const bool had_device_before_revoke =
                g_app_tokens.get_device(device_id, &device_before_revoke);

            const bool was_already_revoked =
                had_device_before_revoke && device_before_revoke.revoked;

            if (!g_app_tokens.revoke_device(device_id, &err)) {
                return false;
            }

            if (had_device_before_revoke && !was_already_revoked && !device_before_revoke.fingerprint_hex.empty()) {
                const std::string target_name = activity_security_target_name_local(
                    device_before_revoke.device_name,
                    device_before_revoke.platform,
                    "session"
                );

                record_security_activity_best_effort_local(
                    users,
                    device_before_revoke.fingerprint_hex,
                    device_before_revoke.fingerprint_hex,
                    "security.session_revoked",
                    "session",
                    target_name,
                    activity_security_device_details_local(device_before_revoke)
                );
            }

            return true;
    	};
	// cookie minting + base64
	v5.session_cookie_mint = [&](const unsigned char* key,
                             const std::string& fp_b64,
                             long iat,
                             long exp,
                             std::string& out_cookie) {
    	return session_cookie_mint(key, fp_b64, iat, exp, out_cookie);
	};

    v5.sign_token_v4_ed25519 = [&](const nlohmann::json& p, const unsigned char* sk) {
        return sign_token_v4_ed25519(p, sk);
    };



	v5.b64_std = [&](const unsigned char* data, size_t len) { return pqnas::b64_std(data, len); };
	v5.client_ip = [&](const httplib::Request& r) { return client_ip(r); };
	v5.shorten   = [&](const std::string& s, size_t n) { return pqnas::shorten(s, n); };

	v5.audit_emit = [&](const std::string& event,
                    const std::string& outcome,
                    const std::function<void(std::map<std::string,std::string>&)>& fill) {
    	pqnas::AuditEvent ev;
	    ev.event   = event;
    	ev.outcome = outcome;
	    std::map<std::string,std::string> f;
    	fill(f);
	    for (auto& kv : f) ev.f[kv.first] = kv.second;
	    audit_append(ev);
	};
v5.require_user_cookie =
    [&](const httplib::Request& req, httplib::Response& res, std::string* fp_hex, std::string* role) -> bool {
        return require_user_cookie_users_actor(req, res, COOKIE_KEY, &users, fp_hex, role);
    };

v5.app_pair_prune = [&](long now) {
    g_app_pairing.prune_expired(now);
};

    v5.app_pair_start =
        [&](const std::string& fingerprint_hex,
            const std::string& role,
            RoutesV5Context::AppPairStartResult& out,
            std::string& err) -> bool {

            out = RoutesV5Context::AppPairStartResult{};
            err.clear();

            if (!v5.tls_spki_sha256_pin || v5.tls_spki_sha256_pin->empty()) {
                err = "tls pin not configured";
                return false;
            }

            pqnas::AppPairingSession s;
            if (!g_app_pairing.start_pairing(fingerprint_hex, role, 300, &s, &err)) {
                return false;
            }

            out.pair_id = s.pair_id;
            out.pair_token = s.pair_token;
            out.expires_at = s.expires_at;

            out.qr_uri = pqnas::AppPairingStore::build_pair_qr_uri(
                *v5.origin,
                s.pair_token,
                *v5.app,
                *v5.tls_spki_sha256_pin,
                v5.url_encode
            );

            return true;
    };

v5.app_pair_get =
    [&](const std::string& pair_id,
        RoutesV5Context::AppPairStatusResult& out,
        std::string& err) -> bool {

        pqnas::AppPairingSession s;
        if (!g_app_pairing.get_by_pair_id(pair_id, &s, &err)) return false;

        out.pair_id = s.pair_id;
        out.pair_token = s.pair_token;
        out.fingerprint_hex = s.fingerprint_hex;
        out.role = s.role;
		out.issued_at = s.issued_at;
		out.expires_at = s.expires_at;
		out.consumed = s.consumed;
		out.consumed_at = s.consumed_at;
		out.consumed_device_id = s.consumed_device_id;
        return true;
    };

v5.app_pair_cancel =
    [&](const std::string& pair_id, std::string& err) -> bool {
        return g_app_pairing.cancel_pairing(pair_id, &err);
    };

v5.app_pair_consume =
    [&](const std::string& pair_token,
        std::string& out_pair_id,
        std::string& out_fingerprint_hex,
        std::string& out_role,
        std::string& err) -> bool {
        return g_app_pairing.consume_pair_token(pair_token, &out_pair_id, &out_fingerprint_hex, &out_role, &err);
    };

v5.app_pair_rollback_consumed =
    [&](const std::string& pair_id, std::string& err) -> bool {
        return g_app_pairing.rollback_consumed(pair_id, &err);
    };

v5.app_pair_mark_consumed_device =
    [&](const std::string& pair_id, const std::string& device_id, std::string& err) -> bool {
        return g_app_pairing.mark_consumed_device(pair_id, device_id, &err);
    };

v5.app_pair_build_qr_uri =
    [&](const std::string& origin,
        const std::string& pair_token,
        const std::string& app_name,
        const std::string& tls_pin_sha256) -> std::string {
        return pqnas::AppPairingStore::build_pair_qr_uri(
            origin,
            pair_token,
            app_name,
            tls_pin_sha256,
            v5.url_encode
        );
    };
    // Verify bridge from shared verifier into RoutesV5Context
	v5.verify_v4_json = [&](const std::string& body) -> RoutesV5Context::VerifyResult {
    	pqnas::VerifyV4Config cfg;
    	cfg.now_unix_sec = 0;
	    cfg.expected_origin = ORIGIN;
    	cfg.expected_rp_id  = RP_ID;
	    cfg.enforce_allowlist = false;

	    std::array<unsigned char, 32> pk{};
    	std::memcpy(pk.data(), SERVER_PK, 32);

    	auto vr = pqnas::verify_v4_json(body, pk, cfg);

	    RoutesV5Context::VerifyResult out;
    	out.ok = vr.ok;
	    out.detail = vr.detail;

	    out.sid         = vr.sid;
    	out.origin      = vr.origin;
	    out.rp_id_hash  = vr.rp_id_hash;
    	out.st_hash_b64 = vr.st_hash_b64;
	    out.fingerprint_hex = vr.fingerprint_hex;

    // map rc (coarse)
    if (vr.ok) out.rc = RoutesV5Context::VerifyRc::OK;
	    else {
    	    switch (vr.rc) {
        	    case pqnas::VerifyV4Rc::ST_EXPIRED: out.rc = RoutesV5Context::VerifyRc::ST_EXPIRED; break;
            	case pqnas::VerifyV4Rc::RP_ID_HASH_MISMATCH: out.rc = RoutesV5Context::VerifyRc::RP_ID_HASH_MISMATCH; break;
	            case pqnas::VerifyV4Rc::FINGERPRINT_MISMATCH: out.rc = RoutesV5Context::VerifyRc::FINGERPRINT_MISMATCH; break;
    	        case pqnas::VerifyV4Rc::PQ_SIG_INVALID: out.rc = RoutesV5Context::VerifyRc::PQ_SIG_INVALID; break;
        	    case pqnas::VerifyV4Rc::POLICY_DENY: out.rc = RoutesV5Context::VerifyRc::POLICY_DENY; break;
            	default: out.rc = RoutesV5Context::VerifyRc::OTHER; break;
	        }
    	}
	    return out;
	};

	// (leave v5.sign_token_v4_ed25519 unset for now; we’ll wire once v5 verify uses it)

    register_routes_v5(srv, v5);

    pqnas::ActivityRoutesDeps activity_deps;
    activity_deps.users = &users;
    activity_deps.cookie_key = COOKIE_KEY;

    activity_deps.require_user_auth_users_actor =
        [&](const httplib::Request& req,
            httplib::Response& res,
            const unsigned char* cookie_key,
            pqnas::UsersRegistry* users_ptr,
            std::string* fp_hex,
            std::string* role) -> bool {
            return require_user_auth_users_actor(req, res, cookie_key, users_ptr, fp_hex, role);
    };

    activity_deps.reply_json =
        [&](httplib::Response& res, int code, const std::string& body) {
            reply_json(res, code, body);
    };

    activity_deps.user_dir_for_fp =
        [&](pqnas::UsersRegistry& users_ref, const std::string& fp_hex) -> std::filesystem::path {
            return user_dir_for_fp(users_ref, fp_hex);
    };

    pqnas::register_activity_routes(srv, activity_deps);

    pqnas::backups::SystemBackupRoutesDeps system_backup_deps;
    {
        pqnas::ServiceNoticeRoutesDeps service_notice_deps;
        service_notice_deps.store = &service_notices_store;
        service_notice_deps.static_admin_service_notices_html = STATIC_ADMIN_SERVICE_NOTICES_HTML;

        service_notice_deps.require_user =
            [&](const httplib::Request& req,
                httplib::Response& res,
                std::string* fp_hex,
                std::string* role) -> bool {
                return require_user_cookie_users_actor(
                    req,
                    res,
                    COOKIE_KEY,
                    &users,
                    fp_hex,
                    role
                );
        };

        service_notice_deps.require_admin =
            [&](const httplib::Request& req,
                httplib::Response& res,
                std::string* actor_fp) -> bool {
                return require_admin_cookie_users_actor(
                    req,
                    res,
                    COOKIE_KEY,
                    users_path,
                    &users,
                    actor_fp
                );
        };

        service_notice_deps.require_same_origin =
            [&](const httplib::Request& req, httplib::Response& res) -> bool {
                return require_same_origin_for_cookie_mutation(req, res);
        };

        service_notice_deps.reply_json =
            [&](httplib::Response& res, int code, const std::string& body) {
                reply_json(res, code, body);
        };

        service_notice_deps.audit_append =
            [&](const pqnas::AuditEvent& ev) {
                audit_append(ev);
        };

        service_notice_deps.record_activity =
            [&](const httplib::Request& req,
                const std::string& actor_fp,
                const pqnas::ServiceNotice& notice,
                const std::string& action) {
                if (actor_fp.empty()) return;

                std::filesystem::path user_root;
                try {
                    user_root = user_dir_for_fp(users, actor_fp);
                } catch (...) {
                    return;
                }

                if (user_root.empty()) return;

                pqnas::activity::ActivityEvent ev;
                ev.owner_user_id = actor_fp;

                ev.actor.user_id = actor_fp;
                ev.actor.kind = "user";
                ev.actor.fingerprint_short =
                    actor_fp.size() > 12 ? actor_fp.substr(0, 12) : actor_fp;

                try {
                    auto uopt = users.get(actor_fp);
                    if (uopt.has_value() && !uopt->name.empty()) {
                        ev.actor.display_name = uopt->name;
                    }
                } catch (...) {
                }

                if (action == "created") {
                    ev.event_type = "service_notice.created";
                    ev.message = "Published service notice: " +
                        pqnas::shorten(notice.title.empty() ? notice.id : notice.title, 160);
                } else if (action == "updated") {
                    ev.event_type = "service_notice.updated";
                    ev.message = "Updated service notice: " +
                        pqnas::shorten(notice.title.empty() ? notice.id : notice.title, 160);
                } else if (action == "deleted") {
                    ev.event_type = "service_notice.deleted";
                    ev.message = "Deleted service notice: " +
                        pqnas::shorten(notice.title.empty() ? notice.id : notice.title, 160);
                } else {
                    ev.event_type = "service_notice.changed";
                    ev.message = "Changed service notice: " +
                        pqnas::shorten(notice.title.empty() ? notice.id : notice.title, 160);
                }

                ev.scope_type = "security";
                ev.scope_id = actor_fp;
                ev.target_kind = "service_notice";
                ev.target_name = pqnas::shorten(
                    notice.title.empty() ? notice.id : notice.title,
                    180
                );

                ev.details = nlohmann::json::object();
                ev.details["action"] = action;
                ev.details["notice_id"] = notice.id;
                if (!notice.title.empty()) ev.details["title"] = notice.title;
                if (!notice.kind.empty()) ev.details["kind"] = notice.kind;
                if (!notice.severity.empty()) ev.details["severity"] = notice.severity;
                ev.details["enabled"] = notice.enabled;
                ev.details["pinned"] = notice.pinned;
                ev.details["starts_at"] = notice.starts_at;
                ev.details["ends_at"] = notice.ends_at;
                if (!req.remote_addr.empty()) ev.details["ip"] = req.remote_addr;

                std::string activity_err;
                (void)pqnas::activity::record_user_activity(
                    user_root,
                    ev,
                    &activity_err
                );
        };

        service_notice_deps.now_epoch =
            []() -> std::int64_t {
                return static_cast<std::int64_t>(pqnas::now_epoch());
        };

        pqnas::register_service_notice_routes(srv, service_notice_deps);
    }

    system_backup_deps.users = activity_deps.users;
    system_backup_deps.cookie_key = activity_deps.cookie_key;
    system_backup_deps.worker = &system_backup_worker;
    system_backup_deps.require_user_auth_users_actor =
        activity_deps.require_user_auth_users_actor;
    system_backup_deps.reply_json = activity_deps.reply_json;

    pqnas::backups::register_system_backup_routes(srv, system_backup_deps);

    pqnas::notifications::NotificationRoutesDeps notificationRoutesDeps;
    notificationRoutesDeps.users = activity_deps.users;
    notificationRoutesDeps.cookie_key = activity_deps.cookie_key;
    notificationRoutesDeps.require_user_auth_users_actor = activity_deps.require_user_auth_users_actor;
    notificationRoutesDeps.reply_json = activity_deps.reply_json;
    pqnas::notifications::register_notification_routes(srv, notificationRoutesDeps);

    {
        pqnas::updates::UpdateCenterRoutesDeps update_center_deps;
        update_center_deps.static_admin_updates_html = STATIC_ADMIN_UPDATES_HTML;
        update_center_deps.apps_installed_dir = APPS_INSTALLED_DIR;
        update_center_deps.read_file_to_string = read_file_to_string;
        update_center_deps.static_root_dir = static_root_dir;
        update_center_deps.config_root_dir = config_root_dir;
        update_center_deps.getenv_str = getenv_str;
        update_center_deps.sha256_hex = sha256_hex_lower_evp;
        update_center_deps.reply_json = reply_json;
        update_center_deps.require_admin =
            [&](const httplib::Request& req, httplib::Response& res) -> bool {
                return require_admin_cookie_users(req, res, COOKIE_KEY, std::string{}, &users);
            };
        update_center_deps.require_admin_actor =
            [&](const httplib::Request& req, httplib::Response& res, std::string* actor_fp) {
                return require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, actor_fp);
            };
        update_center_deps.audit_emit =
            [&](const std::string& event,
                const std::string& outcome,
                const std::map<std::string, std::string>& fields) {
                if (!v5.audit_emit) return;

                v5.audit_emit(event, outcome, [&](std::map<std::string, std::string>& f) {
                    for (const auto& kv : fields) {
                        f[kv.first] = kv.second;
                    }
                });
            };

        update_center_deps.record_activity =
            [&](const httplib::Request& req,
                const std::string& actor_fp,
                const std::string& event_type,
                const std::string& message,
                const std::map<std::string, std::string>& details) {
                if (actor_fp.empty() || event_type.empty()) return;

                std::filesystem::path user_root;
                try {
                    user_root = user_dir_for_fp(users, actor_fp);
                } catch (...) {
                    return;
                }

                if (user_root.empty()) return;

                pqnas::activity::ActivityEvent ev;
                ev.owner_user_id = actor_fp;
                ev.actor = activity_actor_for_request_local(&req, users, actor_fp);
                ev.event_type = event_type;
                ev.scope_type = "security";
                ev.scope_id = actor_fp;
                ev.target_kind = "update_center";
                ev.target_name = "Update Center";
                ev.message = message;

                ev.details = nlohmann::json::object();
                for (const auto& kv : details) {
                    if (!kv.first.empty() && !kv.second.empty()) {
                        ev.details[kv.first] = kv.second;
                    }
                }

                std::string activity_err;
                (void)pqnas::activity::record_user_activity(user_root, ev, &activity_err);
            };
        update_center_deps.require_same_origin = require_same_origin_for_cookie_mutation;

        pqnas::updates::register_update_center_routes(srv, update_center_deps);

        {
            pqnas::DriveLocateRoutesDeps drive_locate_deps;
            drive_locate_deps.require_admin_actor = update_center_deps.require_admin_actor;
drive_locate_deps.reply_json = reply_json;
            drive_locate_deps.audit_emit = update_center_deps.audit_emit;
            drive_locate_deps.wrapper_path = "/usr/local/sbin/pqnas-drive-locate";
                drive_locate_deps.require_same_origin = [&](const httplib::Request& req, httplib::Response& res) {
        return require_same_origin_for_cookie_mutation(req, res);
    };

pqnas::register_drive_locate_routes(srv, drive_locate_deps);
        }

    }

    pqnas::PeopleRoutesDeps people_deps;
    people_deps.users = activity_deps.users;
    people_deps.cookie_key = activity_deps.cookie_key;
    people_deps.people_db_path = std::filesystem::path(users_path).parent_path() / "people_contacts.sqlite3";
    people_deps.require_user_auth_users_actor = activity_deps.require_user_auth_users_actor;
    people_deps.reply_json = activity_deps.reply_json;
    pqnas::register_people_routes(srv, people_deps);

    pqnas::FileAnnotationRoutesDeps file_annotation_deps;
    file_annotation_deps.users = &users;
    file_annotation_deps.workspaces = &workspaces;
    file_annotation_deps.users_path = users_path;
    file_annotation_deps.workspaces_path = workspaces_path;
    file_annotation_deps.annotations_db_path =
        std::filesystem::path(users_path).parent_path() / "file_annotations.sqlite3";
    file_annotation_deps.cookie_key = COOKIE_KEY;
    file_annotation_deps.require_user_auth_users_actor = activity_deps.require_user_auth_users_actor;
    file_annotation_deps.reply_json = activity_deps.reply_json;
    file_annotation_deps.now_epoch_sec = []() {
        return now_epoch_sec();
    };
    file_annotation_deps.user_dir_for_fp =
        [&](pqnas::UsersRegistry& users_ref, const std::string& fp_hex) -> std::filesystem::path {
            return user_dir_for_fp(users_ref, fp_hex);
        };
    pqnas::register_file_annotation_routes(srv, file_annotation_deps);

    pqnas::FileLockRoutesDeps file_lock_deps;
    file_lock_deps.users = &users;
    file_lock_deps.workspaces = &workspaces;
    file_lock_deps.users_path = users_path;
    file_lock_deps.workspaces_path = workspaces_path;
    file_lock_deps.locks_db_path =
        std::filesystem::path(users_path).parent_path() / "file_locks.sqlite3";
    file_lock_deps.cookie_key = COOKIE_KEY;
    file_lock_deps.require_user_auth_users_actor = activity_deps.require_user_auth_users_actor;
    file_lock_deps.reply_json = activity_deps.reply_json;
    file_lock_deps.now_epoch_sec = []() {
        return now_epoch_sec();
    };
    file_lock_deps.user_dir_for_fp = [&](const std::string& fp_hex) -> std::filesystem::path {
        return user_dir_for_fp(users, fp_hex);
    };
    file_lock_deps.display_name_for_fp = [&](const std::string& fp_hex) -> std::string {
        return activity_user_display_name_local(users, fp_hex);
    };
    pqnas::register_file_lock_routes(srv, file_lock_deps);


trash_service.set_restore_reindexer(
    [&](const pqnas::TrashItemRec& rec,
        const std::filesystem::path& restored_abs_path,
        const std::string& restored_rel_path,
        std::string* err) -> bool {
        if (err) err->clear();

        // For now, only rebuild live metadata for single restored files.
        // Folder/subtree restore reindex can be added later.
        if (rec.item_type != "file") {
            return true;
        }

        auto* idx = pqnas::get_file_location_index();
        if (!idx) {
            if (err) *err = "file location index missing";
            return false;
        }

        if (rec.scope_id.empty()) {
            if (err) *err = "restore reindex failed: empty scope_id";
            return false;
        }
        if (restored_rel_path.empty()) {
            if (err) *err = "restore reindex failed: empty restored_rel_path";
            return false;
        }

        std::error_code ec;
        const auto st = std::filesystem::symlink_status(restored_abs_path, ec);
        if (ec) {
            if (err) *err = "restore stat failed: " + ec.message();
            return false;
        }
        if (!std::filesystem::exists(st) || !std::filesystem::is_regular_file(st)) {
            if (err) *err = "restored file missing or not regular";
            return false;
        }

        std::uint64_t size_bytes = rec.size_bytes;
        if (size_bytes == 0) {
            const auto sz = std::filesystem::file_size(restored_abs_path, ec);
            if (ec) {
                if (err) *err = "file_size failed: " + ec.message();
                return false;
            }
            size_bytes = static_cast<std::uint64_t>(sz);
        }

        const std::int64_t now_ts = now_epoch_sec();

        pqnas::FileLocationRecord flr;
        flr.fp = rec.scope_id;                    // user fp OR workspace_id
        flr.logical_rel_path = restored_rel_path;
        flr.current_pool = rec.source_pool;
        flr.physical_path = restored_abs_path.string();
        flr.tier_state = "capacity";
        flr.size_bytes = size_bytes;
        flr.mtime_epoch = now_ts;
        flr.created_epoch = now_ts;
        flr.updated_epoch = now_ts;
        flr.version = 1;

        std::string merr;
        if (!idx->upsert_landing_file(flr, &merr)) {
            if (err) *err = "file location reindex failed: " + merr;
            return false;
        }

        // Photo Gallery metadata/facts are only for user scope.
        if (rec.scope_type == "user") {
            if (auto* gidx = pqnas::get_gallery_meta_index()) {
                std::string gerr;
                (void)gidx->touch_file_facts(
                    "user",
                    rec.scope_id,
                    restored_rel_path,
                    size_bytes,
                    now_ts,
                    now_ts,
                    &gerr
                );
            }
        }

        return true;
    });

trash_service.set_restore_unindexer(
    [&](const pqnas::TrashItemRec& rec,
        const std::string& restored_rel_path) {
        if (rec.item_type != "file") return;
        if (rec.scope_id.empty() || restored_rel_path.empty()) return;

        if (auto* idx = pqnas::get_file_location_index()) {
            std::string uerr;
            (void)idx->erase(rec.scope_id, restored_rel_path, &uerr);
        }
    });

    pqnas::set_quota_extra_used_bytes_provider(
        [&](const std::string& scope_type,
            const std::string& scope_id,
            std::uint64_t* out_bytes,
            std::string* err) -> bool {
            if (out_bytes) *out_bytes = 0;
            if (err) err->clear();

            if (scope_type != "user") {
                return true;
            }
            if (scope_id.empty()) {
                return true;
            }

            return trash_index.sum_active_scope_bytes("user", scope_id, out_bytes, err);
        });

    trash_service.set_purge_cleanup(
        [&](const pqnas::TrashItemRec& rec,
            std::uint64_t* versions_deleted,
            std::uint64_t* version_bytes_deleted,
            std::uint64_t* version_blobs_missing,
            std::string* err) -> bool {
            if (versions_deleted) *versions_deleted = 0;
            if (version_bytes_deleted) *version_bytes_deleted = 0;
            if (version_blobs_missing) *version_blobs_missing = 0;
            if (err) err->clear();

            if (rec.scope_type != "user" && rec.scope_type != "workspace") {
                return true;
            }
            if (rec.scope_id.empty() || rec.original_rel_path.empty()) {
                return true;
            }

            std::filesystem::path scope_root;
            if (rec.scope_type == "user") {
                scope_root = user_dir_for_fp(users, rec.scope_id);
            } else {
                if (!workspaces.load(workspaces_path)) {
                    if (err) *err = "failed to reload workspaces";
                    return false;
                }

                auto wopt = workspaces.get(rec.scope_id);
                if (!wopt.has_value()) {
                    if (err) *err = "workspace not found";
                    return false;
                }

                const auto& w = *wopt;
                if (!w.storage_pool_id.empty()) {
                    if (err) *err = "workspace version cleanup currently supports default pool only";
                    return false;
                }

                scope_root = std::filesystem::path(pqnas::data_root_dir()) / w.root_rel;
            }

            pqnas::FileVersionsDeleteResult dr;
            std::string derr;
            const bool recursive = (rec.item_type == "dir");

            if (!file_versions_index.delete_versions_for_scope_path(
                    rec.scope_type,
                    rec.scope_id,
                    scope_root,
                    rec.original_rel_path,
                    recursive,
                    &dr,
                    &derr)) {
                if (err) *err = derr;
                return false;
            }

            if (versions_deleted) *versions_deleted = dr.versions_deleted;
            if (version_bytes_deleted) *version_bytes_deleted = dr.bytes_deleted;
            if (version_blobs_missing) *version_blobs_missing = dr.blobs_missing;
            return true;
        });

    pqnas::TrashRoutesDeps trash_deps;
    trash_deps.users = &users;
    trash_deps.users_path = &users_path;
    trash_deps.workspaces = &workspaces;
    trash_deps.workspaces_path = &workspaces_path;
    trash_deps.origin = &ORIGIN;
    trash_deps.trash_index = &trash_index;
    trash_deps.trash_service = &trash_service;
    trash_deps.cookie_key = COOKIE_KEY;

    trash_deps.require_user_auth_users_actor =
        [&](const httplib::Request& req,
            httplib::Response& res,
            const unsigned char* cookie_key,
            pqnas::UsersRegistry* users_ptr,
            std::string* fp_hex,
            std::string* role) -> bool {
            return require_user_auth_users_actor(req, res, cookie_key, users_ptr, fp_hex, role);
    };

    trash_deps.reply_json =
        [&](httplib::Response& res, int code, const std::string& body) {
            reply_json(res, code, body);
    };

    trash_deps.audit_emit =
        [&](const std::string& event,
            const std::string& outcome,
            const std::map<std::string, std::string>& f) {
            pqnas::AuditEvent ev;
            ev.event = event;
            ev.outcome = outcome;
            for (const auto& kv : f) ev.f[kv.first] = kv.second;
            audit_append(ev);
    };

    trash_deps.user_dir_for_fp =
        [&](pqnas::UsersRegistry& users_ref, const std::string& fp_hex) -> std::filesystem::path {
            return user_dir_for_fp(users_ref, fp_hex);
    };

    trash_deps.workspace_dir_for_default_pool_only =
        [&](const std::string& /*users_path_ref*/, const pqnas::WorkspaceRec& w) -> std::filesystem::path {
            return std::filesystem::path(pqnas::data_root_dir()) / w.root_rel;
    };
    pqnas::register_trash_routes(srv, trash_deps);

    pqnas::DropZoneRoutesDeps dropzone_deps;
    dropzone_deps.users = &users;
    dropzone_deps.dropzone_index = &dropzone_index;
    dropzone_deps.file_facts = &gallery_meta_index;
    dropzone_deps.file_locations = &file_location_index;
    dropzone_deps.file_versions = &file_versions_index;
    dropzone_deps.cookie_key = COOKIE_KEY;
    dropzone_deps.origin = &ORIGIN;
    dropzone_deps.random_b64url =
        [&](std::size_t n) -> std::string {
            return random_b64url(static_cast<int>(n));
    };
    dropzone_deps.now_epoch =
        []() -> std::int64_t {
            return static_cast<std::int64_t>(pqnas::now_epoch());
    };

    dropzone_deps.require_user_auth_users_actor =
        [&](const httplib::Request& req,
            httplib::Response& res,
            const unsigned char* cookie_key,
            pqnas::UsersRegistry* users_ptr,
            std::string* fp_hex,
            std::string* role) -> bool {
            return require_user_auth_users_actor(req, res, cookie_key, users_ptr, fp_hex, role);
    };

    dropzone_deps.reply_json =
        [&](httplib::Response& res, int code, const std::string& body) {
            reply_json(res, code, body);
    };

    dropzone_deps.audit_emit =
        [&](const std::string& event,
            const std::string& outcome,
            const std::map<std::string, std::string>& f) {
            pqnas::AuditEvent ev;
            ev.event = event;
            ev.outcome = outcome;
            for (const auto& kv : f) ev.f[kv.first] = kv.second;
            audit_append(ev);
    };

    dropzone_deps.user_dir_for_fp =
    [&](pqnas::UsersRegistry& users_ref, const std::string& fp_hex) -> std::filesystem::path {
        return user_dir_for_fp(users_ref, fp_hex);
    };

    pqnas::register_dropzone_routes(srv, dropzone_deps);
    pqnas::EchoStackRoutesDeps echo_deps;
    echo_deps.users = &users;
    echo_deps.echo_index = &echo_stack_index;
    echo_deps.cookie_key = COOKIE_KEY;
    echo_deps.origin = &ORIGIN;

    echo_deps.random_b64url =
        [&](std::size_t n) -> std::string {
            return random_b64url(static_cast<int>(n));
    };

    echo_deps.now_epoch =
        []() -> std::int64_t {
            return static_cast<std::int64_t>(pqnas::now_epoch());
    };

    echo_deps.user_dir_for_fp =
        [&](pqnas::UsersRegistry& users_ref, const std::string& fp_hex) -> std::filesystem::path {
            return user_dir_for_fp(users_ref, fp_hex);
    };

    echo_deps.require_user_auth_users_actor =
        [&](const httplib::Request& req,
            httplib::Response& res,
            const unsigned char* cookie_key,
            pqnas::UsersRegistry* users_ptr,
            std::string* fp_hex,
            std::string* role) -> bool {
            return require_user_auth_users_actor(req, res, cookie_key, users_ptr, fp_hex, role);
    };

    echo_deps.reply_json =
        [&](httplib::Response& res, int code, const std::string& body) {
            reply_json(res, code, body);
    };

    echo_deps.audit_emit =
        [&](const std::string& event,
            const std::string& outcome,
            const std::map<std::string, std::string>& f) {
            pqnas::AuditEvent ev;
            ev.event = event;
            ev.outcome = outcome;
            for (const auto& kv : f) ev.f[kv.first] = kv.second;
            audit_append(ev);
    };

    pqnas::register_echo_stack_routes(srv, echo_deps);

    pqnas::CircleStackRoutesDeps circle_deps;
    circle_deps.users = &users;
    circle_deps.cookie_key = COOKIE_KEY;
    circle_deps.user_dir_for_fp =
        [](pqnas::UsersRegistry& users_ref, const std::string& fp_hex) {
            return pqnas_user_dir_for_fp(users_ref, fp_hex);
        };
    circle_deps.require_user_auth_users_actor =
        [&](const httplib::Request& req,
            httplib::Response& res,
            const unsigned char* cookie_key,
            pqnas::UsersRegistry* users_ptr,
            std::string* fp_hex,
            std::string* role) -> bool {
            return require_user_auth_users_actor(req, res, cookie_key, users_ptr, fp_hex, role);
        };
    pqnas::register_circle_stack_routes(srv, circle_deps);
    pqnas::GalleryAlbumRoutesDeps gallery_album_deps;
    gallery_album_deps.users = &users;
    gallery_album_deps.albums = &gallery_albums_index;
    gallery_album_deps.cookie_key = COOKIE_KEY;
    gallery_album_deps.require_user_auth_users_actor =
        [](
            const httplib::Request& req,
            httplib::Response& res,
            const unsigned char* cookie_key,
            pqnas::UsersRegistry* users_ptr,
            std::string* fp_hex,
            std::string* role
        ) -> bool {
            return require_user_auth_users_actor(
                req,
                res,
                cookie_key,
                users_ptr,
                fp_hex,
                role
            );
    };
    gallery_album_deps.reply_json = reply_json;

    pqnas::register_gallery_album_routes(srv, gallery_album_deps);

    {
        pqnas::WorkspaceFileRouteDeps external_message_deps;
        external_message_deps.users = &users;
        external_message_deps.workspaces = &workspaces;
        external_message_deps.users_path = users_path;
        external_message_deps.workspaces_path = workspaces_path;
        external_message_deps.origin = &ORIGIN;
        external_message_deps.cookie_key = COOKIE_KEY;

        external_message_deps.reply_json =
            [&](httplib::Response& res, int code, const std::string& body) {
                reply_json(res, code, body);
        };

        external_message_deps.require_user_auth_users_actor =
            [&](const httplib::Request& req,
                httplib::Response& res,
                const unsigned char* cookie_key,
                pqnas::UsersRegistry* users_ptr,
                std::string* fp_hex,
                std::string* role) -> bool {
                return require_user_auth_users_actor(
                    req,
                    res,
                    cookie_key,
                    users_ptr,
                    fp_hex,
                    role
                );
        };

        external_message_deps.audit_emit =
            [&](const std::string& event,
                const std::string& outcome,
                const std::map<std::string, std::string>& fields) {
                pqnas::AuditEvent ev;
                ev.event = event;
                ev.outcome = outcome;
                for (const auto& kv : fields) {
                    ev.f[kv.first] = kv.second;
                }
                audit_append(ev);
        };

        external_message_deps.now_epoch_sec =
            []() -> std::int64_t {
                return static_cast<std::int64_t>(pqnas::now_epoch());
        };

        pqnas::register_workspace_external_message_routes(
            srv,
            external_message_deps
        );
    }

    {
        pqnas::WorkspaceFileRouteDeps workspace_link_deps;
        workspace_link_deps.users = &users;
        workspace_link_deps.workspaces = &workspaces;
        workspace_link_deps.users_path = users_path;
        workspace_link_deps.workspaces_path = workspaces_path;
        workspace_link_deps.origin = &ORIGIN;
        workspace_link_deps.cookie_key = COOKIE_KEY;

        workspace_link_deps.reply_json =
            [&](httplib::Response& res, int code, const std::string& body) {
                reply_json(res, code, body);
        };

        workspace_link_deps.require_user_auth_users_actor =
            [&](const httplib::Request& req,
                httplib::Response& res,
                const unsigned char* cookie_key,
                pqnas::UsersRegistry* users_ptr,
                std::string* fp_hex,
                std::string* role) -> bool {
                return require_user_auth_users_actor(
                    req,
                    res,
                    cookie_key,
                    users_ptr,
                    fp_hex,
                    role
                );
        };

        workspace_link_deps.audit_emit =
            [&](const std::string& event,
                const std::string& outcome,
                const std::map<std::string, std::string>& fields) {
                pqnas::AuditEvent ev;
                ev.event = event;
                ev.outcome = outcome;
                for (const auto& kv : fields) {
                    ev.f[kv.first] = kv.second;
                }
                audit_append(ev);
        };

        workspace_link_deps.now_epoch_sec =
            []() -> std::int64_t {
                return static_cast<std::int64_t>(pqnas::now_epoch());
        };

        pqnas::register_workspace_link_routes(srv, workspace_link_deps);
    }


// GET /api/public/auth_mode
// Returns installer-selected auth mode for login page: v4 | v5 | auto
srv.Get("/api/public/auth_mode", [&](const httplib::Request& /*req*/, httplib::Response& res) {
    std::string mode = "v4";
    if (const char* v = std::getenv("PQNAS_AUTH_MODE")) mode = v;

    mode = pqnas::lower_ascii(mode);
    if (mode != "v4" && mode != "v5" && mode != "auto") mode = "auto";

    nlohmann::json out = {
        {"ok", true},
        {"auth_mode", mode}
    };
    reply_json(res, 200, out.dump());
});



// ----- GET /api/v4/system (user+admin) --------------------------------------
srv.Get("/api/v4/system", [&](const httplib::Request& req, httplib::Response& res) {
    std::string actor_fp, role;
    if (!require_user_cookie_users_actor(req, res, COOKIE_KEY, &users, &actor_fp, &role)) return;

    json out = pqnas::collect_system_snapshot(REPO_ROOT);

    // keep viewer here because it's auth/policy-level, not "system metrics"
    out["viewer"] = {
        {"fingerprint_hex", actor_fp},
        {"role", role}
    };

    res.set_header("Cache-Control", "no-store");
    reply_json(res, 200, out.dump());
});

    // ---- Public installed app asset routes ----
    {
        AppsPublicRoutesContext apps_public_ctx;
        apps_public_ctx.apps_installed_dir = APPS_INSTALLED_DIR;
        apps_public_ctx.apps_bundled_dir = APPS_BUNDLED_DIR;
        apps_public_ctx.server_version = PQNAS_VERSION;

        apps_public_ctx.load_app_launch_policy_json =
            [&]() {
                return load_app_launch_policy_json();
            };

        apps_public_ctx.is_admin_cookie =
            [&](const httplib::Request& req) {
                return is_admin_cookie_users(req, COOKIE_KEY, &users);
            };

        apps_public_ctx.serve_file_under_root =
            [&](const std::string& root,
                const std::string& rel,
                const std::string& content_type,
                httplib::Response& res,
                bool no_store) {
                serve_file_under_root(root, rel, content_type, res, no_store);
            };

        apps_public_ctx.read_file_to_string =
            [](const std::string& path, std::string& body) {
                return read_file_to_string(path, body);
            };

        apps_public_ctx.rel_to_repo =
            [](const std::string& path) {
                return rel_to_repo(path);
            };

        register_apps_public_routes(srv, apps_public_ctx);
    }


    // ---- Core UI shell routes ----
    {
        CoreUiShellRoutesContext core_ui_shell_ctx;
        core_ui_shell_ctx.static_system_js = STATIC_SYSTEM_JS;
        core_ui_shell_ctx.static_audit_html = STATIC_AUDIT_HTML;
        core_ui_shell_ctx.static_admin_html = STATIC_ADMIN_HTML;
        core_ui_shell_ctx.static_app_js = STATIC_APP_JS;
        core_ui_shell_ctx.static_admin_js = STATIC_ADMIN_JS;

        core_ui_shell_ctx.require_admin =
            [&](const httplib::Request& req, httplib::Response& res, std::string* actor_fp) {
                return require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, actor_fp);
            };

        core_ui_shell_ctx.read_file_to_string =
            [](const std::string& path, std::string& body) {
                return read_file_to_string(path, body);
            };

        core_ui_shell_ctx.slurp_file =
            [](const std::string& path) {
                return slurp_file(path);
            };

        register_core_ui_shell_routes(srv, core_ui_shell_ctx);
    }

    // ---- Admin audit read/UI routes ----
    {
        AdminAuditReadRoutesContext admin_audit_read_ctx;
        admin_audit_read_ctx.static_audit_js = STATIC_AUDIT_JS;
        admin_audit_read_ctx.audit_jsonl_path = audit_jsonl_path;
        admin_audit_read_ctx.audit_state_path = audit_state_path;

        admin_audit_read_ctx.require_admin =
            [&](const httplib::Request& req, httplib::Response& res) {
                return require_admin_cookie_users(req, res, COOKIE_KEY, std::string{}, &users);
            };

        admin_audit_read_ctx.reply_json =
            [](httplib::Response& res, int status, const std::string& body) {
                reply_json(res, status, body);
            };

        admin_audit_read_ctx.slurp_file =
            [](const std::string& path) {
                return slurp_file(path);
            };

        admin_audit_read_ctx.trim_nl =
            [](const std::string& v) {
                return trim_nl(v);
            };

        register_admin_audit_read_routes(srv, admin_audit_read_ctx);
    }

    // ---- Admin audit rotate route ----
    {
        AdminAuditRotateRoutesContext admin_audit_rotate_ctx;

        admin_audit_rotate_ctx.require_admin =
            [&](const httplib::Request& req, httplib::Response& res) {
                return require_admin_cookie_users(req, res, COOKIE_KEY, std::string{}, &users);
            };

        admin_audit_rotate_ctx.require_same_origin =
            [&](const httplib::Request& req, httplib::Response& res) {
                return require_same_origin_for_cookie_mutation(req, res);
            };

        admin_audit_rotate_ctx.rotate_audit =
            [&](nlohmann::json* out) -> bool {
                pqnas::AuditLog::RotateOptions opt;
                pqnas::AuditLog::RotateResult rr;

                const bool ok = audit.rotate(opt, &rr);
                if (!ok) return false;

                (*out)["ok"] = true;
                (*out)["rotated_jsonl_path"] = rr.rotated_jsonl_path;
                (*out)["rotated_state_path"] = rr.rotated_state_path;
                (*out)["chain_start_prev_hash"] = rr.chain_start_prev_hash_hex;
                return true;
            };

        register_admin_audit_rotate_routes(srv, admin_audit_rotate_ctx);
    }

    // ---- Admin audit retention routes ----
    {
        AdminAuditRetentionRoutesContext admin_audit_retention_ctx;

        admin_audit_retention_ctx.require_admin =
            [&](const httplib::Request& req, httplib::Response& res) {
                return require_admin_cookie_users(req, res, COOKIE_KEY, std::string{}, &users);
            };

        admin_audit_retention_ctx.require_same_origin =
            [&](const httplib::Request& req, httplib::Response& res) {
                return require_same_origin_for_cookie_mutation(req, res);
            };

        admin_audit_retention_ctx.reply_json =
            [](httplib::Response& res, int status, const std::string& body) {
                reply_json(res, status, body);
            };

        admin_audit_retention_ctx.preview_prune =
            [&](const nlohmann::json& in, nlohmann::json* out, int* status) -> bool {
                if (!out || !status) return false;

                nlohmann::json pol = nlohmann::json::object();
                if (in.contains("audit_retention")) {
                    pol = in["audit_retention"];
                }

                pol = normalize_retention_or_default_local(pol);

                const auto archives = list_rotated_archives_local(audit_jsonl_path);
                *out = build_preview_local(archives, pol);
                *status = 200;
                return true;
            };

        admin_audit_retention_ctx.prune =
            [&](const httplib::Request& req, nlohmann::json* out, int* status) -> bool {
                if (!out || !status) return false;

                nlohmann::json persisted = nlohmann::json::object();
                try {
                    std::ifstream f(admin_settings_path);
                    if (f.good()) f >> persisted;
                    if (!persisted.is_object()) persisted = nlohmann::json::object();
                } catch (...) {
                    persisted = nlohmann::json::object();
                }

                nlohmann::json pol = nlohmann::json::object();
                if (persisted.contains("audit_retention")) {
                    pol = persisted["audit_retention"];
                }
                pol = normalize_retention_or_default_local(pol);

                const auto archives = list_rotated_archives_local(audit_jsonl_path);
                const auto preview = build_preview_local(archives, pol);

                long long deleted_bytes = 0;
                int deleted_files = 0;

                try {
                    const auto cands = preview.value("candidates", nlohmann::json::array());

                    for (const auto& cj : cands) {
                        const std::string name = cj.value("name", "");
                        if (name.empty()) continue;

                        auto it = std::find_if(
                            archives.begin(),
                            archives.end(),
                            [&](const ArchivePair& a) {
                                return a.name == name;
                            }
                        );

                        if (it == archives.end()) continue;

                        std::error_code ec;

                        if (!it->jsonl_path.empty()) {
                            if (std::filesystem::remove(it->jsonl_path, ec)) deleted_files++;
                            ec.clear();
                        }

                        if (!it->state_path.empty()) {
                            if (std::filesystem::remove(it->state_path, ec)) deleted_files++;
                            ec.clear();
                        }

                        deleted_bytes += std::max(0LL, it->size_bytes);
                    }

                    try {
                        pqnas::AuditEvent ev;
                        ev.event = "admin.audit_pruned";
                        ev.outcome = "ok";
                        ev.f["deleted_files"] = deleted_files;
                        ev.f["deleted_bytes"] = deleted_bytes;
                        ev.f["policy"] = pol;
                        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

                        auto it_ua = req.headers.find("User-Agent");
                        ev.f["ua"] = pqnas::shorten(
                            it_ua == req.headers.end() ? "" : it_ua->second
                        );

                        audit_append(ev);
                    } catch (...) {
                    }

                    *out = nlohmann::json{
                        {"ok", true},
                        {"deleted_files", deleted_files},
                        {"deleted_bytes", deleted_bytes}
                    };
                    *status = 200;
                    return true;

                } catch (...) {
                    *out = nlohmann::json{
                        {"ok", false},
                        {"error", "server_error"},
                        {"message", "prune failed"}
                    };
                    *status = 500;
                    return true;
                }
            };

        register_admin_audit_retention_routes(srv, admin_audit_retention_ctx);
    }




    
	srv.Get("/static/pqnas_v5.js", [&](const httplib::Request&, httplib::Response& res) {
    	std::string body;
    	if (!read_file_to_string(STATIC_V5_JS, body) || body.empty()) {
        	res.status = 404;
	        res.set_content("missing pqnas_v5.js", "text/plain; charset=utf-8");
    	    return;
	    }
    	res.set_header("Cache-Control", "no-store");
	    res.set_header("Content-Type", "application/javascript; charset=utf-8");
    	res.body = std::move(body);
	});


    // after successful consume, browser goes here
    srv.Get("/success", [&](const httplib::Request&, httplib::Response& res) {
        res.status = 302;
        res.set_header("Location", "/app");
    });



/*
    srv.Get("/app", [&](const httplib::Request&, httplib::Response& res) {
        const std::string body = slurp_file(STATIC_APP_HTML);
        if (body.empty()) {
            res.status = 404;
            res.set_content("missing app.html", "text/plain");
            return;
        }
        res.set_content(body, "text/html; charset=utf-8");
    });
*/
    srv.Get("/app", [&](const httplib::Request&, httplib::Response& res) {
        const std::string body = slurp_file(STATIC_APP_HTML);
        if (body.empty()) { res.status = 404; res.set_content("missing app.html","text/plain"); return; }
        res.set_header("Cache-Control", "no-store");
        res.set_header("X-Content-Type-Options", "nosniff");
        res.set_content(body, "text/html; charset=utf-8");
    });



    srv.Get("/", [&](const httplib::Request&, httplib::Response& res) {
        std::string body;
        if (!read_file_to_string(STATIC_LOGIN, body)) {
            res.status = 500;
            res.set_header("Content-Type", "text/plain");
            res.body = "Missing static file: " + STATIC_LOGIN;
            return;
        }
        res.status = 200;
        res.set_header("Content-Type", "text/html; charset=utf-8");
        res.set_header("Cache-Control", "no-store");
        res.body = body;
    });


    srv.Get("/api/v4/admin/ping", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_admin_cookie_users(req, res, COOKIE_KEY, users_path, &users)) {
            return;
        }
        reply_json(res, 200, json({{"ok",true},{"admin",true}}).dump());
    });

    register_storage_raid_routes(srv, StorageRaidRoutesContext{
        COOKIE_KEY,
        users_path,
        workspaces_path,
        audit_append
    });




// ============================================================================
//  ADMIN SETTINGS ROUTES
// ============================================================================

    // ---- Admin settings routes ----
    // Transitional bulk split: route/helper block lives in routes_admin_settings.inc.

#include "routes/routes_admin_settings.inc"

	srv.Get("/api/v4/me", [&](const httplib::Request& req, httplib::Response& res) {
    	auto audit_ua = [&]() -> std::string {
	        auto it = req.headers.find("User-Agent");
        	return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    	};

    	auto audit_fail = [&](const std::string& reason) {
	        pqnas::AuditEvent ev;
        	ev.event = "v4.me_fail";
    	    ev.outcome = "fail";
	        ev.f["reason"] = reason;

    	    auto it_cf = req.headers.find("CF-Connecting-IP");
	        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

    	    auto it_xff = req.headers.find("X-Forwarded-For");
	        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

    	    ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
	        ev.f["ua"] = audit_ua();
    	    audit_append(ev);
	    };

	    auto audit_ok = [&](const std::string& fp_b64, long exp, const std::string& role) {
        	pqnas::AuditEvent ev;
    	    ev.event = "me_ok";
	        ev.outcome = "ok";
        	ev.f["fingerprint_b64"] = pqnas::shorten(fp_b64, 120);
    	    ev.f["exp"] = std::to_string(exp);
	        ev.f["role"] = role;

        	ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

    	    auto it_cf = req.headers.find("CF-Connecting-IP");
	        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        	auto it_xff = req.headers.find("X-Forwarded-For");
    	    if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

	        ev.f["ua"] = audit_ua();
    	    audit_append(ev);
	    };

    	auto it = req.headers.find("Cookie");
    	if (it == req.headers.end()) {
	        audit_fail("missing_cookie_header");
        	reply_json(res, 401, json({{"ok",false},{"error","unauthorized"},{"message","missing cookie"}}).dump());
    	    return;
	    }

	    const std::string& hdr = it->second;
        const std::string cookieVal = extract_named_cookie_value(hdr, "pqnas_session");
        if (cookieVal.empty()) {
            audit_fail("missing_pqnas_session");
            reply_json(res, 401, json({{"ok",false},{"error","unauthorized"},{"message","missing pqnas_session"}}).dump());
            return;
        }

    	std::string fp_b64;
    	long exp = 0;
    	if (!session_cookie_verify(COOKIE_KEY, cookieVal, fp_b64, exp)) {
    	    audit_fail("cookie_verify_failed");
	        reply_json(res, 401, json({{"ok",false},{"error","unauthorized"},{"message","invalid session"}}).dump());
        	return;
    	}

    	long now = pqnas::now_epoch();
    	if (now > exp) {
    	    audit_fail("session_expired");
	        reply_json(res, 401, json({{"ok",false},{"error","unauthorized"},{"message","session expired"}}).dump());
        	return;
    	}

    	// Decode cookie identity: cookie stores standard base64 of UTF-8 fingerprint hex string
	    std::string fp_hex;
    	{
    	    std::string raw;
	        if (!b64std_decode_to_bytes(fp_b64, raw)) {
            	audit_fail("fingerprint_b64_decode_failed");
        	    reply_json(res, 401, json({{"ok",false},{"error","unauthorized"},{"message","invalid session"}}).dump());
    	        return;
	        }
        	fp_hex.assign(raw.begin(), raw.end());
    	}

    	// Policy check (fail-closed)
	    const bool is_admin = users.is_admin_enabled(fp_hex);
	   	const bool is_user  = users.is_enabled_user(fp_hex) || is_admin;

    	if (!is_user) {
    	    audit_fail("policy_denied");
	        reply_json(res, 403, json({{"ok",false},{"error","forbidden"},{"message","policy denied"}}).dump());
        	return;
    	}

    	const std::string role = is_admin ? "admin" : "user";
    	audit_ok(fp_b64, exp, role);

    	// Include storage status + profile metadata (if present)
    	std::string storage_state = "unallocated";
	    std::uint64_t quota_bytes = 0;
    	std::string root_rel;
	    std::string group;
    	if (auto u = users.get(fp_hex); u.has_value()) {
    	    if (!u->storage_state.empty()) storage_state = u->storage_state;
	        quota_bytes = u->quota_bytes;
        	root_rel = u->root_rel;
    	    group = u->group;
	    }

    	reply_json(res, 200, json({
    	    {"ok",true},
	        {"exp",exp},
        	{"fingerprint_b64",fp_b64},
    	    {"fingerprint_hex",fp_hex},
	        {"role", role},

        	{"storage_state", storage_state},
    	    {"quota_bytes", quota_bytes},
    	    {"server_version", PQNAS_VERSION},
	        {"root_rel", root_rel},
        	{"group", group}
    	}).dump());
	});
// GET /api/v4/user/profile
// Normal signed-in users can read their own editable profile fields.
srv.Get("/api/v4/user/profile", [&](const httplib::Request& req, httplib::Response& res) {
    std::string actor_fp, actor_role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &actor_fp, &actor_role)) return;

    if (!users.load(users_path)) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "users_reload_failed"},
            {"message", "failed to reload users"}
        }.dump());
        return;
    }

    auto uopt = users.get(actor_fp);
    if (!uopt.has_value()) {
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "user not found"}
        }.dump());
        return;
    }

    const auto& u = *uopt;

    res.set_header("Cache-Control", "no-store");
    reply_json(res, 200, json{
        {"ok", true},
        {"profile", {
            {"fingerprint", actor_fp},
            {"role", actor_role},
            {"status", u.status},
            {"name", u.name},
            {"email", u.email},
            {"avatar_url", u.avatar_url},
            {"group", u.group}
        }}
    }.dump());
});


// POST /api/v4/user/profile/update
// Normal signed-in users can update only their own safe profile fields.
// IMPORTANT: fingerprint is derived from session, never from request body.
srv.Post("/api/v4/user/profile/update", [&](const httplib::Request& req, httplib::Response& res) {
    std::string actor_fp, actor_role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &actor_fp, &actor_role)) return;

    json j;
    try {
        j = json::parse(req.body);
    } catch (...) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid json"}
        }.dump());
        return;
    }

    if (!users.load(users_path)) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "users_reload_failed"},
            {"message", "failed to reload users"}
        }.dump());
        return;
    }

    auto cur = users.get(actor_fp);
    if (!cur.has_value()) {
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "user not found"}
        }.dump());
        return;
    }

    std::string name = trim_copy(j.value("name", std::string()));
    std::string email = trim_copy(j.value("email", std::string()));
    std::string avatar_url = trim_copy(j.value("avatar_url", std::string()));

    if (name.size() > 120) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "name too long"}
        }.dump());
        return;
    }

    if (email.size() > 254 ||
        email.find('\n') != std::string::npos ||
        email.find('\r') != std::string::npos) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid email"}
        }.dump());
        return;
    }

    // Users may clear avatar_url, or keep/set their own internal avatar URL.
    // Do not allow arbitrary external URLs here.
    const std::string own_avatar_url =
        std::string("/api/v4/users/avatar?fingerprint=") + actor_fp;

    if (!avatar_url.empty() && avatar_url != own_avatar_url) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "avatar_url must be empty or your own uploaded avatar URL"}
        }.dump());
        return;
    }

    pqnas::UserRec u = *cur;

    // Safe self-edit fields only.
    u.name = name;
    u.email = email;
    u.avatar_url = avatar_url;

    const bool ok_upsert = users.upsert(u);
    const bool ok_save = ok_upsert ? users.save(users_path) : false;

    {
        pqnas::AuditEvent ev;
        ev.event = "user.profile_update";
        ev.outcome = (ok_upsert && ok_save) ? "ok" : "fail";
        ev.f["actor_fp"] = actor_fp;
        ev.f["role"] = actor_role;
        if (!name.empty()) ev.f["name"] = pqnas::shorten(name, 80);
        if (!email.empty()) ev.f["email"] = pqnas::shorten(email, 120);
        ev.f["avatar"] = avatar_url.empty() ? "empty" : "set";
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
        audit_append(ev);
    }

    if (!ok_upsert) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "profile update failed"}
        }.dump());
        return;
    }

    if (!ok_save) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "users save failed"}
        }.dump());
        return;
    }

    reply_json(res, 200, json{
        {"ok", true},
        {"profile", {
            {"fingerprint", actor_fp},
            {"role", actor_role},
            {"status", u.status},
            {"name", u.name},
            {"email", u.email},
            {"avatar_url", u.avatar_url},
            {"group", u.group}
        }}
    }.dump());
});
    // --- Shared verify context (used by login verification routes) ---
VerifyLoginCommonContext c;

c.origin = &ORIGIN;
c.rp_id  = &RP_ID;

c.server_pk  = SERVER_PK;
c.server_sk  = SERVER_SK;
c.cookie_key = COOKIE_KEY;

c.sess_ttl = &SESS_TTL;

c.allowlist = &allowlist;
c.users     = &users;
c.allowlist_path = &allowlist_path;
c.users_path     = &users_path;

// approvals/pending (bridge)
c.approvals_prune = [&](long now){ approvals_prune(now); };
c.pending_prune   = [&](long now){ pending_prune(now); };

c.approvals_get = [&](const std::string& sid, VerifyLoginCommonContext::ApprovalEntry& out){
    ApprovalEntry e;
    if (!approvals_get(sid, e)) return false;
    out.cookie_val  = e.cookie_val;
    out.fingerprint = e.fingerprint;
    out.expires_at  = e.expires_at;
    return true;
};

c.approvals_put = [&](const std::string& sid, const VerifyLoginCommonContext::ApprovalEntry& in){
    ApprovalEntry e;
    e.cookie_val  = in.cookie_val;
    e.fingerprint = in.fingerprint;
    e.expires_at  = in.expires_at;
    approvals_put(sid, e);
};

c.approvals_pop = [&](const std::string& sid){ approvals_pop(sid); };

    c.pending_get = [&](const std::string& sid, VerifyLoginCommonContext::PendingEntry& out){
        PendingEntry p;
        if (!pending_get(sid, p)) return false;
        out.expires_at = p.expires_at;
        out.reason = p.reason;
        out.browser_bind_hash = p.browser_bind_hash;
        out.fingerprint_hex = p.fingerprint_hex;
        return true;
    };

    c.pending_put = [&](const std::string& sid, const VerifyLoginCommonContext::PendingEntry& in){
        PendingEntry p;
        p.expires_at = in.expires_at;
        p.reason = in.reason;

        // Preserve browser binding and fingerprint independently.
        // A pending-admin update often has a new fingerprint but wants to keep
        // the browser binding minted earlier by /api/v5/session.
        PendingEntry oldp;
        const bool had_old_pending = pending_get(sid, oldp);

        if (!in.browser_bind_hash.empty()) {
            p.browser_bind_hash = in.browser_bind_hash;
        } else if (had_old_pending) {
            p.browser_bind_hash = oldp.browser_bind_hash;
        }

        if (!in.fingerprint_hex.empty()) {
            p.fingerprint_hex = in.fingerprint_hex;
        } else if (had_old_pending) {
            p.fingerprint_hex = oldp.fingerprint_hex;
        }

        pending_put(sid, p);
    };

c.pending_pop = [&](const std::string& sid){ pending_pop(sid); };

// time + helpers
c.now_epoch   = [](){ return pqnas::now_epoch(); };
c.now_iso_utc = [](){ return pqnas::now_iso_utc(); }; // pqnas_util

c.client_ip = [&](const httplib::Request& r){ return client_ip(r); };
c.shorten   = [&](const std::string& s, size_t n){ return pqnas::shorten(s, n); };

// crypto hooks
c.sign_token_v4_ed25519 = [&](const json& payload, const unsigned char* sk){
    return sign_token_v4_ed25519(payload, sk);
};

c.session_cookie_mint = [&](const unsigned char* key,
                            const std::string& fp_b64,
                            long iat, long exp,
                            std::string& out_cookie){
    return session_cookie_mint(key, fp_b64, iat, exp, out_cookie);
};

c.b64_std = [&](const unsigned char* data, size_t len){
    return pqnas::b64_std(data, len);
};

c.audit_emit = [&](const std::string& event,
                   const std::string& outcome,
                   const std::function<void(std::map<std::string,std::string>&)>& fill){
    pqnas::AuditEvent ev;
    ev.event   = event;
    ev.outcome = outcome;

    std::map<std::string,std::string> f;
    fill(f);
    for (auto& kv : f) ev.f[kv.first] = kv.second;

    audit_append(ev);
};




c.external_invite_accept_by_st_hash =
    [&](const std::string& st_hash_b64,
        const std::string& fingerprint_hex,
        VerifyLoginCommonContext::ExternalInviteAcceptResult& out,
        std::string& err) -> bool {
        out = VerifyLoginCommonContext::ExternalInviteAcceptResult{};
        err.clear();

        if (st_hash_b64.empty() || fingerprint_hex.empty()) {
            return false;
        }

        if (!workspace_external_invites.load(workspace_external_invites_path)) {
            err = "failed to load external invites";
            return false;
        }

        const auto inv_opt = workspace_external_invites.get_by_st_hash_b64(st_hash_b64);
        if (!inv_opt.has_value()) {
            return false; // normal login/auth flow, not an external invite
        }

        pqnas::WorkspaceExternalInviteRec inv = *inv_opt;

        out.invite_id = inv.invite_id;
        out.workspace_id = inv.workspace_id;
        out.role = inv.role;
        out.fingerprint_hex = fingerprint_hex;

        const long now = now_epoch_sec();

        if (inv.status != "pending") {
            out.accepted = false;
            out.message = "invite is not pending";
            err = out.message;
            return true; // handled as external invite, but refused
        }

        if (inv.expires_at_epoch > 0 && now > inv.expires_at_epoch) {
            workspace_external_invites.mark_expired_pending(now);
            (void)workspace_external_invites.save(workspace_external_invites_path);

            out.accepted = false;
            out.message = "invite expired";
            err = out.message;
            return true;
        }

        if (!workspaces.load(workspaces_path)) {
            out.accepted = false;
            out.message = "failed to load workspaces";
            err = out.message;
            return true;
        }

        auto wopt = workspaces.get(inv.workspace_id);
        if (!wopt.has_value() || wopt->status != "enabled") {
            out.accepted = false;
            out.message = "workspace not found";
            err = out.message;
            return true;
        }

        const auto existing_member = workspaces.get_member(inv.workspace_id, fingerprint_hex);
        if (existing_member.has_value() &&
            existing_member->status == "enabled" &&
            existing_member->member_kind != "external") {
            out.accepted = false;
            out.message = "fingerprint is already a normal workspace member";
            err = out.message;
            return true;
        }

        const std::string now_iso = pqnas::now_iso_utc();

        pqnas::WorkspaceMemberRec member;
        member.fingerprint = fingerprint_hex;
        member.role = inv.role;
        member.status = "enabled";
        member.member_kind = "external";
        member.display_name = "External member";
        member.added_at = inv.created_at.empty() ? now_iso : inv.created_at;
        member.added_by = inv.created_by;
        member.responded_at = now_iso;
        member.responded_by = fingerprint_hex;

        if (!workspaces.add_or_update_member(inv.workspace_id, member)) {
            out.accepted = false;
            out.message = "failed to add external member";
            err = out.message;
            return true;
        }

        if (!workspaces.save(workspaces_path)) {
            out.accepted = false;
            out.message = "failed to save workspace member";
            err = out.message;
            return true;
        }

        if (!workspace_external_invites.mark_accepted(inv.invite_id, fingerprint_hex, now_iso)) {
            out.accepted = false;
            out.message = "failed to mark invite accepted";
            err = out.message;
            return true;
        }

        if (!workspace_external_invites.save(workspace_external_invites_path)) {
            out.accepted = false;
            out.message = "failed to save external invite";
            err = out.message;
            return true;
        }

        pqnas::AuditEvent ev;
        ev.event = "workspace.external_invite_accepted";
        ev.outcome = "ok";
        ev.f = {
            {"invite_id", inv.invite_id},
            {"workspace_id", inv.workspace_id},
            {"role", inv.role},
            {"fingerprint", fingerprint_hex},
            {"created_by", inv.created_by}
        };
        audit_append(ev);

        out.accepted = true;
        out.message = "accepted";
        return true;
    };



c.external_session_accept_by_st_hash =
    [&](const std::string& st_hash_b64,
        const std::string& fingerprint_hex,
        VerifyLoginCommonContext::ExternalSessionAcceptResult& out,
        std::string& err) -> bool {
        out = VerifyLoginCommonContext::ExternalSessionAcceptResult{};
        err.clear();

        if (st_hash_b64.empty() || fingerprint_hex.empty()) {
            return false;
        }

        const auto sess_opt = workspace_external_sessions.get_by_st_hash_b64(st_hash_b64);
        if (!sess_opt.has_value()) {
            return false; // normal login/auth flow, not an external workspace session
        }

        pqnas::WorkspaceExternalSessionRec sess = *sess_opt;

        out.session_id = sess.session_id;
        out.workspace_id = sess.workspace_id;
        out.fingerprint_hex = fingerprint_hex;

        const long now = now_epoch_sec();

        if (sess.status != "pending") {
            out.accepted = false;
            out.message = "session is not pending";
            err = out.message;
            return true;
        }

        if (sess.expires_at_epoch > 0 && now > sess.expires_at_epoch) {
            workspace_external_sessions.mark_expired_pending(now);

            out.accepted = false;
            out.message = "session expired";
            err = out.message;
            return true;
        }

        if (!workspaces.load(workspaces_path)) {
            out.accepted = false;
            out.message = "failed to load workspaces";
            err = out.message;
            return true;
        }

        auto member_opt = workspaces.get_member(sess.workspace_id, fingerprint_hex);
        if (!member_opt.has_value() || member_opt->status != "enabled") {
            workspace_external_sessions.mark_denied(sess.session_id, "not a workspace member");

            out.accepted = false;
            out.message = "not a workspace member";
            err = out.message;
            return true;
        }

        if (member_opt->member_kind != "external") {
            workspace_external_sessions.mark_denied(sess.session_id, "not an external member");

            out.accepted = false;
            out.message = "not an external member";
            err = out.message;
            return true;
        }

        const std::string role = member_opt->role;
        const std::string now_iso = pqnas::now_iso_utc();

        if (!workspace_external_sessions.mark_approved(sess.session_id, fingerprint_hex, role, now_iso)) {
            out.accepted = false;
            out.message = "failed to approve external session";
            err = out.message;
            return true;
        }

        pqnas::AuditEvent ev;
        ev.event = "workspace.external_session_approved";
        ev.outcome = "ok";
        ev.f = {
            {"session_id", sess.session_id},
            {"workspace_id", sess.workspace_id},
            {"role", role},
            {"fingerprint", fingerprint_hex}
        };
        audit_append(ev);

        out.accepted = true;
        out.role = role;
        out.message = "approved";
        return true;
    };


srv.Post("/api/v5/verify", [&](const httplib::Request& req, httplib::Response& res) {
    const std::string ip_for_rate_limit = client_ip(req);
    if (!simple_ip_rate_limit_allow(
            "v5.verify",
            ip_for_rate_limit,
            30,
            std::chrono::seconds(60))) {
        if (c.audit_emit) c.audit_emit("rate_limited", "deny", [&](std::map<std::string,std::string>& f){
            f["path"] = "/api/v5/verify";
            f["ip"] = ip_for_rate_limit;
            f["limit"] = "30_per_60s";
        });
        return set_rate_limited_json(res, "too_many_verify_requests");
    }

    if (c.audit_emit) c.audit_emit("route.hit", "ok", [&](std::map<std::string,std::string>& f){
        f["path"] = "/api/v5/verify";
        f["ip"]   = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it = req.headers.find("User-Agent");
        if (it != req.headers.end()) f["ua"] = c.shorten ? c.shorten(it->second, 120) : it->second;

        auto ct = req.headers.find("Content-Type");
        if (ct != req.headers.end()) f["content_type"] = c.shorten ? c.shorten(ct->second, 80) : ct->second;

        f["body_len"] = std::to_string(req.body.size());

        f["body_len"] = std::to_string(req.body.size());
        if (!req.body.empty()) {
            f["body_sha256"] = sha256_hex_lower_evp(req.body);
        }
    });

    handle_verify_login_common(req, res, c);
});



    // GET /wait-approval (static UI)
    srv.Get("/wait-approval", [&](const httplib::Request&, httplib::Response& res) {
        const std::string body = slurp_file(STATIC_WAIT_APPROVAL_HTML);
        if (body.empty()) { res.status = 404; res.set_content("missing wait_approval.html","text/plain"); return; }
        res.set_header("Cache-Control", "no-store");
        res.set_content(body, "text/html; charset=utf-8");
    });

    // GET /static/wait_approval.js
    srv.Get("/static/wait_approval.js", [&](const httplib::Request&, httplib::Response& res) {
        const std::string body = slurp_file(STATIC_WAIT_APPROVAL_JS);
        if (body.empty()) { res.status = 404; res.set_content("missing wait_approval.js","text/plain"); return; }
        res.set_header("Cache-Control", "no-store");
        res.set_content(body, "application/javascript; charset=utf-8");
    });

    srv.Get("/admin/apps", [&](const httplib::Request& req, httplib::Response& res) {
    if (!require_admin_cookie_users(req, res, COOKIE_KEY, std::string{}, &users)) return;

    std::string body;
    if (!read_file_to_string(STATIC_ADMIN_APPS_HTML, body)) {
        res.status = 404;
        res.body = "Missing static file: " + STATIC_ADMIN_APPS_HTML;
        return;
    }

    res.set_header("Cache-Control", "no-store");
    res.set_content(body, "text/html; charset=utf-8");
    });


    






























    srv.Get("/static/admin_apps.js", [&](const httplib::Request&, httplib::Response& res) {
        std::string body;
        if (!read_file_to_string(STATIC_ADMIN_APPS_JS, body)) {
            res.status = 404;
            res.body = "Missing static file: " + STATIC_ADMIN_APPS_JS;
            return;
        }

        res.set_header("Cache-Control", "no-store");
        res.set_content(body, "application/javascript; charset=utf-8");
    });


    srv.Get("/admin/users", [&](const httplib::Request& req, httplib::Response& res) {
        std::string actor_fp;
        if (!require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, &actor_fp)) return;

        const std::string body = slurp_file(STATIC_USERS_HTML);
        if (body.empty()) { res.status = 404; res.set_content("missing admin_users.html","text/plain"); return; }
        res.set_header("Cache-Control", "no-store");
        res.set_content(body, "text/html; charset=utf-8");
    });

    srv.Get("/static/admin_users.js", [&](const httplib::Request&, httplib::Response& res) {
        const std::string body = slurp_file(STATIC_USERS_JS);
        if (body.empty()) { res.status = 404; res.set_content("missing admin_users.js","text/plain"); return; }
        res.set_header("Cache-Control", "no-store");
        res.set_content(body, "application/javascript; charset=utf-8");
    });

	srv.Get("/static/theme.css", [&](const httplib::Request&, httplib::Response& res) {
    std::string body;
    if (!read_file_to_string(STATIC_THEME_CSS, body)) {
        res.status = 404;
        res.body = "Missing static file: " + STATIC_THEME_CSS;
        return;
    }
    res.set_header("Cache-Control", "no-store");
    res.set_content(body, "text/css; charset=utf-8");
	});

	srv.Get("/static/theme.js", [&](const httplib::Request&, httplib::Response& res) {
    std::string body;
    if (!read_file_to_string(STATIC_THEME_JS, body)) {
        res.status = 404;
        res.body = "Missing static file: " + STATIC_THEME_JS;
        return;
    }
    res.set_header("Cache-Control", "no-store");
    res.set_content(body, "application/javascript; charset=utf-8");
	});


// ============================================================================
//  ADMIN STATS / BADGES ROUTES
// ============================================================================

    // ---- Admin badges / stats routes ----
    // Transitional bulk split: route/helper block lives in routes_admin_stats_badges.inc.

#include "routes/routes_admin_stats_badges.inc"

	srv.Get(R"(/static/(.+))", [&](const httplib::Request& req, httplib::Response& res) {
    	// req.matches[1] is the captured path after /static/
    	if (req.matches.size() < 2) {
        	res.status = 400;
	        res.set_header("Content-Type", "text/plain");
    	    res.body = "Bad static request";
        	return;
	    }

    	const std::string rel = req.matches[1].str();

	    if (!is_safe_static_relpath(rel)) {
    	    res.status = 403;
        	res.set_header("Content-Type", "text/plain");
	        res.body = "Forbidden";
    	    return;
    	}

    	const std::filesystem::path base = std::filesystem::path(static_root_dir());
    	const std::filesystem::path full = base / rel;

	    // Fail-closed: only serve known safe extensions
    	if (!has_allowed_static_ext(full)) {
        	res.status = 404;
	        res.set_header("Content-Type", "text/plain");
    	    res.body = "Not found";
        	return;
	    }

	    std::string ext = full.extension().string();
    	std::transform(
        	ext.begin(),
	        ext.end(),
    	    ext.begin(),
        	[](unsigned char c) { return (char)std::tolower(c); }
	    );

    	const std::string ct = mime_for_ext(ext);

	    // Hardened static serving (headers + cache control handled inside)
    	// Set to true if you want /static to be completely no-cache.
	    const bool no_store = false;

	    if (!serve_static_file(req, res, full.string(), ct, no_store)) {
    	    // serve_static_file already set status/body
        	return;
	    }
	});


    auto now_iso_utc = []() -> std::string {
        using namespace std::chrono;
        auto now = system_clock::now();
        auto ms  = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

        std::time_t t = system_clock::to_time_t(now);
        std::tm tm{};
        gmtime_r(&t, &tm);

        char buf[64];
        std::snprintf(buf, sizeof(buf),
                      "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                      tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                      tm.tm_hour, tm.tm_min, tm.tm_sec,
                      (int)ms.count());
        return std::string(buf);
    };

    // Security: prevent admin lockout when disabling/revoking admins.
    // Returns true only when the target is currently the sole enabled admin.
    auto admin_would_remove_last_enabled_admin = [&](const std::string& target_fp_hex) -> bool {
        auto target = users.get(target_fp_hex);
        if (!target.has_value()) {
            return false;
        }

        if (target->role != "admin" || target->status != "enabled") {
            return false;
        }

        const auto snap = users.snapshot();
        for (const auto& kv : snap) {
            const auto& u = kv.second;
            if (u.fingerprint == target_fp_hex) {
                continue;
            }
            if (u.role == "admin" && u.status == "enabled") {
                return false;
            }
        }

        return true;
    };


    auto trim_ascii_for_opaque_enrollments = [](const std::string& in) -> std::string {
        std::size_t a = 0;
        while (a < in.size() && std::isspace(static_cast<unsigned char>(in[a]))) ++a;

        std::size_t b = in.size();
        while (b > a && std::isspace(static_cast<unsigned char>(in[b - 1]))) --b;

        return in.substr(a, b - a);
    };

    auto opaque_enrollments_path_for_admin_status = [&]() -> std::string {
        const char* raw = std::getenv("PQNAS_OPAQUE_ENROLLMENTS_PATH");
        const std::string env_path = trim_ascii_for_opaque_enrollments(raw ? raw : "");
        if (!env_path.empty()) return env_path;

        if (!users_path.empty()) {
            std::filesystem::path p(users_path);
            return (p.parent_path() / "opaque_enrollments.json").string();
        }

        return "/var/lib/pqnas/opaque_enrollments.json";
    };

    class AdminStatusOpaqueEnrollmentsFileLock {
    public:
        AdminStatusOpaqueEnrollmentsFileLock(const std::string& enrollments_path, std::string* err) {
            if (err) err->clear();

            std::error_code ec;
            const std::filesystem::path target(enrollments_path);
            const std::filesystem::path parent = target.parent_path();
            if (!parent.empty()) {
                std::filesystem::create_directories(parent, ec);
                if (ec) {
                    if (err) *err = "create_lock_parent_failed: " + ec.message();
                    return;
                }
            }

            const std::filesystem::path lock_path = target.string() + ".lock";

#ifdef O_CLOEXEC
            const int flags = O_CREAT | O_RDWR | O_CLOEXEC;
#else
            const int flags = O_CREAT | O_RDWR;
#endif

            fd_ = ::open(lock_path.c_str(), flags, 0600);
            if (fd_ < 0) {
                if (err) *err = std::string("open_lock_failed: ") + std::strerror(errno);
                return;
            }

            if (::flock(fd_, LOCK_EX) != 0) {
                if (err) *err = std::string("flock_failed: ") + std::strerror(errno);
                ::close(fd_);
                fd_ = -1;
                return;
            }

            ok_ = true;
        }

        AdminStatusOpaqueEnrollmentsFileLock(const AdminStatusOpaqueEnrollmentsFileLock&) = delete;
        AdminStatusOpaqueEnrollmentsFileLock& operator=(const AdminStatusOpaqueEnrollmentsFileLock&) = delete;

        ~AdminStatusOpaqueEnrollmentsFileLock() {
            if (fd_ >= 0) {
                (void)::flock(fd_, LOCK_UN);
                (void)::close(fd_);
            }
        }

        bool ok() const { return ok_; }

    private:
        int fd_ = -1;
        bool ok_ = false;
    };


    auto load_opaque_enrollments_for_admin_status = [](const std::string& path, std::string* err) -> json {
        if (err) err->clear();

        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            return json{{"version", 1}, {"tokens", json::array()}};
        }

        std::ifstream in(path);
        if (!in) {
            if (err) *err = "open_failed";
            return json{};
        }

        try {
            json doc = json::parse(in);
            if (!doc.is_object()) {
                if (err) *err = "json_not_object";
                return json{};
            }
            if (!doc.contains("tokens") || !doc["tokens"].is_array()) {
                doc["tokens"] = json::array();
            }
            if (!doc.contains("version")) {
                doc["version"] = 1;
            }
            return doc;
        } catch (const std::exception& e) {
            if (err) *err = std::string("json_parse_failed: ") + e.what();
            return json{};
        }
    };

    auto save_opaque_enrollments_for_admin_status = [](const std::string& path, const json& doc, std::string* err) -> bool {
        if (err) err->clear();

        std::error_code ec;
        const std::filesystem::path target(path);
        std::filesystem::create_directories(target.parent_path(), ec);
        if (ec) {
            if (err) *err = "create_directories_failed: " + ec.message();
            return false;
        }

        const std::filesystem::path tmp =
            target.string() +
            ".tmp." +
            std::to_string(static_cast<long long>(::getpid())) +
            "." +
            std::to_string(static_cast<long long>(
                std::chrono::steady_clock::now().time_since_epoch().count()));

        {
            std::ofstream out(tmp, std::ios::trunc);
            if (!out) {
                if (err) *err = "open_tmp_for_write_failed";
                return false;
            }

            out << doc.dump(2) << "\n";
            out.flush();
            out.close();

            if (!out) {
                std::error_code rm_ec;
                std::filesystem::remove(tmp, rm_ec);
                if (err) *err = "write_tmp_failed";
                return false;
            }
        }

        std::filesystem::rename(tmp, target, ec);
        if (ec) {
            std::error_code rm_ec;
            std::filesystem::remove(tmp, rm_ec);
            if (err) *err = "atomic_rename_failed: " + ec.message();
            return false;
        }

        return true;
    };

    auto invalidate_opaque_enrollment_tokens_for_fingerprint_on_status_revoke =
        [&](const std::string& fp, std::size_t* invalidated, std::string* err) -> bool {
            if (invalidated) *invalidated = 0;
            if (err) err->clear();

            if (fp.empty()) {
                if (err) *err = "missing_fingerprint";
                return false;
            }

            const std::string path = opaque_enrollments_path_for_admin_status();

            std::string opaque_enrollments_file_lock_err;
            AdminStatusOpaqueEnrollmentsFileLock opaque_enrollments_file_lock(
                path,
                &opaque_enrollments_file_lock_err);
            if (!opaque_enrollments_file_lock.ok()) {
                if (err) *err = "opaque_enrollments_lock_failed: " + opaque_enrollments_file_lock_err;
                return false;
            }

            const long now = static_cast<long>(std::time(nullptr));

            std::string lerr;
            json doc = load_opaque_enrollments_for_admin_status(path, &lerr);
            if (!lerr.empty()) {
                if (err) *err = "opaque_enrollments_load_failed: " + lerr;
                return false;
            }

            if (!doc.contains("tokens") || !doc["tokens"].is_array()) {
                doc["tokens"] = json::array();
            }

            std::size_t changed = 0;

            for (auto& rec : doc["tokens"]) {
                if (!rec.is_object()) continue;
                if (rec.value("fingerprint", "") != fp) continue;

                const long used_at = rec.value("used_at", 0L);
                const long expires_at = rec.value("expires_at", 0L);
                if (used_at > 0 || expires_at <= now) continue;

                rec["used_at"] = now;
                rec["invalidated_at"] = now;
                rec["invalidated_reason"] = "user_revoked";
                ++changed;
            }

            if (changed > 0) {
                std::string serr;
                if (!save_opaque_enrollments_for_admin_status(path, doc, &serr)) {
                    if (err) *err = "opaque_enrollments_save_failed: " + serr;
                    return false;
                }
            }

            if (invalidated) *invalidated = changed;
            return true;
        };


    // ---- Admin user status routes ----
    {
        AdminUserStatusRoutesContext admin_user_status_ctx;
        admin_user_status_ctx.users = &users;
        admin_user_status_ctx.users_path = users_path;

        admin_user_status_ctx.require_admin_auth =
            [&](const httplib::Request& req, httplib::Response& res, std::string* actor_fp) {
                return require_admin_auth_users_actor(req, res, COOKIE_KEY, users_path, &users, actor_fp);
            };

        admin_user_status_ctx.require_same_origin =
            [&](const httplib::Request& req, httplib::Response& res) {
                return require_same_origin_for_cookie_mutation(req, res);
            };

        admin_user_status_ctx.reply_json =
            [](httplib::Response& res, int status, const std::string& body) {
                reply_json(res, status, body);
            };

        admin_user_status_ctx.admin_would_remove_last_enabled_admin =
            [&](const std::string& fp) {
                return admin_would_remove_last_enabled_admin(fp);
            };

        admin_user_status_ctx.invalidate_opaque_enrollment_tokens_for_revoke =
            [&](const std::string& fp, std::size_t* invalidated, std::string* err) {
                return invalidate_opaque_enrollment_tokens_for_fingerprint_on_status_revoke(
                    fp,
                    invalidated,
                    err
                );
            };

        admin_user_status_ctx.revoke_devices_for_fingerprint =
            [&](const std::string& fp) {
                std::string token_revoke_err;
                (void)g_app_tokens.revoke_devices_for_fingerprint(fp, &token_revoke_err);
            };

        admin_user_status_ctx.now_iso_utc =
            [&]() {
                return now_iso_utc();
            };

        admin_user_status_ctx.audit_append =
            [&](const pqnas::AuditEvent& ev) {
                audit_append(ev);
            };

        register_admin_user_status_routes(srv, admin_user_status_ctx);
    }


    // ---- Admin user storage allocation routes ----
    {
        AdminUserStorageRoutesContext admin_user_storage_ctx;
        admin_user_storage_ctx.users = &users;
        admin_user_storage_ctx.workspaces = &workspaces;
        admin_user_storage_ctx.users_path = users_path;
        admin_user_storage_ctx.workspaces_path = workspaces_path;

        admin_user_storage_ctx.require_admin_auth =
            [&](const httplib::Request& req, httplib::Response& res, std::string* actor_fp) {
                return require_admin_auth_users_actor(req, res, COOKIE_KEY, users_path, &users, actor_fp);
            };

        admin_user_storage_ctx.require_same_origin =
            [&](const httplib::Request& req, httplib::Response& res) {
                return require_same_origin_for_cookie_mutation(req, res);
            };

        admin_user_storage_ctx.reply_json =
            [](httplib::Response& res, int status, const std::string& body) {
                reply_json(res, status, body);
            };

        admin_user_storage_ctx.trim_copy =
            [](const std::string& v) {
                return trim_copy(v);
            };

        admin_user_storage_ctx.is_valid_fingerprint_hex =
            [](const std::string& fp) {
                return is_valid_fingerprint_hex(fp);
            };

        admin_user_storage_ctx.normalize_storage_pool_id =
            [](const std::string& pool_id) {
                return normalize_storage_pool_id(pool_id);
            };

        admin_user_storage_ctx.storage_pool_mount_by_id =
            [&](const std::string& pool_id, std::string* mount, std::string* err) {
                return storage_pool_mount_by_id_adminonly(users_path, pool_id, mount, err);
            };

        admin_user_storage_ctx.data_root_dir =
            []() {
                return pqnas::data_root_dir();
            };

        admin_user_storage_ctx.default_root_rel_for_fp =
            [](const std::string& fp) {
                return default_root_rel_for_fp(fp);
            };

        admin_user_storage_ctx.is_safe_rel_path =
            [](const std::string& rel) {
                return is_safe_rel_path(rel);
            };

        admin_user_storage_ctx.statvfs_path =
            [](const std::string& path, std::uint64_t* total, std::uint64_t* free) {
                return statvfs_path(path, total, free);
            };

        admin_user_storage_ctx.dir_size_bytes_best_effort =
            [](const std::filesystem::path& path) -> std::uint64_t {
                return static_cast<std::uint64_t>(dir_size_bytes_best_effort(path));
            };

        admin_user_storage_ctx.ensure_dir_exists =
            [](const std::filesystem::path& path, std::string* err) {
                return ensure_dir_exists(path, err);
            };

        admin_user_storage_ctx.sum_allocated_workspace_quota_on_pool =
            [&](const std::string& pool_id, const std::string& exclude_workspace_id) -> std::uint64_t {
                return pqnas::sum_allocated_workspace_quota_on_pool(
                    workspaces,
                    pool_id,
                    exclude_workspace_id
                );
            };

        admin_user_storage_ctx.now_iso_utc =
            [&]() {
                return now_iso_utc();
            };

        admin_user_storage_ctx.audit_append =
            [&](const pqnas::AuditEvent& ev) {
                audit_append(ev);
            };

        register_admin_user_storage_routes(srv, admin_user_storage_ctx);
    }


    // ---- Admin user storage preview routes ----
    {
        AdminUserStoragePreviewRoutesContext admin_user_storage_preview_ctx;
        admin_user_storage_preview_ctx.users = &users;
        admin_user_storage_preview_ctx.workspaces = &workspaces;
        admin_user_storage_preview_ctx.users_path = users_path;
        admin_user_storage_preview_ctx.workspaces_path = workspaces_path;

        admin_user_storage_preview_ctx.require_admin_cookie =
            [&](const httplib::Request& req, httplib::Response& res, std::string* actor_fp) {
                return require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, actor_fp);
            };

        admin_user_storage_preview_ctx.reply_json =
            [](httplib::Response& res, int status, const std::string& body) {
                reply_json(res, status, body);
            };

        admin_user_storage_preview_ctx.trim_copy =
            [](const std::string& v) {
                return trim_copy(v);
            };

        admin_user_storage_preview_ctx.is_valid_fingerprint_hex =
            [](const std::string& fp) {
                return is_valid_fingerprint_hex(fp);
            };

        admin_user_storage_preview_ctx.normalize_storage_pool_id =
            [](const std::string& pool_id) {
                return normalize_storage_pool_id(pool_id);
            };

        admin_user_storage_preview_ctx.storage_pool_mount_by_id =
            [&](const std::string& pool_id, std::string* mount, std::string* err) {
                return storage_pool_mount_by_id_adminonly(users_path, pool_id, mount, err);
            };

        admin_user_storage_preview_ctx.data_root_dir =
            []() {
                return pqnas::data_root_dir();
            };

        admin_user_storage_preview_ctx.default_root_rel_for_fp =
            [](const std::string& fp) {
                return default_root_rel_for_fp(fp);
            };

        admin_user_storage_preview_ctx.is_safe_rel_path =
            [](const std::string& rel) {
                return is_safe_rel_path(rel);
            };

        admin_user_storage_preview_ctx.statvfs_path =
            [](const std::string& path, std::uint64_t* total, std::uint64_t* free) {
                return statvfs_path(path, total, free);
            };

        admin_user_storage_preview_ctx.dir_size_bytes_best_effort =
            [](const std::filesystem::path& path) -> std::uint64_t {
                return static_cast<std::uint64_t>(dir_size_bytes_best_effort(path));
            };

        admin_user_storage_preview_ctx.sum_allocated_workspace_quota_on_pool =
            [&](const std::string& pool_id, const std::string& exclude_workspace_id) -> std::uint64_t {
                return pqnas::sum_allocated_workspace_quota_on_pool(
                    workspaces,
                    pool_id,
                    exclude_workspace_id
                );
            };

        register_admin_user_storage_preview_routes(srv, admin_user_storage_preview_ctx);
    }


    // GET /system (static UI) - visible to user + admin (cookie required)
    srv.Get("/system", [&](const httplib::Request& req, httplib::Response& res) {
        std::string actor_fp, role;
        if (!require_user_cookie_users_actor(req, res, COOKIE_KEY, &users, &actor_fp, &role)) return;

        std::string body;
        if (!read_file_to_string(STATIC_SYSTEM_HTML, body) || body.empty()) {
            res.status = 500;
            res.set_header("Content-Type", "text/plain");
            res.body = "Missing static file: " + STATIC_SYSTEM_HTML;
            return;
        }

        res.status = 200;
        res.set_header("Content-Type", "text/html; charset=utf-8");
        res.set_header("Cache-Control", "no-store");
        res.body = body;
    });

// GET /api/v4/system/storage  (used by /system page)
srv.Get("/api/v4/system/storage", [&](const httplib::Request& req, httplib::Response& res) {
    std::string actor_fp, role;
    if (!require_user_cookie_users_actor(req, res, COOKIE_KEY, &users, &actor_fp, &role)) return;

    const std::string data_root = pqnas::data_root_dir();

    pqnas::StorageInfo si;
    std::string err;
    pqnas::get_storage_info(data_root, &si, &err);

    json out;
    out["ok"] = true;

    out["root"] = si.root;
    out["fstype"] = si.fstype;
    out["mountpoint"] = si.mountpoint;
    out["source"] = si.source;
    out["options"] = si.options;
    out["prjquota_enabled"] = si.prjquota_enabled;

    out["note"] = si.prjquota_enabled
        ? "Project quotas appear enabled (prjquota/pquota)."
        : "Project quotas not detected in mount options.";

    if (!err.empty())
        out["warning"] = err;

    reply_json(res, 200, out.dump());
});

srv.Get("/api/v4/system/drives", [&](const httplib::Request& req, httplib::Response& res) {
    std::string actor_fp, role;
    if (!require_user_cookie_users_actor(req, res, COOKIE_KEY, &users, &actor_fp, &role)) return;

    auto snap = pqnas::drive_health_monitor_snapshot();

    bool should_refresh = !snap.ready;
    if (!should_refresh) {
        for (const auto& d : snap.drives) {
            if (d.selftest_status == "running") {
                should_refresh = true;
                break;
            }
        }
    }

    if (should_refresh) {
        std::string err;
        pqnas::drive_health_monitor_refresh_now(&err);
        snap = pqnas::drive_health_monitor_snapshot();
    }

    json arr = json::array();
    for (const auto& d : snap.drives) {
        json j;
        j["name"] = d.name;
        j["dev"] = d.dev;
        j["kind"] = d.kind;
        j["transport"] = d.transport;
        j["rota"] = d.rota;

        j["model"] = d.model;
        j["serial"] = d.serial;
        j["firmware"] = d.firmware;
        j["disk_id"] = d.disk_id;
        j["by_id"] = d.by_id;
        j["by_path"] = d.by_path;
                auto pqnas_locate_tool_exists = [](const char* p) -> bool {
                    return p && ::access(p, X_OK) == 0;
                };

                const bool pqnas_ledctl_available =
                    pqnas_locate_tool_exists("/usr/sbin/ledctl") ||
                    pqnas_locate_tool_exists("/usr/bin/ledctl") ||
                    pqnas_locate_tool_exists("/sbin/ledctl") ||
                    pqnas_locate_tool_exists("/bin/ledctl");

                const bool pqnas_sg_ses_available =
                    pqnas_locate_tool_exists("/usr/bin/sg_ses") ||
                    pqnas_locate_tool_exists("/usr/sbin/sg_ses") ||
                    pqnas_locate_tool_exists("/bin/sg_ses") ||
                    pqnas_locate_tool_exists("/sbin/sg_ses");

                json pqnas_locate_methods = json::array();
                if (pqnas_ledctl_available) pqnas_locate_methods.push_back("ledctl");
                if (pqnas_sg_ses_available) pqnas_locate_methods.push_back("sg_ses");

                j["locate_supported"] = false; // no start/stop endpoint is enabled yet
                j["locate_ready"] = !pqnas_locate_methods.empty();
                j["locate_method"] = "";
                j["locate_methods_available"] = pqnas_locate_methods;
                j["physical_hint"] = pqnas_locate_methods.empty()
                    ? "Use serial/by-id label"
                    : "Locate tools detected, but blink action is not enabled yet";
        j["size_bytes"] = d.size_bytes;

        j["smart_available"] = d.smart_available;
        j["smart_enabled"] = d.smart_enabled;

        j["health_status"] = d.health_status;
        j["health_text"] = d.health_text;

        if (d.temperature_c >= 0) j["temperature_c"] = d.temperature_c;
        if (d.power_on_hours >= 0) j["power_on_hours"] = d.power_on_hours;

        if (d.percentage_used >= 0) j["percentage_used"] = d.percentage_used;
        if (d.available_spare >= 0) j["available_spare"] = d.available_spare;
        if (d.available_spare_threshold >= 0) j["available_spare_threshold"] = d.available_spare_threshold;
        if (d.media_errors >= 0) j["media_errors"] = d.media_errors;
        if (d.unsafe_shutdowns >= 0) j["unsafe_shutdowns"] = d.unsafe_shutdowns;
        if (d.num_err_log_entries >= 0) j["num_err_log_entries"] = d.num_err_log_entries;
        if (d.data_units_read >= 0) j["data_units_read"] = d.data_units_read;
        if (d.data_units_written >= 0) j["data_units_written"] = d.data_units_written;
        if (d.host_reads >= 0) j["host_reads"] = d.host_reads;
        if (d.host_writes >= 0) j["host_writes"] = d.host_writes;
        if (d.reallocated_sectors >= 0) j["reallocated_sectors"] = d.reallocated_sectors;
        if (d.current_pending_sectors >= 0) j["current_pending_sectors"] = d.current_pending_sectors;
        if (d.offline_uncorrectable >= 0) j["offline_uncorrectable"] = d.offline_uncorrectable;
        if (d.reported_uncorrect >= 0) j["reported_uncorrect"] = d.reported_uncorrect;
        if (d.udma_crc_errors >= 0) j["udma_crc_errors"] = d.udma_crc_errors;

        j["selftest_supported"] = d.selftest_supported;
        j["selftest_status"] = d.selftest_status;
        j["selftest_text"] = d.selftest_text;
        if (d.selftest_progress_pct >= 0) j["selftest_progress_pct"] = d.selftest_progress_pct;

        j["warning"] = d.warning;

        json msgs = json::array();
        for (const auto& m : d.messages) msgs.push_back(m);
        j["messages"] = std::move(msgs);

        arr.push_back(std::move(j));
    }

    json out;
    out["ok"] = true;
    out["updated_iso"] = snap.updated_iso;
    out["drives"] = std::move(arr);
    if (!snap.last_error.empty()) out["warning"] = snap.last_error;

    reply_json(res, 200, out.dump());
});

// drive-health-refresh-now: backend: force a fresh SMART/NVMe probe from the System page.
srv.Post("/api/v4/system/drives/refresh-now", [](const auto& /*req*/, auto& res) {
    nlohmann::json out = nlohmann::json::object();

    std::string err;
    const bool ok = pqnas::drive_health_monitor_refresh_now(&err);
    const auto snap = pqnas::drive_health_monitor_snapshot();

    out["ok"] = ok;
    out["ready"] = snap.ready;
    out["updated_iso"] = snap.updated_iso;
    out["last_error"] = snap.last_error;

    if (!ok) {
        res.status = 500;
        out["message"] = err.empty() ? "drive health refresh failed" : err;
    }

    res.set_content(out.dump(), "application/json");
});

srv.Post("/api/v4/system/drives/selftest/start", [&](const httplib::Request& req, httplib::Response& res) {
    std::string actor_fp, role;
    if (!require_user_cookie_users_actor(req, res, COOKIE_KEY, &users, &actor_fp, &role)) return;

    if (role != "admin") {
        reply_json(res, 403, R"({"ok":false,"error":"forbidden","message":"admin required"})");
        return;
    }

    json in;
    try {
        in = json::parse(req.body.empty() ? "{}" : req.body);
    } catch (...) {
        reply_json(res, 400, R"({"ok":false,"error":"bad_json","message":"invalid JSON body"})");
        return;
    }

    const std::string dev  = in.value("dev", "");
    const std::string type = in.value("type", "short");

    std::string err;
    if (!pqnas::start_drive_selftest(dev, type, &err)) {
        json out;
        out["ok"] = false;
        out["error"] = "selftest_start_failed";
        out["message"] = err.empty() ? "failed to start self-test" : err;
        reply_json(res, 400, out.dump());
        return;
    }

    // Refresh monitor snapshot so the UI can show the new state sooner.
    std::string refresh_err;
    pqnas::drive_health_monitor_refresh_now(&refresh_err);

    json out;
    out["ok"] = true;
    out["dev"] = dev;
    out["type"] = type;
    out["message"] = "self-test started";
    if (!refresh_err.empty()) out["warning"] = refresh_err;
    reply_json(res, 200, out.dump());
});


    // ---- Chunked Upload API (user storage, My Files) ----
    // ---- Chunked Upload API (user storage, My Files) ----
    {
        ChunkedUploadRoutesContext uploads_ctx;
        uploads_ctx.users = &users;
        uploads_ctx.file_versions = &file_versions_index;
        uploads_ctx.users_path = users_path;
        uploads_ctx.cookie_key = COOKIE_KEY;

        uploads_ctx.require_user_auth =
            [&](const httplib::Request& req,
                httplib::Response& res,
                std::string* fp_hex,
                std::string* role) {
                return require_user_auth_users_actor(req, res, COOKIE_KEY, &users, fp_hex, role);
            };

        uploads_ctx.require_same_origin =
            [&](const httplib::Request& req, httplib::Response& res) {
                return require_same_origin_for_cookie_mutation(req, res);
            };

        uploads_ctx.require_no_live_lock_for_write =
            [&](httplib::Response& res,
                const std::string& fp_hex,
                const std::string& rel_norm,
                const std::string& action,
                bool allow_when_locked) {
                return require_user_no_live_lock_for_write_local(
                    users,
                    users_path,
                    res,
                    fp_hex,
                    rel_norm,
                    action,
                    allow_when_locked
                );
            };

        uploads_ctx.reply_json =
            [&](httplib::Response& res, int status, const std::string& body) {
                reply_json(res, status, body);
            };

        uploads_ctx.reply_quota_error =
            [&](httplib::Response& res,
                const std::string& fp_hex,
                const pqnas::QuotaCheckResult& qc) {
                return reply_quota_error_v1(res, fp_hex, qc);
            };

        uploads_ctx.user_dir_for_fp =
            [&](const std::string& fp_hex) {
                return user_dir_for_fp(users, fp_hex);
            };

        uploads_ctx.random_b64url =
            [&](std::size_t n) {
                return random_b64url(n);
            };

        uploads_ctx.now_epoch_sec =
            [&]() -> std::int64_t {
                return static_cast<std::int64_t>(now_epoch_sec());
            };

        uploads_ctx.audit_append =
            [&](const pqnas::AuditEvent& ev) {
                audit_append(ev);
            };

        uploads_ctx.upload_tiering_config =
            [&]() {
                const auto cfg = upload_tiering_config();

                ChunkedUploadTieringConfig out;
                out.enabled = cfg.enabled;
                out.landing_pool_id = cfg.landing_pool_id;
                return out;
            };

        uploads_ctx.build_landing_abs_path =
            [&](const std::string& pool_id,
                const std::string& fp_hex,
                const std::string& rel_norm,
                std::filesystem::path* out_abs,
                std::string* err) {
                return build_landing_abs_path(pool_id, fp_hex, rel_norm, out_abs, err);
            };

        uploads_ctx.ensure_no_symlink_in_existing_path_prefix =
            [&](const std::filesystem::path& path, std::string* err) {
                return ensure_no_symlink_in_existing_path_prefix(path, err);
            };

        uploads_ctx.record_user_file_activity =
            [&](const std::string& fp_hex,
                const std::string& event_name,
                const std::string& logical_rel_path,
                const std::string& item_type,
                const std::string& aux,
                std::uint64_t bytes,
                int count,
                const httplib::Request* req) {
                record_user_file_activity_best_effort_local(
                    users,
                    fp_hex,
                    event_name,
                    logical_rel_path,
                    item_type,
                    aux,
                    bytes,
                    count,
                    req
                );
            };

        register_chunked_upload_routes(srv, uploads_ctx);
    }


// ============================================================================
//  FILES / GALLERY / SHARES ROUTE INCLUDES
// ============================================================================

    // ---- Files core routes ----
    // Transitional bulk split: route/helper block lives in routes_files_core.inc.

#include "routes/routes_files_core.inc"

    // ---- Gallery / ReelStack routes ----
    // Transitional bulk split: route/helper block lives in routes_gallery_reelstack.inc.
#include "routes/routes_gallery_reelstack.inc"


    // ---- Files PUT upload route ----
    // Transitional bulk split: route/helper block lives in routes_files_put.inc.
#include "routes/routes_files_put.inc"


    // ---- Shares / public share / gallery export routes ----
    // Transitional bulk split: route/helper block lives in routes_shares_public.inc.
#include "routes/routes_shares_public.inc"


    // ************************* END OF ROUTES *************************** //


    constexpr std::size_t k_trash_cleanup_batch_limit = 32;
    constexpr int k_trash_cleanup_interval_seconds = 300;

    auto run_trash_cleanup_pass = [&]() {
        const std::int64_t now_ts = now_epoch_sec();

        std::string lerr;
        const auto rows = trash_index.list_expired(now_ts, k_trash_cleanup_batch_limit, &lerr);
        if (!lerr.empty()) {
            std::cerr << "[trash] cleanup list_expired failed: " << lerr << std::endl;
            return;
        }

        for (const auto& rec : rows) {
            pqnas::TrashService::PurgeParams pp;
            pp.trash_id = rec.trash_id;

            pqnas::TrashService::PurgeResult pr;
            std::string perr;
            if (!trash_service.purge_from_trash(pp, &pr, &perr)) {
                // Benign race: another actor already restored/purged/claimed it.
                if (perr == "trash item is not active") {
                    continue;
                }

                std::cerr << "[trash] auto purge failed"
                          << " trash_id=" << rec.trash_id
                          << " scope_type=" << rec.scope_type
                          << " scope_id=" << rec.scope_id
                          << " detail=" << perr
                          << std::endl;

                pqnas::AuditEvent ev;
                ev.event = "trash.auto_purge";
                ev.outcome = "fail";
                ev.f["reason"] = "retention_expired";
                ev.f["trash_id"] = rec.trash_id;
                ev.f["scope_type"] = rec.scope_type;
                ev.f["scope_id"] = rec.scope_id;
                ev.f["item_type"] = rec.item_type;
                ev.f["original_rel_path"] = pqnas::shorten(rec.original_rel_path, 240);
                ev.f["origin_app"] = rec.origin_app;
                ev.f["source_pool"] = rec.source_pool;
                ev.f["source_tier_state"] = rec.source_tier_state;
                ev.f["size_bytes"] = std::to_string(rec.size_bytes);
                ev.f["file_count"] = std::to_string(rec.file_count);
                ev.f["deleted_epoch"] = std::to_string(rec.deleted_epoch);
                ev.f["purge_after_epoch"] = std::to_string(rec.purge_after_epoch);
                ev.f["detail"] = pqnas::shorten(perr, 240);
                audit_append(ev);
                continue;
            }

            pqnas::AuditEvent ev;
            ev.event = "trash.auto_purge";
            ev.outcome = "ok";
            ev.f["reason"] = "retention_expired";
            ev.f["trash_id"] = rec.trash_id;
            ev.f["scope_type"] = rec.scope_type;
            ev.f["scope_id"] = rec.scope_id;
            ev.f["item_type"] = rec.item_type;
            ev.f["original_rel_path"] = pqnas::shorten(rec.original_rel_path, 240);
            ev.f["origin_app"] = rec.origin_app;
            ev.f["source_pool"] = rec.source_pool;
            ev.f["source_tier_state"] = rec.source_tier_state;
            ev.f["size_bytes"] = std::to_string(pr.size_bytes);
            ev.f["file_count"] = std::to_string(pr.file_count);
            ev.f["versions_deleted"] = std::to_string(pr.versions_deleted);
            ev.f["version_bytes_deleted"] = std::to_string(pr.version_bytes_deleted);
            ev.f["version_blobs_missing"] = std::to_string(pr.version_blobs_missing);
            if (!pr.version_cleanup_error.empty()) {
                ev.f["version_cleanup_error"] = pqnas::shorten(pr.version_cleanup_error, 240);
            }
            ev.f["deleted_epoch"] = std::to_string(rec.deleted_epoch);
            ev.f["purge_after_epoch"] = std::to_string(rec.purge_after_epoch);
            audit_append(ev);
        }
    };

	// ---- Start background workers ----

	std::atomic<bool> tiering_worker_stop{false};
	std::thread tiering_worker([&]() {
    	tiering_worker_loop(&users, &tiering_worker_stop);
	});

    // garbage service
    std::atomic<bool> trash_cleanup_stop{false};
    std::thread trash_cleanup_worker([&]() {
        std::cerr << "[trash] cleanup worker started"
                  << " interval_s=" << k_trash_cleanup_interval_seconds
                  << " batch_limit=" << k_trash_cleanup_batch_limit
                  << std::endl;

        while (!trash_cleanup_stop.load()) {
            run_trash_cleanup_pass();

            for (int i = 0; i < k_trash_cleanup_interval_seconds && !trash_cleanup_stop.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    });

	// ---- Start HTTP server ----
    {
        std::string mlkem_err;
        const bool mlkem_ok = pqnas::mlkem768_selftest_v1(&mlkem_err);
        std::cerr << "[pq/mlkem] backend=" << pqnas::mlkem768_backend_name_v1()
                  << " available=" << (pqnas::mlkem768_available_v1() ? "yes" : "no")
                  << " selftest=" << (mlkem_ok ? "ok" : "fail");
        if (!mlkem_ok && !mlkem_err.empty()) {
            std::cerr << " detail=" << mlkem_err;
        }
        std::cerr << std::endl;
    }
    {
        std::string env_err;
        const bool env_ok = pqnas::pq_open_envelope_mlkem768_selftest_v1(&env_err);
        std::cerr << "[pq/mlkem-envelope] selftest=" << (env_ok ? "ok" : "fail");
        if (!env_ok && !env_err.empty()) {
            std::cerr << " detail=" << env_err;
        }
        std::cerr << std::endl;
    }
	std::cerr << "PQ-NAS server listening on 0.0.0.0:" << LISTEN_PORT << std::endl;
	srv.listen("0.0.0.0", LISTEN_PORT);

	// ---- Shutdown sequence ----

	// Stop background drive health monitor
	pqnas::drive_health_monitor_stop();

	// Stop RAID / migration async worker (best-effort clean shutdown)
	user_mig_worker_stop_and_join();
	user_cleanup_worker_stop_and_join();
	// RAID worker stop/join is handled by routes_storage_raid.cpp.
    trash_cleanup_stop.store(true);
    if (trash_cleanup_worker.joinable()) trash_cleanup_worker.join();

    tiering_worker_stop.store(true);
    if (tiering_worker.joinable()) tiering_worker.join();

    admin_stats_sampler_stop.store(true);
    if (admin_stats_sampler_thread.joinable()) admin_stats_sampler_thread.join();

    snapshots_stop.store(true);
    if (snapshots_thread.joinable()) snapshots_thread.join();

	return 0;
}
