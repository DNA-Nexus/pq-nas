#include "routes_storage_raid.h"

// TODO: kopioi tarvittavat include-rivit main.cpp:stä build-errorien mukaan.
// Aluksi voi olla helpompi ottaa vähän liikaa includeja kuin liian vähän.

#include "httplib.h"

void register_storage_raid_routes(httplib::Server& srv) {

// ----- GET /api/v4/storage/disks (admin-only) --------------------------------
srv.Get("/api/v4/storage/disks", [&](const httplib::Request& req, httplib::Response& res) {
pqnas::UsersRegistry users;

// IMPORTANT: load users from disk before checking admin role
if (!users.load(users_path)) {
    reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}, {"path", users_path}}.dump());
    return;
}

if (!require_admin_cookie_users(req, res, COOKIE_KEY, users_path, &users)) return;


	std::string raw;
	json j = storage_list_disks_json(&raw);

	// Optional: include raw lsblk JSON for debugging (cap size to avoid huge responses)
	if (getenv_bool("PQNAS_STORAGE_DEBUG_LSBLK", false)) {
    	if (raw.size() > 1024 * 1024) raw.resize(1024 * 1024); // 1 MiB cap
    	j["lsblk_raw"] = raw;
	}


    reply_json(res, 200, j.dump());
});

// ----- GET /api/v4/storage/status?mount=/path (admin-only) -------------------
srv.Get("/api/v4/storage/status", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;

    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}, {"path", users_path}}.dump());
        return;
    }

    if (!require_admin_cookie_users(req, res, COOKIE_KEY, users_path, &users)) return;


    // Default mount: prefer configured storage root
    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";

    // default mount inside allowed_prefix
    std::string mount = allowed_prefix + "/data";

    // override if caller provided mount param
    if (req.has_param("mount")) {
        mount = req.get_param_value("mount");
    }


    if (!is_abs_path_safe(mount)) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mount"}}.dump());
        return;
    }

    // --- Resolve mountpoint + fstype first (must happen before running btrfs) ---
    std::string fs_target_out;
    int rc_target = run_capture("/usr/bin/findmnt -no TARGET --target " + sh_quote(mount), &fs_target_out);
    cap_string(fs_target_out, 16 * 1024);
    rtrim_inplace(fs_target_out);

    std::string fstype_out;
    int rc_fs = run_capture("/usr/bin/findmnt -no FSTYPE --target " + sh_quote(mount), &fstype_out);
    cap_string(fstype_out, 16 * 1024);
    rtrim_inplace(fstype_out);

    std::string source_out;
    int rc_src = run_capture("/usr/bin/findmnt -no SOURCE --target " + sh_quote(mount), &source_out);
    cap_string(source_out, 16 * 1024);
    rtrim_inplace(source_out);

    if (rc_target != 0 || fs_target_out.empty() ||
        rc_fs != 0 || fstype_out.empty() ||
        rc_src != 0 || source_out.empty()) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "mount_not_found"},
            {"mount", mount}
        }.dump());
        return;
    }


    // Enforce allowlist on the *resolved mountpoint* (not the user-provided directory)
    const std::string resolved_mount = fs_target_out;
    const std::string resolved_source = source_out;


    if (resolved_mount.rfind(allowed_prefix, 0) != 0) {
        const std::string test_prefix  = "/srv/pqnas-test";
        const std::string test_prefix2 = "/srv/pqnas-test-btrfs";
        if (resolved_mount.rfind(test_prefix, 0) != 0 && resolved_mount.rfind(test_prefix2, 0) != 0) {
            reply_json(res, 403, json{
                {"ok", false},
                {"error", "mount_not_allowed"},
                {"allowed_prefix", allowed_prefix},
                {"resolved_mount", resolved_mount},
                {"resolved_source", resolved_source}
            }.dump());

            return;
        }
    }

    if (fstype_out != "btrfs") {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "not_btrfs"},
            {"mount", mount},
            {"resolved_mount", resolved_mount},
            {"resolved_source", resolved_source},
            {"fstype", fstype_out}
        }.dump());

        return;
    }

    // Run btrfs commands against the resolved mountpoint (fixes /srv/pqnas/data case)
    json j = storage_btrfs_status_json(resolved_mount);
    j["input_mount"] = mount;
    j["resolved_mount"] = resolved_mount;
    j["resolved_source"] = resolved_source;
    {
    const std::string d = parent_disk_from_dev(resolved_source);
    if (!d.empty()) j["resolved_disk"] = d;
    }
    j["fstype"] = fstype_out;
    reply_json(res, 200, j.dump());


});

// ----- GET /api/v4/storage/pools (admin-only, read-only) -------------------
srv.Get("/api/v4/storage/pools", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;

    if (!users.load(users_path)) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "users_load_failed"},
            {"path", users_path}
        }.dump());
        return;
    }

    if (!require_admin_cookie_users(req, res, COOKIE_KEY, users_path, &users)) return;

    json pools_cfg = pqnas::load_or_init_pools_cfg_v3(users_path);

    if (!workspaces.load(workspaces_path)) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "workspaces_load_failed"},
            {"message", "failed to reload workspaces"}
        }.dump());
        return;
    }

    auto sum_allocated_user_quota_on_pool_local =
    [&](const std::string& want_pool_id) -> std::uint64_t
{
    const std::string want_pool = normalize_storage_pool_id(want_pool_id);
    std::uint64_t total = 0;

    for (const auto& kv : users.snapshot()) {
        const auto& u = kv.second;
        if (u.storage_state != "allocated") continue;

        const std::string user_pool = normalize_storage_pool_id(u.storage_pool_id);
        if (user_pool != want_pool) continue;

        const std::uint64_t q = static_cast<std::uint64_t>(u.quota_bytes);
        if (std::numeric_limits<std::uint64_t>::max() - total < q) {
            return std::numeric_limits<std::uint64_t>::max();
        }
        total += q;
    }

    return total;
};
        auto attach_pool_accounting = [&](json* pj) {
        if (!pj || !pj->is_object()) return;

        const std::string mount = pj->value("mount", "");
        const bool is_editable_pool = pj->value("is_editable_pool", false);

        // Editable pool cards map to named pools.
        // Non-editable system-volume card represents the default pool.
        const std::string effective_pool_id =
            is_editable_pool
                ? normalize_storage_pool_id(pj->value("pool_id", ""))
                : std::string{};

        const std::uint64_t allocated_user_quota_bytes =
            sum_allocated_user_quota_on_pool_local(effective_pool_id);

        const std::uint64_t allocated_workspace_quota_bytes =
            pqnas::sum_allocated_workspace_quota_on_pool(workspaces, effective_pool_id, "");

        std::uint64_t allocated_total_quota_bytes = allocated_user_quota_bytes;
        if (std::numeric_limits<std::uint64_t>::max() - allocated_total_quota_bytes < allocated_workspace_quota_bytes) {
            allocated_total_quota_bytes = std::numeric_limits<std::uint64_t>::max();
        } else {
            allocated_total_quota_bytes += allocated_workspace_quota_bytes;
        }

        std::uint64_t accounting_pool_total_bytes = 0;
        std::uint64_t accounting_pool_free_bytes = 0;

        const std::string stat_path =
            is_editable_pool
                ? mount
                : pqnas::data_root_dir();

        if (!statvfs_path(stat_path, &accounting_pool_total_bytes, &accounting_pool_free_bytes)) {
            accounting_pool_total_bytes = 0;
            accounting_pool_free_bytes = 0;
        }

        const std::uint64_t remaining_allocatable_bytes =
            (accounting_pool_total_bytes > allocated_total_quota_bytes)
                ? (accounting_pool_total_bytes - allocated_total_quota_bytes)
                : 0;

        (*pj)["accounting_pool_id"] = effective_pool_id.empty() ? "default" : effective_pool_id;
        (*pj)["allocated_user_quota_bytes"] = allocated_user_quota_bytes;
        (*pj)["allocated_workspace_quota_bytes"] = allocated_workspace_quota_bytes;
        (*pj)["allocated_total_quota_bytes"] = allocated_total_quota_bytes;
        (*pj)["remaining_allocatable_bytes"] = remaining_allocatable_bytes;
        (*pj)["accounting_pool_total_bytes"] = accounting_pool_total_bytes;
        (*pj)["accounting_pool_free_bytes"] = accounting_pool_free_bytes;
    };
    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";

    const std::string test_prefix  = "/srv/pqnas-test";
    const std::string test_prefix2 = "/srv/pqnas-test-btrfs";

    const char* BTRFS1 = "/usr/bin/btrfs";
    const char* BTRFS2 = "/usr/sbin/btrfs";

    auto exists_exec = [](const char* p) -> bool {
        std::error_code ec;
        auto st = std::filesystem::status(p, ec);
        if (ec) return false;
        if (!std::filesystem::is_regular_file(st)) return false;
        auto perms = st.permissions();
        using P = std::filesystem::perms;
        return (perms & P::owner_exec) != P::none ||
               (perms & P::group_exec) != P::none ||
               (perms & P::others_exec) != P::none;
    };

    const char* BTRFS = exists_exec(BTRFS1) ? BTRFS1 : (exists_exec(BTRFS2) ? BTRFS2 : BTRFS1);

    // Capabilities for UI
    std::string root_fstype;
    {
        int ec_rootfs = 0;
        run_cmd_capture("/usr/bin/findmnt -no FSTYPE --target " + sh_quote(allowed_prefix), &root_fstype, &ec_rootfs);
        cap_string(root_fstype, 4096);
        rtrim_inplace(root_fstype);
        if (ec_rootfs != 0) root_fstype.clear();
    }

    // Load lsblk once so btrfs_show_parsed_to_json can derive parent_disk reliably.
    std::string raw_lsblk;
    json disks_j = storage_list_disks_json(&raw_lsblk);
    json by_path = disks_j.value("by_path", json::object());
    json by_name = disks_j.value("by_name", json::object());

    // Runtime pools keyed by mount so we can later merge config-only entries.
    std::map<std::string, json> runtime_by_mount;

    std::string mounts_out;
    int rc = run_capture("/usr/bin/findmnt -rn -t btrfs -o TARGET,SOURCE,FSTYPE", &mounts_out);
    cap_string(mounts_out, 1024 * 1024);
    rtrim_inplace(mounts_out);

    // findmnt returns non-zero when there are no matches.
    // That is valid on systems with no mounted Btrfs pools.
    const bool no_btrfs_matches = (rc != 0 && trim_copy(mounts_out).empty());

    if (rc != 0 && !no_btrfs_matches) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "findmnt_failed"}
        }.dump());
        return;
    }

    for (const std::string& raw : split_lines(mounts_out)) {
        std::string line = trim_copy(raw);
        if (line.empty()) continue;

        std::vector<std::string> toks;
        {
            std::string cur;
            for (char c : line) {
                if (c == ' ' || c == '\t') {
                    if (!cur.empty()) {
                        toks.push_back(cur);
                        cur.clear();
                    }
                } else {
                    cur.push_back(c);
                }
            }
            if (!cur.empty()) toks.push_back(cur);
        }

        if (toks.size() < 3) continue;

        const std::string target = toks[0];
        const std::string source = toks[1];
        const std::string fstype = toks[2];

        if (fstype != "btrfs") continue;
        if (target.empty() || source.empty()) continue;
        if (target[0] != '/') continue;

        const bool allowed =
            starts_with(target, allowed_prefix) ||
            starts_with(target, test_prefix) ||
            starts_with(target, test_prefix2);

        if (!allowed) continue;

        std::string show_out, df_out, usage_out;

        int rc_show = run_capture(
            std::string("/usr/bin/sudo -n ") + BTRFS + " filesystem show " + sh_quote(target) + " 2>&1",
            &show_out
        );
        int rc_df = run_capture(
            std::string("/usr/bin/sudo -n ") + BTRFS + " filesystem df -b " + sh_quote(target) + " 2>&1",
            &df_out
        );
        int rc_usage = run_capture(
            std::string("/usr/bin/sudo -n ") + BTRFS + " filesystem usage -b " + sh_quote(target) + " 2>&1",
            &usage_out
        );

        cap_string(show_out, 256 * 1024);   rtrim_inplace(show_out);
        cap_string(df_out, 256 * 1024);     rtrim_inplace(df_out);
        cap_string(usage_out, 512 * 1024);  rtrim_inplace(usage_out);

        if (rc_show != 0) continue;

        std::string label, uuid;
        int devices = -1;
        parse_btrfs_filesystem_show(show_out, &label, &uuid, &devices);

        std::string prof_data, prof_meta;
        if (rc_df == 0) {
            parse_btrfs_df_profiles(df_out, &prof_data, &prof_meta);
        }

        int64_t size_bytes = -1;
        int64_t used_bytes = -1;
        int64_t free_estimated_bytes = -1;

        if (rc_usage == 0) {
            parse_btrfs_usage_bytes(usage_out, &size_bytes, &used_bytes, &free_estimated_bytes);
        }

        BtrfsShowParsed parsed_show = parse_btrfs_filesystem_show(show_out);
        json show_j = btrfs_show_parsed_to_json(parsed_show, by_path, by_name);

        const std::vector<std::string> runtime_member_parents =
            pqnas::runtime_member_parent_disks_from_show_json(show_j);

        bool busy = false;
        std::string busy_lock;
        {
            const std::string lockp = raid_mount_lock_path(target);
            std::error_code ec;
            if (std::filesystem::exists(lockp, ec) && !ec) {
                busy = true;
                busy_lock = lockp;
            }
        }

        json runtime_j;
        runtime_j["mount"] = target;
        runtime_j["pool_id"] = pool_id_from_mount_best_effort(target);
        runtime_j["uuid"] = uuid.empty() ? "" : uuid;
        runtime_j["label"] = label.empty() ? "" : label;
        runtime_j["devices"] = (devices >= 0) ? devices : 0;
        runtime_j["profile_data"] = prof_data.empty() ? "" : prof_data;
        runtime_j["profile_metadata"] = prof_meta.empty() ? "" : prof_meta;
        runtime_j["runtime_mode"] = pqnas::pool_mode_from_profiles_best_effort(prof_data, prof_meta);
        runtime_j["size_bytes"] = (size_bytes >= 0) ? size_bytes : 0;
        runtime_j["used_bytes"] = (used_bytes >= 0) ? used_bytes : 0;
        runtime_j["free_estimated_bytes"] = (free_estimated_bytes >= 0) ? free_estimated_bytes : 0;
        runtime_j["usable_total_bytes"] =
            (used_bytes >= 0 && free_estimated_bytes >= 0) ? (used_bytes + free_estimated_bytes) : 0;
        runtime_j["resolved_source"] = source;

        {
            const std::string d = parent_disk_from_dev(source);
            if (!d.empty()) runtime_j["resolved_disk"] = d;
        }

        json cfg_pool = json::object();
        if (pools_cfg.contains("pools") && pools_cfg["pools"].is_object()) {
            auto itp = pools_cfg["pools"].find(target);
            if (itp != pools_cfg["pools"].end() && itp->is_object()) {
                cfg_pool = *itp;
            }
        }

        if (cfg_pool.empty()) {
            cfg_pool = json{
                {"mount", target},
                {"pool_id", pool_id_from_mount_best_effort(target)},
                {"display_name", pqnas::pools_display_name_for_mount_v3(pools_cfg, target)},
                {"managed", false},
                {"mode", pqnas::pool_mode_from_profiles_best_effort(prof_data, prof_meta)},
                {"slots", json::array()},
                {"slot_count", 0}
            };
        } else {
            cfg_pool["mount"] = target;
        }

        pqnas::infer_slots_from_runtime_if_missing(&cfg_pool, runtime_member_parents);

        json merged = pqnas::merge_pool_runtime_and_config(
            cfg_pool,
            runtime_j,
            runtime_member_parents,
            busy,
            busy_lock
        );

        const std::string pools_root = allowed_prefix + "/pools/";
        merged["is_editable_pool"] = starts_with(target, pools_root);

        attach_pool_accounting(&merged);

        runtime_by_mount[target] = merged;
    }

    // Final output array:
    // 1) all runtime pools
    // 2) plus config-defined pools not currently mounted
    json arr = json::array();
    std::set<std::string> emitted;

    for (const auto& kv : runtime_by_mount) {
        arr.push_back(kv.second);
        emitted.insert(kv.first);
    }

    if (pools_cfg.contains("pools") && pools_cfg["pools"].is_object()) {
        for (auto it = pools_cfg["pools"].begin(); it != pools_cfg["pools"].end(); ++it) {
            const std::string mount = it.key();
            if (emitted.find(mount) != emitted.end()) continue;
            if (!it->is_object()) continue;

            json cfg_pool = *it;
            cfg_pool["mount"] = mount;

            bool busy = false;
            std::string busy_lock;
            {
                const std::string lockp = raid_mount_lock_path(mount);
                std::error_code ec;
                if (std::filesystem::exists(lockp, ec) && !ec) {
                    busy = true;
                    busy_lock = lockp;
                }
            }

            // No runtime state for this one
            pqnas::infer_slots_from_runtime_if_missing(&cfg_pool, std::vector<std::string>{});

            json merged = pqnas::merge_pool_runtime_and_config(
                cfg_pool,
                json::object(),
                std::vector<std::string>{},
                busy,
                busy_lock
            );

            const std::string pools_root = allowed_prefix + "/pools/";
            merged["is_editable_pool"] = starts_with(mount, pools_root);

            attach_pool_accounting(&merged);

            arr.push_back(merged);
        }
    }

    // Stable ordering for UI: by mount
    std::sort(arr.begin(), arr.end(), [](const json& a, const json& b) {
        return a.value("mount", "") < b.value("mount", "");
    });

    reply_json(res, 200, json{
        {"ok", true},
        {"storage_root_fstype", root_fstype.empty() ? "unknown" : root_fstype},
        {"has_runtime_btrfs_pools", !runtime_by_mount.empty()},
        {"pools", arr}
    }.dump());
});


// ----- POST /api/v4/storage/pools/set-name (admin-only) ---------------------
// Body: { "mount": "/srv/pqnas", "display_name": "Home pool" }
srv.Post("/api/v4/storage/pools/set-name", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;
    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}, {"path", users_path}}.dump());
        return;
    }

    // ---- audit helpers ----
    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto audit_fail = [&](const std::string& actor_fp,
                          const std::string& reason,
                          int http,
                          const std::string& detail = "",
                          const json& extra = json::object()) {
        pqnas::AuditEvent ev;
        ev.event = "v4.storage_pools_set_name_fail";
        ev.outcome = "fail";
        if (!actor_fp.empty()) ev.f["fingerprint"] = actor_fp;
        ev.f["reason"] = reason;
        ev.f["http"] = std::to_string(http);
        if (!detail.empty()) ev.f["detail"] = pqnas::shorten(detail, 220);

        if (extra.is_object()) {
            for (auto it = extra.begin(); it != extra.end(); ++it) {
                const std::string k = pqnas::shorten(it.key(), 64);
                if (it.value().is_string()) ev.f["x_" + k] = pqnas::shorten(it.value().get<std::string>(), 220);
                else if (it.value().is_number_integer() || it.value().is_number_unsigned()) ev.f["x_" + k] = it.value().dump();
                else if (it.value().is_boolean()) ev.f["x_" + k] = (it.value().get<bool>() ? "true" : "false");
                else ev.f["x_" + k] = pqnas::shorten(it.value().dump(), 220);
            }
        }

        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
        audit_append(ev);
    };

    auto audit_ok = [&](const std::string& actor_fp,
                        const json& extra = json::object()) {
        pqnas::AuditEvent ev;
        ev.event = "v4.storage_pools_set_name_ok";
        ev.outcome = "ok";
        if (!actor_fp.empty()) ev.f["fingerprint"] = actor_fp;

        if (extra.is_object()) {
            for (auto it = extra.begin(); it != extra.end(); ++it) {
                const std::string k = pqnas::shorten(it.key(), 64);
                if (it.value().is_string()) ev.f["x_" + k] = pqnas::shorten(it.value().get<std::string>(), 220);
                else if (it.value().is_number_integer() || it.value().is_number_unsigned()) ev.f["x_" + k] = it.value().dump();
                else if (it.value().is_boolean()) ev.f["x_" + k] = (it.value().get<bool>() ? "true" : "false");
                else ev.f["x_" + k] = pqnas::shorten(it.value().dump(), 220);
            }
        }

        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
        audit_append(ev);
    };

    // ---- auth (need actor fingerprint for audit) ----
    std::string actor_fp;
    if (!require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, &actor_fp)) return;
        if (!require_same_origin_for_cookie_mutation(req, res)) return;

    json body;
    try {
        body = json::parse(req.body);
    } catch (...) {
        audit_fail(actor_fp, "bad_json", 400);
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_json"}}.dump());
        return;
    }

    const std::string mount = body.value("mount", "");
    std::string name = body.value("display_name", "");

    if (mount.empty() || mount[0] != '/') {
        audit_fail(actor_fp, "bad_mount", 400, "", json{{"mount", mount}});
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mount"}, {"mount", mount}}.dump());
        return;
    }

    // Basic name hygiene (avoid absurd sizes / control chars)
    if (name.size() > 64) name.resize(64);
    for (char& c : name) {
        unsigned char uc = (unsigned char)c;
        if (uc < 32) c = ' ';
    }
    // trim-ish
    while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) name.erase(name.begin());
    while (!name.empty() && (name.back()  == ' ' || name.back()  == '\t')) name.pop_back();

    // Only allow setting name for allowed mounts (same allowlist as GET pools)
    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";
    const std::string test_prefix  = "/srv/pqnas-test";
    const std::string test_prefix2 = "/srv/pqnas-test-btrfs";

    const bool allowed =
        starts_with(mount, allowed_prefix) ||
        starts_with(mount, test_prefix) ||
        starts_with(mount, test_prefix2);

    if (!allowed) {
        audit_fail(actor_fp, "mount_not_allowed", 400, "",
                   json{{"mount", mount}, {"allowed_prefix", allowed_prefix}});
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "mount_not_allowed"},
            {"mount", mount},
            {"allowed_prefix", allowed_prefix}
        }.dump());
        return;
    }

    // Load + update + write
    json cfg = load_or_init_pools_cfg(users_path);
    if (!cfg.is_object()) cfg = json::object();
    if (!cfg.contains("version")) cfg["version"] = 1;
    if (!cfg.contains("names_by_mount") || !cfg["names_by_mount"].is_object())
        cfg["names_by_mount"] = json::object();

    const bool was_delete = name.empty();

    if (was_delete) {
        // empty name => delete key (reverts to btrfs label fallback)
        cfg["names_by_mount"].erase(mount);
    } else {
        cfg["names_by_mount"][mount] = name;
    }

    const auto cfg_path = pools_cfg_path_from_users_path(users_path);
    std::error_code ec;
    std::filesystem::create_directories(cfg_path.parent_path(), ec);

    if (!write_text_file_atomic(cfg_path.string(), cfg.dump(2) + "\n")) {
        audit_fail(actor_fp, "write_failed", 500, "",
                   json{{"mount", mount}, {"display_name", name}, {"path", cfg_path.string()}});
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "write_failed"},
            {"path", cfg_path.string()}
        }.dump());
        return;
    }

    audit_ok(actor_fp, json{
        {"mount", mount},
        {"display_name", name},
        {"op", (was_delete ? "delete" : "set")},
        {"path", cfg_path.string()}
    });

    reply_json(res, 200, json{
        {"ok", true},
        {"mount", mount},
        {"display_name", name},
        {"path", cfg_path.string()}
    }.dump());
});

