// Progressive counterpart of /gallery/search. Authentication and path syntax
// checks happen before streaming headers; later scan failures are terminal
// NDJSON error frames so a partial result can never masquerade as complete.
srv.Get("/api/v4/gallery/search_stream", [&users, collect_fast_gallery_tree](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;
    (void)role;
    const auto uopt = users.get(fp_hex);
    if (!uopt.has_value()) {
        reply_json(res, 403, json{{"ok", false}, {"error", "forbidden"}, {"message", "policy denied"}}.dump());
        return;
    }
    if (uopt->storage_state != "allocated") {
        reply_json(res, 403, json{{"ok", false}, {"error", "storage_unallocated"},
                   {"message", "Storage not allocated"}, {"fingerprint_hex", fp_hex},
                   {"quota_bytes", uopt->quota_bytes}}.dump());
        return;
    }
    std::string rel_root;
    const auto raw_path = req.has_param("path") ? req.get_param_value("path") : std::string();
    if (!raw_path.empty()) {
        std::string nerr;
        if (!pqnas::normalize_user_rel_path_strict(raw_path, &rel_root, &nerr)) {
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid path"}}.dump());
            return;
        }
    }
    const bool force = req.has_param("force") &&
        (req.get_param_value("force") == "1" || req.get_param_value("force") == "true");
    const auto user_dir = user_dir_for_fp(users, fp_hex);
    res.set_header("Cache-Control", "no-store, no-transform");
    res.set_header("X-Accel-Buffering", "no");
    res.set_header("X-Content-Type-Options", "nosniff");
    res.set_chunked_content_provider("application/x-ndjson; charset=utf-8",
        [collect_fast_gallery_tree, fp_hex, user_dir, rel_root, force](std::size_t, httplib::DataSink& sink) -> bool {
            auto emit = [&](const json& frame) -> bool {
                const std::string line = frame.dump() + "\n";
                return sink.write(line.data(), line.size());
            };
            if (!emit(json{{"type", "progress"}, {"stats", {{"dirs", 0}, {"files", 0}, {"file_bytes", 0}}}})) return false;
            std::uint64_t dirs = 0, files = 0, bytes = 0;
            bool truncated = false;
            std::string code, message;
            try {
                const bool ok = collect_fast_gallery_tree(fp_hex, user_dir, rel_root, true, force, 0,
                    nullptr, &dirs, &files, &bytes, &truncated, &code, &message, emit);
                if (!ok) {
                    if (code == "cancelled") return false;
                    if (!emit(json{{"type", "error"}, {"error", code.empty() ? "server_error" : code},
                                   {"message", message.empty() ? "gallery search failed" : message}})) return false;
                } else if (!emit(json{{"type", "done"}, {"stats", {{"dirs", dirs}, {"files", files}, {"file_bytes", bytes}}},
                                      {"truncated", false}})) return false;
            } catch (const std::exception&) {
                if (!emit(json{{"type", "error"}, {"error", "server_error"}, {"message", "gallery search failed"}})) return false;
            }
            sink.done();
            return true;
        });
});
