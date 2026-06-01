#include "updates/update_center_routes.h"
#include "version.h"

#include <nlohmann/json.hpp>
#include <sys/wait.h>
#include <vector>
#include <iterator>
#include <cctype>
#include <sstream>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <ios>
#include <fstream>

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

std::filesystem::path updates_root_dir(const UpdateCenterRoutesDeps& deps) {
    const std::string env = deps.getenv_str ? deps.getenv_str("PQNAS_UPDATES_ROOT") : "";
    if (!env.empty()) return std::filesystem::path(env);
    return std::filesystem::path("/var/lib/pqnas/updates");
}

std::filesystem::path update_incoming_dir(const UpdateCenterRoutesDeps& deps) {
    return updates_root_dir(deps) / "incoming";
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

} // namespace



// build_update_plan_json_r6b
[[maybe_unused]] json build_update_plan_json(const UpdateCenterRoutesDeps& deps,
                                             const std::string& stored_name,
                                             const std::string& listing,
                                             const std::string& package_bytes) {
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

    const std::string package_server_version =
        update_extract_server_version_from_stored_name(stored_name);
    const std::string current_server_version =
        update_current_server_version(deps);

    const bool server_package_version_known =
        !package_server_version.empty() && !current_server_version.empty();
    const bool server_package_is_newer =
        server_package_version_known &&
        update_compare_versions(package_server_version, current_server_version) > 0;

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
            const std::string package_version =
                update_extract_app_package_version(entry, app_id);

            if (!update_installed_app_exists(deps, app_id)) {
                add_action(
                    "bundled_app_package",
                    "skip_not_installed",
                    raw,
                    "",
                    "bundled app package is present in release but this app is not installed on this server",
                    app_id
                );
                ++skipped;
                bump_skip("bundled_app_package_not_installed");
                continue;
            }

            const std::string installed_version =
                update_latest_installed_app_version(deps, app_id);

            if (package_version.empty()) {
                add_action(
                    "bundled_app_package",
                    "skip_version_unknown",
                    raw,
                    (std::filesystem::path(deps.apps_installed_dir) / app_id).string(),
                    "package app version could not be determined; refusing app update",
                    app_id
                );
                ++skipped;
                bump_skip("bundled_app_package_version_unknown");
                continue;
            }

            if (installed_version.empty()) {
                add_action(
                    "bundled_app_package",
                    "skip_installed_version_unknown",
                    raw,
                    (std::filesystem::path(deps.apps_installed_dir) / app_id).string(),
                    "installed app version could not be determined; refusing app update to prevent downgrade",
                    app_id
                );
                ++skipped;
                bump_skip("bundled_app_package_installed_version_unknown");
                continue;
            }

            const int app_cmp =
                update_compare_versions(package_version, installed_version);

            if (app_cmp <= 0) {
                add_action(
                    "bundled_app_package",
                    "skip_version_not_newer",
                    raw,
                    (std::filesystem::path(deps.apps_installed_dir) / app_id).string(),
                    "package app version " + package_version +
                        " is not newer than installed version " + installed_version,
                    app_id
                );
                ++skipped;
                bump_skip("bundled_app_package_version_not_newer");
                continue;
            }

            add_action(
                "bundled_app_package",
                "update_existing_app_package",
                raw,
                (std::filesystem::path(deps.apps_installed_dir) / app_id).string(),
                "bundled app update from " + installed_version + " to " + package_version,
                app_id
            );
            ++planned_updates;
            continue;
        }

        if (update_starts_with(entry_low, "runtime/") ||
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

    return json{
        {"ok", true},
        {"status", "plan_built"},
        {"stored_name", stored_name},
        {"package_sha256", package_sha256},
        {"package_size", package_size_bytes},
        {"package_server_version", package_server_version},
        {"current_server_version", current_server_version},
        {"server_package_is_newer", server_package_is_newer},
        {"entry_count", entry_count},
        {"planned_updates", planned_updates},
        {"skipped", skipped},
        {"has_core_binary_action", has_core_binary_action},
        {"skipped_summary", skipped_summary},
        {"actions", actions}
    };
}

void register_update_center_routes(httplib::Server& srv, const UpdateCenterRoutesDeps& deps) {
    srv.Get("/admin/updates", [deps](const httplib::Request& req, httplib::Response& res) {
        if (!deps.require_admin(req, res)) return;

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
        if (!deps.require_admin(req, res)) return;

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

    srv.Post("/api/v4/admin/updates/upload", [deps](const httplib::Request& req, httplib::Response& res) {
        if (!deps.require_admin(req, res)) return;
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

        deps.reply_json(res, 200, meta.dump(2));
    });

    srv.Post("/api/v4/admin/updates/verify", [deps](const httplib::Request& req, httplib::Response& res) {
        if (!deps.require_admin(req, res)) return;
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

        deps.reply_json(res, safe ? 200 : 400, json{
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
        }.dump(2));
    });

    srv.Post("/api/v4/admin/updates/plan", [deps](const httplib::Request& req, httplib::Response& res) {
        if (!deps.require_admin(req, res)) return;
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

        json plan = build_update_plan_json(deps, stored_name, listing, package_bytes);
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

        deps.reply_json(res, 200, plan.dump(2));
    });

    srv.Post("/api/v4/admin/updates/install", [deps](const httplib::Request& req, httplib::Response& res) {
        if (!deps.require_admin(req, res)) return;
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



}

} // namespace pqnas::updates
