// Transitional avatar split from main.cpp.
// This file is intentionally included inside main() route-registration scope.
// TODO: convert to routes_user_avatars.cpp/.h after avatar helpers have a clean context.

srv.Post("/api/v4/admin/users/avatar_upload", [&](const httplib::Request& req, httplib::Response& res) {
    std::string actor_fp;
    if (!require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, &actor_fp)) return;
        if (!require_same_origin_for_cookie_mutation(req, res)) return;

    json j;
    try { j = json::parse(req.body); }
    catch (...) {
        reply_json(res, 400, json({{"ok",false},{"error","bad_request"},{"message","invalid json"}}).dump());
        return;
    }

    const std::string fp   = j.value("fingerprint", "");
    const std::string mime = j.value("mime", "");
    const std::string b64  = j.value("data_b64", "");

    if (fp.empty() || b64.empty()) {
        reply_json(res, 400, json({{"ok",false},{"error","bad_request"},{"message","missing fingerprint or data"}}).dump());
        return;
    }

    // Allowlist types
    std::string ext;
    if (mime == "image/png") ext = ".png";
    else if (mime == "image/jpeg") ext = ".jpg";
    else if (mime == "image/webp") ext = ".webp";
    else {
        reply_json(res, 400, json({{"ok",false},{"error","bad_request"},{"message","unsupported image type"}}).dump());
        return;
    }

    // Decode standard base64 (with padding) -> bytes
    std::string bytes;
    if (!b64std_decode_to_bytes(b64, bytes)) {
        reply_json(res, 400, json({{"ok",false},{"error","bad_request"},{"message","base64 decode failed"}}).dump());
        return;
    }


    if (bytes.size() > 256 * 1024) {
        reply_json(res, 400, json({{"ok",false},{"error","bad_request"},{"message","file too large"}}).dump());
        return;
    }

    std::filesystem::path dir = std::filesystem::path(pqnas::data_root_dir()) / "avatars";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        reply_json(res, 500, json({{"ok",false},{"error","server_error"},{"message","mkdir failed"}}).dump());
        return;
    }

    std::filesystem::path out = dir / (fp + ext);
    {
        std::ofstream o(out.string(), std::ios::binary | std::ios::trunc);
        if (!o.good()) {
            reply_json(res, 500, json({{"ok",false},{"error","server_error"},{"message","write failed"}}).dump());
            return;
        }
        o.write(bytes.data(), (std::streamsize)bytes.size());
    }

    const std::string url = std::string("/api/v4/users/avatar?fingerprint=") + fp;
    reply_json(res, 200, json({{"ok",true},{"avatar_url",url}}).dump());
});


// Shared avatar file responder used by both admin and normal-user avatar routes.
auto serve_avatar_for_fingerprint = [&](const std::string& fp, httplib::Response& res) {
    const std::filesystem::path dir = std::filesystem::path(pqnas::data_root_dir()) / "avatars";
    const std::filesystem::path p_png  = dir / (fp + ".png");
    const std::filesystem::path p_jpg  = dir / (fp + ".jpg");
    const std::filesystem::path p_webp = dir / (fp + ".webp");

    std::filesystem::path p;
    std::string ct;

    if (std::filesystem::exists(p_png))      { p = p_png;  ct = "image/png"; }
    else if (std::filesystem::exists(p_jpg)) { p = p_jpg;  ct = "image/jpeg"; }
    else if (std::filesystem::exists(p_webp)){ p = p_webp; ct = "image/webp"; }
    else {
        reply_json(res, 404,
            json({{"ok",false},{"error","not_found"},{"message","file missing"}}).dump());
        return;
    }

    std::ifstream f(p, std::ios::binary);
    if (!f.good()) {
        reply_json(res, 500,
            json({{"ok",false},{"error","server_error"},{"message","failed to open avatar"}}).dump());
        return;
    }

    std::string bytes(
        (std::istreambuf_iterator<char>(f)),
        std::istreambuf_iterator<char>());

    if (!f.good() && !f.eof()) {
        reply_json(res, 500,
            json({{"ok",false},{"error","server_error"},{"message","failed to read avatar"}}).dump());
        return;
    }

    res.set_header("Cache-Control", "no-store");
    res.set_content(bytes, ct.c_str());
};

