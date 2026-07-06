    // Admin Settings UI
    srv.Get("/admin/settings", [&](const httplib::Request& req, httplib::Response& res) {
        // Gate page itself (admin-only)
        if (!require_admin_cookie_users(req, res, COOKIE_KEY, std::string{}, &users)) return;

        std::string body;
        if (!read_file_to_string(STATIC_ADMIN_SETTINGS_HTML, body)) {
            res.status = 500;
            res.set_header("Content-Type", "text/plain");
            res.body = "Missing static file: " + STATIC_ADMIN_SETTINGS_HTML;
            return;
        }
        res.status = 200;
        res.set_header("Content-Type", "text/html; charset=utf-8");
        res.set_header("Cache-Control", "no-store");
        res.body = body;
    });









    srv.Get("/static/admin_settings.js", [&](const httplib::Request&, httplib::Response& res) {
        // You can leave JS ungated (like other static files), page is gated anyway.
        std::string body;
        if (!read_file_to_string(STATIC_ADMIN_SETTINGS_JS, body)) {
            res.status = 500;
            res.set_header("Content-Type", "text/plain");
            res.body = "Missing static file: " + STATIC_ADMIN_SETTINGS_JS;
            return;
        }
        res.status = 200;
        res.set_header("Content-Type", "application/javascript; charset=utf-8");
        res.set_header("Cache-Control", "no-store");
        res.body = body;
    });

    // User-safe Vault recovery public key API.
    // Security: this exposes only the active organization recovery public key
    // and recovery key ID. It never exposes private key material or unrelated
    // admin settings.
    srv.Get("/api/v4/vault/recovery-public-key", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string actor_fp_hex;
            std::string actor_role;
            if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &actor_fp_hex, &actor_role)) return;

            auto load_settings_json = [&]() -> json {
                try {
                    std::ifstream f(admin_settings_path);
                    if (!f.good()) return json::object();

                    json j;
                    f >> j;

                    if (!j.is_object()) return json::object();
                    return j;
                } catch (...) {
                    return json::object();
                }
            };

            json persisted = load_settings_json();

            bool enabled = false;
            std::string status = "not_configured";
            std::string public_key_b64;
            std::string recovery_key_id;

            if (persisted.contains("vault_recovery") && persisted["vault_recovery"].is_object()) {
                const json& vr = persisted["vault_recovery"];

                if (vr.contains("enabled") && vr["enabled"].is_boolean()) {
                    enabled = vr["enabled"].get<bool>();
                }

                if (vr.contains("status") && vr["status"].is_string()) {
                    status = vr["status"].get<std::string>();
                }

                if (vr.contains("public_key_b64") && vr["public_key_b64"].is_string()) {
                    public_key_b64 = trim_copy(vr["public_key_b64"].get<std::string>());
                }

                if (vr.contains("public_key_sha256") && vr["public_key_sha256"].is_string()) {
                    recovery_key_id = trim_copy(vr["public_key_sha256"].get<std::string>());
                }
            }

            const bool active =
                enabled &&
                status == "active" &&
                !public_key_b64.empty() &&
                recovery_key_id.size() == 64;

            if (!active) {
                enabled = false;
                if (status != "disabled") status = "not_configured";
                public_key_b64.clear();
                recovery_key_id.clear();
            }

            res.status = 200;
            res.set_header("Content-Type", "application/json; charset=utf-8");
            res.set_header("Cache-Control", "no-store");
            res.body = json{
                {"ok", true},
                {"enabled", enabled},
                {"status", status},
                {"public_key_b64", public_key_b64},
                {"recovery_key_id", recovery_key_id}
            }.dump();
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_header("Content-Type", "application/json; charset=utf-8");
            res.set_header("Cache-Control", "no-store");
            res.body = json{
                {"ok", false},
                {"error", "vault_recovery_public_key_failed"},
                {"message", e.what()}
            }.dump();
        } catch (...) {
            res.status = 500;
            res.set_header("Content-Type", "application/json; charset=utf-8");
            res.set_header("Cache-Control", "no-store");
            res.body = json{
                {"ok", false},
                {"error", "vault_recovery_public_key_failed"},
                {"message", "unknown error"}
            }.dump();
        }
    });

    // Admin settings API
    srv.Get("/api/v4/admin/settings", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_admin_cookie_users(req, res, COOKIE_KEY, std::string{}, &users)) return;

		// SAFE accessor for audit_min_level (never throws)
		auto get_level_safe = [&](const json& j, const std::string& fallback) -> std::string {
    		auto it2 = j.find("audit_min_level");
    		if (it2 != j.end() && it2->is_string()) return it2->get<std::string>();
    		return fallback;
		};

        auto load_settings_json = [&]() -> json {
            try {
                std::ifstream f(admin_settings_path);
                if (!f.good()) return json::object();
                json j;
                f >> j;
                if (!j.is_object()) return json::object();
                return j;
            } catch (...) {
                return json::object();
            }
        };

        // Defaults
        json persisted = load_settings_json();

        // ---- transport max upload (soft cap; clamped to payload cap) ----
        std::uint64_t tmax = k_payload_max_upload_bytes; // default = payload max
        if (persisted.contains("transport_max_upload_bytes")) {
            try {
                if (persisted["transport_max_upload_bytes"].is_number_integer()) {
                    long long v = persisted["transport_max_upload_bytes"].get<long long>();
                    if (v > 0) tmax = (std::uint64_t)v;
                }
            } catch (...) {}
        }
        if (tmax > k_payload_max_upload_bytes) tmax = k_payload_max_upload_bytes;

        // Persisted min level (file), runtime min level (AuditLog)
        std::string persisted_lvl = audit.min_level_str();
        auto it = persisted.find("audit_min_level");
        if (it != persisted.end() && it->is_string()) {
            persisted_lvl = it->get<std::string>();
        }

		// default theme
		std::string ui_theme = "dark";
		if (persisted.contains("ui_theme") && persisted["ui_theme"].is_string()) {
    		ui_theme = persisted["ui_theme"].get<std::string>();
		}
		if (!(ui_theme == "dark" || ui_theme == "bright" || ui_theme == "cpunk_orange" || ui_theme == "win_classic"))
		    ui_theme = "dark";

        // Retention defaults (if absent)
        json retention = json::object();
        if (persisted.contains("audit_retention") && persisted["audit_retention"].is_object()) {
            retention = persisted["audit_retention"];
        } else {
            retention = json{
                {"mode", "never"},
                {"days", 90},
                {"max_files", 50},
                {"max_total_mb", 20480}
            };
        }
        // Rotation defaults (if absent)
        json rotation = json::object();
        if (persisted.contains("audit_rotation") && persisted["audit_rotation"].is_object()) {
            rotation = persisted["audit_rotation"];
        } else {
            rotation = json{
                {"mode", "manual"},        // manual | daily | size_mb | daily_or_size_mb
                {"max_active_mb", 256},    // used when size-based trigger is enabled
                {"rotate_utc_day", ""}     // optional: last-rotated UTC day tracker (YYYY-MM-DD)
            };
        }

		const long long active_bytes = file_size_bytes_safe(audit_jsonl_path);
		// Snapshots defaults (if absent)
		json snapshots = json::object();
		if (persisted.contains("snapshots") && persisted["snapshots"].is_object()) {
    		snapshots = persisted["snapshots"];
		} else {
    		snapshots = json{
	    	    {"enabled", false},
    	    	{"backend", "btrfs"},
		        {"volumes", json::array()},
		        {"schedule", json{{"mode","times_per_day"},{"times_per_day",6},{"jitter_seconds",120}}},
        		{"retention", json{{"keep_days",7},{"keep_min",12},{"keep_max",500}}}
    		};
		}

	json storage_roots = json::object();
	storage_roots["data_root"] = pqnas::data_root_dir();

	// Expose pools for admin UI dropdown: [{pool_id,mount}, ...]
	{
    	json pools = json::array();

	    // default pool
    	pools.push_back(json{
        	{"pool_id", "default"},
	        {"mount", pqnas::data_root_dir()}
    	});

	    // runtime-managed pools
    	{
        	std::lock_guard<std::mutex> lk(pool_mu());
	        for (const auto& kv : pool_mount_by_id()) {
    	        const std::string& pid = kv.first;
        	    const std::string& mount = kv.second;
            	if (pid.empty() || pid == "default") continue;
	            pools.push_back(json{
    	            {"pool_id", pid},
        	        {"mount", mount}
            	});
	        }
    	}

    	storage_roots["pools"] = std::move(pools);
	}

	// Tiering defaults (if absent)
	json tiering = json::object();
	if (persisted.contains("tiering") && persisted["tiering"].is_object()) {
	    tiering = persisted["tiering"];
	} else {
    	tiering = json{
        	{"enabled", false},
	        {"landing_pool_id", ""},
	        {"worker_interval_sec", 60},
	        {"min_age_sec", 60},
	        {"max_candidates_per_pass", 8}
    	};
	}

	if (!tiering.contains("enabled") || !tiering["enabled"].is_boolean()) {
	    tiering["enabled"] = false;
	}
	if (!tiering.contains("landing_pool_id") || !tiering["landing_pool_id"].is_string()) {
	    tiering["landing_pool_id"] = "";
	}
	if (!tiering.contains("worker_interval_sec") || !tiering["worker_interval_sec"].is_number_integer()) {
	    tiering["worker_interval_sec"] = 60;
	}
	if (!tiering.contains("min_age_sec") || !tiering["min_age_sec"].is_number_integer()) {
	    tiering["min_age_sec"] = 60;
	}
	if (!tiering.contains("max_candidates_per_pass") || !tiering["max_candidates_per_pass"].is_number_integer()) {
	    tiering["max_candidates_per_pass"] = 8;
	}
	// DNA Connect alerts defaults
	json dna_connect_alerts = json::object();
	if (persisted.contains("dna_connect_alerts") && persisted["dna_connect_alerts"].is_object()) {
    	dna_connect_alerts = persisted["dna_connect_alerts"];
	} else {
	    dna_connect_alerts = json{
    	    {"enabled", false},
        	{"recipient", ""},
	        {"cli_path", "/usr/local/bin/dna-connect-cli"},
    	    {"data_dir", "/var/lib/pqnas/dna-alerts"},
        	{"min_level", "warning"}   // security | error | warning | info
	    };
	}

	if (!dna_connect_alerts.contains("enabled") || !dna_connect_alerts["enabled"].is_boolean()) {
	    dna_connect_alerts["enabled"] = false;
	}
	if (!dna_connect_alerts.contains("recipient") || !dna_connect_alerts["recipient"].is_string()) {
	    dna_connect_alerts["recipient"] = "";
	}
	if (!dna_connect_alerts.contains("cli_path") || !dna_connect_alerts["cli_path"].is_string()) {
	    dna_connect_alerts["cli_path"] = "/usr/local/bin/dna-connect-cli";
	}
	if (!dna_connect_alerts.contains("data_dir") || !dna_connect_alerts["data_dir"].is_string()) {
    	dna_connect_alerts["data_dir"] = "/var/lib/pqnas/dna-alerts";
	}
	if (!dna_connect_alerts.contains("min_level") || !dna_connect_alerts["min_level"].is_string()) {
	    dna_connect_alerts["min_level"] = "warning";
	}
	{
	    const std::string ml = dna_connect_alerts["min_level"].get<std::string>();
	    if (!(ml == "security" || ml == "error" || ml == "warning" || ml == "info")) {
	        dna_connect_alerts["min_level"] = "warning";
	    }
	}
