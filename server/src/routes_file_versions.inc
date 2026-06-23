// Transitional restore_version split from main.cpp.
// This file is intentionally included inside main() route-registration scope.
// TODO: convert restore_version route to module after restore helpers have a clean context.

// Transitional remaining mutating/summary file-version split from main.cpp.
// This file is intentionally included inside main() route-registration scope.
// TODO: convert remaining file-version routes to modules after helpers have a clean context.

    srv.Post("/api/v4/files/restore_version", [&](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;
    (void)role;

    json body = json::parse(req.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid json"}
        }.dump());
        return;
    }

    const std::string path_rel = body.value("path", "");
    const std::string version_id = body.value("version_id", "");

    if (path_rel.empty() || version_id.empty()) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "missing path or version_id"}
        }.dump());
        return;
    }

    std::string rel_norm, nerr;
    if (!pqnas::normalize_user_rel_path_strict(path_rel, &rel_norm, &nerr)) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid path"}
        }.dump());
        return;
    }

    const std::filesystem::path user_dir = user_dir_for_fp(users, fp_hex);

    std::filesystem::path abs_path;
    std::string perr;
    if (!pqnas::resolve_user_path_strict(user_dir, rel_norm, &abs_path, &perr)) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid path"}
        }.dump());
        return;
    }
    std::string found_ancestor;
    std::string ancestor_err;
    if (pqnas::any_file_ancestor_exists(users, fp_hex, rel_norm, &found_ancestor, &ancestor_err)) {
        reply_json(res, 409, json{
            {"ok", false},
            {"error", "path_conflict"},
            {"message", "a parent path is an existing file"},
            {"ancestor", found_ancestor}
        }.dump());
        return;
    }

    auto uopt = users.get(fp_hex);
    if (!uopt.has_value() || uopt->storage_state != "allocated") {
        reply_json(res, 403, json{
            {"ok", false},
            {"error", "storage_unallocated"},
            {"message", "Storage not allocated"}
        }.dump());
        return;
    }

    auto* vix = &file_versions_index;

    std::error_code ec;
    auto live_st = std::filesystem::symlink_status(abs_path, ec);
    if (!ec && std::filesystem::exists(live_st) && !std::filesystem::is_symlink(live_st) && std::filesystem::is_regular_file(live_st)) {
        pqnas::PreserveCurrentVersionParams vp;
        vp.scope_type = "user";
        vp.scope_id = fp_hex;
        vp.scope_root = user_dir;
        vp.logical_rel_path = rel_norm;
        vp.live_abs_path = abs_path;
        vp.event_kind = "restore_preserve";
        vp.actor_fp = fp_hex;
        vp.users = &users;
        vp.file_versions = vix;

        std::string preserve_err;
        std::string ignored_version_id;
        if (!pqnas::preserve_current_file_version(vp, &ignored_version_id, &preserve_err)) {
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to preserve current file before restore"},
                {"detail", pqnas::shorten(preserve_err, 180)}
            }.dump());
            return;
        }
    } else {
        std::filesystem::create_directories(abs_path.parent_path(), ec);
        if (ec) {
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to create destination directory"},
                {"detail", ec.message()}
            }.dump());
            return;
        }
    }

    auto rr = pqnas::restore_version_blob_to_path(vix, "user", fp_hex, rel_norm, version_id, abs_path);
    if (!rr.ok) {
        reply_json(res,
                   rr.error == "not_found" ? 404 : 500,
                   json{
                       {"ok", false},
                       {"error", rr.error.empty() ? "server_error" : rr.error},
                       {"message", rr.message.empty() ? "restore failed" : rr.message},
                       {"detail", pqnas::shorten(rr.detail, 180)}
                   }.dump());
        return;
    }
    // Refresh cached live file facts after restore so gallery list + thumb URLs
    // reflect the restored version immediately.
    {
        const std::uint64_t restored_size =
            rr.bytes > 0 ? static_cast<std::uint64_t>(rr.bytes)
                         : pqnas::file_size_u64_safe(abs_path);

        const std::int64_t restored_mtime =
            rr.mtime_epoch > 0
                ? static_cast<std::int64_t>(rr.mtime_epoch)
                : static_cast<std::int64_t>(file_mtime_epoch_safe(abs_path));

        const std::int64_t now_ts = static_cast<std::int64_t>(std::time(nullptr));

        if (auto* gidx = pqnas::get_gallery_meta_index()) {
            std::string gerr;
            (void)gidx->touch_file_facts(
                "user",
                fp_hex,
                rel_norm,
                restored_size,
                restored_mtime,
                now_ts,
                &gerr
            );
        }

        // Also refresh file_locations if you have a single-path fact update helper.
        // Use the exact helper from file_location_index.h/.cpp that updates
        // size_bytes + mtime_epoch for one logical path.
        //
        // Example shape only:
        //
        // if (auto* flix = pqnas::get_file_location_index()) {
        //     std::string ferr;
        //     if (!flix->YOUR_SINGLE_PATH_FACT_UPDATE_METHOD(
        //             fp_hex, rel_norm, restored_size, restored_mtime, &ferr)) {
        //         audit_warn("file_location_restore_refresh_failed", ferr, rel_norm);
        //     }
        // }
    }
    reply_json(res, 200, json{
        {"ok", true},
        {"scope_type", "user"},
        {"scope_id", fp_hex},
        {"path", rel_norm},
        {"restored_version_id", version_id},
        {"bytes", rr.bytes},
        {"mtime_epoch", rr.mtime_epoch},
        {"sha256_hex", rr.sha256_hex}
    }.dump());
});
//
// ---- Snapshots API (admin-only, v1) ----
//

// Body: { "volume":"data", "id":"OPTIONAL_ID" }
// If id is empty, server generates: MANUAL_<utc stamp>
