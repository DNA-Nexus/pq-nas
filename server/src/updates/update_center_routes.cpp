#include "updates/update_center_routes.h"
#include "version.h"

#include <nlohmann/json.hpp>
#include <sys/wait.h>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <unistd.h>
#include <vector>
#include <iterator>
#include <cctype>
#include <sstream>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <ios>
#include <fstream>
#include <map>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <mutex>
#include <thread>

#include <openssl/evp.h>

#include <string>
#include <system_error>
#include <filesystem>

namespace pqnas::updates {

using json = nlohmann::json;

namespace {

std::string update_lower(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}


std::string update_audit_trunc(std::string s, std::size_t max_len = 300) {
    for (char& c : s) {
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    }

    if (s.size() > max_len) {
        s.resize(max_len);
        s += "...";
    }

    return s;
}

std::string update_audit_json_scalar_to_string(const json& v) {
    if (v.is_string()) return update_audit_trunc(v.get<std::string>());
    if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
    if (v.is_number_integer()) return std::to_string(v.get<long long>());
    if (v.is_number_unsigned()) return std::to_string(v.get<unsigned long long>());
    if (v.is_number_float()) return std::to_string(v.get<double>());
    return "";
}

void update_audit_add_json_field(std::map<std::string, std::string>& fields,
                                 const json& obj,
                                 const std::string& key) {
    if (!obj.is_object() || !obj.contains(key)) return;

    const std::string value = update_audit_json_scalar_to_string(obj.at(key));
    if (!value.empty()) fields[key] = value;
}

std::map<std::string, std::string> update_audit_fields_from_json(const json& obj) {
    std::map<std::string, std::string> fields;

    // Allow-list only. Do not copy helper output, tar listings, command lines,
    // environment values, action arrays, cookies, tokens, device ids, or secrets.
    static const std::vector<std::string> allowed = {
        "status",
        "error",
        "message",
        "original_name",
        "stored_name",
        "size",
        "sha256",
        "package_sha256",
        "package_size",
        "package_server_version",
        "current_server_version",
        "server_package_is_newer",
        "entry_count",
        "planned_updates",
        "skipped",
        "has_core_binary_action",
        "plan_id",
        "plan_hash",
        "plan_saved",
        "applicable_action_count",
        "planned_action_count",
        "applied_action_count",
        "helper_enabled",
        "apply_allowed",
        "helper_exit_code",
        "install_performed",
        "restart_required"
    };

    for (const std::string& key : allowed) {
        update_audit_add_json_field(fields, obj, key);
    }

    return fields;
}

void update_audit_emit_local(const UpdateCenterRoutesDeps& deps,
                             const std::string& event,
                             const std::string& outcome,
                             const std::string& actor_fp,
                             const json& payload = json::object(),
                             std::map<std::string, std::string> extra = {}) {
    if (!deps.audit_emit) return;

    std::map<std::string, std::string> fields = update_audit_fields_from_json(payload);

    for (auto& kv : extra) {
        if (!kv.first.empty() && !kv.second.empty()) {
            fields[kv.first] = update_audit_trunc(kv.second);
        }
    }

    fields["component"] = "update_center";
    if (!actor_fp.empty()) {
        fields["fingerprint"] = actor_fp;
    }

    deps.audit_emit(event, outcome, fields);
}


std::map<std::string, std::string> update_activity_details_from_json(const json& obj) {
    std::map<std::string, std::string> details;

    // My Activity is user-visible. Keep it high-level:
    // no helper stdout/stderr, no command lines, no tar listings, no cookies,
    // no tokens, no device ids, no raw environment values, no action arrays.
    static const std::vector<std::string> allowed = {
        "status",
        "error",
        "message",
        "original_name",
        "stored_name",
        "size",
        "sha256",
        "package_sha256",
        "package_server_version",
        "current_server_version",
        "plan_id",
        "applicable_action_count",
        "planned_action_count",
        "applied_action_count",
        "helper_exit_code",
        "install_performed",
        "restart_required"
    };

    for (const std::string& key : allowed) {
        if (!obj.is_object() || !obj.contains(key)) continue;
        std::string value = update_audit_json_scalar_to_string(obj.at(key));
        if (value.empty()) continue;

        if (key == "sha256" || key == "package_sha256") {
            if (value.size() > 16) value = value.substr(0, 16);
        }

        if (key == "plan_id" && value.size() > 80) {
            value = value.substr(0, 80);
        }

        details[key] = value;
    }

    return details;
}

void update_activity_record_local(const UpdateCenterRoutesDeps& deps,
                                  const httplib::Request& req,
                                  const std::string& actor_fp,
                                  const std::string& event_type,
                                  const std::string& message,
                                  const json& payload = json::object()) {
    if (!deps.record_activity || actor_fp.empty() || event_type.empty()) return;

    deps.record_activity(
        req,
        actor_fp,
        event_type,
        message,
        update_activity_details_from_json(payload)
    );
}


bool update_require_admin_actor_local(const UpdateCenterRoutesDeps& deps,
                                      const httplib::Request& req,
                                      httplib::Response& res,
                                      std::string* actor_fp) {
    if (actor_fp) actor_fp->clear();

    if (deps.require_admin_actor) {
        return deps.require_admin_actor(req, res, actor_fp);
    }

    if (deps.require_admin) {
        return deps.require_admin(req, res);
    }

    res.status = 500;
    res.body = "Update Center admin guard is not configured.";
    return false;
}


bool update_starts_with(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool update_ends_with(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string update_safe_filename(std::string name, std::string* err) {
    for (char& c : name) {
        if (c == '\\') c = '/';
    }

    const std::size_t slash = name.find_last_of('/');
    if (slash != std::string::npos) {
        name = name.substr(slash + 1);
    }

    if (name.empty()) {
        if (err) *err = "empty filename";
        return "";
    }

    if (name.size() > 180) {
        if (err) *err = "filename too long";
        return "";
    }

    if (name.find("..") != std::string::npos) {
        if (err) *err = "filename must not contain ..";
        return "";
    }

    for (char c : name) {
        const bool ok =
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '.' || c == '_' || c == '-';

        if (!ok) {
            if (err) *err = "filename contains unsupported characters";
            return "";
        }
    }

    const std::string low = update_lower(name);
    const bool ext_ok =
        update_ends_with(low, ".dnxupd") ||
        update_ends_with(low, ".tar.gz") ||
        update_ends_with(low, ".tgz") ||
        update_ends_with(low, ".zip");

    if (!ext_ok) {
        if (err) *err = "unsupported update package extension";
        return "";
    }

    const bool pqnas_release =
        update_starts_with(low, "pqnas-") &&
        low.find("-linux-") != std::string::npos &&
        (
            update_ends_with(low, ".tar.gz") ||
            update_ends_with(low, ".tgz") ||
            update_ends_with(low, ".zip")
        );

    const bool named_core =
        low.find("dna-nexus-server") != std::string::npos ||
        low.find("pqnas-server") != std::string::npos ||
        low.find("pq-nas-server") != std::string::npos ||
        low.find("pqnas_server") != std::string::npos ||
        low.find("server-update") != std::string::npos ||
        update_ends_with(low, ".dnxupd");

    if (!pqnas_release && !named_core) {
        if (err) *err = "not a recognized core/server update package";
        return "";
    }

    return name;
}

std::string update_shell_quote(const std::string& in) {
    std::string out = "'";
    for (char c : in) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

std::string update_run_command_limited(const std::string& cmd, std::size_t max_bytes, int* status_out) {
    if (status_out) *status_out = -1;

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";

    std::string out;
    char buf[4096];

    while (fgets(buf, sizeof(buf), pipe)) {
        if (out.size() < max_bytes) {
            const std::size_t room = max_bytes - out.size();
            const std::size_t len = std::strlen(buf);
            out.append(buf, std::min(room, len));
        }
    }

    const int st = pclose(pipe);
    if (status_out) *status_out = st;
    return out;
}


struct UpdateArgvResult {
    int exit_code = 127;
    std::string output;
};

UpdateArgvResult update_run_argv_limited(
    const std::vector<std::string>& argv_s,
    std::size_t max_bytes,
    int timeout_ms
) {
    UpdateArgvResult result;

    if (argv_s.empty()) {
        result.output = "empty argv";
        return result;
    }

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

        ::execv(argv_s[0].c_str(), argv.data());
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

        const std::size_t have = result.output.size();
        if (have < max_bytes) {
            const std::size_t room = max_bytes - have;
            const std::size_t take = std::min<std::size_t>(
                static_cast<std::size_t>(n),
                room
            );
            result.output.append(buf, take);
        }

        if (result.output.size() >= max_bytes) {
            truncated = true;
        }
    };

    auto drain = [&]() {
        std::array<char, 4096> buf{};
        for (;;) {
            const ssize_t n = ::read(pipefd[0], buf.data(), buf.size());
            if (n > 0) {
                append_bytes(buf.data(), n);
                continue;
            }
            if (n == 0) return;
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            return;
        }
    };

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    int status = 0;
    for (;;) {
        drain();

        const pid_t w = ::waitpid(pid, &status, WNOHANG);
        if (w == pid) break;

        if (w < 0 && errno != EINTR) {
            result.output += "\nwaitpid failed: ";
            result.output += std::strerror(errno);
            result.exit_code = 127;
            ::close(pipefd[0]);
            return result;
        }

        if (std::chrono::steady_clock::now() >= deadline) {
            // Security: cap update helper runtime without invoking shell timeout.
            (void)::kill(pid, SIGKILL);
            (void)::waitpid(pid, &status, 0);
            drain();
            ::close(pipefd[0]);
            if (truncated) result.output += "\n[output truncated]\n";
            result.output += "\ncommand timed out";
            result.exit_code = 124;
            return result;
        }

        ::usleep(10 * 1000);
    }

    drain();
    ::close(pipefd[0]);

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


std::filesystem::path updates_root_dir(const UpdateCenterRoutesDeps& deps) {
    const std::string env = deps.getenv_str ? deps.getenv_str("PQNAS_UPDATES_ROOT") : "";
    if (!env.empty()) return std::filesystem::path(env);
    return std::filesystem::path("/var/lib/pqnas/updates");
}

std::filesystem::path update_incoming_dir(const UpdateCenterRoutesDeps& deps) {
    return updates_root_dir(deps) / "incoming";
}


bool update_is_safe_staged_package_name(const std::string& name) {
    if (name.empty() || name.size() > 240) return false;

    if (name.find('/') != std::string::npos ||
        name.find('\\') != std::string::npos ||
        name.find("..") != std::string::npos) {
        return false;
    }

    for (char c : name) {
        const bool ok =
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '.' || c == '_' || c == '-';

        if (!ok) return false;
    }

    const std::string low = update_lower(name);

    if (update_ends_with(low, ".part") ||
        update_ends_with(low, ".json")) {
        return false;
    }

    return
        update_ends_with(low, ".dnxupd") ||
        update_ends_with(low, ".tar.gz") ||
        update_ends_with(low, ".tgz") ||
        update_ends_with(low, ".zip");
}


// update_plan_helpers_r6a
[[maybe_unused]] std::string update_plan_normalize_entry(std::string entry) {
    while (!entry.empty() && (entry.back() == '\r' || entry.back() == '\n')) {
        entry.pop_back();
    }

    for (char& c : entry) {
        if (c == '\\') c = '/';
    }

    while (update_starts_with(entry, "./")) {
        entry = entry.substr(2);
    }

    while (update_starts_with(entry, "pqnas/")) {
        entry = entry.substr(6);
    }

    return entry;
}

[[maybe_unused]] std::string update_plan_path_segment_after(const std::string& s, const std::string& prefix) {
    if (!update_starts_with(s, prefix)) return "";
    std::string rest = s.substr(prefix.size());
    const std::size_t slash = rest.find('/');
    if (slash == std::string::npos) return rest;
    return rest.substr(0, slash);
}

[[maybe_unused]] std::string update_trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.erase(s.begin());
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
    return s;
}

[[maybe_unused]] std::string update_read_first_line(const std::filesystem::path& path) {
    std::ifstream f(path);
    if (!f.good()) return "";
    std::string line;
    std::getline(f, line);
    return update_trim(line);
}

[[maybe_unused]] std::vector<long long> update_version_numbers(const std::string& v) {
    std::vector<long long> nums;
    std::string cur;

    for (char c : v) {
        if (c >= '0' && c <= '9') {
            cur.push_back(c);
        } else if (!cur.empty()) {
            try {
                nums.push_back(std::stoll(cur));
            } catch (...) {
                nums.push_back(0);
            }
            cur.clear();
        }
    }

    if (!cur.empty()) {
        try {
            nums.push_back(std::stoll(cur));
        } catch (...) {
            nums.push_back(0);
        }
    }

    return nums;
}

[[maybe_unused]] int update_compare_versions(const std::string& a, const std::string& b) {
    const std::vector<long long> av = update_version_numbers(a);
    const std::vector<long long> bv = update_version_numbers(b);

    if (av.empty() || bv.empty()) return 0;

    const std::size_t n = std::max(av.size(), bv.size());
    for (std::size_t i = 0; i < n; ++i) {
        const long long ai = i < av.size() ? av[i] : 0;
        const long long bi = i < bv.size() ? bv[i] : 0;

        if (ai < bi) return -1;
        if (ai > bi) return 1;
    }

    return 0;
}

[[maybe_unused]] std::string update_strip_archive_suffix(std::string name) {
    const std::string low = update_lower(name);

    if (update_ends_with(low, ".tar.gz")) return name.substr(0, name.size() - 7);
    if (update_ends_with(low, ".tgz")) return name.substr(0, name.size() - 4);
    if (update_ends_with(low, ".zip")) return name.substr(0, name.size() - 4);
    if (update_ends_with(low, ".dnxupd")) return name.substr(0, name.size() - 7);

    return name;
}

[[maybe_unused]] std::string update_basename(const std::string& path) {
    std::string s = path;
    for (char& c : s) {
        if (c == '\\') c = '/';
    }

    const std::size_t slash = s.find_last_of('/');
    if (slash != std::string::npos) return s.substr(slash + 1);
    return s;
}

[[maybe_unused]] std::string update_extract_server_version_from_stored_name(std::string stored) {
    stored = update_basename(stored);

    const std::size_t underscore = stored.find('_');
    if (underscore != std::string::npos) {
        stored = stored.substr(underscore + 1);
    }

    stored = update_strip_archive_suffix(stored);

    const std::string low = update_lower(stored);
    const std::string prefix = "pqnas-";
    const std::string arch_marker = "-linux-";

    if (!update_starts_with(low, prefix)) return "";

    const std::size_t arch = low.find(arch_marker, prefix.size());
    if (arch == std::string::npos) return "";

    return stored.substr(prefix.size(), arch - prefix.size());
}

[[maybe_unused]] std::string update_current_server_version(const UpdateCenterRoutesDeps& deps) {
#ifdef PQNAS_VERSION
    const std::string compiled_version = PQNAS_VERSION;
    if (!compiled_version.empty()) return compiled_version;
#endif

    if (deps.getenv_str) {
        const std::string env = deps.getenv_str("PQNAS_CURRENT_VERSION");
        if (!env.empty()) return env;
    }

    const std::string opt_marker =
        update_read_first_line(std::filesystem::path("/opt/pqnas/VERSION"));
    if (!opt_marker.empty()) return opt_marker;

    return "";
}

[[maybe_unused]] std::string update_extract_app_package_version(const std::string& entry,
                                                               const std::string& app_id) {
    if (app_id.empty()) return "";

    std::string base = update_strip_archive_suffix(update_basename(entry));
    const std::string low_base = update_lower(base);
    const std::string low_app = update_lower(app_id);

    const std::string prefix_dash = low_app + "-";
    const std::string prefix_underscore = low_app + "_";

    if (update_starts_with(low_base, prefix_dash)) {
        return base.substr(app_id.size() + 1);
    }

    if (update_starts_with(low_base, prefix_underscore)) {
        return base.substr(app_id.size() + 1);
    }

    return "";
}

[[maybe_unused]] bool update_installed_app_exists(const UpdateCenterRoutesDeps& deps,
                                                  const std::string& app_id) {
    if (app_id.empty()) return false;
    std::error_code app_ec;
    const std::filesystem::path p = std::filesystem::path(deps.apps_installed_dir) / app_id;
    return std::filesystem::exists(p, app_ec) && !app_ec;
}

[[maybe_unused]] std::string update_latest_installed_app_version(const UpdateCenterRoutesDeps& deps,
                                                                 const std::string& app_id) {
    if (app_id.empty()) return "";

    std::error_code ec;
    const std::filesystem::path root = std::filesystem::path(deps.apps_installed_dir) / app_id;

    if (!std::filesystem::exists(root, ec) || ec) return "";

    std::string best;
    std::error_code it_ec;

    for (const auto& ent : std::filesystem::directory_iterator(root, it_ec)) {
        if (it_ec) break;

        std::error_code st_ec;
        if (!ent.is_directory(st_ec) || st_ec) continue;

        const std::string ver = ent.path().filename().string();
        if (ver.empty()) continue;

        if (best.empty() || update_compare_versions(ver, best) > 0) {
            best = ver;
        }
    }

    return best;
}


[[maybe_unused]] bool update_manifest_capability_supported(const std::string& cap) {
    // Keep this intentionally small. New capabilities must be explicitly
    // added when the updater can really enforce/apply them safely.
    return cap == "update-manifest-v1";
}

[[maybe_unused]] std::string update_join_json_string_array(const json& arr) {
    if (!arr.is_array()) return "";

    std::string out;
    for (const auto& v : arr) {
        if (!v.is_string()) continue;
        const std::string s = v.get<std::string>();
        if (s.empty()) continue;

        if (!out.empty()) out += ", ";
        out += s;
    }

    return out;
}

[[maybe_unused]] std::string update_requires_prior_hint(const json& requires_prior) {
    if (!requires_prior.is_array()) return "";

    for (const auto& item : requires_prior) {
        if (!item.is_object()) continue;

        const std::string version =
            item.contains("version") && item["version"].is_string()
                ? item["version"].get<std::string>()
                : "";

        const std::string message =
            item.contains("message") && item["message"].is_string()
                ? item["message"].get<std::string>()
                : "";

        if (!message.empty()) return message;
        if (!version.empty()) return "Install " + version + " first.";
    }

    return "";
}

[[maybe_unused]] json update_extract_update_manifest_info(const std::filesystem::path& package_path) {
    json out = {
        {"present", false},
        {"ok", true},
        {"path", ""},
        {"error", ""}
    };

    const std::vector<std::string> candidates = {
        "pqnas/update_manifest.json",
        "./pqnas/update_manifest.json",
        "update_manifest.json",
        "./update_manifest.json"
    };

    for (const std::string& candidate : candidates) {
        int status = -1;

        const std::string cmd =
            "tar -xOzf " + update_shell_quote(package_path.string()) + " " +
            update_shell_quote(candidate) + " 2>/dev/null";

        std::string raw = update_run_command_limited(cmd, 256u * 1024u, &status);
        raw = update_trim(raw);

        if (status != 0) {
            continue;
        }

        out["present"] = true;
        out["path"] = candidate;

        if (raw.empty()) {
            out["ok"] = false;
            out["error"] = "update manifest is empty";
            return out;
        }

        try {
            json manifest = json::parse(raw);
            if (!manifest.is_object()) {
                out["ok"] = false;
                out["error"] = "update manifest must be a JSON object";
                return out;
            }

            out["ok"] = true;
            out["manifest"] = manifest;
            return out;
        } catch (const std::exception& e) {
            out["ok"] = false;
            out["error"] = std::string("update manifest JSON parse failed: ") + e.what();
            return out;
        } catch (...) {
            out["ok"] = false;
            out["error"] = "update manifest JSON parse failed";
            return out;
        }
    }

    // No manifest is still allowed for older packages. Manifest-aware
    // packages are validated when the manifest is present.
    return out;
}


} // namespace



// build_update_plan_json_r6b
[[maybe_unused]] json build_update_plan_json(const UpdateCenterRoutesDeps& deps,
                                             const std::string& stored_name,
                                             const std::string& listing,
                                             const std::string& package_bytes,
                                             const json& manifest_info) {
    const std::string package_sha256 = deps.sha256_hex(package_bytes);
    const std::uintmax_t package_size_bytes =
        static_cast<std::uintmax_t>(package_bytes.size());

    json actions = json::array();
    json skipped_summary = json::object();

    auto bump_skip = [&](const std::string& key) {
        const int old = skipped_summary.value(key, 0);
        skipped_summary[key] = old + 1;
    };

    auto add_action = [&](const std::string& type,
                          const std::string& action,
                          const std::string& source,
                          const std::string& target,
                          const std::string& reason,
                          const std::string& app_id = "") {
        json a = {
            {"type", type},
            {"action", action},
            {"source", source}
        };

        if (!target.empty()) a["target"] = target;
        if (!reason.empty()) a["reason"] = reason;
        if (!app_id.empty()) a["app_id"] = app_id;

        actions.push_back(a);
    };

    std::size_t entry_count = 0;
    std::size_t planned_updates = 0;
    std::size_t skipped = 0;

    bool has_core_binary_action = false;

    const json manifest_info_obj = manifest_info.is_object() ? manifest_info : json::object();
    const bool manifest_present = manifest_info_obj.value("present", false);
    const bool manifest_ok = manifest_info_obj.value("ok", false);
    const std::string manifest_path = manifest_info_obj.value("path", "");
    const std::string manifest_error = manifest_info_obj.value("error", "");

    json update_manifest = json::object();
    if (manifest_present &&
        manifest_ok &&
        manifest_info_obj.contains("manifest") &&
        manifest_info_obj["manifest"].is_object()) {
        update_manifest = manifest_info_obj["manifest"];
    }

    const std::string filename_package_server_version =
        update_extract_server_version_from_stored_name(stored_name);

    std::string manifest_package_version;
    if (update_manifest.contains("package_version") &&
        update_manifest["package_version"].is_string()) {
        manifest_package_version = update_trim(update_manifest["package_version"].get<std::string>());
    }

    const std::string package_server_version =
        !manifest_package_version.empty()
            ? manifest_package_version
            : filename_package_server_version;

    const std::string current_server_version =
        update_current_server_version(deps);

    const bool server_package_version_known =
        !package_server_version.empty() && !current_server_version.empty();
    const bool server_package_is_newer =
        server_package_version_known &&
        update_compare_versions(package_server_version, current_server_version) > 0;

    bool manifest_compatibility_ok = true;
    json manifest_missing_capabilities = json::array();

    auto add_manifest_reject = [&](const std::string& skip_key,
                                   const std::string& reason) {
        manifest_compatibility_ok = false;
        add_action(
            "manifest",
            "reject",
            manifest_path.empty() ? "update_manifest.json" : manifest_path,
            "",
            reason
        );
        ++skipped;
        bump_skip(skip_key);
    };

    if (manifest_present) {
        if (!manifest_ok) {
            add_manifest_reject(
                "manifest_invalid",
                manifest_error.empty()
                    ? "update manifest is invalid"
                    : manifest_error
            );
        } else {
            int schema = -1;
            if (update_manifest.contains("schema") &&
                update_manifest["schema"].is_number_integer()) {
                schema = update_manifest["schema"].get<int>();
            }

            if (schema != 1) {
                add_manifest_reject(
                    "manifest_schema_unsupported",
                    "unsupported update manifest schema; expected schema 1"
                );
            }

            const std::string kind =
                update_manifest.contains("kind") && update_manifest["kind"].is_string()
                    ? update_manifest["kind"].get<std::string>()
                    : "";

            if (kind != "pqnas_update_manifest") {
                add_manifest_reject(
                    "manifest_kind_invalid",
                    "update manifest kind must be pqnas_update_manifest"
                );
            }

            const std::string package =
                update_manifest.contains("package") && update_manifest["package"].is_string()
                    ? update_manifest["package"].get<std::string>()
                    : "";

            if (package != "pqnas") {
                add_manifest_reject(
                    "manifest_package_invalid",
                    "update manifest package must be pqnas"
                );
            }

            if (manifest_package_version.empty()) {
                add_manifest_reject(
                    "manifest_package_version_missing",
                    "update manifest is missing package_version"
                );
            }

            if (!manifest_package_version.empty() &&
                !filename_package_server_version.empty() &&
                update_compare_versions(manifest_package_version, filename_package_server_version) != 0) {
                add_manifest_reject(
                    "manifest_package_version_mismatch",
                    "update manifest package_version " + manifest_package_version +
                        " does not match tarball filename version " + filename_package_server_version
                );
            }

            json compatibility = json::object();
            if (update_manifest.contains("compatibility")) {
                compatibility = update_manifest["compatibility"];
            }

            if (!compatibility.is_object()) {
                add_manifest_reject(
                    "manifest_compatibility_invalid",
                    "update manifest compatibility must be a JSON object"
                );
            } else {
                const std::string min_current_version =
                    compatibility.contains("min_current_version") &&
                    compatibility["min_current_version"].is_string()
                        ? update_trim(compatibility["min_current_version"].get<std::string>())
                        : "";

                const std::string max_current_version_exclusive =
                    compatibility.contains("max_current_version_exclusive") &&
                    compatibility["max_current_version_exclusive"].is_string()
                        ? update_trim(compatibility["max_current_version_exclusive"].get<std::string>())
                        : "";

                const json requires_prior =
                    compatibility.contains("requires_prior")
                        ? compatibility["requires_prior"]
                        : json::array();

                const std::string prior_hint = update_requires_prior_hint(requires_prior);

                if (!min_current_version.empty()) {
                    if (current_server_version.empty()) {
                        add_manifest_reject(
                            "manifest_current_version_unknown",
                            "current server version is unknown; cannot enforce manifest min_current_version " +
                                min_current_version
                        );
                    } else if (update_compare_versions(current_server_version, min_current_version) < 0) {
                        std::string reason =
                            "current server version " + current_server_version +
                            " is too old for this update; minimum required version is " +
                            min_current_version;

                        if (!prior_hint.empty()) {
                            reason += ". " + prior_hint;
                        }

                        add_manifest_reject("manifest_min_current_version", reason);
                    }
                }

                if (!max_current_version_exclusive.empty()) {
                    if (current_server_version.empty()) {
                        add_manifest_reject(
                            "manifest_current_version_unknown",
                            "current server version is unknown; cannot enforce manifest max_current_version_exclusive " +
                                max_current_version_exclusive
                        );
                    } else if (update_compare_versions(current_server_version, max_current_version_exclusive) >= 0) {
                        add_manifest_reject(
                            "manifest_max_current_version_exclusive",
                            "current server version " + current_server_version +
                                " is not lower than manifest max_current_version_exclusive " +
                                max_current_version_exclusive +
                                "; refusing reinstall, downgrade, or invalid update path"
                        );
                    }
                }
            }

            json capabilities = json::object();
            if (update_manifest.contains("capabilities")) {
                capabilities = update_manifest["capabilities"];
            }

            if (capabilities.is_object() && capabilities.contains("requires")) {
                const json required_caps = capabilities["requires"];

                if (!required_caps.is_array()) {
                    add_manifest_reject(
                        "manifest_capabilities_requires_invalid",
                        "update manifest capabilities.requires must be an array"
                    );
                } else {
                    for (const auto& cap_v : required_caps) {
                        if (!cap_v.is_string()) {
                            manifest_missing_capabilities.push_back("<non-string capability>");
                            continue;
                        }

                        const std::string cap = cap_v.get<std::string>();
                        if (!update_manifest_capability_supported(cap)) {
                            manifest_missing_capabilities.push_back(cap);
                        }
                    }

                    if (!manifest_missing_capabilities.empty()) {
                        add_manifest_reject(
                            "manifest_missing_capabilities",
                            "this update requires unsupported Update Center capabilities: " +
                                update_join_json_string_array(manifest_missing_capabilities)
                        );
                    }
                }
            } else if (!capabilities.is_null() && !capabilities.is_object()) {
                add_manifest_reject(
                    "manifest_capabilities_invalid",
                    "update manifest capabilities must be a JSON object"
                );
            }
        }
    }

    if (!server_package_version_known) {
        bump_skip("server_version_unknown");
    } else if (!server_package_is_newer) {
        bump_skip("server_package_not_newer");
    }

    std::istringstream in(listing);
    std::string raw;

    while (std::getline(in, raw)) {
        std::string entry = update_plan_normalize_entry(raw);
        if (entry.empty()) continue;

        ++entry_count;

        const bool is_dir = update_ends_with(entry, "/");
        if (is_dir) continue;

        const std::string entry_low = update_lower(entry);

        if (!entry.empty() && entry[0] == '/') {
            add_action("unsafe_entry", "reject", raw, "", "absolute paths are not allowed");
            ++skipped;
            bump_skip("unsafe_entry");
            continue;
        }

        if (entry == ".." ||
            update_starts_with(entry, "../") ||
            entry.find("/../") != std::string::npos ||
            update_ends_with(entry, "/..")) {
            add_action("unsafe_entry", "reject", raw, "", "path traversal is not allowed");
            ++skipped;
            bump_skip("unsafe_entry");
            continue;
        }

        if (entry == "pqnas_server" ||
            entry == "bin/pqnas_server" ||
            update_ends_with(entry_low, "/pqnas_server")) {
            if (package_server_version.empty()) {
                add_action(
                    "core_binary",
                    "skip_version_unknown",
                    raw,
                    "/usr/local/bin/pqnas_server",
                    "package server version could not be determined; refusing core binary update"
                );
                ++skipped;
                bump_skip("core_version_unknown");
                continue;
            }

            if (current_server_version.empty()) {
                add_action(
                    "core_binary",
                    "skip_current_version_unknown",
                    raw,
                    "/usr/local/bin/pqnas_server",
                    "current server version is unknown; refusing core binary update to prevent downgrade"
                );
                ++skipped;
                bump_skip("core_current_version_unknown");
                continue;
            }

            if (!server_package_is_newer) {
                add_action(
                    "core_binary",
                    "skip_version_not_newer",
                    raw,
                    "/usr/local/bin/pqnas_server",
                    "package version " + package_server_version +
                        " is not newer than current server version " + current_server_version
                );
                ++skipped;
                bump_skip("core_version_not_newer");
                continue;
            }

            add_action(
                "core_binary",
                "update",
                raw,
                "/usr/local/bin/pqnas_server",
                "core server binary update from " + current_server_version +
                    " to " + package_server_version
            );
            ++planned_updates;
            has_core_binary_action = true;
            continue;
        }

        if (update_starts_with(entry_low, "static/")) {
            const std::string rel = entry.substr(std::string("static/").size());
            if (rel.empty()) {
                ++skipped;
                bump_skip("static_empty");
                continue;
            }

            const std::string target =
                (std::filesystem::path(deps.static_root_dir()) / rel).string();

            if (package_server_version.empty()) {
                add_action(
                    "static_file",
                    "skip_version_unknown",
                    raw,
                    target,
                    "package server version could not be determined; refusing static file update"
                );
                ++skipped;
                bump_skip("static_version_unknown");
                continue;
            }

            if (current_server_version.empty()) {
                add_action(
                    "static_file",
                    "skip_current_version_unknown",
                    raw,
                    target,
                    "current server version is unknown; refusing static file update to prevent downgrade"
                );
                ++skipped;
                bump_skip("static_current_version_unknown");
                continue;
            }

            if (!server_package_is_newer) {
                add_action(
                    "static_file",
                    "skip_version_not_newer",
                    raw,
                    target,
                    "package version " + package_server_version +
                        " is not newer than current server version " + current_server_version
                );
                ++skipped;
                bump_skip("static_version_not_newer");
                continue;
            }

            add_action(
                "static_file",
                "update",
                raw,
                target,
                "static UI file update from server package " + current_server_version +
                    " to " + package_server_version
            );
            ++planned_updates;
            continue;
        }

        if (update_starts_with(entry_low, "config/")) {
            const std::string rel = entry.substr(std::string("config/").size());

            add_action(
                "config",
                "skip",
                raw,
                (std::filesystem::path(deps.config_root_dir()) / rel).string(),
                "live config is never overwritten by updater"
            );
            ++skipped;
            bump_skip("config");
            continue;
        }

        if (update_starts_with(entry_low, "systemd/")) {
            add_action(
                "systemd",
                "skip",
                raw,
                "/etc/systemd/system/" + std::filesystem::path(entry).filename().string(),
                "systemd changes require explicit admin choice"
            );
            ++skipped;
            bump_skip("systemd");
            continue;
        }

        if (update_starts_with(entry_low, "apps/bundled/")) {
            const std::string app_id = update_plan_path_segment_after(entry, "apps/bundled/");
            if (update_installed_app_exists(deps, app_id)) {
                add_action(
                    "bundled_app",
                    "update_existing_app",
                    raw,
                    (std::filesystem::path(deps.apps_installed_dir) / app_id).string(),
                    "app already exists on this server; eligible for app update",
                    app_id
                );
                ++planned_updates;
            } else {
                add_action(
                    "bundled_app",
                    "skip_not_installed",
                    raw,
                    "",
                    "app is present in release package but is not installed on this server",
                    app_id
                );
                ++skipped;
                bump_skip("bundled_app_not_installed");
            }
            continue;
        }

        if (update_starts_with(entry_low, "apps/installed/")) {
            const std::string app_id = update_plan_path_segment_after(entry, "apps/installed/");
            if (update_installed_app_exists(deps, app_id)) {
                add_action(
                    "installed_app",
                    "update_existing_app",
                    raw,
                    (std::filesystem::path(deps.apps_installed_dir) / app_id).string(),
                    "app already exists on this server; eligible for app update",
                    app_id
                );
                ++planned_updates;
            } else {
                add_action(
                    "installed_app",
                    "skip_not_installed",
                    raw,
                    "",
                    "app is present in release package but is not installed on this server",
                    app_id
                );
                ++skipped;
                bump_skip("installed_app_not_installed");
            }
            continue;
        }

        if (update_starts_with(entry_low, "bundled/")) {
            const std::string app_id = update_plan_path_segment_after(entry, "bundled/");

            add_action(
                "bundled_app_package",
                "skip_managed_by_admin_apps",
                raw,
                "",
                "bundled app packages are managed by Admin Apps; use /admin/apps to install or update apps",
                app_id
            );
            ++skipped;
            bump_skip("bundled_app_package_managed_by_admin_apps");
            continue;
        }if (update_starts_with(entry_low, "runtime/") ||
            update_starts_with(entry_low, "lib/")) {
            add_action(
                "runtime_component",
                "skip",
                raw,
                "",
                "runtime/library components require explicit updater support before installation"
            );
            ++skipped;
            bump_skip("runtime_component");
            continue;
        }

        if (entry == "install.sh" ||
            entry == "uninstall.sh" ||
            update_starts_with(entry_low, "installer/") ||
            update_starts_with(entry_low, "docs/") ||
            entry == "README.md" ||
            entry == "LICENSE" ||
            entry == "pqnas_keygen") {
            add_action(
                "package_support_file",
                "skip",
                raw,
                "",
                "fresh-install/support files are not applied by in-place updater"
            );
            ++skipped;
            bump_skip("support_file");
            continue;
        }

        add_action(
            "unknown",
            "skip",
            raw,
            "",
            "not part of the safe in-place update plan"
        );
        ++skipped;
        bump_skip("unknown");
    }

    json out = json{
        {"ok", true},
        {"status", "plan_built"},
        {"stored_name", stored_name},
        {"package_sha256", package_sha256},
        {"package_size", package_size_bytes},
        {"filename_package_server_version", filename_package_server_version},
        {"package_server_version", package_server_version},
        {"current_server_version", current_server_version},
        {"server_package_is_newer", server_package_is_newer},
        {"manifest_present", manifest_present},
        {"manifest_ok", manifest_ok},
        {"manifest_path", manifest_path},
        {"manifest_error", manifest_error},
        {"manifest_compatibility_ok", manifest_compatibility_ok},
        {"manifest_missing_capabilities", manifest_missing_capabilities},
        {"entry_count", entry_count},
        {"planned_updates", planned_updates},
        {"skipped", skipped},
        {"has_core_binary_action", has_core_binary_action},
        {"skipped_summary", skipped_summary},
        {"actions", actions}
    };

    if (manifest_present && manifest_ok) {
        out["manifest"] = update_manifest;
    }

    return out;
}


// github_update_download_jobs_v1
struct UpdateGithubDownloadJob {
    std::string id;
    std::string status;
    std::string original_name;
    std::string stored_name;
    std::string error;
    std::string message;

    std::uintmax_t downloaded_bytes = 0;
    std::uintmax_t total_bytes = 0;

    bool done = false;
    int http_status = 0;
    long long started_epoch = 0;
    long long updated_epoch = 0;
};

constexpr std::uintmax_t kMaxGithubDownloadPackageBytes =
    512ull * 1024ull * 1024ull;

std::mutex update_github_download_jobs_mu;
std::map<std::string, UpdateGithubDownloadJob> update_github_download_jobs;
std::atomic<unsigned long long> update_github_download_seq{0};

long long update_now_epoch_local() {
    return static_cast<long long>(std::time(nullptr));
}

void update_github_download_cleanup_locked() {
    const long long now = update_now_epoch_local();

    for (auto it = update_github_download_jobs.begin();
         it != update_github_download_jobs.end();) {
        const long long age = now - it->second.updated_epoch;
        if (age > 3600) {
            it = update_github_download_jobs.erase(it);
        } else {
            ++it;
        }
    }
}

json update_github_download_job_json(const UpdateGithubDownloadJob& j) {
    return json{
        {"id", j.id},
        {"status", j.status},
        {"original_name", j.original_name},
        {"stored_name", j.stored_name},
        {"downloaded_bytes", static_cast<unsigned long long>(j.downloaded_bytes)},
        {"total_bytes", static_cast<unsigned long long>(j.total_bytes)},
        {"done", j.done},
        {"http_status", j.http_status},
        {"error", j.error},
        {"message", j.message},
        {"started_epoch", j.started_epoch},
        {"updated_epoch", j.updated_epoch}
    };
}

template <typename Fn>
void update_github_download_mutate(const std::string& job_id, Fn fn) {
    std::lock_guard<std::mutex> lock(update_github_download_jobs_mu);
    auto it = update_github_download_jobs.find(job_id);
    if (it == update_github_download_jobs.end()) return;

    fn(it->second);
    it->second.updated_epoch = update_now_epoch_local();
}

json update_github_download_snapshot(const std::string& job_id, bool* found) {
    std::lock_guard<std::mutex> lock(update_github_download_jobs_mu);
    update_github_download_cleanup_locked();

    const auto it = update_github_download_jobs.find(job_id);
    if (it == update_github_download_jobs.end()) {
        if (found) *found = false;
        return json::object();
    }

    if (found) *found = true;
    return update_github_download_job_json(it->second);
}

std::string update_make_github_download_job_id() {
    const unsigned long long seq =
        update_github_download_seq.fetch_add(1, std::memory_order_relaxed) + 1;

    return "ghdl_" + std::to_string(update_now_epoch_local()) + "_" + std::to_string(seq);
}

bool update_parse_http_url(const std::string& url,
                           std::string* origin,
                           std::string* path,
                           std::string* host_only) {
    if (url.size() > 2048) return false;
    if (url.find_first_of(" \r\n\t") != std::string::npos) return false;

    const std::size_t scheme_pos = url.find("://");
    if (scheme_pos == std::string::npos) return false;

    const std::string scheme = update_lower(url.substr(0, scheme_pos));
    if (scheme != "https" && scheme != "http") return false;

    const std::size_t host_start = scheme_pos + 3;
    const std::size_t path_start = url.find('/', host_start);
    const std::string host_port = path_start == std::string::npos
        ? url.substr(host_start)
        : url.substr(host_start, path_start - host_start);

    if (host_port.empty()) return false;
    if (host_port.find('@') != std::string::npos) return false;

    std::string host = host_port;
    const std::size_t colon = host.find(':');
    if (colon != std::string::npos) {
        host = host.substr(0, colon);
    }

    if (host.empty()) return false;

    if (origin) {
        *origin = scheme + "://" + host_port;
    }

    if (path) {
        *path = path_start == std::string::npos ? "/" : url.substr(path_start);
    }

    if (host_only) {
        *host_only = update_lower(host);
    }

    return true;
}

bool update_is_allowed_github_release_asset_url(const std::string& url) {
    std::string origin;
    std::string path;
    std::string host;

    if (!update_parse_http_url(url, &origin, &path, &host)) return false;
    if (host != "github.com") return false;

    const std::string low_path = update_lower(path);
    return update_starts_with(low_path, "/dna-nexus/pq-nas/releases/download/");
}

std::string update_filename_from_url_path(const std::string& url) {
    std::string origin;
    std::string path;
    std::string host;

    if (!update_parse_http_url(url, &origin, &path, &host)) return "";

    const std::size_t q = path.find('?');
    if (q != std::string::npos) {
        path = path.substr(0, q);
    }

    const std::size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) return path;
    return path.substr(slash + 1);
}

std::string update_sha256_file_hex(const std::filesystem::path& path,
                                   std::string* err) {
    if (err) err->clear();

    std::ifstream in(path, std::ios::binary);
    if (!in.good()) {
        if (err) *err = "open_file_failed";
        return "";
    }

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        if (err) *err = "sha256_ctx_failed";
        return "";
    }

