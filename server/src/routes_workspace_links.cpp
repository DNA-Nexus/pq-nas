#include "routes_workspace_links.h"

#include "storage_resolver.h"
#include "workspace_access_shared.h"
#include "workspace_links.h"

#include <algorithm>
#include <map>
#include <string>
#include <system_error>

namespace pqnas {
namespace {

std::string trim_copy_wsl(std::string s) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

void reply_workspace_link_json(const WorkspaceFileRouteDeps& deps,
                               httplib::Response& res,
                               int status,
                               const json& body) {
    if (deps.reply_json) {
        deps.reply_json(res, status, body.dump());
        return;
    }

    res.status = status;
    res.set_header("Content-Type", "application/json; charset=utf-8");
    res.body = body.dump();
}

bool require_same_origin_workspace_link_mutation(const WorkspaceFileRouteDeps& deps,
                                                 const httplib::Request& req,
                                                 httplib::Response& res) {
    if (!deps.origin || deps.origin->empty()) {
        reply_workspace_link_json(deps, res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "origin not configured"}
        });
        return false;
    }

    auto it = req.headers.find("Origin");
    if (it != req.headers.end() && it->second != *deps.origin) {
        reply_workspace_link_json(deps, res, 403, json{
            {"ok", false},
            {"error", "forbidden"},
            {"message", "origin mismatch"}
        });
        return false;
    }

    return true;
}

json workspace_link_item_json(const WorkspaceLinkRec& rec) {
    return json{
        {"name", rec.name},
        {"type", "link"},
        {"link_id", rec.id},
        {"id", rec.id},
        {"parent_path", rec.parent_path},
        {"url", rec.url},
        {"detected_type", rec.detected_type},
        {"size_bytes", 0},
        {"mtime_unix", rec.updated_at_epoch},
        {"created_by_fp", rec.created_by_fp},
        {"created_at_epoch", rec.created_at_epoch},
        {"updated_at_epoch", rec.updated_at_epoch}
    };
}

bool parse_workspace_link_body(const WorkspaceFileRouteDeps& deps,
                               const httplib::Request& req,
                               httplib::Response& res,
                               json* out) {
    try {
        *out = req.body.empty() ? json::object() : json::parse(req.body);
    } catch (...) {
        reply_workspace_link_json(deps, res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid json"}
        });
        return false;
    }

    if (!out->is_object()) {
        reply_workspace_link_json(deps, res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "json object required"}
        });
        return false;
    }

    return true;
}

bool normalize_workspace_link_parent_path(const std::string& raw,
                                          std::string* out,
                                          std::string* err) {
    const std::string s = trim_copy_wsl(raw);
    if (s.empty() || s == ".") {
        if (out) *out = "";
        return true;
    }

    return pqnas::normalize_user_rel_path_strict(s, out, err);
}