// ----- POST /api/v4/storage/pools/rename (admin-only; updates PQ-NAS display name) ----
// Body: { "mount": "/srv/pqnas/data", "display_name": "My Pool", "expect_uuid": "..." }
// Safety: verifies mount is allowlisted AND (if expect_uuid provided) matches current btrfs UUID.
srv.Post("/api/v4/storage/pools/rename", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;

    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}, {"path", users_path}}.dump());
        return;
    }

    // ---- audit helpers ----
    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto audit_fail = [&](const std::string& actor_fp,
                          const std::string& reason,
                          int http,
                          const std::string& detail = "",
                          const json& extra = json::object()) {
        pqnas::AuditEvent ev;
        ev.event = "v4.storage_pools_rename_fail";
        ev.outcome = "fail";
        if (!actor_fp.empty()) ev.f["fingerprint"] = actor_fp;
        ev.f["reason"] = reason;
        ev.f["http"] = std::to_string(http);
        if (!detail.empty()) ev.f["detail"] = pqnas::shorten(detail, 220);

        if (extra.is_object()) {
            for (auto it = extra.begin(); it != extra.end(); ++it) {
                const std::string k = pqnas::shorten(it.key(), 64);
                if (it.value().is_string()) ev.f["x_" + k] = pqnas::shorten(it.value().get<std::string>(), 220);
                else if (it.value().is_number_integer() || it.value().is_number_unsigned()) ev.f["x_" + k] = it.value().dump();
                else if (it.value().is_boolean()) ev.f["x_" + k] = (it.value().get<bool>() ? "true" : "false");
                else ev.f["x_" + k] = pqnas::shorten(it.value().dump(), 220);
            }
        }

        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
        audit_append(ev);
    };

    auto audit_ok = [&](const std::string& actor_fp,
                        const json& extra = json::object()) {
        pqnas::AuditEvent ev;
        ev.event = "v4.storage_pools_rename_ok";
        ev.outcome = "ok";
        if (!actor_fp.empty()) ev.f["fingerprint"] = actor_fp;

        if (extra.is_object()) {
            for (auto it = extra.begin(); it != extra.end(); ++it) {
                const std::string k = pqnas::shorten(it.key(), 64);
                if (it.value().is_string()) ev.f["x_" + k] = pqnas::shorten(it.value().get<std::string>(), 220);
                else if (it.value().is_number_integer() || it.value().is_number_unsigned()) ev.f["x_" + k] = it.value().dump();
                else if (it.value().is_boolean()) ev.f["x_" + k] = (it.value().get<bool>() ? "true" : "false");
                else ev.f["x_" + k] = pqnas::shorten(it.value().dump(), 220);
            }
        }

        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
        audit_append(ev);
    };

    // ---- auth (need actor fingerprint for audit) ----
    std::string actor_fp;
    if (!require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, &actor_fp)) return;
        if (!require_same_origin_for_cookie_mutation(req, res)) return;

    json body;
    try {
        body = json::parse(req.body.empty() ? "{}" : req.body);
    } catch (...) {
        audit_fail(actor_fp, "bad_json", 400);
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_json"}}.dump());
        return;
    }

    const std::string mount = trim_copy(body.value("mount", ""));
    std::string display_name = body.value("display_name", "");
    const std::string expect_uuid = trim_copy(body.value("expect_uuid", ""));

    if (mount.empty() || mount[0] != '/') {
        audit_fail(actor_fp, "bad_mount", 400, "", json{{"mount", mount}});
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mount"}, {"mount", mount}}.dump());
        return;
    }

    // Normalize display name (server-side)
    display_name = trim_copy(display_name);
    if (display_name.size() > 64) display_name.resize(64);

    // Allowed prefix (storage root)
    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";
    const std::string test_prefix  = "/srv/pqnas-test";
    const std::string test_prefix2 = "/srv/pqnas-test-btrfs";

    const bool allowed =
        starts_with(mount, allowed_prefix) ||
        starts_with(mount, test_prefix) ||
        starts_with(mount, test_prefix2);

    if (!allowed) {
        audit_fail(actor_fp, "mount_not_allowed", 403, "",
                   json{{"mount", mount}, {"allowed_prefix", allowed_prefix}});
        reply_json(res, 403, json{
            {"ok", false},
            {"error", "mount_not_allowed"},
            {"mount", mount},
            {"allowed_prefix", allowed_prefix}
        }.dump());
        return;
    }

    // Resolve: ensure mount is actually a btrfs mount we can see (prevents typo mounts)
    std::string mounts_out;
    int rc = run_capture("/usr/bin/findmnt -rn -t btrfs -o TARGET", &mounts_out);
    cap_string(mounts_out, 1024 * 1024);
    rtrim_inplace(mounts_out);

    if (rc != 0) {
        audit_fail(actor_fp, "findmnt_failed", 500);
        reply_json(res, 500, json{{"ok", false}, {"error", "findmnt_failed"}}.dump());
        return;
    }

    bool found = false;
    for (const std::string& raw : split_lines(mounts_out)) {
        const std::string t = trim_copy(raw);
        if (t == mount) { found = true; break; }
    }
    if (!found) {
        audit_fail(actor_fp, "mount_not_found", 404, "", json{{"mount", mount}});
        reply_json(res, 404, json{{"ok", false}, {"error", "mount_not_found"}, {"mount", mount}}.dump());
        return;
    }

    // If client provided expect_uuid, verify it matches current UUID (hard safety guard)
    if (!expect_uuid.empty()) {
        // pick btrfs binary (same helper you used above in /storage/pools)
        const char* BTRFS1 = "/usr/bin/btrfs";
        const char* BTRFS2 = "/usr/sbin/btrfs";

        auto exists_exec = [](const char* p) -> bool {
            std::error_code ec;
            auto st = std::filesystem::status(p, ec);
            if (ec) return false;
            if (!std::filesystem::is_regular_file(st)) return false;
            auto perms = st.permissions();
            using P = std::filesystem::perms;
            return (perms & P::owner_exec) != P::none ||
                   (perms & P::group_exec) != P::none ||
                   (perms & P::others_exec) != P::none;
        };
        const char* BTRFS = exists_exec(BTRFS1) ? BTRFS1 : (exists_exec(BTRFS2) ? BTRFS2 : BTRFS1);

        std::string show_out;
        int rc_show = run_capture(std::string("/usr/bin/sudo -n ") + BTRFS + " filesystem show " + sh_quote(mount) + " 2>&1", &show_out);
        cap_string(show_out, 256 * 1024);
        rtrim_inplace(show_out);

        if (rc_show != 0) {
            audit_fail(actor_fp, "btrfs_show_failed", 500, "",
                       json{{"mount", mount}});
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "btrfs_show_failed"},
                {"mount", mount}
            }.dump());
            return;
        }

        std::string label, uuid;
        int devices = -1;
        parse_btrfs_filesystem_show(show_out, &label, &uuid, &devices);

        if (uuid.empty() || uuid != expect_uuid) {
            audit_fail(actor_fp, "uuid_mismatch", 409, "",
                       json{{"mount", mount}, {"expect_uuid", expect_uuid}, {"actual_uuid", uuid}});
            reply_json(res, 409, json{
                {"ok", false},
                {"error", "uuid_mismatch"},
                {"mount", mount},
                {"expect_uuid", expect_uuid},
                {"actual_uuid", uuid}
            }.dump());
            return;
        }
    }

    // Load + update pools.json (display names) and write atomically
    const json cfg0 = load_or_init_pools_cfg(users_path);
    json cfg = cfg0;

    if (!cfg.is_object()) cfg = json::object();
    if (!cfg.contains("version")) cfg["version"] = 1;
    if (!cfg.contains("names_by_mount") || !cfg["names_by_mount"].is_object()) cfg["names_by_mount"] = json::object();

    const bool removed = display_name.empty();

    if (removed) {
        // empty => remove override
        cfg["names_by_mount"].erase(mount);
    } else {
        cfg["names_by_mount"][mount] = display_name;
    }

    const auto cfg_path = pools_cfg_path_from_users_path(users_path);
    std::error_code ec;
    std::filesystem::create_directories(cfg_path.parent_path(), ec);

    const bool ok_write = write_text_file_atomic(cfg_path.string(), cfg.dump(2) + "\n");
    if (!ok_write) {
        audit_fail(actor_fp, "write_failed", 500, "",
                   json{{"path", cfg_path.string()}, {"mount", mount}, {"display_name", display_name}});
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "write_failed"},
            {"path", cfg_path.string()}
        }.dump());
        return;
    }

    audit_ok(actor_fp, json{
        {"mount", mount},
        {"display_name", display_name},
        {"removed", removed},
        {"expect_uuid", expect_uuid} // may be ""
    });

    reply_json(res, 200, json{
        {"ok", true},
        {"mount", mount},
        {"display_name", display_name},
        {"removed", removed}
    }.dump());
});
// ----- POST /api/v4/poolmgr/add-slot (admin-only, metadata only) -----------
srv.Post("/api/v4/poolmgr/add-slot", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;
    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}}.dump());
        return;
    }
    if (!require_admin_cookie_users(req, res, COOKIE_KEY, users_path, &users)) return;
        if (!require_same_origin_for_cookie_mutation(req, res)) return;

    json in;
    try { in = json::parse(req.body.empty() ? "{}" : req.body); }
    catch (...) {
        reply_json(res, 400, json{{"ok", false}, {"error", "invalid_json"}}.dump());
        return;
    }

    const std::string mount = in.value("mount", "");
    if (mount.empty() || !is_abs_path_safe(mount)) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mount"}}.dump());
        return;
    }

    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";
    const std::string pools_root = allowed_prefix + "/pools/";

    if (!starts_with(mount, pools_root)) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "mount_not_editable_pool"},
            {"mount", mount}
        }.dump());
        return;
    }

    json cfg = pqnas::load_or_init_pools_cfg_v3(users_path);

    if (!cfg.contains("pools") || !cfg["pools"].is_object()) {
        reply_json(res, 500, json{{"ok", false}, {"error", "bad_pools_cfg"}}.dump());
        return;
    }

    auto it = cfg["pools"].find(mount);
    if (it == cfg["pools"].end() || !it->is_object()) {
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "pool_not_found"},
            {"mount", mount}
        }.dump());
        return;
    }

    json& pool = *it;
    pqnas::normalize_pool_entry_v3(&pool);

    if (!pool.contains("slots") || !pool["slots"].is_array()) {
        pool["slots"] = json::array();
    }

    const int next_index = static_cast<int>(pool["slots"].size());
    pool["slots"].push_back(json{
        {"index", next_index},
        {"device", nullptr}
    });
    pool["slot_count"] = static_cast<int>(pool["slots"].size());

    pqnas::enrich_pool_slots_with_runtime_identity_v3(&pool);


    std::string err;
    if (!pqnas::write_pools_cfg_v3(users_path, cfg, &err)) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "write_failed"},
            {"detail", err}
        }.dump());
        return;
    }

    reply_json(res, 200, json{
        {"ok", true},
        {"mount", mount},
        {"slot_count", pool.value("slot_count", 0)},
        {"slots", pool["slots"]}
    }.dump());
});

// ----- POST /api/v4/poolmgr/remove-slot (admin-only, metadata only) --------
srv.Post("/api/v4/poolmgr/remove-slot", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;
    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}}.dump());
        return;
    }
    if (!require_admin_cookie_users(req, res, COOKIE_KEY, users_path, &users)) return;
        if (!require_same_origin_for_cookie_mutation(req, res)) return;

    json in;
    try { in = json::parse(req.body.empty() ? "{}" : req.body); }
    catch (...) {
        reply_json(res, 400, json{{"ok", false}, {"error", "invalid_json"}}.dump());
        return;
    }

    const std::string mount = in.value("mount", "");
    if (mount.empty() || !is_abs_path_safe(mount)) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mount"}}.dump());
        return;
    }

    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";
    const std::string pools_root = allowed_prefix + "/pools/";

    if (!starts_with(mount, pools_root)) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "mount_not_editable_pool"},
            {"mount", mount}
        }.dump());
        return;
    }

    json cfg = pqnas::load_or_init_pools_cfg_v3(users_path);

    if (!cfg.contains("pools") || !cfg["pools"].is_object()) {
        reply_json(res, 500, json{{"ok", false}, {"error", "bad_pools_cfg"}}.dump());
        return;
    }

    auto it = cfg["pools"].find(mount);
    if (it == cfg["pools"].end() || !it->is_object()) {
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "pool_not_found"},
            {"mount", mount}
        }.dump());
        return;
    }

    json& pool = *it;
    pqnas::normalize_pool_entry_v3(&pool);

    if (!pool.contains("slots") || !pool["slots"].is_array() || pool["slots"].empty()) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "no_slots"}
        }.dump());
        return;
    }

    if (pool["slots"].size() <= 1) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "cannot_remove_last_slot"}
        }.dump());
        return;
    }

    const json& last = pool["slots"].back();
    const bool assigned = last.contains("device") && last["device"].is_string() &&
                          !last["device"].get<std::string>().empty();

    if (assigned) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "last_slot_not_empty"},
            {"message", "Only an empty trailing slot can be removed."}
        }.dump());
        return;
    }

    pool["slots"].erase(pool["slots"].end() - 1);
    pool["slot_count"] = static_cast<int>(pool["slots"].size());

    // reindex defensively
    for (size_t i = 0; i < pool["slots"].size(); ++i) {
        pool["slots"][i]["index"] = static_cast<int>(i);
    }

    pqnas::enrich_pool_slots_with_runtime_identity_v3(&pool);


    std::string err;
    if (!pqnas::write_pools_cfg_v3(users_path, cfg, &err)) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "write_failed"},
            {"detail", err}
        }.dump());
        return;
    }

    reply_json(res, 200, json{
        {"ok", true},
        {"mount", mount},
        {"slot_count", pool.value("slot_count", 0)},
        {"slots", pool["slots"]}
    }.dump());
});

// ----- POST /api/v4/poolmgr/set-layout (admin-only, metadata only) ----------
srv.Post("/api/v4/poolmgr/set-layout", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;
    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}}.dump());
        return;
    }
    if (!require_admin_cookie_users(req, res, COOKIE_KEY, users_path, &users)) return;
        if (!require_same_origin_for_cookie_mutation(req, res)) return;

    json in;
    try { in = json::parse(req.body.empty() ? "{}" : req.body); }
    catch (...) {
        reply_json(res, 400, json{{"ok", false}, {"error", "invalid_json"}}.dump());
        return;
    }

    const std::string mount = in.value("mount", "");
    if (mount.empty() || !is_abs_path_safe(mount)) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mount"}}.dump());
        return;
    }

    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";
    const std::string pools_root = allowed_prefix + "/pools/";

    if (!starts_with(mount, pools_root)) {
        reply_json(res, 400, json{{"ok", false}, {"error", "mount_not_editable_pool"}, {"mount", mount}}.dump());
        return;
    }

    const std::string display_name = in.value("display_name", "");
    const std::string mode = in.value("mode", "single");
    const int slot_count_in = in.value("slot_count", 0);
    const json slots_in = in.value("slots", json::array());

    if (mode != "single" && mode != "raid1") {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mode"}}.dump());
        return;
    }
    if (!slots_in.is_array()) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_slots"}}.dump());
        return;
    }

    std::set<std::string> seen;
    json norm_slots = json::array();

    for (size_t i = 0; i < slots_in.size(); ++i) {
        const auto& s = slots_in[i];
        std::string dev;
        if (s.is_object() && s.contains("device") && s["device"].is_string()) {
            dev = trim_copy(s["device"].get<std::string>());
        }

        if (!dev.empty()) {
            if (!is_dev_path_basic_safe(dev)) {
                reply_json(res, 400, json{{"ok", false}, {"error", "bad_device"}, {"device", dev}}.dump());
                return;
            }
            if (!seen.insert(dev).second) {
                reply_json(res, 400, json{{"ok", false}, {"error", "duplicate_device"}, {"device", dev}}.dump());
                return;
            }
        }

        norm_slots.push_back(json{
            {"index", static_cast<int>(i)},
            {"device", dev.empty() ? json(nullptr) : json(dev)}
        });
    }

    int slot_count = slot_count_in > 0 ? slot_count_in : static_cast<int>(norm_slots.size());
    if (slot_count < static_cast<int>(norm_slots.size())) {
        slot_count = static_cast<int>(norm_slots.size());
    }
    if (slot_count < 1) slot_count = 1;

    while (static_cast<int>(norm_slots.size()) < slot_count) {
        norm_slots.push_back(json{
            {"index", static_cast<int>(norm_slots.size())},
            {"device", nullptr}
        });
    }

    json cfg = pqnas::load_or_init_pools_cfg_v3(users_path);
    if (!cfg.contains("pools") || !cfg["pools"].is_object()) {
        reply_json(res, 500, json{{"ok", false}, {"error", "bad_pools_cfg"}}.dump());
        return;
    }

    auto it = cfg["pools"].find(mount);
    if (it == cfg["pools"].end() || !it->is_object()) {
        reply_json(res, 404, json{{"ok", false}, {"error", "pool_not_found"}, {"mount", mount}}.dump());
        return;
    }

    json& pool = *it;
    pqnas::normalize_pool_entry_v3(&pool);

    pool["mount"] = mount;
    pool["mode"] = mode;
    pool["slot_count"] = slot_count;
    pool["slots"] = norm_slots;
    if (!display_name.empty()) {
        pool["display_name"] = display_name;
        cfg["names_by_mount"][mount] = display_name;
    }

    std::string err;
    pqnas::enrich_pool_slots_with_runtime_identity_v3(&cfg["pools"][mount]);

    if (!pqnas::write_pools_cfg_v3(users_path, cfg, &err)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "write_failed"}, {"detail", err}}.dump());
        return;
    }

    reply_json(res, 200, json{
        {"ok", true},
        {"mount", mount},
        {"mode", pool["mode"]},
        {"slot_count", pool["slot_count"]},
        {"slots", pool["slots"]}
    }.dump());
});

// ----- POST /api/v4/poolmgr/plan-layout (admin-only) ------------------------
srv.Post("/api/v4/poolmgr/plan-layout", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;
    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}}.dump());
        return;
    }
    if (!require_admin_cookie_users(req, res, COOKIE_KEY, users_path, &users)) return;
        if (!require_same_origin_for_cookie_mutation(req, res)) return;

    json in;
    try { in = json::parse(req.body.empty() ? "{}" : req.body); }
    catch (...) {
        reply_json(res, 400, json{{"ok", false}, {"error", "invalid_json"}}.dump());
        return;
    }

    const std::string mount = in.value("mount", "");
    if (mount.empty() || !is_abs_path_safe(mount)) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mount"}}.dump());
        return;
    }

    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";
    const std::string pools_root = allowed_prefix + "/pools/";

    if (!starts_with(mount, pools_root)) {
        reply_json(res, 400, json{{"ok", false}, {"error", "mount_not_editable_pool"}, {"mount", mount}}.dump());
        return;
    }

    json cfg = pqnas::load_or_init_pools_cfg_v3(users_path);
    if (!cfg.contains("pools") || !cfg["pools"].is_object()) {
        reply_json(res, 500, json{{"ok", false}, {"error", "bad_pools_cfg"}}.dump());
        return;
    }

    auto it = cfg["pools"].find(mount);
    if (it == cfg["pools"].end() || !it->is_object()) {
        reply_json(res, 404, json{{"ok", false}, {"error", "pool_not_found"}, {"mount", mount}}.dump());
        return;
    }

    json cfg_pool = *it;
    cfg_pool["mount"] = mount;
    pqnas::normalize_pool_entry_v3(&cfg_pool);

    // Runtime state from current merged route logic
    std::string source_out, fstype_out;
    int ec_src = 0, ec_fs = 0;

    run_cmd_capture("/usr/bin/findmnt -no SOURCE --target " + sh_quote(mount), &source_out, &ec_src);
    cap_string(source_out, 4096);
    rtrim_inplace(source_out);

    run_cmd_capture("/usr/bin/findmnt -no FSTYPE --target " + sh_quote(mount), &fstype_out, &ec_fs);
    cap_string(fstype_out, 4096);
    rtrim_inplace(fstype_out);

    if (ec_fs != 0 || fstype_out != "btrfs") {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "not_btrfs"},
            {"mount", mount},
            {"fstype", fstype_out}
        }.dump());
        return;
    }

    std::string raw_lsblk;
    json disks_j = storage_list_disks_json(&raw_lsblk);
    json by_path = disks_j.value("by_path", json::object());
    json by_name = disks_j.value("by_name", json::object());

    std::string show_out;
    int rc_show = run_capture("/usr/bin/sudo -n /usr/bin/btrfs filesystem show " + sh_quote(mount) + " 2>&1", &show_out);
    cap_string(show_out, 256 * 1024);
    rtrim_inplace(show_out);

    if (rc_show != 0 || show_out.empty()) {
        reply_json(res, 500, json{{"ok", false}, {"error", "btrfs_show_failed"}, {"mount", mount}}.dump());
        return;
    }

    BtrfsShowParsed parsed_show = parse_btrfs_filesystem_show(show_out);
    json show_j = btrfs_show_parsed_to_json(parsed_show, by_path, by_name);
    const std::vector<std::string> runtime_members =
        pqnas::runtime_member_parent_disks_from_show_json(show_j);

    std::set<std::string> desired_set;
    if (cfg_pool.contains("slots") && cfg_pool["slots"].is_array()) {
        for (const auto& s : cfg_pool["slots"]) {
            if (s.is_object() && s.contains("device") && s["device"].is_string()) {
                const std::string d = trim_copy(s["device"].get<std::string>());
                if (!d.empty()) desired_set.insert(d);
            }
        }
    }

    std::set<std::string> runtime_set(runtime_members.begin(), runtime_members.end());

    std::vector<std::string> to_add;
    std::vector<std::string> to_remove;

    for (const auto& d : desired_set) {
        if (runtime_set.find(d) == runtime_set.end()) to_add.push_back(d);
    }
    for (const auto& d : runtime_set) {
        if (desired_set.find(d) == desired_set.end()) to_remove.push_back(d);
    }

    json warnings = json::array();
    json ops = json::array();

    if (to_add.size() > 1 || to_remove.size() > 1 || (to_add.size() + to_remove.size()) > 1) {
        warnings.push_back("multiple_changes_not_supported_yet");
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "multiple_changes_not_supported_yet"},
            {"mount", mount},
            {"to_add", to_add},
            {"to_remove", to_remove},
            {"warnings", warnings}
        }.dump());
        return;
    }

    if (to_add.size() == 1) {
        ops.push_back(json{
            {"type", "add-device"},
            {"disk", to_add[0]},
            {"mode", cfg_pool.value("mode", "single")}
        });
    }

    if (to_remove.size() == 1) {
        ops.push_back(json{
            {"type", "remove-device"},
            {"disk", to_remove[0]}
        });
    }

    const bool busy = [&]() {
        const std::string lockp = raid_mount_lock_path(mount);
        std::error_code ec;
        return std::filesystem::exists(lockp, ec) && !ec;
    }();

    reply_json(res, 200, json{
        {"ok", true},
        {"mount", mount},
        {"busy", busy},
        {"desired_members", desired_set},
        {"runtime_members", runtime_members},
        {"to_add", to_add},
        {"to_remove", to_remove},
        {"ops", ops},
        {"warnings", warnings},
        {"layout_drift", desired_set != runtime_set}
    }.dump());
});

// ----- POST /api/v4/poolmgr/apply-layout (admin-only) -----------------------
srv.Post("/api/v4/poolmgr/apply-layout", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;
    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}}.dump());
        return;
    }

    std::string actor_fp;
    if (!require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, &actor_fp)) return;
        if (!require_same_origin_for_cookie_mutation(req, res)) return;

    json in;
    try { in = json::parse(req.body.empty() ? "{}" : req.body); }
    catch (...) {
        reply_json(res, 400, json{{"ok", false}, {"error", "invalid_json"}}.dump());
        return;
    }

    const std::string mount = in.value("mount", "");
    const bool confirm = in.value("confirm", false);

    if (mount.empty() || !is_abs_path_safe(mount)) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mount"}}.dump());
        return;
    }
    if (!confirm) {
        reply_json(res, 400, json{{"ok", false}, {"error", "confirm_required"}}.dump());
        return;
    }

    // Re-run same planning logic inline (keeps this self-contained)
    json cfg = pqnas::load_or_init_pools_cfg_v3(users_path);
    if (!cfg.contains("pools") || !cfg["pools"].is_object()) {
        reply_json(res, 500, json{{"ok", false}, {"error", "bad_pools_cfg"}}.dump());
        return;
    }

    auto it = cfg["pools"].find(mount);
    if (it == cfg["pools"].end() || !it->is_object()) {
        reply_json(res, 404, json{{"ok", false}, {"error", "pool_not_found"}, {"mount", mount}}.dump());
        return;
    }

    json cfg_pool = *it;
    cfg_pool["mount"] = mount;
    pqnas::normalize_pool_entry_v3(&cfg_pool);

    std::string raw_lsblk;
    json disks_j = storage_list_disks_json(&raw_lsblk);
    json by_path = disks_j.value("by_path", json::object());
    json by_name = disks_j.value("by_name", json::object());

    std::string show_out;
    int rc_show = run_capture("/usr/bin/sudo -n /usr/bin/btrfs filesystem show " + sh_quote(mount) + " 2>&1", &show_out);
    cap_string(show_out, 256 * 1024);
    rtrim_inplace(show_out);

    if (rc_show != 0 || show_out.empty()) {
        reply_json(res, 500, json{{"ok", false}, {"error", "btrfs_show_failed"}, {"mount", mount}}.dump());
        return;
    }

    BtrfsShowParsed parsed_show = parse_btrfs_filesystem_show(show_out);
    json show_j = btrfs_show_parsed_to_json(parsed_show, by_path, by_name);
    const std::vector<std::string> runtime_members =
        pqnas::runtime_member_parent_disks_from_show_json(show_j);

    std::set<std::string> desired_set;
    if (cfg_pool.contains("slots") && cfg_pool["slots"].is_array()) {
        for (const auto& s : cfg_pool["slots"]) {
            if (s.is_object() && s.contains("device") && s["device"].is_string()) {
                const std::string d = trim_copy(s["device"].get<std::string>());
                if (!d.empty()) desired_set.insert(d);
            }
        }
    }

    std::set<std::string> runtime_set(runtime_members.begin(), runtime_members.end());

    std::vector<std::string> to_add;
    std::vector<std::string> to_remove;

    for (const auto& d : desired_set) {
        if (runtime_set.find(d) == runtime_set.end()) to_add.push_back(d);
    }
    for (const auto& d : runtime_set) {
        if (desired_set.find(d) == desired_set.end()) to_remove.push_back(d);
    }

    if (to_add.size() > 1 || to_remove.size() > 1 || (to_add.size() + to_remove.size()) > 1) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "multiple_changes_not_supported_yet"},
            {"mount", mount},
            {"to_add", to_add},
            {"to_remove", to_remove}
        }.dump());
        return;
    }

    if (to_add.empty() && to_remove.empty()) {
        reply_json(res, 200, json{
            {"ok", true},
            {"mount", mount},
            {"skipped", true},
            {"skip_reason", "no_layout_changes"}
        }.dump());
        return;
    }

    if (to_add.size() == 1) {
        const std::string disk = to_add[0];
        const std::string mode = cfg_pool.value("mode", "single");

        // Build add-device plan
        json plan_in = {
            {"mount", mount},
            {"new_disk", disk},
            {"mode", mode},
            {"force", false}
        };

        httplib::Request fake_req = req;
        httplib::Response fake_res;

        // Reuse over HTTP internally is messy, so do the simpler path:
        // ask client to use current backend endpoints through one response.
        const std::string plan_nonce = rand_hex_16();
        // We need the real plan_id from current plan endpoint, so compute by making the same HTTP-visible plan not here.
        reply_json(res, 200, json{
            {"ok", true},
            {"mount", mount},
            {"next_action", "add-device"},
            {"disk", disk},
            {"mode", mode},
            {"plan_nonce", plan_nonce}
        }.dump());
        return;
    }

    if (to_remove.size() == 1) {
        reply_json(res, 200, json{
            {"ok", true},
            {"mount", mount},
            {"next_action", "remove-device"},
            {"disk", to_remove[0]}
        }.dump());
        return;
    }

    reply_json(res, 500, json{{"ok", false}, {"error", "unexpected_state"}}.dump());
});

// ----- GET /api/v4/storage/overview?mount=/path (admin-only) -----------------
srv.Get("/api/v4/storage/overview", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;

    if (!users.load(users_path)) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "users_load_failed"},
            {"path", users_path}
        }.dump());
        return;
    }

    if (!require_admin_cookie_users(req, res, COOKIE_KEY, users_path, &users)) return;

    // -------------------- disks (always returned) --------------------
    std::string raw_lsblk;
    json disks_j = storage_list_disks_json(&raw_lsblk);

    // -------------------- mount selection --------------------
    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";

    std::string mount = allowed_prefix + "/data";
    if (req.has_param("mount")) mount = req.get_param_value("mount");

    json out;
    out["ok"] = false;  // becomes true only if valid btrfs status included

    out["input_mount"] = mount;
    out["allowed_prefix"] = allowed_prefix;

    // always include disks and index maps
    out["disks"]   = disks_j.value("disks", json::array());
    out["by_path"] = disks_j.value("by_path", json::object());
    out["by_name"] = disks_j.value("by_name", json::object());

    // Optional debug raw lsblk at top level
    if (getenv_bool("PQNAS_STORAGE_DEBUG_LSBLK", false)) {
        cap_string(raw_lsblk, 1024 * 1024);
        out["lsblk_raw"] = raw_lsblk;
    }

    // -------------------- input validation --------------------
    if (!is_abs_path_safe(mount)) {
        out["error"] = "bad_mount";
        reply_json(res, 400, out.dump());  // keep 400 for invalid path
        return;
    }

    // -------------------- resolve mountpoint, fstype, source --------------------
    std::string target_out, fstype_out, source_out;

    int rc_target = run_capture(
        "/usr/bin/findmnt -no TARGET --target " + sh_quote(mount),
        &target_out
    );
    cap_string(target_out, 16 * 1024);
    rtrim_inplace(target_out);

    int rc_fs = run_capture(
        "/usr/bin/findmnt -no FSTYPE --target " + sh_quote(mount),
        &fstype_out
    );
    cap_string(fstype_out, 16 * 1024);
    rtrim_inplace(fstype_out);

    int rc_src = run_capture(
        "/usr/bin/findmnt -no SOURCE --target " + sh_quote(mount),
        &source_out
    );
    cap_string(source_out, 16 * 1024);
    rtrim_inplace(source_out);

    if (rc_target != 0 || target_out.empty() ||
        rc_fs != 0 || fstype_out.empty() ||
        rc_src != 0 || source_out.empty()) {

        out["error"] = "mount_not_found";
        reply_json(res, 200, out.dump());  // overview still useful
        return;
    }

    const std::string resolved_mount  = target_out;
    const std::string resolved_source = source_out;
    const std::string resolved_disk   = parent_disk_from_dev(resolved_source);

    out["resolved_mount"]  = resolved_mount;
    out["resolved_source"] = resolved_source;
    out["resolved_disk"]   = resolved_disk;
    out["fstype"]          = fstype_out;

    // -------------------- allowlist enforcement --------------------
    if (resolved_mount.rfind(allowed_prefix, 0) != 0) {
        const std::string test_prefix  = "/srv/pqnas-test";
        const std::string test_prefix2 = "/srv/pqnas-test-btrfs";

        if (resolved_mount.rfind(test_prefix, 0) != 0 &&
            resolved_mount.rfind(test_prefix2, 0) != 0) {

            out["error"] = "mount_not_allowed";
            reply_json(res, 200, out.dump());  // still return disks
            return;
        }
    }

    // -------------------- non-btrfs case --------------------
    if (fstype_out != "btrfs") {
        out["error"] = "not_btrfs";
        reply_json(res, 200, out.dump());  // overview still useful
        return;
    }

    // -------------------- btrfs status --------------------
    json status = storage_btrfs_status_json(resolved_mount);

    status["input_mount"]     = mount;
    status["resolved_mount"]  = resolved_mount;
    status["resolved_source"] = resolved_source;
    status["resolved_disk"]   = resolved_disk;
    status["fstype"]          = fstype_out;

    out["ok"]     = true;
    out["status"] = status;

    reply_json(res, 200, out.dump());
});


