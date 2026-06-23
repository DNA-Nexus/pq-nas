srv.Post("/api/v4/files/move", [&](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;

	if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;
    if (!require_same_origin_for_cookie_mutation(req, res)) return;

    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto audit_fail = [&](const std::string& reason, int http, const std::string& detail = "",
                          const std::string& from_rel = "", const std::string& to_rel = "") {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_move_fail";
        ev.outcome = "fail";
        ev.f["fingerprint"] = fp_hex;
        ev.f["reason"] = reason;
        ev.f["http"] = std::to_string(http);
        if (!from_rel.empty()) ev.f["from"] = pqnas::shorten(from_rel, 200);
        if (!to_rel.empty())   ev.f["to"]   = pqnas::shorten(to_rel, 200);
        if (!detail.empty())   ev.f["detail"] = pqnas::shorten(detail, 180);

        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
        audit_append(ev);
    };

    auto audit_ok = [&](const std::string& from_rel,
                        const std::string& to_rel,
                        const std::string& type,
                        std::uint64_t bytes) {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_move_ok";
        ev.outcome = "ok";
        ev.f["fingerprint"] = fp_hex;
        ev.f["from"] = pqnas::shorten(from_rel, 200);
        ev.f["to"]   = pqnas::shorten(to_rel, 200);
        ev.f["type"] = type;
        if (type == "file") ev.f["bytes"] = std::to_string((unsigned long long)bytes);

        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
        audit_append(ev);
    };

    auto audit_warn = [&](const std::string& reason,
                          const std::string& detail,
                          const std::string& from_rel,
                          const std::string& to_rel) {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_move_warn";
        ev.outcome = "warn";
        ev.f["fingerprint"] = fp_hex;
        ev.f["reason"] = reason;
        ev.f["from"] = pqnas::shorten(from_rel, 200);
        ev.f["to"]   = pqnas::shorten(to_rel, 200);
        if (!detail.empty()) ev.f["detail"] = pqnas::shorten(detail, 180);
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
        ev.f["ua"] = audit_ua();
        audit_append(ev);
    };

    auto uopt = users.get(fp_hex);
    if (!uopt.has_value() || uopt->storage_state != "allocated") {
        audit_fail("storage_unallocated", 403);
        reply_json(res, 403, json{
            {"ok", false},
            {"error", "storage_unallocated"},
            {"message", "Storage not allocated"}
        }.dump());
        return;
    }

    std::string from_rel, to_rel;
    if (req.has_param("from")) from_rel = req.get_param_value("from");
    if (req.has_param("to"))   to_rel   = req.get_param_value("to");

    if (from_rel.empty() || to_rel.empty()) {
        audit_fail("missing_from_or_to", 400);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "missing from or to"}
        }.dump());
        return;
    }

    std::string from_rel_norm, to_rel_norm;
    std::string nerr1, nerr2;

    if (!pqnas::normalize_user_rel_path_strict(from_rel, &from_rel_norm, &nerr1)) {
        audit_fail("invalid_from_path", 400, nerr1, from_rel, to_rel);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid from path"}
        }.dump());
        return;
    }

    if (!pqnas::normalize_user_rel_path_strict(to_rel, &to_rel_norm, &nerr2)) {
        audit_fail("invalid_to_path", 400, nerr2, from_rel, to_rel);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid to path"}
        }.dump());
        return;
    }

    // Serialize writes on overlapping logical paths for this user.
    // This prevents PUT/MOVE/DELETE races on the same file/subtree.
    auto* plm = pqnas::get_path_lock_manager();
    auto write_guard = plm->lock_paths(fp_hex, {from_rel_norm, to_rel_norm});

    // Refuse moves into a destination whose ancestor is already a file.
    // Example: if "a" is a file, moving anything to "a/b.txt" must fail.
    {
        std::string found_ancestor;
        std::string aerr;
        const bool conflict =
            pqnas::any_file_ancestor_exists(users, fp_hex, to_rel_norm, &found_ancestor, &aerr);

        if (!aerr.empty()) {
            audit_fail("ancestor_check_failed", 500, aerr, from_rel, to_rel);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "ancestor conflict check failed"},
                {"detail", pqnas::shorten(aerr, 180)}
            }.dump());
            return;
        }

        if (conflict) {
            audit_fail("dest_ancestor_is_file", 409, found_ancestor, from_rel, to_rel);
            reply_json(res, 409, json{
                {"ok", false},
                {"error", "path_conflict"},
                {"message", "destination parent path is an existing file"},
                {"ancestor", found_ancestor}
            }.dump());
            return;
        }
    }
    if (from_rel_norm == to_rel_norm) {
        audit_fail("same_path", 400, "", from_rel, to_rel);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "from and to are the same"}
        }.dump());
        return;
    }

    // Resolve logical source (file or dir)
    pqnas::ResolvedLogicalItem src;
    std::string rerr;
    if (!pqnas::resolve_existing_user_item(users, fp_hex, from_rel_norm, &src, &rerr) || !src.exists) {
        audit_fail("not_found", 404, rerr, from_rel, to_rel);
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "source not found"}
        }.dump());
        return;
    }

    // Destination must not already exist logically
    {
        pqnas::ResolvedLogicalItem dst_existing;
        std::string derr;
        if (pqnas::resolve_existing_user_item(users, fp_hex, to_rel_norm, &dst_existing, &derr) &&
            dst_existing.exists) {
            audit_fail("dest_exists", 409, "", from_rel, to_rel);
            reply_json(res, 409, json{
                {"ok", false},
                {"error", "dest_exists"},
                {"message", "destination already exists"}
            }.dump());
            return;
        }
    }

    // Prevent moving directory into itself/subtree
    if (src.is_dir) {
        if (to_rel_norm == from_rel_norm ||
            to_rel_norm.rfind(from_rel_norm + "/", 0) == 0) {
            audit_fail("dir_into_self", 400, "", from_rel, to_rel);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "cannot move a directory into itself"}
            }.dump());
            return;
        }
    }
    // File locks are path-based, so move/rename must be blocked before the
    // payload changes path. Otherwise a locked item can be renamed and the lock
    // becomes detached from the real file.
    {
        const std::filesystem::path locks_db_path =
            std::filesystem::path(users_path).parent_path() / "file_locks.sqlite3";

        pqnas::FileLocksStore lock_store(locks_db_path);
        std::string lock_err;

        const auto now = static_cast<std::int64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count()
        );

        (void)lock_store.delete_expired(now, nullptr);

        auto src_lock = lock_store.find_live_conflict(
            "user",
            fp_hex,
            from_rel_norm,
            now,
            &lock_err
        );

        if (!lock_err.empty()) {
            audit_fail("source_lock_check_failed", 500, lock_err, from_rel_norm, to_rel_norm);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to check file lock"}
            }.dump());
            return;
        }

        if (src_lock.has_value()) {
            audit_fail("source_locked", 409, "", from_rel_norm, to_rel_norm);
            reply_json(res, 409, json{
                {"ok", false},
                {"error", "locked"},
                {"message", "move source blocked because this item is locked"},
                {"lock", json{
                    {"scope_type", src_lock->scope_type},
                    {"scope_id", src_lock->scope_id},
                    {"logical_rel_path", src_lock->logical_rel_path},
                    {"item_kind", src_lock->item_kind},
                    {"locked_by_fp_short", user_file_lock_fp_short_local(src_lock->locked_by_fp)},
                    {"note", src_lock->note},
                    {"expires_at_epoch", src_lock->expires_at_epoch}
                }}
            }.dump());
            return;
        }

        lock_err.clear();

        auto dst_lock = lock_store.find_live_conflict(
            "user",
            fp_hex,
            to_rel_norm,
            now,
            &lock_err
        );

        if (!lock_err.empty()) {
            audit_fail("destination_lock_check_failed", 500, lock_err, from_rel_norm, to_rel_norm);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to check destination file lock"}
            }.dump());
            return;
        }

        if (dst_lock.has_value()) {
            audit_fail("destination_locked", 409, "", from_rel_norm, to_rel_norm);
            reply_json(res, 409, json{
                {"ok", false},
                {"error", "locked"},
                {"message", "move destination blocked because this item is locked"},
                {"lock", json{
                    {"scope_type", dst_lock->scope_type},
                    {"scope_id", dst_lock->scope_id},
                    {"logical_rel_path", dst_lock->logical_rel_path},
                    {"item_kind", dst_lock->item_kind},
                    {"locked_by_fp_short", user_file_lock_fp_short_local(dst_lock->locked_by_fp)},
                    {"note", dst_lock->note},
                    {"expires_at_epoch", dst_lock->expires_at_epoch}
                }}
            }.dump());
            return;
        }
    }
    const std::filesystem::path user_dir = user_dir_for_fp(users, fp_hex);
    auto* idx = pqnas::get_file_location_index();
    if (!idx) {
        audit_fail("metadata_index_missing", 500, "", from_rel, to_rel);
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "metadata index missing"}
        }.dump());
        return;
    }

    auto move_one_path = [&](const std::filesystem::path& src_path,
                             const std::filesystem::path& dst_path,
                             bool src_is_dir,
                             std::string* move_err) -> bool {
        if (move_err) move_err->clear();

        std::error_code mec;

        {
            std::string serr;
            if (!ensure_no_symlink_in_existing_path_prefix(dst_path.parent_path(), &serr)) {
                if (move_err) *move_err = "destination parent invalid: " + serr;
                return false;
            }
        }

        std::filesystem::create_directories(dst_path.parent_path(), mec);
        if (mec) {
            if (move_err) *move_err = "mkdir failed: " + mec.message();
            return false;
        }

        {
            std::string serr;
            if (!ensure_no_symlink_in_existing_path_prefix(dst_path.parent_path(), &serr)) {
                if (move_err) *move_err = "destination parent invalid: " + serr;
                return false;
            }
        }

        mec.clear();
        auto dst_st = std::filesystem::symlink_status(dst_path, mec);
        if (!mec && std::filesystem::exists(dst_st)) {
            if (std::filesystem::is_symlink(dst_st)) {
                if (move_err) *move_err = "destination exists as symlink";
                return false;
            }
            if (move_err) *move_err = "destination appeared before move";
            return false;
        }
        if (mec && mec != std::make_error_code(std::errc::no_such_file_or_directory)) {
            if (move_err) *move_err = "destination check failed: " + mec.message();
            return false;
        }

        mec.clear();
        std::filesystem::rename(src_path, dst_path, mec);
        // PQNAS_VERSION_HISTORY_MOVE_AFTER_RENAME_V1
        // Best-effort: keep My Files version history attached after rename/move.
        // Version rows are path-keyed, so the filesystem rename alone would leave
        // history under the old path until the file is renamed back.
        if (!mec) {
            std::error_code pqnas_versions_type_ec;
            const bool pqnas_versions_recursive =
                std::filesystem::is_directory(dst_path, pqnas_versions_type_ec);
            pqnas::FileVersionsMoveResult pqnas_versions_move_result;
            std::string pqnas_versions_move_err;
            (void)file_versions_index.move_versions_for_scope_path(
                "user",
                fp_hex,
                from_rel_norm,
                to_rel_norm,
                pqnas_versions_recursive,
                &pqnas_versions_move_result,
                &pqnas_versions_move_err);
        }


        if (!mec) return true;

        const bool is_exdev =
            (mec == std::make_error_code(std::errc::cross_device_link));

        if (!is_exdev) {
            if (move_err) *move_err = "rename failed: " + mec.message();
            return false;
        }

        if (src_is_dir) {
            std::error_code sec;
            for (std::filesystem::recursive_directory_iterator it(
                     src_path,
                     std::filesystem::directory_options::skip_permission_denied,
                     sec), end;
                 it != end;
                 it.increment(sec)) {

                if (sec) {
                    if (move_err) *move_err = "symlink scan failed: " + sec.message();
                    return false;
                }

                std::error_code ec2;
                auto st2 = std::filesystem::symlink_status(it->path(), ec2);
                if (ec2) {
                    if (move_err) *move_err = "symlink scan failed: " + ec2.message();
                    return false;
                }

                if (std::filesystem::is_symlink(st2)) {
                    if (move_err) *move_err =
                        "copy fallback refused: symlink in source tree: " + it->path().string();
                    return false;
                }
            }
        }

        std::error_code ec_copy;
        if (src_is_dir) {
            std::filesystem::copy(
                src_path,
                dst_path,
                std::filesystem::copy_options::recursive |
                std::filesystem::copy_options::skip_symlinks,
                ec_copy
            );
        } else {
            std::filesystem::copy_file(
                src_path,
                dst_path,
                std::filesystem::copy_options::none,
                ec_copy
            );
        }
        if (ec_copy) {
            if (move_err) *move_err = "copy fallback failed: " + ec_copy.message();
            return false;
        }

        std::error_code ec_rm;
        if (src_is_dir) std::filesystem::remove_all(src_path, ec_rm);
        else            std::filesystem::remove(src_path, ec_rm);

        if (ec_rm) {
            if (move_err) *move_err = "cleanup failed: " + ec_rm.message();
            return false;
        }

        return true;
    };

    if (src.is_file) {
        // ----- FILE MOVE -----
        if (!src.has_physical_anchor) {
            audit_fail("file_missing_physical_anchor", 500, "", from_rel, to_rel);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "file missing physical anchor"}
            }.dump());
            return;
        }

        std::string gerr;
        auto rec = idx->get(fp_hex, from_rel_norm, &gerr);
        if (!rec.has_value()) {
            audit_fail("metadata_source_missing", 500, gerr, from_rel, to_rel);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "source metadata missing"},
                {"detail", pqnas::shorten(gerr, 180)}
            }.dump());
            return;
        }

        const std::filesystem::path from_abs = src.abs_path;
        const std::filesystem::path old_phys = std::filesystem::path(rec->physical_path);
        const std::filesystem::path old_logical_path = std::filesystem::path(from_rel_norm);

        std::filesystem::path physical_root = old_phys;
        for (const auto& _ : old_logical_path) {
            (void)_;
            physical_root = physical_root.parent_path();
        }

        const std::filesystem::path to_abs = physical_root / std::filesystem::path(to_rel_norm);

        {
            std::string serr;
            if (!ensure_no_symlink_in_existing_path_prefix(from_abs, &serr)) {
                audit_fail("symlink_not_supported", 400, serr, from_rel, to_rel);
                reply_json(res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "symlinks not supported"}
                }.dump());
                return;
            }
        }

        {
            std::string serr;
            if (!ensure_no_symlink_in_existing_path_prefix(to_abs.parent_path(), &serr)) {
                audit_fail("symlink_not_supported", 400, serr, from_rel, to_rel);
                reply_json(res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "symlinks not supported"}
                }.dump());
                return;
            }
        }

        std::error_code ec;
        auto st = std::filesystem::symlink_status(from_abs, ec);
        if (ec || !std::filesystem::exists(st)) {
            audit_fail("not_found", 404, "", from_rel, to_rel);
            reply_json(res, 404, json{
                {"ok", false},
                {"error", "not_found"},
                {"message", "source not found"}
            }.dump());
            return;
        }

        if (std::filesystem::is_symlink(st)) {
            audit_fail("symlink_not_supported", 400, "", from_rel, to_rel);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "symlinks not supported"}
            }.dump());
            return;
        }

        if (!std::filesystem::is_regular_file(st)) {
            audit_fail("src_not_file", 400, "", from_rel, to_rel);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "source must be a file"}
            }.dump());
            return;
        }

        {
            std::error_code dec;
            auto dst_st = std::filesystem::symlink_status(to_abs, dec);
            if (!dec && std::filesystem::exists(dst_st)) {
                audit_fail("dest_exists", 409, "", from_rel, to_rel);
                reply_json(res, 409, json{
                    {"ok", false},
                    {"error", "dest_exists"},
                    {"message", "destination already exists"}
                }.dump());
                return;
            }
        }

        const std::uint64_t bytes = pqnas::file_size_u64_safe(from_abs);


        std::string move_err;
        if (!move_one_path(from_abs, to_abs, false, &move_err)) {
            audit_fail("move_failed", 500, move_err, from_rel, to_rel);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "move failed"},
                {"detail", pqnas::shorten(move_err, 180)}
            }.dump());
            return;
        }

        const std::int64_t now_ts = now_epoch_sec();
        std::string merr;
        if (!idx->rename_one(fp_hex,
                             from_rel_norm,
                             to_rel_norm,
                             from_abs.string(),
                             to_abs.string(),
                             now_ts,
                             &merr)) {
            audit_fail("metadata_rename_failed", 500, merr, from_rel, to_rel);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "move succeeded but metadata rename failed"},
                {"detail", pqnas::shorten(merr, 180)}
            }.dump());
            return;
        }

        {
            std::string ferr;
            if (!pqnas::favorites_move_path(user_dir, from_rel_norm, to_rel_norm, "file", &ferr)) {
                audit_warn("favorites_move_failed", ferr, from_rel, to_rel);
            }
        }
        {
            std::string gerr;
            if (auto* gidx = pqnas::get_gallery_meta_index()) {
                if (!gidx->rename_one("user", fp_hex, from_rel_norm, to_rel_norm, now_ts, &gerr)) {
                    audit_warn("gallery_meta_move_failed", gerr, from_rel, to_rel);
                }
            }
            {
                std::string aerr;
                if (!gallery_albums_index.rename_one("user", fp_hex, from_rel_norm, to_rel_norm, now_ts, &aerr)) {
                    audit_warn("gallery_albums_move_failed", aerr, from_rel, to_rel);
                }
            }
        }
        {
            std::string rerr;
            if (!reelstack_meta_rename_one_path_local("user", fp_hex, from_rel_norm, to_rel_norm, now_ts, &rerr)) {
                audit_warn("reelstack_meta_move_failed", rerr, from_rel, to_rel);
            }
        }
        audit_ok(from_rel, to_rel, "file", bytes);
        record_user_file_moved_activity_best_effort_local(
            users,
            fp_hex,
            from_rel_norm,
            to_rel_norm,
            "file",
            bytes,
            &req
        );
        reply_json(res, 200, json{
            {"ok", true},
            {"from", from_rel},
            {"to", to_rel},
            {"type", "file"},
            {"bytes", bytes}
        }.dump());
        return;
    }

    if (src.is_dir) {
        // ----- DIRECTORY MOVE -----
        std::string lerr;
        auto subtree_rows = idx->list_subtree_records(fp_hex, from_rel_norm, &lerr);
        if (!lerr.empty()) {
            audit_fail("metadata_subtree_list_failed", 500, lerr, from_rel, to_rel);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to list metadata subtree"},
                {"detail", pqnas::shorten(lerr, 180)}
            }.dump());
            return;
        }

        // Metadata-backed logical dir must have subtree rows.
        if (subtree_rows.empty()) {
            // Legacy physical dir fallback path
            if (!src.has_physical_anchor) {
                audit_fail("not_found", 404, "", from_rel, to_rel);
                reply_json(res, 404, json{
                    {"ok", false},
                    {"error", "not_found"},
                    {"message", "source not found"}
                }.dump());
                return;
            }
        }

        // For now, require one physical root tree for all metadata-backed descendants.
        std::filesystem::path from_abs;
        bool have_root = false;

        // For metadata-backed dirs, metadata physical paths are authoritative.
        // Do NOT prefer legacy physical anchor here.
        if (!subtree_rows.empty()) {
            const auto& first = subtree_rows.front();
            std::filesystem::path root = std::filesystem::path(first.physical_path);
            for (const auto& _ : std::filesystem::path(first.logical_rel_path)) {
                (void)_;
                root = root.parent_path();
            }
            from_abs = root / std::filesystem::path(from_rel_norm);
            have_root = true;
        } else if (src.has_physical_anchor) {
            from_abs = src.abs_path;
            have_root = true;
        }

        if (!have_root) {
            audit_fail("dir_missing_physical_anchor", 409, "", from_rel, to_rel);
            reply_json(res, 409, json{
                {"ok", false},
                {"error", "unsupported"},
                {"message", "directory move not supported for this layout"}
            }.dump());
            return;
        }

        bool all_under_root = true;
        const std::string from_root_s = from_abs.lexically_normal().string();
        for (const auto& rec : subtree_rows) {
            const std::string phys = std::filesystem::path(rec.physical_path).lexically_normal().string();
            if (phys != from_root_s && phys.rfind(from_root_s + "/", 0) != 0) {
                all_under_root = false;
                break;
            }
        }

        if (!all_under_root) {
            audit_fail("dir_move_cross_root_unsupported", 409, "", from_rel, to_rel);
            reply_json(res, 409, json{
                {"ok", false},
                {"error", "unsupported"},
                {"message", "directory move across mixed storage roots not supported yet"}
            }.dump());
            return;
        }

        // Destination physical path under same root
        std::filesystem::path physical_root = from_abs;
        for (const auto& _ : std::filesystem::path(from_rel_norm)) {
            (void)_;
            physical_root = physical_root.parent_path();
        }
        const std::filesystem::path to_abs = physical_root / std::filesystem::path(to_rel_norm);

        {
            std::string serr;
            if (!ensure_no_symlink_in_existing_path_prefix(from_abs, &serr)) {
                audit_fail("symlink_not_supported", 400, serr, from_rel, to_rel);
                reply_json(res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "symlinks not supported"}
                }.dump());
                return;
            }
        }

        {
            std::string serr;
            if (!ensure_no_symlink_in_existing_path_prefix(to_abs.parent_path(), &serr)) {
                audit_fail("symlink_not_supported", 400, serr, from_rel, to_rel);
                reply_json(res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "symlinks not supported"}
                }.dump());
                return;
            }
        }

        {
            std::error_code dec;
            auto dst_st = std::filesystem::symlink_status(to_abs, dec);
            if (!dec && std::filesystem::exists(dst_st)) {
                audit_fail("dest_exists", 409, "", from_rel, to_rel);
                reply_json(res, 409, json{
                    {"ok", false},
                    {"error", "dest_exists"},
                    {"message", "destination already exists"}
                }.dump());
                return;
            }
        }

        {
            std::error_code sec;
            auto src_st = std::filesystem::symlink_status(from_abs, sec);
            if (sec || !std::filesystem::exists(src_st)) {
                audit_fail("not_found", 404, "", from_rel, to_rel);
                reply_json(res, 404, json{
                    {"ok", false},
                    {"error", "not_found"},
                    {"message", "source not found"}
                }.dump());
                return;
            }

            if (std::filesystem::is_symlink(src_st)) {
                audit_fail("symlink_not_supported", 400, "", from_rel, to_rel);
                reply_json(res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "symlinks not supported"}
                }.dump());
                return;
            }

            if (!std::filesystem::is_directory(src_st)) {
                audit_fail("src_not_dir", 400, "", from_rel, to_rel);
                reply_json(res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "source must be a directory"}
                }.dump());
                return;
            }
        }

        std::string move_err;
        if (!move_one_path(from_abs, to_abs, true, &move_err)) {
            audit_fail("move_failed", 500, move_err, from_rel, to_rel);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "move failed"},
                {"detail", pqnas::shorten(move_err, 180)}
            }.dump());
            return;
        }

        const std::int64_t now_ts = now_epoch_sec();
        std::string merr;
        if (!idx->rename_subtree(fp_hex,
                                 from_rel_norm,
                                 to_rel_norm,
                                 now_ts,
                                 &merr)) {
            audit_fail("metadata_subtree_rename_failed", 500, merr, from_rel, to_rel);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "move succeeded but metadata subtree rename failed"},
                {"detail", pqnas::shorten(merr, 180)}
            }.dump());
            return;
        }

        {
            std::string ferr;
            if (!pqnas::favorites_move_path(user_dir, from_rel_norm, to_rel_norm, "dir", &ferr)) {
                audit_warn("favorites_move_failed", ferr, from_rel, to_rel);
            }
        }
        {
            std::string gerr;
            if (auto* gidx = pqnas::get_gallery_meta_index()) {
                if (!gidx->rename_subtree("user", fp_hex, from_rel_norm, to_rel_norm, now_ts, &gerr)) {
                    audit_warn("gallery_meta_move_failed", gerr, from_rel, to_rel);
                }
            }
            {
                std::string aerr;
                if (!gallery_albums_index.rename_subtree("user", fp_hex, from_rel_norm, to_rel_norm, now_ts, &aerr)) {
                    audit_warn("gallery_albums_move_failed", aerr, from_rel, to_rel);
                }
            }
        }
        {
            std::string rerr;
            if (!reelstack_meta_rename_subtree_path_local("user", fp_hex, from_rel_norm, to_rel_norm, now_ts, &rerr)) {
                audit_warn("reelstack_meta_move_failed", rerr, from_rel, to_rel);
            }
        }
        audit_ok(from_rel, to_rel, "dir", 0);
        record_user_file_moved_activity_best_effort_local(
            users,
            fp_hex,
            from_rel_norm,
            to_rel_norm,
            "folder",
            0,
            &req
        );
        reply_json(res, 200, json{
            {"ok", true},
            {"from", from_rel},
            {"to", to_rel},
            {"type", "dir"},
            {"bytes", 0}
        }.dump());
        return;
    }

    audit_fail("unsupported_type", 400, "", from_rel, to_rel);
    reply_json(res, 400, json{
        {"ok", false},
        {"error", "bad_request"},
        {"message", "unsupported source type"}
    }.dump());
});
// ---- Files API (user storage) ----
// POST /api/v4/files/mkdir?path=relative/dir
srv.Post("/api/v4/files/mkdir", [&](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;

	if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;
    if (!require_same_origin_for_cookie_mutation(req, res)) return;
    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto audit_fail = [&](const std::string& reason, int http, const std::string& detail = "") {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_mkdir_fail";
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

    auto audit_ok = [&](const std::string& rel_path) {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_mkdir_ok";
        ev.outcome = "ok";
        ev.f["fingerprint"] = fp_hex;
        ev.f["path"] = pqnas::shorten(rel_path, 200);

        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
        audit_append(ev);
    };

    // ---- path param ----
    std::string rel_path;
    if (req.has_param("path"))
        rel_path = req.get_param_value("path");

    if (rel_path.empty()) {
        audit_fail("missing_path", 400);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "missing path"}
        }.dump());
        return;
    }

    const std::filesystem::path user_dir = user_dir_for_fp(users, fp_hex);

    auto ensure_no_symlink_dir_chain = [&](const std::filesystem::path& base,
                                       const std::filesystem::path& dir,
                                       bool require_exists,
                                       std::string* out_err) -> bool {
        if (out_err) out_err->clear();

        std::filesystem::path cur = base.lexically_normal();
        const std::filesystem::path target = dir.lexically_normal();

        std::error_code ec;
        auto base_st = std::filesystem::symlink_status(cur, ec);
        if (ec) {
            if (out_err) *out_err = "base symlink_status failed: " + ec.message();
            return false;
        }
        if (!std::filesystem::exists(base_st)) {
            if (out_err) *out_err = "base directory missing";
            return false;
        }
        if (std::filesystem::is_symlink(base_st)) {
            if (out_err) *out_err = "base directory is symlink";
            return false;
        }
        if (!std::filesystem::is_directory(base_st)) {
            if (out_err) *out_err = "base path is not a directory";
            return false;
        }

        const auto rel = target.lexically_relative(cur);
        if (rel.empty() || rel == ".") return true;

        for (const auto& part : rel) {
            if (part.empty()) continue;
            cur /= part;

            ec.clear();
            auto st = std::filesystem::symlink_status(cur, ec);
            if (ec) {
                if (require_exists) {
                    if (out_err) *out_err = "symlink_status failed: " + ec.message();
                    return false;
                }
                continue;
            }

            if (!std::filesystem::exists(st)) {
                if (require_exists) {
                    if (out_err) *out_err = "directory missing";
                    return false;
                }
                continue;
            }

            if (std::filesystem::is_symlink(st)) {
                if (out_err) *out_err = "directory chain contains symlink";
                return false;
            }

            if (!std::filesystem::is_directory(st)) {
                if (out_err) *out_err = "path component is not a directory";
                return false;
            }
        }

        return true;
    };
    // reuse strict resolver
    std::filesystem::path abs;
    std::string path_err;
    if (!pqnas::resolve_user_path_strict(user_dir, rel_path, &abs, &path_err)) {
        audit_fail("invalid_path", 400, path_err);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid path"}
        }.dump());
        return;
    }

    // must have allocated storage
    auto uopt = users.get(fp_hex);
    if (!uopt.has_value() || uopt->storage_state != "allocated") {
        audit_fail("storage_unallocated", 403);
        reply_json(res, 403, json{
            {"ok", false},
            {"error", "storage_unallocated"},
            {"message", "Storage not allocated"}
        }.dump());
        return;
    }

    std::error_code ec;

    {
        std::string derr;
        if (!ensure_no_symlink_dir_chain(user_dir, abs, false, &derr)) {
            audit_fail("path_conflict", 409, derr);
            reply_json(res, 409, json{
                {"ok", false},
                {"error", "path_conflict"},
                {"message", "directory path is invalid"},
                {"detail", pqnas::shorten(derr, 180)}
            }.dump());
            return;
        }
    }

    const bool created_dir = std::filesystem::create_directories(abs, ec);
    if (ec) {
        audit_fail("mkdir_failed", 500, ec.message());
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "failed to create directory"},
            {"detail", pqnas::shorten(ec.message(), 180)}
        }.dump());
        return;
    }

    {
        std::string derr;
        if (!ensure_no_symlink_dir_chain(user_dir, abs, true, &derr)) {
            audit_fail("path_conflict", 409, derr);
            reply_json(res, 409, json{
                {"ok", false},
                {"error", "path_conflict"},
                {"message", "directory path is invalid"},
                {"detail", pqnas::shorten(derr, 180)}
            }.dump());
            return;
        }
    }

    audit_ok(rel_path);

    if (created_dir) {
        record_user_file_activity_best_effort_local(
            users,
            fp_hex,
            "folder.created",
            rel_path,
            "folder",
            "",
            0,
            1,
            &req
        );
    }

    reply_json(res, 200, json{
        {"ok", true},
        {"path", rel_path}
    }.dump());
});

    // POST /api/v4/files/hash?path=rel/path&algo=sha256
srv.Post("/api/v4/files/hash", [&](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;

    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto add_ip_headers = [&](pqnas::AuditEvent& ev) {
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
    };

    auto audit_fail = [&](const std::string& reason, int http, const std::string& detail = "",
                          const std::string& path_rel = "", const std::string& algo = "") {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_hash_fail";
        ev.outcome = "fail";
        ev.f["fingerprint"] = fp_hex;
        ev.f["reason"] = reason;
        ev.f["http"] = std::to_string(http);
        if (!path_rel.empty()) ev.f["path"] = pqnas::shorten(path_rel, 200);
        if (!algo.empty())     ev.f["algo"] = pqnas::shorten(algo, 40);
        if (!detail.empty())   ev.f["detail"] = pqnas::shorten(detail, 180);

        add_ip_headers(ev);
        audit_append(ev);
    };

    auto audit_ok = [&](const std::string& path_rel,
                        const std::string& algo,
                        std::uint64_t bytes,
                        const std::string& digest_hex) {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_hash_ok";
        ev.outcome = "ok";
        ev.f["fingerprint"] = fp_hex;
        ev.f["path"] = pqnas::shorten(path_rel, 200);
        ev.f["algo"] = pqnas::shorten(algo, 40);
        ev.f["bytes"] = std::to_string((unsigned long long)bytes);
        // Don’t store full digest if you want shorter logs; but full is often fine.
        ev.f["digest"] = pqnas::shorten(digest_hex, 80);

        add_ip_headers(ev);
        audit_append(ev);
    };

    // must have allocated storage
    auto uopt = users.get(fp_hex);
    if (!uopt.has_value() || uopt->storage_state != "allocated") {
        audit_fail("storage_unallocated", 403);
        reply_json(res, 403, json{
            {"ok", false},
            {"error", "storage_unallocated"},
            {"message", "Storage not allocated"}
        }.dump());
        return;
    }

    std::string path_rel;
    if (req.has_param("path")) path_rel = req.get_param_value("path");
    if (path_rel.empty()) {
        audit_fail("missing_path", 400);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "missing path"}
        }.dump());
        return;
    }

    std::string algo = "sha256";
    if (req.has_param("algo")) algo = req.get_param_value("algo");

    // v1: only sha256 supported
    if (algo != "sha256") {
        audit_fail("unsupported_algo", 400, "", path_rel, algo);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "unsupported algo (use sha256)"}
        }.dump());
        return;
    }

    pqnas::ResolvedExistingPath resolved;
    std::string err;
    if (!pqnas::resolve_existing_user_file_path(users, fp_hex, path_rel, &resolved, &err)) {
        if (err == "empty path" || err == "invalid path" || err == "absolute path not allowed" || err == "reserved path") {
            audit_fail("invalid_path", 400, err, path_rel, algo);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "invalid path"}
            }.dump());
            return;
        }

        audit_fail("not_found", 404, err, path_rel, algo);
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "file not found"}
        }.dump());
        return;
    }

    const std::filesystem::path path_abs = resolved.abs_path;

    std::error_code ec;
    auto st = std::filesystem::symlink_status(path_abs, ec);
    if (ec || !std::filesystem::exists(st)) {
        audit_fail("not_found", 404, "", path_rel, algo);
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "file not found"}
        }.dump());
        return;
    }

    if (std::filesystem::is_symlink(st)) {
        audit_fail("symlink_not_supported", 400, "", path_rel, algo);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "symlinks not supported"}
        }.dump());
        return;
    }

    if (!std::filesystem::is_regular_file(st)) {
        audit_fail("not_found", 404, "", path_rel, algo);
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "file not found"}
        }.dump());
        return;
    }

    const std::uint64_t bytes = pqnas::file_size_u64_safe(path_abs);

    std::string digest_hex, herr;
    if (!sha256_file(path_abs, &digest_hex, &herr)) {
        audit_fail("hash_failed", 500, herr, path_rel, algo);
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "hash failed"},
            {"detail", pqnas::shorten(herr, 180)}
        }.dump());
        return;
    }

    audit_ok(path_rel, algo, bytes, digest_hex);

    reply_json(res, 200, json{
        {"ok", true},
        {"path", path_rel},
        {"algo", algo},
        {"bytes", bytes},
        {"digest_hex", digest_hex}
    }.dump());
});