bool require_workspace_link_actor(const WorkspaceFileRouteDeps& deps,
                                  const httplib::Request& req,
                                  httplib::Response& res,
                                  const std::string& workspace_id,
                                  const std::string& parent_path,
                                  bool require_write,
                                  std::string* actor_fp,
                                  std::string* actor_role,
                                  WorkspaceRec* workspace,
                                  WorkspaceMemberRec* member,
                                  std::filesystem::path* workspace_root) {
    if (!deps.users || !deps.workspaces ||
        !deps.require_user_auth_users_actor ||
        !deps.reply_json) {
        reply_workspace_link_json(deps, res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "workspace link route dependencies missing"}
        });
        return false;
    }

    std::string fp, role;
    if (!deps.require_user_auth_users_actor(
            req, res, deps.cookie_key, deps.users, &fp, &role)) {
        return false;
    }

    if (workspace_id.empty()) {
        reply_workspace_link_json(deps, res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "missing workspace_id"}
        });
        return false;
    }

    if (!deps.workspaces->load(deps.workspaces_path)) {
        reply_workspace_link_json(deps, res, 500, json{
            {"ok", false},
            {"error", "workspaces_reload_failed"},
            {"message", "failed to reload workspaces"}
        });
        return false;
    }

    auto wopt = deps.workspaces->get(workspace_id);
    if (!wopt.has_value()) {
        reply_workspace_link_json(deps, res, 404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "workspace not found"}
        });
        return false;
    }

    WorkspaceRec w = *wopt;

    if (w.status != "enabled") {
        reply_workspace_link_json(deps, res, 403, json{
            {"ok", false},
            {"error", "forbidden"},
            {"message", "workspace disabled"}
        });
        return false;
    }

    if (w.storage_state != "allocated") {
        reply_workspace_link_json(deps, res, 403, json{
            {"ok", false},
            {"error", "storage_unallocated"},
            {"message", "Workspace storage not allocated"},
            {"workspace_id", workspace_id},
            {"quota_bytes", w.quota_bytes}
        });
        return false;
    }

    if (!w.storage_pool_id.empty()) {
        reply_workspace_link_json(deps, res, 400, json{
            {"ok", false},
            {"error", "pool_not_supported_yet"},
            {"message", "workspace links currently support default pool only"}
        });
        return false;
    }

    auto mopt = workspace_enabled_member_for_actor(w, fp);
    if (!mopt.has_value()) {
        reply_workspace_link_json(deps, res, 403, json{
            {"ok", false},
            {"error", "forbidden"},
            {"message", "workspace access denied"}
        });
        return false;
    }

    if (require_write && !workspace_member_can_write(*mopt)) {
        reply_workspace_link_json(deps, res, 403, json{
            {"ok", false},
            {"error", "forbidden"},
            {"message", "workspace write access required"}
        });
        return false;
    }

    const std::filesystem::path root_abs =
        workspace_default_root_from_users_path(deps.users_path, w);

    if (parent_path.empty()) {
        std::error_code ec;
        const auto st = std::filesystem::symlink_status(root_abs, ec);
        if (ec ||
            !std::filesystem::exists(st) ||
            std::filesystem::is_symlink(st) ||
            !std::filesystem::is_directory(st)) {
            reply_workspace_link_json(deps, res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "workspace root directory not found"}
            });
            return false;
        }
    } else {
        WorkspaceResolvedTarget target;
        std::string terr;
        if (!resolve_workspace_member_target_default_pool_only(
                deps.users_path, w, fp, require_write, parent_path, &target, &terr)) {
            reply_workspace_link_json(deps, res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", terr.empty() ? "invalid workspace path" : terr}
            });
            return false;
        }

        if (!target.is_dir) {
            reply_workspace_link_json(deps, res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "link parent path must be a directory"}
            });
            return false;
        }
    }

    if (actor_fp) *actor_fp = fp;
    if (actor_role) *actor_role = role;
    if (workspace) *workspace = std::move(w);
    if (member) *member = *mopt;
    if (workspace_root) *workspace_root = root_abs;

    return true;
}

void audit_workspace_link(const WorkspaceFileRouteDeps& deps,
                          const std::string& event,
                          const std::string& outcome,
                          const std::string& actor_fp,
                          const std::string& workspace_id,
                          const std::string& link_id,
                          const std::string& reason = "") {
    if (!deps.audit_emit) return;

    std::map<std::string, std::string> f;
    f["actor_fp"] = actor_fp;
    f["workspace_id"] = workspace_id;
    if (!link_id.empty()) f["link_id"] = link_id;
    if (!reason.empty()) f["reason"] = reason;

    deps.audit_emit(event, outcome, f);
}

} // namespace

