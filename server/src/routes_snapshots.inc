// Transitional restore split from main.cpp.
// This file is intentionally included inside main() route-registration scope.
// TODO: convert restore routes to routes_snapshots_restore.cpp/.h after restore helpers have a clean context.

// POST /api/v4/snapshots/restore/prepare
// Body: {"volume":"data","id":"...","mode":"swap","force_stop":true}
srv.Post("/api/v4/snapshots/restore/prepare", [&](const httplib::Request& req, httplib::Response& res) {
    std::string actor_fp;
    if (!require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, &actor_fp)) return;

    json j;
    try { j = json::parse(req.body); }
    catch (...) {
        reply_json(res, 400, json{{"ok",false},{"error","bad_request"},{"message","invalid json"}}.dump());
        return;
    }

    const std::string vol  = j.value("volume", "");
    const std::string id   = j.value("id", "");
    const std::string mode = j.value("mode", "swap");
    const bool force_stop  = j.value("force_stop", false);

    if (vol.empty() || id.empty()) {
        reply_json(res, 400, json{{"ok",false},{"error","bad_request"},{"message","missing volume or id"}}.dump());
        return;
    }
    if (mode != "swap") {
        reply_json(res, 400, json{{"ok",false},{"error","bad_request"},{"message","unsupported mode"}}.dump());
        return;
    }
    if (!force_stop) {
        reply_json(res, 400, json{{"ok",false},{"error","bad_request"},{"message","force_stop must be true in v1"}}.dump());
        return;
    }

    std::string backend, err;
    std::vector<SnapVol> vols;
    if (!load_snapshot_volumes_from_admin_settings(admin_settings_path, &backend, &vols, &err)) {
        reply_json(res, 500, json{{"ok",false},{"error","server_error"},{"message","failed to load snapshot settings"}}.dump());
        return;
    }

    auto it = std::find_if(vols.begin(), vols.end(), [&](const SnapVol& v){ return v.name == vol; });
    if (it == vols.end()) {
        reply_json(res, 404, json{{"ok",false},{"error","not_found"},{"message","unknown volume"}}.dump());
        return;
    }

    const std::string snap_root = it->snap_root;
    const std::string snap_path = (std::filesystem::path(snap_root) / id).string();

    if (!is_path_under(snap_path, snap_root)) {
        reply_json(res, 400, json{{"ok",false},{"error","bad_request"},{"message","invalid snapshot id"}}.dump());
        return;
    }

    if (!is_btrfs_subvolume_sudo_n(snap_path)) {
        reply_json(res, 400, json{{"ok",false},{"error","bad_request"},{"message","snapshot is not a btrfs subvolume"}}.dump());
        return;
    }

    const std::string confirm_phrase = "RESTORE " + vol + " " + id;

    // create confirm id
    restore_cache_gc_best_effort();
    const std::string confirm_id = "RSTR_" + rand_hex_32();

    RestorePlan plan;
    plan.volume = vol;
    plan.snapshot_id = id;
    plan.snapshot_path = snap_path;
    plan.source_subvolume = it->source_subvolume;
    plan.mode = "swap";
    plan.confirm_phrase = confirm_phrase;
    plan.created_iso = now_iso_utc();
    // v1: expiry is handled by client + simple cache; you can add strict expiry later.

    {
        std::lock_guard<std::mutex> lk(g_restore_mu);
        g_restore_by_id[confirm_id] = plan;
    }

    // audit
    {
        pqnas::AuditEvent ev;
        ev.event = "snapshots.restore_prepare";
        ev.outcome = "ok";
        ev.f["actor_fp"] = actor_fp;
        ev.f["volume"] = vol;
        ev.f["id"] = id;
        ev.f["mode"] = mode;
        ev.f["confirm_id"] = confirm_id;
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
        audit_append(ev);
    }
	auto has_systemd_unit = [&]() -> bool {
    	// We only claim stop/start steps if systemctl exists AND pqnas.service is known
	    int rc1 = std::system("command -v systemctl >/dev/null 2>&1");
    	if (rc1 != 0) return false;
	    int rc2 = std::system("sudo -n /usr/bin/systemctl status pqnas.service >/dev/null 2>&1");
    	return (rc2 == 0);
	};

	const bool can_service = has_systemd_unit();

	json steps = json::array();
	if (can_service) steps.push_back("stop pqnas.service");
	else steps.push_back("STOP PQ-NAS manually (dev mode: running via ./start.sh)");

	steps.push_back("rename source_subvolume -> source_subvolume.pre_restore.<ts>");
	steps.push_back("btrfs subvolume snapshot <snapshot> <source_subvolume>");

	if (can_service) steps.push_back("start pqnas.service");
	else steps.push_back("START PQ-NAS manually (dev mode)");

	json warnings = json::array({
    	"Restoring replaces the live volume content",
    	"Service downtime required"
	});
	if (!can_service) warnings.push_back("Dev mode: pqnas.service not detected; you must stop/start PQ-NAS yourself");

    reply_json(res, 200, json{
        {"ok", true},
        {"confirm_id", confirm_id},
        {"expires_in_sec", 120},
        {"plan", json{
            {"volume", vol},
            {"source_subvolume", it->source_subvolume},
            {"snapshot_path", snap_path},
            {"mode", mode},
			{"steps", steps},
			{"warnings", warnings}
        }}
    }.dump());
});

    // GET /api/v4/snapshots/restore/status?job_id=RJOB_...
