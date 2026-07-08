#include "backups/system_backup_worker.h"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <system_error>
#include <vector>

extern char **environ;

namespace pqnas::backups {
namespace {

std::mutex& backup_mutex() {
    static std::mutex m;
    return m;
}

std::string utc_stamp_compact() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);

    std::tm tm{};
    gmtime_r(&tt, &tm);

    std::ostringstream os;
    os << std::put_time(&tm, "%Y%m%d-%H%M%S");
    return os.str();
}

std::int64_t now_epoch_seconds() {
    return static_cast<std::int64_t>(std::time(nullptr));
}

std::string sanitize_token(std::string s) {
    if (s.empty()) return "manual";

    for (char& c : s) {
        const bool ok =
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '-' ||
            c == '_';

        if (!ok) c = '-';
    }

    while (!s.empty() && s.front() == '-') s.erase(s.begin());
    while (!s.empty() && s.back() == '-') s.pop_back();

    return s.empty() ? std::string("manual") : s;
}

bool is_allowed_tier(const std::string& tier) {
    return tier == "quarter-hourly" ||
           tier == "hourly" ||
           tier == "daily" ||
           tier == "weekly" ||
           tier == "manual";
}

std::uintmax_t file_size_or_zero(const std::filesystem::path& p) {
    std::error_code ec;
    const auto n = std::filesystem::file_size(p, ec);
    return ec ? 0 : n;
}

std::uintmax_t directory_size(const std::filesystem::path& root) {
    std::error_code ec;
    if (!std::filesystem::exists(root, ec)) return 0;

    std::uintmax_t total = 0;
    std::filesystem::recursive_directory_iterator it(
        root,
        std::filesystem::directory_options::skip_permission_denied,
        ec
    );
    std::filesystem::recursive_directory_iterator end;

    for (; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        total += file_size_or_zero(it->path());
    }

    return total;
}

bool copy_regular_file_safe(const std::filesystem::path& src,
                            const std::filesystem::path& dst,
                            std::string* err) {
    std::error_code ec;
    std::filesystem::create_directories(dst.parent_path(), ec);
    if (ec) {
        if (err) *err = "failed to create destination directory: " + ec.message();
        return false;
    }

    std::filesystem::copy_file(
        src,
        dst,
        std::filesystem::copy_options::overwrite_existing,
        ec
    );

    if (ec) {
        if (err) *err = "copy_file failed: " + ec.message();
        return false;
    }

    std::filesystem::permissions(
        dst,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace,
        ec
    );

    return true;
}

