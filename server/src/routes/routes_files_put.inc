// ---- Files API (user storage) ----
// PUT /api/v4/files/put?path=relative/path.bin[&overwrite=1]
// Body: raw bytes (streamed to temp file, then renamed atomically)
srv.Put("/api/v4/files/put",
    [&](const httplib::Request& req,
    httplib::Response& res,
    const httplib::ContentReader& content_reader) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;

    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto header_u64 = [&](const char* name, std::uint64_t* out) -> bool {
        if (!out) return false;
        auto it = req.headers.find(name);
        if (it == req.headers.end()) return false;
        const std::string& s = it->second;
        try {
            size_t idx = 0;
            unsigned long long v = std::stoull(s, &idx, 10);
            if (idx != s.size()) return false;
            *out = (std::uint64_t)v;
            return true;
        } catch (...) {
            return false;
        }
    };

    auto audit_fail = [&](const std::string& reason, int http, const std::string& detail = "") {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_put_fail";
        ev.outcome = "fail";
        ev.f["fingerprint"] = fp_hex;
        ev.f["reason"] = reason;
        ev.f["http"] = std::to_string(http);
        if (!detail.empty()) ev.f["detail"] = pqnas::shorten(detail, 180);

        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
        audit_append(ev);
    };

    auto audit_quota_deny = [&](const std::string& rel_path, const pqnas::QuotaCheckResult& qc) {
        pqnas::AuditEvent ev;
        ev.event = "user_storage_quota_exceeded";
        ev.outcome = "deny";
        ev.f["fingerprint"] = fp_hex;
        ev.f["path"] = pqnas::shorten(rel_path, 200);

        ev.f["used_bytes"] = std::to_string((unsigned long long)qc.used_bytes);
        ev.f["quota_bytes"] = std::to_string((unsigned long long)qc.quota_bytes);
        ev.f["incoming_bytes"] = std::to_string((unsigned long long)qc.incoming_bytes);
        ev.f["existing_bytes"] = std::to_string((unsigned long long)qc.existing_bytes);
        ev.f["would_used_bytes"] = std::to_string((unsigned long long)qc.would_used_bytes);

        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
        audit_append(ev);
    };

    auto audit_ok = [&](const std::string& rel_path, std::uint64_t bytes) {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_put_ok";
        ev.outcome = "ok";
        ev.f["fingerprint"] = fp_hex;
        ev.f["path"] = pqnas::shorten(rel_path, 200);
        ev.f["bytes"] = std::to_string((unsigned long long)bytes);

        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
        audit_append(ev);
    };
    if (!require_same_origin_for_cookie_mutation(req, res)) {
        audit_fail("origin_mismatch", 403);
        return;
    }
    auto file_time_to_epoch_sec = [](const std::filesystem::file_time_type& ft) -> std::int64_t {
        using namespace std::chrono;
        const auto sctp = time_point_cast<system_clock::duration>(
            ft - std::filesystem::file_time_type::clock::now() + system_clock::now()
        );
        return (std::int64_t)duration_cast<seconds>(sctp.time_since_epoch()).count();
    };

    // path param
    std::string rel_path;
    if (req.has_param("path")) rel_path = req.get_param_value("path");
    if (rel_path.empty()) {
        audit_fail("missing_path", 400);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "missing path"}
        }.dump());
        return;
    }

    std::string rel_norm;
    {
        std::string nerr;
        if (!pqnas::normalize_user_rel_path_strict(rel_path, &rel_norm, &nerr)) {
            audit_fail("invalid_path", 400, nerr);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "invalid path"}
            }.dump());
            return;
        }
    }

        bool overwrite = false;
        if (req.has_param("overwrite")) {
            const std::string ov = req.get_param_value("overwrite");
            overwrite = (ov == "1" || ov == "true" || ov == "yes");
        }

     // Serialize writes on overlapping logical paths for this user.
    // This prevents PUT/MOVE/DELETE races on the same file/subtree.
    if (!require_user_no_live_lock_for_write_local(
            users, users_path, res, fp_hex, rel_norm, "write", false)) {
        return;
    }

    auto* plm = pqnas::get_path_lock_manager();
    auto write_guard = plm->lock_paths(fp_hex, {rel_norm});

    // Refuse uploads under a path whose ancestor is already a file.
    // Example: if "a" is a file, "a/b.txt" must be rejected.
    {
        std::string found_ancestor;
        std::string aerr;
        const bool conflict =
            pqnas::any_file_ancestor_exists(users, fp_hex, rel_norm, &found_ancestor, &aerr);

        if (!aerr.empty()) {
            audit_fail("ancestor_check_failed", 500, aerr);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "ancestor conflict check failed"},
                {"detail", pqnas::shorten(aerr, 180)}
            }.dump());
            return;
        }

        if (conflict) {
            audit_fail("ancestor_is_file", 409, found_ancestor);
            reply_json(res, 409, json{
                {"ok", false},
                {"error", "path_conflict"},
                {"message", "a parent path is an existing file"},
                {"ancestor", found_ancestor}
            }.dump());
            return;
        }
    }

    {
        auto* idx = pqnas::get_file_location_index();
        if (!idx) {
            audit_fail("metadata_index_missing", 500);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "metadata index missing"}
            }.dump());
            return;
        }

        std::string derr;
        const bool has_descendants = idx->logical_dir_exists(fp_hex, rel_norm, &derr);

        if (!derr.empty()) {
            audit_fail("descendant_check_failed", 500, derr);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "descendant conflict check failed"},
                {"detail", pqnas::shorten(derr, 180)}
            }.dump());
            return;
        }

        if (has_descendants) {
            audit_fail("path_has_descendants", 409, rel_norm);
            reply_json(res, 409, json{
                {"ok", false},
                {"error", "path_conflict"},
                {"message", "path already exists as a logical directory"},
                {"path", rel_norm}
            }.dump());
            return;
        }
    }

    // Require Content-Length for v1 streamer so quota can be checked before write.
    std::uint64_t cl = 0;
    if (!header_u64("Content-Length", &cl)) {
        audit_fail("missing_content_length", 411);
        reply_json(res, 411, json{
            {"ok", false},
            {"error", "length_required"},
            {"message", "Content-Length required"}
        }.dump());
        return;
    }

    const std::uint64_t incoming_bytes = cl;

    auto drain_request_body_best_effort = [&]() {
        try {
            content_reader([&](const char* /*data*/, size_t /*len*/) {
                return true; // consume and discard everything
            });
        } catch (...) {
            // best effort only
        }
    };

    // transport max (server-controlled, distinct from quota)
    const std::uint64_t transport_max =
        (g_transport_max_upload_bytes ? g_transport_max_upload_bytes : k_payload_max_upload_bytes);

    if (incoming_bytes > transport_max) {
        audit_fail("transport_limit_exceeded", 413,
                   "Content-Length=" + std::to_string((unsigned long long)incoming_bytes) +
                   " max=" + std::to_string((unsigned long long)transport_max));
        reply_json(res, 413, json{
            {"ok", false},
            {"error", "transport_limit_exceeded"},
            {"message", "Upload exceeds maximum allowed size"},
            {"content_length", incoming_bytes},
            {"max_bytes", transport_max},
            {"payload_max_upload_bytes", k_payload_max_upload_bytes}
        }.dump());
        return;
    }

    // quota + path resolve
    const std::filesystem::path user_dir = user_dir_for_fp(users, fp_hex);
    pqnas::QuotaCheckResult qc = pqnas::quota_check_for_upload_v1(
        users, fp_hex, user_dir, rel_path, incoming_bytes
    );

    if (!qc.ok) {
        if (qc.error == "quota_exceeded") audit_quota_deny(rel_path, qc);
        if (qc.error == "invalid_path") audit_fail("invalid_path", 400);
        if (qc.error != "storage_unallocated" && qc.error != "invalid_path" && qc.error != "quota_exceeded") {
            audit_fail("quota_check_failed", 403, qc.error);
        }
        if (reply_quota_error_v1(res, fp_hex, qc)) return;
    }

    const UploadTieringConfig tier_cfg = upload_tiering_config();

     std::filesystem::path out_abs = qc.abs_path;
    bool tiering_write = false;

     if (tier_cfg.enabled) {
        std::string terr;
        if (!build_landing_abs_path(tier_cfg.landing_pool_id, fp_hex, rel_norm, &out_abs, &terr)) {
            audit_fail("tiering_landing_pool_invalid", 500, terr);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "tiering landing pool not found"},
                {"detail", pqnas::shorten(terr, 180)}
            }.dump());
            return;
        }
        tiering_write = true;
    }

     {
        std::string serr;
        if (!ensure_no_symlink_in_existing_path_prefix(out_abs.parent_path(), &serr)) {

            audit_fail("symlink_not_supported", 400, serr);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "symlinks not supported"},
                {"detail", serr}
            }.dump());
            return;
        }
    }

    // Detect existing logical file before writing.
    // Metadata index is authoritative for logical existence.
    auto* idx = pqnas::get_file_location_index();
    if (!idx) {
        audit_fail("metadata_index_missing", 500);
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "metadata index missing"}
        }.dump());
        return;
    }

    std::optional<pqnas::FileLocationRecord> existing_rec;
    std::string existing_rec_err;
    existing_rec = idx->get(fp_hex, rel_norm, &existing_rec_err);
    if (!existing_rec_err.empty()) {
        audit_fail("metadata_lookup_failed", 500, existing_rec_err);
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "metadata lookup failed"},
            {"detail", pqnas::shorten(existing_rec_err, 180)}
        }.dump());
        return;
    }

    bool physical_exists_at_target = false;
    std::uint64_t physical_existing_size = 0;
    std::int64_t physical_existing_mtime = 0;

    {
        std::error_code ec;
        auto st = std::filesystem::symlink_status(out_abs, ec);
        if (ec) {
            if (ec != std::make_error_code(std::errc::no_such_file_or_directory)) {
                audit_fail("target_exists_check_failed", 500, ec.message());
                reply_json(res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "target existence check failed"},
                    {"detail", pqnas::shorten(ec.message(), 180)}
                }.dump());
                return;
            }
            ec.clear();
        } else {
            physical_exists_at_target = std::filesystem::exists(st);

            if (physical_exists_at_target) {
                if (std::filesystem::is_symlink(st)) {
                    audit_fail("target_symlink_not_supported", 409, out_abs.string());
                    reply_json(res, 409, json{
                        {"ok", false},
                        {"error", "path_conflict"},
                        {"message", "target path exists as a symlink"},
                        {"path", rel_norm}
                    }.dump());
                    return;
                }

                if (!std::filesystem::is_regular_file(st)) {
                    audit_fail("target_not_regular_file", 409, out_abs.string());
                    reply_json(res, 409, json{
                        {"ok", false},
                        {"error", "path_conflict"},
                        {"message", "target path exists and is not a regular file"},
                        {"path", rel_norm}
                    }.dump());
                    return;
                }

                physical_existing_size = pqnas::file_size_u64_safe(out_abs);
                auto ft = std::filesystem::last_write_time(out_abs, ec);
                if (!ec) {
                    physical_existing_mtime = file_time_to_epoch_sec(ft);
                }
            }
        }
    }

        if (!overwrite && (existing_rec.has_value() || physical_exists_at_target)) {
            std::cerr << "[put] early conflict file_exists rel_norm=" << rel_norm << " draining body\n";

            drain_request_body_best_effort();

            json existing = json::object();

            if (existing_rec.has_value()) {
                existing["size_bytes"] = existing_rec->size_bytes;
                existing["mtime_epoch"] = existing_rec->mtime_epoch;
                existing["tier_state"] = existing_rec->tier_state;
                existing["current_pool"] = existing_rec->current_pool;
                existing["physical_path"] = existing_rec->physical_path;
            } else {
                existing["size_bytes"] = physical_existing_size;
                existing["mtime_epoch"] = physical_existing_mtime;
                existing["physical_path"] = out_abs.string();
            }

            audit_fail("file_exists", 409, rel_norm);
            reply_json(res, 409, json{
                {"ok", false},
                {"error", "file_exists"},
                {"message", "file already exists"},
                {"path", rel_norm},
                {"existing", existing}
            }.dump());
            return;
        }

    // Remember old physical path so overwrite to a different pool/path can clean it up later.
    std::string old_physical_path;
    if (existing_rec.has_value()) {
        old_physical_path = existing_rec->physical_path;
    } else if (physical_exists_at_target) {
        old_physical_path = out_abs.string();
    }

    // Ensure parent directory exists, but never through a symlinked parent chain.
    {
        std::string serr;
        if (!ensure_no_symlink_in_existing_path_prefix(out_abs.parent_path(), &serr)) {
            audit_fail("symlink_not_supported", 400, serr);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "symlinks not supported"}
            }.dump());
            return;
        }
    }

    {
        std::error_code ec;
        std::filesystem::create_directories(out_abs.parent_path(), ec);
        if (ec) {
            audit_fail("mkdir_failed", 500, ec.message());
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to create directories"},
                {"detail", pqnas::shorten(ec.message(), 180)}
            }.dump());
            return;
        }
    }

    {
        std::string serr;
        if (!ensure_no_symlink_in_existing_path_prefix(out_abs.parent_path(), &serr)) {
            audit_fail("symlink_not_supported", 400, serr);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "symlinks not supported"}
            }.dump());
            return;
        }
    }

    // Temp write + rename
    const std::filesystem::path tmp =
        out_abs.parent_path() /
        (out_abs.filename().string() + ".upload." + random_b64url(8) + ".tmp");

    {
    std::error_code tmp_ec;
    auto tmp_st = std::filesystem::symlink_status(tmp, tmp_ec);
    if (!tmp_ec && std::filesystem::exists(tmp_st)) {
        if (std::filesystem::is_symlink(tmp_st)) {
            audit_fail("tmp_is_symlink", 409, tmp.string());
            reply_json(res, 409, json{
                {"ok", false},
                {"error", "path_conflict"},
                {"message", "temporary path is invalid"}
            }.dump());
            return;
        }

        if (!std::filesystem::is_regular_file(tmp_st)) {
            audit_fail("tmp_path_conflict", 409, tmp.string());
            reply_json(res, 409, json{
                {"ok", false},
                {"error", "path_conflict"},
                {"message", "temporary path is invalid"}
            }.dump());
            return;
        }

        std::filesystem::remove(tmp, tmp_ec);
        if (tmp_ec) {
            audit_fail("tmp_remove_failed", 500, tmp_ec.message());
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to prepare temporary file"},
                {"detail", pqnas::shorten(tmp_ec.message(), 180)}
            }.dump());
            return;
        }
    }
}

    std::uint64_t bytes_written = 0;
    bool stream_ok = true;
    std::string stream_err;

    try {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f.good()) {
            throw std::runtime_error("open tmp failed");
        }

        content_reader([&](const char* data, size_t len) {
            if (!stream_ok) return false;
            if (len == 0) return true;

            const std::uint64_t chunk = (std::uint64_t)len;
            const std::uint64_t next = bytes_written + chunk;

            if (next < bytes_written) {
                stream_ok = false;
                stream_err = "byte_count_overflow";
                return false;
            }

            if (next > incoming_bytes) {
                stream_ok = false;
                stream_err = "content_length_exceeded";
                return false;
            }

            if (next > transport_max) {
                stream_ok = false;
                stream_err = "transport_limit_exceeded";
                return false;
            }

            f.write(data, (std::streamsize)len);
            if (!f.good()) {
                stream_ok = false;
                stream_err = "write_tmp_failed";
                return false;
            }

            bytes_written = next;
            return true;
        });

        f.flush();
        if (!f.good()) {
            throw std::runtime_error("write tmp failed");
        }
        f.close();

        if (!stream_ok) {
            std::error_code ec;
            std::filesystem::remove(tmp, ec);

            const int http = (stream_err == "transport_limit_exceeded") ? 413 : 400;
            audit_fail(stream_err, http);

            reply_json(res, http, json{
                {"ok", false},
                {"error", (stream_err == "transport_limit_exceeded") ? "transport_limit_exceeded" : "bad_request"},
                {"message", stream_err},
                {"content_length", incoming_bytes},
                {"bytes_written", bytes_written}
            }.dump());
            return;
        }

        if (bytes_written != incoming_bytes) {
            std::error_code ec;
            std::filesystem::remove(tmp, ec);

            audit_fail("content_length_mismatch", 400,
                       "Content-Length=" + std::to_string((unsigned long long)incoming_bytes) +
                       " written=" + std::to_string((unsigned long long)bytes_written));

            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "Content-Length mismatch"},
                {"content_length", incoming_bytes},
                {"bytes_written", bytes_written}
            }.dump());
            return;
        }

        std::cerr << "[put] tmp=" << tmp.string() << "\n";
        std::cerr << "[put] out_abs=" << out_abs.string() << "\n";
        std::cerr << "[put] tmp_exists=" << (std::filesystem::exists(tmp) ? 1 : 0) << "\n";
        std::cerr << "[put] out_parent=" << out_abs.parent_path().string() << "\n";
        std::cerr << "[put] out_parent_exists=" << (std::filesystem::exists(out_abs.parent_path()) ? 1 : 0) << "\n";

        {
            std::string serr;
            if (!ensure_no_symlink_in_existing_path_prefix(out_abs.parent_path(), &serr)) {
                std::error_code rm_ec;
                std::filesystem::remove(tmp, rm_ec);

                audit_fail("symlink_not_supported", 400, serr);
                reply_json(res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "symlinks not supported"}
                }.dump());
                return;
            }
        }

        {
        std::error_code dst_ec;
        auto dst_st = std::filesystem::symlink_status(out_abs, dst_ec);
        if (dst_ec) {
            if (dst_ec != std::make_error_code(std::errc::no_such_file_or_directory)) {
                std::error_code rm_ec;
                std::filesystem::remove(tmp, rm_ec);

                audit_fail("target_exists_check_failed", 500, dst_ec.message());
                reply_json(res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "target existence check failed"},
                    {"detail", pqnas::shorten(dst_ec.message(), 180)}
                }.dump());
                return;
            }
            dst_ec.clear();
        } else if (std::filesystem::exists(dst_st)) {
            if (std::filesystem::is_symlink(dst_st)) {
                std::error_code rm_ec;
                std::filesystem::remove(tmp, rm_ec);

                audit_fail("target_symlink_not_supported", 409, out_abs.string());
                reply_json(res, 409, json{
                    {"ok", false},
                    {"error", "path_conflict"},
                    {"message", "target path exists as a symlink"},
                    {"path", rel_norm}
                }.dump());
                return;
            }

            if (!std::filesystem::is_regular_file(dst_st)) {
                std::error_code rm_ec;
                std::filesystem::remove(tmp, rm_ec);

                audit_fail("target_not_regular_file", 409, out_abs.string());
                reply_json(res, 409, json{
                    {"ok", false},
                    {"error", "path_conflict"},
                    {"message", "target path exists and is not a regular file"},
                    {"path", rel_norm}
                }.dump());
                return;
            }
        }
    }
        if (overwrite && (existing_rec.has_value() || physical_exists_at_target)) {
            const std::filesystem::path live_abs_to_preserve =
                !old_physical_path.empty() ? std::filesystem::path(old_physical_path) : out_abs;

            pqnas::PreserveLiveFileVersionParams vp;
            vp.scope_type = "user";
            vp.scope_id = fp_hex;
            vp.scope_root = user_dir;
            vp.logical_rel_path = rel_norm;
            vp.live_abs_path = live_abs_to_preserve;
            vp.event_kind = "overwrite_preserve";
            vp.actor_fp = fp_hex;
            vp.users = &users;

            pqnas::FileVersionRec vrec;
            std::string verr;
            if (!file_versions_index.preserve_live_file_version(vp, &vrec, &verr)) {
                std::error_code rm_ec;
                std::filesystem::remove(tmp, rm_ec);

                audit_fail("version_preserve_failed", 500, verr);
                reply_json(res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "failed to preserve previous version"},
                    {"detail", pqnas::shorten(verr, 180)}
                }.dump());
                return;
            }
        }
        std::error_code rename_ec;
        std::filesystem::rename(tmp, out_abs, rename_ec);
        if (rename_ec) {
            std::error_code rm_ec;
            std::filesystem::remove(tmp, rm_ec);
            throw std::runtime_error(std::string("rename failed: ") + rename_ec.message());
        }

        {
            const std::int64_t now_ts = now_epoch_sec();

            pqnas::FileLocationRecord rec;
            rec.fp = fp_hex;
            rec.logical_rel_path = rel_norm;
            rec.current_pool = tiering_write ? tier_cfg.landing_pool_id : "";
            rec.physical_path = out_abs.string();
            rec.tier_state = tiering_write ? "landing" : "capacity";
            rec.size_bytes = bytes_written;
            rec.mtime_epoch = now_ts;
            rec.created_epoch = now_ts;
            rec.updated_epoch = now_ts;
            rec.version = 1;

            std::string merr;
            if (!idx->upsert_landing_file(rec, &merr)) {
                std::error_code ec2;
                std::filesystem::remove(out_abs, ec2);
                throw std::runtime_error(std::string("metadata upsert failed: ") + merr);
            }
            {
                std::string gerr;
                if (auto* gidx = pqnas::get_gallery_meta_index()) {
                    if (!gidx->touch_file_facts("user", fp_hex, rel_norm, bytes_written, now_ts, now_ts, &gerr)) {
                        // best-effort only; no hard fail
                    }
                }
            }
            if (tiering_write) {
                pqnas::AuditEvent tev;
                tev.event = "storage.tiering_upload_landed";
                tev.outcome = "ok";
                tev.f["fingerprint"] = fp_hex;
                tev.f["path"] = pqnas::shorten(rel_norm, 200);
                tev.f["tier_state"] = "landing";
                tev.f["pool_id"] = tier_cfg.landing_pool_id;
                tev.f["physical_path"] = pqnas::shorten(out_abs.string(), 220);
                tev.f["bytes"] = std::to_string((unsigned long long)bytes_written);

                tev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

                auto it_cf = req.headers.find("CF-Connecting-IP");
                if (it_cf != req.headers.end())
                    tev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

                auto it_xff = req.headers.find("X-Forwarded-For");
                if (it_xff != req.headers.end())
                    tev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

                tev.f["ua"] = audit_ua();

                audit_append(tev);
            }
        }

        // If overwrite moved the logical file to a different physical path,
        // remove the old physical file after metadata points at the new one.
        if (!old_physical_path.empty()) {
            const std::string new_physical_path = out_abs.string();
            if (old_physical_path != new_physical_path) {
                std::error_code old_rm_ec;
                std::filesystem::remove(old_physical_path, old_rm_ec);
                if (old_rm_ec) {
                    pqnas::AuditEvent ev;
                    ev.event = "v4.files_put_old_physical_cleanup_fail";
                    ev.outcome = "warn";
                    ev.f["fingerprint"] = fp_hex;
                    ev.f["path"] = pqnas::shorten(rel_norm, 200);
                    ev.f["old_physical_path"] = pqnas::shorten(old_physical_path, 220);
                    ev.f["new_physical_path"] = pqnas::shorten(new_physical_path, 220);
                    ev.f["detail"] = pqnas::shorten(old_rm_ec.message(), 180);
                    ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
                    ev.f["ua"] = audit_ua();
                    audit_append(ev);
                }
            }
        }

        audit_ok(rel_path, bytes_written);

        record_user_file_activity_best_effort_local(
            users,
            fp_hex,
            "file.uploaded",
            rel_norm,
            "file",
            "",
            bytes_written,
            1,
            &req
        );

        reply_json(res, 200, json{
            {"ok", true},
            {"fingerprint_hex", fp_hex},
            {"path", rel_path},
            {"bytes", bytes_written},
            {"overwrite", overwrite}
        }.dump());
        return;

    } catch (const std::exception& e) {
        std::error_code ec;
        std::filesystem::remove(tmp, ec);

        audit_fail("write_failed", 500, e.what());
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "upload failed"},
            {"detail", pqnas::shorten(e.what(), 180)}
        }.dump());
        return;
    }
});



    // ---- File Manager file-version archive/blob routes ----
    {
        FileVersionArchiveBlobRoutesContext file_version_archive_blob_ctx;
        file_version_archive_blob_ctx.file_versions = &file_versions_index;
        file_version_archive_blob_ctx.require_user_auth =
            [&](const httplib::Request& req,
                httplib::Response& res,
                std::string* fp_hex,
                std::string* role) {
                return require_user_auth_users_actor(req, res, COOKIE_KEY, &users, fp_hex, role);
            };
        file_version_archive_blob_ctx.user_dir_for_fp =
            [&](const std::string& fp_hex) {
                return user_dir_for_fp(users, fp_hex);
            };

        register_file_version_archive_blob_routes(srv, file_version_archive_blob_ctx);
    }

    // ---- File version read routes ----
    {
        FileVersionReadRoutesContext file_version_read_ctx;
        file_version_read_ctx.file_versions = &file_versions_index;
        file_version_read_ctx.users = &users;
        file_version_read_ctx.require_user_auth =
            [&](const httplib::Request& req,
                httplib::Response& res,
                std::string* fp_hex,
                std::string* role) {
                return require_user_auth_users_actor(req, res, COOKIE_KEY, &users, fp_hex, role);
            };
        file_version_read_ctx.reply_json =
            [&](httplib::Response& res, int status, const std::string& body) {
                reply_json(res, status, body);
            };
        file_version_read_ctx.user_dir_for_fp =
            [&](const std::string& fp_hex) {
                return user_dir_for_fp(users, fp_hex);
            };

        register_file_version_read_routes(srv, file_version_read_ctx);
    }

    // ---- File version manage routes ----
    {
        FileVersionManageRoutesContext file_version_manage_ctx;
        file_version_manage_ctx.file_versions = &file_versions_index;
        file_version_manage_ctx.users = &users;
        file_version_manage_ctx.require_same_origin =
            [&](const httplib::Request& req, httplib::Response& res) {
                return require_same_origin_for_cookie_mutation(req, res);
            };
        file_version_manage_ctx.require_user_auth =
            [&](const httplib::Request& req,
                httplib::Response& res,
                std::string* fp_hex,
                std::string* role) {
                return require_user_auth_users_actor(req, res, COOKIE_KEY, &users, fp_hex, role);
            };
        file_version_manage_ctx.reply_json =
            [&](httplib::Response& res, int status, const std::string& body) {
                reply_json(res, status, body);
            };
        file_version_manage_ctx.user_dir_for_fp =
            [&](const std::string& fp_hex) {
                return user_dir_for_fp(users, fp_hex);
            };

        register_file_version_manage_routes(srv, file_version_manage_ctx);
    }

    // ---- File version restore route ----
    {
        FileVersionRestoreRoutesContext file_version_restore_ctx;
        file_version_restore_ctx.file_versions = &file_versions_index;
        file_version_restore_ctx.users = &users;
        file_version_restore_ctx.require_same_origin =
            [&](const httplib::Request& req, httplib::Response& res) {
                return require_same_origin_for_cookie_mutation(req, res);
            };
        file_version_restore_ctx.require_user_auth =
            [&](const httplib::Request& req,
                httplib::Response& res,
                std::string* fp_hex,
                std::string* role) {
                return require_user_auth_users_actor(req, res, COOKIE_KEY, &users, fp_hex, role);
            };
        file_version_restore_ctx.reply_json =
            [&](httplib::Response& res, int status, const std::string& body) {
                reply_json(res, status, body);
            };
        file_version_restore_ctx.user_dir_for_fp =
            [&](const std::string& fp_hex) {
                return user_dir_for_fp(users, fp_hex);
            };
        file_version_restore_ctx.file_size_u64 =
            [&](const std::filesystem::path& path) -> std::uint64_t {
                return pqnas::file_size_u64_safe(path);
            };
        file_version_restore_ctx.file_mtime_epoch =
            [&](const std::filesystem::path& path) -> std::int64_t {
                return static_cast<std::int64_t>(file_mtime_epoch_safe(path));
            };
        file_version_restore_ctx.touch_gallery_file_facts =
            [&](const std::string& scope_type,
                const std::string& scope_id,
                const std::string& logical_rel_path,
                std::uint64_t size_bytes,
                std::int64_t mtime_epoch,
                std::int64_t now_epoch) {
                if (auto* gidx = pqnas::get_gallery_meta_index()) {
                    std::string gerr;
                    (void)gidx->touch_file_facts(
                        scope_type,
                        scope_id,
                        logical_rel_path,
                        size_bytes,
                        mtime_epoch,
                        now_epoch,
                        &gerr
                    );
                }
            };

        register_file_version_restore_routes(srv, file_version_restore_ctx);
    }


    // ---- Snapshot browse/list/info routes ----
    {
        SnapshotBrowseRoutesContext snapshot_browse_ctx;
        snapshot_browse_ctx.require_admin =
            [&](const httplib::Request& req, httplib::Response& res, std::string* actor_fp) {
                return require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, actor_fp);
            };
        snapshot_browse_ctx.reply_json =
            [&](httplib::Response& res, int status, const std::string& body) {
                reply_json(res, status, body);
            };
        snapshot_browse_ctx.audit_append =
            [&](const pqnas::AuditEvent& ev) {
                audit_append(ev);
            };
        snapshot_browse_ctx.load_volumes =
            [&](std::string* backend,
                std::vector<SnapshotBrowseVolume>* out,
                std::string* err) {
                std::vector<SnapVol> vols;
                if (!load_snapshot_volumes_from_admin_settings(admin_settings_path, backend, &vols, err)) {
                    return false;
                }

                if (out) {
                    out->clear();
                    out->reserve(vols.size());
                    for (const auto& v : vols) {
                        SnapshotBrowseVolume rec;
                        rec.name = v.name;
                        rec.source_subvolume = v.source_subvolume;
                        rec.snap_root = v.snap_root;
                        rec.enabled = v.enabled;
                        out->push_back(rec);
                    }
                }

                return true;
            };
        snapshot_browse_ctx.is_btrfs_subvolume =
            [&](const std::string& path, std::string* detail) {
                return is_btrfs_subvolume_sudo_n(path, detail);
            };
        snapshot_browse_ctx.is_path_under =
            [&](const std::string& path, const std::string& root) {
                return is_path_under(path, root);
            };
        snapshot_browse_ctx.btrfs_subvolume_show =
            [&](const std::string& path, std::string* out, int* rc) {
                // Security: browse probing uses pqnas-restore-root argv execution,
                // not direct btrfs sudo through a shell string.
                const RestoreRootProbeResult r =
                    run_restore_root_probe_argv({"subvolume-show", path});
                if (out) *out = r.output;
                if (rc) *rc = r.exit_code;
            };
        snapshot_browse_ctx.audit_safe_header_value =
            [&](const std::string& value, std::size_t maxlen) {
                return audit_safe_header_value(value, maxlen);
            };

        register_snapshot_browse_routes(srv, snapshot_browse_ctx);
    }

    // ---- Snapshot create routes ----
    {
        SnapshotCreateRoutesContext snapshot_create_ctx;
        snapshot_create_ctx.require_admin =
            [&](const httplib::Request& req, httplib::Response& res, std::string* actor_fp) {
                return require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, actor_fp);
            };
        snapshot_create_ctx.require_same_origin =
            [&](const httplib::Request& req, httplib::Response& res) {
                return require_same_origin_for_cookie_mutation(req, res);
            };
        snapshot_create_ctx.reply_json =
            [&](httplib::Response& res, int status, const std::string& body) {
                reply_json(res, status, body);
            };
        snapshot_create_ctx.audit_append =
            [&](const pqnas::AuditEvent& ev) {
                audit_append(ev);
            };
        snapshot_create_ctx.load_volumes =
            [&](std::string* backend,
                std::vector<SnapshotCreateVolume>* out,
                std::string* err) {
                std::vector<SnapVol> vols;
                if (!load_snapshot_volumes_from_admin_settings(admin_settings_path, backend, &vols, err)) {
                    return false;
                }

                if (out) {
                    out->clear();
                    out->reserve(vols.size());
                    for (const auto& v : vols) {
                        SnapshotCreateVolume rec;
                        rec.name = v.name;
                        rec.source_subvolume = v.source_subvolume;
                        rec.snap_root = v.snap_root;
                        rec.enabled = v.enabled;
                        out->push_back(rec);
                    }
                }

                return true;
            };
        snapshot_create_ctx.is_btrfs_subvolume =
            [&](const std::string& path, std::string* detail) {
                return is_btrfs_subvolume_sudo_n(path, detail);
            };
        snapshot_create_ctx.audit_safe_header_value =
            [&](const std::string& value, std::size_t maxlen) {
                return audit_safe_header_value(value, maxlen);
            };

        register_snapshot_create_routes(srv, snapshot_create_ctx);
    }

    // ---- Snapshot restore routes ----
    {
        SnapshotRestoreRoutesContext snapshot_restore_ctx;
        snapshot_restore_ctx.require_admin =
            [&](const httplib::Request& req, httplib::Response& res, std::string* actor_fp) {
                return require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, actor_fp);
            };
        snapshot_restore_ctx.require_same_origin =
            [&](const httplib::Request& req, httplib::Response& res) {
                return require_same_origin_for_cookie_mutation(req, res);
            };
        snapshot_restore_ctx.reply_json =
            [&](httplib::Response& res, int status, const std::string& body) {
                reply_json(res, status, body);
            };
        snapshot_restore_ctx.audit_append =
            [&](const pqnas::AuditEvent& ev) {
                audit_append(ev);
            };
        snapshot_restore_ctx.load_volumes =
            [&](std::string* backend,
                std::vector<SnapshotRestoreVolume>* out,
                std::string* err) {
                std::vector<SnapVol> vols;
                if (!load_snapshot_volumes_from_admin_settings(admin_settings_path, backend, &vols, err)) {
                    return false;
                }

                if (out) {
                    out->clear();
                    out->reserve(vols.size());
                    for (const auto& v : vols) {
                        SnapshotRestoreVolume rec;
                        rec.name = v.name;
                        rec.source_subvolume = v.source_subvolume;
                        rec.snap_root = v.snap_root;
                        rec.enabled = v.enabled;
                        out->push_back(rec);
                    }
                }

                return true;
            };
        snapshot_restore_ctx.is_path_under =
            [&](const std::string& path, const std::string& root) {
                return is_path_under(path, root);
            };
        snapshot_restore_ctx.is_btrfs_subvolume =
            [&](const std::string& path) {
                return is_btrfs_subvolume_sudo_n(path);
            };
        snapshot_restore_ctx.realpath_str =
            [&](const std::string& path) {
                return realpath_str(path);
            };
        snapshot_restore_ctx.now_iso_utc =
            [&]() {
                return now_iso_utc();
            };
        snapshot_restore_ctx.rand_hex_32 =
            [&]() {
                return rand_hex_32();
            };
        snapshot_restore_ctx.random_b64url =
            [&](std::size_t n) {
                return random_b64url(n);
            };
        snapshot_restore_ctx.popen_capture =
            [&](const std::string& cmd, std::string* out, int* rc) {
                popen_capture(cmd, out, rc);
            };

        register_snapshot_restore_routes(srv, snapshot_restore_ctx);
    }



