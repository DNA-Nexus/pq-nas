// Transitional split from main.cpp.
// This file is intentionally included inside main() route-registration scope.
// TODO: convert to routes_admin_storage_tiering.cpp/.h after storage tiering helpers have a clean context.

srv.Post("/api/v4/admin/storage/tiering/migrate_one", [&](const httplib::Request& req, httplib::Response& res) {
    std::string actor_fp;
    if (!require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, &actor_fp)) return;
        if (!require_same_origin_for_cookie_mutation(req, res)) return;

    json j;
    try {
        j = json::parse(req.body.empty() ? "{}" : req.body);
    } catch (...) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid json"}
        }.dump());
        return;
    }

    const std::string fp = trim_copy(j.value("fingerprint", ""));
    const std::string rel_path = trim_copy(j.value("path", ""));

    if (fp.empty() || rel_path.empty()) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "missing fingerprint or path"}
        }.dump());
        return;
    }

    std::string rel_norm;
    std::string nerr;
    if (!pqnas::normalize_user_rel_path_strict(rel_path, &rel_norm, &nerr)) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid path"},
            {"detail", nerr}
        }.dump());
        return;
    }

    std::string merr;
    if (!migrate_one_landing_file(users, fp, rel_norm, &merr)) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "migration_failed"},
            {"message", "tiering migration failed"},
            {"detail", pqnas::shorten(merr, 180)}
        }.dump());
        return;
    }

    pqnas::AuditEvent ev;
    ev.event = "admin.storage_tiering_migrate_one";
    ev.outcome = "ok";
    ev.f["actor_fp"] = actor_fp;
    ev.f["fingerprint"] = fp;
    ev.f["path"] = pqnas::shorten(rel_norm, 200);
    ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
    audit_append(ev);

    reply_json(res, 200, json{
        {"ok", true},
        {"fingerprint", fp},
        {"path", rel_norm}
    }.dump());
});

    // Body JSON: { "paths": ["rel/a.jpg", "rel/dir", ...], "max_bytes": 52428800, "base": "rel/dir" }
    // Response: application/zip (in-memory, bounded)
    //
    // Semantics:
    // - paths[] are user-root-relative (same as other v4 file endpoints).
    // - Selected files and folders are staged into a temporary export tree.
    // - For supported image files, matching gallery metadata is exported as standard XMP sidecars.
    // - If "base" is provided, exported zip entry paths are made relative to base.
    // - The original files are not modified; metadata sidecars are generated only inside the export zip.