// ----- GET /api/v4/raid/exec-record?plan_id=<sha256hex>[&full=1] (admin-only) -----
// Default returns a polling-friendly summary. Add full=1 to return full record (including results[]).
srv.Get("/api/v4/raid/exec-record", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;

    if (!users.load(users_path)) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "users_load_failed"},
            {"path", users_path}
        }.dump());
        return;
    }
    if (!require_admin_cookie_users(req, res, COOKIE_KEY, users_path, &users)) return;

    const std::string plan_id = req.has_param("plan_id") ? req.get_param_value("plan_id") : "";
    const bool full = req.has_param("full") && req.get_param_value("full") == "1";

    if (plan_id.empty()) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "missing plan_id"}
        }.dump());
        return;
    }
    if (!is_sha256_hex_lower(plan_id)) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "plan_id must be 64 lowercase hex chars"}
        }.dump());
        return;
    }

    json rec;
    std::string err;
    if (!raid_exec_record_read(plan_id, &rec, &err)) {
        reply_json(res, 200, json{
            {"ok", false},
            {"error", err.empty() ? "record_not_found" : err},
            {"plan_id", plan_id}
        }.dump());
        return;
    }

    // Always include these fields for UI
    json out = json::object();
    out["ok"]       = true;
    out["plan_id"]  = plan_id;
    out["state"]    = rec.value("state", "unknown");
    out["busy"]     = rec.value("busy", false);
    out["step_index"] = rec.value("step_index", 0);
    out["step_total"] = rec.value("step_total", 0);
    out["ts_start"] = rec.value("ts_start", "");
    out["ts_last"]  = rec.value("ts_last", "");
    out["ts_end"] = (rec.contains("ts_end") ? rec["ts_end"] : json(nullptr));


    // Include plan always (small enough / useful)
    if (rec.contains("plan")) out["plan"] = rec["plan"];

    if (full) {
        // Full payload for debugging
        if (rec.contains("results")) out["results"] = rec["results"];
        if (rec.contains("post_status")) out["post_status"] = rec["post_status"];
        if (rec.contains("error")) out["error"] = rec["error"];
    }

    reply_json(res, 200, out.dump());
});

// ----- GET /api/v4/raid/discovery?mount=/path (admin-only, read-only) --------
srv.Get("/api/v4/raid/discovery", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;

    if (!users.load(users_path)) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "users_load_failed"},
            {"path", users_path}
        }.dump());
        return;
    }

    if (!require_admin_cookie_users(req, res, COOKIE_KEY, users_path, &users)) return;

    // -------------------- disks (always returned) --------------------
    std::string raw_lsblk;
    json disks_j = storage_list_disks_json(&raw_lsblk);

    // -------------------- mount selection --------------------
    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";

    std::string mount = allowed_prefix + "/data";
    if (req.has_param("mount")) mount = req.get_param_value("mount");

    json out;
    out["ok"] = false;

    out["input_mount"] = mount;
    out["allowed_prefix"] = allowed_prefix;

    out["disks"]   = disks_j.value("disks", json::array());
    out["by_path"] = disks_j.value("by_path", json::object());
    out["by_name"] = disks_j.value("by_name", json::object());

    // Optional debug raw lsblk at top level
    if (getenv_bool("PQNAS_STORAGE_DEBUG_LSBLK", false)) {
        cap_string(raw_lsblk, 1024 * 1024);
        out["lsblk_raw"] = raw_lsblk;
    }

    // -------------------- input validation --------------------
    if (!is_abs_path_safe(mount)) {
        out["error"] = "bad_mount";
        reply_json(res, 400, out.dump());
        return;
    }

    // -------------------- resolve mountpoint, fstype, source --------------------
    std::string target_out, fstype_out, source_out;
    int ec_target = 0, ec_fs = 0, ec_src = 0;

    const bool ok_target = run_cmd_capture(
        "/usr/bin/findmnt -no TARGET --target " + sh_quote(mount),
        &target_out, &ec_target
    );
    cap_string(target_out, 16 * 1024);
    rtrim_inplace(target_out);

    const bool ok_fs = run_cmd_capture(
        "/usr/bin/findmnt -no FSTYPE --target " + sh_quote(mount),
        &fstype_out, &ec_fs
    );
    cap_string(fstype_out, 16 * 1024);
    rtrim_inplace(fstype_out);

    const bool ok_src = run_cmd_capture(
        "/usr/bin/findmnt -no SOURCE --target " + sh_quote(mount),
        &source_out, &ec_src
    );
    cap_string(source_out, 16 * 1024);
    rtrim_inplace(source_out);

    if (!ok_target || ec_target != 0 || target_out.empty() ||
        !ok_fs     || ec_fs     != 0 || fstype_out.empty() ||
        !ok_src    || ec_src    != 0 || source_out.empty()) {

        out["error"] = "mount_not_found";
        reply_json(res, 200, out.dump());
        return;
    }

    const std::string resolved_mount  = target_out;
    const std::string resolved_source = source_out;
    const std::string resolved_disk   = parent_disk_from_dev(resolved_source);

    out["resolved_mount"]  = resolved_mount;
    out["resolved_source"] = resolved_source;
    if (!resolved_disk.empty()) out["resolved_disk"] = resolved_disk;
    out["fstype"]          = fstype_out;

    // -------------------- allowlist enforcement --------------------
    if (resolved_mount.rfind(allowed_prefix, 0) != 0) {
        const std::string test_prefix  = "/srv/pqnas-test";
        const std::string test_prefix2 = "/srv/pqnas-test-btrfs";

        if (resolved_mount.rfind(test_prefix, 0) != 0 &&
            resolved_mount.rfind(test_prefix2, 0) != 0) {

            out["error"] = "mount_not_allowed";
            reply_json(res, 200, out.dump());
            return;
        }
    }

    // -------------------- non-btrfs case --------------------
    if (fstype_out != "btrfs") {
        out["error"] = "not_btrfs";
        reply_json(res, 200, out.dump());
        return;
    }

    // -------------------- btrfs filesystem show (read-only) --------------------
    const std::string cmd_show =
        "/usr/bin/sudo -n /usr/bin/btrfs filesystem show " + sh_quote(resolved_mount);

    std::string show_raw;
    int ec_show = 0;

    // NOTE: stderr capture is now inside run_cmd_capture(); do NOT add "2>&1" here.
    const bool ok_show = run_cmd_capture(cmd_show, &show_raw, &ec_show);

    cap_string(show_raw, 256 * 1024);

    if (!ok_show || ec_show != 0 || show_raw.empty()) {
        out["error"] = "btrfs_show_failed";
        out["btrfs_show_rc"] = ec_show;

        if (getenv_bool("PQNAS_RAID_DEBUG_SHOW", false)) {
            cap_string(show_raw, 1024 * 1024);
            out["btrfs_show_raw"] = show_raw;
        }

        reply_json(res, 200, out.dump());
        return;
    }

    BtrfsShowParsed parsed = parse_btrfs_filesystem_show(show_raw);

    json by_path = out.value("by_path", json::object());
    json by_name = out.value("by_name", json::object());

    json btrfs_j = btrfs_show_parsed_to_json(parsed, by_path, by_name);

    // Build device_to_disk_map (best-effort)
    json map_j = json::object();
    if (btrfs_j.contains("devices") && btrfs_j["devices"].is_array()) {
        for (const auto& dev : btrfs_j["devices"]) {
            if (!dev.is_object()) continue;
            const std::string p = dev.value("path", "");
            if (p.empty()) continue;

            json m;
            const std::string parent = dev.value("parent_disk", "");
            if (!parent.empty()) m["parent_disk"] = parent;

            if (dev.contains("lsblk_disk_index") && dev["lsblk_disk_index"].is_number_integer()) {
                m["disk_index"] = dev["lsblk_disk_index"];

                // Add disk_name as a convenience (from parent basename)
                if (!parent.empty()) {
                    std::string name = parent;
                    const size_t slash = name.rfind('/');
                    if (slash != std::string::npos) name = name.substr(slash + 1);
                    if (!name.empty()) m["disk_name"] = name;
                }
            }

            map_j[p] = m;
        }
    }

    out["ok"] = true;
    out["btrfs"] = btrfs_j;
    out["device_to_disk_map"] = map_j;

    if (getenv_bool("PQNAS_RAID_DEBUG_SHOW", false)) {
        cap_string(show_raw, 1024 * 1024);
        out["btrfs_show_raw"] = show_raw;
        out["btrfs_show_rc"]  = ec_show;
    }

    reply_json(res, 200, out.dump());
});

// ----- GET /api/v4/raid/balance-status?mount=/path (admin-only, read-only) ----
// Runs: btrfs balance status <mount>
// Returns: { ok, resolved_mount, running, status_raw, rc, ... }
srv.Get("/api/v4/raid/balance-status", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;

    if (!users.load(users_path)) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "users_load_failed"},
            {"path", users_path}
        }.dump());
        return;
    }
    if (!require_admin_cookie_users(req, res, COOKIE_KEY, users_path, &users)) return;

    // Allowed_prefix + default mount
    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";

    std::string mount = allowed_prefix + "/data";
    if (req.has_param("mount")) mount = req.get_param_value("mount");

    if (!is_abs_path_safe(mount)) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_mount"},
            {"mount", mount}
        }.dump());
        return;
    }

    // Resolve mount -> resolved_mount / source / fstype
    std::string target_out, fstype_out, source_out;
    int ec_target = 0, ec_fs = 0, ec_src = 0;

    const bool ok_target = run_cmd_capture(
        "/usr/bin/findmnt -no TARGET --target " + sh_quote(mount), &target_out, &ec_target);
    cap_string(target_out, 16 * 1024);
    rtrim_inplace(target_out);

    const bool ok_fs = run_cmd_capture(
        "/usr/bin/findmnt -no FSTYPE --target " + sh_quote(mount), &fstype_out, &ec_fs);
    cap_string(fstype_out, 16 * 1024);
    rtrim_inplace(fstype_out);

    const bool ok_src = run_cmd_capture(
        "/usr/bin/findmnt -no SOURCE --target " + sh_quote(mount), &source_out, &ec_src);
    cap_string(source_out, 16 * 1024);
    rtrim_inplace(source_out);

    if (!ok_target || ec_target != 0 || target_out.empty() ||
        !ok_fs     || ec_fs     != 0 || fstype_out.empty() ||
        !ok_src    || ec_src    != 0 || source_out.empty()) {

        reply_json(res, 200, json{
            {"ok", false},
            {"error", "mount_not_found"},
            {"mount", mount}
        }.dump());
        return;
    }

    const std::string resolved_mount  = target_out;
    const std::string resolved_source = source_out;
    const std::string resolved_disk   = parent_disk_from_dev(resolved_source);

    // Allowlist on resolved mount
    if (resolved_mount.rfind(allowed_prefix, 0) != 0) {
        const std::string test_prefix  = "/srv/pqnas-test";
        const std::string test_prefix2 = "/srv/pqnas-test-btrfs";
        if (resolved_mount.rfind(test_prefix, 0) != 0 &&
            resolved_mount.rfind(test_prefix2, 0) != 0) {
            reply_json(res, 200, json{
                {"ok", false},
                {"error", "mount_not_allowed"},
                {"allowed_prefix", allowed_prefix},
                {"resolved_mount", resolved_mount}
            }.dump());
            return;
        }
    }

    if (fstype_out != "btrfs") {
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "not_btrfs"},
            {"resolved_mount", resolved_mount},
            {"fstype", fstype_out}
        }.dump());
        return;
    }

    // Run balance status
    const std::string cmd =
        "/usr/bin/sudo -n /usr/bin/btrfs balance status " + sh_quote(resolved_mount);

    std::string out;
    int rc = 0;
    const bool ok = run_cmd_capture(cmd, &out, &rc);
    cap_string(out, 256 * 1024);

    // Parse best-effort
    bool running = false;
    bool paused  = false;
    bool found   = false;

    // Common outputs:
    // - "No balance found on '<mount>'"
    // - "Balance on '<mount>' is running"
    // - "Balance on '<mount>' is paused"
    // - "Balance on '<mount>' is finished"
    // btrfs-progs varies slightly by version; be tolerant.
    {
        const std::string low = to_lower_copy(out);
        if (low.find("no balance found") != std::string::npos) {
            found = false;
            running = false;
        } else if (low.find("is running") != std::string::npos) {
            found = true;
            running = true;
        } else if (low.find("is paused") != std::string::npos) {
            found = true;
            running = true;
            paused = true;
        } else if (low.find("is finished") != std::string::npos ||
                   low.find("finished") != std::string::npos ||
                   low.find("done") != std::string::npos) {
            found = true;
            running = false;
        } else {
            // Unknown wording; if command succeeded and output isn't empty,
            // return it as-is without hard claims.
            found = ok && (rc == 0);
        }
    }

    json j = {
        {"ok", true},
        {"input_mount", mount},
        {"resolved_mount", resolved_mount},
        {"resolved_source", resolved_source},
        {"fstype", fstype_out},
        {"rc", rc},
        {"status_raw", out},
        {"found", found},
        {"running", running},
        {"paused", paused}
    };
    if (!resolved_disk.empty()) j["resolved_disk"] = resolved_disk;

    reply_json(res, 200, j.dump());
});

// ----- GET /api/v4/raid/scrub-status?mount=/path (admin-only, read-only) -----
// Runs: btrfs scrub status <mount>
// Returns: { ok, resolved_mount, running, status_raw, rc, ... }
srv.Get("/api/v4/raid/scrub-status", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;

    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}, {"path", users_path}}.dump());
        return;
    }
    if (!require_admin_cookie_users(req, res, COOKIE_KEY, users_path, &users)) return;

    // Allowed_prefix + default mount
    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";

    std::string mount = allowed_prefix + "/data";
    if (req.has_param("mount")) mount = req.get_param_value("mount");

    if (!is_abs_path_safe(mount)) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mount"}, {"mount", mount}}.dump());
        return;
    }

    // Resolve mount -> resolved_mount / source / fstype
    std::string target_out, fstype_out, source_out;
    int ec_target = 0, ec_fs = 0, ec_src = 0;

    const bool ok_target = run_cmd_capture(
        "/usr/bin/findmnt -no TARGET --target " + sh_quote(mount), &target_out, &ec_target);
    cap_string(target_out, 16 * 1024);
    rtrim_inplace(target_out);

    const bool ok_fs = run_cmd_capture(
        "/usr/bin/findmnt -no FSTYPE --target " + sh_quote(mount), &fstype_out, &ec_fs);
    cap_string(fstype_out, 16 * 1024);
    rtrim_inplace(fstype_out);

    const bool ok_src = run_cmd_capture(
        "/usr/bin/findmnt -no SOURCE --target " + sh_quote(mount), &source_out, &ec_src);
    cap_string(source_out, 16 * 1024);
    rtrim_inplace(source_out);

    if (!ok_target || ec_target != 0 || target_out.empty() ||
        !ok_fs     || ec_fs     != 0 || fstype_out.empty() ||
        !ok_src    || ec_src    != 0 || source_out.empty()) {
        reply_json(res, 200, json{{"ok", false}, {"error", "mount_not_found"}, {"mount", mount}}.dump());
        return;
    }

    const std::string resolved_mount  = target_out;
    const std::string resolved_source = source_out;
    const std::string resolved_disk   = parent_disk_from_dev(resolved_source);

    // Allowlist on resolved mount
    if (resolved_mount.rfind(allowed_prefix, 0) != 0) {
        const std::string test_prefix  = "/srv/pqnas-test";
        const std::string test_prefix2 = "/srv/pqnas-test-btrfs";
        if (resolved_mount.rfind(test_prefix, 0) != 0 &&
            resolved_mount.rfind(test_prefix2, 0) != 0) {
            reply_json(res, 200, json{
                {"ok", false},
                {"error", "mount_not_allowed"},
                {"allowed_prefix", allowed_prefix},
                {"resolved_mount", resolved_mount}
            }.dump());
            return;
        }
    }

    if (fstype_out != "btrfs") {
        reply_json(res, 200, json{{"ok", false}, {"error", "not_btrfs"}, {"resolved_mount", resolved_mount}, {"fstype", fstype_out}}.dump());
        return;
    }

    // Run scrub status
    const std::string cmd =
        "/usr/bin/sudo -n /usr/bin/btrfs scrub status " + sh_quote(resolved_mount);

    std::string out;
    int rc = 0;
    const bool ok = run_cmd_capture(cmd, &out, &rc);
    cap_string(out, 256 * 1024);

    // Parse best-effort
    bool running = false;
    bool found   = false;

    // Typical btrfs-progs outputs include:
    // - "no stats available" (often means nothing running / never run)
    // - "scrub status for <mount>"
    // - "running for ..." / "finished" / "canceled"
    {
        const std::string low = to_lower_copy(out);

        if (low.find("no stats available") != std::string::npos ||
            low.find("no scrub") != std::string::npos) {
            found = false;
            running = false;
        } else if (low.find("running") != std::string::npos) {
            found = true;
            running = true;
        } else if (low.find("finished") != std::string::npos ||
                   low.find("completed") != std::string::npos ||
                   low.find("canceled") != std::string::npos ||
                   low.find("cancelled") != std::string::npos) {
            found = true;
            running = false;
        } else {
            found = ok && (rc == 0);
        }
    }

    json j = {
        {"ok", true},
        {"input_mount", mount},
        {"resolved_mount", resolved_mount},
        {"resolved_source", resolved_source},
        {"fstype", fstype_out},
        {"rc", rc},
        {"status_raw", out},
        {"found", found},
        {"running", running}
    };
    if (!resolved_disk.empty()) j["resolved_disk"] = resolved_disk;

    reply_json(res, 200, j.dump());
});

// ----- POST /api/v4/raid/plan/scrub (admin-only, plan-only) ------------------
// Body: { mount?:string, readonly?:bool }  (readonly currently informational only)
srv.Post("/api/v4/raid/plan/scrub", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;

    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}, {"path", users_path}}.dump());
        return;
    }
    if (!require_admin_cookie_users(req, res, COOKIE_KEY, users_path, &users)) return;
        if (!require_same_origin_for_cookie_mutation(req, res)) return;

    json in;
    try { in = json::parse(req.body.empty() ? "{}" : req.body); }
    catch (...) {
        reply_json(res, 400, json({{"ok",false},{"error","bad_request"},{"message","invalid json"}}).dump());
        return;
    }

    // Inputs
    std::string mount = in.value("mount", "");
    const bool readonly = in.value("readonly", false); // informational for now

    // Allowed_prefix + default mount
    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";
    if (mount.empty()) mount = allowed_prefix + "/data";

    if (!is_abs_path_safe(mount)) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mount"}}.dump());
        return;
    }

    // Resolve mount -> resolved_mount / source / fstype
    std::string target_out, fstype_out, source_out;
    int ec_target = 0, ec_fs = 0, ec_src = 0;

    const bool ok_target = run_cmd_capture(
        "/usr/bin/findmnt -no TARGET --target " + sh_quote(mount), &target_out, &ec_target);
    cap_string(target_out, 16 * 1024);
    rtrim_inplace(target_out);

    const bool ok_fs = run_cmd_capture(
        "/usr/bin/findmnt -no FSTYPE --target " + sh_quote(mount), &fstype_out, &ec_fs);
    cap_string(fstype_out, 16 * 1024);
    rtrim_inplace(fstype_out);

    const bool ok_src = run_cmd_capture(
        "/usr/bin/findmnt -no SOURCE --target " + sh_quote(mount), &source_out, &ec_src);
    cap_string(source_out, 16 * 1024);
    rtrim_inplace(source_out);

    if (!ok_target || ec_target != 0 || target_out.empty() ||
        !ok_fs     || ec_fs     != 0 || fstype_out.empty() ||
        !ok_src    || ec_src    != 0 || source_out.empty()) {
        reply_json(res, 200, json{{"ok", false}, {"error", "mount_not_found"}, {"mount", mount}}.dump());
        return;
    }

    const std::string resolved_mount  = target_out;
    const std::string resolved_source = source_out;
    const std::string resolved_disk   = parent_disk_from_dev(resolved_source);

    // Allowlist on resolved mount
    if (resolved_mount.rfind(allowed_prefix, 0) != 0) {
        const std::string test_prefix  = "/srv/pqnas-test";
        const std::string test_prefix2 = "/srv/pqnas-test-btrfs";
        if (resolved_mount.rfind(test_prefix, 0) != 0 &&
            resolved_mount.rfind(test_prefix2, 0) != 0) {
            reply_json(res, 200, json{
                {"ok", false},
                {"error", "mount_not_allowed"},
                {"allowed_prefix", allowed_prefix},
                {"resolved_mount", resolved_mount}
            }.dump());
            return;
        }
    }

    if (fstype_out != "btrfs") {
        reply_json(res, 200, json{{"ok", false}, {"error", "not_btrfs"}, {"resolved_mount", resolved_mount}, {"fstype", fstype_out}}.dump());
        return;
    }

    // Busy signal (per-mount lock). Plan is allowed while busy, but caller should see it.
    bool busy = false;
    std::string busy_lock;
    {
        const std::string lockp = raid_mount_lock_path(resolved_mount);
        std::error_code ec;
        const bool exists = std::filesystem::exists(lockp, ec);
        if (!ec && exists) {
            busy = true;
            busy_lock = lockp;
        }
    }

    // Build plan
    json plan;
    plan["mount"] = resolved_mount;
    plan["input_mount"] = mount;
    plan["resolved_mount"] = resolved_mount;
    plan["resolved_source"] = resolved_source;
    if (!resolved_disk.empty()) plan["resolved_disk"] = resolved_disk;
    plan["fstype"] = fstype_out;
    plan["requires_downtime"] = false;

    plan["readonly"] = readonly;

    plan["busy"] = busy;
    if (busy && !busy_lock.empty()) plan["busy_lock"] = busy_lock;

    json warnings = json::array();
    json steps = json::array();
    json commands = json::array();

    steps.push_back("Sanity-check: mount resolves to btrfs and is within allowed prefix.");
    steps.push_back("Start scrub (typically runs in background).");
    steps.push_back("Query scrub status immediately (may show running).");

    if (busy) {
        warnings.push_back("BUSY: another RAID operation is currently running for this mount; execute will likely return raid_busy until it finishes.");
    }
    warnings.push_back("Scrub can generate significant IO and may impact performance.");
    warnings.push_back("On single-device filesystems scrub validates checksums but cannot repair corrupted data without redundancy.");
    warnings.push_back("PLAN ONLY: commands are returned as strings; nothing is executed by this endpoint.");

    commands.push_back("/usr/bin/sudo -n /usr/bin/btrfs scrub start " + sh_quote(resolved_mount));
    commands.push_back("/usr/bin/sudo -n /usr/bin/btrfs scrub status " + sh_quote(resolved_mount));

    plan["steps"] = steps;
    plan["commands"] = commands;
    plan["warnings"] = warnings;

    // plan_id = sha256(joined commands)
    {
        const std::string joined2 = join_commands_for_hash(commands);
        const std::string pid = sha256_hex_lower_evp(joined2);
        if (!pid.empty()) plan["plan_id"] = pid;
    }

    reply_json(res, 200, json{{"ok", true}, {"plan", plan}}.dump());
});