// GET /api/v4/admin/users/avatar?fingerprint=...
srv.Get("/api/v4/admin/users/avatar", [&](const httplib::Request& req, httplib::Response& res) {
    std::string actor_fp;
    if (!require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, &actor_fp))
        return;

    const std::string fp =
        req.has_param("fingerprint") ? req.get_param_value("fingerprint") : "";

    if (fp.empty()) {
        reply_json(res, 400,
            json({{"ok",false},{"error","bad_request"},{"message","missing fingerprint"}}).dump());
        return;
    }

    serve_avatar_for_fingerprint(fp, res);
});

// GET /api/v4/users/avatar?fingerprint=...
srv.Get("/api/v4/users/avatar", [&](const httplib::Request& req, httplib::Response& res) {
    std::string actor_fp, actor_role;

    // Use your normal logged-in user auth helper here.
    // If your helper name differs, only this one line needs renaming.
    if (!require_user_cookie_users_actor(req, res, COOKIE_KEY, &users, &actor_fp, &actor_role))
        return;


    const std::string target_fp =
        req.has_param("fingerprint") ? req.get_param_value("fingerprint") : "";

    if (target_fp.empty()) {
        reply_json(res, 400,
            json({{"ok",false},{"error","bad_request"},{"message","missing fingerprint"}}).dump());
        return;
    }

    if (!users.load(users_path)) {
        reply_json(res, 500,
            json({{"ok",false},{"error","users_reload_failed"},{"message","failed to reload users"}}).dump());
        return;
    }

    if (!workspaces.load(workspaces_path)) {
        reply_json(res, 500,
            json({{"ok",false},{"error","workspaces_reload_failed"},{"message","failed to reload workspaces"}}).dump());
        return;
    }

    bool allowed = false;

    // Own avatar is always allowed.
    if (target_fp == actor_fp) {
        allowed = true;
    }

    // Enabled admins may view any avatar.
    if (!allowed && users.is_admin_enabled(actor_fp)) {
        allowed = true;
    }

    // Otherwise require both users to be enabled members of the same enabled workspace.
    if (!allowed) {
        for (const auto& kv : workspaces.snapshot()) {
            const auto& w = kv.second;
            if (w.status != "enabled") continue;

            bool actor_in_same_workspace = false;
            bool target_in_same_workspace = false;

            for (const auto& m : w.members) {
                if (m.status != "enabled") continue;

                if (m.fingerprint == actor_fp)  actor_in_same_workspace = true;
                if (m.fingerprint == target_fp) target_in_same_workspace = true;

                if (actor_in_same_workspace && target_in_same_workspace) {
                    allowed = true;
                    break;
                }
            }

            if (allowed) break;
        }
    }

    if (!allowed) {
        reply_json(res, 403,
            json({{"ok",false},{"error","forbidden"},{"message","avatar access denied"}}).dump());
        return;
    }

    serve_avatar_for_fingerprint(target_fp, res);
});

    // POST /api/v4/admin/users/avatar_remove
    srv.Post("/api/v4/admin/users/avatar_remove", [&](const httplib::Request& req, httplib::Response& res) {
        std::string actor_fp;
        if (!require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, &actor_fp)) return;
        if (!require_same_origin_for_cookie_mutation(req, res)) return;

        json j;
        try { j = json::parse(req.body); }
        catch (...) {
            reply_json(res, 400, json({{"ok",false},{"error","bad_request"},{"message","invalid json"}}).dump());
            return;
        }

        const std::string fp = j.value("fingerprint", "");
        if (fp.empty()) {
            reply_json(res, 400, json({{"ok",false},{"error","bad_request"},{"message","missing fingerprint"}}).dump());
            return;
        }

        auto cur = users.get(fp);
        if (!cur.has_value()) {
            reply_json(res, 404, json({{"ok",false},{"error","not_found"},{"message","user not found"}}).dump());
            return;
        }

        // delete any avatar files (best-effort)
        const std::filesystem::path dir = std::filesystem::path(pqnas::data_root_dir()) / "avatars";
        std::error_code ec;
        std::filesystem::remove(dir / (fp + ".png"), ec);
        std::filesystem::remove(dir / (fp + ".jpg"), ec);
        std::filesystem::remove(dir / (fp + ".webp"), ec);

        // clear stored url
        pqnas::UserRec u = *cur;
        u.avatar_url.clear();

        const bool ok_upsert = users.upsert(u);
        const bool ok_save   = ok_upsert ? users.save(users_path) : false;

        if (!ok_upsert || !ok_save) {
            reply_json(res, 500, json({{"ok",false},{"error","server_error"},{"message","save failed"}}).dump());
            return;
        }

        reply_json(res, 200, json({{"ok",true}}).dump());
    });
