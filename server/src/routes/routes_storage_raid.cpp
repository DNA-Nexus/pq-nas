#include "routes_storage_raid.h"
#include "httplib.h"

#include "audit_log.h"
#include "users_registry.h"
#include "authz.h"
#include "storage_pools.h"
#include "storage_info.h"
#include "runtime_paths.h"

#include <array>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <utility>
#include <stdexcept>

#include "workspaces.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <sys/statvfs.h>
#include <thread>
#include <unordered_map>
#include <sodium.h>


#include <condition_variable>
#include <deque>
#include <mutex>
#include <sys/statvfs.h>
#include <thread>
#include <unordered_map>
#include <sodium.h>



#include "users_registry.h"
#include "authz.h"
#include "storage_pools.h"
#include "storage_info.h"
#include "workspaces.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

using json = nlohmann::json;


// -----------------------------------------------------------------------------
// Manual forward declarations for copied helpers used before their definitions.
// Do not generate these automatically: C++ snippets inside function bodies can
// otherwise be mistaken for declarations.
// -----------------------------------------------------------------------------
static std::string shorten(const std::string& s, std::size_t max_len);
static std::string shell_escape_single_quotes(std::string s);
static std::string jstr_cap(const json& j, const char* key, size_t max_len = 220);
static bool parse_btrfs_df_line(const std::string& line, std::string* profile, std::uint64_t* total_bytes, std::uint64_t* used_bytes, std::string* total_str, std::string* used_str);
static bool parse_human_bytes(const std::string& s, std::uint64_t* out);
static double round_dp(double v, int dp);
static void lsblk_collect_mountpoints_recursive(const json& node, json& out);
static std::vector<std::string> split_lines(const std::string& s);
static std::string to_lower_ascii_copy(std::string s);
static std::uint64_t parse_btrfs_human_bytes_to_u64(const std::string& s);
static bool is_hex_lower_or_upper(char c);
static bool is_dev_path_basic_safe(const std::string& p);
static std::filesystem::path pools_cfg_path_from_users_path(const std::string& users_path);
static std::string pqnas_trim_copy(std::string s);
static std::string header_value(const httplib::Request& req, const std::string& name);
static std::string header_value(const httplib::Request& req, const char* name);
static std::string raid_job_new_id();
static void raid_worker_start_once();
static std::string raid_exec_state_best_effort_from_path(const std::string& path);
static std::string raid_exec_record_archive_path_for_plan(const std::string& plan_id);
static bool raid_write_queued_record_fail_closed(
    const std::string& plan_id,
    const std::string& job_id,
    const json& request,
    const json& commands,
    std::string* err
);
static json raid_exec_record_read_best_effort_obj(
    const std::string& plan_id,
    const std::string& job_id,
    const json& request,
    const json& commands
);

namespace pqnas {
[[maybe_unused]] static inline std::string shorten(const std::string& s, std::size_t max_len = 220) {
    return ::shorten(s, max_len);
}
[[maybe_unused]] static inline std::string now_iso_utc() {
    return pqnas::AuditLog::now_iso_utc();
}
} // namespace pqnas


// Local transitional helpers missing from the extracted block.
[[maybe_unused]] static std::string shorten(const std::string& s, std::size_t max_len) {
    if (s.size() <= max_len) return s;
    if (max_len <= 3) return s.substr(0, max_len);
    return s.substr(0, max_len - 3) + "...";
}

[[maybe_unused]] static std::string header_value(const httplib::Request& req, const char* name) {
    auto it = req.headers.find(name);
    if (it == req.headers.end()) return {};
    return it->second;
}

[[maybe_unused]] static std::string header_value(const httplib::Request& req, const std::string& name) {
    return header_value(req, name.c_str());
}

// Transitional same-origin origin value. Later this should be passed via context/config.
static const std::string ORIGIN = []() -> std::string {
    const char* v = std::getenv("PQNAS_ORIGIN");
    return (v && *v) ? std::string(v) : std::string();
}();



// -----------------------------------------------------------------------------
// Early copied transitional structs required by helper functions.
// -----------------------------------------------------------------------------

// copied transitional struct from main.cpp: BtrfsShowDevice
struct BtrfsShowDevice {
    int devid = -1;
    std::string path;           // capped
    uint64_t size_bytes = 0;
    uint64_t used_bytes = 0;
    std::string parent_disk;    // derived (e.g. /dev/nvme0n1)
};


// copied transitional struct from main.cpp: BtrfsShowParsed
struct BtrfsShowParsed {
    std::string label;          // capped
    std::string uuid;           // capped
    int total_devices = -1;
    uint64_t fs_bytes_used_bytes = 0;
    std::vector<BtrfsShowDevice> devices;
};


// copied transitional struct from main.cpp: RaidJob
struct RaidJob {
    std::string job_id;
    std::string plan_id;
    std::string resolved_mount;

    // Optional: keep some metadata for nicer responses/status
    json plan;                 // plan payload to echo in response
    json record;               // execution record JSON we mutate and write
    json commands;             // array of strings (same as plan["commands"])
};


// -----------------------------------------------------------------------------
// Early copied transitional RAID job globals required by helper functions.
// -----------------------------------------------------------------------------

// copied transitional global from main.cpp: g_raid_jobs_mu
static std::mutex g_raid_jobs_mu;
// copied transitional global from main.cpp: g_raid_jobs_cv
static std::condition_variable g_raid_jobs_cv;
// copied transitional global from main.cpp: g_raid_jobs_q
static std::deque<RaidJob> g_raid_jobs_q;
// copied transitional global from main.cpp: g_raid_job_meta
static std::unordered_map<std::string, json> g_raid_job_meta;
// copied transitional RAID worker globals from main.cpp
static std::string g_users_path_for_raid;
static std::atomic<bool> g_raid_worker_stop{false};
static std::thread g_raid_worker_thr;
static std::function<void(const pqnas::AuditEvent&)> g_storage_raid_audit_append;

static void audit_append(const pqnas::AuditEvent& ev) {
    if (g_storage_raid_audit_append) {
        g_storage_raid_audit_append(ev);
    }
}





// Local transitional helper: collect lsblk mountpoints recursively.
[[maybe_unused]] static void lsblk_collect_mountpoints_recursive(const json& node, json& out) {
    if (!out.is_array()) out = json::array();

    auto add_mountpoint = [&](const std::string& mp) {
        if (mp.empty()) return;
        for (const auto& existing : out) {
            if (existing.is_string() && existing.get<std::string>() == mp) return;
        }
        out.push_back(mp);
    };

    if (node.contains("mountpoint") && node["mountpoint"].is_string()) {
        add_mountpoint(node["mountpoint"].get<std::string>());
    }

    if (node.contains("mountpoints") && node["mountpoints"].is_array()) {
        for (const auto& mp : node["mountpoints"]) {
            if (mp.is_string()) add_mountpoint(mp.get<std::string>());
        }
    }

    if (node.contains("children") && node["children"].is_array()) {
        for (const auto& child : node["children"]) {
            if (child.is_object()) {
                lsblk_collect_mountpoints_recursive(child, out);
            }
        }
    }
}

// -----------------------------------------------------------------------------
// Copied transitional helpers from main.cpp.
// TODO: move shared helpers into proper modules after route split is stable.
// -----------------------------------------------------------------------------

// copied transitional helper from main.cpp: reply_json
[[maybe_unused]] static void reply_json(httplib::Response& res, int code, const std::string& body_json) {
    res.status = code;
    res.set_header("Content-Type", "application/json");
    res.body = body_json;
}


// copied transitional helper from main.cpp: getenv_str
[[maybe_unused]] static std::string getenv_str(const char* k) {
    const char* v = std::getenv(k);
    return (v && *v) ? std::string(v) : std::string();
}


// copied transitional helper from main.cpp: getenv_bool
[[maybe_unused]] static bool getenv_bool(const char* k, bool defv) {
    const char* v = std::getenv(k);
    if (!v) return defv;
    std::string s(v);
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    if (s == "1" || s == "true" || s == "yes" || s == "on") return true;
    if (s == "0" || s == "false" || s == "no" || s == "off") return false;
    return defv;
}


// copied transitional helper from main.cpp: cap_string
[[maybe_unused]] static inline void cap_string(std::string& s, size_t cap_bytes) {
    if (s.size() > cap_bytes) {
        s.resize(cap_bytes);
    }
}


// copied transitional helper from main.cpp: sh_quote
[[maybe_unused]] static std::string sh_quote(const std::string& s) {
    // Wrap in single quotes and escape any embedded single quotes safely.
    return "'" + shell_escape_single_quotes(s) + "'";
}


// copied transitional helper from main.cpp: rtrim_inplace
[[maybe_unused]] static inline void rtrim_inplace(std::string& s) {
    while (!s.empty()) {
        char c = s.back();
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t') s.pop_back();
        else break;
    }
}


// Security: known root-helper command strings are legacy display/plan-hash
// strings. Execute them via argv before any shell fallback.
[[maybe_unused]] static bool run_argv_capture_limited(
    const std::vector<std::string>& argv_s,
    std::string* out,
    int* ec,
    int timeout_ms,
    std::size_t max_bytes);

static std::string raid_cmd_trim_copy(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.erase(s.begin());
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
    return s;
}

static bool raid_parse_legacy_helper_tail(const std::string& tail,
                                          std::vector<std::string>* args_out) {
    if (args_out) args_out->clear();

    std::vector<std::string> args;
    std::string cur;
    bool in_single = false;
    bool have = false;

    for (std::size_t i = 0; i < tail.size(); ++i) {
        const char c = tail[i];

        if (in_single) {
            if (c == '\'') {
                // sh_quote() encodes embedded apostrophe as: '\''.
                if (i + 3 < tail.size() &&
                    tail[i + 1] == '\\' &&
                    tail[i + 2] == '\'' &&
                    tail[i + 3] == '\'') {
                    cur.push_back('\'');
                    i += 3;
                    have = true;
                    continue;
                }

                in_single = false;
                have = true;
                continue;
            }

            cur.push_back(c);
            have = true;
            continue;
        }

        if (std::isspace(static_cast<unsigned char>(c))) {
            if (have) {
                args.push_back(cur);
                cur.clear();
                have = false;
            }
            continue;
        }

        if (c == '\'') {
            in_single = true;
            have = true;
            continue;
        }

        // Fail closed for shell metacharacters. Known helper action names are
        // simple unquoted tokens; arguments produced by sh_quote() are quoted.
        if (c == '\\' || c == ';' || c == '|' || c == '&' ||
            c == '<' || c == '>' || c == '$' || c == '`' ||
            c == '(' || c == ')') {
            return false;
        }

        cur.push_back(c);
        have = true;
    }

    if (in_single) return false;

    if (have) {
        args.push_back(cur);
    }

    if (args_out) *args_out = std::move(args);
    return true;
}

// Security: pqnas-btrfs-status is a read-only root helper. Keep the C++
// interceptor fail-closed too, so malformed legacy command strings never reach
// sudo even though the helper also validates its action/mount allowlist.
static bool raid_btrfs_status_args_are_supported(const std::vector<std::string>& args) {
    if (args.size() != 2) return false;

    const std::string& action = args[0];
    const std::string& mount = args[1];

    const bool action_ok =
        action == "filesystem-show" ||
        action == "filesystem-df" ||
        action == "filesystem-df-bytes" ||
        action == "filesystem-usage" ||
        action == "filesystem-usage-bytes" ||
        action == "device-stats" ||
        action == "scrub-status" ||
        action == "balance-status";

    if (!action_ok) return false;
    if (mount.empty() || mount[0] != '/') return false;
    if (mount.size() > 512) return false;
    if (mount.find("..") != std::string::npos) return false;

    for (unsigned char c : mount) {
        if (c == '\0' || c == '\n' || c == '\r' || c == '\t') return false;
    }

    return true;
}

// Security: pqnas-raid-root is a mutating root helper. Keep the C++
// interceptor fail-closed too, so malformed legacy command strings never reach
// sudo even though the helper also performs full validation.
static bool raid_root_arg_has_no_control_or_traversal(const std::string& v) {
    if (v.empty() || v.size() > 512) return false;
    if (v.find("..") != std::string::npos) return false;

    for (unsigned char c : v) {
        if (c == '\0' || c == '\n' || c == '\r' || c == '\t') return false;
    }

    return true;
}

static bool raid_root_dev_arg_is_supported(const std::string& v) {
    return raid_root_arg_has_no_control_or_traversal(v) &&
           v.rfind("/dev/", 0) == 0;
}

static bool raid_root_pool_arg_is_supported(const std::string& v) {
    if (!raid_root_arg_has_no_control_or_traversal(v)) return false;

    const char* roots[] = {
        "/srv/pqnas/pools/",
        "/srv/pqnas-test/pools/",
        "/srv/pqnas-test-btrfs/pools/"
    };

    for (const char* root : roots) {
        if (v.rfind(root, 0) == 0 && v.size() > std::strlen(root)) {
            return true;
        }
    }

    return false;
}

static bool raid_root_pool_data_arg_is_supported(const std::string& v) {
    const std::string suffix = "/data";
    return raid_root_pool_arg_is_supported(v) &&
           v.size() > suffix.size() &&
           v.compare(v.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static bool raid_root_label_arg_is_supported(const std::string& label) {
    const std::string prefix = "PQNAS_";
    if (label.rfind(prefix, 0) != 0 || label.size() <= prefix.size() || label.size() > 80) {
        return false;
    }

    for (unsigned char c : label) {
        if (std::isalnum(c) || c == '.' || c == '_' || c == '-') continue;
        return false;
    }

    return true;
}

static bool raid_root_uuid_arg_is_supported(const std::string& uuid) {
    if (uuid.size() != 36) return false;

    for (std::size_t i = 0; i < uuid.size(); ++i) {
        const char c = uuid[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return false;
            continue;
        }
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    }

    return true;
}

static bool raid_root_mount_spec_arg_is_supported(const std::string& spec) {
    const std::string label_prefix = "LABEL=";
    const std::string uuid_prefix = "UUID=";

    if (spec.rfind(label_prefix, 0) == 0) {
        return raid_root_label_arg_is_supported(spec.substr(label_prefix.size()));
    }

    if (spec.rfind(uuid_prefix, 0) == 0) {
        return raid_root_uuid_arg_is_supported(spec.substr(uuid_prefix.size()));
    }

    return false;
}

static bool raid_root_args_are_supported(const std::vector<std::string>& args) {
    if (args.empty()) return false;

    const std::string& action = args[0];

    if (action == "zap-disk" ||
        action == "create-btrfs-partition" ||
        action == "partprobe" ||
        action == "wipefs") {
        return args.size() == 2 && raid_root_dev_arg_is_supported(args[1]);
    }

    if (action == "mkfs-btrfs") {
        if (args.size() < 4) return false;

        const std::string& mode = args[1];
        const std::string& label = args[2];

        if (mode != "single" && mode != "raid1") return false;
        if (!raid_root_label_arg_is_supported(label)) return false;
        if (mode == "raid1" && args.size() < 5) return false;

        for (std::size_t i = 3; i < args.size(); ++i) {
            if (!raid_root_dev_arg_is_supported(args[i])) return false;
        }

        return true;
    }

    if (action == "mkdir-p") {
        return args.size() == 2 && raid_root_pool_arg_is_supported(args[1]);
    }

    if (action == "chown-pqnas" || action == "chmod-0755") {
        return args.size() == 2 && raid_root_pool_data_arg_is_supported(args[1]);
    }

    if (action == "mount-label") {
        return args.size() == 3 &&
               raid_root_label_arg_is_supported(args[1]) &&
               raid_root_pool_arg_is_supported(args[2]);
    }

    if (action == "mount-spec") {
        return args.size() == 3 &&
               raid_root_mount_spec_arg_is_supported(args[1]) &&
               raid_root_pool_arg_is_supported(args[2]);
    }

    if (action == "umount-pool" ||
        action == "rmdir-pool" ||
        action == "btrfs-scrub-start" ||
        action == "btrfs-balance-raid1" ||
        action == "btrfs-balance-single-force") {
        return args.size() == 2 && raid_root_pool_arg_is_supported(args[1]);
    }

    if (action == "udev-settle" || action == "btrfs-device-scan") {
        return args.size() == 1;
    }

    if (action == "btrfs-device-add" || action == "btrfs-device-remove") {
        return args.size() == 3 &&
               raid_root_dev_arg_is_supported(args[1]) &&
               raid_root_pool_arg_is_supported(args[2]);
    }

    if (action == "btrfs-balance-force-profile") {
        return args.size() == 3 &&
               (args[1] == "single" || args[1] == "raid1") &&
               raid_root_pool_arg_is_supported(args[2]);
    }

    return false;
}

static bool raid_try_run_known_root_helper_argv(const std::string& cmd_in,
                                                std::string* out,
                                                int* exit_code) {
    std::string cmd = raid_cmd_trim_copy(cmd_in);

    // Existing callers often append shell stderr redirection. Keep the display
    // string unchanged, but remove the suffix before strict legacy parsing.
    if (cmd.size() >= 4 && cmd.compare(cmd.size() - 4, 4, "2>&1") == 0) {
        cmd.resize(cmd.size() - 4);
        cmd = raid_cmd_trim_copy(cmd);
    }

    struct KnownHelper {
        const char* prefix;
        const char* helper;
    };

    static const KnownHelper helpers[] = {
        {
            "/usr/bin/sudo -n /usr/local/sbin/pqnas-raid-root",
            "/usr/local/sbin/pqnas-raid-root"
        },
        {
            "/usr/bin/sudo -n /usr/local/sbin/pqnas-btrfs-status",
            "/usr/local/sbin/pqnas-btrfs-status"
        },
    };

    for (const auto& h : helpers) {
        const std::string prefix(h.prefix);

        if (cmd != prefix && cmd.rfind(prefix + " ", 0) != 0) {
            continue;
        }

        std::string tail;
        if (cmd.size() > prefix.size()) {
            tail = cmd.substr(prefix.size() + 1);
        }

        std::vector<std::string> args;
        if (!raid_parse_legacy_helper_tail(tail, &args)) {
            if (out) *out = "err: failed to parse legacy root-helper command\n";
            if (exit_code) *exit_code = 2;
            return true;
        }

        if (std::string(h.helper) == "/usr/local/sbin/pqnas-raid-root" &&
            !raid_root_args_are_supported(args)) {
            if (out) *out = "err: unsupported raid root helper command\n";
            if (exit_code) *exit_code = 2;
            return true;
        }

        if (std::string(h.helper) == "/usr/local/sbin/pqnas-btrfs-status" &&
            !raid_btrfs_status_args_are_supported(args)) {
            if (out) *out = "err: unsupported btrfs status helper command\n";
            if (exit_code) *exit_code = 2;
            return true;
        }

        std::vector<std::string> argv = {
            "/usr/bin/sudo",
            "-n",
            h.helper
        };
        argv.insert(argv.end(), args.begin(), args.end());

        // Some btrfs balance/remove operations can be long-running. Preserve the
        // old no-shell behavior without imposing a short command timeout.
        return run_argv_capture_limited(
            argv,
            out,
            exit_code,
            24 * 60 * 60 * 1000,
            2u * 1024u * 1024u
        );
    }

    return false;
}

// Security: route known non-root storage probe command strings through argv
// before the legacy popen fallback. This keeps existing display strings stable
// while preventing mount/device arguments from being interpreted by a shell.
[[maybe_unused]] static bool raid_probe_abs_path_arg_is_safe(const std::string& v) {
    if (v.empty() || v[0] != '/') return false;
    if (v.size() > 512) return false;
    for (unsigned char c : v) {
        if (c == '\0' || c == '\n' || c == '\r' || c == '\t') return false;
    }
    return true;
}

[[maybe_unused]] static bool raid_strip_probe_redirect_suffix(std::string* cmd) {
    if (!cmd) return false;

    bool stripped = false;
    for (;;) {
        std::string before = *cmd;

        const char* suffixes[] = {
            " 2>&1",
            " 2>/dev/null"
        };

        for (const char* suffix : suffixes) {
            const std::size_t n = std::strlen(suffix);
            if (cmd->size() >= n && cmd->compare(cmd->size() - n, n, suffix) == 0) {
                cmd->resize(cmd->size() - n);
                *cmd = raid_cmd_trim_copy(*cmd);
                stripped = true;
                break;
            }
        }

        if (*cmd == before) break;
    }

    return stripped;
}

[[maybe_unused]] static bool raid_try_run_nonroot_probe_argv(const std::string& cmd_in,
                                                             std::string* out,
                                                             int* exit_code) {
    std::string cmd = raid_cmd_trim_copy(cmd_in);
    raid_strip_probe_redirect_suffix(&cmd);

    auto run_probe = [&](const std::vector<std::string>& argv) -> bool {
        return run_argv_capture_limited(
            argv,
            out,
            exit_code,
            10000,
            1024u * 1024u
        );
    };

    if (cmd == "lsblk -J -b -O" || cmd == "/usr/bin/lsblk -J -b -O") {
        return run_probe({"/usr/bin/lsblk", "-J", "-b", "-O"});
    }

    const std::string lsblk_prefix = "/usr/bin/lsblk ";
    if (cmd.rfind(lsblk_prefix, 0) == 0) {
        std::vector<std::string> args;
        if (!raid_parse_legacy_helper_tail(cmd.substr(lsblk_prefix.size()), &args)) {
            if (out) *out = "err: failed to parse lsblk probe command\n";
            if (exit_code) *exit_code = 2;
            return true;
        }

        if (args.size() == 4 &&
            args[0] == "-J" &&
            args[1] == "-b" &&
            args[2] == "-O" &&
            args[3].rfind("/dev/", 0) == 0 &&
            raid_probe_abs_path_arg_is_safe(args[3])) {
            return run_probe({"/usr/bin/lsblk", "-J", "-b", "-O", args[3]});
        }

        if (out) *out = "err: unsupported lsblk probe command\n";
        if (exit_code) *exit_code = 2;
        return true;
    }

    const std::string findmnt_prefix = "/usr/bin/findmnt ";
    if (cmd.rfind(findmnt_prefix, 0) == 0) {
        std::vector<std::string> args;
        if (!raid_parse_legacy_helper_tail(cmd.substr(findmnt_prefix.size()), &args)) {
            if (out) *out = "err: failed to parse findmnt probe command\n";
            if (exit_code) *exit_code = 2;
            return true;
        }

        if (args.size() == 4 &&
            args[0] == "-no" &&
            (args[1] == "TARGET" || args[1] == "FSTYPE" || args[1] == "SOURCE") &&
            args[2] == "--target" &&
            raid_probe_abs_path_arg_is_safe(args[3])) {
            return run_probe({"/usr/bin/findmnt", "-no", args[1], "--target", args[3]});
        }

        if (args.size() == 5 &&
            args[0] == "-rn" &&
            args[1] == "-t" &&
            args[2] == "btrfs" &&
            args[3] == "-o" &&
            (args[4] == "TARGET" || args[4] == "TARGET,SOURCE,FSTYPE")) {
            return run_probe({"/usr/bin/findmnt", "-rn", "-t", "btrfs", "-o", args[4]});
        }

        if (out) *out = "err: unsupported findmnt probe command\n";
        if (exit_code) *exit_code = 2;
        return true;
    }

    return false;
}

// copied transitional helper from main.cpp: run_capture
[[maybe_unused]] static int run_capture(const std::string& cmd, std::string* out) {
    int helper_ec = 127;
    if (raid_try_run_known_root_helper_argv(cmd, out, &helper_ec)) {
        return helper_ec;
    }

    int probe_ec = 127;
    if (raid_try_run_nonroot_probe_argv(cmd, out, &probe_ec)) {
        return probe_ec;
    }

    // Security: unsupported command strings fail closed instead of reaching a shell.
    // Supported legacy strings are intercepted above and executed via argv.
    if (out) *out = "err: unsupported RAID capture command\n";
    return 127;
}


// copied transitional helper from main.cpp: run_cmd_capture
[[maybe_unused]] static bool run_argv_capture_limited(
    const std::vector<std::string>& argv_s,
    std::string* out,
    int* ec,
    int timeout_ms,
    std::size_t max_bytes);

[[maybe_unused]] static int run_lsblk_json_all_props_argv(const std::string& disk_path,
                                                          std::string* out) {
    std::vector<std::string> argv{
        "/usr/bin/lsblk",
        "-J",
        "-b",
        "-O"
    };

    // Security: non-empty disk paths must be absolute /dev paths before argv exec.
    // Empty disk_path means full lsblk inventory without adding a user argument.
    if (!disk_path.empty()) {
        if (disk_path.rfind("/dev/", 0) != 0 || !raid_probe_abs_path_arg_is_safe(disk_path)) {
            if (out) *out = "err: unsafe lsblk disk path\n";
            return 2;
        }
        argv.push_back(disk_path);
    }

    int ec = 127;
    const bool ran = run_argv_capture_limited(
        argv,
        out,
        &ec,
        10000,
        1024u * 1024u
    );

    return ran ? ec : 127;
}

[[maybe_unused]] static int run_findmnt_no_target_argv(const std::string& field,
                                                   const std::string& target,
                                                   std::string* out) {
    if (field != "TARGET" && field != "FSTYPE" && field != "SOURCE") {
        if (out) *out = "err: unsupported findmnt field\n";
        return 2;
    }

    // Security: findmnt target paths must be safe absolute paths before argv exec.
    if (!raid_probe_abs_path_arg_is_safe(target)) {
        if (out) *out = "err: unsafe findmnt target path\n";
        return 2;
    }

    int ec = 127;
    const bool ran = run_argv_capture_limited(
        {"/usr/bin/findmnt", "-no", field, "--target", target},
        out,
        &ec,
        10000,
        128u * 1024u
    );

    return ran ? ec : 127;
}

[[maybe_unused]] static int run_findmnt_btrfs_list_argv(const std::string& fields,
                                                    std::string* out) {
    if (fields != "TARGET" && fields != "TARGET,SOURCE,FSTYPE") {
        if (out) *out = "err: unsupported findmnt btrfs field list\n";
        return 2;
    }

    int ec = 127;
    const bool ran = run_argv_capture_limited(
        {"/usr/bin/findmnt", "-rn", "-t", "btrfs", "-o", fields},
        out,
        &ec,
        10000,
        1024u * 1024u
    );

    return ran ? ec : 127;
}

static bool run_cmd_capture(const std::string& cmd, std::string* out, int* exit_code) {
    // hardening: fstab pseudo commands use argv exec, not shell.
    auto run_fstab_pseudo_argv = [&](const std::string& prefix,
                                     const std::string& helper) -> bool {
        if (cmd.rfind(prefix, 0) != 0) return false;

        std::string mount = cmd.substr(prefix.size());
        while (!mount.empty() && std::isspace(static_cast<unsigned char>(mount.front()))) {
            mount.erase(mount.begin());
        }
        while (!mount.empty() && std::isspace(static_cast<unsigned char>(mount.back()))) {
            mount.pop_back();
        }

        const std::string pools_root = "/srv/pqnas/pools/";
        if (mount.empty() ||
            mount.rfind(pools_root, 0) != 0 ||
            mount.find('/', pools_root.size()) != std::string::npos) {
            if (out) *out = "err: fstab helper only allows /srv/pqnas/pools/<pool_id>\n";
            if (exit_code) *exit_code = 2;
            return true;
        }

        // hardening: no shell parsing for root helper arguments.
        return run_argv_capture_limited({
            "/usr/bin/sudo",
            "-n",
            helper,
            mount
        }, out, exit_code, 10000, 64 * 1024);
    };

    if (run_fstab_pseudo_argv("FSTAB_ADD_BTRFS ", "/usr/local/sbin/pqnas-fstab-add-btrfs")) {
        return true;
    }
    if (run_fstab_pseudo_argv("FSTAB_REMOVE ", "/usr/local/sbin/pqnas-fstab-remove")) {
        return true;
    }

    if (raid_try_run_known_root_helper_argv(cmd, out, exit_code)) {
        return true;
    }

    if (out) out->clear();
    if (exit_code) *exit_code = 127; // default like "command failed"

    // Always capture stderr too.
    int probe_ec = 127;
    if (raid_try_run_nonroot_probe_argv(cmd, out, &probe_ec)) {
        if (exit_code) *exit_code = probe_ec;
        return probe_ec == 0;
    }

    // Security: unsupported command strings fail closed instead of reaching a shell.
    // All supported plan, root-helper, fstab, lsblk, and findmnt command forms
    // must be handled above via argv dispatchers.
    if (out) *out = "err: unsupported RAID command\n";
    if (exit_code) *exit_code = 127;
    return false;
}

// Security: use argv execution for direct btrfs-status helper call sites.
// This removes visible shell-string construction from hot paths while keeping
// the root helper action/mount allowlist as the first gate.
static bool run_btrfs_status_helper_argv(const std::string& action,
                                         const std::string& mount,
                                         std::string* out,
                                         int* exit_code) {
    const std::vector<std::string> args = {action, mount};

    if (!raid_btrfs_status_args_are_supported(args)) {
        if (out) *out = "err: unsupported btrfs status helper command\n";
        if (exit_code) *exit_code = 2;
        return true;
    }

    return run_argv_capture_limited({
        "/usr/bin/sudo",
        "-n",
        "/usr/local/sbin/pqnas-btrfs-status",
        action,
        mount
    }, out, exit_code, 24 * 60 * 60 * 1000, 2u * 1024u * 1024u);
}

static int run_btrfs_status_helper_capture(const std::string& action,
                                           const std::string& mount,
                                           std::string* out) {
    int ec = 127;
    const bool ok = run_btrfs_status_helper_argv(action, mount, out, &ec);
    if (!ok && ec == 0) return 127;
    return ec;
}


// copied transitional helper from main.cpp: trim_copy
[[maybe_unused]] static inline std::string trim_copy(std::string s) {
    // reuse your rtrim + simple ltrim
    rtrim_inplace(s);
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) i++;
    if (i > 0) s.erase(0, i);
    return s;
}


// copied transitional helper from main.cpp: starts_with
[[maybe_unused]] static inline bool starts_with(const std::string& s, const std::string& pfx) {
    return s.rfind(pfx, 0) == 0;
}


// copied transitional helper from main.cpp: str_contains
[[maybe_unused]] static inline bool str_contains(const std::string& s, const char* needle) {
    return s.find(needle) != std::string::npos;
}


// copied transitional helper from main.cpp: read_text_file
[[maybe_unused]] static bool read_text_file(const std::string& path, std::string* out) {
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


// copied transitional helper from main.cpp: write_text_file_atomic
[[maybe_unused]] static bool write_text_file_atomic(const std::string& path, const std::string& content) {
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


// copied transitional helper from main.cpp: sha256_hex_lower_evp
[[maybe_unused]] static std::string sha256_hex_lower_evp(const std::string& s) {
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


// copied transitional helper from main.cpp: rand_hex_16
[[maybe_unused]] static std::string rand_hex_16() {
    static const char* k = "0123456789abcdef";
    std::array<unsigned char, 8> b{};
    randombytes_buf(b.data(), b.size());
    std::string s;
    s.reserve(16);
    for (unsigned char c : b) { s.push_back(k[c >> 4]); s.push_back(k[c & 0x0f]); }
    return s;
}


// copied transitional helper from main.cpp: iso8601_now
[[maybe_unused]] static std::string iso8601_now() {
    using namespace std::chrono;
    auto now = system_clock::now();
    std::time_t tt = system_clock::to_time_t(now);

    std::tm tm{};
    gmtime_r(&tt, &tm); // UTC

    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buf);
}


// copied transitional helper from main.cpp: audit_safe_header_value
[[maybe_unused]] static std::string audit_safe_header_value(const std::string& raw, std::size_t max_len = 512) {
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


// copied transitional helper from main.cpp: is_sha256_hex_lower
[[maybe_unused]] static bool is_sha256_hex_lower(const std::string& s) {
    if (s.size() != 64) return false;
    for (char c : s) {
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!ok) return false;
    }
    return true;
}


// copied transitional helper from main.cpp: open_excl_lockfile
[[maybe_unused]] static int open_excl_lockfile(const std::string& path, std::string* err) {
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0640);
    if (fd < 0) {
        if (err) *err = std::string("open(O_EXCL) failed: ") + std::strerror(errno);
        return -1;
    }
    return fd;
}


// copied transitional helper from main.cpp: ensure_dir_fail_closed
[[maybe_unused]] static bool ensure_dir_fail_closed(const std::string& dir, std::string* err) {
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


// copied transitional helper from main.cpp: statvfs_path
[[maybe_unused]] static bool statvfs_path(const std::string& path, std::uint64_t* total_bytes, std::uint64_t* free_bytes) {
    if (total_bytes) *total_bytes = 0;
    if (free_bytes) *free_bytes = 0;

    struct statvfs sv {};
    if (::statvfs(path.c_str(), &sv) != 0) return false;

    const std::uint64_t frsize = (std::uint64_t)sv.f_frsize;
    if (total_bytes) *total_bytes = frsize * (std::uint64_t)sv.f_blocks;
    if (free_bytes)  *free_bytes  = frsize * (std::uint64_t)sv.f_bavail;
    return true;
}


// copied transitional helper from main.cpp: is_abs_path_safe
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


// copied transitional helper from main.cpp: parent_disk_from_dev
[[maybe_unused]] static inline std::string parent_disk_from_dev(const std::string& dev_in) {
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


// copied transitional helper from main.cpp: storage_list_disks_json
[[maybe_unused]] static json storage_list_disks_json(std::string* raw_lsblk_json_out = nullptr) {
    std::string out;
    // -J JSON, -b bytes, -O all props
    // Security: call lsblk via argv directly, not through a shell string.
    run_lsblk_json_all_props_argv("", &out);

    // Only cap the *debug/raw* string, never cap the parsed JSON input
    if (raw_lsblk_json_out) {
        std::string raw = out;
        cap_string(raw, 1024 * 1024); // 1 MiB cap (debug safety)
        *raw_lsblk_json_out = raw;
    }

    json root;
    try {
        root = json::parse(out);
    } catch (...) {
        return json{
            {"ok", false},
            {"error", "lsblk_parse_failed"}
        };
    }

    const bool allow_loop = getenv_bool("PQNAS_STORAGE_ALLOW_LOOP", false);

    json disks = json::array();
    json by_path = json::object();
    json by_name = json::object();

    if (!root.contains("blockdevices") || !root["blockdevices"].is_array()) {
        return json{{"ok", true}, {"disks", disks}, {"by_path", by_path}, {"by_name", by_name}};
    }


    for (const auto& d : root["blockdevices"]) {
        // type
        const std::string type = d.value("type", "");
        if (type != "disk") {
            // Allow loop devices only when explicitly enabled (dev/testing)
            if (!(allow_loop && type == "loop")) continue;
        }


        std::string name = d.value("name", "");
        if (name.size() > 256) name.resize(256);

        std::string path = d.value("path", "");
        if (path.size() > 256) path.resize(256);

        if (name.empty()) continue;
        if (path.empty()) continue;

        // filter loop devices unless explicitly allowed (snap uses tons of /dev/loop*)
        if (!allow_loop) {
            if (name.rfind("loop", 0) == 0) continue;
        }

        // collect mountpoints (lsblk sometimes returns array or string; handle both)
        json mps = json::array();
        if (d.contains("mountpoints")) {
            const auto& mp = d["mountpoints"];
            if (mp.is_array()) {
                for (const auto& x : mp) {
                    if (x.is_string() && !x.get<std::string>().empty()) mps.push_back(x);
                }
            } else if (mp.is_string()) {
                auto s = mp.get<std::string>();
                if (!s.empty()) mps.push_back(s);
            }
        } else if (d.contains("mountpoint") && d["mountpoint"].is_string()) {
            auto s = d["mountpoint"].get<std::string>();
            if (!s.empty()) mps.push_back(s);
        }

        // children count (partitions)
        int children = 0;
        if (d.contains("children") && d["children"].is_array()) children = (int)d["children"].size();

        // size: lsblk -b gives size bytes as string or number depending on version; normalize to uint64.
        uint64_t size_bytes = 0;
        if (d.contains("size")) {
            if (d["size"].is_number_unsigned()) size_bytes = d["size"].get<uint64_t>();
            else if (d["size"].is_number()) size_bytes = (uint64_t)d["size"].get<double>();
            else if (d["size"].is_string()) {
                try { size_bytes = (uint64_t)std::stoull(d["size"].get<std::string>()); } catch (...) {}
            }
        }

        // Use capped strings consistently in both the object and the index maps
        const std::string name_c = name;
        const std::string path_c = path;

        json one{
            {"path", path_c},
            {"name", name_c},
            {"size_bytes", size_bytes},

            {"model",  jstr_cap(d, "model")},
            {"serial", jstr_cap(d, "serial")},
            {"vendor", jstr_cap(d, "vendor")},
            {"tran",   jstr_cap(d, "tran")},

            {"rota", d.contains("rota") ? d["rota"] : json(nullptr)},
            {"rm",   d.contains("rm")   ? d["rm"]   : json(nullptr)},
            {"mountpoints", mps},
            {"children", children},

            {"fstype", jstr_cap(d, "fstype")},
            {"fsver",  jstr_cap(d, "fsver")},
            {"label",  jstr_cap(d, "label")},
            {"uuid",   jstr_cap(d, "uuid")}
        };

        disks.push_back(one);
        const int idx = (int)disks.size() - 1;

        by_path[path_c] = idx;
        by_name[name_c] = idx;

    }

    return json{{"ok", true}, {"disks", disks}, {"by_path", by_path}, {"by_name", by_name}};
}


// copied transitional helper from main.cpp: storage_btrfs_status_json
[[maybe_unused]] static json storage_btrfs_status_json(const std::string& mountpoint) {
    json j;
    j["ok"] = true;
	j["error"] = nullptr;
    j["btrfs_mount"] = mountpoint;

    std::string show, df, stats;

    int rc_show  = run_btrfs_status_helper_capture("filesystem-show", mountpoint, &show);
    int rc_df    = run_btrfs_status_helper_capture("filesystem-df", mountpoint, &df);
    int rc_stats = run_btrfs_status_helper_capture("device-stats", mountpoint, &stats);

    // Cap outputs
    cap_string(show,  256 * 1024);
    cap_string(df,    256 * 1024);
    cap_string(stats, 256 * 1024);

    j["btrfs_filesystem_show"] = show;
    j["btrfs_filesystem_df"]   = df;
    j["btrfs_device_stats"]    = stats;
    // ---- df_summary (best effort) parsed from "btrfs filesystem df" ----
    // Example lines:
    // "Data, single: total=2.01GiB, used=19.12MiB"
    // "Metadata, DUP: total=1.00GiB, used=1.14MiB"
    json df_summary = json::object();

    {
        size_t pos = 0;
        while (pos < df.size()) {
            size_t end = df.find('\n', pos);
            if (end == std::string::npos) end = df.size();
            std::string line = df.substr(pos, end - pos);
            rtrim_inplace(line);

            std::string name, total_s, used_s;
            uint64_t total_b = 0, used_b = 0;
            if (parse_btrfs_df_line(line, &name, &total_b, &used_b, &total_s, &used_s)) {
                df_summary[name] = json{
                        {"total", total_s},
                        {"used", used_s},
                        {"total_bytes", total_b},
                        {"used_bytes", used_b}
                };
            }

            if (end == df.size()) break;
            pos = end + 1;
        }
    }

    // Always include for stable schema (may be empty)
    j["df_summary"] = df_summary;
    j["rc_show"]  = rc_show;
    j["rc_df"]    = rc_df;
    j["rc_stats"] = rc_stats;

    // ---- summary (best effort) parsed from "btrfs filesystem show" ----
    // Works for lines like:
    //   Label: 'PQNAS_DATA'  uuid: ...
    //   Total devices 1 FS bytes used 20.27MiB
    //   devid 1 size 238.47GiB used 4.02GiB path /dev/nvme0n1p1
    json summary = json::object();

    // label + uuid
    {
        const std::string k1 = "Label:";
        const std::string k2 = "uuid:";
        auto p1 = show.find(k1);
        auto p2 = show.find(k2);
        if (p1 != std::string::npos && p2 != std::string::npos && p2 > p1) {
            std::string label_part = show.substr(p1 + k1.size(), p2 - (p1 + k1.size()));
            // trim label_part
            while (!label_part.empty() && (label_part.front() == ' ' || label_part.front() == '\t')) label_part.erase(label_part.begin());
            while (!label_part.empty() && (label_part.back() == ' ' || label_part.back() == '\t')) label_part.pop_back();

            // label_part often looks like "'PQNAS_DATA'"
            if (!label_part.empty() && label_part.front() == '\'') {
                size_t q = label_part.find('\'', 1);
                if (q != std::string::npos && q > 1) {
                    summary["label"] = label_part.substr(1, q - 1);
                } else {
                    summary["label"] = label_part;
                }
            } else if (!label_part.empty()) {
                summary["label"] = label_part;
            }

            // uuid token until whitespace/newline
            size_t ustart = p2 + k2.size();
            while (ustart < show.size() && (show[ustart] == ' ' || show[ustart] == '\t')) ustart++;
            size_t uend = ustart;
            while (uend < show.size()) {
                char c = show[uend];
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r') break;
                uend++;
            }
            if (uend > ustart) summary["uuid"] = show.substr(ustart, uend - ustart);
        }
    }

    // total devices + FS bytes used
    {
        const std::string key = "Total devices";
        auto p = show.find(key);
        if (p != std::string::npos) {
            // Grab the line
            size_t line_end = show.find('\n', p);
            if (line_end == std::string::npos) line_end = show.size();
            std::string line = show.substr(p, line_end - p);

            // naive token scan
            // "Total devices 1 FS bytes used 20.27MiB"
            size_t td = line.find("Total devices");
            if (td != std::string::npos) {
                size_t pos = td + std::string("Total devices").size();
                while (pos < line.size() && line[pos] == ' ') pos++;
                size_t pos2 = pos;
                while (pos2 < line.size() && line[pos2] >= '0' && line[pos2] <= '9') pos2++;
                if (pos2 > pos) {
                    summary["total_devices"] = std::atoi(line.substr(pos, pos2 - pos).c_str());
                }
            }

            const std::string k_used = "FS bytes used";
            auto pu = line.find(k_used);
            if (pu != std::string::npos) {
                size_t pos = pu + k_used.size();
                while (pos < line.size() && line[pos] == ' ') pos++;
                size_t pos2 = pos;
                while (pos2 < line.size() && line[pos2] != ' ' && line[pos2] != '\t') pos2++;
                if (pos2 > pos) {
                    const std::string tok = line.substr(pos, pos2 - pos);
                    uint64_t bytes = 0;
                    if (parse_human_bytes(tok, &bytes)) {
                        summary["fs_bytes_used"] = tok;
                        summary["fs_bytes_used_bytes"] = bytes;
                    } else {
                        summary["fs_bytes_used"] = tok;
                    }
                }
            }
        }
    }

	// device lines: size/used/path (collect ALL devices)
	{
    	json devices = json::array();

	    const std::string key = "devid";
    	auto p = show.find(key);

    	while (p != std::string::npos) {
        	size_t line_end = show.find('\n', p);
	        if (line_end == std::string::npos) line_end = show.size();
    	    std::string line = show.substr(p, line_end - p);

	        auto ps = line.find("size ");
    	    auto pu = line.find("used ");
        	auto pp = line.find("path ");
	        if (ps != std::string::npos && pu != std::string::npos && pp != std::string::npos) {
    	        // size token
        	    size_t s1 = ps + 5;
	            size_t s2 = line.find(' ', s1);
    	        if (s2 == std::string::npos) s2 = line.size();
        	    std::string size_tok = line.substr(s1, s2 - s1);

	            // used token
    	        size_t u1 = pu + 5;
        	    size_t u2 = line.find(' ', u1);
            	if (u2 == std::string::npos) u2 = line.size();
	            std::string used_tok = line.substr(u1, u2 - u1);

    	        // path token to end
        	    size_t p1 = pp + 5;
            	while (p1 < line.size() && (line[p1] == ' ' || line[p1] == '\t')) p1++;
	            std::string path_tok = (p1 < line.size()) ? line.substr(p1) : std::string();

    	        json one = json::object();

        	    if (!path_tok.empty()) {
            	    one["path"] = path_tok;
                	const std::string parent = parent_disk_from_dev(path_tok);
	                if (!parent.empty()) one["parent_disk"] = parent;
    	        }

        	    if (!size_tok.empty()) {
            	    one["size"] = size_tok;
                	uint64_t bytes = 0;
	                if (parse_human_bytes(size_tok, &bytes)) one["size_bytes"] = bytes;
    	        }

        	    if (!used_tok.empty()) {
            	    one["used"] = used_tok;
                	uint64_t bytes = 0;
	                if (parse_human_bytes(used_tok, &bytes)) one["used_bytes"] = bytes;
    	        }

	            devices.push_back(one);
    	    }

	        if (line_end == show.size()) break;
    	    p = show.find(key, line_end + 1);
    	}

    	// Always include devices array for stable schema
    	summary["devices"] = devices;

    	// Backward compatibility: keep old single-device fields as "first device"
    	if (devices.is_array() && !devices.empty() && devices[0].is_object()) {
        	const auto& d0 = devices[0];

	        if (d0.contains("path"))        summary["device_path"] = d0["path"];
    	    if (d0.contains("parent_disk")) summary["device_parent_disk"] = d0["parent_disk"];

        	if (d0.contains("size"))        summary["device_size"] = d0["size"];
	        if (d0.contains("size_bytes"))  summary["device_size_bytes"] = d0["size_bytes"];

    	    if (d0.contains("used"))        summary["device_used"] = d0["used"];
        	if (d0.contains("used_bytes"))  summary["device_used_bytes"] = d0["used_bytes"];
	    }
	}


    // Always include summary for stable schema (may be empty if parsing failed)
    j["summary"] = summary;

    // ---- usage percent (UI-friendly) ----
    // Prefer filesystem-used vs device-size from the parsed "summary".
    json usage = json::object();

    // overall: FS bytes used / device size
    if (j.contains("summary") && j["summary"].is_object()) {
        const auto& s = j["summary"];
        if (s.contains("fs_bytes_used_bytes") && s.contains("device_size_bytes") &&
            s["fs_bytes_used_bytes"].is_number_unsigned() &&
            s["device_size_bytes"].is_number_unsigned()) {

            const double used = (double)s["fs_bytes_used_bytes"].get<uint64_t>();
            const double size = (double)s["device_size_bytes"].get<uint64_t>();
            if (size > 0.0) {
                double pct = (used * 100.0) / size;
                if (pct < 0.0) pct = 0.0;
                if (pct > 100.0) pct = 100.0;

                usage["used_bytes"] = (uint64_t)used;
                usage["size_bytes"] = (uint64_t)size;
                usage["used_percent"] = pct;
                usage["used_percent_1dp"] = round_dp(pct, 1);
                usage["used_percent_int"] = (int)std::round(pct);

            }
        }
    }

    // data profile: df_summary["Data"] used/total (optional, but useful)
    if (j.contains("df_summary") && j["df_summary"].is_object()) {
        const auto& ds = j["df_summary"];
        if (ds.contains("Data") && ds["Data"].is_object()) {
            const auto& d = ds["Data"];
            if (d.contains("used_bytes") && d.contains("total_bytes") &&
                d["used_bytes"].is_number_unsigned() &&
                d["total_bytes"].is_number_unsigned()) {

                const double used = (double)d["used_bytes"].get<uint64_t>();
                const double total = (double)d["total_bytes"].get<uint64_t>();
                if (total > 0.0) {
                    double pct = (used * 100.0) / total;
                    if (pct < 0.0) pct = 0.0;
                    if (pct > 100.0) pct = 100.0;

                    usage["data_used_bytes"] = (uint64_t)used;
                    usage["data_total_bytes"] = (uint64_t)total;
                    usage["data_used_percent"] = pct;
                    usage["data_used_percent_1dp"] = round_dp(pct, 1);
                    usage["data_used_percent_int"] = (int)std::round(pct);

                }
                }
        }
    }
	// overall: FS bytes used / total device size (multi-device safe)
	if (j.contains("summary") && j["summary"].is_object()) {
    	const auto& s = j["summary"];

	    if (s.contains("fs_bytes_used_bytes") && s["fs_bytes_used_bytes"].is_number_unsigned()) {
    	    const double fs_used = (double)s["fs_bytes_used_bytes"].get<uint64_t>();

        	// Prefer summed size if we have devices[]
        	uint64_t denom_size_u64 = 0;

	        if (s.contains("devices") && s["devices"].is_array()) {
    	        for (const auto& dev : s["devices"]) {
        	        if (!dev.is_object()) continue;
            	    if (dev.contains("size_bytes") && dev["size_bytes"].is_number_unsigned())
                	    denom_size_u64 += dev["size_bytes"].get<uint64_t>();
	            }
    	    }

	        // Fallback: old single-device field
    	    if (denom_size_u64 == 0 &&
        	    s.contains("device_size_bytes") && s["device_size_bytes"].is_number_unsigned()) {
            	denom_size_u64 = s["device_size_bytes"].get<uint64_t>();
        	}

        	if (denom_size_u64 > 0) {
            	const double size = (double)denom_size_u64;
	            double pct = (fs_used * 100.0) / size;
    	        if (pct < 0.0) pct = 0.0;
        	    if (pct > 100.0) pct = 100.0;

	            usage["used_bytes"] = (uint64_t)fs_used;
    	        usage["size_bytes"] = denom_size_u64;           // <-- now correct for multi-device
        	    usage["used_percent"] = pct;
            	usage["used_percent_1dp"] = round_dp(pct, 1);
	            usage["used_percent_int"] = (int)std::round(pct);
    	    }
    	}
	}


	j["usage"] = usage;
    // ---- ok/error classification (fail-closed for "ok") ----
    if (rc_show != 0 || rc_df != 0 || rc_stats != 0) {
        j["ok"] = false;

        // More specific errors for common cases
        if (str_contains(show, "sudo:") || str_contains(df, "sudo:") || str_contains(stats, "sudo:")) {
            j["error"] = "sudo_not_allowed";
        } else if (str_contains(show, "not a valid btrfs filesystem") ||
                   str_contains(df, "not a valid btrfs filesystem") ||
                   str_contains(stats, "not a valid btrfs filesystem")) {
            j["error"] = "not_btrfs";
        } else {
            j["error"] = "btrfs_failed";
        }
    }

    return j;
}


// copied transitional helper from main.cpp: lsblk_disk_mountpoints_json
[[maybe_unused]] static json lsblk_disk_mountpoints_json(const std::string& disk_path) {
    json out;
    out["ok"] = false;
    out["disk"] = disk_path;

    std::string raw;
    // Security: call lsblk via argv directly; disk_path is validated before exec.
    int rc = run_lsblk_json_all_props_argv(disk_path, &raw);

    out["rc"] = rc;

    if (rc != 0 || raw.empty()) {
        out["error"] = "lsblk_failed";
        std::string raw_cap = raw;
        cap_string(raw_cap, 64 * 1024); // only for error/debug payload
        out["raw"] = raw_cap;
        return out;
    }

    // Safety: lsblk for a single disk should be small. If it's unexpectedly huge,
    // fail-closed rather than truncating JSON and mis-parsing.
    if (raw.size() > 2 * 1024 * 1024) { // 2 MiB
        out["error"] = "lsblk_too_large";
        out["raw_bytes"] = (uint64_t)raw.size();
        return out;
    }

    json root;
    try { root = json::parse(raw); }
    catch (...) {
        out["error"] = "lsblk_parse_failed";
        std::string raw_cap = raw;
        cap_string(raw_cap, 64 * 1024); // only for error/debug payload
        out["raw"] = raw_cap;
        return out;
    }

    json mps = json::array();

    if (root.contains("blockdevices") && root["blockdevices"].is_array()) {
        for (const auto& bd : root["blockdevices"]) {
            // Disk node + descendants
            lsblk_collect_mountpoints_recursive(bd, mps);
        }
    }

    // de-dup mountpoints (stable-ish order: first occurrence wins)
    std::set<std::string> seen;
    json uniq = json::array();
    for (const auto& x : mps) {
        if (!x.is_string()) continue;
        std::string s = x.get<std::string>();
        if (s.empty()) continue;
        if (seen.insert(s).second) uniq.push_back(s);
    }

    out["ok"] = true;
    out["mountpoints"] = uniq;
    return out;
}


// copied transitional helper from main.cpp: parse_btrfs_filesystem_show
[[maybe_unused]] static inline void parse_btrfs_filesystem_show(const std::string& out,
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


// copied transitional helper from main.cpp: parse_btrfs_df_profiles
[[maybe_unused]] static inline void parse_btrfs_df_profiles(const std::string& out,
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


// copied transitional helper from main.cpp: parse_btrfs_usage_bytes
[[maybe_unused]] static inline void parse_btrfs_usage_bytes(const std::string& out,
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


// copied transitional helper from main.cpp: btrfs_membership_fingerprint
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


// copied transitional helper from main.cpp: join_commands_for_hash
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


// copied transitional helper from main.cpp: raid_mount_lock_path
[[maybe_unused]] static std::string raid_mount_lock_path(const std::string& resolved_mount) {
    const std::string h = sha256_hex_lower_evp(resolved_mount);
    // Keep filename deterministic and safe even if hashing fails (shouldn't).
    return std::string("/run/pqnas/raid/lock-mount-") + (h.empty() ? "bad" : h) + ".lock";
}


// copied transitional helper from main.cpp: raid_exec_record_path
[[maybe_unused]] static std::string raid_exec_record_path(const std::string& plan_id) {
    return std::string("/run/pqnas/raid/") + plan_id + ".json";
}


// copied transitional helper from main.cpp: raid_exec_record_read
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


// copied transitional helper from main.cpp: raid_exec_record_write_atomic
[[maybe_unused]] static bool raid_exec_record_write_atomic(const std::string& plan_id, const json& rec) {
    if (!is_sha256_hex_lower(plan_id)) return false;
    const std::string path = raid_exec_record_path(plan_id);
    return write_text_file_atomic(path, rec.dump(2) + "\n");
}


// copied transitional helper from main.cpp: raid_exec_record_append_step
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


// copied transitional helper from main.cpp: raid_enqueue_job_fail_closed
[[maybe_unused]] static json raid_enqueue_job_fail_closed(const std::string& plan_id,
                                        const std::string& resolved_mount,
                                        const json& plan,
                                        const json& commands) {
    // Ensure /run/pqnas/raid exists (fail-closed)
    std::string raid_dir_err;
    if (!ensure_dir_fail_closed("/run/pqnas/raid", &raid_dir_err)) {
        throw std::runtime_error("raid_state_dir_failed: " + raid_dir_err);
    }

    // Replay safety:
    // - if canonical exists and state==running => refuse
    // - otherwise archive old canonical to <plan_id>.<ts>.json (best-effort)
    const std::string canonical = raid_exec_record_path(plan_id);
    if (std::filesystem::exists(canonical)) {
        const std::string st = raid_exec_state_best_effort_from_path(canonical);
        if (st == "running") {
            throw std::runtime_error("already_running");
        }
        const std::string ap = raid_exec_record_archive_path_for_plan(plan_id);
        try { std::filesystem::rename(canonical, ap); } catch (...) { /* ignore */ }
    }

	std::string werr;
	if (!raid_write_queued_record_fail_closed(plan_id, resolved_mount, plan, commands, &werr)) {
    	throw std::runtime_error("exec_record_write_failed: " + werr);
	}
    // start worker if needed
    raid_worker_start_once();

    // enqueue
    RaidJob job;
    job.job_id = raid_job_new_id();
    job.plan_id = plan_id;
    job.resolved_mount = resolved_mount;
    job.plan = plan;
    job.commands = commands;

	job.record = raid_exec_record_read_best_effort_obj(plan_id, resolved_mount, plan, commands);

    const std::string job_id = job.job_id;

    {
        std::lock_guard<std::mutex> lk(g_raid_jobs_mu);

        g_raid_job_meta[job_id] = json{
            {"job_id", job_id},
            {"plan_id", plan_id},
            {"resolved_mount", resolved_mount},
            {"record_path", canonical},
            {"state", "queued"},
            {"ts_created", pqnas::now_iso_utc()}
        };

        g_raid_jobs_q.push_back(std::move(job));
    }

    // Ensure worker exists, then wake it. If this is the first job, the
    // predicate in raid_worker_main will see the already-populated queue.
    raid_worker_start_once();
    g_raid_jobs_cv.notify_one();

    return json{
        {"ok", true},
        {"job_id", job_id},
        {"plan_id", plan_id},
        {"record_path", canonical},
        {"state", "queued"}
    };
}


// copied transitional helper from main.cpp: validate_create_pool_devices
[[maybe_unused]] static bool validate_create_pool_devices(
    const json& devices_json,
    const json& disk_inventory,
    std::vector<std::string>& devices_out,
    std::string& err)
{
    devices_out.clear();

    if (!devices_json.is_array() || devices_json.empty()) {
        err = "devices must be a non-empty array";
        return false;
    }

    std::unordered_set<std::string> seen;

    for (const auto& v : devices_json) {
        if (!v.is_string()) {
            err = "device entry must be a string";
            return false;
        }

        std::string dev = httplib::detail::trim_copy(v.get<std::string>());
        if (!is_dev_path_basic_safe(dev)) {
            err = "invalid device path: " + dev;
            return false;
        }

        if (!seen.insert(dev).second) {
            err = "duplicate device: " + dev;
            return false;
        }

        if (!disk_inventory.contains("by_path") || !disk_inventory["by_path"].is_object()) {
            err = "disk inventory missing by_path";
            return false;
        }

        const auto& by_path = disk_inventory["by_path"];
        if (!by_path.contains(dev) || by_path[dev].is_null()) {
            err = "device not found in disk inventory: " + dev;
            return false;
        }

        const auto& dinfo = by_path[dev];

        auto truthy = [&](const char* key) -> bool {
            if (!dinfo.contains(key) || dinfo[key].is_null()) return false;
            const auto& x = dinfo[key];
            if (x.is_boolean()) return x.get<bool>();
            if (x.is_number_integer()) return x.get<long long>() != 0;
            if (x.is_number_unsigned()) return x.get<unsigned long long>() != 0;
            if (x.is_string()) {
                const std::string s = x.get<std::string>();
                return s == "1" || s == "true" || s == "yes";
            }
            return false;
        };

        if (truthy("mounted") ||
            truthy("in_use") ||
            truthy("busy") ||
            truthy("has_children") ||
            truthy("has_partitions")) {
            err = "device is mounted or in use: " + dev;
            return false;
        }

        if (truthy("is_system_disk") ||
            truthy("is_root_disk")) {
            err = "refusing system/root disk: " + dev;
            return false;
        }

        devices_out.push_back(dev);
    }

    std::sort(devices_out.begin(), devices_out.end());
    return true;
}


// copied transitional helper from main.cpp: build_create_pool_commands_json
[[maybe_unused]] static json build_create_pool_commands_json(
    const std::string& pool_id,
    const std::string& mode,
    const std::vector<std::string>& devices,
    bool force)
{
    std::string root = getenv_str("PQNAS_STORAGE_ROOT");
    if (root.empty()) root = "/srv/pqnas";

    const std::string mount = root + "/pools/" + pool_id;

    std::string label_pool_id = pool_id;
    std::transform(label_pool_id.begin(), label_pool_id.end(), label_pool_id.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    const std::string label = "PQNAS_" + label_pool_id;

    json cmds = json::array();

    // Security: emit internal pseudo-commands so the executor routes create-pool
    // root-helper steps through argv, not shell command strings.
    if (force) {
        for (const auto& d : devices) {
            cmds.push_back("RAID_ROOT zap-disk " + d);
            cmds.push_back("RAID_ROOT partprobe " + d);
            cmds.push_back("RAID_ROOT wipefs " + d);
        }
    }

    std::string mkfs = "RAID_ROOT mkfs-btrfs " + mode + " " + label;
    for (const auto& d : devices) mkfs += " " + d;
    cmds.push_back(mkfs);

    cmds.push_back("RAID_ROOT mkdir-p " + mount);
    cmds.push_back("RAID_ROOT mount-label " + label + " " + mount);

    cmds.push_back("RAID_ROOT udev-settle");
    cmds.push_back("RAID_ROOT btrfs-device-scan");
    // Security: use an internal pseudo-command so the executor routes this
    // read-only btrfs-status check through argv, not a shell command string.
    cmds.push_back("BTRFS_STATUS filesystem-show " + mount);
    // hardening: pseudo command keeps fstab helper argv-only.
    cmds.push_back("FSTAB_ADD_BTRFS " + mount);

    const std::string data_dir = mount + "/data";
    cmds.push_back("RAID_ROOT mkdir-p " + data_dir);
    cmds.push_back("RAID_ROOT chown-pqnas " + data_dir);
    cmds.push_back("RAID_ROOT chmod-0755 " + data_dir);

    return cmds;
}


// copied transitional helper from main.cpp: compute_create_pool_plan_id
[[maybe_unused]] static std::string compute_create_pool_plan_id(
    const std::string& plan_nonce,
    const std::string& pool_id,
    const std::string& mode,
    const std::vector<std::string>& devices,
    bool force,
    const json& canonical_commands)
{
    json basis;
    basis["op"] = "create-pool";
    basis["plan_nonce"] = plan_nonce;
    basis["pool_id"] = pool_id;
    basis["mode"] = mode;
    basis["force"] = force;
    basis["devices"] = devices;
    basis["commands"] = canonical_commands;

    return sha256_hex_lower_evp(basis.dump());
}


// copied transitional helper from main.cpp: detect_system_pool_root_disk
[[maybe_unused]] static std::string detect_system_pool_root_disk() {
    std::string root = getenv_str("PQNAS_STORAGE_ROOT");
    if (root.empty()) root = "/srv/pqnas";

    std::string source_out;
    int ec_src = 0;

    ec_src = run_findmnt_no_target_argv("SOURCE", root, &source_out);
    cap_string(source_out, 4096);
    rtrim_inplace(source_out);

    if (ec_src != 0 || source_out.empty()) return "";

    return parent_disk_from_dev(source_out);
}


// copied transitional helper from main.cpp: part1_path_from_disk
[[maybe_unused]] static std::string part1_path_from_disk(const std::string& disk) {
    if (disk.rfind("/dev/", 0) != 0) return "";
    if (disk.find("/dev/nvme") == 0)   return disk + "p1";
    if (disk.find("/dev/mmcblk") == 0) return disk + "p1";
    if (disk.find("/dev/loop") == 0)   return disk + "p1";
    return disk + "1";
}


// copied transitional helper from main.cpp: is_dev_path_basic_safe
[[maybe_unused]] static bool is_dev_path_basic_safe(const std::string& s) {
    if (s.rfind("/dev/", 0) != 0) return false;
    for (char c : s) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') return false;
    }
    if (s.find("..") != std::string::npos) return false;
    return true;
}


// copied transitional helper from main.cpp: is_hex_64_lower_or_upper
[[maybe_unused]] static bool is_hex_64_lower_or_upper(const std::string& s) {
    if (s.size() != 64) return false;
    for (char c : s) {
        if (!is_hex_lower_or_upper(c)) return false;  // uses your char helper at line ~313
    }
    return true;
}


// copied transitional helper from main.cpp: upper_ascii
[[maybe_unused]] static std::string upper_ascii(std::string s) {
    for (char& c : s) {
        if (c >= 'a' && c <= 'z')
            c = (char)(c - ('a' - 'A'));
    }
    return s;
}


// copied transitional helper from main.cpp: to_lower_copy
[[maybe_unused]] static std::string to_lower_copy(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}


// copied transitional helper from main.cpp: write_fd_all
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


// copied transitional helper from main.cpp: ensure_dir_best_effort
[[maybe_unused]] static void ensure_dir_best_effort(const std::string& p) {
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
}


// copied transitional helper from main.cpp: normalize_storage_pool_id
[[maybe_unused]] static std::string normalize_storage_pool_id(std::string v) {
    v = trim_copy(v);
    return v.empty() ? "default" : v;
}


// copied transitional helper from main.cpp: pool_id_from_mount_best_effort
[[maybe_unused]] static std::string pool_id_from_mount_best_effort(const std::string& mount) {
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


// copied transitional helper from main.cpp: load_or_init_pools_cfg
[[maybe_unused]] static json load_or_init_pools_cfg(const std::string& users_path) {
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


// copied transitional helper from main.cpp: parse_btrfs_scrub_status_best_effort
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
        if (b > a) j["uuid"] = pqnas_trim_copy(s.substr(a, b - a));
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
            std::string tok = pqnas_trim_copy(s.substr(a, b - a));
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
            std::string tok = pqnas_trim_copy(s.substr(a, b - a));
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
        if (b > a) j["error_summary"] = pqnas_trim_copy(s.substr(a, b - a));
    }
}

    return j;
}


// -----------------------------------------------------------------------------
// Additional copied transitional storage/RAID symbols from main.cpp.
// TODO: move these into proper storage/raid modules after the split is stable.
// -----------------------------------------------------------------------------



// copied transitional helper from main.cpp: split_lines

[[maybe_unused]] static inline std::vector<std::string> split_lines(const std::string& s) {
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


// copied transitional helper from main.cpp: pqnas_trim_copy

[[maybe_unused]] static inline std::string pqnas_trim_copy(std::string s) {
    rtrim_inplace(s);
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) i++;
    if (i > 0) s.erase(0, i);
    return s;
}


// copied transitional helper from main.cpp: round_dp
[[maybe_unused]] static inline double round_dp(double value, int decimals) {
    if (decimals <= 0) {
        return std::round(value);
    }
    const double scale = std::pow(10.0, decimals);
    return std::round(value * scale) / scale;
}


// copied transitional helper from main.cpp: parse_human_bytes
[[maybe_unused]] static inline bool parse_human_bytes(const std::string& tok, uint64_t* out_bytes) {
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


// copied transitional helper from main.cpp: parse_btrfs_human_bytes_to_u64

[[maybe_unused]] static uint64_t parse_btrfs_human_bytes_to_u64(const std::string& s_in) {
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


// copied transitional helper from main.cpp: parse_btrfs_df_line
[[maybe_unused]] static inline bool parse_btrfs_df_line(const std::string& line,
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


// copied transitional helper from main.cpp: to_lower_ascii_copy

[[maybe_unused]] static inline std::string to_lower_ascii_copy(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    }
    return s;
}


// copied transitional helper from main.cpp: shell_escape_single_quotes

[[maybe_unused]] static std::string shell_escape_single_quotes(std::string s) {
    size_t pos = 0;
    while ((pos = s.find("'", pos)) != std::string::npos) {
        s.replace(pos, 1, "'\\''");
        pos += 4;
    }
    return s;
}


// copied transitional helper from main.cpp: jstr_cap
[[maybe_unused]] static inline std::string jstr_cap(const json& o, const char* k, size_t max_len) {
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


// copied transitional helper from main.cpp: btrfs_filesystem_has_device
[[maybe_unused]] static bool btrfs_filesystem_has_device(const std::string& mount, const std::string& device_path) {
    std::string show;
    int ec = 0;

    const bool ok = run_btrfs_status_helper_argv("filesystem-show", mount, &show, &ec);
    cap_string(show, 256 * 1024);

    if (!ok || ec != 0) return false;
    return show.find(device_path) != std::string::npos;
}


// copied transitional helper from main.cpp: raid_write_queued_record_fail_closed

[[maybe_unused]] static bool raid_write_queued_record_fail_closed(const std::string& plan_id,
                                                 const std::string& resolved_mount,
                                                 const json& plan,
                                                 const json& commands,
                                                 std::string* err_out) {
    const std::string ts0 = pqnas::now_iso_utc();
    const std::string op = plan.is_object() ? plan.value("operation", "") : "";

    json rec = {
        {"ts_start", ts0},
        {"ts_last",  ts0},
        {"ts_end",   nullptr},

        {"plan_id", plan_id},
        {"record_path", raid_exec_record_path(plan_id)}, // canonical
        {"operation", op},        // <-- IMPORTANT
        {"ok", true},             // <-- IMPORTANT (queued == “so far ok”)
        {"state", "queued"},
        {"busy", true},

        {"mount", resolved_mount},
        {"plan", plan},

        {"commands", commands},
        {"step_index", 0},
        {"step_total", commands.is_array() ? (int)commands.size() : 0},
        {"results", json::array()}
    };

    if (!raid_exec_record_write_atomic(plan_id, rec)) {
        if (err_out) *err_out = "raid_exec_record_write_atomic failed";
        return false;
    }
    return true;
}


// copied transitional helper from main.cpp: raid_job_new_id

[[maybe_unused]] static std::string raid_job_new_id() {
    // Prefer your existing random hex helper if you have one.
    // Fallback: SHA256(now+pid+rand) style (best-effort uniqueness).
    std::string seed = pqnas::now_iso_utc();
    seed += "|pid=" + std::to_string((int)getpid());
    seed += "|rnd=" + std::to_string((uint64_t)std::rand());
    std::string h = sha256_hex_lower_evp(seed);
    if (h.size() >= 24) return h.substr(0, 24);
    return h.empty() ? "job_bad" : h;
}


// copied transitional helper from main.cpp: raid_exec_state_best_effort_from_path

[[maybe_unused]] static std::string raid_exec_state_best_effort_from_path(const std::string& path) {
    std::string txt;
    if (!read_text_file(path, &txt)) return "";
    if (txt.size() > (512 * 1024)) txt.resize(512 * 1024);
    try {
        json j = json::parse(txt);
        if (j.contains("state") && j["state"].is_string()) return j["state"].get<std::string>();
    } catch (...) {}
    return "";
}


// copied transitional helper from main.cpp: raid_exec_record_read_best_effort_obj

[[maybe_unused]] static json raid_exec_record_read_best_effort_obj(const std::string& plan_id,
                                                 const std::string& resolved_mount,
                                                 const json& plan,
                                                 const json& commands) {
    json rec = json::object();
    std::string err;
    if (raid_exec_record_read(plan_id, &rec, &err)) return rec;

    // fallback minimal (but UI-consistent)
    const std::string ts0 = pqnas::now_iso_utc();

    const std::string op = plan.is_object() ? plan.value("operation", "") : "";

    rec["plan_id"]      = plan_id;
    rec["record_path"]  = raid_exec_record_path(plan_id); // canonical
    rec["operation"]    = op;            // <-- IMPORTANT: no more null
    rec["ok"]           = true;          // <-- IMPORTANT: boolean, not null
    rec["state"]        = "queued";
    rec["busy"]         = true;

    rec["mount"]        = resolved_mount;
    rec["plan"]         = plan;
    rec["commands"]     = commands;

    rec["step_index"]   = 0;
    rec["step_total"]   = commands.is_array() ? (int)commands.size() : 0;
    rec["results"]      = json::array();

    rec["ts_start"]     = ts0;
    rec["ts_last"]      = ts0;
    rec["ts_end"]       = nullptr;
    return rec;
}


// copied transitional helper from main.cpp: raid_exec_record_archive_path_for_plan

[[maybe_unused]] static std::string raid_exec_record_archive_path_for_plan(const std::string& plan_id) {
    // /run/pqnas/raid/<plan_id>.<timestamp>.json
    std::string ts = pqnas::now_iso_utc(); // e.g. 2026-02-22T02:15:30Z
    for (char& c : ts) {
        if (c == ':' || c == '-') c = '_';
    }
    return raid_exec_record_path(plan_id + "." + ts);
}


// copied transitional helper from main.cpp: is_hex_lower_or_upper


[[maybe_unused]] static bool is_hex_lower_or_upper(char c) {
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}


// -----------------------------------------------------------------------------
// Remaining copied transitional storage/RAID symbols from main.cpp.
// TODO: move these into proper modules after this split builds cleanly.
// -----------------------------------------------------------------------------

// copied transitional struct from main.cpp: BtrfsShowDevice



// copied transitional helper from main.cpp: require_same_origin_for_cookie_mutation
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


// copied transitional helper from main.cpp: btrfs_show_parsed_to_json
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


// copied transitional helper from main.cpp: pools_cfg_path_from_users_path
[[maybe_unused]] static std::filesystem::path pools_cfg_path_from_users_path(const std::string& users_path) {
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





// Transitional overload: copied routes call parse_btrfs_filesystem_show(out)
// and expect BtrfsShowParsed. The older helper only fills label/uuid/device count.
// TODO: replace with the full original parser module after the split is stable.
[[maybe_unused]] static BtrfsShowParsed parse_btrfs_filesystem_show(const std::string& out) {
    BtrfsShowParsed p;
    parse_btrfs_filesystem_show(out, &p.label, &p.uuid, &p.total_devices);
    return p;
}




// copied transitional helper from main.cpp: raid_finalize_record

[[maybe_unused]] static void raid_finalize_record(const std::string& plan_id, json* rec,
                                 bool ok, const std::string& err_msg,
                                 const json* post_status_or_null) {
    if (!rec) return;

    const std::string ts_end = pqnas::now_iso_utc();

    // Always fill the lifecycle fields
    (*rec)["ts_end"]  = ts_end;
    (*rec)["ts_last"] = ts_end;
    (*rec)["busy"]    = false;
    (*rec)["state"]   = ok ? "done" : "failed";

    // FIX: ok must never be null
    (*rec)["ok"] = ok;

    // Keep operation stable (best-effort)
    try {
        if (!rec->contains("operation") || (*rec)["operation"].is_null()) {
            std::string op;
            if (rec->contains("plan") && (*rec)["plan"].is_object()) {
                op = (*rec)["plan"].value("operation", "");
            }
            (*rec)["operation"] = op;
        }
    } catch (...) {
        // ignore
    }

    // Error/post_status hygiene
    if (!ok) {
        // prefer provided error message; otherwise leave any existing error (if present)
        if (!err_msg.empty()) (*rec)["error"] = err_msg;
        else if (!rec->contains("error")) (*rec)["error"] = "failed";

        // On failure, post_status is misleading
        if (rec->contains("post_status")) rec->erase("post_status");
    } else {
        // On success, error is misleading
        if (rec->contains("error")) rec->erase("error");

        if (post_status_or_null) (*rec)["post_status"] = *post_status_or_null;
        else if (rec->contains("post_status")) rec->erase("post_status");
    }

    (void)raid_exec_record_write_atomic(plan_id, *rec);
}



// hardening: argv exec avoids shell parsing.
[[maybe_unused]] static bool run_argv_capture_limited(
    const std::vector<std::string>& argv_s,
    std::string* out,
    int* ec,
    int timeout_ms = 10000,
    std::size_t max_bytes = 64 * 1024)
{
    if (!out || !ec) return false;
    out->clear();
    *ec = 1;

    if (argv_s.empty() || argv_s[0].empty()) {
        *out = "err: empty argv\n";
        *ec = 2;
        return true;
    }

    int pipefd[2] = {-1, -1};
    if (::pipe(pipefd) != 0) {
        *out = std::string("err: pipe failed: ") + std::strerror(errno) + "\n";
        *ec = 1;
        return true;
    }

    pid_t pid = ::fork();
    if (pid < 0) {
        const int e = errno;
        ::close(pipefd[0]);
        ::close(pipefd[1]);
        *out = std::string("err: fork failed: ") + std::strerror(e) + "\n";
        *ec = 1;
        return true;
    }

    if (pid == 0) {
        ::close(pipefd[0]);
        ::dup2(pipefd[1], STDOUT_FILENO);
        ::dup2(pipefd[1], STDERR_FILENO);
        ::close(pipefd[1]);

        std::vector<char*> argv;
        argv.reserve(argv_s.size() + 1);
        for (const auto& a : argv_s) {
            argv.push_back(const_cast<char*>(a.c_str()));
        }
        argv.push_back(nullptr);

        ::execv(argv[0], argv.data());
        _exit(127);
    }

    ::close(pipefd[1]);

    const int flags = ::fcntl(pipefd[0], F_GETFL, 0);
    if (flags >= 0) {
        (void)::fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);
    }

    bool truncated = false;

    auto append_bytes = [&](const char* buf, ssize_t n) {
        if (n <= 0) return;
        const std::size_t have = out->size();
        if (have < max_bytes) {
            const std::size_t room = max_bytes - have;
            const std::size_t take = (static_cast<std::size_t>(n) < room)
                ? static_cast<std::size_t>(n)
                : room;
            out->append(buf, take);
        }
        if (out->size() >= max_bytes && static_cast<std::size_t>(n) > 0) {
            truncated = true;
        }
    };

    auto drain = [&]() {
        char buf[4096];
        for (;;) {
            const ssize_t n = ::read(pipefd[0], buf, sizeof(buf));
            if (n > 0) {
                append_bytes(buf, n);
                continue;
            }
            if (n == 0) return;
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            return;
        }
    };

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    int status = 0;

    for (;;) {
        drain();

        const pid_t w = ::waitpid(pid, &status, WNOHANG);
        if (w == pid) break;

        if (w < 0 && errno != EINTR) {
            *ec = 1;
            break;
        }

        if (std::chrono::steady_clock::now() >= deadline) {
            // hardenin: timeout caps external helper runtime.
            (void)::kill(pid, SIGKILL);
            (void)::waitpid(pid, &status, 0);
            drain();
            ::close(pipefd[0]);
            if (truncated) out->append("\n[output truncated]\n");
            out->append("err: command timed out\n");
            *ec = 124;
            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    drain();
    ::close(pipefd[0]);

    if (truncated) out->append("\n[output truncated]\n");

    if (WIFEXITED(status)) {
        *ec = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        *ec = 128 + WTERMSIG(status);
    } else {
        *ec = 1;
    }

    return true;
}


// copied transitional helpr from main.cpp: raid_run_one_step

[[maybe_unused]] static bool raid_run_one_step(const std::string& cmd,
                              const std::string& users_path,
                              std::string* out,
                              int* ec) {
    if (!out || !ec) return false;
    out->clear();
    *ec = 0;

    // Pseudo-command: WAIT_BLOCK <path> <timeout_ms>
    if (cmd.rfind("WAIT_BLOCK ", 0) == 0) {
        std::istringstream iss(cmd);
        std::string tag, path;
        int timeout_ms = 0;
        iss >> tag >> path >> timeout_ms;

        if (path.empty() || timeout_ms <= 0) {
            *out = "err: WAIT_BLOCK format is: WAIT_BLOCK <path> <timeout_ms>\n";
            *ec = 2;
            return true;
        }

        auto is_block = [](const std::string& p) -> bool {
            struct stat st;
            if (::stat(p.c_str(), &st) != 0) return false;
            return S_ISBLK(st.st_mode);
        };

        const int sleep_step_ms = 50;
        int waited = 0;
        while (waited < timeout_ms) {
            if (is_block(path)) {
                *out = "ok: block device present: " + path + "\n";
                *ec = 0;
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_step_ms));
            waited += sleep_step_ms;
        }

        *out = "err: timed out waiting for block device: " + path + "\n";
        *ec = 1;
        return true;
    }
    // Pseudo-command: POOLS_CFG_REMOVE <mount>
    // Removes mount entry from pools.json so /api/v4/storage/pools stops listing it.
    if (cmd.rfind("POOLS_CFG_REMOVE ", 0) == 0) {
        const std::string mount = trim_copy(cmd.substr(std::string("POOLS_CFG_REMOVE ").size()));
        if (mount.empty()) {
            *out = "err: POOLS_CFG_REMOVE format is: POOLS_CFG_REMOVE <mount>\n";
            *ec = 2;
            return true;
        }

        try {
            json cfg = load_or_init_pools_cfg(users_path);

            if (!cfg.contains("names_by_mount") || !cfg["names_by_mount"].is_object())
                cfg["names_by_mount"] = json::object();
            const bool had_legacy = cfg["names_by_mount"].contains(mount);
            cfg["names_by_mount"].erase(mount);

            if (!cfg.contains("pools") || !cfg["pools"].is_object())
                cfg["pools"] = json::object();
            const bool had_v2 = cfg["pools"].contains(mount);
            cfg["pools"].erase(mount);

            const auto cfg_path = pools_cfg_path_from_users_path(users_path);
            if (!write_text_file_atomic(cfg_path.string(), cfg.dump(2) + "\n")) {
                *out = "err: failed to write pools.json\n";
                *ec = 1;
                return true;
            }

            *out = std::string("ok: pools.json updated (removed mount=") + mount +
                   ", legacy=" + (had_legacy ? "yes" : "no") +
                   ", v2=" + (had_v2 ? "yes" : "no") + ")\n";
            *ec = 0;
            return true;
        } catch (const std::exception& e) {
            *out = std::string("err: POOLS_CFG_REMOVE exception: ") + e.what() + "\n";
            *ec = 1;
            return true;
        }
    }
    // Pseudo-command: FSTAB_ADD_BTRFS <mount>
    // hardening: narrow root helper runs through argv, not shell.
    if (cmd.rfind("FSTAB_ADD_BTRFS ", 0) == 0) {
        const std::string mount = trim_copy(cmd.substr(std::string("FSTAB_ADD_BTRFS ").size()));
        if (mount.empty() || !is_abs_path_safe(mount)) {
            *out = "err: FSTAB_ADD_BTRFS format is: FSTAB_ADD_BTRFS <mount>\n";
            *ec = 2;
            return true;
        }

        const std::string pools_root = "/srv/pqnas/pools/";
        if (mount.rfind(pools_root, 0) != 0 ||
            mount.find('/', pools_root.size()) != std::string::npos) {
            // hardening: reject traversal before root helper.
            *out = "err: FSTAB_ADD_BTRFS only allows /srv/pqnas/pools/<pool_id>\n";
            *ec = 2;
            return true;
        }

        return run_argv_capture_limited({
            "/usr/bin/sudo",
            "-n",
            "/usr/local/sbin/pqnas-fstab-add-btrfs",
            mount
        }, out, ec, 10000, 64 * 1024);
    }

    // Pseudo-command: RAID_ROOT <action> [args...]
    // Security: create-pool plan root-helper steps route through argv, not
    // shell command strings, so devices and mounts cannot be shell-interpreted.
    if (cmd.rfind("RAID_ROOT ", 0) == 0) {
        const std::string tail = trim_copy(cmd.substr(std::string("RAID_ROOT ").size()));
        if (tail.empty()) {
            *out = "err: RAID_ROOT format is: RAID_ROOT <action> [args...]\n";
            *ec = 2;
            return true;
        }

        std::istringstream iss(tail);
        std::vector<std::string> args;
        std::string arg;
        while (iss >> arg) {
            args.push_back(arg);
        }

        if (!raid_root_args_are_supported(args)) {
            *out = "err: unsupported RAID_ROOT command\n";
            *ec = 2;
            return true;
        }

        std::vector<std::string> argv = {
            "/usr/bin/sudo",
            "-n",
            "/usr/local/sbin/pqnas-raid-root"
        };
        argv.insert(argv.end(), args.begin(), args.end());

        return run_argv_capture_limited(
            argv,
            out,
            ec,
            24 * 60 * 60 * 1000,
            2u * 1024u * 1024u
        );
    }

    // Pseudo-command: BTRFS_STATUS <action> <mount>
    // Security: read-only btrfs-status checks route through argv, not a shell
    // command string, so plan mounts cannot be shell-interpreted.
    if (cmd.rfind("BTRFS_STATUS ", 0) == 0) {
        const std::string rest = trim_copy(cmd.substr(std::string("BTRFS_STATUS ").size()));
        const size_t sp = rest.find(' ');
        if (sp == std::string::npos) {
            *out = "err: BTRFS_STATUS format is: BTRFS_STATUS <action> <mount>\n";
            *ec = 2;
            return true;
        }

        const std::string action = trim_copy(rest.substr(0, sp));
        const std::string mount = trim_copy(rest.substr(sp + 1));
        if (action.empty() || mount.empty() || !is_abs_path_safe(mount)) {
            *out = "err: BTRFS_STATUS format is: BTRFS_STATUS <action> <mount>\n";
            *ec = 2;
            return true;
        }

        std::string status_out;
        const int rc = run_btrfs_status_helper_capture(action, mount, &status_out);
        *out = status_out;
        *ec = rc;
        return true;
    }

    // Pseudo-command: FSTAB_REMOVE <mount>
    // Removes only /etc/fstab rows whose second field exactly matches the mount.
    // The privileged edit is performed by a guarded root helper.
    if (cmd.rfind("FSTAB_REMOVE ", 0) == 0) {
        const std::string mount = trim_copy(cmd.substr(std::string("FSTAB_REMOVE ").size()));
        if (mount.empty() || !is_abs_path_safe(mount)) {
            *out = "err: FSTAB_REMOVE format is: FSTAB_REMOVE <mount>\n";
            *ec = 2;
            return true;
        }

        const std::string pools_root = "/srv/pqnas/pools/";
        if (mount.rfind(pools_root, 0) != 0 ||
            mount.find('/', pools_root.size()) != std::string::npos) {
            *out = "err: FSTAB_REMOVE only allows /srv/pqnas/pools/<pool_id>\n";
            *ec = 2;
            return true;
        }

        // hardening: narrow root helper runs through argv, not shell.
        return run_argv_capture_limited({
            "/usr/bin/sudo",
            "-n",
            "/usr/local/sbin/pqnas-fstab-remove",
            mount
        }, out, ec, 10000, 64 * 1024);
    }

    // Pseudo-command: POOLS_CFG_SET_MODE <mount> <mode>
    // Updates pools.json only after a successful storage operation.
    if (cmd.rfind("POOLS_CFG_SET_MODE ", 0) == 0) {
        std::istringstream iss(cmd);
        std::string tag, mount, mode;
        iss >> tag >> mount >> mode;

        if (mount.empty() || mode.empty()) {
            *out = "err: POOLS_CFG_SET_MODE format is: POOLS_CFG_SET_MODE <mount> <mode>\n";
            *ec = 2;
            return true;
        }

        if (mode != "single" && mode != "raid1") {
            *out = "err: POOLS_CFG_SET_MODE invalid mode: " + mode + "\n";
            *ec = 2;
            return true;
        }

        try {
            json cfg = load_or_init_pools_cfg(users_path);

            if (!cfg.contains("pools") || !cfg["pools"].is_object()) {
                cfg["pools"] = json::object();
            }

            if (!cfg["pools"].contains(mount) || !cfg["pools"][mount].is_object()) {
                *out = "err: pool not found in pools.json: " + mount + "\n";
                *ec = 1;
                return true;
            }

            cfg["pools"][mount]["mode"] = mode;
            cfg["pools"][mount]["updated_ts"] = pqnas::now_iso_utc();

            const auto cfg_path = pools_cfg_path_from_users_path(users_path);
            if (!write_text_file_atomic(cfg_path.string(), cfg.dump(2) + "\n")) {
                *out = "err: failed to write pools.json\n";
                *ec = 1;
                return true;
            }

            *out = "ok: pools.json mode updated mount=" + mount + " mode=" + mode + "\n";
            *ec = 0;
            return true;
        } catch (const std::exception& e) {
            *out = std::string("err: POOLS_CFG_SET_MODE exception: ") + e.what() + "\n";
            *ec = 1;
            return true;
        }
    }

    // normal command
    const bool ran = run_cmd_capture(cmd, out, ec);
    return ran;
}

// copied transitional helper from main.cpp: raid_worker_main
static void raid_worker_main(std::string users_path) {
    // ---- worker audit helper (best-effort; no HTTP request context) ----
    auto raid_worker_audit = [&](const std::string& event,
                                 const std::string& outcome,
                                 const RaidJob& job,
                                 const json& extra = json::object()) {
        try {
            pqnas::AuditEvent ev;
            ev.event = event;
            ev.outcome = outcome;

            // actor fingerprint comes from the queued plan (set by execute endpoints)
            try {
                if (job.plan.is_object()) {
                    const std::string actor_fp = job.plan.value("actor_fp", "");
                    if (!actor_fp.empty()) ev.f["fingerprint"] = actor_fp;
                }
            } catch (...) {}

            ev.f["ip"] = "local"; // worker thread
            ev.f["job_id"] = job.job_id;
            ev.f["plan_id"] = job.plan_id;
            ev.f["mount"] = job.resolved_mount;

            // optional: operation name
            try {
                if (job.plan.is_object()) {
                    const std::string op = job.plan.value("operation", "");
                    if (!op.empty()) ev.f["op"] = pqnas::shorten(op, 64);
                }
            } catch (...) {}

            // merge extra fields as x_*
            if (extra.is_object()) {
                for (auto it = extra.begin(); it != extra.end(); ++it) {
                    const std::string k  = pqnas::shorten(it.key(), 64);
                    const std::string kk = "x_" + k;

                    if (it.value().is_string()) ev.f[kk] = pqnas::shorten(it.value().get<std::string>(), 220);
                    else if (it.value().is_number_integer() || it.value().is_number_unsigned()) ev.f[kk] = it.value().dump();
                    else if (it.value().is_boolean()) ev.f[kk] = (it.value().get<bool>() ? "true" : "false");
                    else ev.f[kk] = pqnas::shorten(it.value().dump(), 220);
                }
            }

            // IMPORTANT: audit_append wrapper already calls maybe_auto_rotate_before_append()
            audit_append(ev);
        } catch (...) {
            // never let audit failures break the worker
        }
    };

    for (;;) {
        RaidJob job;

        {
            std::unique_lock<std::mutex> lk(g_raid_jobs_mu);
            g_raid_jobs_cv.wait(lk, [&] { return g_raid_worker_stop.load() || !g_raid_jobs_q.empty(); });
            if (g_raid_worker_stop.load()) return;

            job = std::move(g_raid_jobs_q.front());
            g_raid_jobs_q.pop_front();

            // mark running in meta
            g_raid_job_meta[job.job_id]["state"] = "running";
            g_raid_job_meta[job.job_id]["ts_started"] = pqnas::now_iso_utc();
        }

        // ---------------- FIX: normalize record fields so they never end up null ----------------
        {
            // plan_id / mount are useful invariants
            job.record["plan_id"] = job.plan_id;
            job.record["mount"]   = job.resolved_mount;

            // operation should be a string (not null)
            if (!job.record.contains("operation") || job.record["operation"].is_null()) {
                const std::string op = job.plan.is_object() ? job.plan.value("operation", "") : "";
                job.record["operation"] = op;
            }

            // ok should be a boolean, never null
            if (!job.record.contains("ok") || job.record["ok"].is_null()) {
                job.record["ok"] = true; // "so far ok" while running/queued
            }
        }
        // --------------------------------------------------------------------------------------

        // Move record to running (without adding a fake step)
        job.record["state"]   = "running";
        job.record["busy"]    = true;
        job.record["ts_last"] = pqnas::now_iso_utc();
        (void)raid_exec_record_write_atomic(job.plan_id, job.record);

        // Audit: job start (best-effort)
        raid_worker_audit("v4.raid_job_start_ok", "ok", job, json{
            {"state", "running"},
            {"commands_total", (job.commands.is_array() ? (int)job.commands.size() : 0)}
        });

        // Acquire per-mount lock inside worker (so HTTP returns immediately)
        int fd_mount_lock = -1;
        std::string mount_lockp = raid_mount_lock_path(job.resolved_mount);
        {
            std::string mount_lock_err;
            fd_mount_lock = open_excl_lockfile(mount_lockp, &mount_lock_err);
            if (fd_mount_lock < 0) {
                // FIX: ensure record shows failure even if raid_finalize_record forgets to set ok
                job.record["ok"] = false;

                raid_finalize_record(job.plan_id, &job.record, false,
                                     "raid_busy: another raid operation is in progress for this mount",
                                     nullptr);

                // Audit: failed to start due to lock contention
                raid_worker_audit("v4.raid_job_start_fail", "fail", job, json{
                    {"reason", "raid_busy"},
                    {"detail", pqnas::shorten(mount_lock_err, 220)}
                });

                std::lock_guard<std::mutex> lk(g_raid_jobs_mu);
                g_raid_job_meta[job.job_id]["state"] = "failed";
                g_raid_job_meta[job.job_id]["error"] = "raid_busy";
                g_raid_job_meta[job.job_id]["ts_finished"] = pqnas::now_iso_utc();
                continue;
            }
        }

        auto close_mount_lock = [&]() {
            if (fd_mount_lock >= 0) { ::close(fd_mount_lock); fd_mount_lock = -1; }
            if (!mount_lockp.empty()) (void)std::filesystem::remove(mount_lockp);
        };

        bool all_ok = true;
        json results = json::array();

        const int total = job.commands.is_array() ? (int)job.commands.size() : 0;

        for (int i = 0; i < total; i++) {
            if (!job.commands[i].is_string()) continue;
            const std::string cmd = job.commands[i].get<std::string>();

            std::string out;
            int ec = 0;
            const bool ran = raid_run_one_step(cmd, users_path, &out, &ec);
            const bool step_ok = ran && (ec == 0);

            cap_string(out, 128 * 1024);

            json one = {
                {"i", i},
                {"cmd", cmd},
                {"rc", ec},
                {"ok", step_ok},
                {"out", out}
            };
            results.push_back(one);

            raid_exec_record_append_step(&job.record, i + 1, total, cmd, step_ok, ec, out);
            (void)raid_exec_record_write_atomic(job.plan_id, job.record);

            if (!step_ok) { all_ok = false; break; }
        }

        // finalize (+ post_status if ok and mount still exists)
        if (all_ok) {
            json* pst = nullptr;
            json status;

            std::error_code ec;
            if (std::filesystem::exists(job.resolved_mount, ec) && !ec) {
                status = storage_btrfs_status_json(job.resolved_mount);
                status["resolved_mount"] = job.resolved_mount;
                pst = &status;
            }

            // FIX: ensure ok becomes true at least here
            job.record["ok"] = true;

            raid_finalize_record(job.plan_id, &job.record, true, "", pst);
        } else {
            std::string emsg = "command failed";
            try {
                if (!results.empty() && results.back().is_object()) {
                    emsg = std::string("step failed: ") + results.back().value("cmd","") +
                           " rc=" + std::to_string(results.back().value("rc", -1));
                }
            } catch (...) {}

            // FIX: ensure ok becomes false at least here
            job.record["ok"] = false;

            raid_finalize_record(job.plan_id, &job.record, false, emsg, nullptr);
        }

        // Audit: job completion (best-effort)
        if (all_ok) {
            raid_worker_audit("v4.raid_job_finish_ok", "ok", job, json{
                {"commands_total", total}
            });
        } else {
            std::string last_cmd = "";
            int last_rc = -1;
            try {
                if (!results.empty() && results.back().is_object()) {
                    last_cmd = results.back().value("cmd", "");
                    last_rc  = results.back().value("rc", -1);
                }
            } catch (...) {}

            raid_worker_audit("v4.raid_job_finish_fail", "fail", job, json{
                {"reason", "command_failed"},
                {"commands_total", total},
                {"last_cmd", pqnas::shorten(last_cmd, 160)},
                {"last_rc", last_rc}
            });
        }

        close_mount_lock();

        {
            std::lock_guard<std::mutex> lk(g_raid_jobs_mu);
            g_raid_job_meta[job.job_id]["state"] = all_ok ? "done" : "failed";
            g_raid_job_meta[job.job_id]["ts_finished"] = pqnas::now_iso_utc();
        }
    }
}


// copied transitional helper from main.cpp: raid_worker_start_once
[[maybe_unused]] static void raid_worker_start_once() {
    static std::once_flag once;
    std::call_once(once, [] {
        g_raid_worker_stop.store(false);
        g_raid_worker_thr = std::thread(raid_worker_main, g_users_path_for_raid);

        std::atexit([] {
            g_raid_worker_stop.store(true);
            g_raid_jobs_cv.notify_all();
            if (g_raid_worker_thr.joinable()) {
                g_raid_worker_thr.join();
            }
        });
    });
}

// ----- Pool layout diff helpers ------------------------------------------------
// Layout apply must compare saved slots against runtime btrfs members by identity,
// not only by one /dev/... string. The same disk may appear as /dev/sda,
// /dev/sda1, runtime_dev, parent_disk, by-id, or by-path depending on the source.
static std::string poolmgr_json_string_trim(const json& j, const char* key) {
    if (!j.is_object() || !j.contains(key) || !j[key].is_string()) return {};
    return trim_copy(j[key].get<std::string>());
}

static void poolmgr_add_identity_value(std::set<std::string>& vals, const std::string& v) {
    const std::string t = trim_copy(v);
    if (!t.empty()) vals.insert(t);
}

static std::set<std::string> poolmgr_slot_identity_values(const json& slot) {
    std::set<std::string> vals;
    if (!slot.is_object()) return vals;

    const char* keys[] = {
        "device",
        "runtime_dev",
        "by_id",
        "by_path",
        "disk_id",
        "id_serial_short"
    };

    for (const char* k : keys) {
        poolmgr_add_identity_value(vals, poolmgr_json_string_trim(slot, k));
    }

    return vals;
}

static std::set<std::string> poolmgr_runtime_identity_values(const json& member) {
    std::set<std::string> vals;
    if (!member.is_object()) return vals;

    const char* keys[] = {
        "path",
        "parent_disk",
        "runtime_dev",
        "device",
        "by_id",
        "by_path",
        "disk_id",
        "id_serial_short"
    };

    for (const char* k : keys) {
        poolmgr_add_identity_value(vals, poolmgr_json_string_trim(member, k));
    }

    return vals;
}

static bool poolmgr_slot_matches_runtime_member(const json& slot, const json& member) {
    const auto a = poolmgr_slot_identity_values(slot);
    const auto b = poolmgr_runtime_identity_values(member);

    for (const auto& v : a) {
        if (b.find(v) != b.end()) return true;
    }

    return false;
}

static std::string poolmgr_desired_slot_device(const json& slot) {
    std::string dev = poolmgr_json_string_trim(slot, "runtime_dev");
    if (dev.empty()) dev = poolmgr_json_string_trim(slot, "device");
    return dev;
}

static std::string poolmgr_runtime_member_device(const json& member) {
    std::string dev = poolmgr_json_string_trim(member, "parent_disk");
    if (dev.empty()) dev = poolmgr_json_string_trim(member, "runtime_dev");
    if (dev.empty()) dev = poolmgr_json_string_trim(member, "path");
    if (dev.empty()) dev = poolmgr_json_string_trim(member, "device");
    return dev;
}

static std::vector<json> poolmgr_runtime_member_infos_from_show(const json& show_j) {
    std::vector<json> out;
    std::set<std::string> seen;

    auto push_member = [&](json one) {
        if (!one.is_object()) return;

        std::string chosen = poolmgr_runtime_member_device(one);
        if (chosen.empty()) return;

        // Normalize partitions to parent disks for layout matching.
        const std::string parent = parent_disk_from_dev(chosen);
        if (!parent.empty()) {
            chosen = parent;
        }

        one["runtime_dev"] = chosen;
        one["device"] = chosen;
        one["parent_disk"] = chosen;

        if (seen.insert(chosen).second) {
            out.push_back(std::move(one));
        }
    };

    if (!show_j.is_object()) return out;

    // Shape 1: direct devices[]
    if (show_j.contains("devices") && show_j["devices"].is_array()) {
        for (const auto& d : show_j["devices"]) {
            if (d.is_object()) push_member(d);
        }
    }

    // Shape 2: summary.devices[]
    if (show_j.contains("summary") &&
        show_j["summary"].is_object() &&
        show_j["summary"].contains("devices") &&
        show_j["summary"]["devices"].is_array()) {
        for (const auto& d : show_j["summary"]["devices"]) {
            if (d.is_object()) push_member(d);
        }
    }

    // Shape 3: raw btrfs filesystem show output.
    // Example:
    //   devid    1 size 57.67GiB used 2.02GiB path /dev/sda
    if (show_j.contains("btrfs_filesystem_show") && show_j["btrfs_filesystem_show"].is_string()) {
        const std::string raw = show_j["btrfs_filesystem_show"].get<std::string>();
        size_t pos = 0;

        while (pos < raw.size()) {
            size_t endline = raw.find('\n', pos);
            if (endline == std::string::npos) endline = raw.size();

            std::string line = trim_copy(raw.substr(pos, endline - pos));

            if (line.find("devid") != std::string::npos) {
                const std::string key = "path ";
                size_t pp = line.find(key);
                if (pp != std::string::npos) {
                    std::string path = trim_copy(line.substr(pp + key.size()));
                    if (!path.empty()) {
                        json one = json::object();
                        one["path"] = path;
                        one["runtime_dev"] = path;
                        const std::string parent = parent_disk_from_dev(path);
                        if (!parent.empty()) one["parent_disk"] = parent;
                        push_member(one);
                    }
                }
            }

            if (endline == raw.size()) break;
            pos = endline + 1;
        }
    }

    return out;
}

static void poolmgr_compute_layout_diff_identity_aware(
    const json& cfg_pool,
    const json& show_j,
    std::vector<std::string>* runtime_members_out,
    std::set<std::string>* desired_set_out,
    std::vector<std::string>* to_add_out,
    std::vector<std::string>* to_remove_out
) {
    if (runtime_members_out) runtime_members_out->clear();
    if (desired_set_out) desired_set_out->clear();
    if (to_add_out) to_add_out->clear();
    if (to_remove_out) to_remove_out->clear();

    std::vector<json> desired_slots;

    if (cfg_pool.contains("slots") && cfg_pool["slots"].is_array()) {
        for (const auto& s : cfg_pool["slots"]) {
            if (!s.is_object()) continue;

            const std::string dev = poolmgr_desired_slot_device(s);
            if (dev.empty()) continue;

            desired_slots.push_back(s);
            if (desired_set_out) desired_set_out->insert(dev);
        }
    }

    const std::vector<json> runtime_infos = poolmgr_runtime_member_infos_from_show(show_j);
    std::set<size_t> matched_runtime;

    for (const auto& rt : runtime_infos) {
        const std::string dev = poolmgr_runtime_member_device(rt);
        if (!dev.empty() && runtime_members_out) {
            runtime_members_out->push_back(dev);
        }
    }

    for (const auto& slot : desired_slots) {
        const std::string wanted = poolmgr_desired_slot_device(slot);
        bool matched = false;

        for (size_t i = 0; i < runtime_infos.size(); ++i) {
            if (matched_runtime.find(i) != matched_runtime.end()) continue;

            if (poolmgr_slot_matches_runtime_member(slot, runtime_infos[i])) {
                matched_runtime.insert(i);
                matched = true;
                break;
            }
        }

        if (!matched && !wanted.empty() && to_add_out) {
            to_add_out->push_back(wanted);
        }
    }

    for (size_t i = 0; i < runtime_infos.size(); ++i) {
        if (matched_runtime.find(i) != matched_runtime.end()) continue;

        const std::string dev = poolmgr_runtime_member_device(runtime_infos[i]);
        if (!dev.empty() && to_remove_out) {
            to_remove_out->push_back(dev);
        }
    }
}

void register_storage_raid_routes(
    httplib::Server& srv,
    const StorageRaidRoutesContext& ctx
) {
    static const unsigned char* COOKIE_KEY = nullptr;
    static std::string users_path;
    static std::string workspaces_path;
    static pqnas::WorkspacesRegistry workspaces;

    COOKIE_KEY = ctx.cookie_key;
    users_path = ctx.users_path;
    workspaces_path = ctx.workspaces_path;

    g_storage_raid_audit_append = ctx.audit_append;
    g_users_path_for_raid = users_path;

    static auto audit_append = [](const pqnas::AuditEvent& ev) {
        if (g_storage_raid_audit_append) {
            g_storage_raid_audit_append(ev);
        }
    };

    if (!COOKIE_KEY || users_path.empty() || workspaces_path.empty()) {
        throw std::runtime_error("StorageRaidRoutesContext missing required fields");
    }


srv.Get("/api/v4/storage/disks", [&](const httplib::Request& req, httplib::Response& res) {
pqnas::UsersRegistry users;

// IMPORTANT: load users from disk before checking admin role
if (!users.load(users_path)) {
    reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}, {"path", users_path}}.dump());
    return;
}

if (!require_admin_cookie_users(req, res, COOKIE_KEY, users_path, &users)) return;


	std::string raw;
	json j = storage_list_disks_json(&raw);

	// Optional: include raw lsblk JSON for debugging (cap size to avoid huge responses)
	if (getenv_bool("PQNAS_STORAGE_DEBUG_LSBLK", false)) {
    	if (raw.size() > 1024 * 1024) raw.resize(1024 * 1024); // 1 MiB cap
    	j["lsblk_raw"] = raw;
	}


    reply_json(res, 200, j.dump());
});

// ----- GET /api/v4/storage/status?mount=/path (admin-only) -------------------
srv.Get("/api/v4/storage/status", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;

    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}, {"path", users_path}}.dump());
        return;
    }

    if (!require_admin_cookie_users(req, res, COOKIE_KEY, users_path, &users)) return;


    // Default mount: prefer configured storage root
    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";

    // default mount inside allowed_prefix
    std::string mount = allowed_prefix + "/data";

    // override if caller provided mount param
    if (req.has_param("mount")) {
        mount = req.get_param_value("mount");
    }


    if (!is_abs_path_safe(mount)) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mount"}}.dump());
        return;
    }

    // --- Resolve mountpoint + fstype first (must happen before running btrfs) ---
    std::string fs_target_out;
    // Security: call findmnt via argv directly, not through shell strings.
    int rc_target = run_findmnt_no_target_argv("TARGET", mount, &fs_target_out);
    cap_string(fs_target_out, 16 * 1024);
    rtrim_inplace(fs_target_out);

    std::string fstype_out;
    int rc_fs = run_findmnt_no_target_argv("FSTYPE", mount, &fstype_out);
    cap_string(fstype_out, 16 * 1024);
    rtrim_inplace(fstype_out);

    std::string source_out;
    int rc_src = run_findmnt_no_target_argv("SOURCE", mount, &source_out);
    cap_string(source_out, 16 * 1024);
    rtrim_inplace(source_out);

    if (rc_target != 0 || fs_target_out.empty() ||
        rc_fs != 0 || fstype_out.empty() ||
        rc_src != 0 || source_out.empty()) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "mount_not_found"},
            {"mount", mount}
        }.dump());
        return;
    }


    // Enforce allowlist on the *resolved mountpoint* (not the user-provided directory)
    const std::string resolved_mount = fs_target_out;
    const std::string resolved_source = source_out;


    if (resolved_mount.rfind(allowed_prefix, 0) != 0) {
        const std::string test_prefix  = "/srv/pqnas-test";
        const std::string test_prefix2 = "/srv/pqnas-test-btrfs";
        if (resolved_mount.rfind(test_prefix, 0) != 0 && resolved_mount.rfind(test_prefix2, 0) != 0) {
            reply_json(res, 403, json{
                {"ok", false},
                {"error", "mount_not_allowed"},
                {"allowed_prefix", allowed_prefix},
                {"resolved_mount", resolved_mount},
                {"resolved_source", resolved_source}
            }.dump());

            return;
        }
    }

    if (fstype_out != "btrfs") {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "not_btrfs"},
            {"mount", mount},
            {"resolved_mount", resolved_mount},
            {"resolved_source", resolved_source},
            {"fstype", fstype_out}
        }.dump());

        return;
    }

    // Run btrfs commands against the resolved mountpoint (fixes /srv/pqnas/data case)
    json j = storage_btrfs_status_json(resolved_mount);
    j["input_mount"] = mount;
    j["resolved_mount"] = resolved_mount;
    j["resolved_source"] = resolved_source;
    {
    const std::string d = parent_disk_from_dev(resolved_source);
    if (!d.empty()) j["resolved_disk"] = d;
    }
    j["fstype"] = fstype_out;
    reply_json(res, 200, j.dump());


});

// ----- GET /api/v4/storage/pools (admin-only, read-only) -------------------
srv.Get("/api/v4/storage/pools", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;

    if (!users.load(users_path)) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "users_load_failed"},
            {"path", users_path}
        }.dump());
        return;
    }

    if (!require_admin_cookie_users(req, res, COOKIE_KEY, users_path, &users)) return;

    json pools_cfg = pqnas::load_or_init_pools_cfg_v3(users_path);

    if (!workspaces.load(workspaces_path)) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "workspaces_load_failed"},
            {"message", "failed to reload workspaces"}
        }.dump());
        return;
    }

    auto sum_allocated_user_quota_on_pool_local =
    [&](const std::string& want_pool_id) -> std::uint64_t
{
    const std::string want_pool = normalize_storage_pool_id(want_pool_id);
    std::uint64_t total = 0;

    for (const auto& kv : users.snapshot()) {
        const auto& u = kv.second;
        if (u.storage_state != "allocated") continue;

        const std::string user_pool = normalize_storage_pool_id(u.storage_pool_id);
        if (user_pool != want_pool) continue;

        const std::uint64_t q = static_cast<std::uint64_t>(u.quota_bytes);
        if (std::numeric_limits<std::uint64_t>::max() - total < q) {
            return std::numeric_limits<std::uint64_t>::max();
        }
        total += q;
    }

    return total;
};
        auto attach_pool_accounting = [&](json* pj) {
        if (!pj || !pj->is_object()) return;

        const std::string mount = pj->value("mount", "");
        const bool is_editable_pool = pj->value("is_editable_pool", false);

        // Editable pool cards map to named pools.
        // Non-editable system-volume card represents the default pool.
        const std::string effective_pool_id =
            is_editable_pool
                ? normalize_storage_pool_id(pj->value("pool_id", ""))
                : std::string{};

        const std::uint64_t allocated_user_quota_bytes =
            sum_allocated_user_quota_on_pool_local(effective_pool_id);

        const std::uint64_t allocated_workspace_quota_bytes =
            pqnas::sum_allocated_workspace_quota_on_pool(workspaces, effective_pool_id, "");

        std::uint64_t allocated_total_quota_bytes = allocated_user_quota_bytes;
        if (std::numeric_limits<std::uint64_t>::max() - allocated_total_quota_bytes < allocated_workspace_quota_bytes) {
            allocated_total_quota_bytes = std::numeric_limits<std::uint64_t>::max();
        } else {
            allocated_total_quota_bytes += allocated_workspace_quota_bytes;
        }

        std::uint64_t accounting_pool_total_bytes = 0;
        std::uint64_t accounting_pool_free_bytes = 0;

        const std::string stat_path =
            is_editable_pool
                ? mount
                : pqnas::data_root_dir();

        if (!statvfs_path(stat_path, &accounting_pool_total_bytes, &accounting_pool_free_bytes)) {
            accounting_pool_total_bytes = 0;
            accounting_pool_free_bytes = 0;
        }

        const std::uint64_t remaining_allocatable_bytes =
            (accounting_pool_total_bytes > allocated_total_quota_bytes)
                ? (accounting_pool_total_bytes - allocated_total_quota_bytes)
                : 0;

        (*pj)["accounting_pool_id"] = effective_pool_id.empty() ? "default" : effective_pool_id;
        (*pj)["allocated_user_quota_bytes"] = allocated_user_quota_bytes;
        (*pj)["allocated_workspace_quota_bytes"] = allocated_workspace_quota_bytes;
        (*pj)["allocated_total_quota_bytes"] = allocated_total_quota_bytes;
        (*pj)["remaining_allocatable_bytes"] = remaining_allocatable_bytes;
        (*pj)["accounting_pool_total_bytes"] = accounting_pool_total_bytes;
        (*pj)["accounting_pool_free_bytes"] = accounting_pool_free_bytes;
    };
    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";

    const std::string test_prefix  = "/srv/pqnas-test";
    const std::string test_prefix2 = "/srv/pqnas-test-btrfs";


    // Capabilities for UI
    std::string root_fstype;
    {
        int ec_rootfs = 0;
        ec_rootfs = run_findmnt_no_target_argv("FSTYPE", allowed_prefix, &root_fstype);
        cap_string(root_fstype, 4096);
        rtrim_inplace(root_fstype);
        if (ec_rootfs != 0) root_fstype.clear();
    }

    // Load lsblk once so btrfs_show_parsed_to_json can derive parent_disk reliably.
    std::string raw_lsblk;
    json disks_j = storage_list_disks_json(&raw_lsblk);
    json by_path = disks_j.value("by_path", json::object());
    json by_name = disks_j.value("by_name", json::object());

    // Runtime pools keyed by mount so we can later merge config-only entries.
    std::map<std::string, json> runtime_by_mount;

    std::string mounts_out;
    // Security: call findmnt via argv directly, not through a shell string.
    int rc = run_findmnt_btrfs_list_argv("TARGET,SOURCE,FSTYPE", &mounts_out);
    cap_string(mounts_out, 1024 * 1024);
    rtrim_inplace(mounts_out);

    // findmnt returns non-zero when there are no matches.
    // That is valid on systems with no mounted Btrfs pools.
    const bool no_btrfs_matches = (rc != 0 && trim_copy(mounts_out).empty());

    if (rc != 0 && !no_btrfs_matches) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "findmnt_failed"}
        }.dump());
        return;
    }

    for (const std::string& raw : split_lines(mounts_out)) {
        std::string line = trim_copy(raw);
        if (line.empty()) continue;

        std::vector<std::string> toks;
        {
            std::string cur;
            for (char c : line) {
                if (c == ' ' || c == '\t') {
                    if (!cur.empty()) {
                        toks.push_back(cur);
                        cur.clear();
                    }
                } else {
                    cur.push_back(c);
                }
            }
            if (!cur.empty()) toks.push_back(cur);
        }

        if (toks.size() < 3) continue;

        const std::string target = toks[0];
        const std::string source = toks[1];
        const std::string fstype = toks[2];

        if (fstype != "btrfs") continue;
        if (target.empty() || source.empty()) continue;
        if (target[0] != '/') continue;

        const bool allowed =
            starts_with(target, allowed_prefix) ||
            starts_with(target, test_prefix) ||
            starts_with(target, test_prefix2);

        if (!allowed) continue;

        std::string show_out, df_out, usage_out;

        // Security: call the read-only btrfs-status helper via argv, not a
        // shell command string, so mount targets cannot be shell-interpreted.
        int rc_show = run_btrfs_status_helper_capture("filesystem-show", target, &show_out);
        int rc_df = run_btrfs_status_helper_capture("filesystem-df-bytes", target, &df_out);
        int rc_usage = run_btrfs_status_helper_capture("filesystem-usage-bytes", target, &usage_out);

        cap_string(show_out, 256 * 1024);   rtrim_inplace(show_out);
        cap_string(df_out, 256 * 1024);     rtrim_inplace(df_out);
        cap_string(usage_out, 512 * 1024);  rtrim_inplace(usage_out);

        if (rc_show != 0) continue;

        std::string label, uuid;
        int devices = -1;
        parse_btrfs_filesystem_show(show_out, &label, &uuid, &devices);

        std::string prof_data, prof_meta;
        if (rc_df == 0) {
            parse_btrfs_df_profiles(df_out, &prof_data, &prof_meta);
        }

        int64_t size_bytes = -1;
        int64_t used_bytes = -1;
        int64_t free_estimated_bytes = -1;

        if (rc_usage == 0) {
            parse_btrfs_usage_bytes(usage_out, &size_bytes, &used_bytes, &free_estimated_bytes);
        }

        BtrfsShowParsed parsed_show = parse_btrfs_filesystem_show(show_out);
        json show_j = btrfs_show_parsed_to_json(parsed_show, by_path, by_name);
        show_j["btrfs_filesystem_show"] = show_out;

        std::vector<std::string> runtime_member_parents =
            pqnas::runtime_member_parent_disks_from_show_json(show_j);

        auto add_runtime_member_parent = [&](const std::string& dev_path_raw) {
            const std::string dev_path = trim_copy(dev_path_raw);
            if (dev_path.empty()) return;
            if (!is_dev_path_basic_safe(dev_path)) return;

            std::string parent = parent_disk_from_dev(dev_path);
            if (parent.empty()) parent = dev_path;

            if (std::find(runtime_member_parents.begin(),
                          runtime_member_parents.end(),
                          parent) == runtime_member_parents.end()) {
                runtime_member_parents.push_back(parent);
            }
        };

        // Fallback: some btrfs-show JSON shapes do not expose devices in the
        // form expected by runtime_member_parent_disks_from_show_json().
        // Parse the already-captured `btrfs filesystem show` text instead.
        if (runtime_member_parents.empty()) {
            const std::string needle = " path ";
            for (const std::string& raw_line : split_lines(show_out)) {
                const std::string line = trim_copy(raw_line);
                const std::size_t pos = line.rfind(needle);
                if (pos == std::string::npos) continue;
                add_runtime_member_parent(line.substr(pos + needle.size()));
            }
        }

        // Final fallback for simple single-device pools.
        if (runtime_member_parents.empty()) {
            add_runtime_member_parent(source);
        }

        bool busy = false;
        std::string busy_lock;
        {
            const std::string lockp = raid_mount_lock_path(target);
            std::error_code ec;
            if (std::filesystem::exists(lockp, ec) && !ec) {
                busy = true;
                busy_lock = lockp;
            }
        }

        json runtime_j;
        runtime_j["mount"] = target;
        runtime_j["pool_id"] = pool_id_from_mount_best_effort(target);
        runtime_j["uuid"] = uuid.empty() ? "" : uuid;
        runtime_j["label"] = label.empty() ? "" : label;
        runtime_j["devices"] = (devices >= 0) ? devices : 0;
        runtime_j["profile_data"] = prof_data.empty() ? "" : prof_data;
        runtime_j["profile_metadata"] = prof_meta.empty() ? "" : prof_meta;
        runtime_j["runtime_mode"] = pqnas::pool_mode_from_profiles_best_effort(prof_data, prof_meta);
        runtime_j["size_bytes"] = (size_bytes >= 0) ? size_bytes : 0;
        runtime_j["used_bytes"] = (used_bytes >= 0) ? used_bytes : 0;
        runtime_j["free_estimated_bytes"] = (free_estimated_bytes >= 0) ? free_estimated_bytes : 0;
        runtime_j["usable_total_bytes"] =
            (used_bytes >= 0 && free_estimated_bytes >= 0) ? (used_bytes + free_estimated_bytes) : 0;
        runtime_j["resolved_source"] = source;

        {
            const std::string d = parent_disk_from_dev(source);
            if (!d.empty()) runtime_j["resolved_disk"] = d;
        }

        json cfg_pool = json::object();
        if (pools_cfg.contains("pools") && pools_cfg["pools"].is_object()) {
            auto itp = pools_cfg["pools"].find(target);
            if (itp != pools_cfg["pools"].end() && itp->is_object()) {
                cfg_pool = *itp;
            }
        }

        if (cfg_pool.empty()) {
            cfg_pool = json{
                {"mount", target},
                {"pool_id", pool_id_from_mount_best_effort(target)},
                {"display_name", pqnas::pools_display_name_for_mount_v3(pools_cfg, target)},
                {"managed", false},
                {"mode", pqnas::pool_mode_from_profiles_best_effort(prof_data, prof_meta)},
                {"slots", json::array()},
                {"slot_count", 0}
            };
        } else {
            cfg_pool["mount"] = target;
        }

        pqnas::infer_slots_from_runtime_if_missing(&cfg_pool, runtime_member_parents);

        json merged = pqnas::merge_pool_runtime_and_config(
            cfg_pool,
            runtime_j,
            runtime_member_parents,
            busy,
            busy_lock
        );

        const std::string pools_root = allowed_prefix + "/pools/";
        merged["is_editable_pool"] = starts_with(target, pools_root);

        attach_pool_accounting(&merged);

        runtime_by_mount[target] = merged;
    }

    // Final output array:
    // 1) all runtime pools
    // 2) plus config-defined pools not currently mounted
    json arr = json::array();
    std::set<std::string> emitted;

    for (const auto& kv : runtime_by_mount) {
        arr.push_back(kv.second);
        emitted.insert(kv.first);
    }

    if (pools_cfg.contains("pools") && pools_cfg["pools"].is_object()) {
        for (auto it = pools_cfg["pools"].begin(); it != pools_cfg["pools"].end(); ++it) {
            const std::string mount = it.key();
            if (emitted.find(mount) != emitted.end()) continue;
            if (!it->is_object()) continue;

            json cfg_pool = *it;
            cfg_pool["mount"] = mount;

            bool busy = false;
            std::string busy_lock;
            {
                const std::string lockp = raid_mount_lock_path(mount);
                std::error_code ec;
                if (std::filesystem::exists(lockp, ec) && !ec) {
                    busy = true;
                    busy_lock = lockp;
                }
            }

            // No runtime state for this one
            pqnas::infer_slots_from_runtime_if_missing(&cfg_pool, std::vector<std::string>{});

            json merged = pqnas::merge_pool_runtime_and_config(
                cfg_pool,
                json::object(),
                std::vector<std::string>{},
                busy,
                busy_lock
            );

            const std::string pools_root = allowed_prefix + "/pools/";
            merged["is_editable_pool"] = starts_with(mount, pools_root);

            attach_pool_accounting(&merged);

            arr.push_back(merged);
        }
    }

    // Stable ordering for UI: by mount
    std::sort(arr.begin(), arr.end(), [](const json& a, const json& b) {
        return a.value("mount", "") < b.value("mount", "");
    });

    reply_json(res, 200, json{
        {"ok", true},
        {"storage_root_fstype", root_fstype.empty() ? "unknown" : root_fstype},
        {"has_runtime_btrfs_pools", !runtime_by_mount.empty()},
        {"pools", arr}
    }.dump());
});


// ----- POST /api/v4/storage/pools/set-name (admin-only) ---------------------
// Body: { "mount": "/srv/pqnas", "display_name": "Home pool" }
srv.Post("/api/v4/storage/pools/set-name", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;
    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}, {"path", users_path}}.dump());
        return;
    }

    // ---- audit helpers ----
    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto audit_fail = [&](const std::string& actor_fp,
                          const std::string& reason,
                          int http,
                          const std::string& detail = "",
                          const json& extra = json::object()) {
        pqnas::AuditEvent ev;
        ev.event = "v4.storage_pools_set_name_fail";
        ev.outcome = "fail";
        if (!actor_fp.empty()) ev.f["fingerprint"] = actor_fp;
        ev.f["reason"] = reason;
        ev.f["http"] = std::to_string(http);
        if (!detail.empty()) ev.f["detail"] = pqnas::shorten(detail, 220);

        if (extra.is_object()) {
            for (auto it = extra.begin(); it != extra.end(); ++it) {
                const std::string k = pqnas::shorten(it.key(), 64);
                if (it.value().is_string()) ev.f["x_" + k] = pqnas::shorten(it.value().get<std::string>(), 220);
                else if (it.value().is_number_integer() || it.value().is_number_unsigned()) ev.f["x_" + k] = it.value().dump();
                else if (it.value().is_boolean()) ev.f["x_" + k] = (it.value().get<bool>() ? "true" : "false");
                else ev.f["x_" + k] = pqnas::shorten(it.value().dump(), 220);
            }
        }

        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
        audit_append(ev);
    };

    auto audit_ok = [&](const std::string& actor_fp,
                        const json& extra = json::object()) {
        pqnas::AuditEvent ev;
        ev.event = "v4.storage_pools_set_name_ok";
        ev.outcome = "ok";
        if (!actor_fp.empty()) ev.f["fingerprint"] = actor_fp;

        if (extra.is_object()) {
            for (auto it = extra.begin(); it != extra.end(); ++it) {
                const std::string k = pqnas::shorten(it.key(), 64);
                if (it.value().is_string()) ev.f["x_" + k] = pqnas::shorten(it.value().get<std::string>(), 220);
                else if (it.value().is_number_integer() || it.value().is_number_unsigned()) ev.f["x_" + k] = it.value().dump();
                else if (it.value().is_boolean()) ev.f["x_" + k] = (it.value().get<bool>() ? "true" : "false");
                else ev.f["x_" + k] = pqnas::shorten(it.value().dump(), 220);
            }
        }

        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
        audit_append(ev);
    };

    // ---- auth (need actor fingerprint for audit) ----
    std::string actor_fp;
    if (!require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, &actor_fp)) return;
        if (!require_same_origin_for_cookie_mutation(req, res)) return;

    json body;
    try {
        body = json::parse(req.body);
    } catch (...) {
        audit_fail(actor_fp, "bad_json", 400);
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_json"}}.dump());
        return;
    }

    const std::string mount = body.value("mount", "");
    std::string name = body.value("display_name", "");

    if (mount.empty() || mount[0] != '/') {
        audit_fail(actor_fp, "bad_mount", 400, "", json{{"mount", mount}});
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mount"}, {"mount", mount}}.dump());
        return;
    }

    // Basic name hygiene (avoid absurd sizes / control chars)
    if (name.size() > 64) name.resize(64);
    for (char& c : name) {
        unsigned char uc = (unsigned char)c;
        if (uc < 32) c = ' ';
    }
    // trim-ish
    while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) name.erase(name.begin());
    while (!name.empty() && (name.back()  == ' ' || name.back()  == '\t')) name.pop_back();

    // Only allow setting name for allowed mounts (same allowlist as GET pools)
    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";
    const std::string test_prefix  = "/srv/pqnas-test";
    const std::string test_prefix2 = "/srv/pqnas-test-btrfs";

    const bool allowed =
        starts_with(mount, allowed_prefix) ||
        starts_with(mount, test_prefix) ||
        starts_with(mount, test_prefix2);

    if (!allowed) {
        audit_fail(actor_fp, "mount_not_allowed", 400, "",
                   json{{"mount", mount}, {"allowed_prefix", allowed_prefix}});
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "mount_not_allowed"},
            {"mount", mount},
            {"allowed_prefix", allowed_prefix}
        }.dump());
        return;
    }

    // Load + update + write
    json cfg = load_or_init_pools_cfg(users_path);
    if (!cfg.is_object()) cfg = json::object();
    if (!cfg.contains("version")) cfg["version"] = 1;
    if (!cfg.contains("names_by_mount") || !cfg["names_by_mount"].is_object())
        cfg["names_by_mount"] = json::object();

    const bool was_delete = name.empty();

    if (was_delete) {
        // empty name => delete key (reverts to btrfs label fallback)
        cfg["names_by_mount"].erase(mount);
    } else {
        cfg["names_by_mount"][mount] = name;
    }

    const auto cfg_path = pools_cfg_path_from_users_path(users_path);
    std::error_code ec;
    std::filesystem::create_directories(cfg_path.parent_path(), ec);

    if (!write_text_file_atomic(cfg_path.string(), cfg.dump(2) + "\n")) {
        audit_fail(actor_fp, "write_failed", 500, "",
                   json{{"mount", mount}, {"display_name", name}, {"path", cfg_path.string()}});
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "write_failed"},
            {"path", cfg_path.string()}
        }.dump());
        return;
    }

    audit_ok(actor_fp, json{
        {"mount", mount},
        {"display_name", name},
        {"op", (was_delete ? "delete" : "set")},
        {"path", cfg_path.string()}
    });

    reply_json(res, 200, json{
        {"ok", true},
        {"mount", mount},
        {"display_name", name},
        {"path", cfg_path.string()}
    }.dump());
});

// ----- POST /api/v4/storage/pools/rename (admin-only; updates PQ-NAS display name) ----
// Body: { "mount": "/srv/pqnas/data", "display_name": "My Pool", "expect_uuid": "..." }
// Safety: verifies mount is allowlisted AND (if expect_uuid provided) matches current btrfs UUID.
srv.Post("/api/v4/storage/pools/rename", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;

    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}, {"path", users_path}}.dump());
        return;
    }

    // ---- audit helpers ----
    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto audit_fail = [&](const std::string& actor_fp,
                          const std::string& reason,
                          int http,
                          const std::string& detail = "",
                          const json& extra = json::object()) {
        pqnas::AuditEvent ev;
        ev.event = "v4.storage_pools_rename_fail";
        ev.outcome = "fail";
        if (!actor_fp.empty()) ev.f["fingerprint"] = actor_fp;
        ev.f["reason"] = reason;
        ev.f["http"] = std::to_string(http);
        if (!detail.empty()) ev.f["detail"] = pqnas::shorten(detail, 220);

        if (extra.is_object()) {
            for (auto it = extra.begin(); it != extra.end(); ++it) {
                const std::string k = pqnas::shorten(it.key(), 64);
                if (it.value().is_string()) ev.f["x_" + k] = pqnas::shorten(it.value().get<std::string>(), 220);
                else if (it.value().is_number_integer() || it.value().is_number_unsigned()) ev.f["x_" + k] = it.value().dump();
                else if (it.value().is_boolean()) ev.f["x_" + k] = (it.value().get<bool>() ? "true" : "false");
                else ev.f["x_" + k] = pqnas::shorten(it.value().dump(), 220);
            }
        }

        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
        audit_append(ev);
    };

    auto audit_ok = [&](const std::string& actor_fp,
                        const json& extra = json::object()) {
        pqnas::AuditEvent ev;
        ev.event = "v4.storage_pools_rename_ok";
        ev.outcome = "ok";
        if (!actor_fp.empty()) ev.f["fingerprint"] = actor_fp;

        if (extra.is_object()) {
            for (auto it = extra.begin(); it != extra.end(); ++it) {
                const std::string k = pqnas::shorten(it.key(), 64);
                if (it.value().is_string()) ev.f["x_" + k] = pqnas::shorten(it.value().get<std::string>(), 220);
                else if (it.value().is_number_integer() || it.value().is_number_unsigned()) ev.f["x_" + k] = it.value().dump();
                else if (it.value().is_boolean()) ev.f["x_" + k] = (it.value().get<bool>() ? "true" : "false");
                else ev.f["x_" + k] = pqnas::shorten(it.value().dump(), 220);
            }
        }

        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
        audit_append(ev);
    };

    // ---- auth (need actor fingerprint for audit) ----
    std::string actor_fp;
    if (!require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, &actor_fp)) return;
        if (!require_same_origin_for_cookie_mutation(req, res)) return;

    json body;
    try {
        body = json::parse(req.body.empty() ? "{}" : req.body);
    } catch (...) {
        audit_fail(actor_fp, "bad_json", 400);
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_json"}}.dump());
        return;
    }

    const std::string mount = trim_copy(body.value("mount", ""));
    std::string display_name = body.value("display_name", "");
    const std::string expect_uuid = trim_copy(body.value("expect_uuid", ""));

    if (mount.empty() || mount[0] != '/') {
        audit_fail(actor_fp, "bad_mount", 400, "", json{{"mount", mount}});
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mount"}, {"mount", mount}}.dump());
        return;
    }

    // Normalize display name (server-side)
    display_name = trim_copy(display_name);
    if (display_name.size() > 64) display_name.resize(64);

    // Allowed prefix (storage root)
    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";
    const std::string test_prefix  = "/srv/pqnas-test";
    const std::string test_prefix2 = "/srv/pqnas-test-btrfs";

    const bool allowed =
        starts_with(mount, allowed_prefix) ||
        starts_with(mount, test_prefix) ||
        starts_with(mount, test_prefix2);

    if (!allowed) {
        audit_fail(actor_fp, "mount_not_allowed", 403, "",
                   json{{"mount", mount}, {"allowed_prefix", allowed_prefix}});
        reply_json(res, 403, json{
            {"ok", false},
            {"error", "mount_not_allowed"},
            {"mount", mount},
            {"allowed_prefix", allowed_prefix}
        }.dump());
        return;
    }

    // Resolve: ensure mount is actually a btrfs mount we can see (prevents typo mounts)
    std::string mounts_out;
    // Security: call findmnt via argv directly, not through a shell string.
    int rc = run_findmnt_btrfs_list_argv("TARGET", &mounts_out);
    cap_string(mounts_out, 1024 * 1024);
    rtrim_inplace(mounts_out);

    if (rc != 0) {
        audit_fail(actor_fp, "findmnt_failed", 500);
        reply_json(res, 500, json{{"ok", false}, {"error", "findmnt_failed"}}.dump());
        return;
    }

    bool found = false;
    for (const std::string& raw : split_lines(mounts_out)) {
        const std::string t = trim_copy(raw);
        if (t == mount) { found = true; break; }
    }
    if (!found) {
        audit_fail(actor_fp, "mount_not_found", 404, "", json{{"mount", mount}});
        reply_json(res, 404, json{{"ok", false}, {"error", "mount_not_found"}, {"mount", mount}}.dump());
        return;
    }

    // If client provided expect_uuid, verify it matches current UUID (hard safety guard)
    if (!expect_uuid.empty()) {
        // pick btrfs binary (same helper you used above in /storage/pools)

        std::string show_out;
        // Security: call the read-only btrfs-status helper via argv, not a
        // shell command string, so expected UUID mounts cannot be shell-interpreted.
        int rc_show = run_btrfs_status_helper_capture("filesystem-show", mount, &show_out);
        cap_string(show_out, 256 * 1024);
        rtrim_inplace(show_out);

        if (rc_show != 0) {
            audit_fail(actor_fp, "btrfs_show_failed", 500, "",
                       json{{"mount", mount}});
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "btrfs_show_failed"},
                {"mount", mount}
            }.dump());
            return;
        }

        std::string label, uuid;
        int devices = -1;
        parse_btrfs_filesystem_show(show_out, &label, &uuid, &devices);

        if (uuid.empty() || uuid != expect_uuid) {
            audit_fail(actor_fp, "uuid_mismatch", 409, "",
                       json{{"mount", mount}, {"expect_uuid", expect_uuid}, {"actual_uuid", uuid}});
            reply_json(res, 409, json{
                {"ok", false},
                {"error", "uuid_mismatch"},
                {"mount", mount},
                {"expect_uuid", expect_uuid},
                {"actual_uuid", uuid}
            }.dump());
            return;
        }
    }

    // Load + update pools.json (display names) and write atomically
    const json cfg0 = load_or_init_pools_cfg(users_path);
    json cfg = cfg0;

    if (!cfg.is_object()) cfg = json::object();
    if (!cfg.contains("version")) cfg["version"] = 1;
    if (!cfg.contains("names_by_mount") || !cfg["names_by_mount"].is_object()) cfg["names_by_mount"] = json::object();

    const bool removed = display_name.empty();

    if (removed) {
        // empty => remove override
        cfg["names_by_mount"].erase(mount);
    } else {
        cfg["names_by_mount"][mount] = display_name;
    }

    const auto cfg_path = pools_cfg_path_from_users_path(users_path);
    std::error_code ec;
    std::filesystem::create_directories(cfg_path.parent_path(), ec);

    const bool ok_write = write_text_file_atomic(cfg_path.string(), cfg.dump(2) + "\n");
    if (!ok_write) {
        audit_fail(actor_fp, "write_failed", 500, "",
                   json{{"path", cfg_path.string()}, {"mount", mount}, {"display_name", display_name}});
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "write_failed"},
            {"path", cfg_path.string()}
        }.dump());
        return;
    }

    audit_ok(actor_fp, json{
        {"mount", mount},
        {"display_name", display_name},
        {"removed", removed},
        {"expect_uuid", expect_uuid} // may be ""
    });

    reply_json(res, 200, json{
        {"ok", true},
        {"mount", mount},
        {"display_name", display_name},
        {"removed", removed}
    }.dump());
});
// ----- POST /api/v4/poolmgr/add-slot (admin-only, metadata only) -----------
srv.Post("/api/v4/poolmgr/add-slot", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;
    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}}.dump());
        return;
    }
    if (!require_admin_cookie_users(req, res, COOKIE_KEY, users_path, &users)) return;
        if (!require_same_origin_for_cookie_mutation(req, res)) return;

    json in;
    try { in = json::parse(req.body.empty() ? "{}" : req.body); }
    catch (...) {
        reply_json(res, 400, json{{"ok", false}, {"error", "invalid_json"}}.dump());
        return;
    }

    const std::string mount = in.value("mount", "");
    if (mount.empty() || !is_abs_path_safe(mount)) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mount"}}.dump());
        return;
    }

    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";
    const std::string pools_root = allowed_prefix + "/pools/";

    if (!starts_with(mount, pools_root)) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "mount_not_editable_pool"},
            {"mount", mount}
        }.dump());
        return;
    }

    json cfg = pqnas::load_or_init_pools_cfg_v3(users_path);

    if (!cfg.contains("pools") || !cfg["pools"].is_object()) {
        reply_json(res, 500, json{{"ok", false}, {"error", "bad_pools_cfg"}}.dump());
        return;
    }

    auto it = cfg["pools"].find(mount);
    if (it == cfg["pools"].end() || !it->is_object()) {
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "pool_not_found"},
            {"mount", mount}
        }.dump());
        return;
    }

    json& pool = *it;
    pqnas::normalize_pool_entry_v3(&pool);

    if (!pool.contains("slots") || !pool["slots"].is_array()) {
        pool["slots"] = json::array();
    }

    const int next_index = static_cast<int>(pool["slots"].size());
    pool["slots"].push_back(json{
        {"index", next_index},
        {"device", nullptr}
    });
    pool["slot_count"] = static_cast<int>(pool["slots"].size());

    pqnas::enrich_pool_slots_with_runtime_identity_v3(&pool);


    std::string err;
    if (!pqnas::write_pools_cfg_v3(users_path, cfg, &err)) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "write_failed"},
            {"detail", err}
        }.dump());
        return;
    }

    reply_json(res, 200, json{
        {"ok", true},
        {"mount", mount},
        {"slot_count", pool.value("slot_count", 0)},
        {"slots", pool["slots"]}
    }.dump());
});

// ----- POST /api/v4/poolmgr/remove-slot (admin-only, metadata only) --------
srv.Post("/api/v4/poolmgr/remove-slot", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;
    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}}.dump());
        return;
    }
    if (!require_admin_cookie_users(req, res, COOKIE_KEY, users_path, &users)) return;
        if (!require_same_origin_for_cookie_mutation(req, res)) return;

    json in;
    try { in = json::parse(req.body.empty() ? "{}" : req.body); }
    catch (...) {
        reply_json(res, 400, json{{"ok", false}, {"error", "invalid_json"}}.dump());
        return;
    }

    const std::string mount = in.value("mount", "");
    if (mount.empty() || !is_abs_path_safe(mount)) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mount"}}.dump());
        return;
    }

    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";
    const std::string pools_root = allowed_prefix + "/pools/";

    if (!starts_with(mount, pools_root)) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "mount_not_editable_pool"},
            {"mount", mount}
        }.dump());
        return;
    }

    json cfg = pqnas::load_or_init_pools_cfg_v3(users_path);

    if (!cfg.contains("pools") || !cfg["pools"].is_object()) {
        reply_json(res, 500, json{{"ok", false}, {"error", "bad_pools_cfg"}}.dump());
        return;
    }

    auto it = cfg["pools"].find(mount);
    if (it == cfg["pools"].end() || !it->is_object()) {
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "pool_not_found"},
            {"mount", mount}
        }.dump());
        return;
    }

    json& pool = *it;
    pqnas::normalize_pool_entry_v3(&pool);

    if (!pool.contains("slots") || !pool["slots"].is_array() || pool["slots"].empty()) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "no_slots"}
        }.dump());
        return;
    }

    if (pool["slots"].size() <= 1) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "cannot_remove_last_slot"}
        }.dump());
        return;
    }

    const json& last = pool["slots"].back();
    const bool assigned = last.contains("device") && last["device"].is_string() &&
                          !last["device"].get<std::string>().empty();

    if (assigned) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "last_slot_not_empty"},
            {"message", "Only an empty trailing slot can be removed."}
        }.dump());
        return;
    }

    pool["slots"].erase(pool["slots"].end() - 1);
    pool["slot_count"] = static_cast<int>(pool["slots"].size());

    // reindex defensively
    for (size_t i = 0; i < pool["slots"].size(); ++i) {
        pool["slots"][i]["index"] = static_cast<int>(i);
    }

    pqnas::enrich_pool_slots_with_runtime_identity_v3(&pool);


    std::string err;
    if (!pqnas::write_pools_cfg_v3(users_path, cfg, &err)) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "write_failed"},
            {"detail", err}
        }.dump());
        return;
    }

    reply_json(res, 200, json{
        {"ok", true},
        {"mount", mount},
        {"slot_count", pool.value("slot_count", 0)},
        {"slots", pool["slots"]}
    }.dump());
});

// ----- POST /api/v4/poolmgr/set-layout (admin-only, metadata only) ----------
srv.Post("/api/v4/poolmgr/set-layout", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;
    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}}.dump());
        return;
    }
    if (!require_admin_cookie_users(req, res, COOKIE_KEY, users_path, &users)) return;
        if (!require_same_origin_for_cookie_mutation(req, res)) return;

    json in;
    try { in = json::parse(req.body.empty() ? "{}" : req.body); }
    catch (...) {
        reply_json(res, 400, json{{"ok", false}, {"error", "invalid_json"}}.dump());
        return;
    }

    const std::string mount = in.value("mount", "");
    if (mount.empty() || !is_abs_path_safe(mount)) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mount"}}.dump());
        return;
    }

    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";
    const std::string pools_root = allowed_prefix + "/pools/";

    if (!starts_with(mount, pools_root)) {
        reply_json(res, 400, json{{"ok", false}, {"error", "mount_not_editable_pool"}, {"mount", mount}}.dump());
        return;
    }

    const std::string display_name = in.value("display_name", "");
    const std::string mode = in.value("mode", "single");
    const int slot_count_in = in.value("slot_count", 0);
    const json slots_in = in.value("slots", json::array());

    if (mode != "single" && mode != "raid1") {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mode"}}.dump());
        return;
    }
    if (!slots_in.is_array()) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_slots"}}.dump());
        return;
    }

    std::set<std::string> seen;
    json norm_slots = json::array();

    for (size_t i = 0; i < slots_in.size(); ++i) {
        const auto& s = slots_in[i];
        std::string dev;
        if (s.is_object() && s.contains("device") && s["device"].is_string()) {
            dev = trim_copy(s["device"].get<std::string>());
        }

        if (!dev.empty()) {
            if (!is_dev_path_basic_safe(dev)) {
                reply_json(res, 400, json{{"ok", false}, {"error", "bad_device"}, {"device", dev}}.dump());
                return;
            }
            if (!seen.insert(dev).second) {
                reply_json(res, 400, json{{"ok", false}, {"error", "duplicate_device"}, {"device", dev}}.dump());
                return;
            }
        }

        norm_slots.push_back(json{
            {"index", static_cast<int>(i)},
            {"device", dev.empty() ? json(nullptr) : json(dev)}
        });
    }

    int slot_count = slot_count_in > 0 ? slot_count_in : static_cast<int>(norm_slots.size());
    if (slot_count < static_cast<int>(norm_slots.size())) {
        slot_count = static_cast<int>(norm_slots.size());
    }
    if (slot_count < 1) slot_count = 1;

    while (static_cast<int>(norm_slots.size()) < slot_count) {
        norm_slots.push_back(json{
            {"index", static_cast<int>(norm_slots.size())},
            {"device", nullptr}
        });
    }

    json cfg = pqnas::load_or_init_pools_cfg_v3(users_path);
    if (!cfg.contains("pools") || !cfg["pools"].is_object()) {
        reply_json(res, 500, json{{"ok", false}, {"error", "bad_pools_cfg"}}.dump());
        return;
    }

    auto it = cfg["pools"].find(mount);
    if (it == cfg["pools"].end() || !it->is_object()) {
        reply_json(res, 404, json{{"ok", false}, {"error", "pool_not_found"}, {"mount", mount}}.dump());
        return;
    }

    json& pool = *it;
    pqnas::normalize_pool_entry_v3(&pool);

    pool["mount"] = mount;
    pool["mode"] = mode;
    pool["slot_count"] = slot_count;
    pool["slots"] = norm_slots;
    if (!display_name.empty()) {
        pool["display_name"] = display_name;
        cfg["names_by_mount"][mount] = display_name;
    }

    std::string err;
    pqnas::enrich_pool_slots_with_runtime_identity_v3(&cfg["pools"][mount]);

    if (!pqnas::write_pools_cfg_v3(users_path, cfg, &err)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "write_failed"}, {"detail", err}}.dump());
        return;
    }

    reply_json(res, 200, json{
        {"ok", true},
        {"mount", mount},
        {"mode", pool["mode"]},
        {"slot_count", pool["slot_count"]},
        {"slots", pool["slots"]}
    }.dump());
});


// ----- POST /api/v4/poolmgr/plan-layout (admin-only) ------------------------
srv.Post("/api/v4/poolmgr/plan-layout", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;
    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}}.dump());
        return;
    }
    if (!require_admin_cookie_users(req, res, COOKIE_KEY, users_path, &users)) return;
        if (!require_same_origin_for_cookie_mutation(req, res)) return;

    json in;
    try { in = json::parse(req.body.empty() ? "{}" : req.body); }
    catch (...) {
        reply_json(res, 400, json{{"ok", false}, {"error", "invalid_json"}}.dump());
        return;
    }

    const std::string mount = in.value("mount", "");
    if (mount.empty() || !is_abs_path_safe(mount)) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mount"}}.dump());
        return;
    }

    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";
    const std::string pools_root = allowed_prefix + "/pools/";

    if (!starts_with(mount, pools_root)) {
        reply_json(res, 400, json{{"ok", false}, {"error", "mount_not_editable_pool"}, {"mount", mount}}.dump());
        return;
    }

    json cfg = pqnas::load_or_init_pools_cfg_v3(users_path);
    if (!cfg.contains("pools") || !cfg["pools"].is_object()) {
        reply_json(res, 500, json{{"ok", false}, {"error", "bad_pools_cfg"}}.dump());
        return;
    }

    auto it = cfg["pools"].find(mount);
    if (it == cfg["pools"].end() || !it->is_object()) {
        reply_json(res, 404, json{{"ok", false}, {"error", "pool_not_found"}, {"mount", mount}}.dump());
        return;
    }

    json cfg_pool = *it;
    cfg_pool["mount"] = mount;
    pqnas::normalize_pool_entry_v3(&cfg_pool);

    // Runtime state from current merged route logic
    std::string source_out, fstype_out;
    [[maybe_unused]] int ec_src = 0;
    int ec_fs = 0;

    ec_src = run_findmnt_no_target_argv("SOURCE", mount, &source_out);
    cap_string(source_out, 4096);
    rtrim_inplace(source_out);

    ec_fs = run_findmnt_no_target_argv("FSTYPE", mount, &fstype_out);
    cap_string(fstype_out, 4096);
    rtrim_inplace(fstype_out);

    if (ec_fs != 0 || fstype_out != "btrfs") {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "not_btrfs"},
            {"mount", mount},
            {"fstype", fstype_out}
        }.dump());
        return;
    }

    std::string raw_lsblk;
    json disks_j = storage_list_disks_json(&raw_lsblk);
    json by_path = disks_j.value("by_path", json::object());
    json by_name = disks_j.value("by_name", json::object());

    std::string show_out;
    // Security: call the read-only btrfs-status helper via argv, not a
    // shell command string, so pool manager mounts cannot be shell-interpreted.
    int rc_show = run_btrfs_status_helper_capture("filesystem-show", mount, &show_out);
    cap_string(show_out, 256 * 1024);
    rtrim_inplace(show_out);

    if (rc_show != 0 || show_out.empty()) {
        reply_json(res, 500, json{{"ok", false}, {"error", "btrfs_show_failed"}, {"mount", mount}}.dump());
        return;
    }

    BtrfsShowParsed parsed_show = parse_btrfs_filesystem_show(show_out);
    json show_j = btrfs_show_parsed_to_json(parsed_show, by_path, by_name);
    show_j["btrfs_filesystem_show"] = show_out;
    std::vector<std::string> runtime_members;
    std::set<std::string> desired_set;
    std::vector<std::string> to_add;
    std::vector<std::string> to_remove;

    poolmgr_compute_layout_diff_identity_aware(
        cfg_pool,
        show_j,
        &runtime_members,
        &desired_set,
        &to_add,
        &to_remove
    );

    json warnings = json::array();
    json ops = json::array();

    if (to_add.size() > 1 || to_remove.size() > 1 || (to_add.size() + to_remove.size()) > 1) {
        warnings.push_back("multiple_changes_not_supported_yet");
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "multiple_changes_not_supported_yet"},
            {"mount", mount},
            {"diff_impl", "identity_aware_raw_show_v2"},
            {"runtime_members", runtime_members},
            {"to_add", to_add},
            {"to_remove", to_remove},
            {"warnings", warnings}
        }.dump());
        return;
    }

    if (to_add.size() == 1) {
        ops.push_back(json{
            {"type", "add-device"},
            {"disk", to_add[0]},
            {"mode", cfg_pool.value("mode", "single")}
        });
    }

    if (to_remove.size() == 1) {
        ops.push_back(json{
            {"type", "remove-device"},
            {"disk", to_remove[0]}
        });
    }

    const bool busy = [&]() {
        const std::string lockp = raid_mount_lock_path(mount);
        std::error_code ec;
        return std::filesystem::exists(lockp, ec) && !ec;
    }();

    reply_json(res, 200, json{
        {"ok", true},
        {"mount", mount},
        {"busy", busy},
        {"desired_members", desired_set},
        {"runtime_members", runtime_members},
        {"to_add", to_add},
        {"to_remove", to_remove},
        {"ops", ops},
        {"warnings", warnings},
        {"layout_drift", (!to_add.empty() || !to_remove.empty())}
    }.dump());
});

// ----- POST /api/v4/poolmgr/apply-layout (admin-only) -----------------------
srv.Post("/api/v4/poolmgr/apply-layout", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;
    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}}.dump());
        return;
    }

    std::string actor_fp;
    if (!require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, &actor_fp)) return;
        if (!require_same_origin_for_cookie_mutation(req, res)) return;

    json in;
    try { in = json::parse(req.body.empty() ? "{}" : req.body); }
    catch (...) {
        reply_json(res, 400, json{{"ok", false}, {"error", "invalid_json"}}.dump());
        return;
    }

    const std::string mount = in.value("mount", "");
    const bool confirm = in.value("confirm", false);

    if (mount.empty() || !is_abs_path_safe(mount)) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mount"}}.dump());
        return;
    }
    if (!confirm) {
        reply_json(res, 400, json{{"ok", false}, {"error", "confirm_required"}}.dump());
        return;
    }

    // Re-run same planning logic inline (keeps this self-contained)
    json cfg = pqnas::load_or_init_pools_cfg_v3(users_path);
    if (!cfg.contains("pools") || !cfg["pools"].is_object()) {
        reply_json(res, 500, json{{"ok", false}, {"error", "bad_pools_cfg"}}.dump());
        return;
    }

    auto it = cfg["pools"].find(mount);
    if (it == cfg["pools"].end() || !it->is_object()) {
        reply_json(res, 404, json{{"ok", false}, {"error", "pool_not_found"}, {"mount", mount}}.dump());
        return;
    }

    json cfg_pool = *it;
    cfg_pool["mount"] = mount;
    pqnas::normalize_pool_entry_v3(&cfg_pool);

    std::string raw_lsblk;
    json disks_j = storage_list_disks_json(&raw_lsblk);
    json by_path = disks_j.value("by_path", json::object());
    json by_name = disks_j.value("by_name", json::object());

    std::string show_out;
    // Security: call the read-only btrfs-status helper via argv, not a
    // shell command string, so pool manager mounts cannot be shell-interpreted.
    int rc_show = run_btrfs_status_helper_capture("filesystem-show", mount, &show_out);
    cap_string(show_out, 256 * 1024);
    rtrim_inplace(show_out);

    if (rc_show != 0 || show_out.empty()) {
        reply_json(res, 500, json{{"ok", false}, {"error", "btrfs_show_failed"}, {"mount", mount}}.dump());
        return;
    }

    BtrfsShowParsed parsed_show = parse_btrfs_filesystem_show(show_out);
    json show_j = btrfs_show_parsed_to_json(parsed_show, by_path, by_name);
    show_j["btrfs_filesystem_show"] = show_out;
    std::vector<std::string> runtime_members;
    std::set<std::string> desired_set;
    std::vector<std::string> to_add;
    std::vector<std::string> to_remove;

    poolmgr_compute_layout_diff_identity_aware(
        cfg_pool,
        show_j,
        &runtime_members,
        &desired_set,
        &to_add,
        &to_remove
    );

    if (to_add.size() > 1 || to_remove.size() > 1 || (to_add.size() + to_remove.size()) > 1) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "multiple_changes_not_supported_yet"},
            {"mount", mount},
            {"diff_impl", "identity_aware_raw_show_v2"},
            {"runtime_members", runtime_members},
            {"to_add", to_add},
            {"to_remove", to_remove}
        }.dump());
        return;
    }

    if (to_add.empty() && to_remove.empty()) {
        reply_json(res, 200, json{
            {"ok", true},
            {"mount", mount},
            {"skipped", true},
            {"skip_reason", "no_layout_changes"}
        }.dump());
        return;
    }

    if (to_add.size() == 1) {
        const std::string disk = to_add[0];
        const std::string mode = cfg_pool.value("mode", "single");

        // Build add-device plan
        json plan_in = {
            {"mount", mount},
            {"new_disk", disk},
            {"mode", mode},
            {"force", false}
        };

        httplib::Request fake_req = req;
        httplib::Response fake_res;

        // Reuse over HTTP internally is messy, so do the simpler path:
        // ask client to use current backend endpoints through one response.
        const std::string plan_nonce = rand_hex_16();
        // We need the real plan_id from current plan endpoint, so compute by making the same HTTP-visible plan not here.
        reply_json(res, 200, json{
            {"ok", true},
            {"mount", mount},
            {"next_action", "add-device"},
            {"disk", disk},
            {"mode", mode},
            {"plan_nonce", plan_nonce}
        }.dump());
        return;
    }

    if (to_remove.size() == 1) {
        reply_json(res, 200, json{
            {"ok", true},
            {"mount", mount},
            {"next_action", "remove-device"},
            {"disk", to_remove[0]}
        }.dump());
        return;
    }

    reply_json(res, 500, json{{"ok", false}, {"error", "unexpected_state"}}.dump());
});

// ----- GET /api/v4/storage/overview?mount=/path (admin-only) -----------------
srv.Get("/api/v4/storage/overview", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;

    if (!users.load(users_path)) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "users_load_failed"},
            {"path", users_path}
        }.dump());
        return;
    }

    if (!require_admin_cookie_users(req, res, COOKIE_KEY, users_path, &users)) return;

    // -------------------- disks (always returned) --------------------
    std::string raw_lsblk;
    json disks_j = storage_list_disks_json(&raw_lsblk);

    // -------------------- mount selection --------------------
    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";

    std::string mount = allowed_prefix + "/data";
    if (req.has_param("mount")) mount = req.get_param_value("mount");

    json out;
    out["ok"] = false;  // becomes true only if valid btrfs status included

    out["input_mount"] = mount;
    out["allowed_prefix"] = allowed_prefix;

    // always include disks and index maps
    out["disks"]   = disks_j.value("disks", json::array());
    out["by_path"] = disks_j.value("by_path", json::object());
    out["by_name"] = disks_j.value("by_name", json::object());

    // Optional debug raw lsblk at top level
    if (getenv_bool("PQNAS_STORAGE_DEBUG_LSBLK", false)) {
        cap_string(raw_lsblk, 1024 * 1024);
        out["lsblk_raw"] = raw_lsblk;
    }

    // -------------------- input validation --------------------
    if (!is_abs_path_safe(mount)) {
        out["error"] = "bad_mount";
        reply_json(res, 400, out.dump());  // keep 400 for invalid path
        return;
    }

    // -------------------- resolve mountpoint, fstype, source --------------------
    std::string target_out, fstype_out, source_out;

    int rc_target = run_findmnt_no_target_argv("TARGET", mount, &target_out);
    cap_string(target_out, 16 * 1024);
    rtrim_inplace(target_out);

    int rc_fs = run_findmnt_no_target_argv("FSTYPE", mount, &fstype_out);
    cap_string(fstype_out, 16 * 1024);
    rtrim_inplace(fstype_out);

    int rc_src = run_findmnt_no_target_argv("SOURCE", mount, &source_out);
    cap_string(source_out, 16 * 1024);
    rtrim_inplace(source_out);

    if (rc_target != 0 || target_out.empty() ||
        rc_fs != 0 || fstype_out.empty() ||
        rc_src != 0 || source_out.empty()) {

        out["error"] = "mount_not_found";
        reply_json(res, 200, out.dump());  // overview still useful
        return;
    }

    const std::string resolved_mount  = target_out;
    const std::string resolved_source = source_out;
    const std::string resolved_disk   = parent_disk_from_dev(resolved_source);

    out["resolved_mount"]  = resolved_mount;
    out["resolved_source"] = resolved_source;
    out["resolved_disk"]   = resolved_disk;
    out["fstype"]          = fstype_out;

    // -------------------- allowlist enforcement --------------------
    if (resolved_mount.rfind(allowed_prefix, 0) != 0) {
        const std::string test_prefix  = "/srv/pqnas-test";
        const std::string test_prefix2 = "/srv/pqnas-test-btrfs";

        if (resolved_mount.rfind(test_prefix, 0) != 0 &&
            resolved_mount.rfind(test_prefix2, 0) != 0) {

            out["error"] = "mount_not_allowed";
            reply_json(res, 200, out.dump());  // still return disks
            return;
        }
    }

    // -------------------- non-btrfs case --------------------
    if (fstype_out != "btrfs") {
        out["error"] = "not_btrfs";
        reply_json(res, 200, out.dump());  // overview still useful
        return;
    }

    // -------------------- btrfs status --------------------
    json status = storage_btrfs_status_json(resolved_mount);

    status["input_mount"]     = mount;
    status["resolved_mount"]  = resolved_mount;
    status["resolved_source"] = resolved_source;
    status["resolved_disk"]   = resolved_disk;
    status["fstype"]          = fstype_out;

    out["ok"]     = true;
    out["status"] = status;

    reply_json(res, 200, out.dump());
});


// ----- GET /api/v4/raid/exec-record?plan_id=<sha256hex>[&full=1] (admin-only) -----
// Default returns a polling-friendly summary. Add full=1 to return full record (including results[]).
srv.Get("/api/v4/raid/exec-record", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;

    if (!users.load(users_path)) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "users_load_failed"},
            {"path", users_path}
        }.dump());
        return;
    }
    if (!require_admin_cookie_users(req, res, COOKIE_KEY, users_path, &users)) return;

    const std::string plan_id = req.has_param("plan_id") ? req.get_param_value("plan_id") : "";
    const bool full = req.has_param("full") && req.get_param_value("full") == "1";

    if (plan_id.empty()) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "missing plan_id"}
        }.dump());
        return;
    }
    if (!is_sha256_hex_lower(plan_id)) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "plan_id must be 64 lowercase hex chars"}
        }.dump());
        return;
    }

    json rec;
    std::string err;
    if (!raid_exec_record_read(plan_id, &rec, &err)) {
        reply_json(res, 200, json{
            {"ok", false},
            {"error", err.empty() ? "record_not_found" : err},
            {"plan_id", plan_id}
        }.dump());
        return;
    }

    // Always include these fields for UI
    json out = json::object();
    out["ok"]       = true;
    out["plan_id"]  = plan_id;
    out["state"]    = rec.value("state", "unknown");
    out["busy"]     = rec.value("busy", false);
    out["step_index"] = rec.value("step_index", 0);
    out["step_total"] = rec.value("step_total", 0);
    out["ts_start"] = rec.value("ts_start", "");
    out["ts_last"]  = rec.value("ts_last", "");
    out["ts_end"] = (rec.contains("ts_end") ? rec["ts_end"] : json(nullptr));


    // Include plan always (small enough / useful)
    if (rec.contains("plan")) out["plan"] = rec["plan"];

    if (full) {
        // Full payload for debugging
        if (rec.contains("results")) out["results"] = rec["results"];
        if (rec.contains("post_status")) out["post_status"] = rec["post_status"];
        if (rec.contains("error")) out["error"] = rec["error"];
    }

    reply_json(res, 200, out.dump());
});

// ----- GET /api/v4/raid/discovery?mount=/path (admin-only, read-only) --------
srv.Get("/api/v4/raid/discovery", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;

    if (!users.load(users_path)) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "users_load_failed"},
            {"path", users_path}
        }.dump());
        return;
    }

    if (!require_admin_cookie_users(req, res, COOKIE_KEY, users_path, &users)) return;

    // -------------------- disks (always returned) --------------------
    std::string raw_lsblk;
    json disks_j = storage_list_disks_json(&raw_lsblk);

    // -------------------- mount selection --------------------
    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";

    std::string mount = allowed_prefix + "/data";
    if (req.has_param("mount")) mount = req.get_param_value("mount");

    json out;
    out["ok"] = false;

    out["input_mount"] = mount;
    out["allowed_prefix"] = allowed_prefix;

    out["disks"]   = disks_j.value("disks", json::array());
    out["by_path"] = disks_j.value("by_path", json::object());
    out["by_name"] = disks_j.value("by_name", json::object());

    // Optional debug raw lsblk at top level
    if (getenv_bool("PQNAS_STORAGE_DEBUG_LSBLK", false)) {
        cap_string(raw_lsblk, 1024 * 1024);
        out["lsblk_raw"] = raw_lsblk;
    }

    // -------------------- input validation --------------------
    if (!is_abs_path_safe(mount)) {
        out["error"] = "bad_mount";
        reply_json(res, 400, out.dump());
        return;
    }

    // -------------------- resolve mountpoint, fstype, source --------------------
    std::string target_out, fstype_out, source_out;
    int ec_target = 0, ec_fs = 0, ec_src = 0;

    ec_target = run_findmnt_no_target_argv("TARGET", mount, &target_out);
    const bool ok_target = (ec_target == 0);
    cap_string(target_out, 16 * 1024);
    rtrim_inplace(target_out);

    ec_fs = run_findmnt_no_target_argv("FSTYPE", mount, &fstype_out);
    const bool ok_fs = (ec_fs == 0);
    cap_string(fstype_out, 16 * 1024);
    rtrim_inplace(fstype_out);

    ec_src = run_findmnt_no_target_argv("SOURCE", mount, &source_out);
    const bool ok_src = (ec_src == 0);
    cap_string(source_out, 16 * 1024);
    rtrim_inplace(source_out);

    if (!ok_target || ec_target != 0 || target_out.empty() ||
        !ok_fs     || ec_fs     != 0 || fstype_out.empty() ||
        !ok_src    || ec_src    != 0 || source_out.empty()) {

        out["error"] = "mount_not_found";
        reply_json(res, 200, out.dump());
        return;
    }

    const std::string resolved_mount  = target_out;
    const std::string resolved_source = source_out;
    const std::string resolved_disk   = parent_disk_from_dev(resolved_source);

    out["resolved_mount"]  = resolved_mount;
    out["resolved_source"] = resolved_source;
    if (!resolved_disk.empty()) out["resolved_disk"] = resolved_disk;
    out["fstype"]          = fstype_out;

    // -------------------- allowlist enforcement --------------------
    if (resolved_mount.rfind(allowed_prefix, 0) != 0) {
        const std::string test_prefix  = "/srv/pqnas-test";
        const std::string test_prefix2 = "/srv/pqnas-test-btrfs";

        if (resolved_mount.rfind(test_prefix, 0) != 0 &&
            resolved_mount.rfind(test_prefix2, 0) != 0) {

            out["error"] = "mount_not_allowed";
            reply_json(res, 200, out.dump());
            return;
        }
    }

    // -------------------- non-btrfs case --------------------
    if (fstype_out != "btrfs") {
        out["error"] = "not_btrfs";
        reply_json(res, 200, out.dump());
        return;
    }

    // -------------------- btrfs filesystem show (read-only) --------------------
    std::string show_raw;
    int ec_show = 0;

    // Security: call the read-only btrfs-status helper via argv, not a
    // shell command string, so resolved mount targets cannot be shell-interpreted.
    const bool ok_show = run_btrfs_status_helper_argv("filesystem-show", resolved_mount, &show_raw, &ec_show);

    cap_string(show_raw, 256 * 1024);

    if (!ok_show || ec_show != 0 || show_raw.empty()) {
        out["error"] = "btrfs_show_failed";
        out["btrfs_show_rc"] = ec_show;

        if (getenv_bool("PQNAS_RAID_DEBUG_SHOW", false)) {
            cap_string(show_raw, 1024 * 1024);
            out["btrfs_show_raw"] = show_raw;
        }

        reply_json(res, 200, out.dump());
        return;
    }

    BtrfsShowParsed parsed = parse_btrfs_filesystem_show(show_raw);

    json by_path = out.value("by_path", json::object());
    json by_name = out.value("by_name", json::object());

    json btrfs_j = btrfs_show_parsed_to_json(parsed, by_path, by_name);

    // Build device_to_disk_map (best-effort)
    json map_j = json::object();
    if (btrfs_j.contains("devices") && btrfs_j["devices"].is_array()) {
        for (const auto& dev : btrfs_j["devices"]) {
            if (!dev.is_object()) continue;
            const std::string p = dev.value("path", "");
            if (p.empty()) continue;

            json m;
            const std::string parent = dev.value("parent_disk", "");
            if (!parent.empty()) m["parent_disk"] = parent;

            if (dev.contains("lsblk_disk_index") && dev["lsblk_disk_index"].is_number_integer()) {
                m["disk_index"] = dev["lsblk_disk_index"];

                // Add disk_name as a convenience (from parent basename)
                if (!parent.empty()) {
                    std::string name = parent;
                    const size_t slash = name.rfind('/');
                    if (slash != std::string::npos) name = name.substr(slash + 1);
                    if (!name.empty()) m["disk_name"] = name;
                }
            }

            map_j[p] = m;
        }
    }

    out["ok"] = true;
    out["btrfs"] = btrfs_j;
    out["device_to_disk_map"] = map_j;

    if (getenv_bool("PQNAS_RAID_DEBUG_SHOW", false)) {
        cap_string(show_raw, 1024 * 1024);
        out["btrfs_show_raw"] = show_raw;
        out["btrfs_show_rc"]  = ec_show;
    }

    reply_json(res, 200, out.dump());
});

// ----- GET /api/v4/raid/balance-status?mount=/path (admin-only, read-only) ----
// Runs: btrfs balance status <mount>
// Returns: { ok, resolved_mount, running, status_raw, rc, ... }
srv.Get("/api/v4/raid/balance-status", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;

    if (!users.load(users_path)) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "users_load_failed"},
            {"path", users_path}
        }.dump());
        return;
    }
    if (!require_admin_cookie_users(req, res, COOKIE_KEY, users_path, &users)) return;

    // Allowed_prefix + default mount
    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";

    std::string mount = allowed_prefix + "/data";
    if (req.has_param("mount")) mount = req.get_param_value("mount");

    if (!is_abs_path_safe(mount)) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_mount"},
            {"mount", mount}
        }.dump());
        return;
    }

    // Resolve mount -> resolved_mount / source / fstype
    std::string target_out, fstype_out, source_out;
    int ec_target = 0, ec_fs = 0, ec_src = 0;

    ec_target = run_findmnt_no_target_argv("TARGET", mount, &target_out);
    const bool ok_target = (ec_target == 0);
    cap_string(target_out, 16 * 1024);
    rtrim_inplace(target_out);

    ec_fs = run_findmnt_no_target_argv("FSTYPE", mount, &fstype_out);
    const bool ok_fs = (ec_fs == 0);
    cap_string(fstype_out, 16 * 1024);
    rtrim_inplace(fstype_out);

    ec_src = run_findmnt_no_target_argv("SOURCE", mount, &source_out);
    const bool ok_src = (ec_src == 0);
    cap_string(source_out, 16 * 1024);
    rtrim_inplace(source_out);

    if (!ok_target || ec_target != 0 || target_out.empty() ||
        !ok_fs     || ec_fs     != 0 || fstype_out.empty() ||
        !ok_src    || ec_src    != 0 || source_out.empty()) {

        reply_json(res, 200, json{
            {"ok", false},
            {"error", "mount_not_found"},
            {"mount", mount}
        }.dump());
        return;
    }

    const std::string resolved_mount  = target_out;
    const std::string resolved_source = source_out;
    const std::string resolved_disk   = parent_disk_from_dev(resolved_source);

    // Allowlist on resolved mount
    if (resolved_mount.rfind(allowed_prefix, 0) != 0) {
        const std::string test_prefix  = "/srv/pqnas-test";
        const std::string test_prefix2 = "/srv/pqnas-test-btrfs";
        if (resolved_mount.rfind(test_prefix, 0) != 0 &&
            resolved_mount.rfind(test_prefix2, 0) != 0) {
            reply_json(res, 200, json{
                {"ok", false},
                {"error", "mount_not_allowed"},
                {"allowed_prefix", allowed_prefix},
                {"resolved_mount", resolved_mount}
            }.dump());
            return;
        }
    }

    if (fstype_out != "btrfs") {
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "not_btrfs"},
            {"resolved_mount", resolved_mount},
            {"fstype", fstype_out}
        }.dump());
        return;
    }

    // Run balance status
    // Security: call the read-only btrfs-status helper via argv, not a
    // shell command string, so resolved mount targets cannot be shell-interpreted.
    std::string out;
    int rc = 0;
    const bool ok = run_btrfs_status_helper_argv("balance-status", resolved_mount, &out, &rc);
    cap_string(out, 256 * 1024);

    // Parse best-effort
    bool running = false;
    bool paused  = false;
    bool found   = false;

    // Common outputs:
    // - "No balance found on '<mount>'"
    // - "Balance on '<mount>' is running"
    // - "Balance on '<mount>' is paused"
    // - "Balance on '<mount>' is finished"
    // btrfs-progs varies slightly by version; be tolerant.
    {
        const std::string low = to_lower_copy(out);
        if (low.find("no balance found") != std::string::npos) {
            found = false;
            running = false;
        } else if (low.find("is running") != std::string::npos) {
            found = true;
            running = true;
        } else if (low.find("is paused") != std::string::npos) {
            found = true;
            running = true;
            paused = true;
        } else if (low.find("is finished") != std::string::npos ||
                   low.find("finished") != std::string::npos ||
                   low.find("done") != std::string::npos) {
            found = true;
            running = false;
        } else {
            // Unknown wording; if command succeeded and output isn't empty,
            // return it as-is without hard claims.
            found = ok && (rc == 0);
        }
    }

    json j = {
        {"ok", true},
        {"input_mount", mount},
        {"resolved_mount", resolved_mount},
        {"resolved_source", resolved_source},
        {"fstype", fstype_out},
        {"rc", rc},
        {"status_raw", out},
        {"found", found},
        {"running", running},
        {"paused", paused}
    };
    if (!resolved_disk.empty()) j["resolved_disk"] = resolved_disk;

    reply_json(res, 200, j.dump());
});

// ----- GET /api/v4/raid/scrub-status?mount=/path (admin-only, read-only) -----
// Runs: btrfs scrub status <mount>
// Returns: { ok, resolved_mount, running, status_raw, rc, ... }
srv.Get("/api/v4/raid/scrub-status", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;

    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}, {"path", users_path}}.dump());
        return;
    }
    if (!require_admin_cookie_users(req, res, COOKIE_KEY, users_path, &users)) return;

    // Allowed_prefix + default mount
    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";

    std::string mount = allowed_prefix + "/data";
    if (req.has_param("mount")) mount = req.get_param_value("mount");

    if (!is_abs_path_safe(mount)) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mount"}, {"mount", mount}}.dump());
        return;
    }

    // Resolve mount -> resolved_mount / source / fstype
    std::string target_out, fstype_out, source_out;
    int ec_target = 0, ec_fs = 0, ec_src = 0;

    ec_target = run_findmnt_no_target_argv("TARGET", mount, &target_out);
    const bool ok_target = (ec_target == 0);
    cap_string(target_out, 16 * 1024);
    rtrim_inplace(target_out);

    ec_fs = run_findmnt_no_target_argv("FSTYPE", mount, &fstype_out);
    const bool ok_fs = (ec_fs == 0);
    cap_string(fstype_out, 16 * 1024);
    rtrim_inplace(fstype_out);

    ec_src = run_findmnt_no_target_argv("SOURCE", mount, &source_out);
    const bool ok_src = (ec_src == 0);
    cap_string(source_out, 16 * 1024);
    rtrim_inplace(source_out);

    if (!ok_target || ec_target != 0 || target_out.empty() ||
        !ok_fs     || ec_fs     != 0 || fstype_out.empty() ||
        !ok_src    || ec_src    != 0 || source_out.empty()) {
        reply_json(res, 200, json{{"ok", false}, {"error", "mount_not_found"}, {"mount", mount}}.dump());
        return;
    }

    const std::string resolved_mount  = target_out;
    const std::string resolved_source = source_out;
    const std::string resolved_disk   = parent_disk_from_dev(resolved_source);

    // Allowlist on resolved mount
    if (resolved_mount.rfind(allowed_prefix, 0) != 0) {
        const std::string test_prefix  = "/srv/pqnas-test";
        const std::string test_prefix2 = "/srv/pqnas-test-btrfs";
        if (resolved_mount.rfind(test_prefix, 0) != 0 &&
            resolved_mount.rfind(test_prefix2, 0) != 0) {
            reply_json(res, 200, json{
                {"ok", false},
                {"error", "mount_not_allowed"},
                {"allowed_prefix", allowed_prefix},
                {"resolved_mount", resolved_mount}
            }.dump());
            return;
        }
    }

    if (fstype_out != "btrfs") {
        reply_json(res, 200, json{{"ok", false}, {"error", "not_btrfs"}, {"resolved_mount", resolved_mount}, {"fstype", fstype_out}}.dump());
        return;
    }

    // Run scrub status
    // Security: call the read-only btrfs-status helper via argv, not a
    // shell command string, so resolved mount targets cannot be shell-interpreted.
    std::string out;
    int rc = 0;
    const bool ok = run_btrfs_status_helper_argv("scrub-status", resolved_mount, &out, &rc);
    cap_string(out, 256 * 1024);

    // Parse best-effort
    bool running = false;
    bool found   = false;

    // Typical btrfs-progs outputs include:
    // - "no stats available" (often means nothing running / never run)
    // - "scrub status for <mount>"
    // - "running for ..." / "finished" / "canceled"
    {
        const std::string low = to_lower_copy(out);

        if (low.find("no stats available") != std::string::npos ||
            low.find("no scrub") != std::string::npos) {
            found = false;
            running = false;
        } else if (low.find("running") != std::string::npos) {
            found = true;
            running = true;
        } else if (low.find("finished") != std::string::npos ||
                   low.find("completed") != std::string::npos ||
                   low.find("canceled") != std::string::npos ||
                   low.find("cancelled") != std::string::npos) {
            found = true;
            running = false;
        } else {
            found = ok && (rc == 0);
        }
    }

    json j = {
        {"ok", true},
        {"input_mount", mount},
        {"resolved_mount", resolved_mount},
        {"resolved_source", resolved_source},
        {"fstype", fstype_out},
        {"rc", rc},
        {"status_raw", out},
        {"found", found},
        {"running", running}
    };
    if (!resolved_disk.empty()) j["resolved_disk"] = resolved_disk;

    reply_json(res, 200, j.dump());
});

// ----- POST /api/v4/raid/plan/scrub (admin-only, plan-only) ------------------
// Body: { mount?:string, readonly?:bool }  (readonly currently informational only)
srv.Post("/api/v4/raid/plan/scrub", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;

    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}, {"path", users_path}}.dump());
        return;
    }
    if (!require_admin_cookie_users(req, res, COOKIE_KEY, users_path, &users)) return;
        if (!require_same_origin_for_cookie_mutation(req, res)) return;

    json in;
    try { in = json::parse(req.body.empty() ? "{}" : req.body); }
    catch (...) {
        reply_json(res, 400, json({{"ok",false},{"error","bad_request"},{"message","invalid json"}}).dump());
        return;
    }

    // Inputs
    std::string mount = in.value("mount", "");
    const bool readonly = in.value("readonly", false); // informational for now

    // Allowed_prefix + default mount
    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";
    if (mount.empty()) mount = allowed_prefix + "/data";

    if (!is_abs_path_safe(mount)) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mount"}}.dump());
        return;
    }

    // Resolve mount -> resolved_mount / source / fstype
    std::string target_out, fstype_out, source_out;
    int ec_target = 0, ec_fs = 0, ec_src = 0;

    ec_target = run_findmnt_no_target_argv("TARGET", mount, &target_out);
    const bool ok_target = (ec_target == 0);
    cap_string(target_out, 16 * 1024);
    rtrim_inplace(target_out);

    ec_fs = run_findmnt_no_target_argv("FSTYPE", mount, &fstype_out);
    const bool ok_fs = (ec_fs == 0);
    cap_string(fstype_out, 16 * 1024);
    rtrim_inplace(fstype_out);

    ec_src = run_findmnt_no_target_argv("SOURCE", mount, &source_out);
    const bool ok_src = (ec_src == 0);
    cap_string(source_out, 16 * 1024);
    rtrim_inplace(source_out);

    if (!ok_target || ec_target != 0 || target_out.empty() ||
        !ok_fs     || ec_fs     != 0 || fstype_out.empty() ||
        !ok_src    || ec_src    != 0 || source_out.empty()) {
        reply_json(res, 200, json{{"ok", false}, {"error", "mount_not_found"}, {"mount", mount}}.dump());
        return;
    }

    const std::string resolved_mount  = target_out;
    const std::string resolved_source = source_out;
    const std::string resolved_disk   = parent_disk_from_dev(resolved_source);

    // Allowlist on resolved mount
    if (resolved_mount.rfind(allowed_prefix, 0) != 0) {
        const std::string test_prefix  = "/srv/pqnas-test";
        const std::string test_prefix2 = "/srv/pqnas-test-btrfs";
        if (resolved_mount.rfind(test_prefix, 0) != 0 &&
            resolved_mount.rfind(test_prefix2, 0) != 0) {
            reply_json(res, 200, json{
                {"ok", false},
                {"error", "mount_not_allowed"},
                {"allowed_prefix", allowed_prefix},
                {"resolved_mount", resolved_mount}
            }.dump());
            return;
        }
    }

    if (fstype_out != "btrfs") {
        reply_json(res, 200, json{{"ok", false}, {"error", "not_btrfs"}, {"resolved_mount", resolved_mount}, {"fstype", fstype_out}}.dump());
        return;
    }

    // Busy signal (per-mount lock). Plan is allowed while busy, but caller should see it.
    bool busy = false;
    std::string busy_lock;
    {
        const std::string lockp = raid_mount_lock_path(resolved_mount);
        std::error_code ec;
        const bool exists = std::filesystem::exists(lockp, ec);
        if (!ec && exists) {
            busy = true;
            busy_lock = lockp;
        }
    }

    // Build plan
    json plan;
    plan["mount"] = resolved_mount;
    plan["input_mount"] = mount;
    plan["resolved_mount"] = resolved_mount;
    plan["resolved_source"] = resolved_source;
    if (!resolved_disk.empty()) plan["resolved_disk"] = resolved_disk;
    plan["fstype"] = fstype_out;
    plan["requires_downtime"] = false;

    plan["readonly"] = readonly;

    plan["busy"] = busy;
    if (busy && !busy_lock.empty()) plan["busy_lock"] = busy_lock;

    json warnings = json::array();
    json steps = json::array();
    json commands = json::array();

    steps.push_back("Sanity-check: mount resolves to btrfs and is within allowed prefix.");
    steps.push_back("Start scrub (typically runs in background).");
    steps.push_back("Query scrub status immediately (may show running).");

    if (busy) {
        warnings.push_back("BUSY: another RAID operation is currently running for this mount; execute will likely return raid_busy until it finishes.");
    }
    warnings.push_back("Scrub can generate significant IO and may impact performance.");
    warnings.push_back("On single-device filesystems scrub validates checksums but cannot repair corrupted data without redundancy.");
    warnings.push_back("PLAN ONLY: commands are returned as strings; nothing is executed by this endpoint.");

    // Security: use an internal pseudo-command so the executor routes this
    // root-helper scrub start through argv, not a shell command string.
    commands.push_back("RAID_ROOT btrfs-scrub-start " + resolved_mount);
    // Security: use an internal pseudo-command so the executor routes this
    // read-only scrub-status check through argv, not a shell command string.
    commands.push_back("BTRFS_STATUS scrub-status " + resolved_mount);

    plan["steps"] = steps;
    plan["commands"] = commands;
    plan["warnings"] = warnings;

    // plan_id = sha256(joined commands)
    {
        const std::string joined2 = join_commands_for_hash(commands);
        const std::string pid = sha256_hex_lower_evp(joined2);
        if (!pid.empty()) plan["plan_id"] = pid;
    }

    reply_json(res, 200, json{{"ok", true}, {"plan", plan}}.dump());
});

// ----- POST /api/v4/raid/execute/scrub (admin-only) --------------------------
// Body: { mount, readonly?:bool, plan_id:string, dry_run?:bool(true), confirm?:bool(false) }
srv.Post("/api/v4/raid/execute/scrub", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;

    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}, {"path", users_path}}.dump());
        return;
    }

    // ---- auth (need actor fingerprint for audit) ----
    std::string actor_fp;
    if (!require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, &actor_fp)) return;
        if (!require_same_origin_for_cookie_mutation(req, res)) return;

    // ---- audit helpers (match Files API style) ----
    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto audit_kv_merge = [&](pqnas::AuditEvent& ev, const json& extra) {
        if (!extra.is_object()) return;
        for (auto it = extra.begin(); it != extra.end(); ++it) {
            const std::string k  = pqnas::shorten(it.key(), 64);
            const std::string kk = "x_" + k;

            if (it.value().is_string()) ev.f[kk] = pqnas::shorten(it.value().get<std::string>(), 220);
            else if (it.value().is_number_integer() || it.value().is_number_unsigned()) ev.f[kk] = it.value().dump();
            else if (it.value().is_boolean()) ev.f[kk] = (it.value().get<bool>() ? "true" : "false");
            else ev.f[kk] = pqnas::shorten(it.value().dump(), 220);
        }
    };

    auto audit_common = [&](pqnas::AuditEvent& ev) {
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
    };

    auto audit_fail = [&](const std::string& reason,
                          int http,
                          const std::string& detail = "",
                          const json& extra = json::object()) {
        pqnas::AuditEvent ev;
        ev.event = "v4.raid_execute_scrub_fail";
        ev.outcome = "fail";
        if (!actor_fp.empty()) ev.f["fingerprint"] = actor_fp;
        ev.f["reason"] = reason;
        ev.f["http"] = std::to_string(http);
        if (!detail.empty()) ev.f["detail"] = pqnas::shorten(detail, 220);

        audit_kv_merge(ev, extra);
        audit_common(ev);
        // IMPORTANT: do NOT call maybe_auto_rotate_before_append() here; audit_append wrapper does it.
        audit_append(ev);
    };

    auto audit_ok = [&](const json& extra = json::object()) {
        pqnas::AuditEvent ev;
        ev.event = "v4.raid_execute_scrub_ok";
        ev.outcome = "ok";
        if (!actor_fp.empty()) ev.f["fingerprint"] = actor_fp;

        audit_kv_merge(ev, extra);
        audit_common(ev);
        audit_append(ev);
    };

    json in;
    try { in = json::parse(req.body.empty() ? "{}" : req.body); }
    catch (...) {
        audit_fail("bad_json", 400);
        reply_json(res, 400, json({{"ok",false},{"error","bad_request"},{"message","invalid json"}}).dump());
        return;
    }

    // Inputs
    std::string mount = in.value("mount", "");
    const bool readonly = in.value("readonly", false); // informational for now
    const std::string client_plan_id = in.value("plan_id", "");

    // Safety: default dry_run=true
    const bool dry_run = in.value("dry_run", true);
    const bool confirm = in.value("confirm", false);

    if (client_plan_id.empty()) {
        audit_fail("missing_plan_id", 400);
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message","missing plan_id"}}.dump());
        return;
    }

    if (!dry_run && !confirm) {
        audit_fail("confirm_required", 400, "", json{{"dry_run", dry_run}, {"confirm", confirm}});
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "confirm_required"},
            {"message", "set confirm=true when dry_run=false"}
        }.dump());
        return;
    }

    // Allowed_prefix + default mount
    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";
    if (mount.empty()) mount = allowed_prefix + "/data";

    if (!is_abs_path_safe(mount)) {
        audit_fail("bad_mount", 400, "", json{{"mount", mount}});
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mount"}}.dump());
        return;
    }

    // Resolve mount -> resolved_mount / source / fstype
    std::string target_out, fstype_out, source_out;
    int ec_target = 0, ec_fs = 0, ec_src = 0;

    ec_target = run_findmnt_no_target_argv("TARGET", mount, &target_out);
    const bool ok_target = (ec_target == 0);
    cap_string(target_out, 16 * 1024);
    rtrim_inplace(target_out);

    ec_fs = run_findmnt_no_target_argv("FSTYPE", mount, &fstype_out);
    const bool ok_fs = (ec_fs == 0);
    cap_string(fstype_out, 16 * 1024);
    rtrim_inplace(fstype_out);

    ec_src = run_findmnt_no_target_argv("SOURCE", mount, &source_out);
    const bool ok_src = (ec_src == 0);
    cap_string(source_out, 16 * 1024);
    rtrim_inplace(source_out);

    if (!ok_target || ec_target != 0 || target_out.empty() ||
        !ok_fs     || ec_fs     != 0 || fstype_out.empty() ||
        !ok_src    || ec_src    != 0 || source_out.empty()) {

        audit_fail("mount_not_found", 200, "", json{{"mount", mount}});
        reply_json(res, 200, json{{"ok", false}, {"error", "mount_not_found"}, {"mount", mount}}.dump());
        return;
    }

    const std::string resolved_mount  = target_out;
    const std::string resolved_source = source_out;
    const std::string resolved_disk   = parent_disk_from_dev(resolved_source);

    // Allowlist on resolved mount
    if (resolved_mount.rfind(allowed_prefix, 0) != 0) {
        const std::string test_prefix  = "/srv/pqnas-test";
        const std::string test_prefix2 = "/srv/pqnas-test-btrfs";
        if (resolved_mount.rfind(test_prefix, 0) != 0 &&
            resolved_mount.rfind(test_prefix2, 0) != 0) {

            audit_fail("mount_not_allowed", 200, "", json{
                {"mount", mount},
                {"resolved_mount", resolved_mount},
                {"allowed_prefix", allowed_prefix}
            });

            reply_json(res, 200, json{
                {"ok", false},
                {"error", "mount_not_allowed"},
                {"allowed_prefix", allowed_prefix},
                {"resolved_mount", resolved_mount}
            }.dump());
            return;
        }
    }

    if (fstype_out != "btrfs") {
        audit_fail("not_btrfs", 200, "", json{{"resolved_mount", resolved_mount}, {"fstype", fstype_out}});
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "not_btrfs"},
            {"resolved_mount", resolved_mount},
            {"fstype", fstype_out}
        }.dump());
        return;
    }

    // -------- Build commands exactly like plan endpoint --------
    json commands = json::array();
    // Security: use an internal pseudo-command so the executor routes this
    // root-helper scrub start through argv, not a shell command string.
    commands.push_back("RAID_ROOT btrfs-scrub-start " + resolved_mount);
    // Security: use an internal pseudo-command so the executor routes this
    // read-only scrub-status check through argv, not a shell command string.
    commands.push_back("BTRFS_STATUS scrub-status " + resolved_mount);

    // plan_id check (must match exactly)
    const std::string joined = join_commands_for_hash(commands);
    const std::string expected_plan_id = sha256_hex_lower_evp(joined);
    if (expected_plan_id.empty()) {
        audit_fail("plan_id_compute_failed", 500, "", json{{"resolved_mount", resolved_mount}});
        reply_json(res, 500, json{{"ok", false}, {"error", "plan_id_compute_failed"}}.dump());
        return;
    }
    if (client_plan_id != expected_plan_id) {
        audit_fail("plan_mismatch", 400, "", json{
            {"mount", mount},
            {"resolved_mount", resolved_mount},
            {"expected_plan_id", expected_plan_id},
            {"provided_plan_id", client_plan_id}
        });

        reply_json(res, 400, json{
            {"ok", false},
            {"error", "plan_mismatch"},
            {"message", "plan_id does not match server recomputed plan"},
            {"expected_plan_id", expected_plan_id},
            {"provided_plan_id", client_plan_id}
        }.dump());
        return;
    }

    // Plan payload (for caller/UI)
    json plan;
    plan["plan_id"] = expected_plan_id;
    plan["mount"] = resolved_mount;
    plan["input_mount"] = mount;
    plan["resolved_mount"] = resolved_mount;
    plan["resolved_source"] = resolved_source;
    if (!resolved_disk.empty()) plan["resolved_disk"] = resolved_disk;
    plan["fstype"] = fstype_out;
    plan["readonly"] = readonly;
    plan["requires_downtime"] = false;
    plan["commands"] = commands;

    if (dry_run) {
        audit_ok(json{
            {"dry_run", true},
            {"mount", mount},
            {"resolved_mount", resolved_mount},
            {"plan_id", expected_plan_id},
            {"readonly", readonly},
            {"commands", (int)commands.size()}
        });

        reply_json(res, 200, json{{"ok", true}, {"dry_run", true}, {"plan", plan}}.dump());
        return;
    }

    // Locks + execution record (fail-closed)
    int fd_mount_lock = -1;
    int fd_plan_rec   = -1;
    std::string mount_lockp;

    const std::string recp = raid_exec_record_path(expected_plan_id);

    auto close_locks = [&]() {
        if (fd_plan_rec >= 0) { ::close(fd_plan_rec); fd_plan_rec = -1; }
        if (fd_mount_lock >= 0) { ::close(fd_mount_lock); fd_mount_lock = -1; }
        if (!mount_lockp.empty()) {
            (void)std::filesystem::remove(mount_lockp); // lease
        }
    };

    // Ensure state dir exists
    std::string raid_dir_err;
    if (!ensure_dir_fail_closed("/run/pqnas/raid", &raid_dir_err)) {
        audit_fail("raid_state_dir_failed", 500, raid_dir_err, json{
            {"mount", mount},
            {"resolved_mount", resolved_mount},
            {"plan_id", expected_plan_id}
        });

        reply_json(res, 500, json{
            {"ok", false},
            {"error", "raid_state_dir_failed"},
            {"message", "cannot create/verify /run/pqnas/raid; refusing to execute"},
            {"detail", raid_dir_err}
        }.dump());
        return;
    }

    // Acquire per-mount lock first
    mount_lockp = raid_mount_lock_path(resolved_mount);
    {
        std::string mount_lock_err;
        fd_mount_lock = open_excl_lockfile(mount_lockp, &mount_lock_err);
        if (fd_mount_lock < 0) {
            audit_fail("raid_busy", 409, mount_lock_err, json{
                {"mount", mount},
                {"resolved_mount", resolved_mount},
                {"path", mount_lockp},
                {"plan_id", expected_plan_id}
            });

            reply_json(res, 409, json{
                {"ok", false},
                {"error", "raid_busy"},
                {"message", "another raid operation is in progress for this mount"},
                {"mount", resolved_mount},
                {"path", mount_lockp},
                {"detail", mount_lock_err}
            }.dump());
            return;
        }
    }

    // Acquire per-plan execution record lock (replay protection)
    {
        std::string rec_err;
        fd_plan_rec = open_excl_lockfile(recp, &rec_err);
        if (fd_plan_rec < 0) {
            close_locks();

            audit_fail("already_executed", 200, rec_err, json{
                {"mount", mount},
                {"resolved_mount", resolved_mount},
                {"plan_id", expected_plan_id},
                {"path", recp}
            });

            reply_json(res, 200, json{
                {"ok", false},
                {"error", "already_executed"},
                {"message", "this plan_id already has an execution record; refusing replay"},
                {"plan_id", expected_plan_id},
                {"path", recp},
                {"detail", rec_err}
            }.dump());
            return;
        }
    }

    // Initial record
    const std::string ts0 = pqnas::now_iso_utc();
    json record = {
        {"ts_start", ts0},
        {"ts_last",  ts0},
        {"ts_end",   nullptr},

        {"plan_id", expected_plan_id},
        {"state", "running"},
        {"busy", true},

        {"mount", resolved_mount},
        {"input_mount", mount},
        {"resolved_source", resolved_source},
        {"resolved_disk", resolved_disk},

        {"readonly", readonly},
        {"dry_run", false},

        {"plan", plan},
        {"commands", commands},
        {"step_index", 0},
        {"step_total", (int)commands.size()},
        {"results", json::array()}
    };

    // Write initial record to replay-lock file (then close fd)
    {
        const std::string txt = record.dump(2) + "\n";
        if (!write_fd_all(fd_plan_rec, txt)) {
            const std::string err = std::string("write record failed: ") + std::strerror(errno);
            close_locks();

            audit_fail("exec_record_write_failed", 500, err, json{
                {"mount", mount},
                {"resolved_mount", resolved_mount},
                {"plan_id", expected_plan_id}
            });

            reply_json(res, 500, json{
                {"ok", false},
                {"error", "exec_record_write_failed"},
                {"message", "failed to write execution record; refusing to execute"},
                {"detail", err}
            }.dump());
            return;
        }
        ::close(fd_plan_rec);
        fd_plan_rec = -1;
    }

    // Execute commands (stop on first failure)
    json results = json::array();
    bool all_ok = true;

    const int total = (int)commands.size();
    int fail_i = -1;
    int fail_rc = 0;

    for (int i = 0; i < total; i++) {
        const auto& c = commands[i];
        if (!c.is_string()) continue;
        const std::string cmd = c.get<std::string>();

        std::string out;
        int ec = 0;
        // hardening: route pseudo commands through guarded runner.
        const bool okc = run_cmd_capture(cmd, &out, &ec);
        cap_string(out, 128 * 1024);

        json one = {{"i", i}, {"cmd", cmd}, {"rc", ec}, {"ok", okc}, {"out", out}};
        results.push_back(one);

        raid_exec_record_append_step(&record, i + 1, total, cmd, okc, ec, out);
        (void)raid_exec_record_write_atomic(expected_plan_id, record);

        if (!okc || ec != 0) { all_ok = false; fail_i = i; fail_rc = ec; break; }
    }

    // Finalize record
    const std::string ts_end = pqnas::now_iso_utc();
    record["ts_end"]  = ts_end;
    record["ts_last"] = ts_end;
    record["busy"]    = false;
    record["state"]   = all_ok ? "done" : "failed";
    record["results"] = results;

    // Attach final scrub-status snapshot (best-effort)
    if (all_ok) {
        std::string s_out;
        int s_rc = 0;
        // Security: call the read-only scrub-status helper via argv, not a
        // shell command string, so resolved mount targets cannot be shell-interpreted.
        (void)run_btrfs_status_helper_argv("scrub-status", resolved_mount, &s_out, &s_rc);
        cap_string(s_out, 256 * 1024);
        record["post_scrub_status"] = json{{"rc", s_rc}, {"status_raw", s_out}};
    }

    (void)write_text_file_atomic(recp, record.dump(2) + "\n");

    json outj = {
        {"ok", all_ok},
        {"dry_run", false},
        {"plan", plan},
        {"results", results}
    };
    if (record.contains("post_scrub_status")) outj["post_scrub_status"] = record["post_scrub_status"];

    // ---- audit outcome ----
    if (all_ok) {
        audit_ok(json{
            {"dry_run", false},
            {"mount", mount},
            {"resolved_mount", resolved_mount},
            {"plan_id", expected_plan_id},
            {"readonly", readonly},
            {"commands", (int)commands.size()}
        });
    } else {
        audit_fail("command_failed", 200, "", json{
            {"dry_run", false},
            {"mount", mount},
            {"resolved_mount", resolved_mount},
            {"plan_id", expected_plan_id},
            {"readonly", readonly},
            {"commands", (int)commands.size()},
            {"failed_i", fail_i},
            {"failed_rc", fail_rc}
        });
    }

    close_locks();
    reply_json(res, 200, outj.dump());
});
// ----- GET /api/v4/raid/status?mount=/path (admin-only, read-only) -----------
// Combines: filesystem show/df/device stats + balance status + scrub status
srv.Get("/api/v4/raid/status", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;

    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}, {"path", users_path}}.dump());
        return;
    }
    if (!require_admin_cookie_users(req, res, COOKIE_KEY, users_path, &users)) return;

    // Allowed_prefix + default mount
    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";

    std::string mount = allowed_prefix + "/data";
    if (req.has_param("mount")) mount = req.get_param_value("mount");

    if (!is_abs_path_safe(mount)) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mount"}, {"mount", mount}}.dump());
        return;
    }

    // Resolve mount -> resolved_mount / source / fstype
    std::string target_out, fstype_out, source_out;
    int ec_target = 0, ec_fs = 0, ec_src = 0;

    ec_target = run_findmnt_no_target_argv("TARGET", mount, &target_out);
    const bool ok_target = (ec_target == 0);
    cap_string(target_out, 16 * 1024);
    rtrim_inplace(target_out);

    ec_fs = run_findmnt_no_target_argv("FSTYPE", mount, &fstype_out);
    const bool ok_fs = (ec_fs == 0);
    cap_string(fstype_out, 16 * 1024);
    rtrim_inplace(fstype_out);

    ec_src = run_findmnt_no_target_argv("SOURCE", mount, &source_out);
    const bool ok_src = (ec_src == 0);
    cap_string(source_out, 16 * 1024);
    rtrim_inplace(source_out);

    if (!ok_target || ec_target != 0 || target_out.empty() ||
        !ok_fs     || ec_fs     != 0 || fstype_out.empty() ||
        !ok_src    || ec_src    != 0 || source_out.empty()) {
        reply_json(res, 200, json{{"ok", false}, {"error", "mount_not_found"}, {"mount", mount}}.dump());
        return;
    }

    const std::string resolved_mount  = target_out;
    const std::string resolved_source = source_out;
    const std::string resolved_disk   = parent_disk_from_dev(resolved_source);

    // Allowlist on resolved mount
    if (resolved_mount.rfind(allowed_prefix, 0) != 0) {
        const std::string test_prefix  = "/srv/pqnas-test";
        const std::string test_prefix2 = "/srv/pqnas-test-btrfs";
        if (resolved_mount.rfind(test_prefix, 0) != 0 &&
            resolved_mount.rfind(test_prefix2, 0) != 0) {
            reply_json(res, 200, json{
                {"ok", false},
                {"error", "mount_not_allowed"},
                {"allowed_prefix", allowed_prefix},
                {"resolved_mount", resolved_mount}
            }.dump());
            return;
        }
    }

    if (fstype_out != "btrfs") {
        reply_json(res, 200, json{{"ok", false}, {"error", "not_btrfs"}, {"resolved_mount", resolved_mount}, {"fstype", fstype_out}}.dump());
        return;
    }

    // Helper to run btrfs-status actions via argv + capture
    auto run_btrfs_status = [&](const std::string& action, int cap_bytes, std::string* out_txt, int* out_rc) -> json {
        std::string out;
        int rc = 0;
        // Security: call the read-only btrfs-status helper via argv, not a
        // shell command string, so resolved mount targets cannot be shell-interpreted.
        const bool ok = run_btrfs_status_helper_argv(action, resolved_mount, &out, &rc);
        cap_string(out, cap_bytes);
        if (out_txt) *out_txt = out;
        if (out_rc)  *out_rc = rc;
        return json{{"ok", ok && rc == 0}, {"rc", rc}, {"out", out}};
    };

    // Collect raw outputs
    json out = {
        {"ok", true},
        {"input_mount", mount},
        {"resolved_mount", resolved_mount},
        {"resolved_source", resolved_source},
        {"fstype", fstype_out}
    };
    if (!resolved_disk.empty()) out["resolved_disk"] = resolved_disk;

    // btrfs filesystem show
    out["btrfs_filesystem_show"] = run_btrfs_status(
        "filesystem-show",
        256 * 1024, nullptr, nullptr
    );

    // btrfs filesystem df
    out["btrfs_filesystem_df"] = run_btrfs_status(
        "filesystem-df",
        256 * 1024, nullptr, nullptr
    );

    // btrfs device stats
    out["btrfs_device_stats"] = run_btrfs_status(
        "device-stats",
        256 * 1024, nullptr, nullptr
    );

    // balance status
    {
        std::string raw;
        int rc = 0;
        json r = run_btrfs_status(
            "balance-status",
            256 * 1024, &raw, &rc
        );

        bool running = false, paused = false, found = false;
        const std::string low = to_lower_copy(raw);
        if (low.find("no balance found") != std::string::npos) {
            found = false; running = false;
        } else if (low.find("is running") != std::string::npos) {
            found = true; running = true;
        } else if (low.find("is paused") != std::string::npos) {
            found = true; running = true; paused = true;
        } else if (low.find("is finished") != std::string::npos ||
                   low.find("finished") != std::string::npos ||
                   low.find("done") != std::string::npos) {
            found = true; running = false;
        } else {
            found = r.value("ok", false);
        }

        r["found"] = found;
        r["running"] = running;
        r["paused"] = paused;
        out["balance_status"] = r;
    }

    // scrub status
    {
        std::string raw;
        int rc = 0;
        json r = run_btrfs_status(
            "scrub-status",
            256 * 1024, &raw, &rc
        );

        bool running = false, found = false;
        const std::string low = to_lower_copy(raw);

        if (low.find("no stats available") != std::string::npos ||
            low.find("no scrub") != std::string::npos) {
            found = false; running = false;
        } else if (low.find("running") != std::string::npos) {
            found = true; running = true;
        } else if (low.find("finished") != std::string::npos ||
                   low.find("completed") != std::string::npos ||
                   low.find("canceled") != std::string::npos ||
                   low.find("cancelled") != std::string::npos) {
            found = true; running = false;
        } else {
            found = r.value("ok", false);
        }

        r["found"] = found;
        r["running"] = running;
        out["scrub_status"] = r;
    }

    // Busy (mount lock exists)
    {
        bool busy = false;
        std::string busy_lock;
        const std::string lockp = raid_mount_lock_path(resolved_mount);
        std::error_code ec;
        const bool exists = std::filesystem::exists(lockp, ec);
        if (!ec && exists) { busy = true; busy_lock = lockp; }
        out["busy"] = busy;
        if (busy && !busy_lock.empty()) out["busy_lock"] = busy_lock;
    }
    // Parsed + UI-friendly summary (existing helper)
    out["parsed"] = storage_btrfs_status_json(resolved_mount);

    // Enrich parsed block with mount resolution context (helps UI avoid re-resolving)
    out["parsed"]["input_mount"] = mount;
    out["parsed"]["resolved_mount"] = resolved_mount;
    out["parsed"]["resolved_source"] = resolved_source;
    if (!resolved_disk.empty()) out["parsed"]["resolved_disk"] = resolved_disk;
    out["parsed"]["fstype"] = fstype_out;


    reply_json(res, 200, out.dump());
});

// ----- POST /api/v4/raid/plan/add-device (admin-only, plan-only) -------------
srv.Post("/api/v4/raid/plan/add-device", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;

    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}, {"path", users_path}}.dump());
        return;
    }
    if (!require_admin_cookie_users(req, res, COOKIE_KEY, users_path, &users)) return;
        if (!require_same_origin_for_cookie_mutation(req, res)) return;

    json in;
    try { in = json::parse(req.body.empty() ? "{}" : req.body); }
    catch (...) {
        reply_json(res, 400, json({{"ok",false},{"error","bad_request"},{"message","invalid json"}}).dump());
        return;
    }

    // Inputs
    std::string mount    = in.value("mount", "");
    std::string new_disk = in.value("new_disk", "");
    std::string mode     = in.value("mode", "single");  // single|raid1
    bool force           = in.value("force", false);

    // Allowed_prefix + default mount
    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";
    if (mount.empty()) mount = allowed_prefix + "/data";

    // Validate inputs
    if (!is_abs_path_safe(mount)) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mount"}}.dump());
        return;
    }
    if (!is_dev_path_basic_safe(new_disk)) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_device"}, {"message","expected /dev/..."} }.dump());
        return;
    }
    if (mode != "single" && mode != "raid1") {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message","mode must be single|raid1"} }.dump());
        return;
    }

    // Resolve mount -> resolved_mount / source / fstype
    std::string target_out, fstype_out, source_out;
    int ec_target = 0, ec_fs = 0, ec_src = 0;

    ec_target = run_findmnt_no_target_argv("TARGET", mount, &target_out);
    const bool ok_target = (ec_target == 0);
    cap_string(target_out, 16 * 1024);
    rtrim_inplace(target_out);

    ec_fs = run_findmnt_no_target_argv("FSTYPE", mount, &fstype_out);
    const bool ok_fs = (ec_fs == 0);
    cap_string(fstype_out, 16 * 1024);
    rtrim_inplace(fstype_out);

    ec_src = run_findmnt_no_target_argv("SOURCE", mount, &source_out);
    const bool ok_src = (ec_src == 0);
    cap_string(source_out, 16 * 1024);
    rtrim_inplace(source_out);

    if (!ok_target || ec_target != 0 || target_out.empty() ||
        !ok_fs     || ec_fs     != 0 || fstype_out.empty() ||
        !ok_src    || ec_src    != 0 || source_out.empty()) {
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "mount_not_found"},
            {"mount", mount}
        }.dump());
        return;
    }

    const std::string resolved_mount  = target_out;
    const std::string resolved_source = source_out;
    const std::string resolved_disk   = parent_disk_from_dev(resolved_source);

    const std::string system_root_disk = detect_system_pool_root_disk();
    if (!system_root_disk.empty() && new_disk == system_root_disk) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "device_is_system_root_disk"},
            {"system_root_disk", system_root_disk},
            {"new_disk", new_disk}
        }.dump());
        return;
    }

    // Allowlist on resolved mount
    if (resolved_mount.rfind(allowed_prefix, 0) != 0) {
        const std::string test_prefix  = "/srv/pqnas-test";
        const std::string test_prefix2 = "/srv/pqnas-test-btrfs";
        if (resolved_mount.rfind(test_prefix, 0) != 0 &&
            resolved_mount.rfind(test_prefix2, 0) != 0) {
            reply_json(res, 200, json{
                {"ok", false},
                {"error", "mount_not_allowed"},
                {"allowed_prefix", allowed_prefix},
                {"resolved_mount", resolved_mount}
            }.dump());
            return;
        }
    }

    if (fstype_out != "btrfs") {
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "not_btrfs"},
            {"resolved_mount", resolved_mount},
            {"fstype", fstype_out}
        }.dump());
        return;
    }

    // Read btrfs filesystem show (used to salt plan_id so add->remove->add works)
    std::string show_raw;
    int ec_show = 0;
    // Security: call the read-only btrfs-status helper via argv, not a
    // shell command string, so resolved mount targets cannot be shell-interpreted.
    const bool ok_show = run_btrfs_status_helper_argv("filesystem-show", resolved_mount, &show_raw, &ec_show);
    cap_string(show_raw, 256 * 1024);

    if (!ok_show || ec_show != 0 || show_raw.empty()) {
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "btrfs_show_failed"},
            {"resolved_mount", resolved_mount},
            {"btrfs_show_rc", ec_show}
        }.dump());
        return;
    }

    // Busy signal (per-mount lock). Plan is allowed while busy, but caller should see it.
    bool busy = false;
    std::string busy_lock;
    {
        const std::string lockp = raid_mount_lock_path(resolved_mount);
        std::error_code ec;
        const bool exists = std::filesystem::exists(lockp, ec);
        if (!ec && exists) {
            busy = true;
            busy_lock = lockp;
        }
    }

    // Load disks allowlist (inherits PQNAS_STORAGE_ALLOW_LOOP policy)
    std::string raw_lsblk;
    json disks_j = storage_list_disks_json(&raw_lsblk);
    json by_path = disks_j.value("by_path", json::object());
    json disks   = disks_j.value("disks", json::array());

    if (!by_path.is_object() || !by_path.contains(new_disk)) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "device_not_allowed"},
            {"new_disk", new_disk}
        }.dump());
        return;
    }

    int disk_index = -1;
    try { disk_index = by_path[new_disk].get<int>(); } catch (...) { disk_index = -1; }

    if (disk_index < 0 || !disks.is_array() || disk_index >= (int)disks.size()) {
        reply_json(res, 500, json{{"ok", false}, {"error", "lsblk_index_error"}}.dump());
        return;
    }

    json d = disks[disk_index];

    // Hard-refuse disks that have ANY mountpoints anywhere (fail-closed even with force)
    {
        json mpcheck = lsblk_disk_mountpoints_json(new_disk);
        if (mpcheck.value("ok", false) && mpcheck.contains("mountpoints") && mpcheck["mountpoints"].is_array()) {
            if (!mpcheck["mountpoints"].empty()) {
                reply_json(res, 400, json{
                    {"ok", false},
                    {"error", "disk_in_use"},
                    {"new_disk", new_disk},
                    {"disk_index", disk_index},
                    {"model", d.value("model","")},
                    {"serial", d.value("serial","")},
                    {"mountpoints", mpcheck["mountpoints"]}
                }.dump());
                return;
            }
        } else {
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "disk_in_use_check_failed"},
                {"new_disk", new_disk},
                {"detail", mpcheck}
            }.dump());
            return;
        }
    }

    // Refuse adding the same disk the FS is already on (safety)
    if (!resolved_disk.empty() && new_disk == resolved_disk) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "device_is_current_disk"},
            {"resolved_disk", resolved_disk},
            {"new_disk", new_disk}
        }.dump());
        return;
    }

    const int children = d.value("children", 0);
    const uint64_t new_disk_size = d.value("size_bytes", (uint64_t)0);

    // Compute FS membership fingerprint (for plan_id salting)
    std::string membership_fp;
    {
        BtrfsShowParsed parsed2 = parse_btrfs_filesystem_show(show_raw);
        json btrfs2 = btrfs_show_parsed_to_json(parsed2, by_path, disks_j.value("by_name", json::object()));
        membership_fp = btrfs_membership_fingerprint(btrfs2);
    }
    if (membership_fp.empty()) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "membership_fp_failed"},
            {"message", "failed to compute btrfs membership fingerprint"}
        }.dump());
        return;
    }

    // Build plan
    json plan;
    plan["mount"] = resolved_mount;
    plan["input_mount"] = mount;
    plan["resolved_mount"] = resolved_mount;
    plan["resolved_source"] = resolved_source;
    if (!resolved_disk.empty()) plan["resolved_disk"] = resolved_disk;
    plan["fstype"] = fstype_out;

    plan["new_disk"] = new_disk;
    plan["new_disk_index"] = disk_index;
    plan["new_disk_size_bytes"] = new_disk_size;
    plan["mode"] = mode;
    plan["force"] = force;
    plan["requires_downtime"] = false;

    // Busy surface
    plan["busy"] = busy;
    if (busy && !busy_lock.empty()) plan["busy_lock"] = busy_lock;

    json warnings = json::array();
    json steps = json::array();
    json commands = json::array();

    // If disk has partitions: refuse unless force (strict default)
    if (children > 0 && !force) {
        warnings.push_back("new_disk_has_partitions");
        warnings.push_back("refusing_to_plan_destructive_partitioning_without_force=true");
        plan["children"] = children;
        plan["warnings"] = warnings;

        reply_json(res, 200, json{
            {"ok", false},
            {"error", "disk_not_empty"},
            {"plan", plan}
        }.dump());
        return;
    }

    // Partition path (we plan to create p1)
    const std::string new_part = part1_path_from_disk(new_disk);
    if (new_part.empty()) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_device"}}.dump());
        return;
    }
    plan["new_partition"] = new_part;

    // Steps / commands (plan-only)
    steps.push_back("Sanity-check: mount resolves to btrfs and is within allowed prefix.");
    steps.push_back("Sanity-check: new_disk is allowlisted by lsblk and has no mounted partitions.");
    steps.push_back("Sanity-check: new_disk is not the current filesystem disk.");

    if (busy) {
        warnings.push_back("BUSY: another RAID operation is currently running for this mount; execute will likely return raid_busy until it finishes.");
    }

    if (children > 0 && force) {
        warnings.push_back("DESTRUCTIVE: new_disk has existing partitions; plan includes wiping partition table and signatures.");
    } else {
        warnings.push_back("DESTRUCTIVE: plan includes wiping any existing signatures on new_disk.");
    }
    warnings.push_back("Adding a device and converting profiles can take a long time; expect background IO (balance).");
    warnings.push_back("PLAN ONLY: commands are returned as strings; nothing is executed by this endpoint.");

    // Security: use internal pseudo-commands so add-device root-helper steps
    // route through argv, not shell command strings.
    commands.push_back("RAID_ROOT zap-disk " + new_disk);
    commands.push_back("RAID_ROOT wipefs " + new_disk);
    commands.push_back("RAID_ROOT create-btrfs-partition " + new_disk);
    commands.push_back("RAID_ROOT partprobe " + new_disk);

    // NEW — must match execute endpoint exactly
    commands.push_back("RAID_ROOT udev-settle");

    // Wait for partition node to appear (handled internally by executor)
    commands.push_back("WAIT_BLOCK " + new_part + " 2000");

    // Wipe the newly-created partition too. Old btrfs signatures may live
    // inside the partition range even after wiping the parent disk.
    commands.push_back("RAID_ROOT wipefs " + new_part);
    commands.push_back("RAID_ROOT btrfs-device-add " + new_part + " " + resolved_mount);

    if (mode == "raid1") {
        commands.push_back("RAID_ROOT btrfs-balance-raid1 " + resolved_mount);
        commands.push_back(std::string("POOLS_CFG_SET_MODE ") + resolved_mount + " raid1");
        steps.push_back("Convert data/metadata profiles to RAID1 via balance.");
    } else {
        steps.push_back("No profile conversion requested (mode=single). Filesystem will remain in its current profiles until converted.");
    }

    plan["steps"] = steps;
    plan["commands"] = commands;
    plan["warnings"] = warnings;

    // plan_nonce: per-attempt uniqueness (prevents add->remove->add collisions)
    const std::string plan_nonce = rand_hex_16();
    plan["plan_nonce"] = plan_nonce;

    // plan_id = sha256(joined commands + salt + plan_nonce)
    // MUST match execute/add-device exactly.
    {
        const std::string joined2 = join_commands_for_hash(commands);

        const std::string salt =
            std::string("mount=") + resolved_mount + "\n" +
            std::string("btrfs_membership_fp=") + membership_fp + "\n";

        const std::string pid =
            sha256_hex_lower_evp(joined2 + "\n" + salt + "plan_nonce=" + plan_nonce + "\n");

        if (!pid.empty()) plan["plan_id"] = pid;
    }

    reply_json(res, 200, json{{"ok", true}, {"plan", plan}}.dump());
});

// ----- POST /api/v4/raid/plan/convert-mode (admin-only, plan-only) ----------
srv.Post("/api/v4/raid/plan/convert-mode", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;

    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}, {"path", users_path}}.dump());
        return;
    }
    if (!require_admin_cookie_users(req, res, COOKIE_KEY, users_path, &users)) return;
        if (!require_same_origin_for_cookie_mutation(req, res)) return;

    json in;
    try { in = json::parse(req.body.empty() ? "{}" : req.body); }
    catch (...) {
        reply_json(res, 400, json({{"ok",false},{"error","bad_request"},{"message","invalid json"}}).dump());
        return;
    }

    std::string mount = in.value("mount", "");
    std::string mode  = in.value("mode", "single"); // single|raid1

    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";
    if (mount.empty()) mount = allowed_prefix + "/data";

    if (!is_abs_path_safe(mount)) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mount"}}.dump());
        return;
    }
    if (mode != "single" && mode != "raid1") {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "mode must be single|raid1"}}.dump());
        return;
    }

    std::string target_out, fstype_out, source_out;
    int ec_target = 0, ec_fs = 0, ec_src = 0;

    ec_target = run_findmnt_no_target_argv("TARGET", mount, &target_out);
    const bool ok_target = (ec_target == 0);
    cap_string(target_out, 16 * 1024);
    rtrim_inplace(target_out);

    ec_fs = run_findmnt_no_target_argv("FSTYPE", mount, &fstype_out);
    const bool ok_fs = (ec_fs == 0);
    cap_string(fstype_out, 16 * 1024);
    rtrim_inplace(fstype_out);

    ec_src = run_findmnt_no_target_argv("SOURCE", mount, &source_out);
    const bool ok_src = (ec_src == 0);
    cap_string(source_out, 16 * 1024);
    rtrim_inplace(source_out);

    if (!ok_target || ec_target != 0 || target_out.empty() ||
        !ok_fs     || ec_fs     != 0 || fstype_out.empty() ||
        !ok_src    || ec_src    != 0 || source_out.empty()) {
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "mount_not_found"},
            {"mount", mount}
        }.dump());
        return;
    }

    const std::string resolved_mount  = target_out;
    const std::string resolved_source = source_out;
    const std::string resolved_disk   = parent_disk_from_dev(resolved_source);

    if (resolved_mount.rfind(allowed_prefix, 0) != 0) {
        const std::string test_prefix  = "/srv/pqnas-test";
        const std::string test_prefix2 = "/srv/pqnas-test-btrfs";
        if (resolved_mount.rfind(test_prefix, 0) != 0 &&
            resolved_mount.rfind(test_prefix2, 0) != 0) {
            reply_json(res, 200, json{
                {"ok", false},
                {"error", "mount_not_allowed"},
                {"allowed_prefix", allowed_prefix},
                {"resolved_mount", resolved_mount}
            }.dump());
            return;
        }
    }

    if (fstype_out != "btrfs") {
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "not_btrfs"},
            {"resolved_mount", resolved_mount},
            {"fstype", fstype_out}
        }.dump());
        return;
    }

    bool busy = false;
    std::string busy_lock;
    {
        const std::string lockp = raid_mount_lock_path(resolved_mount);
        std::error_code ec;
        const bool exists = std::filesystem::exists(lockp, ec);
        if (!ec && exists) {
            busy = true;
            busy_lock = lockp;
        }
    }

    std::string raw_lsblk;
    json disks_j = storage_list_disks_json(&raw_lsblk);
    json by_path = disks_j.value("by_path", json::object());

    std::string show_raw;
    int ec_show = 0;
    // Security: call the read-only btrfs-status helper via argv, not a
    // shell command string, so resolved mount targets cannot be shell-interpreted.
    const bool ok_show = run_btrfs_status_helper_argv("filesystem-show", resolved_mount, &show_raw, &ec_show);
    cap_string(show_raw, 256 * 1024);

    if (!ok_show || ec_show != 0 || show_raw.empty()) {
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "btrfs_show_failed"},
            {"resolved_mount", resolved_mount},
            {"btrfs_show_rc", ec_show}
        }.dump());
        return;
    }

    BtrfsShowParsed parsed = parse_btrfs_filesystem_show(show_raw);
    json btrfs_j = btrfs_show_parsed_to_json(parsed, by_path, disks_j.value("by_name", json::object()));

    const std::string membership_fp = btrfs_membership_fingerprint(btrfs_j);
    if (membership_fp.empty()) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "membership_fp_failed"},
            {"message", "failed to compute btrfs membership fingerprint"}
        }.dump());
        return;
    }

    const int total_devices = btrfs_j.value("total_devices", 0);
    if (total_devices < 1) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "no_devices_in_filesystem"},
            {"resolved_mount", resolved_mount}
        }.dump());
        return;
    }

    if (mode == "raid1" && total_devices < 2) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "raid1_requires_2_devices"},
            {"resolved_mount", resolved_mount},
            {"total_devices", total_devices}
        }.dump());
        return;
    }

    json plan;
    plan["mount"] = resolved_mount;
    plan["input_mount"] = mount;
    plan["resolved_mount"] = resolved_mount;
    plan["resolved_source"] = resolved_source;
    if (!resolved_disk.empty()) plan["resolved_disk"] = resolved_disk;
    plan["fstype"] = fstype_out;
    plan["mode"] = mode;
    plan["requires_downtime"] = false;
    plan["busy"] = busy;
    if (busy && !busy_lock.empty()) plan["busy_lock"] = busy_lock;
    plan["total_devices"] = total_devices;
    plan["btrfs_membership_fp"] = membership_fp;

    json warnings = json::array();
    json steps = json::array();
    json commands = json::array();

    steps.push_back("Sanity-check: mount resolves to btrfs and is within allowed prefix.");
    steps.push_back("Sanity-check: filesystem has enough devices for requested mode.");

    if (busy) {
        warnings.push_back("BUSY: another RAID operation is currently running for this mount; execute will likely return raid_busy until it finishes.");
    }

    // Security: use RAID_ROOT so profile-conversion root-helper steps route
    // through argv, not shell command strings.
    if (mode == "raid1") {
        commands.push_back("RAID_ROOT btrfs-balance-raid1 " + resolved_mount);
        commands.push_back(std::string("POOLS_CFG_SET_MODE ") + resolved_mount + " raid1");
        steps.push_back("Convert data/metadata profiles to RAID1 via balance.");
    } else {
        commands.push_back("RAID_ROOT btrfs-balance-single-force " + resolved_mount);
        steps.push_back("Convert data/metadata/system profiles to SINGLE via balance (--force for system chunks).");
        warnings.push_back("Converting to SINGLE with multiple devices reduces redundancy.");
    }

    warnings.push_back("Profile conversion can take a long time and generate background IO.");
    warnings.push_back("PLAN ONLY: commands are returned as strings; nothing is executed by this endpoint.");

    plan["steps"] = steps;
    plan["commands"] = commands;
    plan["warnings"] = warnings;

    {
        const std::string joined = join_commands_for_hash(commands);
        const std::string salt =
            std::string("mount=") + resolved_mount + "\n" +
            std::string("btrfs_membership_fp=") + membership_fp + "\n";
        const std::string pid = sha256_hex_lower_evp(joined + "\n" + salt);
        if (!pid.empty()) plan["plan_id"] = pid;
    }

    reply_json(res, 200, json{{"ok", true}, {"plan", plan}}.dump());
});

// ----- POST /api/v4/raid/execute/convert-mode (admin-only) ------------------
// Body: { mount, mode:"single|raid1", plan_id:string, dry_run?:bool, confirm?:bool }
srv.Post("/api/v4/raid/execute/convert-mode", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;

    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}, {"path", users_path}}.dump());
        return;
    }

    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto audit_kv_merge = [&](pqnas::AuditEvent& ev, const json& extra) {
        if (!extra.is_object()) return;
        for (auto it = extra.begin(); it != extra.end(); ++it) {
            const std::string k = pqnas::shorten(it.key(), 64);
            const std::string kk = "x_" + k;
            if (it.value().is_string()) ev.f[kk] = pqnas::shorten(it.value().get<std::string>(), 220);
            else if (it.value().is_number_integer() || it.value().is_number_unsigned()) ev.f[kk] = it.value().dump();
            else if (it.value().is_boolean()) ev.f[kk] = (it.value().get<bool>() ? "true" : "false");
            else ev.f[kk] = pqnas::shorten(it.value().dump(), 220);
        }
    };

    auto audit_common = [&](pqnas::AuditEvent& ev) {
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
    };

    auto audit_fail = [&](const std::string& actor_fp,
                          const std::string& reason,
                          int http,
                          const std::string& detail = "",
                          const json& extra = json::object()) {
        pqnas::AuditEvent ev;
        ev.event = "v4.raid_execute_convert_mode_fail";
        ev.outcome = "fail";
        if (!actor_fp.empty()) ev.f["fingerprint"] = actor_fp;
        ev.f["reason"] = reason;
        ev.f["http"] = std::to_string(http);
        if (!detail.empty()) ev.f["detail"] = pqnas::shorten(detail, 220);
        audit_kv_merge(ev, extra);
        audit_common(ev);
        audit_append(ev);
    };

    auto audit_ok = [&](const std::string& actor_fp,
                        const json& extra = json::object()) {
        pqnas::AuditEvent ev;
        ev.event = "v4.raid_execute_convert_mode_ok";
        ev.outcome = "ok";
        if (!actor_fp.empty()) ev.f["fingerprint"] = actor_fp;
        audit_kv_merge(ev, extra);
        audit_common(ev);
        audit_append(ev);
    };

    std::string actor_fp;
    if (!require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, &actor_fp)) return;
        if (!require_same_origin_for_cookie_mutation(req, res)) return;

    json in;
    try { in = json::parse(req.body.empty() ? "{}" : req.body); }
    catch (...) {
        audit_fail(actor_fp, "bad_json", 400);
        reply_json(res, 400, json({{"ok",false},{"error","bad_request"},{"message","invalid json"}}).dump());
        return;
    }

    std::string mount = in.value("mount", "");
    std::string mode  = in.value("mode", "single");
    bool dry_run      = in.value("dry_run", true);
    bool confirm      = in.value("confirm", false);
    const std::string client_plan_id = in.value("plan_id", "");

    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";
    if (mount.empty()) mount = allowed_prefix + "/data";

    if (!is_abs_path_safe(mount)) {
        audit_fail(actor_fp, "bad_mount", 400, "", json{{"mount", mount}});
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mount"}}.dump());
        return;
    }
    if (mode != "single" && mode != "raid1") {
        audit_fail(actor_fp, "bad_mode", 400, "", json{{"mode", mode}});
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message","mode must be single|raid1"}}.dump());
        return;
    }
    if (client_plan_id.empty()) {
        audit_fail(actor_fp, "missing_plan_id", 400);
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message","missing plan_id"}}.dump());
        return;
    }
    if (!dry_run && !confirm) {
        audit_fail(actor_fp, "confirm_required", 400, "", json{{"dry_run", dry_run}});
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "confirm_required"},
            {"message", "set confirm=true when dry_run=false"}
        }.dump());
        return;
    }

    std::string target_out, fstype_out, source_out;
    int ec_target = 0, ec_fs = 0, ec_src = 0;

    ec_target = run_findmnt_no_target_argv("TARGET", mount, &target_out);
    const bool ok_target = (ec_target == 0);
    cap_string(target_out, 16 * 1024);
    rtrim_inplace(target_out);

    ec_fs = run_findmnt_no_target_argv("FSTYPE", mount, &fstype_out);
    const bool ok_fs = (ec_fs == 0);
    cap_string(fstype_out, 16 * 1024);
    rtrim_inplace(fstype_out);

    ec_src = run_findmnt_no_target_argv("SOURCE", mount, &source_out);
    const bool ok_src = (ec_src == 0);
    cap_string(source_out, 16 * 1024);
    rtrim_inplace(source_out);

    if (!ok_target || ec_target != 0 || target_out.empty() ||
        !ok_fs     || ec_fs     != 0 || fstype_out.empty() ||
        !ok_src    || ec_src    != 0 || source_out.empty()) {
        audit_fail(actor_fp, "mount_not_found", 200, "",
                   json{{"mount", mount}, {"ec_target", ec_target}, {"ec_fs", ec_fs}, {"ec_src", ec_src}});
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "mount_not_found"},
            {"mount", mount}
        }.dump());
        return;
    }

    const std::string resolved_mount  = target_out;
    const std::string resolved_source = source_out;
    const std::string resolved_disk   = parent_disk_from_dev(resolved_source);

    if (resolved_mount.rfind(allowed_prefix, 0) != 0) {
        const std::string test_prefix  = "/srv/pqnas-test";
        const std::string test_prefix2 = "/srv/pqnas-test-btrfs";
        if (resolved_mount.rfind(test_prefix, 0) != 0 && resolved_mount.rfind(test_prefix2, 0) != 0) {
            audit_fail(actor_fp, "mount_not_allowed", 200, "",
                       json{{"allowed_prefix", allowed_prefix}, {"resolved_mount", resolved_mount}});
            reply_json(res, 200, json{
                {"ok", false},
                {"error", "mount_not_allowed"},
                {"allowed_prefix", allowed_prefix},
                {"resolved_mount", resolved_mount}
            }.dump());
            return;
        }
    }

    if (fstype_out != "btrfs") {
        audit_fail(actor_fp, "not_btrfs", 200, "",
                   json{{"resolved_mount", resolved_mount}, {"fstype", fstype_out}});
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "not_btrfs"},
            {"resolved_mount", resolved_mount},
            {"fstype", fstype_out}
        }.dump());
        return;
    }

    std::string raw_lsblk;
    json disks_j = storage_list_disks_json(&raw_lsblk);
    json by_path = disks_j.value("by_path", json::object());

    std::string show_raw;
    int ec_show = 0;
    // Security: call the read-only btrfs-status helper via argv, not a
    // shell command string, so resolved mount targets cannot be shell-interpreted.
    const bool ok_show = run_btrfs_status_helper_argv("filesystem-show", resolved_mount, &show_raw, &ec_show);
    cap_string(show_raw, 256 * 1024);

    if (!ok_show || ec_show != 0 || show_raw.empty()) {
        audit_fail(actor_fp, "btrfs_show_failed", 200, "",
                   json{{"resolved_mount", resolved_mount}, {"btrfs_show_rc", ec_show}});
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "btrfs_show_failed"},
            {"resolved_mount", resolved_mount},
            {"btrfs_show_rc", ec_show}
        }.dump());
        return;
    }

    BtrfsShowParsed parsed = parse_btrfs_filesystem_show(show_raw);
    json btrfs_j = btrfs_show_parsed_to_json(parsed, by_path, disks_j.value("by_name", json::object()));

    const std::string membership_fp = btrfs_membership_fingerprint(btrfs_j);
    if (membership_fp.empty()) {
        audit_fail(actor_fp, "membership_fp_failed", 500,
                   "failed to compute btrfs membership fingerprint",
                   json{{"resolved_mount", resolved_mount}});
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "membership_fp_failed"},
            {"message", "failed to compute btrfs membership fingerprint"}
        }.dump());
        return;
    }

    const int total_devices = btrfs_j.value("total_devices", 0);
    if (total_devices < 1) {
        audit_fail(actor_fp, "no_devices_in_filesystem", 400, "",
                   json{{"resolved_mount", resolved_mount}});
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "no_devices_in_filesystem"},
            {"resolved_mount", resolved_mount}
        }.dump());
        return;
    }

    if (mode == "raid1" && total_devices < 2) {
        audit_fail(actor_fp, "raid1_requires_2_devices", 400, "",
                   json{{"resolved_mount", resolved_mount}, {"total_devices", total_devices}});
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "raid1_requires_2_devices"},
            {"resolved_mount", resolved_mount},
            {"total_devices", total_devices}
        }.dump());
        return;
    }

    json commands = json::array();
    // Security: use RAID_ROOT so profile-conversion root-helper steps route
    // through argv, not shell command strings.
    if (mode == "raid1") {
        commands.push_back("RAID_ROOT btrfs-balance-raid1 " + resolved_mount);
        commands.push_back(std::string("POOLS_CFG_SET_MODE ") + resolved_mount + " raid1");
    } else {
        commands.push_back("RAID_ROOT btrfs-balance-single-force " + resolved_mount);
    }

    const std::string joined = join_commands_for_hash(commands);
    const std::string salt =
        std::string("mount=") + resolved_mount + "\n" +
        std::string("btrfs_membership_fp=") + membership_fp + "\n";

    const std::string expected_plan_id = sha256_hex_lower_evp(joined + "\n" + salt);

    if (expected_plan_id.empty()) {
        audit_fail(actor_fp, "plan_id_compute_failed", 500, "",
                   json{{"mount", resolved_mount}, {"mode", mode}});
        reply_json(res, 500, json{{"ok", false}, {"error", "plan_id_compute_failed"}}.dump());
        return;
    }

    if (client_plan_id != expected_plan_id) {
        audit_fail(actor_fp, "plan_mismatch", 400, "",
                   json{{"expected_plan_id", expected_plan_id}, {"provided_plan_id", client_plan_id}});
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "plan_mismatch"},
            {"message", "plan_id does not match server recomputed plan"},
            {"expected_plan_id", expected_plan_id},
            {"provided_plan_id", client_plan_id}
        }.dump());
        return;
    }

    json plan;
    plan["plan_id"] = expected_plan_id;
    plan["mount"] = resolved_mount;
    plan["input_mount"] = mount;
    plan["resolved_mount"] = resolved_mount;
    plan["resolved_source"] = resolved_source;
    if (!resolved_disk.empty()) plan["resolved_disk"] = resolved_disk;
    plan["fstype"] = fstype_out;
    plan["mode"] = mode;
    plan["requires_downtime"] = false;
    plan["commands"] = commands;
    plan["actor_fp"] = actor_fp;
    plan["total_devices"] = total_devices;
    plan["btrfs_membership_fp"] = membership_fp;

    if (dry_run) {
        audit_ok(actor_fp, json{
            {"dry_run", true},
            {"mount", resolved_mount},
            {"mode", mode},
            {"plan_id", expected_plan_id},
            {"total_devices", total_devices},
            {"commands", (int)commands.size()}
        });

        reply_json(res, 200, json{
            {"ok", true},
            {"dry_run", true},
            {"plan", plan}
        }.dump());
        return;
    }

    try {
        json q = raid_enqueue_job_fail_closed(expected_plan_id, resolved_mount, plan, commands);
        audit_ok(actor_fp, json{
            {"dry_run", false},
            {"enqueued", true},
            {"mount", resolved_mount},
            {"mode", mode},
            {"plan_id", expected_plan_id},
            {"total_devices", total_devices}
        });

        q["dry_run"] = false;
        q["plan"] = plan;
        reply_json(res, 200, q.dump());
        return;
    } catch (const std::exception& e) {
        const std::string msg = e.what();

        if (msg == "already_running") {
            audit_fail(actor_fp, "already_running", 409, "",
                       json{{"plan_id", expected_plan_id}, {"mount", resolved_mount}});
            reply_json(res, 409, json{
                {"ok", false},
                {"error", "already_running"},
                {"message", "this plan_id already has a running execution record; refusing replay"},
                {"plan_id", expected_plan_id},
                {"record_path", raid_exec_record_path(expected_plan_id)}
            }.dump());
            return;
        }

        if (msg.rfind("raid_state_dir_failed:", 0) == 0) {
            audit_fail(actor_fp, "raid_state_dir_failed", 500, msg,
                       json{{"plan_id", expected_plan_id}, {"mount", resolved_mount}});
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "raid_state_dir_failed"},
                {"message", "cannot create/verify /run/pqnas/raid; refusing to execute"},
                {"detail", msg}
            }.dump());
            return;
        }

        audit_fail(actor_fp, "enqueue_failed", 500, msg,
                   json{{"plan_id", expected_plan_id}, {"mount", resolved_mount}});
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "enqueue_failed"},
            {"detail", msg}
        }.dump());
        return;
    }
});

// ----- POST /api/v4/raid/plan/remove-device (admin-only, plan-only) ----------
srv.Post("/api/v4/raid/plan/remove-device", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;

    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}, {"path", users_path}}.dump());
        return;
    }
    if (!require_admin_cookie_users(req, res, COOKIE_KEY, users_path, &users)) return;
        if (!require_same_origin_for_cookie_mutation(req, res)) return;

    json in;
    try { in = json::parse(req.body.empty() ? "{}" : req.body); }
    catch (...) {
        reply_json(res, 400, json({{"ok",false},{"error","bad_request"},{"message","invalid json"}}).dump());
        return;
    }

    // Inputs
    std::string mount         = in.value("mount", "");
    std::string remove_device = in.value("remove_device", "");
    bool force                = in.value("force", false);

    // Allowed_prefix + default mount
    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";
    if (mount.empty()) mount = allowed_prefix + "/data";

    // Validate inputs
    if (!is_abs_path_safe(mount)) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mount"}}.dump());
        return;
    }
    if (!is_dev_path_basic_safe(remove_device)) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_device"}, {"message","expected /dev/..."} }.dump());
        return;
    }

    // Resolve mount -> resolved_mount / source / fstype
    std::string target_out, fstype_out, source_out;
    int ec_target = 0, ec_fs = 0, ec_src = 0;

    ec_target = run_findmnt_no_target_argv("TARGET", mount, &target_out);
    const bool ok_target = (ec_target == 0);
    cap_string(target_out, 16 * 1024);
    rtrim_inplace(target_out);

    ec_fs = run_findmnt_no_target_argv("FSTYPE", mount, &fstype_out);
    const bool ok_fs = (ec_fs == 0);
    cap_string(fstype_out, 16 * 1024);
    rtrim_inplace(fstype_out);

    ec_src = run_findmnt_no_target_argv("SOURCE", mount, &source_out);
    const bool ok_src = (ec_src == 0);
    cap_string(source_out, 16 * 1024);
    rtrim_inplace(source_out);

    if (!ok_target || ec_target != 0 || target_out.empty() ||
        !ok_fs     || ec_fs     != 0 || fstype_out.empty() ||
        !ok_src    || ec_src    != 0 || source_out.empty()) {
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "mount_not_found"},
            {"mount", mount}
        }.dump());
        return;
    }

    const std::string resolved_mount  = target_out;
    const std::string resolved_source = source_out;
    const std::string resolved_disk   = parent_disk_from_dev(resolved_source);

    const std::string system_root_disk = detect_system_pool_root_disk();

    // Allowlist on resolved mount
    if (resolved_mount.rfind(allowed_prefix, 0) != 0) {
        const std::string test_prefix  = "/srv/pqnas-test";
        const std::string test_prefix2 = "/srv/pqnas-test-btrfs";
        if (resolved_mount.rfind(test_prefix, 0) != 0 &&
            resolved_mount.rfind(test_prefix2, 0) != 0) {
            reply_json(res, 200, json{
                {"ok", false},
                {"error", "mount_not_allowed"},
                {"allowed_prefix", allowed_prefix},
                {"resolved_mount", resolved_mount}
            }.dump());
            return;
        }
    }

    if (fstype_out != "btrfs") {
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "not_btrfs"},
            {"resolved_mount", resolved_mount},
            {"fstype", fstype_out}
        }.dump());
        return;
    }

    // Busy signal (per-mount lock). Plan is allowed while busy, but caller should see it.
    bool busy = false;
    std::string busy_lock;
    {
        const std::string lockp = raid_mount_lock_path(resolved_mount);
        std::error_code ec;
        const bool exists = std::filesystem::exists(lockp, ec);
        if (!ec && exists) {
            busy = true;
            busy_lock = lockp;
        }
    }

    // Load disks allowlist (inherits PQNAS_STORAGE_ALLOW_LOOP policy)
    // For remove, allow /dev/<disk> OR /dev/<partition> (partition may not be in by_path).
    std::string raw_lsblk;
    json disks_j = storage_list_disks_json(&raw_lsblk);
    json by_path = disks_j.value("by_path", json::object());
    json disks   = disks_j.value("disks", json::array());

    // Read btrfs filesystem show so we can map /dev/loop33 -> /dev/loop33p1 member path
    std::string show_raw;
    int ec_show = 0;
    // Security: call the read-only btrfs-status helper via argv, not a
    // shell command string, so resolved mount targets cannot be shell-interpreted.
    const bool ok_show = run_btrfs_status_helper_argv("filesystem-show", resolved_mount, &show_raw, &ec_show);
    cap_string(show_raw, 256 * 1024);

    if (!ok_show || ec_show != 0 || show_raw.empty()) {
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "btrfs_show_failed"},
            {"resolved_mount", resolved_mount},
            {"btrfs_show_rc", ec_show}
        }.dump());
        return;
    }

    // Parse show -> json
    BtrfsShowParsed parsed = parse_btrfs_filesystem_show(show_raw);
    json btrfs_j = btrfs_show_parsed_to_json(parsed, by_path, disks_j.value("by_name", json::object()));

    // Compute stable membership fingerprint used for plan_id salting
    const std::string membership_fp = btrfs_membership_fingerprint(btrfs_j);
    if (membership_fp.empty()) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "membership_fp_failed"},
            {"message", "failed to compute btrfs membership fingerprint"}
        }.dump());
        return;
    }

    // Find member device in filesystem that corresponds to remove_device
    // Accept:
    //   - remove_device == member path (e.g. /dev/loop33p1)
    //   - remove_device == parent_disk (e.g. /dev/loop33)
    std::string member_path;
    std::string parent_disk;
    int member_disk_index = -1;

    if (btrfs_j.contains("devices") && btrfs_j["devices"].is_array()) {
        for (const auto& dev : btrfs_j["devices"]) {
            if (!dev.is_object()) continue;
            const std::string p  = dev.value("path", "");
            const std::string pd = dev.value("parent_disk", "");
            if (p.empty()) continue;

            if (remove_device == p || (!pd.empty() && remove_device == pd)) {
                member_path = p;
                parent_disk = pd;
                if (dev.contains("lsblk_disk_index") && dev["lsblk_disk_index"].is_number_integer()) {
                    member_disk_index = dev["lsblk_disk_index"].get<int>();
                }
                break;
            }
        }
    }

    if (member_path.empty()) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "device_not_in_filesystem"},
            {"remove_device", remove_device},
            {"mount", resolved_mount}
        }.dump());
        return;
    }

    if (!system_root_disk.empty() && !parent_disk.empty() && parent_disk == system_root_disk) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "device_is_system_root_disk"},
            {"system_root_disk", system_root_disk},
            {"remove_device", remove_device},
            {"parent_disk", parent_disk}
        }.dump());
        return;
    }

    // Enforce allowlist: parent_disk must be allowlisted (preferred).
    // If parent_disk missing (rare), fall back to requiring remove_device itself in allowlist.
    if (!by_path.is_object() ||
        ((!parent_disk.empty() && !by_path.contains(parent_disk)) &&
         (parent_disk.empty() && !by_path.contains(remove_device)))) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "device_not_allowed"},
            {"remove_device", remove_device},
            {"member_path", member_path},
            {"parent_disk", parent_disk}
        }.dump());
        return;
    }

    // Refuse removing current filesystem disk by default (same safety posture as add-device)
    if (!resolved_disk.empty() && !parent_disk.empty() && parent_disk == resolved_disk && !force) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "device_is_current_disk"},
            {"resolved_disk", resolved_disk},
            {"parent_disk", parent_disk},
            {"remove_device", remove_device}
        }.dump());
        return;
    }

    // Refuse removing if it's the last remaining device
    const int total_devices = btrfs_j.value("total_devices", 0);
    if (total_devices <= 1) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "cannot_remove_last_device"},
            {"total_devices", total_devices},
            {"member_path", member_path},
            {"mount", resolved_mount}
        }.dump());
        return;
    }

    // Build plan
    json plan;
    plan["mount"] = resolved_mount;
    plan["input_mount"] = mount;
    plan["resolved_mount"] = resolved_mount;
    plan["resolved_source"] = resolved_source;
    if (!resolved_disk.empty()) plan["resolved_disk"] = resolved_disk;
    plan["fstype"] = fstype_out;

    plan["remove_device"] = remove_device;
    plan["remove_member_path"] = member_path;
    if (!parent_disk.empty()) plan["remove_parent_disk"] = parent_disk;
    if (member_disk_index >= 0) plan["remove_disk_index"] = member_disk_index;

    plan["force"] = force;
    plan["requires_downtime"] = false;

    // Busy surface
    plan["busy"] = busy;
    if (busy && !busy_lock.empty()) plan["busy_lock"] = busy_lock;

    // Expose fingerprint (nice for debugging)
    plan["btrfs_membership_fp"] = membership_fp;

    json warnings = json::array();
    json steps = json::array();
    json commands = json::array();

    steps.push_back("Sanity-check: mount resolves to btrfs and is within allowed prefix.");
    steps.push_back("Sanity-check: remove_device maps to a btrfs member device currently in the filesystem.");
    steps.push_back("Sanity-check: refusing to remove last device; refusing current FS disk unless force=true.");

    if (busy) {
        warnings.push_back("BUSY: another RAID operation is currently running for this mount; execute will likely return raid_busy until it finishes.");
    }
    warnings.push_back("Removing a device migrates data off the device and can take a long time.");
    warnings.push_back("If the filesystem cannot relocate all extents (space/profile constraints), btrfs may fail the remove.");
    warnings.push_back("PLAN ONLY: commands are returned as strings; nothing is executed by this endpoint.");

    // If this removal would drop from 2 devices -> 1 device, we must convert off RAID1 first
    // Security: use RAID_ROOT so device-remove root-helper steps route through
    // argv, not shell command strings.
    if (total_devices == 2) {
        warnings.push_back("Pre-step required: converting metadata/system profiles to SINGLE to allow removing down to one device.");
        warnings.push_back("This includes --force because newer btrfs-progs refuse explicit system-chunk operations otherwise.");
        warnings.push_back("Pre-step required: converting DATA profile to SINGLE too (cannot remove a device while DATA remains RAID1).");
        commands.push_back("RAID_ROOT btrfs-balance-single-force " + resolved_mount);
        steps.push_back("Convert data/metadata/system profiles to SINGLE via balance (--force for system chunks).");
    }

    commands.push_back("RAID_ROOT btrfs-device-remove " + member_path + " " + resolved_mount);
    steps.push_back("Remove device from filesystem (data migration may take time).");

    plan["steps"] = steps;
    plan["commands"] = commands;
    plan["warnings"] = warnings;

    // plan_id = sha256(joined commands + salt)
    {
        const std::string joined2 = join_commands_for_hash(commands);

        const std::string salt =
            std::string("mount=") + resolved_mount + "\n" +
            std::string("btrfs_membership_fp=") + membership_fp + "\n";

        const std::string pid = sha256_hex_lower_evp(joined2 + "\n" + salt);
        if (!pid.empty()) plan["plan_id"] = pid;
    }

    reply_json(res, 200, json{{"ok", true}, {"plan", plan}}.dump());
});

// ----- POST /api/v4/raid/plan/create-pool (admin-only, plan-only) -------------
srv.Post("/api/v4/raid/plan/create-pool", [&](const httplib::Request& req, httplib::Response& res) {

    pqnas::UsersRegistry users;
    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}}.dump());
        return;
    }
    if (!require_admin_cookie_users(req, res, COOKIE_KEY, users_path, &users)) return;
        if (!require_same_origin_for_cookie_mutation(req, res)) return;

    json in;
    try {
        in = json::parse(req.body.empty() ? "{}" : req.body);
    } catch (...) {
        reply_json(res, 400, json{{"ok", false}, {"error", "invalid_json"}}.dump());
        return;
    }

    const std::string pool_id = trim_copy(in.value("pool_id", ""));
    const std::string mode    = trim_copy(in.value("mode", "single"));
    const bool force          = in.value("force", false);

    json devices_json = json::array();
    if (in.contains("devices")) devices_json = in["devices"];

    if (!std::regex_match(pool_id, std::regex("^[a-z0-9_-]{1,32}$"))) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_pool_id"}}.dump());
        return;
    }

    if (mode != "single" && mode != "raid1") {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mode"}}.dump());
        return;
    }

    json disk_inventory = storage_list_disks_json();

    std::vector<std::string> devices;
    std::string dev_err;
    if (!validate_create_pool_devices(devices_json, disk_inventory, devices, dev_err)) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_devices"},
            {"message", dev_err}
        }.dump());
        return;
    }

    if (mode == "raid1" && devices.size() < 2) {
        reply_json(res, 400, json{{"ok", false}, {"error", "raid1_requires_2_devices"}}.dump());
        return;
    }

    std::string root = getenv_str("PQNAS_STORAGE_ROOT");
    if (root.empty()) root = "/srv/pqnas";

    const std::string mount = root + "/pools/" + pool_id;
    if (std::filesystem::exists(mount)) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "mount_exists"},
            {"mount", mount}
        }.dump());
        return;
    }

    const std::string label = "PQNAS_" + upper_ascii(pool_id);

    const json commands =
        build_create_pool_commands_json(pool_id, mode, devices, force);

    if (!commands.is_array() || commands.empty()) {
        reply_json(res, 500, json{{"ok", false}, {"error", "canonical_plan_empty"}}.dump());
        return;
    }

    json steps = json::array();
    steps.push_back("Create Btrfs filesystem.");
    steps.push_back("Create mount directory.");
    steps.push_back("Mount new pool.");
    steps.push_back("Prepare pool data directory.");

    json warnings = json::array();
    if (force) warnings.push_back("DESTRUCTIVE: devices will be wiped.");

    const std::string plan_nonce = rand_hex_16();
    const std::string plan_id =
        compute_create_pool_plan_id(plan_nonce, pool_id, mode, devices, force, commands);

    if (plan_id.empty()) {
        reply_json(res, 500, json{{"ok", false}, {"error", "plan_id_compute_failed"}}.dump());
        return;
    }

    json plan;
    plan["pool_id"]    = pool_id;
    plan["mount"]      = mount;
    plan["devices"]    = devices;   // canonical validated device list
    plan["mode"]       = mode;
    plan["force"]      = force;
    plan["label"]      = label;
    plan["commands"]   = commands;  // canonical commands from shared helper
    plan["steps"]      = steps;
    plan["warnings"]   = warnings;
    plan["plan_nonce"] = plan_nonce;
    plan["plan_id"]    = plan_id;

    reply_json(res, 200, json{{"ok", true}, {"plan", plan}}.dump());
});

// ----- POST /api/v4/raid/execute/add-device (admin-only) ---------------------
// Body: { mount, new_disk, mode:"single|raid1", force:bool, plan_id:string, dry_run?:bool, confirm?:bool }
srv.Post("/api/v4/raid/execute/add-device", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;

    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}, {"path", users_path}}.dump());
        return;
    }

    // ---- audit helpers ----
    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto audit_kv_merge = [&](pqnas::AuditEvent& ev, const json& extra) {
        if (!extra.is_object()) return;
        for (auto it = extra.begin(); it != extra.end(); ++it) {
            const std::string k = pqnas::shorten(it.key(), 64);
            const std::string kk = "x_" + k;
            if (it.value().is_string()) ev.f[kk] = pqnas::shorten(it.value().get<std::string>(), 220);
            else if (it.value().is_number_integer() || it.value().is_number_unsigned()) ev.f[kk] = it.value().dump();
            else if (it.value().is_boolean()) ev.f[kk] = (it.value().get<bool>() ? "true" : "false");
            else ev.f[kk] = pqnas::shorten(it.value().dump(), 220);
        }
    };

    auto audit_common = [&](pqnas::AuditEvent& ev) {
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
    };

    auto audit_fail = [&](const std::string& actor_fp,
                          const std::string& reason,
                          int http,
                          const std::string& detail = "",
                          const json& extra = json::object()) {
        pqnas::AuditEvent ev;
        ev.event = "v4.raid_execute_add_device_fail";
        ev.outcome = "fail";
        if (!actor_fp.empty()) ev.f["fingerprint"] = actor_fp;
        ev.f["reason"] = reason;
        ev.f["http"] = std::to_string(http);
        if (!detail.empty()) ev.f["detail"] = pqnas::shorten(detail, 220);
        audit_kv_merge(ev, extra);
        audit_common(ev);
        audit_append(ev); // <-- no maybe_auto_rotate_before_append() here
    };

    auto audit_ok = [&](const std::string& actor_fp,
                        const json& extra = json::object()) {
        pqnas::AuditEvent ev;
        ev.event = "v4.raid_execute_add_device_ok";
        ev.outcome = "ok";
        if (!actor_fp.empty()) ev.f["fingerprint"] = actor_fp;
        audit_kv_merge(ev, extra);
        audit_common(ev);
        audit_append(ev); // <-- no maybe_auto_rotate_before_append() here
    };

    // ---- auth (need actor fingerprint for audit) ----
    std::string actor_fp;
    if (!require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, &actor_fp)) return;
    if (!require_same_origin_for_cookie_mutation(req, res)) {
        audit_fail(actor_fp, "origin_mismatch", 403);
        return;
    }

    json in;
    try { in = json::parse(req.body.empty() ? "{}" : req.body); }
    catch (...) {
        audit_fail(actor_fp, "bad_json", 400);
        reply_json(res, 400, json({{"ok",false},{"error","bad_request"},{"message","invalid json"}}).dump());
        return;
    }

    // Inputs
    std::string mount    = in.value("mount", "");
    std::string new_disk = in.value("new_disk", "");
    std::string mode     = in.value("mode", "single"); // single|raid1
    bool force           = in.value("force", false);

    // Safety: default dry_run=true
    bool dry_run = in.value("dry_run", true);
    bool confirm = in.value("confirm", false);

    const std::string client_plan_id = in.value("plan_id", "");

    // Per-attempt nonce (must be provided by plan/add-device and echoed back by UI)
    const std::string client_plan_nonce = in.value("plan_nonce", "");
    if (client_plan_nonce.empty()) {
        audit_fail(actor_fp, "missing_plan_nonce", 400,
                   "", json{{"mount", mount}, {"new_disk", new_disk}, {"mode", mode}, {"dry_run", dry_run}});
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "missing plan_nonce"}
        }.dump());
        return;
    }

    if (!is_hex_64_lower_or_upper(client_plan_id)) {
        audit_fail(actor_fp, "bad_plan_id_format", 400, "",
                   json{{"plan_id", client_plan_id}});
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "plan_id must be 64 hex chars"},
            {"plan_id", client_plan_id}
        }.dump());
        return;
    }
    // Allowed_prefix + default mount
    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";
    if (mount.empty()) mount = allowed_prefix + "/data";

    // Validate inputs
    if (!is_abs_path_safe(mount)) {
        audit_fail(actor_fp, "bad_mount", 400, "", json{{"mount", mount}});
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mount"}}.dump());
        return;
    }
    if (!is_dev_path_basic_safe(new_disk)) {
        audit_fail(actor_fp, "bad_device", 400, "", json{{"new_disk", new_disk}});
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_device"}, {"message","expected /dev/..."} }.dump());
        return;
    }
    if (mode != "single" && mode != "raid1") {
        audit_fail(actor_fp, "bad_mode", 400, "", json{{"mode", mode}});
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message","mode must be single|raid1"} }.dump());
        return;
    }
    if (client_plan_id.empty()) {
        audit_fail(actor_fp, "missing_plan_id", 400);
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message","missing plan_id"} }.dump());
        return;
    }

    // If not dry-run, require explicit confirm=true
    if (!dry_run && !confirm) {
        audit_fail(actor_fp, "confirm_required", 400, "", json{{"dry_run", dry_run}});
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "confirm_required"},
            {"message", "set confirm=true when dry_run=false"}
        }.dump());
        return;
    }

    // Resolve mount -> resolved_mount / source / fstype
    std::string target_out, fstype_out, source_out;
    int ec_target = 0, ec_fs = 0, ec_src = 0;

    ec_target = run_findmnt_no_target_argv("TARGET", mount, &target_out);
    const bool ok_target = (ec_target == 0);
    cap_string(target_out, 16 * 1024);
    rtrim_inplace(target_out);

    ec_fs = run_findmnt_no_target_argv("FSTYPE", mount, &fstype_out);
    const bool ok_fs = (ec_fs == 0);
    cap_string(fstype_out, 16 * 1024);
    rtrim_inplace(fstype_out);

    ec_src = run_findmnt_no_target_argv("SOURCE", mount, &source_out);
    const bool ok_src = (ec_src == 0);
    cap_string(source_out, 16 * 1024);
    rtrim_inplace(source_out);

    if (!ok_target || ec_target != 0 || target_out.empty() ||
        !ok_fs     || ec_fs     != 0 || fstype_out.empty() ||
        !ok_src    || ec_src    != 0 || source_out.empty()) {

        audit_fail(actor_fp, "mount_not_found", 200, "",
                   json{{"mount", mount}, {"ec_target", ec_target}, {"ec_fs", ec_fs}, {"ec_src", ec_src}});
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "mount_not_found"},
            {"mount", mount}
        }.dump());
        return;
    }

    const std::string resolved_mount  = target_out;
    const std::string resolved_source = source_out;
    const std::string resolved_disk   = parent_disk_from_dev(resolved_source);

    const std::string system_root_disk = detect_system_pool_root_disk();
    if (!system_root_disk.empty() && new_disk == system_root_disk) {
        audit_fail(actor_fp, "device_is_system_root_disk", 400, "",
                   json{{"system_root_disk", system_root_disk}, {"new_disk", new_disk}});
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "device_is_system_root_disk"},
            {"system_root_disk", system_root_disk},
            {"new_disk", new_disk}
        }.dump());
        return;
    }

    // Allowlist on resolved mount
    if (resolved_mount.rfind(allowed_prefix, 0) != 0) {
        const std::string test_prefix  = "/srv/pqnas-test";
        const std::string test_prefix2 = "/srv/pqnas-test-btrfs";
        if (resolved_mount.rfind(test_prefix, 0) != 0 && resolved_mount.rfind(test_prefix2, 0) != 0) {
            audit_fail(actor_fp, "mount_not_allowed", 200, "",
                       json{{"allowed_prefix", allowed_prefix}, {"resolved_mount", resolved_mount}});
            reply_json(res, 200, json{
                {"ok", false},
                {"error", "mount_not_allowed"},
                {"allowed_prefix", allowed_prefix},
                {"resolved_mount", resolved_mount}
            }.dump());
            return;
        }
    }

    if (fstype_out != "btrfs") {
        audit_fail(actor_fp, "not_btrfs", 200, "",
                   json{{"resolved_mount", resolved_mount}, {"fstype", fstype_out}});
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "not_btrfs"},
            {"resolved_mount", resolved_mount},
            {"fstype", fstype_out}
        }.dump());
        return;
    }

    // Read btrfs filesystem show (used to salt plan_id so add->remove->add works)
    std::string show_raw;
    int ec_show = 0;
    // Security: call the read-only btrfs-status helper via argv, not a
    // shell command string, so resolved mount targets cannot be shell-interpreted.
    const bool ok_show = run_btrfs_status_helper_argv("filesystem-show", resolved_mount, &show_raw, &ec_show);
    cap_string(show_raw, 256 * 1024);

    if (!ok_show || ec_show != 0 || show_raw.empty()) {
        audit_fail(actor_fp, "btrfs_show_failed", 200, "",
                   json{{"resolved_mount", resolved_mount}, {"btrfs_show_rc", ec_show}});
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "btrfs_show_failed"},
            {"resolved_mount", resolved_mount},
            {"btrfs_show_rc", ec_show}
        }.dump());
        return;
    }

    // Load disks allowlist (inherits PQNAS_STORAGE_ALLOW_LOOP policy)
    std::string raw_lsblk;
    json disks_j = storage_list_disks_json(&raw_lsblk);
    json by_path = disks_j.value("by_path", json::object());
    json disks   = disks_j.value("disks", json::array());

    // Build stable membership fingerprint used for plan_id salting
    BtrfsShowParsed parsed2 = parse_btrfs_filesystem_show(show_raw);
    json btrfs2 = btrfs_show_parsed_to_json(parsed2, by_path, disks_j.value("by_name", json::object()));
    const std::string membership_fp = btrfs_membership_fingerprint(btrfs2);

    if (!by_path.is_object() || !by_path.contains(new_disk)) {
        audit_fail(actor_fp, "device_not_allowed", 400, "",
                   json{{"new_disk", new_disk}, {"resolved_mount", resolved_mount}});
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "device_not_allowed"},
            {"new_disk", new_disk}
        }.dump());
        return;
    }

    int disk_index = -1;
    try { disk_index = by_path[new_disk].get<int>(); } catch (...) { disk_index = -1; }

    if (disk_index < 0 || !disks.is_array() || disk_index >= (int)disks.size()) {
        audit_fail(actor_fp, "lsblk_index_error", 500, "",
                   json{{"new_disk", new_disk}, {"disk_index", disk_index}});
        reply_json(res, 500, json{{"ok", false}, {"error", "lsblk_index_error"}}.dump());
        return;
    }

    json d = disks[disk_index];

    // Hard-refuse disks that have ANY mountpoints anywhere (fail-closed even with force)
    {
        json mpcheck = lsblk_disk_mountpoints_json(new_disk);
        if (mpcheck.value("ok", false) && mpcheck.contains("mountpoints") && mpcheck["mountpoints"].is_array()) {
            if (!mpcheck["mountpoints"].empty()) {
                audit_fail(actor_fp, "disk_in_use", 400, "",
                           json{{"new_disk", new_disk}, {"disk_index", disk_index}});
                reply_json(res, 400, json{
                    {"ok", false},
                    {"error", "disk_in_use"},
                    {"new_disk", new_disk},
                    {"disk_index", disk_index},
                    {"model", d.value("model","")},
                    {"serial", d.value("serial","")},
                    {"mountpoints", mpcheck["mountpoints"]}
                }.dump());
                return;
            }
        } else {
            audit_fail(actor_fp, "disk_in_use_check_failed", 500, "",
                       json{{"new_disk", new_disk}});
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "disk_in_use_check_failed"},
                {"new_disk", new_disk},
                {"detail", mpcheck}
            }.dump());
            return;
        }
    }

    // Refuse adding the same disk the FS is already on (safety)
    if (!resolved_disk.empty() && new_disk == resolved_disk) {
        audit_fail(actor_fp, "device_is_current_disk", 400, "",
                   json{{"resolved_disk", resolved_disk}, {"new_disk", new_disk}});
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "device_is_current_disk"},
            {"resolved_disk", resolved_disk},
            {"new_disk", new_disk}
        }.dump());
        return;
    }

    const int children = d.value("children", 0);
    const uint64_t new_disk_size = d.value("size_bytes", (uint64_t)0);

    // If disk has partitions: refuse unless force (strict default)
    if (children > 0 && !force) {
        json plan_tmp;
        plan_tmp["mount"] = resolved_mount;
        plan_tmp["new_disk"] = new_disk;
        plan_tmp["new_disk_index"] = disk_index;
        plan_tmp["new_disk_size_bytes"] = new_disk_size;
        plan_tmp["mode"] = mode;
        plan_tmp["force"] = force;
        plan_tmp["requires_downtime"] = false;
        plan_tmp["children"] = children;
        plan_tmp["warnings"] = json::array({"new_disk_has_partitions", "refusing_to_execute_destructive_partitioning_without_force=true"});
        audit_fail(actor_fp, "disk_not_empty", 200, "",
                   json{{"mount", resolved_mount}, {"new_disk", new_disk}, {"children", children}, {"force", force}});
        reply_json(res, 200, json{{"ok", false}, {"error", "disk_not_empty"}, {"plan", plan_tmp}}.dump());
        return;
    }

    const std::string new_part = part1_path_from_disk(new_disk);
    if (new_part.empty()) {
        audit_fail(actor_fp, "bad_device_partition", 400, "", json{{"new_disk", new_disk}});
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_device"}}.dump());
        return;
    }

    // -------- Build commands exactly like plan endpoint --------
    json commands = json::array();
    // Security: use internal pseudo-commands so add-device root-helper steps
    // route through argv, not shell command strings.
    commands.push_back("RAID_ROOT zap-disk " + new_disk);
    commands.push_back("RAID_ROOT wipefs " + new_disk);
    commands.push_back("RAID_ROOT create-btrfs-partition " + new_disk);
    commands.push_back("RAID_ROOT partprobe " + new_disk);
    commands.push_back("RAID_ROOT udev-settle");
    commands.push_back("WAIT_BLOCK " + new_part + " 2000");
    // Wipe the newly-created partition too. Old btrfs signatures may live
    // inside the partition range even after wiping the parent disk.
    commands.push_back("RAID_ROOT wipefs " + new_part);
    commands.push_back("RAID_ROOT btrfs-device-add " + new_part + " " + resolved_mount);
    if (mode == "raid1") {
        commands.push_back("RAID_ROOT btrfs-balance-raid1 " + resolved_mount);
        commands.push_back(std::string("POOLS_CFG_SET_MODE ") + resolved_mount + " raid1");
    }

    // plan_id check (must match exactly)
    const std::string joined = join_commands_for_hash(commands);

    // Salt with current FS membership/state (MUST match plan/add-device).
    const std::string salt =
        std::string("mount=") + resolved_mount + "\n" +
        std::string("btrfs_membership_fp=") + membership_fp + "\n";

    // include per-attempt nonce
    const std::string expected_plan_id =
        sha256_hex_lower_evp(joined + "\n" + salt + "plan_nonce=" + client_plan_nonce + "\n");

    if (expected_plan_id.empty()) {
        audit_fail(actor_fp, "plan_id_compute_failed", 500,
                   "", json{{"mount", resolved_mount}, {"new_disk", new_disk}});
        reply_json(res, 500, json{{"ok", false}, {"error", "plan_id_compute_failed"}}.dump());
        return;
    }
    if (client_plan_id != expected_plan_id) {
        audit_fail(actor_fp, "plan_mismatch", 400, "",
                   json{{"expected_plan_id", expected_plan_id}, {"provided_plan_id", client_plan_id}});
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "plan_mismatch"},
            {"message", "plan_id does not match server recomputed plan"},
            {"expected_plan_id", expected_plan_id},
            {"provided_plan_id", client_plan_id}
        }.dump());
        return;
    }

    // Prepare response plan payload (for caller/UI)
    json plan;
    plan["plan_id"] = expected_plan_id;
    plan["plan_nonce"] = client_plan_nonce;
    plan["btrfs_membership_fp"] = membership_fp;
    plan["mount"] = resolved_mount;
    plan["input_mount"] = mount;
    plan["resolved_mount"] = resolved_mount;
    plan["resolved_source"] = resolved_source;
    if (!resolved_disk.empty()) plan["resolved_disk"] = resolved_disk;
    plan["fstype"] = fstype_out;
    plan["new_disk"] = new_disk;
    plan["new_partition"] = new_part;
    plan["new_disk_index"] = disk_index;
    plan["new_disk_size_bytes"] = new_disk_size;
    plan["mode"] = mode;
    plan["force"] = force;
    plan["requires_downtime"] = false;
    plan["commands"] = commands;
	plan["actor_fp"] = actor_fp;

    // Preflight AFTER plan-id verification: if device already present, return idempotent success
    if (btrfs_filesystem_has_device(resolved_mount, new_part)) {
        json status = storage_btrfs_status_json(resolved_mount);
        status["input_mount"] = mount;
        status["resolved_mount"] = resolved_mount;
        status["resolved_source"] = resolved_source;
        status["resolved_disk"] = resolved_disk;
        status["fstype"] = fstype_out;

        audit_ok(actor_fp, json{
            {"dry_run", dry_run},
            {"skipped", true},
            {"skip_reason", "already_in_filesystem"},
            {"mount", resolved_mount},
            {"new_disk", new_disk},
            {"new_partition", new_part},
            {"mode", mode},
            {"force", force},
            {"plan_id", expected_plan_id}
        });

        reply_json(res, 200, json{
            {"ok", true},
            {"dry_run", dry_run},
            {"skipped", true},
            {"skip_reason", "already_in_filesystem"},
            {"mount", resolved_mount},
            {"device", new_part},
            {"plan_id", expected_plan_id},
            {"plan", plan},
            {"post_status", status}
        }.dump());
        return;
    }

    if (dry_run) {
        audit_ok(actor_fp, json{
            {"dry_run", true},
            {"mount", resolved_mount},
            {"new_disk", new_disk},
            {"new_partition", new_part},
            {"mode", mode},
            {"force", force},
            {"plan_id", expected_plan_id},
            {"commands", (int)commands.size()}
        });

        reply_json(res, 200, json{
            {"ok", true},
            {"dry_run", true},
            {"plan", plan}
        }.dump());
        return;
    }

    // Async enqueue (fail-closed): create canonical queued record + return immediately.
    try {
        json q = raid_enqueue_job_fail_closed(expected_plan_id, resolved_mount, plan, commands);
			json extra = {
  			{"dry_run", false},
  			{"enqueued", true},
  			{"mount", resolved_mount},
  			{"new_disk", new_disk},
  			{"new_partition", new_part},
  			{"mode", mode},
  			{"force", force},
  			{"plan_id", expected_plan_id},
  			{"plan_nonce", client_plan_nonce}
			};

			try {
	    		if (q.contains("job_id")) extra["job_id"] = q["job_id"];
			} catch (...) {}

		audit_ok(actor_fp, extra);

        // Keep UX fields consistent with old response shape
        q["dry_run"] = false;
        q["plan"] = plan;
        reply_json(res, 200, q.dump());
        return;
    } catch (const std::exception& e) {
        const std::string msg = e.what();

        if (msg == "already_running") {
            audit_fail(actor_fp, "already_running", 409, "",
                       json{{"plan_id", expected_plan_id}, {"mount", resolved_mount}});
            reply_json(res, 409, json{
                {"ok", false},
                {"error", "already_running"},
                {"message", "this plan_id already has a running execution record; refusing replay"},
                {"plan_id", expected_plan_id},
                {"record_path", raid_exec_record_path(expected_plan_id)}
            }.dump());
            return;
        }

        if (msg.rfind("raid_state_dir_failed:", 0) == 0) {
            audit_fail(actor_fp, "raid_state_dir_failed", 500, msg,
                       json{{"plan_id", expected_plan_id}, {"mount", resolved_mount}});
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "raid_state_dir_failed"},
                {"message", "cannot create/verify /run/pqnas/raid; refusing to execute"},
                {"detail", msg}
            }.dump());
            return;
        }

        audit_fail(actor_fp, "enqueue_failed", 500, msg,
                   json{{"plan_id", expected_plan_id}, {"mount", resolved_mount}});
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "enqueue_failed"},
            {"detail", msg}
        }.dump());
        return;
    }
});
// ----- POST /api/v4/raid/execute/destroy-pool (admin-only) --------------------
// Body: { mount, plan_id, plan_nonce, confirm:true, force_wipe?:bool }
srv.Post("/api/v4/raid/execute/destroy-pool", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;
    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}}.dump());
        return;
    }

    // ---- audit helpers ----
    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto audit_kv_merge = [&](pqnas::AuditEvent& ev, const json& extra) {
        if (!extra.is_object()) return;
        for (auto it = extra.begin(); it != extra.end(); ++it) {
            const std::string k  = pqnas::shorten(it.key(), 64);
            const std::string kk = "x_" + k;
            if (it.value().is_string()) ev.f[kk] = pqnas::shorten(it.value().get<std::string>(), 220);
            else if (it.value().is_number_integer() || it.value().is_number_unsigned()) ev.f[kk] = it.value().dump();
            else if (it.value().is_boolean()) ev.f[kk] = (it.value().get<bool>() ? "true" : "false");
            else ev.f[kk] = pqnas::shorten(it.value().dump(), 220);
        }
    };

    auto audit_common = [&](pqnas::AuditEvent& ev) {
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
    };

    auto audit_fail = [&](const std::string& actor_fp,
                          const std::string& reason,
                          int http,
                          const std::string& detail = "",
                          const json& extra = json::object()) {
        pqnas::AuditEvent ev;
        ev.event   = "v4.raid_execute_destroy_pool_fail";
        ev.outcome = "fail";
        if (!actor_fp.empty()) ev.f["fingerprint"] = actor_fp;
        ev.f["reason"] = reason;
        ev.f["http"]   = std::to_string(http);
        if (!detail.empty()) ev.f["detail"] = pqnas::shorten(detail, 220);
        audit_kv_merge(ev, extra);
        audit_common(ev);
        audit_append(ev);
    };

    auto audit_ok = [&](const std::string& actor_fp,
                        const json& extra = json::object()) {
        pqnas::AuditEvent ev;
        ev.event   = "v4.raid_execute_destroy_pool_ok";
        ev.outcome = "ok";
        if (!actor_fp.empty()) ev.f["fingerprint"] = actor_fp;
        audit_kv_merge(ev, extra);
        audit_common(ev);
        audit_append(ev);
    };

    // ---- auth (need actor fingerprint for audit) ----
    std::string actor_fp;
    if (!require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, &actor_fp)) return;
        if (!require_same_origin_for_cookie_mutation(req, res)) return;

    json in;
    try { in = json::parse(req.body.empty() ? "{}" : req.body); }
    catch (...) {
        audit_fail(actor_fp, "invalid_json", 400);
        reply_json(res, 400, json{{"ok", false}, {"error", "invalid_json"}}.dump());
        return;
    }

    std::string mount = in.value("mount", "");
    const std::string plan_id    = in.value("plan_id", "");
    const std::string plan_nonce = in.value("plan_nonce", "");
    const bool confirm           = in.value("confirm", false);
    const bool force_wipe        = in.value("force_wipe", false);

    // Will be filled after findmnt succeeds; used by audit_ctx().
    std::string resolved_mount;
    std::string resolved_source;

    // Consistent audit context keys across create-pool / worker.
    auto audit_ctx = [&](const json& extra = json::object()) -> json {
        json j = {
            {"plan_id", plan_id},
            {"plan_nonce", plan_nonce},
            {"job_id", plan_id},
            {"op", "destroy-pool"},

            {"mount", mount},
            {"resolved_mount", resolved_mount.empty() ? mount : resolved_mount},
            {"resolved_source", resolved_source},

            {"confirm", confirm},
            {"force_wipe", force_wipe},

            {"recp", raid_exec_record_path(plan_id)}
        };
        if (extra.is_object()) {
            for (auto it = extra.begin(); it != extra.end(); ++it) j[it.key()] = it.value();
        }
        return j;
    };

    if (!confirm || plan_id.empty() || plan_nonce.empty() || mount.empty()) {
        audit_fail(actor_fp, "bad_request_missing_fields", 400, "",
                   audit_ctx(json{{"message", "requires confirm=true, plan_id, plan_nonce, mount"}}));
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "requires confirm=true, plan_id, plan_nonce, mount"}
        }.dump());
        return;
    }

    // Plan id format: allow safe printable ids (align with create-pool usage like "usbclean1-plan-1")
    if (!std::regex_match(plan_id, std::regex("^[a-zA-Z0-9_.:-]{1,96}$"))) {
        audit_fail(actor_fp, "bad_plan_id_format", 400, "",
                   audit_ctx(json{{"plan_id", plan_id}}));
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "plan_id contains invalid characters"},
            {"plan_id", plan_id}
        }.dump());
        return;
    }

    // Basic mount validation
    if (!is_abs_path_safe(mount)) {
        audit_fail(actor_fp, "bad_mount", 400, "", audit_ctx());
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mount"}}.dump());
        return;
    }

    // Allow only destroying pools under PQNAS_STORAGE_ROOT/pools
    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";

    const std::string pools_root = allowed_prefix + "/pools/";
    if (mount.rfind(pools_root, 0) != 0) {
        audit_fail(actor_fp, "mount_not_allowed", 400, "",
                   audit_ctx(json{{"pools_root", pools_root}}));
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "mount_not_allowed"},
            {"message", "destroy is only allowed under PQNAS_STORAGE_ROOT/pools"},
            {"mount", mount},
            {"pools_root", pools_root}
        }.dump());
        return;
    }

    // Must be a mounted btrfs target (we want stable membership list before umount)
    std::string target_out, fstype_out, source_out;
    int ec_target = 0, ec_fs = 0, ec_src = 0;

    ec_target = run_findmnt_no_target_argv("TARGET", mount, &target_out);
    const bool ok_target = (ec_target == 0);
    cap_string(target_out, 16 * 1024);
    rtrim_inplace(target_out);

    ec_fs = run_findmnt_no_target_argv("FSTYPE", mount, &fstype_out);
    const bool ok_fs = (ec_fs == 0);
    cap_string(fstype_out, 16 * 1024);
    rtrim_inplace(fstype_out);

    ec_src = run_findmnt_no_target_argv("SOURCE", mount, &source_out);
    const bool ok_src = (ec_src == 0);
    cap_string(source_out, 16 * 1024);
    rtrim_inplace(source_out);

    if (!ok_target || ec_target != 0 || target_out.empty() ||
        !ok_fs     || ec_fs     != 0 || fstype_out.empty() ||
        !ok_src    || ec_src    != 0 || source_out.empty()) {

        audit_fail(actor_fp, "mount_not_found", 400, "",
                   audit_ctx(json{
                       {"findmnt_rc_target", ec_target},
                       {"findmnt_rc_fs", ec_fs},
                       {"findmnt_rc_src", ec_src}
                   }));
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "mount_not_found"},
            {"mount", mount}
        }.dump());
        return;
    }

    resolved_mount  = target_out;
    resolved_source = source_out;

    if (fstype_out != "btrfs") {
        audit_fail(actor_fp, "not_btrfs", 400, "",
                   audit_ctx(json{{"fstype", fstype_out}}));
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "not_btrfs"},
            {"resolved_mount", resolved_mount},
            {"fstype", fstype_out}
        }.dump());
        return;
    }

    // Lock check only (worker owns lock lifecycle)
    {
        std::string raid_dir_err;
        if (!ensure_dir_fail_closed("/run/pqnas/raid", &raid_dir_err)) {
            audit_fail(actor_fp, "raid_state_dir_failed", 500, raid_dir_err,
                       audit_ctx(json{{"resolved_mount", resolved_mount}}));
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "raid_state_dir_failed"},
                {"detail", raid_dir_err}
            }.dump());
            return;
        }

        const std::string lockp = raid_mount_lock_path(resolved_mount);
        std::error_code ec;
        if (std::filesystem::exists(lockp, ec)) {
            audit_fail(actor_fp, "raid_busy", 409, "",
                       audit_ctx(json{{"state","blocked"}, {"lock_path", lockp}}));
            reply_json(res, 409, json{{"ok", false}, {"error", "raid_busy"}, {"lock_path", lockp}}.dump());
            return;
        }
    }

    // Read membership BEFORE umount so we can optionally wipe member disks
    std::string show_raw;
    int ec_show = 0;
    // Security: call the read-only btrfs-status helper via argv, not a
    // shell command string, so resolved mount targets cannot be shell-interpreted.
    const bool ok_show = run_btrfs_status_helper_argv("filesystem-show", resolved_mount, &show_raw, &ec_show);
    cap_string(show_raw, 256 * 1024);
    rtrim_inplace(show_raw);

    if (!ok_show || ec_show != 0 || show_raw.empty()) {
        audit_fail(actor_fp, "btrfs_show_failed", 500, "",
                   audit_ctx(json{{"btrfs_show_rc", ec_show}}));
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "btrfs_show_failed"},
            {"resolved_mount", resolved_mount},
            {"btrfs_show_rc", ec_show}
        }.dump());
        return;
    }

    // Parse show -> get member device paths
    std::string raw_lsblk;
    json disks_j = storage_list_disks_json(&raw_lsblk);
    json by_path = disks_j.value("by_path", json::object());

    BtrfsShowParsed parsed = parse_btrfs_filesystem_show(show_raw);
    json btrfs_j = btrfs_show_parsed_to_json(parsed, by_path, disks_j.value("by_name", json::object()));

    std::vector<std::string> member_devs;
    if (btrfs_j.contains("devices") && btrfs_j["devices"].is_array()) {
        for (auto& d : btrfs_j["devices"]) {
            const std::string p = d.value("path", "");
            if (!p.empty() && p.rfind("/dev/", 0) == 0) member_devs.push_back(p);
        }
    }

    // Build plan + commands for worker
    json plan;
    plan["plan_id"]         = plan_id;
    plan["plan_nonce"]      = plan_nonce;
    plan["operation"]       = "destroy-pool";
    plan["mount"]           = resolved_mount;
    plan["input_mount"]     = mount;
    plan["resolved_mount"]  = resolved_mount;
    plan["resolved_source"] = resolved_source;
    plan["force_wipe"]      = force_wipe;
    plan["member_devices"]  = member_devs;
    plan["actor_fp"]        = actor_fp;

    json commands = json::array();

    // Security: use RAID_ROOT so destroy-pool root-helper steps route
    // through argv, not shell command strings.
    commands.push_back("RAID_ROOT udev-settle");
    commands.push_back("RAID_ROOT umount-pool " + resolved_mount);
    commands.push_back("RAID_ROOT btrfs-device-scan");

    if (force_wipe) {
        for (const auto& dev : member_devs) {
            commands.push_back("RAID_ROOT wipefs " + dev);
            commands.push_back("RAID_ROOT zap-disk " + dev);
        }
        commands.push_back("RAID_ROOT udev-settle");
    }

    commands.push_back(std::string("POOLS_CFG_REMOVE ") + resolved_mount);
    commands.push_back(std::string("FSTAB_REMOVE ") + resolved_mount);
    commands.push_back("RAID_ROOT rmdir-pool " + resolved_mount);

    // Enqueue (fail-closed)
    try {
        json q = raid_enqueue_job_fail_closed(plan_id, resolved_mount, plan, commands);
        q["plan"] = plan;

        // IMPORTANT: this is "enqueue accepted", NOT "job started" (worker emits job lifecycle)
        audit_ok(actor_fp, audit_ctx(json{
            {"state", "queued"},
            {"member_devices_n", (int)member_devs.size()},
            {"commands_total", (int)commands.size()}
        }));

        reply_json(res, 200, q.dump());
        return;

    } catch (const std::exception& e) {
        const std::string msg = e.what();

        if (msg == "already_running") {
            audit_fail(actor_fp, "already_running", 409, "",
                       audit_ctx(json{{"state","blocked"}}));
            reply_json(res, 409, json{
                {"ok", false},
                {"error", "already_running"},
                {"message", "this plan_id already has a running execution record; refusing replay"},
                {"plan_id", plan_id},
                {"record_path", raid_exec_record_path(plan_id)}
            }.dump());
            return;
        }

        audit_fail(actor_fp, "enqueue_failed", 500, msg,
                   audit_ctx(json{{"state","enqueue_failed"}}));

        reply_json(res, 500, json{
            {"ok", false},
            {"error", "enqueue_failed"},
            {"detail", msg}
        }.dump());
        return;
    }
});
// ----- POST /api/v4/raid/execute/remove-device (admin-only) ------------------
// Body: { mount, remove_device, force:bool, plan_id:string, dry_run?:bool, confirm?:bool }
srv.Post("/api/v4/raid/execute/remove-device", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;

    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}, {"path", users_path}}.dump());
        return;
    }

    // ---- audit helpers ----
    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto audit_kv_merge = [&](pqnas::AuditEvent& ev, const json& extra) {
        if (!extra.is_object()) return;
        for (auto it = extra.begin(); it != extra.end(); ++it) {
            const std::string k = pqnas::shorten(it.key(), 64);
            const std::string kk = "x_" + k;
            if (it.value().is_string()) ev.f[kk] = pqnas::shorten(it.value().get<std::string>(), 220);
            else if (it.value().is_number_integer() || it.value().is_number_unsigned()) ev.f[kk] = it.value().dump();
            else if (it.value().is_boolean()) ev.f[kk] = (it.value().get<bool>() ? "true" : "false");
            else ev.f[kk] = pqnas::shorten(it.value().dump(), 220);
        }
    };

    auto audit_common = [&](pqnas::AuditEvent& ev) {
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
    };

    auto audit_fail = [&](const std::string& actor_fp,
                          const std::string& reason,
                          int http,
                          const std::string& detail = "",
                          const json& extra = json::object()) {
        pqnas::AuditEvent ev;
        ev.event = "v4.raid_execute_remove_device_fail";
        ev.outcome = "fail";
        if (!actor_fp.empty()) ev.f["fingerprint"] = actor_fp;
        ev.f["reason"] = reason;
        ev.f["http"] = std::to_string(http);
        if (!detail.empty()) ev.f["detail"] = pqnas::shorten(detail, 220);
        audit_kv_merge(ev, extra);
        audit_common(ev);
        audit_append(ev);
    };

    auto audit_ok = [&](const std::string& actor_fp,
                        const json& extra = json::object()) {
        pqnas::AuditEvent ev;
        ev.event = "v4.raid_execute_remove_device_ok";
        ev.outcome = "ok";
        if (!actor_fp.empty()) ev.f["fingerprint"] = actor_fp;
        audit_kv_merge(ev, extra);
        audit_common(ev);
        audit_append(ev);
    };

    // ---- auth (need actor fingerprint for audit) ----
    std::string actor_fp;
    if (!require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, &actor_fp)) return;
    if (!require_same_origin_for_cookie_mutation(req, res)) {
        audit_fail(actor_fp, "origin_mismatch", 403);
        return;
    }

    json in;
    try { in = json::parse(req.body.empty() ? "{}" : req.body); }
    catch (...) {
        audit_fail(actor_fp, "bad_json", 400);
        reply_json(res, 400, json({{"ok",false},{"error","bad_request"},{"message","invalid json"}}).dump());
        return;
    }

    // Inputs
    std::string mount         = in.value("mount", "");
    std::string remove_device = in.value("remove_device", "");
    bool force                = in.value("force", false);

    // Safety: default dry_run=true
    bool dry_run = in.value("dry_run", true);
    bool confirm = in.value("confirm", false);

    const std::string client_plan_id = in.value("plan_id", "");

    // Allowed_prefix + default mount
    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";
    if (mount.empty()) mount = allowed_prefix + "/data";

    // Validate inputs
    if (!is_abs_path_safe(mount)) {
        audit_fail(actor_fp, "bad_mount", 400, "", json{{"mount", mount}});
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mount"}}.dump());
        return;
    }
    if (!is_dev_path_basic_safe(remove_device)) {
        audit_fail(actor_fp, "bad_device", 400, "", json{{"remove_device", remove_device}});
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_device"}, {"message","expected /dev/..."} }.dump());
        return;
    }
    if (client_plan_id.empty()) {
        audit_fail(actor_fp, "missing_plan_id", 400);
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message","missing plan_id"} }.dump());
        return;
    }

    // If not dry-run, require explicit confirm=true
    if (!dry_run && !confirm) {
        audit_fail(actor_fp, "confirm_required", 400, "", json{{"dry_run", dry_run}});
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "confirm_required"},
            {"message", "set confirm=true when dry_run=false"}
        }.dump());
        return;
    }

    // Resolve mount -> resolved_mount / source / fstype
    std::string target_out, fstype_out, source_out;
    int ec_target = 0, ec_fs = 0, ec_src = 0;

    ec_target = run_findmnt_no_target_argv("TARGET", mount, &target_out);
    const bool ok_target = (ec_target == 0);
    cap_string(target_out, 16 * 1024);
    rtrim_inplace(target_out);

    ec_fs = run_findmnt_no_target_argv("FSTYPE", mount, &fstype_out);
    const bool ok_fs = (ec_fs == 0);
    cap_string(fstype_out, 16 * 1024);
    rtrim_inplace(fstype_out);

    ec_src = run_findmnt_no_target_argv("SOURCE", mount, &source_out);
    const bool ok_src = (ec_src == 0);
    cap_string(source_out, 16 * 1024);
    rtrim_inplace(source_out);

    if (!ok_target || ec_target != 0 || target_out.empty() ||
        !ok_fs     || ec_fs     != 0 || fstype_out.empty() ||
        !ok_src    || ec_src    != 0 || source_out.empty()) {

        audit_fail(actor_fp, "mount_not_found", 200, "",
                   json{{"mount", mount}, {"ec_target", ec_target}, {"ec_fs", ec_fs}, {"ec_src", ec_src}});
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "mount_not_found"},
            {"mount", mount}
        }.dump());
        return;
    }

    const std::string resolved_mount  = target_out;
    const std::string resolved_source = source_out;
    const std::string resolved_disk   = parent_disk_from_dev(resolved_source);
    const std::string system_root_disk = detect_system_pool_root_disk();

    // Allowlist on resolved mount
    if (resolved_mount.rfind(allowed_prefix, 0) != 0) {
        const std::string test_prefix  = "/srv/pqnas-test";
        const std::string test_prefix2 = "/srv/pqnas-test-btrfs";
        if (resolved_mount.rfind(test_prefix, 0) != 0 && resolved_mount.rfind(test_prefix2, 0) != 0) {
            audit_fail(actor_fp, "mount_not_allowed", 200, "",
                       json{{"allowed_prefix", allowed_prefix}, {"resolved_mount", resolved_mount}});
            reply_json(res, 200, json{
                {"ok", false},
                {"error", "mount_not_allowed"},
                {"allowed_prefix", allowed_prefix},
                {"resolved_mount", resolved_mount}
            }.dump());
            return;
        }
    }

    if (fstype_out != "btrfs") {
        audit_fail(actor_fp, "not_btrfs", 200, "",
                   json{{"resolved_mount", resolved_mount}, {"fstype", fstype_out}});
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "not_btrfs"},
            {"resolved_mount", resolved_mount},
            {"fstype", fstype_out}
        }.dump());
        return;
    }

    // Load disks allowlist (inherits PQNAS_STORAGE_ALLOW_LOOP policy)
    std::string raw_lsblk;
    json disks_j = storage_list_disks_json(&raw_lsblk);
    json by_path = disks_j.value("by_path", json::object());

    // Read btrfs filesystem show to map /dev/disk -> member path
    std::string show_raw;
    int ec_show = 0;
    // Security: call the read-only btrfs-status helper via argv, not a
    // shell command string, so resolved mount targets cannot be shell-interpreted.
    const bool ok_show = run_btrfs_status_helper_argv("filesystem-show", resolved_mount, &show_raw, &ec_show);
    cap_string(show_raw, 256 * 1024);

    if (!ok_show || ec_show != 0 || show_raw.empty()) {
        audit_fail(actor_fp, "btrfs_show_failed", 200, "",
                   json{{"resolved_mount", resolved_mount}, {"btrfs_show_rc", ec_show}});
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "btrfs_show_failed"},
            {"resolved_mount", resolved_mount},
            {"btrfs_show_rc", ec_show}
        }.dump());
        return;
    }

    BtrfsShowParsed parsed = parse_btrfs_filesystem_show(show_raw);
    json btrfs_j = btrfs_show_parsed_to_json(parsed, by_path, disks_j.value("by_name", json::object()));

    const std::string membership_fp = btrfs_membership_fingerprint(btrfs_j);
    if (membership_fp.empty()) {
        audit_fail(actor_fp, "membership_fp_failed", 500,
                   "failed to compute btrfs membership fingerprint",
                   json{{"resolved_mount", resolved_mount}});
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "membership_fp_failed"},
            {"message", "failed to compute btrfs membership fingerprint"}
        }.dump());
        return;
    }

    // Find member device in filesystem corresponding to remove_device
    std::string member_path;
    std::string parent_disk;

    if (btrfs_j.contains("devices") && btrfs_j["devices"].is_array()) {
        for (const auto& dev : btrfs_j["devices"]) {
            if (!dev.is_object()) continue;
            const std::string p  = dev.value("path", "");
            const std::string pd = dev.value("parent_disk", "");
            if (p.empty()) continue;

            if (remove_device == p || (!pd.empty() && remove_device == pd)) {
                member_path = p;
                parent_disk = pd;
                break;
            }
        }
    }

    if (member_path.empty()) {
        // idempotent "skipped"
        audit_ok(actor_fp, json{
            {"dry_run", dry_run},
            {"skipped", true},
            {"skip_reason", "already_not_in_filesystem"},
            {"mount", resolved_mount},
            {"remove_device", remove_device},
            {"plan_id", client_plan_id}
        });

        reply_json(res, 200, json{
            {"ok", true},
            {"dry_run", dry_run},
            {"skipped", true},
            {"skip_reason", "already_not_in_filesystem"},
            {"mount", resolved_mount},
            {"remove_device", remove_device},
            {"plan_id", client_plan_id}
        }.dump());
        return;
    }

    if (!system_root_disk.empty() && !parent_disk.empty() && parent_disk == system_root_disk) {
        json extra = json::object();
        extra["system_root_disk"] = system_root_disk;
        extra["remove_device"] = remove_device;
        extra["parent_disk"] = parent_disk;

        audit_fail(actor_fp, "device_is_system_root_disk", 400, "", extra);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "device_is_system_root_disk"},
            {"system_root_disk", system_root_disk},
            {"remove_device", remove_device},
            {"parent_disk", parent_disk}
        }.dump());
        return;
    }

    // Allowlist enforcement (prefer parent disk)
    if (!by_path.is_object() ||
        ((!parent_disk.empty() && !by_path.contains(parent_disk)) &&
         (parent_disk.empty() && !by_path.contains(remove_device)))) {
        audit_fail(actor_fp, "device_not_allowed", 400, "",
                   json{{"remove_device", remove_device}, {"member_path", member_path}, {"parent_disk", parent_disk}});
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "device_not_allowed"},
            {"remove_device", remove_device},
            {"member_path", member_path},
            {"parent_disk", parent_disk}
        }.dump());
        return;
    }

    // Refuse removing current filesystem disk unless force=true
    if (!resolved_disk.empty() && !parent_disk.empty() && parent_disk == resolved_disk && !force) {
        audit_fail(actor_fp, "device_is_current_disk", 400, "",
                   json{{"resolved_disk", resolved_disk}, {"parent_disk", parent_disk}, {"remove_device", remove_device}});
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "device_is_current_disk"},
            {"resolved_disk", resolved_disk},
            {"parent_disk", parent_disk},
            {"remove_device", remove_device}
        }.dump());
        return;
    }

    // Refuse removing if it's the last remaining device
    const int total_devices = btrfs_j.value("total_devices", 0);
    if (total_devices <= 1) {
        audit_fail(actor_fp, "cannot_remove_last_device", 400, "",
                   json{{"total_devices", total_devices}, {"member_path", member_path}, {"mount", resolved_mount}});
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "cannot_remove_last_device"},
            {"total_devices", total_devices},
            {"member_path", member_path},
            {"mount", resolved_mount}
        }.dump());
        return;
    }

    // -------- Build commands exactly like plan endpoint --------
    json commands = json::array();

    // If this removal would drop from 2 devices -> 1 device, we must convert off RAID1 first
    // Security: use RAID_ROOT so device-remove root-helper steps route through
    // argv, not shell command strings.
    if (total_devices == 2) {
        commands.push_back("RAID_ROOT btrfs-balance-single-force " + resolved_mount);
    }

    commands.push_back("RAID_ROOT btrfs-device-remove " + member_path + " " + resolved_mount);

    // plan_id check (must match exactly plan endpoint)
    const std::string joined = join_commands_for_hash(commands);

    // Salt plan_id with current FS membership/state so repeats after add/remove
    // don't collide with old execution records (MUST match plan/remove-device).
    const std::string salt =
        std::string("mount=") + resolved_mount + "\n" +
        std::string("btrfs_membership_fp=") + membership_fp + "\n";

    const std::string expected_plan_id = sha256_hex_lower_evp(joined + "\n" + salt);

    if (expected_plan_id.empty()) {
        audit_fail(actor_fp, "plan_id_compute_failed", 500, "",
                   json{{"mount", resolved_mount}, {"remove_device", remove_device}});
        reply_json(res, 500, json{{"ok", false}, {"error", "plan_id_compute_failed"}}.dump());
        return;
    }
    if (client_plan_id != expected_plan_id) {
        audit_fail(actor_fp, "plan_mismatch", 400, "",
                   json{{"expected_plan_id", expected_plan_id}, {"provided_plan_id", client_plan_id}});
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "plan_mismatch"},
            {"message", "plan_id does not match server recomputed plan"},
            {"expected_plan_id", expected_plan_id},
            {"provided_plan_id", client_plan_id}
        }.dump());
        return;
    }

    // Prepare response plan payload (for caller/UI)
    json plan;
    plan["plan_id"] = expected_plan_id;
    plan["btrfs_membership_fp"] = membership_fp;
    plan["mount"] = resolved_mount;
    plan["input_mount"] = mount;
    plan["resolved_mount"] = resolved_mount;
    plan["resolved_source"] = resolved_source;
    if (!resolved_disk.empty()) plan["resolved_disk"] = resolved_disk;
    plan["fstype"] = fstype_out;
    plan["remove_device"] = remove_device;
    plan["remove_member_path"] = member_path;
    if (!parent_disk.empty()) plan["remove_parent_disk"] = parent_disk;
    plan["force"] = force;
    plan["requires_downtime"] = false;
    plan["commands"] = commands;
	plan["actor_fp"] = actor_fp;

    if (dry_run) {
        audit_ok(actor_fp, json{
            {"dry_run", true},
            {"mount", resolved_mount},
            {"remove_device", remove_device},
            {"member_path", member_path},
            {"parent_disk", parent_disk},
            {"force", force},
            {"plan_id", expected_plan_id},
            {"total_devices", total_devices},
            {"commands", (int)commands.size()}
        });

        reply_json(res, 200, json{
            {"ok", true},
            {"dry_run", true},
            {"plan", plan}
        }.dump());
        return;
    }

    // Async enqueue (fail-closed): create canonical queued record + return immediately.
    try {
        json q = raid_enqueue_job_fail_closed(expected_plan_id, resolved_mount, plan, commands);
		try { if (q.contains("job_id")) plan["job_id"] = q["job_id"]; } catch (...) {}
        audit_ok(actor_fp, json{
            {"dry_run", false},
            {"enqueued", true},
            {"mount", resolved_mount},
            {"remove_device", remove_device},
            {"member_path", member_path},
            {"parent_disk", parent_disk},
            {"force", force},
            {"plan_id", expected_plan_id},
            {"total_devices", total_devices}
            // optional: if q has "job_id" you can log it explicitly if you want
        });

        q["dry_run"] = false;
        q["plan"] = plan;
        reply_json(res, 200, q.dump());
        return;
    } catch (const std::exception& e) {
        const std::string msg = e.what();

        if (msg == "already_running") {
            audit_fail(actor_fp, "already_running", 409, "",
                       json{{"plan_id", expected_plan_id}, {"mount", resolved_mount}});
            reply_json(res, 409, json{
                {"ok", false},
                {"error", "already_running"},
                {"message", "this plan_id already has a running execution record; refusing replay"},
                {"plan_id", expected_plan_id},
                {"record_path", raid_exec_record_path(expected_plan_id)}
            }.dump());
            return;
        }

        if (msg.rfind("raid_state_dir_failed:", 0) == 0) {
            audit_fail(actor_fp, "raid_state_dir_failed", 500, msg,
                       json{{"plan_id", expected_plan_id}, {"mount", resolved_mount}});
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "raid_state_dir_failed"},
                {"message", "cannot create/verify /run/pqnas/raid; refusing to execute"},
                {"detail", msg}
            }.dump());
            return;
        }

        audit_fail(actor_fp, "enqueue_failed", 500, msg,
                   json{{"plan_id", expected_plan_id}, {"mount", resolved_mount}});
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "enqueue_failed"},
            {"detail", msg}
        }.dump());
        return;
    }
});
// ----- POST /api/v4/raid/execute/create-pool (admin-only) --------------------
srv.Post("/api/v4/raid/execute/create-pool", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;
    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}}.dump());
        return;
    }

    // ---- audit helpers ----
    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto audit_kv_merge = [&](pqnas::AuditEvent& ev, const json& extra) {
        if (!extra.is_object()) return;
        for (auto it = extra.begin(); it != extra.end(); ++it) {
            const std::string k  = pqnas::shorten(it.key(), 64);
            const std::string kk = "x_" + k;
            if (it.value().is_string()) ev.f[kk] = pqnas::shorten(it.value().get<std::string>(), 220);
            else if (it.value().is_number_integer() || it.value().is_number_unsigned()) ev.f[kk] = it.value().dump();
            else if (it.value().is_boolean()) ev.f[kk] = (it.value().get<bool>() ? "true" : "false");
            else ev.f[kk] = pqnas::shorten(it.value().dump(), 220);
        }
    };

    auto audit_common = [&](pqnas::AuditEvent& ev) {
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
    };

    auto audit_fail = [&](const std::string& actor_fp,
                          const std::string& reason,
                          int http,
                          const std::string& detail = "",
                          const json& extra = json::object()) {
        pqnas::AuditEvent ev;
        ev.event   = "v4.raid_execute_create_pool_fail";
        ev.outcome = "fail";
        if (!actor_fp.empty()) ev.f["fingerprint"] = actor_fp;
        ev.f["reason"] = reason;
        ev.f["http"]   = std::to_string(http);
        if (!detail.empty()) ev.f["detail"] = pqnas::shorten(detail, 220);
        audit_kv_merge(ev, extra);
        audit_common(ev);
        audit_append(ev);
    };

    auto audit_ok = [&](const std::string& actor_fp,
                        const json& extra = json::object()) {
        pqnas::AuditEvent ev;
        ev.event   = "v4.raid_execute_create_pool_ok";
        ev.outcome = "ok";
        if (!actor_fp.empty()) ev.f["fingerprint"] = actor_fp;
        audit_kv_merge(ev, extra);
        audit_common(ev);
        audit_append(ev);
    };

    // job lifecycle audits
    auto audit_job_start_ok = [&](const std::string& actor_fp,
                                  const json& extra = json::object()) {
        pqnas::AuditEvent ev;
        ev.event   = "v4.raid_job_start_ok";
        ev.outcome = "ok";
        if (!actor_fp.empty()) ev.f["fingerprint"] = actor_fp;
        audit_kv_merge(ev, extra);
        audit_common(ev);
        audit_append(ev);
    };

    auto audit_job_finish = [&](const std::string& actor_fp,
                                bool ok,
                                const std::string& detail = "",
                                const json& extra = json::object()) {
        pqnas::AuditEvent ev;
        ev.event   = ok ? "v4.raid_job_finish_ok" : "v4.raid_job_finish_fail";
        ev.outcome = ok ? "ok" : "fail";
        if (!actor_fp.empty()) ev.f["fingerprint"] = actor_fp;
        if (!detail.empty()) ev.f["detail"] = pqnas::shorten(detail, 220);
        audit_kv_merge(ev, extra);
        audit_common(ev);
        audit_append(ev);
    };

    // ---- auth (need actor fingerprint for audit) ----
    std::string actor_fp;
    if (!require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, &actor_fp)) return;
    if (!require_same_origin_for_cookie_mutation(req, res)) {
        audit_fail(actor_fp, "origin_mismatch", 403);
        return;
    }

    json in;
    try { in = json::parse(req.body.empty() ? "{}" : req.body); }
    catch (...) {
        audit_fail(actor_fp, "invalid_json", 400);
        reply_json(res, 400, json{{"ok", false}, {"error", "invalid_json"}}.dump());
        return;
    }

    const std::string plan_id    = trim_copy(in.value("plan_id", ""));
    const std::string plan_nonce = trim_copy(in.value("plan_nonce", ""));
    const bool confirm           = in.value("confirm", false);

    const std::string pool_id = trim_copy(in.value("pool_id", ""));
    const std::string mode    = trim_copy(in.value("mode", "single"));
    const bool force          = in.value("force", false);

    json devices_json = json::array();
    if (in.contains("devices")) devices_json = in["devices"];

    std::vector<std::string> devices; // canonical validated device list

    std::string root = getenv_str("PQNAS_STORAGE_ROOT");
    if (root.empty()) root = "/srv/pqnas";
    const std::string mount = root + "/pools/" + pool_id;
    const std::string label = "PQNAS_" + upper_ascii(pool_id);

    auto audit_ctx = [&](const json& extra = json::object()) -> json {
        json j = {
            {"op", "create-pool"},
            {"confirm", confirm},
            {"plan_id", plan_id},
            {"plan_nonce", plan_nonce},
            {"pool_id", pool_id},
            {"mode", mode},
            {"force", force},
            {"devices_n", (int)devices.size()},
            {"mount", mount},
            {"label", label}
        };
        if (extra.is_object()) {
            for (auto it = extra.begin(); it != extra.end(); ++it) j[it.key()] = it.value();
        }
        return j;
    };

    if (!confirm || plan_id.empty() || plan_nonce.empty()) {
        audit_fail(actor_fp, "missing_plan_id_or_confirm", 400, "", audit_ctx());
        reply_json(res, 400, json{{"ok", false}, {"error", "missing_plan_id_or_confirm"}}.dump());
        return;
    }

    if (!is_hex_64_lower_or_upper(plan_id)) {
        audit_fail(actor_fp, "bad_plan_id_format", 400, "", audit_ctx());
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "plan_id must be 64 hex chars"},
            {"plan_id", plan_id}
        }.dump());
        return;
    }

    // keep this mild unless your planner already emits a stricter format
    if (plan_nonce.size() > 128) {
        audit_fail(actor_fp, "bad_plan_nonce", 400, "", audit_ctx());
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_plan_nonce"}}.dump());
        return;
    }

    if (!std::regex_match(pool_id, std::regex("^[a-z0-9_-]{1,32}$"))) {
        audit_fail(actor_fp, "bad_pool_id", 400, "", audit_ctx());
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_pool_id"}}.dump());
        return;
    }

    if (mode != "single" && mode != "raid1") {
        audit_fail(actor_fp, "bad_mode", 400, "", audit_ctx());
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mode"}}.dump());
        return;
    }

    json disk_inventory = storage_list_disks_json();

    std::string dev_err;
    if (!validate_create_pool_devices(devices_json, disk_inventory, devices, dev_err)) {
        audit_fail(actor_fp, "bad_devices", 400, dev_err, audit_ctx());
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_devices"},
            {"message", dev_err}
        }.dump());
        return;
    }

    if (mode == "raid1" && devices.size() < 2) {
        audit_fail(actor_fp, "raid1_requires_2_devices", 400, "", audit_ctx());
        reply_json(res, 400, json{{"ok", false}, {"error", "raid1_requires_2_devices"}}.dump());
        return;
    }

    const json canonical_commands =
        build_create_pool_commands_json(pool_id, mode, devices, force);

    if (!canonical_commands.is_array() || canonical_commands.empty()) {
        audit_fail(actor_fp, "canonical_plan_empty", 500, "", audit_ctx());
        reply_json(res, 500, json{{"ok", false}, {"error", "canonical_plan_empty"}}.dump());
        return;
    }

    const std::string expected_plan_id =
        compute_create_pool_plan_id(plan_nonce, pool_id, mode, devices, force, canonical_commands);

    if (expected_plan_id != plan_id) {
        audit_fail(actor_fp, "plan_mismatch", 400, "",
                   audit_ctx(json{{"expected_plan_id", expected_plan_id}}));
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "plan_mismatch"}
        }.dump());
        return;
    }

    // Exec-record dir + replay protection (refuse if this plan_id already executed)
    ensure_dir_best_effort("/run/pqnas/raid");
    const std::string recp = raid_exec_record_path(plan_id);

    {
        std::error_code ec;
        if (std::filesystem::exists(recp, ec)) {
            audit_fail(actor_fp, "already_executed", 200, "",
                       audit_ctx(json{{"recp", recp}}));
            reply_json(res, 200, json{
                {"ok", false},
                {"error", "already_executed"},
                {"plan_id", plan_id}
            }.dump());
            return;
        }
    }

    if (std::filesystem::exists(mount)) {
        audit_fail(actor_fp, "mount_exists", 400, "", audit_ctx());
        reply_json(res, 400, json{{"ok", false}, {"error", "mount_exists"}}.dump());
        return;
    }

    // Lock path (prevent concurrent ops)
    const std::string lockp = raid_mount_lock_path(mount);
    {
        std::error_code ec;
        if (std::filesystem::exists(lockp, ec)) {
            audit_fail(actor_fp, "raid_busy", 200, "",
                       audit_ctx(json{{"lockp", lockp}}));
            reply_json(res, 200, json{{"ok", false}, {"error", "raid_busy"}}.dump());
            return;
        }
    }

    // Ensure lock removed on all exits
    struct LockGuard {
        std::string p;
        ~LockGuard() { if (!p.empty()) { std::error_code ec; std::filesystem::remove(p, ec); } }
    } lock_guard{lockp};

    {
        std::ofstream lock(lockp);
        lock << "create-pool\n";
        lock.close();
    }

    // ---- exec-record init (must be before try/catch) ----
    json record;
    record["ok"]         = true;
    record["plan_id"]    = plan_id;
    record["plan_nonce"] = plan_nonce;
    record["operation"]  = "create-pool";
    record["state"]      = "running";
    record["busy"]       = true;
    record["ts_start"]   = iso8601_now();
    record["ts_last"]    = record["ts_start"];
    record["results"]    = json::array();

    record["pool_id"]    = pool_id;
    record["mode"]       = mode;
    record["force"]      = force;
    record["devices"]    = devices;
    record["commands"]   = canonical_commands;

    (void)write_text_file_atomic(recp, record.dump(2) + "\n");

    json results = json::array();
    bool all_ok = true;
    size_t step_i = 0;

    // Emit job_start once, right before the first command actually runs.
    bool job_start_emitted = false;
    auto emit_job_start_once = [&]() {
        if (job_start_emitted) return;
        job_start_emitted = true;
        audit_job_start_ok(actor_fp, audit_ctx(json{{"job_id", plan_id}, {"recp", recp}}));
    };

    // Helper: run one command, append to both results + record, update ts_last, persist record.
    auto run_step = [&](const std::string& cmd) -> bool {
        emit_job_start_once();

        const size_t i = step_i++;

        std::string out;
        int ec = 0;
        // hardening: route pseudo commands through guarded runner.
        const bool ran = run_cmd_capture(cmd, &out, &ec);
        const bool step_ok = ran && (ec == 0);

        cap_string(out, 128 * 1024);

        json one = {
            {"i", (int)i},
            {"cmd", cmd},
            {"rc", ec},
            {"ok", step_ok},
            {"out", out}
        };

        results.push_back(one);
        record["results"].push_back(one);
        record["ts_last"] = iso8601_now();

        if (!write_text_file_atomic(recp, record.dump(2) + "\n")) {
            all_ok = false;
            return false;
        }

        if (!step_ok) {
            all_ok = false;
            record["ok"]     = false;
            record["state"]  = "failed";
            record["busy"]   = false;
            record["ts_end"] = iso8601_now();
            (void)write_text_file_atomic(recp, record.dump(2) + "\n");
            return false;
        }

        return true;
    };

    try {
        for (const auto& cmdv : canonical_commands) {
            if (!cmdv.is_string()) {
                all_ok = false;
                break;
            }
            if (!run_step(cmdv.get<std::string>())) break;
        }

        // update pools.json
        if (all_ok) {
            json cfg = pqnas::load_or_init_pools_cfg_v3(users_path);

            if (!cfg.contains("names_by_mount") || !cfg["names_by_mount"].is_object())
                cfg["names_by_mount"] = json::object();
            cfg["names_by_mount"][mount] = pool_id;

            if (!cfg.contains("pools") || !cfg["pools"].is_object())
                cfg["pools"] = json::object();

            std::string fs_label_detected;
            std::string fs_uuid_detected;
            int fs_devices_detected = -1;

            {
                std::string show_out;
                // Security: call the read-only btrfs-status helper via argv, not a
                // shell command string, so newly-created pool mounts cannot be shell-interpreted.
                int rc_show = run_btrfs_status_helper_capture("filesystem-show", mount, &show_out);
                if (rc_show == 0) {
                    parse_btrfs_filesystem_show(show_out,
                                                &fs_label_detected,
                                                &fs_uuid_detected,
                                                &fs_devices_detected);
                }
            }

            json initial_slots = json::array();
            for (size_t i = 0; i < devices.size(); ++i) {
                initial_slots.push_back(json{
                    {"index", static_cast<int>(i)},
                    {"device", devices[i]},
                    {"runtime_dev", devices[i]}
                });
            }

            cfg["pools"][mount] = json{
                {"pool_id", pool_id},
                {"display_name", pool_id},
                {"created_ts", iso8601_now()},
                {"managed", true},
                {"mode", mode},
                {"slot_count", static_cast<int>(initial_slots.size())},
                {"slots", initial_slots},
                {"fs_label", fs_label_detected.empty() ? label : fs_label_detected},
                {"fs_uuid", fs_uuid_detected}
            };

            pqnas::enrich_pool_slots_with_runtime_identity_v3(&cfg["pools"][mount]);

            cfg["version"] = 3;

            const auto cfg_path = pools_cfg_path_from_users_path(users_path);
            if (!write_text_file_atomic(cfg_path.string(), cfg.dump(2) + "\n")) {
                all_ok = false;
            }
        }


        // finalize exec record
        record["ok"]     = all_ok;
        record["state"]  = all_ok ? "done" : "failed";
        record["busy"]   = false;
        record["ts_end"] = iso8601_now();
        (void)write_text_file_atomic(recp, record.dump(2) + "\n");

        // lock_guard will remove lockp on return

        if (all_ok) {
            audit_ok(actor_fp, audit_ctx(json{{"job_id", plan_id}, {"recp", recp}}));
            audit_job_finish(actor_fp, true, "", audit_ctx(json{{"job_id", plan_id}, {"recp", recp}}));
        } else {
            audit_fail(actor_fp, "command_failed", 200, "", audit_ctx(json{{"job_id", plan_id}, {"recp", recp}}));
            audit_job_finish(actor_fp, false, "command_failed", audit_ctx(json{{"job_id", plan_id}, {"recp", recp}}));
        }

        reply_json(res, 200, json{
            {"ok", all_ok},
            {"results", results}
        }.dump());
        return;

    } catch (...) {
        all_ok = false;
        record["ok"]     = false;
        record["state"]  = "failed";
        record["busy"]   = false;
        record["ts_end"] = iso8601_now();
        (void)write_text_file_atomic(recp, record.dump(2) + "\n");

        audit_fail(actor_fp, "exception", 500, "", audit_ctx(json{{"job_id", plan_id}, {"recp", recp}}));
        audit_job_finish(actor_fp, false, "exception", audit_ctx(json{{"job_id", plan_id}, {"recp", recp}}));

        reply_json(res, 500, json{{"ok", false}, {"error", "exception"}}.dump());
        return;
    }
});

// ----- GET /api/v4/raid/job?job_id=... (admin-only) ---------------------------
srv.Get("/api/v4/raid/job", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;
    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}, {"path", users_path}}.dump());
        return;
    }
    if (!require_admin_cookie_users(req, res, COOKIE_KEY, users_path, &users)) return;

	std::string job_id;
	if (req.has_param("job_id")) job_id = req.get_param_value("job_id");
	if (job_id.empty()) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "missing job_id"}}.dump());
        return;
    }

    auto it = g_raid_job_meta.find(job_id);
    if (it == g_raid_job_meta.end()) {
        reply_json(res, 404, json{{"ok", false}, {"error", "not_found"}, {"job_id", job_id}}.dump());
        return;
    }

    reply_json(res, 200, it->second.dump());
});



// ----- GET /api/v4/raid/health?mount=/path (admin-only, read-only) -----------
srv.Get("/api/v4/raid/health", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;

    if (!users.load(users_path)) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "users_load_failed"},
            {"path", users_path}
        }.dump());
        return;
    }

    if (!require_admin_cookie_users(req, res, COOKIE_KEY, users_path, &users)) return;

    // -------------------- mount selection --------------------
    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";

    std::string mount = allowed_prefix + "/data";
    if (req.has_param("mount")) mount = req.get_param_value("mount");

    json out;
    out["ok"] = false;
    out["input_mount"] = mount;
    out["allowed_prefix"] = allowed_prefix;

    if (!is_abs_path_safe(mount)) {
        out["error"] = "bad_mount";
        reply_json(res, 400, out.dump());
        return;
    }

    // -------------------- resolve mountpoint, fstype, source --------------------
    std::string target_out, fstype_out, source_out;

    // Security: call findmnt via argv directly, not through shell strings.
    int rc_target = run_findmnt_no_target_argv("TARGET", mount, &target_out);
    cap_string(target_out, 16 * 1024);
    rtrim_inplace(target_out);

    int rc_fs = run_findmnt_no_target_argv("FSTYPE", mount, &fstype_out);
    cap_string(fstype_out, 16 * 1024);
    rtrim_inplace(fstype_out);

    int rc_src = run_findmnt_no_target_argv("SOURCE", mount, &source_out);
    cap_string(source_out, 16 * 1024);
    rtrim_inplace(source_out);

    if (rc_target != 0 || target_out.empty() ||
        rc_fs != 0 || fstype_out.empty() ||
        rc_src != 0 || source_out.empty()) {
        out["error"] = "mount_not_found";
        reply_json(res, 200, out.dump());
        return;
    }

    const std::string resolved_mount  = target_out;
    const std::string resolved_source = source_out;
    const std::string resolved_disk   = parent_disk_from_dev(resolved_source);

    out["resolved_mount"]  = resolved_mount;
    out["resolved_source"] = resolved_source;
    if (!resolved_disk.empty()) out["resolved_disk"] = resolved_disk;
    out["fstype"]          = fstype_out;

    // -------------------- allowlist enforcement --------------------
    if (resolved_mount.rfind(allowed_prefix, 0) != 0) {
        const std::string test_prefix  = "/srv/pqnas-test";
        const std::string test_prefix2 = "/srv/pqnas-test-btrfs";
        if (resolved_mount.rfind(test_prefix, 0) != 0 &&
            resolved_mount.rfind(test_prefix2, 0) != 0) {
            out["error"] = "mount_not_allowed";
            reply_json(res, 200, out.dump());
            return;
        }
    }

    // -------------------- non-btrfs case --------------------
    if (fstype_out != "btrfs") {
        out["error"] = "not_btrfs";
        reply_json(res, 200, out.dump());
        return;
    }

    // -------------------- btrfs read-only health commands --------------------
    std::string dev_stats, scrub_status, balance_status;

    // Security: call the read-only btrfs-status helper via argv, not shell
    // command strings, so resolved mount targets cannot be shell-interpreted.
    int rc_dev_stats = run_btrfs_status_helper_capture("device-stats", resolved_mount, &dev_stats);
    int rc_scrub     = run_btrfs_status_helper_capture("scrub-status", resolved_mount, &scrub_status);
    int rc_balance   = run_btrfs_status_helper_capture("balance-status", resolved_mount, &balance_status);

    cap_string(dev_stats,       256 * 1024);
    cap_string(scrub_status,    256 * 1024);
    cap_string(balance_status,  256 * 1024);

    out["rc_device_stats"] = rc_dev_stats;
    out["rc_scrub_status"] = rc_scrub;
    out["rc_balance_status"] = rc_balance;

    // Always include raw outputs (capped) for now; if you want, you can gate these with PQNAS_RAID_DEBUG_* later.
    out["btrfs_device_stats"]  = dev_stats;
    out["btrfs_scrub_status"]  = scrub_status;
    out["btrfs_balance_status"] = balance_status;

    // Parsed scrub summary (best effort)
    out["scrub"] = parse_btrfs_scrub_status_best_effort(scrub_status);

    // ok/error classification (match your existing style)
    if (rc_dev_stats != 0 || rc_scrub != 0 || rc_balance != 0) {
        out["ok"] = false;
        if (str_contains(dev_stats, "sudo:") || str_contains(scrub_status, "sudo:") || str_contains(balance_status, "sudo:")) {
            out["error"] = "sudo_not_allowed";
        } else if (str_contains(dev_stats, "not a valid btrfs filesystem") ||
                   str_contains(scrub_status, "not a valid btrfs filesystem") ||
                   str_contains(balance_status, "not a valid btrfs filesystem")) {
            out["error"] = "not_btrfs";
        } else {
            out["error"] = "btrfs_failed";
        }

        reply_json(res, 200, out.dump());
        return;
    }

    out["ok"] = true;
    reply_json(res, 200, out.dump());
});

}