// ----- POST /api/v4/raid/execute/scrub (admin-only) --------------------------
// Body: { mount, readonly?:bool, plan_id:string, dry_run?:bool(true), confirm?:bool(false) }
srv.Post("/api/v4/raid/execute/scrub", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;

    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}, {"path", users_path}}.dump());
        return;
    }

    // ---- auth (need actor fingerprint for audit) ----
    std::string actor_fp;
    if (!require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, &actor_fp)) return;
        if (!require_same_origin_for_cookie_mutation(req, res)) return;

    // ---- audit helpers (match Files API style) ----
    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto audit_kv_merge = [&](pqnas::AuditEvent& ev, const json& extra) {
        if (!extra.is_object()) return;
        for (auto it = extra.begin(); it != extra.end(); ++it) {
            const std::string k  = pqnas::shorten(it.key(), 64);
            const std::string kk = "x_" + k;

            if (it.value().is_string()) ev.f[kk] = pqnas::shorten(it.value().get<std::string>(), 220);
            else if (it.value().is_number_integer() || it.value().is_number_unsigned()) ev.f[kk] = it.value().dump();
            else if (it.value().is_boolean()) ev.f[kk] = (it.value().get<bool>() ? "true" : "false");
            else ev.f[kk] = pqnas::shorten(it.value().dump(), 220);
        }
    };

    auto audit_common = [&](pqnas::AuditEvent& ev) {
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
    };

    auto audit_fail = [&](const std::string& reason,
                          int http,
                          const std::string& detail = "",
                          const json& extra = json::object()) {
        pqnas::AuditEvent ev;
        ev.event = "v4.raid_execute_scrub_fail";
        ev.outcome = "fail";
        if (!actor_fp.empty()) ev.f["fingerprint"] = actor_fp;
        ev.f["reason"] = reason;
        ev.f["http"] = std::to_string(http);
        if (!detail.empty()) ev.f["detail"] = pqnas::shorten(detail, 220);

        audit_kv_merge(ev, extra);
        audit_common(ev);
        // IMPORTANT: do NOT call maybe_auto_rotate_before_append() here; audit_append wrapper does it.
        audit_append(ev);
    };

    auto audit_ok = [&](const json& extra = json::object()) {
        pqnas::AuditEvent ev;
        ev.event = "v4.raid_execute_scrub_ok";
        ev.outcome = "ok";
        if (!actor_fp.empty()) ev.f["fingerprint"] = actor_fp;

        audit_kv_merge(ev, extra);
        audit_common(ev);
        audit_append(ev);
    };

    json in;
    try { in = json::parse(req.body.empty() ? "{}" : req.body); }
    catch (...) {
        audit_fail("bad_json", 400);
        reply_json(res, 400, json({{"ok",false},{"error","bad_request"},{"message","invalid json"}}).dump());
        return;
    }

    // Inputs
    std::string mount = in.value("mount", "");
    const bool readonly = in.value("readonly", false); // informational for now
    const std::string client_plan_id = in.value("plan_id", "");

    // Safety: default dry_run=true
    const bool dry_run = in.value("dry_run", true);
    const bool confirm = in.value("confirm", false);

    if (client_plan_id.empty()) {
        audit_fail("missing_plan_id", 400);
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message","missing plan_id"}}.dump());
        return;
    }

    if (!dry_run && !confirm) {
        audit_fail("confirm_required", 400, "", json{{"dry_run", dry_run}, {"confirm", confirm}});
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "confirm_required"},
            {"message", "set confirm=true when dry_run=false"}
        }.dump());
        return;
    }

    // Allowed_prefix + default mount
    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";
    if (mount.empty()) mount = allowed_prefix + "/data";

    if (!is_abs_path_safe(mount)) {
        audit_fail("bad_mount", 400, "", json{{"mount", mount}});
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mount"}}.dump());
        return;
    }

    // Resolve mount -> resolved_mount / source / fstype
    std::string target_out, fstype_out, source_out;
    int ec_target = 0, ec_fs = 0, ec_src = 0;

    const bool ok_target = run_cmd_capture(
        "/usr/bin/findmnt -no TARGET --target " + sh_quote(mount), &target_out, &ec_target);
    cap_string(target_out, 16 * 1024);
    rtrim_inplace(target_out);

    const bool ok_fs = run_cmd_capture(
        "/usr/bin/findmnt -no FSTYPE --target " + sh_quote(mount), &fstype_out, &ec_fs);
    cap_string(fstype_out, 16 * 1024);
    rtrim_inplace(fstype_out);

    const bool ok_src = run_cmd_capture(
        "/usr/bin/findmnt -no SOURCE --target " + sh_quote(mount), &source_out, &ec_src);
    cap_string(source_out, 16 * 1024);
    rtrim_inplace(source_out);

    if (!ok_target || ec_target != 0 || target_out.empty() ||
        !ok_fs     || ec_fs     != 0 || fstype_out.empty() ||
        !ok_src    || ec_src    != 0 || source_out.empty()) {

        audit_fail("mount_not_found", 200, "", json{{"mount", mount}});
        reply_json(res, 200, json{{"ok", false}, {"error", "mount_not_found"}, {"mount", mount}}.dump());
        return;
    }

    const std::string resolved_mount  = target_out;
    const std::string resolved_source = source_out;
    const std::string resolved_disk   = parent_disk_from_dev(resolved_source);

    // Allowlist on resolved mount
    if (resolved_mount.rfind(allowed_prefix, 0) != 0) {
        const std::string test_prefix  = "/srv/pqnas-test";
        const std::string test_prefix2 = "/srv/pqnas-test-btrfs";
        if (resolved_mount.rfind(test_prefix, 0) != 0 &&
            resolved_mount.rfind(test_prefix2, 0) != 0) {

            audit_fail("mount_not_allowed", 200, "", json{
                {"mount", mount},
                {"resolved_mount", resolved_mount},
                {"allowed_prefix", allowed_prefix}
            });

            reply_json(res, 200, json{
                {"ok", false},
                {"error", "mount_not_allowed"},
                {"allowed_prefix", allowed_prefix},
                {"resolved_mount", resolved_mount}
            }.dump());
            return;
        }
    }

    if (fstype_out != "btrfs") {
        audit_fail("not_btrfs", 200, "", json{{"resolved_mount", resolved_mount}, {"fstype", fstype_out}});
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "not_btrfs"},
            {"resolved_mount", resolved_mount},
            {"fstype", fstype_out}
        }.dump());
        return;
    }

    // -------- Build commands exactly like plan endpoint --------
    json commands = json::array();
    commands.push_back("/usr/bin/sudo -n /usr/bin/btrfs scrub start " + sh_quote(resolved_mount));
    commands.push_back("/usr/bin/sudo -n /usr/bin/btrfs scrub status " + sh_quote(resolved_mount));

    // plan_id check (must match exactly)
    const std::string joined = join_commands_for_hash(commands);
    const std::string expected_plan_id = sha256_hex_lower_evp(joined);
    if (expected_plan_id.empty()) {
        audit_fail("plan_id_compute_failed", 500, "", json{{"resolved_mount", resolved_mount}});
        reply_json(res, 500, json{{"ok", false}, {"error", "plan_id_compute_failed"}}.dump());
        return;
    }
    if (client_plan_id != expected_plan_id) {
        audit_fail("plan_mismatch", 400, "", json{
            {"mount", mount},
            {"resolved_mount", resolved_mount},
            {"expected_plan_id", expected_plan_id},
            {"provided_plan_id", client_plan_id}
        });

        reply_json(res, 400, json{
            {"ok", false},
            {"error", "plan_mismatch"},
            {"message", "plan_id does not match server recomputed plan"},
            {"expected_plan_id", expected_plan_id},
            {"provided_plan_id", client_plan_id}
        }.dump());
        return;
    }

    // Plan payload (for caller/UI)
    json plan;
    plan["plan_id"] = expected_plan_id;
    plan["mount"] = resolved_mount;
    plan["input_mount"] = mount;
    plan["resolved_mount"] = resolved_mount;
    plan["resolved_source"] = resolved_source;
    if (!resolved_disk.empty()) plan["resolved_disk"] = resolved_disk;
    plan["fstype"] = fstype_out;
    plan["readonly"] = readonly;
    plan["requires_downtime"] = false;
    plan["commands"] = commands;

    if (dry_run) {
        audit_ok(json{
            {"dry_run", true},
            {"mount", mount},
            {"resolved_mount", resolved_mount},
            {"plan_id", expected_plan_id},
            {"readonly", readonly},
            {"commands", (int)commands.size()}
        });

        reply_json(res, 200, json{{"ok", true}, {"dry_run", true}, {"plan", plan}}.dump());
        return;
    }

    // Locks + execution record (fail-closed)
    int fd_mount_lock = -1;
    int fd_plan_rec   = -1;
    std::string mount_lockp;

    const std::string recp = raid_exec_record_path(expected_plan_id);

    auto close_locks = [&]() {
        if (fd_plan_rec >= 0) { ::close(fd_plan_rec); fd_plan_rec = -1; }
        if (fd_mount_lock >= 0) { ::close(fd_mount_lock); fd_mount_lock = -1; }
        if (!mount_lockp.empty()) {
            (void)std::filesystem::remove(mount_lockp); // lease
        }
    };

    // Ensure state dir exists
    std::string raid_dir_err;
    if (!ensure_dir_fail_closed("/run/pqnas/raid", &raid_dir_err)) {
        audit_fail("raid_state_dir_failed", 500, raid_dir_err, json{
            {"mount", mount},
            {"resolved_mount", resolved_mount},
            {"plan_id", expected_plan_id}
        });

        reply_json(res, 500, json{
            {"ok", false},
            {"error", "raid_state_dir_failed"},
            {"message", "cannot create/verify /run/pqnas/raid; refusing to execute"},
            {"detail", raid_dir_err}
        }.dump());
        return;
    }

    // Acquire per-mount lock first
    mount_lockp = raid_mount_lock_path(resolved_mount);
    {
        std::string mount_lock_err;
        fd_mount_lock = open_excl_lockfile(mount_lockp, &mount_lock_err);
        if (fd_mount_lock < 0) {
            audit_fail("raid_busy", 409, mount_lock_err, json{
                {"mount", mount},
                {"resolved_mount", resolved_mount},
                {"path", mount_lockp},
                {"plan_id", expected_plan_id}
            });

            reply_json(res, 409, json{
                {"ok", false},
                {"error", "raid_busy"},
                {"message", "another raid operation is in progress for this mount"},
                {"mount", resolved_mount},
                {"path", mount_lockp},
                {"detail", mount_lock_err}
            }.dump());
            return;
        }
    }

    // Acquire per-plan execution record lock (replay protection)
    {
        std::string rec_err;
        fd_plan_rec = open_excl_lockfile(recp, &rec_err);
        if (fd_plan_rec < 0) {
            close_locks();

            audit_fail("already_executed", 200, rec_err, json{
                {"mount", mount},
                {"resolved_mount", resolved_mount},
                {"plan_id", expected_plan_id},
                {"path", recp}
            });

            reply_json(res, 200, json{
                {"ok", false},
                {"error", "already_executed"},
                {"message", "this plan_id already has an execution record; refusing replay"},
                {"plan_id", expected_plan_id},
                {"path", recp},
                {"detail", rec_err}
            }.dump());
            return;
        }
    }

    // Initial record
    const std::string ts0 = pqnas::now_iso_utc();
    json record = {
        {"ts_start", ts0},
        {"ts_last",  ts0},
        {"ts_end",   nullptr},

        {"plan_id", expected_plan_id},
        {"state", "running"},
        {"busy", true},

        {"mount", resolved_mount},
        {"input_mount", mount},
        {"resolved_source", resolved_source},
        {"resolved_disk", resolved_disk},

        {"readonly", readonly},
        {"dry_run", false},

        {"plan", plan},
        {"commands", commands},
        {"step_index", 0},
        {"step_total", (int)commands.size()},
        {"results", json::array()}
    };

    // Write initial record to replay-lock file (then close fd)
    {
        const std::string txt = record.dump(2) + "\n";
        if (!write_fd_all(fd_plan_rec, txt)) {
            const std::string err = std::string("write record failed: ") + std::strerror(errno);
            close_locks();

            audit_fail("exec_record_write_failed", 500, err, json{
                {"mount", mount},
                {"resolved_mount", resolved_mount},
                {"plan_id", expected_plan_id}
            });

            reply_json(res, 500, json{
                {"ok", false},
                {"error", "exec_record_write_failed"},
                {"message", "failed to write execution record; refusing to execute"},
                {"detail", err}
            }.dump());
            return;
        }
        ::close(fd_plan_rec);
        fd_plan_rec = -1;
    }

    // Execute commands (stop on first failure)
    json results = json::array();
    bool all_ok = true;

    const int total = (int)commands.size();
    int fail_i = -1;
    int fail_rc = 0;

    for (int i = 0; i < total; i++) {
        const auto& c = commands[i];
        if (!c.is_string()) continue;
        const std::string cmd = c.get<std::string>();

        std::string out;
        int ec = 0;
        const bool okc = run_cmd_capture(cmd, &out, &ec);
        cap_string(out, 128 * 1024);

        json one = {{"i", i}, {"cmd", cmd}, {"rc", ec}, {"ok", okc}, {"out", out}};
        results.push_back(one);

        raid_exec_record_append_step(&record, i + 1, total, cmd, okc, ec, out);
        (void)raid_exec_record_write_atomic(expected_plan_id, record);

        if (!okc || ec != 0) { all_ok = false; fail_i = i; fail_rc = ec; break; }
    }

    // Finalize record
    const std::string ts_end = pqnas::now_iso_utc();
    record["ts_end"]  = ts_end;
    record["ts_last"] = ts_end;
    record["busy"]    = false;
    record["state"]   = all_ok ? "done" : "failed";
    record["results"] = results;

    // Attach final scrub-status snapshot (best-effort)
    if (all_ok) {
        std::string s_out;
        int s_rc = 0;
        (void)run_cmd_capture("/usr/bin/sudo -n /usr/bin/btrfs scrub status " + sh_quote(resolved_mount), &s_out, &s_rc);
        cap_string(s_out, 256 * 1024);
        record["post_scrub_status"] = json{{"rc", s_rc}, {"status_raw", s_out}};
    }

    (void)write_text_file_atomic(recp, record.dump(2) + "\n");

    json outj = {
        {"ok", all_ok},
        {"dry_run", false},
        {"plan", plan},
        {"results", results}
    };
    if (record.contains("post_scrub_status")) outj["post_scrub_status"] = record["post_scrub_status"];

    // ---- audit outcome ----
    if (all_ok) {
        audit_ok(json{
            {"dry_run", false},
            {"mount", mount},
            {"resolved_mount", resolved_mount},
            {"plan_id", expected_plan_id},
            {"readonly", readonly},
            {"commands", (int)commands.size()}
        });
    } else {
        audit_fail("command_failed", 200, "", json{
            {"dry_run", false},
            {"mount", mount},
            {"resolved_mount", resolved_mount},
            {"plan_id", expected_plan_id},
            {"readonly", readonly},
            {"commands", (int)commands.size()},
            {"failed_i", fail_i},
            {"failed_rc", fail_rc}
        });
    }

    close_locks();
    reply_json(res, 200, outj.dump());
});
// ----- GET /api/v4/raid/status?mount=/path (admin-only, read-only) -----------
// Combines: filesystem show/df/device stats + balance status + scrub status
srv.Get("/api/v4/raid/status", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;

    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}, {"path", users_path}}.dump());
        return;
    }
    if (!require_admin_cookie_users(req, res, COOKIE_KEY, users_path, &users)) return;

    // Allowed_prefix + default mount
    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";

    std::string mount = allowed_prefix + "/data";
    if (req.has_param("mount")) mount = req.get_param_value("mount");

    if (!is_abs_path_safe(mount)) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mount"}, {"mount", mount}}.dump());
        return;
    }

    // Resolve mount -> resolved_mount / source / fstype
    std::string target_out, fstype_out, source_out;
    int ec_target = 0, ec_fs = 0, ec_src = 0;

    const bool ok_target = run_cmd_capture(
        "/usr/bin/findmnt -no TARGET --target " + sh_quote(mount), &target_out, &ec_target);
    cap_string(target_out, 16 * 1024);
    rtrim_inplace(target_out);

    const bool ok_fs = run_cmd_capture(
        "/usr/bin/findmnt -no FSTYPE --target " + sh_quote(mount), &fstype_out, &ec_fs);
    cap_string(fstype_out, 16 * 1024);
    rtrim_inplace(fstype_out);

    const bool ok_src = run_cmd_capture(
        "/usr/bin/findmnt -no SOURCE --target " + sh_quote(mount), &source_out, &ec_src);
    cap_string(source_out, 16 * 1024);
    rtrim_inplace(source_out);

    if (!ok_target || ec_target != 0 || target_out.empty() ||
        !ok_fs     || ec_fs     != 0 || fstype_out.empty() ||
        !ok_src    || ec_src    != 0 || source_out.empty()) {
        reply_json(res, 200, json{{"ok", false}, {"error", "mount_not_found"}, {"mount", mount}}.dump());
        return;
    }

    const std::string resolved_mount  = target_out;
    const std::string resolved_source = source_out;
    const std::string resolved_disk   = parent_disk_from_dev(resolved_source);

    // Allowlist on resolved mount
    if (resolved_mount.rfind(allowed_prefix, 0) != 0) {
        const std::string test_prefix  = "/srv/pqnas-test";
        const std::string test_prefix2 = "/srv/pqnas-test-btrfs";
        if (resolved_mount.rfind(test_prefix, 0) != 0 &&
            resolved_mount.rfind(test_prefix2, 0) != 0) {
            reply_json(res, 200, json{
                {"ok", false},
                {"error", "mount_not_allowed"},
                {"allowed_prefix", allowed_prefix},
                {"resolved_mount", resolved_mount}
            }.dump());
            return;
        }
    }

    if (fstype_out != "btrfs") {
        reply_json(res, 200, json{{"ok", false}, {"error", "not_btrfs"}, {"resolved_mount", resolved_mount}, {"fstype", fstype_out}}.dump());
        return;
    }

    // Helper to run cmd + capture
    auto run = [&](const std::string& cmd, int cap_bytes, std::string* out_txt, int* out_rc) -> json {
        std::string out;
        int rc = 0;
        const bool ok = run_cmd_capture(cmd, &out, &rc);
        cap_string(out, cap_bytes);
        if (out_txt) *out_txt = out;
        if (out_rc)  *out_rc = rc;
        return json{{"ok", ok && rc == 0}, {"rc", rc}, {"out", out}};
    };

    // Collect raw outputs
    json out = {
        {"ok", true},
        {"input_mount", mount},
        {"resolved_mount", resolved_mount},
        {"resolved_source", resolved_source},
        {"fstype", fstype_out}
    };
    if (!resolved_disk.empty()) out["resolved_disk"] = resolved_disk;

    // btrfs filesystem show
    out["btrfs_filesystem_show"] = run(
        "/usr/bin/sudo -n /usr/bin/btrfs filesystem show " + sh_quote(resolved_mount),
        256 * 1024, nullptr, nullptr
    );

    // btrfs filesystem df
    out["btrfs_filesystem_df"] = run(
        "/usr/bin/sudo -n /usr/bin/btrfs filesystem df " + sh_quote(resolved_mount),
        256 * 1024, nullptr, nullptr
    );

    // btrfs device stats
    out["btrfs_device_stats"] = run(
        "/usr/bin/sudo -n /usr/bin/btrfs device stats " + sh_quote(resolved_mount),
        256 * 1024, nullptr, nullptr
    );

    // balance status
    {
        std::string raw;
        int rc = 0;
        json r = run(
            "/usr/bin/sudo -n /usr/bin/btrfs balance status " + sh_quote(resolved_mount),
            256 * 1024, &raw, &rc
        );

        bool running = false, paused = false, found = false;
        const std::string low = to_lower_copy(raw);
        if (low.find("no balance found") != std::string::npos) {
            found = false; running = false;
        } else if (low.find("is running") != std::string::npos) {
            found = true; running = true;
        } else if (low.find("is paused") != std::string::npos) {
            found = true; running = true; paused = true;
        } else if (low.find("is finished") != std::string::npos ||
                   low.find("finished") != std::string::npos ||
                   low.find("done") != std::string::npos) {
            found = true; running = false;
        } else {
            found = r.value("ok", false);
        }

        r["found"] = found;
        r["running"] = running;
        r["paused"] = paused;
        out["balance_status"] = r;
    }

    // scrub status
    {
        std::string raw;
        int rc = 0;
        json r = run(
            "/usr/bin/sudo -n /usr/bin/btrfs scrub status " + sh_quote(resolved_mount),
            256 * 1024, &raw, &rc
        );

        bool running = false, found = false;
        const std::string low = to_lower_copy(raw);

        if (low.find("no stats available") != std::string::npos ||
            low.find("no scrub") != std::string::npos) {
            found = false; running = false;
        } else if (low.find("running") != std::string::npos) {
            found = true; running = true;
        } else if (low.find("finished") != std::string::npos ||
                   low.find("completed") != std::string::npos ||
                   low.find("canceled") != std::string::npos ||
                   low.find("cancelled") != std::string::npos) {
            found = true; running = false;
        } else {
            found = r.value("ok", false);
        }

        r["found"] = found;
        r["running"] = running;
        out["scrub_status"] = r;
    }

    // Busy (mount lock exists)
    {
        bool busy = false;
        std::string busy_lock;
        const std::string lockp = raid_mount_lock_path(resolved_mount);
        std::error_code ec;
        const bool exists = std::filesystem::exists(lockp, ec);
        if (!ec && exists) { busy = true; busy_lock = lockp; }
        out["busy"] = busy;
        if (busy && !busy_lock.empty()) out["busy_lock"] = busy_lock;
    }
    // Parsed + UI-friendly summary (existing helper)
    out["parsed"] = storage_btrfs_status_json(resolved_mount);

    // Enrich parsed block with mount resolution context (helps UI avoid re-resolving)
    out["parsed"]["input_mount"] = mount;
    out["parsed"]["resolved_mount"] = resolved_mount;
    out["parsed"]["resolved_source"] = resolved_source;
    if (!resolved_disk.empty()) out["parsed"]["resolved_disk"] = resolved_disk;
    out["parsed"]["fstype"] = fstype_out;


    reply_json(res, 200, out.dump());
});

// ----- POST /api/v4/raid/plan/add-device (admin-only, plan-only) -------------
srv.Post("/api/v4/raid/plan/add-device", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;

    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}, {"path", users_path}}.dump());
        return;
    }
    if (!require_admin_cookie_users(req, res, COOKIE_KEY, users_path, &users)) return;
        if (!require_same_origin_for_cookie_mutation(req, res)) return;

    json in;
    try { in = json::parse(req.body.empty() ? "{}" : req.body); }
    catch (...) {
        reply_json(res, 400, json({{"ok",false},{"error","bad_request"},{"message","invalid json"}}).dump());
        return;
    }

    // Inputs
    std::string mount    = in.value("mount", "");
    std::string new_disk = in.value("new_disk", "");
    std::string mode     = in.value("mode", "single");  // single|raid1
    bool force           = in.value("force", false);

    // Allowed_prefix + default mount
    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";
    if (mount.empty()) mount = allowed_prefix + "/data";

    // Validate inputs
    if (!is_abs_path_safe(mount)) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mount"}}.dump());
        return;
    }
    if (!is_dev_path_basic_safe(new_disk)) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_device"}, {"message","expected /dev/..."} }.dump());
        return;
    }
    if (mode != "single" && mode != "raid1") {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message","mode must be single|raid1"} }.dump());
        return;
    }

    // Resolve mount -> resolved_mount / source / fstype
    std::string target_out, fstype_out, source_out;
    int ec_target = 0, ec_fs = 0, ec_src = 0;

    const bool ok_target = run_cmd_capture(
        "/usr/bin/findmnt -no TARGET --target " + sh_quote(mount), &target_out, &ec_target);
    cap_string(target_out, 16 * 1024);
    rtrim_inplace(target_out);

    const bool ok_fs = run_cmd_capture(
        "/usr/bin/findmnt -no FSTYPE --target " + sh_quote(mount), &fstype_out, &ec_fs);
    cap_string(fstype_out, 16 * 1024);
    rtrim_inplace(fstype_out);

    const bool ok_src = run_cmd_capture(
        "/usr/bin/findmnt -no SOURCE --target " + sh_quote(mount), &source_out, &ec_src);
    cap_string(source_out, 16 * 1024);
    rtrim_inplace(source_out);

    if (!ok_target || ec_target != 0 || target_out.empty() ||
        !ok_fs     || ec_fs     != 0 || fstype_out.empty() ||
        !ok_src    || ec_src    != 0 || source_out.empty()) {
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "mount_not_found"},
            {"mount", mount}
        }.dump());
        return;
    }

    const std::string resolved_mount  = target_out;
    const std::string resolved_source = source_out;
    const std::string resolved_disk   = parent_disk_from_dev(resolved_source);

    const std::string system_root_disk = detect_system_pool_root_disk();
    if (!system_root_disk.empty() && new_disk == system_root_disk) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "device_is_system_root_disk"},
            {"system_root_disk", system_root_disk},
            {"new_disk", new_disk}
        }.dump());
        return;
    }

    // Allowlist on resolved mount
    if (resolved_mount.rfind(allowed_prefix, 0) != 0) {
        const std::string test_prefix  = "/srv/pqnas-test";
        const std::string test_prefix2 = "/srv/pqnas-test-btrfs";
        if (resolved_mount.rfind(test_prefix, 0) != 0 &&
            resolved_mount.rfind(test_prefix2, 0) != 0) {
            reply_json(res, 200, json{
                {"ok", false},
                {"error", "mount_not_allowed"},
                {"allowed_prefix", allowed_prefix},
                {"resolved_mount", resolved_mount}
            }.dump());
            return;
        }
    }

    if (fstype_out != "btrfs") {
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "not_btrfs"},
            {"resolved_mount", resolved_mount},
            {"fstype", fstype_out}
        }.dump());
        return;
    }

    // Read btrfs filesystem show (used to salt plan_id so add->remove->add works)
    const std::string cmd_show =
        "/usr/bin/sudo -n /usr/bin/btrfs filesystem show " + sh_quote(resolved_mount);

    std::string show_raw;
    int ec_show = 0;
    const bool ok_show = run_cmd_capture(cmd_show, &show_raw, &ec_show);
    cap_string(show_raw, 256 * 1024);

    if (!ok_show || ec_show != 0 || show_raw.empty()) {
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "btrfs_show_failed"},
            {"resolved_mount", resolved_mount},
            {"btrfs_show_rc", ec_show}
        }.dump());
        return;
    }

    // Busy signal (per-mount lock). Plan is allowed while busy, but caller should see it.
    bool busy = false;
    std::string busy_lock;
    {
        const std::string lockp = raid_mount_lock_path(resolved_mount);
        std::error_code ec;
        const bool exists = std::filesystem::exists(lockp, ec);
        if (!ec && exists) {
            busy = true;
            busy_lock = lockp;
        }
    }

    // Load disks allowlist (inherits PQNAS_STORAGE_ALLOW_LOOP policy)
    std::string raw_lsblk;
    json disks_j = storage_list_disks_json(&raw_lsblk);
    json by_path = disks_j.value("by_path", json::object());
    json disks   = disks_j.value("disks", json::array());

    if (!by_path.is_object() || !by_path.contains(new_disk)) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "device_not_allowed"},
            {"new_disk", new_disk}
        }.dump());
        return;
    }

    int disk_index = -1;
    try { disk_index = by_path[new_disk].get<int>(); } catch (...) { disk_index = -1; }

    if (disk_index < 0 || !disks.is_array() || disk_index >= (int)disks.size()) {
        reply_json(res, 500, json{{"ok", false}, {"error", "lsblk_index_error"}}.dump());
        return;
    }

    json d = disks[disk_index];

    // Hard-refuse disks that have ANY mountpoints anywhere (fail-closed even with force)
    {
        json mpcheck = lsblk_disk_mountpoints_json(new_disk);
        if (mpcheck.value("ok", false) && mpcheck.contains("mountpoints") && mpcheck["mountpoints"].is_array()) {
            if (!mpcheck["mountpoints"].empty()) {
                reply_json(res, 400, json{
                    {"ok", false},
                    {"error", "disk_in_use"},
                    {"new_disk", new_disk},
                    {"disk_index", disk_index},
                    {"model", d.value("model","")},
                    {"serial", d.value("serial","")},
                    {"mountpoints", mpcheck["mountpoints"]}
                }.dump());
                return;
            }
        } else {
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "disk_in_use_check_failed"},
                {"new_disk", new_disk},
                {"detail", mpcheck}
            }.dump());
            return;
        }
    }

    // Refuse adding the same disk the FS is already on (safety)
    if (!resolved_disk.empty() && new_disk == resolved_disk) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "device_is_current_disk"},
            {"resolved_disk", resolved_disk},
            {"new_disk", new_disk}
        }.dump());
        return;
    }

    const int children = d.value("children", 0);
    const uint64_t new_disk_size = d.value("size_bytes", (uint64_t)0);

    // Compute FS membership fingerprint (for plan_id salting)
    std::string membership_fp;
    {
        BtrfsShowParsed parsed2 = parse_btrfs_filesystem_show(show_raw);
        json btrfs2 = btrfs_show_parsed_to_json(parsed2, by_path, disks_j.value("by_name", json::object()));
        membership_fp = btrfs_membership_fingerprint(btrfs2);
    }
    if (membership_fp.empty()) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "membership_fp_failed"},
            {"message", "failed to compute btrfs membership fingerprint"}
        }.dump());
        return;
    }

    // Build plan
    json plan;
    plan["mount"] = resolved_mount;
    plan["input_mount"] = mount;
    plan["resolved_mount"] = resolved_mount;
    plan["resolved_source"] = resolved_source;
    if (!resolved_disk.empty()) plan["resolved_disk"] = resolved_disk;
    plan["fstype"] = fstype_out;

    plan["new_disk"] = new_disk;
    plan["new_disk_index"] = disk_index;
    plan["new_disk_size_bytes"] = new_disk_size;
    plan["mode"] = mode;
    plan["force"] = force;
    plan["requires_downtime"] = false;

    // Busy surface
    plan["busy"] = busy;
    if (busy && !busy_lock.empty()) plan["busy_lock"] = busy_lock;

    json warnings = json::array();
    json steps = json::array();
    json commands = json::array();

    // If disk has partitions: refuse unless force (strict default)
    if (children > 0 && !force) {
        warnings.push_back("new_disk_has_partitions");
        warnings.push_back("refusing_to_plan_destructive_partitioning_without_force=true");
        plan["children"] = children;
        plan["warnings"] = warnings;

        reply_json(res, 200, json{
            {"ok", false},
            {"error", "disk_not_empty"},
            {"plan", plan}
        }.dump());
        return;
    }

    // Partition path (we plan to create p1)
    const std::string new_part = part1_path_from_disk(new_disk);
    if (new_part.empty()) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_device"}}.dump());
        return;
    }
    plan["new_partition"] = new_part;

    // Steps / commands (plan-only)
    steps.push_back("Sanity-check: mount resolves to btrfs and is within allowed prefix.");
    steps.push_back("Sanity-check: new_disk is allowlisted by lsblk and has no mounted partitions.");
    steps.push_back("Sanity-check: new_disk is not the current filesystem disk.");

    if (busy) {
        warnings.push_back("BUSY: another RAID operation is currently running for this mount; execute will likely return raid_busy until it finishes.");
    }

    if (children > 0 && force) {
        warnings.push_back("DESTRUCTIVE: new_disk has existing partitions; plan includes wiping partition table and signatures.");
    } else {
        warnings.push_back("DESTRUCTIVE: plan includes wiping any existing signatures on new_disk.");
    }
    warnings.push_back("Adding a device and converting profiles can take a long time; expect background IO (balance).");
    warnings.push_back("PLAN ONLY: commands are returned as strings; nothing is executed by this endpoint.");

    commands.push_back("/usr/bin/sudo -n /usr/sbin/sgdisk --zap-all " + sh_quote(new_disk));
    commands.push_back("/usr/bin/sudo -n /usr/sbin/wipefs -a " + sh_quote(new_disk));
    commands.push_back("/usr/bin/sudo -n /usr/sbin/sgdisk -n 1:0:0 -t 1:8300 -c 1:PQNAS_BTRFS " + sh_quote(new_disk));
    commands.push_back("/usr/bin/sudo -n /usr/sbin/partprobe " + sh_quote(new_disk));

    // NEW — must match execute endpoint exactly
    commands.push_back("/usr/bin/sudo -n /usr/bin/udevadm settle");

    // Wait for partition node to appear (handled internally by executor)
    commands.push_back("WAIT_BLOCK " + new_part + " 2000");

    commands.push_back("/usr/bin/sudo -n /usr/bin/btrfs device add " + sh_quote(new_part) + " " + sh_quote(resolved_mount));

    if (mode == "raid1") {
        commands.push_back("/usr/bin/sudo -n /usr/bin/btrfs balance start -dconvert=raid1 -mconvert=raid1 " + sh_quote(resolved_mount));
        steps.push_back("Convert data/metadata profiles to RAID1 via balance.");
    } else {
        steps.push_back("No profile conversion requested (mode=single). Filesystem will remain in its current profiles until converted.");
    }

    plan["steps"] = steps;
    plan["commands"] = commands;
    plan["warnings"] = warnings;

    // plan_nonce: per-attempt uniqueness (prevents add->remove->add collisions)
    const std::string plan_nonce = rand_hex_16();
    plan["plan_nonce"] = plan_nonce;

    // plan_id = sha256(joined commands + salt + plan_nonce)
    // MUST match execute/add-device exactly.
    {
        const std::string joined2 = join_commands_for_hash(commands);

        const std::string salt =
            std::string("mount=") + resolved_mount + "\n" +
            std::string("btrfs_membership_fp=") + membership_fp + "\n";

        const std::string pid =
            sha256_hex_lower_evp(joined2 + "\n" + salt + "plan_nonce=" + plan_nonce + "\n");

        if (!pid.empty()) plan["plan_id"] = pid;
    }

    reply_json(res, 200, json{{"ok", true}, {"plan", plan}}.dump());
});