// POST /api/v4/user/profile/avatar_upload
// Normal signed-in users can upload only their own avatar.
srv.Post("/api/v4/user/profile/avatar_upload", [&](const httplib::Request& req, httplib::Response& res) {
    std::string actor_fp, actor_role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &actor_fp, &actor_role)) return;

    if (!is_valid_fingerprint_hex(actor_fp)) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid actor fingerprint"}
        }.dump());
        return;
    }

    json j;
    try {
        j = json::parse(req.body);
    } catch (...) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid json"}
        }.dump());
        return;
    }

    const std::string mime = j.value("mime", "");
    const std::string b64  = j.value("data_b64", "");

    if (b64.empty()) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "missing data"}
        }.dump());
        return;
    }

    std::string ext;
    if (mime == "image/png") ext = ".png";
    else if (mime == "image/jpeg") ext = ".jpg";
    else if (mime == "image/webp") ext = ".webp";
    else {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "unsupported image type"}
        }.dump());
        return;
    }

    std::string bytes;
    if (!b64std_decode_to_bytes(b64, bytes)) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "base64 decode failed"}
        }.dump());
        return;
    }

    if (bytes.size() > 256 * 1024) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "file too large"}
        }.dump());
        return;
    }

    if (!users.load(users_path)) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "users_reload_failed"},
            {"message", "failed to reload users"}
        }.dump());
        return;
    }

    auto cur = users.get(actor_fp);
    if (!cur.has_value()) {
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "user not found"}
        }.dump());
        return;
    }

    const std::filesystem::path dir =
        std::filesystem::path(pqnas::data_root_dir()) / "avatars";

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "avatar directory create failed"}
        }.dump());
        return;
    }

    const std::filesystem::path out = dir / (actor_fp + ext);

    {
        std::ofstream o(out.string(), std::ios::binary | std::ios::trunc);
        if (!o.good()) {
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "avatar write failed"}
            }.dump());
            return;
        }

        o.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    // Remove stale avatar variants so serve_avatar_for_fingerprint resolves this upload.
    if (ext != ".png")  std::filesystem::remove(dir / (actor_fp + ".png"), ec);
    if (ext != ".jpg")  std::filesystem::remove(dir / (actor_fp + ".jpg"), ec);
    if (ext != ".webp") std::filesystem::remove(dir / (actor_fp + ".webp"), ec);

    const std::string url =
        std::string("/api/v4/users/avatar?fingerprint=") + actor_fp;

    pqnas::UserRec u = *cur;
    u.avatar_url = url;

    const bool ok_upsert = users.upsert(u);
    const bool ok_save = ok_upsert ? users.save(users_path) : false;

    {
        pqnas::AuditEvent ev;
        ev.event = "user.avatar_upload";
        ev.outcome = (ok_upsert && ok_save) ? "ok" : "fail";
        ev.f["actor_fp"] = actor_fp;
        ev.f["mime"] = mime;
        ev.f["bytes"] = std::to_string(bytes.size());
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
        audit_append(ev);
    }

    if (!ok_upsert || !ok_save) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "avatar metadata save failed"}
        }.dump());
        return;
    }

    reply_json(res, 200, json{
        {"ok", true},
        {"avatar_url", url}
    }.dump());
});


// POST /api/v4/user/profile/avatar_remove
// Normal signed-in users can remove only their own avatar.