bool sqlite_backup_database(const std::filesystem::path& src,
                            const std::filesystem::path& dst,
                            std::string* err) {
    std::error_code ec;
    std::filesystem::create_directories(dst.parent_path(), ec);
    if (ec) {
        if (err) *err = "failed to create sqlite backup directory: " + ec.message();
        return false;
    }

    std::filesystem::remove(dst, ec);

    sqlite3* src_db = nullptr;
    sqlite3* dst_db = nullptr;

    if (sqlite3_open_v2(src.string().c_str(), &src_db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        if (err) *err = src_db ? sqlite3_errmsg(src_db) : "sqlite source open failed";
        if (src_db) sqlite3_close(src_db);
        return false;
    }

    sqlite3_busy_timeout(src_db, 10000);

    if (sqlite3_open_v2(dst.string().c_str(), &dst_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        if (err) *err = dst_db ? sqlite3_errmsg(dst_db) : "sqlite destination open failed";
        sqlite3_close(src_db);
        if (dst_db) sqlite3_close(dst_db);
        return false;
    }

    sqlite3_busy_timeout(dst_db, 10000);

    sqlite3_backup* backup = sqlite3_backup_init(dst_db, "main", src_db, "main");
    if (!backup) {
        if (err) *err = sqlite3_errmsg(dst_db);
        sqlite3_close(dst_db);
        sqlite3_close(src_db);
        return false;
    }

    int rc = SQLITE_OK;
    while (true) {
        rc = sqlite3_backup_step(backup, 256);
        if (rc == SQLITE_DONE) break;

        if (rc == SQLITE_OK || rc == SQLITE_BUSY || rc == SQLITE_LOCKED) {
            if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED) {
                sqlite3_sleep(50);
            }
            continue;
        }

        break;
    }

    const int finish_rc = sqlite3_backup_finish(backup);
    if (rc == SQLITE_DONE && finish_rc == SQLITE_OK) {
        rc = sqlite3_exec(dst_db, "PRAGMA integrity_check;", nullptr, nullptr, nullptr);
    }

    if (rc != SQLITE_OK && rc != SQLITE_DONE) {
        if (err) *err = sqlite3_errmsg(dst_db);
        sqlite3_close(dst_db);
        sqlite3_close(src_db);
        return false;
    }

    sqlite3_close(dst_db);
    sqlite3_close(src_db);
    return true;
}

nlohmann::json source_json(const SystemBackupSource& s,
                           const std::filesystem::path& dst,
                           const std::string& status,
                           std::uintmax_t size_bytes,
                           const std::string& message = "") {
    return {
        {"set_id", s.set_id},
        {"label", s.label},
        {"kind", s.kind == SystemBackupSourceKind::SQLiteDatabase ? "sqlite" : "file"},
        {"source_path", s.source_path.string()},
        {"backup_path", dst.string()},
        {"status", status},
        {"size_bytes", size_bytes},
        {"message", message}
    };
}


std::string trim_ascii_copy_local(std::string s) {
    auto is_ws = [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };

    std::size_t a = 0;
    while (a < s.size() && is_ws(static_cast<unsigned char>(s[a]))) ++a;

    std::size_t b = s.size();
    while (b > a && is_ws(static_cast<unsigned char>(s[b - 1]))) --b;

    return s.substr(a, b - a);
}

std::filesystem::path env_path_or_empty_local(const char* name) {
    if (!name || !*name) return {};

    const char* raw = std::getenv(name);
    if (!raw) return {};

    const std::string v = trim_ascii_copy_local(raw);
    return v.empty() ? std::filesystem::path{} : std::filesystem::path(v);
}

std::filesystem::path configured_path_local(const char* env_name,
                                            const std::vector<std::filesystem::path>& fallbacks) {
    const auto env_path = env_path_or_empty_local(env_name);
    if (!env_path.empty()) {
        return env_path;
    }

    std::filesystem::path first;
    for (const auto& p : fallbacks) {
        if (p.empty()) continue;
        if (first.empty()) first = p;

        std::error_code ec;
        if (std::filesystem::is_regular_file(p, ec)) {
            return p;
        }
    }

    return first;
}

std::filesystem::path notification_settings_path_for_backup_local() {
    // Security: notification settings contain secret-bearing values such as
    // Telegram bot tokens and SMTP passwords. Match notification_settings.cpp:
    // derive the path from the trusted config root and fixed filename instead
    // of honoring a per-file environment override.
    const auto config_dir = env_path_or_empty_local("PQNAS_CONFIG_DIR");
    if (!config_dir.empty()) {
        return config_dir / "notifications.json";
    }

    const auto config = env_path_or_empty_local("PQNAS_CONFIG");
    if (!config.empty()) {
        return config / "notifications.json";
    }

    return configured_path_local(
        nullptr,
        {"/etc/pqnas/notifications.json", "/srv/pqnas/config/notifications.json"}
    );
}

std::filesystem::path password_credentials_path_for_backup_local(const std::filesystem::path& users_path) {
    const auto env_path = env_path_or_empty_local("PQNAS_PASSWORD_CREDENTIALS_PATH");
    if (!env_path.empty()) {
        return env_path;
    }

    if (!users_path.empty() && !users_path.parent_path().empty()) {
        return users_path.parent_path() / "password_credentials.json";
    }

    return "/var/lib/pqnas/password_credentials.json";
}

std::string env_key_from_line_local(const std::string& line) {
    std::string s = trim_ascii_copy_local(line);
    if (s.rfind("export ", 0) == 0) {
        s = trim_ascii_copy_local(s.substr(7));
    }

    const std::size_t eq = s.find('=');
    if (eq == std::string::npos) return {};

    return trim_ascii_copy_local(s.substr(0, eq));
}

bool should_redact_env_key_local(const std::string& key) {
    if (key.empty()) return false;

    return key == "PQNAS_PASSWORD_BOOTSTRAP_TOKEN" ||
           key == "PQNAS_SERVER_SK_B64URL" ||
           key == "PQNAS_COOKIE_KEY_B64URL" ||
           key.find("PRIVATE_KEY") != std::string::npos ||
           key.find("_SECRET") != std::string::npos ||
           key.find("_TOKEN") != std::string::npos ||
           key.find("_PASSWORD") != std::string::npos ||
           key.find("PASSPHRASE") != std::string::npos;
}

void write_redacted_env_line_local(std::ofstream& out, const std::string& line) {
    const std::string key = env_key_from_line_local(line);
    if (should_redact_env_key_local(key)) {
        out << "# " << key << " redacted by PQ-NAS system backup\n";
    } else {
        out << line << "\n";
    }
}

bool copy_process_env_redacted_safe(const std::filesystem::path& dst,
                                    std::string* err) {
    std::error_code ec;
    std::filesystem::create_directories(dst.parent_path(), ec);
    if (ec) {
        if (err) *err = "failed to create destination directory: " + ec.message();
        return false;
    }

    std::ofstream out(dst, std::ios::trunc);
    if (!out.good()) {
        if (err) *err = "failed to open destination env file";
        return false;
    }

    out << "# /etc/pqnas/pqnas.env was not readable by the service user.\n";
    out << "# This redacted snapshot was reconstructed from the pqnas process environment.\n";

    if (environ) {
        for (char** e = environ; *e; ++e) {
            const std::string line(*e);
            if (line.rfind("PQNAS_", 0) != 0) continue;
            write_redacted_env_line_local(out, line);
        }
    }

    out.flush();
    if (!out.good()) {
        if (err) *err = "failed to write redacted process environment";
        return false;
    }

    std::filesystem::permissions(
        dst,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace,
        ec
    );

    return true;
}

bool copy_env_file_redacted_safe(const std::filesystem::path& src,
                                 const std::filesystem::path& dst,
                                 std::string* err) {
    std::error_code ec;
    std::filesystem::create_directories(dst.parent_path(), ec);
    if (ec) {
        if (err) *err = "failed to create destination directory: " + ec.message();
        return false;
    }

    std::ifstream in(src);
    if (!in.good()) {
        return copy_process_env_redacted_safe(dst, err);
    }

    std::ofstream out(dst, std::ios::trunc);
    if (!out.good()) {
        if (err) *err = "failed to open destination env file";
        return false;
    }

    std::string line;
    while (std::getline(in, line)) {
        write_redacted_env_line_local(out, line);
    }

    out.flush();
    if (!out.good()) {
        if (err) *err = "failed to write redacted env file";
        return false;
    }

    std::filesystem::permissions(
        dst,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace,
        ec
    );

    return true;
}

std::vector<SystemBackupSource> default_sources() {
    std::vector<SystemBackupSource> v;

    auto add_regular = [&](const std::string& set_id,
                           const std::string& label,
                           const std::filesystem::path& source_path,
                           const std::filesystem::path& backup_relative_path,
                           bool optional = true) {
        if (source_path.empty() || backup_relative_path.empty()) return;

        v.push_back({
            set_id,
            label,
            SystemBackupSourceKind::RegularFile,
            source_path,
            backup_relative_path,
            optional
        });
    };

    auto add_sqlite = [&](const std::string& set_id,
                          const std::string& label,
                          const std::filesystem::path& source_path,
                          const std::filesystem::path& backup_relative_path,
                          bool optional = true) {
        if (source_path.empty() || backup_relative_path.empty()) return;

        v.push_back({
            set_id,
            label,
            SystemBackupSourceKind::SQLiteDatabase,
            source_path,
            backup_relative_path,
            optional
        });
    };

    // Core system config. This is copied through a redacting path so temporary
    // bootstrap tokens or accidental secrets do not get preserved in backups.
    add_regular(
        "core",
        "PQ-NAS environment",
        "/etc/pqnas/pqnas.env",
        "core/etc/pqnas/pqnas.env"
    );

    // Current installer/runtime layout uses /etc/pqnas through env vars.
    // Keep /srv/pqnas fallbacks so older dev/prototype installs still back up.
    const std::filesystem::path admin_settings_path = configured_path_local(
        "PQNAS_ADMIN_SETTINGS_PATH",
        {"/etc/pqnas/admin_settings.json", "/srv/pqnas/config/admin_settings.json"}
    );

    const std::filesystem::path policy_path = configured_path_local(
        "PQNAS_POLICY_PATH",
        {"/etc/pqnas/policy.json", "/srv/pqnas/config/policy.json"}
    );

    const std::filesystem::path users_path = configured_path_local(
        "PQNAS_USERS_PATH",
        {"/etc/pqnas/users.json", "/srv/pqnas/config/users.json", "/srv/pqnas/users.json"}
    );

    const std::filesystem::path shares_path = configured_path_local(
        "PQNAS_SHARES_PATH",
        {"/etc/pqnas/shares.json", "/srv/pqnas/config/shares.json", "/srv/pqnas/shares.json"}
    );

    const std::filesystem::path pools_path = configured_path_local(
        "PQNAS_POOLS_PATH",
        {"/etc/pqnas/pools.json", "/srv/pqnas/config/pools.json"}
    );

    const std::filesystem::path app_auth_path = configured_path_local(
        "PQNAS_APP_AUTH_PATH",
        {"/etc/pqnas/app_auth.json", "/srv/pqnas/config/app_auth.json"}
    );

    const std::filesystem::path notifications_path =
        notification_settings_path_for_backup_local();

    const std::filesystem::path password_credentials_path =
        password_credentials_path_for_backup_local(users_path);

    const std::filesystem::path opaque_credentials_path = configured_path_local(
        "PQNAS_OPAQUE_CREDENTIALS_PATH",
        {"/etc/pqnas/opaque_credentials.json", "/srv/pqnas/config/opaque_credentials.json"}
    );

    const std::filesystem::path opaque_server_setup_path = configured_path_local(
        "PQNAS_OPAQUE_SERVER_SETUP_PATH",
        {"/etc/pqnas/opaque_server_setup.bin", "/srv/pqnas/config/opaque_server_setup.bin"}
    );

    add_regular("config", "Admin settings", admin_settings_path, "config/admin_settings.json");
    add_regular("config", "Policy", policy_path, "config/policy.json");
    add_regular("users_auth", "Users registry", users_path, "users/users.json");
    add_regular("users_auth", "Password credentials", password_credentials_path, "users/password_credentials.json");
    add_regular("users_auth", "OPAQUE credentials store", opaque_credentials_path, "users/opaque_credentials.json");
    add_regular("users_auth", "OPAQUE server setup", opaque_server_setup_path, "users/opaque_server_setup.bin");
    add_regular("shares", "Share registry", shares_path, "shares/shares.json");
    add_regular("storage", "Storage pools", pools_path, "storage/pools.json");
    add_regular("auth", "App auth store", app_auth_path, "auth/app_auth.json");
    add_regular("notifications", "Notification settings", notifications_path, "notifications/notifications.json");

    // Circle Stack
    add_sqlite(
        "circle_stack",
        "Circle Stack local database",
        "/srv/pqnas/circlestack.db",
        "circlestack/circlestack.db"
    );

    add_sqlite(
        "circle_stack",
        "Circle Stack federation outbox",
        "/srv/pqnas/config/circlestack_federation_outbox.sqlite3",
        "circlestack/circlestack_federation_outbox.sqlite3"
    );

    add_sqlite(
        "circle_stack",
        "Circle Stack federation inbox",
        "/srv/pqnas/config/circlestack_federation_inbox.sqlite3",
        "circlestack/circlestack_federation_inbox.sqlite3"
    );

    add_sqlite(
        "circle_stack",
        "Circle Stack federation remote feed",
        "/srv/pqnas/config/circlestack_federation_remote_feed.sqlite3",
        "circlestack/circlestack_federation_remote_feed.sqlite3"
    );

    return v;
}

std::vector<SystemBackupRetention> default_retention() {
    return {
        {"quarter-hourly", 24LL * 60LL * 60LL},
        {"hourly", 7LL * 24LL * 60LL * 60LL},
        {"daily", 30LL * 24LL * 60LL * 60LL},
        {"weekly", 12LL * 7LL * 24LL * 60LL * 60LL},
        {"manual", 0}
    };
}

std::int64_t next_quarter_epoch(std::int64_t now) {
    static constexpr std::int64_t kQuarter = 15LL * 60LL;
    return ((now / kQuarter) + 1LL) * kQuarter;
}

std::vector<std::string> scheduled_tiers_for_epoch(std::int64_t epoch) {
    std::vector<std::string> tiers;
    tiers.push_back("quarter-hourly");

    std::time_t tt = static_cast<std::time_t>(epoch);
    std::tm local_tm{};
    localtime_r(&tt, &local_tm);

    if (local_tm.tm_min == 0) {
        tiers.push_back("hourly");

        if (local_tm.tm_hour == 3) {
            tiers.push_back("daily");

            // localtime_r: Sunday = 0
            if (local_tm.tm_wday == 0) {
                tiers.push_back("weekly");
            }
        }
    }

    return tiers;
}

std::string join_errors(const std::vector<std::string>& errors) {
    std::ostringstream os;

    for (std::size_t i = 0; i < errors.size(); ++i) {
        if (i) os << "; ";
        os << errors[i];
    }

    return os.str();
}

void write_manifest(const std::filesystem::path& manifest_path,
                    const nlohmann::json& manifest) {
    std::error_code ec;
    std::filesystem::create_directories(manifest_path.parent_path(), ec);

    std::ofstream out(manifest_path, std::ios::trunc);
    out << manifest.dump(2) << "\n";
    out.flush();
}

std::vector<std::filesystem::path> tier_backup_dirs(const std::filesystem::path& tier_root) {
    std::vector<std::filesystem::path> dirs;
    std::error_code ec;

    if (!std::filesystem::is_directory(tier_root, ec)) return dirs;

    for (const auto& ent : std::filesystem::directory_iterator(tier_root, ec)) {
        if (ec) break;
        if (ent.is_directory(ec)) dirs.push_back(ent.path());
    }

    std::sort(dirs.begin(), dirs.end());
    return dirs;
}

} // namespace