// ----- POST /api/v4/raid/plan/convert-mode (admin-only, plan-only) ----------
srv.Post("/api/v4/raid/plan/convert-mode", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;

    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}, {"path", users_path}}.dump());
        return;
    }
    if (!require_admin_cookie_users(req, res, COOKIE_KEY, users_path, &users)) return;
        if (!require_same_origin_for_cookie_mutation(req, res)) return;

    json in;
    try { in = json::parse(req.body.empty() ? "{}" : req.body); }
    catch (...) {
        reply_json(res, 400, json({{"ok",false},{"error","bad_request"},{"message","invalid json"}}).dump());
        return;
    }

    std::string mount = in.value("mount", "");
    std::string mode  = in.value("mode", "single"); // single|raid1

    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";
    if (mount.empty()) mount = allowed_prefix + "/data";

    if (!is_abs_path_safe(mount)) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mount"}}.dump());
        return;
    }
    if (mode != "single" && mode != "raid1") {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "mode must be single|raid1"}}.dump());
        return;
    }

    std::string target_out, fstype_out, source_out;
    int ec_target = 0, ec_fs = 0, ec_src = 0;

    const bool ok_target = run_cmd_capture(
        "/usr/bin/findmnt -no TARGET --target " + sh_quote(mount), &target_out, &ec_target);
    cap_string(target_out, 16 * 1024);
    rtrim_inplace(target_out);

    const bool ok_fs = run_cmd_capture(
        "/usr/bin/findmnt -no FSTYPE --target " + sh_quote(mount), &fstype_out, &ec_fs);
    cap_string(fstype_out, 16 * 1024);
    rtrim_inplace(fstype_out);

    const bool ok_src = run_cmd_capture(
        "/usr/bin/findmnt -no SOURCE --target " + sh_quote(mount), &source_out, &ec_src);
    cap_string(source_out, 16 * 1024);
    rtrim_inplace(source_out);

    if (!ok_target || ec_target != 0 || target_out.empty() ||
        !ok_fs     || ec_fs     != 0 || fstype_out.empty() ||
        !ok_src    || ec_src    != 0 || source_out.empty()) {
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "mount_not_found"},
            {"mount", mount}
        }.dump());
        return;
    }

    const std::string resolved_mount  = target_out;
    const std::string resolved_source = source_out;
    const std::string resolved_disk   = parent_disk_from_dev(resolved_source);

    if (resolved_mount.rfind(allowed_prefix, 0) != 0) {
        const std::string test_prefix  = "/srv/pqnas-test";
        const std::string test_prefix2 = "/srv/pqnas-test-btrfs";
        if (resolved_mount.rfind(test_prefix, 0) != 0 &&
            resolved_mount.rfind(test_prefix2, 0) != 0) {
            reply_json(res, 200, json{
                {"ok", false},
                {"error", "mount_not_allowed"},
                {"allowed_prefix", allowed_prefix},
                {"resolved_mount", resolved_mount}
            }.dump());
            return;
        }
    }

    if (fstype_out != "btrfs") {
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "not_btrfs"},
            {"resolved_mount", resolved_mount},
            {"fstype", fstype_out}
        }.dump());
        return;
    }

    bool busy = false;
    std::string busy_lock;
    {
        const std::string lockp = raid_mount_lock_path(resolved_mount);
        std::error_code ec;
        const bool exists = std::filesystem::exists(lockp, ec);
        if (!ec && exists) {
            busy = true;
            busy_lock = lockp;
        }
    }

    std::string raw_lsblk;
    json disks_j = storage_list_disks_json(&raw_lsblk);
    json by_path = disks_j.value("by_path", json::object());

    const std::string cmd_show =
        "/usr/bin/sudo -n /usr/bin/btrfs filesystem show " + sh_quote(resolved_mount);

    std::string show_raw;
    int ec_show = 0;
    const bool ok_show = run_cmd_capture(cmd_show, &show_raw, &ec_show);
    cap_string(show_raw, 256 * 1024);

    if (!ok_show || ec_show != 0 || show_raw.empty()) {
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "btrfs_show_failed"},
            {"resolved_mount", resolved_mount},
            {"btrfs_show_rc", ec_show}
        }.dump());
        return;
    }

    BtrfsShowParsed parsed = parse_btrfs_filesystem_show(show_raw);
    json btrfs_j = btrfs_show_parsed_to_json(parsed, by_path, disks_j.value("by_name", json::object()));

    const std::string membership_fp = btrfs_membership_fingerprint(btrfs_j);
    if (membership_fp.empty()) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "membership_fp_failed"},
            {"message", "failed to compute btrfs membership fingerprint"}
        }.dump());
        return;
    }

    const int total_devices = btrfs_j.value("total_devices", 0);
    if (total_devices < 1) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "no_devices_in_filesystem"},
            {"resolved_mount", resolved_mount}
        }.dump());
        return;
    }

    if (mode == "raid1" && total_devices < 2) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "raid1_requires_2_devices"},
            {"resolved_mount", resolved_mount},
            {"total_devices", total_devices}
        }.dump());
        return;
    }

    json plan;
    plan["mount"] = resolved_mount;
    plan["input_mount"] = mount;
    plan["resolved_mount"] = resolved_mount;
    plan["resolved_source"] = resolved_source;
    if (!resolved_disk.empty()) plan["resolved_disk"] = resolved_disk;
    plan["fstype"] = fstype_out;
    plan["mode"] = mode;
    plan["requires_downtime"] = false;
    plan["busy"] = busy;
    if (busy && !busy_lock.empty()) plan["busy_lock"] = busy_lock;
    plan["total_devices"] = total_devices;
    plan["btrfs_membership_fp"] = membership_fp;

    json warnings = json::array();
    json steps = json::array();
    json commands = json::array();

    steps.push_back("Sanity-check: mount resolves to btrfs and is within allowed prefix.");
    steps.push_back("Sanity-check: filesystem has enough devices for requested mode.");

    if (busy) {
        warnings.push_back("BUSY: another RAID operation is currently running for this mount; execute will likely return raid_busy until it finishes.");
    }

    if (mode == "raid1") {
        commands.push_back("/usr/bin/sudo -n /usr/bin/btrfs balance start -dconvert=raid1 -mconvert=raid1 " + sh_quote(resolved_mount));
        steps.push_back("Convert data/metadata profiles to RAID1 via balance.");
    } else {
        commands.push_back("/usr/bin/sudo -n /usr/bin/btrfs balance start --force -dconvert=single -mconvert=single -sconvert=single " + sh_quote(resolved_mount));
        steps.push_back("Convert data/metadata/system profiles to SINGLE via balance (--force for system chunks).");
        warnings.push_back("Converting to SINGLE with multiple devices reduces redundancy.");
    }

    warnings.push_back("Profile conversion can take a long time and generate background IO.");
    warnings.push_back("PLAN ONLY: commands are returned as strings; nothing is executed by this endpoint.");

    plan["steps"] = steps;
    plan["commands"] = commands;
    plan["warnings"] = warnings;

    {
        const std::string joined = join_commands_for_hash(commands);
        const std::string salt =
            std::string("mount=") + resolved_mount + "\n" +
            std::string("btrfs_membership_fp=") + membership_fp + "\n";
        const std::string pid = sha256_hex_lower_evp(joined + "\n" + salt);
        if (!pid.empty()) plan["plan_id"] = pid;
    }

    reply_json(res, 200, json{{"ok", true}, {"plan", plan}}.dump());
});

// ----- POST /api/v4/raid/execute/convert-mode (admin-only) ------------------
// Body: { mount, mode:"single|raid1", plan_id:string, dry_run?:bool, confirm?:bool }
srv.Post("/api/v4/raid/execute/convert-mode", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;

    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}, {"path", users_path}}.dump());
        return;
    }

    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto audit_kv_merge = [&](pqnas::AuditEvent& ev, const json& extra) {
        if (!extra.is_object()) return;
        for (auto it = extra.begin(); it != extra.end(); ++it) {
            const std::string k = pqnas::shorten(it.key(), 64);
            const std::string kk = "x_" + k;
            if (it.value().is_string()) ev.f[kk] = pqnas::shorten(it.value().get<std::string>(), 220);
            else if (it.value().is_number_integer() || it.value().is_number_unsigned()) ev.f[kk] = it.value().dump();
            else if (it.value().is_boolean()) ev.f[kk] = (it.value().get<bool>() ? "true" : "false");
            else ev.f[kk] = pqnas::shorten(it.value().dump(), 220);
        }
    };

    auto audit_common = [&](pqnas::AuditEvent& ev) {
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
    };

    auto audit_fail = [&](const std::string& actor_fp,
                          const std::string& reason,
                          int http,
                          const std::string& detail = "",
                          const json& extra = json::object()) {
        pqnas::AuditEvent ev;
        ev.event = "v4.raid_execute_convert_mode_fail";
        ev.outcome = "fail";
        if (!actor_fp.empty()) ev.f["fingerprint"] = actor_fp;
        ev.f["reason"] = reason;
        ev.f["http"] = std::to_string(http);
        if (!detail.empty()) ev.f["detail"] = pqnas::shorten(detail, 220);
        audit_kv_merge(ev, extra);
        audit_common(ev);
        audit_append(ev);
    };

    auto audit_ok = [&](const std::string& actor_fp,
                        const json& extra = json::object()) {
        pqnas::AuditEvent ev;
        ev.event = "v4.raid_execute_convert_mode_ok";
        ev.outcome = "ok";
        if (!actor_fp.empty()) ev.f["fingerprint"] = actor_fp;
        audit_kv_merge(ev, extra);
        audit_common(ev);
        audit_append(ev);
    };

    std::string actor_fp;
    if (!require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, &actor_fp)) return;
        if (!require_same_origin_for_cookie_mutation(req, res)) return;

    json in;
    try { in = json::parse(req.body.empty() ? "{}" : req.body); }
    catch (...) {
        audit_fail(actor_fp, "bad_json", 400);
        reply_json(res, 400, json({{"ok",false},{"error","bad_request"},{"message","invalid json"}}).dump());
        return;
    }

    std::string mount = in.value("mount", "");
    std::string mode  = in.value("mode", "single");
    bool dry_run      = in.value("dry_run", true);
    bool confirm      = in.value("confirm", false);
    const std::string client_plan_id = in.value("plan_id", "");

    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";
    if (mount.empty()) mount = allowed_prefix + "/data";

    if (!is_abs_path_safe(mount)) {
        audit_fail(actor_fp, "bad_mount", 400, "", json{{"mount", mount}});
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mount"}}.dump());
        return;
    }
    if (mode != "single" && mode != "raid1") {
        audit_fail(actor_fp, "bad_mode", 400, "", json{{"mode", mode}});
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message","mode must be single|raid1"}}.dump());
        return;
    }
    if (client_plan_id.empty()) {
        audit_fail(actor_fp, "missing_plan_id", 400);
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message","missing plan_id"}}.dump());
        return;
    }
    if (!dry_run && !confirm) {
        audit_fail(actor_fp, "confirm_required", 400, "", json{{"dry_run", dry_run}});
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "confirm_required"},
            {"message", "set confirm=true when dry_run=false"}
        }.dump());
        return;
    }

    std::string target_out, fstype_out, source_out;
    int ec_target = 0, ec_fs = 0, ec_src = 0;

    const bool ok_target = run_cmd_capture(
        "/usr/bin/findmnt -no TARGET --target " + sh_quote(mount), &target_out, &ec_target);
    cap_string(target_out, 16 * 1024);
    rtrim_inplace(target_out);

    const bool ok_fs = run_cmd_capture(
        "/usr/bin/findmnt -no FSTYPE --target " + sh_quote(mount), &fstype_out, &ec_fs);
    cap_string(fstype_out, 16 * 1024);
    rtrim_inplace(fstype_out);

    const bool ok_src = run_cmd_capture(
        "/usr/bin/findmnt -no SOURCE --target " + sh_quote(mount), &source_out, &ec_src);
    cap_string(source_out, 16 * 1024);
    rtrim_inplace(source_out);

    if (!ok_target || ec_target != 0 || target_out.empty() ||
        !ok_fs     || ec_fs     != 0 || fstype_out.empty() ||
        !ok_src    || ec_src    != 0 || source_out.empty()) {
        audit_fail(actor_fp, "mount_not_found", 200, "",
                   json{{"mount", mount}, {"ec_target", ec_target}, {"ec_fs", ec_fs}, {"ec_src", ec_src}});
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "mount_not_found"},
            {"mount", mount}
        }.dump());
        return;
    }

    const std::string resolved_mount  = target_out;
    const std::string resolved_source = source_out;
    const std::string resolved_disk   = parent_disk_from_dev(resolved_source);

    if (resolved_mount.rfind(allowed_prefix, 0) != 0) {
        const std::string test_prefix  = "/srv/pqnas-test";
        const std::string test_prefix2 = "/srv/pqnas-test-btrfs";
        if (resolved_mount.rfind(test_prefix, 0) != 0 && resolved_mount.rfind(test_prefix2, 0) != 0) {
            audit_fail(actor_fp, "mount_not_allowed", 200, "",
                       json{{"allowed_prefix", allowed_prefix}, {"resolved_mount", resolved_mount}});
            reply_json(res, 200, json{
                {"ok", false},
                {"error", "mount_not_allowed"},
                {"allowed_prefix", allowed_prefix},
                {"resolved_mount", resolved_mount}
            }.dump());
            return;
        }
    }

    if (fstype_out != "btrfs") {
        audit_fail(actor_fp, "not_btrfs", 200, "",
                   json{{"resolved_mount", resolved_mount}, {"fstype", fstype_out}});
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "not_btrfs"},
            {"resolved_mount", resolved_mount},
            {"fstype", fstype_out}
        }.dump());
        return;
    }

    std::string raw_lsblk;
    json disks_j = storage_list_disks_json(&raw_lsblk);
    json by_path = disks_j.value("by_path", json::object());

    const std::string cmd_show =
        "/usr/bin/sudo -n /usr/bin/btrfs filesystem show " + sh_quote(resolved_mount);

    std::string show_raw;
    int ec_show = 0;
    const bool ok_show = run_cmd_capture(cmd_show, &show_raw, &ec_show);
    cap_string(show_raw, 256 * 1024);

    if (!ok_show || ec_show != 0 || show_raw.empty()) {
        audit_fail(actor_fp, "btrfs_show_failed", 200, "",
                   json{{"resolved_mount", resolved_mount}, {"btrfs_show_rc", ec_show}});
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "btrfs_show_failed"},
            {"resolved_mount", resolved_mount},
            {"btrfs_show_rc", ec_show}
        }.dump());
        return;
    }

    BtrfsShowParsed parsed = parse_btrfs_filesystem_show(show_raw);
    json btrfs_j = btrfs_show_parsed_to_json(parsed, by_path, disks_j.value("by_name", json::object()));

    const std::string membership_fp = btrfs_membership_fingerprint(btrfs_j);
    if (membership_fp.empty()) {
        audit_fail(actor_fp, "membership_fp_failed", 500,
                   "failed to compute btrfs membership fingerprint",
                   json{{"resolved_mount", resolved_mount}});
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "membership_fp_failed"},
            {"message", "failed to compute btrfs membership fingerprint"}
        }.dump());
        return;
    }

    const int total_devices = btrfs_j.value("total_devices", 0);
    if (total_devices < 1) {
        audit_fail(actor_fp, "no_devices_in_filesystem", 400, "",
                   json{{"resolved_mount", resolved_mount}});
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "no_devices_in_filesystem"},
            {"resolved_mount", resolved_mount}
        }.dump());
        return;
    }

    if (mode == "raid1" && total_devices < 2) {
        audit_fail(actor_fp, "raid1_requires_2_devices", 400, "",
                   json{{"resolved_mount", resolved_mount}, {"total_devices", total_devices}});
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "raid1_requires_2_devices"},
            {"resolved_mount", resolved_mount},
            {"total_devices", total_devices}
        }.dump());
        return;
    }

    json commands = json::array();
    if (mode == "raid1") {
        commands.push_back("/usr/bin/sudo -n /usr/bin/btrfs balance start -dconvert=raid1 -mconvert=raid1 " + sh_quote(resolved_mount));
    } else {
        commands.push_back("/usr/bin/sudo -n /usr/bin/btrfs balance start --force -dconvert=single -mconvert=single -sconvert=single " + sh_quote(resolved_mount));
    }

    const std::string joined = join_commands_for_hash(commands);
    const std::string salt =
        std::string("mount=") + resolved_mount + "\n" +
        std::string("btrfs_membership_fp=") + membership_fp + "\n";

    const std::string expected_plan_id = sha256_hex_lower_evp(joined + "\n" + salt);

    if (expected_plan_id.empty()) {
        audit_fail(actor_fp, "plan_id_compute_failed", 500, "",
                   json{{"mount", resolved_mount}, {"mode", mode}});
        reply_json(res, 500, json{{"ok", false}, {"error", "plan_id_compute_failed"}}.dump());
        return;
    }

    if (client_plan_id != expected_plan_id) {
        audit_fail(actor_fp, "plan_mismatch", 400, "",
                   json{{"expected_plan_id", expected_plan_id}, {"provided_plan_id", client_plan_id}});
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "plan_mismatch"},
            {"message", "plan_id does not match server recomputed plan"},
            {"expected_plan_id", expected_plan_id},
            {"provided_plan_id", client_plan_id}
        }.dump());
        return;
    }

    json plan;
    plan["plan_id"] = expected_plan_id;
    plan["mount"] = resolved_mount;
    plan["input_mount"] = mount;
    plan["resolved_mount"] = resolved_mount;
    plan["resolved_source"] = resolved_source;
    if (!resolved_disk.empty()) plan["resolved_disk"] = resolved_disk;
    plan["fstype"] = fstype_out;
    plan["mode"] = mode;
    plan["requires_downtime"] = false;
    plan["commands"] = commands;
    plan["actor_fp"] = actor_fp;
    plan["total_devices"] = total_devices;
    plan["btrfs_membership_fp"] = membership_fp;

    if (dry_run) {
        audit_ok(actor_fp, json{
            {"dry_run", true},
            {"mount", resolved_mount},
            {"mode", mode},
            {"plan_id", expected_plan_id},
            {"total_devices", total_devices},
            {"commands", (int)commands.size()}
        });

        reply_json(res, 200, json{
            {"ok", true},
            {"dry_run", true},
            {"plan", plan}
        }.dump());
        return;
    }

    try {
        json q = raid_enqueue_job_fail_closed(expected_plan_id, resolved_mount, plan, commands);
        audit_ok(actor_fp, json{
            {"dry_run", false},
            {"enqueued", true},
            {"mount", resolved_mount},
            {"mode", mode},
            {"plan_id", expected_plan_id},
            {"total_devices", total_devices}
        });

        q["dry_run"] = false;
        q["plan"] = plan;
        reply_json(res, 200, q.dump());
        return;
    } catch (const std::exception& e) {
        const std::string msg = e.what();

        if (msg == "already_running") {
            audit_fail(actor_fp, "already_running", 409, "",
                       json{{"plan_id", expected_plan_id}, {"mount", resolved_mount}});
            reply_json(res, 409, json{
                {"ok", false},
                {"error", "already_running"},
                {"message", "this plan_id already has a running execution record; refusing replay"},
                {"plan_id", expected_plan_id},
                {"record_path", raid_exec_record_path(expected_plan_id)}
            }.dump());
            return;
        }

        if (msg.rfind("raid_state_dir_failed:", 0) == 0) {
            audit_fail(actor_fp, "raid_state_dir_failed", 500, msg,
                       json{{"plan_id", expected_plan_id}, {"mount", resolved_mount}});
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "raid_state_dir_failed"},
                {"message", "cannot create/verify /run/pqnas/raid; refusing to execute"},
                {"detail", msg}
            }.dump());
            return;
        }

        audit_fail(actor_fp, "enqueue_failed", 500, msg,
                   json{{"plan_id", expected_plan_id}, {"mount", resolved_mount}});
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "enqueue_failed"},
            {"detail", msg}
        }.dump());
        return;
    }
});