json dna_connect_identity = json{
    {"exists", false},
    {"fingerprint", ""},
    {"name", "pqnasalerts"},
    {"cli_path", dna_connect_alerts.value("cli_path", std::string("/usr/local/bin/dna-connect-cli"))},
    {"data_dir", dna_connect_alerts.value("data_dir", std::string("/var/lib/pqnas/dna-alerts"))},
    {"admin_recipient", dna_connect_alerts.value("recipient", std::string(""))}
};

// Vault organization recovery settings.
// Security: only the public recovery key and metadata are persisted.
// The private recovery key is generated client-side and must never be sent here.
json vault_recovery = json{
    {"enabled", false},
    {"status", "not_configured"},
    {"public_key_b64", ""},
    {"public_key_sha256", ""},
    {"created_at", 0},
    {"label", ""}
};
if (persisted.contains("vault_recovery") && persisted["vault_recovery"].is_object()) {
    const json& vr = persisted["vault_recovery"];
    if (vr.contains("enabled") && vr["enabled"].is_boolean()) {
        vault_recovery["enabled"] = vr["enabled"];
    }
    if (vr.contains("status") && vr["status"].is_string()) {
        const std::string status = vr["status"].get<std::string>();
        if (status == "active" || status == "disabled" || status == "not_configured") {
            vault_recovery["status"] = status;
        }
    }
    if (vr.contains("public_key_b64") && vr["public_key_b64"].is_string()) {
        vault_recovery["public_key_b64"] = vr["public_key_b64"];
    }
    if (vr.contains("public_key_sha256") && vr["public_key_sha256"].is_string()) {
        vault_recovery["public_key_sha256"] = vr["public_key_sha256"];
    }
    if (vr.contains("created_at") && vr["created_at"].is_number_integer()) {
        vault_recovery["created_at"] = vr["created_at"];
    }
    if (vr.contains("label") && vr["label"].is_string()) {
        vault_recovery["label"] = vr["label"];
    }
}

	try {
    	const std::string data_dir =
        	trim_copy(dna_connect_alerts.value("data_dir", std::string("/var/lib/pqnas/dna-alerts")));

    	const std::filesystem::path fp_path =
        	std::filesystem::path(data_dir) / "identity" / "fingerprint.txt";

	    const std::filesystem::path keys_dir =
    	    std::filesystem::path(data_dir) / "keys";

	    if (std::filesystem::is_directory(keys_dir)) {
    	    dna_connect_identity["exists"] = true;

	        if (std::filesystem::exists(fp_path)) {
    	        std::ifstream f(fp_path.string());
        	    std::string fp;
            	std::getline(f, fp);
	            dna_connect_identity["fingerprint"] = trim_copy(fp);
    	    }
    	}
	} catch (...) {}

		reply_json(res, 200, json{
    		{"ok", true},

    		{"audit_min_level", get_level_safe(persisted, audit.min_level_str())},
    		{"audit_min_level_runtime", audit.min_level_str()},
    		{"allowed", json::array({"SECURITY","ADMIN","INFO","DEBUG"})},

    		{"audit_retention", retention},
    		{"audit_rotation", rotation},

    		{"audit_active_path", audit_jsonl_path},
    		{"audit_active_bytes", active_bytes},

    		{"ui_theme", ui_theme},

			{"snapshots", snapshots},
			{"storage_roots", storage_roots},
			{"tiering", tiering},
			{"dna_connect_alerts", dna_connect_alerts},
			{"dna_connect_identity", dna_connect_identity},
            {"vault_recovery", vault_recovery},
			{"upload_tiering_candidates", build_upload_tiering_candidates_json()},

			{"transport_max_upload_bytes", tmax},
			{"payload_max_upload_bytes", k_payload_max_upload_bytes}
		}.dump());

    });