// POST /api/v4/files/rmdir?path=rel/dir
srv.Post("/api/v4/files/rmdir", [&](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;
    if (!require_same_origin_for_cookie_mutation(req, res)) return;
    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto add_ip_headers = [&](pqnas::AuditEvent& ev) {
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
    };

    auto audit_fail = [&](const std::string& reason, int http, const std::string& detail = "",
                          const std::string& path_rel = "") {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_rmdir_fail";
        ev.outcome = "fail";
        ev.f["fingerprint"] = fp_hex;
        ev.f["reason"] = reason;
        ev.f["http"] = std::to_string(http);
        if (!path_rel.empty()) ev.f["path"] = pqnas::shorten(path_rel, 200);
        if (!detail.empty())   ev.f["detail"] = pqnas::shorten(detail, 180);

        add_ip_headers(ev);
        audit_append(ev);
    };

    auto audit_ok = [&](const std::string& path_rel) {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_rmdir_ok";
        ev.outcome = "ok";
        ev.f["fingerprint"] = fp_hex;
        ev.f["path"] = pqnas::shorten(path_rel, 200);

        add_ip_headers(ev);
        audit_append(ev);
    };

    // must have allocated storage
    auto uopt = users.get(fp_hex);
    if (!uopt.has_value() || uopt->storage_state != "allocated") {
        audit_fail("storage_unallocated", 403);
        reply_json(res, 403, json{
            {"ok", false},
            {"error", "storage_unallocated"},
            {"message", "Storage not allocated"}
        }.dump());
        return;
    }

    std::string path_rel;
    if (req.has_param("path")) path_rel = req.get_param_value("path");
    if (path_rel.empty()) {
        audit_fail("missing_path", 400);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "missing path"}
        }.dump());
        return;
    }

    const std::filesystem::path user_dir = user_dir_for_fp(users, fp_hex);

    std::string rel_norm;
    std::string nerr;
    if (!pqnas::normalize_user_rel_path_strict(path_rel, &rel_norm, &nerr)) {
        audit_fail("invalid_path", 400, nerr, path_rel);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid path"}
        }.dump());
        return;
    }

    // refuse root-ish directory removal too
    if (rel_norm.empty() || rel_norm == "." || rel_norm == "/") {
        audit_fail("refuse_root", 400, "", path_rel);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "refusing to delete root"}
        }.dump());
        return;
    }

    // Serialize writes on overlapping logical paths for this user.
    if (!require_user_no_live_lock_for_write_local(
            users, users_path, res, fp_hex, rel_norm, "write", false)) {
        return;
    }

    auto* plm = pqnas::get_path_lock_manager();
    auto write_guard = plm->lock_paths(fp_hex, {rel_norm});

    pqnas::ResolvedLogicalItem item;
    std::string rerr;
    if (!pqnas::resolve_existing_user_item(users, fp_hex, rel_norm, &item, &rerr) || !item.exists) {
        audit_fail("not_found", 404, rerr, path_rel);
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "directory not found"}
        }.dump());
        return;
    }

    if (!item.is_dir) {
        audit_fail("not_dir", 400, "", path_rel);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "path is not a directory"}
        }.dump());
        return;
    }

    auto* idx = pqnas::get_file_location_index();
    if (!idx) {
        audit_fail("metadata_index_missing", 500, "", path_rel);
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "metadata index missing"}
        }.dump());
        return;
    }

    // Logical emptiness check first.
    // If metadata subtree has any descendant file rows, the directory is not empty.
    std::string lerr;
    auto subtree_rows = idx->list_subtree_records(fp_hex, rel_norm, &lerr);
    if (!lerr.empty()) {
        audit_fail("metadata_subtree_list_failed", 500, lerr, path_rel);
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "failed to inspect directory contents"},
            {"detail", pqnas::shorten(lerr, 180)}
        }.dump());
        return;
    }

    if (!subtree_rows.empty()) {
        audit_fail("not_empty", 409, "", path_rel);
        reply_json(res, 409, json{
            {"ok", false},
            {"error", "not_empty"},
            {"message", "directory is not empty"}
        }.dump());
        return;
    }

    // No metadata descendants.
    // Allow legacy physical-empty-dir fallback.
    if (!item.has_physical_anchor) {
        // Logical dir exists but has no metadata subtree and no physical anchor:
        // treat as not found / unsupported for now.
        audit_fail("not_found", 404, "", path_rel);
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "directory not found"}
        }.dump());
        return;
    }

    const std::filesystem::path path_abs = item.abs_path;

    if (path_abs == user_dir) {
        audit_fail("refuse_user_root", 400, "", path_rel);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "refusing to delete user storage root"}
        }.dump());
        return;
    }

    std::error_code ec;
    auto st = std::filesystem::symlink_status(path_abs, ec);
    if (ec || !std::filesystem::exists(st)) {
        audit_fail("not_found", 404, "", path_rel);
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "directory not found"}
        }.dump());
        return;
    }

    if (std::filesystem::is_symlink(st)) {
        audit_fail("symlink_not_supported", 400, "", path_rel);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "symlinks not supported"}
        }.dump());
        return;
    }

    if (!std::filesystem::is_directory(st)) {
        audit_fail("not_dir", 400, "", path_rel);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "path is not a directory"}
        }.dump());
        return;
    }

    // Empty-only physical fallback
    ec.clear();
    bool empty = std::filesystem::is_empty(path_abs, ec);
    if (ec) {
        audit_fail("is_empty_failed", 500, ec.message(), path_rel);
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "failed to check if directory is empty"},
            {"detail", pqnas::shorten(ec.message(), 180)}
        }.dump());
        return;
    }
    if (!empty) {
        audit_fail("not_empty", 409, "", path_rel);
        reply_json(res, 409, json{
            {"ok", false},
            {"error", "not_empty"},
            {"message", "directory is not empty"}
        }.dump());
        return;
    }

    // Recheck right before remove so the target cannot be swapped to a symlink or other type.
    ec.clear();
    auto st2 = std::filesystem::symlink_status(path_abs, ec);
    if (ec || !std::filesystem::exists(st2)) {
        audit_fail("not_found", 404, "", path_rel);
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "directory not found"}
        }.dump());
        return;
    }

    if (std::filesystem::is_symlink(st2)) {
        audit_fail("symlink_not_supported", 400, "", path_rel);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "symlinks not supported"}
        }.dump());
        return;
    }

    if (!std::filesystem::is_directory(st2)) {
        audit_fail("path_changed_before_delete", 409, "", path_rel);
        reply_json(res, 409, json{
            {"ok", false},
            {"error", "path_conflict"},
            {"message", "path changed before delete"}
        }.dump());
        return;
    }

    bool removed = std::filesystem::remove(path_abs, ec);
    if (ec || !removed) {
        audit_fail("remove_failed", 500, ec.message(), path_rel);
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "failed to remove directory"},
            {"detail", pqnas::shorten(ec.message(), 180)}
        }.dump());
        return;
    }

    // Best-effort favorites cleanup in case an empty dir was favorited.
    {
        std::string ferr;
        if (!pqnas::favorites_remove_under_prefix(user_dir, rel_norm, "dir", &ferr)) {
            pqnas::AuditEvent ev;
            ev.event = "v4.files_rmdir_warn";
            ev.outcome = "warn";
            ev.f["fingerprint"] = fp_hex;
            ev.f["reason"] = "favorites_cleanup_failed";
            ev.f["path"] = pqnas::shorten(rel_norm, 200);
            ev.f["detail"] = pqnas::shorten(ferr, 180);
            add_ip_headers(ev);
            audit_append(ev);
        }
    }

    audit_ok(path_rel);

    reply_json(res, 200, json{
        {"ok", true},
        {"path", path_rel}
    }.dump());
    });


    // POST /api/v4/files/tree?path=rel/path&max=500
srv.Post("/api/v4/files/tree", [&](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;

    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto add_ip_headers = [&](pqnas::AuditEvent& ev) {
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
    };

    auto audit_fail = [&](const std::string& reason, int http, const std::string& detail = "",
                          const std::string& path_rel = "", int max_entries = -1) {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_tree_fail";
        ev.outcome = "fail";
        ev.f["fingerprint"] = fp_hex;
        ev.f["reason"] = reason;
        ev.f["http"] = std::to_string(http);
        if (!path_rel.empty()) ev.f["path"] = pqnas::shorten(path_rel, 200);
        if (max_entries >= 0)  ev.f["max"]  = std::to_string(max_entries);
        if (!detail.empty())   ev.f["detail"] = pqnas::shorten(detail, 180);

        add_ip_headers(ev);
        audit_append(ev);
    };

    auto audit_ok = [&](const std::string& path_rel, int max_entries,
                        std::uint64_t entries, std::uint64_t files, std::uint64_t dirs, bool truncated) {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_tree_ok";
        ev.outcome = "ok";
        ev.f["fingerprint"] = fp_hex;
        ev.f["path"] = pqnas::shorten(path_rel, 200);
        ev.f["max"] = std::to_string(max_entries);
        ev.f["entries"] = std::to_string((unsigned long long)entries);
        ev.f["files"] = std::to_string((unsigned long long)files);
        ev.f["dirs"]  = std::to_string((unsigned long long)dirs);
        ev.f["truncated"] = truncated ? "1" : "0";

        add_ip_headers(ev);
        audit_append(ev);
    };

    // must have allocated storage
    auto uopt = users.get(fp_hex);
    if (!uopt.has_value() || uopt->storage_state != "allocated") {
        audit_fail("storage_unallocated", 403);
        reply_json(res, 403, json{
            {"ok", false},
            {"error", "storage_unallocated"},
            {"message", "Storage not allocated"}
        }.dump());
        return;
    }

    std::string path_rel;
    if (req.has_param("path")) path_rel = req.get_param_value("path");
    if (path_rel.empty()) path_rel = "."; // convenience: root of user storage

    int max_entries = 500;
    if (req.has_param("max")) {
        try { max_entries = std::stoi(req.get_param_value("max")); } catch (...) {}
    }
    max_entries = std::max(1, std::min(5000, max_entries));

    const std::filesystem::path user_dir = user_dir_for_fp(users, fp_hex);

    std::filesystem::path path_abs;
    std::string err;
    if (!pqnas::resolve_user_path_strict(user_dir, path_rel, &path_abs, &err)) {
        audit_fail("invalid_path", 400, err, path_rel, max_entries);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid path"}
        }.dump());
        return;
    }

    {
        std::string serr;
        if (!ensure_no_symlink_in_existing_path_prefix(path_abs, &serr)) {
            audit_fail("symlink_not_supported", 400, serr, path_rel, max_entries);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "symlinks not supported for tree base"}
            }.dump());
            return;
        }
    }

    std::error_code ec;
    auto st = std::filesystem::symlink_status(path_abs, ec);
    if (ec || !std::filesystem::exists(st)) {
        audit_fail("not_found", 404, "", path_rel, max_entries);
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "path not found"}
        }.dump());
        return;
    }

    if (std::filesystem::is_symlink(st)) {
        audit_fail("symlink_not_supported", 400, "", path_rel, max_entries);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "symlinks not supported for tree base"}
        }.dump());
        return;
    }

    // Helpers for building relative paths under user_dir (for UI)
    auto to_rel_from_user_dir = [&](const std::filesystem::path& abs) -> std::string {
        std::error_code ec3;
        auto rel = std::filesystem::relative(abs, user_dir, ec3);
        if (ec3) return path_rel; // fallback
        return rel.generic_string();
    };

    std::uint64_t entries = 0, files = 0, dirs = 0;
    bool truncated = false;

    // Return shape:
    // node = { name, path, type, bytes?, children? }
    std::function<json(const std::filesystem::path&, const std::filesystem::path&)> build_node;

    build_node = [&](const std::filesystem::path& abs, const std::filesystem::path& name_for_display) -> json {
        json node;
        node["name"] = name_for_display.empty() ? std::string("") : name_for_display.generic_string();
        node["path"] = to_rel_from_user_dir(abs);

        std::error_code ec2;
        auto st2 = std::filesystem::symlink_status(abs, ec2);
        if (ec2) {
            node["type"] = "other";
            return node;
        }

        // Do not follow symlinks: report and stop.
        if (std::filesystem::is_symlink(st2)) {
            node["type"] = "symlink";
            return node;
        }

        if (std::filesystem::is_regular_file(st2)) {
            node["type"] = "file";
            node["bytes"] = pqnas::file_size_u64_safe(abs);
            return node;
        }

        if (std::filesystem::is_directory(st2)) {
            node["type"] = "dir";
            node["children"] = json::array();

            // Stop adding children if we hit max
            if ((int)entries >= max_entries) {
                truncated = true;
                return node;
            }

            // Stable order: name sort
            std::vector<std::filesystem::directory_entry> kids;
            for (auto it = std::filesystem::directory_iterator(abs, std::filesystem::directory_options::skip_permission_denied, ec2);
                 it != std::filesystem::directory_iterator();
                 it.increment(ec2)) {
                if (ec2) break;
                kids.push_back(*it);
            }
            std::sort(kids.begin(), kids.end(), [](const auto& a, const auto& b) {
                return a.path().filename().string() < b.path().filename().string();
            });

            for (auto& de : kids) {
                if ((int)entries >= max_entries) { truncated = true; break; }

                std::error_code ec4;
                auto stc = std::filesystem::symlink_status(de.path(), ec4);
                if (ec4) continue;

                // Skip symlink children entirely.
                if (std::filesystem::is_symlink(stc)) {
                    continue;
                }

                entries++;

                if (std::filesystem::is_directory(stc)) {
                    dirs++;
                    node["children"].push_back(build_node(de.path(), de.path().filename()));
                    continue;
                }

                if (std::filesystem::is_regular_file(stc)) {
                    files++;
                    node["children"].push_back(json{
                        {"name", de.path().filename().string()},
                        {"path", to_rel_from_user_dir(de.path())},
                        {"type", "file"},
                        {"bytes", pqnas::file_size_u64_safe(de.path())}
                    });
                    continue;
                }

                node["children"].push_back(json{
                    {"name", de.path().filename().string()},
                    {"path", to_rel_from_user_dir(de.path())},
                    {"type", "other"}
                });
            }

            return node;
        }

        node["type"] = "other";
        return node;
    };

    json root;

    // Root node counts
    entries = 1; // root itself
    if (std::filesystem::is_directory(st)) dirs = 1;
    else if (std::filesystem::is_regular_file(st)) files = 1;

    // For root node name: show last component unless "." (use "")
    std::filesystem::path root_name = path_abs.filename();
    if (path_rel == "." || path_rel == "/" || path_rel.empty()) root_name = "";

    root = build_node(path_abs, root_name);

    audit_ok(path_rel, max_entries, entries, files, dirs, truncated);

    reply_json(res, 200, json{
        {"ok", true},
        {"path", path_rel},
        {"max", max_entries},
        {"truncated", truncated},
        {"entries", entries},
        {"files", files},
        {"dirs", dirs},
        {"tree", root}
    }.dump());
});

// POST /api/v4/files/touch?path=rel/file
srv.Post("/api/v4/files/touch", [&](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_cookie_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role))
        return;

    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto add_ip_headers = [&](pqnas::AuditEvent& ev) {
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
    };

    auto audit_fail = [&](const std::string& reason, int http, const std::string& detail = "",
                          const std::string& path_rel = "") {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_touch_fail";
        ev.outcome = "fail";
        ev.f["fingerprint"] = fp_hex;
        ev.f["reason"] = reason;
        ev.f["http"] = std::to_string(http);
        if (!path_rel.empty()) ev.f["path"] = pqnas::shorten(path_rel, 200);
        if (!detail.empty())   ev.f["detail"] = pqnas::shorten(detail, 180);

        add_ip_headers(ev);
        audit_append(ev);
    };

    auto audit_ok = [&](const std::string& path_rel) {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_touch_ok";
        ev.outcome = "ok";
        ev.f["fingerprint"] = fp_hex;
        ev.f["path"] = pqnas::shorten(path_rel, 200);
        ev.f["action"] = "created";

        add_ip_headers(ev);
        audit_append(ev);
    };

    // must have allocated storage
    auto uopt = users.get(fp_hex);
    if (!uopt.has_value() || uopt->storage_state != "allocated") {
        audit_fail("storage_unallocated", 403);
        reply_json(res, 403, json{
            {"ok", false},
            {"error", "storage_unallocated"},
            {"message", "Storage not allocated"}
        }.dump());
        return;
    }

    std::string path_rel;
    if (req.has_param("path")) path_rel = req.get_param_value("path");

    if (path_rel.empty()) {
        audit_fail("missing_path", 400);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "missing path"}
        }.dump());
        return;
    }

    const std::filesystem::path user_dir = user_dir_for_fp(users, fp_hex);

    auto ensure_no_symlink_dir_chain = [&](const std::filesystem::path& base,
                                       const std::filesystem::path& dir,
                                       bool require_exists,
                                       std::string* out_err) -> bool {
    if (out_err) out_err->clear();

    std::filesystem::path cur = base.lexically_normal();
    const std::filesystem::path target = dir.lexically_normal();

    std::error_code ec;
    auto base_st = std::filesystem::symlink_status(cur, ec);
    if (ec) {
        if (out_err) *out_err = "base symlink_status failed: " + ec.message();
        return false;
    }
    if (!std::filesystem::exists(base_st)) {
        if (out_err) *out_err = "base directory missing";
        return false;
    }
    if (std::filesystem::is_symlink(base_st)) {
        if (out_err) *out_err = "base directory is symlink";
        return false;
    }
    if (!std::filesystem::is_directory(base_st)) {
        if (out_err) *out_err = "base path is not a directory";
        return false;
    }

    const auto rel = target.lexically_relative(cur);
    if (rel.empty() || rel == ".") return true;

    for (const auto& part : rel) {
        if (part.empty()) continue;
        cur /= part;

        ec.clear();
        auto st = std::filesystem::symlink_status(cur, ec);
        if (ec) {
            if (require_exists) {
                if (out_err) *out_err = "symlink_status failed: " + ec.message();
                return false;
            }
            continue;
        }

        if (!std::filesystem::exists(st)) {
            if (require_exists) {
                if (out_err) *out_err = "directory missing";
                return false;
            }
            continue;
        }

        if (std::filesystem::is_symlink(st)) {
            if (out_err) *out_err = "directory chain contains symlink";
            return false;
        }

        if (!std::filesystem::is_directory(st)) {
            if (out_err) *out_err = "path component is not a directory";
            return false;
        }
    }

    return true;
};
    std::filesystem::path path_abs;
    std::string err;
    if (!pqnas::resolve_user_path_strict(user_dir, path_rel, &path_abs, &err)) {
        audit_fail("invalid_path", 400, err, path_rel);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid path"}
        }.dump());
        return;
    }

    const std::filesystem::path parent_abs = path_abs.parent_path();

    // Ensure parent exists, but never through a symlinked parent chain.
    std::error_code ec;
    {
        std::string derr;
        if (!ensure_no_symlink_dir_chain(user_dir, parent_abs, false, &derr)) {
            audit_fail("path_conflict", 409, derr, path_rel);
            reply_json(res, 409, json{
                {"ok", false},
                {"error", "path_conflict"},
                {"message", "parent path is invalid"},
                {"detail", pqnas::shorten(derr, 180)}
            }.dump());
            return;
        }
    }

    std::filesystem::create_directories(parent_abs, ec);
    if (ec) {
        audit_fail("mkdir_failed", 500, ec.message(), path_rel);
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "failed to create parent directories"},
            {"detail", pqnas::shorten(ec.message(), 180)}
        }.dump());
        return;
    }

    {
        std::string derr;
        if (!ensure_no_symlink_dir_chain(user_dir, parent_abs, true, &derr)) {
            audit_fail("path_conflict", 409, derr, path_rel);
            reply_json(res, 409, json{
                {"ok", false},
                {"error", "path_conflict"},
                {"message", "parent path is invalid"},
                {"detail", pqnas::shorten(derr, 180)}
            }.dump());
            return;
        }
    }

    // Create-only: if target exists, reject. Do not follow symlinks.
    ec.clear();
    auto st = std::filesystem::symlink_status(path_abs, ec);
    if (!ec && std::filesystem::exists(st)) {
        if (std::filesystem::is_symlink(st)) {
            audit_fail("symlink_not_supported", 400, "", path_rel);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "symlinks not supported"}
            }.dump());
            return;
        }

        audit_fail("already_exists", 409, "", path_rel);
        reply_json(res, 409, json{
            {"ok", false},
            {"error", "already_exists"},
            {"message", "file already exists"}
        }.dump());
        return;
    }

    if (ec && ec != std::make_error_code(std::errc::no_such_file_or_directory)) {
        audit_fail("stat_failed", 500, ec.message(), path_rel);
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "failed to inspect target path"},
            {"detail", pqnas::shorten(ec.message(), 180)}
        }.dump());
        return;
    }
    ec.clear();

    // Create empty file
    {
        std::ofstream f(path_abs, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!f.good()) {
            audit_fail("create_failed", 500, "cannot create file", path_rel);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to create file"}
            }.dump());
            return;
        }
    }

    audit_ok(path_rel);

    reply_json(res, 200, json{
        {"ok", true},
        {"path", path_rel},
        {"action", "created"}
    }.dump());
});

    // POST /api/v4/files/cat?path=rel/file&max_bytes=65536
srv.Post("/api/v4/files/cat", [&](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_cookie_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role))
        return;

    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto add_ip_headers = [&](pqnas::AuditEvent& ev) {
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
    };

    auto audit_fail = [&](const std::string& reason, int http, const std::string& detail = "",
                          const std::string& path_rel = "", int max_bytes = -1) {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_cat_fail";
        ev.outcome = "fail";
        ev.f["fingerprint"] = fp_hex;
        ev.f["reason"] = reason;
        ev.f["http"] = std::to_string(http);
        if (!path_rel.empty()) ev.f["path"] = pqnas::shorten(path_rel, 200);
        if (max_bytes >= 0)    ev.f["max_bytes"] = std::to_string(max_bytes);
        if (!detail.empty())   ev.f["detail"] = pqnas::shorten(detail, 180);

        add_ip_headers(ev);
        audit_append(ev);
    };

    auto audit_ok = [&](const std::string& path_rel,
                        int max_bytes,
                        std::uint64_t bytes_total,
                        std::uint64_t bytes_returned,
                        bool truncated) {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_cat_ok";
        ev.outcome = "ok";
        ev.f["fingerprint"] = fp_hex;
        ev.f["path"] = pqnas::shorten(path_rel, 200);
        ev.f["max_bytes"] = std::to_string(max_bytes);
        ev.f["bytes_total"] = std::to_string((unsigned long long)bytes_total);
        ev.f["bytes_returned"] = std::to_string((unsigned long long)bytes_returned);
        ev.f["truncated"] = truncated ? "1" : "0";

        add_ip_headers(ev);
        audit_append(ev);
    };

    // must have allocated storage
    auto uopt = users.get(fp_hex);
    if (!uopt.has_value() || uopt->storage_state != "allocated") {
        audit_fail("storage_unallocated", 403);
        reply_json(res, 403, json{
            {"ok", false},
            {"error", "storage_unallocated"},
            {"message", "Storage not allocated"}
        }.dump());
        return;
    }

    std::string path_rel;
    if (req.has_param("path")) path_rel = req.get_param_value("path");
    if (path_rel.empty()) {
        audit_fail("missing_path", 400);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "missing path"}
        }.dump());
        return;
    }

    int max_bytes = 64 * 1024;
    if (req.has_param("max_bytes")) {
        try { max_bytes = std::stoi(req.get_param_value("max_bytes")); } catch (...) {}
    }
    max_bytes = std::max(1, std::min(1024 * 1024, max_bytes)); // 1..1MiB

    const std::filesystem::path user_dir = user_dir_for_fp(users, fp_hex);

    std::filesystem::path path_abs;
    std::string err;
    if (!pqnas::resolve_user_path_strict(user_dir, path_rel, &path_abs, &err)) {
        audit_fail("invalid_path", 400, err, path_rel, max_bytes);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid path"}
        }.dump());
        return;
    }

    {
    std::string serr;
    if (!ensure_no_symlink_in_existing_path_prefix(path_abs, &serr)) {
        audit_fail("symlink_not_supported", 400, serr, path_rel, max_bytes);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "symlinks not supported"}
        }.dump());
        return;
    }
}

    // must exist + must be regular file, without following symlinks
    std::error_code ec;
    auto st = std::filesystem::symlink_status(path_abs, ec);
    if (ec || !std::filesystem::exists(st)) {
        audit_fail("not_found", 404, "", path_rel, max_bytes);
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "file not found"}
        }.dump());
        return;
    }

    if (std::filesystem::is_symlink(st)) {
        audit_fail("symlink_not_supported", 400, "", path_rel, max_bytes);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "symlinks not supported"}
        }.dump());
        return;
    }

    if (!std::filesystem::is_regular_file(st)) {
        audit_fail("not_file", 400, "", path_rel, max_bytes);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "path is not a file"}
        }.dump());
        return;
    }

    const std::uint64_t bytes_total = pqnas::file_size_u64_safe(path_abs);

    std::ifstream f(path_abs, std::ios::binary);
    if (!f.good()) {
        audit_fail("open_failed", 500, "cannot open file", path_rel, max_bytes);
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "failed to open file"}
        }.dump());
        return;
    }

    std::string buf;
    buf.resize((size_t)max_bytes);
    f.read(&buf[0], (std::streamsize)buf.size());
    std::streamsize n = f.gcount();
    if (n < 0) n = 0;
    buf.resize((size_t)n);

    bool truncated = (bytes_total > (std::uint64_t)buf.size());

    // Detect binary-ish content: reject if NUL found in returned bytes
    if (std::find(buf.begin(), buf.end(), '\0') != buf.end()) {
        audit_fail("binary_detected", 415, "", path_rel, max_bytes);
        reply_json(res, 415, json{
            {"ok", false},
            {"error", "unsupported_media_type"},
            {"message", "binary file cannot be previewed as text"}
        }.dump());
        return;
    }

    audit_ok(path_rel, max_bytes, bytes_total, (std::uint64_t)buf.size(), truncated);

    reply_json(res, 200, json{
        {"ok", true},
        {"path", path_rel},
        {"bytes_total", bytes_total},
        {"bytes_returned", (std::uint64_t)buf.size()},
        {"truncated", truncated},
        {"text", buf}
    }.dump());
});

    // POST /api/v4/files/save_text?path=rel/file   (body = UTF-8 text)
srv.Post("/api/v4/files/save_text", [&](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_cookie_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role))
        return;

    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto add_ip_headers = [&](pqnas::AuditEvent& ev) {
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
    };

    auto audit_fail = [&](const std::string& reason, int http, const std::string& detail = "",
                          const std::string& path_rel = "") {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_save_text_fail";
        ev.outcome = "fail";
        ev.f["fingerprint"] = fp_hex;
        ev.f["reason"] = reason;
        ev.f["http"] = std::to_string(http);
        if (!path_rel.empty()) ev.f["path"] = pqnas::shorten(path_rel, 200);
        if (!detail.empty())   ev.f["detail"] = pqnas::shorten(detail, 180);

        add_ip_headers(ev);
        audit_append(ev);
    };

    auto audit_quota = [&](int http,
                           const std::string& detail,
                           const std::string& path_rel,
                           std::uint64_t new_bytes,
                           std::uint64_t old_bytes,
                           std::uint64_t delta_bytes) {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_save_text_quota_exceeded";
        ev.outcome = "fail";
        ev.f["fingerprint"] = fp_hex;
        ev.f["reason"] = "quota_exceeded";
        ev.f["http"] = std::to_string(http);
        ev.f["path"] = pqnas::shorten(path_rel, 200);
        ev.f["new_bytes"] = std::to_string((unsigned long long)new_bytes);
        ev.f["old_bytes"] = std::to_string((unsigned long long)old_bytes);
        ev.f["delta_bytes"] = std::to_string((unsigned long long)delta_bytes);
        if (!detail.empty()) ev.f["detail"] = pqnas::shorten(detail, 180);

        add_ip_headers(ev);
        audit_append(ev);
    };

    auto audit_ok = [&](const std::string& path_rel,
                        std::uint64_t new_bytes,
                        std::uint64_t old_bytes,
                        std::uint64_t delta_bytes,
                        bool overwrote) {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_save_text_ok";
        ev.outcome = "ok";
        ev.f["fingerprint"] = fp_hex;
        ev.f["path"] = pqnas::shorten(path_rel, 200);
        ev.f["type"] = "file";
        ev.f["new_bytes"] = std::to_string((unsigned long long)new_bytes);
        ev.f["old_bytes"] = std::to_string((unsigned long long)old_bytes);
        ev.f["delta_bytes"] = std::to_string((unsigned long long)delta_bytes);
        ev.f["overwrote"] = overwrote ? "1" : "0";

        add_ip_headers(ev);
        audit_append(ev);
    };

    // must have allocated storage
    auto uopt = users.get(fp_hex);
    if (!uopt.has_value() || uopt->storage_state != "allocated") {
        audit_fail("storage_unallocated", 403);
        reply_json(res, 403, json{
            {"ok", false},
            {"error", "storage_unallocated"},
            {"message", "Storage not allocated"}
        }.dump());
        return;
    }

    std::string path_rel;
    if (req.has_param("path")) path_rel = req.get_param_value("path");
    if (path_rel.empty()) {
        audit_fail("missing_path", 400);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "missing path"}
        }.dump());
        return;
    }

    // Body is the text content (can be empty)
    const std::string& body = req.body;

    // Reject binary-ish: any NUL in body
    if (std::find(body.begin(), body.end(), '\0') != body.end()) {
        audit_fail("binary_detected", 415, "", path_rel);
        reply_json(res, 415, json{
            {"ok", false},
            {"error", "unsupported_media_type"},
            {"message", "binary content not allowed for save_text"}
        }.dump());
        return;
    }

    const std::filesystem::path user_dir = user_dir_for_fp(users, fp_hex);

    std::filesystem::path path_abs;
    std::string err;
    if (!pqnas::resolve_user_path_strict(user_dir, path_rel, &path_abs, &err)) {
        audit_fail("invalid_path", 400, err, path_rel);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid path"}
        }.dump());
        return;
    }
    {
        std::string serr;
        if (!ensure_no_symlink_in_existing_path_prefix(path_abs.parent_path(), &serr)) {
            audit_fail("symlink_not_supported", 400, serr, path_rel);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "symlinks not supported"}
            }.dump());
            return;
        }
    }
    // If destination exists: must be a regular file, and symlinks are rejected.
    bool overwrote = false;
    std::uint64_t old_bytes = 0;

    std::error_code ec;
    auto st_dst = std::filesystem::symlink_status(path_abs, ec);
    if (ec) {
        if (ec != std::make_error_code(std::errc::no_such_file_or_directory)) {
            audit_fail("dst_stat_failed", 500, ec.message(), path_rel);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to inspect destination"},
                {"detail", pqnas::shorten(ec.message(), 180)}
            }.dump());
            return;
        }
        ec.clear();
    } else if (std::filesystem::exists(st_dst)) {
        overwrote = true;

        if (std::filesystem::is_symlink(st_dst)) {
            audit_fail("symlink_not_supported", 400, "", path_rel);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "symlinks not supported"}
            }.dump());
            return;
        }

        if (!std::filesystem::is_regular_file(st_dst)) {
            audit_fail("dst_not_file", 400, "", path_rel);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "destination exists and is not a file"}
            }.dump());
            return;
        }

        old_bytes = pqnas::file_size_u64_safe(path_abs);
    }

    // Ensure parent dirs exist, but never through a symlinked parent chain.
    ec.clear();
    std::filesystem::create_directories(path_abs.parent_path(), ec);
    if (ec) {
        audit_fail("mkdir_failed", 500, ec.message(), path_rel);
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "failed to create parent directories"},
            {"detail", pqnas::shorten(ec.message(), 180)}
        }.dump());
        return;
    }

    {
        std::string serr;
        if (!ensure_no_symlink_in_existing_path_prefix(path_abs.parent_path(), &serr)) {
            audit_fail("symlink_not_supported", 400, serr, path_rel);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "symlinks not supported"}
            }.dump());
            return;
        }
    }

    const std::uint64_t new_bytes = (std::uint64_t)body.size();

    // Quota delta: only additional bytes count
    std::uint64_t delta_bytes = 0;
    if (new_bytes > old_bytes) delta_bytes = (new_bytes - old_bytes);