// ----- POST /api/v4/raid/plan/remove-device (admin-only, plan-only) ----------
srv.Post("/api/v4/raid/plan/remove-device", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;

    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}, {"path", users_path}}.dump());
        return;
    }
    if (!require_admin_cookie_users(req, res, COOKIE_KEY, users_path, &users)) return;
        if (!require_same_origin_for_cookie_mutation(req, res)) return;

    json in;
    try { in = json::parse(req.body.empty() ? "{}" : req.body); }
    catch (...) {
        reply_json(res, 400, json({{"ok",false},{"error","bad_request"},{"message","invalid json"}}).dump());
        return;
    }

    // Inputs
    std::string mount         = in.value("mount", "");
    std::string remove_device = in.value("remove_device", "");
    bool force                = in.value("force", false);

    // Allowed_prefix + default mount
    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";
    if (mount.empty()) mount = allowed_prefix + "/data";

    // Validate inputs
    if (!is_abs_path_safe(mount)) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mount"}}.dump());
        return;
    }
    if (!is_dev_path_basic_safe(remove_device)) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_device"}, {"message","expected /dev/..."} }.dump());
        return;
    }

    // Resolve mount -> resolved_mount / source / fstype
    std::string target_out, fstype_out, source_out;
    int ec_target = 0, ec_fs = 0, ec_src = 0;

    const bool ok_target = run_cmd_capture(
        "/usr/bin/findmnt -no TARGET --target " + sh_quote(mount), &target_out, &ec_target);
    cap_string(target_out, 16 * 1024);
    rtrim_inplace(target_out);

    const bool ok_fs = run_cmd_capture(
        "/usr/bin/findmnt -no FSTYPE --target " + sh_quote(mount), &fstype_out, &ec_fs);
    cap_string(fstype_out, 16 * 1024);
    rtrim_inplace(fstype_out);

    const bool ok_src = run_cmd_capture(
        "/usr/bin/findmnt -no SOURCE --target " + sh_quote(mount), &source_out, &ec_src);
    cap_string(source_out, 16 * 1024);
    rtrim_inplace(source_out);

    if (!ok_target || ec_target != 0 || target_out.empty() ||
        !ok_fs     || ec_fs     != 0 || fstype_out.empty() ||
        !ok_src    || ec_src    != 0 || source_out.empty()) {
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "mount_not_found"},
            {"mount", mount}
        }.dump());
        return;
    }

    const std::string resolved_mount  = target_out;
    const std::string resolved_source = source_out;
    const std::string resolved_disk   = parent_disk_from_dev(resolved_source);

    const std::string system_root_disk = detect_system_pool_root_disk();

    // Allowlist on resolved mount
    if (resolved_mount.rfind(allowed_prefix, 0) != 0) {
        const std::string test_prefix  = "/srv/pqnas-test";
        const std::string test_prefix2 = "/srv/pqnas-test-btrfs";
        if (resolved_mount.rfind(test_prefix, 0) != 0 &&
            resolved_mount.rfind(test_prefix2, 0) != 0) {
            reply_json(res, 200, json{
                {"ok", false},
                {"error", "mount_not_allowed"},
                {"allowed_prefix", allowed_prefix},
                {"resolved_mount", resolved_mount}
            }.dump());
            return;
        }
    }

    if (fstype_out != "btrfs") {
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "not_btrfs"},
            {"resolved_mount", resolved_mount},
            {"fstype", fstype_out}
        }.dump());
        return;
    }

    // Busy signal (per-mount lock). Plan is allowed while busy, but caller should see it.
    bool busy = false;
    std::string busy_lock;
    {
        const std::string lockp = raid_mount_lock_path(resolved_mount);
        std::error_code ec;
        const bool exists = std::filesystem::exists(lockp, ec);
        if (!ec && exists) {
            busy = true;
            busy_lock = lockp;
        }
    }

    // Load disks allowlist (inherits PQNAS_STORAGE_ALLOW_LOOP policy)
    // For remove, allow /dev/<disk> OR /dev/<partition> (partition may not be in by_path).
    std::string raw_lsblk;
    json disks_j = storage_list_disks_json(&raw_lsblk);
    json by_path = disks_j.value("by_path", json::object());
    json disks   = disks_j.value("disks", json::array());

    // Read btrfs filesystem show so we can map /dev/loop33 -> /dev/loop33p1 member path
    const std::string cmd_show =
        "/usr/bin/sudo -n /usr/bin/btrfs filesystem show " + sh_quote(resolved_mount);

    std::string show_raw;
    int ec_show = 0;
    const bool ok_show = run_cmd_capture(cmd_show, &show_raw, &ec_show);
    cap_string(show_raw, 256 * 1024);

    if (!ok_show || ec_show != 0 || show_raw.empty()) {
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "btrfs_show_failed"},
            {"resolved_mount", resolved_mount},
            {"btrfs_show_rc", ec_show}
        }.dump());
        return;
    }

    // Parse show -> json
    BtrfsShowParsed parsed = parse_btrfs_filesystem_show(show_raw);
    json btrfs_j = btrfs_show_parsed_to_json(parsed, by_path, disks_j.value("by_name", json::object()));

    // Compute stable membership fingerprint used for plan_id salting
    const std::string membership_fp = btrfs_membership_fingerprint(btrfs_j);
    if (membership_fp.empty()) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "membership_fp_failed"},
            {"message", "failed to compute btrfs membership fingerprint"}
        }.dump());
        return;
    }

    // Find member device in filesystem that corresponds to remove_device
    // Accept:
    //   - remove_device == member path (e.g. /dev/loop33p1)
    //   - remove_device == parent_disk (e.g. /dev/loop33)
    std::string member_path;
    std::string parent_disk;
    int member_disk_index = -1;

    if (btrfs_j.contains("devices") && btrfs_j["devices"].is_array()) {
        for (const auto& dev : btrfs_j["devices"]) {
            if (!dev.is_object()) continue;
            const std::string p  = dev.value("path", "");
            const std::string pd = dev.value("parent_disk", "");
            if (p.empty()) continue;

            if (remove_device == p || (!pd.empty() && remove_device == pd)) {
                member_path = p;
                parent_disk = pd;
                if (dev.contains("lsblk_disk_index") && dev["lsblk_disk_index"].is_number_integer()) {
                    member_disk_index = dev["lsblk_disk_index"].get<int>();
                }
                break;
            }
        }
    }

    if (member_path.empty()) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "device_not_in_filesystem"},
            {"remove_device", remove_device},
            {"mount", resolved_mount}
        }.dump());
        return;
    }

    if (!system_root_disk.empty() && !parent_disk.empty() && parent_disk == system_root_disk) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "device_is_system_root_disk"},
            {"system_root_disk", system_root_disk},
            {"remove_device", remove_device},
            {"parent_disk", parent_disk}
        }.dump());
        return;
    }

    // Enforce allowlist: parent_disk must be allowlisted (preferred).
    // If parent_disk missing (rare), fall back to requiring remove_device itself in allowlist.
    if (!by_path.is_object() ||
        ((!parent_disk.empty() && !by_path.contains(parent_disk)) &&
         (parent_disk.empty() && !by_path.contains(remove_device)))) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "device_not_allowed"},
            {"remove_device", remove_device},
            {"member_path", member_path},
            {"parent_disk", parent_disk}
        }.dump());
        return;
    }

    // Refuse removing current filesystem disk by default (same safety posture as add-device)
    if (!resolved_disk.empty() && !parent_disk.empty() && parent_disk == resolved_disk && !force) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "device_is_current_disk"},
            {"resolved_disk", resolved_disk},
            {"parent_disk", parent_disk},
            {"remove_device", remove_device}
        }.dump());
        return;
    }

    // Refuse removing if it's the last remaining device
    const int total_devices = btrfs_j.value("total_devices", 0);
    if (total_devices <= 1) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "cannot_remove_last_device"},
            {"total_devices", total_devices},
            {"member_path", member_path},
            {"mount", resolved_mount}
        }.dump());
        return;
    }

    // Build plan
    json plan;
    plan["mount"] = resolved_mount;
    plan["input_mount"] = mount;
    plan["resolved_mount"] = resolved_mount;
    plan["resolved_source"] = resolved_source;
    if (!resolved_disk.empty()) plan["resolved_disk"] = resolved_disk;
    plan["fstype"] = fstype_out;

    plan["remove_device"] = remove_device;
    plan["remove_member_path"] = member_path;
    if (!parent_disk.empty()) plan["remove_parent_disk"] = parent_disk;
    if (member_disk_index >= 0) plan["remove_disk_index"] = member_disk_index;

    plan["force"] = force;
    plan["requires_downtime"] = false;

    // Busy surface
    plan["busy"] = busy;
    if (busy && !busy_lock.empty()) plan["busy_lock"] = busy_lock;

    // Expose fingerprint (nice for debugging)
    plan["btrfs_membership_fp"] = membership_fp;

    json warnings = json::array();
    json steps = json::array();
    json commands = json::array();

    steps.push_back("Sanity-check: mount resolves to btrfs and is within allowed prefix.");
    steps.push_back("Sanity-check: remove_device maps to a btrfs member device currently in the filesystem.");
    steps.push_back("Sanity-check: refusing to remove last device; refusing current FS disk unless force=true.");

    if (busy) {
        warnings.push_back("BUSY: another RAID operation is currently running for this mount; execute will likely return raid_busy until it finishes.");
    }
    warnings.push_back("Removing a device migrates data off the device and can take a long time.");
    warnings.push_back("If the filesystem cannot relocate all extents (space/profile constraints), btrfs may fail the remove.");
    warnings.push_back("PLAN ONLY: commands are returned as strings; nothing is executed by this endpoint.");

    // If this removal would drop from 2 devices -> 1 device, we must convert off RAID1 first
    if (total_devices == 2) {
        warnings.push_back("Pre-step required: converting metadata/system profiles to SINGLE to allow removing down to one device.");
        warnings.push_back("This includes --force because newer btrfs-progs refuse explicit system-chunk operations otherwise.");
        warnings.push_back("Pre-step required: converting DATA profile to SINGLE too (cannot remove a device while DATA remains RAID1).");
        commands.push_back("/usr/bin/sudo -n /usr/bin/btrfs balance start --force -dconvert=single -mconvert=single -sconvert=single " + sh_quote(resolved_mount));
        steps.push_back("Convert data/metadata/system profiles to SINGLE via balance (--force for system chunks).");
    }

    commands.push_back("/usr/bin/sudo -n /usr/bin/btrfs device remove " + sh_quote(member_path) + " " + sh_quote(resolved_mount));
    steps.push_back("Remove device from filesystem (data migration may take time).");

    plan["steps"] = steps;
    plan["commands"] = commands;
    plan["warnings"] = warnings;

    // plan_id = sha256(joined commands + salt)
    {
        const std::string joined2 = join_commands_for_hash(commands);

        const std::string salt =
            std::string("mount=") + resolved_mount + "\n" +
            std::string("btrfs_membership_fp=") + membership_fp + "\n";

        const std::string pid = sha256_hex_lower_evp(joined2 + "\n" + salt);
        if (!pid.empty()) plan["plan_id"] = pid;
    }

    reply_json(res, 200, json{{"ok", true}, {"plan", plan}}.dump());
});

// ----- POST /api/v4/raid/plan/create-pool (admin-only, plan-only) -------------
srv.Post("/api/v4/raid/plan/create-pool", [&](const httplib::Request& req, httplib::Response& res) {

    pqnas::UsersRegistry users;
    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}}.dump());
        return;
    }
    if (!require_admin_cookie_users(req, res, COOKIE_KEY, users_path, &users)) return;
        if (!require_same_origin_for_cookie_mutation(req, res)) return;

    json in;
    try {
        in = json::parse(req.body.empty() ? "{}" : req.body);
    } catch (...) {
        reply_json(res, 400, json{{"ok", false}, {"error", "invalid_json"}}.dump());
        return;
    }

    const std::string pool_id = trim_copy(in.value("pool_id", ""));
    const std::string mode    = trim_copy(in.value("mode", "single"));
    const bool force          = in.value("force", false);

    json devices_json = json::array();
    if (in.contains("devices")) devices_json = in["devices"];

    if (!std::regex_match(pool_id, std::regex("^[a-z0-9_-]{1,32}$"))) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_pool_id"}}.dump());
        return;
    }

    if (mode != "single" && mode != "raid1") {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mode"}}.dump());
        return;
    }

    json disk_inventory = storage_list_disks_json();

    std::vector<std::string> devices;
    std::string dev_err;
    if (!validate_create_pool_devices(devices_json, disk_inventory, devices, dev_err)) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_devices"},
            {"message", dev_err}
        }.dump());
        return;
    }

    if (mode == "raid1" && devices.size() < 2) {
        reply_json(res, 400, json{{"ok", false}, {"error", "raid1_requires_2_devices"}}.dump());
        return;
    }

    std::string root = getenv_str("PQNAS_STORAGE_ROOT");
    if (root.empty()) root = "/srv/pqnas";

    const std::string mount = root + "/pools/" + pool_id;
    if (std::filesystem::exists(mount)) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "mount_exists"},
            {"mount", mount}
        }.dump());
        return;
    }

    const std::string label = "PQNAS_" + upper_ascii(pool_id);

    const json commands =
        build_create_pool_commands_json(pool_id, mode, devices, force);

    if (!commands.is_array() || commands.empty()) {
        reply_json(res, 500, json{{"ok", false}, {"error", "canonical_plan_empty"}}.dump());
        return;
    }

    json steps = json::array();
    steps.push_back("Create Btrfs filesystem.");
    steps.push_back("Create mount directory.");
    steps.push_back("Mount new pool.");
    steps.push_back("Prepare pool data directory.");

    json warnings = json::array();
    if (force) warnings.push_back("DESTRUCTIVE: devices will be wiped.");

    const std::string plan_nonce = rand_hex_16();
    const std::string plan_id =
        compute_create_pool_plan_id(plan_nonce, pool_id, mode, devices, force, commands);

    if (plan_id.empty()) {
        reply_json(res, 500, json{{"ok", false}, {"error", "plan_id_compute_failed"}}.dump());
        return;
    }

    json plan;
    plan["pool_id"]    = pool_id;
    plan["mount"]      = mount;
    plan["devices"]    = devices;   // canonical validated device list
    plan["mode"]       = mode;
    plan["force"]      = force;
    plan["label"]      = label;
    plan["commands"]   = commands;  // canonical commands from shared helper
    plan["steps"]      = steps;
    plan["warnings"]   = warnings;
    plan["plan_nonce"] = plan_nonce;
    plan["plan_id"]    = plan_id;

    reply_json(res, 200, json{{"ok", true}, {"plan", plan}}.dump());
});