// GET /api/v4/snapshots/restore/status?job_id=RJOB_...
srv.Get("/api/v4/snapshots/restore/status", [&](const httplib::Request& req, httplib::Response& res) {
    std::string actor_fp;
    if (!require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, &actor_fp)) return;

    const std::string job_id = req.has_param("job_id") ? req.get_param_value("job_id") : "";
    if (job_id.empty()) {
        reply_json(res, 400, json{{"ok",false},{"error","bad_request"},{"message","missing job_id"}}.dump());
        return;
    }
    if (job_id.rfind("RJOB_", 0) != 0) {
        reply_json(res, 400, json{{"ok",false},{"error","bad_request"},{"message","invalid job_id"}}.dump());
        return;
    }

    const std::filesystem::path run_dir = "/run/pqnas/restore";
    const std::filesystem::path job_path = run_dir / (job_id + ".json");
    const std::filesystem::path result_path = run_dir / (job_id + ".result.json");

    auto file_read_all = [&](const std::filesystem::path& p) -> std::string {
        std::ifstream f(p);
        if (!f) return {};
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    };

    auto sh_quote = [](const std::string& s)->std::string{
        std::string q = s;
        size_t pos = 0;
        while ((pos = q.find("'", pos)) != std::string::npos) { q.replace(pos, 1, "'\\''"); pos += 4; }
        return "'" + q + "'";
    };

    auto run_cmd = [&](const std::string& cmd, std::string* out, int* rc_out)->bool{
        int rc = 0;
        std::string o;
        popen_capture(cmd + " 2>&1", &o, &rc);
        if (out) *out = o;
        if (rc_out) *rc_out = rc;
        return (rc == 0);
    };

    std::error_code ec;

    // 1) If result exists, wrap it as "done"
    if (std::filesystem::exists(result_path, ec) && !ec) {
        const std::string body = file_read_all(result_path);
        if (body.empty()) {
            reply_json(res, 500, json{{"ok",false},{"error","server_error"},{"message","result file unreadable"}}.dump());
            return;
        }
        try {
            json jr = json::parse(body);
            if (jr.value("job_id","") != job_id) {
                reply_json(res, 500, json{{"ok",false},{"error","server_error"},{"message","result job_id mismatch"}}.dump());
                return;
            }
            reply_json(res, 200, json{
                {"ok", true},
                {"job_id", job_id},
                {"status", "done"},
                {"result", jr}
            }.dump());
            return;
        } catch (...) {
            reply_json(res, 500, json{{"ok",false},{"error","server_error"},{"message","result file contains invalid json"}}.dump());
            return;
        }
    }

    // 2) No result yet — try systemd state
    const std::string unit = "pqnas-restore@" + job_id + ".service";

    std::string out_show;
    int rc_show = 0;

    const std::string cmd_show =
        "sudo -n /usr/bin/systemctl show " + sh_quote(unit) +
        " -p ActiveState -p SubState -p Result -p ExecMainStatus -p ExecMainCode";

    if (run_cmd(cmd_show, &out_show, &rc_show)) {
        auto kv = [&](const std::string& key)->std::string{
            std::istringstream iss(out_show);
            std::string line;
            while (std::getline(iss, line)) {
                if (line.rfind(key + "=", 0) == 0) return line.substr(key.size() + 1);
            }
            return "";
        };

        const std::string active  = kv("ActiveState");
        const std::string sub     = kv("SubState");
        const std::string result  = kv("Result");
        const std::string code    = kv("ExecMainCode");
        const std::string status2 = kv("ExecMainStatus");

        std::string derived = "running";
        if (active == "failed") derived = "failed";
        else if (active == "active" && sub == "running") derived = "running";
        else if (active == "inactive" && sub == "dead") derived = "queued";
        else if (active == "inactive" && sub == "exited") derived = "exited";
        else derived = active.empty() ? "unknown" : active;

        reply_json(res, 200, json{
            {"ok", true},
            {"job_id", job_id},
            {"status", derived},
            {"unit", unit},
            {"systemd", {
                {"ActiveState", active},
                {"SubState", sub},
                {"Result", result},
                {"ExecMainCode", code},
                {"ExecMainStatus", status2}
            }},
            {"hint", "result not written yet"}
        }.dump());
        return;
    }

    // 3) If systemd query not permitted, fall back to job file existence
    ec.clear();
    if (std::filesystem::exists(job_path, ec) && !ec) {
        reply_json(res, 200, json{
            {"ok", true},
            {"job_id", job_id},
            {"status", "queued"},
            {"hint", "result not written yet (and systemd status unavailable)"}
        }.dump());
        return;
    }

    // 4) Unknown
    reply_json(res, 404, json{{"ok",false},{"error","not_found"},{"message","unknown job_id"}}.dump());
});

