// Transitional split from main.cpp.
// This file is intentionally included inside main() route-registration scope.
// TODO: convert to routes_admin_user_storage_jobs.cpp/.h after storage job helpers have a clean context.

// POST /api/v4/admin/users/migrate_storage
// Body: {"fingerprint":"...","pool_id":"raidtest"}
// Async v2: validates + enqueues job, does not run migration inline.
srv.Post("/api/v4/admin/users/migrate_storage", [&](const httplib::Request& req, httplib::Response& res) {
    std::string actor_fp;
    if (!require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, &actor_fp)) return;
        if (!require_same_origin_for_cookie_mutation(req, res)) return;

    res.set_header("Cache-Control", "no-store");

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

	const std::string fp = trim_copy(j.value("fingerprint", ""));
	std::string pool_id = trim_copy(j.value("pool_id", ""));
	if (pool_id == "DEFAULT" || pool_id == "Default" || pool_id == "default") pool_id = "default";

    if (fp.empty() || pool_id.empty()) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "missing fingerprint or pool_id"}
        }.dump());
        return;
    }

    // Preserve current fast validation behavior at submit time.
    // Full execution still happens in worker later.
    pqnas::UserStorageMigrationPlan plan;
    std::string resolve_err;
    if (!pqnas::resolve_user_storage_migration(users, users_path, fp, pool_id, &plan, &resolve_err)) {
        pqnas::AuditEvent ev;
        ev.event = "admin.user_storage_migration_rejected";
        ev.outcome = "fail";
        ev.f["fingerprint"] = fp;
        ev.f["actor_fp"] = actor_fp;
        ev.f["to_pool_id"] = pool_id;
        ev.f["reason"] = "resolve_failed";
        if (!resolve_err.empty()) ev.f["detail"] = pqnas::shorten(resolve_err, 180);
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
        audit_append(ev);

        int http = 400;
        if (resolve_err == "user_missing" || resolve_err == "storage_unallocated") http = 404;

        reply_json(res, http, json{
            {"ok", false},
            {"error", "resolve_failed"},
            {"message", "user storage migration rejected"},
            {"detail", resolve_err}
        }.dump());
        return;
    }

    if (plan.from_pool_id == plan.to_pool_id) {
        pqnas::AuditEvent ev;
        ev.event = "admin.user_storage_migration_rejected";
        ev.outcome = "fail";
        ev.f["fingerprint"] = fp;
        ev.f["actor_fp"] = actor_fp;
        ev.f["from_pool_id"] = plan.from_pool_id;
        ev.f["to_pool_id"] = plan.to_pool_id;
        ev.f["reason"] = "same_pool";
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
        audit_append(ev);

        reply_json(res, 409, json{
            {"ok", false},
            {"error", "same_pool"},
            {"message", "source and destination pool are the same"}
        }.dump());
        return;
    }

    try {
        json out = user_mig_enqueue_job_fail_closed(actor_fp, fp, pool_id, req.remote_addr);

        pqnas::AuditEvent ev;
        ev.event = "admin.user_storage_migration_job_created";
        ev.outcome = "ok";
        ev.f["fingerprint"] = fp;
        ev.f["actor_fp"] = actor_fp;
        ev.f["from_pool_id"] = plan.from_pool_id;
        ev.f["to_pool_id"] = plan.to_pool_id;
        ev.f["job_id"] = out.value("job_id", "");
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
        audit_append(ev);

        reply_json(res, 200, out.dump());
        return;
    } catch (const std::exception& e) {
        pqnas::AuditEvent ev;
        ev.event = "admin.user_storage_migration_rejected";
        ev.outcome = "fail";
        ev.f["fingerprint"] = fp;
        ev.f["actor_fp"] = actor_fp;
        ev.f["to_pool_id"] = pool_id;
        ev.f["reason"] = "enqueue_failed";
        ev.f["detail"] = pqnas::shorten(e.what(), 180);
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
        audit_append(ev);

        reply_json(res, 500, json{
            {"ok", false},
            {"error", "enqueue_failed"},
            {"message", "failed to create migration job"}
        }.dump());
        return;
    }
});

