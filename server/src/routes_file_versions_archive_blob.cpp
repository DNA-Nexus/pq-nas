#include "routes_file_versions_archive_blob.h"

#include "httplib.h"
#include "archive_zip_manifest.h"
#include "file_versions_read.h"
#include "storage_resolver.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

using json = nlohmann::json;

namespace {

bool context_ok(const FileVersionArchiveBlobRoutesContext& c) {
    return c.file_versions && c.require_user_auth && c.user_dir_for_fp;
}

void json_error(httplib::Response& res, int status, const json& body) {
    res.status = status;
    res.set_content(body.dump(), "application/json");
}

bool ends_with_ascii_ci(const std::string& value, const std::string& suffix) {
    if (value.size() < suffix.size()) return false;

    return std::equal(
        suffix.rbegin(),
        suffix.rend(),
        value.rbegin(),
        [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) ==
                   std::tolower(static_cast<unsigned char>(b));
        }
    );
}

std::string mime_for_rel_path(const std::string& rel_norm) {
    if (ends_with_ascii_ci(rel_norm, ".png")) return "image/png";
    if (ends_with_ascii_ci(rel_norm, ".jpg") || ends_with_ascii_ci(rel_norm, ".jpeg")) return "image/jpeg";
    if (ends_with_ascii_ci(rel_norm, ".webp")) return "image/webp";
    if (ends_with_ascii_ci(rel_norm, ".gif")) return "image/gif";
    if (ends_with_ascii_ci(rel_norm, ".bmp")) return "image/bmp";
    if (ends_with_ascii_ci(rel_norm, ".svg")) return "image/svg+xml";
    return "application/octet-stream";
}

} // namespace