SystemBackupWorker::SystemBackupWorker(SystemBackupConfig config)
    : config_(std::move(config)) {}

SystemBackupWorker::~SystemBackupWorker() {
    stop_scheduler();
}

void SystemBackupWorker::start_scheduler() {
    std::lock_guard<std::mutex> lk(scheduler_mu_);

    if (scheduler_thread_.joinable()) return;

    scheduler_stop_requested_ = false;
    scheduler_started_ = true;
    scheduler_started_epoch_ = now_epoch_seconds();
    scheduler_next_run_epoch_ = next_quarter_epoch(scheduler_started_epoch_);
    scheduler_last_error_.clear();

    scheduler_thread_ = std::thread(&SystemBackupWorker::scheduler_loop, this);
}

void SystemBackupWorker::stop_scheduler() {
    std::thread t;

    {
        std::lock_guard<std::mutex> lk(scheduler_mu_);
        scheduler_stop_requested_ = true;
        scheduler_cv_.notify_all();

        if (scheduler_thread_.joinable()) {
            t = std::move(scheduler_thread_);
        }
    }

    if (t.joinable()) {
        t.join();
    }
}

void SystemBackupWorker::scheduler_loop() {
    while (true) {
        std::int64_t due_epoch = 0;

        {
            std::unique_lock<std::mutex> lk(scheduler_mu_);

            const std::int64_t now = now_epoch_seconds();
            if (scheduler_next_run_epoch_ <= now) {
                scheduler_next_run_epoch_ = next_quarter_epoch(now);
            }

            due_epoch = scheduler_next_run_epoch_;

            const auto due_time = std::chrono::system_clock::from_time_t(
                static_cast<std::time_t>(due_epoch)
            );

            scheduler_cv_.wait_until(lk, due_time, [&] {
                return scheduler_stop_requested_;
            });

            if (scheduler_stop_requested_) return;
        }

        const auto tiers = scheduled_tiers_for_epoch(due_epoch);

        for (const auto& tier : tiers) {
            const auto result = run_now(tier, "scheduled");

            std::lock_guard<std::mutex> lk(scheduler_mu_);
            scheduler_last_run_epoch_ = now_epoch_seconds();
            scheduler_last_tier_ = tier;
            scheduler_last_backup_id_ = result.backup_id;
            scheduler_last_error_ = result.ok ? std::string{} : join_errors(result.errors);
        }

        const auto prune = prune_now();
        if (!prune.ok) {
            std::lock_guard<std::mutex> lk(scheduler_mu_);
            scheduler_last_error_ = join_errors(prune.errors);
        }

        {
            std::lock_guard<std::mutex> lk(scheduler_mu_);
            scheduler_next_run_epoch_ = next_quarter_epoch(now_epoch_seconds());
        }
    }
}