if (delta_bytes > 0) {
    pqnas::QuotaCheckResult qc = pqnas::quota_check_for_upload_v1(
        users, fp_hex, user_dir, path_rel, delta_bytes
    );

    if (!qc.ok) {
        // storage not allocated
        if (qc.error == "storage_unallocated") {
            audit_fail("storage_unallocated", 403, "", path_rel);
            reply_json(res, 403, json{
                {"ok", false},
                {"error", "storage_unallocated"},
                {"message", "Storage not allocated"},
                {"fingerprint_hex", fp_hex},
                {"quota_bytes", qc.quota_bytes},
                {"incoming_bytes", qc.incoming_bytes}
            }.dump());
            return;
        }

        // invalid path (should be rare here because we already resolved it, but keep fail-closed)
        if (qc.error == "invalid_path") {
            audit_fail("invalid_path", 400, "", path_rel);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "invalid path"}
            }.dump());
            return;
        }

        // quota exceeded
        if (qc.error == "quota_exceeded") {
            const int http = 413; // or 413 if you want, but keep consistent across endpoints
            audit_quota(http, "", path_rel, new_bytes, old_bytes, delta_bytes);

            reply_json(res, http, json{
                {"ok", false},
                {"error", "quota_exceeded"},
                {"message", "Quota exceeded"},
                {"fingerprint_hex", fp_hex},
                {"used_bytes", qc.used_bytes},
                {"quota_bytes", qc.quota_bytes},
                {"free_bytes", (qc.quota_bytes > qc.used_bytes ? (qc.quota_bytes - qc.used_bytes) : 0)},
                {"incoming_bytes", qc.incoming_bytes},
                {"existing_bytes", qc.existing_bytes},
                {"would_used_bytes", qc.would_used_bytes}
            }.dump());
            return;
        }

        // other deny
        audit_fail("quota_check_failed", 403, qc.error, path_rel);
        reply_json(res, 403, json{
            {"ok", false},
            {"error", "forbidden"},
            {"message", "policy denied"}
        }.dump());
        return;
    }
}

    // Atomic write: temp file in same directory + rename
    const std::filesystem::path tmp =
        path_abs.parent_path() /
        (path_abs.filename().string() + ".tmp.save_text." + random_b64url(12));

    // Write temp
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!out.good()) {
            audit_fail("tmp_create_failed", 500, "cannot create temp file", path_rel);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to create temp file"}
            }.dump());
            return;
        }
        out.write(body.data(), (std::streamsize)body.size());
        if (!out.good()) {
            std::error_code ec2;
            std::filesystem::remove(tmp, ec2);

            audit_fail("tmp_write_failed", 500, "write failed", path_rel);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "write failed"}
            }.dump());
            return;
        }
    }

    // Overwrite handling: recheck destination first, then remove only if still a regular file.
    if (overwrote) {
        ec.clear();
        auto st2 = std::filesystem::symlink_status(path_abs, ec);
        if (ec || !std::filesystem::exists(st2)) {
            std::error_code ec2;
            std::filesystem::remove(tmp, ec2);

            audit_fail("not_found", 404, "", path_rel);
            reply_json(res, 404, json{
                {"ok", false},
                {"error", "not_found"},
                {"message", "destination disappeared before save"}
            }.dump());
            return;
        }

        if (std::filesystem::is_symlink(st2)) {
            std::error_code ec2;
            std::filesystem::remove(tmp, ec2);

            audit_fail("symlink_not_supported", 400, "", path_rel);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "symlinks not supported"}
            }.dump());
            return;
        }

        if (!std::filesystem::is_regular_file(st2)) {
            std::error_code ec2;
            std::filesystem::remove(tmp, ec2);

            audit_fail("path_changed_before_save", 409, "", path_rel);
            reply_json(res, 409, json{
                {"ok", false},
                {"error", "path_conflict"},
                {"message", "destination changed before save"}
            }.dump());
            return;
        }

        ec.clear();
        std::filesystem::remove(path_abs, ec);
        if (ec) {
            std::error_code ec2;
            std::filesystem::remove(tmp, ec2);

            audit_fail("overwrite_remove_failed", 500, ec.message(), path_rel);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to overwrite destination"},
                {"detail", pqnas::shorten(ec.message(), 180)}
            }.dump());
            return;
        }
    }

    // Final recheck before rename into place.
    {
        std::string serr;
        if (!ensure_no_symlink_in_existing_path_prefix(path_abs.parent_path(), &serr)) {
            std::error_code ec2;
            std::filesystem::remove(tmp, ec2);

            audit_fail("symlink_not_supported", 400, serr, path_rel);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "symlinks not supported"}
            }.dump());
            return;
        }
    }

    ec.clear();
    auto st3 = std::filesystem::symlink_status(path_abs, ec);
    if (!ec && std::filesystem::exists(st3)) {
        if (std::filesystem::is_symlink(st3)) {
            std::error_code ec2;
            std::filesystem::remove(tmp, ec2);

            audit_fail("symlink_not_supported", 400, "", path_rel);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "symlinks not supported"}
            }.dump());
            return;
        }

        if (!std::filesystem::is_regular_file(st3)) {
            std::error_code ec2;
            std::filesystem::remove(tmp, ec2);

            audit_fail("path_changed_before_save", 409, "", path_rel);
            reply_json(res, 409, json{
                {"ok", false},
                {"error", "path_conflict"},
                {"message", "destination changed before save"}
            }.dump());
            return;
        }
    } else if (ec && ec != std::make_error_code(std::errc::no_such_file_or_directory)) {
        std::error_code ec2;
        std::filesystem::remove(tmp, ec2);

        audit_fail("dst_stat_failed", 500, ec.message(), path_rel);
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "failed to inspect destination"},
            {"detail", pqnas::shorten(ec.message(), 180)}
        }.dump());
        return;
    }

    // Rename into place
    ec.clear();
    std::filesystem::rename(tmp, path_abs, ec);
    if (ec) {
        std::error_code ec2;
        std::filesystem::remove(tmp, ec2);

        audit_fail("rename_failed", 500, ec.message(), path_rel);
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "save failed"},
            {"detail", pqnas::shorten(ec.message(), 180)}
        }.dump());
        return;
    }

    audit_ok(path_rel, new_bytes, old_bytes, delta_bytes, overwrote);

    reply_json(res, 200, json{
        {"ok", true},
        {"path", path_rel},
        {"new_bytes", new_bytes},
        {"old_bytes", old_bytes},
        {"delta_bytes", delta_bytes},
        {"overwrote", overwrote}
    }.dump());
});

// GET /api/v4/files/read_text?path=rel/path
srv.Get("/api/v4/files/read_text", [&](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;

    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto add_ip_headers = [&](pqnas::AuditEvent& ev) {
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
    };

    auto audit_fail = [&](const std::string& reason, int http,
                          const std::string& detail = "",
                          const std::string& path_rel = "") {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_read_text_fail";
        ev.outcome = "fail";
        ev.f["fingerprint"] = fp_hex;
        ev.f["reason"] = reason;
        ev.f["http"] = std::to_string(http);
        if (!path_rel.empty()) ev.f["path"] = pqnas::shorten(path_rel, 200);
        if (!detail.empty()) ev.f["detail"] = pqnas::shorten(detail, 180);
        add_ip_headers(ev);
        audit_append(ev);
    };

    auto audit_ok = [&](const std::string& path_rel,
                        std::uint64_t bytes,
                        std::uint64_t mtime_epoch,
                        const std::string& sha256_hex) {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_read_text_ok";
        ev.outcome = "ok";
        ev.f["fingerprint"] = fp_hex;
        ev.f["path"] = pqnas::shorten(path_rel, 200);
        ev.f["bytes"] = std::to_string((unsigned long long)bytes);
        ev.f["mtime_epoch"] = std::to_string((unsigned long long)mtime_epoch);
        ev.f["sha256"] = pqnas::shorten(sha256_hex, 80);
        add_ip_headers(ev);
        audit_append(ev);
    };

    auto uopt = users.get(fp_hex);
    if (!uopt.has_value() || uopt->storage_state != "allocated") {
        audit_fail("storage_unallocated", 403);
        reply_json(res, 403, json{
            {"ok", false},
            {"error", "storage_unallocated"},
            {"message", "Storage not allocated"}
        }.dump());
        return;
    }

    std::string path_rel;
    if (req.has_param("path")) path_rel = req.get_param_value("path");
    if (path_rel.empty()) {
        audit_fail("missing_path", 400);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "missing path"}
        }.dump());
        return;
    }

    pqnas::ResolvedExistingPath rp;
    std::string err;
    if (!pqnas::resolve_existing_user_file_path(users, fp_hex, path_rel, &rp, &err)) {
        if (err == "empty path" || err == "invalid path" || err == "absolute path not allowed" || err == "reserved path") {
            audit_fail("invalid_path", 400, err, path_rel);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "invalid path"}
            }.dump());
            return;
        }

        audit_fail("not_found", 404, err, path_rel);
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "file not found"}
        }.dump());
        return;
    }

    const std::filesystem::path path_abs = rp.abs_path;

    {
        std::string serr;
        if (!ensure_no_symlink_in_existing_path_prefix(path_abs, &serr)) {
            audit_fail("symlink_not_supported", 400, serr, path_rel);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "symlinks not supported"}
            }.dump());
            return;
        }
    }

    std::error_code ec;
    auto st = std::filesystem::symlink_status(path_abs, ec);
    if (ec || !std::filesystem::exists(st)) {
        audit_fail("not_found", 404, "", path_rel);
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "file not found"}
        }.dump());
        return;
    }

    if (std::filesystem::is_symlink(st)) {
        audit_fail("symlink_not_supported", 400, "", path_rel);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "symlinks not supported"}
        }.dump());
        return;
    }

    if (!std::filesystem::is_regular_file(st)) {
        audit_fail("not_file", 400, "", path_rel);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "not a file"}
        }.dump());
        return;
    }

    const std::uint64_t bytes = pqnas::file_size_u64_safe(path_abs);
    if (bytes > k_text_edit_max_bytes) {
        audit_fail("too_large", 413, std::to_string((unsigned long long)bytes), path_rel);
        reply_json(res, 413, json{
            {"ok", false},
            {"error", "too_large"},
            {"message", "file too large to edit in browser"},
            {"bytes", bytes},
            {"max_bytes", k_text_edit_max_bytes}
        }.dump());
        return;
    }

    std::string terr;
    if (!looks_like_text_no_nul_prefix(path_abs, &terr)) {
        audit_fail("not_text", 400, terr, path_rel);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "not_text"},
            {"message", "file does not look like text"}
        }.dump());
        return;
    }

    std::string raw;
    if (!read_file_bytes_all(path_abs, &raw, &err)) {
        audit_fail("read_failed", 500, err, path_rel);
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "read failed"},
            {"detail", pqnas::shorten(err, 180)}
        }.dump());
        return;
    }

    bool had_bom = false;
    std::string text = strip_utf8_bom(raw, &had_bom);
    if (!is_valid_utf8(text)) {
        audit_fail("decode_failed", 400, "invalid utf-8", path_rel);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "decode_failed"},
            {"message", "file is not valid UTF-8 text"}
        }.dump());
        return;
    }

    std::string digest_hex, herr;
    if (!sha256_file(path_abs, &digest_hex, &herr)) {
        audit_fail("hash_failed", 500, herr, path_rel);
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "hash failed"},
            {"detail", pqnas::shorten(herr, 180)}
        }.dump());
        return;
    }

    const std::uint64_t mtime_epoch = file_mtime_epoch_safe(path_abs);
    const std::string name = path_abs.filename().string();

    audit_ok(path_rel, bytes, mtime_epoch, digest_hex);

    reply_json(res, 200, json{
        {"ok", true},
        {"path", rp.normalized_rel_path},
        {"name", pqnas::shorten(name, 200)},
        {"mime", guess_text_mime_from_name(name)},
        {"encoding", "utf-8"},
        {"had_utf8_bom", had_bom},
        {"bytes", bytes},
        {"mtime_epoch", mtime_epoch},
        {"sha256", digest_hex},
        {"text", text}
    }.dump());
});

// POST /api/v4/files/write_text
srv.Post("/api/v4/files/write_text", [&](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;
    if (!require_same_origin_for_cookie_mutation(req, res)) return;
    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto add_ip_headers = [&](pqnas::AuditEvent& ev) {
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
    };

    auto audit_fail = [&](const std::string& reason, int http,
                          const std::string& detail = "",
                          const std::string& path_rel = "") {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_write_text_fail";
        ev.outcome = "fail";
        ev.f["fingerprint"] = fp_hex;
        ev.f["reason"] = reason;
        ev.f["http"] = std::to_string(http);
        if (!path_rel.empty()) ev.f["path"] = pqnas::shorten(path_rel, 200);
        if (!detail.empty()) ev.f["detail"] = pqnas::shorten(detail, 180);
        add_ip_headers(ev);
        audit_append(ev);
    };

    auto audit_ok = [&](const std::string& path_rel,
                        std::uint64_t bytes,
                        std::uint64_t mtime_epoch,
                        const std::string& sha256_hex) {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_write_text_ok";
        ev.outcome = "ok";
        ev.f["fingerprint"] = fp_hex;
        ev.f["path"] = pqnas::shorten(path_rel, 200);
        ev.f["bytes"] = std::to_string((unsigned long long)bytes);
        ev.f["mtime_epoch"] = std::to_string((unsigned long long)mtime_epoch);
        ev.f["sha256"] = pqnas::shorten(sha256_hex, 80);
        add_ip_headers(ev);
        audit_append(ev);
    };

    auto uopt = users.get(fp_hex);
    if (!uopt.has_value() || uopt->storage_state != "allocated") {
        audit_fail("storage_unallocated", 403);
        reply_json(res, 403, json{
            {"ok", false},
            {"error", "storage_unallocated"},
            {"message", "Storage not allocated"}
        }.dump());
        return;
    }

    json in = json::parse(req.body, nullptr, false);
    if (in.is_discarded() || !in.is_object()) {
        audit_fail("invalid_json", 400);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid json"}
        }.dump());
        return;
    }

    const std::string path_rel = in.value("path", "");
    const std::string text = in.value("text", "");
    const std::uint64_t expected_mtime_epoch =
        (in.contains("expected_mtime_epoch") && in["expected_mtime_epoch"].is_number_unsigned())
            ? in["expected_mtime_epoch"].get<std::uint64_t>()
            : 0;
    const std::string expected_sha256 = in.value("expected_sha256", "");

    if (path_rel.empty()) {
        audit_fail("missing_path", 400);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "missing path"}
        }.dump());
        return;
    }

    if (!is_valid_utf8(text)) {
        audit_fail("invalid_utf8", 400, "", path_rel);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "text must be valid UTF-8"}
        }.dump());
        return;
    }

    if ((std::uint64_t)text.size() > k_text_edit_max_bytes) {
        audit_fail("too_large", 413, std::to_string((unsigned long long)text.size()), path_rel);
        reply_json(res, 413, json{
            {"ok", false},
            {"error", "too_large"},
            {"message", "file too large to edit in browser"},
            {"bytes", (std::uint64_t)text.size()},
            {"max_bytes", k_text_edit_max_bytes}
        }.dump());
        return;
    }

    pqnas::ResolvedExistingPath rp;
    std::string err;
    if (!pqnas::resolve_existing_user_file_path(users, fp_hex, path_rel, &rp, &err)) {
        if (err == "empty path" || err == "invalid path" || err == "absolute path not allowed" || err == "reserved path") {
            audit_fail("invalid_path", 400, err, path_rel);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "invalid path"}
            }.dump());
            return;
        }

        audit_fail("not_found", 404, err, path_rel);
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "file not found"}
        }.dump());
        return;
    }

    const std::filesystem::path path_abs = rp.abs_path;

    {
        std::string serr;
        if (!ensure_no_symlink_in_existing_path_prefix(path_abs, &serr)) {
            audit_fail("symlink_not_supported", 400, serr, path_rel);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "symlinks not supported"}
            }.dump());
            return;
        }
    }

    std::error_code ec;
    auto st = std::filesystem::symlink_status(path_abs, ec);
    if (ec || !std::filesystem::exists(st)) {
        audit_fail("not_found", 404, "", path_rel);
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "file not found"}
        }.dump());
        return;
    }

    if (std::filesystem::is_symlink(st)) {
        audit_fail("symlink_not_supported", 400, "", path_rel);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "symlinks not supported"}
        }.dump());
        return;
    }

    if (!std::filesystem::is_regular_file(st)) {
        audit_fail("not_file", 400, "", path_rel);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "not a file"}
        }.dump());
        return;
    }

    const std::uint64_t old_bytes = pqnas::file_size_u64_safe(path_abs);
    if (old_bytes > k_text_edit_max_bytes) {
        audit_fail("too_large", 413, std::to_string((unsigned long long)old_bytes), path_rel);
        reply_json(res, 413, json{
            {"ok", false},
            {"error", "too_large"},
            {"message", "file too large to edit in browser"},
            {"bytes", old_bytes},
            {"max_bytes", k_text_edit_max_bytes}
        }.dump());
        return;
    }

    std::string terr;
    if (!looks_like_text_no_nul_prefix(path_abs, &terr)) {
        audit_fail("not_text", 400, terr, path_rel);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "not_text"},
            {"message", "file does not look like text"}
        }.dump());
        return;
    }

    const std::uint64_t current_mtime_epoch = file_mtime_epoch_safe(path_abs);

    std::string current_sha256, herr;
    if (!sha256_file(path_abs, &current_sha256, &herr)) {
        audit_fail("hash_failed", 500, herr, path_rel);
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "hash failed"},
            {"detail", pqnas::shorten(herr, 180)}
        }.dump());
        return;
    }

    if (expected_mtime_epoch != 0 && current_mtime_epoch != 0 && expected_mtime_epoch != current_mtime_epoch) {
        audit_fail("changed_on_server", 409, "mtime mismatch", path_rel);
        reply_json(res, 409, json{
            {"ok", false},
            {"error", "changed_on_server"},
            {"message", "file changed on server"},
            {"current_mtime_epoch", current_mtime_epoch},
            {"current_sha256", current_sha256}
        }.dump());
        return;
    }

    if (!expected_sha256.empty() && expected_sha256 != current_sha256) {
        audit_fail("changed_on_server", 409, "sha256 mismatch", path_rel);
        reply_json(res, 409, json{
            {"ok", false},
            {"error", "changed_on_server"},
            {"message", "file changed on server"},
            {"current_mtime_epoch", current_mtime_epoch},
            {"current_sha256", current_sha256}
        }.dump());
        return;
    }

    // Optional quota safety: text edit can increase file size.
    // Reuse upload quota model using overwrite semantics.
    {
        const std::filesystem::path user_dir = user_dir_for_fp(users, fp_hex);
        auto qc = pqnas::quota_check_for_upload_v1(
            users,
            fp_hex,
            user_dir,
            rp.normalized_rel_path,
            (std::uint64_t)text.size()
        );

        if (!qc.ok) {
            if (qc.error == "quota_exceeded") {
                audit_fail("quota_exceeded", 413, "", path_rel);
                reply_json(res, 413, json{
                    {"ok", false},
                    {"error", "quota_exceeded"},
                    {"message", "Quota exceeded"},
                    {"used_bytes", qc.used_bytes},
                    {"quota_bytes", qc.quota_bytes},
                    {"incoming_bytes", qc.incoming_bytes},
                    {"existing_bytes", qc.existing_bytes},
                    {"would_used_bytes", qc.would_used_bytes}
                }.dump());
                return;
            }

            audit_fail("quota_check_failed", 500, qc.error, path_rel);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "quota check failed"},
                {"detail", qc.error}
            }.dump());
            return;
        }
    }

    {
        std::string serr;
        if (!ensure_no_symlink_in_existing_path_prefix(path_abs, &serr)) {
            audit_fail("symlink_not_supported", 400, serr, path_rel);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "symlinks not supported"}
            }.dump());
            return;
        }
    }

    ec.clear();
    auto st2 = std::filesystem::symlink_status(path_abs, ec);
    if (ec || !std::filesystem::exists(st2)) {
        audit_fail("not_found", 404, "", path_rel);
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "file not found"}
        }.dump());
        return;
    }

    if (std::filesystem::is_symlink(st2)) {
        audit_fail("symlink_not_supported", 400, "", path_rel);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "symlinks not supported"}
        }.dump());
        return;
    }

    if (!std::filesystem::is_regular_file(st2)) {
        audit_fail("path_changed_before_write", 409, "", path_rel);
        reply_json(res, 409, json{
            {"ok", false},
            {"error", "path_conflict"},
            {"message", "path changed before write"}
        }.dump());
        return;
    }

    if (!write_text_file_atomic_utf8(path_abs, text, &err)) {
        audit_fail("write_failed", 500, err, path_rel);
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "write failed"},
            {"detail", pqnas::shorten(err, 180)}
        }.dump());
        return;
    }
    const std::uint64_t new_bytes = pqnas::file_size_u64_safe(path_abs);
    const std::uint64_t new_mtime_epoch = file_mtime_epoch_safe(path_abs);

    std::string new_sha256, herr2;
    if (!sha256_file(path_abs, &new_sha256, &herr2)) {
        audit_fail("hash_after_write_failed", 500, herr2, path_rel);
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "hash failed after write"},
            {"detail", pqnas::shorten(herr2, 180)}
        }.dump());
        return;
    }

    audit_ok(path_rel, new_bytes, new_mtime_epoch, new_sha256);

    reply_json(res, 200, json{
        {"ok", true},
        {"path", rp.normalized_rel_path},
        {"bytes", new_bytes},
        {"mtime_epoch", new_mtime_epoch},
        {"sha256", new_sha256}
    }.dump());
});

    // POST /api/v4/files/zip?path=rel/path&max_bytes=52428800
srv.Post("/api/v4/files/zip", [&](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;
    if (!require_same_origin_for_cookie_mutation(req, res)) return;
    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto add_ip_headers = [&](pqnas::AuditEvent& ev) {
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
    };

    auto audit_fail = [&](const std::string& reason, int http, const std::string& detail = "",
                          const std::string& path_rel = "", std::uint64_t max_bytes = 0) {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_zip_fail";
        ev.outcome = "fail";
        ev.f["fingerprint"] = fp_hex;
        ev.f["reason"] = reason;
        ev.f["http"] = std::to_string(http);
        if (!path_rel.empty()) ev.f["path"] = pqnas::shorten(path_rel, 200);
        if (max_bytes) ev.f["max_bytes"] = std::to_string((unsigned long long)max_bytes);
        if (!detail.empty()) ev.f["detail"] = pqnas::shorten(detail, 180);

        add_ip_headers(ev);
        audit_append(ev);
    };

    auto audit_ok = [&](const std::string& path_rel,
                        const std::string& type,
                        std::uint64_t max_bytes,
                        std::uint64_t input_bytes,
                        std::uint64_t zip_bytes,
                        std::uint64_t files,
                        std::uint64_t dirs) {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_zip_ok";
        ev.outcome = "ok";
        ev.f["fingerprint"] = fp_hex;
        ev.f["path"] = pqnas::shorten(path_rel, 200);
        ev.f["type"] = type;
        ev.f["max_bytes"] = std::to_string((unsigned long long)max_bytes);
        ev.f["input_bytes"] = std::to_string((unsigned long long)input_bytes);
        ev.f["zip_bytes"] = std::to_string((unsigned long long)zip_bytes);
        ev.f["files"] = std::to_string((unsigned long long)files);
        ev.f["dirs"]  = std::to_string((unsigned long long)dirs);

        add_ip_headers(ev);
        audit_append(ev);
    };

    // must have allocated storage
    auto uopt = users.get(fp_hex);
    if (!uopt.has_value() || uopt->storage_state != "allocated") {
        audit_fail("storage_unallocated", 403);
        reply_json(res, 403, json{
            {"ok", false},
            {"error", "storage_unallocated"},
            {"message", "Storage not allocated"}
        }.dump());
        return;
    }

    std::string path_rel;
    if (req.has_param("path")) path_rel = req.get_param_value("path");
    if (path_rel.empty()) {
        audit_fail("missing_path", 400);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "missing path"}
        }.dump());
        return;
    }

    // Safety: reject a leading '-' so it can't be treated as a zip option
    if (!path_rel.empty() && path_rel[0] == '-') {
        audit_fail("invalid_path", 400, "leading '-' refused", path_rel);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid path"}
        }.dump());
        return;
    }

    // max_bytes cap (controls RAM usage too)
    std::uint64_t max_bytes = 50ull * 1024 * 1024; // 50 MiB default
    if (req.has_param("max_bytes")) {
        try {
            long long v = std::stoll(req.get_param_value("max_bytes"));
            if (v > 0) max_bytes = (std::uint64_t)v;
        } catch (...) {}
    }
    const std::uint64_t MINB = 1ull * 1024 * 1024;       // 1 MiB
    const std::uint64_t MAXB = 250ull * 1024 * 1024;     // 250 MiB hard clamp
    if (max_bytes < MINB) max_bytes = MINB;
    if (max_bytes > MAXB) max_bytes = MAXB;

    const std::filesystem::path user_dir = user_dir_for_fp(users, fp_hex);

    std::filesystem::path path_abs;
    std::string err;
    if (!pqnas::resolve_user_path_strict(user_dir, path_rel, &path_abs, &err)) {
        audit_fail("invalid_path", 400, err, path_rel, max_bytes);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid path"}
        }.dump());
        return;
    }

    {
        std::string serr;
        if (!ensure_no_symlink_in_existing_path_prefix(path_abs, &serr)) {
            audit_fail("symlink_not_supported", 400, serr, path_rel, max_bytes);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "symlinks not supported for zip download"}
            }.dump());
            return;
        }
    }

    // must exist
    std::error_code ec;
    auto st = std::filesystem::symlink_status(path_abs, ec);
    if (ec || !std::filesystem::exists(st)) {
        audit_fail("not_found", 404, "", path_rel, max_bytes);
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "path not found"}
        }.dump());
        return;
    }

    // No symlinks in v1 (avoid surprises with zip behavior)
    if (std::filesystem::is_symlink(st)) {
        audit_fail("symlink_not_supported", 400, "", path_rel, max_bytes);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "symlinks not supported for zip download"}
        }.dump());
        return;
    }

    const bool is_file = std::filesystem::is_regular_file(st);
    const bool is_dir  = std::filesystem::is_directory(st);
    const std::string type = is_dir ? "dir" : (is_file ? "file" : "other");

    // Pre-walk to compute total bytes and ensure no symlinks inside
    std::uint64_t files = 0, dirs = 0, input_bytes = 0;

    if (is_file) {
        files = 1;
        input_bytes = pqnas::file_size_u64_safe(path_abs);
    } else if (is_dir) {
        dirs = 1; // include root dir

        std::filesystem::directory_options opts = std::filesystem::directory_options::skip_permission_denied;
        ec.clear();
        for (auto it = std::filesystem::recursive_directory_iterator(path_abs, opts, ec);
             it != std::filesystem::recursive_directory_iterator();
             it.increment(ec)) {

            if (ec) {
                audit_fail("walk_failed", 500, ec.message(), path_rel, max_bytes);
                reply_json(res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "directory walk failed"},
                    {"detail", pqnas::shorten(ec.message(), 180)}
                }.dump());
                return;
            }

            std::error_code ec2;
            auto st2 = std::filesystem::symlink_status(it->path(), ec2);
            if (ec2) continue;

            if (std::filesystem::is_symlink(st2)) {
                audit_fail("symlink_not_supported", 400, "symlink inside tree", path_rel, max_bytes);
                reply_json(res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "symlinks inside directory are not supported for zip download"}
                }.dump());
                return;
            }

            if (std::filesystem::is_directory(st2)) {
                dirs += 1;
                continue;
            }

            if (std::filesystem::is_regular_file(st2)) {
                files += 1;
                input_bytes += pqnas::file_size_u64_safe(it->path());
                if (input_bytes > max_bytes) break;
                continue;
            }

            // other file types count as “file-like” but 0 bytes
            files += 1;
        }
    } else {
        audit_fail("unsupported_type", 400, "", path_rel, max_bytes);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "unsupported path type for zip download"}
        }.dump());
        return;
    }

    if (input_bytes > max_bytes) {
        audit_fail("too_large", 413, "input exceeds max_bytes", path_rel, max_bytes);
        reply_json(res, 413, json{
            {"ok", false},
            {"error", "too_large"},
            {"message", "selected content exceeds max_bytes"}
        }.dump());
        return;
    }

    // Build zip using /usr/bin/zip and capture to memory (bounded).
    // We run: zip -r -q - <relpath>  (stdout is the zip)
    // in cwd=user_dir, so relpath stays inside user storage.
    int pipefd[2];
    if (::pipe(pipefd) != 0) {
        audit_fail("pipe_failed", 500, "pipe()", path_rel, max_bytes);
        reply_json(res, 500, json{{"ok",false},{"error","server_error"},{"message","zip failed"}}.dump());
        return;
    }

    pid_t pid = ::fork();
    if (pid < 0) {
        ::close(pipefd[0]); ::close(pipefd[1]);
        audit_fail("fork_failed", 500, "fork()", path_rel, max_bytes);
        reply_json(res, 500, json{{"ok",false},{"error","server_error"},{"message","zip failed"}}.dump());
        return;
    }

    if (pid == 0) {
        // child
        ::dup2(pipefd[1], STDOUT_FILENO);
        // keep STDERR separate

        ::close(pipefd[0]);
        ::close(pipefd[1]);

        if (::chdir(user_dir.c_str()) != 0) _exit(127);



        // args: zip -r -q - <path_rel>
        // Using "--" is supported by many zip builds, but to be safe we already reject leading '-'.
        const char* argv[] = {
            "zip",
            "-r",
            "-q",
            "-",
            path_rel.c_str(),
            nullptr
        };
        ::execvp("zip", (char* const*)argv);
        _exit(127);
    }

    // parent
    ::close(pipefd[1]);

    std::string zip_data;
    zip_data.reserve((size_t)std::min<std::uint64_t>(max_bytes, 4ull * 1024 * 1024));

    const std::uint64_t zip_limit = max_bytes + 8ull * 1024 * 1024; // allow some zip overhead
    std::array<char, 64 * 1024> buf{};
    while (true) {
        ssize_t n = ::read(pipefd[0], buf.data(), (ssize_t)buf.size());
        if (n == 0) break;
        if (n < 0) {
            ::close(pipefd[0]);
            ::kill(pid, SIGKILL);
            audit_fail("read_failed", 500, "read()", path_rel, max_bytes);
            reply_json(res, 500, json{{"ok",false},{"error","server_error"},{"message","zip failed"}}.dump());
            return;
        }

        if (zip_data.size() + (size_t)n > (size_t)zip_limit) {
            ::close(pipefd[0]);
            ::kill(pid, SIGKILL);
            audit_fail("zip_too_large", 413, "zip output exceeds limit", path_rel, max_bytes);
            reply_json(res, 413, json{
                {"ok", false},
                {"error", "too_large"},
                {"message", "zip output too large"}
            }.dump());
            return;
        }

        zip_data.append(buf.data(), (size_t)n);
    }
    ::close(pipefd[0]);

    int status = 0;
    ::waitpid(pid, &status, 0);
    if (!(WIFEXITED(status) && WEXITSTATUS(status) == 0)) {
        audit_fail("zip_failed", 500, "zip exit nonzero", path_rel, max_bytes);
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "zip failed"}
        }.dump());
        return;
    }

    // filename suggestion
    std::string base = std::filesystem::path(path_rel).filename().string();
    if (base.empty()) base = "download";
    std::string fname = base + ".zip";

    audit_ok(path_rel, type, max_bytes, input_bytes, (std::uint64_t)zip_data.size(), files, dirs);

    res.status = 200;
    res.set_header("Cache-Control", "no-store");
    res.set_header("Content-Type", "application/zip");
    res.set_header("Content-Disposition", build_content_disposition("attachment", fname));
    res.body = std::move(zip_data);
});