    auto free_ctx = [&]() {
        EVP_MD_CTX_free(ctx);
        ctx = nullptr;
    };

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
        free_ctx();
        if (err) *err = "sha256_init_failed";
        return "";
    }

    std::array<char, 64 * 1024> buf{};
    while (in.good()) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const std::streamsize got = in.gcount();
        if (got > 0) {
            if (EVP_DigestUpdate(ctx, buf.data(), static_cast<std::size_t>(got)) != 1) {
                free_ctx();
                if (err) *err = "sha256_update_failed";
                return "";
            }
        }
    }

    if (in.bad()) {
        free_ctx();
        if (err) *err = "read_file_failed";
        return "";
    }

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;

    if (EVP_DigestFinal_ex(ctx, digest, &digest_len) != 1) {
        free_ctx();
        if (err) *err = "sha256_final_failed";
        return "";
    }

    free_ctx();

    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(static_cast<std::size_t>(digest_len) * 2);

    for (unsigned int i = 0; i < digest_len; ++i) {
        const unsigned char b = digest[i];
        out.push_back(hex[(b >> 4) & 0x0f]);
        out.push_back(hex[b & 0x0f]);
    }

    return out;
}

void update_github_download_worker(UpdateCenterRoutesDeps deps,
                                   std::string job_id,
                                   std::string actor_fp,
                                   std::string url,
                                   std::string safe_name,
                                   std::uintmax_t expected_size) {
    auto fail_job = [&](const std::string& code, const std::string& msg) {
        update_github_download_mutate(job_id, [&](UpdateGithubDownloadJob& j) {
            j.status = "failed";
            j.error = code;
            j.message = msg;
            j.done = true;
        });

        update_audit_emit_local(deps, "update_center.github_download_fail", "fail", actor_fp, json{
            {"ok", false},
            {"status", "failed"},
            {"original_name", safe_name},
            {"error", code},
            {"message", msg}
        });
    };

    std::string origin;
    std::string path;
    std::string host;

    if (!update_parse_http_url(url, &origin, &path, &host)) {
        fail_job("bad_url", "Could not parse GitHub release asset URL.");
        return;
    }

    std::error_code ec;
    const std::filesystem::path incoming = update_incoming_dir(deps);
    std::filesystem::create_directories(incoming, ec);
    if (ec) {
        fail_job("create_incoming_failed", ec.message());
        return;
    }

    const std::filesystem::path tmp_path = incoming / (job_id + "_" + safe_name + ".part");

    std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
    if (!out.good()) {
        fail_job("open_tmp_failed", "Could not open temporary download file.");
        return;
    }

    std::uintmax_t written = 0;
    std::string receiver_error;

    update_github_download_mutate(job_id, [&](UpdateGithubDownloadJob& j) {
        j.status = "running";
        j.total_bytes = expected_size;
    });

    httplib::Client cli(origin);
    cli.set_follow_location(true);
    cli.set_connection_timeout(30, 0);
    cli.set_read_timeout(300, 0);
    cli.set_write_timeout(30, 0);

    httplib::Headers headers = {
        {"User-Agent", "DNA-Nexus-Update-Center"},
        {"Accept", "application/octet-stream"}
    };

    auto receiver = [&](const char* data, std::size_t data_len) -> bool {
        const std::uintmax_t add = static_cast<std::uintmax_t>(data_len);
        if (add > kMaxGithubDownloadPackageBytes ||
            written > kMaxGithubDownloadPackageBytes - add) {
            receiver_error = "download exceeded maximum update package size";
            return false;
        }

        out.write(data, static_cast<std::streamsize>(data_len));
        if (!out.good()) {
            receiver_error = "write failed";
            return false;
        }

        written += add;

        update_github_download_mutate(job_id, [&](UpdateGithubDownloadJob& j) {
            j.downloaded_bytes = written;
            if (j.total_bytes == 0 && expected_size > 0) {
                j.total_bytes = expected_size;
            }
        });

        return true;
    };

    auto progress = [&](std::size_t current, std::size_t total) -> bool {
        const std::uintmax_t cur = static_cast<std::uintmax_t>(current);
        const std::uintmax_t tot = static_cast<std::uintmax_t>(total);

        if (cur > kMaxGithubDownloadPackageBytes ||
            tot > kMaxGithubDownloadPackageBytes) {
            receiver_error = "download exceeded maximum update package size";
            return false;
        }

        update_github_download_mutate(job_id, [&](UpdateGithubDownloadJob& j) {
            if (cur > j.downloaded_bytes) {
                j.downloaded_bytes = cur;
            }
            if (tot > 0) {
                j.total_bytes = tot;
            } else if (j.total_bytes == 0 && expected_size > 0) {
                j.total_bytes = expected_size;
            }
        });

        return true;
    };

    auto result = cli.Get(path, headers, receiver, progress);
    out.close();

    if (!result) {
        std::error_code rm_ec;
        std::filesystem::remove(tmp_path, rm_ec);

        const std::string msg = !receiver_error.empty()
            ? receiver_error
            : httplib::to_string(result.error());

        fail_job("download_failed", msg);
        return;
    }

    update_github_download_mutate(job_id, [&](UpdateGithubDownloadJob& j) {
        j.http_status = result->status;
    });

    if (result->status < 200 || result->status >= 300) {
        std::error_code rm_ec;
        std::filesystem::remove(tmp_path, rm_ec);
        fail_job("http_error", "GitHub download returned HTTP " + std::to_string(result->status));
        return;
    }

    if (written == 0) {
        std::error_code rm_ec;
        std::filesystem::remove(tmp_path, rm_ec);
        fail_job("empty_download", "Downloaded file is empty.");
        return;
    }

    std::string sha_error;
    const std::string sha = update_sha256_file_hex(tmp_path, &sha_error);
    if (sha.empty()) {
        std::error_code rm_ec;
        std::filesystem::remove(tmp_path, rm_ec);
        fail_job("sha256_failed", sha_error.empty() ? "Could not hash downloaded package." : sha_error);
        return;
    }

    const std::string prefix = sha.size() >= 12 ? sha.substr(0, 12) : sha;
    const std::string stored_name = prefix + "_" + safe_name;
    const std::filesystem::path final_path = incoming / stored_name;

    std::filesystem::remove(final_path, ec);
    ec.clear();
    std::filesystem::remove(final_path.string() + ".json", ec);
    ec.clear();

    std::filesystem::rename(tmp_path, final_path, ec);
    if (ec) {
        std::error_code rm_ec;
        std::filesystem::remove(tmp_path, rm_ec);
        fail_job("rename_failed", ec.message());
        return;
    }

    const json meta = {
        {"ok", true},
        {"status", "downloaded"},
        {"source", "github"},
        {"original_name", safe_name},
        {"stored_name", stored_name},
        {"size", static_cast<unsigned long long>(written)},
        {"sha256", sha}
    };

    {
        std::ofstream mf(final_path.string() + ".json", std::ios::binary | std::ios::trunc);
        if (mf.good()) {
            mf << meta.dump(2) << "\n";
        }
    }

    update_github_download_mutate(job_id, [&](UpdateGithubDownloadJob& j) {
        j.status = "done";
        j.original_name = safe_name;
        j.stored_name = stored_name;
        j.downloaded_bytes = written;
        if (j.total_bytes == 0) {
            j.total_bytes = written;
        }
        j.done = true;
        j.message = "GitHub release asset downloaded to server.";
    });

    update_audit_emit_local(deps, "update_center.github_download_ok", "ok", actor_fp, meta);
}