// ----- POST /api/v4/raid/execute/add-device (admin-only) ---------------------
// Body: { mount, new_disk, mode:"single|raid1", force:bool, plan_id:string, dry_run?:bool, confirm?:bool }
srv.Post("/api/v4/raid/execute/add-device", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;

    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}, {"path", users_path}}.dump());
        return;
    }

    // ---- audit helpers ----
    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto audit_kv_merge = [&](pqnas::AuditEvent& ev, const json& extra) {
        if (!extra.is_object()) return;
        for (auto it = extra.begin(); it != extra.end(); ++it) {
            const std::string k = pqnas::shorten(it.key(), 64);
            const std::string kk = "x_" + k;
            if (it.value().is_string()) ev.f[kk] = pqnas::shorten(it.value().get<std::string>(), 220);
            else if (it.value().is_number_integer() || it.value().is_number_unsigned()) ev.f[kk] = it.value().dump();
            else if (it.value().is_boolean()) ev.f[kk] = (it.value().get<bool>() ? "true" : "false");
            else ev.f[kk] = pqnas::shorten(it.value().dump(), 220);
        }
    };

    auto audit_common = [&](pqnas::AuditEvent& ev) {
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
    };

    auto audit_fail = [&](const std::string& actor_fp,
                          const std::string& reason,
                          int http,
                          const std::string& detail = "",
                          const json& extra = json::object()) {
        pqnas::AuditEvent ev;
        ev.event = "v4.raid_execute_add_device_fail";
        ev.outcome = "fail";
        if (!actor_fp.empty()) ev.f["fingerprint"] = actor_fp;
        ev.f["reason"] = reason;
        ev.f["http"] = std::to_string(http);
        if (!detail.empty()) ev.f["detail"] = pqnas::shorten(detail, 220);
        audit_kv_merge(ev, extra);
        audit_common(ev);
        audit_append(ev); // <-- no maybe_auto_rotate_before_append() here
    };

    auto audit_ok = [&](const std::string& actor_fp,
                        const json& extra = json::object()) {
        pqnas::AuditEvent ev;
        ev.event = "v4.raid_execute_add_device_ok";
        ev.outcome = "ok";
        if (!actor_fp.empty()) ev.f["fingerprint"] = actor_fp;
        audit_kv_merge(ev, extra);
        audit_common(ev);
        audit_append(ev); // <-- no maybe_auto_rotate_before_append() here
    };

    // ---- auth (need actor fingerprint for audit) ----
    std::string actor_fp;
    if (!require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, &actor_fp)) return;
    if (!require_same_origin_for_cookie_mutation(req, res)) {
        audit_fail(actor_fp, "origin_mismatch", 403);
        return;
    }

    json in;
    try { in = json::parse(req.body.empty() ? "{}" : req.body); }
    catch (...) {
        audit_fail(actor_fp, "bad_json", 400);
        reply_json(res, 400, json({{"ok",false},{"error","bad_request"},{"message","invalid json"}}).dump());
        return;
    }

    // Inputs
    std::string mount    = in.value("mount", "");
    std::string new_disk = in.value("new_disk", "");
    std::string mode     = in.value("mode", "single"); // single|raid1
    bool force           = in.value("force", false);

    // Safety: default dry_run=true
    bool dry_run = in.value("dry_run", true);
    bool confirm = in.value("confirm", false);

    const std::string client_plan_id = in.value("plan_id", "");

    // Per-attempt nonce (must be provided by plan/add-device and echoed back by UI)
    const std::string client_plan_nonce = in.value("plan_nonce", "");
    if (client_plan_nonce.empty()) {
        audit_fail(actor_fp, "missing_plan_nonce", 400,
                   "", json{{"mount", mount}, {"new_disk", new_disk}, {"mode", mode}, {"dry_run", dry_run}});
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "missing plan_nonce"}
        }.dump());
        return;
    }

    if (!is_hex_64_lower_or_upper(client_plan_id)) {
        audit_fail(actor_fp, "bad_plan_id_format", 400, "",
                   json{{"plan_id", client_plan_id}});
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "plan_id must be 64 hex chars"},
            {"plan_id", client_plan_id}
        }.dump());
        return;
    }
    // Allowed_prefix + default mount
    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";
    if (mount.empty()) mount = allowed_prefix + "/data";

    // Validate inputs
    if (!is_abs_path_safe(mount)) {
        audit_fail(actor_fp, "bad_mount", 400, "", json{{"mount", mount}});
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mount"}}.dump());
        return;
    }
    if (!is_dev_path_basic_safe(new_disk)) {
        audit_fail(actor_fp, "bad_device", 400, "", json{{"new_disk", new_disk}});
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_device"}, {"message","expected /dev/..."} }.dump());
        return;
    }
    if (mode != "single" && mode != "raid1") {
        audit_fail(actor_fp, "bad_mode", 400, "", json{{"mode", mode}});
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message","mode must be single|raid1"} }.dump());
        return;
    }
    if (client_plan_id.empty()) {
        audit_fail(actor_fp, "missing_plan_id", 400);
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message","missing plan_id"} }.dump());
        return;
    }

    // If not dry-run, require explicit confirm=true
    if (!dry_run && !confirm) {
        audit_fail(actor_fp, "confirm_required", 400, "", json{{"dry_run", dry_run}});
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "confirm_required"},
            {"message", "set confirm=true when dry_run=false"}
        }.dump());
        return;
    }

    // Resolve mount -> resolved_mount / source / fstype
    std::string target_out, fstype_out, source_out;
    int ec_target = 0, ec_fs = 0, ec_src = 0;

    const bool ok_target = run_cmd_capture(
        "/usr/bin/findmnt -no TARGET --target " + sh_quote(mount), &target_out, &ec_target);
    cap_string(target_out, 16 * 1024);
    rtrim_inplace(target_out);

    const bool ok_fs = run_cmd_capture(
        "/usr/bin/findmnt -no FSTYPE --target " + sh_quote(mount), &fstype_out, &ec_fs);
    cap_string(fstype_out, 16 * 1024);
    rtrim_inplace(fstype_out);

    const bool ok_src = run_cmd_capture(
        "/usr/bin/findmnt -no SOURCE --target " + sh_quote(mount), &source_out, &ec_src);
    cap_string(source_out, 16 * 1024);
    rtrim_inplace(source_out);

    if (!ok_target || ec_target != 0 || target_out.empty() ||
        !ok_fs     || ec_fs     != 0 || fstype_out.empty() ||
        !ok_src    || ec_src    != 0 || source_out.empty()) {

        audit_fail(actor_fp, "mount_not_found", 200, "",
                   json{{"mount", mount}, {"ec_target", ec_target}, {"ec_fs", ec_fs}, {"ec_src", ec_src}});
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "mount_not_found"},
            {"mount", mount}
        }.dump());
        return;
    }

    const std::string resolved_mount  = target_out;
    const std::string resolved_source = source_out;
    const std::string resolved_disk   = parent_disk_from_dev(resolved_source);

    const std::string system_root_disk = detect_system_pool_root_disk();
    if (!system_root_disk.empty() && new_disk == system_root_disk) {
        audit_fail(actor_fp, "device_is_system_root_disk", 400, "",
                   json{{"system_root_disk", system_root_disk}, {"new_disk", new_disk}});
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "device_is_system_root_disk"},
            {"system_root_disk", system_root_disk},
            {"new_disk", new_disk}
        }.dump());
        return;
    }

    // Allowlist on resolved mount
    if (resolved_mount.rfind(allowed_prefix, 0) != 0) {
        const std::string test_prefix  = "/srv/pqnas-test";
        const std::string test_prefix2 = "/srv/pqnas-test-btrfs";
        if (resolved_mount.rfind(test_prefix, 0) != 0 && resolved_mount.rfind(test_prefix2, 0) != 0) {
            audit_fail(actor_fp, "mount_not_allowed", 200, "",
                       json{{"allowed_prefix", allowed_prefix}, {"resolved_mount", resolved_mount}});
            reply_json(res, 200, json{
                {"ok", false},
                {"error", "mount_not_allowed"},
                {"allowed_prefix", allowed_prefix},
                {"resolved_mount", resolved_mount}
            }.dump());
            return;
        }
    }

    if (fstype_out != "btrfs") {
        audit_fail(actor_fp, "not_btrfs", 200, "",
                   json{{"resolved_mount", resolved_mount}, {"fstype", fstype_out}});
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "not_btrfs"},
            {"resolved_mount", resolved_mount},
            {"fstype", fstype_out}
        }.dump());
        return;
    }

    // Read btrfs filesystem show (used to salt plan_id so add->remove->add works)
    const std::string cmd_show =
        "/usr/bin/sudo -n /usr/bin/btrfs filesystem show " + sh_quote(resolved_mount);

    std::string show_raw;
    int ec_show = 0;
    const bool ok_show = run_cmd_capture(cmd_show, &show_raw, &ec_show);
    cap_string(show_raw, 256 * 1024);

    if (!ok_show || ec_show != 0 || show_raw.empty()) {
        audit_fail(actor_fp, "btrfs_show_failed", 200, "",
                   json{{"resolved_mount", resolved_mount}, {"btrfs_show_rc", ec_show}});
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "btrfs_show_failed"},
            {"resolved_mount", resolved_mount},
            {"btrfs_show_rc", ec_show}
        }.dump());
        return;
    }

    // Load disks allowlist (inherits PQNAS_STORAGE_ALLOW_LOOP policy)
    std::string raw_lsblk;
    json disks_j = storage_list_disks_json(&raw_lsblk);
    json by_path = disks_j.value("by_path", json::object());
    json disks   = disks_j.value("disks", json::array());

    // Build stable membership fingerprint used for plan_id salting
    BtrfsShowParsed parsed2 = parse_btrfs_filesystem_show(show_raw);
    json btrfs2 = btrfs_show_parsed_to_json(parsed2, by_path, disks_j.value("by_name", json::object()));
    const std::string membership_fp = btrfs_membership_fingerprint(btrfs2);

    if (!by_path.is_object() || !by_path.contains(new_disk)) {
        audit_fail(actor_fp, "device_not_allowed", 400, "",
                   json{{"new_disk", new_disk}, {"resolved_mount", resolved_mount}});
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "device_not_allowed"},
            {"new_disk", new_disk}
        }.dump());
        return;
    }

    int disk_index = -1;
    try { disk_index = by_path[new_disk].get<int>(); } catch (...) { disk_index = -1; }

    if (disk_index < 0 || !disks.is_array() || disk_index >= (int)disks.size()) {
        audit_fail(actor_fp, "lsblk_index_error", 500, "",
                   json{{"new_disk", new_disk}, {"disk_index", disk_index}});
        reply_json(res, 500, json{{"ok", false}, {"error", "lsblk_index_error"}}.dump());
        return;
    }

    json d = disks[disk_index];

    // Hard-refuse disks that have ANY mountpoints anywhere (fail-closed even with force)
    {
        json mpcheck = lsblk_disk_mountpoints_json(new_disk);
        if (mpcheck.value("ok", false) && mpcheck.contains("mountpoints") && mpcheck["mountpoints"].is_array()) {
            if (!mpcheck["mountpoints"].empty()) {
                audit_fail(actor_fp, "disk_in_use", 400, "",
                           json{{"new_disk", new_disk}, {"disk_index", disk_index}});
                reply_json(res, 400, json{
                    {"ok", false},
                    {"error", "disk_in_use"},
                    {"new_disk", new_disk},
                    {"disk_index", disk_index},
                    {"model", d.value("model","")},
                    {"serial", d.value("serial","")},
                    {"mountpoints", mpcheck["mountpoints"]}
                }.dump());
                return;
            }
        } else {
            audit_fail(actor_fp, "disk_in_use_check_failed", 500, "",
                       json{{"new_disk", new_disk}});
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "disk_in_use_check_failed"},
                {"new_disk", new_disk},
                {"detail", mpcheck}
            }.dump());
            return;
        }
    }

    // Refuse adding the same disk the FS is already on (safety)
    if (!resolved_disk.empty() && new_disk == resolved_disk) {
        audit_fail(actor_fp, "device_is_current_disk", 400, "",
                   json{{"resolved_disk", resolved_disk}, {"new_disk", new_disk}});
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "device_is_current_disk"},
            {"resolved_disk", resolved_disk},
            {"new_disk", new_disk}
        }.dump());
        return;
    }

    const int children = d.value("children", 0);
    const uint64_t new_disk_size = d.value("size_bytes", (uint64_t)0);

    // If disk has partitions: refuse unless force (strict default)
    if (children > 0 && !force) {
        json plan_tmp;
        plan_tmp["mount"] = resolved_mount;
        plan_tmp["new_disk"] = new_disk;
        plan_tmp["new_disk_index"] = disk_index;
        plan_tmp["new_disk_size_bytes"] = new_disk_size;
        plan_tmp["mode"] = mode;
        plan_tmp["force"] = force;
        plan_tmp["requires_downtime"] = false;
        plan_tmp["children"] = children;
        plan_tmp["warnings"] = json::array({"new_disk_has_partitions", "refusing_to_execute_destructive_partitioning_without_force=true"});
        audit_fail(actor_fp, "disk_not_empty", 200, "",
                   json{{"mount", resolved_mount}, {"new_disk", new_disk}, {"children", children}, {"force", force}});
        reply_json(res, 200, json{{"ok", false}, {"error", "disk_not_empty"}, {"plan", plan_tmp}}.dump());
        return;
    }

    const std::string new_part = part1_path_from_disk(new_disk);
    if (new_part.empty()) {
        audit_fail(actor_fp, "bad_device_partition", 400, "", json{{"new_disk", new_disk}});
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_device"}}.dump());
        return;
    }

    // -------- Build commands exactly like plan endpoint --------
    json commands = json::array();
    commands.push_back("/usr/bin/sudo -n /usr/sbin/sgdisk --zap-all " + sh_quote(new_disk));
    commands.push_back("/usr/bin/sudo -n /usr/sbin/wipefs -a " + sh_quote(new_disk));
    commands.push_back("/usr/bin/sudo -n /usr/sbin/sgdisk -n 1:0:0 -t 1:8300 -c 1:PQNAS_BTRFS " + sh_quote(new_disk));
    commands.push_back("/usr/bin/sudo -n /usr/sbin/partprobe " + sh_quote(new_disk));
    commands.push_back("/usr/bin/sudo -n /usr/bin/udevadm settle");
    commands.push_back("WAIT_BLOCK " + new_part + " 2000");
    commands.push_back("/usr/bin/sudo -n /usr/bin/btrfs device add " + sh_quote(new_part) + " " + sh_quote(resolved_mount));
    if (mode == "raid1") {
        commands.push_back("/usr/bin/sudo -n /usr/bin/btrfs balance start -dconvert=raid1 -mconvert=raid1 " + sh_quote(resolved_mount));
    }

    // plan_id check (must match exactly)
    const std::string joined = join_commands_for_hash(commands);

    // Salt with current FS membership/state (MUST match plan/add-device).
    const std::string salt =
        std::string("mount=") + resolved_mount + "\n" +
        std::string("btrfs_membership_fp=") + membership_fp + "\n";

    // include per-attempt nonce
    const std::string expected_plan_id =
        sha256_hex_lower_evp(joined + "\n" + salt + "plan_nonce=" + client_plan_nonce + "\n");

    if (expected_plan_id.empty()) {
        audit_fail(actor_fp, "plan_id_compute_failed", 500,
                   "", json{{"mount", resolved_mount}, {"new_disk", new_disk}});
        reply_json(res, 500, json{{"ok", false}, {"error", "plan_id_compute_failed"}}.dump());
        return;
    }
    if (client_plan_id != expected_plan_id) {
        audit_fail(actor_fp, "plan_mismatch", 400, "",
                   json{{"expected_plan_id", expected_plan_id}, {"provided_plan_id", client_plan_id}});
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "plan_mismatch"},
            {"message", "plan_id does not match server recomputed plan"},
            {"expected_plan_id", expected_plan_id},
            {"provided_plan_id", client_plan_id}
        }.dump());
        return;
    }

    // Prepare response plan payload (for caller/UI)
    json plan;
    plan["plan_id"] = expected_plan_id;
    plan["plan_nonce"] = client_plan_nonce;
    plan["btrfs_membership_fp"] = membership_fp;
    plan["mount"] = resolved_mount;
    plan["input_mount"] = mount;
    plan["resolved_mount"] = resolved_mount;
    plan["resolved_source"] = resolved_source;
    if (!resolved_disk.empty()) plan["resolved_disk"] = resolved_disk;
    plan["fstype"] = fstype_out;
    plan["new_disk"] = new_disk;
    plan["new_partition"] = new_part;
    plan["new_disk_index"] = disk_index;
    plan["new_disk_size_bytes"] = new_disk_size;
    plan["mode"] = mode;
    plan["force"] = force;
    plan["requires_downtime"] = false;
    plan["commands"] = commands;
	plan["actor_fp"] = actor_fp;

    // Preflight AFTER plan-id verification: if device already present, return idempotent success
    if (btrfs_filesystem_has_device(resolved_mount, new_part)) {
        json status = storage_btrfs_status_json(resolved_mount);
        status["input_mount"] = mount;
        status["resolved_mount"] = resolved_mount;
        status["resolved_source"] = resolved_source;
        status["resolved_disk"] = resolved_disk;
        status["fstype"] = fstype_out;

        audit_ok(actor_fp, json{
            {"dry_run", dry_run},
            {"skipped", true},
            {"skip_reason", "already_in_filesystem"},
            {"mount", resolved_mount},
            {"new_disk", new_disk},
            {"new_partition", new_part},
            {"mode", mode},
            {"force", force},
            {"plan_id", expected_plan_id}
        });

        reply_json(res, 200, json{
            {"ok", true},
            {"dry_run", dry_run},
            {"skipped", true},
            {"skip_reason", "already_in_filesystem"},
            {"mount", resolved_mount},
            {"device", new_part},
            {"plan_id", expected_plan_id},
            {"plan", plan},
            {"post_status", status}
        }.dump());
        return;
    }

    if (dry_run) {
        audit_ok(actor_fp, json{
            {"dry_run", true},
            {"mount", resolved_mount},
            {"new_disk", new_disk},
            {"new_partition", new_part},
            {"mode", mode},
            {"force", force},
            {"plan_id", expected_plan_id},
            {"commands", (int)commands.size()}
        });

        reply_json(res, 200, json{
            {"ok", true},
            {"dry_run", true},
            {"plan", plan}
        }.dump());
        return;
    }

    // Async enqueue (fail-closed): create canonical queued record + return immediately.
    try {
        json q = raid_enqueue_job_fail_closed(expected_plan_id, resolved_mount, plan, commands);
			json extra = {
  			{"dry_run", false},
  			{"enqueued", true},
  			{"mount", resolved_mount},
  			{"new_disk", new_disk},
  			{"new_partition", new_part},
  			{"mode", mode},
  			{"force", force},
  			{"plan_id", expected_plan_id},
  			{"plan_nonce", client_plan_nonce}
			};

			try {
	    		if (q.contains("job_id")) extra["job_id"] = q["job_id"];
			} catch (...) {}

		audit_ok(actor_fp, extra);

        // Keep UX fields consistent with old response shape
        q["dry_run"] = false;
        q["plan"] = plan;
        reply_json(res, 200, q.dump());
        return;
    } catch (const std::exception& e) {
        const std::string msg = e.what();

        if (msg == "already_running") {
            audit_fail(actor_fp, "already_running", 409, "",
                       json{{"plan_id", expected_plan_id}, {"mount", resolved_mount}});
            reply_json(res, 409, json{
                {"ok", false},
                {"error", "already_running"},
                {"message", "this plan_id already has a running execution record; refusing replay"},
                {"plan_id", expected_plan_id},
                {"record_path", raid_exec_record_path(expected_plan_id)}
            }.dump());
            return;
        }

        if (msg.rfind("raid_state_dir_failed:", 0) == 0) {
            audit_fail(actor_fp, "raid_state_dir_failed", 500, msg,
                       json{{"plan_id", expected_plan_id}, {"mount", resolved_mount}});
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "raid_state_dir_failed"},
                {"message", "cannot create/verify /run/pqnas/raid; refusing to execute"},
                {"detail", msg}
            }.dump());
            return;
        }

        audit_fail(actor_fp, "enqueue_failed", 500, msg,
                   json{{"plan_id", expected_plan_id}, {"mount", resolved_mount}});
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "enqueue_failed"},
            {"detail", msg}
        }.dump());
        return;
    }
});
// ----- POST /api/v4/raid/execute/destroy-pool (admin-only) --------------------
// Body: { mount, plan_id, plan_nonce, confirm:true, force_wipe?:bool }
srv.Post("/api/v4/raid/execute/destroy-pool", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;
    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}}.dump());
        return;
    }

    // ---- audit helpers ----
    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto audit_kv_merge = [&](pqnas::AuditEvent& ev, const json& extra) {
        if (!extra.is_object()) return;
        for (auto it = extra.begin(); it != extra.end(); ++it) {
            const std::string k  = pqnas::shorten(it.key(), 64);
            const std::string kk = "x_" + k;
            if (it.value().is_string()) ev.f[kk] = pqnas::shorten(it.value().get<std::string>(), 220);
            else if (it.value().is_number_integer() || it.value().is_number_unsigned()) ev.f[kk] = it.value().dump();
            else if (it.value().is_boolean()) ev.f[kk] = (it.value().get<bool>() ? "true" : "false");
            else ev.f[kk] = pqnas::shorten(it.value().dump(), 220);
        }
    };

    auto audit_common = [&](pqnas::AuditEvent& ev) {
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
    };

    auto audit_fail = [&](const std::string& actor_fp,
                          const std::string& reason,
                          int http,
                          const std::string& detail = "",
                          const json& extra = json::object()) {
        pqnas::AuditEvent ev;
        ev.event   = "v4.raid_execute_destroy_pool_fail";
        ev.outcome = "fail";
        if (!actor_fp.empty()) ev.f["fingerprint"] = actor_fp;
        ev.f["reason"] = reason;
        ev.f["http"]   = std::to_string(http);
        if (!detail.empty()) ev.f["detail"] = pqnas::shorten(detail, 220);
        audit_kv_merge(ev, extra);
        audit_common(ev);
        audit_append(ev);
    };

    auto audit_ok = [&](const std::string& actor_fp,
                        const json& extra = json::object()) {
        pqnas::AuditEvent ev;
        ev.event   = "v4.raid_execute_destroy_pool_ok";
        ev.outcome = "ok";
        if (!actor_fp.empty()) ev.f["fingerprint"] = actor_fp;
        audit_kv_merge(ev, extra);
        audit_common(ev);
        audit_append(ev);
    };

    // ---- auth (need actor fingerprint for audit) ----
    std::string actor_fp;
    if (!require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, &actor_fp)) return;
        if (!require_same_origin_for_cookie_mutation(req, res)) return;

    json in;
    try { in = json::parse(req.body.empty() ? "{}" : req.body); }
    catch (...) {
        audit_fail(actor_fp, "invalid_json", 400);
        reply_json(res, 400, json{{"ok", false}, {"error", "invalid_json"}}.dump());
        return;
    }

    std::string mount = in.value("mount", "");
    const std::string plan_id    = in.value("plan_id", "");
    const std::string plan_nonce = in.value("plan_nonce", "");
    const bool confirm           = in.value("confirm", false);
    const bool force_wipe        = in.value("force_wipe", false);

    // Will be filled after findmnt succeeds; used by audit_ctx().
    std::string resolved_mount;
    std::string resolved_source;

    // Consistent audit context keys across create-pool / worker.
    auto audit_ctx = [&](const json& extra = json::object()) -> json {
        json j = {
            {"plan_id", plan_id},
            {"plan_nonce", plan_nonce},
            {"job_id", plan_id},
            {"op", "destroy-pool"},

            {"mount", mount},
            {"resolved_mount", resolved_mount.empty() ? mount : resolved_mount},
            {"resolved_source", resolved_source},

            {"confirm", confirm},
            {"force_wipe", force_wipe},

            {"recp", raid_exec_record_path(plan_id)}
        };
        if (extra.is_object()) {
            for (auto it = extra.begin(); it != extra.end(); ++it) j[it.key()] = it.value();
        }
        return j;
    };

    if (!confirm || plan_id.empty() || plan_nonce.empty() || mount.empty()) {
        audit_fail(actor_fp, "bad_request_missing_fields", 400, "",
                   audit_ctx(json{{"message", "requires confirm=true, plan_id, plan_nonce, mount"}}));
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "requires confirm=true, plan_id, plan_nonce, mount"}
        }.dump());
        return;
    }

    // Plan id format: allow safe printable ids (align with create-pool usage like "usbclean1-plan-1")
    if (!std::regex_match(plan_id, std::regex("^[a-zA-Z0-9_.:-]{1,96}$"))) {
        audit_fail(actor_fp, "bad_plan_id_format", 400, "",
                   audit_ctx(json{{"plan_id", plan_id}}));
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "plan_id contains invalid characters"},
            {"plan_id", plan_id}
        }.dump());
        return;
    }

    // Basic mount validation
    if (!is_abs_path_safe(mount)) {
        audit_fail(actor_fp, "bad_mount", 400, "", audit_ctx());
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mount"}}.dump());
        return;
    }

    // Allow only destroying pools under PQNAS_STORAGE_ROOT/pools
    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";

    const std::string pools_root = allowed_prefix + "/pools/";
    if (mount.rfind(pools_root, 0) != 0) {
        audit_fail(actor_fp, "mount_not_allowed", 400, "",
                   audit_ctx(json{{"pools_root", pools_root}}));
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "mount_not_allowed"},
            {"message", "destroy is only allowed under PQNAS_STORAGE_ROOT/pools"},
            {"mount", mount},
            {"pools_root", pools_root}
        }.dump());
        return;
    }

    // Must be a mounted btrfs target (we want stable membership list before umount)
    std::string target_out, fstype_out, source_out;
    int ec_target = 0, ec_fs = 0, ec_src = 0;

    const bool ok_target = run_cmd_capture(
        "/usr/bin/findmnt -no TARGET --target " + sh_quote(mount), &target_out, &ec_target);
    cap_string(target_out, 16 * 1024);
    rtrim_inplace(target_out);

    const bool ok_fs = run_cmd_capture(
        "/usr/bin/findmnt -no FSTYPE --target " + sh_quote(mount), &fstype_out, &ec_fs);
    cap_string(fstype_out, 16 * 1024);
    rtrim_inplace(fstype_out);

    const bool ok_src = run_cmd_capture(
        "/usr/bin/findmnt -no SOURCE --target " + sh_quote(mount), &source_out, &ec_src);
    cap_string(source_out, 16 * 1024);
    rtrim_inplace(source_out);

    if (!ok_target || ec_target != 0 || target_out.empty() ||
        !ok_fs     || ec_fs     != 0 || fstype_out.empty() ||
        !ok_src    || ec_src    != 0 || source_out.empty()) {

        audit_fail(actor_fp, "mount_not_found", 400, "",
                   audit_ctx(json{
                       {"findmnt_rc_target", ec_target},
                       {"findmnt_rc_fs", ec_fs},
                       {"findmnt_rc_src", ec_src}
                   }));
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "mount_not_found"},
            {"mount", mount}
        }.dump());
        return;
    }

    resolved_mount  = target_out;
    resolved_source = source_out;

    if (fstype_out != "btrfs") {
        audit_fail(actor_fp, "not_btrfs", 400, "",
                   audit_ctx(json{{"fstype", fstype_out}}));
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "not_btrfs"},
            {"resolved_mount", resolved_mount},
            {"fstype", fstype_out}
        }.dump());
        return;
    }

    // Lock check only (worker owns lock lifecycle)
    {
        std::string raid_dir_err;
        if (!ensure_dir_fail_closed("/run/pqnas/raid", &raid_dir_err)) {
            audit_fail(actor_fp, "raid_state_dir_failed", 500, raid_dir_err,
                       audit_ctx(json{{"resolved_mount", resolved_mount}}));
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "raid_state_dir_failed"},
                {"detail", raid_dir_err}
            }.dump());
            return;
        }

        const std::string lockp = raid_mount_lock_path(resolved_mount);
        std::error_code ec;
        if (std::filesystem::exists(lockp, ec)) {
            audit_fail(actor_fp, "raid_busy", 409, "",
                       audit_ctx(json{{"state","blocked"}, {"lock_path", lockp}}));
            reply_json(res, 409, json{{"ok", false}, {"error", "raid_busy"}, {"lock_path", lockp}}.dump());
            return;
        }
    }

    // Read membership BEFORE umount so we can optionally wipe member disks
    const std::string cmd_show =
        "/usr/bin/sudo -n /usr/bin/btrfs filesystem show " + sh_quote(resolved_mount) + " 2>&1";

    std::string show_raw;
    int ec_show = 0;
    const bool ok_show = run_cmd_capture(cmd_show, &show_raw, &ec_show);
    cap_string(show_raw, 256 * 1024);
    rtrim_inplace(show_raw);

    if (!ok_show || ec_show != 0 || show_raw.empty()) {
        audit_fail(actor_fp, "btrfs_show_failed", 500, "",
                   audit_ctx(json{{"btrfs_show_rc", ec_show}}));
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "btrfs_show_failed"},
            {"resolved_mount", resolved_mount},
            {"btrfs_show_rc", ec_show}
        }.dump());
        return;
    }

    // Parse show -> get member device paths
    std::string raw_lsblk;
    json disks_j = storage_list_disks_json(&raw_lsblk);
    json by_path = disks_j.value("by_path", json::object());

    BtrfsShowParsed parsed = parse_btrfs_filesystem_show(show_raw);
    json btrfs_j = btrfs_show_parsed_to_json(parsed, by_path, disks_j.value("by_name", json::object()));

    std::vector<std::string> member_devs;
    if (btrfs_j.contains("devices") && btrfs_j["devices"].is_array()) {
        for (auto& d : btrfs_j["devices"]) {
            const std::string p = d.value("path", "");
            if (!p.empty() && p.rfind("/dev/", 0) == 0) member_devs.push_back(p);
        }
    }

    // Build plan + commands for worker
    json plan;
    plan["plan_id"]         = plan_id;
    plan["plan_nonce"]      = plan_nonce;
    plan["operation"]       = "destroy-pool";
    plan["mount"]           = resolved_mount;
    plan["input_mount"]     = mount;
    plan["resolved_mount"]  = resolved_mount;
    plan["resolved_source"] = resolved_source;
    plan["force_wipe"]      = force_wipe;
    plan["member_devices"]  = member_devs;
    plan["actor_fp"]        = actor_fp;

    json commands = json::array();

    commands.push_back("/usr/bin/sudo -n /usr/bin/udevadm settle");
    commands.push_back("/usr/bin/sudo -n /bin/umount " + sh_quote(resolved_mount));
    commands.push_back("/usr/bin/sudo -n /usr/bin/btrfs device scan");

    if (force_wipe) {
        for (const auto& dev : member_devs) {
            commands.push_back("/usr/bin/sudo -n /usr/sbin/wipefs -a " + sh_quote(dev));
            commands.push_back("/usr/bin/sudo -n /usr/sbin/sgdisk --zap-all " + sh_quote(dev));
        }
        commands.push_back("/usr/bin/sudo -n /usr/bin/udevadm settle");
    }

    commands.push_back(std::string("POOLS_CFG_REMOVE ") + resolved_mount);
    commands.push_back("/usr/bin/sudo -n /bin/rmdir " + sh_quote(resolved_mount));

    // Enqueue (fail-closed)
    try {
        json q = raid_enqueue_job_fail_closed(plan_id, resolved_mount, plan, commands);
        q["plan"] = plan;

        // IMPORTANT: this is "enqueue accepted", NOT "job started" (worker emits job lifecycle)
        audit_ok(actor_fp, audit_ctx(json{
            {"state", "queued"},
            {"member_devices_n", (int)member_devs.size()},
            {"commands_total", (int)commands.size()}
        }));

        reply_json(res, 200, q.dump());
        return;

    } catch (const std::exception& e) {
        const std::string msg = e.what();

        if (msg == "already_running") {
            audit_fail(actor_fp, "already_running", 409, "",
                       audit_ctx(json{{"state","blocked"}}));
            reply_json(res, 409, json{
                {"ok", false},
                {"error", "already_running"},
                {"message", "this plan_id already has a running execution record; refusing replay"},
                {"plan_id", plan_id},
                {"record_path", raid_exec_record_path(plan_id)}
            }.dump());
            return;
        }

        audit_fail(actor_fp, "enqueue_failed", 500, msg,
                   audit_ctx(json{{"state","enqueue_failed"}}));

        reply_json(res, 500, json{
            {"ok", false},
            {"error", "enqueue_failed"},
            {"detail", msg}
        }.dump());
        return;
    }
});
// ----- POST /api/v4/raid/execute/remove-device (admin-only) ------------------
// Body: { mount, remove_device, force:bool, plan_id:string, dry_run?:bool, confirm?:bool }
srv.Post("/api/v4/raid/execute/remove-device", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;

    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}, {"path", users_path}}.dump());
        return;
    }

    // ---- audit helpers ----
    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto audit_kv_merge = [&](pqnas::AuditEvent& ev, const json& extra) {
        if (!extra.is_object()) return;
        for (auto it = extra.begin(); it != extra.end(); ++it) {
            const std::string k = pqnas::shorten(it.key(), 64);
            const std::string kk = "x_" + k;
            if (it.value().is_string()) ev.f[kk] = pqnas::shorten(it.value().get<std::string>(), 220);
            else if (it.value().is_number_integer() || it.value().is_number_unsigned()) ev.f[kk] = it.value().dump();
            else if (it.value().is_boolean()) ev.f[kk] = (it.value().get<bool>() ? "true" : "false");
            else ev.f[kk] = pqnas::shorten(it.value().dump(), 220);
        }
    };

    auto audit_common = [&](pqnas::AuditEvent& ev) {
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
    };

    auto audit_fail = [&](const std::string& actor_fp,
                          const std::string& reason,
                          int http,
                          const std::string& detail = "",
                          const json& extra = json::object()) {
        pqnas::AuditEvent ev;
        ev.event = "v4.raid_execute_remove_device_fail";
        ev.outcome = "fail";
        if (!actor_fp.empty()) ev.f["fingerprint"] = actor_fp;
        ev.f["reason"] = reason;
        ev.f["http"] = std::to_string(http);
        if (!detail.empty()) ev.f["detail"] = pqnas::shorten(detail, 220);
        audit_kv_merge(ev, extra);
        audit_common(ev);
        audit_append(ev);
    };

    auto audit_ok = [&](const std::string& actor_fp,
                        const json& extra = json::object()) {
        pqnas::AuditEvent ev;
        ev.event = "v4.raid_execute_remove_device_ok";
        ev.outcome = "ok";
        if (!actor_fp.empty()) ev.f["fingerprint"] = actor_fp;
        audit_kv_merge(ev, extra);
        audit_common(ev);
        audit_append(ev);
    };

    // ---- auth (need actor fingerprint for audit) ----
    std::string actor_fp;
    if (!require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, &actor_fp)) return;
    if (!require_same_origin_for_cookie_mutation(req, res)) {
        audit_fail(actor_fp, "origin_mismatch", 403);
        return;
    }

    json in;
    try { in = json::parse(req.body.empty() ? "{}" : req.body); }
    catch (...) {
        audit_fail(actor_fp, "bad_json", 400);
        reply_json(res, 400, json({{"ok",false},{"error","bad_request"},{"message","invalid json"}}).dump());
        return;
    }

    // Inputs
    std::string mount         = in.value("mount", "");
    std::string remove_device = in.value("remove_device", "");
    bool force                = in.value("force", false);

    // Safety: default dry_run=true
    bool dry_run = in.value("dry_run", true);
    bool confirm = in.value("confirm", false);

    const std::string client_plan_id = in.value("plan_id", "");

    // Allowed_prefix + default mount
    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";
    if (mount.empty()) mount = allowed_prefix + "/data";

    // Validate inputs
    if (!is_abs_path_safe(mount)) {
        audit_fail(actor_fp, "bad_mount", 400, "", json{{"mount", mount}});
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mount"}}.dump());
        return;
    }
    if (!is_dev_path_basic_safe(remove_device)) {
        audit_fail(actor_fp, "bad_device", 400, "", json{{"remove_device", remove_device}});
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_device"}, {"message","expected /dev/..."} }.dump());
        return;
    }
    if (client_plan_id.empty()) {
        audit_fail(actor_fp, "missing_plan_id", 400);
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message","missing plan_id"} }.dump());
        return;
    }

    // If not dry-run, require explicit confirm=true
    if (!dry_run && !confirm) {
        audit_fail(actor_fp, "confirm_required", 400, "", json{{"dry_run", dry_run}});
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "confirm_required"},
            {"message", "set confirm=true when dry_run=false"}
        }.dump());
        return;
    }

    // Resolve mount -> resolved_mount / source / fstype
    std::string target_out, fstype_out, source_out;
    int ec_target = 0, ec_fs = 0, ec_src = 0;

    const bool ok_target = run_cmd_capture(
        "/usr/bin/findmnt -no TARGET --target " + sh_quote(mount), &target_out, &ec_target);
    cap_string(target_out, 16 * 1024);
    rtrim_inplace(target_out);

    const bool ok_fs = run_cmd_capture(
        "/usr/bin/findmnt -no FSTYPE --target " + sh_quote(mount), &fstype_out, &ec_fs);
    cap_string(fstype_out, 16 * 1024);
    rtrim_inplace(fstype_out);

    const bool ok_src = run_cmd_capture(
        "/usr/bin/findmnt -no SOURCE --target " + sh_quote(mount), &source_out, &ec_src);
    cap_string(source_out, 16 * 1024);
    rtrim_inplace(source_out);

    if (!ok_target || ec_target != 0 || target_out.empty() ||
        !ok_fs     || ec_fs     != 0 || fstype_out.empty() ||
        !ok_src    || ec_src    != 0 || source_out.empty()) {

        audit_fail(actor_fp, "mount_not_found", 200, "",
                   json{{"mount", mount}, {"ec_target", ec_target}, {"ec_fs", ec_fs}, {"ec_src", ec_src}});
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "mount_not_found"},
            {"mount", mount}
        }.dump());
        return;
    }

    const std::string resolved_mount  = target_out;
    const std::string resolved_source = source_out;
    const std::string resolved_disk   = parent_disk_from_dev(resolved_source);
    const std::string system_root_disk = detect_system_pool_root_disk();

    // Allowlist on resolved mount
    if (resolved_mount.rfind(allowed_prefix, 0) != 0) {
        const std::string test_prefix  = "/srv/pqnas-test";
        const std::string test_prefix2 = "/srv/pqnas-test-btrfs";
        if (resolved_mount.rfind(test_prefix, 0) != 0 && resolved_mount.rfind(test_prefix2, 0) != 0) {
            audit_fail(actor_fp, "mount_not_allowed", 200, "",
                       json{{"allowed_prefix", allowed_prefix}, {"resolved_mount", resolved_mount}});
            reply_json(res, 200, json{
                {"ok", false},
                {"error", "mount_not_allowed"},
                {"allowed_prefix", allowed_prefix},
                {"resolved_mount", resolved_mount}
            }.dump());
            return;
        }
    }

    if (fstype_out != "btrfs") {
        audit_fail(actor_fp, "not_btrfs", 200, "",
                   json{{"resolved_mount", resolved_mount}, {"fstype", fstype_out}});
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "not_btrfs"},
            {"resolved_mount", resolved_mount},
            {"fstype", fstype_out}
        }.dump());
        return;
    }

    // Load disks allowlist (inherits PQNAS_STORAGE_ALLOW_LOOP policy)
    std::string raw_lsblk;
    json disks_j = storage_list_disks_json(&raw_lsblk);
    json by_path = disks_j.value("by_path", json::object());

    // Read btrfs filesystem show to map /dev/disk -> member path
    const std::string cmd_show =
        "/usr/bin/sudo -n /usr/bin/btrfs filesystem show " + sh_quote(resolved_mount);

    std::string show_raw;
    int ec_show = 0;
    const bool ok_show = run_cmd_capture(cmd_show, &show_raw, &ec_show);
    cap_string(show_raw, 256 * 1024);

    if (!ok_show || ec_show != 0 || show_raw.empty()) {
        audit_fail(actor_fp, "btrfs_show_failed", 200, "",
                   json{{"resolved_mount", resolved_mount}, {"btrfs_show_rc", ec_show}});
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "btrfs_show_failed"},
            {"resolved_mount", resolved_mount},
            {"btrfs_show_rc", ec_show}
        }.dump());
        return;
    }

    BtrfsShowParsed parsed = parse_btrfs_filesystem_show(show_raw);
    json btrfs_j = btrfs_show_parsed_to_json(parsed, by_path, disks_j.value("by_name", json::object()));

    const std::string membership_fp = btrfs_membership_fingerprint(btrfs_j);
    if (membership_fp.empty()) {
        audit_fail(actor_fp, "membership_fp_failed", 500,
                   "failed to compute btrfs membership fingerprint",
                   json{{"resolved_mount", resolved_mount}});
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "membership_fp_failed"},
            {"message", "failed to compute btrfs membership fingerprint"}
        }.dump());
        return;
    }

    // Find member device in filesystem corresponding to remove_device
    std::string member_path;
    std::string parent_disk;

    if (btrfs_j.contains("devices") && btrfs_j["devices"].is_array()) {
        for (const auto& dev : btrfs_j["devices"]) {
            if (!dev.is_object()) continue;
            const std::string p  = dev.value("path", "");
            const std::string pd = dev.value("parent_disk", "");
            if (p.empty()) continue;

            if (remove_device == p || (!pd.empty() && remove_device == pd)) {
                member_path = p;
                parent_disk = pd;
                break;
            }
        }
    }

    if (member_path.empty()) {
        // idempotent "skipped"
        audit_ok(actor_fp, json{
            {"dry_run", dry_run},
            {"skipped", true},
            {"skip_reason", "already_not_in_filesystem"},
            {"mount", resolved_mount},
            {"remove_device", remove_device},
            {"plan_id", client_plan_id}
        });

        reply_json(res, 200, json{
            {"ok", true},
            {"dry_run", dry_run},
            {"skipped", true},
            {"skip_reason", "already_not_in_filesystem"},
            {"mount", resolved_mount},
            {"remove_device", remove_device},
            {"plan_id", client_plan_id}
        }.dump());
        return;
    }

    if (!system_root_disk.empty() && !parent_disk.empty() && parent_disk == system_root_disk) {
        json extra = json::object();
        extra["system_root_disk"] = system_root_disk;
        extra["remove_device"] = remove_device;
        extra["parent_disk"] = parent_disk;

        audit_fail(actor_fp, "device_is_system_root_disk", 400, "", extra);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "device_is_system_root_disk"},
            {"system_root_disk", system_root_disk},
            {"remove_device", remove_device},
            {"parent_disk", parent_disk}
        }.dump());
        return;
    }

    // Allowlist enforcement (prefer parent disk)
    if (!by_path.is_object() ||
        ((!parent_disk.empty() && !by_path.contains(parent_disk)) &&
         (parent_disk.empty() && !by_path.contains(remove_device)))) {
        audit_fail(actor_fp, "device_not_allowed", 400, "",
                   json{{"remove_device", remove_device}, {"member_path", member_path}, {"parent_disk", parent_disk}});
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "device_not_allowed"},
            {"remove_device", remove_device},
            {"member_path", member_path},
            {"parent_disk", parent_disk}
        }.dump());
        return;
    }

    // Refuse removing current filesystem disk unless force=true
    if (!resolved_disk.empty() && !parent_disk.empty() && parent_disk == resolved_disk && !force) {
        audit_fail(actor_fp, "device_is_current_disk", 400, "",
                   json{{"resolved_disk", resolved_disk}, {"parent_disk", parent_disk}, {"remove_device", remove_device}});
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "device_is_current_disk"},
            {"resolved_disk", resolved_disk},
            {"parent_disk", parent_disk},
            {"remove_device", remove_device}
        }.dump());
        return;
    }

    // Refuse removing if it's the last remaining device
    const int total_devices = btrfs_j.value("total_devices", 0);
    if (total_devices <= 1) {
        audit_fail(actor_fp, "cannot_remove_last_device", 400, "",
                   json{{"total_devices", total_devices}, {"member_path", member_path}, {"mount", resolved_mount}});
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "cannot_remove_last_device"},
            {"total_devices", total_devices},
            {"member_path", member_path},
            {"mount", resolved_mount}
        }.dump());
        return;
    }

    // -------- Build commands exactly like plan endpoint --------
    json commands = json::array();

    // If this removal would drop from 2 devices -> 1 device, we must convert off RAID1 first
    if (total_devices == 2) {
        commands.push_back(
            "/usr/bin/sudo -n /usr/bin/btrfs balance start --force "
            "-dconvert=single -mconvert=single -sconvert=single "
            + sh_quote(resolved_mount)
        );
    }

    commands.push_back("/usr/bin/sudo -n /usr/bin/btrfs device remove " + sh_quote(member_path) + " " + sh_quote(resolved_mount));

    // plan_id check (must match exactly plan endpoint)
    const std::string joined = join_commands_for_hash(commands);

    // Salt plan_id with current FS membership/state so repeats after add/remove
    // don't collide with old execution records (MUST match plan/remove-device).
    const std::string salt =
        std::string("mount=") + resolved_mount + "\n" +
        std::string("btrfs_membership_fp=") + membership_fp + "\n";

    const std::string expected_plan_id = sha256_hex_lower_evp(joined + "\n" + salt);

    if (expected_plan_id.empty()) {
        audit_fail(actor_fp, "plan_id_compute_failed", 500, "",
                   json{{"mount", resolved_mount}, {"remove_device", remove_device}});
        reply_json(res, 500, json{{"ok", false}, {"error", "plan_id_compute_failed"}}.dump());
        return;
    }
    if (client_plan_id != expected_plan_id) {
        audit_fail(actor_fp, "plan_mismatch", 400, "",
                   json{{"expected_plan_id", expected_plan_id}, {"provided_plan_id", client_plan_id}});
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "plan_mismatch"},
            {"message", "plan_id does not match server recomputed plan"},
            {"expected_plan_id", expected_plan_id},
            {"provided_plan_id", client_plan_id}
        }.dump());
        return;
    }

    // Prepare response plan payload (for caller/UI)
    json plan;
    plan["plan_id"] = expected_plan_id;
    plan["btrfs_membership_fp"] = membership_fp;
    plan["mount"] = resolved_mount;
    plan["input_mount"] = mount;
    plan["resolved_mount"] = resolved_mount;
    plan["resolved_source"] = resolved_source;
    if (!resolved_disk.empty()) plan["resolved_disk"] = resolved_disk;
    plan["fstype"] = fstype_out;
    plan["remove_device"] = remove_device;
    plan["remove_member_path"] = member_path;
    if (!parent_disk.empty()) plan["remove_parent_disk"] = parent_disk;
    plan["force"] = force;
    plan["requires_downtime"] = false;
    plan["commands"] = commands;
	plan["actor_fp"] = actor_fp;

    if (dry_run) {
        audit_ok(actor_fp, json{
            {"dry_run", true},
            {"mount", resolved_mount},
            {"remove_device", remove_device},
            {"member_path", member_path},
            {"parent_disk", parent_disk},
            {"force", force},
            {"plan_id", expected_plan_id},
            {"total_devices", total_devices},
            {"commands", (int)commands.size()}
        });

        reply_json(res, 200, json{
            {"ok", true},
            {"dry_run", true},
            {"plan", plan}
        }.dump());
        return;
    }

    // Async enqueue (fail-closed): create canonical queued record + return immediately.
    try {
        json q = raid_enqueue_job_fail_closed(expected_plan_id, resolved_mount, plan, commands);
		try { if (q.contains("job_id")) plan["job_id"] = q["job_id"]; } catch (...) {}
        audit_ok(actor_fp, json{
            {"dry_run", false},
            {"enqueued", true},
            {"mount", resolved_mount},
            {"remove_device", remove_device},
            {"member_path", member_path},
            {"parent_disk", parent_disk},
            {"force", force},
            {"plan_id", expected_plan_id},
            {"total_devices", total_devices}
            // optional: if q has "job_id" you can log it explicitly if you want
        });

        q["dry_run"] = false;
        q["plan"] = plan;
        reply_json(res, 200, q.dump());
        return;
    } catch (const std::exception& e) {
        const std::string msg = e.what();

        if (msg == "already_running") {
            audit_fail(actor_fp, "already_running", 409, "",
                       json{{"plan_id", expected_plan_id}, {"mount", resolved_mount}});
            reply_json(res, 409, json{
                {"ok", false},
                {"error", "already_running"},
                {"message", "this plan_id already has a running execution record; refusing replay"},
                {"plan_id", expected_plan_id},
                {"record_path", raid_exec_record_path(expected_plan_id)}
            }.dump());
            return;
        }

        if (msg.rfind("raid_state_dir_failed:", 0) == 0) {
            audit_fail(actor_fp, "raid_state_dir_failed", 500, msg,
                       json{{"plan_id", expected_plan_id}, {"mount", resolved_mount}});
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "raid_state_dir_failed"},
                {"message", "cannot create/verify /run/pqnas/raid; refusing to execute"},
                {"detail", msg}
            }.dump());
            return;
        }

        audit_fail(actor_fp, "enqueue_failed", 500, msg,
                   json{{"plan_id", expected_plan_id}, {"mount", resolved_mount}});
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "enqueue_failed"},
            {"detail", msg}
        }.dump());
        return;
    }
});
// ----- POST /api/v4/raid/execute/create-pool (admin-only) --------------------
srv.Post("/api/v4/raid/execute/create-pool", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;
    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}}.dump());
        return;
    }

    // ---- audit helpers ----
    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto audit_kv_merge = [&](pqnas::AuditEvent& ev, const json& extra) {
        if (!extra.is_object()) return;
        for (auto it = extra.begin(); it != extra.end(); ++it) {
            const std::string k  = pqnas::shorten(it.key(), 64);
            const std::string kk = "x_" + k;
            if (it.value().is_string()) ev.f[kk] = pqnas::shorten(it.value().get<std::string>(), 220);
            else if (it.value().is_number_integer() || it.value().is_number_unsigned()) ev.f[kk] = it.value().dump();
            else if (it.value().is_boolean()) ev.f[kk] = (it.value().get<bool>() ? "true" : "false");
            else ev.f[kk] = pqnas::shorten(it.value().dump(), 220);
        }
    };

    auto audit_common = [&](pqnas::AuditEvent& ev) {
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
    };

    auto audit_fail = [&](const std::string& actor_fp,
                          const std::string& reason,
                          int http,
                          const std::string& detail = "",
                          const json& extra = json::object()) {
        pqnas::AuditEvent ev;
        ev.event   = "v4.raid_execute_create_pool_fail";
        ev.outcome = "fail";
        if (!actor_fp.empty()) ev.f["fingerprint"] = actor_fp;
        ev.f["reason"] = reason;
        ev.f["http"]   = std::to_string(http);
        if (!detail.empty()) ev.f["detail"] = pqnas::shorten(detail, 220);
        audit_kv_merge(ev, extra);
        audit_common(ev);
        audit_append(ev);
    };

    auto audit_ok = [&](const std::string& actor_fp,
                        const json& extra = json::object()) {
        pqnas::AuditEvent ev;
        ev.event   = "v4.raid_execute_create_pool_ok";
        ev.outcome = "ok";
        if (!actor_fp.empty()) ev.f["fingerprint"] = actor_fp;
        audit_kv_merge(ev, extra);
        audit_common(ev);
        audit_append(ev);
    };

    // job lifecycle audits
    auto audit_job_start_ok = [&](const std::string& actor_fp,
                                  const json& extra = json::object()) {
        pqnas::AuditEvent ev;
        ev.event   = "v4.raid_job_start_ok";
        ev.outcome = "ok";
        if (!actor_fp.empty()) ev.f["fingerprint"] = actor_fp;
        audit_kv_merge(ev, extra);
        audit_common(ev);
        audit_append(ev);
    };

    auto audit_job_finish = [&](const std::string& actor_fp,
                                bool ok,
                                const std::string& detail = "",
                                const json& extra = json::object()) {
        pqnas::AuditEvent ev;
        ev.event   = ok ? "v4.raid_job_finish_ok" : "v4.raid_job_finish_fail";
        ev.outcome = ok ? "ok" : "fail";
        if (!actor_fp.empty()) ev.f["fingerprint"] = actor_fp;
        if (!detail.empty()) ev.f["detail"] = pqnas::shorten(detail, 220);
        audit_kv_merge(ev, extra);
        audit_common(ev);
        audit_append(ev);
    };

    // ---- auth (need actor fingerprint for audit) ----
    std::string actor_fp;
    if (!require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, &actor_fp)) return;
    if (!require_same_origin_for_cookie_mutation(req, res)) {
        audit_fail(actor_fp, "origin_mismatch", 403);
        return;
    }

    json in;
    try { in = json::parse(req.body.empty() ? "{}" : req.body); }
    catch (...) {
        audit_fail(actor_fp, "invalid_json", 400);
        reply_json(res, 400, json{{"ok", false}, {"error", "invalid_json"}}.dump());
        return;
    }

    const std::string plan_id    = trim_copy(in.value("plan_id", ""));
    const std::string plan_nonce = trim_copy(in.value("plan_nonce", ""));
    const bool confirm           = in.value("confirm", false);

    const std::string pool_id = trim_copy(in.value("pool_id", ""));
    const std::string mode    = trim_copy(in.value("mode", "single"));
    const bool force          = in.value("force", false);

    json devices_json = json::array();
    if (in.contains("devices")) devices_json = in["devices"];

    std::vector<std::string> devices; // canonical validated device list

    std::string root = getenv_str("PQNAS_STORAGE_ROOT");
    if (root.empty()) root = "/srv/pqnas";
    const std::string mount = root + "/pools/" + pool_id;
    const std::string label = "PQNAS_" + upper_ascii(pool_id);

    auto audit_ctx = [&](const json& extra = json::object()) -> json {
        json j = {
            {"op", "create-pool"},
            {"confirm", confirm},
            {"plan_id", plan_id},
            {"plan_nonce", plan_nonce},
            {"pool_id", pool_id},
            {"mode", mode},
            {"force", force},
            {"devices_n", (int)devices.size()},
            {"mount", mount},
            {"label", label}
        };
        if (extra.is_object()) {
            for (auto it = extra.begin(); it != extra.end(); ++it) j[it.key()] = it.value();
        }
        return j;
    };

    if (!confirm || plan_id.empty() || plan_nonce.empty()) {
        audit_fail(actor_fp, "missing_plan_id_or_confirm", 400, "", audit_ctx());
        reply_json(res, 400, json{{"ok", false}, {"error", "missing_plan_id_or_confirm"}}.dump());
        return;
    }

    if (!is_hex_64_lower_or_upper(plan_id)) {
        audit_fail(actor_fp, "bad_plan_id_format", 400, "", audit_ctx());
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "plan_id must be 64 hex chars"},
            {"plan_id", plan_id}
        }.dump());
        return;
    }

    // keep this mild unless your planner already emits a stricter format
    if (plan_nonce.size() > 128) {
        audit_fail(actor_fp, "bad_plan_nonce", 400, "", audit_ctx());
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_plan_nonce"}}.dump());
        return;
    }

    if (!std::regex_match(pool_id, std::regex("^[a-z0-9_-]{1,32}$"))) {
        audit_fail(actor_fp, "bad_pool_id", 400, "", audit_ctx());
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_pool_id"}}.dump());
        return;
    }

    if (mode != "single" && mode != "raid1") {
        audit_fail(actor_fp, "bad_mode", 400, "", audit_ctx());
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_mode"}}.dump());
        return;
    }

    json disk_inventory = storage_list_disks_json();

    std::string dev_err;
    if (!validate_create_pool_devices(devices_json, disk_inventory, devices, dev_err)) {
        audit_fail(actor_fp, "bad_devices", 400, dev_err, audit_ctx());
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_devices"},
            {"message", dev_err}
        }.dump());
        return;
    }

    if (mode == "raid1" && devices.size() < 2) {
        audit_fail(actor_fp, "raid1_requires_2_devices", 400, "", audit_ctx());
        reply_json(res, 400, json{{"ok", false}, {"error", "raid1_requires_2_devices"}}.dump());
        return;
    }

    const json canonical_commands =
        build_create_pool_commands_json(pool_id, mode, devices, force);

    if (!canonical_commands.is_array() || canonical_commands.empty()) {
        audit_fail(actor_fp, "canonical_plan_empty", 500, "", audit_ctx());
        reply_json(res, 500, json{{"ok", false}, {"error", "canonical_plan_empty"}}.dump());
        return;
    }

    const std::string expected_plan_id =
        compute_create_pool_plan_id(plan_nonce, pool_id, mode, devices, force, canonical_commands);

    if (expected_plan_id != plan_id) {
        audit_fail(actor_fp, "plan_mismatch", 400, "",
                   audit_ctx(json{{"expected_plan_id", expected_plan_id}}));
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "plan_mismatch"}
        }.dump());
        return;
    }

    // Exec-record dir + replay protection (refuse if this plan_id already executed)
    ensure_dir_best_effort("/run/pqnas/raid");
    const std::string recp = raid_exec_record_path(plan_id);

    {
        std::error_code ec;
        if (std::filesystem::exists(recp, ec)) {
            audit_fail(actor_fp, "already_executed", 200, "",
                       audit_ctx(json{{"recp", recp}}));
            reply_json(res, 200, json{
                {"ok", false},
                {"error", "already_executed"},
                {"plan_id", plan_id}
            }.dump());
            return;
        }
    }

    if (std::filesystem::exists(mount)) {
        audit_fail(actor_fp, "mount_exists", 400, "", audit_ctx());
        reply_json(res, 400, json{{"ok", false}, {"error", "mount_exists"}}.dump());
        return;
    }

    // Lock path (prevent concurrent ops)
    const std::string lockp = raid_mount_lock_path(mount);
    {
        std::error_code ec;
        if (std::filesystem::exists(lockp, ec)) {
            audit_fail(actor_fp, "raid_busy", 200, "",
                       audit_ctx(json{{"lockp", lockp}}));
            reply_json(res, 200, json{{"ok", false}, {"error", "raid_busy"}}.dump());
            return;
        }
    }

    // Ensure lock removed on all exits
    struct LockGuard {
        std::string p;
        ~LockGuard() { if (!p.empty()) { std::error_code ec; std::filesystem::remove(p, ec); } }
    } lock_guard{lockp};

    {
        std::ofstream lock(lockp);
        lock << "create-pool\n";
        lock.close();
    }

    // ---- exec-record init (must be before try/catch) ----
    json record;
    record["ok"]         = true;
    record["plan_id"]    = plan_id;
    record["plan_nonce"] = plan_nonce;
    record["operation"]  = "create-pool";
    record["state"]      = "running";
    record["busy"]       = true;
    record["ts_start"]   = iso8601_now();
    record["ts_last"]    = record["ts_start"];
    record["results"]    = json::array();

    record["pool_id"]    = pool_id;
    record["mode"]       = mode;
    record["force"]      = force;
    record["devices"]    = devices;
    record["commands"]   = canonical_commands;

    (void)write_text_file_atomic(recp, record.dump(2) + "\n");

    json results = json::array();
    bool all_ok = true;
    size_t step_i = 0;

    // Emit job_start once, right before the first command actually runs.
    bool job_start_emitted = false;
    auto emit_job_start_once = [&]() {
        if (job_start_emitted) return;
        job_start_emitted = true;
        audit_job_start_ok(actor_fp, audit_ctx(json{{"job_id", plan_id}, {"recp", recp}}));
    };

    // Helper: run one command, append to both results + record, update ts_last, persist record.
    auto run_step = [&](const std::string& cmd) -> bool {
        emit_job_start_once();

        const size_t i = step_i++;

        std::string out;
        int ec = 0;
        const bool ran = run_cmd_capture(cmd, &out, &ec);
        const bool step_ok = ran && (ec == 0);

        cap_string(out, 128 * 1024);

        json one = {
            {"i", (int)i},
            {"cmd", cmd},
            {"rc", ec},
            {"ok", step_ok},
            {"out", out}
        };

        results.push_back(one);
        record["results"].push_back(one);
        record["ts_last"] = iso8601_now();

        if (!write_text_file_atomic(recp, record.dump(2) + "\n")) {
            all_ok = false;
            return false;
        }

        if (!step_ok) {
            all_ok = false;
            record["ok"]     = false;
            record["state"]  = "failed";
            record["busy"]   = false;
            record["ts_end"] = iso8601_now();
            (void)write_text_file_atomic(recp, record.dump(2) + "\n");
            return false;
        }

        return true;
    };

    try {
        for (const auto& cmdv : canonical_commands) {
            if (!cmdv.is_string()) {
                all_ok = false;
                break;
            }
            if (!run_step(cmdv.get<std::string>())) break;
        }

        // update pools.json
        if (all_ok) {
            json cfg = pqnas::load_or_init_pools_cfg_v3(users_path);

            if (!cfg.contains("names_by_mount") || !cfg["names_by_mount"].is_object())
                cfg["names_by_mount"] = json::object();
            cfg["names_by_mount"][mount] = pool_id;

            if (!cfg.contains("pools") || !cfg["pools"].is_object())
                cfg["pools"] = json::object();

            std::string fs_label_detected;
            std::string fs_uuid_detected;
            int fs_devices_detected = -1;

            {
                std::string show_out;
                int rc_show = run_capture(
                    "/usr/bin/sudo -n /usr/bin/btrfs filesystem show " + sh_quote(mount) + " 2>&1",
                    &show_out
                );
                if (rc_show == 0) {
                    parse_btrfs_filesystem_show(show_out,
                                                &fs_label_detected,
                                                &fs_uuid_detected,
                                                &fs_devices_detected);
                }
            }

            cfg["pools"][mount] = json{
                {"pool_id", pool_id},
                {"display_name", pool_id},
                {"created_ts", iso8601_now()},
                {"managed", true},
                {"fs_label", fs_label_detected.empty() ? label : fs_label_detected},
                {"fs_uuid", fs_uuid_detected}
            };

            cfg["version"] = 2;

            const auto cfg_path = pools_cfg_path_from_users_path(users_path);
            if (!write_text_file_atomic(cfg_path.string(), cfg.dump(2) + "\n")) {
                all_ok = false;
            }
        }


        // finalize exec record
        record["ok"]     = all_ok;
        record["state"]  = all_ok ? "done" : "failed";
        record["busy"]   = false;
        record["ts_end"] = iso8601_now();
        (void)write_text_file_atomic(recp, record.dump(2) + "\n");

        // lock_guard will remove lockp on return

        if (all_ok) {
            audit_ok(actor_fp, audit_ctx(json{{"job_id", plan_id}, {"recp", recp}}));
            audit_job_finish(actor_fp, true, "", audit_ctx(json{{"job_id", plan_id}, {"recp", recp}}));
        } else {
            audit_fail(actor_fp, "command_failed", 200, "", audit_ctx(json{{"job_id", plan_id}, {"recp", recp}}));
            audit_job_finish(actor_fp, false, "command_failed", audit_ctx(json{{"job_id", plan_id}, {"recp", recp}}));
        }

        reply_json(res, 200, json{
            {"ok", all_ok},
            {"results", results}
        }.dump());
        return;

    } catch (...) {
        all_ok = false;
        record["ok"]     = false;
        record["state"]  = "failed";
        record["busy"]   = false;
        record["ts_end"] = iso8601_now();
        (void)write_text_file_atomic(recp, record.dump(2) + "\n");

        audit_fail(actor_fp, "exception", 500, "", audit_ctx(json{{"job_id", plan_id}, {"recp", recp}}));
        audit_job_finish(actor_fp, false, "exception", audit_ctx(json{{"job_id", plan_id}, {"recp", recp}}));

        reply_json(res, 500, json{{"ok", false}, {"error", "exception"}}.dump());
        return;
    }
});