srv.Post("/api/v4/admin/settings/send-dna-alert-contact-request", [&](const httplib::Request& req, httplib::Response& res) {
    if (!require_admin_cookie_users(req, res, COOKIE_KEY, std::string{}, &users)) return;
        if (!require_same_origin_for_cookie_mutation(req, res)) return;

    try {
        const json persisted = load_admin_settings_json_safe();

        json cfg = json::object();
        if (persisted.contains("dna_connect_alerts") && persisted["dna_connect_alerts"].is_object()) {
            cfg = persisted["dna_connect_alerts"];
        }

        const std::string cli_path = trim_copy(cfg.value("cli_path", std::string("/usr/local/bin/dna-connect-cli")));
        const std::string data_dir = trim_copy(cfg.value("data_dir", std::string("/var/lib/pqnas/dna-alerts")));
        const std::string recipient = trim_copy(cfg.value("recipient", std::string()));

        if (recipient.empty()) {
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "recipient is empty in dna_connect_alerts settings"}
            }.dump());
            return;
        }

        auto st = build_dna_connect_identity_status_json();
        if (!st.value("exists", false)) {
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "PQ-NAS DNA identity does not exist yet"}
            }.dump());
            return;
        }

        std::vector<std::string> argv = {
            cli_path,
            "-d", data_dir,
            "contact", "request", recipient,
            "PQ-NAS alerts"
        };

        std::string out;
        int exit_code = -1;
        const bool ok_run = run_cmd_capture_argv(argv, &out, &exit_code);

        if (!ok_run || exit_code != 0) {
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to send DNA contact request"},
                {"detail", out}
            }.dump());
            return;
        }

        reply_json(res, 200, json{
            {"ok", true},
            {"message", "DNA contact request sent"},
            {"detail", out}
        }.dump());
        return;

    } catch (const std::exception& e) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "exception while sending DNA contact request"},
            {"detail", e.what()}
        }.dump());
        return;
    }
});
 // Admin settings API