// POST /api/v4/files/zip_sel
// Body JSON: { "paths": ["rel/a.txt", "rel/dir", ...], "max_bytes": 52428800, "base": "rel/dir" }
// Response: application/zip (in-memory, bounded)
//
// Semantics:
// - paths[] are user-root-relative (same as other v4 file endpoints).
// - If "base" is provided, we chdir() into user_dir/base and feed zip with paths
//   stripped to be relative to base. This avoids the annoying "selection/<cwd>/..."
//   nesting when user selects files from inside a folder.
srv.Post("/api/v4/files/zip_sel", [&](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;

    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto add_ip_headers = [&](pqnas::AuditEvent& ev) {
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
    };

    auto audit_fail = [&](const std::string& reason, int http, const std::string& detail = "",
                          std::uint64_t max_bytes = 0, std::uint64_t paths_n = 0) {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_zip_sel_fail";
        ev.outcome = "fail";
        ev.f["fingerprint"] = fp_hex;
        ev.f["reason"] = reason;
        ev.f["http"] = std::to_string(http);
        if (max_bytes) ev.f["max_bytes"] = std::to_string((unsigned long long)max_bytes);
        if (paths_n)  ev.f["paths_n"]  = std::to_string((unsigned long long)paths_n);
        if (!detail.empty()) ev.f["detail"] = pqnas::shorten(detail, 180);

        add_ip_headers(ev);
        audit_append(ev);
    };

    auto audit_ok = [&](std::uint64_t max_bytes,
                        std::uint64_t input_bytes,
                        std::uint64_t zip_bytes,
                        std::uint64_t files,
                        std::uint64_t dirs,
                        std::uint64_t paths_n,
                        const std::string& base_rel) {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_zip_sel_ok";
        ev.outcome = "ok";
        ev.f["fingerprint"] = fp_hex;
        ev.f["max_bytes"] = std::to_string((unsigned long long)max_bytes);
        ev.f["input_bytes"] = std::to_string((unsigned long long)input_bytes);
        ev.f["zip_bytes"] = std::to_string((unsigned long long)zip_bytes);
        ev.f["files"] = std::to_string((unsigned long long)files);
        ev.f["dirs"]  = std::to_string((unsigned long long)dirs);
        ev.f["paths_n"] = std::to_string((unsigned long long)paths_n);
        if (!base_rel.empty()) ev.f["base"] = pqnas::shorten(base_rel, 200);

        add_ip_headers(ev);
        audit_append(ev);
    };

    // must have allocated storage
    auto uopt = users.get(fp_hex);
    if (!uopt.has_value() || uopt->storage_state != "allocated") {
        audit_fail("storage_unallocated", 403);
        reply_json(res, 403, json{
            {"ok", false},
            {"error", "storage_unallocated"},
            {"message", "Storage not allocated"}
        }.dump());
        return;
    }

    // Parse JSON body
    json body;
    try {
        body = json::parse(req.body.empty() ? "{}" : req.body);
    } catch (const std::exception& e) {
        audit_fail("json_parse", 400, e.what());
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid json"}
        }.dump());
        return;
    }

    if (!body.is_object()) {
        audit_fail("json_schema", 400, "body must be object");
        reply_json(res, 400, json{{"ok",false},{"error","bad_request"},{"message","invalid json schema"}}.dump());
        return;
    }

    if (!body.contains("paths") || !body["paths"].is_array()) {
        audit_fail("missing_paths", 400);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "missing paths[]"}
        }.dump());
        return;
    }

    // Optional base folder: when provided, zip entries are stored relative to base.
    // Example: base="test" and paths=["test/note.txt"] -> zip entry "note.txt".
    std::string base_rel;
    if (body.contains("base") && body["base"].is_string()) {
        base_rel = body["base"].get<std::string>();

        // normalize slashes + trim leading slashes
        for (char& c : base_rel) if (c == '\\') c = '/';
        while (!base_rel.empty() && base_rel[0] == '/') base_rel.erase(base_rel.begin());

        // strip duplicate slashes
        {
            std::string tmp;
            tmp.reserve(base_rel.size());
            bool prev_slash = false;
            for (char c : base_rel) {
                if (c == '/') {
                    if (prev_slash) continue;
                    prev_slash = true;
                    tmp.push_back(c);
                } else {
                    prev_slash = false;
                    tmp.push_back(c);
                }
            }
            base_rel.swap(tmp);
        }

        // remove trailing slash
        while (!base_rel.empty() && base_rel.back() == '/') base_rel.pop_back();

        // reject unsafe base
        bool bad = false;
        if (!base_rel.empty() && base_rel[0] == '-') bad = true;
        if (base_rel.find('\n') != std::string::npos || base_rel.find('\r') != std::string::npos) bad = true;

        // reject traversal segments
        if (!bad && !base_rel.empty()) {
            size_t start = 0;
            while (start < base_rel.size()) {
                size_t end = base_rel.find('/', start);
                if (end == std::string::npos) end = base_rel.size();
                std::string seg = base_rel.substr(start, end - start);
                if (seg == "." || seg == ".." || seg.empty()) { bad = true; break; }
                start = end + 1;
            }
        }

        if (bad) base_rel.clear();
    }

    // max_bytes cap (controls RAM usage too)
    std::uint64_t max_bytes = 50ull * 1024 * 1024; // 50 MiB default
    if (body.contains("max_bytes")) {
        try {
            long long v = 0;
            if (body["max_bytes"].is_number_integer()) v = body["max_bytes"].get<long long>();
            else if (body["max_bytes"].is_string()) v = std::stoll(body["max_bytes"].get<std::string>());
            if (v > 0) max_bytes = (std::uint64_t)v;
        } catch (...) {}
    }
    const std::uint64_t MINB = 1ull * 1024 * 1024;       // 1 MiB
    const std::uint64_t MAXB = 250ull * 1024 * 1024;     // 250 MiB hard clamp
    if (max_bytes < MINB) max_bytes = MINB;
    if (max_bytes > MAXB) max_bytes = MAXB;

    // Collect + basic sanitize
    std::vector<std::string> paths_in;
    paths_in.reserve(body["paths"].size());

    for (const auto& it : body["paths"]) {
        if (!it.is_string()) continue;
        std::string p = it.get<std::string>();

        // normalize slashes + trim leading slashes
        for (char& c : p) if (c == '\\') c = '/';
        while (!p.empty() && p[0] == '/') p.erase(p.begin());

        // strip duplicate slashes
        {
            std::string tmp;
            tmp.reserve(p.size());
            bool prev_slash = false;
            for (char c : p) {
                if (c == '/') {
                    if (prev_slash) continue;
                    prev_slash = true;
                    tmp.push_back(c);
                } else {
                    prev_slash = false;
                    tmp.push_back(c);
                }
            }
            p.swap(tmp);
        }

        // remove trailing slash (we still treat dir fine)
        while (p.size() > 1 && p.back() == '/') p.pop_back();

        if (p.empty()) continue;

        // Safety: reject leading '-' so it can't be treated as an option by zip
        if (!p.empty() && p[0] == '-') continue;

        // Safety: reject traversal segments
        bool bad = false;
        {
            size_t start = 0;
            while (start < p.size()) {
                size_t end = p.find('/', start);
                if (end == std::string::npos) end = p.size();
                std::string seg = p.substr(start, end - start);
                if (seg == "." || seg == ".." || seg.empty()) { bad = true; break; }
                start = end + 1;
            }
        }
        if (bad) continue;

        // prevent CR/LF injection into -@ stdin list
        if (p.find('\n') != std::string::npos || p.find('\r') != std::string::npos) continue;

        paths_in.push_back(std::move(p));
    }

    // Hard cap selection count to avoid abuse
    const std::size_t MAX_PATHS = 500;
    if (paths_in.empty()) {
        audit_fail("no_valid_paths", 400, "", max_bytes, 0);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "no valid paths"}
        }.dump());
        return;
    }
    if (paths_in.size() > MAX_PATHS) {
        audit_fail("too_many_paths", 413, "paths[] too large", max_bytes, (std::uint64_t)paths_in.size());
        reply_json(res, 413, json{
            {"ok", false},
            {"error", "too_large"},
            {"message", "too many selected paths"}
        }.dump());
        return;
    }

    // Dedupe + drop children if parent dir is selected
    std::sort(paths_in.begin(), paths_in.end());
    paths_in.erase(std::unique(paths_in.begin(), paths_in.end()), paths_in.end());

    std::vector<std::string> paths_rel;
    paths_rel.reserve(paths_in.size());

    auto is_child_of = [&](const std::string& child, const std::string& parent) -> bool {
        if (child.size() <= parent.size()) return false;
        if (child.compare(0, parent.size(), parent) != 0) return false;
        return child[parent.size()] == '/';
    };

    for (const auto& p : paths_in) {
        if (paths_rel.empty()) {
            paths_rel.push_back(p);
            continue;
        }
        bool covered = false;
        for (const auto& sel : paths_rel) {
            if (is_child_of(p, sel)) { covered = true; break; }
        }
        if (!covered) paths_rel.push_back(p);
    }

    const std::filesystem::path user_dir = user_dir_for_fp(users, fp_hex);

    // If base is provided, ensure it's a real directory under user_dir (fail-closed).
    std::filesystem::path base_abs;
    if (!base_rel.empty()) {
        std::string berr;
        if (!pqnas::resolve_user_path_strict(user_dir, base_rel, &base_abs, &berr)) {
            audit_fail("invalid_base", 400, berr, max_bytes, (std::uint64_t)paths_rel.size());
            reply_json(res, 400, json{{"ok",false},{"error","bad_request"},{"message","invalid base"}}.dump());
            return;
        }

        {
            std::string serr;
            if (!ensure_no_symlink_in_existing_path_prefix(base_abs, &serr)) {
                audit_fail("invalid_base", 400, serr, max_bytes, (std::uint64_t)paths_rel.size());
                reply_json(res, 400, json{{"ok",false},{"error","bad_request"},{"message","invalid base"}}.dump());
                return;
            }
        }

        std::error_code bec;
        auto bst = std::filesystem::symlink_status(base_abs, bec);
        if (bec || !std::filesystem::exists(bst) || !std::filesystem::is_directory(bst) || std::filesystem::is_symlink(bst)) {
            audit_fail("invalid_base", 400, "base must be an existing directory", max_bytes, (std::uint64_t)paths_rel.size());
            reply_json(res, 400, json{{"ok",false},{"error","bad_request"},{"message","invalid base"}}.dump());
            return;
        }
    }

    // If base is set, require all selected paths to be within base.
    // This prevents weird results when we chdir into base but zip tries to access paths outside it.
    if (!base_rel.empty()) {
        for (const auto& p : paths_rel) {
            if (p == base_rel) continue;
            if (!is_child_of(p, base_rel)) {
                audit_fail("path_outside_base", 400, pqnas::shorten(p, 180), max_bytes, (std::uint64_t)paths_rel.size());
                reply_json(res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "selected path outside base"}
                }.dump());
                return;
            }
        }
    }

    // Resolve + pre-walk all selections (size + symlink checks)
    std::uint64_t files = 0, dirs = 0, input_bytes = 0;

    for (const auto& path_rel : paths_rel) {
        std::filesystem::path path_abs;
        std::string err;
        if (!pqnas::resolve_user_path_strict(user_dir, path_rel, &path_abs, &err)) {
            audit_fail("invalid_path", 400, err, max_bytes, (std::uint64_t)paths_rel.size());
            reply_json(res, 400, json{{"ok",false},{"error","bad_request"},{"message","invalid path"}}.dump());
            return;
        }

        std::error_code ec;
        {
        std::string serr;
        if (!ensure_no_symlink_in_existing_path_prefix(path_abs, &serr)) {
            audit_fail("symlink_not_supported", 400, serr, max_bytes, (std::uint64_t)paths_rel.size());
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "symlinks not supported for zip download"}
            }.dump());
            return;
        }
    }
        auto st = std::filesystem::symlink_status(path_abs, ec);
        if (ec || !std::filesystem::exists(st)) {
            audit_fail("not_found", 404, path_rel, max_bytes, (std::uint64_t)paths_rel.size());
            reply_json(res, 404, json{{"ok",false},{"error","not_found"},{"message","path not found"}}.dump());
            return;
        }

        if (std::filesystem::is_symlink(st)) {
            audit_fail("symlink_not_supported", 400, "symlink selected", max_bytes, (std::uint64_t)paths_rel.size());
            reply_json(res, 400, json{{"ok",false},{"error","bad_request"},{"message","symlinks not supported for zip download"}}.dump());
            return;
        }

        const bool is_file = std::filesystem::is_regular_file(st);
        const bool is_dir  = std::filesystem::is_directory(st);

        if (is_file) {
            files += 1;
            input_bytes += pqnas::file_size_u64_safe(path_abs);
            if (input_bytes > max_bytes) break;
            continue;
        }

        if (is_dir) {
            dirs += 1; // include selected root dir

            std::filesystem::directory_options opts = std::filesystem::directory_options::skip_permission_denied;
            ec.clear();
            for (auto it = std::filesystem::recursive_directory_iterator(path_abs, opts, ec);
                 it != std::filesystem::recursive_directory_iterator();
                 it.increment(ec)) {

                if (ec) {
                    audit_fail("walk_failed", 500, ec.message(), max_bytes, (std::uint64_t)paths_rel.size());
                    reply_json(res, 500, json{
                        {"ok", false},
                        {"error", "server_error"},
                        {"message", "directory walk failed"},
                        {"detail", pqnas::shorten(ec.message(), 180)}
                    }.dump());
                    return;
                }

                std::error_code ec2;
                auto st2 = std::filesystem::symlink_status(it->path(), ec2);
                if (ec2) continue;

                if (std::filesystem::is_symlink(st2)) {
                    audit_fail("symlink_not_supported", 400, "symlink inside tree", max_bytes, (std::uint64_t)paths_rel.size());
                    reply_json(res, 400, json{
                        {"ok", false},
                        {"error", "bad_request"},
                        {"message", "symlinks inside directory are not supported for zip download"}
                    }.dump());
                    return;
                }

                if (std::filesystem::is_regular_file(st2)) {
                    files += 1;
                    input_bytes += pqnas::file_size_u64_safe(it->path());
                    if (input_bytes > max_bytes) break;
                    continue;
                }

                files += 1; // other types, 0 bytes
            }
            if (input_bytes > max_bytes) break;
            continue;
        }

        audit_fail("unsupported_type", 400, path_rel, max_bytes, (std::uint64_t)paths_rel.size());
        reply_json(res, 400, json{{"ok",false},{"error","bad_request"},{"message","unsupported path type for zip download"}}.dump());
        return;
    }

    if (input_bytes > max_bytes) {
        audit_fail("too_large", 413, "input exceeds max_bytes", max_bytes, (std::uint64_t)paths_rel.size());
        reply_json(res, 413, json{{"ok",false},{"error","too_large"},{"message","selected content exceeds max_bytes"}}.dump());
        return;
    }

    // Run: zip -r -q - -@ (read file list from stdin, write zip to stdout)
    int out_pipe[2] = {-1, -1};
    int in_pipe[2]  = {-1, -1};
    if (::pipe(out_pipe) != 0 || ::pipe(in_pipe) != 0) {
        if (out_pipe[0] >= 0) { ::close(out_pipe[0]); ::close(out_pipe[1]); }
        if (in_pipe[0]  >= 0) { ::close(in_pipe[0]);  ::close(in_pipe[1]);  }
        audit_fail("pipe_failed", 500, "pipe()", max_bytes, (std::uint64_t)paths_rel.size());
        reply_json(res, 500, json{{"ok",false},{"error","server_error"},{"message","zip failed"}}.dump());
        return;
    }

    pid_t pid = ::fork();
    if (pid < 0) {
        ::close(out_pipe[0]); ::close(out_pipe[1]);
        ::close(in_pipe[0]);  ::close(in_pipe[1]);
        audit_fail("fork_failed", 500, "fork()", max_bytes, (std::uint64_t)paths_rel.size());
        reply_json(res, 500, json{{"ok",false},{"error","server_error"},{"message","zip failed"}}.dump());
        return;
    }

    if (pid == 0) {
        // child
        ::dup2(in_pipe[0], STDIN_FILENO);
        ::dup2(out_pipe[1], STDOUT_FILENO);

        ::close(out_pipe[0]);
        ::close(out_pipe[1]);
        ::close(in_pipe[0]);
        ::close(in_pipe[1]);

        if (!base_rel.empty()) {
            std::filesystem::path cd = user_dir / base_rel;
            if (::chdir(cd.c_str()) != 0) _exit(127);
        } else {
            if (::chdir(user_dir.c_str()) != 0) _exit(127);
        }

        const char* argv[] = {
            "zip",
            "-r",
            "-q",
            "-",
            "-@",
            nullptr
        };
        ::execvp("zip", (char* const*)argv);
        _exit(127);
    }

    // parent
    ::close(out_pipe[1]); // read zip from out_pipe[0]
    ::close(in_pipe[0]);  // write list to in_pipe[1]

    // feed paths list
    {
        auto starts_with_dir = [](const std::string& p, const std::string& base) -> bool {
            if (base.empty()) return false;
            if (p.size() <= base.size()) return false;
            if (p.compare(0, base.size(), base) != 0) return false;
            return p[base.size()] == '/';
        };

        bool write_ok = true;
        for (const auto& p0 : paths_rel) {
            std::string p = p0;

            // When chdir() into base, feed zip paths relative to base.
            if (!base_rel.empty()) {
                if (p0 == base_rel) {
                    p = "."; // zip the current directory (the base itself)
                } else if (starts_with_dir(p0, base_rel)) {
                    p = p0.substr(base_rel.size() + 1);
                } else {
                    // should be impossible due to earlier validation
                    write_ok = false;
                }
            }

            if (!write_ok) break;

            if (p.empty()) p = "."; // never send empty line

            std::string line = p;
            line.push_back('\n');

            const char* data = line.data();
            size_t left = line.size();
            while (left > 0) {
                ssize_t n = ::write(in_pipe[1], data, (ssize_t)left);
                if (n <= 0) { write_ok = false; break; }
                data += n;
                left -= (size_t)n;
            }
            if (!write_ok) break;
        }

        ::close(in_pipe[1]);

        if (!write_ok) {
            ::close(out_pipe[0]);
            ::kill(pid, SIGKILL);
            audit_fail("write_failed", 500, "write(stdin)", max_bytes, (std::uint64_t)paths_rel.size());
            reply_json(res, 500, json{{"ok",false},{"error","server_error"},{"message","zip failed"}}.dump());
            return;
        }
    }

    std::string zip_data;
    zip_data.reserve((size_t)std::min<std::uint64_t>(max_bytes, 4ull * 1024 * 1024));

    const std::uint64_t zip_limit = max_bytes + 8ull * 1024 * 1024; // allow some zip overhead
    std::array<char, 64 * 1024> buf{};
    while (true) {
        ssize_t n = ::read(out_pipe[0], buf.data(), (ssize_t)buf.size());
        if (n == 0) break;
        if (n < 0) {
            ::close(out_pipe[0]);
            ::kill(pid, SIGKILL);
            audit_fail("read_failed", 500, "read(zip)", max_bytes, (std::uint64_t)paths_rel.size());
            reply_json(res, 500, json{{"ok",false},{"error","server_error"},{"message","zip failed"}}.dump());
            return;
        }

        if (zip_data.size() + (size_t)n > (size_t)zip_limit) {
            ::close(out_pipe[0]);
            ::kill(pid, SIGKILL);
            audit_fail("zip_too_large", 413, "zip output exceeds limit", max_bytes, (std::uint64_t)paths_rel.size());
            reply_json(res, 413, json{{"ok",false},{"error","too_large"},{"message","zip output too large"}}.dump());
            return;
        }

        zip_data.append(buf.data(), (size_t)n);
    }
    ::close(out_pipe[0]);

    int st = 0;
    ::waitpid(pid, &st, 0);
    if (!(WIFEXITED(st) && WEXITSTATUS(st) == 0)) {
        audit_fail("zip_failed", 500, "zip exit nonzero", max_bytes, (std::uint64_t)paths_rel.size());
        reply_json(res, 500, json{{"ok",false},{"error","server_error"},{"message","zip failed"}}.dump());
        return;
    }

    // filename suggestion
    const std::string fname = "selection.zip";

    audit_ok(max_bytes, input_bytes, (std::uint64_t)zip_data.size(), files, dirs, (std::uint64_t)paths_rel.size(), base_rel);

    res.status = 200;
    res.set_header("Cache-Control", "no-store");
    res.set_header("Content-Type", "application/zip");
    res.set_header("Content-Disposition", build_content_disposition("attachment", fname));
    res.body = std::move(zip_data);
});


    // POST /api/v4/files/rmrf?path=rel/path
srv.Post("/api/v4/files/rmrf", [&](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;

    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto add_ip_headers = [&](pqnas::AuditEvent& ev) {
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
    };

    auto audit_fail = [&](const std::string& reason, int http, const std::string& detail = "",
                          const std::string& path_rel = "") {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_rmrf_fail";
        ev.outcome = "fail";
        ev.f["fingerprint"] = fp_hex;
        ev.f["reason"] = reason;
        ev.f["http"] = std::to_string(http);
        if (!path_rel.empty()) ev.f["path"] = pqnas::shorten(path_rel, 200);
        if (!detail.empty())   ev.f["detail"] = pqnas::shorten(detail, 180);

        add_ip_headers(ev);
        audit_append(ev);
    };

    auto audit_ok = [&](const std::string& path_rel,
                        const std::string& type,
                        std::uint64_t removed_files,
                        std::uint64_t removed_dirs,
                        std::uint64_t removed_bytes) {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_rmrf_ok";
        ev.outcome = "ok";
        ev.f["fingerprint"] = fp_hex;
        ev.f["path"] = pqnas::shorten(path_rel, 200);
        ev.f["type"] = type;
        ev.f["removed_files"] = std::to_string((unsigned long long)removed_files);
        ev.f["removed_dirs"]  = std::to_string((unsigned long long)removed_dirs);
        ev.f["removed_bytes"] = std::to_string((unsigned long long)removed_bytes);

        add_ip_headers(ev);
        audit_append(ev);
    };

    // must have allocated storage
    auto uopt = users.get(fp_hex);
    if (!uopt.has_value() || uopt->storage_state != "allocated") {
        audit_fail("storage_unallocated", 403);
        reply_json(res, 403, json{
            {"ok", false},
            {"error", "storage_unallocated"},
            {"message", "Storage not allocated"}
        }.dump());
        return;
    }

    std::string path_rel;
    if (req.has_param("path")) path_rel = req.get_param_value("path");

    if (path_rel.empty()) {
        audit_fail("missing_path", 400);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "missing path"}
        }.dump());
        return;
    }

    // extra safety: refuse root-ish deletes
    auto norm = path_rel;
    while (!norm.empty() && (norm.back() == ' ' || norm.back() == '\t' || norm.back() == '\n' || norm.back() == '\r'))
        norm.pop_back();
    while (!norm.empty() && (norm.front() == ' ' || norm.front() == '\t' || norm.front() == '\n' || norm.front() == '\r'))
        norm.erase(norm.begin());

    if (norm.empty() || norm == "." || norm == "/" || norm == "./") {
        audit_fail("refuse_root", 400, "", path_rel);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "refusing to delete root"}
        }.dump());
        return;
    }

const std::filesystem::path user_dir = user_dir_for_fp(users, fp_hex);

// Serialize writes on overlapping logical paths for this user.
auto* plm = pqnas::get_path_lock_manager();
auto write_guard = plm->lock_paths(fp_hex, {norm});

pqnas::ResolvedLogicalItem item;
std::string rerr;
if (!pqnas::resolve_existing_user_item(users, fp_hex, norm, &item, &rerr) || !item.exists) {
    audit_fail("not_found", 404, rerr, path_rel);
    reply_json(res, 404, json{
        {"ok", false},
        {"error", "not_found"},
        {"message", "path not found"}
    }.dump());
    return;
}

auto* idx = pqnas::get_file_location_index();
if (!idx) {
    audit_fail("metadata_index_missing", 500, "", path_rel);
    reply_json(res, 500, json{
        {"ok", false},
        {"error", "server_error"},
        {"message", "metadata index missing"}
    }.dump());
    return;
}

auto file_size_safe = [&](const std::filesystem::path& p) -> std::uint64_t {
    std::error_code ec;
    auto sz = std::filesystem::file_size(p, ec);
    return ec ? 0 : static_cast<std::uint64_t>(sz);
};

auto count_logical_dirs_under_prefix = [&](const std::string& rel_norm,
                                           const std::vector<pqnas::FileLocationRecord>& rows) -> std::uint64_t {
    std::set<std::string> dirs;
    dirs.insert(rel_norm); // count root dir itself

    const std::string prefix = rel_norm + "/";

    for (const auto& rec : rows) {
        const std::string& lp = rec.logical_rel_path;
        if (lp.size() < prefix.size()) continue;
        if (lp.rfind(prefix, 0) != 0) continue;

        std::string rest = lp.substr(prefix.size());
        std::string acc = rel_norm;

        std::size_t pos = 0;
        while (true) {
            std::size_t slash = rest.find('/', pos);
            if (slash == std::string::npos) break;

            std::string part = rest.substr(pos, slash - pos);
            if (!part.empty()) {
                acc += "/" + part;
                dirs.insert(acc);
            }
            pos = slash + 1;
        }
    }

    return static_cast<std::uint64_t>(dirs.size());
};

if (item.is_file) {
    if (!item.has_physical_anchor) {
        audit_fail("file_missing_physical_anchor", 500, "", path_rel);
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "file missing physical anchor"}
        }.dump());
        return;
    }

    const std::filesystem::path abs_path = item.abs_path;

    if (abs_path == user_dir) {
        audit_fail("refuse_root_delete", 400, "", path_rel);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "refusing to delete root"}
        }.dump());
        return;
    }

    std::error_code ec;
    auto st = std::filesystem::symlink_status(abs_path, ec);
    if (ec || !std::filesystem::exists(st)) {
        audit_fail("not_found", 404, "", path_rel);
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "path not found"}
        }.dump());
        return;
    }

    if (std::filesystem::is_symlink(st)) {
        audit_fail("symlink_not_supported", 400, "", path_rel);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "symlinks not supported"}
        }.dump());
        return;
    }

    if (!std::filesystem::is_regular_file(st)) {
        audit_fail("path_not_file", 400, "", path_rel);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "path is not a file"}
        }.dump());
        return;
    }

    const std::uint64_t removed_bytes = file_size_safe(abs_path);

    // Recheck right before remove so we do not delete after type swap.
    ec.clear();
    auto st2 = std::filesystem::symlink_status(abs_path, ec);
    if (ec || !std::filesystem::exists(st2)) {
        audit_fail("not_found", 404, "", path_rel);
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "path not found"}
        }.dump());
        return;
    }

    if (std::filesystem::is_symlink(st2)) {
        audit_fail("symlink_not_supported", 400, "", path_rel);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "symlinks not supported"}
        }.dump());
        return;
    }

    if (!std::filesystem::is_regular_file(st2)) {
        audit_fail("path_changed_before_delete", 409, "", path_rel);
        reply_json(res, 409, json{
            {"ok", false},
            {"error", "path_conflict"},
            {"message", "path changed before delete"}
        }.dump());
        return;
    }

    bool removed = std::filesystem::remove(abs_path, ec);
    if (ec || !removed) {
        audit_fail("remove_failed", 500, ec.message(), path_rel);
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "recursive delete failed"},
            {"detail", pqnas::shorten(ec.message(), 180)}
        }.dump());
        return;
    }

    {
        std::string derr;
        if (!idx->erase(fp_hex, norm, &derr)) {
            audit_fail("metadata_erase_failed", 500, derr, path_rel);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "delete metadata cleanup failed"},
                {"detail", pqnas::shorten(derr, 180)}
            }.dump());
            return;
        }
    }

    {
        std::string ferr;
        if (!pqnas::favorites_remove_under_prefix(user_dir, norm, "file", &ferr)) {
            pqnas::AuditEvent ev;
            ev.event = "v4.files_rmrf_warn";
            ev.outcome = "warn";
            ev.f["fingerprint"] = fp_hex;
            ev.f["reason"] = "favorites_cleanup_failed";
            ev.f["path"] = pqnas::shorten(norm, 200);
            ev.f["detail"] = pqnas::shorten(ferr, 180);
            add_ip_headers(ev);
            audit_append(ev);
        }
    }

    audit_ok(path_rel, "file", 1, 0, removed_bytes);
    reply_json(res, 200, json{
        {"ok", true},
        {"path", path_rel},
        {"type", "file"},
        {"removed_files", 1},
        {"removed_dirs", 0},
        {"removed_bytes", removed_bytes}
    }.dump());
    return;
}