void register_workspace_link_routes(httplib::Server& srv,
                                    const WorkspaceFileRouteDeps& deps) {
    srv.Get("/api/v4/workspaces/files/links/list",
            [&](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Cache-Control", "no-store");

        const std::string workspace_id =
            req.has_param("workspace_id") ? trim_copy_wsl(req.get_param_value("workspace_id")) : "";
        const std::string raw_path =
            req.has_param("path") ? req.get_param_value("path") : "";

        std::string parent_path;
        std::string perr;
        if (!normalize_workspace_link_parent_path(raw_path, &parent_path, &perr)) {
            reply_workspace_link_json(deps, res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "invalid path"}
            });
            return;
        }

        std::string actor_fp, actor_role;
        WorkspaceRec workspace;
        WorkspaceMemberRec member;
        std::filesystem::path workspace_root;

        if (!require_workspace_link_actor(
                deps, req, res, workspace_id, parent_path, false,
                &actor_fp, &actor_role, &workspace, &member, &workspace_root)) {
            return;
        }

        std::vector<WorkspaceLinkRec> all_links;
        std::string lerr;
        if (!workspace_links_load_all(workspace_root, &all_links, &lerr)) {
            audit_workspace_link(deps, "workspace.links_list_fail", "fail",
                                 actor_fp, workspace_id, "", lerr);
            reply_workspace_link_json(deps, res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to load workspace links"},
                {"detail", lerr}
            });
            return;
        }

        json items = json::array();
        for (const auto& rec : all_links) {
            if (rec.parent_path == parent_path) {
                items.push_back(workspace_link_item_json(rec));
            }
        }

        audit_workspace_link(deps, "workspace.links_list_ok", "ok",
                             actor_fp, workspace_id, "");

        reply_workspace_link_json(deps, res, 200, json{
            {"ok", true},
            {"workspace_id", workspace_id},
            {"path", parent_path},
            {"role", member.role},
            {"read_only", !workspace_member_can_write(member)},
            {"links", items}
        });
    });

    srv.Post("/api/v4/workspaces/files/links/create",
             [&](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Cache-Control", "no-store");

        if (!require_same_origin_workspace_link_mutation(deps, req, res)) return;

        json body;
        if (!parse_workspace_link_body(deps, req, res, &body)) return;

        const std::string workspace_id = trim_copy_wsl(body.value("workspace_id", ""));
        const std::string raw_parent =
            body.contains("parent_path") ? body.value("parent_path", "") : body.value("path", "");
        const std::string name = trim_copy_wsl(body.value("name", ""));
        const std::string url = trim_copy_wsl(body.value("url", ""));

        std::string parent_path;
        std::string perr;
        if (!normalize_workspace_link_parent_path(raw_parent, &parent_path, &perr)) {
            reply_workspace_link_json(deps, res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "invalid path"}
            });
            return;
        }

        if (name.empty() || name.size() > 160) {
            reply_workspace_link_json(deps, res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", name.empty() ? "link name is required" : "link name is too long"}
            });
            return;
        }

        std::string url_err;
        if (!workspace_link_validate_url(url, &url_err)) {
            reply_workspace_link_json(deps, res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", url_err}
            });
            return;
        }

        std::string actor_fp, actor_role;
        WorkspaceRec workspace;
        WorkspaceMemberRec member;
        std::filesystem::path workspace_root;

        if (!require_workspace_link_actor(
                deps, req, res, workspace_id, parent_path, true,
                &actor_fp, &actor_role, &workspace, &member, &workspace_root)) {
            return;
        }

        const std::int64_t now =
            deps.now_epoch_sec ? deps.now_epoch_sec() : static_cast<std::int64_t>(0);

        WorkspaceLinkRec rec;
        rec.id = workspace_link_new_id();
        rec.parent_path = parent_path;
        rec.name = name;
        rec.url = url;
        rec.detected_type = workspace_link_detect_type(url);
        rec.created_by_fp = actor_fp;
        rec.created_at_epoch = now;
        rec.updated_at_epoch = now;

        std::string serr;
        if (!workspace_links_save_one(workspace_root, rec, true, &serr)) {
            audit_workspace_link(deps, "workspace.links_create_fail", "fail",
                                 actor_fp, workspace_id, rec.id, serr);
            reply_workspace_link_json(deps, res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to save workspace link"},
                {"detail", serr}
            });
            return;
        }

        audit_workspace_link(deps, "workspace.links_create_ok", "ok",
                             actor_fp, workspace_id, rec.id);

        reply_workspace_link_json(deps, res, 200, json{
            {"ok", true},
            {"workspace_id", workspace_id},
            {"link", workspace_link_item_json(rec)}
        });
    });

    srv.Post("/api/v4/workspaces/files/links/update",
             [&](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Cache-Control", "no-store");

        if (!require_same_origin_workspace_link_mutation(deps, req, res)) return;

        json body;
        if (!parse_workspace_link_body(deps, req, res, &body)) return;

        const std::string workspace_id = trim_copy_wsl(body.value("workspace_id", ""));
        const std::string id = trim_copy_wsl(body.value("id", body.value("link_id", "")));

        if (!workspace_link_is_valid_id(id)) {
            reply_workspace_link_json(deps, res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "invalid link id"}
            });
            return;
        }

        std::string actor_fp, actor_role;
        WorkspaceRec workspace;
        WorkspaceMemberRec member;
        std::filesystem::path workspace_root;

        if (!require_workspace_link_actor(
                deps, req, res, workspace_id, "", true,
                &actor_fp, &actor_role, &workspace, &member, &workspace_root)) {
            return;
        }

        WorkspaceLinkRec rec;
        std::string lerr;
        if (!workspace_links_load_one(workspace_root, id, &rec, &lerr)) {
            reply_workspace_link_json(deps, res, 404, json{
                {"ok", false},
                {"error", "not_found"},
                {"message", "link not found"}
            });
            return;
        }

        if (body.contains("parent_path") || body.contains("path")) {
            const std::string raw_parent =
                body.contains("parent_path") ? body.value("parent_path", "") : body.value("path", "");

            std::string parent_path;
            std::string perr;
            if (!normalize_workspace_link_parent_path(raw_parent, &parent_path, &perr)) {
                reply_workspace_link_json(deps, res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "invalid path"}
                });
                return;
            }

            if (parent_path.empty()) {
                std::error_code ec;
                const auto root_st = std::filesystem::symlink_status(workspace_root, ec);
                if (ec ||
                    !std::filesystem::exists(root_st) ||
                    std::filesystem::is_symlink(root_st) ||
                    !std::filesystem::is_directory(root_st)) {
                    reply_workspace_link_json(deps, res, 400, json{
                        {"ok", false},
                        {"error", "bad_request"},
                        {"message", "workspace root directory not found"}
                    });
                    return;
                }
            } else {
                WorkspaceResolvedTarget target;
                std::string terr;
                if (!resolve_workspace_member_target_default_pool_only(
                        deps.users_path, workspace, actor_fp, true, parent_path, &target, &terr) ||
                    !target.is_dir) {
                    reply_workspace_link_json(deps, res, 400, json{
                        {"ok", false},
                        {"error", "bad_request"},
                        {"message", terr.empty() ? "link parent path must be a directory" : terr}
                    });
                    return;
                }
            }

            rec.parent_path = parent_path;
        }

        if (body.contains("name")) {
            const std::string name = trim_copy_wsl(body.value("name", ""));
            if (name.empty() || name.size() > 160) {
                reply_workspace_link_json(deps, res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", name.empty() ? "link name is required" : "link name is too long"}
                });
                return;
            }
            rec.name = name;
        }

        if (body.contains("url")) {
            const std::string url = trim_copy_wsl(body.value("url", ""));
            std::string url_err;
            if (!workspace_link_validate_url(url, &url_err)) {
                reply_workspace_link_json(deps, res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", url_err}
                });
                return;
            }

            rec.url = url;
            rec.detected_type = workspace_link_detect_type(url);
        }

        rec.updated_at_epoch =
            deps.now_epoch_sec ? deps.now_epoch_sec() : rec.updated_at_epoch;

        std::string serr;
        if (!workspace_links_save_one(workspace_root, rec, false, &serr)) {
            audit_workspace_link(deps, "workspace.links_update_fail", "fail",
                                 actor_fp, workspace_id, rec.id, serr);
            reply_workspace_link_json(deps, res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to update workspace link"},
                {"detail", serr}
            });
            return;
        }

        audit_workspace_link(deps, "workspace.links_update_ok", "ok",
                             actor_fp, workspace_id, rec.id);

        reply_workspace_link_json(deps, res, 200, json{
            {"ok", true},
            {"workspace_id", workspace_id},
            {"link", workspace_link_item_json(rec)}
        });
    });

    srv.Post("/api/v4/workspaces/files/links/delete",
             [&](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Cache-Control", "no-store");

        if (!require_same_origin_workspace_link_mutation(deps, req, res)) return;

        json body;
        if (!parse_workspace_link_body(deps, req, res, &body)) return;

        const std::string workspace_id = trim_copy_wsl(body.value("workspace_id", ""));
        const std::string id = trim_copy_wsl(body.value("id", body.value("link_id", "")));

        if (!workspace_link_is_valid_id(id)) {
            reply_workspace_link_json(deps, res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "invalid link id"}
            });
            return;
        }

        std::string actor_fp, actor_role;
        WorkspaceRec workspace;
        WorkspaceMemberRec member;
        std::filesystem::path workspace_root;

        if (!require_workspace_link_actor(
                deps, req, res, workspace_id, "", true,
                &actor_fp, &actor_role, &workspace, &member, &workspace_root)) {
            return;
        }

        std::string derr;
        if (!workspace_links_delete_one(workspace_root, id, &derr)) {
            audit_workspace_link(deps, "workspace.links_delete_fail", "fail",
                                 actor_fp, workspace_id, id, derr);
            reply_workspace_link_json(deps, res, 404, json{
                {"ok", false},
                {"error", "not_found"},
                {"message", "link not found"}
            });
            return;
        }

        audit_workspace_link(deps, "workspace.links_delete_ok", "ok",
                             actor_fp, workspace_id, id);

        reply_workspace_link_json(deps, res, 200, json{
            {"ok", true},
            {"workspace_id", workspace_id},
            {"link_id", id}
        });
    });
}

} // namespace pqnas
