#include "updates/update_center_routes.h"
#include "version.h"

#include <nlohmann/json.hpp>
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


}

} // namespace pqnas::updates
