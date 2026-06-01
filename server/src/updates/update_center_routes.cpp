#include "updates/update_center_routes.h"

#include <nlohmann/json.hpp>
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

std::filesystem::path updates_root_dir(const UpdateCenterRoutesDeps& deps) {
    const std::string env = deps.getenv_str ? deps.getenv_str("PQNAS_UPDATES_ROOT") : "";
    if (!env.empty()) return std::filesystem::path(env);
    return std::filesystem::path("/var/lib/pqnas/updates");
}

std::filesystem::path update_incoming_dir(const UpdateCenterRoutesDeps& deps) {
    return updates_root_dir(deps) / "incoming";
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

}

} // namespace pqnas::updates
