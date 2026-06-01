#pragma once

#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

namespace pqnas::backups {

enum class SystemBackupSourceKind {
    RegularFile,
    SQLiteDatabase
};

struct SystemBackupSource {
    std::string set_id;
    std::string label;
    SystemBackupSourceKind kind = SystemBackupSourceKind::RegularFile;
    std::filesystem::path source_path;
    std::filesystem::path backup_relative_path;
    bool optional = true;
};

struct SystemBackupRetention {
    std::string tier;
    std::int64_t keep_seconds = 0;
};

struct SystemBackupConfig {
    bool enabled = true;
    std::filesystem::path backup_root = "/srv/pqnas/backups/system";
    std::vector<SystemBackupSource> sources;
    std::vector<SystemBackupRetention> retention;
};

struct SystemBackupRunResult {
    bool ok = false;
    std::string tier;
    std::string backup_id;
    std::filesystem::path backup_dir;
    std::uintmax_t bytes_written = 0;
    int files_written = 0;
    int files_skipped = 0;
    int dirs_removed = 0;
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
};

class SystemBackupWorker {
public:
    explicit SystemBackupWorker(SystemBackupConfig config);
    ~SystemBackupWorker();

    SystemBackupWorker(const SystemBackupWorker&) = delete;
    SystemBackupWorker& operator=(const SystemBackupWorker&) = delete;

    static SystemBackupConfig default_config();

    void start_scheduler();
    void stop_scheduler();

    SystemBackupRunResult run_now(const std::string& tier, const std::string& reason);
    SystemBackupRunResult prune_now();

    nlohmann::json status_json() const;
    nlohmann::json list_backups_json(int limit) const;

    const SystemBackupConfig& config() const { return config_; }

private:
    void scheduler_loop();

    SystemBackupConfig config_;

    mutable std::mutex scheduler_mu_;
    std::condition_variable scheduler_cv_;
    std::thread scheduler_thread_;
    bool scheduler_stop_requested_ = false;
    bool scheduler_started_ = false;
    std::int64_t scheduler_started_epoch_ = 0;
    std::int64_t scheduler_next_run_epoch_ = 0;
    std::int64_t scheduler_last_run_epoch_ = 0;
    std::string scheduler_last_tier_;
    std::string scheduler_last_backup_id_;
    std::string scheduler_last_error_;
};

nlohmann::json system_backup_run_result_json(const SystemBackupRunResult& r);

} // namespace pqnas::backups