SystemBackupConfig SystemBackupWorker::default_config() {
    SystemBackupConfig cfg;
    cfg.enabled = true;
    cfg.backup_root = "/srv/pqnas/backups/system";
    cfg.sources = default_sources();
    cfg.retention = default_retention();
    return cfg;
}

SystemBackupRunResult SystemBackupWorker::run_now(const std::string& tier_in,
                                                  const std::string& reason_in) {
    std::lock_guard<std::mutex> lk(backup_mutex());

    SystemBackupRunResult r;
    r.tier = tier_in.empty() ? "manual" : sanitize_token(tier_in);

    if (!is_allowed_tier(r.tier)) {
        r.errors.push_back("invalid backup tier: " + r.tier);
        return r;
    }

    if (!config_.enabled) {
        r.errors.push_back("system backups are disabled");
        return r;
    }

    const std::string reason = sanitize_token(reason_in.empty() ? "manual" : reason_in);
    r.backup_id = utc_stamp_compact() + "-" + reason;
    r.backup_dir = config_.backup_root / r.tier / r.backup_id;

    std::error_code ec;
    std::filesystem::create_directories(r.backup_dir, ec);
    if (ec) {
        r.errors.push_back("failed to create backup directory: " + ec.message());
        return r;
    }

    nlohmann::json manifest = {
        {"schema", "pqnas.system_backup.v1"},
        {"backup_id", r.backup_id},
        {"tier", r.tier},
        {"reason", reason},
        {"created_epoch", now_epoch_seconds()},
        {"backup_dir", r.backup_dir.string()},
        {"included_policy", "core_config_users_auth_password_credentials_app_auth_shares_pools_circlestack_only"},
        {"files", nlohmann::json::array()}
    };

    for (const auto& src : config_.sources) {
        if (src.source_path.empty() || src.backup_relative_path.empty()) {
            r.files_skipped += 1;
            r.warnings.push_back("skipped source with empty path: " + src.label);
            continue;
        }

        std::error_code src_ec;
        if (!std::filesystem::is_regular_file(src.source_path, src_ec)) {
            const std::string msg = "source missing";

            // Optional sources are intentionally silent. This keeps installs
            // without Circle Stack or optional config files from producing
            // noisy skipped entries on every scheduled backup.
            if (src.optional) {
                continue;
            }

            r.files_skipped += 1;
            manifest["files"].push_back(source_json(src, {}, "skipped", 0, msg));
            r.errors.push_back(src.source_path.string() + ": " + msg);
            continue;
        }

        const std::filesystem::path dst = r.backup_dir / src.backup_relative_path;

        std::string err;
        bool ok = false;

        if (src.kind == SystemBackupSourceKind::SQLiteDatabase) {
            ok = sqlite_backup_database(src.source_path, dst, &err);
        } else if (src.backup_relative_path == std::filesystem::path("core/etc/pqnas/pqnas.env")) {
            ok = copy_env_file_redacted_safe(src.source_path, dst, &err);
        } else {
            ok = copy_regular_file_safe(src.source_path, dst, &err);
        }

        if (!ok) {
            r.errors.push_back(src.source_path.string() + ": " + err);
            manifest["files"].push_back(source_json(src, dst, "error", 0, err));
            continue;
        }

        const auto bytes = file_size_or_zero(dst);
        r.bytes_written += bytes;
        r.files_written += 1;
        manifest["files"].push_back(source_json(src, dst, "ok", bytes));
    }

    r.ok = r.errors.empty();

    manifest["ok"] = r.ok;
    manifest["bytes_written"] = r.bytes_written;
    manifest["files_written"] = r.files_written;
    manifest["files_skipped"] = r.files_skipped;
    manifest["warnings"] = r.warnings;
    manifest["errors"] = r.errors;

    write_manifest(r.backup_dir / "manifest.json", manifest);

    return r;
}