if (item.is_dir) {
    std::string lerr;
    auto subtree_rows = idx->list_subtree_records(fp_hex, norm, &lerr);
    if (!lerr.empty()) {
        audit_fail("metadata_subtree_list_failed", 500, lerr, path_rel);
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "failed to list metadata subtree"},
            {"detail", pqnas::shorten(lerr, 180)}
        }.dump());
        return;
    }

    // Legacy physical dir with no metadata subtree
    if (subtree_rows.empty()) {
        if (!item.has_physical_anchor) {
            audit_fail("not_found", 404, "", path_rel);
            reply_json(res, 404, json{
                {"ok", false},
                {"error", "not_found"},
                {"message", "path not found"}
            }.dump());
            return;
        }

        const std::filesystem::path abs_path = item.abs_path;
        if (abs_path == user_dir) {
            audit_fail("refuse_root_delete", 400, "", path_rel);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "refusing to delete user storage root"}
            }.dump());
            return;
        }

        std::uint64_t removed_files = 0;
        std::uint64_t removed_dirs = 1;
        std::uint64_t removed_bytes = 0;

        std::error_code ec;

        auto root_st = std::filesystem::symlink_status(abs_path, ec);
        if (ec || !std::filesystem::exists(root_st)) {
            audit_fail("not_found", 404, "", path_rel);
            reply_json(res, 404, json{
                {"ok", false},
                {"error", "not_found"},
                {"message", "path not found"}
            }.dump());
            return;
        }

        if (std::filesystem::is_symlink(root_st)) {
            audit_fail("symlink_not_supported", 400, "", path_rel);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "symlinks not supported"}
            }.dump());
            return;
        }

        if (!std::filesystem::is_directory(root_st)) {
            audit_fail("path_not_dir", 400, "", path_rel);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "path is not a directory"}
            }.dump());
            return;
        }

        for (std::filesystem::recursive_directory_iterator it(
                 abs_path, std::filesystem::directory_options::skip_permission_denied, ec), end;
             it != end && !ec;
             it.increment(ec)) {

            std::error_code ec2;
            auto st2 = std::filesystem::symlink_status(it->path(), ec2);
            if (ec2) {
                audit_fail("tree_stat_failed", 500, ec2.message(), path_rel);
                reply_json(res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "recursive delete scan failed"},
                    {"detail", pqnas::shorten(ec2.message(), 180)}
                }.dump());
                return;
            }

            if (std::filesystem::is_symlink(st2)) {
                audit_fail("tree_contains_symlink", 409, it->path().string(), path_rel);
                reply_json(res, 409, json{
                    {"ok", false},
                    {"error", "path_conflict"},
                    {"message", "directory contains symlinks"}
                }.dump());
                return;
            }

            if (std::filesystem::is_directory(st2)) {
                removed_dirs += 1;
                continue;
            }
            if (std::filesystem::is_regular_file(st2)) {
                removed_files += 1;
                removed_bytes += file_size_safe(it->path());
                continue;
            }
            removed_files += 1;
        }

        if (ec) {
            audit_fail("tree_walk_failed", 500, ec.message(), path_rel);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "recursive delete scan failed"},
                {"detail", pqnas::shorten(ec.message(), 180)}
            }.dump());
            return;
        }

        // Recheck root right before remove_all.
        ec.clear();
        auto root_st2 = std::filesystem::symlink_status(abs_path, ec);
        if (ec || !std::filesystem::exists(root_st2)) {
            audit_fail("not_found", 404, "", path_rel);
            reply_json(res, 404, json{
                {"ok", false},
                {"error", "not_found"},
                {"message", "path not found"}
            }.dump());
            return;
        }

        if (std::filesystem::is_symlink(root_st2)) {
            audit_fail("symlink_not_supported", 400, "", path_rel);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "symlinks not supported"}
            }.dump());
            return;
        }

        if (!std::filesystem::is_directory(root_st2)) {
            audit_fail("path_changed_before_delete", 409, "", path_rel);
            reply_json(res, 409, json{
                {"ok", false},
                {"error", "path_conflict"},
                {"message", "path changed before delete"}
            }.dump());
            return;
        }

        std::uintmax_t removed = std::filesystem::remove_all(abs_path, ec);
        (void)removed;
        if (ec) {
            audit_fail("remove_all_failed", 500, ec.message(), path_rel);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "recursive delete failed"},
                {"detail", pqnas::shorten(ec.message(), 180)}
            }.dump());
            return;
        }

        {
            std::string ferr;
            if (!pqnas::favorites_remove_under_prefix(user_dir, norm, "dir", &ferr)) {
                pqnas::AuditEvent ev;
                ev.event = "v4.files_rmrf_warn";
                ev.outcome = "warn";
                ev.f["fingerprint"] = fp_hex;
                ev.f["reason"] = "favorites_cleanup_failed";
                ev.f["path"] = pqnas::shorten(norm, 200);
                ev.f["detail"] = pqnas::shorten(ferr, 180);
                add_ip_headers(ev);
                audit_append(ev);
            }
        }

        audit_ok(path_rel, "dir", removed_files, removed_dirs, removed_bytes);
        reply_json(res, 200, json{
            {"ok", true},
            {"path", path_rel},
            {"type", "dir"},
            {"removed_files", removed_files},
            {"removed_dirs", removed_dirs},
            {"removed_bytes", removed_bytes}
        }.dump());
        return;
    }

    std::filesystem::path dir_abs;
    bool have_root = false;

    const auto& first = subtree_rows.front();
    std::filesystem::path root = std::filesystem::path(first.physical_path);
    for (const auto& _ : std::filesystem::path(first.logical_rel_path)) {
        (void)_;
        root = root.parent_path();
    }
    dir_abs = root / std::filesystem::path(norm);
    have_root = true;

    if (!have_root || dir_abs == user_dir) {
        audit_fail("refuse_root_delete", 400, "", path_rel);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "refusing to delete user storage root"}
        }.dump());
        return;
    }

    const std::string dir_root_s = dir_abs.lexically_normal().string();
    bool all_under_root = true;
    std::uint64_t removed_files = 0;
    std::uint64_t removed_bytes = 0;

    for (const auto& rec : subtree_rows) {
        const std::filesystem::path phys = std::filesystem::path(rec.physical_path).lexically_normal();
        const std::string phys_s = phys.string();

        if (phys_s != dir_root_s && phys_s.rfind(dir_root_s + "/", 0) != 0) {
            all_under_root = false;
            break;
        }
        removed_files += 1;
        removed_bytes += rec.size_bytes;
    }

    if (!all_under_root) {
        audit_fail("dir_delete_cross_root_unsupported", 409, "", path_rel);
        reply_json(res, 409, json{
            {"ok", false},
            {"error", "unsupported"},
            {"message", "directory delete across mixed storage roots not supported yet"}
        }.dump());
        return;
    }

    const std::uint64_t removed_dirs = count_logical_dirs_under_prefix(norm, subtree_rows);

    std::error_code ec;

    auto dir_st = std::filesystem::symlink_status(dir_abs, ec);
    if (ec || !std::filesystem::exists(dir_st)) {
        audit_fail("not_found", 404, "", path_rel);
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "path not found"}
        }.dump());
        return;
    }

    if (std::filesystem::is_symlink(dir_st)) {
        audit_fail("symlink_not_supported", 400, "", path_rel);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "symlinks not supported"}
        }.dump());
        return;
    }

    if (!std::filesystem::is_directory(dir_st)) {
        audit_fail("path_not_dir", 400, "", path_rel);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "path is not a directory"}
        }.dump());
        return;
    }

    for (std::filesystem::recursive_directory_iterator it(
             dir_abs, std::filesystem::directory_options::skip_permission_denied, ec), end;
         it != end && !ec;
         it.increment(ec)) {

        std::error_code ec2;
        auto st2 = std::filesystem::symlink_status(it->path(), ec2);
        if (ec2) {
            audit_fail("tree_stat_failed", 500, ec2.message(), path_rel);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "recursive delete scan failed"},
                {"detail", pqnas::shorten(ec2.message(), 180)}
            }.dump());
            return;
        }

        if (std::filesystem::is_symlink(st2)) {
            audit_fail("tree_contains_symlink", 409, it->path().string(), path_rel);
            reply_json(res, 409, json{
                {"ok", false},
                {"error", "path_conflict"},
                {"message", "directory contains symlinks"}
            }.dump());
            return;
        }
    }

    if (ec) {
        audit_fail("tree_walk_failed", 500, ec.message(), path_rel);
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "recursive delete scan failed"},
            {"detail", pqnas::shorten(ec.message(), 180)}
        }.dump());
        return;
    }

    // Recheck right before remove_all.
    ec.clear();
    auto dir_st2 = std::filesystem::symlink_status(dir_abs, ec);
    if (ec || !std::filesystem::exists(dir_st2)) {
        audit_fail("not_found", 404, "", path_rel);
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "path not found"}
        }.dump());
        return;
    }

    if (std::filesystem::is_symlink(dir_st2)) {
        audit_fail("symlink_not_supported", 400, "", path_rel);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "symlinks not supported"}
        }.dump());
        return;
    }

    if (!std::filesystem::is_directory(dir_st2)) {
        audit_fail("path_changed_before_delete", 409, "", path_rel);
        reply_json(res, 409, json{
            {"ok", false},
            {"error", "path_conflict"},
            {"message", "path changed before delete"}
        }.dump());
        return;
    }

    std::uintmax_t removed = std::filesystem::remove_all(dir_abs, ec);
    (void)removed;
    if (ec) {
        audit_fail("remove_all_failed", 500, ec.message(), path_rel);
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "recursive delete failed"},
            {"detail", pqnas::shorten(ec.message(), 180)}
        }.dump());
        return;
    }

    {
        std::string derr;
        if (!idx->erase_subtree(fp_hex, norm, &derr)) {
            audit_fail("metadata_erase_subtree_failed", 500, derr, path_rel);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "delete metadata cleanup failed"},
                {"detail", pqnas::shorten(derr, 180)}
            }.dump());
            return;
        }
    }

    {
        std::string ferr;
        if (!pqnas::favorites_remove_under_prefix(user_dir, norm, "dir", &ferr)) {
            pqnas::AuditEvent ev;
            ev.event = "v4.files_rmrf_warn";
            ev.outcome = "warn";
            ev.f["fingerprint"] = fp_hex;
            ev.f["reason"] = "favorites_cleanup_failed";
            ev.f["path"] = pqnas::shorten(norm, 200);
            ev.f["detail"] = pqnas::shorten(ferr, 180);
            add_ip_headers(ev);
            audit_append(ev);
        }
    }

    audit_ok(path_rel, "dir", removed_files, removed_dirs, removed_bytes);
    reply_json(res, 200, json{
        {"ok", true},
        {"path", path_rel},
        {"type", "dir"},
        {"removed_files", removed_files},
        {"removed_dirs", removed_dirs},
        {"removed_bytes", removed_bytes}
    }.dump());
    return;
}

audit_fail("unsupported_type", 400, "", path_rel);
reply_json(res, 400, json{
    {"ok", false},
    {"error", "bad_request"},
    {"message", "unsupported path type"}
}.dump());
});

    // POST /api/v4/files/search?path=rel/dir&q=needle&max=200
srv.Post("/api/v4/files/search", [&](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;

    const std::uint64_t SCAN_HARD_CAP = 200000; // hard cap for file search

    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto add_ip_headers = [&](pqnas::AuditEvent& ev) {
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
    };

    auto audit_fail = [&](const std::string& reason, int http, const std::string& detail = "",
                          const std::string& path_rel = "", const std::string& q = "", int max = 0) {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_search_fail";
        ev.outcome = "fail";
        ev.f["fingerprint"] = fp_hex;
        ev.f["reason"] = reason;
        ev.f["http"] = std::to_string(http);
        if (!path_rel.empty()) ev.f["path"] = pqnas::shorten(path_rel, 200);
        if (!q.empty())        ev.f["q"] = pqnas::shorten(q, 120);
        if (max > 0)           ev.f["max"] = std::to_string(max);
        if (!detail.empty())   ev.f["detail"] = pqnas::shorten(detail, 180);

        add_ip_headers(ev);
        audit_append(ev);
    };

    auto audit_ok = [&](const std::string& path_rel, const std::string& q, int max,
                        std::uint64_t scanned, std::uint64_t matched,
                        std::uint64_t returned, bool truncated,
                        bool scan_capped) {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_search_ok";
        ev.outcome = "ok";
        ev.f["fingerprint"] = fp_hex;
        ev.f["path"] = pqnas::shorten(path_rel.empty() ? "." : path_rel, 200);
        ev.f["q"] = pqnas::shorten(q, 120);
        ev.f["max"] = std::to_string(max);
        ev.f["scanned"] = std::to_string((unsigned long long)scanned);
        ev.f["matched"] = std::to_string((unsigned long long)matched);
        ev.f["returned"] = std::to_string((unsigned long long)returned);
        ev.f["truncated"] = truncated ? "1" : "0";

        if (scan_capped) {
            ev.f["scan_capped"] = "1";
            ev.f["scan_cap"] = std::to_string((unsigned long long)SCAN_HARD_CAP);
        }

        add_ip_headers(ev);
        audit_append(ev);
    };

    // must have allocated storage
    auto uopt = users.get(fp_hex);
    if (!uopt.has_value() || uopt->storage_state != "allocated") {
        audit_fail("storage_unallocated", 403);
        reply_json(res, 403, json{
            {"ok", false},
            {"error", "storage_unallocated"},
            {"message", "Storage not allocated"}
        }.dump());
        return;
    }

    std::string path_rel, q;
    if (req.has_param("path")) path_rel = req.get_param_value("path");
    if (req.has_param("q"))    q = req.get_param_value("q");

    // allow "." as user root for convenience
    if (path_rel == "." || path_rel == "./") path_rel.clear();

    if (q.empty()) {
        audit_fail("missing_q", 400, "", path_rel, q);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "missing q"}
        }.dump());
        return;
    }

    // v1: avoid pathological queries
    if (q.size() > 128) {
        audit_fail("q_too_long", 400, "", path_rel, q);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "q too long"}
        }.dump());
        return;
    }

    int max = 200;
    if (req.has_param("max")) {
        try { max = std::stoi(req.get_param_value("max")); } catch (...) {}
    }
    max = std::max(1, std::min(2000, max));

    const std::filesystem::path user_dir = user_dir_for_fp(users, fp_hex);

    std::filesystem::path base_abs;
    std::string err;

    // IMPORTANT: do NOT call strict resolver for empty/"." path; treat as user root.
    if (path_rel.empty()) {
        base_abs = user_dir;
    } else {
        if (!pqnas::resolve_user_path_strict(user_dir, path_rel, &base_abs, &err)) {
            audit_fail("invalid_path", 400, err, path_rel, q, max);
            reply_json(res, 400, json{{"ok",false},{"error","bad_request"},{"message","invalid path"}}.dump());
            return;
        }
    }
    {
        std::string serr;
        if (!ensure_no_symlink_in_existing_path_prefix(base_abs, &serr)) {
            audit_fail("symlink_not_supported", 400, serr, path_rel, q, max);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "symlinks not supported for search base"}
            }.dump());
            return;
        }
    }
    std::error_code ec;
    auto st = std::filesystem::symlink_status(base_abs, ec);
    if (ec || !std::filesystem::exists(st)) {
        audit_fail("not_found", 404, "", path_rel, q, max);
        reply_json(res, 404, json{{"ok",false},{"error","not_found"},{"message","path not found"}}.dump());
        return;
    }
    if (std::filesystem::is_symlink(st)) {
        audit_fail("symlink_not_supported", 400, "", path_rel, q, max);
        reply_json(res, 400, json{{"ok",false},{"error","bad_request"},{"message","symlinks not supported for search base"}}.dump());
        return;
    }
    if (!std::filesystem::is_directory(st)) {
        audit_fail("not_a_directory", 400, "", path_rel, q, max);
        reply_json(res, 400, json{{"ok",false},{"error","bad_request"},{"message","path must be a directory"}}.dump());
        return;
    }

    auto lower_ascii = [](std::string s) {
        for (char& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    };

    const std::string ql = lower_ascii(q);

    std::uint64_t scanned = 0;
    std::uint64_t matched = 0;
    bool truncated = false;
    bool scan_capped = false;

    json results = json::array();

    std::filesystem::directory_options opts = std::filesystem::directory_options::skip_permission_denied;

    ec.clear();
    for (auto it = std::filesystem::recursive_directory_iterator(base_abs, opts, ec);
         it != std::filesystem::recursive_directory_iterator();
         it.increment(ec)) {

        if (ec) {
            audit_fail("walk_failed", 500, ec.message(), path_rel, q, max);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "directory walk failed"},
                {"detail", pqnas::shorten(ec.message(), 180)}
            }.dump());
            return;
        }

        scanned++;

        if (scanned >= SCAN_HARD_CAP) {
            scan_capped = true;
            truncated = true;
            break;
        }

        // Do not follow symlinks and do not include them in results.
        std::error_code ec2;
        auto st2 = std::filesystem::symlink_status(it->path(), ec2);
        if (ec2) continue;
        if (std::filesystem::is_symlink(st2)) {
            continue;
        }

        const std::filesystem::path p = it->path();
        const std::string name = p.filename().string();
        const std::string namel = lower_ascii(name);

        if (namel.find(ql) == std::string::npos) {
            continue;
        }

        matched++;

        std::string type = "other";
        std::uint64_t bytes = 0;

        if (!ec2 && std::filesystem::is_directory(st2)) {
            type = "dir";
        } else if (!ec2 && std::filesystem::is_regular_file(st2)) {
            type = "file";
            bytes = pqnas::file_size_u64_safe(p);
        }

        // Convert to user-relative path
        std::filesystem::path rel = std::filesystem::relative(p, user_dir, ec2);
        std::string rel_s = ec2 ? "" : rel.generic_string();

        json item;
        item["path"] = rel_s.empty() ? pqnas::shorten(p.string(), 220) : rel_s;
        item["name"] = pqnas::shorten(name, 200);
        item["type"] = type;
        if (type == "file") item["bytes"] = bytes;

        results.push_back(item);

        if ((int)results.size() >= max) {
            truncated = true;
            break;
        }
    }

    audit_ok(path_rel, q, max, scanned, matched,
         (std::uint64_t)results.size(), truncated, scan_capped);

    reply_json(res, 200, json{
        {"ok", true},
        {"path", path_rel.empty() ? "." : path_rel},
        {"q", q},
        {"max", max},
        {"scanned", scanned},
        {"matched", matched},
        {"truncated", truncated},
        {"scan_capped", scan_capped},
        {"results", results}
    }.dump());
});

// /api/v4/files/stat?path=rel/path or "." (dir -> children + recursive bytes by default)
// GET/POST /api/v4/files/stat?path=rel/path or "." ...
auto files_stat_handler = [&](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;

    // Caps for recursive directory aggregation (UI-safe)
    const std::uint64_t RECURSIVE_HARD_CAP = 100000; // max entries scanned
    const int RECURSIVE_TIME_CAP_MS = 300;           // soft wall clock cap

    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto add_ip_headers = [&](pqnas::AuditEvent& ev) {
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
    };

    auto audit_fail = [&](const std::string& reason, int http,
                          const std::string& detail = "",
                          const std::string& path_rel = "") {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_stat_fail";
        ev.outcome = "fail";
        ev.f["fingerprint"] = fp_hex;
        ev.f["reason"] = reason;
        ev.f["http"] = std::to_string(http);
        if (!path_rel.empty()) ev.f["path"] = pqnas::shorten(path_rel, 200);
        if (!detail.empty())   ev.f["detail"] = pqnas::shorten(detail, 180);

        add_ip_headers(ev);
        audit_append(ev);
    };

    // must have allocated storage
    auto uopt = users.get(fp_hex);
    if (!uopt.has_value() || uopt->storage_state != "allocated") {
        audit_fail("storage_unallocated", 403);
        reply_json(res, 403, json{
            {"ok", false},
            {"error", "storage_unallocated"},
            {"message", "Storage not allocated"}
        }.dump());
        return;
    }

    std::string path_rel;
    if (req.has_param("path")) path_rel = req.get_param_value("path");

    // allow "." as user root for convenience
    if (path_rel == "." || path_rel == "./" || path_rel == "/") path_rel.clear();

const std::filesystem::path user_dir = user_dir_for_fp(users, fp_hex);

std::filesystem::path path_abs;
std::string err;
std::string logical_rel_norm;
pqnas::ResolvedLogicalItem resolved_item;
bool have_resolved_item = false;


// IMPORTANT: root stat still maps to the current user root directory.
// Non-root paths must resolve through logical metadata-aware resolver.
if (path_rel.empty()) {
    path_abs = user_dir;
    logical_rel_norm.clear();
    have_resolved_item = false;
} else {
    if (!pqnas::resolve_existing_user_item(users, fp_hex, path_rel, &resolved_item, &err) ||
        !resolved_item.exists) {
        audit_fail("not_found", 404, err, path_rel);
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "path not found"}
        }.dump());
        return;
    }

    logical_rel_norm = resolved_item.normalized_rel_path;
    have_resolved_item = true;

    if (resolved_item.is_file) {
        path_abs = resolved_item.abs_path;
    } else if (resolved_item.is_dir) {
        auto* idx = pqnas::get_file_location_index();
        if (idx) {
            std::string lerr;
            auto subtree_rows = idx->list_subtree_records(fp_hex, logical_rel_norm, &lerr);
            if (!lerr.empty()) {
                audit_fail("metadata_subtree_list_failed", 500, lerr, path_rel);
                reply_json(res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "failed to list metadata subtree"},
                    {"detail", pqnas::shorten(lerr, 180)}
                }.dump());
                return;
            }

            // For metadata-backed dirs, derive physical root from metadata rows first.
            if (!subtree_rows.empty()) {
                const auto& first = subtree_rows.front();
                std::filesystem::path root = std::filesystem::path(first.physical_path);
                for (const auto& _ : std::filesystem::path(first.logical_rel_path)) {
                    (void)_;
                    root = root.parent_path();
                }
                path_abs = root / std::filesystem::path(logical_rel_norm);
            } else if (resolved_item.has_physical_anchor) {
                path_abs = resolved_item.abs_path;
            } else {
                audit_fail("not_found", 404, "", path_rel);
                reply_json(res, 404, json{
                    {"ok", false},
                    {"error", "not_found"},
                    {"message", "path not found"}
                }.dump());
                return;
            }
        } else if (resolved_item.has_physical_anchor) {
            path_abs = resolved_item.abs_path;
        } else {
            audit_fail("metadata_index_missing", 500, "", path_rel);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "metadata index missing"}
            }.dump());
            return;
        }
    } else {
        audit_fail("not_found", 404, "", path_rel);
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "path not found"}
        }.dump());
        return;
    }
}

    // Detect symlink without following (matches /search base behavior)
    std::error_code ec;
    auto st = std::filesystem::symlink_status(path_abs, ec);
    if (ec || !std::filesystem::exists(st)) {
        audit_fail("not_found", 404, "", path_rel);
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "path not found"}
        }.dump());
        return;
    }
    if (std::filesystem::is_symlink(st)) {
        audit_fail("symlink_not_supported", 400, "", path_rel);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "symlinks not supported"}
        }.dump());
        return;
    }

    // Helper: absolute-like normalized path for UI (/foo/bar). Root => "/"
    auto make_path_norm = [&](const std::filesystem::path& p) -> std::string {
        std::error_code ec2;
        auto rel = std::filesystem::relative(p, user_dir, ec2);
        if (ec2) return "/";
        std::string s = rel.generic_string();
        if (s.empty() || s == ".") return "/";
        if (!s.empty() && s[0] != '/') s = "/" + s;
        return s;
    };

    auto mode_octal_from_perms = [&](std::filesystem::perms pr) -> std::string {
        auto has = [&](std::filesystem::perms bit) { return (pr & bit) != std::filesystem::perms::none; };

        int m = 0;
        if (has(std::filesystem::perms::owner_read))  m |= 0400;
        if (has(std::filesystem::perms::owner_write)) m |= 0200;
        if (has(std::filesystem::perms::owner_exec))  m |= 0100;

        if (has(std::filesystem::perms::group_read))  m |= 0040;
        if (has(std::filesystem::perms::group_write)) m |= 0020;
        if (has(std::filesystem::perms::group_exec))  m |= 0010;

        if (has(std::filesystem::perms::others_read))  m |= 0004;
        if (has(std::filesystem::perms::others_write)) m |= 0002;
        if (has(std::filesystem::perms::others_exec))  m |= 0001;

        char buf[8];
        std::snprintf(buf, sizeof(buf), "%04o", m);
        return std::string(buf);
    };

    auto guess_mime = [&](const std::string& name) -> std::string {
        auto lower = [](std::string s) {
            for (char& c : s) c = (char)std::tolower((unsigned char)c);
            return s;
        };
        std::string n = lower(name);
        auto dot = n.rfind('.');
        std::string ext = (dot == std::string::npos) ? "" : n.substr(dot + 1);

        if (ext == "txt" || ext == "log" || ext == "md") return "text/plain";
        if (ext == "json") return "application/json";
        if (ext == "html" || ext == "htm") return "text/html";
        if (ext == "css") return "text/css";
        if (ext == "js") return "application/javascript";
        if (ext == "xml") return "application/xml";
        if (ext == "csv") return "text/csv";

        if (ext == "png") return "image/png";
        if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
        if (ext == "gif") return "image/gif";
        if (ext == "webp") return "image/webp";
        if (ext == "svg") return "image/svg+xml";

        if (ext == "pdf") return "application/pdf";
        if (ext == "zip") return "application/zip";

        return "application/octet-stream";
    };

    auto looks_like_text = [&](const std::filesystem::path& p) -> bool {
        // lightweight "binary reject": treat NUL as binary
        std::ifstream f(p, std::ios::binary);
        if (!f.good()) return false;

        char buf[4096];
        f.read(buf, sizeof(buf));
        std::streamsize n = f.gcount();
        for (std::streamsize i = 0; i < n; i++) {
            if (buf[i] == '\0') return false;
        }
        return true;
    };
    bool is_dir = std::filesystem::is_directory(st);
    bool is_file = std::filesystem::is_regular_file(st);

    if (!path_rel.empty() && have_resolved_item) {
        if (resolved_item.is_dir) {
            is_dir = true;
            is_file = false;
        } else if (resolved_item.is_file) {
            is_file = true;
            is_dir = false;
        }
    }

    std::string type = "other";
    if (is_dir) type = "dir";
    else if (is_file) type = "file";

	std::string path_norm;
	if (path_rel.empty()) {
    	path_norm = "/";
	} else if (!logical_rel_norm.empty()) {
    	path_norm = "/" + logical_rel_norm;
	} else {
    	path_norm = make_path_norm(path_abs);
	}

	const std::string name =
    	path_rel.empty()
        	? ""
	        : (!logical_rel_norm.empty()
    	          ? std::filesystem::path(logical_rel_norm).filename().string()
        	      : path_abs.filename().string());

    // mtime (portable via last_write_time)
    std::uint64_t mtime_epoch = 0;
    {
        std::error_code ec3;
        auto ftime = std::filesystem::last_write_time(path_abs, ec3);
        if (!ec3) {
            using namespace std::chrono;
            auto sctp = time_point_cast<system_clock::duration>(
                ftime - std::filesystem::file_time_type::clock::now() + system_clock::now()
            );
            auto sec = duration_cast<seconds>(sctp.time_since_epoch()).count();
            if (sec > 0) mtime_epoch = (std::uint64_t)sec;
        }
    }

    // mode (best-effort, do not follow symlinks)
    std::string mode_octal = "0000";
    {
        std::error_code ec4;
        auto stp = std::filesystem::symlink_status(path_abs, ec4);
        if (!ec4 && !std::filesystem::is_symlink(stp)) {
            mode_octal = mode_octal_from_perms(stp.permissions());
        }
    }

    json out;
    out["ok"] = true;
    out["path"] = path_rel.empty() ? "." : path_rel;
    out["path_norm"] = path_norm;
    out["name"] = pqnas::shorten(name, 200);
    out["type"] = type;
    out["exists"] = true;
    if (mtime_epoch > 0) out["mtime_epoch"] = mtime_epoch;
    out["mode_octal"] = mode_octal;

    if (type == "file") {
        out["bytes"] = pqnas::file_size_u64_safe(path_abs);
        out["mime"] = guess_mime(name);
        out["is_text"] = looks_like_text(path_abs);
        reply_json(res, 200, out.dump());
        return;
    }

    if (type == "dir") {
        // immediate children counts
        // Match /list policy: do not follow symlinks and do not expose them as normal children.
        std::uint64_t c_files = 0, c_dirs = 0, c_other = 0;

        std::error_code ec5;
        for (auto it = std::filesystem::directory_iterator(path_abs, std::filesystem::directory_options::skip_permission_denied, ec5);
             !ec5 && it != std::filesystem::directory_iterator();
             it.increment(ec5)) {

            std::error_code ec6;
            auto stc = std::filesystem::symlink_status(it->path(), ec6);
            if (ec6) { c_other++; continue; }

            if (std::filesystem::is_symlink(stc)) {
                continue;
            }
            if (std::filesystem::is_directory(stc)) c_dirs++;
            else if (std::filesystem::is_regular_file(stc)) c_files++;
            else c_other++;
        }

        out["children"] = json{
            {"files", c_files},
            {"dirs", c_dirs},
            {"other", c_other}
        };

        // recursive bytes: sum regular files only, skip symlinks (matches /du)
        std::uint64_t bytes_recursive = 0;
        std::uint64_t scanned = 0;
        bool complete = true;

        auto t0 = std::chrono::steady_clock::now();
        std::filesystem::directory_options opts = std::filesystem::directory_options::skip_permission_denied;

        ec5.clear();
        for (auto it = std::filesystem::recursive_directory_iterator(path_abs, opts, ec5);
             it != std::filesystem::recursive_directory_iterator();
             it.increment(ec5)) {

            if (ec5) {
                audit_fail("walk_failed", 500, ec5.message(), path_rel);
                reply_json(res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "directory walk failed"},
                    {"detail", pqnas::shorten(ec5.message(), 180)}
                }.dump());
                return;
            }

            scanned++;

            if (scanned >= RECURSIVE_HARD_CAP) { complete = false; break; }

            auto now = std::chrono::steady_clock::now();
            auto ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(now - t0).count();
            if (ms >= RECURSIVE_TIME_CAP_MS) { complete = false; break; }

            std::error_code ec6;
            auto st2 = std::filesystem::symlink_status(it->path(), ec6);
            if (ec6) continue;

            // Do not follow symlinks and do not account for them.
            // recursive_directory_iterator is not using follow_directory_symlink here,
            // so skipping is enough.
            if (std::filesystem::is_symlink(st2)) {
                continue;
            }

            if (std::filesystem::is_regular_file(st2)) {
                bytes_recursive += pqnas::file_size_u64_safe(it->path());
            }
        }

        out["bytes_recursive"] = bytes_recursive;
        out["recursive_scanned_entries"] = scanned;
        out["recursive_complete"] = complete;
        out["scan_cap"] = RECURSIVE_HARD_CAP;
        out["time_cap_ms"] = RECURSIVE_TIME_CAP_MS;

        reply_json(res, 200, out.dump());
        return;
    }

    // other types: just return metadata
    reply_json(res, 200, out.dump());
};

srv.Post("/api/v4/files/stat", files_stat_handler);
srv.Get ("/api/v4/files/stat", files_stat_handler);