srv.Post("/api/v4/admin/settings", [&](const httplib::Request& req, httplib::Response& res) {
    if (!require_admin_cookie_users(req, res, COOKIE_KEY, std::string{}, &users)) return;
        if (!require_same_origin_for_cookie_mutation(req, res)) return;

    auto load_settings_json = [&]() -> json {
        try {
            std::ifstream f(admin_settings_path);
            if (!f.good()) return json::object();
            json j;
            f >> j;
            if (!j.is_object()) return json::object();
            return j;
        } catch (...) {
            return json::object();
        }
    };

    // Merge patch into existing file, write atomically (tmp + rename)
    auto save_settings_patch = [&](const json& patch, std::string& err_out) -> bool {
        err_out.clear();
        try {
            if (!patch.is_object()) { err_out = "patch is not an object"; return false; }

            json merged = json::object();
            {
                std::ifstream in(admin_settings_path);
                if (in.good()) {
                    in >> merged;
                    if (!merged.is_object()) merged = json::object();
                }
            }

            for (auto& it : patch.items()) merged[it.key()] = it.value();

            const std::string tmp = admin_settings_path + ".tmp";
            {
                std::ofstream f(tmp, std::ios::trunc);
                if (!f.good()) { err_out = "open tmp for write failed: " + tmp; return false; }
                f << merged.dump(2) << "\n";
                f.flush();
                if (!f.good()) { err_out = "write tmp failed: " + tmp; return false; }
            }

            std::error_code ec;

            // Try atomic rename
            std::filesystem::rename(tmp, admin_settings_path, ec);
            if (ec) {
                // Cleanup best-effort
                std::error_code ec2;
                std::filesystem::remove(tmp, ec2);

                err_out =
                    "rename(" + tmp + " -> " + admin_settings_path + ") failed: " +
                    ec.message() + " (value=" + std::to_string(ec.value()) + ")";
                return false;
            }

            return true;
        } catch (const std::exception& e) {
            err_out = std::string("exception: ") + e.what();
            return false;
        } catch (...) {
            err_out = "unknown exception";
            return false;
        }
    };


    auto is_allowed_level = [&](const std::string& lvl) -> bool {
        return (lvl == "SECURITY" || lvl == "ADMIN" || lvl == "INFO" || lvl == "DEBUG");
    };

    auto is_allowed_rotation_mode = [&](const std::string& m) -> bool {
        return (m == "manual" || m == "daily" || m == "size_mb" || m == "daily_or_size_mb");
    };

	auto is_allowed_theme = [&](const std::string& t) -> bool {
    return (t == "dark" || t == "bright" || t == "cpunk_orange" || t == "win_classic");
	};

	auto is_allowed_dna_alert_level = [&](const std::string& s) -> bool {
	    return (s == "security" || s == "error" || s == "warning" || s == "info");
	};

	auto normalize_dna_connect_alerts = [&](const json& in_cfg, std::string& err) -> json {
	    err.clear();

	    if (!in_cfg.is_object()) {
	        err = "dna_connect_alerts must be an object";
	        return json();
	    }

	    bool enabled = false;
	    if (in_cfg.contains("enabled")) {
	        if (!in_cfg["enabled"].is_boolean()) {
	            err = "dna_connect_alerts.enabled must be boolean";
	            return json();
	        }
	        enabled = in_cfg["enabled"].get<bool>();
	    }

	    std::string recipient = "";
	    if (in_cfg.contains("recipient")) {
	        if (!in_cfg["recipient"].is_string()) {
	            err = "dna_connect_alerts.recipient must be string";
	            return json();
	        }
	        recipient = trim_copy(in_cfg["recipient"].get<std::string>());
	    }

	    std::string cli_path = "/usr/local/bin/dna-connect-cli";
	    if (in_cfg.contains("cli_path")) {
	        if (!in_cfg["cli_path"].is_string()) {
	            err = "dna_connect_alerts.cli_path must be string";
	            return json();
	        }
	        cli_path = trim_copy(in_cfg["cli_path"].get<std::string>());
	    }

	    std::string data_dir = "";
	    if (in_cfg.contains("data_dir")) {
	        if (!in_cfg["data_dir"].is_string()) {
	            err = "dna_connect_alerts.data_dir must be string";
	            return json();
	        }
	        data_dir = trim_copy(in_cfg["data_dir"].get<std::string>());
	    }

	    std::string min_level = "warning";
	    if (in_cfg.contains("min_level")) {
	        if (!in_cfg["min_level"].is_string()) {
	            err = "dna_connect_alerts.min_level must be string";
	            return json();
	        }
	        min_level = trim_copy(in_cfg["min_level"].get<std::string>());
	    }

	    if (!is_allowed_dna_alert_level(min_level)) {
	        err = "dna_connect_alerts.min_level must be one of: security, error, warning, info";
	        return json();
	    }

	    if (enabled) {
	        if (recipient.empty()) {
	            err = "dna_connect_alerts.recipient required when alerts are enabled";
	            return json();
	        }
	        if (cli_path.empty()) {
	            err = "dna_connect_alerts.cli_path required when alerts are enabled";
	            return json();
	        }
	    }

	    return json{
	        {"enabled", enabled},
	        {"recipient", recipient},
	        {"cli_path", cli_path},
	        {"data_dir", data_dir},
	        {"min_level", min_level}
	    };
	};

auto normalize_vault_recovery = [&](const json& in_vault, std::string& err) -> json {
    if (!in_vault.is_object()) {
        err = "vault_recovery must be an object";
        return json();
    }

    // Security: reject private key material at the API boundary. Organization
    // recovery private keys are shown once in the admin browser and must never
    // be persisted on the DNA-Nexus server.
    if (in_vault.contains("private_key_b64") ||
        in_vault.contains("privateKeyB64") ||
        in_vault.contains("private_key") ||
        in_vault.contains("secret_key") ||
        in_vault.contains("secretKey")) {
        err = "vault_recovery private key material must not be sent to server";
        return json();
    }

    json out = json{
        {"enabled", false},
        {"status", "not_configured"},
        {"public_key_b64", ""},
        {"public_key_sha256", ""},
        {"created_at", 0},
        {"label", ""}
    };

    if (in_vault.contains("enabled")) {
        if (!in_vault["enabled"].is_boolean()) {
            err = "vault_recovery.enabled must be boolean";
            return json();
        }
        out["enabled"] = in_vault["enabled"];
    }

    if (in_vault.contains("status")) {
        if (!in_vault["status"].is_string()) {
            err = "vault_recovery.status must be string";
            return json();
        }
        const std::string status = in_vault["status"].get<std::string>();
        if (!(status == "active" || status == "disabled" || status == "not_configured")) {
            err = "vault_recovery.status must be one of: active, disabled, not_configured";
            return json();
        }
        out["status"] = status;
    }

    if (in_vault.contains("public_key_b64")) {
        if (!in_vault["public_key_b64"].is_string()) {
            err = "vault_recovery.public_key_b64 must be string";
            return json();
        }
        const std::string pk = trim_copy(in_vault["public_key_b64"].get<std::string>());
        if (pk.size() > 8192) {
            err = "vault_recovery.public_key_b64 too large";
            return json();
        }
        out["public_key_b64"] = pk;
    }

    if (in_vault.contains("public_key_sha256")) {
        if (!in_vault["public_key_sha256"].is_string()) {
            err = "vault_recovery.public_key_sha256 must be string";
            return json();
        }
        const std::string fp = trim_copy(in_vault["public_key_sha256"].get<std::string>());
        if (fp.size() != 64) {
            err = "vault_recovery.public_key_sha256 must be 64 hex characters";
            return json();
        }
        for (char c : fp) {
            const bool hex =
                (c >= '0' && c <= '9') ||
                (c >= 'a' && c <= 'f') ||
                (c >= 'A' && c <= 'F');
            if (!hex) {
                err = "vault_recovery.public_key_sha256 must be hex";
                return json();
            }
        }
        out["public_key_sha256"] = fp;
    }

    if (in_vault.contains("created_at")) {
        if (!in_vault["created_at"].is_number_integer()) {
            err = "vault_recovery.created_at must be integer";
            return json();
        }
        const long long ts = in_vault["created_at"].get<long long>();
        if (ts < 0) {
            err = "vault_recovery.created_at must be >= 0";
            return json();
        }
        out["created_at"] = ts;
    }

    if (in_vault.contains("label")) {
        if (!in_vault["label"].is_string()) {
            err = "vault_recovery.label must be string";
            return json();
        }
        std::string label = trim_copy(in_vault["label"].get<std::string>());
        if (label.size() > 120) label.resize(120);
        out["label"] = label;
    }

    const bool wants_active =
        out.value("enabled", false) || out.value("status", std::string("")) == "active";

    if (wants_active) {
        if (out.value("public_key_b64", std::string()).empty()) {
            err = "vault_recovery.public_key_b64 required when active";
            return json();
        }
        if (out.value("public_key_sha256", std::string()).empty()) {
            err = "vault_recovery.public_key_sha256 required when active";
            return json();
        }
        out["enabled"] = true;
        out["status"] = "active";
    }

    return out;
};

auto normalize_tiering = [&](const json& in_tier, std::string& err) -> json {
    err.clear();

    if (!in_tier.is_object()) {
        err = "tiering must be an object";
        return json();
    }

    bool enabled = false;
    if (in_tier.contains("enabled")) {
        if (!in_tier["enabled"].is_boolean()) {
            err = "tiering.enabled must be boolean";
            return json();
        }
        enabled = in_tier["enabled"].get<bool>();
    }

    std::string landing_pool_id;
    if (in_tier.contains("landing_pool_id")) {
        if (!in_tier["landing_pool_id"].is_string()) {
            err = "tiering.landing_pool_id must be string";
            return json();
        }
        landing_pool_id = trim_copy(in_tier["landing_pool_id"].get<std::string>());
    }

    auto get_int = [&](const char* key, int def, int lo, int hi) -> int {
        auto it = in_tier.find(key);
        if (it == in_tier.end() || it->is_null()) return def;

        if (!it->is_number_integer()) {
            err = std::string("tiering.") + key + " must be integer";
            return def;
        }

        int v = it->get<int>();
        if (v < lo) v = lo;
        if (v > hi) v = hi;
        return v;
    };

    int worker_interval_sec      = get_int("worker_interval_sec", 60, 5, 3600);
    if (!err.empty()) return json();

    int min_age_sec              = get_int("min_age_sec", 60, 0, 86400);
    if (!err.empty()) return json();

    int max_candidates_per_pass  = get_int("max_candidates_per_pass", 8, 1, 1000);
    if (!err.empty()) return json();

    if (enabled) {
        if (landing_pool_id.empty()) {
            err = "tiering.landing_pool_id required when tiering is enabled";
            return json();
        }

        std::string reason;
        json candidate;
        if (!validate_upload_tiering_pool_id(landing_pool_id, &reason, &candidate)) {
            err = "tiering.landing_pool_id invalid: " + reason;
            return json();
        }
    }

    return json{
        {"enabled", enabled},
        {"landing_pool_id", landing_pool_id},
        {"worker_interval_sec", worker_interval_sec},
        {"min_age_sec", min_age_sec},
        {"max_candidates_per_pass", max_candidates_per_pass}
    };
};

    // SAFE accessor for audit_min_level (never throws)
    auto get_level_safe = [&](const json& j, const std::string& fallback) -> std::string {
        auto it2 = j.find("audit_min_level");
        if (it2 != j.end() && it2->is_string()) return it2->get<std::string>();
        return fallback;
    };
    // transport max upload helper (soft cap; clamped to payload cap)
    auto compute_transport_max = [&](const json& persisted_settings) -> std::uint64_t {
        std::uint64_t tmax = k_payload_max_upload_bytes; // default = payload max
        if (persisted_settings.contains("transport_max_upload_bytes")) {
            try {
                if (persisted_settings["transport_max_upload_bytes"].is_number_integer()) {
                    long long v = persisted_settings["transport_max_upload_bytes"].get<long long>();
                    if (v > 0) tmax = (std::uint64_t)v;
                }
            } catch (...) {}
        }
        if (tmax > k_payload_max_upload_bytes) tmax = k_payload_max_upload_bytes;
        return tmax;
    };
    // Normalize rotation safely (never throws; returns null json on error + sets err)
    auto normalize_rotation = [&](const json& in_rot, std::string& err) -> json {
        err.clear();

        if (!in_rot.is_object()) {
            err = "audit_rotation must be an object";
            return json();
        }

        std::string mode = "manual";
        {
            auto it2 = in_rot.find("mode");
            if (it2 != in_rot.end() && !it2->is_null()) {
                if (!it2->is_string()) {
                    err = "audit_rotation.mode must be string";
                    return json();
                }
                mode = it2->get<std::string>();
            }
        }
        if (!is_allowed_rotation_mode(mode)) {
            err = "audit_rotation.mode must be one of: manual, daily, size_mb, daily_or_size_mb";
            return json();
        }

        auto get_int = [&](const char* key, int def, int lo, int hi) -> int {
            auto it2 = in_rot.find(key);
            if (it2 == in_rot.end() || it2->is_null()) return def;

            if (!it2->is_number_integer()) {
                err = std::string("audit_rotation.") + key + " must be integer";
                return def;
            }
            int v = it2->get<int>();
            if (v < lo) v = lo;
            if (v > hi) v = hi;
            return v;
        };

        // only used for size-based modes
        int max_active_mb = get_int("max_active_mb", 256, 1, 10000000);
        if (!err.empty()) return json();

        // optional tracker field
        std::string rotate_utc_day = "";
        {
            auto it2 = in_rot.find("rotate_utc_day");
            if (it2 != in_rot.end() && it2->is_string()) rotate_utc_day = it2->get<std::string>();
        }

        return json{
            {"mode", mode},
            {"max_active_mb", max_active_mb},
            {"rotate_utc_day", rotate_utc_day},
        };
    };

    // Normalize retention safely (never throws; returns null json on error + sets err)
    auto normalize_retention = [&](const json& in_ret, std::string& err) -> json {
        err.clear();

        if (!in_ret.is_object()) {
            err = "audit_retention must be an object";
            return json();
        }

        // mode
        std::string mode = "never";
        {
            auto it2 = in_ret.find("mode");
            if (it2 != in_ret.end() && !it2->is_null()) {
                if (!it2->is_string()) {
                    err = "audit_retention.mode must be string";
                    return json();
                }
                mode = it2->get<std::string>();
            }
        }

        if (!(mode == "never" || mode == "days" || mode == "files" || mode == "size_mb")) {
            err = "audit_retention.mode must be one of: never, days, files, size_mb";
            return json();
        }

        auto get_int = [&](const char* key, int def, int lo, int hi) -> int {
            auto it2 = in_ret.find(key);
            if (it2 == in_ret.end() || it2->is_null()) return def;

            if (!it2->is_number_integer()) {
                err = std::string("audit_retention.") + key + " must be integer";
                return def;
            }

            int v = it2->get<int>();
            if (v < lo) v = lo;
            if (v > hi) v = hi;
            return v;
        };

        int days         = get_int("days",         90,    1,       3650);
        if (!err.empty()) return json();
        int max_files    = get_int("max_files",    50,    1,      50000);
        if (!err.empty()) return json();
        int max_total_mb = get_int("max_total_mb", 20480, 1,   10000000);
        if (!err.empty()) return json();

        return json{
            {"mode", mode},
            {"days", days},
            {"max_files", max_files},
            {"max_total_mb", max_total_mb},
        };
    };
    auto normalize_snapshots = [&](const json& in_snap, std::string& err) -> json {
        err.clear();
        if (!in_snap.is_object()) { err = "snapshots must be an object"; return json(); }

        bool enabled = in_snap.value("enabled", false);
        if (in_snap.contains("enabled") && !in_snap["enabled"].is_boolean()) {
            err = "snapshots.enabled must be boolean"; return json();
        }

        std::string backend = in_snap.value("backend", "btrfs");
        if (in_snap.contains("backend") && !in_snap["backend"].is_string()) {
            err = "snapshots.backend must be string"; return json();
        }
        if (backend != "btrfs") { err = "snapshots.backend must be: btrfs"; return json(); }

        // per-volume policy flag (persist it!)
        bool per_volume_policy = false;
        if (in_snap.contains("per_volume_policy")) {
            if (!in_snap["per_volume_policy"].is_boolean()) {
                err = "snapshots.per_volume_policy must be boolean";
                return json();
            }
            per_volume_policy = in_snap["per_volume_policy"].get<bool>();
        }
        // schedule
        json sched = in_snap.value("schedule", json::object());
        if (!sched.is_object()) { err = "snapshots.schedule must be an object"; return json(); }

        std::string mode = sched.value("mode", "times_per_day");
        if (sched.contains("mode") && !sched["mode"].is_string()) { err = "snapshots.schedule.mode must be string"; return json(); }
        if (mode != "manual" && mode != "times_per_day") {
            err = "snapshots.schedule.mode must be: manual or times_per_day";
            return json();
        }

        int tpd = sched.value("times_per_day", 6);
        if (sched.contains("times_per_day") && !sched["times_per_day"].is_number_integer()) {
            err = "snapshots.schedule.times_per_day must be integer"; return json();
        }
        if (tpd < 1 || tpd > 24) { err = "snapshots.schedule.times_per_day must be 1..24"; return json(); }

        int jitter = sched.value("jitter_seconds", 120);
        if (sched.contains("jitter_seconds") && !sched["jitter_seconds"].is_number_integer()) {
            err = "snapshots.schedule.jitter_seconds must be integer"; return json();
        }
        if (jitter < 0 || jitter > 3600) { err = "snapshots.schedule.jitter_seconds must be 0..3600"; return json(); }

        // retention
        json ret = in_snap.value("retention", json::object());
        if (!ret.is_object()) { err = "snapshots.retention must be an object"; return json(); }

        auto get_int = [&](const char* key, int def, int lo, int hi) -> int {
            auto it = ret.find(key);
            if (it == ret.end() || it->is_null()) return def;
            if (!it->is_number_integer()) { err = std::string("snapshots.retention.") + key + " must be integer"; return def; }
            int v = it->get<int>();
            if (v < lo) v = lo;
            if (v > hi) v = hi;
            return v;
        };

        int keep_days = get_int("keep_days", 7, 0, 3650);
        if (!err.empty()) return json();
        int keep_min  = get_int("keep_min", 12, 0, 5000);
        if (!err.empty()) return json();
        int keep_max  = get_int("keep_max", 500, 1, 50000);
        if (!err.empty()) return json();
        if (keep_max < keep_min) { err = "snapshots.retention.keep_max must be >= keep_min"; return json(); }

        // volumes
        json vols = in_snap.value("volumes", json::array());
        if (!vols.is_array()) { err = "snapshots.volumes must be array"; return json(); }

        json out_vols = json::array();
        for (const auto& v : vols) {
            if (!v.is_object()) { err = "snapshots.volumes[] must be object"; return json(); }
            if (!v.contains("name") || !v["name"].is_string()) { err = "snapshots.volumes[].name must be string"; return json(); }
            if (!v.contains("source_subvolume") || !v["source_subvolume"].is_string()) { err = "snapshots.volumes[].source_subvolume must be string"; return json(); }
            if (!v.contains("snap_root") || !v["snap_root"].is_string()) { err = "snapshots.volumes[].snap_root must be string"; return json(); }

            std::string src = v["source_subvolume"].get<std::string>();
            std::string dst = v["snap_root"].get<std::string>();
            if (src.empty() || src[0] != '/') { err = "snapshots.volumes[].source_subvolume must be absolute path"; return json(); }

			            if (dst.empty() || dst[0] != '/') { err = "snapshots.volumes[].snap_root must be absolute path"; return json(); }

            // optional per-volume override schedule (only if per_volume_policy=true)
            json vsched = json(); // null by default
            if (per_volume_policy && v.contains("schedule")) {
                if (!v["schedule"].is_object()) { err = "snapshots.volumes[].schedule must be object"; return json(); }

                std::string vmode = v["schedule"].value("mode", "times_per_day");
                if (v["schedule"].contains("mode") && !v["schedule"]["mode"].is_string()) {
                    err = "snapshots.volumes[].schedule.mode must be string";
                    return json();
                }
                if (vmode != "manual" && vmode != "times_per_day") {
                    err = "snapshots.volumes[].schedule.mode must be: manual or times_per_day";
                    return json();
                }

                int vtpd = v["schedule"].value("times_per_day", tpd);
                if (v["schedule"].contains("times_per_day") && !v["schedule"]["times_per_day"].is_number_integer()) {
                    err = "snapshots.volumes[].schedule.times_per_day must be integer";
                    return json();
                }
                if (vtpd < 1 || vtpd > 24) { err = "snapshots.volumes[].schedule.times_per_day must be 1..24"; return json(); }

                int vjit = v["schedule"].value("jitter_seconds", jitter);
                if (v["schedule"].contains("jitter_seconds") && !v["schedule"]["jitter_seconds"].is_number_integer()) {
                    err = "snapshots.volumes[].schedule.jitter_seconds must be integer";
                    return json();
                }
                if (vjit < 0 || vjit > 3600) { err = "snapshots.volumes[].schedule.jitter_seconds must be 0..3600"; return json(); }

                vsched = json{{"mode","times_per_day"},{"times_per_day", vtpd},{"jitter_seconds", vjit}};
            }

            json outv = json{
                {"name", v["name"].get<std::string>()},
                {"source_subvolume", src},
                {"snap_root", dst}
            };
            if (per_volume_policy && vsched.is_object()) outv["schedule"] = vsched;

            out_vols.push_back(outv);

        }

       return json{
            {"enabled", enabled},
            {"backend", backend},
            {"per_volume_policy", per_volume_policy},
            {"schedule", json{{"mode", mode},{"times_per_day", tpd},{"jitter_seconds", jitter}}},
            {"retention", json{{"keep_days", keep_days},{"keep_min", keep_min},{"keep_max", keep_max}}},
            {"volumes", out_vols}
        };
    };

    try {
        if (req.body.empty()) {
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "empty request body (expected JSON object)"}
            }.dump());
            return;
        }

        json in = json::parse(req.body, nullptr, false);
        if (in.is_discarded()) {
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "invalid json"}
            }.dump());
            return;
        }

        if (!in.is_object()) {
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "json must be object"}
            }.dump());
            return;
        }

        json persisted = load_settings_json();
        if (!persisted.is_object()) persisted = json::object();

        const std::string before_runtime   = audit.min_level_str();
        const std::string before_persisted = get_level_safe(persisted, before_runtime);

        json before_rotation = json::object();
        if (persisted.contains("audit_rotation") && persisted["audit_rotation"].is_object()) {
            before_rotation = persisted["audit_rotation"];
        }

        json before_ret = json::object();
        if (persisted.contains("audit_retention") && persisted["audit_retention"].is_object()) {
            before_ret = persisted["audit_retention"];
        }

        std::string before_theme = "dark";
        if (persisted.contains("ui_theme") && persisted["ui_theme"].is_string()) {
            before_theme = persisted["ui_theme"].get<std::string>();
        }
        if (!is_allowed_theme(before_theme)) before_theme = "dark";

        bool changed_level    = false;
        bool changed_ret      = false;
        bool changed_rotation = false;
        bool changed_theme    = false;
        bool changed_snapshots = false;
        bool changed_transport = false;
		bool changed_tiering   = false;
        bool changed_vault_recovery = false;
        bool changed_dna_alerts = false;

        json patch = json::object();

        // ---- audit_min_level (optional) ----
        if (in.contains("audit_min_level")) {
            if (!in["audit_min_level"].is_string()) {
                reply_json(res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "audit_min_level must be string"}
                }.dump());
                return;
            }

            const std::string lvl = in["audit_min_level"].get<std::string>();
            if (!is_allowed_level(lvl) || !audit.set_min_level_str(lvl)) {
                reply_json(res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "invalid audit_min_level"}
                }.dump());
                return;
            }

            patch["audit_min_level"] = lvl;
            persisted["audit_min_level"] = lvl; // for response shaping
            changed_level = true;

            // Audit (best-effort)
            try {
                pqnas::AuditEvent ev;
                ev.event = "admin.settings_changed";
                ev.outcome = "ok";
                ev.f["audit_min_level_before"] = before_persisted;
                ev.f["audit_min_level_after"] = lvl;
                ev.f["audit_min_level_runtime_after"] = audit.min_level_str();
                ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
                auto it_ua = req.headers.find("User-Agent");
                ev.f["ua"] = pqnas::shorten(it_ua == req.headers.end() ? "" : it_ua->second);
                audit_append(ev);
            } catch (...) {}
        }

        // ---- audit_retention (optional) ----
        if (in.contains("audit_retention")) {
            std::string err;
            json norm = normalize_retention(in["audit_retention"], err);
            if (!err.empty()) {
                reply_json(res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", err}
                }.dump());
                return;
            }

            patch["audit_retention"] = norm;
            persisted["audit_retention"] = norm; // for response shaping
            changed_ret = true;

            // Audit (best-effort)
            try {
                pqnas::AuditEvent ev;
                ev.event = "admin.settings_changed";
                ev.outcome = "ok";
                ev.f["audit_retention_before"] = before_ret.is_null() ? json::object() : before_ret;
                ev.f["audit_retention_after"]  = norm;
                ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
                auto it_ua = req.headers.find("User-Agent");
                ev.f["ua"] = pqnas::shorten(it_ua == req.headers.end() ? "" : it_ua->second);
                audit_append(ev);
            } catch (...) {}
        }
        // ---- snapshots (optional) ----
        if (in.contains("snapshots")) {
            std::string e2;
            json norm = normalize_snapshots(in["snapshots"], e2);
            if (!e2.empty()) {
                reply_json(res, 400, json{{"ok", false}, {"message", e2}});
                return;
            }

            json before_snap = json::object();
            if (persisted.contains("snapshots") && persisted["snapshots"].is_object()) {
                before_snap = persisted["snapshots"];
            }

            patch["snapshots"] = norm;
            persisted["snapshots"] = norm; // for response shaping

            changed_snapshots = true;

            // Audit (best-effort)
            try {
                pqnas::AuditEvent ev;
                ev.event = "admin.settings_changed";
                ev.outcome = "ok";
                ev.f["snapshots_before"] = before_snap.is_null() ? json::object() : before_snap;
                ev.f["snapshots_after"]  = norm;
                ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
                auto it_ua = req.headers.find("User-Agent");
                ev.f["ua"] = pqnas::shorten(it_ua == req.headers.end() ? "" : it_ua->second);
                audit_append(ev);
            } catch (...) {}

        }

		// ---- ui_theme (optional) ----
		if (in.contains("ui_theme")) {
    		if (!in["ui_theme"].is_string()) {
        		reply_json(res, 400, json{
            		{"ok", false},
            		{"error", "bad_request"},
            		{"message", "ui_theme must be string"}
        		}.dump());
        		return;
    		}

    		std::string t = in["ui_theme"].get<std::string>();

    		if (!is_allowed_theme(t)) {
        		reply_json(res, 400, json{
            		{"ok", false},
            		{"error", "bad_request"},
            		{"message", "invalid ui_theme (allowed: dark, bright, cpunk_orange, win_classic)"}
        		}.dump());
        		return;
    		}

    		patch["ui_theme"] = t;
    		persisted["ui_theme"] = t;
    		changed_theme = true;

    		// Audit (best-effort)
    		try {
        		pqnas::AuditEvent ev;
        		ev.event = "admin.settings_changed";
        		ev.outcome = "ok";
        		ev.f["ui_theme_after"] = t;
        		ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
        		auto it_ua = req.headers.find("User-Agent");
        		ev.f["ua"] = pqnas::shorten(it_ua == req.headers.end() ? "" : it_ua->second);
        		audit_append(ev);
    		} catch (...) {}
		}


        // ---- audit_rotation (optional) ----
        if (in.contains("audit_rotation")) {
            std::string err;
            json norm = normalize_rotation(in["audit_rotation"], err);
            if (!err.empty()) {
                reply_json(res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", err}
                }.dump());
                return;
            }

            patch["audit_rotation"] = norm;
            persisted["audit_rotation"] = norm; // for response shaping
            changed_rotation = true;

            // Audit (best-effort)
            try {
                pqnas::AuditEvent ev;
                ev.event = "admin.settings_changed";
                ev.outcome = "ok";
                ev.f["audit_rotation_before"] = before_rotation.is_null() ? json::object() : before_rotation;
                ev.f["audit_rotation_after"]  = norm;
                ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
                auto it_ua = req.headers.find("User-Agent");
                ev.f["ua"] = pqnas::shorten(it_ua == req.headers.end() ? "" : it_ua->second);
                audit_append(ev);
            } catch (...) {}
        }
		// ---- tiering (optional) ----
		if (in.contains("tiering")) {
		    std::string err;
		    json norm = normalize_tiering(in["tiering"], err);
    		if (!err.empty()) {
		        reply_json(res, 400, json{
        		    {"ok", false},
		            {"error", "bad_request"},
        		    {"message", err}
		        }.dump());
        		return;
		    }

		    json before_tiering = json::object();
    		if (persisted.contains("tiering") && persisted["tiering"].is_object()) {
		        before_tiering = persisted["tiering"];
    		}

		    patch["tiering"] = norm;
    		persisted["tiering"] = norm;
    		changed_tiering = true;

		    // Optional runtime mirror to environment-style globals, if you have such setters later.
    		// For now this is persisted config only; worker/upload path can keep reading persisted/admin settings.

    		try {
        		pqnas::AuditEvent ev;
		        ev.event = "admin.settings_changed";
        		ev.outcome = "ok";
		        ev.f["tiering_before"] = before_tiering.is_null() ? json::object() : before_tiering;
        		ev.f["tiering_after"]  = norm;
		        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
        		auto it_ua = req.headers.find("User-Agent");
		        ev.f["ua"] = pqnas::shorten(it_ua == req.headers.end() ? "" : it_ua->second);
        		audit_append(ev);
		    } catch (...) {}
		}
				// ---- dna_connect_alerts (optional) ----
		if (in.contains("dna_connect_alerts")) {
		    std::string err;
		    json norm = normalize_dna_connect_alerts(in["dna_connect_alerts"], err);
		    if (!err.empty()) {
		        reply_json(res, 400, json{
		            {"ok", false},
		            {"error", "bad_request"},
		            {"message", err}
		        }.dump());
		        return;
		    }

		    json before_dna_alerts = json::object();
		    if (persisted.contains("dna_connect_alerts") && persisted["dna_connect_alerts"].is_object()) {
		        before_dna_alerts = persisted["dna_connect_alerts"];
		    }

		    patch["dna_connect_alerts"] = norm;
		    persisted["dna_connect_alerts"] = norm;
		    changed_dna_alerts = true;

		    try {
		        pqnas::AuditEvent ev;
		        ev.event = "admin.settings_changed";
		        ev.outcome = "ok";
		        ev.f["dna_connect_alerts_before"] = before_dna_alerts.is_null() ? json::object() : before_dna_alerts;
		        ev.f["dna_connect_alerts_after"]  = norm;
		        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
		        auto it_ua = req.headers.find("User-Agent");
		        ev.f["ua"] = pqnas::shorten(it_ua == req.headers.end() ? "" : it_ua->second);
		        audit_append(ev);
		    } catch (...) {}
		}
        // ---- vault_recovery (optional) ----
        if (in.contains("vault_recovery")) {
            std::string vault_recovery_err;
            json norm = normalize_vault_recovery(in["vault_recovery"], vault_recovery_err);
            if (!vault_recovery_err.empty()) {
                res.status = 400;
                res.set_header("Content-Type", "application/json; charset=utf-8");
                res.body = json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", vault_recovery_err}
                }.dump();
                return;
            }

            patch["vault_recovery"] = norm;
            persisted["vault_recovery"] = norm;
            changed_vault_recovery = true;
        }

       // ---- transport_max_upload_bytes (optional) ----
        if (in.contains("transport_max_upload_bytes")) {
            // Accept: integer only (safe-by-default)
            if (!in["transport_max_upload_bytes"].is_number_integer()) {
                reply_json(res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "transport_max_upload_bytes must be integer bytes"}
                }.dump());
                return;
            }

            long long v = 0;
            try {
                v = in["transport_max_upload_bytes"].get<long long>();
            } catch (...) {
                reply_json(res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "transport_max_upload_bytes invalid"}
                }.dump());
                return;
            }

            if (v <= 0) {
                reply_json(res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "transport_max_upload_bytes must be > 0"}
                }.dump());
                return;
            }

            std::uint64_t tmax_new = (std::uint64_t)v;
            if (tmax_new > k_payload_max_upload_bytes) tmax_new = k_payload_max_upload_bytes;

            std::uint64_t before_tmax = compute_transport_max(persisted);

            patch["transport_max_upload_bytes"] = tmax_new;
            persisted["transport_max_upload_bytes"] = tmax_new; // for response shaping
            changed_transport = true;

            // Audit (best-effort)
            try {
                pqnas::AuditEvent ev;
                ev.event = "admin.settings_changed";
                ev.outcome = "ok";
                ev.f["transport_max_upload_bytes_before"] = std::to_string((unsigned long long)before_tmax);
                ev.f["transport_max_upload_bytes_after"]  = std::to_string((unsigned long long)tmax_new);
                ev.f["payload_max_upload_bytes"] = std::to_string((unsigned long long)k_payload_max_upload_bytes);
                ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
                auto it_ua = req.headers.find("User-Agent");
                ev.f["ua"] = pqnas::shorten(it_ua == req.headers.end() ? "" : it_ua->second);
                audit_append(ev);
            } catch (...) {}
        }

        if (!changed_level && !changed_ret && !changed_rotation && !changed_theme &&
                !changed_snapshots && !changed_transport && !changed_tiering && !changed_dna_alerts && !changed_vault_recovery) {
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                                {"message", "nothing to update (provide audit_min_level and/or audit_retention and/or audit_rotation and/or ui_theme and/or snapshots and/or tiering and/or dna_connect_alerts and/or vault_recovery and/or transport_max_upload_bytes)"}
            }.dump());
            return;
        }

        json snapshots = json::object();
        if (persisted.contains("snapshots") && persisted["snapshots"].is_object()) {
            snapshots = persisted["snapshots"];
        }

		json tiering = json::object();
		if (persisted.contains("tiering") && persisted["tiering"].is_object()) {
		    tiering = persisted["tiering"];
		} else {
		    tiering = json{
        		{"enabled", false},
		        {"landing_pool_id", ""},
		        {"worker_interval_sec", 60},
		        {"min_age_sec", 60},
		        {"max_candidates_per_pass", 8}
    		};
		}

		if (!tiering.contains("enabled") || !tiering["enabled"].is_boolean()) {
		    tiering["enabled"] = false;
		}
		if (!tiering.contains("landing_pool_id") || !tiering["landing_pool_id"].is_string()) {
		    tiering["landing_pool_id"] = "";
		}
		if (!tiering.contains("worker_interval_sec") || !tiering["worker_interval_sec"].is_number_integer()) {
		    tiering["worker_interval_sec"] = 60;
		}
		if (!tiering.contains("min_age_sec") || !tiering["min_age_sec"].is_number_integer()) {
		    tiering["min_age_sec"] = 60;
		}
		if (!tiering.contains("max_candidates_per_pass") || !tiering["max_candidates_per_pass"].is_number_integer()) {
		    tiering["max_candidates_per_pass"] = 8;
		}
		json dna_connect_alerts = json::object();
		if (persisted.contains("dna_connect_alerts") && persisted["dna_connect_alerts"].is_object()) {
		    dna_connect_alerts = persisted["dna_connect_alerts"];
		} else {
		    dna_connect_alerts = json{
		        {"enabled", false},
		        {"recipient", ""},
		        {"cli_path", "/usr/local/bin/dna-connect-cli"},
		        {"data_dir", ""},
		        {"min_level", "warning"}
		    };
		}

        json vault_recovery = json{
            {"enabled", false},
            {"status", "not_configured"},
            {"public_key_b64", ""},
            {"public_key_sha256", ""},
            {"created_at", 0},
            {"label", ""}
        };
        if (persisted.contains("vault_recovery") && persisted["vault_recovery"].is_object()) {
            std::string vr_err;
            json norm_vr = normalize_vault_recovery(persisted["vault_recovery"], vr_err);
            if (vr_err.empty() && norm_vr.is_object()) {
                vault_recovery = norm_vr;
            }
        }

		if (!dna_connect_alerts.contains("enabled") || !dna_connect_alerts["enabled"].is_boolean()) {
		    dna_connect_alerts["enabled"] = false;
		}
		if (!dna_connect_alerts.contains("recipient") || !dna_connect_alerts["recipient"].is_string()) {
		    dna_connect_alerts["recipient"] = "";
		}
		if (!dna_connect_alerts.contains("cli_path") || !dna_connect_alerts["cli_path"].is_string()) {
		    dna_connect_alerts["cli_path"] = "/usr/local/bin/dna-connect-cli";
		}
		if (!dna_connect_alerts.contains("data_dir") || !dna_connect_alerts["data_dir"].is_string()) {
		    dna_connect_alerts["data_dir"] = "";
		}
		if (!dna_connect_alerts.contains("min_level") || !dna_connect_alerts["min_level"].is_string()) {
		    dna_connect_alerts["min_level"] = "warning";
		}
		{
		    const std::string ml = dna_connect_alerts["min_level"].get<std::string>();
		    if (!(ml == "security" || ml == "error" || ml == "warning" || ml == "info")) {
		        dna_connect_alerts["min_level"] = "warning";
		    }
		}
        std::string save_err;
        if (!save_settings_patch(patch, save_err)) {
            // Also audit the failure (best-effort)
            try {
                pqnas::AuditEvent ev;
                ev.event = "admin.settings_save_failed";
                ev.outcome = "fail";
                ev.f["path"] = admin_settings_path;
                ev.f["detail"] = save_err;
                ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
                auto it_ua = req.headers.find("User-Agent");
                ev.f["ua"] = pqnas::shorten(it_ua == req.headers.end() ? "" : it_ua->second);
                audit_append(ev);
            } catch (...) {}

            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to save settings"},
                {"detail", save_err},
                {"path", admin_settings_path}
            }.dump());
            return;
        }


        // Reply with current state (defaults if missing)
        json retention = json::object();
        if (persisted.contains("audit_retention") && persisted["audit_retention"].is_object()) {
            retention = persisted["audit_retention"];
        } else {
            retention = json{
                {"mode","never"},{"days",90},{"max_files",50},{"max_total_mb",20480}
            };
        }

        // ---- audit_rotation in response ----
        json rotation = json::object();
        if (persisted.contains("audit_rotation") && persisted["audit_rotation"].is_object()) {
            rotation = persisted["audit_rotation"];
        } else {
            rotation = json{
                {"mode","manual"},
                {"max_active_mb",256},
                {"rotate_utc_day",""}
            };
        }

        // ---- ui_theme in response ----
        std::string ui_theme_value = "dark";
        if (persisted.contains("ui_theme") && persisted["ui_theme"].is_string()) {
            ui_theme_value = persisted["ui_theme"].get<std::string>();
        }
        if (!is_allowed_theme(ui_theme_value)) ui_theme_value = "dark";

        const long long active_bytes = file_size_bytes_safe(audit_jsonl_path);
        const std::uint64_t tmax = compute_transport_max(persisted);
		json storage_roots = json::object();
		storage_roots["data_root"] = pqnas::data_root_dir();

		{
		    json pools = json::array();
		    pools.push_back(json{
        		{"pool_id", "default"},
		        {"mount", pqnas::data_root_dir()}
    		});

		    {
        		std::lock_guard<std::mutex> lk(pool_mu());
		        for (const auto& kv : pool_mount_by_id()) {
        		    const std::string& pid = kv.first;
		            const std::string& mount = kv.second;
        		    if (pid.empty() || pid == "default") continue;
		            pools.push_back(json{
        		        {"pool_id", pid},
                		{"mount", mount}
		            });
        		}
		    }

    		storage_roots["pools"] = std::move(pools);
		}

        reply_json(res, 200, json{
            {"ok", true},

            {"audit_min_level", get_level_safe(persisted, audit.min_level_str())},
            {"audit_min_level_runtime", audit.min_level_str()},
            {"allowed", json::array({"SECURITY","ADMIN","INFO","DEBUG"})},

            {"audit_retention", retention},
            {"audit_rotation", rotation},

            {"audit_active_path", audit_jsonl_path},
            {"audit_active_bytes", active_bytes},

            {"ui_theme", ui_theme_value},
            {"snapshots", snapshots},
            {"storage_roots", storage_roots},
			{"tiering", tiering},
			{"dna_connect_alerts", dna_connect_alerts},
            {"vault_recovery", vault_recovery},
    		{"upload_tiering_candidates", build_upload_tiering_candidates_json()},

            {"transport_max_upload_bytes", tmax},
            {"payload_max_upload_bytes", k_payload_max_upload_bytes}
        }.dump());

        return;

    } catch (const std::exception& e) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "exception while saving settings"},
            {"detail", e.what()}
        }.dump());
        return;
    }
});
srv.Post("/api/v4/admin/settings/create-dna-alert-identity", [&](const httplib::Request& req, httplib::Response& res) {
    if (!require_admin_cookie_users(req, res, COOKIE_KEY, std::string{}, &users)) return;
        if (!require_same_origin_for_cookie_mutation(req, res)) return;

    try {
        const json persisted = load_admin_settings_json_safe();
        json cfg = json::object();
        if (persisted.contains("dna_connect_alerts") && persisted["dna_connect_alerts"].is_object()) {
            cfg = persisted["dna_connect_alerts"];
        }

        const std::string cli_path = trim_copy(cfg.value("cli_path", std::string("/usr/local/bin/dna-connect-cli")));
        const std::string data_dir = trim_copy(cfg.value("data_dir", std::string("/var/lib/pqnas/dna-alerts")));

        if (cli_path.empty() || !std::filesystem::exists(cli_path)) {
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "dna-connect-cli not found"},
                {"detail", std::string("cli_path does not exist: ") + cli_path}
            }.dump());
            return;
        }

        if (data_dir.empty()) {
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "DNA data directory is empty"}
            }.dump());
            return;
        }

        std::error_code ec;
        std::filesystem::create_directories(data_dir, ec);

        auto status = build_dna_connect_identity_status_json();
        if (status.value("exists", false)) {
            reply_json(res, 200, json{
                {"ok", true},
                {"message", "DNA identity already exists"},
                {"dna_connect_identity", status}
            }.dump());
            return;
        }

        std::vector<std::string> argv = {
            cli_path,
            "-d", data_dir,
            "identity", "create", "pqnasalerts"
        };

        std::string out;
        int exit_code = -1;
        const bool ok_run = run_cmd_capture_argv(argv, &out, &exit_code);

        if (!ok_run || exit_code != 0) {
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to create DNA identity"},
                {"detail", out}
            }.dump());
            return;
        }

        auto after = build_dna_connect_identity_status_json();

        reply_json(res, 200, json{
            {"ok", true},
            {"message", "DNA identity created"},
            {"detail", out},
            {"dna_connect_identity", after}
        }.dump());
        return;

    } catch (const std::exception& e) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "exception while creating DNA identity"},
            {"detail", e.what()}
        }.dump());
        return;
    }
});

