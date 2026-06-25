#include "routes_admin_api_explorer.h"

#include "httplib.h"

#include <nlohmann/json.hpp>

#include <string>

using json = nlohmann::json;

namespace {

bool context_ok(const AdminApiExplorerRoutesContext& c) {
    return !c.html_path.empty() &&
           !c.js_path.empty() &&
           c.require_admin &&
           c.slurp_file &&
           c.reply_json;
}

void reply_context_error(const AdminApiExplorerRoutesContext& c, httplib::Response& res) {
    if (c.reply_json) {
        c.reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "admin api explorer route context incomplete"}
        }.dump());
        return;
    }

    res.status = 500;
    res.set_header("Cache-Control", "no-store");
    res.set_content(
        "{\"ok\":false,\"error\":\"server_error\",\"message\":\"admin api explorer route context incomplete\"}",
        "application/json; charset=utf-8"
    );
}

json param(const std::string& name,
           const std::string& in,
           const std::string& required,
           const std::string& desc) {
    return json{
        {"name", name},
        {"in", in},
        {"required", required},
        {"description", desc}
    };
}

json route(const std::string& id,
           const std::string& category,
           const std::string& risk,
           const std::string& method,
           const std::string& path,
           const std::string& title,
           const std::string& purpose,
           const std::string& auth,
           const std::string& source,
           json params,
           json body,
           json responses,
           json tags,
           const std::string& curl) {
    return json{
        {"id", id},
        {"category", category},
        {"risk", risk},
        {"method", method},
        {"path", path},
        {"title", title},
        {"purpose", purpose},
        {"auth", auth},
        {"source", source},
        {"params", std::move(params)},
        {"body", std::move(body)},
        {"responses", std::move(responses)},
        {"tags", std::move(tags)},
        {"curl", curl}
    };
}

