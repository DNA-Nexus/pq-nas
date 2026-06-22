// Transitional split from main.cpp.
// This file is intentionally included inside main() route-registration scope.
// TODO: convert to routes_admin_user_lifecycle.cpp/.h after admin user lifecycle helpers have a clean context.

// POST /api/v4/admin/users/disable  {"fingerprint":"..."}
    srv.Post("/api/v4/admin/users/disable", [&](const httplib::Request& req, httplib::Response& res) {
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
        if (!users.exists(fp)) {
            reply_json(res, 404, json({{"ok",false},{"error","not_found"},{"message","user not found"}}).dump());
            return;
        }


        if (fp == actor_fp) {
            res.status = 400;
            res.set_content("{\"ok\":false,\"error\":\"self_disable\",\"message\":\"Cannot disable your own admin account.\"}", "application/json");
            return;
        }

        if (admin_would_remove_last_enabled_admin(fp)) {
            res.status = 400;
            res.set_content("{\"ok\":false,\"error\":\"last_admin\",\"message\":\"Cannot disable the last enabled admin.\"}", "application/json");
            return;
        }

        if (!users.set_status(fp, "disabled")) {
            reply_json(res, 500, json({{"ok",false},{"error","server_error"},{"message","set_status failed"}}).dump());
            return;
        }
        {
            std::string token_revoke_err;
            (void)g_app_tokens.revoke_devices_for_fingerprint(fp, &token_revoke_err);
        }

        const bool saved = users.save(users_path);

        {
            pqnas::AuditEvent ev;
            ev.event = "admin.user_disabled";
            ev.outcome = saved ? "ok" : "fail";
            ev.f["fingerprint"] = fp;
            ev.f["ts"] = now_iso_utc();
            ev.f["actor_fp"] = actor_fp;
            ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
            audit_append(ev);
        }

        if (!saved) {
            reply_json(res, 500, json({{"ok",false},{"error","server_error"},{"message","users save failed"}}).dump());
            return;
        }

        reply_json(res, 200, json({{"ok",true}}).dump());
    });

    srv.Get("/api/debug/auth/approvals", [&](const httplib::Request& req, httplib::Response& res) {
        const bool auth_debug_enabled = (std::getenv("PQNAS_ENABLE_AUTH_DEBUG") != nullptr);
        if (!auth_debug_enabled) {
            reply_json(res, 404, json{
                {"ok", false},
                {"error", "not_found"}
            }.dump());
            return;
        }
        if (!require_admin_cookie_users(req, res, COOKIE_KEY, std::string{}, &users)) return;

        res.set_header("Cache-Control", "no-store");

        json out;
        out["ok"] = true;
        out["count"] = 0;
        out["items_redacted"] = true;
        out["message"] = "Auth debug approval TTL details are redacted.";

        {
            std::lock_guard<std::mutex> lk(g_approvals_mu);
            out["count"] = (int)g_approvals.size();
        }

        reply_json(res, 200, out.dump());
    });

    // POST /api/v4/admin/users/delete
    // Body: {"fingerprint":"..."}
	srv.Post("/api/v4/admin/users/delete", [&](const httplib::Request& req, httplib::Response& res) {
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

    	// Prevent admin self-lockout: do not allow deleting your own identity entry.
    	if (fp == actor_fp) {
	        {
        	    pqnas::AuditEvent ev;
    	        ev.event = "admin.self_lockout_blocked";
	            ev.outcome = "fail";
            	ev.f["action"] = "delete";
        	    ev.f["fingerprint"] = fp;
    	        ev.f["ts"] = now_iso_utc();
	            ev.f["actor_fp"] = actor_fp;
            	ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
    	        audit_append(ev);
	        }

        	reply_json(res, 400, json({
    	        {"ok",false},
	            {"error","bad_request"},
            	{"message","refusing to delete your own admin entry (prevents lockout)"}
        	}).dump());
    	    return;
	    }

	    if (!users.exists(fp)) {
        	reply_json(res, 404, json({{"ok",false},{"error","not_found"},{"message","user not found"}}).dump());
    	    return;
	    }

    	// Safety: refuse deleting an enabled admin identity (keeps system recoverable)
	    if (users.is_admin_enabled(fp)) {
        	reply_json(res, 400, json({{"ok",false},{"error","bad_request"},{"message","refusing to delete an enabled admin"}}).dump());
    	    return;
	    }

    	const bool ok_del  = users.erase(fp);
    	const bool ok_save = ok_del ? users.save(users_path) : false;

    	{
	        pqnas::AuditEvent ev;
        	ev.event = "admin.user_deleted";
    	    ev.outcome = (ok_del && ok_save) ? "ok" : "fail";
	        ev.f["fingerprint"] = fp;
        	ev.f["ts"] = now_iso_utc();
    	    ev.f["actor_fp"] = actor_fp;
	        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
    	    audit_append(ev);
	    }

    	if (!ok_del) {
	        reply_json(res, 500, json({{"ok",false},{"error","server_error"},{"message","delete failed"}}).dump());
        	return;
    	}
	    if (!ok_save) {
        	reply_json(res, 500, json({{"ok",false},{"error","server_error"},{"message","users save failed"}}).dump());
    	    return;
	    }

    	reply_json(res, 200, json({{"ok",true}}).dump());
	});