srv.Get("/api/v4/admin/settings/dna-alert-identity-info", [&](const httplib::Request& req, httplib::Response& res) {
    if (!require_admin_cookie_users(req, res, COOKIE_KEY, std::string{}, &users)) return;

    json st = build_dna_connect_identity_status_json();
    reply_json(res, 200, json{
        {"ok", true},
        {"dna_connect_identity", st}
    }.dump());
});

srv.Post("/api/v4/admin/settings/send-dna-alert-contact-request", [&](const httplib::Request& req, httplib::Response& res) {
    if (!require_admin_cookie_users(req, res, COOKIE_KEY, std::string{}, &users)) return;
        if (!require_same_origin_for_cookie_mutation(req, res)) return;

    try {
        const json persisted = load_admin_settings_json_safe();
        json cfg = json::object();
        if (persisted.contains("dna_connect_alerts") && persisted["dna_connect_alerts"].is_object()) {
            cfg = persisted["dna_connect_alerts"];
        }

        const std::string cli_path = trim_copy(cfg.value("cli_path", std::string("/usr/local/bin/dna-connect-cli")));
        const std::string data_dir = trim_copy(cfg.value("data_dir", std::string("/var/lib/pqnas/dna-alerts")));
        const std::string recipient = trim_copy(cfg.value("recipient", std::string()));

        if (recipient.empty()) {
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "recipient is empty in dna_connect_alerts settings"}
            }.dump());
            return;
        }

        auto st = build_dna_connect_identity_status_json();
        if (!st.value("exists", false)) {
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "PQ-NAS DNA identity does not exist yet"}
            }.dump());
            return;
        }

        std::vector<std::string> argv = {
            cli_path,
            "-d", data_dir,
            "contact", "request", recipient,
            "PQ-NAS alerts"
        };

        std::string out;
        int exit_code = -1;
        const bool ok_run = run_cmd_capture_argv(argv, &out, &exit_code);

        if (!ok_run || exit_code != 0) {
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to send DNA contact request"},
                {"detail", out}
            }.dump());
            return;
        }

        reply_json(res, 200, json{
            {"ok", true},
            {"message", "DNA contact request sent"},
            {"detail", out}
        }.dump());
        return;

    } catch (const std::exception& e) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "exception while sending contact request"},
            {"detail", e.what()}
        }.dump());
        return;
    }
});
	// GET /api/v4/me  (returns role + decoded fingerprint)