json api_catalog_v1() {
    json routes = json::array();

    routes.push_back(route(
        "files.move",
        "Files",
        "mutating",
        "POST",
        "/api/v4/files/move",
        "Move or rename file/directory",
        "Move or rename a user-visible file or directory.",
        "User session. Requires same-origin cookie mutation protection and allocated user storage.",
        "server/src/routes/routes_files_core.inc",
        json::array({
            param("from", "query", "yes", "Source user-relative path."),
            param("to", "query", "yes", "Destination user-relative path.")
        }),
        json::object(),
        json::array({"200 ok", "400 bad_request", "403 storage_unallocated", "404 not_found", "409 dest_exists", "409 locked", "500 server_error"}),
        json::array({"file", "move", "rename", "mv"}),
        "curl -k -i -sS -X POST \"$BASE/api/v4/files/move?from=old.txt&to=new.txt\" -H \"Cookie: $COOKIE\" -H \"Origin: $BASE\""
    ));

    routes.push_back(route(
        "files.copy",
        "Files",
        "mutating",
        "POST",
        "/api/v4/files/copy",
        "Copy file",
        "Copy a user-visible file to a destination path.",
        "User session. Requires same-origin cookie mutation protection and allocated user storage.",
        "server/src/routes/routes_files_core.inc",
        json::array({
            param("from", "query", "yes", "Source user-relative file path."),
            param("to", "query", "yes", "Destination user-relative file path.")
        }),
        json::object(),
        json::array({"200 ok", "400 bad_request", "403 quota_exceeded", "404 not_found", "409 dest_exists", "500 server_error"}),
        json::array({"file", "copy", "duplicate"}),
        "curl -k -i -sS -X POST \"$BASE/api/v4/files/copy?from=old.txt&to=copy.txt\" -H \"Cookie: $COOKIE\" -H \"Origin: $BASE\""
    ));

    routes.push_back(route(
        "files.delete",
        "Files",
        "destructive",
        "POST",
        "/api/v4/files/delete",
        "Move file/directory to trash",
        "Move a user-visible file or directory to trash.",
        "User session. Requires same-origin cookie mutation protection and allocated user storage.",
        "server/src/routes/routes_files_core.inc",
        json::array({
            param("path", "query", "yes", "User-relative file or directory path.")
        }),
        json::object(),
        json::array({"200 ok", "400 bad_request", "403 storage_unallocated", "404 not_found", "409 locked", "500 server_error"}),
        json::array({"file", "delete", "trash"}),
        "curl -k -i -sS -X POST \"$BASE/api/v4/files/delete?path=test.txt\" -H \"Cookie: $COOKIE\" -H \"Origin: $BASE\""
    ));

    routes.push_back(route(
        "files.rmrf",
        "Files",
        "danger",
        "POST",
        "/api/v4/files/rmrf",
        "Permanently remove file/directory",
        "Directly remove a user-visible file or directory without trash.",
        "User session. Requires same-origin cookie mutation protection and allocated user storage.",
        "server/src/routes/routes_files_core.inc",
        json::array({
            param("path", "query", "yes", "User-relative file or directory path. Root-like paths are refused.")
        }),
        json::object(),
        json::array({"200 ok", "400 bad_request", "403 storage_unallocated", "404 not_found", "409 locked", "500 server_error"}),
        json::array({"file", "delete", "rmrf", "danger", "permanent"}),
        "curl -k -i -sS -X POST \"$BASE/api/v4/files/rmrf?path=test.txt\" -H \"Cookie: $COOKIE\" -H \"Origin: $BASE\""
    ));

    routes.push_back(route(
        "files.favorites.add",
        "Files",
        "mutating",
        "POST",
        "/api/v4/files/favorites/add",
        "Add favorite",
        "Add a file or directory to the authenticated user's favorites.",
        "User session. Requires same-origin cookie mutation protection and allocated user storage.",
        "server/src/routes/routes_files_core.inc",
        json::array(),
        json{
            {"content_type", "application/json"},
            {"example", json{{"path", "docs/readme.txt"}, {"type", "file"}}}
        },
        json::array({"200 ok", "400 bad_request", "403 storage_unallocated", "404 not_found", "409 type_mismatch", "500 server_error"}),
        json::array({"favorites", "star", "file"}),
        "curl -k -i -sS -X POST \"$BASE/api/v4/files/favorites/add\" -H \"Cookie: $COOKIE\" -H \"Origin: $BASE\" -H \"Content-Type: application/json\" --data '{\"path\":\"docs/readme.txt\",\"type\":\"file\"}'"
    ));

    routes.push_back(route(
        "uploads.start",
        "Uploads",
        "upload",
        "POST",
        "/api/v4/uploads/start",
        "Start chunked upload",
        "Start a chunked upload session for a user-storage file.",
        "User session. Requires same-origin cookie mutation protection and allocated user storage.",
        "server/src/routes/routes_uploads_chunked.cpp",
        json::array(),
        json{
            {"content_type", "application/json"},
            {"example", json{{"path", "big/video.mp4"}, {"size_bytes", 104857600}, {"overwrite", false}}}
        },
        json::array({"200 ok", "400 bad_request", "403 quota_exceeded", "409 file_exists", "413 upload_too_large", "500 server_error"}),
        json::array({"upload", "chunked", "file"}),
        "curl -k -i -sS -X POST \"$BASE/api/v4/uploads/start\" -H \"Cookie: $COOKIE\" -H \"Origin: $BASE\" -H \"Content-Type: application/json\" --data '{\"path\":\"big/video.mp4\",\"size_bytes\":104857600,\"overwrite\":false}'"
    ));

    routes.push_back(route(
        "uploads.chunk",
        "Uploads",
        "upload",
        "PUT",
        "/api/v4/uploads/chunk",
        "Upload chunk",
        "Upload one raw chunk for an existing chunked upload session.",
        "User session. Requires same-origin cookie mutation protection.",
        "server/src/routes/routes_uploads_chunked.cpp",
        json::array({
            param("upload_id", "query", "yes", "Upload session id from /api/v4/uploads/start."),
            param("index", "query", "yes", "Zero-based chunk index.")
        }),
        json{{"content_type", "application/octet-stream"}},
        json::array({"200 ok", "400 bad_request", "403 quota_exceeded", "404 not_found", "411 length_required", "500 server_error"}),
        json::array({"upload", "chunked", "file"}),
        "curl -k -i -sS -X PUT \"$BASE/api/v4/uploads/chunk?upload_id=$UPLOAD_ID&index=0\" -H \"Cookie: $COOKIE\" -H \"Origin: $BASE\" --data-binary '@chunk-000.part'"
    ));

    routes.push_back(route(
        "uploads.finish",
        "Uploads",
        "upload",
        "POST",
        "/api/v4/uploads/finish",
        "Finish chunked upload",
        "Assemble uploaded chunks and commit the completed file into user storage.",
        "User session. Requires same-origin cookie mutation protection and allocated user storage.",
        "server/src/routes/routes_uploads_chunked.cpp",
        json::array(),
        json{
            {"content_type", "application/json"},
            {"example", json{{"upload_id", "$UPLOAD_ID"}}}
        },
        json::array({"200 ok", "400 bad_request", "403 quota_exceeded", "404 not_found", "409 file_exists", "500 server_error"}),
        json::array({"upload", "chunked", "finish"}),
        "curl -k -i -sS -X POST \"$BASE/api/v4/uploads/finish\" -H \"Cookie: $COOKIE\" -H \"Origin: $BASE\" -H \"Content-Type: application/json\" --data '{\"upload_id\":\"'$UPLOAD_ID'\"}'"
    ));

    routes.push_back(route(
        "versions.delete",
        "File Versions",
        "destructive",
        "POST",
        "/api/v4/files/versions/delete",
        "Delete stored file version",
        "Delete one stored file version for a user-visible path.",
        "User session. Requires same-origin cookie mutation protection and allocated user storage.",
        "server/src/routes/routes_file_versions_manage.cpp",
        json::array(),
        json{
            {"content_type", "application/json"},
            {"example", json{{"path", "docs/readme.txt"}, {"version_id", "v-example"}}}
        },
        json::array({"200 ok", "400 bad_request", "403 storage_unallocated", "404 not_found", "500 server_error"}),
        json::array({"version", "delete", "history"}),
        "curl -k -i -sS -X POST \"$BASE/api/v4/files/versions/delete\" -H \"Cookie: $COOKIE\" -H \"Origin: $BASE\" -H \"Content-Type: application/json\" --data '{\"path\":\"docs/readme.txt\",\"version_id\":\"v-example\"}'"
    ));

    routes.push_back(route(
        "versions.restore",
        "File Versions",
        "mutating",
        "POST",
        "/api/v4/files/restore_version",
        "Restore file version",
        "Restore a stored file version back to a live file path.",
        "User session. Requires same-origin cookie mutation protection and allocated user storage.",
        "server/src/routes/routes_file_versions_restore.cpp",
        json::array(),
        json{
            {"content_type", "application/json"},
            {"example", json{{"path", "docs/readme.txt"}, {"version_id", "v-example"}}}
        },
        json::array({"200 ok", "400 bad_request", "403 storage_unallocated", "404 not_found", "409 path_conflict", "500 server_error"}),
        json::array({"version", "restore", "history"}),
        "curl -k -i -sS -X POST \"$BASE/api/v4/files/restore_version\" -H \"Cookie: $COOKIE\" -H \"Origin: $BASE\" -H \"Content-Type: application/json\" --data '{\"path\":\"docs/readme.txt\",\"version_id\":\"v-example\"}'"
    ));

    return json{
        {"ok", true},
        {"version", 1},
        {"generated_by", "admin_api_explorer_catalog_v1"},
        {"routes", routes}
    };
}

} // namespace