// POST /api/v4/files/stat_sel
// Body: { "paths": ["rel/path", ".", ...] }
// Returns aggregated selection stats (total bytes etc) + per-item minimal stats.
srv.Post("/api/v4/files/stat_sel", [&](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;

    // must have allocated storage
    auto uopt = users.get(fp_hex);
    if (!uopt.has_value() || uopt->storage_state != "allocated") {
        reply_json(res, 403, json{
            {"ok", false},
            {"error", "storage_unallocated"},
            {"message", "Storage not allocated"}
        }.dump());
        return;
    }

    // caps (match /stat)
    const std::uint64_t RECURSIVE_HARD_CAP = 100000;
    const int RECURSIVE_TIME_CAP_MS = 300;

    // selection cap (protect server)
    const int MAX_ITEMS = 200;

    json body;
    try {
        body = json::parse(req.body);
    } catch (...) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid json"}
        }.dump());
        return;
    }

    if (!body.is_object() || !body.contains("paths") || !body["paths"].is_array()) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "body must be { paths: [...] }"}
        }.dump());
        return;
    }

    const std::filesystem::path user_dir = user_dir_for_fp(users, fp_hex);

    // Helpers (copied from /stat for consistency)
    auto mode_octal_from_perms = [&](std::filesystem::perms pr) -> std::string {
        auto has = [&](std::filesystem::perms bit) { return (pr & bit) != std::filesystem::perms::none; };
        int m = 0;
        if (has(std::filesystem::perms::owner_read))  m |= 0400;
        if (has(std::filesystem::perms::owner_write)) m |= 0200;
        if (has(std::filesystem::perms::owner_exec))  m |= 0100;
        if (has(std::filesystem::perms::group_read))  m |= 0040;
        if (has(std::filesystem::perms::group_write)) m |= 0020;
        if (has(std::filesystem::perms::group_exec))  m |= 0010;
        if (has(std::filesystem::perms::others_read))  m |= 0004;
        if (has(std::filesystem::perms::others_write)) m |= 0002;
        if (has(std::filesystem::perms::others_exec))  m |= 0001;
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%04o", m);
        return std::string(buf);
    };

    auto make_path_norm = [&](const std::filesystem::path& p) -> std::string {
        std::error_code ec2;
        auto rel = std::filesystem::relative(p, user_dir, ec2);
        if (ec2) return "/";
        std::string s = rel.generic_string();
        if (s.empty() || s == ".") return "/";
        if (!s.empty() && s[0] != '/') s = "/" + s;
        return s;
    };

    auto dir_bytes_recursive = [&](const std::filesystem::path& base_abs,
                                   std::uint64_t* out_scanned,
                                   bool* out_complete) -> std::uint64_t {
        std::uint64_t bytes_recursive = 0;
        std::uint64_t scanned = 0;
        bool complete = true;

        auto t0 = std::chrono::steady_clock::now();
        std::filesystem::directory_options opts = std::filesystem::directory_options::skip_permission_denied;

        std::error_code ec;
        for (auto it = std::filesystem::recursive_directory_iterator(base_abs, opts, ec);
             it != std::filesystem::recursive_directory_iterator();
             it.increment(ec)) {

            if (ec) {
                // Treat walk failure as incomplete; caller will record error.
                complete = false;
                break;
            }

            scanned++;
            if (scanned >= RECURSIVE_HARD_CAP) { complete = false; break; }

            auto now = std::chrono::steady_clock::now();
            auto ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(now - t0).count();
            if (ms >= RECURSIVE_TIME_CAP_MS) { complete = false; break; }

            std::error_code ec6;
            auto st2 = std::filesystem::symlink_status(it->path(), ec6);
            if (ec6) continue;

            // Do not follow symlinks and do not account for them.
            if (std::filesystem::is_symlink(st2)) {
                continue;
            }

            if (std::filesystem::is_regular_file(st2)) {
                bytes_recursive += pqnas::file_size_u64_safe(it->path());
            }
        }

        if (out_scanned) *out_scanned = scanned;
        if (out_complete) *out_complete = complete;
        return bytes_recursive;
    };

    // Aggregate
    std::uint64_t total_bytes = 0;
    int n_files = 0, n_dirs = 0, n_other = 0;
    bool partial = false;

    json items = json::array();
    json errors = json::array();

    int idx = 0;
    for (const auto& v : body["paths"]) {
        if (idx >= MAX_ITEMS) { partial = true; break; }
        idx++;

        if (!v.is_string()) {
            partial = true;
            errors.push_back(json{{"path",""}, {"error","bad_request"}, {"message","path must be string"}});
            continue;
        }

        std::string path_rel = v.get<std::string>();

        // allow "." as user root
        if (path_rel == "." || path_rel == "./" || path_rel == "/") path_rel.clear();

    std::filesystem::path path_abs;
	std::string logical_rel_norm;
	pqnas::ResolvedLogicalItem resolved_item;
	bool have_resolved_item = false;

	if (path_rel.empty()) {
    	path_abs = user_dir;
	    logical_rel_norm.clear();
    	have_resolved_item = false;
	} else {
    	std::string rerr;
	    if (!pqnas::resolve_existing_user_item(users, fp_hex, path_rel, &resolved_item, &rerr) ||
    	    !resolved_item.exists) {
        	partial = true;
	        errors.push_back(json{{"path", path_rel}, {"error","not_found"}});
    	    continue;
    	}

	    logical_rel_norm = resolved_item.normalized_rel_path;
    	have_resolved_item = true;

	    if (resolved_item.is_file) {
    	    path_abs = resolved_item.abs_path;
	    } else if (resolved_item.is_dir) {
    	    auto* idx2 = pqnas::get_file_location_index();
        	if (idx2) {
            	std::string lerr;
	            auto subtree_rows = idx2->list_subtree_records(fp_hex, logical_rel_norm, &lerr);
    	        if (!lerr.empty()) {
        	        partial = true;
            	    errors.push_back(json{
                	    {"path", path_rel},
                    	{"error", "server_error"},
	                    {"message", pqnas::shorten(lerr, 180)}
    	            });
        	        continue;
            	}

	            if (!subtree_rows.empty()) {
    	            const auto& first = subtree_rows.front();
        	        std::filesystem::path root = std::filesystem::path(first.physical_path);
            	    for (const auto& _ : std::filesystem::path(first.logical_rel_path)) {
                	    (void)_;
                    	root = root.parent_path();
	                }
    	            path_abs = root / std::filesystem::path(logical_rel_norm);
        	    } else if (resolved_item.has_physical_anchor) {
            	    path_abs = resolved_item.abs_path;
	            } else {
    	            partial = true;
        	        errors.push_back(json{{"path", path_rel}, {"error","not_found"}});
            	    continue;
	            }
    	    } else if (resolved_item.has_physical_anchor) {
        	    path_abs = resolved_item.abs_path;
	        } else {
    	        partial = true;
        	    errors.push_back(json{{"path", path_rel}, {"error","server_error"}, {"message","metadata index missing"}});
            	continue;
	        }
    	} else {
        	partial = true;
	        errors.push_back(json{{"path", path_rel}, {"error","not_found"}});
    	    continue;
    	}
	}

	// Detect symlink without following
	std::error_code ec;
	auto st = std::filesystem::symlink_status(path_abs, ec);
	if (ec || !std::filesystem::exists(st)) {
	    partial = true;
    	errors.push_back(json{{"path", path_rel.empty() ? "." : path_rel}, {"error","not_found"}});
    	continue;
	}
	if (std::filesystem::is_symlink(st)) {
    	partial = true;
	    errors.push_back(json{{"path", path_rel.empty() ? "." : path_rel}, {"error","symlink_not_supported"}});
    	continue;
	}

	bool is_dir = std::filesystem::is_directory(st);
	bool is_file = std::filesystem::is_regular_file(st);

	if (!path_rel.empty() && have_resolved_item) {
    	if (resolved_item.is_dir) {
        	is_dir = true;
	        is_file = false;
    	} else if (resolved_item.is_file) {
        	is_file = true;
	        is_dir = false;
    	}
	}

        std::string type = "other";
        if (is_dir) type = "dir";
        else if (is_file) type = "file";

        json itj;
        itj["path"] = path_rel.empty() ? "." : path_rel;
        if (path_rel.empty()) {
 		   itj["path_norm"] = "/";
		} else if (!logical_rel_norm.empty()) {
    		itj["path_norm"] = "/" + logical_rel_norm;
		} else {
    		itj["path_norm"] = make_path_norm(path_abs);
		}
        itj["type"] = type;

        // mode (best-effort, do not follow symlinks)
        {
            std::error_code ec4;
            auto stp = std::filesystem::symlink_status(path_abs, ec4);
            if (!ec4 && !std::filesystem::is_symlink(stp)) {
                itj["mode_octal"] = mode_octal_from_perms(stp.permissions());
            }
        }

        if (type == "file") {
            std::uint64_t b = pqnas::file_size_u64_safe(path_abs);
            itj["bytes"] = b;
            total_bytes += b;
            n_files++;
            items.push_back(itj);
            continue;
        }

        if (type == "dir") {
            std::uint64_t scanned = 0;
            bool complete = true;

            // recursive bytes: sum regular files only, skip symlinks (same as /stat)
            std::uint64_t b = dir_bytes_recursive(path_abs, &scanned, &complete);

            itj["bytes_recursive"] = b;
            itj["recursive_scanned_entries"] = scanned;
            itj["recursive_complete"] = complete;

            total_bytes += b;
            n_dirs++;

            if (!complete) partial = true;

            items.push_back(itj);
            continue;
        }

        // other
        n_other++;
        items.push_back(itj);
        partial = true; // “other” is uncommon; treat aggregate as partial/unknown
    }

    json out;
    out["ok"] = true;
    out["count"] = (int)items.size();
    out["files"] = n_files;
    out["dirs"] = n_dirs;
    out["other"] = n_other;
    out["bytes_total"] = total_bytes;
    out["partial"] = partial;

    out["limits"] = json{
        {"max_items", MAX_ITEMS},
        {"scan_cap", RECURSIVE_HARD_CAP},
        {"time_cap_ms", RECURSIVE_TIME_CAP_MS}
    };

    out["items"] = items;
    out["errors"] = errors;

    reply_json(res, 200, out.dump());
});



    // POST /api/v4/files/du?path=rel/path
srv.Post("/api/v4/files/du", [&](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;

    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto add_ip_headers = [&](pqnas::AuditEvent& ev) {
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
    };

    auto audit_fail = [&](const std::string& reason, int http, const std::string& detail = "",
                          const std::string& path_rel = "") {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_du_fail";
        ev.outcome = "fail";
        ev.f["fingerprint"] = fp_hex;
        ev.f["reason"] = reason;
        ev.f["http"] = std::to_string(http);
        if (!path_rel.empty()) ev.f["path"] = pqnas::shorten(path_rel, 200);
        if (!detail.empty())   ev.f["detail"] = pqnas::shorten(detail, 180);

        add_ip_headers(ev);
        audit_append(ev);
    };

    auto audit_ok = [&](const std::string& path_rel,
                        const std::string& type,
                        std::uint64_t bytes_total,
                        std::uint64_t files,
                        std::uint64_t dirs) {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_du_ok";
        ev.outcome = "ok";
        ev.f["fingerprint"] = fp_hex;
        ev.f["path"] = pqnas::shorten(path_rel, 200);
        ev.f["type"] = type;
        ev.f["bytes_total"] = std::to_string((unsigned long long)bytes_total);
        ev.f["files"] = std::to_string((unsigned long long)files);
        ev.f["dirs"]  = std::to_string((unsigned long long)dirs);

        add_ip_headers(ev);
        audit_append(ev);
    };

    // must have allocated storage
    auto uopt = users.get(fp_hex);
    if (!uopt.has_value() || uopt->storage_state != "allocated") {
        audit_fail("storage_unallocated", 403);
        reply_json(res, 403, json{
            {"ok", false},
            {"error", "storage_unallocated"},
            {"message", "Storage not allocated"}
        }.dump());
        return;
    }

    std::string path_rel;
    if (req.has_param("path")) path_rel = req.get_param_value("path");
    if (path_rel.empty()) {
        audit_fail("missing_path", 400);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "missing path"}
        }.dump());
        return;
    }

    const std::filesystem::path user_dir = user_dir_for_fp(users, fp_hex);

    std::filesystem::path path_abs;
    std::string err;
    if (!pqnas::resolve_user_path_strict(user_dir, path_rel, &path_abs, &err)) {
        audit_fail("invalid_path", 400, err, path_rel);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid path"}
        }.dump());
        return;
    }

    {
        std::string serr;
        if (!ensure_no_symlink_in_existing_path_prefix(path_abs.parent_path(), &serr)) {
            audit_fail("symlink_not_supported", 400, serr, path_rel);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "symlinks not supported"}
            }.dump());
            return;
        }
    }

    std::error_code ec;
    auto st = std::filesystem::symlink_status(path_abs, ec);
    if (ec || !std::filesystem::exists(st)) {
        audit_fail("not_found", 404, "", path_rel);
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "path not found"}
        }.dump());
        return;
    }

    if (std::filesystem::is_symlink(st)) {
        audit_fail("symlink_not_supported", 400, "", path_rel);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "symlinks not supported"}
        }.dump());
        return;
    }

    // If file: trivial
    if (std::filesystem::is_regular_file(st)) {
        std::uint64_t bytes = pqnas::file_size_u64_safe(path_abs);
        audit_ok(path_rel, "file", bytes, /*files=*/1, /*dirs=*/0);
        reply_json(res, 200, json{
            {"ok", true},
            {"path", path_rel},
            {"type", "file"},
            {"bytes_total", bytes},
            {"files", 1},
            {"dirs", 0}
        }.dump());
        return;
    }

    // If not a directory: reject (keeps semantics clean)
    if (!std::filesystem::is_directory(st)) {
        audit_fail("not_file_or_dir", 400, "", path_rel);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "path is not a file or directory"}
        }.dump());
        return;
    }

    // Directory recursive: sum regular files only, do not follow symlinks
    std::uint64_t bytes_total = 0;
    std::uint64_t files = 0;
    std::uint64_t dirs = 1; // count root dir

    std::filesystem::directory_options opts = std::filesystem::directory_options::skip_permission_denied;

    ec.clear();
    for (auto it = std::filesystem::recursive_directory_iterator(path_abs, opts, ec);
         it != std::filesystem::recursive_directory_iterator();
         it.increment(ec)) {

        if (ec) {
            // Fail closed: if traversal hits an error, stop and report
            audit_fail("walk_failed", 500, ec.message(), path_rel);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "directory walk failed"},
                {"detail", pqnas::shorten(ec.message(), 180)}
            }.dump());
            return;
        }

        std::error_code ec2;
        auto st2 = std::filesystem::symlink_status(it->path(), ec2);
        if (ec2) continue;

        // Do not follow or count symlinks
        if (std::filesystem::is_symlink(st2)) {
            continue;
        }

        if (std::filesystem::is_directory(st2)) {
            dirs++;
            continue;
        }

        if (std::filesystem::is_regular_file(st2)) {
            files++;
            std::uint64_t sz = pqnas::file_size_u64_safe(it->path());
            bytes_total += sz;
            continue;
        }

        // other types ignored
    }

    audit_ok(path_rel, "dir", bytes_total, files, dirs);

    reply_json(res, 200, json{
        {"ok", true},
        {"path", path_rel},
        {"type", "dir"},
        {"bytes_total", bytes_total},
        {"files", files},
        {"dirs", dirs}
    }.dump());
});

srv.Get("/api/v4/files/favorites", [&](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;

    auto uopt = users.get(fp_hex);
    if (!uopt.has_value() || uopt->storage_state != "allocated") {
        reply_json(res, 403, json{
            {"ok", false},
            {"error", "storage_unallocated"},
            {"message", "Storage not allocated"}
        }.dump());
        return;
    }

    const std::filesystem::path user_dir = user_dir_for_fp(users, fp_hex);

    std::vector<pqnas::FavoriteItem> items;
    std::string err;
    if (!pqnas::favorites_list_items(user_dir, &items, &err)) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "favorites list failed"},
            {"detail", pqnas::shorten(err, 180)}
        }.dump());
        return;
    }

    json out;
    out["ok"] = true;
    out["items"] = json::array();
    for (const auto& it : items) {
        out["items"].push_back(json{
            {"path", it.path},
            {"type", it.type},
            {"added_at", it.added_at}
        });
    }

    reply_json(res, 200, out.dump());
});

srv.Post("/api/v4/files/favorites/add", [&](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;

    auto uopt = users.get(fp_hex);
    if (!uopt.has_value() || uopt->storage_state != "allocated") {
        reply_json(res, 403, json{
            {"ok", false},
            {"error", "storage_unallocated"},
            {"message", "Storage not allocated"}
        }.dump());
        return;
    }

    json in = json::parse(req.body, nullptr, false);
    if (in.is_discarded()) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid json"}}.dump());
        return;
    }

    const std::string rel_path = in.value("path", "");
    const std::string type = in.value("type", "");

    std::string rel_norm, nerr;
    if (!pqnas::normalize_user_rel_path_strict(rel_path, &rel_norm, &nerr)) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid path"}}.dump());
        return;
    }
    if (type != "file" && type != "dir") {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid type"}}.dump());
        return;
    }

    const std::filesystem::path user_dir = user_dir_for_fp(users, fp_hex);

    // Validate logical item existence/type using metadata-aware resolver so
    // files are favoritable during landing/migration as well as in simple installs.
    pqnas::ResolvedLogicalItem item;
    std::string rerr;
    if (!pqnas::resolve_existing_user_item(users, fp_hex, rel_norm, &item, &rerr) || !item.exists) {
        reply_json(res, 404, json{{"ok", false}, {"error", "not_found"}, {"message", "path not found"}}.dump());
        return;
    }

    if ((type == "dir" && !item.is_dir) || (type == "file" && !item.is_file)) {
        reply_json(res, 409, json{{"ok", false}, {"error", "type_mismatch"}, {"message", "type mismatch"}}.dump());
        return;
    }

    std::string err;
    if (!pqnas::favorites_add(user_dir, rel_norm, type, &err)) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "favorites add failed"},
            {"detail", pqnas::shorten(err, 180)}
        }.dump());
        return;
    }

    reply_json(res, 200, json{
        {"ok", true},
        {"path", rel_norm},
        {"type", type}
    }.dump());
});

srv.Post("/api/v4/files/favorites/remove", [&](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;

    auto uopt = users.get(fp_hex);
    if (!uopt.has_value() || uopt->storage_state != "allocated") {
        reply_json(res, 403, json{
            {"ok", false},
            {"error", "storage_unallocated"},
            {"message", "Storage not allocated"}
        }.dump());
        return;
    }

    json in = json::parse(req.body, nullptr, false);
    if (in.is_discarded()) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid json"}}.dump());
        return;
    }

    const std::string rel_path = in.value("path", "");
    const std::string type = in.value("type", "");

    std::string rel_norm, nerr;
    if (!pqnas::normalize_user_rel_path_strict(rel_path, &rel_norm, &nerr)) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid path"}}.dump());
        return;
    }
    if (type != "file" && type != "dir") {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid type"}}.dump());
        return;
    }

    const std::filesystem::path user_dir = user_dir_for_fp(users, fp_hex);

    std::string err;
    if (!pqnas::favorites_remove(user_dir, rel_norm, type, &err)) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "favorites remove failed"},
            {"detail", pqnas::shorten(err, 180)}
        }.dump());
        return;
    }

    reply_json(res, 200, json{
        {"ok", true},
        {"path", rel_norm},
        {"type", type}
    }.dump());
});

// DELETE /api/v4/files/delete?path=relative/path
// Moves a file or directory to trash. Refuses empty path (won't delete user root).
srv.Post("/api/v4/files/delete", [&](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;

    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto audit_fail = [&](const std::string& reason, int http, const std::string& detail = "") {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_delete_fail";
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

    auto audit_ok = [&](const std::string& rel_path, const std::string& type,
                        std::uint64_t freed_bytes) {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_delete_ok";
        ev.outcome = "ok";
        ev.f["fingerprint"] = fp_hex;
        ev.f["path"] = pqnas::shorten(rel_path, 200);
        ev.f["type"] = type;
        ev.f["freed_bytes"] = std::to_string((unsigned long long)freed_bytes);

        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
        audit_append(ev);
    };

    auto audit_warn = [&](const std::string& reason,
                          const std::string& detail,
                          const std::string& rel_path) {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_delete_warn";
        ev.outcome = "warn";
        ev.f["fingerprint"] = fp_hex;
        ev.f["reason"] = reason;
        ev.f["path"] = pqnas::shorten(rel_path, 200);
        if (!detail.empty()) ev.f["detail"] = pqnas::shorten(detail, 180);
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
        ev.f["ua"] = audit_ua();
        audit_append(ev);
    };

    auto uopt = users.get(fp_hex);
    if (!uopt.has_value()) {
        audit_fail("user_missing", 403);
        reply_json(res, 403, json{
            {"ok", false},
            {"error", "forbidden"},
            {"message", "policy denied"}
        }.dump());
        return;
    }
    const auto u = *uopt;

    if (u.storage_state != "allocated") {
        audit_fail("storage_unallocated", 403);
        reply_json(res, 403, json{
            {"ok", false},
            {"error", "storage_unallocated"},
            {"message", "Storage not allocated"},
            {"fingerprint_hex", fp_hex},
            {"quota_bytes", u.quota_bytes}
        }.dump());
        return;
    }

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

    if (!require_user_no_live_lock_for_write_local(
            users, users_path, res, fp_hex, rel_norm, "delete", false)) {
        audit_fail("locked_delete_blocked", 409, rel_norm);
        return;
    }

    if (!require_user_no_live_lock_for_write_local(
            users, users_path, res, fp_hex, rel_norm, "write", false)) {
        return;
    }

    auto* plm = pqnas::get_path_lock_manager();
    auto write_guard = plm->lock_paths(fp_hex, {rel_norm});

    const std::filesystem::path user_dir = user_dir_for_fp(users, fp_hex);

    pqnas::ResolvedLogicalItem item;
    std::string rerr;
    if (!pqnas::resolve_existing_user_item(users, fp_hex, rel_norm, &item, &rerr) || !item.exists) {
        audit_fail("not_found", 404, rerr);
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "path not found"}
        }.dump());
        return;
    }

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

    auto file_size_safe = [&](const std::filesystem::path& p) -> std::uint64_t {
        std::error_code ec;
        auto sz = std::filesystem::file_size(p, ec);
        return ec ? 0 : static_cast<std::uint64_t>(sz);
    };

    if (item.is_file) {
        if (!item.has_physical_anchor) {
            audit_fail("file_missing_physical_anchor", 500);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "file missing physical anchor"}
            }.dump());
            return;
        }

        const std::filesystem::path abs_path = item.abs_path;

        if (abs_path == user_dir) {
            audit_fail("refuse_root_delete", 400);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "refusing to delete root"}
            }.dump());
            return;
        }

        std::error_code ec;
        auto st = std::filesystem::symlink_status(abs_path, ec);
        if (ec || !std::filesystem::exists(st)) {
            audit_fail("not_found", 404);
            reply_json(res, 404, json{
                {"ok", false},
                {"error", "not_found"},
                {"message", "path not found"}
            }.dump());
            return;
        }

        if (std::filesystem::is_symlink(st)) {
            audit_fail("symlink_not_supported", 400);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "symlinks not supported"}
            }.dump());
            return;
        }

        if (!std::filesystem::is_regular_file(st)) {
            audit_fail("path_not_file", 400);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "path is not a file"}
            }.dump());
            return;
        }

        const std::uint64_t freed_bytes = file_size_safe(abs_path);

        ec.clear();
        auto st2 = std::filesystem::symlink_status(abs_path, ec);
        if (ec || !std::filesystem::exists(st2)) {
            audit_fail("not_found", 404);
            reply_json(res, 404, json{
                {"ok", false},
                {"error", "not_found"},
                {"message", "path not found"}
            }.dump());
            return;
        }

        if (std::filesystem::is_symlink(st2)) {
            audit_fail("symlink_not_supported", 400);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "symlinks not supported"}
            }.dump());
            return;
        }

        if (!std::filesystem::is_regular_file(st2)) {
            audit_fail("path_changed_before_delete", 409);
            reply_json(res, 409, json{
                {"ok", false},
                {"error", "path_conflict"},
                {"message", "path changed before delete"}
            }.dump());
            return;
        }

        const std::string pool_id = u.storage_pool_id.empty() ? "default" : u.storage_pool_id;
        const std::filesystem::path storage_root = data_root_for_pool_id(pool_id);

        std::string merr;
        auto mrec = idx->get(fp_hex, rel_norm, &merr);
        if (!merr.empty()) {
            audit_fail("metadata_lookup_failed", 500, merr);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to read metadata before trash move"},
                {"detail", pqnas::shorten(merr, 180)}
            }.dump());
            return;
        }

        std::string terr;

        pqnas::TrashService::MoveToTrashParams tp;
        tp.scope_type = "user";
        tp.scope_id = fp_hex;
        tp.deleted_by_fp = fp_hex;
        tp.origin_app = "filemgr";
        tp.item_type = "file";
        tp.original_rel_path = rel_norm;
        tp.payload_abs_path = abs_path;
        tp.storage_root = storage_root;
        tp.size_bytes = freed_bytes;
        tp.file_count = 1;

        if (mrec.has_value()) {
            tp.source_pool = mrec->current_pool;
            tp.source_tier_state = mrec->tier_state;
        }

        pqnas::TrashService::MoveToTrashResult tr;
        if (!trash_service.move_to_trash(tp, &tr, &terr)) {
            audit_fail("trash_move_failed", 500, terr);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to move item to trash"},
                {"detail", pqnas::shorten(terr, 180)}
            }.dump());
            return;
        }
        record_user_file_activity_best_effort_local(
            users,
            fp_hex,
            "file.trashed",
            tp.original_rel_path,
            tp.item_type,
            tr.trash_id,
            tp.size_bytes,
            tp.file_count,
            &req
        );


        if (item.from_metadata) {
            std::string derr;
            if (!idx->erase(fp_hex, rel_norm, &derr)) {
                audit_fail("metadata_erase_failed", 500, derr);
                reply_json(res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "delete metadata cleanup failed"},
                    {"detail", pqnas::shorten(derr, 180)}
                }.dump());
                return;
            }
        } else {
            audit_warn("metadata_erase_skipped_legacy_file", "legacy physical file had no metadata row", rel_norm);
        }

        {
            std::string ferr;
            if (!pqnas::favorites_remove_under_prefix(user_dir, rel_norm, "file", &ferr)) {
                audit_warn("favorites_cleanup_failed", ferr, rel_norm);
            }
        }
        {
            std::string gerr;
            if (auto* gidx = pqnas::get_gallery_meta_index()) {
                if (!gidx->erase_subtree("user", fp_hex, rel_norm, &gerr)) {
                    audit_warn("gallery_meta_cleanup_failed", gerr, rel_norm);
                }
            }
        }
        {
            std::string rerr;
            if (!reelstack_meta_remove_under_prefix_path_local("user", fp_hex, rel_norm, "file", &rerr)) {
                audit_warn("reelstack_meta_cleanup_failed", rerr, rel_norm);
            }
        }

        audit_ok(rel_path, "file", freed_bytes);
        reply_json(res, 200, json{
            {"ok", true},
            {"fingerprint_hex", fp_hex},
            {"path", rel_path},
            {"type", "file"},
            {"freed_bytes", freed_bytes},
            {"trash_id", tr.trash_id}
        }.dump());
        return;
    }

    if (item.is_dir) {
        std::string lerr;
        auto subtree_rows = idx->list_subtree_records(fp_hex, rel_norm, &lerr);
        if (!lerr.empty()) {
            audit_fail("metadata_subtree_list_failed", 500, lerr);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to list metadata subtree"},
                {"detail", pqnas::shorten(lerr, 180)}
            }.dump());
            return;
        }

        if (subtree_rows.empty()) {
            if (!item.has_physical_anchor) {
                audit_fail("not_found", 404);
                reply_json(res, 404, json{
                    {"ok", false},
                    {"error", "not_found"},
                    {"message", "path not found"}
                }.dump());
                return;
            }

            const std::filesystem::path abs_path = item.abs_path;
            if (abs_path == user_dir) {
                audit_fail("refuse_root_delete", 400);
                reply_json(res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "refusing to delete root"}
                }.dump());
                return;
            }

            std::uint64_t freed_bytes = 0;
            std::uint64_t file_count = 0;
            std::error_code ec;

            auto root_st = std::filesystem::symlink_status(abs_path, ec);
            if (ec || !std::filesystem::exists(root_st)) {
                audit_fail("not_found", 404);
                reply_json(res, 404, json{
                    {"ok", false},
                    {"error", "not_found"},
                    {"message", "path not found"}
                }.dump());
                return;
            }

            if (std::filesystem::is_symlink(root_st)) {
                audit_fail("symlink_not_supported", 400);
                reply_json(res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "symlinks not supported"}
                }.dump());
                return;
            }

            if (!std::filesystem::is_directory(root_st)) {
                audit_fail("path_not_dir", 400);
                reply_json(res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "path is not a directory"}
                }.dump());
                return;
            }

            for (std::filesystem::recursive_directory_iterator it(
                     abs_path, std::filesystem::directory_options::skip_permission_denied, ec), end;
                 it != end && !ec;
                 it.increment(ec)) {
                std::error_code ec2;
                auto ent_st = std::filesystem::symlink_status(it->path(), ec2);
                if (ec2) {
                    audit_fail("tree_stat_failed", 500, ec2.message());
                    reply_json(res, 500, json{
                        {"ok", false},
                        {"error", "server_error"},
                        {"message", "delete scan failed"},
                        {"detail", pqnas::shorten(ec2.message(), 180)}
                    }.dump());
                    return;
                }

                if (std::filesystem::is_symlink(ent_st)) {
                    audit_fail("tree_contains_symlink", 409, it->path().string());
                    reply_json(res, 409, json{
                        {"ok", false},
                        {"error", "path_conflict"},
                        {"message", "directory contains symlinks"}
                    }.dump());
                    return;
                }

                if (std::filesystem::is_regular_file(ent_st)) {
                    std::error_code ec3;
                    auto sz = std::filesystem::file_size(it->path(), ec3);
                    if (ec3) {
                        audit_fail("file_size_failed", 500, ec3.message());
                        reply_json(res, 500, json{
                            {"ok", false},
                            {"error", "server_error"},
                            {"message", "delete scan failed"},
                            {"detail", pqnas::shorten(ec3.message(), 180)}
                        }.dump());
                        return;
                    }
                    ++file_count;
                    freed_bytes += static_cast<std::uint64_t>(sz);
                }
            }

            if (ec) {
                audit_fail("tree_walk_failed", 500, ec.message());
                reply_json(res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "delete scan failed"},
                    {"detail", pqnas::shorten(ec.message(), 180)}
                }.dump());
                return;
            }

            ec.clear();
            auto root_st2 = std::filesystem::symlink_status(abs_path, ec);
            if (ec || !std::filesystem::exists(root_st2)) {
                audit_fail("not_found", 404);
                reply_json(res, 404, json{
                    {"ok", false},
                    {"error", "not_found"},
                    {"message", "path not found"}
                }.dump());
                return;
            }

            if (std::filesystem::is_symlink(root_st2)) {
                audit_fail("symlink_not_supported", 400);
                reply_json(res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "symlinks not supported"}
                }.dump());
                return;
            }

            if (!std::filesystem::is_directory(root_st2)) {
                audit_fail("path_changed_before_delete", 409);
                reply_json(res, 409, json{
                    {"ok", false},
                    {"error", "path_conflict"},
                    {"message", "path changed before delete"}
                }.dump());
                return;
            }

            const std::string pool_id = u.storage_pool_id.empty() ? "default" : u.storage_pool_id;
            const std::filesystem::path storage_root = data_root_for_pool_id(pool_id);

            std::string terr;

            pqnas::TrashService::MoveToTrashParams tp;
            tp.scope_type = "user";
            tp.scope_id = fp_hex;
            tp.deleted_by_fp = fp_hex;
            tp.origin_app = "filemgr";
            tp.item_type = "dir";
            tp.original_rel_path = rel_norm;
            tp.payload_abs_path = abs_path;
            tp.storage_root = storage_root;
            tp.size_bytes = freed_bytes;
            tp.file_count = file_count;

            pqnas::TrashService::MoveToTrashResult tr;
            if (!trash_service.move_to_trash(tp, &tr, &terr)) {
                audit_fail("trash_move_failed", 500, terr);
                reply_json(res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "failed to move directory to trash"},
                    {"detail", pqnas::shorten(terr, 180)}
                }.dump());
                return;
            }
        record_user_file_activity_best_effort_local(
            users,
            fp_hex,
            "file.trashed",
            tp.original_rel_path,
            tp.item_type,
            tr.trash_id,
            tp.size_bytes,
            tp.file_count,
            &req
        );


            {
                std::string ferr;
                if (!pqnas::favorites_remove_under_prefix(user_dir, rel_norm, "dir", &ferr)) {
                    audit_warn("favorites_cleanup_failed", ferr, rel_norm);
                }
            }
            {
                std::string gerr;
                if (auto* gidx = pqnas::get_gallery_meta_index()) {
                    if (!gidx->erase_subtree("user", fp_hex, rel_norm, &gerr)) {
                        audit_warn("gallery_meta_cleanup_failed", gerr, rel_norm);
                    }
                }
            }
            {
                std::string rerr;
                if (!reelstack_meta_remove_under_prefix_path_local("user", fp_hex, rel_norm, "dir", &rerr)) {
                    audit_warn("reelstack_meta_cleanup_failed", rerr, rel_norm);
                }
            }

            audit_ok(rel_path, "dir", freed_bytes);
            reply_json(res, 200, json{
                {"ok", true},
                {"fingerprint_hex", fp_hex},
                {"path", rel_path},
                {"type", "dir"},
                {"freed_bytes", freed_bytes},
                {"trash_id", tr.trash_id}
            }.dump());
            return;
        }

        std::filesystem::path dir_abs;
        bool have_root = false;

        if (!subtree_rows.empty()) {
            const auto& first = subtree_rows.front();
            std::filesystem::path root = std::filesystem::path(first.physical_path);
            for (const auto& _ : std::filesystem::path(first.logical_rel_path)) {
                (void)_;
                root = root.parent_path();
            }
            dir_abs = root / std::filesystem::path(rel_norm);
            have_root = true;
        } else if (item.has_physical_anchor) {
            dir_abs = item.abs_path;
            have_root = true;
        }

        if (!have_root || dir_abs == user_dir) {
            audit_fail("refuse_root_delete", 400);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "refusing to delete root"}
            }.dump());
            return;
        }

        const std::string dir_root_s = dir_abs.lexically_normal().string();
        bool all_under_root = true;
        std::uint64_t freed_bytes = 0;
        const std::uint64_t file_count = static_cast<std::uint64_t>(subtree_rows.size());

        for (const auto& rec : subtree_rows) {
            const std::filesystem::path phys = std::filesystem::path(rec.physical_path).lexically_normal();
            const std::string phys_s = phys.string();

            if (phys_s != dir_root_s && phys_s.rfind(dir_root_s + "/", 0) != 0) {
                all_under_root = false;
                break;
            }
            freed_bytes += rec.size_bytes;
        }

        if (!all_under_root) {
            audit_fail("dir_delete_cross_root_unsupported", 409);
            reply_json(res, 409, json{
                {"ok", false},
                {"error", "unsupported"},
                {"message", "directory delete across mixed storage roots not supported yet"}
            }.dump());
            return;
        }

        std::error_code ec;

        auto dir_st = std::filesystem::symlink_status(dir_abs, ec);
        if (ec || !std::filesystem::exists(dir_st)) {
            audit_fail("not_found", 404);
            reply_json(res, 404, json{
                {"ok", false},
                {"error", "not_found"},
                {"message", "path not found"}
            }.dump());
            return;
        }

        if (std::filesystem::is_symlink(dir_st)) {
            audit_fail("symlink_not_supported", 400);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "symlinks not supported"}
            }.dump());
            return;
        }

        if (!std::filesystem::is_directory(dir_st)) {
            audit_fail("path_not_dir", 400);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "path is not a directory"}
            }.dump());
            return;
        }

        for (std::filesystem::recursive_directory_iterator it(
                 dir_abs, std::filesystem::directory_options::skip_permission_denied, ec), end;
             it != end && !ec;
             it.increment(ec)) {
            std::error_code ec2;
            auto ent_st = std::filesystem::symlink_status(it->path(), ec2);
            if (ec2) {
                audit_fail("tree_stat_failed", 500, ec2.message());
                reply_json(res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "delete scan failed"},
                    {"detail", pqnas::shorten(ec2.message(), 180)}
                }.dump());
                return;
            }

            if (std::filesystem::is_symlink(ent_st)) {
                audit_fail("tree_contains_symlink", 409, it->path().string());
                reply_json(res, 409, json{
                    {"ok", false},
                    {"error", "path_conflict"},
                    {"message", "directory contains symlinks"}
                }.dump());
                return;
            }
        }

        if (ec) {
            audit_fail("tree_walk_failed", 500, ec.message());
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "delete scan failed"},
                {"detail", pqnas::shorten(ec.message(), 180)}
            }.dump());
            return;
        }

        ec.clear();
        auto dir_st2 = std::filesystem::symlink_status(dir_abs, ec);
        if (ec || !std::filesystem::exists(dir_st2)) {
            audit_fail("not_found", 404);
            reply_json(res, 404, json{
                {"ok", false},
                {"error", "not_found"},
                {"message", "path not found"}
            }.dump());
            return;
        }

        if (std::filesystem::is_symlink(dir_st2)) {
            audit_fail("symlink_not_supported", 400);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "symlinks not supported"}
            }.dump());
            return;
        }

        if (!std::filesystem::is_directory(dir_st2)) {
            audit_fail("path_changed_before_delete", 409);
            reply_json(res, 409, json{
                {"ok", false},
                {"error", "path_conflict"},
                {"message", "path changed before delete"}
            }.dump());
            return;
        }

        const std::string pool_id = u.storage_pool_id.empty() ? "default" : u.storage_pool_id;
        const std::filesystem::path storage_root = data_root_for_pool_id(pool_id);

        std::string terr;

        pqnas::TrashService::MoveToTrashParams tp;
        tp.scope_type = "user";
        tp.scope_id = fp_hex;
        tp.deleted_by_fp = fp_hex;
        tp.origin_app = "filemgr";
        tp.item_type = "dir";
        tp.original_rel_path = rel_norm;
        tp.payload_abs_path = dir_abs;
        tp.storage_root = storage_root;
        tp.size_bytes = freed_bytes;
        tp.file_count = file_count;

        if (!subtree_rows.empty()) {
            tp.source_pool = subtree_rows.front().current_pool;
            tp.source_tier_state = subtree_rows.front().tier_state;
        }

        pqnas::TrashService::MoveToTrashResult tr;
        if (!trash_service.move_to_trash(tp, &tr, &terr)) {
            audit_fail("trash_move_failed", 500, terr);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to move directory to trash"},
                {"detail", pqnas::shorten(terr, 180)}
            }.dump());
            return;
        }
        record_user_file_activity_best_effort_local(
            users,
            fp_hex,
            "file.trashed",
            tp.original_rel_path,
            tp.item_type,
            tr.trash_id,
            tp.size_bytes,
            tp.file_count,
            &req
        );


        if (item.from_metadata) {
            std::string derr;
            if (!idx->erase_subtree(fp_hex, rel_norm, &derr)) {
                audit_fail("metadata_erase_subtree_failed", 500, derr);
                reply_json(res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "delete metadata cleanup failed"},
                    {"detail", pqnas::shorten(derr, 180)}
                }.dump());
                return;
            }
        } else {
            audit_warn("metadata_erase_skipped_legacy_dir", "legacy physical directory had no metadata subtree", rel_norm);
        }

        {
            std::string ferr;
            if (!pqnas::favorites_remove_under_prefix(user_dir, rel_norm, "dir", &ferr)) {
                audit_warn("favorites_cleanup_failed", ferr, rel_norm);
            }
        }
        {
            std::string gerr;
            if (auto* gidx = pqnas::get_gallery_meta_index()) {
                if (!gidx->erase_subtree("user", fp_hex, rel_norm, &gerr)) {
                    audit_warn("gallery_meta_cleanup_failed", gerr, rel_norm);
                }
            }
        }

        audit_ok(rel_path, "dir", freed_bytes);
        reply_json(res, 200, json{
            {"ok", true},
            {"fingerprint_hex", fp_hex},
            {"path", rel_path},
            {"type", "dir"},
            {"freed_bytes", freed_bytes},
            {"trash_id", tr.trash_id}
        }.dump());
        return;
    }

    audit_fail("unsupported_type", 400);
    reply_json(res, 400, json{
        {"ok", false},
        {"error", "bad_request"},
        {"message", "unsupported path type"}
    }.dump());
});

