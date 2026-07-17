/*
 * File Manager search
 * -------------------
 * This route searches only the authenticated fingerprint's logical namespace.
 *
 * Security/resource rules:
 * - the browser never supplies a fingerprint or an absolute filesystem root
 * - metadata rows are selected by authenticated fingerprint
 * - legacy physical fallback stays below user_dir and never follows symlinks
 * - reserved .pqnas paths are excluded
 * - no absolute physical path is returned
 * - query, result count, scan count, depth and wall-clock time are bounded
 */
srv.Post("/api/v4/files/search", [&](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;

    constexpr std::uint64_t SCAN_HARD_CAP = 50000;
    constexpr std::size_t METADATA_FETCH_CAP = static_cast<std::size_t>(SCAN_HARD_CAP + 1);
    constexpr int RESULT_HARD_CAP = 200;
    constexpr std::size_t QUERY_MIN_BYTES = 2;
    constexpr std::size_t QUERY_MAX_BYTES = 128;
    constexpr std::size_t MAX_RELATIVE_DEPTH = 32;
    constexpr auto SEARCH_TIME_LIMIT = std::chrono::milliseconds(1500);

    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto add_ip_headers = [&](pqnas::AuditEvent& ev) {
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) {
            ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);
        }

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) {
            ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);
        }

        ev.f["ua"] = audit_ua();
    };

    auto audit_fail = [&](const std::string& reason,
                          int http,
                          const std::string& detail = "",
                          const std::string& path_rel = "",
                          std::size_t query_bytes = 0,
                          int max_results = 0) {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_search_fail";
        ev.outcome = "fail";
        ev.f["fingerprint"] = fp_hex;
        ev.f["reason"] = reason;
        ev.f["http"] = std::to_string(http);

        if (!path_rel.empty()) {
            ev.f["path"] = pqnas::shorten(path_rel, 200);
        }
        if (query_bytes > 0) {
            // Privacy: record query length, not the user's search text.
            ev.f["q_bytes"] = std::to_string(query_bytes);
        }
        if (max_results > 0) {
            ev.f["max"] = std::to_string(max_results);
        }
        if (!detail.empty()) {
            ev.f["detail"] = pqnas::shorten(detail, 180);
        }

        add_ip_headers(ev);
        audit_append(ev);
    };

    auto audit_ok = [&](const std::string& path_rel,
                        std::size_t query_bytes,
                        int max_results,
                        std::uint64_t scanned,
                        std::uint64_t matched,
                        std::uint64_t returned,
                        bool truncated,
                        bool scan_capped,
                        bool depth_capped,
                        bool timed_out) {
        pqnas::AuditEvent ev;
        ev.event = "v4.files_search_ok";
        ev.outcome = "ok";
        ev.f["fingerprint"] = fp_hex;
        ev.f["path"] = pqnas::shorten(path_rel.empty() ? "." : path_rel, 200);
        ev.f["q_bytes"] = std::to_string(query_bytes);
        ev.f["max"] = std::to_string(max_results);
        ev.f["scanned"] = std::to_string(static_cast<unsigned long long>(scanned));
        ev.f["matched"] = std::to_string(static_cast<unsigned long long>(matched));
        ev.f["returned"] = std::to_string(static_cast<unsigned long long>(returned));
        ev.f["truncated"] = truncated ? "1" : "0";
        ev.f["scan_capped"] = scan_capped ? "1" : "0";
        ev.f["depth_capped"] = depth_capped ? "1" : "0";
        ev.f["timed_out"] = timed_out ? "1" : "0";

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

    auto trim_ascii = [](std::string value) {
        auto is_space = [](unsigned char c) {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n';
        };

        while (!value.empty() && is_space(static_cast<unsigned char>(value.front()))) {
            value.erase(value.begin());
        }
        while (!value.empty() && is_space(static_cast<unsigned char>(value.back()))) {
            value.pop_back();
        }
        return value;
    };

    std::string path_input;
    std::string query;

    if (req.has_param("path")) path_input = req.get_param_value("path");
    if (req.has_param("q")) query = req.get_param_value("q");

    query = trim_ascii(query);

    if (query.size() < QUERY_MIN_BYTES || query.size() > QUERY_MAX_BYTES) {
        audit_fail(
            "invalid_query_length",
            400,
            "",
            path_input,
            query.size()
        );
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "q length must be between 2 and 128 bytes"}
        }.dump());
        return;
    }

    const bool query_has_control = std::any_of(
        query.begin(),
        query.end(),
        [](unsigned char c) {
            return c < 0x20 || c == 0x7f;
        }
    );

    if (query_has_control) {
        audit_fail(
            "query_contains_control_characters",
            400,
            "",
            path_input,
            query.size()
        );
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "q contains control characters"}
        }.dump());
        return;
    }

    int max_results = RESULT_HARD_CAP;
    if (req.has_param("max")) {
        try {
            max_results = std::stoi(req.get_param_value("max"));
        } catch (...) {
            max_results = RESULT_HARD_CAP;
        }
    }
    max_results = std::max(1, std::min(RESULT_HARD_CAP, max_results));

    if (path_input == "." || path_input == "./") {
        path_input.clear();
    }

    std::string base_rel;
    if (!path_input.empty()) {
        std::string normalize_err;
        if (!pqnas::normalize_user_rel_path_strict(
                path_input,
                &base_rel,
                &normalize_err)) {
            audit_fail(
                "invalid_path",
                400,
                normalize_err,
                path_input,
                query.size(),
                max_results
            );
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "invalid path"}
            }.dump());
            return;
        }
    }

    auto path_has_reserved_segment = [](const std::string& rel) {
        if (rel.empty()) return false;

        std::size_t start = 0;
        while (start <= rel.size()) {
            const std::size_t slash = rel.find('/', start);
            const std::size_t end =
                slash == std::string::npos ? rel.size() : slash;

            if (rel.substr(start, end - start) == ".pqnas") {
                return true;
            }

            if (slash == std::string::npos) break;
            start = slash + 1;
        }

        return false;
    };

    if (path_has_reserved_segment(base_rel)) {
        audit_fail(
            "reserved_path",
            400,
            "",
            base_rel,
            query.size(),
            max_results
        );
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "reserved path"}
        }.dump());
        return;
    }

    const std::filesystem::path user_dir = user_dir_for_fp(users, fp_hex);
    std::filesystem::path base_abs = user_dir;

    if (!base_rel.empty()) {
        std::string resolve_err;
        if (!pqnas::resolve_user_path_strict(
                user_dir,
                base_rel,
                &base_abs,
                &resolve_err)) {
            audit_fail(
                "invalid_path",
                400,
                resolve_err,
                base_rel,
                query.size(),
                max_results
            );
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "invalid path"}
            }.dump());
            return;
        }
    }

    {
        std::string symlink_err;
        if (!ensure_no_symlink_in_existing_path_prefix(base_abs, &symlink_err)) {
            audit_fail(
                "symlink_not_supported",
                400,
                symlink_err,
                base_rel,
                query.size(),
                max_results
            );
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "symlinks not supported for search base"}
            }.dump());
            return;
        }
    }

    auto* index = pqnas::get_file_location_index();
    if (!index) {
        audit_fail(
            "metadata_index_missing",
            500,
            "",
            base_rel,
            query.size(),
            max_results
        );
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "metadata index missing"}
        }.dump());
        return;
    }

    std::string metadata_err;
    auto metadata_rows = index->list_all_records_for_fp(
        fp_hex,
        METADATA_FETCH_CAP,
        &metadata_err
    );

    if (!metadata_err.empty()) {
        audit_fail(
            "metadata_search_list_failed",
            500,
            metadata_err,
            base_rel,
            query.size(),
            max_results
        );
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "failed to read logical file index"},
            {"detail", pqnas::shorten(metadata_err, 180)}
        }.dump());
        return;
    }

    bool physical_base_is_dir = false;
    bool physical_base_exists = false;

    {
        std::error_code stat_ec;
        const auto base_status = std::filesystem::symlink_status(base_abs, stat_ec);

        if (!stat_ec && std::filesystem::exists(base_status)) {
            physical_base_exists = true;

            if (std::filesystem::is_symlink(base_status)) {
                audit_fail(
                    "symlink_not_supported",
                    400,
                    "",
                    base_rel,
                    query.size(),
                    max_results
                );
                reply_json(res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "symlinks not supported for search base"}
                }.dump());
                return;
            }

            physical_base_is_dir = std::filesystem::is_directory(base_status);
        }
    }

    auto is_under_base = [&](const std::string& rel) {
        if (base_rel.empty()) return true;
        return rel.rfind(base_rel + "/", 0) == 0;
    };

    bool logical_base_exists = base_rel.empty();
    if (!logical_base_exists) {
        for (const auto& row : metadata_rows) {
            if (row.logical_rel_path.rfind(base_rel + "/", 0) == 0) {
                logical_base_exists = true;
                break;
            }
        }
    }

    if (physical_base_exists && !physical_base_is_dir) {
        audit_fail(
            "not_a_directory",
            400,
            "",
            base_rel,
            query.size(),
            max_results
        );
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "path must be a directory"}
        }.dump());
        return;
    }

    if (!physical_base_is_dir && !logical_base_exists) {
        audit_fail(
            "not_found",
            404,
            "",
            base_rel,
            query.size(),
            max_results
        );
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "path not found"}
        }.dump());
        return;
    }

    auto lower_ascii = [](std::string value) {
        for (char& c : value) {
            c = static_cast<char>(
                std::tolower(static_cast<unsigned char>(c))
            );
        }
        return value;
    };

    const std::string query_lower = lower_ascii(query);

    auto path_depth_from_base = [&](const std::string& rel) -> std::size_t {
        std::string tail = rel;

        if (!base_rel.empty()) {
            if (!is_under_base(rel)) {
                return MAX_RELATIVE_DEPTH + 1;
            }
            tail = rel.substr(base_rel.size() + 1);
        }

        if (tail.empty()) return 0;
        return 1 + static_cast<std::size_t>(
            std::count(tail.begin(), tail.end(), '/')
        );
    };

    auto name_from_rel = [](const std::string& rel) {
        const std::size_t slash = rel.rfind('/');
        return slash == std::string::npos ? rel : rel.substr(slash + 1);
    };

    auto parent_from_rel = [](const std::string& rel) {
        const std::size_t slash = rel.rfind('/');
        return slash == std::string::npos ? std::string() : rel.substr(0, slash);
    };

    auto matches_query = [&](const std::string& rel) {
        const std::string name = name_from_rel(rel);
        const std::string haystack = lower_ascii(name + "\n" + rel);
        return haystack.find(query_lower) != std::string::npos;
    };

    struct SearchHit {
        std::string path;
        std::string name;
        std::string parent_path;
        std::string type;
        std::uint64_t size_bytes = 0;
        std::int64_t mtime_unix = 0;
    };

    std::map<std::string, SearchHit> hits;

    std::uint64_t scanned = 0;
    std::uint64_t matched = 0;
    bool truncated = false;
    bool scan_capped = false;
    bool depth_capped = false;
    bool timed_out = false;

    const auto deadline = std::chrono::steady_clock::now() + SEARCH_TIME_LIMIT;

    auto time_available = [&]() {
        if (std::chrono::steady_clock::now() < deadline) return true;
        timed_out = true;
        truncated = true;
        return false;
    };

    auto consume_scan_slot = [&]() {
        if (scanned >= SCAN_HARD_CAP) {
            scan_capped = true;
            truncated = true;
            return false;
        }

        if (!time_available()) return false;

        ++scanned;
        return true;
    };

    auto add_hit = [&](SearchHit hit) {
        if (hit.path.empty() || path_has_reserved_segment(hit.path)) return true;

        if (hits.find(hit.path) != hits.end()) {
            return true;
        }

        if (static_cast<int>(hits.size()) >= max_results) {
            truncated = true;
            return false;
        }

        ++matched;
        hits.emplace(hit.path, std::move(hit));
        return true;
    };

    auto add_matching_directory = [&](const std::string& dir_rel) {
        if (dir_rel.empty() || !is_under_base(dir_rel)) return true;

        const std::size_t depth = path_depth_from_base(dir_rel);
        if (depth > MAX_RELATIVE_DEPTH) {
            depth_capped = true;
            truncated = true;
            return true;
        }

        if (!matches_query(dir_rel)) return true;

        SearchHit hit;
        hit.path = dir_rel;
        hit.name = name_from_rel(dir_rel);
        hit.parent_path = parent_from_rel(dir_rel);
        hit.type = "dir";
        return add_hit(std::move(hit));
    };

    if (metadata_rows.size() >= METADATA_FETCH_CAP) {
        metadata_rows.resize(static_cast<std::size_t>(SCAN_HARD_CAP));
        scan_capped = true;
        truncated = true;
    }

    for (const auto& row : metadata_rows) {
        if (!consume_scan_slot()) break;

        std::string rel;
        std::string normalize_err;
        if (!pqnas::normalize_user_rel_path_strict(
                row.logical_rel_path,
                &rel,
                &normalize_err)) {
            continue;
        }

        if (rel != row.logical_rel_path ||
            rel.empty() ||
            path_has_reserved_segment(rel) ||
            !is_under_base(rel)) {
            continue;
        }

        const std::size_t depth = path_depth_from_base(rel);
        if (depth > MAX_RELATIVE_DEPTH) {
            depth_capped = true;
            truncated = true;
            continue;
        }

        // Metadata is authoritative for tiered logical paths, but stale or
        // symlinked physical targets must not become visible through search.
        std::error_code physical_ec;
        const auto physical_status = std::filesystem::symlink_status(
            std::filesystem::path(row.physical_path),
            physical_ec
        );

        if (physical_ec ||
            !std::filesystem::exists(physical_status) ||
            std::filesystem::is_symlink(physical_status) ||
            !std::filesystem::is_regular_file(physical_status)) {
            continue;
        }

        std::size_t slash = rel.find('/');
        while (slash != std::string::npos) {
            const std::string dir_rel = rel.substr(0, slash);
            if (!add_matching_directory(dir_rel)) break;
            slash = rel.find('/', slash + 1);
        }

        if (static_cast<int>(hits.size()) >= max_results) break;

        if (matches_query(rel)) {
            SearchHit hit;
            hit.path = rel;
            hit.name = name_from_rel(rel);
            hit.parent_path = parent_from_rel(rel);
            hit.type = "file";
            hit.size_bytes = row.size_bytes;
            hit.mtime_unix = row.mtime_epoch;

            if (!add_hit(std::move(hit))) break;
        }
    }

    // Legacy physical fallback keeps pre-index files searchable. It is rooted
    // below user_dir, does not follow symlinks, and is deduplicated against the
    // logical metadata results above.
    if (physical_base_is_dir &&
        static_cast<int>(hits.size()) < max_results &&
        !scan_capped &&
        !timed_out) {

        std::error_code walk_ec;
        const auto options =
            std::filesystem::directory_options::skip_permission_denied;

        for (std::filesystem::recursive_directory_iterator it(
                 base_abs,
                 options,
                 walk_ec),
             end;
             it != end;
             it.increment(walk_ec)) {

            if (walk_ec) {
                audit_fail(
                    "walk_failed",
                    500,
                    walk_ec.message(),
                    base_rel,
                    query.size(),
                    max_results
                );
                reply_json(res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "directory walk failed"},
                    {"detail", pqnas::shorten(walk_ec.message(), 180)}
                }.dump());
                return;
            }

            if (!consume_scan_slot()) break;

            std::error_code item_ec;
            const auto item_status =
                std::filesystem::symlink_status(it->path(), item_ec);

            if (item_ec) continue;

            if (std::filesystem::is_symlink(item_status)) {
                if (it->is_directory(item_ec)) {
                    it.disable_recursion_pending();
                }
                continue;
            }

            const std::filesystem::path rel_path =
                it->path().lexically_relative(user_dir);

            if (rel_path.empty()) continue;

            const std::string raw_rel = rel_path.generic_string();
            if (raw_rel == ".." || raw_rel.rfind("../", 0) == 0) {
                continue;
            }

            std::string rel;
            std::string normalize_err;
            if (!pqnas::normalize_user_rel_path_strict(
                    raw_rel,
                    &rel,
                    &normalize_err)) {
                continue;
            }

            if (rel != raw_rel ||
                path_has_reserved_segment(rel) ||
                !is_under_base(rel)) {
                if (std::filesystem::is_directory(item_status)) {
                    it.disable_recursion_pending();
                }
                continue;
            }

            const std::size_t depth = path_depth_from_base(rel);
            if (depth > MAX_RELATIVE_DEPTH) {
                depth_capped = true;
                truncated = true;

                if (std::filesystem::is_directory(item_status)) {
                    it.disable_recursion_pending();
                }
                continue;
            }

            if (depth == MAX_RELATIVE_DEPTH &&
                std::filesystem::is_directory(item_status)) {
                it.disable_recursion_pending();
            }

            if (!matches_query(rel)) continue;

            SearchHit hit;
            hit.path = rel;
            hit.name = name_from_rel(rel);
            hit.parent_path = parent_from_rel(rel);

            if (std::filesystem::is_directory(item_status)) {
                hit.type = "dir";
            } else if (std::filesystem::is_regular_file(item_status)) {
                hit.type = "file";
                hit.size_bytes = pqnas::file_size_u64_safe(it->path());
            } else {
                continue;
            }

            if (!add_hit(std::move(hit))) break;
        }
    }

    json results = json::array();

    for (const auto& entry : hits) {
        const SearchHit& hit = entry.second;

        json item{
            {"path", hit.path},
            {"name", hit.name},
            {"parent_path", hit.parent_path},
            {"type", hit.type},
            {"mtime_unix", hit.mtime_unix}
        };

        if (hit.type == "file") {
            item["size_bytes"] = hit.size_bytes;
            item["bytes"] = hit.size_bytes;
        }

        results.push_back(std::move(item));
    }

    audit_ok(
        base_rel,
        query.size(),
        max_results,
        scanned,
        matched,
        static_cast<std::uint64_t>(results.size()),
        truncated,
        scan_capped,
        depth_capped,
        timed_out
    );

    reply_json(res, 200, json{
        {"ok", true},
        {"scope", "user_root"},
        {"path", base_rel.empty() ? "." : base_rel},
        {"q", query},
        {"max", max_results},
        {"scanned", scanned},
        {"matched", matched},
        {"returned", results.size()},
        {"truncated", truncated},
        {"scan_capped", scan_capped},
        {"depth_capped", depth_capped},
        {"timed_out", timed_out},
        {"limits", json{
            {"query_min_bytes", QUERY_MIN_BYTES},
            {"query_max_bytes", QUERY_MAX_BYTES},
            {"result_hard_cap", RESULT_HARD_CAP},
            {"scan_hard_cap", SCAN_HARD_CAP},
            {"max_relative_depth", MAX_RELATIVE_DEPTH},
            {"time_limit_ms", SEARCH_TIME_LIMIT.count()}
        }},
        {"results", results}
    }.dump());
});