void register_update_center_routes(httplib::Server& srv, const UpdateCenterRoutesDeps& deps) {
    srv.Get("/admin/updates", [deps](const httplib::Request& req, httplib::Response& res) {
        std::string actor_fp;
        if (!update_require_admin_actor_local(deps, req, res, &actor_fp)) return;

        std::string body;
        if (!deps.read_file_to_string(deps.static_admin_updates_html, body)) {
            res.status = 404;
            res.body = "Missing static file: " + deps.static_admin_updates_html;
            return;
        }

        res.set_header("Cache-Control", "no-store");
        res.set_content(body, "text/html; charset=utf-8");
    });

    srv.Get("/api/v4/admin/updates/status", [deps](const httplib::Request& req, httplib::Response& res) {
        std::string actor_fp;
        if (!update_require_admin_actor_local(deps, req, res, &actor_fp)) return;

        std::error_code ec;
        const std::filesystem::path incoming = update_incoming_dir(deps);
        std::filesystem::create_directories(incoming, ec);
        if (ec) {
            deps.reply_json(res, 500, json{
                {"ok", false},
                {"error", "create_incoming_failed"},
                {"message", ec.message()}
            }.dump(2));
            return;
        }

        json files = json::array();

        std::error_code it_ec;
        for (const auto& ent : std::filesystem::directory_iterator(incoming, it_ec)) {
            if (it_ec) break;

            std::error_code st_ec;
            if (!ent.is_regular_file(st_ec) || st_ec) continue;

            const std::string name = ent.path().filename().string();
            const std::string low = update_lower(name);

            if (update_ends_with(low, ".part") || update_ends_with(low, ".json")) {
                continue;
            }

            std::uintmax_t sz = ent.file_size(st_ec);
            if (st_ec) sz = 0;

            files.push_back(json{
                {"name", name},
                {"size", sz}
            });
        }

        deps.reply_json(res, 200, json{
            {"ok", true},
            {"incoming_count", files.size()},
            {"incoming", files}
        }.dump(2));
    });


    srv.Post("/api/v4/admin/updates/github-download", [deps](const httplib::Request& req, httplib::Response& res) {
        std::string actor_fp;
        if (!update_require_admin_actor_local(deps, req, res, &actor_fp)) return;
        if (!deps.require_same_origin(req, res)) return;

        std::string url;
        std::string name;
        std::uintmax_t expected_size = 0;

        try {
            const json body = json::parse(req.body.empty() ? "{}" : req.body);
            url = body.value("url", "");
            name = body.value("name", "");

            if (body.contains("size")) {
                if (body["size"].is_number_unsigned()) {
                    expected_size = body["size"].get<std::uintmax_t>();
                } else if (body["size"].is_number_integer()) {
                    const long long signed_size = body["size"].get<long long>();
                    if (signed_size > 0) {
                        expected_size = static_cast<std::uintmax_t>(signed_size);
                    }
                }
            }
        } catch (...) {
            deps.reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_json"}
            }.dump(2));
            return;
        }

        if (url.empty()) {
            deps.reply_json(res, 400, json{
                {"ok", false},
                {"error", "missing_url"}
            }.dump(2));
            return;
        }

        if (!update_is_allowed_github_release_asset_url(url)) {
            deps.reply_json(res, 400, json{
                {"ok", false},
                {"error", "unsupported_url"},
                {"message", "Only DNA-Nexus/pq-nas GitHub release asset URLs are accepted."}
            }.dump(2));
            return;
        }

        if (name.empty()) {
            name = update_filename_from_url_path(url);
        }

        std::string filename_error;
        const std::string safe_name = update_safe_filename(name, &filename_error);
        if (safe_name.empty()) {
            deps.reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_filename"},
                {"message", filename_error}
            }.dump(2));
            return;
        }

        if (expected_size > kMaxGithubDownloadPackageBytes) {
            deps.reply_json(res, 413, json{
                {"ok", false},
                {"error", "download_too_large"},
                {"max_bytes", static_cast<unsigned long long>(kMaxGithubDownloadPackageBytes)}
            }.dump(2));
            return;
        }

        const std::string job_id = update_make_github_download_job_id();
        const long long now = update_now_epoch_local();

        UpdateGithubDownloadJob job;
        job.id = job_id;
        job.status = "queued";
        job.original_name = safe_name;
        job.total_bytes = expected_size;
        job.started_epoch = now;
        job.updated_epoch = now;

        {
            std::lock_guard<std::mutex> lock(update_github_download_jobs_mu);
            update_github_download_cleanup_locked();
            update_github_download_jobs[job_id] = job;
        }

        update_audit_emit_local(deps, "update_center.github_download_start", "ok", actor_fp, json{
            {"ok", true},
            {"status", "queued"},
            {"original_name", safe_name},
            {"size", static_cast<unsigned long long>(expected_size)}
        });

        update_activity_record_local(
            deps,
            req,
            actor_fp,
            "update.github_download",
            "GitHub update package download started",
            json{
                {"status", "queued"},
                {"original_name", safe_name},
                {"size", static_cast<unsigned long long>(expected_size)}
            }
        );

        std::thread(
            update_github_download_worker,
            deps,
            job_id,
            actor_fp,
            url,
            safe_name,
            expected_size
        ).detach();

        bool found = false;
        const json snap = update_github_download_snapshot(job_id, &found);

        deps.reply_json(res, 200, json{
            {"ok", true},
            {"job", snap}
        }.dump(2));
    });

    srv.Get("/api/v4/admin/updates/github-download/status", [deps](const httplib::Request& req, httplib::Response& res) {
        std::string actor_fp;
        if (!update_require_admin_actor_local(deps, req, res, &actor_fp)) return;

        const std::string job_id = req.has_param("job_id") ? req.get_param_value("job_id") : "";
        if (job_id.empty()) {
            deps.reply_json(res, 400, json{
                {"ok", false},
                {"error", "missing_job_id"}
            }.dump(2));
            return;
        }

        bool found = false;
        const json snap = update_github_download_snapshot(job_id, &found);
        if (!found) {
            deps.reply_json(res, 404, json{
                {"ok", false},
                {"error", "job_not_found"}
            }.dump(2));
            return;
        }

        deps.reply_json(res, 200, json{
            {"ok", true},
            {"job", snap}
        }.dump(2));
    });

    srv.Post("/api/v4/admin/updates/upload", [deps](const httplib::Request& req, httplib::Response& res) {
        std::string actor_fp;
        if (!update_require_admin_actor_local(deps, req, res, &actor_fp)) return;
        if (!deps.require_same_origin(req, res)) return;

        constexpr std::size_t kMaxUpdatePackageBytes = 512ull * 1024ull * 1024ull;

        if (req.body.empty()) {
            deps.reply_json(res, 400, json{
                {"ok", false},
                {"error", "empty_upload"},
                {"message", "Upload body is empty."}
            }.dump(2));
            return;
        }

        if (req.body.size() > kMaxUpdatePackageBytes) {
            deps.reply_json(res, 413, json{
                {"ok", false},
                {"error", "upload_too_large"},
                {"max_bytes", kMaxUpdatePackageBytes}
            }.dump(2));
            return;
        }

        std::string original_name = req.get_header_value("X-PQNAS-Filename");
        if (original_name.empty()) {
            original_name = "update-package";
        }

        std::string filename_error;
        const std::string safe_name = update_safe_filename(original_name, &filename_error);
        if (safe_name.empty()) {
            deps.reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_filename"},
                {"message", filename_error}
            }.dump(2));
            return;
        }

        std::error_code ec;
        const std::filesystem::path incoming = update_incoming_dir(deps);
        std::filesystem::create_directories(incoming, ec);
        if (ec) {
            deps.reply_json(res, 500, json{
                {"ok", false},
                {"error", "create_incoming_failed"},
                {"message", ec.message()}
            }.dump(2));
            return;
        }

        const std::string sha = deps.sha256_hex(req.body);
        const std::string prefix = sha.size() >= 12 ? sha.substr(0, 12) : sha;
        const std::string stored_name = prefix + "_" + safe_name;

        const std::filesystem::path tmp_path = incoming / (stored_name + ".part");
        const std::filesystem::path final_path = incoming / stored_name;

        {
            std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
            if (!out.good()) {
                deps.reply_json(res, 500, json{
                    {"ok", false},
                    {"error", "open_tmp_failed"}
                }.dump(2));
                return;
            }

            out.write(req.body.data(), static_cast<std::streamsize>(req.body.size()));
            if (!out.good()) {
                std::error_code rm_ec;
                std::filesystem::remove(tmp_path, rm_ec);

                deps.reply_json(res, 500, json{
                    {"ok", false},
                    {"error", "write_failed"}
                }.dump(2));
                return;
            }
        }

        std::filesystem::rename(tmp_path, final_path, ec);
        if (ec && std::filesystem::exists(final_path)) {
            std::error_code rm_ec;
            std::filesystem::remove(final_path, rm_ec);
            ec.clear();
            std::filesystem::rename(tmp_path, final_path, ec);
        }

        if (ec) {
            std::error_code rm_ec;
            std::filesystem::remove(tmp_path, rm_ec);

            deps.reply_json(res, 500, json{
                {"ok", false},
                {"error", "rename_failed"},
                {"message", ec.message()}
            }.dump(2));
            return;
        }

        const json meta = {
            {"ok", true},
            {"status", "uploaded"},
            {"original_name", safe_name},
            {"stored_name", stored_name},
            {"size", req.body.size()},
            {"sha256", sha}
        };

        {
            std::ofstream mf(final_path.string() + ".json", std::ios::binary | std::ios::trunc);
            if (mf.good()) {
                mf << meta.dump(2) << "\n";
            }
        }

        update_audit_emit_local(deps, "update_center.upload_ok", "ok", actor_fp, meta);
        update_activity_record_local(
            deps,
            req,
            actor_fp,
            "update.upload",
            "Update package uploaded",
            meta
        );
        deps.reply_json(res, 200, meta.dump(2));
    });


    srv.Post("/api/v4/admin/updates/delete", [deps](const httplib::Request& req, httplib::Response& res) {
        std::string actor_fp;
        if (!update_require_admin_actor_local(deps, req, res, &actor_fp)) return;
        if (!deps.require_same_origin(req, res)) return;

        std::string stored_name;

        try {
            const json body = json::parse(req.body.empty() ? "{}" : req.body);
            stored_name = body.value("stored_name", "");
        } catch (...) {
            deps.reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_json"}
            }.dump(2));
            return;
        }

        if (stored_name.empty()) {
            deps.reply_json(res, 400, json{
                {"ok", false},
                {"error", "missing_stored_name"}
            }.dump(2));
            return;
        }

        if (!update_is_safe_staged_package_name(stored_name)) {
            deps.reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_stored_name"},
                {"message", "Invalid staged update package name."}
            }.dump(2));
            return;
        }

        const std::filesystem::path incoming = update_incoming_dir(deps);
        const std::filesystem::path package_path = incoming / stored_name;
        const std::filesystem::path meta_path = incoming / (stored_name + ".json");

        std::error_code ec;
        if (!std::filesystem::is_regular_file(package_path, ec) || ec) {
            deps.reply_json(res, 404, json{
                {"ok", false},
                {"error", "package_not_found"}
            }.dump(2));
            return;
        }

        std::uintmax_t size = 0;
        ec.clear();
        size = std::filesystem::file_size(package_path, ec);
        if (ec) size = 0;

        ec.clear();
        std::filesystem::remove(package_path, ec);
        if (ec) {
            deps.reply_json(res, 500, json{
                {"ok", false},
                {"error", "delete_failed"},
                {"message", ec.message()}
            }.dump(2));
            return;
        }

        std::error_code meta_ec;
        std::filesystem::remove(meta_path, meta_ec);

        const json out = {
            {"ok", true},
            {"status", "deleted"},
            {"stored_name", stored_name},
            {"size", size}
        };

        update_audit_emit_local(deps, "update_center.delete_staged_ok", "ok", actor_fp, out);
        update_activity_record_local(
            deps,
            req,
            actor_fp,
            "update.delete_staged",
            "Staged update package deleted",
            out
        );

        deps.reply_json(res, 200, out.dump(2));
    });

    srv.Post("/api/v4/admin/updates/verify", [deps](const httplib::Request& req, httplib::Response& res) {
        std::string actor_fp;
        if (!update_require_admin_actor_local(deps, req, res, &actor_fp)) return;
        if (!deps.require_same_origin(req, res)) return;

        std::string stored_name;

        try {
            const json body = json::parse(req.body.empty() ? "{}" : req.body);
            stored_name = body.value("stored_name", "");
        } catch (...) {
            deps.reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_json"}
            }.dump(2));
            return;
        }

        if (stored_name.empty()) {
            deps.reply_json(res, 400, json{
                {"ok", false},
                {"error", "missing_stored_name"}
            }.dump(2));
            return;
        }

        if (stored_name.find('/') != std::string::npos ||
            stored_name.find('\\') != std::string::npos ||
            stored_name.find("..") != std::string::npos) {
            deps.reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_stored_name"}
            }.dump(2));
            return;
        }

        const std::filesystem::path incoming = update_incoming_dir(deps);
        const std::filesystem::path package_path = incoming / stored_name;

        std::error_code ec;
        if (!std::filesystem::is_regular_file(package_path, ec) || ec) {
            deps.reply_json(res, 404, json{
                {"ok", false},
                {"error", "package_not_found"}
            }.dump(2));
            return;
        }

        const std::string low = update_lower(stored_name);
        const bool is_tar =
            update_ends_with(low, ".tar.gz") ||
            update_ends_with(low, ".tgz");

        if (!is_tar) {
            deps.reply_json(res, 400, json{
                {"ok", false},
                {"error", "verify_format_not_supported_yet"},
                {"message", "Phase 2B verifies .tar.gz/.tgz packages only."}
            }.dump(2));
            return;
        }

        const std::uintmax_t package_size = std::filesystem::file_size(package_path, ec);
        if (ec || package_size == 0) {
            deps.reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_package_size"}
            }.dump(2));
            return;
        }

        int tar_status = -1;
        const std::string cmd =
            "tar -tzf " + update_shell_quote(package_path.string()) + " 2>&1";
        const std::string listing =
            update_run_command_limited(cmd, 2u * 1024u * 1024u, &tar_status);

        if (tar_status != 0) {
            deps.reply_json(res, 400, json{
                {"ok", false},
                {"error", "tar_list_failed"},
                {"status", tar_status},
                {"output", listing.substr(0, 12000)}
            }.dump(2));
            return;
        }

        json unsafe = json::array();
        json sample = json::array();
        json warnings = json::array();

        std::size_t entries = 0;
        bool has_pqnas_binary = false;
        bool has_static_files = false;

        std::istringstream in(listing);
        std::string line;

        while (std::getline(in, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
                line.pop_back();
            }

            if (line.empty()) continue;

            ++entries;
            if (sample.size() < 40) sample.push_back(line);

            std::string norm = line;
            for (char& c : norm) {
                if (c == '\\') c = '/';
            }

            const std::string norm_low = update_lower(norm);

            if (!norm.empty() && norm[0] == '/') {
                unsafe.push_back(json{{"entry", line}, {"reason", "absolute path"}});
            }

            if (norm == ".." ||
                update_starts_with(norm, "../") ||
                norm.find("/../") != std::string::npos ||
                update_ends_with(norm, "/..")) {
                unsafe.push_back(json{{"entry", line}, {"reason", "path traversal"}});
            }

            if (norm_low.find("pqnas_server") != std::string::npos) {
                has_pqnas_binary = true;
            }

            if (norm_low.find("server/src/static/") != std::string::npos ||
                norm_low.find("/static/") != std::string::npos ||
                update_starts_with(norm_low, "static/")) {
                has_static_files = true;
            }

            if (entries > 50000) {
                warnings.push_back("package has more than 50000 listed entries; listing was truncated for verification summary");
                break;
            }
        }

        if (entries == 0) {
            deps.reply_json(res, 400, json{
                {"ok", false},
                {"error", "empty_archive_listing"}
            }.dump(2));
            return;
        }

        if (!has_pqnas_binary) {
            warnings.push_back("pqnas_server binary was not found in the archive listing");
        }

        if (!has_static_files) {
            warnings.push_back("static files directory was not found in the archive listing");
        }

        const bool safe = unsafe.empty();

        json out = json{
            {"ok", safe},
            {"status", safe ? "verified" : "unsafe"},
            {"stored_name", stored_name},
            {"size", package_size},
            {"entry_count", entries},
            {"has_pqnas_binary", has_pqnas_binary},
            {"has_static_files", has_static_files},
            {"unsafe_entries", unsafe},
            {"warnings", warnings},
            {"sample_entries", sample}
        };

        update_audit_emit_local(
            deps,
            safe ? "update_center.verify_ok" : "update_center.verify_fail",
            safe ? "ok" : "fail",
            actor_fp,
            out
        );
        update_activity_record_local(
            deps,
            req,
            actor_fp,
            safe ? "update.verify" : "update.verify_failed",
            safe ? "Update package verified" : "Update package verification failed",
            out
        );

        deps.reply_json(res, safe ? 200 : 400, out.dump(2));
    });

    srv.Post("/api/v4/admin/updates/plan", [deps](const httplib::Request& req, httplib::Response& res) {
        std::string actor_fp;
        if (!update_require_admin_actor_local(deps, req, res, &actor_fp)) return;
        if (!deps.require_same_origin(req, res)) return;

        std::string stored_name;

        try {
            const json body = json::parse(req.body.empty() ? "{}" : req.body);
            stored_name = body.value("stored_name", "");
        } catch (...) {
            deps.reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_json"}
            }.dump(2));
            return;
        }

        if (stored_name.empty()) {
            deps.reply_json(res, 400, json{
                {"ok", false},
                {"error", "missing_stored_name"}
            }.dump(2));
            return;
        }

        if (stored_name.find('/') != std::string::npos ||
            stored_name.find('\\') != std::string::npos ||
            stored_name.find("..") != std::string::npos) {
            deps.reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_stored_name"}
            }.dump(2));
            return;
        }

        const std::filesystem::path package_path = update_incoming_dir(deps) / stored_name;

        std::error_code ec;
        if (!std::filesystem::is_regular_file(package_path, ec) || ec) {
            deps.reply_json(res, 404, json{
                {"ok", false},
                {"error", "package_not_found"}
            }.dump(2));
            return;
        }

        std::string package_bytes;
        {
            std::ifstream pf(package_path, std::ios::binary);
            if (!pf.good()) {
                deps.reply_json(res, 500, json{
                    {"ok", false},
                    {"error", "open_package_failed"}
                }.dump(2));
                return;
            }

            package_bytes.assign(
                std::istreambuf_iterator<char>(pf),
                std::istreambuf_iterator<char>()
            );
        }

        const std::string low = update_lower(stored_name);
        const bool is_tar =
            update_ends_with(low, ".tar.gz") ||
            update_ends_with(low, ".tgz");

        if (!is_tar) {
            deps.reply_json(res, 400, json{
                {"ok", false},
                {"error", "plan_format_not_supported_yet"},
                {"message", "R6c test route plans .tar.gz/.tgz packages only."}
            }.dump(2));
            return;
        }

        int tar_status = -1;
        const std::string cmd =
            "tar -tzf " + update_shell_quote(package_path.string()) + " 2>&1";
        const std::string listing =
            update_run_command_limited(cmd, 2u * 1024u * 1024u, &tar_status);

        if (tar_status != 0) {
            deps.reply_json(res, 400, json{
                {"ok", false},
                {"error", "tar_list_failed"},
                {"status", tar_status},
                {"output", listing.substr(0, 12000)}
            }.dump(2));
            return;
        }

        const json manifest_info = update_extract_update_manifest_info(package_path);
        json plan = build_update_plan_json(deps, stored_name, listing, package_bytes, manifest_info);
        const std::string plan_hash = deps.sha256_hex(plan.dump());
        const std::string plan_id = plan_hash.substr(0, 16) + "_" + stored_name;

        plan["plan_hash"] = plan_hash;
        plan["plan_id"] = plan_id;
        plan["install_contract"] =
            "Install must use plan_id and revalidate package_sha256 and plan_hash before applying.";

        std::error_code plan_ec;
        const std::filesystem::path plans_dir = updates_root_dir(deps) / "plans";
        std::filesystem::create_directories(plans_dir, plan_ec);
        if (plan_ec) {
            deps.reply_json(res, 500, json{
                {"ok", false},
                {"error", "create_plans_failed"},
                {"message", plan_ec.message()}
            }.dump(2));
            return;
        }

        const std::filesystem::path tmp_plan_path = plans_dir / (plan_id + ".json.part");
        const std::filesystem::path final_plan_path = plans_dir / (plan_id + ".json");

        {
            std::ofstream out(tmp_plan_path, std::ios::binary | std::ios::trunc);
            if (!out.good()) {
                deps.reply_json(res, 500, json{
                    {"ok", false},
                    {"error", "open_plan_tmp_failed"}
                }.dump(2));
                return;
            }

            out << plan.dump(2) << "\n";
            if (!out.good()) {
                deps.reply_json(res, 500, json{
                    {"ok", false},
                    {"error", "write_plan_failed"}
                }.dump(2));
                return;
            }
        }

        std::filesystem::rename(tmp_plan_path, final_plan_path, plan_ec);
        if (plan_ec && std::filesystem::exists(final_plan_path)) {
            std::error_code rm_ec;
            std::filesystem::remove(final_plan_path, rm_ec);
            plan_ec.clear();
            std::filesystem::rename(tmp_plan_path, final_plan_path, plan_ec);
        }

        if (plan_ec) {
            std::error_code rm_ec;
            std::filesystem::remove(tmp_plan_path, rm_ec);

            deps.reply_json(res, 500, json{
                {"ok", false},
                {"error", "rename_plan_failed"},
                {"message", plan_ec.message()}
            }.dump(2));
            return;
        }

        plan["plan_saved"] = true;
        plan["plan_path"] = final_plan_path.string();

        update_audit_emit_local(deps, "update_center.plan_ok", "ok", actor_fp, plan);
        update_activity_record_local(
            deps,
            req,
            actor_fp,
            "update.plan",
            "Update install plan built",
            plan
        );
        deps.reply_json(res, 200, plan.dump(2));
    });

    srv.Post("/api/v4/admin/updates/install", [deps](const httplib::Request& req, httplib::Response& res) {
        std::string actor_fp;
        if (!update_require_admin_actor_local(deps, req, res, &actor_fp)) return;
        if (!deps.require_same_origin(req, res)) return;

        std::string plan_id;

        try {
            const json body = json::parse(req.body.empty() ? "{}" : req.body);
            plan_id = body.value("plan_id", "");
        } catch (...) {
            deps.reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_json"}
            }.dump(2));
            return;
        }

        if (plan_id.empty()) {
            deps.reply_json(res, 400, json{
                {"ok", false},
                {"error", "missing_plan_id"}
            }.dump(2));
            return;
        }

        if (plan_id.size() > 240 ||
            plan_id.find('/') != std::string::npos ||
            plan_id.find('\\') != std::string::npos ||
            plan_id.find("..") != std::string::npos) {
            deps.reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_plan_id"}
            }.dump(2));
            return;
        }

        for (char c : plan_id) {
            const bool ok =
                (c >= 'a' && c <= 'z') ||
                (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') ||
                c == '.' || c == '_' || c == '-';

            if (!ok) {
                deps.reply_json(res, 400, json{
                    {"ok", false},
                    {"error", "bad_plan_id_chars"}
                }.dump(2));
                return;
            }
        }

        const std::string helper_enabled =
            deps.getenv_str ? deps.getenv_str("PQNAS_UPDATE_HELPER_ENABLED") : "";

        if (helper_enabled == "1" ||
            helper_enabled == "true" ||
            helper_enabled == "TRUE" ||
            helper_enabled == "yes" ||
            helper_enabled == "YES") {
            std::string helper_path =
                deps.getenv_str ? deps.getenv_str("PQNAS_UPDATE_HELPER_PATH") : "";

            if (helper_path.empty()) {
                helper_path = "/usr/local/libexec/pqnas/pqnas_update_apply.py";
            }

            if (helper_path.find('\'') != std::string::npos) {
                deps.reply_json(res, 500, json{
                    {"ok", false},
                    {"error", "bad_helper_path"},
                    {"message", "Helper path must not contain single quotes."}
                }.dump(2));
                return;
            }

            const std::string cmd =
                "timeout 30 " +
                update_shell_quote(helper_path) +
                " --plan-id " +
                update_shell_quote(plan_id) +
                " --validation-only 2>&1";

            int helper_status = -1;
            const std::string helper_output =
                update_run_command_limited(cmd, 2u * 1024u * 1024u, &helper_status);

            json helper_json;
            bool parsed_json = false;

            try {
                helper_json = json::parse(helper_output);
                parsed_json = true;
            } catch (...) {
                parsed_json = false;
            }

            if (!parsed_json) {
                deps.reply_json(res, 500, json{
                    {"ok", false},
                    {"error", "helper_bad_output"},
                    {"helper_status", helper_status},
                    {"output", helper_output.substr(0, 12000)}
                }.dump(2));
                return;
            }

            int helper_exit_code = helper_status;
            if (helper_status >= 0) {
                if (WIFEXITED(helper_status)) {
                    helper_exit_code = WEXITSTATUS(helper_status);
                } else if (helper_status % 256 == 0) {
                    helper_exit_code = helper_status / 256;
                }
            }

            helper_json["helper_enabled"] = true;
            helper_json["helper_status"] = helper_status;
            helper_json["helper_exit_code"] = helper_exit_code;
            helper_json["install_helper_not_enabled_yet"] = true;
            helper_json["install_performed"] = false;

            const bool ok = helper_json.value("ok", false);
            update_audit_emit_local(
                deps,
                ok ? "update_center.install_validate_ok" : "update_center.install_validate_fail",
                ok ? "ok" : "fail",
                actor_fp,
                helper_json
            );
            update_activity_record_local(
                deps,
                req,
                actor_fp,
                ok ? "update.install_validate" : "update.install_validate_failed",
                ok ? "Update install plan validated" : "Update install plan validation failed",
                helper_json
            );
            deps.reply_json(res, ok ? 200 : 400, helper_json.dump(2));
            return;
        }

        const std::filesystem::path plans_dir = updates_root_dir(deps) / "plans";
        const std::filesystem::path plan_path = plans_dir / (plan_id + ".json");

        std::error_code ec;
        if (!std::filesystem::is_regular_file(plan_path, ec) || ec) {
            deps.reply_json(res, 404, json{
                {"ok", false},
                {"error", "plan_not_found"}
            }.dump(2));
            return;
        }

        json plan;
        {
            std::ifstream pf(plan_path, std::ios::binary);
            if (!pf.good()) {
                deps.reply_json(res, 500, json{
                    {"ok", false},
                    {"error", "open_plan_failed"}
                }.dump(2));
                return;
            }

            try {
                pf >> plan;
            } catch (...) {
                deps.reply_json(res, 400, json{
                    {"ok", false},
                    {"error", "bad_plan_json"}
                }.dump(2));
                return;
            }
        }

        const std::string stored_plan_id = plan.value("plan_id", "");
        const std::string stored_plan_hash = plan.value("plan_hash", "");
        const std::string stored_name = plan.value("stored_name", "");
        const std::string expected_package_sha256 = plan.value("package_sha256", "");
        const std::string package_server_version = plan.value("package_server_version", "");

        if (stored_plan_id != plan_id) {
            deps.reply_json(res, 400, json{
                {"ok", false},
                {"error", "plan_id_mismatch"},
                {"requested_plan_id", plan_id},
                {"stored_plan_id", stored_plan_id}
            }.dump(2));
            return;
        }

        json canonical = plan;
        canonical.erase("plan_hash");
        canonical.erase("plan_id");
        canonical.erase("install_contract");
        canonical.erase("plan_saved");
        canonical.erase("plan_path");

        const std::string computed_plan_hash = deps.sha256_hex(canonical.dump());

        if (stored_plan_hash.empty() || computed_plan_hash != stored_plan_hash) {
            deps.reply_json(res, 400, json{
                {"ok", false},
                {"error", "plan_hash_mismatch"},
                {"stored_plan_hash", stored_plan_hash},
                {"computed_plan_hash", computed_plan_hash}
            }.dump(2));
            return;
        }

        if (stored_name.empty() ||
            stored_name.find('/') != std::string::npos ||
            stored_name.find('\\') != std::string::npos ||
            stored_name.find("..") != std::string::npos) {
            deps.reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_stored_name_in_plan"}
            }.dump(2));
            return;
        }

        const std::filesystem::path package_path = update_incoming_dir(deps) / stored_name;

        if (!std::filesystem::is_regular_file(package_path, ec) || ec) {
            deps.reply_json(res, 404, json{
                {"ok", false},
                {"error", "package_not_found"},
                {"stored_name", stored_name}
            }.dump(2));
            return;
        }

        std::string package_bytes;
        {
            std::ifstream in(package_path, std::ios::binary);
            if (!in.good()) {
                deps.reply_json(res, 500, json{
                    {"ok", false},
                    {"error", "open_package_failed"}
                }.dump(2));
                return;
            }

            package_bytes.assign(
                std::istreambuf_iterator<char>(in),
                std::istreambuf_iterator<char>()
            );
        }

        const std::string actual_package_sha256 = deps.sha256_hex(package_bytes);

        if (expected_package_sha256.empty() || actual_package_sha256 != expected_package_sha256) {
            deps.reply_json(res, 400, json{
                {"ok", false},
                {"error", "package_sha256_mismatch"},
                {"expected_package_sha256", expected_package_sha256},
                {"actual_package_sha256", actual_package_sha256}
            }.dump(2));
            return;
        }

        auto update_install_version_numbers = [](const std::string& v) -> std::vector<long long> {
            std::vector<long long> nums;
            std::string cur;

            for (char c : v) {
                if (c >= '0' && c <= '9') {
                    cur.push_back(c);
                } else if (!cur.empty()) {
                    try {
                        nums.push_back(std::stoll(cur));
                    } catch (...) {
                        nums.push_back(0);
                    }
                    cur.clear();
                }
            }

            if (!cur.empty()) {
                try {
                    nums.push_back(std::stoll(cur));
                } catch (...) {
                    nums.push_back(0);
                }
            }

            return nums;
        };

        auto update_install_compare_versions = [&](const std::string& a, const std::string& b) -> int {
            const std::vector<long long> av = update_install_version_numbers(a);
            const std::vector<long long> bv = update_install_version_numbers(b);

            if (av.empty() || bv.empty()) return 0;

            const std::size_t n = std::max(av.size(), bv.size());
            for (std::size_t i = 0; i < n; ++i) {
                const long long ai = i < av.size() ? av[i] : 0;
                const long long bi = i < bv.size() ? bv[i] : 0;

                if (ai < bi) return -1;
                if (ai > bi) return 1;
            }

            return 0;
        };

        auto update_install_strip_archive_suffix = [&](std::string name) -> std::string {
            const std::string low = update_lower(name);

            if (update_ends_with(low, ".tar.gz")) return name.substr(0, name.size() - 7);
            if (update_ends_with(low, ".tgz")) return name.substr(0, name.size() - 4);
            if (update_ends_with(low, ".zip")) return name.substr(0, name.size() - 4);
            if (update_ends_with(low, ".dnxupd")) return name.substr(0, name.size() - 7);

            return name;
        };

        auto update_install_basename = [](const std::string& path) -> std::string {
            std::string s = path;
            for (char& c : s) {
                if (c == '\\') c = '/';
            }

            const std::size_t slash = s.find_last_of('/');
            if (slash != std::string::npos) return s.substr(slash + 1);
            return s;
        };

        auto update_install_extract_app_package_version =
            [&](const std::string& source, const std::string& app_id) -> std::string {
                if (app_id.empty()) return "";

                std::string base =
                    update_install_strip_archive_suffix(update_install_basename(source));

                const std::string low_base = update_lower(base);
                const std::string low_app = update_lower(app_id);

                const std::string prefix_dash = low_app + "-";
                const std::string prefix_underscore = low_app + "_";

                if (update_starts_with(low_base, prefix_dash)) {
                    return base.substr(app_id.size() + 1);
                }

                if (update_starts_with(low_base, prefix_underscore)) {
                    return base.substr(app_id.size() + 1);
                }

                return "";
            };

        auto update_install_latest_installed_app_version = [&](const std::string& app_id) -> std::string {
            if (app_id.empty()) return "";

            std::error_code app_ec;
            const std::filesystem::path root = std::filesystem::path(deps.apps_installed_dir) / app_id;

            if (!std::filesystem::exists(root, app_ec) || app_ec) return "";

            std::string best;
            std::error_code it_ec;

            for (const auto& ent : std::filesystem::directory_iterator(root, it_ec)) {
                if (it_ec) break;

                std::error_code st_ec;
                if (!ent.is_directory(st_ec) || st_ec) continue;

                const std::string ver = ent.path().filename().string();
                if (ver.empty()) continue;

                if (best.empty() || update_install_compare_versions(ver, best) > 0) {
                    best = ver;
                }
            }

            return best;
        };

        std::string current_server_version;