// GET /api/v4/files/list?path=relative/dir   (path optional; empty => user root)
// Response: JSON listing of immediate children (no recursion)
srv.Get("/api/v4/files/list", [&](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;

    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto audit_fail = [&](const std::string& reason, int http, const std::string& detail = "") {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_list_fail";
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

    auto audit_ok = [&](const std::string& rel_dir, std::size_t count) {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_list_ok";
        ev.outcome = "ok";
        ev.f["fingerprint"] = fp_hex;
        ev.f["path"] = pqnas::shorten(rel_dir, 200);
        ev.f["count"] = std::to_string((unsigned long long)count);

        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
        audit_append(ev);
    };

    // Storage allocated check (fail-closed)
    {
        auto uopt = users.get(fp_hex);
        if (!uopt.has_value()) {
            audit_fail("user_missing", 403);
            reply_json(res, 403, json{{"ok", false}, {"error", "forbidden"}, {"message", "policy denied"}}.dump());
            return;
        }
        const auto& u = *uopt;
        if (u.storage_state != "allocated") {
            audit_fail("storage_unallocated", 403);
            reply_json(res, 403, json{
                {"ok", false},
                {"error", "storage_unallocated"},
                {"message", "Storage not allocated"},
                {"fingerprint_hex", fp_hex},
                {"quota_bytes", u.quota_bytes}
            }.dump());
            return;
        }
    }

    // path param (optional). Empty => root listing.
    std::string rel_dir;
    if (req.has_param("path")) rel_dir = req.get_param_value("path");

    const std::filesystem::path user_dir = user_dir_for_fp(users, fp_hex);
    std::filesystem::path abs_dir = user_dir;
    std::string rel_dir_norm;

    if (!rel_dir.empty()) {
        std::string nerr;
        if (!pqnas::normalize_user_rel_path_strict(rel_dir, &rel_dir_norm, &nerr)) {
            audit_fail("invalid_path", 400, nerr);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "invalid path"}
            }.dump());
            return;
        }

        // Physical tree is still resolved from legacy visible root.
        std::string perr;
        if (!pqnas::resolve_user_path_strict(user_dir, rel_dir_norm, &abs_dir, &perr)) {
            // Do NOT fail yet. Directory may exist only logically via metadata.
            abs_dir.clear();
        }
    }

    auto is_reserved_name = [&](const std::string& name) -> bool {
        return name == ".pqnas";
    };

    struct ListedItem {
        std::string name;
        std::string type;
        std::uint64_t size_bytes = 0;
        long long mtime_unix = 0;
    };

    std::map<std::string, ListedItem> merged;

    // 1) Legacy filesystem children under current visible directory, if physical dir exists.
    // Symlinks are never followed here: a symlinked listed directory is rejected,
    // and symlink children are skipped entirely.
    bool legacy_dir_ok = false;
    {
        std::error_code ec;
        std::filesystem::path dir_to_check = abs_dir.empty() ? user_dir : abs_dir;
        auto st = std::filesystem::symlink_status(dir_to_check, ec);

        if (!ec && std::filesystem::exists(st)) {
            if (std::filesystem::is_symlink(st)) {
                audit_fail("symlink_not_supported", 400);
                reply_json(res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "symlinks not supported"}
                }.dump());
                return;
            }

            if (std::filesystem::is_directory(st)) {
                legacy_dir_ok = true;

                for (std::filesystem::directory_iterator it(dir_to_check, ec), end; it != end && !ec; it.increment(ec)) {
                    std::error_code ec2;

                    const auto child_path = it->path();
                    const auto name = child_path.filename().string();
                    if (name == "." || name == ".." || name.empty()) continue;
                    if (is_reserved_name(name)) continue;

                    auto child_st = std::filesystem::symlink_status(child_path, ec2);
                    if (ec2) continue;

                    if (std::filesystem::is_symlink(child_st)) {
                        continue;
                    }

                    std::string type = "other";
                    if (std::filesystem::is_directory(child_st)) {
                        type = "dir";
                    } else if (std::filesystem::is_regular_file(child_st)) {
                        type = "file";
                    } else {
                        continue;
                    }

                    std::uint64_t size_bytes = 0;
                    if (type == "file") {
                        ec2.clear();
                        auto sz = std::filesystem::file_size(child_path, ec2);
                        if (!ec2) size_bytes = static_cast<std::uint64_t>(sz);
                    }

                    long long mtime_unix = 0;
                    ec2.clear();
                    auto ft = std::filesystem::last_write_time(child_path, ec2);
                    if (!ec2) {
                        using namespace std::chrono;
                        auto sctp = time_point_cast<system_clock::duration>(
                            ft - decltype(ft)::clock::now() + system_clock::now()
                        );
                        mtime_unix = (long long)duration_cast<seconds>(sctp.time_since_epoch()).count();
                    }

                    merged[name] = ListedItem{
                        name,
                        type,
                        size_bytes,
                        mtime_unix
                    };
                }
            }
        }
    }

    // 2) Merge metadata-backed immediate children.
    bool metadata_ok = false;
    if (auto* idx = pqnas::get_file_location_index()) {
        std::string lerr;
        auto rows = idx->list_immediate_children(fp_hex, rel_dir_norm, &lerr);
        if (!lerr.empty()) {
            audit_fail("metadata_list_failed", 500, lerr);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "metadata list failed"},
                {"detail", pqnas::shorten(lerr, 180)}
            }.dump());
            return;
        }

        if (!rows.empty()) metadata_ok = true;

        for (const auto& rec : rows) {
            if (rec.name.empty()) continue;
            if (is_reserved_name(rec.name)) continue;

            auto mit = merged.find(rec.name);
            if (mit == merged.end()) {
                merged[rec.name] = ListedItem{
                    rec.name,
                    rec.type.empty() ? "file" : rec.type,
                    rec.type == "file" ? rec.size_bytes : 0,
                    (long long)rec.mtime_epoch
                };
            } else {
                // Metadata wins over legacy for files and dirs.
                mit->second.type = rec.type.empty() ? mit->second.type : rec.type;
                if (mit->second.type == "file") {
                    mit->second.size_bytes = rec.size_bytes;
                } else {
                    mit->second.size_bytes = 0;
                }
                mit->second.mtime_unix = (long long)rec.mtime_epoch;
            }
        }
    }

    // If neither physical dir nor metadata children exist, report not found for non-root paths.
    if (!legacy_dir_ok && !metadata_ok && !rel_dir_norm.empty()) {
        audit_fail("not_found", 404);
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "directory not found"}
        }.dump());
        return;
    }

    json out;
    out["ok"] = true;
    out["fingerprint_hex"] = fp_hex;
    out["path"] = rel_dir_norm.empty() ? rel_dir : rel_dir_norm;
    out["items"] = json::array();

    std::size_t count = 0;
    for (const auto& kv : merged) {
        out["items"].push_back(json{
            {"name", kv.second.name},
            {"type", kv.second.type},
            {"size_bytes", kv.second.size_bytes},
            {"mtime_unix", kv.second.mtime_unix}
        });
        count++;
        if (count >= 5000) break;
    }

    audit_ok(rel_dir_norm.empty() ? rel_dir : rel_dir_norm, count);
    reply_json(res, 200, out.dump());
});
    // GET /api/v4/me/storage   (user/admin)
// Purpose: UI-friendly storage/quota summary for the *current* user.
// - Soft UI: filemgr can poll this and show warnings at 80%/90%
// - Does NOT expose mountpoints; only user dir usage.
//
// Response example:
// {
//   "ok": true,
//   "fingerprint_hex": "...",
//   "role": "user",
//   "storage_state": "allocated",
//   "quota_bytes": 10737418240,
//   "used_bytes": 123456789,
//   "used_percent": 1.149,
//   "warn_level": "ok"|"warn"|"crit",
//   "scanned_entries": 1234,
//   "partial": false,
//   "ts": "..."
// }
srv.Get("/api/v4/me/storage", [&](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;

    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto audit_ok = [&](const json& extra) {
        pqnas::AuditEvent ev;
        ev.event = "v4.me_storage_ok";
        ev.outcome = "ok";
        ev.f["fingerprint"] = fp_hex;
        ev.f["role"] = role;
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();

        // Optional small extras
        try {
            if (extra.contains("storage_state")) ev.f["storage_state"] = pqnas::shorten(extra["storage_state"].get<std::string>(), 40);
            if (extra.contains("warn_level"))    ev.f["warn_level"]    = pqnas::shorten(extra["warn_level"].get<std::string>(), 16);
            if (extra.contains("partial"))       ev.f["partial"]       = extra["partial"].get<bool>() ? "true" : "false";
        } catch (...) {}

        audit_append(ev);
    };

    auto audit_fail = [&](const std::string& reason, int http, const std::string& detail = "") {
        pqnas::AuditEvent ev;
        ev.event = "v4.me_storage_fail";
        ev.outcome = "fail";
        ev.f["fingerprint"] = fp_hex;
        ev.f["role"] = role;
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

    // UsersRegistry lookup (fail-closed)
    auto uopt = users.get(fp_hex);
    if (!uopt.has_value()) {
        audit_fail("user_missing", 403);
        reply_json(res, 403, json{
            {"ok", false},
            {"error", "forbidden"},
            {"message", "policy denied"}
        }.dump());
        return;
    }
    const auto& u = *uopt;

    const std::string now_iso = now_iso_utc();
    const std::filesystem::path user_dir = user_dir_for_fp(users, fp_hex);

    json out;
    out["ok"] = true;
    out["fingerprint_hex"] = fp_hex;
    out["role"] = role;
    out["storage_state"] = u.storage_state.empty() ? "" : u.storage_state;
    out["quota_bytes"] = (std::uint64_t)u.quota_bytes;
    out["used_bytes"] = (std::uint64_t)0;
    out["live_bytes"] = (std::uint64_t)0;
    out["trash_bytes"] = (std::uint64_t)0;
    out["used_percent"] = 0.0;
    out["warn_level"] = "ok";
    out["partial"] = false;
    out["scanned_entries"] = 0;
    out["ts"] = now_iso;

    // If not allocated, return ok:true (UI uses this to show friendly state).
    if (u.storage_state != "allocated") {
        audit_ok(out);
        reply_json(res, 200, out.dump());
        return;
    }

    // Best-effort: scan user_dir recursively for used_bytes.
    // Caps keep it safe for huge trees.
    const std::uint64_t ENTRY_CAP = 250000;  // max files/dirs visited
    const long long TIME_CAP_MS   = 250;     // soft time cap

    std::uint64_t used = 0;
    std::uint64_t entries = 0;
    bool partial = false;

    std::error_code ec;
    auto st = std::filesystem::status(user_dir, ec);
    if (ec || !std::filesystem::exists(st) || !std::filesystem::is_directory(st)) {
        // allocated but directory missing => treat as 0 used, but mark partial + warn
        out["used_bytes"] = (std::uint64_t)0;
        out["partial"] = true;
        out["warn_level"] = "warn";
        out["note"] = "user_dir_missing_or_not_dir";
        audit_ok(out);
        reply_json(res, 200, out.dump());
        return;
    }

    const auto t0 = std::chrono::steady_clock::now();

    std::filesystem::recursive_directory_iterator it(
        user_dir,
        std::filesystem::directory_options::skip_permission_denied,
        ec
    );
    std::filesystem::recursive_directory_iterator end;

    for (; it != end && !ec; it.increment(ec)) {
        entries++;
        if (entries >= ENTRY_CAP) { partial = true; break; }

        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0
        ).count();
        if (elapsed >= TIME_CAP_MS) { partial = true; break; }

        // skip internal quota metadata
        {
            std::error_code qec;
            const auto rel = it->path().lexically_relative(user_dir.lexically_normal());
            auto rit = rel.begin();
            if (rit != rel.end() && rit->string() == ".pqnas_activity") {
                if (it->is_directory(qec) && !qec) {
                    it.disable_recursion_pending();
                }
                continue;
            }
        }

        std::error_code ec2;
        if (it->is_regular_file(ec2) && !ec2) {
            ec2.clear();
            auto sz = it->file_size(ec2);
            if (!ec2) used += (std::uint64_t)sz;
        }
    }
    if (ec) partial = true;

    const std::uint64_t live_bytes = used;

    std::uint64_t trash_bytes = 0;
    std::string trash_sum_err;
    if (!trash_index.sum_active_scope_bytes("user", fp_hex, &trash_bytes, &trash_sum_err)) {
        partial = true;
        out["trash_bytes_error"] = pqnas::shorten(trash_sum_err, 180);
        trash_bytes = 0;
    }

    std::uint64_t effective_used = live_bytes;
    if (std::numeric_limits<std::uint64_t>::max() - effective_used < trash_bytes) {
        effective_used = std::numeric_limits<std::uint64_t>::max();
    } else {
        effective_used += trash_bytes;
    }

    out["live_bytes"] = (std::uint64_t)live_bytes;
    out["trash_bytes"] = (std::uint64_t)trash_bytes;
    out["used_bytes"] = (std::uint64_t)effective_used;
    out["scanned_entries"] = (std::uint64_t)entries;
    out["partial"] = partial;

    // used_percent (quota==0 => define as 0, but warn if used>0)
    double pct = 0.0;
    if (u.quota_bytes > 0) {
        pct = (double)effective_used * 100.0 / (double)u.quota_bytes;
    } else {
        pct = (effective_used > 0) ? 100.0 : 0.0;
    }
    out["used_percent"] = pct;

    // Soft warning levels (tune as you like)
    // - warn >= 80%
    // - crit >= 90%
    std::string lvl = "ok";
    if (pct >= 90.0) lvl = "crit";
    else if (pct >= 80.0) lvl = "warn";
    if (partial && lvl == "ok") lvl = "warn"; // partial scan => at least warn
    out["warn_level"] = lvl;

    audit_ok(out);
    reply_json(res, 200, out.dump());
});

    // POST /api/v4/files/exists?path=rel/path
srv.Post("/api/v4/files/exists", [&](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;

    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto add_ip_headers = [&](pqnas::AuditEvent& ev) {
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
    };

    auto audit_fail = [&](const std::string& reason, int http, const std::string& detail = "",
                          const std::string& path_rel = "") {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_exists_fail";
        ev.outcome = "fail";
        ev.f["fingerprint"] = fp_hex;
        ev.f["reason"] = reason;
        ev.f["http"] = std::to_string(http);
        if (!path_rel.empty()) ev.f["path"] = pqnas::shorten(path_rel, 200);
        if (!detail.empty())   ev.f["detail"] = pqnas::shorten(detail, 180);

        add_ip_headers(ev);
        audit_append(ev);
    };

    auto audit_ok = [&](const std::string& path_rel,
                        const std::string& type,
                        bool exists,
                        std::uint64_t bytes) {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_exists_ok";
        ev.outcome = "ok";
        ev.f["fingerprint"] = fp_hex;
        ev.f["path"] = pqnas::shorten(path_rel, 200);
        ev.f["exists"] = exists ? "1" : "0";
        ev.f["type"] = type;
        if (exists && type == "file") ev.f["bytes"] = std::to_string((unsigned long long)bytes);

        add_ip_headers(ev);
        audit_append(ev);
    };

    // must have allocated storage
    auto uopt = users.get(fp_hex);
    if (!uopt.has_value() || uopt->storage_state != "allocated") {
        audit_fail("storage_unallocated", 403);
        reply_json(res, 403, json{
            {"ok", false},
            {"error", "storage_unallocated"},
            {"message", "Storage not allocated"}
        }.dump());
        return;
    }

    std::string path_rel;
    if (req.has_param("path")) path_rel = req.get_param_value("path");

    if (path_rel.empty()) {
        audit_fail("missing_path", 400);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "missing path"}
        }.dump());
        return;
    }

pqnas::ResolvedExistingPath rp;
std::string err;
if (!pqnas::resolve_existing_user_file_path(users, fp_hex, path_rel, &rp, &err)) {
    // Distinguish invalid path from just "not found"
    std::string rel_norm;
    std::string nerr;
    if (!pqnas::normalize_user_rel_path_strict(path_rel, &rel_norm, &nerr)) {
        audit_fail("invalid_path", 400, nerr, path_rel);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid path"}
        }.dump());
        return;
    }

    audit_ok(path_rel, "missing", false, 0);
    reply_json(res, 200, json{
        {"ok", true},
        {"path", path_rel},
        {"exists", false},
        {"type", "missing"},
        {"bytes", 0}
    }.dump());
    return;
}

    const std::filesystem::path path_abs = rp.abs_path;

    // Never trust a path that traverses an existing symlinked parent chain.
    {
        std::string serr;
        if (!ensure_no_symlink_in_existing_path_prefix(path_abs, &serr)) {
            audit_ok(path_rel, "missing", false, 0);
            reply_json(res, 200, json{
                {"ok", true},
                {"path", path_rel},
                {"exists", false},
                {"type", "missing"},
                {"bytes", 0}
            }.dump());
            return;
        }
    }

    std::error_code ec;
    auto st = std::filesystem::symlink_status(path_abs, ec);

    bool exists = (!ec && std::filesystem::exists(st));

    std::string type = "missing";
    std::uint64_t bytes = 0;

    if (exists) {
        if (std::filesystem::is_symlink(st)) {
            exists = false;
            type = "missing";
        } else if (std::filesystem::is_regular_file(st)) {
            type = "file";
            bytes = pqnas::file_size_u64_safe(path_abs);
        } else if (std::filesystem::is_directory(st)) {
            type = "dir";
        } else {
            type = "other";
        }
    }

    audit_ok(path_rel, type, exists, bytes);

    reply_json(res, 200, json{
        {"ok", true},
        {"path", path_rel},
        {"exists", exists},
        {"type", type},
        {"bytes", bytes}
    }.dump());
});


    // POST /api/v4/files/copy?from=old/path&to=new/path