SystemBackupRunResult SystemBackupWorker::prune_now() {
    std::lock_guard<std::mutex> lk(backup_mutex());

    SystemBackupRunResult r;
    r.ok = true;
    r.tier = "prune";
    r.backup_id = utc_stamp_compact() + "-prune";

    const std::int64_t now = now_epoch_seconds();

    for (const auto& ret : config_.retention) {
        if (ret.keep_seconds <= 0) continue;

        const std::filesystem::path tier_root = config_.backup_root / ret.tier;
        const auto dirs = tier_backup_dirs(tier_root);

        for (const auto& dir : dirs) {
            std::error_code ec;
            const auto ftime = std::filesystem::last_write_time(dir, ec);
            if (ec) continue;

            const auto sys_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
            );

            const std::time_t tt = std::chrono::system_clock::to_time_t(sys_time);
            if (tt <= 0) continue;

            const std::int64_t age = now - static_cast<std::int64_t>(tt);
            if (age <= ret.keep_seconds) continue;

            std::filesystem::remove_all(dir, ec);
            if (ec) {
                r.ok = false;
                r.errors.push_back("failed to remove " + dir.string() + ": " + ec.message());
            } else {
                r.dirs_removed += 1;
            }
        }
    }

    return r;
}

nlohmann::json SystemBackupWorker::status_json() const {
    nlohmann::json sets = nlohmann::json::object();
    for (const auto& src : config_.sources) {
        std::error_code ec;
        const bool present = std::filesystem::is_regular_file(src.source_path, ec);

        // Optional missing sources should not clutter status. For example,
        // many installs may not use Circle Stack yet, so its DBs should only
        // appear once they actually exist.
        if (!present && src.optional) {
            continue;
        }

        auto& set = sets[src.set_id];
        if (!set.is_object()) {
            set = {
                {"id", src.set_id},
                {"sources", nlohmann::json::array()},
                {"present", 0},
                {"missing", 0}
            };
        }

        set["sources"].push_back({
            {"label", src.label},
            {"kind", src.kind == SystemBackupSourceKind::SQLiteDatabase ? "sqlite" : "file"},
            {"path", src.source_path.string()},
            {"present", present},
            {"optional", src.optional},
            {"size_bytes", present ? file_size_or_zero(src.source_path) : 0}
        });

        if (present) {
            set["present"] = set.value("present", 0) + 1;
        } else {
            set["missing"] = set.value("missing", 0) + 1;
        }
    }

    nlohmann::json retention = nlohmann::json::array();
    for (const auto& r : config_.retention) {
        retention.push_back({
            {"tier", r.tier},
            {"keep_seconds", r.keep_seconds}
        });
    }

    nlohmann::json scheduler;
    {
        std::lock_guard<std::mutex> lk(scheduler_mu_);
        scheduler = {
            {"running", scheduler_thread_.joinable()},
            {"started", scheduler_started_},
            {"started_epoch", scheduler_started_epoch_},
            {"next_run_epoch", scheduler_next_run_epoch_},
            {"last_run_epoch", scheduler_last_run_epoch_},
            {"last_tier", scheduler_last_tier_},
            {"last_backup_id", scheduler_last_backup_id_},
            {"last_error", scheduler_last_error_}
        };
    }

    return {
        {"ok", true},
        {"enabled", config_.enabled},
        {"backup_root", config_.backup_root.string()},
        {"storage_used_bytes", directory_size(config_.backup_root)},
        {"sets", sets},
        {"retention", retention},
        {"scheduler", scheduler}
    };
}