#ifdef PQNAS_VERSION
        current_server_version = PQNAS_VERSION;
#endif
        if (current_server_version.empty()) {
            const std::string env = deps.getenv_str("PQNAS_CURRENT_VERSION");
            if (!env.empty()) current_server_version = env;
        }

        json validation_errors = json::array();
        json applicable_actions = json::array();

        auto add_validation_error = [&](const std::string& code,
                                        const std::string& message,
                                        const json& action = json::object()) {
            json e = {
                {"code", code},
                {"message", message}
            };
            if (!action.empty()) e["action"] = action;
            validation_errors.push_back(e);
        };

        const json actions = plan.value("actions", json::array());

        if (!actions.is_array()) {
            add_validation_error("bad_actions", "plan actions is not an array");
        }

        for (const auto& a : actions) {
            if (!a.is_object()) continue;

            const std::string action = a.value("action", "");
            const std::string type = a.value("type", "");
            const std::string source = a.value("source", "");
            const std::string app_id = a.value("app_id", "");

            const bool is_update_action =
                action == "update" ||
                action == "update_existing_app" ||
                action == "update_existing_app_package" ||
                action.find("update") != std::string::npos;

            if (action == "reject") {
                add_validation_error("reject_action_present", "plan contains a reject action", a);
                continue;
            }

            if (!is_update_action) {
                continue;
            }

            if (type == "core_binary" || type == "static_file") {
                if (package_server_version.empty()) {
                    add_validation_error(
                        "package_server_version_unknown",
                        "server package version is unknown; refusing install",
                        a
                    );
                    continue;
                }

                if (current_server_version.empty()) {
                    add_validation_error(
                        "current_server_version_unknown",
                        "current server version is unknown; refusing install",
                        a
                    );
                    continue;
                }

                if (update_install_compare_versions(package_server_version, current_server_version) <= 0) {
                    add_validation_error(
                        "server_package_not_newer",
                        "server package version " + package_server_version +
                            " is not newer than current server version " + current_server_version,
                        a
                    );
                    continue;
                }

                applicable_actions.push_back(a);
                continue;
            }

            if (type == "bundled_app_package" ||
                type == "bundled_app" ||
                type == "installed_app") {
                const std::string package_app_version =
                    update_install_extract_app_package_version(source, app_id);
                const std::string installed_app_version =
                    update_install_latest_installed_app_version(app_id);

                if (app_id.empty()) {
                    add_validation_error("app_id_missing", "app update action is missing app_id", a);
                    continue;
                }

                if (package_app_version.empty()) {
                    add_validation_error(
                        "app_package_version_unknown",
                        "app package version is unknown; refusing app update",
                        a
                    );
                    continue;
                }

                if (installed_app_version.empty()) {
                    add_validation_error(
                        "installed_app_version_unknown",
                        "installed app version is unknown; refusing app update",
                        a
                    );
                    continue;
                }

                if (update_install_compare_versions(package_app_version, installed_app_version) <= 0) {
                    add_validation_error(
                        "app_package_not_newer",
                        "app package version " + package_app_version +
                            " is not newer than installed version " + installed_app_version,
                        a
                    );
                    continue;
                }

                applicable_actions.push_back(a);
                continue;
            }

            add_validation_error(
                "unsupported_update_action",
                "update action type is not supported by installer validation",
                a
            );
        }

        if (validation_errors.empty() && applicable_actions.empty()) {
            add_validation_error(
                "no_applicable_actions",
                "Plan has no installable update actions. Package may be older than the current installation or all actions were skipped."
            );
        }

        const bool installable =
            validation_errors.empty() && !applicable_actions.empty();

        deps.reply_json(res, installable ? 200 : 400, json{
            {"ok", installable},
            {"validated", validation_errors.empty()},
            {"installable", installable},
            {"install_helper_not_enabled_yet", true},
            {"message", installable
                ? "Plan validated. Install helper is not enabled yet; nothing was installed."
                : "Plan is not installable. Nothing was installed."},
            {"plan_id", plan_id},
            {"plan_hash", stored_plan_hash},
            {"computed_plan_hash", computed_plan_hash},
            {"stored_name", stored_name},
            {"package_sha256", actual_package_sha256},
            {"package_server_version", package_server_version},
            {"current_server_version", current_server_version},
            {"applicable_action_count", applicable_actions.size()},
            {"validation_errors", validation_errors},
            {"applicable_actions", applicable_actions}
        }.dump(2));
    });

    srv.Post("/api/v4/admin/updates/dry-run", [deps](const httplib::Request& req, httplib::Response& res) {
        std::string actor_fp;
        if (!update_require_admin_actor_local(deps, req, res, &actor_fp)) return;
        if (!deps.require_same_origin(req, res)) return;

        std::string plan_id;

        try {
            const json body = json::parse(req.body.empty() ? "{}" : req.body);
            plan_id = body.value("plan_id", "");
        } catch (...) {
            deps.reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_json"}
            }.dump(2));
            return;
        }

        if (plan_id.empty()) {
            deps.reply_json(res, 400, json{
                {"ok", false},
                {"error", "missing_plan_id"}
            }.dump(2));
            return;
        }

        if (plan_id.size() > 240 ||
            plan_id.find('/') != std::string::npos ||
            plan_id.find('\\') != std::string::npos ||
            plan_id.find("..") != std::string::npos) {
            deps.reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_plan_id"}
            }.dump(2));
            return;
        }

        for (char c : plan_id) {
            const bool ok =
                (c >= 'a' && c <= 'z') ||
                (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') ||
                c == '.' || c == '_' || c == '-';

            if (!ok) {
                deps.reply_json(res, 400, json{
                    {"ok", false},
                    {"error", "bad_plan_id_chars"}
                }.dump(2));
                return;
            }
        }

        const std::string helper_enabled =
            deps.getenv_str ? deps.getenv_str("PQNAS_UPDATE_HELPER_ENABLED") : "";

        if (!(helper_enabled == "1" ||
              helper_enabled == "true" ||
              helper_enabled == "TRUE" ||
              helper_enabled == "yes" ||
              helper_enabled == "YES")) {
            deps.reply_json(res, 400, json{
                {"ok", false},
                {"error", "update_helper_not_enabled"},
                {"message", "Dry-run requires PQNAS_UPDATE_HELPER_ENABLED=1."},
                {"helper_enabled", false},
                {"install_performed", false}
            }.dump(2));
            return;
        }

        std::string helper_path =
            deps.getenv_str ? deps.getenv_str("PQNAS_UPDATE_HELPER_PATH") : "";

        if (helper_path.empty()) {
            helper_path = "/usr/local/libexec/pqnas/pqnas_update_apply.py";
        }

        const std::string cmd =
            "timeout 60 " +
            update_shell_quote(helper_path) +
            " --plan-id " +
            update_shell_quote(plan_id) +
            " --dry-run 2>&1";

        int helper_status = -1;
        const std::string helper_output =
            update_run_command_limited(cmd, 4u * 1024u * 1024u, &helper_status);

        json helper_json;
        bool parsed_json = false;

        try {
            helper_json = json::parse(helper_output);
            parsed_json = true;
        } catch (...) {
            parsed_json = false;
        }

        if (!parsed_json) {
            deps.reply_json(res, 500, json{
                {"ok", false},
                {"error", "helper_bad_output"},
                {"helper_status", helper_status},
                {"output", helper_output.substr(0, 12000)},
                {"install_performed", false}
            }.dump(2));
            return;
        }

        int helper_exit_code = helper_status;
        if (helper_status >= 0) {
            if (WIFEXITED(helper_status)) {
                helper_exit_code = WEXITSTATUS(helper_status);
            } else if (helper_status % 256 == 0) {
                helper_exit_code = helper_status / 256;
            }
        }

        helper_json["helper_enabled"] = true;
        helper_json["helper_status"] = helper_status;
        helper_json["helper_exit_code"] = helper_exit_code;
        helper_json["dry_run"] = true;
        helper_json["install_performed"] = false;

        const bool ok = helper_json.value("ok", false);
        update_audit_emit_local(
            deps,
            ok ? "update_center.dry_run_ok" : "update_center.dry_run_fail",
            ok ? "ok" : "fail",
            actor_fp,
            helper_json
        );
        update_activity_record_local(
            deps,
            req,
            actor_fp,
            ok ? "update.dry_run" : "update.dry_run_failed",
            ok ? "Update dry-run completed" : "Update dry-run failed",
            helper_json
        );
        deps.reply_json(res, ok ? 200 : 400, helper_json.dump(2));
    });

    srv.Post("/api/v4/admin/updates/apply", [deps](const httplib::Request& req, httplib::Response& res) {
        std::string actor_fp;
        if (!update_require_admin_actor_local(deps, req, res, &actor_fp)) return;
        if (!deps.require_same_origin(req, res)) return;

        std::string plan_id;

        try {
            const json body = json::parse(req.body.empty() ? "{}" : req.body);
            plan_id = body.value("plan_id", "");
        } catch (...) {
            deps.reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_json"}
            }.dump(2));
            return;
        }

        if (plan_id.empty()) {
            deps.reply_json(res, 400, json{
                {"ok", false},
                {"error", "missing_plan_id"}
            }.dump(2));
            return;
        }

        if (plan_id.size() > 240 ||
            plan_id.find('/') != std::string::npos ||
            plan_id.find('\\') != std::string::npos ||
            plan_id.find("..") != std::string::npos) {
            deps.reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_plan_id"}
            }.dump(2));
            return;
        }

        for (char c : plan_id) {
            const bool ok =
                (c >= 'a' && c <= 'z') ||
                (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') ||
                c == '.' || c == '_' || c == '-';

            if (!ok) {
                deps.reply_json(res, 400, json{
                    {"ok", false},
                    {"error", "bad_plan_id_chars"}
                }.dump(2));
                return;
            }
        }

        const std::string helper_enabled =
            deps.getenv_str ? deps.getenv_str("PQNAS_UPDATE_HELPER_ENABLED") : "";

        if (!(helper_enabled == "1" ||
              helper_enabled == "true" ||
              helper_enabled == "TRUE" ||
              helper_enabled == "yes" ||
              helper_enabled == "YES")) {
            deps.reply_json(res, 400, json{
                {"ok", false},
                {"error", "update_helper_not_enabled"},
                {"message", "Apply requires PQNAS_UPDATE_HELPER_ENABLED=1."},
                {"helper_enabled", false},
                {"install_performed", false}
            }.dump(2));
            return;
        }

        // Security: root apply must use a fixed sudoers-controlled helper path
        // and argv execution. Do not allow environment-selected root helper paths.
        const UpdateArgvResult helper_result = update_run_argv_limited(
            {
                "/usr/bin/sudo",
                "-n",
                "/usr/local/sbin/pqnas-update-apply",
                "--plan-id",
                plan_id
            },
            8u * 1024u * 1024u,
            120000
        );

        const int helper_status = helper_result.exit_code;
        const std::string helper_output = helper_result.output;

        json helper_json;
        bool parsed_json = false;

        try {
            helper_json = json::parse(helper_output);
            parsed_json = true;
        } catch (...) {
            parsed_json = false;
        }

        if (!parsed_json) {
            deps.reply_json(res, 500, json{
                {"ok", false},
                {"error", "helper_bad_output"},
                {"helper_status", helper_status},
                {"output", helper_output.substr(0, 12000)},
                {"install_performed", false}
            }.dump(2));
            return;
        }

        int helper_exit_code = helper_status;
        if (helper_status >= 0) {
            if (WIFEXITED(helper_status)) {
                helper_exit_code = WEXITSTATUS(helper_status);
            } else if (helper_status % 256 == 0) {
                helper_exit_code = helper_status / 256;
            }
        }

        helper_json["helper_enabled"] = true;
        helper_json["apply_allowed"] = true;
        helper_json["helper_status"] = helper_status;
        helper_json["helper_exit_code"] = helper_exit_code;

        const bool ok = helper_json.value("ok", false);
        update_audit_emit_local(
            deps,
            ok ? "update_center.apply_ok" : "update_center.apply_fail",
            ok ? "ok" : "fail",
            actor_fp,
            helper_json
        );
        update_activity_record_local(
            deps,
            req,
            actor_fp,
            ok ? "update.apply" : "update.apply_failed",
            ok ? "Update applied" : "Update apply failed",
            helper_json
        );
        deps.reply_json(res, ok ? 200 : 400, helper_json.dump(2));
    });





}

} // namespace pqnas::updates