// POST /api/v4/snapshots/restore/confirm
// Body: {"confirm_id":"RSTR_...","confirm_text":"RESTORE data 2026-..."}
srv.Post("/api/v4/snapshots/restore/confirm", [&](const httplib::Request& req, httplib::Response& res) {
    std::string actor_fp;
    if (!require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, &actor_fp)) return;

    json j;
    try { j = json::parse(req.body); }
    catch (...) {
        reply_json(res, 400, json{{"ok",false},{"error","bad_request"},{"message","invalid json"}}.dump());
        return;
    }

    const std::string confirm_id   = j.value("confirm_id", "");
    const std::string confirm_text = j.value("confirm_text", "");

    if (confirm_id.empty() || confirm_text.empty()) {
        reply_json(res, 400, json{{"ok",false},{"error","bad_request"},{"message","missing confirm_id or confirm_text"}}.dump());
        return;
    }

    RestorePlan plan;
    {
        std::lock_guard<std::mutex> lk(g_restore_mu);
        auto it = g_restore_by_id.find(confirm_id);
        if (it == g_restore_by_id.end()) {
            reply_json(res, 404, json{{"ok",false},{"error","not_found"},{"message","unknown confirm_id"}}.dump());
            return;
        }
        plan = it->second;
        // one-shot token: remove now (fail-safe)
        //g_restore_by_id.erase(it);
    }

    if (confirm_text != plan.confirm_phrase) {
        // audit fail
        pqnas::AuditEvent ev;
        ev.event = "snapshots.restore_confirm";
        ev.outcome = "fail";
        ev.f["actor_fp"] = actor_fp;
        ev.f["confirm_id"] = confirm_id;
        ev.f["volume"] = plan.volume;
        ev.f["id"] = plan.snapshot_id;
        ev.f["reason"] = "confirm_text_mismatch";
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
        audit_append(ev);

        reply_json(res, 400, json{{"ok",false},{"error","bad_request"},{"message","confirmation text mismatch"}}.dump());
        return;
    }
    {
        std::lock_guard<std::mutex> lk(g_restore_mu);
        g_restore_by_id.erase(confirm_id);
    }
    // Validate paths again
    const std::string snap_root_real = realpath_str(std::filesystem::path(plan.snapshot_path).parent_path().string());
    if (!is_path_under(plan.snapshot_path, snap_root_real)) {
        reply_json(res, 400, json{{"ok",false},{"error","bad_request"},{"message","snapshot path invalid"}}.dump());
        return;
    }
    if (!is_btrfs_subvolume_sudo_n(plan.snapshot_path)) {
        reply_json(res, 400, json{{"ok",false},{"error","bad_request"},{"message","snapshot no longer valid"}}.dump());
        return;
    }

    // ---- systemd restore job enqueue (does NOT stop pqnas.service here) ----

    const std::string job_id = "RJOB_" + random_b64url(18);
    const std::string created_utc = now_iso_utc();

    // Runtime dir for restore jobs
    const std::filesystem::path run_dir = "/run/pqnas/restore";
    {
        std::error_code ec;
        std::filesystem::create_directories(run_dir, ec);
        if (ec) {
            reply_json(res, 500, json{
                {"ok",false},
                {"error","server_error"},
                {"message","failed to create /run/pqnas/restore"},
                {"detail", pqnas::shorten(ec.message(), 200)}
            }.dump());
            return;
        }
    }

    const std::filesystem::path job_path = run_dir / (job_id + ".json");
    const std::filesystem::path tmp_path = run_dir / (job_id + ".tmp." + random_b64url(10));

    // Build job JSON (matches /usr/local/lib/pqnas/pqnas_restore_job.sh contract)
    json job = {
        {"job_id", job_id},
        {"created_utc", created_utc},
        {"api_version", 4},

        // REQUIRED by script:
        {"service_name", "pqnas.service"},
        {"volume", {
            {"name", plan.volume},
            {"live_path", plan.source_subvolume},
            {"snap_path", plan.snapshot_path}
        }},
        {"snapshot_id", plan.snapshot_id},
        {"request", {
            // REQUIRED by script:
            {"mode", "swap"},
            {"confirm_id", confirm_id},

            // extra metadata (ok for script to ignore):
            {"actor_fp", actor_fp},
            {"ip", req.remote_addr.empty() ? "?" : req.remote_addr}
        }}
    };


    const std::string job_text = job.dump(2) + "\n";

    // Atomic write: temp + rename
    {
        std::ofstream out(tmp_path, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!out.good()) {
            std::error_code ec2;
            std::filesystem::remove(tmp_path, ec2);

            reply_json(res, 500, json{
                {"ok",false},
                {"error","server_error"},
                {"message","failed to create temp job file"},
                {"path", tmp_path.string()}
            }.dump());
            return;
        }
        out.write(job_text.data(), (std::streamsize)job_text.size());
        if (!out.good()) {
            std::error_code ec2;
            std::filesystem::remove(tmp_path, ec2);

            reply_json(res, 500, json{
                {"ok",false},
                {"error","server_error"},
                {"message","failed to write temp job file"},
                {"path", tmp_path.string()}
            }.dump());
            return;
        }
    }

    {
        std::error_code ec;
        // If job_path exists (shouldn't), remove first to avoid rename failure.
        if (std::filesystem::exists(job_path, ec) && !ec) {
            ec.clear();
            std::filesystem::remove(job_path, ec);
            if (ec) {
                std::error_code ec2;
                std::filesystem::remove(tmp_path, ec2);

                reply_json(res, 500, json{
                    {"ok",false},
                    {"error","server_error"},
                    {"message","failed to overwrite existing job file"},
                    {"path", job_path.string()},
                    {"detail", pqnas::shorten(ec.message(), 200)}
                }.dump());
                return;
            }
        }

        ec.clear();
        std::filesystem::rename(tmp_path, job_path, ec);
        if (ec) {
            std::error_code ec2;
            std::filesystem::remove(tmp_path, ec2);

            reply_json(res, 500, json{
                {"ok",false},
                {"error","server_error"},
                {"message","failed to finalize job file"},
                {"path", job_path.string()},
                {"detail", pqnas::shorten(ec.message(), 200)}
            }.dump());
            return;
        }

        // Best-effort: restrict perms (root helper refuses world-writable)
        {
            std::error_code ec_perm;
            std::filesystem::permissions(
                job_path,
                std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                std::filesystem::perm_options::replace,
                ec_perm
            );
        }
    }


    // Start systemd restore unit via sudo (pqnas user must be allowed in sudoers)
    auto sh_quote = [](const std::string& s)->std::string{
        std::string q = s;
        size_t pos = 0;
        while ((pos = q.find("'", pos)) != std::string::npos) { q.replace(pos, 1, "'\\''"); pos += 4; }
        return "'" + q + "'";
    };

    auto run_cmd = [&](const std::string& cmd, std::string* out, int* rc_out)->bool{
        int rc = 0;
        std::string o;
        popen_capture(cmd + " 2>&1", &o, &rc);
        if (out) *out = o;
        if (rc_out) *rc_out = rc;
        return (rc == 0);
    };

    const std::string unit = "pqnas-restore@" + job_id + ".service";
    const std::string cmd_start_restore =
        "sudo -n /usr/bin/systemctl start " + sh_quote(unit);

    std::string out_start;
    int rc_start = 0;
    if (!run_cmd(cmd_start_restore, &out_start, &rc_start)) {
        // audit fail
        pqnas::AuditEvent ev;
        ev.event = "snapshots.restore_job_start";
        ev.outcome = "fail";
        ev.f["actor_fp"] = actor_fp;
        ev.f["confirm_id"] = confirm_id;
        ev.f["job_id"] = job_id;
        ev.f["volume"] = plan.volume;
        ev.f["id"] = plan.snapshot_id;
        ev.f["reason"] = "systemctl_start_failed";
        ev.f["rc"] = std::to_string(rc_start);
        ev.f["out"] = pqnas::shorten(out_start, 300);
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
        audit_append(ev);

        reply_json(res, 500, json{
            {"ok",false},
            {"error","restore_start_failed"},
            {"message","failed to start restore unit"},
            {"job_id", job_id},
            {"unit", unit},
            {"rc", rc_start},
            {"out", pqnas::shorten(out_start, 400)}
        }.dump());
        return;
    }

    // audit ok
    {
        pqnas::AuditEvent ev;
        ev.event = "snapshots.restore_job_start";
        ev.outcome = "ok";
        ev.f["actor_fp"] = actor_fp;
        ev.f["confirm_id"] = confirm_id;
        ev.f["job_id"] = job_id;
        ev.f["volume"] = plan.volume;
        ev.f["id"] = plan.snapshot_id;
        ev.f["job_path"] = pqnas::shorten(job_path.string(), 220);
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
        audit_append(ev);
    }

    reply_json(res, 200, json{
        {"ok", true},
        {"job_id", job_id},
        {"volume", plan.volume},
        {"id", plan.snapshot_id}
    }.dump());

});