// Admin routes

    // ---- Admin user profile routes ----
    {
        AdminUserProfileRoutesContext admin_user_profile_ctx;
        admin_user_profile_ctx.users = &users;
        admin_user_profile_ctx.users_path = users_path;
        admin_user_profile_ctx.require_admin_cookie =
            [&](const httplib::Request& req, httplib::Response& res, std::string* actor_fp) {
                return require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, actor_fp);
            };
        admin_user_profile_ctx.require_admin_auth =
            [&](const httplib::Request& req, httplib::Response& res, std::string* actor_fp) {
                return require_admin_auth_users_actor(req, res, COOKIE_KEY, users_path, &users, actor_fp);
            };
        admin_user_profile_ctx.require_same_origin =
            [&](const httplib::Request& req, httplib::Response& res) {
                return require_same_origin_for_cookie_mutation(req, res);
            };
        admin_user_profile_ctx.reply_json =
            [&](httplib::Response& res, int status, const std::string& body) {
                reply_json(res, status, body);
            };
        admin_user_profile_ctx.now_iso_utc =
            [&]() {
                return now_iso_utc();
            };
        admin_user_profile_ctx.audit_append =
            [&](const pqnas::AuditEvent& ev) {
                audit_append(ev);
            };

        register_admin_user_profile_routes(srv, admin_user_profile_ctx);
    }

    // ---- User/avatar routes ----
    {
        UserAvatarRoutesContext user_avatar_ctx;
        user_avatar_ctx.users = &users;
        user_avatar_ctx.workspaces = &workspaces;
        user_avatar_ctx.users_path = users_path;
        user_avatar_ctx.workspaces_path = workspaces_path;
        user_avatar_ctx.require_admin_cookie =
            [&](const httplib::Request& req, httplib::Response& res, std::string* actor_fp) {
                return require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, actor_fp);
            };
        user_avatar_ctx.require_user_cookie =
            [&](const httplib::Request& req, httplib::Response& res, std::string* actor_fp, std::string* actor_role) {
                return require_user_cookie_users_actor(req, res, COOKIE_KEY, &users, actor_fp, actor_role);
            };
        user_avatar_ctx.require_user_auth =
            [&](const httplib::Request& req, httplib::Response& res, std::string* actor_fp, std::string* actor_role) {
                return require_user_auth_users_actor(req, res, COOKIE_KEY, &users, actor_fp, actor_role);
            };
        user_avatar_ctx.require_same_origin =
            [&](const httplib::Request& req, httplib::Response& res) {
                return require_same_origin_for_cookie_mutation(req, res);
            };
        user_avatar_ctx.reply_json =
            [&](httplib::Response& res, int status, const std::string& body) {
                reply_json(res, status, body);
            };
        user_avatar_ctx.b64std_decode_to_bytes =
            [&](const std::string& b64, std::string& out) {
                return b64std_decode_to_bytes(b64, out);
            };
        user_avatar_ctx.is_valid_fingerprint_hex =
            [&](const std::string& fp) {
                return is_valid_fingerprint_hex(fp);
            };
        user_avatar_ctx.audit_append =
            [&](const pqnas::AuditEvent& ev) {
                audit_append(ev);
            };

        register_user_avatar_routes(srv, user_avatar_ctx);
    }

    const std::string PQNAS_SERVER_VERSION = PQNAS_VERSION;

    auto app_version_component = [](const std::string& v, std::size_t& pos) -> long long {
        while (pos < v.size() && v[pos] == '.') ++pos;

        long long n = 0;
        bool any = false;

        while (pos < v.size() && v[pos] >= '0' && v[pos] <= '9') {
            any = true;
            n = (n * 10) + (v[pos] - '0');
            ++pos;
        }

        while (pos < v.size() && v[pos] != '.') ++pos;
        if (pos < v.size() && v[pos] == '.') ++pos;

        return any ? n : 0;
    };

    auto compare_app_versions = [&](const std::string& a, const std::string& b) -> int {
        std::size_t ia = 0;
        std::size_t ib = 0;

        for (int i = 0; i < 4; ++i) {
            const long long av = app_version_component(a, ia);
            const long long bv = app_version_component(b, ib);

            if (av < bv) return -1;
            if (av > bv) return 1;
        }

        return 0;
    };

    auto app_manifest_min_server_version = [](const json& mani) -> std::string {
        try {
            if (mani.is_object() &&
                mani.contains("min_server_version") &&
                mani["min_server_version"].is_string()) {
                return mani["min_server_version"].get<std::string>();
            }
        } catch (...) {
        }

        return "";
    };

    auto app_server_version_ok = [&](const std::string& minServerVersion) -> bool {
        if (minServerVersion.empty()) return true;
        return compare_app_versions(PQNAS_SERVER_VERSION, minServerVersion) >= 0;
    };

    auto app_compatibility_message = [&](const std::string& minServerVersion) -> std::string {
        if (minServerVersion.empty()) return "";
        return std::string("Requires DNA-Nexus Server ") +
               minServerVersion +
               " or newer. Current server is " +
               PQNAS_SERVER_VERSION +
               ".";
    };

    auto apply_app_compatibility_fields = [&](json& item, const json& mani) {
        const std::string minServerVersion = app_manifest_min_server_version(mani);
        const bool ok = app_server_version_ok(minServerVersion);

        item["server_version"] = PQNAS_SERVER_VERSION;
        item["min_server_version"] = minServerVersion;
        item["compatibility_ok"] = ok;
        item["compatibility_state"] = ok ? "ok" : "server_too_old";
        item["compatibility_message"] = ok ? "" : app_compatibility_message(minServerVersion);
    };

    // ---- Apps management routes ----
    {
        AppsManageRoutesContext apps_ctx;
        apps_ctx.users = &users;
        apps_ctx.cookie_key = COOKIE_KEY;
        apps_ctx.apps_installed_dir = APPS_INSTALLED_DIR;
        apps_ctx.apps_bundled_dir = APPS_BUNDLED_DIR;
        apps_ctx.server_version = PQNAS_SERVER_VERSION;

        apps_ctx.require_user_auth =
            [&](const httplib::Request& req,
                httplib::Response& res,
                std::string* fp_hex,
                std::string* role) {
                return require_user_auth_users_actor(req, res, COOKIE_KEY, &users, fp_hex, role);
            };

        apps_ctx.require_admin_cookie =
            [&](const httplib::Request& req, httplib::Response& res) {
                return require_admin_cookie_users(req, res, COOKIE_KEY, std::string{}, &users);
            };

        apps_ctx.is_admin_cookie =
            [&](const httplib::Request& req) {
                return is_admin_cookie_users(req, COOKIE_KEY, &users);
            };

        apps_ctx.safe_app_id =
            [&](const std::string& id) {
                return safe_app_id(id);
            };

        apps_ctx.safe_app_ver =
            [&](const std::string& ver) {
                return safe_app_ver(ver);
            };

        apps_ctx.load_app_launch_policy_json =
            [&]() {
                return load_app_launch_policy_json();
            };

        apps_ctx.save_app_launch_policy_json =
            [&](const json& j) {
                return save_app_launch_policy_json(j);
            };

        apps_ctx.normalize_app_launch_policy_json =
            [&](const json& j) {
                return normalize_app_launch_policy_json(j);
            };

        apps_ctx.normalize_app_launch_policy_entry =
            [&](const json& j) {
                return normalize_app_launch_policy_entry(j);
            };

        apps_ctx.app_launch_policy_defaults_json =
            [&]() {
                return app_launch_policy_defaults_json();
            };

        apps_ctx.read_file_to_string =
            [&](const std::string& path, std::string& out) {
                return read_file_to_string(path, out);
            };

        apps_ctx.file_size_bytes_safe =
            [&](const std::string& path) {
                return file_size_bytes_safe(path);
            };

        apps_ctx.sha256_file =
            [&](const std::filesystem::path& path, std::string* hex, std::string* err) {
                return sha256_file(path, hex, err);
            };

        apps_ctx.apply_app_compatibility_fields =
            [&](json& item, const json& manifest) {
                apply_app_compatibility_fields(item, manifest);
            };

        apps_ctx.rand_hex_16 =
            [&]() {
                return rand_hex_16();
            };

        apps_ctx.run_cmd_capture =
            [&](const std::string& cmd, std::string* out, int* rc) {
                return run_cmd_capture(cmd, out, rc);
            };

        apps_ctx.app_manifest_min_server_version =
            [&](const json& mani) {
                return app_manifest_min_server_version(mani);
            };

        apps_ctx.app_server_version_ok =
            [&](const std::string& min_server_version) {
                return app_server_version_ok(min_server_version);
            };

        apps_ctx.app_compatibility_message =
            [&](const std::string& min_server_version) {
                return app_compatibility_message(min_server_version);
            };

        apps_ctx.app_launch_value_ok =
            [&](const std::string& v) {
                return app_launch_value_ok(v);
            };

        apps_ctx.app_window_profile_ok =
            [&](const std::string& v) {
                return app_window_profile_ok(v);
            };

        apps_ctx.rel_to_repo =
            [&](const std::string& path) {
                return rel_to_repo(path);
            };

        apps_ctx.client_ip =
            [&](const httplib::Request& req) {
                return client_ip(req);
            };

        apps_ctx.now_iso_utc =
            [&]() {
                return now_iso_utc();
            };

        apps_ctx.audit_append =
            [&](const pqnas::AuditEvent& ev) {
                audit_append(ev);
            };

                apps_ctx.require_same_origin = [&](const httplib::Request& req, httplib::Response& res) {
            return require_same_origin_for_cookie_mutation(req, res);
        };

register_apps_manage_routes(srv, apps_ctx);
    }

    // ---- Admin user lifecycle routes ----
    {
        AdminUserLifecycleRoutesContext admin_user_lifecycle_ctx;
        admin_user_lifecycle_ctx.users = &users;
        admin_user_lifecycle_ctx.users_path = users_path;
        admin_user_lifecycle_ctx.require_admin =
            [&](const httplib::Request& req, httplib::Response& res, std::string* actor_fp) {
                return require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, actor_fp);
            };
        admin_user_lifecycle_ctx.require_same_origin =
            [&](const httplib::Request& req, httplib::Response& res) {
                return require_same_origin_for_cookie_mutation(req, res);
            };
        admin_user_lifecycle_ctx.reply_json =
            [&](httplib::Response& res, int status, const std::string& body) {
                reply_json(res, status, body);
            };
        admin_user_lifecycle_ctx.admin_would_remove_last_enabled_admin =
            [&](const std::string& fp) {
                return admin_would_remove_last_enabled_admin(fp);
            };
        admin_user_lifecycle_ctx.revoke_devices_for_fingerprint =
            [&](const std::string& fp) {
                std::string token_revoke_err;
                (void)g_app_tokens.revoke_devices_for_fingerprint(fp, &token_revoke_err);
            };
        admin_user_lifecycle_ctx.now_iso_utc =
            [&]() {
                return now_iso_utc();
            };
        admin_user_lifecycle_ctx.audit_append =
            [&](const pqnas::AuditEvent& ev) {
                audit_append(ev);
            };

        register_admin_user_lifecycle_routes(srv, admin_user_lifecycle_ctx);
    }

    // ---- Auth debug approval routes ----
    {
        AuthDebugApprovalsRoutesContext auth_debug_approvals_ctx;

        auth_debug_approvals_ctx.auth_debug_enabled =
            []() {
                return std::getenv("PQNAS_ENABLE_AUTH_DEBUG") != nullptr;
            };

        auth_debug_approvals_ctx.require_admin_cookie =
            [&](const httplib::Request& req, httplib::Response& res) {
                return require_admin_cookie_users(req, res, COOKIE_KEY, std::string{}, &users);
            };

        auth_debug_approvals_ctx.approvals_count =
            [&]() -> int {
                std::lock_guard<std::mutex> lk(g_approvals_mu);
                return static_cast<int>(g_approvals.size());
            };

        auth_debug_approvals_ctx.reply_json =
            [](httplib::Response& res, int status, const std::string& body) {
                reply_json(res, status, body);
            };

        register_auth_debug_approvals_routes(srv, auth_debug_approvals_ctx);
    }