void register_file_version_archive_blob_routes(
    httplib::Server& srv,
    const FileVersionArchiveBlobRoutesContext& ctx
) {
    const FileVersionArchiveBlobRoutesContext c = ctx;

    srv.Get("/api/v4/files/archive_manifest",
        [c](const httplib::Request& req, httplib::Response& res) {
            if (!context_ok(c)) {
                json_error(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "file version archive/blob route context incomplete"}});
                return;
            }

            std::string fp_hex;
            std::string role;
            if (!c.require_user_auth(req, res, &fp_hex, &role)) return;
            (void)role;

            const std::string rel_raw = req.get_param_value("path");
            std::string rel_norm;
            std::string norm_err;
            if (!pqnas::normalize_user_rel_path_strict(rel_raw, &rel_norm, &norm_err)) {
                json_error(res, 400, json{{"ok", false}, {"error", norm_err.empty() ? "invalid_path" : norm_err}});
                return;
            }

            const auto user_root = c.user_dir_for_fp(fp_hex).lexically_normal();
            const auto abs_path = (user_root / rel_norm).lexically_normal();

            std::error_code ec;
            const auto canon_root = std::filesystem::weakly_canonical(user_root, ec);
            if (ec) {
                json_error(res, 500, json{{"ok", false}, {"error", "user_root_not_found"}});
                return;
            }

            ec.clear();
            const auto canon_file = std::filesystem::weakly_canonical(abs_path, ec);
            if (ec || canon_file.string().rfind(canon_root.string(), 0) != 0) {
                json_error(res, 404, json{{"ok", false}, {"error", "not_found"}});
                return;
            }

            ec.clear();
            if (!std::filesystem::is_regular_file(canon_file, ec)) {
                json_error(res, 404, json{{"ok", false}, {"error", "not_a_file"}});
                return;
            }

            auto manifest = pqnas::read_archive_manifest_from_file(canon_file);

            json out;
            out["ok"] = manifest.ok;
            out["path"] = rel_norm;

            if (!manifest.ok) {
                out["error"] = manifest.error.empty() ? "failed_to_read_zip_manifest" : manifest.error;
                out["zip64"] = manifest.zip64;
                res.status = 422;
                res.set_content(out.dump(), "application/json");
                return;
            }

            out["entries"] = json::array();
            for (const auto& e : manifest.entries) {
                out["entries"].push_back(json{
                    {"path", e.path},
                    {"size", e.size},
                    {"compressed_size", e.compressed_size},
                    {"crc32", pqnas::zip_crc32_hex(e.crc32)}
                });
            }

            res.set_header("Cache-Control", "no-store");
            res.set_content(out.dump(), "application/json");
        }
    );

    srv.Get("/api/v4/files/versions/archive_manifest",
        [c](const httplib::Request& req, httplib::Response& res) {
            if (!context_ok(c)) {
                json_error(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "file version archive/blob route context incomplete"}});
                return;
            }

            std::string fp_hex;
            std::string role;
            if (!c.require_user_auth(req, res, &fp_hex, &role)) return;
            (void)role;

            const std::string rel_raw = req.get_param_value("path");
            const std::string version_id = req.get_param_value("version_id");

            std::string rel_norm;
            std::string norm_err;
            if (!pqnas::normalize_user_rel_path_strict(rel_raw, &rel_norm, &norm_err)) {
                json_error(res, 400, json{{"ok", false}, {"error", norm_err.empty() ? "invalid_path" : norm_err}});
                return;
            }

            const auto scope_root = c.user_dir_for_fp(fp_hex).lexically_normal();

            auto rr = pqnas::resolve_version_blob_for_download(
                c.file_versions,
                "user",
                fp_hex,
                rel_norm,
                version_id,
                scope_root
            );

            if (!rr.ok) {
                json_error(res, 404, json{{"ok", false}, {"error", rr.error.empty() ? "version_blob_not_found" : rr.error}});
                return;
            }

            auto manifest = pqnas::read_archive_manifest_from_file(rr.blob_abs_path, rel_norm);

            json out;
            out["ok"] = manifest.ok;
            out["path"] = rel_norm;
            out["version_id"] = version_id;

            if (!manifest.ok) {
                out["error"] = manifest.error.empty() ? "failed_to_read_zip_manifest" : manifest.error;
                out["zip64"] = manifest.zip64;
                res.status = 422;
                res.set_content(out.dump(), "application/json");
                return;
            }

            out["entries"] = json::array();
            for (const auto& e : manifest.entries) {
                out["entries"].push_back(json{
                    {"path", e.path},
                    {"size", e.size},
                    {"compressed_size", e.compressed_size},
                    {"crc32", pqnas::zip_crc32_hex(e.crc32)}
                });
            }

            res.set_header("Cache-Control", "no-store");
            res.set_content(out.dump(), "application/json");
        }
    );

    srv.Get("/api/v4/files/versions/blob",
        [c](const httplib::Request& req, httplib::Response& res) {
            if (!context_ok(c)) {
                json_error(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "file version archive/blob route context incomplete"}});
                return;
            }

            std::string fp_hex;
            std::string role;
            if (!c.require_user_auth(req, res, &fp_hex, &role)) return;
            (void)role;

            const std::string rel_raw = req.get_param_value("path");
            const std::string version_id = req.get_param_value("version_id");
            const bool inline_view = req.has_param("inline") && req.get_param_value("inline") == "1";

            if (rel_raw.empty() || version_id.empty()) {
                res.status = 400;
                res.set_content(R"({"ok":false,"error":"bad_request","message":"missing path or version_id"})", "application/json");
                return;
            }

            std::string rel_norm;
            std::string norm_err;
            if (!pqnas::normalize_user_rel_path_strict(rel_raw, &rel_norm, &norm_err)) {
                res.status = 400;
                res.set_content(R"({"ok":false,"error":"bad_request","message":"invalid path"})", "application/json");
                return;
            }

            const std::filesystem::path scope_root = c.user_dir_for_fp(fp_hex).lexically_normal();

            auto rr = pqnas::resolve_version_blob_for_download(
                c.file_versions,
                "user",
                fp_hex,
                rel_norm,
                version_id,
                scope_root
            );

            if (!rr.ok) {
                const int code =
                    rr.error == "bad_request" ? 400 :
                    rr.error == "not_found" ? 404 :
                    rr.error == "unsupported" ? 415 :
                    500;

                res.status = code;
                json out = {
                    {"ok", false},
                    {"error", rr.error.empty() ? "server_error" : rr.error},
                    {"message", rr.message.empty() ? "failed to read version" : rr.message}
                };
                res.set_content(out.dump(), "application/json");
                return;
            }

            std::ifstream in(rr.blob_abs_path, std::ios::binary);
            if (!in.good()) {
                res.status = 404;
                res.set_content(R"({"ok":false,"error":"not_found","message":"version blob not found"})", "application/json");
                return;
            }

            std::string body(
                (std::istreambuf_iterator<char>(in)),
                std::istreambuf_iterator<char>()
            );

            const std::string mime = mime_for_rel_path(rel_norm);

            res.set_header("X-Content-Type-Options", "nosniff");
            res.set_header("Cache-Control", "no-store");
            res.set_header(
                "Content-Disposition",
                std::string(inline_view ? "inline" : "attachment") +
                "; filename=\"version-" + version_id + "\""
            );

            res.set_content(std::move(body), mime.c_str());
        }
    );
}