void register_admin_api_explorer_routes(
    httplib::Server& srv,
    const AdminApiExplorerRoutesContext& ctx
) {
    const AdminApiExplorerRoutesContext c = ctx;

    srv.Get("/admin/api-explorer",
        [c](const httplib::Request& req, httplib::Response& res) {
            if (!context_ok(c)) {
                reply_context_error(c, res);
                return;
            }

            std::string actor_fp;
            if (!c.require_admin(req, res, &actor_fp)) return;

            const std::string body = c.slurp_file(c.html_path);
            if (body.empty()) {
                res.status = 404;
                res.set_header("Cache-Control", "no-store");
                res.set_content("missing admin_api_explorer.html", "text/plain; charset=utf-8");
                return;
            }

            res.set_header("Cache-Control", "no-store");
            res.set_content(body, "text/html; charset=utf-8");
        }
    );

    srv.Get("/static/admin_api_explorer.js",
        [c](const httplib::Request& req, httplib::Response& res) {
            if (!context_ok(c)) {
                reply_context_error(c, res);
                return;
            }

            std::string actor_fp;
            if (!c.require_admin(req, res, &actor_fp)) return;

            const std::string body = c.slurp_file(c.js_path);
            if (body.empty()) {
                res.status = 404;
                res.set_header("Cache-Control", "no-store");
                res.set_content("missing admin_api_explorer.js", "text/plain; charset=utf-8");
                return;
            }

            res.set_header("Cache-Control", "no-store");
            res.set_content(body, "application/javascript; charset=utf-8");
        }
    );

    srv.Get("/api/v4/admin/api-explorer/routes",
        [c](const httplib::Request& req, httplib::Response& res) {
            if (!context_ok(c)) {
                reply_context_error(c, res);
                return;
            }

            std::string actor_fp;
            if (!c.require_admin(req, res, &actor_fp)) return;

            res.set_header("Cache-Control", "no-store");
            c.reply_json(res, 200, api_catalog_v1().dump());
        }
    );
}