srv.Post("/api/v4/files/copy", [&](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;
    if (!require_same_origin_for_cookie_mutation(req, res)) return;
    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto add_ip_headers = [&](pqnas::AuditEvent& ev) {
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
    };

    auto audit_fail = [&](const std::string& reason, int http, const std::string& detail = "",
                          const std::string& from_rel = "", const std::string& to_rel = "") {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_copy_fail";
        ev.outcome = "fail";
        ev.f["fingerprint"] = fp_hex;
        ev.f["reason"] = reason;
        ev.f["http"] = std::to_string(http);
        if (!from_rel.empty()) ev.f["from"] = pqnas::shorten(from_rel, 200);
        if (!to_rel.empty())   ev.f["to"]   = pqnas::shorten(to_rel, 200);
        if (!detail.empty())   ev.f["detail"] = pqnas::shorten(detail, 180);

        add_ip_headers(ev);
        audit_append(ev);
    };

    auto audit_quota = [&](int http,
                           const std::string& detail,
                           const std::string& from_rel,
                           const std::string& to_rel,
                           std::uint64_t src_bytes,
                           std::uint64_t dst_old_bytes,
                           std::uint64_t delta_bytes) {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_copy_quota_exceeded";
        ev.outcome = "fail";
        ev.f["fingerprint"] = fp_hex;
        ev.f["reason"] = "quota_exceeded";
        ev.f["http"] = std::to_string(http);
        ev.f["from"] = pqnas::shorten(from_rel, 200);
        ev.f["to"]   = pqnas::shorten(to_rel, 200);
        ev.f["src_bytes"] = std::to_string((unsigned long long)src_bytes);
        ev.f["dst_old_bytes"] = std::to_string((unsigned long long)dst_old_bytes);
        ev.f["delta_bytes"] = std::to_string((unsigned long long)delta_bytes);
        if (!detail.empty()) ev.f["detail"] = pqnas::shorten(detail, 180);

        add_ip_headers(ev);
        audit_append(ev);
    };

    auto audit_ok = [&](const std::string& from_rel,
                        const std::string& to_rel,
                        std::uint64_t src_bytes,
                        std::uint64_t dst_old_bytes,
                        std::uint64_t delta_bytes,
                        bool overwrote) {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_copy_ok";
        ev.outcome = "ok";
        ev.f["fingerprint"] = fp_hex;
        ev.f["from"] = pqnas::shorten(from_rel, 200);
        ev.f["to"]   = pqnas::shorten(to_rel, 200);
        ev.f["type"] = "file";
        ev.f["src_bytes"] = std::to_string((unsigned long long)src_bytes);
        ev.f["dst_old_bytes"] = std::to_string((unsigned long long)dst_old_bytes);
        ev.f["delta_bytes"] = std::to_string((unsigned long long)delta_bytes);
        ev.f["overwrote"] = overwrote ? "1" : "0";

        add_ip_headers(ev);
        audit_append(ev);
    };

    // must have allocated storage
    auto uopt = users.get(fp_hex);
    if (!uopt.has_value() || uopt->storage_state != "allocated") {
        audit_fail("storage_unallocated", 403);
        reply_json(res, 403, json{
            {"ok", false},
            {"error", "storage_unallocated"},
            {"message", "Storage not allocated"}
        }.dump());
        return;
    }

    std::string from_rel, to_rel;
    if (req.has_param("from")) from_rel = req.get_param_value("from");
    if (req.has_param("to"))   to_rel   = req.get_param_value("to");

    if (from_rel.empty() || to_rel.empty()) {
        audit_fail("missing_from_or_to", 400);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "missing from or to"}
        }.dump());
        return;
    }

	const std::filesystem::path user_dir = user_dir_for_fp(users, fp_hex);

	std::string from_rel_norm, to_rel_norm;
	std::string nerr1, nerr2;

	if (!pqnas::normalize_user_rel_path_strict(from_rel, &from_rel_norm, &nerr1)) {
    	audit_fail("invalid_from_path", 400, nerr1, from_rel, to_rel);
	    reply_json(res, 400, json{
    	    {"ok", false},
        	{"error", "bad_request"},
	        {"message", "invalid from path"}
    	}.dump());
    	return;
	}

	if (!pqnas::normalize_user_rel_path_strict(to_rel, &to_rel_norm, &nerr2)) {
    	audit_fail("invalid_to_path", 400, nerr2, from_rel, to_rel);
	    reply_json(res, 400, json{
    	    {"ok", false},
        	{"error", "bad_request"},
	        {"message", "invalid to path"}
    	}.dump());
    	return;
	}

	if (from_rel_norm == to_rel_norm) {
    	audit_fail("same_path", 400, "", from_rel, to_rel);
	    reply_json(res, 400, json{
    	    {"ok", false},
        	{"error", "bad_request"},
	        {"message", "from and to are the same"}
    	}.dump());
    	return;
	}

	auto* idx = pqnas::get_file_location_index();
	if (!idx) {
    	audit_fail("metadata_index_missing", 500, "", from_rel, to_rel);
	    reply_json(res, 500, json{
    	    {"ok", false},
        	{"error", "server_error"},
	        {"message", "metadata index missing"}
    	}.dump());
    	return;
	}

	// Resolve logical source exactly like move/stat-era code.
	pqnas::ResolvedLogicalItem src;
	std::string rerr;
	if (!pqnas::resolve_existing_user_item(users, fp_hex, from_rel_norm, &src, &rerr) || !src.exists) {
    	audit_fail("not_found", 404, rerr, from_rel, to_rel);
    	reply_json(res, 404, json{
        	{"ok", false},
        	{"error", "not_found"},
        	{"message", "source not found"}
	    }.dump());
    	return;
	}
	if (!src.is_file) {
    	audit_fail("src_not_file", 400, "", from_rel, to_rel);
	    reply_json(res, 400, json{
    	    {"ok", false},
        	{"error", "bad_request"},
	        {"message", "source must be a file (directories not supported yet)"}
    	}.dump());
    	return;
	}

	std::string gerr;
	auto src_rec = idx->get(fp_hex, from_rel_norm, &gerr);
	if (!src_rec.has_value()) {
	    audit_fail("metadata_source_missing", 500, gerr, from_rel, to_rel);
    	reply_json(res, 500, json{
        	{"ok", false},
	        {"error", "server_error"},
    	    {"message", "source metadata missing"},
        	{"detail", pqnas::shorten(gerr, 180)}
	    }.dump());
    	return;
	}

	const std::filesystem::path from_abs = src.abs_path;
	const std::filesystem::path old_phys = std::filesystem::path(src_rec->physical_path);
	const std::filesystem::path old_logical_path = std::filesystem::path(from_rel_norm);

	std::filesystem::path physical_root = old_phys;
	for (const auto& _ : old_logical_path) {
    	(void)_;
    	physical_root = physical_root.parent_path();
	}

	const std::filesystem::path to_abs = physical_root / std::filesystem::path(to_rel_norm);

	// Destination must not already exist logically.
	{
    	pqnas::ResolvedLogicalItem dst_existing;
	    std::string derr;
    	if (pqnas::resolve_existing_user_item(users, fp_hex, to_rel_norm, &dst_existing, &derr) &&
        	dst_existing.exists) {
	        audit_fail("dest_exists", 409, "", from_rel, to_rel);
    	    reply_json(res, 409, json{
        	    {"ok", false},
            	{"error", "dest_exists"},
            	{"message", "destination already exists"}
	        }.dump());
    	    return;
    	}
	}

    std::error_code ec;
    auto st_src = std::filesystem::symlink_status(from_abs, ec);
    if (ec || !std::filesystem::exists(st_src)) {
        audit_fail("not_found", 404, "", from_rel, to_rel);
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "source not found"}
        }.dump());
        return;
    }

    if (std::filesystem::is_symlink(st_src)) {
        audit_fail("symlink_not_supported", 400, "", from_rel, to_rel);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "symlinks not supported"}
        }.dump());
        return;
    }

    if (!std::filesystem::is_regular_file(st_src)) {
        audit_fail("src_not_file", 400, "", from_rel, to_rel);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "source must be a file"}
        }.dump());
        return;
    }

    const std::uint64_t src_bytes = pqnas::file_size_u64_safe(from_abs);

    // Physical destination may or may not already exist; refuse symlinks.
    bool dst_exists = false;
    std::uint64_t dst_old_bytes = 0;

    ec.clear();
    auto st_dst = std::filesystem::symlink_status(to_abs, ec);
    if (ec) {
        if (ec != std::make_error_code(std::errc::no_such_file_or_directory)) {
            audit_fail("dest_stat_failed", 500, ec.message(), from_rel, to_rel);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "destination stat failed"},
                {"detail", pqnas::shorten(ec.message(), 180)}
            }.dump());
            return;
        }
        ec.clear();
    } else if (std::filesystem::exists(st_dst)) {
        dst_exists = true;

        if (std::filesystem::is_symlink(st_dst)) {
            audit_fail("symlink_not_supported", 400, "", from_rel, to_rel);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "symlinks not supported"}
            }.dump());
            return;
        }

        if (!std::filesystem::is_regular_file(st_dst)) {
            audit_fail("dst_not_file", 400, "", from_rel, to_rel);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "destination exists and is not a file"}
            }.dump());
            return;
        }

        dst_old_bytes = pqnas::file_size_u64_safe(to_abs);
    }

        auto ensure_no_symlink_dir_chain = [&](const std::filesystem::path& base,
                                           const std::filesystem::path& dir,
                                           bool require_exists,
                                           std::string* out_err) -> bool {
        if (out_err) out_err->clear();

        std::filesystem::path cur = base.lexically_normal();
        const std::filesystem::path target = dir.lexically_normal();

        std::error_code ec;
        auto base_st = std::filesystem::symlink_status(cur, ec);
        if (ec) {
            if (out_err) *out_err = "base symlink_status failed: " + ec.message();
            return false;
        }
        if (!std::filesystem::exists(base_st)) {
            if (out_err) *out_err = "base directory missing";
            return false;
        }
        if (std::filesystem::is_symlink(base_st)) {
            if (out_err) *out_err = "base directory is symlink";
            return false;
        }
        if (!std::filesystem::is_directory(base_st)) {
            if (out_err) *out_err = "base path is not a directory";
            return false;
        }

        const auto rel = target.lexically_relative(cur);
        if (rel.empty() || rel == ".") return true;

        for (const auto& part : rel) {
            if (part.empty()) continue;
            cur /= part;

            ec.clear();
            auto st = std::filesystem::symlink_status(cur, ec);
            if (ec) {
                if (out_err) *out_err = "symlink_status failed: " + ec.message();
                return false;
            }

            if (!std::filesystem::exists(st)) {
                if (require_exists) {
                    if (out_err) *out_err = "directory missing";
                    return false;
                }
                continue;
            }

            if (std::filesystem::is_symlink(st)) {
                if (out_err) *out_err = "directory chain contains symlink";
                return false;
            }

            if (!std::filesystem::is_directory(st)) {
                if (out_err) *out_err = "path component is not a directory";
                return false;
            }
        }

        return true;
    };

    const std::filesystem::path dst_parent = to_abs.parent_path();

    {
        std::string derr;
        if (!ensure_no_symlink_dir_chain(physical_root, dst_parent, false, &derr)) {
            audit_fail("dest_parent_invalid", 409, derr, from_rel, to_rel);
            reply_json(res, 409, json{
                {"ok", false},
                {"error", "path_conflict"},
                {"message", "destination parent path is invalid"},
                {"detail", pqnas::shorten(derr, 180)}
            }.dump());
            return;
        }
    }

    ec.clear();
    std::filesystem::create_directories(dst_parent, ec);
    if (ec) {
        audit_fail("mkdir_failed", 500, ec.message(), from_rel, to_rel);
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "failed to create destination directories"},
            {"detail", pqnas::shorten(ec.message(), 180)}
        }.dump());
        return;
    }

    {
        std::string derr;
        if (!ensure_no_symlink_dir_chain(physical_root, dst_parent, true, &derr)) {
            audit_fail("dest_parent_invalid", 409, derr, from_rel, to_rel);
            reply_json(res, 409, json{
                {"ok", false},
                {"error", "path_conflict"},
                {"message", "destination parent path is invalid"},
                {"detail", pqnas::shorten(derr, 180)}
            }.dump());
            return;
        }
    }

    // quota-aware: delta = src_bytes - dst_old_bytes (only if positive)
    std::uint64_t delta_bytes = 0;
    if (src_bytes > dst_old_bytes) delta_bytes = (src_bytes - dst_old_bytes);

    if (delta_bytes > 0) {
        pqnas::QuotaCheckResult qc = pqnas::quota_check_for_upload_v1(
            users, fp_hex, user_dir, to_rel_norm, delta_bytes
        );
        if (!qc.ok) {
            if (qc.error == "storage_unallocated") {
                audit_fail("storage_unallocated", 403, "", from_rel, to_rel);
                reply_json(res, 403, json{
                    {"ok", false},
                    {"error", "storage_unallocated"},
                    {"message", "Storage not allocated"},
                    {"fingerprint_hex", fp_hex},
                    {"quota_bytes", qc.quota_bytes},
                    {"incoming_bytes", qc.incoming_bytes}
                }.dump());
                return;
            }

            if (qc.error == "invalid_path") {
                audit_fail("invalid_to_path", 400, "", from_rel, to_rel);
                reply_json(res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "invalid to path"}
                }.dump());
                return;
            }

            if (qc.error == "quota_exceeded") {
                const int http = 403;
                audit_quota(http, "", from_rel, to_rel, src_bytes, dst_old_bytes, delta_bytes);

                reply_json(res, http, json{
                    {"ok", false},
                    {"error", "quota_exceeded"},
                    {"message", "Quota exceeded"},
                    {"fingerprint_hex", fp_hex},
                    {"used_bytes", qc.used_bytes},
                    {"quota_bytes", qc.quota_bytes},
                    {"free_bytes", (qc.quota_bytes > qc.used_bytes ? (qc.quota_bytes - qc.used_bytes) : 0)},
                    {"incoming_bytes", qc.incoming_bytes},
                    {"existing_bytes", qc.existing_bytes},
                    {"would_used_bytes", qc.would_used_bytes}
                }.dump());
                return;
            }

            audit_fail("quota_check_failed", 403, qc.error, from_rel, to_rel);
            reply_json(res, 403, json{
                {"ok", false},
                {"error", "forbidden"},
                {"message", "policy denied"}
            }.dump());
            return;
        }
    }

    const std::filesystem::path tmp =
        dst_parent /
        (to_abs.filename().string() + ".tmp.copy." + random_b64url(12));

    ec.clear();
    {
        auto tmp_st = std::filesystem::symlink_status(tmp, ec);
        if (!ec && std::filesystem::exists(tmp_st)) {
            if (std::filesystem::is_symlink(tmp_st)) {
                audit_fail("tmp_is_symlink", 409, "", from_rel, to_rel);
                reply_json(res, 409, json{
                    {"ok", false},
                    {"error", "path_conflict"},
                    {"message", "temporary path is invalid"}
                }.dump());
                return;
            }
            std::filesystem::remove(tmp, ec);
        }
        ec.clear();
    }

    ec.clear();
    std::filesystem::copy_file(from_abs, tmp, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        audit_fail("copy_failed", 500, ec.message(), from_rel, to_rel);
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "copy failed"},
            {"detail", pqnas::shorten(ec.message(), 180)}
        }.dump());
        return;
    }

    if (dst_exists) {
        ec.clear();
        auto dst_st = std::filesystem::symlink_status(to_abs, ec);
        if (ec) {
            std::error_code ec2;
            std::filesystem::remove(tmp, ec2);

            audit_fail("dst_stat_failed", 500, ec.message(), from_rel, to_rel);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "destination stat failed"},
                {"detail", pqnas::shorten(ec.message(), 180)}
            }.dump());
            return;
        }

        if (!std::filesystem::exists(dst_st)) {
            dst_exists = false;
        } else {
            if (std::filesystem::is_symlink(dst_st)) {
                std::error_code ec2;
                std::filesystem::remove(tmp, ec2);

                audit_fail("dst_symlink_not_supported", 409, "", from_rel, to_rel);
                reply_json(res, 409, json{
                    {"ok", false},
                    {"error", "path_conflict"},
                    {"message", "destination exists and is not a regular file"}
                }.dump());
                return;
            }

            if (!std::filesystem::is_regular_file(dst_st)) {
                std::error_code ec2;
                std::filesystem::remove(tmp, ec2);

                audit_fail("dst_not_file", 400, "", from_rel, to_rel);
                reply_json(res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "destination exists and is not a file"}
                }.dump());
                return;
            }

            ec.clear();
            std::filesystem::remove(to_abs, ec);
            if (ec) {
                std::error_code ec2;
                std::filesystem::remove(tmp, ec2);

                audit_fail("overwrite_remove_failed", 500, ec.message(), from_rel, to_rel);
                reply_json(res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "failed to overwrite destination"},
                    {"detail", pqnas::shorten(ec.message(), 180)}
                }.dump());
                return;
            }
        }
    }

    ec.clear();
    std::filesystem::rename(tmp, to_abs, ec);
    if (ec) {
        std::error_code ec2;
        std::filesystem::remove(tmp, ec2);

        audit_fail("rename_failed", 500, ec.message(), from_rel, to_rel);
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "copy failed"},
            {"detail", pqnas::shorten(ec.message(), 180)}
        }.dump());
        return;
    }

	{
    	std::int64_t now_ts = now_epoch_sec();

	    std::error_code mec;
    	auto mtime_ft = std::filesystem::last_write_time(to_abs, mec);
	    std::int64_t mtime_epoch = now_ts;
    	if (!mec) {
        	using namespace std::chrono;
	        auto sctp = time_point_cast<system_clock::duration>(
    	        mtime_ft - std::filesystem::file_time_type::clock::now() + system_clock::now()
        	);
	        mtime_epoch = (std::int64_t)duration_cast<seconds>(sctp.time_since_epoch()).count();
    	}

	    pqnas::FileLocationRecord rec;
    	rec.fp = fp_hex;
	    rec.logical_rel_path = to_rel_norm;
    	rec.current_pool = src_rec->current_pool;
	    rec.physical_path = to_abs.string();
    	rec.tier_state = src_rec->tier_state;
	    rec.size_bytes = src_bytes;
    	rec.mtime_epoch = mtime_epoch;
	    rec.created_epoch = now_ts;
    	rec.updated_epoch = now_ts;
	    rec.version = 1;

    	std::string merr;
    	if (!idx->upsert_landing_file(rec, &merr)) {
        	std::error_code ec2;
	        std::filesystem::remove(to_abs, ec2);

    	    audit_fail("metadata_upsert_failed", 500, merr, from_rel, to_rel);
        	reply_json(res, 500, json{
            	{"ok", false},
	            {"error", "server_error"},
    	        {"message", "copy succeeded but metadata upsert failed"},
        	    {"detail", pqnas::shorten(merr, 180)}
	        }.dump());
    	    return;
    	}
	}

	audit_ok(from_rel, to_rel, src_bytes, dst_old_bytes, delta_bytes, dst_exists);

	
        record_user_file_copied_activity_best_effort_local(
            users,
            fp_hex,
            from_rel_norm,
            to_rel_norm,
            "file",
            src_bytes,
            &req
        );
reply_json(res, 200, json{
    	{"ok", true},
	    {"from", from_rel},
    	{"to", to_rel},
    	{"type", "file"},
	    {"src_bytes", src_bytes},
    	{"dst_old_bytes", dst_old_bytes},
	    {"delta_bytes", delta_bytes},
    	{"overwrote", dst_exists}
	}.dump());
});

// GET /api/v4/files/zip?path=relative/dir
// Response: application/zip (streams a zip of the directory)
srv.Get("/api/v4/files/zip", [&](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;

    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto audit_fail = [&](const std::string& reason, int http, const std::string& detail = "") {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_zip_fail";
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

    auto audit_ok = [&](const std::string& rel_path, std::uint64_t bytes) {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_zip_ok";
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

    // path param (directory)
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

    // Must be allocated (fail-closed)
    {
        auto uopt = users.get(fp_hex);
        if (!uopt.has_value()) {
            audit_fail("user_missing", 403);
            reply_json(res, 403, json{
                {"ok", false},
                {"error", "forbidden"},
                {"message", "policy denied"}
            }.dump());
            return;
        }
        const auto& u = *uopt;
        if (u.storage_state != "allocated") {
            audit_fail("storage_unallocated", 403);
            reply_json(res, 403, json{
                {"ok", false},
                {"error", "storage_unallocated"},
                {"message", "Storage not allocated"},
                {"fingerprint_hex", fp_hex},
                {"quota_bytes", u.quota_bytes}
            }.dump());
            return;
        }
    }

    // Resolve directory path strictly under user dir
    const std::filesystem::path user_dir = user_dir_for_fp(users, fp_hex);
    std::filesystem::path abs_dir;
    std::string perr;
    if (!pqnas::resolve_user_path_strict(user_dir, rel_path, &abs_dir, &perr)) {
        audit_fail("invalid_path", 400, perr);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid path"}
        }.dump());
        return;
    }

    // Refuse any symlink in the existing prefix chain
    {
        std::string serr;
        if (!ensure_no_symlink_in_existing_path_prefix(abs_dir, &serr)) {
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

    // Validate exists + directory, without following symlinks
    std::error_code ec;
    auto st = std::filesystem::symlink_status(abs_dir, ec);
    if (ec || !std::filesystem::exists(st)) {
        audit_fail("not_found", 404);
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "directory not found"}
        }.dump());
        return;
    }

    if (std::filesystem::is_symlink(st)) {
        audit_fail("symlink_not_supported", 400, abs_dir.string());
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "symlinks not supported"}
        }.dump());
        return;
    }

    if (!std::filesystem::is_directory(st)) {
        audit_fail("not_directory", 400);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "not a directory"}
        }.dump());
        return;
    }

    // Collect files (pass 1)
    std::vector<ZipFileItem> items;
    std::uint64_t total_payload = 0;

    // ZIP will contain entries relative to rel_path root.
    // Example: rel_path="photos" => inside zip, paths like "photos/a.jpg" etc.
    // This makes it intuitive when extracted.
    const std::string zip_root = zip_sanitize_relpath(rel_path);
    const std::filesystem::path base = abs_dir;

    for (auto it = std::filesystem::recursive_directory_iterator(base, ec);
         !ec && it != std::filesystem::recursive_directory_iterator();
         it.increment(ec)) {

        if (ec) break;
        const auto& de = *it;

        std::error_code ec2;
        auto ent_st = std::filesystem::symlink_status(de.path(), ec2);
        if (ec2) {
            ec2.clear();
            continue;
        }

        // Never include symlinks in the zip, and never recurse through them.
        if (std::filesystem::is_symlink(ent_st)) {
            continue;
        }

        if (!std::filesystem::is_regular_file(ent_st)) {
            continue;
        }

        const std::filesystem::path abs = de.path();
        std::uint64_t sz = pqnas::file_size_u64_safe(abs);

        // build relative name within the selected directory
        std::filesystem::path rel_inside = std::filesystem::relative(abs, base, ec);
        if (ec) {
            ec.clear();
            continue;
        }

        std::string name = zip_root.empty()
            ? rel_inside.string()
            : (zip_root + "/" + rel_inside.string());

        name = zip_sanitize_relpath(name);
        if (name.empty()) continue;

        ZipFileItem z;
        z.abs_path = abs;
        z.zip_name = name;
        z.size_u64 = sz;

        std::uint16_t dt = 0, dd = 0;
        zip_dos_time_date(de.last_write_time(ec), dt, dd);
        if (ec) ec.clear();
        z.dos_time = dt;
        z.dos_date = dd;

        items.push_back(std::move(z));
        total_payload += sz;
    }

    if (ec) {
        audit_fail("iter_failed", 500, ec.message());
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "failed to enumerate directory"}
        }.dump());
        return;
    }

    if (items.empty()) {
        // Allow empty zips (valid), but still produce something useful.
        // We'll return a small zip with no entries.
    }

    // Sort stable by name for deterministic output
    std::sort(items.begin(), items.end(), [](const ZipFileItem& a, const ZipFileItem& b) {
        return a.zip_name < b.zip_name;
    });

    // Compute total zip size (exact, ZIP32)
    ZipTotals totals = zip_compute_totals(items);
    const std::uint64_t zip_bytes = totals.total_bytes;

    if (zip_bytes > 0xFFFFFFFFull) {
        audit_fail("zip_too_large_zip32", 413);
        reply_json(res, 413, json{
            {"ok", false},
            {"error", "too_large"},
            {"message", "directory too large for zip (zip32 limit)"}
        }.dump());
        return;
    }

    // Build filename
    std::string out_name = zip_basename(abs_dir) + ".zip";

    res.set_header("Cache-Control", "no-store");
    res.set_header("Content-Type", "application/zip");
    res.set_header("Content-Length", std::to_string((unsigned long long)zip_bytes));
    res.set_header("Content-Disposition", build_content_disposition("attachment", out_name));

    auto streamer = std::make_shared<ZipStreamer>(std::move(items), totals);

    res.set_content_provider(
        (size_t)zip_bytes,
        "application/zip",
        [streamer](size_t offset, size_t length, httplib::DataSink& sink) mutable {
            return streamer->emit(offset, length, sink);
        },
        [streamer](bool /*success*/) mutable {
            // nothing to close; files are opened/closed per entry inside streamer
        }
    );

    audit_ok(rel_path, zip_bytes);
});

// ---- Office document preview ------------------------------------------------
// Converts common Office/OpenDocument files to a temporary PDF using LibreOffice.
// These helpers are lambdas because this route block lives inside main().

auto lower_ext_no_dot_local = [](const std::filesystem::path& p) -> std::string {
    std::string e = p.extension().string();
    if (!e.empty() && e[0] == '.') e.erase(e.begin());
    for (char& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return e;
};

auto executable_in_path_local = [](const std::string& exe) -> bool {
    if (exe.empty()) return false;

    if (exe.find('/') != std::string::npos) {
        return ::access(exe.c_str(), X_OK) == 0;
    }

    const char* path_env = std::getenv("PATH");
    if (!path_env || !*path_env) return false;

    std::string path(path_env);
    std::size_t pos = 0;

    while (pos <= path.size()) {
        std::size_t next = path.find(':', pos);
        std::string dir = path.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
        if (dir.empty()) dir = ".";

        const std::filesystem::path candidate = std::filesystem::path(dir) / exe;
        if (::access(candidate.c_str(), X_OK) == 0) return true;

        if (next == std::string::npos) break;
        pos = next + 1;
    }

    return false;
};

auto is_office_preview_ext_local = [](const std::string& ext) -> bool {
    static const std::set<std::string> kExts = {
        "doc", "docx",
        "xls", "xlsx",
        "ppt", "pptx",
        "odt", "ods", "odp",
        "rtf"
    };
    return kExts.find(ext) != kExts.end();
};

auto make_office_preview_tmpdir_local = [](std::string* err) -> std::filesystem::path {
    std::error_code ec;

    const auto base = std::filesystem::temp_directory_path(ec);
    if (ec) {
        if (err) *err = ec.message();
        return {};
    }

    for (int i = 0; i < 64; ++i) {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto p = base / (
            "pqnas-office-preview-" +
            std::to_string(static_cast<unsigned long long>(::getpid())) + "-" +
            std::to_string(static_cast<unsigned long long>(now)) + "-" +
            std::to_string(i)
        );

        std::filesystem::create_directory(p, ec);
        if (!ec) return p;
        ec.clear();
    }

    if (err) *err = "failed to create temporary directory";
    return {};
};

auto run_soffice_convert_pdf_local = [](const std::filesystem::path& in_file,
                                        const std::filesystem::path& out_dir,
                                        std::string* err) -> bool {
    pid_t pid = ::fork();
    if (pid < 0) {
        if (err) *err = "fork failed";
        return false;
    }

    if (pid == 0) {
        const std::string in_file_s = in_file.string();
        const std::string out_dir_s = out_dir.string();

        // LibreOffice may fail with exit code 77 when its default user profile
        // cannot be created/locked by the service user. Give each preview its own
        // temporary profile under the already-created preview temp directory.
        const std::string profile_arg =
            std::string("-env:UserInstallation=file://") + out_dir_s + "/lo-profile";

        ::setenv("HOME", out_dir_s.c_str(), 1);

        ::execlp(
            "soffice",
            "soffice",
            profile_arg.c_str(),
            "--headless",
            "--nologo",
            "--nofirststartwizard",
            "--convert-to",
            "pdf",
            "--outdir",
            out_dir_s.c_str(),
            in_file_s.c_str(),
            static_cast<char*>(nullptr)
        );

        _exit(127);
    }

    int status = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);

    while (true) {
        pid_t r = ::waitpid(pid, &status, WNOHANG);
        if (r == pid) break;

        if (r < 0) {
            if (err) *err = "waitpid failed";
            return false;
        }

        if (std::chrono::steady_clock::now() > deadline) {
            ::kill(pid, SIGKILL);
            ::waitpid(pid, &status, 0);
            if (err) *err = "LibreOffice conversion timed out";
            return false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        if (err) {
            *err = "LibreOffice conversion failed with exit code ";
            *err += WIFEXITED(status) ? std::to_string(WEXITSTATUS(status)) : "signal";
        }
        return false;
    }

    return true;
};

auto find_generated_pdf_local = [&](const std::filesystem::path& out_dir,
                                    std::string* err) -> std::filesystem::path {
    std::error_code ec;

    for (const auto& de : std::filesystem::directory_iterator(out_dir, ec)) {
        if (ec) break;
        if (!de.is_regular_file(ec)) {
            ec.clear();
            continue;
        }

        const auto ext = lower_ext_no_dot_local(de.path());
        if (ext == "pdf") return de.path();
    }

    if (err) *err = ec ? ec.message() : "LibreOffice did not produce a PDF";
    return {};
};

// GET /api/v4/files/office_preview?path=relative/document.docx
// Converts supported Office/OpenDocument files to PDF and streams the PDF inline.
srv.Get("/api/v4/files/office_preview", [&](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;

    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto audit_fail = [&](const std::string& reason, int http, const std::string& detail = "") {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_office_preview_fail";
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

    auto audit_ok = [&](const std::string& rel_path, std::uint64_t bytes) {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_office_preview_ok";
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

    {
        auto uopt = users.get(fp_hex);
        if (!uopt.has_value()) {
            audit_fail("user_missing", 403);
            reply_json(res, 403, json{{"ok", false}, {"error", "forbidden"}, {"message", "policy denied"}}.dump());
            return;
        }

        const auto& u = *uopt;
        if (u.storage_state != "allocated") {
            audit_fail("storage_unallocated", 403);
            reply_json(res, 403, json{
                {"ok", false},
                {"error", "storage_unallocated"},
                {"message", "Storage not allocated"},
                {"fingerprint_hex", fp_hex},
                {"quota_bytes", u.quota_bytes}
            }.dump());
            return;
        }
    }

    pqnas::ResolvedExistingPath rp;
    std::string perr;
    if (!pqnas::resolve_existing_user_file_path(users, fp_hex, rel_path, &rp, &perr)) {
        audit_fail("invalid_path", 400, perr);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid path"}
        }.dump());
        return;
    }

    const std::filesystem::path abs = rp.abs_path;

    {
        std::string serr;
        if (!ensure_no_symlink_in_existing_path_prefix(abs, &serr)) {
            audit_fail("symlink_not_supported", 400, serr);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "symlinks not supported"}
            }.dump());
            return;
        }
    }

    std::error_code ec;
    auto st = std::filesystem::symlink_status(abs, ec);
    if (ec || !std::filesystem::exists(st)) {
        audit_fail("not_found", 404);
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "file not found"}
        }.dump());
        return;
    }

    if (std::filesystem::is_symlink(st)) {
        audit_fail("symlink_not_supported", 400);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "symlinks not supported"}
        }.dump());
        return;
    }

    if (!std::filesystem::is_regular_file(st)) {
        audit_fail("not_regular_file", 400);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "not a regular file"}
        }.dump());
        return;
    }

    const std::string ext = lower_ext_no_dot_local(abs);
    if (!is_office_preview_ext_local(ext)) {
        audit_fail("unsupported_ext", 415, ext);
        reply_json(res, 415, json{
            {"ok", false},
            {"error", "unsupported_media_type"},
            {"message", "office preview supports doc/docx/xls/xlsx/ppt/pptx/odt/ods/odp/rtf"}
        }.dump());
        return;
    }

    const std::uint64_t input_sz = pqnas::file_size_u64_safe(abs);
    constexpr std::uint64_t kMaxOfficePreviewBytes = 50ull * 1024ull * 1024ull;
    if (input_sz > kMaxOfficePreviewBytes) {
        audit_fail("too_large", 413);
        reply_json(res, 413, json{
            {"ok", false},
            {"error", "too_large"},
            {"message", "office preview file is too large"}
        }.dump());
        return;
    }

    if (!executable_in_path_local("soffice")) {
        audit_fail("soffice_missing", 503);
        reply_json(res, 503, json{
            {"ok", false},
            {"error", "office_preview_unavailable"},
            {"message", "Office preview requires LibreOffice on this server"},
            {"detail", "soffice was not found in PATH"}
        }.dump());
        return;
    }

    std::string terr;
    const auto tmpdir = make_office_preview_tmpdir_local(&terr);
    if (tmpdir.empty()) {
        audit_fail("tmpdir_failed", 500, terr);
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "failed to create temporary preview directory"}
        }.dump());
        return;
    }

    auto cleanup_tmpdir = std::make_shared<std::filesystem::path>(tmpdir);

    std::string cerr;
    if (!run_soffice_convert_pdf_local(abs, tmpdir, &cerr)) {
        std::error_code rm_ec;
        std::filesystem::remove_all(tmpdir, rm_ec);

        audit_fail("convert_failed", 500, cerr);
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "conversion_failed"},
            {"message", "LibreOffice failed to convert this document"},
            {"detail", cerr}
        }.dump());
        return;
    }

    std::string ferr;
    const auto pdf_path = find_generated_pdf_local(tmpdir, &ferr);
    if (pdf_path.empty()) {
        std::error_code rm_ec;
        std::filesystem::remove_all(tmpdir, rm_ec);

        audit_fail("pdf_missing", 500, ferr);
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "conversion_failed"},
            {"message", "converted PDF was not found"},
            {"detail", ferr}
        }.dump());
        return;
    }

    const std::uint64_t pdf_sz = pqnas::file_size_u64_safe(pdf_path);

    auto fp = std::make_shared<std::ifstream>(pdf_path, std::ios::binary);
    if (!fp->good()) {
        std::error_code rm_ec;
        std::filesystem::remove_all(tmpdir, rm_ec);

        audit_fail("open_pdf_failed", 500);
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "failed to open converted PDF"}
        }.dump());
        return;
    }

    const std::string out_name = abs.stem().string() + ".pdf";

    res.set_header("Cache-Control", "no-store");
    res.set_header("Content-Disposition", build_content_disposition("inline", out_name));

    res.set_content_provider(
        static_cast<size_t>(pdf_sz),
        "application/pdf",
        [fp](size_t /*offset*/, size_t length, httplib::DataSink& sink) mutable {
            std::string buf;
            buf.resize(length);

            fp->read(buf.data(), static_cast<std::streamsize>(length));
            std::streamsize n = fp->gcount();
            if (n > 0) sink.write(buf.data(), static_cast<size_t>(n));

            return n > 0;
        },
        [fp, cleanup_tmpdir](bool /*success*/) mutable {
            if (fp && fp->is_open()) fp->close();

            std::error_code rm_ec;
            std::filesystem::remove_all(*cleanup_tmpdir, rm_ec);
        }
    );

    audit_ok(rel_path, pdf_sz);
});

// GET /api/v4/files/get?path=relative/path.bin
// Response: raw bytes (streams file)
srv.Get("/api/v4/files/get", [&](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;

    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto audit_fail = [&](const std::string& reason, int http, const std::string& detail = "") {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_get_fail";
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

    auto audit_ok = [&](const std::string& rel_path, std::uint64_t bytes) {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_get_ok";
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

    // Must be allocated (fail-closed)
    {
        auto uopt = users.get(fp_hex);
        if (!uopt.has_value()) {
            audit_fail("user_missing", 403);
            reply_json(res, 403, json{{"ok", false}, {"error", "forbidden"}, {"message", "policy denied"}}.dump());
            return;
        }
        const auto& u = *uopt;
        if (u.storage_state != "allocated") {
            audit_fail("storage_unallocated", 403);
            reply_json(res, 403, json{
                {"ok", false},
                {"error", "storage_unallocated"},
                {"message", "Storage not allocated"},
                {"fingerprint_hex", fp_hex},
                {"quota_bytes", u.quota_bytes}
            }.dump());
            return;
        }
    }

    // Resolve file path strictly under user dir
    pqnas::ResolvedExistingPath rp;
    std::string perr;
    if (!pqnas::resolve_existing_user_file_path(users, fp_hex, rel_path, &rp, &perr)) {
        audit_fail("invalid_path", 400, perr);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid path"}
        }.dump());
        return;
    }

    const std::filesystem::path abs = rp.abs_path;

    {
    std::string serr;
    if (!ensure_no_symlink_in_existing_path_prefix(abs, &serr)) {
        audit_fail("symlink_not_supported", 400, serr);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "symlinks not supported"}
        }.dump());
        return;
    }
}

    // Validate exists + regular + size
    std::error_code ec;
    auto st = std::filesystem::symlink_status(abs, ec);
    if (ec || !std::filesystem::exists(st)) {
        audit_fail("not_found", 404);
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "file not found"}
        }.dump());
        return;
    }
    if (std::filesystem::is_symlink(st)) {
        audit_fail("symlink_not_supported", 400);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "symlinks not supported"}
        }.dump());
        return;
    }
    if (!std::filesystem::is_regular_file(st)) {
        audit_fail("not_regular_file", 400);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "not a regular file"}
        }.dump());
        return;
    }

    const std::uint64_t sz = pqnas::file_size_u64_safe(abs);

    // Stream it
    auto fp = std::make_shared<std::ifstream>(abs, std::ios::binary);
    if (!fp->good()) {
        audit_fail("open_failed", 500);
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "failed to open file"}
        }.dump());
        return;
    }

    const std::string mime = guess_download_mime_from_name(abs.filename().string());
    const bool inline_preview = is_inline_preview_mime(mime);

    res.set_header("Cache-Control", "no-store");

    res.set_header(
        "Content-Disposition",
        build_content_disposition(
            inline_preview ? "inline" : "attachment",
            abs.filename().string()
        )
    );

    res.set_header("Accept-Ranges", "bytes");

    // httplib calls this provider with the exact byte offset requested by
    // normal downloads, single Range requests, and multi-range media probes.
    // We must seek to that offset every time; otherwise Range headers are
    // correct but the body comes from the wrong place.
    res.set_content_provider(
        static_cast<size_t>(sz),
        mime.c_str(),
        [fp](size_t offset, size_t length, httplib::DataSink& sink) mutable {
            if (!fp || !fp->is_open()) return false;

            fp->clear();
            fp->seekg(static_cast<std::streamoff>(offset), std::ios::beg);
            if (!fp->good()) return false;

            constexpr size_t kChunk = 64 * 1024;
            std::vector<char> buf;
            buf.resize(std::min(kChunk, length));

            size_t remaining = length;

            while (remaining > 0) {
                const size_t want = std::min(buf.size(), remaining);

                fp->read(buf.data(), static_cast<std::streamsize>(want));
                const std::streamsize got = fp->gcount();

                if (got <= 0) break;

                sink.write(buf.data(), static_cast<size_t>(got));
                remaining -= static_cast<size_t>(got);

                if (static_cast<size_t>(got) < want) break;
            }

            return remaining == 0;
        },
        [fp](bool /*success*/) mutable {
            if (fp && fp->is_open()) fp->close();
        }
    );


    audit_ok(rel_path, sz);
});



