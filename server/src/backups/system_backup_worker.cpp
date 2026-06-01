#include "backups/system_backup_worker.h"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <system_error>
#include <vector>

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

std::vector<SystemBackupSource> default_sources() {
    std::vector<SystemBackupSource> v;

    // Core System
    v.push_back({
        "core",
        "PQ-NAS environment",
        SystemBackupSourceKind::RegularFile,
        "/etc/pqnas/pqnas.env",
        "core/etc/pqnas/pqnas.env",
        true
    });

    // Users & Auth. Prefer the current config path. Keep legacy root fallback
    // only if it actually exists, so status/UI does not show a harmless missing
    // users.json warning on normal installs.
    const std::filesystem::path users_config_path = "/srv/pqnas/config/users.json";
    const std::filesystem::path users_legacy_path = "/srv/pqnas/users.json";

    if (std::filesystem::is_regular_file(users_config_path)) {
        v.push_back({
            "users_auth",
            "Users registry",
            SystemBackupSourceKind::RegularFile,
            users_config_path,
            "users/users.json",
            true
        });
    } else if (std::filesystem::is_regular_file(users_legacy_path)) {
        v.push_back({
            "users_auth",
            "Users registry",
            SystemBackupSourceKind::RegularFile,
            users_legacy_path,
            "users/users.json",
            true
        });
    } else {
        v.push_back({
            "users_auth",
            "Users registry",
            SystemBackupSourceKind::RegularFile,
            users_config_path,
            "users/users.json",
            true
        });
    }

    // Circle Stack
    v.push_back({
        "circle_stack",
        "Circle Stack local database",
        SystemBackupSourceKind::SQLiteDatabase,
        "/srv/pqnas/circlestack.db",
        "circlestack/circlestack.db",
        true
    });

    v.push_back({
        "circle_stack",
        "Circle Stack federation outbox",
        SystemBackupSourceKind::SQLiteDatabase,
        "/srv/pqnas/config/circlestack_federation_outbox.sqlite3",
        "circlestack/circlestack_federation_outbox.sqlite3",
        true
    });

    v.push_back({
        "circle_stack",
        "Circle Stack federation inbox",
        SystemBackupSourceKind::SQLiteDatabase,
        "/srv/pqnas/config/circlestack_federation_inbox.sqlite3",
        "circlestack/circlestack_federation_inbox.sqlite3",
        true
    });

    v.push_back({
        "circle_stack",
        "Circle Stack federation remote feed",
        SystemBackupSourceKind::SQLiteDatabase,
        "/srv/pqnas/config/circlestack_federation_remote_feed.sqlite3",
        "circlestack/circlestack_federation_remote_feed.sqlite3",
        true
    });

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
        {"included_policy", "core_config_users_auth_circlestack_only"},
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
            r.files_skipped += 1;
            const std::string msg = "source missing";
            manifest["files"].push_back(source_json(src, {}, "skipped", 0, msg));
            if (!src.optional) r.errors.push_back(src.source_path.string() + ": " + msg);
            continue;
        }

        const std::filesystem::path dst = r.backup_dir / src.backup_relative_path;

        std::string err;
        bool ok = false;

        if (src.kind == SystemBackupSourceKind::SQLiteDatabase) {
            ok = sqlite_backup_database(src.source_path, dst, &err);
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
        auto& set = sets[src.set_id];
        if (!set.is_object()) {
            set = {
                {"id", src.set_id},
                {"sources", nlohmann::json::array()},
                {"present", 0},
                {"missing", 0}
            };
        }

        std::error_code ec;
        const bool present = std::filesystem::is_regular_file(src.source_path, ec);

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