nlohmann::json SystemBackupWorker::list_backups_json(int limit) const {
    limit = std::clamp(limit, 1, 500);

    struct Item {
        std::filesystem::path manifest;
        std::filesystem::file_time_type time;
    };

    std::vector<Item> items;
    std::error_code ec;

    if (std::filesystem::is_directory(config_.backup_root, ec)) {
        for (const auto& ent : std::filesystem::recursive_directory_iterator(
                 config_.backup_root,
                 std::filesystem::directory_options::skip_permission_denied,
                 ec)) {
            if (ec) break;
            if (!ent.is_regular_file(ec)) continue;
            if (ent.path().filename() != "manifest.json") continue;
            items.push_back({ent.path(), ent.last_write_time(ec)});
        }
    }

    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        return a.time > b.time;
    });

    nlohmann::json backups = nlohmann::json::array();

    for (const auto& item : items) {
        if (static_cast<int>(backups.size()) >= limit) break;

        std::ifstream in(item.manifest);
        nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
        if (!j.is_object()) continue;

        backups.push_back(j);
    }

    return {
        {"ok", true},
        {"count", backups.size()},
        {"backups", backups}
    };
}

nlohmann::json system_backup_run_result_json(const SystemBackupRunResult& r) {
    return {
        {"ok", r.ok},
        {"tier", r.tier},
        {"backup_id", r.backup_id},
        {"backup_dir", r.backup_dir.string()},
        {"bytes_written", r.bytes_written},
        {"files_written", r.files_written},
        {"files_skipped", r.files_skipped},
        {"dirs_removed", r.dirs_removed},
        {"warnings", r.warnings},
        {"errors", r.errors}
    };
}

} // namespace pqnas::backups