// ----- GET /api/v4/raid/job?job_id=... (admin-only) ---------------------------
srv.Get("/api/v4/raid/job", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;
    if (!users.load(users_path)) {
        reply_json(res, 500, json{{"ok", false}, {"error", "users_load_failed"}, {"path", users_path}}.dump());
        return;
    }
    if (!require_admin_cookie_users(req, res, COOKIE_KEY, users_path, &users)) return;

	std::string job_id;
	if (req.has_param("job_id")) job_id = req.get_param_value("job_id");
	if (job_id.empty()) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "missing job_id"}}.dump());
        return;
    }

    static std::string g_users_path_for_raid;

    std::lock_guard<std::mutex> lk(g_raid_jobs_mu);
    auto it = g_raid_job_meta.find(job_id);
    if (it == g_raid_job_meta.end()) {
        reply_json(res, 404, json{{"ok", false}, {"error", "not_found"}, {"job_id", job_id}}.dump());
        return;
    }

    reply_json(res, 200, it->second.dump());
});

// ----- GET /api/v4/raid/exec-record?plan_id=<sha256hex> (admin-only, read-only) -----
// Reads /run/pqnas/raid/<plan_id>.json and returns it as JSON.
srv.Get("/api/v4/raid/exec-record", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;

    if (!users.load(users_path)) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "users_load_failed"},
            {"path", users_path}
        }.dump());
        return;
    }
    if (!require_admin_cookie_users(req, res, COOKIE_KEY, users_path, &users)) return;

    try {
    // existing handler code...
    } catch (const std::exception& e) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "exec_record_exception"},
            {"message", e.what()}
        }.dump());
        return;
    } catch (...) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "exec_record_exception"},
            {"message", "unknown exception"}
        }.dump());
        return;
    }

    const std::string plan_id = req.has_param("plan_id") ? req.get_param_value("plan_id") : "";

    if (plan_id.empty()) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "missing plan_id"}
        }.dump());
        return;
    }

    if (!is_sha256_hex_lower(plan_id)) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "plan_id must be 64 lowercase hex chars"}
        }.dump());
        return;
    }

    const std::string path = raid_exec_record_path(plan_id);

    std::string text;
    if (!read_text_file(path, &text)) {
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "record_not_found"},
            {"plan_id", plan_id}
        }.dump());
        return;
    }

    cap_string(text, 1024 * 1024); // 1 MiB cap for safety

    json record;
    try {
        record = json::parse(text.empty() ? "{}" : text);
    } catch (...) {
        // Corrupt record file (or partial write etc.)
        reply_json(res, 200, json{
            {"ok", false},
            {"error", "record_parse_failed"},
            {"plan_id", plan_id}
        }.dump());
        return;
    }

    // Normalize response shape (type-safe; never throw)
    json out = json::object();

    // If record is a JSON object, merge it shallowly.
    if (record.is_object()) {
        out = record;
    } else {
        out["record_raw"] = record;
    }

    out["ok"] = true;
    out["plan_id"] = plan_id;

    reply_json(res, 200, out.dump());

});



// ----- GET /api/v4/raid/health?mount=/path (admin-only, read-only) -----------
srv.Get("/api/v4/raid/health", [&](const httplib::Request& req, httplib::Response& res) {
    pqnas::UsersRegistry users;

    if (!users.load(users_path)) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "users_load_failed"},
            {"path", users_path}
        }.dump());
        return;
    }

    if (!require_admin_cookie_users(req, res, COOKIE_KEY, users_path, &users)) return;

    // -------------------- mount selection --------------------
    std::string allowed_prefix = getenv_str("PQNAS_STORAGE_ROOT");
    if (allowed_prefix.empty()) allowed_prefix = "/srv/pqnas";

    std::string mount = allowed_prefix + "/data";
    if (req.has_param("mount")) mount = req.get_param_value("mount");

    json out;
    out["ok"] = false;
    out["input_mount"] = mount;
    out["allowed_prefix"] = allowed_prefix;

    if (!is_abs_path_safe(mount)) {
        out["error"] = "bad_mount";
        reply_json(res, 400, out.dump());
        return;
    }

    // -------------------- resolve mountpoint, fstype, source --------------------
    std::string target_out, fstype_out, source_out;

    int rc_target = run_capture("/usr/bin/findmnt -no TARGET --target " + sh_quote(mount), &target_out);
    cap_string(target_out, 16 * 1024);
    rtrim_inplace(target_out);

    int rc_fs = run_capture("/usr/bin/findmnt -no FSTYPE --target " + sh_quote(mount), &fstype_out);
    cap_string(fstype_out, 16 * 1024);
    rtrim_inplace(fstype_out);

    int rc_src = run_capture("/usr/bin/findmnt -no SOURCE --target " + sh_quote(mount), &source_out);
    cap_string(source_out, 16 * 1024);
    rtrim_inplace(source_out);

    if (rc_target != 0 || target_out.empty() ||
        rc_fs != 0 || fstype_out.empty() ||
        rc_src != 0 || source_out.empty()) {
        out["error"] = "mount_not_found";
        reply_json(res, 200, out.dump());
        return;
    }

    const std::string resolved_mount  = target_out;
    const std::string resolved_source = source_out;
    const std::string resolved_disk   = parent_disk_from_dev(resolved_source);

    out["resolved_mount"]  = resolved_mount;
    out["resolved_source"] = resolved_source;
    if (!resolved_disk.empty()) out["resolved_disk"] = resolved_disk;
    out["fstype"]          = fstype_out;

    // -------------------- allowlist enforcement --------------------
    if (resolved_mount.rfind(allowed_prefix, 0) != 0) {
        const std::string test_prefix  = "/srv/pqnas-test";
        const std::string test_prefix2 = "/srv/pqnas-test-btrfs";
        if (resolved_mount.rfind(test_prefix, 0) != 0 &&
            resolved_mount.rfind(test_prefix2, 0) != 0) {
            out["error"] = "mount_not_allowed";
            reply_json(res, 200, out.dump());
            return;
        }
    }

    // -------------------- non-btrfs case --------------------
    if (fstype_out != "btrfs") {
        out["error"] = "not_btrfs";
        reply_json(res, 200, out.dump());
        return;
    }

    // -------------------- btrfs read-only health commands --------------------
    const std::string mp = sh_quote(resolved_mount);

    std::string dev_stats, scrub_status, balance_status;

    const std::string cmd_dev_stats = "/usr/bin/sudo -n /usr/bin/btrfs device stats " + mp;
    const std::string cmd_scrub     = "/usr/bin/sudo -n /usr/bin/btrfs scrub status " + mp;
    const std::string cmd_balance   = "/usr/bin/sudo -n /usr/bin/btrfs balance status " + mp;


    int rc_dev_stats = run_capture(cmd_dev_stats, &dev_stats);
    int rc_scrub     = run_capture(cmd_scrub,     &scrub_status);
    int rc_balance   = run_capture(cmd_balance,   &balance_status);

    cap_string(dev_stats,       256 * 1024);
    cap_string(scrub_status,    256 * 1024);
    cap_string(balance_status,  256 * 1024);

    out["rc_device_stats"] = rc_dev_stats;
    out["rc_scrub_status"] = rc_scrub;
    out["rc_balance_status"] = rc_balance;

    // Always include raw outputs (capped) for now; if you want, you can gate these with PQNAS_RAID_DEBUG_* later.
    out["btrfs_device_stats"]  = dev_stats;
    out["btrfs_scrub_status"]  = scrub_status;
    out["btrfs_balance_status"] = balance_status;

    // Parsed scrub summary (best effort)
    out["scrub"] = parse_btrfs_scrub_status_best_effort(scrub_status);

    // ok/error classification (match your existing style)
    if (rc_dev_stats != 0 || rc_scrub != 0 || rc_balance != 0) {
        out["ok"] = false;
        if (str_contains(dev_stats, "sudo:") || str_contains(scrub_status, "sudo:") || str_contains(balance_status, "sudo:")) {
            out["error"] = "sudo_not_allowed";
        } else if (str_contains(dev_stats, "not a valid btrfs filesystem") ||
                   str_contains(scrub_status, "not a valid btrfs filesystem") ||
                   str_contains(balance_status, "not a valid btrfs filesystem")) {
            out["error"] = "not_btrfs";
        } else {
            out["error"] = "btrfs_failed";
        }

        reply_json(res, 200, out.dump());
        return;
    }

    out["ok"] = true;
    reply_json(res, 200, out.dump());
});

}