// GET /api/v4/admin/users/migrate_storage_status?job_id=<sha256>
srv.Get("/api/v4/admin/users/migrate_storage_status", [&](const httplib::Request& req, httplib::Response& res) {
    std::string actor_fp;
    if (!require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, &actor_fp)) return;

    res.set_header("Cache-Control", "no-store");

    const std::string job_id = trim_copy(req.get_param_value("job_id"));
    if (!is_sha256_hex_lower(job_id)) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_job_id"},
            {"message", "invalid job_id"}
        }.dump());
        return;
    }

    json rec;
    std::string err;
    if (!user_mig_record_read(job_id, &rec, &err)) {
        reply_json(res, 404, json{
            {"ok", false},
            {"error", err.empty() ? "not_found" : err},
            {"message", "migration job not found"}
        }.dump());
        return;
    }

    reply_json(res, 200, json{
        {"ok", true},
        {"job", rec}
    }.dump());
});

srv.Post("/api/v4/admin/users/cleanup_old_storage", [&](const httplib::Request& req, httplib::Response& res) {
    std::string actor_fp;
    if (!require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, &actor_fp)) return;
        if (!require_same_origin_for_cookie_mutation(req, res)) return;

    res.set_header("Cache-Control", "no-store");

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

    const std::string fp = trim_copy(j.value("fingerprint", ""));
    const std::string expected_active_pool_id = trim_copy(j.value("expected_active_pool_id", ""));
    const std::string old_pool_id = trim_copy(j.value("old_pool_id", ""));

    if (fp.empty() || expected_active_pool_id.empty() || old_pool_id.empty()) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "missing fingerprint, expected_active_pool_id or old_pool_id"}
        }.dump());
        return;
    }

    if (expected_active_pool_id == old_pool_id) {
        pqnas::AuditEvent ev;
        ev.event = "admin.user_storage_cleanup_rejected";
        ev.outcome = "fail";
        ev.f["fingerprint"] = fp;
        ev.f["actor_fp"] = actor_fp;
        ev.f["expected_active_pool_id"] = expected_active_pool_id;
        ev.f["old_pool_id"] = old_pool_id;
        ev.f["reason"] = "same_pool";
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
        audit_append(ev);

        reply_json(res, 409, json{
            {"ok", false},
            {"error", "same_pool"},
            {"message", "expected active pool and old pool must differ"}
        }.dump());
        return;
    }

    try {
        json out = user_cleanup_enqueue_job_fail_closed(
            actor_fp,
            fp,
            expected_active_pool_id,
            old_pool_id,
            req.remote_addr
        );

        pqnas::AuditEvent ev;
        ev.event = "admin.user_storage_cleanup_job_created";
        ev.outcome = "ok";
        ev.f["fingerprint"] = fp;
        ev.f["actor_fp"] = actor_fp;
        ev.f["expected_active_pool_id"] = expected_active_pool_id;
        ev.f["old_pool_id"] = old_pool_id;
        ev.f["job_id"] = out.value("job_id", "");
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
        audit_append(ev);

        reply_json(res, 200, out.dump());
    } catch (const std::exception& e) {
        pqnas::AuditEvent ev;
        ev.event = "admin.user_storage_cleanup_rejected";
        ev.outcome = "fail";
        ev.f["fingerprint"] = fp;
        ev.f["actor_fp"] = actor_fp;
        ev.f["expected_active_pool_id"] = expected_active_pool_id;
        ev.f["old_pool_id"] = old_pool_id;
        ev.f["reason"] = "enqueue_failed";
        ev.f["detail"] = pqnas::shorten(e.what(), 180);
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
        audit_append(ev);

        reply_json(res, 500, json{
            {"ok", false},
            {"error", "enqueue_failed"},
            {"message", e.what()}
        }.dump());
    }
});

srv.Get("/api/v4/admin/users/cleanup_old_storage_status", [&](const httplib::Request& req, httplib::Response& res) {
    std::string actor_fp;
    if (!require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, &actor_fp)) return;

    res.set_header("Cache-Control", "no-store");

    const std::string job_id = trim_copy(req.get_param_value("job_id"));
    if (!is_sha256_hex_lower(job_id)) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_job_id"},
            {"message", "invalid job_id"}
        }.dump());
        return;
    }

    json rec;
    std::string err;
    if (!user_cleanup_record_read(job_id, &rec, &err)) {
        reply_json(res, 404, json{
            {"ok", false},
            {"error", err.empty() ? "not_found" : err},
            {"message", "cleanup job record not found"}
        }.dump());
        return;
    }

    reply_json(res, 200, json{
        {"ok", true},
        {"job", rec}
    }.dump());
});
