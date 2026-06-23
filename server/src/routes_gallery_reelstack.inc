
// GET /api/v4/gallery/list?path=relative/dir
// Lists immediate children for Photo Gallery:
// - directories are always included
// - files are included only if they look like supported images
// - joins gallery metadata for exact child files and returns both old and standardized fields
srv.Get("/api/v4/gallery/list", [&](const httplib::Request& req, httplib::Response& res) {
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
                          const std::string& rel_dir = "") {
        pqnas::AuditEvent ev;
        ev.event = "v4.gallery_list_fail";
        ev.outcome = "fail";
        ev.f["fingerprint"] = fp_hex;
        ev.f["reason"] = reason;
        ev.f["http"] = std::to_string(http);
        if (!rel_dir.empty()) ev.f["path"] = pqnas::shorten(rel_dir, 200);
        if (!detail.empty()) ev.f["detail"] = pqnas::shorten(detail, 180);
        add_ip_headers(ev);
        audit_append(ev);
    };

    auto audit_ok = [&](const std::string& rel_dir, std::size_t count) {
        pqnas::AuditEvent ev;
        ev.event = "v4.gallery_list_ok";
        ev.outcome = "ok";
        ev.f["fingerprint"] = fp_hex;
        ev.f["path"] = pqnas::shorten(rel_dir, 200);
        ev.f["count"] = std::to_string((unsigned long long)count);
        add_ip_headers(ev);
        audit_append(ev);
    };

    auto lower_ascii_local = [](std::string s) -> std::string {
        for (char& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    };

    auto is_reserved_name = [&](const std::string& name) -> bool {
        return name == ".pqnas";
    };

    auto file_ext_lower = [&](const std::string& name) -> std::string {
        std::string n = name;
        const auto slash = n.find_last_of("/\\");
        if (slash != std::string::npos) n = n.substr(slash + 1);

        auto lower = lower_ascii_local(n);
        const auto dot = lower.rfind('.');
        if (dot == std::string::npos || dot == 0 || dot + 1 >= lower.size()) return "";
        return lower.substr(dot + 1);
    };

    auto is_gallery_image_name = [&](const std::string& name) -> bool {
        const std::string ext = file_ext_lower(name);
        return ext == "png"  ||
               ext == "jpg"  ||
               ext == "jpeg" ||
               ext == "gif"  ||
               ext == "webp" ||
               ext == "svg"  ||
               ext == "bmp"  ||
               ext == "ico"  ||
               ext == "tif"  ||
               ext == "tiff" ||
               ext == "heic" ||
               ext == "heif" ||
               ext == "cr2"  ||
               ext == "cr3"  ||
               ext == "nef"  ||
               ext == "arw"  ||
               ext == "raf"  ||
               ext == "dng"  ||
               ext == "rw2"  ||
               ext == "orf";
    };
    auto join_rel = [&](const std::string& base, const std::string& name) -> std::string {
        if (base.empty()) return name;
        return base + "/" + name;
    };
    auto split_gallery_keywords_text_json = [](const std::string& s) -> json {
    auto trim_ascii_ws_copy_local = [](std::string v) -> std::string {
        auto is_ws = [](unsigned char c) -> bool {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n';
        };

        std::size_t a = 0;
        while (a < v.size() && is_ws((unsigned char)v[a])) ++a;

        std::size_t b = v.size();
        while (b > a && is_ws((unsigned char)v[b - 1])) --b;

        return v.substr(a, b - a);
    };

    json out = json::array();

    std::size_t start = 0;
    while (start <= s.size()) {
        const std::size_t comma = s.find(',', start);

        std::string part = (comma == std::string::npos)
            ? s.substr(start)
            : s.substr(start, comma - start);

        part = trim_ascii_ws_copy_local(part);
        if (!part.empty()) out.push_back(part);

        if (comma == std::string::npos) break;
        start = comma + 1;
    }

    return out;
};
    // Storage allocated check (fail-closed)
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

    std::string rel_dir;
    if (req.has_param("path")) rel_dir = req.get_param_value("path");

    const std::filesystem::path user_dir = user_dir_for_fp(users, fp_hex);
    std::filesystem::path abs_dir = user_dir;
    std::string rel_dir_norm;

    if (!rel_dir.empty()) {
        std::string nerr;
        if (!pqnas::normalize_user_rel_path_strict(rel_dir, &rel_dir_norm, &nerr)) {
            audit_fail("invalid_path", 400, nerr, rel_dir);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "invalid path"}
            }.dump());
            return;
        }

        std::string perr;
        if (!pqnas::resolve_user_path_strict(user_dir, rel_dir_norm, &abs_dir, &perr)) {
            // Do not fail yet: dir may exist only logically via metadata.
            abs_dir.clear();
        }
    }

    struct ListedItem {
        std::string name;
        std::string type; // "dir" | "file"
        std::uint64_t size_bytes = 0;
        long long mtime_unix = 0;

        int rating = 0;
        std::string tags_text;
        std::string notes_text;

        std::string capture_time_text;
        std::int64_t capture_time_unix = 0;

        double gps_latitude = 0.0;
        double gps_longitude = 0.0;
        double gps_altitude = 0.0;
        bool has_gps = false;
        bool has_gps_altitude = false;

        std::string exif_make;
        std::string exif_model;
        std::string exif_lens_model;
        std::int64_t exif_iso = 0;
        double exif_f_number = 0.0;
        double exif_exposure_time = 0.0;
        double exif_focal_length = 0.0;
        bool exif_ok = false;
    };

    std::map<std::string, ListedItem> merged;

    // 1) Legacy physical immediate children, if physical dir exists.
    bool legacy_dir_ok = false;
    {
        std::error_code ec;
        std::filesystem::path dir_to_check = abs_dir.empty() ? user_dir : abs_dir;
        auto st = std::filesystem::symlink_status(dir_to_check, ec);

        if (!ec && std::filesystem::exists(st)) {
            if (std::filesystem::is_symlink(st)) {
                audit_fail("symlink_not_supported", 400, "", rel_dir_norm.empty() ? rel_dir : rel_dir_norm);
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

                    if (name.empty() || name == "." || name == "..") continue;
                    if (is_reserved_name(name)) continue;

                    auto child_st = std::filesystem::symlink_status(child_path, ec2);
                    if (ec2) continue;
                    if (std::filesystem::is_symlink(child_st)) continue;

                    if (std::filesystem::is_directory(child_st)) {
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
                            name, "dir", 0, mtime_unix, 0, "", "", "", 0, 0.0, 0.0, 0.0, false, false,
                            "", "", "", 0, 0.0, 0.0, 0.0, false
                        };
                        continue;
                    }

                    if (std::filesystem::is_regular_file(child_st) && is_gallery_image_name(name)) {
                        std::uint64_t size_bytes = 0;
                        ec2.clear();
                        auto sz = std::filesystem::file_size(child_path, ec2);
                        if (!ec2) size_bytes = static_cast<std::uint64_t>(sz);

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
                            name, "file", size_bytes, mtime_unix, 0, "", "", "", 0, 0.0, 0.0, 0.0, false, false,
                            "", "", "", 0, 0.0, 0.0, 0.0, false
                        };
                    }
                }
            }
        }
    }

    // 2) Merge metadata-backed immediate children from file_locations.
    bool metadata_ok = false;
    {
        auto* idx = pqnas::get_file_location_index();
        if (!idx) {
            audit_fail("metadata_index_missing", 500, "", rel_dir_norm.empty() ? rel_dir : rel_dir_norm);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "metadata index missing"}
            }.dump());
            return;
        }

        std::string lerr;
        auto rows = idx->list_immediate_children(fp_hex, rel_dir_norm, &lerr);
        if (!lerr.empty()) {
            audit_fail("metadata_list_failed", 500, lerr, rel_dir_norm.empty() ? rel_dir : rel_dir_norm);
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

            const std::string t = rec.type.empty() ? "file" : rec.type;
            if (t != "dir" && t != "file") continue;
            if (t == "file" && !is_gallery_image_name(rec.name)) continue;

            auto it = merged.find(rec.name);
            if (it == merged.end()) {
                merged[rec.name] = ListedItem{
                    rec.name,
                    t,
                    (t == "file" ? rec.size_bytes : 0),
                    (long long)rec.mtime_epoch,
                    0,
                    "",
                    "",
                    "",
                    0,
                    0.0,
                    0.0,
                    0.0,
                    false,
                    false,
                    "",
                    "",
                    "",
                    0,
                    0.0,
                    0.0,
                    0.0,
                    false
                };
            } else {
                it->second.type = t;
                it->second.size_bytes = (t == "file" ? rec.size_bytes : 0);
                it->second.mtime_unix = (long long)rec.mtime_epoch;
            }
        }
    }

    if (!legacy_dir_ok && !metadata_ok && !rel_dir_norm.empty()) {
        audit_fail("not_found", 404, "", rel_dir_norm);
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "directory not found"}
        }.dump());
        return;
    }

    // 3) Join gallery metadata for exact child files.
    {
        auto* gidx = pqnas::get_gallery_meta_index();
        if (!gidx) {
            audit_fail("gallery_meta_index_missing", 500, "", rel_dir_norm.empty() ? rel_dir : rel_dir_norm);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "gallery metadata index missing"}
            }.dump());
            return;
        }

        for (auto& kv : merged) {
            auto& item = kv.second;
            if (item.type != "file") continue;

            const std::string child_rel = join_rel(rel_dir_norm, item.name);

            std::string gerr;
            auto grec = gidx->get("user", fp_hex, child_rel, &gerr);
            if (!gerr.empty()) {
                audit_fail("gallery_meta_lookup_failed", 500, gerr, child_rel);
                reply_json(res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "gallery metadata lookup failed"},
                    {"detail", pqnas::shorten(gerr, 180)}
                }.dump());
                return;
            }

            if (grec.has_value()) {
                item.rating = grec->rating;
                item.tags_text = grec->tags_text;
                item.notes_text = grec->notes_text;

                // Prefer fresher gallery-cached facts only if file_locations did not provide them.
                if (grec.has_value()) {
                    item.rating = grec->rating;
                    item.tags_text = grec->tags_text;
                    item.notes_text = grec->notes_text;

                    if (grec->size_bytes > 0) {
                        item.size_bytes = grec->size_bytes;
                    }
                    if (grec->mtime_epoch > 0) {
                        item.mtime_unix = (long long)grec->mtime_epoch;
                    }
                }
            }
        }
    }
    // 4) Best-effort EXIF capture-time cache for visible image files.
    sqlite3* photo_db = nullptr;
    {
        const std::filesystem::path photo_db_path =
            std::filesystem::path(pqnas::data_root_dir()) / "photogallery_stats.sqlite3";

        if (sqlite3_open(photo_db_path.string().c_str(), &photo_db) != SQLITE_OK) {
            if (photo_db) {
                sqlite3_close(photo_db);
                photo_db = nullptr;
            }
        } else {
            std::string schema_err;
            if (!ensure_photo_stats_schema(photo_db, &schema_err)) {
                sqlite3_close(photo_db);
                photo_db = nullptr;
            }
        }
    }

    if (photo_db) {
        for (auto& kv : merged) {
            auto& item = kv.second;
            if (item.type != "file") continue;

            const std::string child_rel = join_rel(rel_dir_norm, item.name);

            PhotoStatsRow row;
            bool have_row = load_cached_photo_stats_row(photo_db, child_rel, &row);

            const bool cache_match =
                have_row &&
                row.size_bytes == item.size_bytes &&
                row.mtime_epoch == static_cast<std::int64_t>(item.mtime_unix);

            if (!cache_match) {
                pqnas::ResolvedLogicalItem resolved;
                std::string rerr;

                if (pqnas::resolve_existing_user_item(users, fp_hex, child_rel, &resolved, &rerr) &&
                    resolved.exists &&
                    resolved.is_file &&
                    resolved.has_physical_anchor) {

                    std::uint64_t exif_size = item.size_bytes;
                    std::int64_t exif_mtime = static_cast<std::int64_t>(item.mtime_unix);

                    if (exif_size == 0) {
                        exif_size = file_size_u64_safe_local(resolved.abs_path);
                        item.size_bytes = exif_size;
                    }

                    if (exif_mtime == 0) {
                        exif_mtime = file_mtime_epoch_safe_local(resolved.abs_path);
                        item.mtime_unix = static_cast<long long>(exif_mtime);
                    }

                    PhotoStatsRow fresh;
                    std::string ferr;
                    if (refresh_one_photo_exif_row(photo_db,
                                                   child_rel,
                                                   resolved.abs_path,
                                                   exif_size,
                                                   exif_mtime,
                                                   &fresh,
                                                   &ferr)) {
                        row = std::move(fresh);
                        have_row = true;
                    }
                }
            }

            if (have_row) {
                item.capture_time_text = row.taken_at;
                item.capture_time_unix = row.taken_epoch;

item.exif_make = row.make;
item.exif_model = row.model;
item.exif_lens_model = row.lens_model;
item.exif_iso = row.iso;
item.exif_f_number = row.f_number;
item.exif_exposure_time = row.exposure_time;
item.exif_focal_length = row.focal_length;
item.exif_ok = row.exif_ok;

                if (row.gps_ok) {
                    item.gps_latitude = row.gps_latitude;
                    item.gps_longitude = row.gps_longitude;
                    item.gps_altitude = row.gps_altitude;
                    item.has_gps = true;
                    item.has_gps_altitude = row.gps_altitude_ok;
                }
            }
        }

        sqlite3_close(photo_db);
        photo_db = nullptr;
    }
    json out;
    out["ok"] = true;
    out["fingerprint_hex"] = fp_hex;
    out["path"] = rel_dir_norm.empty() ? rel_dir : rel_dir_norm;
    out["items"] = json::array();

    std::size_t count = 0;
    for (const auto& kv : merged) {
        const auto& item = kv.second;
        out["items"].push_back(json{
            {"name", item.name},
            {"type", item.type},
            {"size_bytes", item.size_bytes},
            {"mtime_unix", item.mtime_unix},
            {"capture_time_text", item.capture_time_text},
            {"capture_time_unix", item.capture_time_unix},
            {"has_gps", item.has_gps},
            {"gps_latitude", item.has_gps ? json(item.gps_latitude) : json(nullptr)},
            {"gps_longitude", item.has_gps ? json(item.gps_longitude) : json(nullptr)},
            {"gps_altitude", (item.has_gps && item.has_gps_altitude) ? json(item.gps_altitude) : json(nullptr)},
            {"exif", json{
                {"ok", item.exif_ok},
                {"make", item.exif_make},
                {"model", item.exif_model},
                {"lens_model", item.exif_lens_model},
                {"iso", item.exif_iso > 0 ? json(item.exif_iso) : json(nullptr)},
                {"f_number", item.exif_f_number > 0.0 ? json(item.exif_f_number) : json(nullptr)},
                {"exposure_time", item.exif_exposure_time > 0.0 ? json(item.exif_exposure_time) : json(nullptr)},
                {"focal_length", item.exif_focal_length > 0.0 ? json(item.exif_focal_length) : json(nullptr)}
            }},

            // New standardized fields
            {"imageRating", item.rating},
            {"keywords", split_gallery_keywords_text_json(item.tags_text)},
            {"description", json{
                {"x-default", item.notes_text}
            }},

            // Old compatibility fields
            {"rating", item.rating},
            {"tags_text", item.tags_text},
            {"notes_text", item.notes_text}
        });
        count++;
        if (count >= 5000) break;
    }

    const std::string out_path = rel_dir_norm.empty() ? rel_dir : rel_dir_norm;
    audit_ok(out_path, count);
    reply_json(res, 200, out.dump());
});

// GET /api/v4/music/cover?path=relative/audio/file.mp3
// Returns embedded album art if present. 404 = no embedded cover, so clients can fallback to folder.jpg/cover.jpg.
srv.Get("/api/v4/music/cover", [&](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;

    auto reply = [&](int status, const json& j) {
        res.status = status;
        res.set_header("Cache-Control", "no-store");
        res.set_content(j.dump(2), "application/json; charset=utf-8");
    };

    auto lower_ascii = [&](std::string s) -> std::string {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };

    auto is_supported_audio_ext = [&](const std::string& name) -> bool {
        const std::string n = lower_ascii(name);
        const auto dot = n.rfind('.');
        if (dot == std::string::npos) return false;
        const std::string ext = n.substr(dot + 1);

        return ext == "mp3"  || ext == "flac" || ext == "m4a" ||
               ext == "aac"  || ext == "ogg"  || ext == "oga"  ||
               ext == "opus" || ext == "wav"  || ext == "aif"  ||
               ext == "aiff" || ext == "alac" || ext == "wma";
    };

    auto image_mime_from_bytes = [&](const std::string& buf) -> std::string {
        if (buf.size() >= 3 &&
            static_cast<unsigned char>(buf[0]) == 0xff &&
            static_cast<unsigned char>(buf[1]) == 0xd8 &&
            static_cast<unsigned char>(buf[2]) == 0xff) {
            return "image/jpeg";
        }

        if (buf.size() >= 8 &&
            static_cast<unsigned char>(buf[0]) == 0x89 &&
            buf[1] == 'P' && buf[2] == 'N' && buf[3] == 'G' &&
            static_cast<unsigned char>(buf[4]) == 0x0d &&
            static_cast<unsigned char>(buf[5]) == 0x0a &&
            static_cast<unsigned char>(buf[6]) == 0x1a &&
            static_cast<unsigned char>(buf[7]) == 0x0a) {
            return "image/png";
        }

        if (buf.size() >= 12 &&
            buf.compare(0, 4, "RIFF") == 0 &&
            buf.compare(8, 4, "WEBP") == 0) {
            return "image/webp";
        }

        return "";
    };

    auto file_mtime_epoch = [&](const std::filesystem::path& p) -> std::int64_t {
        std::error_code ec;
        auto ft = std::filesystem::last_write_time(p, ec);
        if (ec) return 0;

        using namespace std::chrono;
        auto sctp = time_point_cast<system_clock::duration>(
            ft - std::filesystem::file_time_type::clock::now() + system_clock::now()
        );
        return static_cast<std::int64_t>(duration_cast<seconds>(sctp.time_since_epoch()).count());
    };

    auto fnv1a64 = [&](const std::string& s) -> std::uint64_t {
        std::uint64_t h = 1469598103934665603ULL;
        for (unsigned char c : s) {
            h ^= static_cast<std::uint64_t>(c);
            h *= 1099511628211ULL;
        }
        return h;
    };

    auto hex_u64 = [&](std::uint64_t v) -> std::string {
        std::ostringstream oss;
        oss << std::hex << std::nouppercase << v;
        return oss.str();
    };

    auto read_small_file = [&](const std::filesystem::path& p, std::string* out, std::string* err) -> bool {
        if (out) out->clear();
        if (err) err->clear();

        std::ifstream f(p, std::ios::binary);
        if (!f.good()) {
            if (err) *err = "open failed";
            return false;
        }

        std::ostringstream oss;
        oss << f.rdbuf();

        if (!f.good() && !f.eof()) {
            if (err) *err = "read failed";
            return false;
        }

        if (out) *out = oss.str();
        return true;
    };

    auto spawn_stdout_to_file = [&](const std::vector<std::string>& args,
                                    const std::filesystem::path& out_path) -> int {
        if (args.empty()) return 127;

        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const auto& s : args) argv.push_back(const_cast<char*>(s.c_str()));
        argv.push_back(nullptr);

        pid_t pid = fork();
        if (pid < 0) return 127;

        if (pid == 0) {
            int out_fd = open(out_path.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0600);
            if (out_fd < 0) _exit(127);

            dup2(out_fd, STDOUT_FILENO);
            close(out_fd);

            int devnull = open("/dev/null", O_WRONLY);
            if (devnull >= 0) {
                dup2(devnull, STDERR_FILENO);
                close(devnull);
            }

            execvp(argv[0], argv.data());
            _exit(127);
        }

        int st = 0;
        while (waitpid(pid, &st, 0) < 0) {
            if (errno == EINTR) continue;
            return 127;
        }

        if (WIFEXITED(st)) return WEXITSTATUS(st);
        if (WIFSIGNALED(st)) return 128 + WTERMSIG(st);
        return 127;
    };

    auto cache_root = [&]() -> std::filesystem::path {
        return std::filesystem::path(pqnas::data_root_dir()).parent_path() / "cache" / "music_covers";
    };

    {
        auto uopt = users.get(fp_hex);
        if (!uopt.has_value() || uopt->storage_state != "allocated") {
            reply(403, {
                {"ok", false},
                {"error", "storage_unallocated"},
                {"message", "Storage not allocated"}
            });
            return;
        }
    }

    std::string rel_path;
    if (req.has_param("path")) rel_path = req.get_param_value("path");
    if (rel_path.empty()) {
        reply(400, {
            {"ok", false},
            {"error", "bad_request"},
            {"message", "missing path"}
        });
        return;
    }

    std::string rel_norm;
    {
        std::string nerr;
        if (!pqnas::normalize_user_rel_path_strict(rel_path, &rel_norm, &nerr)) {
            reply(400, {
                {"ok", false},
                {"error", "bad_request"},
                {"message", "invalid path"}
            });
            return;
        }
    }

    pqnas::ResolvedExistingPath rp;
    std::string perr;
    if (!pqnas::resolve_existing_user_file_path(users, fp_hex, rel_path, &rp, &perr)) {
        reply(400, {
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid path"}
        });
        return;
    }

    const std::filesystem::path abs = rp.abs_path;

    {
        std::string serr;
        if (!ensure_no_symlink_in_existing_path_prefix(abs, &serr)) {
            reply(400, {
                {"ok", false},
                {"error", "bad_request"},
                {"message", "symlinks not supported"}
            });
            return;
        }
    }

    std::error_code ec;
    auto st = std::filesystem::symlink_status(abs, ec);
    if (ec || !std::filesystem::exists(st)) {
        reply(404, {
            {"ok", false},
            {"error", "not_found"},
            {"message", "file not found"}
        });
        return;
    }

    if (std::filesystem::is_symlink(st)) {
        reply(400, {
            {"ok", false},
            {"error", "bad_request"},
            {"message", "symlinks not supported"}
        });
        return;
    }

    if (!std::filesystem::is_regular_file(st)) {
        reply(400, {
            {"ok", false},
            {"error", "bad_request"},
            {"message", "not a regular file"}
        });
        return;
    }

    if (!is_supported_audio_ext(abs.filename().string())) {
        reply(415, {
            {"ok", false},
            {"error", "unsupported_media_type"},
            {"message", "unsupported audio type"}
        });
        return;
    }

    const std::uint64_t src_bytes = pqnas::file_size_u64_safe(abs);
    const std::int64_t src_mtime = file_mtime_epoch(abs);

    const std::string key_material =
        fp_hex + "\n" +
        rel_norm + "\n" +
        std::to_string((unsigned long long)src_bytes) + "\n" +
        std::to_string((long long)src_mtime);

    const std::string cover_key = hex_u64(fnv1a64(key_material));

    const std::filesystem::path cache_dir = cache_root() / fp_hex;
    const std::filesystem::path cache_abs = cache_dir / (cover_key + ".bin");

    auto serve_cover = [&](bool cached) {
        std::string buf;
        std::string rerr;
        if (!read_small_file(cache_abs, &buf, &rerr)) {
            reply(500, {
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to read cached cover"}
            });
            return;
        }

        const std::string mime = image_mime_from_bytes(buf);
        if (mime.empty()) {
            std::error_code rm_ec;
            std::filesystem::remove(cache_abs, rm_ec);

            reply(404, {
                {"ok", false},
                {"error", "not_found"},
                {"message", "no embedded cover art"}
            });
            return;
        }

        res.status = 200;
        res.set_header("Cache-Control", cached ? "private, max-age=86400" : "private, max-age=3600");
        res.set_header("X-Content-Type-Options", "nosniff");
        res.set_content(std::move(buf), mime);
    };

    {
        std::error_code cec;
        auto cst = std::filesystem::symlink_status(cache_abs, cec);
        if (!cec &&
            std::filesystem::exists(cst) &&
            !std::filesystem::is_symlink(cst) &&
            std::filesystem::is_regular_file(cst)) {
            serve_cover(true);
            return;
        }
    }

    ec.clear();
    std::filesystem::create_directories(cache_dir, ec);
    if (ec) {
        reply(500, {
            {"ok", false},
            {"error", "server_error"},
            {"message", "failed to create cover cache directory"}
        });
        return;
    }

    const std::filesystem::path tmp_abs =
        cache_dir / (cover_key + ".tmp." + random_b64url(8) + ".bin");

    const std::vector<std::string> exes = {
        "/usr/bin/exiftool",
        "/usr/local/bin/exiftool",
        "exiftool"
    };

    const std::vector<std::string> tags = {
        "Picture",
        "CoverArt",
        "FrontCover",
        "AlbumArt"
    };

    bool extracted = false;

    for (const auto& exe : exes) {
        for (const auto& tag : tags) {
            std::error_code rm_ec;
            std::filesystem::remove(tmp_abs, rm_ec);

            const std::vector<std::string> args = {
                exe,
                "-b",
                "-" + tag,
                abs.string()
            };

            const int rc = spawn_stdout_to_file(args, tmp_abs);
            if (rc != 0) continue;

            std::string test;
            std::string rerr;
            if (!read_small_file(tmp_abs, &test, &rerr)) continue;

            if (test.empty()) continue;
            if (test.size() > 20ULL * 1024ULL * 1024ULL) continue;
            if (image_mime_from_bytes(test).empty()) continue;

            extracted = true;
            break;
        }

        if (extracted) break;
    }

    if (!extracted) {
        std::error_code rm_ec;
        std::filesystem::remove(tmp_abs, rm_ec);

        reply(404, {
            {"ok", false},
            {"error", "not_found"},
            {"message", "no embedded cover art"}
        });
        return;
    }

    {
        std::error_code ren_ec;
        std::filesystem::rename(tmp_abs, cache_abs, ren_ec);
        if (ren_ec) {
            std::error_code cec;
            auto cst = std::filesystem::symlink_status(cache_abs, cec);

            if (!cec &&
                std::filesystem::exists(cst) &&
                !std::filesystem::is_symlink(cst) &&
                std::filesystem::is_regular_file(cst)) {
                std::error_code rm_ec;
                std::filesystem::remove(tmp_abs, rm_ec);
            } else {
                std::error_code rm_ec;
                std::filesystem::remove(tmp_abs, rm_ec);

                reply(500, {
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "failed to finalize cover cache"}
                });
                return;
            }
        }
    }

    serve_cover(false);
});
    auto gallery_fast_prune_cache = [](auto& cache,
                                       std::int64_t now_epoch,
                                       std::int64_t ttl_seconds,
                                       std::size_t max_entries) {
        for (auto it = cache.begin(); it != cache.end(); ) {
            if (now_epoch - it->second.first > ttl_seconds) {
                it = cache.erase(it);
            } else {
                ++it;
            }
        }

        while (cache.size() > max_entries && !cache.empty()) {
            auto oldest = cache.begin();

            for (auto it = cache.begin(); it != cache.end(); ++it) {
                if (it->second.first < oldest->second.first) {
                    oldest = it;
                }
            }

            cache.erase(oldest);
        }
    };
// Fast recursive Photo Gallery helpers/routes.
// These are intentionally lighter than /api/v4/photogallery/stats:
// - no full EXIF refresh while searching
// - only reads cached EXIF/GPS rows when already present
// - returns all supported image files recursively so frontend can filter locally
struct FastGalleryItem {
    std::string rel_path;
    std::string base_path;
    std::string name;
    std::string type; // "dir" | "file"

    std::uint64_t size_bytes = 0;
    std::int64_t mtime_unix = 0;

    int rating = 0;
    std::string tags_text;
    std::string notes_text;

    std::string capture_time_text;
    std::int64_t capture_time_unix = 0;

    double gps_latitude = 0.0;
    double gps_longitude = 0.0;
    double gps_altitude = 0.0;
    bool has_gps = false;
    bool has_gps_altitude = false;

    std::string exif_make;
    std::string exif_model;
    std::string exif_lens_model;
    std::int64_t exif_iso = 0;
    double exif_f_number = 0.0;
    double exif_exposure_time = 0.0;
    double exif_focal_length = 0.0;
    bool exif_ok = false;
};

auto gallery_fast_lower_ascii = [](std::string s) -> std::string {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
};

auto gallery_fast_file_ext_lower = [&](const std::string& name) -> std::string {
    std::string n = name;
    const auto slash = n.find_last_of("/\\");
    if (slash != std::string::npos) n = n.substr(slash + 1);

    auto lower = gallery_fast_lower_ascii(n);
    const auto dot = lower.rfind('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= lower.size()) return "";
    return lower.substr(dot + 1);
};

auto gallery_fast_is_reserved_name = [](const std::string& name) -> bool {
    return name == ".pqnas";
};

auto gallery_fast_is_image_name = [&](const std::string& name) -> bool {
    const std::string ext = gallery_fast_file_ext_lower(name);
    return ext == "png"  ||
           ext == "jpg"  ||
           ext == "jpeg" ||
           ext == "gif"  ||
           ext == "webp" ||
           ext == "svg"  ||
           ext == "bmp"  ||
           ext == "ico"  ||
           ext == "tif"  ||
           ext == "tiff" ||
           ext == "heic" ||
           ext == "heif" ||
           ext == "cr2"  ||
           ext == "cr3"  ||
           ext == "nef"  ||
           ext == "arw"  ||
           ext == "raf"  ||
           ext == "dng"  ||
           ext == "rw2"  ||
           ext == "orf";
};

auto gallery_fast_join_rel = [](const std::string& base, const std::string& name) -> std::string {
    if (base.empty()) return name;
    return base + "/" + name;
};

auto gallery_fast_parent_rel = [](const std::string& rel) -> std::string {
    const auto pos = rel.find_last_of('/');
    if (pos == std::string::npos) return "";
    return rel.substr(0, pos);
};

auto gallery_fast_split_keywords_text_json = [](const std::string& s) -> json {
    auto trim_ascii_ws_copy_local = [](std::string v) -> std::string {
        auto is_ws = [](unsigned char c) -> bool {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n';
        };

        std::size_t a = 0;
        while (a < v.size() && is_ws(static_cast<unsigned char>(v[a]))) ++a;

        std::size_t b = v.size();
        while (b > a && is_ws(static_cast<unsigned char>(v[b - 1]))) --b;

        return v.substr(a, b - a);
    };

    json out = json::array();

    std::size_t start = 0;
    while (start <= s.size()) {
        const std::size_t comma = s.find(',', start);

        std::string part = (comma == std::string::npos)
            ? s.substr(start)
            : s.substr(start, comma - start);

        part = trim_ascii_ws_copy_local(part);
        if (!part.empty()) out.push_back(part);

        if (comma == std::string::npos) break;
        start = comma + 1;
    }

    return out;
};

auto collect_fast_gallery_tree =
    [&](const std::string& fp_hex,
        const std::filesystem::path& user_dir,
        const std::string& rel_root,
        bool include_items,
        bool refresh_stale_exif,
        json* items_out,
        std::uint64_t* dirs_out,
        std::uint64_t* files_out,
        std::uint64_t* bytes_out,
        bool* truncated_out,
        std::string* err_code,
        std::string* err_msg) -> bool {

    if (items_out) *items_out = json::array();
    if (dirs_out) *dirs_out = 0;
    if (files_out) *files_out = 0;
    if (bytes_out) *bytes_out = 0;
    if (truncated_out) *truncated_out = false;
    if (err_code) err_code->clear();
    if (err_msg) err_msg->clear();

    constexpr std::size_t kMaxReturnedItems = 25000;

    std::map<std::string, FastGalleryItem> merged;

    std::filesystem::path abs_root = user_dir;
    bool physical_root_ok = false;

    if (!rel_root.empty()) {
        std::string perr;
        if (!pqnas::resolve_user_path_strict(user_dir, rel_root, &abs_root, &perr)) {
            abs_root.clear();
        }
    }

    if (!abs_root.empty()) {
        std::error_code ec;
        const auto root_st = std::filesystem::symlink_status(abs_root, ec);

        if (!ec && std::filesystem::exists(root_st)) {
            if (std::filesystem::is_symlink(root_st)) {
                if (err_code) *err_code = "bad_request";
                if (err_msg) *err_msg = "symlinks not supported";
                return false;
            }

            if (std::filesystem::is_directory(root_st)) {
                physical_root_ok = true;

                for (std::filesystem::recursive_directory_iterator it(
                         abs_root,
                         std::filesystem::directory_options::skip_permission_denied,
                         ec
                     ), end;
                     it != end && !ec;
                     it.increment(ec)) {

                    std::error_code ec2;
                    const auto child_path = it->path();
                    const auto name = child_path.filename().string();

                    if (name.empty() || name == "." || name == "..") continue;

                    auto child_st = std::filesystem::symlink_status(child_path, ec2);
                    if (ec2) continue;

                    if (std::filesystem::is_symlink(child_st)) {
                        if (std::filesystem::is_directory(child_st)) {
                            it.disable_recursion_pending();
                        }
                        continue;
                    }

                    if (gallery_fast_is_reserved_name(name)) {
                        if (std::filesystem::is_directory(child_st)) {
                            it.disable_recursion_pending();
                        }
                        continue;
                    }

                    std::string rel;
                    ec2.clear();
                    rel = std::filesystem::relative(child_path, user_dir, ec2).generic_string();
                    if (ec2 || rel.empty()) continue;

                    if (std::filesystem::is_directory(child_st)) {
                        FastGalleryItem item;
                        item.rel_path = rel;
                        item.base_path = gallery_fast_parent_rel(rel);
                        item.name = name;
                        item.type = "dir";
                        item.size_bytes = 0;
                        item.mtime_unix = file_mtime_epoch_safe_local(child_path);

                        merged[rel] = item;
                        continue;
                    }

                    if (std::filesystem::is_regular_file(child_st) && gallery_fast_is_image_name(name)) {
                        FastGalleryItem item;
                        item.rel_path = rel;
                        item.base_path = gallery_fast_parent_rel(rel);
                        item.name = name;
                        item.type = "file";
                        item.size_bytes = file_size_u64_safe_local(child_path);
                        item.mtime_unix = file_mtime_epoch_safe_local(child_path);

                        merged[rel] = item;
                    }
                }
            }
        }
    }

    bool metadata_root_ok = false;

    {
        auto* idx = pqnas::get_file_location_index();
        if (!idx) {
            if (err_code) *err_code = "server_error";
            if (err_msg) *err_msg = "metadata index missing";
            return false;
        }

        std::vector<std::string> pending_dirs;
        std::set<std::string> seen_dirs;

        pending_dirs.push_back(rel_root);

        for (std::size_t i = 0; i < pending_dirs.size(); ++i) {
            const std::string cur = pending_dirs[i];
            if (seen_dirs.count(cur)) continue;
            seen_dirs.insert(cur);

            std::string lerr;
            auto rows = idx->list_immediate_children(fp_hex, cur, &lerr);
            if (!lerr.empty()) {
                if (err_code) *err_code = "server_error";
                if (err_msg) *err_msg = "metadata list failed: " + pqnas::shorten(lerr, 180);
                return false;
            }

            if (!rows.empty() && cur == rel_root) {
                metadata_root_ok = true;
            }

            for (const auto& rec : rows) {
                if (rec.name.empty()) continue;
                if (gallery_fast_is_reserved_name(rec.name)) continue;

                const std::string t = rec.type.empty() ? "file" : rec.type;
                if (t != "dir" && t != "file") continue;
                if (t == "file" && !gallery_fast_is_image_name(rec.name)) continue;

                const std::string child_rel = gallery_fast_join_rel(cur, rec.name);

                auto it = merged.find(child_rel);
                if (it == merged.end()) {
                    FastGalleryItem item;
                    item.rel_path = child_rel;
                    item.base_path = cur;
                    item.name = rec.name;
                    item.type = t;
                    item.size_bytes = (t == "file" ? rec.size_bytes : 0);
                    item.mtime_unix = static_cast<std::int64_t>(rec.mtime_epoch);

                    merged[child_rel] = item;
                } else {
                    it->second.type = t;
                    it->second.size_bytes = (t == "file" ? rec.size_bytes : 0);
                    it->second.mtime_unix = static_cast<std::int64_t>(rec.mtime_epoch);
                }

                if (t == "dir") {
                    pending_dirs.push_back(child_rel);
                }
            }
        }
    }

    if (!physical_root_ok && !metadata_root_ok && !rel_root.empty()) {
        if (err_code) *err_code = "not_found";
        if (err_msg) *err_msg = "directory not found";
        return false;
    }

    auto* gidx = pqnas::get_gallery_meta_index();
    if (include_items && !gidx) {
        if (err_code) *err_code = "server_error";
        if (err_msg) *err_msg = "gallery metadata index missing";
        return false;
    }

    if (include_items && gidx) {
        for (auto& kv : merged) {
            auto& item = kv.second;
            if (item.type != "file") continue;

            std::string gerr;
            auto grec = gidx->get("user", fp_hex, item.rel_path, &gerr);
            if (!gerr.empty()) {
                if (err_code) *err_code = "server_error";
                if (err_msg) *err_msg = "gallery metadata lookup failed: " + pqnas::shorten(gerr, 180);
                return false;
            }

            if (grec.has_value()) {
                item.rating = grec->rating;
                item.tags_text = grec->tags_text;
                item.notes_text = grec->notes_text;

                if (grec->size_bytes > 0) {
                    item.size_bytes = grec->size_bytes;
                }
                if (grec->mtime_epoch > 0) {
                    item.mtime_unix = static_cast<std::int64_t>(grec->mtime_epoch);
                }
            }
        }
    }

    sqlite3* photo_db = nullptr;
    if (include_items) {
        const std::filesystem::path photo_db_path =
            std::filesystem::path(pqnas::data_root_dir()) / "photogallery_stats.sqlite3";

        if (sqlite3_open(photo_db_path.string().c_str(), &photo_db) != SQLITE_OK) {
            if (photo_db) {
                sqlite3_close(photo_db);
                photo_db = nullptr;
            }
        } else {
            std::string schema_err;
            if (!ensure_photo_stats_schema(photo_db, &schema_err)) {
                sqlite3_close(photo_db);
                photo_db = nullptr;
            }
        }
    }

    if (photo_db) {
        for (auto& kv : merged) {
            auto& item = kv.second;
            if (item.type != "file") continue;

            PhotoStatsRow row;
            const bool have_row = load_cached_photo_stats_row(photo_db, item.rel_path, &row);

            const bool cache_match =
                have_row &&
                row.size_bytes == item.size_bytes &&
                row.mtime_epoch == item.mtime_unix;

            if (!cache_match) {
                if (!refresh_stale_exif) continue;

                pqnas::ResolvedLogicalItem resolved;
                std::string rerr;

                if (!(pqnas::resolve_existing_user_item(users, fp_hex, item.rel_path, &resolved, &rerr) &&
                      resolved.exists &&
                      resolved.is_file &&
                      resolved.has_physical_anchor)) {
                    continue;
                }

                PhotoStatsRow fresh;
                std::string ferr;

                if (!refresh_one_photo_exif_row(photo_db,
                                                item.rel_path,
                                                resolved.abs_path,
                                                item.size_bytes,
                                                item.mtime_unix,
                                                &fresh,
                                                &ferr)) {
                    continue;
                }

                row = std::move(fresh);
            }

            item.capture_time_text = row.taken_at;
            item.capture_time_unix = row.taken_epoch;

item.exif_make = row.make;
item.exif_model = row.model;
item.exif_lens_model = row.lens_model;
item.exif_iso = row.iso;
item.exif_f_number = row.f_number;
item.exif_exposure_time = row.exposure_time;
item.exif_focal_length = row.focal_length;
item.exif_ok = row.exif_ok;

            if (row.gps_ok) {
                item.gps_latitude = row.gps_latitude;
                item.gps_longitude = row.gps_longitude;
                item.gps_altitude = row.gps_altitude;
                item.has_gps = true;
                item.has_gps_altitude = row.gps_altitude_ok;
            }
        }

        sqlite3_close(photo_db);
        photo_db = nullptr;
    }

    std::uint64_t dir_count = 0;
    std::uint64_t file_count = 0;
    std::uint64_t file_bytes = 0;

    std::size_t returned = 0;

    for (const auto& kv : merged) {
        const auto& item = kv.second;

        if (item.type == "dir") {
            ++dir_count;
            continue;
        }

        if (item.type != "file") continue;

        ++file_count;
        file_bytes += item.size_bytes;

        if (!include_items || !items_out) continue;

        if (returned >= kMaxReturnedItems) {
            if (truncated_out) *truncated_out = true;
            continue;
        }

        json one{
            {"type", "file"},
            {"name", item.name},
            {"rel_path", item.rel_path},
            {"path", item.rel_path},
            {"base_path", item.base_path},
            {"size_bytes", item.size_bytes},
            {"mtime_unix", item.mtime_unix},
            {"rating", item.rating},
            {"tags_text", item.tags_text},
            {"keywords", gallery_fast_split_keywords_text_json(item.tags_text)},
            {"notes_text", item.notes_text},
            {"description", item.notes_text}
        };

        if (!item.capture_time_text.empty()) {
            one["capture_time_text"] = item.capture_time_text;
        }
        if (item.capture_time_unix > 0) {
            one["capture_time_unix"] = item.capture_time_unix;
        }

        if (item.has_gps) {
            one["has_gps"] = true;
            one["gps_latitude"] = item.gps_latitude;
            one["gps_longitude"] = item.gps_longitude;

            if (item.has_gps_altitude) {
                one["has_gps_altitude"] = true;
                one["gps_altitude"] = item.gps_altitude;
            }
        } else {
            one["has_gps"] = false;
        }

        one["exif"] = json{
            {"ok", item.exif_ok},
            {"make", item.exif_make},
            {"model", item.exif_model},
            {"lens_model", item.exif_lens_model},
            {"iso", item.exif_iso > 0 ? json(item.exif_iso) : json(nullptr)},
            {"f_number", item.exif_f_number > 0.0 ? json(item.exif_f_number) : json(nullptr)},
            {"exposure_time", item.exif_exposure_time > 0.0 ? json(item.exif_exposure_time) : json(nullptr)},
            {"focal_length", item.exif_focal_length > 0.0 ? json(item.exif_focal_length) : json(nullptr)}
        };

        items_out->push_back(std::move(one));
        ++returned;
    }

    if (dirs_out) *dirs_out = dir_count;
    if (files_out) *files_out = file_count;
    if (bytes_out) *bytes_out = file_bytes;

    return true;
};

// GET /api/v4/gallery/tree_stats?path=relative/dir
srv.Get("/api/v4/gallery/tree_stats", [&](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;
    (void)role;

    auto uopt = users.get(fp_hex);
    if (!uopt.has_value()) {
        reply_json(res, 403, json{
            {"ok", false},
            {"error", "forbidden"},
            {"message", "policy denied"}
        }.dump());
        return;
    }

    const auto& u = *uopt;
    if (u.storage_state != "allocated") {
        reply_json(res, 403, json{
            {"ok", false},
            {"error", "storage_unallocated"},
            {"message", "Storage not allocated"},
            {"fingerprint_hex", fp_hex},
            {"quota_bytes", u.quota_bytes}
        }.dump());
        return;
    }

    std::string rel_root;
    const std::string raw_path_param = req.has_param("path") ? req.get_param_value("path") : std::string();
    if (!raw_path_param.empty()) {
        std::string nerr;
        if (!pqnas::normalize_user_rel_path_strict(raw_path_param, &rel_root, &nerr)) {
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "invalid path"}
            }.dump());
            return;
        }
    }

    const bool force =
        req.has_param("force") &&
        (req.get_param_value("force") == "1" ||
         req.get_param_value("force") == "true");

    const std::string cache_key = fp_hex + "\n" + rel_root;

    static std::mutex tree_cache_mu;
    static std::map<std::string, std::pair<std::int64_t, json>> tree_cache;

    const std::int64_t now_epoch = static_cast<std::int64_t>(std::time(nullptr));
    constexpr std::int64_t kTreeCacheTtlSeconds = 20;
    constexpr std::size_t kTreeCacheMaxEntries = 256;

    if (!force) {
        std::lock_guard<std::mutex> lk(tree_cache_mu);
        gallery_fast_prune_cache(tree_cache, now_epoch, kTreeCacheTtlSeconds, kTreeCacheMaxEntries);

        auto it = tree_cache.find(cache_key);
        if (it != tree_cache.end() && now_epoch - it->second.first <= kTreeCacheTtlSeconds) {
            json cached = it->second.second;
            cached["cache"] = "memory";
            reply_json(res, 200, cached.dump());
            return;
        }
    }

    const std::filesystem::path user_dir = user_dir_for_fp(users, fp_hex);

    json unused_items = json::array();
    std::uint64_t dirs = 0;
    std::uint64_t files = 0;
    std::uint64_t bytes = 0;
    bool truncated = false;
    std::string err_code;
    std::string err_msg;

    if (!collect_fast_gallery_tree(fp_hex,
                                   user_dir,
                                   rel_root,
                                   false,
                                   false,
                                   &unused_items,
                                   &dirs,
                                   &files,
                                   &bytes,
                                   &truncated,
                                   &err_code,
                                   &err_msg)) {
        const int http = (err_code == "not_found") ? 404 :
                         (err_code == "bad_request") ? 400 : 500;

        reply_json(res, http, json{
            {"ok", false},
            {"error", err_code.empty() ? "server_error" : err_code},
            {"message", err_msg.empty() ? "gallery tree stats failed" : err_msg}
        }.dump());
        return;
    }

    json out{
        {"ok", true},
        {"path", rel_root},
        {"stats", {
            {"dirs", dirs},
            {"files", files},
            {"file_bytes", bytes}
        }}
    };

    {
        std::lock_guard<std::mutex> lk(tree_cache_mu);
        gallery_fast_prune_cache(tree_cache, now_epoch, kTreeCacheTtlSeconds, kTreeCacheMaxEntries);
        tree_cache[cache_key] = {now_epoch, out};
    }
    reply_json(res, 200, out.dump());
});

// GET /api/v4/gallery/search?path=relative/dir
srv.Get("/api/v4/gallery/search", [&](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;
    (void)role;

    auto uopt = users.get(fp_hex);
    if (!uopt.has_value()) {
        reply_json(res, 403, json{
            {"ok", false},
            {"error", "forbidden"},
            {"message", "policy denied"}
        }.dump());
        return;
    }

    const auto& u = *uopt;
    if (u.storage_state != "allocated") {
        reply_json(res, 403, json{
            {"ok", false},
            {"error", "storage_unallocated"},
            {"message", "Storage not allocated"},
            {"fingerprint_hex", fp_hex},
            {"quota_bytes", u.quota_bytes}
        }.dump());
        return;
    }

    std::string rel_root;
    if (req.has_param("path")) {
        std::string nerr;
        if (!pqnas::normalize_user_rel_path_strict(req.get_param_value("path"), &rel_root, &nerr)) {
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "invalid path"}
            }.dump());
            return;
        }
    }

    const bool force =
        req.has_param("force") &&
        (req.get_param_value("force") == "1" ||
         req.get_param_value("force") == "true");

    const std::string cache_key = fp_hex + "\n" + rel_root;

    static std::mutex search_cache_mu;
    static std::map<std::string, std::pair<std::int64_t, json>> search_cache;

    const std::int64_t now_epoch = static_cast<std::int64_t>(std::time(nullptr));
    constexpr std::int64_t kSearchCacheTtlSeconds = 20;
    constexpr std::size_t kSearchCacheMaxEntries = 128;

    if (!force) {
        std::lock_guard<std::mutex> lk(search_cache_mu);
        gallery_fast_prune_cache(search_cache, now_epoch, kSearchCacheTtlSeconds, kSearchCacheMaxEntries);

        auto it = search_cache.find(cache_key);
        if (it != search_cache.end() && now_epoch - it->second.first <= kSearchCacheTtlSeconds) {
            json cached = it->second.second;
            cached["cache"] = "memory";
            reply_json(res, 200, cached.dump());
            return;
        }
    }

    const std::filesystem::path user_dir = user_dir_for_fp(users, fp_hex);

    json items = json::array();
    std::uint64_t dirs = 0;
    std::uint64_t files = 0;
    std::uint64_t bytes = 0;
    bool truncated = false;
    std::string err_code;
    std::string err_msg;

    if (!collect_fast_gallery_tree(fp_hex,
                                   user_dir,
                                   rel_root,
                                   true,
                                   force,
                                   &items,
                                   &dirs,
                                   &files,
                                   &bytes,
                                   &truncated,
                                   &err_code,
                                   &err_msg)) {
        const int http = (err_code == "not_found") ? 404 :
                         (err_code == "bad_request") ? 400 : 500;

        reply_json(res, http, json{
            {"ok", false},
            {"error", err_code.empty() ? "server_error" : err_code},
            {"message", err_msg.empty() ? "gallery search failed" : err_msg}
        }.dump());
        return;
    }

    json out{
        {"ok", true},
        {"path", rel_root},
        {"items", items},
        {"stats", {
            {"dirs", dirs},
            {"files", files},
            {"file_bytes", bytes}
        }},
        {"truncated", truncated}
    };

    {
        std::lock_guard<std::mutex> lk(search_cache_mu);
        gallery_fast_prune_cache(search_cache, now_epoch, kSearchCacheTtlSeconds, kSearchCacheMaxEntries);
        search_cache[cache_key] = {now_epoch, out};
    }

    reply_json(res, 200, out.dump());
});



// ---- Reel Stack persistent video index --------------------------------------
// GET  /api/v4/reelstack/index
// POST /api/v4/reelstack/scan
//
// The browser should not have to recursively scan the file tree every time the
// app opens. The scan endpoint builds a per-user cached index under:
//   <pqnas-root>/cache/reelstack_index/<fingerprint>/index.json
//
// Scan sources:
// - metadata-backed logical namespace via FileLocationIndex
// - physical user tree fallback for legacy/simple files not represented there
//
// This keeps landing-tier / migrated files visible while still catching older
// direct-on-disk files.
auto reelstack_lower_ascii_local = [](std::string text) -> std::string {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
};

auto reelstack_basename_local = [](const std::string& path) -> std::string {
    std::string s = path;
    for (char& c : s) {
        if (c == '\\') c = '/';
    }

    while (!s.empty() && s.back() == '/') s.pop_back();

    const auto pos = s.rfind('/');
    if (pos == std::string::npos) return s;
    return s.substr(pos + 1);
};

auto reelstack_is_video_name_local = [&](const std::string& name) -> bool {
    const std::string n = reelstack_lower_ascii_local(name);
    const auto dot = n.rfind('.');
    if (dot == std::string::npos || dot + 1 >= n.size()) return false;

    const std::string ext = n.substr(dot + 1);
    return ext == "mp4"  || ext == "m4v" ||
           ext == "mov"  || ext == "webm" ||
           ext == "mkv"  || ext == "avi" ||
           ext == "wmv"  || ext == "flv" ||
           ext == "mpeg" || ext == "mpg" ||
           ext == "3gp"  || ext == "ogv" ||
           ext == "ogg";
};

auto reelstack_join_rel_local = [](std::string parent, std::string name) -> std::string {
    for (char& c : parent) {
        if (c == '\\') c = '/';
    }
    for (char& c : name) {
        if (c == '\\') c = '/';
    }

    while (!parent.empty() && parent.front() == '/') parent.erase(parent.begin());
    while (!parent.empty() && parent.back() == '/') parent.pop_back();

    while (!name.empty() && name.front() == '/') name.erase(name.begin());
    while (!name.empty() && name.back() == '/') name.pop_back();

    if (parent.empty()) return name;
    if (name.empty()) return parent;
    return parent + "/" + name;
};

auto reelstack_reserved_rel_local = [](const std::string& rel) -> bool {
    return rel == ".pqnas" || rel.rfind(".pqnas/", 0) == 0;
};

auto reelstack_index_path_local = [&](const std::string& fp_hex) -> std::filesystem::path {
    return std::filesystem::path(pqnas::data_root_dir()).parent_path()
         / "cache"
         / "reelstack_index"
         / fp_hex
         / "index.json";
};


std::mutex reelstack_meta_db_mu;

auto reelstack_trim_copy = [](std::string text) -> std::string {
    auto is_ws = [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };

    std::size_t a = 0;
    while (a < text.size() && is_ws(static_cast<unsigned char>(text[a]))) ++a;

    std::size_t b = text.size();
    while (b > a && is_ws(static_cast<unsigned char>(text[b - 1]))) --b;

    return text.substr(a, b - a);
};

auto reelstack_clamp_text = [](std::string text, std::size_t max_bytes) -> std::string {
    if (text.size() > max_bytes) text.resize(max_bytes);
    return text;
};

auto reelstack_split_tags_text =
    [&](const std::string& tags_text) -> json {
        json out = json::array();

        std::size_t start = 0;
        while (start <= tags_text.size()) {
            const std::size_t comma = tags_text.find(',', start);
            std::string part = (comma == std::string::npos)
                ? tags_text.substr(start)
                : tags_text.substr(start, comma - start);

            part = reelstack_trim_copy(part);
            if (!part.empty()) {
                bool seen = false;
                for (const auto& existing : out) {
                    if (existing.is_string() && existing.get<std::string>() == part) {
                        seen = true;
                        break;
                    }
                }
                if (!seen) out.push_back(part);
            }

            if (comma == std::string::npos) break;
            start = comma + 1;
        }

        return out;
    };

auto reelstack_tags_text_from_json =
    [&](const json& value) -> std::string {
        if (value.is_string()) {
            return reelstack_clamp_text(reelstack_trim_copy(value.get<std::string>()), 1000);
        }

        if (!value.is_array()) return "";

        std::string out;
        for (const auto& item : value) {
            if (!item.is_string()) continue;

            std::string tag = reelstack_trim_copy(item.get<std::string>());
            if (tag.empty()) continue;
            tag = reelstack_clamp_text(tag, 80);

            if (!out.empty()) out += ", ";
            out += tag;
        }

        return reelstack_clamp_text(out, 1000);
    };

auto reelstack_user_meta_default =
    [&](const std::string& rel_norm) -> json {
        return json{
            {"path", rel_norm},
            {"title", ""},
            {"tags_text", ""},
            {"tags", json::array()},
            {"notes", ""},
            {"rating", 0},
            {"watched", false},
            {"favorite", false},
            {"poster_rel_path", ""},
            {"created_epoch", 0},
            {"updated_epoch", 0}
        };
    };

auto reelstack_meta_db_path = [&]() -> std::filesystem::path {
    return std::filesystem::path(pqnas::data_root_dir()).parent_path()
         / "config"
         / "reelstack_meta.sqlite3";
};

auto reelstack_open_meta_db =
    [&](sqlite3** out_db, std::string* err) -> bool {
        if (out_db) *out_db = nullptr;
        if (err) err->clear();

        const std::filesystem::path db_path = reelstack_meta_db_path();

        std::error_code ec;
        std::filesystem::create_directories(db_path.parent_path(), ec);
        if (ec) {
            if (err) *err = "create config dir failed: " + ec.message();
            return false;
        }

        sqlite3* db = nullptr;
        if (sqlite3_open(db_path.string().c_str(), &db) != SQLITE_OK) {
            if (err) *err = db ? sqlite3_errmsg(db) : "sqlite open failed";
            if (db) sqlite3_close(db);
            return false;
        }

        sqlite3_busy_timeout(db, 5000);

        const char* schema = R"SQL(
CREATE TABLE IF NOT EXISTS reelstack_meta (
    scope_type        TEXT NOT NULL,
    scope_id          TEXT NOT NULL,
    logical_rel_path  TEXT NOT NULL,
    title             TEXT NOT NULL DEFAULT '',
    tags_text         TEXT NOT NULL DEFAULT '',
    notes_text        TEXT NOT NULL DEFAULT '',
    rating            INTEGER NOT NULL DEFAULT 0,
    watched           INTEGER NOT NULL DEFAULT 0,
    favorite          INTEGER NOT NULL DEFAULT 0,
    poster_rel_path   TEXT NOT NULL DEFAULT '',
    created_epoch     INTEGER NOT NULL,
    updated_epoch     INTEGER NOT NULL,
    PRIMARY KEY (scope_type, scope_id, logical_rel_path)
);

CREATE INDEX IF NOT EXISTS idx_reelstack_meta_scope_rating
ON reelstack_meta(scope_type, scope_id, rating);

CREATE INDEX IF NOT EXISTS idx_reelstack_meta_scope_watched
ON reelstack_meta(scope_type, scope_id, watched);

CREATE INDEX IF NOT EXISTS idx_reelstack_meta_scope_favorite
ON reelstack_meta(scope_type, scope_id, favorite);
)SQL";

        char* errmsg = nullptr;
        const int rc = sqlite3_exec(db, schema, nullptr, nullptr, &errmsg);
        if (rc != SQLITE_OK) {
            if (err) *err = errmsg ? errmsg : sqlite3_errmsg(db);
            if (errmsg) sqlite3_free(errmsg);
            sqlite3_close(db);
            return false;
        }

        if (out_db) *out_db = db;
        return true;
    };

auto reelstack_read_user_meta =
    [&](const std::string& fp_hex,
        const std::string& rel_norm,
        json* out,
        std::string* err) -> bool {
        if (out) *out = reelstack_user_meta_default(rel_norm);
        if (err) err->clear();

        std::lock_guard<std::mutex> lk(reelstack_meta_db_mu);

        sqlite3* db = nullptr;
        if (!reelstack_open_meta_db(&db, err)) return false;

        const char* sql =
            "SELECT title, tags_text, notes_text, rating, watched, favorite, "
            "       poster_rel_path, created_epoch, updated_epoch "
            "FROM reelstack_meta "
            "WHERE scope_type = 'user' AND scope_id = ?1 AND logical_rel_path = ?2 "
            "LIMIT 1";

        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
            if (err) *err = sqlite3_errmsg(db);
            sqlite3_close(db);
            return false;
        }

        sqlite3_bind_text(st, 1, fp_hex.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, rel_norm.c_str(), -1, SQLITE_TRANSIENT);

        const int rc = sqlite3_step(st);
        if (rc == SQLITE_DONE) {
            sqlite3_finalize(st);
            sqlite3_close(db);
            return true;
        }

        if (rc != SQLITE_ROW) {
            if (err) *err = sqlite3_errmsg(db);
            sqlite3_finalize(st);
            sqlite3_close(db);
            return false;
        }

        auto col_text = [&](int idx) -> std::string {
            const unsigned char* p = sqlite3_column_text(st, idx);
            return p ? reinterpret_cast<const char*>(p) : "";
        };

        const std::string tags_text = col_text(1);

        json meta = reelstack_user_meta_default(rel_norm);
        meta["title"] = col_text(0);
        meta["tags_text"] = tags_text;
        meta["tags"] = reelstack_split_tags_text(tags_text);
        meta["notes"] = col_text(2);
        meta["rating"] = sqlite3_column_int(st, 3);
        meta["watched"] = sqlite3_column_int(st, 4) != 0;
        meta["favorite"] = sqlite3_column_int(st, 5) != 0;
        meta["poster_rel_path"] = col_text(6);
        meta["created_epoch"] = static_cast<std::int64_t>(sqlite3_column_int64(st, 7));
        meta["updated_epoch"] = static_cast<std::int64_t>(sqlite3_column_int64(st, 8));

        if (out) *out = std::move(meta);

        sqlite3_finalize(st);
        sqlite3_close(db);
        return true;
    };

auto reelstack_write_user_meta =
    [&](const std::string& fp_hex,
        const std::string& rel_norm,
        const std::string& title_in,
        const std::string& tags_text_in,
        const std::string& notes_in,
        int rating_in,
        bool watched_in,
        bool favorite_in,
        json* out,
        std::string* err) -> bool {
        if (err) err->clear();

        std::string title = reelstack_clamp_text(reelstack_trim_copy(title_in), 240);
        std::string tags_text = reelstack_clamp_text(reelstack_trim_copy(tags_text_in), 1000);
        std::string notes = reelstack_clamp_text(notes_in, 8000);

        int rating = rating_in;
        if (rating < 0) rating = 0;
        if (rating > 5) rating = 5;

        const std::int64_t now_ts = static_cast<std::int64_t>(std::time(nullptr));

        {
            std::lock_guard<std::mutex> lk(reelstack_meta_db_mu);

            sqlite3* db = nullptr;
            if (!reelstack_open_meta_db(&db, err)) return false;

            const char* sql =
                "INSERT INTO reelstack_meta ("
                "  scope_type, scope_id, logical_rel_path, title, tags_text, notes_text, "
                "  rating, watched, favorite, poster_rel_path, created_epoch, updated_epoch"
                ") VALUES ("
                "  'user', ?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, '', ?9, ?10"
                ") "
                "ON CONFLICT(scope_type, scope_id, logical_rel_path) DO UPDATE SET "
                "  title = excluded.title, "
                "  tags_text = excluded.tags_text, "
                "  notes_text = excluded.notes_text, "
                "  rating = excluded.rating, "
                "  watched = excluded.watched, "
                "  favorite = excluded.favorite, "
                "  updated_epoch = excluded.updated_epoch";

            sqlite3_stmt* st = nullptr;
            if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
                if (err) *err = sqlite3_errmsg(db);
                sqlite3_close(db);
                return false;
            }

            sqlite3_bind_text(st, 1, fp_hex.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 2, rel_norm.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 3, title.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 4, tags_text.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 5, notes.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(st, 6, rating);
            sqlite3_bind_int(st, 7, watched_in ? 1 : 0);
            sqlite3_bind_int(st, 8, favorite_in ? 1 : 0);
            sqlite3_bind_int64(st, 9, static_cast<sqlite3_int64>(now_ts));
            sqlite3_bind_int64(st, 10, static_cast<sqlite3_int64>(now_ts));

            const int rc = sqlite3_step(st);
            if (rc != SQLITE_DONE) {
                if (err) *err = sqlite3_errmsg(db);
                sqlite3_finalize(st);
                sqlite3_close(db);
                return false;
            }

            sqlite3_finalize(st);
            sqlite3_close(db);
        }

        if (out) {
            json meta = reelstack_user_meta_default(rel_norm);
            meta["title"] = title;
            meta["tags_text"] = tags_text;
            meta["tags"] = reelstack_split_tags_text(tags_text);
            meta["notes"] = notes;
            meta["rating"] = rating;
            meta["watched"] = watched_in;
            meta["favorite"] = favorite_in;
            meta["updated_epoch"] = now_ts;
            *out = std::move(meta);
        }

        return true;
    };

auto reelstack_is_supported_video_name = [](const std::string& name) -> bool {
    std::string n = name;
    for (char& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    const auto dot = n.rfind('.');
    if (dot == std::string::npos || dot + 1 >= n.size()) return false;

    const std::string ext = n.substr(dot + 1);
    return ext == "mp4"  || ext == "m4v" ||
           ext == "mov"  || ext == "webm" ||
           ext == "mkv"  || ext == "avi" ||
           ext == "wmv"  || ext == "flv" ||
           ext == "mpeg" || ext == "mpg" ||
           ext == "3gp"  || ext == "ogv" ||
           ext == "ogg";
};


srv.Get("/api/v4/reelstack/index", [&](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;
    (void)role;

    auto reply = [&](int http, const json& body) {
        res.status = http;
        res.set_header("Cache-Control", "no-store");
        res.set_content(body.dump(2), "application/json; charset=utf-8");
    };

    {
        auto uopt = users.get(fp_hex);
        if (!uopt.has_value() || uopt->storage_state != "allocated") {
            reply(403, json{
                {"ok", false},
                {"error", "storage_unallocated"},
                {"message", "Storage not allocated"}
            });
            return;
        }
    }

    const std::filesystem::path index_abs = reelstack_index_path_local(fp_hex);

    std::error_code ec;
    if (!std::filesystem::exists(index_abs, ec)) {
        reply(200, json{
            {"ok", true},
            {"version", 1},
            {"generated_at_epoch", 0},
            {"items", json::array()},
            {"stats", {
                {"videos", 0},
                {"files_seen", 0},
                {"dirs_seen", 0},
                {"truncated", false}
            }},
            {"message", "no saved Reel Stack index yet"}
        });
        return;
    }

    std::string body;
    if (!read_file_to_string(index_abs.string(), body) || body.empty()) {
        reply(500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "failed to read Reel Stack index"}
        });
        return;
    }

    json j = json::parse(body, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        reply(500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "Reel Stack index is corrupt"}
        });
        return;
    }

    j["ok"] = true;
    if (!j.contains("version")) j["version"] = 1;
    if (!j.contains("items") || !j["items"].is_array()) j["items"] = json::array();

    reply(200, j);
});

srv.Post("/api/v4/reelstack/scan", [&](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;
    (void)role;

    if (!require_same_origin_for_cookie_mutation(req, res)) return;

    auto reply = [&](int http, const json& body) {
        res.status = http;
        res.set_header("Cache-Control", "no-store");
        res.set_content(body.dump(2), "application/json; charset=utf-8");
    };

    auto audit_fail = [&](const std::string& reason, int http, const std::string& detail = "") {
        pqnas::AuditEvent ev;
        ev.event = "v4.reelstack_scan_fail";
        ev.outcome = "fail";
        ev.f["fingerprint"] = fp_hex;
        ev.f["reason"] = reason;
        ev.f["http"] = std::to_string(http);
        if (!detail.empty()) ev.f["detail"] = pqnas::shorten(detail, 180);
        audit_append(ev);
    };

    {
        auto uopt = users.get(fp_hex);
        if (!uopt.has_value() || uopt->storage_state != "allocated") {
            audit_fail("storage_unallocated", 403);
            reply(403, json{
                {"ok", false},
                {"error", "storage_unallocated"},
                {"message", "Storage not allocated"}
            });
            return;
        }
    }

    std::string root_rel;
    {
        json body = json::parse(req.body, nullptr, false);
        if (!body.is_discarded() && body.is_object()) {
            root_rel = body.value("path", "");
        }
    }

    if (root_rel == "." || root_rel == "./" || root_rel == "/") root_rel.clear();

    std::string root_norm;
    if (!root_rel.empty()) {
        std::string nerr;
        if (!pqnas::normalize_user_rel_path_strict(root_rel, &root_norm, &nerr)) {
            audit_fail("invalid_path", 400, nerr);
            reply(400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "invalid path"}
            });
            return;
        }
    }

    static constexpr std::uint64_t kMaxDirs = 5000;
    static constexpr std::uint64_t kMaxFiles = 250000;
    static constexpr std::uint64_t kMaxVideos = 50000;

    std::map<std::string, json> video_map;

    std::uint64_t metadata_dirs_seen = 0;
    std::uint64_t physical_dirs_seen = 0;
    std::uint64_t files_seen = 0;
    bool truncated = false;
    json warnings = json::array();

    auto add_video = [&](std::string rel_path,
                         std::uint64_t size_bytes,
                         std::int64_t mtime_epoch,
                         const std::string& source) {
        for (char& c : rel_path) {
            if (c == '\\') c = '/';
        }
        while (!rel_path.empty() && rel_path.front() == '/') rel_path.erase(rel_path.begin());

        if (rel_path.empty()) return;
        if (reelstack_reserved_rel_local(rel_path)) return;

        const std::string name = reelstack_basename_local(rel_path);
        if (!reelstack_is_video_name_local(name)) return;

        if (video_map.size() >= kMaxVideos) {
            truncated = true;
            return;
        }

        if (video_map.find(rel_path) != video_map.end()) return;

        video_map.emplace(rel_path, json{
            {"name", name},
            {"path", rel_path},
            {"size_bytes", size_bytes},
            {"mtime_epoch", mtime_epoch},
            {"source", source}
        });
    };

    // 1) Metadata-backed logical scan. This sees tiered/landing files.
    if (auto* idx = pqnas::get_file_location_index()) {
        std::vector<std::string> queue;
        queue.push_back(root_norm);

        std::map<std::string, bool> seen_dirs;
        std::size_t pos = 0;

        while (pos < queue.size()) {
            if (metadata_dirs_seen >= kMaxDirs) {
                truncated = true;
                warnings.push_back("metadata directory scan limit reached");
                break;
            }

            const std::string cur = queue[pos++];
            if (seen_dirs[cur]) continue;
            seen_dirs[cur] = true;
            metadata_dirs_seen++;

            std::string lerr;
            auto rows = idx->list_immediate_children(fp_hex, cur, &lerr);
            if (!lerr.empty()) {
                warnings.push_back("metadata list failed: " + pqnas::shorten(lerr, 160));
                break;
            }

            for (const auto& row : rows) {
                const std::string child_rel = reelstack_join_rel_local(cur, row.name);
                if (reelstack_reserved_rel_local(child_rel)) continue;

                if (row.type == "dir") {
                    queue.push_back(child_rel);
                    continue;
                }

                if (row.type != "file") continue;

                files_seen++;
                if (files_seen >= kMaxFiles) {
                    truncated = true;
                    warnings.push_back("file scan limit reached");
                    break;
                }

                add_video(child_rel, row.size_bytes, row.mtime_epoch, "metadata");
            }

            if (truncated) break;
        }
    } else {
        warnings.push_back("file location index unavailable; using physical fallback only");
    }

    // 2) Physical fallback scan. This catches legacy/simple files not yet in metadata.
    const std::filesystem::path user_dir = user_dir_for_fp(users, fp_hex);
    std::filesystem::path physical_base = user_dir;

    if (!root_norm.empty()) {
        std::string perr;
        if (!pqnas::resolve_user_path_strict(user_dir, root_norm, &physical_base, &perr)) {
            physical_base.clear();
        }
    }

    if (!physical_base.empty()) {
        std::error_code ec;
        auto st = std::filesystem::symlink_status(physical_base, ec);

        if (!ec &&
            std::filesystem::exists(st) &&
            !std::filesystem::is_symlink(st) &&
            std::filesystem::is_directory(st)) {

            std::filesystem::directory_options opts =
                std::filesystem::directory_options::skip_permission_denied;

            std::filesystem::recursive_directory_iterator it(physical_base, opts, ec);
            std::filesystem::recursive_directory_iterator end;

            while (!ec && it != end) {
                if (physical_dirs_seen >= kMaxDirs || files_seen >= kMaxFiles) {
                    truncated = true;
                    warnings.push_back("physical scan limit reached");
                    break;
                }

                const std::filesystem::path p = it->path();

                std::error_code rel_ec;
                std::filesystem::path rel_fs = std::filesystem::relative(p, user_dir, rel_ec);
                std::string rel = rel_ec ? std::string{} : rel_fs.generic_string();

                if (rel.empty() || reelstack_reserved_rel_local(rel)) {
                    std::error_code sec;
                    auto sst = std::filesystem::symlink_status(p, sec);
                    if (!sec && std::filesystem::is_directory(sst)) {
                        it.disable_recursion_pending();
                    }
                    it.increment(ec);
                    continue;
                }

                std::error_code sec;
                auto sst = std::filesystem::symlink_status(p, sec);
                if (sec || std::filesystem::is_symlink(sst)) {
                    it.increment(ec);
                    continue;
                }

                if (std::filesystem::is_directory(sst)) {
                    if (p.filename().string() == ".pqnas") {
                        it.disable_recursion_pending();
                    } else {
                        physical_dirs_seen++;
                    }

                    it.increment(ec);
                    continue;
                }

                if (!std::filesystem::is_regular_file(sst)) {
                    it.increment(ec);
                    continue;
                }

                files_seen++;

                std::uint64_t size_bytes = pqnas::file_size_u64_safe(p);

                std::int64_t mtime_epoch = 0;
                {
                    std::error_code mt_ec;
                    auto ft = std::filesystem::last_write_time(p, mt_ec);
                    if (!mt_ec) {
                        using namespace std::chrono;
                        auto sctp = time_point_cast<system_clock::duration>(
                            ft - std::filesystem::file_time_type::clock::now() + system_clock::now()
                        );
                        mtime_epoch = static_cast<std::int64_t>(
                            duration_cast<seconds>(sctp.time_since_epoch()).count()
                        );
                    }
                }

                add_video(rel, size_bytes, mtime_epoch, "physical");

                it.increment(ec);
            }

            if (ec) {
                warnings.push_back("physical scan stopped: " + pqnas::shorten(ec.message(), 160));
            }
        }
    }

    json items = json::array();
    for (const auto& kv : video_map) {
        items.push_back(kv.second);
    }

    const std::int64_t now_ts = now_epoch_sec();

    json out = {
        {"ok", true},
        {"version", 1},
        {"generated_at_epoch", now_ts},
        {"root", root_norm},
        {"items", items},
        {"stats", {
            {"videos", static_cast<std::uint64_t>(video_map.size())},
            {"files_seen", files_seen},
            {"metadata_dirs_seen", metadata_dirs_seen},
            {"physical_dirs_seen", physical_dirs_seen},
            {"dirs_seen", metadata_dirs_seen + physical_dirs_seen},
            {"truncated", truncated}
        }},
        {"warnings", warnings}
    };

    const std::filesystem::path index_abs = reelstack_index_path_local(fp_hex);
    const std::filesystem::path index_dir = index_abs.parent_path();

    std::error_code ec;
    std::filesystem::create_directories(index_dir, ec);
    if (ec) {
        audit_fail("cache_mkdir_failed", 500, ec.message());
        reply(500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "failed to create Reel Stack index cache"}
        });
        return;
    }

    const std::filesystem::path tmp_abs =
        index_dir / ("index.json.tmp." + random_b64url(8));

    {
        std::ofstream f(tmp_abs, std::ios::binary | std::ios::trunc);
        if (!f.good()) {
            audit_fail("cache_write_open_failed", 500);
            reply(500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to write Reel Stack index"}
            });
            return;
        }

        f << out.dump(2);

        if (!f.good()) {
            std::error_code rm_ec;
            std::filesystem::remove(tmp_abs, rm_ec);

            audit_fail("cache_write_failed", 500);
            reply(500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to write Reel Stack index"}
            });
            return;
        }
    }

    std::filesystem::rename(tmp_abs, index_abs, ec);
    if (ec) {
        std::error_code rm_ec;
        std::filesystem::remove(tmp_abs, rm_ec);

        audit_fail("cache_rename_failed", 500, ec.message());
        reply(500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "failed to finalize Reel Stack index"}
        });
        return;
    }

    {
        pqnas::AuditEvent ev;
        ev.event = "v4.reelstack_scan_ok";
        ev.outcome = "ok";
        ev.f["fingerprint"] = fp_hex;
        ev.f["videos"] = std::to_string(static_cast<unsigned long long>(video_map.size()));
        ev.f["files_seen"] = std::to_string(static_cast<unsigned long long>(files_seen));
        ev.f["dirs_seen"] = std::to_string(static_cast<unsigned long long>(metadata_dirs_seen + physical_dirs_seen));
        ev.f["truncated"] = truncated ? "1" : "0";
        audit_append(ev);
    }

    reply(200, out);
});


srv.Get("/api/v4/reelstack/thumb", [&](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;
    (void)role;

    auto fail_json = [&](int http, const std::string& error, const std::string& message) {
        reply_json(res, http, json{
            {"ok", false},
            {"error", error},
            {"message", message}
        }.dump());
    };

    auto lower_ascii = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };

    auto supported_video_ext = [&](const std::string& name) -> bool {
        const std::string n = lower_ascii(name);
        const auto dot = n.rfind('.');
        if (dot == std::string::npos || dot + 1 >= n.size()) return false;
        const std::string ext = n.substr(dot + 1);
        return ext == "mp4"  || ext == "m4v" ||
               ext == "mov"  || ext == "webm" ||
               ext == "mkv"  || ext == "avi" ||
               ext == "wmv"  || ext == "flv" ||
               ext == "mpeg" || ext == "mpg" ||
               ext == "3gp"  || ext == "ogv" ||
               ext == "ogg";
    };

    auto file_mtime_epoch = [&](const std::filesystem::path& path) -> std::int64_t {
        std::error_code ec;
        auto ft = std::filesystem::last_write_time(path, ec);
        if (ec) return 0;

        using namespace std::chrono;
        auto sctp = time_point_cast<system_clock::duration>(
            ft - std::filesystem::file_time_type::clock::now() + system_clock::now()
        );
        return static_cast<std::int64_t>(duration_cast<seconds>(sctp.time_since_epoch()).count());
    };

    auto fnv1a64 = [](const std::string& text) -> std::uint64_t {
        std::uint64_t h = 1469598103934665603ULL;
        for (unsigned char c : text) {
            h ^= static_cast<std::uint64_t>(c);
            h *= 1099511628211ULL;
        }
        return h;
    };

    auto hex_u64 = [](std::uint64_t v) -> std::string {
        std::ostringstream oss;
        oss << std::hex << std::nouppercase << v;
        return oss.str();
    };

    auto read_small_file = [&](const std::filesystem::path& path,
                               std::string* out,
                               std::string* err) -> bool {
        if (out) out->clear();
        if (err) err->clear();

        std::ifstream f(path, std::ios::binary);
        if (!f.good()) {
            if (err) *err = "open failed";
            return false;
        }

        std::ostringstream oss;
        oss << f.rdbuf();

        if (!f.good() && !f.eof()) {
            if (err) *err = "read failed";
            return false;
        }

        if (out) *out = oss.str();
        return true;
    };

    auto spawn_wait = [&](const std::vector<std::string>& args) -> int {
        if (args.empty()) return 127;

        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);

        pid_t pid = fork();
        if (pid < 0) return 127;

        if (pid == 0) {
            int devnull = open("/dev/null", O_WRONLY);
            if (devnull >= 0) {
                dup2(devnull, STDOUT_FILENO);
                dup2(devnull, STDERR_FILENO);
                close(devnull);
            }

            execvp(argv[0], argv.data());
            _exit(127);
        }

        int st = 0;
        while (waitpid(pid, &st, 0) < 0) {
            if (errno == EINTR) continue;
            return 127;
        }

        if (WIFEXITED(st)) return WEXITSTATUS(st);
        if (WIFSIGNALED(st)) return 128 + WTERMSIG(st);
        return 127;
    };

    auto generate_thumb = [&](const std::filesystem::path& src_abs,
                              const std::filesystem::path& dst_abs,
                              int size,
                              std::string* err) -> bool {
        if (err) err->clear();

        const std::vector<std::string> ffmpegs = {
            "/usr/bin/ffmpeg",
            "/usr/local/bin/ffmpeg",
            "ffmpeg"
        };

        const std::vector<std::string> seeks = {
            "1.0",
            "0.2",
            "0"
        };

        std::vector<std::string> attempts;

        for (const auto& exe : ffmpegs) {
            for (const auto& seek : seeks) {
                std::error_code rm_ec;
                std::filesystem::remove(dst_abs, rm_ec);

                std::vector<std::string> args = {
                    exe,
                    "-nostdin",
                    "-y",
                    "-hide_banner",
                    "-loglevel", "error",
                    "-ss", seek,
                    "-i", src_abs.string(),
                    "-frames:v", "1",
                    "-an",
                    "-sn",
                    "-vf", "scale=" + std::to_string(size) + ":-2",
                    "-q:v", "3",
                    dst_abs.string()
                };

                const int rc = spawn_wait(args);
                attempts.push_back(exe + "@" + seek + "=" + std::to_string(rc));

                if (rc != 0) continue;

                std::error_code ec;
                auto st = std::filesystem::symlink_status(dst_abs, ec);
                if (!ec &&
                    std::filesystem::exists(st) &&
                    !std::filesystem::is_symlink(st) &&
                    std::filesystem::is_regular_file(st) &&
                    pqnas::file_size_u64_safe(dst_abs) > 0) {
                    return true;
                }
            }
        }

        if (err) {
            std::string joined;
            for (size_t i = 0; i < attempts.size(); ++i) {
                if (i) joined += ", ";
                joined += attempts[i];
            }
            *err = joined;
        }
        return false;
    };

    auto cache_root = [&]() -> std::filesystem::path {
        return std::filesystem::path(pqnas::data_root_dir()).parent_path()
             / "cache"
             / "reelstack_thumbs";
    };

    {
        auto uopt = users.get(fp_hex);
        if (!uopt.has_value() || uopt->storage_state != "allocated") {
            fail_json(403, "storage_unallocated", "Storage not allocated");
            return;
        }
    }

    const std::string rel_path = req.has_param("path") ? req.get_param_value("path") : "";
    if (rel_path.empty()) {
        fail_json(400, "bad_request", "missing path");
        return;
    }

    std::string rel_norm;
    {
        std::string nerr;
        if (!pqnas::normalize_user_rel_path_strict(rel_path, &rel_norm, &nerr)) {
            fail_json(400, "bad_request", "invalid path");
            return;
        }
    }

    int size = 480;
    if (req.has_param("size")) {
        try { size = std::stoi(req.get_param_value("size")); }
        catch (...) { size = 480; }
    }
    if (size < 160) size = 160;
    if (size > 960) size = 960;

    pqnas::ResolvedExistingPath rp;
    std::string perr;
    if (!pqnas::resolve_existing_user_file_path(users, fp_hex, rel_path, &rp, &perr)) {
        fail_json(400, "bad_request", "invalid path");
        return;
    }

    const std::filesystem::path abs = rp.abs_path;

    {
        std::string serr;
        if (!ensure_no_symlink_in_existing_path_prefix(abs, &serr)) {
            fail_json(400, "bad_request", "symlinks not supported");
            return;
        }
    }

    std::error_code ec;
    auto st = std::filesystem::symlink_status(abs, ec);
    if (ec || !std::filesystem::exists(st)) {
        fail_json(404, "not_found", "file not found");
        return;
    }

    if (std::filesystem::is_symlink(st) || !std::filesystem::is_regular_file(st)) {
        fail_json(400, "bad_request", "not a regular file");
        return;
    }

    if (!supported_video_ext(abs.filename().string())) {
        fail_json(415, "unsupported_media_type", "unsupported video type");
        return;
    }

    const std::uint64_t src_bytes = pqnas::file_size_u64_safe(abs);
    const std::int64_t src_mtime = file_mtime_epoch(abs);

    const std::string key_material =
        fp_hex + "\n" +
        rel_norm + "\n" +
        std::to_string(size) + "\n" +
        std::to_string((unsigned long long)src_bytes) + "\n" +
        std::to_string((long long)src_mtime);

    const std::string thumb_key = hex_u64(fnv1a64(key_material));

    const std::filesystem::path cache_dir = cache_root() / fp_hex / std::to_string(size);
    const std::filesystem::path cache_abs = cache_dir / (thumb_key + ".jpg");

    auto serve = [&](bool cached) {
        std::string buf;
        std::string rerr;
        if (!read_small_file(cache_abs, &buf, &rerr)) {
            fail_json(500, "server_error", "failed to read thumbnail");
            return;
        }

        pqnas::AuditEvent ev;
        ev.event = "v4.reelstack_thumb_ok";
        ev.outcome = "ok";
        ev.f["fingerprint"] = fp_hex;
        ev.f["path"] = pqnas::shorten(rel_norm, 200);
        ev.f["cached"] = cached ? "1" : "0";
        ev.f["bytes"] = std::to_string((unsigned long long)buf.size());
        audit_append(ev);

        res.set_header("Cache-Control", "private, max-age=86400");
        res.set_content(std::move(buf), "image/jpeg");
    };

    {
        std::error_code cec;
        auto cst = std::filesystem::symlink_status(cache_abs, cec);
        if (!cec &&
            std::filesystem::exists(cst) &&
            !std::filesystem::is_symlink(cst) &&
            std::filesystem::is_regular_file(cst)) {
            serve(true);
            return;
        }
    }

    std::filesystem::create_directories(cache_dir, ec);
    if (ec) {
        fail_json(500, "server_error", "failed to create thumbnail cache directory");
        return;
    }

    const std::filesystem::path tmp_abs =
        cache_dir / (thumb_key + ".tmp." + random_b64url(8) + ".jpg");

    std::string terr;
    if (!generate_thumb(abs, tmp_abs, size, &terr)) {
        std::error_code rm_ec;
        std::filesystem::remove(tmp_abs, rm_ec);

        pqnas::AuditEvent ev;
        ev.event = "v4.reelstack_thumb_fail";
        ev.outcome = "fail";
        ev.f["fingerprint"] = fp_hex;
        ev.f["path"] = pqnas::shorten(rel_norm, 200);
        ev.f["reason"] = "ffmpeg_failed";
        ev.f["detail"] = pqnas::shorten(terr, 180);
        audit_append(ev);

        fail_json(500, "server_error", "failed to generate thumbnail");
        return;
    }

    std::error_code ren_ec;
    std::filesystem::rename(tmp_abs, cache_abs, ren_ec);
    if (ren_ec) {
        std::error_code rm_ec;
        std::filesystem::remove(tmp_abs, rm_ec);
        fail_json(500, "server_error", "failed to finalize thumbnail");
        return;
    }

    serve(false);
});




srv.Get("/api/v4/reelstack/meta", [&](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;
    (void)role;

    auto fail_json = [&](int http, const std::string& error, const std::string& message) {
        reply_json(res, http, json{
            {"ok", false},
            {"error", error},
            {"message", message}
        }.dump());
    };

    auto lower_ascii = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };

    auto supported_video_ext = [&](const std::string& name) -> bool {
        const std::string n = lower_ascii(name);
        const auto dot = n.rfind('.');
        if (dot == std::string::npos || dot + 1 >= n.size()) return false;
        const std::string ext = n.substr(dot + 1);
        return ext == "mp4"  || ext == "m4v" ||
               ext == "mov"  || ext == "webm" ||
               ext == "mkv"  || ext == "avi" ||
               ext == "wmv"  || ext == "flv" ||
               ext == "mpeg" || ext == "mpg" ||
               ext == "3gp"  || ext == "ogv" ||
               ext == "ogg";
    };

    auto file_mtime_epoch = [&](const std::filesystem::path& path) -> std::int64_t {
        std::error_code ec;
        auto ft = std::filesystem::last_write_time(path, ec);
        if (ec) return 0;

        using namespace std::chrono;
        auto sctp = time_point_cast<system_clock::duration>(
            ft - std::filesystem::file_time_type::clock::now() + system_clock::now()
        );
        return static_cast<std::int64_t>(duration_cast<seconds>(sctp.time_since_epoch()).count());
    };

    auto fnv1a64 = [](const std::string& text) -> std::uint64_t {
        std::uint64_t h = 1469598103934665603ULL;
        for (unsigned char c : text) {
            h ^= static_cast<std::uint64_t>(c);
            h *= 1099511628211ULL;
        }
        return h;
    };

    auto hex_u64 = [](std::uint64_t v) -> std::string {
        std::ostringstream oss;
        oss << std::hex << std::nouppercase << v;
        return oss.str();
    };

    auto read_small_file = [&](const std::filesystem::path& path,
                               std::string* out,
                               std::string* err) -> bool {
        if (out) out->clear();
        if (err) err->clear();

        std::ifstream f(path, std::ios::binary);
        if (!f.good()) {
            if (err) *err = "open failed";
            return false;
        }

        std::ostringstream oss;
        oss << f.rdbuf();

        if (!f.good() && !f.eof()) {
            if (err) *err = "read failed";
            return false;
        }

        if (out) *out = oss.str();
        return true;
    };

    auto write_text_atomic = [&](const std::filesystem::path& final_abs,
                                 const std::string& body,
                                 std::string* err) -> bool {
        if (err) err->clear();

        std::error_code ec;
        std::filesystem::create_directories(final_abs.parent_path(), ec);
        if (ec) {
            if (err) *err = "create_directories failed: " + ec.message();
            return false;
        }

        const std::filesystem::path tmp_abs =
            final_abs.parent_path() / (final_abs.filename().string() + ".tmp." + random_b64url(8));

        {
            std::ofstream f(tmp_abs, std::ios::binary | std::ios::trunc);
            if (!f.good()) {
                if (err) *err = "open temp failed";
                return false;
            }
            f << body;
            if (!f.good()) {
                std::filesystem::remove(tmp_abs, ec);
                if (err) *err = "write temp failed";
                return false;
            }
        }

        std::filesystem::rename(tmp_abs, final_abs, ec);
        if (ec) {
            std::filesystem::remove(tmp_abs, ec);
            if (err) *err = "rename failed: " + ec.message();
            return false;
        }

        return true;
    };

    auto spawn_capture = [&](const std::vector<std::string>& args, std::string* out) -> int {
        if (out) out->clear();
        if (args.empty()) return 127;

        int pipefd[2];
        if (pipe(pipefd) != 0) return 127;

        pid_t pid = fork();
        if (pid < 0) {
            close(pipefd[0]);
            close(pipefd[1]);
            return 127;
        }

        if (pid == 0) {
            dup2(pipefd[1], STDOUT_FILENO);
            dup2(pipefd[1], STDERR_FILENO);
            close(pipefd[0]);
            close(pipefd[1]);

            std::vector<char*> argv;
            argv.reserve(args.size() + 1);
            for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
            argv.push_back(nullptr);

            execvp(argv[0], argv.data());
            _exit(127);
        }

        close(pipefd[1]);

        std::string captured;
        char buf[8192];
        for (;;) {
            ssize_t n = read(pipefd[0], buf, sizeof(buf));
            if (n > 0) {
                captured.append(buf, buf + n);
                if (captured.size() > 1024 * 1024) break;
                continue;
            }
            if (n < 0 && errno == EINTR) continue;
            break;
        }

        close(pipefd[0]);

        int st = 0;
        while (waitpid(pid, &st, 0) < 0) {
            if (errno == EINTR) continue;
            return 127;
        }

        if (out) *out = std::move(captured);

        if (WIFEXITED(st)) return WEXITSTATUS(st);
        if (WIFSIGNALED(st)) return 128 + WTERMSIG(st);
        return 127;
    };

    auto scalar_to_string = [](const json& v) -> std::string {
        try {
            if (v.is_string()) return v.get<std::string>();
            if (v.is_number_integer()) return std::to_string(v.get<long long>());
            if (v.is_number_unsigned()) return std::to_string(v.get<unsigned long long>());
            if (v.is_number_float()) {
                std::ostringstream oss;
                oss << v.get<double>();
                return oss.str();
            }
        } catch (...) {}
        return "";
    };

    auto json_to_double = [&](const json& v, double* out) -> bool {
        if (out) *out = 0.0;
        try {
            if (v.is_number()) {
                if (out) *out = v.get<double>();
                return true;
            }
            if (v.is_string()) {
                const std::string s = v.get<std::string>();
                std::size_t pos = 0;
                const double d = std::stod(s, &pos);
                if (pos > 0) {
                    if (out) *out = d;
                    return true;
                }
            }
        } catch (...) {}
        return false;
    };

    auto json_to_i64 = [&](const json& v, long long* out) -> bool {
        if (out) *out = 0;
        try {
            if (v.is_number_integer()) {
                if (out) *out = v.get<long long>();
                return true;
            }
            if (v.is_number_unsigned()) {
                if (out) *out = static_cast<long long>(v.get<unsigned long long>());
                return true;
            }
            if (v.is_string()) {
                const std::string s = v.get<std::string>();
                std::size_t pos = 0;
                const long long d = std::stoll(s, &pos);
                if (pos > 0) {
                    if (out) *out = d;
                    return true;
                }
            }
        } catch (...) {}
        return false;
    };

    auto parse_rate = [&](const std::string& text) -> double {
        try {
            const auto slash = text.find('/');
            if (slash == std::string::npos) return std::stod(text);
            const double a = std::stod(text.substr(0, slash));
            const double b = std::stod(text.substr(slash + 1));
            if (b <= 0.0) return 0.0;
            return a / b;
        } catch (...) {
            return 0.0;
        }
    };

    auto fmt_duration = [&](double seconds) -> std::string {
        if (!(seconds > 0.0)) return "";
        long long s = static_cast<long long>(seconds + 0.5);
        const long long h = s / 3600;
        s %= 3600;
        const long long m = s / 60;
        const long long sec = s % 60;

        std::ostringstream oss;
        if (h > 0) {
            oss << h << ":";
            oss << std::setw(2) << std::setfill('0') << m << ":";
            oss << std::setw(2) << std::setfill('0') << sec;
        } else {
            oss << m << ":";
            oss << std::setw(2) << std::setfill('0') << sec;
        }
        return oss.str();
    };

    auto cache_root = [&]() -> std::filesystem::path {
        return std::filesystem::path(pqnas::data_root_dir()).parent_path()
             / "cache"
             / "reelstack_meta";
    };

    {
        auto uopt = users.get(fp_hex);
        if (!uopt.has_value() || uopt->storage_state != "allocated") {
            fail_json(403, "storage_unallocated", "Storage not allocated");
            return;
        }
    }

    const std::string rel_path = req.has_param("path") ? req.get_param_value("path") : "";
    if (rel_path.empty()) {
        fail_json(400, "bad_request", "missing path");
        return;
    }

    std::string rel_norm;
    {
        std::string nerr;
        if (!pqnas::normalize_user_rel_path_strict(rel_path, &rel_norm, &nerr)) {
            fail_json(400, "bad_request", "invalid path");
            return;
        }
    }

    pqnas::ResolvedExistingPath rp;
    std::string perr;
    if (!pqnas::resolve_existing_user_file_path(users, fp_hex, rel_path, &rp, &perr)) {
        fail_json(400, "bad_request", "invalid path");
        return;
    }

    const std::filesystem::path abs = rp.abs_path;

    {
        std::string serr;
        if (!ensure_no_symlink_in_existing_path_prefix(abs, &serr)) {
            fail_json(400, "bad_request", "symlinks not supported");
            return;
        }
    }

    std::error_code ec;
    auto st = std::filesystem::symlink_status(abs, ec);
    if (ec || !std::filesystem::exists(st)) {
        fail_json(404, "not_found", "file not found");
        return;
    }

    if (std::filesystem::is_symlink(st) || !std::filesystem::is_regular_file(st)) {
        fail_json(400, "bad_request", "not a regular file");
        return;
    }

    if (!supported_video_ext(abs.filename().string())) {
        fail_json(415, "unsupported_media_type", "unsupported video type");
        return;
    }

    const std::uint64_t src_bytes = pqnas::file_size_u64_safe(abs);
    const std::int64_t src_mtime = file_mtime_epoch(abs);

    const std::string key_material =
        fp_hex + "\n" +
        rel_norm + "\n" +
        std::to_string((unsigned long long)src_bytes) + "\n" +
        std::to_string((long long)src_mtime) + "\n" +
        "reelstack-meta-v1";

    const std::string meta_key = hex_u64(fnv1a64(key_material));
    const std::filesystem::path cache_dir = cache_root() / fp_hex;
    const std::filesystem::path cache_abs = cache_dir / (meta_key + ".json");

    auto serve_meta = [&](json body, bool cached) {
        body["ok"] = true;
        body["cached"] = cached;

        pqnas::AuditEvent ev;
        ev.event = "v4.reelstack_meta_ok";
        ev.outcome = "ok";
        ev.f["fingerprint"] = fp_hex;
        ev.f["path"] = pqnas::shorten(rel_norm, 200);
        ev.f["cached"] = cached ? "1" : "0";
        audit_append(ev);

        res.set_header("Cache-Control", "private, max-age=86400");
        reply_json(res, 200, body.dump());
    };

    {
        std::error_code cec;
        auto cst = std::filesystem::symlink_status(cache_abs, cec);
        if (!cec &&
            std::filesystem::exists(cst) &&
            !std::filesystem::is_symlink(cst) &&
            std::filesystem::is_regular_file(cst)) {
            std::string buf, rerr;
            if (read_small_file(cache_abs, &buf, &rerr)) {
                json cached = json::parse(buf, nullptr, false);
                if (!cached.is_discarded() && cached.is_object()) {
                    serve_meta(cached, true);
                    return;
                }
            }
        }
    }

    std::string probe_out;
    const std::vector<std::string> args = {
        "/usr/bin/ffprobe",
        "-v", "error",
        "-print_format", "json",
        "-show_format",
        "-show_streams",
        abs.string()
    };

    int rc = spawn_capture(args, &probe_out);
    if (rc != 0 || probe_out.empty()) {
        pqnas::AuditEvent ev;
        ev.event = "v4.reelstack_meta_fail";
        ev.outcome = "fail";
        ev.f["fingerprint"] = fp_hex;
        ev.f["path"] = pqnas::shorten(rel_norm, 200);
        ev.f["reason"] = "ffprobe_failed";
        ev.f["rc"] = std::to_string(rc);
        ev.f["detail"] = pqnas::shorten(probe_out, 180);
        audit_append(ev);

        fail_json(500, "server_error", "failed to read video metadata");
        return;
    }

    json probe = json::parse(probe_out, nullptr, false);
    if (probe.is_discarded() || !probe.is_object()) {
        fail_json(500, "server_error", "failed to parse video metadata");
        return;
    }

    std::string format_name;
    std::string format_long_name;
    double duration = 0.0;
    long long bit_rate = 0;

    if (probe.contains("format") && probe["format"].is_object()) {
        const json& fmt = probe["format"];

        if (fmt.contains("format_name")) format_name = scalar_to_string(fmt["format_name"]);
        if (fmt.contains("format_long_name")) format_long_name = scalar_to_string(fmt["format_long_name"]);
        if (fmt.contains("duration")) (void)json_to_double(fmt["duration"], &duration);
        if (fmt.contains("bit_rate")) (void)json_to_i64(fmt["bit_rate"], &bit_rate);
    }

    std::string video_codec;
    std::string video_codec_long;
    std::string audio_codec;
    std::string audio_codec_long;
    long long width = 0;
    long long height = 0;
    double fps = 0.0;

    if (probe.contains("streams") && probe["streams"].is_array()) {
        for (const auto& stream : probe["streams"]) {
            if (!stream.is_object()) continue;

            const std::string codec_type =
                stream.contains("codec_type") ? scalar_to_string(stream["codec_type"]) : "";

            if (codec_type == "video" && video_codec.empty()) {
                if (stream.contains("codec_name")) video_codec = scalar_to_string(stream["codec_name"]);
                if (stream.contains("codec_long_name")) video_codec_long = scalar_to_string(stream["codec_long_name"]);
                if (stream.contains("width")) (void)json_to_i64(stream["width"], &width);
                if (stream.contains("height")) (void)json_to_i64(stream["height"], &height);

                if (stream.contains("avg_frame_rate")) {
                    fps = parse_rate(scalar_to_string(stream["avg_frame_rate"]));
                }

                if (!(duration > 0.0) && stream.contains("duration")) {
                    (void)json_to_double(stream["duration"], &duration);
                }
            }

            if (codec_type == "audio" && audio_codec.empty()) {
                if (stream.contains("codec_name")) audio_codec = scalar_to_string(stream["codec_name"]);
                if (stream.contains("codec_long_name")) audio_codec_long = scalar_to_string(stream["codec_long_name"]);
            }
        }
    }

    json meta;
    meta["path"] = rel_norm;
    meta["name"] = abs.filename().string();
    meta["size_bytes"] = src_bytes;
    meta["mtime_epoch"] = src_mtime;

    meta["duration_seconds"] = (duration > 0.0) ? json(duration) : json(nullptr);
    meta["duration_text"] = fmt_duration(duration);

    meta["width"] = width > 0 ? json(width) : json(nullptr);
    meta["height"] = height > 0 ? json(height) : json(nullptr);
    meta["resolution"] = (width > 0 && height > 0)
        ? (std::to_string(width) + "x" + std::to_string(height))
        : "";

    meta["fps"] = (fps > 0.0) ? json(fps) : json(nullptr);
    meta["format"] = format_name;
    meta["format_long"] = format_long_name;
    meta["video_codec"] = video_codec;
    meta["video_codec_long"] = video_codec_long;
    meta["audio_codec"] = audio_codec;
    meta["audio_codec_long"] = audio_codec_long;
    meta["bit_rate"] = bit_rate > 0 ? json(bit_rate) : json(nullptr);

    std::string werr;
    if (!write_text_atomic(cache_abs, meta.dump(2), &werr)) {
        pqnas::AuditEvent ev;
        ev.event = "v4.reelstack_meta_fail";
        ev.outcome = "fail";
        ev.f["fingerprint"] = fp_hex;
        ev.f["path"] = pqnas::shorten(rel_norm, 200);
        ev.f["reason"] = "cache_write_failed";
        ev.f["detail"] = pqnas::shorten(werr, 180);
        audit_append(ev);
        // Do not fail the request; metadata is still usable.
    }

    serve_meta(meta, false);
});



srv.Get("/api/v4/reelstack/user_meta", [&](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;
    (void)role;

    auto fail = [&](int http, const std::string& error, const std::string& message) {
        reply_json(res, http, json{
            {"ok", false},
            {"error", error},
            {"message", message}
        }.dump());
    };

    const std::string rel_path = req.has_param("path") ? req.get_param_value("path") : "";
    if (rel_path.empty()) {
        fail(400, "bad_request", "missing path");
        return;
    }

    std::string rel_norm;
    {
        std::string nerr;
        if (!pqnas::normalize_user_rel_path_strict(rel_path, &rel_norm, &nerr)) {
            fail(400, "bad_request", "invalid path");
            return;
        }
    }

    pqnas::ResolvedExistingPath rp;
    std::string perr;
    if (!pqnas::resolve_existing_user_file_path(users, fp_hex, rel_norm, &rp, &perr)) {
        fail(404, "not_found", "video not found");
        return;
    }

    if (!reelstack_is_supported_video_name(rp.abs_path.filename().string())) {
        fail(415, "unsupported_media_type", "unsupported video type");
        return;
    }

    json meta;
    std::string merr;
    if (!reelstack_read_user_meta(fp_hex, rel_norm, &meta, &merr)) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "failed to read Reel Stack metadata"},
            {"detail", pqnas::shorten(merr, 180)}
        }.dump());
        return;
    }

    reply_json(res, 200, json{
        {"ok", true},
        {"path", rel_norm},
        {"meta", meta}
    }.dump());
});

srv.Post("/api/v4/reelstack/meta/set", [&](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;
    (void)role;

    if (!require_same_origin_for_cookie_mutation(req, res)) return;

    auto fail = [&](int http, const std::string& error, const std::string& message, const std::string& detail = "") {
        json body{
            {"ok", false},
            {"error", error},
            {"message", message}
        };
        if (!detail.empty()) body["detail"] = pqnas::shorten(detail, 180);
        reply_json(res, http, body.dump());
    };

    json body = json::parse(req.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        fail(400, "bad_request", "invalid json");
        return;
    }

    const std::string rel_path = body.value("path", "");
    if (rel_path.empty()) {
        fail(400, "bad_request", "missing path");
        return;
    }

    std::string rel_norm;
    {
        std::string nerr;
        if (!pqnas::normalize_user_rel_path_strict(rel_path, &rel_norm, &nerr)) {
            fail(400, "bad_request", "invalid path", nerr);
            return;
        }
    }

    pqnas::ResolvedExistingPath rp;
    std::string perr;
    if (!pqnas::resolve_existing_user_file_path(users, fp_hex, rel_norm, &rp, &perr)) {
        fail(404, "not_found", "video not found", perr);
        return;
    }

    if (!reelstack_is_supported_video_name(rp.abs_path.filename().string())) {
        fail(415, "unsupported_media_type", "unsupported video type");
        return;
    }

    const json& src =
        (body.contains("meta") && body["meta"].is_object())
            ? body["meta"]
            : body;

    auto get_string = [&](const char* key) -> std::string {
        if (src.contains(key) && src[key].is_string()) return src[key].get<std::string>();
        return "";
    };

    auto get_int = [&](const char* key, int fallback) -> int {
        if (!src.contains(key)) return fallback;
        try {
            if (src[key].is_number_integer()) return src[key].get<int>();
            if (src[key].is_number()) return static_cast<int>(src[key].get<double>());
            if (src[key].is_string()) return std::stoi(src[key].get<std::string>());
        } catch (...) {
        }
        return fallback;
    };

    auto get_bool = [&](const char* key, bool fallback) -> bool {
        if (!src.contains(key)) return fallback;
        if (src[key].is_boolean()) return src[key].get<bool>();
        if (src[key].is_number_integer()) return src[key].get<int>() != 0;
        if (src[key].is_string()) {
            std::string v = src[key].get<std::string>();
            for (char& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            v = reelstack_trim_copy(v);
            return v == "1" || v == "true" || v == "yes" || v == "y" || v == "watched" || v == "favorite";
        }
        return fallback;
    };

    std::string tags_text;
    if (src.contains("tags_text")) {
        tags_text = reelstack_tags_text_from_json(src["tags_text"]);
    } else if (src.contains("tags")) {
        tags_text = reelstack_tags_text_from_json(src["tags"]);
    }

    std::string notes = get_string("notes");
    if (notes.empty()) notes = get_string("description");

    json meta;
    std::string merr;
    if (!reelstack_write_user_meta(fp_hex,
                                   rel_norm,
                                   get_string("title"),
                                   tags_text,
                                   notes,
                                   get_int("rating", 0),
                                   get_bool("watched", false),
                                   get_bool("favorite", false),
                                   &meta,
                                   &merr)) {
        fail(500, "server_error", "failed to save Reel Stack metadata", merr);
        return;
    }

    {
        pqnas::AuditEvent ev;
        ev.event = "v4.reelstack_meta_set_ok";
        ev.outcome = "ok";
        ev.f["fingerprint"] = fp_hex;
        ev.f["path"] = pqnas::shorten(rel_norm, 200);
        ev.f["rating"] = std::to_string(meta.value("rating", 0));
        ev.f["watched"] = meta.value("watched", false) ? "1" : "0";
        ev.f["favorite"] = meta.value("favorite", false) ? "1" : "0";
        audit_append(ev);
    }

    reply_json(res, 200, json{
        {"ok", true},
        {"path", rel_norm},
        {"meta", meta}
    }.dump());
});


srv.Get("/api/v4/gallery/thumb", [&](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &users, &fp_hex, &role)) return;

    auto audit_ua = [&]() -> std::string {
        auto it = req.headers.find("User-Agent");
        return pqnas::shorten(it == req.headers.end() ? "" : it->second);
    };

    auto audit_fail = [&](const std::string& reason, int http, const std::string& detail = "") {
        pqnas::AuditEvent ev;
        ev.event = "v4.gallery_thumb_fail";
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

    auto audit_ok = [&](const std::string& rel_path_norm, bool cached, std::uint64_t bytes) {
        pqnas::AuditEvent ev;
        ev.event = "v4.gallery_thumb_ok";
        ev.outcome = "ok";
        ev.f["fingerprint"] = fp_hex;
        ev.f["path"] = pqnas::shorten(rel_path_norm, 200);
        ev.f["cached"] = cached ? "1" : "0";
        ev.f["bytes"] = std::to_string((unsigned long long)bytes);
        ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

        auto it_cf = req.headers.find("CF-Connecting-IP");
        if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

        auto it_xff = req.headers.find("X-Forwarded-For");
        if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

        ev.f["ua"] = audit_ua();
        audit_append(ev);
    };

    auto fnv1a64 = [&](const std::string& s) -> std::uint64_t {
        std::uint64_t h = 1469598103934665603ULL;
        for (unsigned char c : s) {
            h ^= static_cast<std::uint64_t>(c);
            h *= 1099511628211ULL;
        }
        return h;
    };

    auto hex_u64 = [&](std::uint64_t v) -> std::string {
        std::ostringstream oss;
        oss << std::hex << std::nouppercase << v;
        return oss.str();
    };

    auto lower_ascii = [&](std::string s) -> std::string {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };

    auto is_supported_gallery_thumb_ext = [&](const std::string& name) -> bool {
        const std::string n = lower_ascii(name);
        const auto dot = n.rfind('.');
        if (dot == std::string::npos) return false;
        const std::string ext = n.substr(dot + 1);

        return ext == "jpg"  || ext == "jpeg" ||
               ext == "png"  || ext == "webp" ||
               ext == "bmp"  || ext == "gif"  ||
               ext == "tif"  || ext == "tiff" ||
               ext == "heic" || ext == "heif";
    };

    auto file_mtime_epoch = [&](const std::filesystem::path& p) -> std::int64_t {
        std::error_code ec;
        auto ft = std::filesystem::last_write_time(p, ec);
        if (ec) return 0;

        using namespace std::chrono;
        auto sctp = time_point_cast<system_clock::duration>(
            ft - std::filesystem::file_time_type::clock::now() + system_clock::now()
        );
        return static_cast<std::int64_t>(duration_cast<seconds>(sctp.time_since_epoch()).count());
    };

    auto read_small_file = [&](const std::filesystem::path& p, std::string* out, std::string* err) -> bool {
        if (out) out->clear();
        if (err) err->clear();

        std::ifstream f(p, std::ios::binary);
        if (!f.good()) {
            if (err) *err = "open failed";
            return false;
        }

        std::ostringstream oss;
        oss << f.rdbuf();

        if (!f.good() && !f.eof()) {
            if (err) *err = "read failed";
            return false;
        }

        if (out) *out = oss.str();
        return true;
    };

    auto spawn_wait = [&](const std::vector<std::string>& args) -> int {
        if (args.empty()) return 127;

        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const auto& s : args) argv.push_back(const_cast<char*>(s.c_str()));
        argv.push_back(nullptr);

        pid_t pid = fork();
        if (pid < 0) return 127;

        if (pid == 0) {
            int devnull = open("/dev/null", O_WRONLY);
            if (devnull >= 0) {
                dup2(devnull, STDOUT_FILENO);
                dup2(devnull, STDERR_FILENO);
                close(devnull);
            }
            execvp(argv[0], argv.data());
            _exit(127);
        }

        int st = 0;
        while (waitpid(pid, &st, 0) < 0) {
            if (errno == EINTR) continue;
            return 127;
        }

        if (WIFEXITED(st)) return WEXITSTATUS(st);
        if (WIFSIGNALED(st)) return 128 + WTERMSIG(st);
        return 127;
    };

    auto generate_gallery_thumb = [&](const std::filesystem::path& src_abs,
                                      const std::filesystem::path& dst_abs,
                                      int size,
                                      std::string* err) -> bool {
        if (err) err->clear();

        const std::string size_arg = std::to_string(size) + "x" + std::to_string(size) + ">";
        std::vector<std::string> attempts;

        auto try_cmd = [&](const std::string& exe) -> bool {
            std::vector<std::string> args = {
                exe,
                src_abs.string(),
                "-auto-orient",
                "-thumbnail", size_arg,
                "-strip",
                "-quality", "82",
                dst_abs.string()
            };

            const int rc = spawn_wait(args);
            attempts.push_back(exe + "=" + std::to_string(rc));

            if (rc != 0) return false;

            std::error_code ec;
            auto st = std::filesystem::symlink_status(dst_abs, ec);
            return !ec && std::filesystem::exists(st) &&
                   !std::filesystem::is_symlink(st) &&
                   std::filesystem::is_regular_file(st);
        };

        if (try_cmd("/usr/bin/magick")) return true;
        if (try_cmd("/usr/local/bin/magick")) return true;
        if (try_cmd("magick")) return true;
        if (try_cmd("/usr/bin/convert")) return true;
        if (try_cmd("/usr/local/bin/convert")) return true;
        if (try_cmd("convert")) return true;

        if (err) {
            std::string joined;
            for (size_t i = 0; i < attempts.size(); ++i) {
                if (i) joined += ", ";
                joined += attempts[i];
            }
            *err = "thumbnail generator failed: " + joined;
        }
        return false;
    };

    auto cache_root = [&]() -> std::filesystem::path {
        return std::filesystem::path(pqnas::data_root_dir()).parent_path() / "cache" / "gallery_thumbs";
    };

    {
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

    int thumb_size = 320;
    if (req.has_param("size")) {
        try {
            thumb_size = std::stoi(req.get_param_value("size"));
        } catch (...) {
            thumb_size = 320;
        }
    }
    if (thumb_size < 64) thumb_size = 64;
    if (thumb_size > 1024) thumb_size = 1024;

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

    if (!is_supported_gallery_thumb_ext(abs.filename().string())) {
        audit_fail("unsupported_image_type", 415, abs.filename().string());
        reply_json(res, 415, json{
            {"ok", false},
            {"error", "unsupported_media_type"},
            {"message", "unsupported image type"}
        }.dump());
        return;
    }

    const std::uint64_t src_bytes = pqnas::file_size_u64_safe(abs);
    const std::int64_t src_mtime = file_mtime_epoch(abs);

    const std::string key_material =
        fp_hex + "\n" +
        rel_norm + "\n" +
        std::to_string(thumb_size) + "\n" +
        std::to_string((unsigned long long)src_bytes) + "\n" +
        std::to_string((long long)src_mtime);

    const std::string thumb_key = hex_u64(fnv1a64(key_material));

    const std::filesystem::path cache_dir =
        cache_root() / fp_hex / std::to_string(thumb_size);
    const std::filesystem::path cache_abs = cache_dir / (thumb_key + ".jpg");

    auto serve_thumb = [&](bool cached) {
        std::string buf;
        std::string rerr;
        if (!read_small_file(cache_abs, &buf, &rerr)) {
            audit_fail("thumb_read_failed", 500, rerr);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to read thumbnail"},
                {"detail", pqnas::shorten(rerr, 180)}
            }.dump());
            return;
        }

        const std::uint64_t out_bytes = static_cast<std::uint64_t>(buf.size());
        res.set_header("Cache-Control", "private, max-age=86400");
        res.set_content(std::move(buf), "image/jpeg");
        audit_ok(rel_norm, cached, out_bytes);
    };

    {
        std::error_code cec;
        auto cst = std::filesystem::symlink_status(cache_abs, cec);
        if (!cec &&
            std::filesystem::exists(cst) &&
            !std::filesystem::is_symlink(cst) &&
            std::filesystem::is_regular_file(cst)) {
            serve_thumb(true);
            return;
        }
    }

    ec.clear();
    std::filesystem::create_directories(cache_dir, ec);
    if (ec) {
        audit_fail("cache_mkdir_failed", 500, ec.message());
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "failed to create thumbnail cache directory"},
            {"detail", pqnas::shorten(ec.message(), 180)}
        }.dump());
        return;
    }

    const std::filesystem::path tmp_abs =
        cache_dir / (thumb_key + ".tmp." + random_b64url(8) + ".jpg");

    std::string terr;
    if (!generate_gallery_thumb(abs, tmp_abs, thumb_size, &terr)) {
        std::error_code rm_ec;
        std::filesystem::remove(tmp_abs, rm_ec);

        audit_fail("thumb_generate_failed", 500, terr);
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "failed to generate thumbnail"},
            {"detail", pqnas::shorten(terr, 180)}
        }.dump());
        return;
    }

    {
        std::error_code ren_ec;
        std::filesystem::rename(tmp_abs, cache_abs, ren_ec);
        if (ren_ec) {
            std::error_code cec;
            auto cst = std::filesystem::symlink_status(cache_abs, cec);

            if (!cec &&
                std::filesystem::exists(cst) &&
                !std::filesystem::is_symlink(cst) &&
                std::filesystem::is_regular_file(cst)) {
                std::error_code rm_ec;
                std::filesystem::remove(tmp_abs, rm_ec);
            } else {
                std::error_code rm_ec;
                std::filesystem::remove(tmp_abs, rm_ec);

                audit_fail("thumb_rename_failed", 500, ren_ec.message());
                reply_json(res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "failed to finalize thumbnail"},
                    {"detail", pqnas::shorten(ren_ec.message(), 180)}
                }.dump());
                return;
            }
        }
    }

    serve_thumb(false);
});

    auto trim_ascii_ws_copy = [](std::string s) -> std::string {
    auto is_ws = [](unsigned char c) -> bool {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };

    std::size_t a = 0;
    while (a < s.size() && is_ws((unsigned char)s[a])) ++a;

    std::size_t b = s.size();
    while (b > a && is_ws((unsigned char)s[b - 1])) --b;

    return s.substr(a, b - a);
};

auto split_gallery_keywords_text = [&](const std::string& s) -> std::vector<std::string> {
    std::vector<std::string> out;

    std::size_t start = 0;
    while (start <= s.size()) {
        const std::size_t comma = s.find(',', start);

        std::string part = (comma == std::string::npos)
            ? s.substr(start)
            : s.substr(start, comma - start);

        part = trim_ascii_ws_copy(part);
        if (!part.empty()) out.push_back(part);

        if (comma == std::string::npos) break;
        start = comma + 1;
    }

    return out;
};

auto join_gallery_keywords_json = [&](const json& jv, std::string* err) -> std::string {
    if (!jv.is_array()) {
        if (err) *err = "meta.keywords must be an array";
        return "";
    }

    std::string out;
    bool first = true;

    for (const auto& it : jv) {
        if (!it.is_string()) {
            if (err) *err = "meta.keywords entries must be strings";
            return "";
        }

        std::string s = trim_ascii_ws_copy(it.get<std::string>());
        if (s.empty()) continue;
        if (s.size() > 256) s.resize(256);

        if (!first) out += ", ";
        out += s;
        first = false;

        if (out.size() > 4000) {
            out.resize(4000);
            break;
        }
    }

    return out;
};

auto extract_gallery_description_json = [&](const json& jv, std::string* err) -> std::string {
    if (jv.is_string()) {
        std::string s = jv.get<std::string>();
        if (s.size() > 12000) s.resize(12000);
        return s;
    }

    if (!jv.is_object()) {
        if (err) *err = "meta.description must be an object";
        return "";
    }

    auto it = jv.find("x-default");
    if (it == jv.end() || it->is_null()) {
        return "";
    }

    if (!it->is_string()) {
        if (err) *err = "meta.description.x-default must be a string";
        return "";
    }

    std::string s = it->get<std::string>();
    if (s.size() > 12000) s.resize(12000);
    return s;
};

auto make_gallery_meta_api_json = [&](int rating,
                                      const std::string& tags_text,
                                      const std::string& notes_text,
                                      std::uint64_t size_bytes,
                                      std::int64_t mtime_epoch,
                                      std::int64_t created_epoch,
                                      std::int64_t updated_epoch) -> json {
    return json{
        {"title", json{{"x-default", ""}}},
        {"description", json{{"x-default", notes_text}}},
        {"keywords", split_gallery_keywords_text(tags_text)},
        {"imageRating", rating},

        // keep old fields too for compatibility
        {"rating", rating},
        {"tags_text", tags_text},
        {"notes_text", notes_text},

        {"size_bytes", size_bytes},
        {"mtime_epoch", mtime_epoch},
        {"created_epoch", created_epoch},
        {"updated_epoch", updated_epoch}
    };
};

    // POST /api/v4/gallery/meta/set
    // Accepts old shape:
    // {
    //   "path": "photos/cat.jpg",
    //   "rating": 5,
    //   "tags_text": "cat, sofa",
    //   "notes_text": "best shot"
    // }
    //
    // Also accepts new shape:
    // {
    //   "path": "photos/cat.jpg",
    //   "meta": {
    //     "imageRating": 5,
    //     "keywords": ["cat", "sofa"],
    //     "description": {
    //       "x-default": "best shot"
    //     }
    //   }
    // }
srv.Post("/api/v4/gallery/meta/set", [&](const httplib::Request& req, httplib::Response& res) {
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
                          const std::string& rel_path = "") {
        pqnas::AuditEvent ev;
        ev.event = "v4.gallery_meta_set_fail";
        ev.outcome = "fail";
        ev.f["fingerprint"] = fp_hex;
        ev.f["reason"] = reason;
        ev.f["http"] = std::to_string(http);
        if (!rel_path.empty()) ev.f["path"] = pqnas::shorten(rel_path, 200);
        if (!detail.empty()) ev.f["detail"] = pqnas::shorten(detail, 180);
        add_ip_headers(ev);
        audit_append(ev);
    };

    auto audit_ok = [&](const std::string& rel_path,
                        bool rating_changed,
                        bool tags_changed,
                        bool notes_changed) {
        pqnas::AuditEvent ev;
        ev.event = "v4.gallery_meta_set_ok";
        ev.outcome = "ok";
        ev.f["fingerprint"] = fp_hex;
        ev.f["path"] = pqnas::shorten(rel_path, 200);
        ev.f["rating_changed"] = rating_changed ? "1" : "0";
        ev.f["tags_changed"] = tags_changed ? "1" : "0";
        ev.f["notes_changed"] = notes_changed ? "1" : "0";
        add_ip_headers(ev);
        audit_append(ev);
    };

    // allocated storage required
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

    json in = json::parse(req.body, nullptr, false);
    if (in.is_discarded() || !in.is_object()) {
        audit_fail("bad_json", 400);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid json body"}
        }.dump());
        return;
    }

    const std::string path_in = in.value("path", "");
    if (path_in.empty()) {
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
    if (!pqnas::normalize_user_rel_path_strict(path_in, &rel_norm, &nerr)) {
        audit_fail("invalid_path", 400, nerr, path_in);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid path"}
        }.dump());
        return;
    }

    const bool has_meta = in.contains("meta");
json meta_in = json::object();

if (has_meta) {
    if (!in["meta"].is_object()) {
        audit_fail("bad_meta", 400, "", rel_norm);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "meta must be an object"}
        }.dump());
        return;
    }
    meta_in = in["meta"];
}

const bool has_rating_old = in.contains("rating");
const bool has_tags_old = in.contains("tags_text");
const bool has_notes_old = in.contains("notes_text");

const bool has_rating_new = has_meta && meta_in.contains("imageRating");
const bool has_tags_new = has_meta && meta_in.contains("keywords");
const bool has_notes_new = has_meta && meta_in.contains("description");

const bool has_rating = has_rating_old || has_rating_new;
const bool has_tags = has_tags_old || has_tags_new;
const bool has_notes = has_notes_old || has_notes_new;

if (!has_rating && !has_tags && !has_notes) {
    audit_fail("no_fields", 400, "", rel_norm);
    reply_json(res, 400, json{
        {"ok", false},
        {"error", "bad_request"},
        {"message", "nothing to update"}
    }.dump());
    return;
}

std::optional<int> rating;
std::optional<std::string> tags_text;
std::optional<std::string> notes_text;

if (has_rating) {
    if (has_rating_new) {
        if (!meta_in["imageRating"].is_number_integer()) {
            audit_fail("bad_imageRating", 400, "", rel_norm);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "meta.imageRating must be an integer"}
            }.dump());
            return;
        }

        int r = meta_in["imageRating"].get<int>();
        if (r < 0) r = 0;
        if (r > 5) r = 5;
        rating = r;
    } else {
        if (!in["rating"].is_number_integer()) {
            audit_fail("bad_rating", 400, "", rel_norm);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "rating must be an integer"}
            }.dump());
            return;
        }

        int r = in["rating"].get<int>();
        if (r < 0) r = 0;
        if (r > 5) r = 5;
        rating = r;
    }
}

if (has_tags) {
    if (has_tags_new) {
        std::string perr;
        auto s = join_gallery_keywords_json(meta_in["keywords"], &perr);
        if (!perr.empty()) {
            audit_fail("bad_keywords", 400, perr, rel_norm);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", perr}
            }.dump());
            return;
        }
        tags_text = s;
    } else {
        if (!in["tags_text"].is_string()) {
            audit_fail("bad_tags_text", 400, "", rel_norm);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "tags_text must be a string"}
            }.dump());
            return;
        }

        auto s = in["tags_text"].get<std::string>();
        if (s.size() > 4000) s.resize(4000);
        tags_text = s;
    }
}

if (has_notes) {
    if (has_notes_new) {
        std::string perr;
        auto s = extract_gallery_description_json(meta_in["description"], &perr);
        if (!perr.empty()) {
            audit_fail("bad_description", 400, perr, rel_norm);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", perr}
            }.dump());
            return;
        }
        notes_text = s;
    } else {
        if (!in["notes_text"].is_string()) {
            audit_fail("bad_notes_text", 400, "", rel_norm);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "notes_text must be a string"}
            }.dump());
            return;
        }

        auto s = in["notes_text"].get<std::string>();
        if (s.size() > 12000) s.resize(12000);
        notes_text = s;
    }
}

    // verify target exists and is a file
    pqnas::ResolvedLogicalItem item;
    std::string rerr;
    if (!pqnas::resolve_existing_user_item(users, fp_hex, rel_norm, &item, &rerr) || !item.exists) {
        audit_fail("not_found", 404, rerr, rel_norm);
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "path not found"}
        }.dump());
        return;
    }

    if (!item.is_file) {
        audit_fail("not_file", 400, "", rel_norm);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "gallery metadata can only be set for files"}
        }.dump());
        return;
    }

    auto lower_ascii_local = [](std::string s) -> std::string {
        for (char& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    };

    auto file_ext_lower = [&](const std::string& name) -> std::string {
        std::string n = name;
        const auto slash = n.find_last_of("/\\");
        if (slash != std::string::npos) n = n.substr(slash + 1);
        auto lower = lower_ascii_local(n);
        const auto dot = lower.rfind('.');
        if (dot == std::string::npos || dot == 0 || dot + 1 >= lower.size()) return "";
        return lower.substr(dot + 1);
    };

    auto is_gallery_image_name = [&](const std::string& name) -> bool {
        const std::string ext = file_ext_lower(name);
        return ext == "png"  ||
               ext == "jpg"  ||
               ext == "jpeg" ||
               ext == "gif"  ||
               ext == "webp" ||
               ext == "svg"  ||
               ext == "bmp"  ||
               ext == "ico"  ||
               ext == "tif"  ||
               ext == "tiff" ||
               ext == "heic" ||
               ext == "heif" ||
               ext == "cr2"  ||
               ext == "cr3"  ||
               ext == "nef"  ||
               ext == "arw"  ||
               ext == "raf"  ||
               ext == "dng"  ||
               ext == "rw2"  ||
               ext == "orf";
    };

    if (!is_gallery_image_name(std::filesystem::path(rel_norm).filename().string())) {
        audit_fail("not_supported_image", 400, "", rel_norm);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "file is not a supported gallery image"}
        }.dump());
        return;
    }

    auto* gidx = pqnas::get_gallery_meta_index();
    if (!gidx) {
        audit_fail("gallery_meta_index_missing", 500, "", rel_norm);
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "gallery metadata index missing"}
        }.dump());
        return;
    }

    std::uint64_t size_bytes = 0;
    std::int64_t mtime_epoch = 0;

    {
        auto* idx = pqnas::get_file_location_index();
        if (idx) {
            std::string lerr;
            auto rec = idx->get(fp_hex, rel_norm, &lerr);
            if (!lerr.empty()) {
                audit_fail("file_location_lookup_failed", 500, lerr, rel_norm);
                reply_json(res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "file metadata lookup failed"},
                    {"detail", pqnas::shorten(lerr, 180)}
                }.dump());
                return;
            }
            if (rec.has_value()) {
                size_bytes = rec->size_bytes;
                mtime_epoch = rec->mtime_epoch;
            }
        }
    }

    // fallback to physical file stat if needed
    if ((size_bytes == 0 || mtime_epoch == 0) && item.has_physical_anchor) {
        std::error_code ec;
        auto st = std::filesystem::symlink_status(item.abs_path, ec);
        if (!ec && std::filesystem::exists(st) && std::filesystem::is_regular_file(st)) {
            if (size_bytes == 0) {
                size_bytes = pqnas::file_size_u64_safe(item.abs_path);
            }
            if (mtime_epoch == 0) {
                auto ft = std::filesystem::last_write_time(item.abs_path, ec);
                if (!ec) {
                    using namespace std::chrono;
                    auto sctp = time_point_cast<system_clock::duration>(
                        ft - std::filesystem::file_time_type::clock::now() + system_clock::now()
                    );
                    mtime_epoch = (std::int64_t)duration_cast<seconds>(sctp.time_since_epoch()).count();
                }
            }
        }
    }

    const std::int64_t now_ts = now_epoch_sec();

    std::string gerr;
    if (!gidx->patch("user", fp_hex, rel_norm, rating, tags_text, notes_text, now_ts, &gerr)) {
        audit_fail("gallery_meta_patch_failed", 500, gerr, rel_norm);
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "gallery metadata update failed"},
            {"detail", pqnas::shorten(gerr, 180)}
        }.dump());
        return;
    }

    // best-effort touch so cached file facts stay current on existing/new row
    {
        std::string terr;
        (void)gidx->touch_file_facts("user", fp_hex, rel_norm, size_bytes, mtime_epoch, now_ts, &terr);
    }

    std::string get_err;
    auto out_rec = gidx->get("user", fp_hex, rel_norm, &get_err);
    if (!get_err.empty()) {
        audit_fail("gallery_meta_readback_failed", 500, get_err, rel_norm);
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "gallery metadata readback failed"},
            {"detail", pqnas::shorten(get_err, 180)}
        }.dump());
        return;
    }

    audit_ok(rel_norm, has_rating, has_tags, has_notes);

    int final_rating = rating.has_value() ? *rating : 0;
    std::string final_tags_text = tags_text.has_value() ? *tags_text : "";
    std::string final_notes_text = notes_text.has_value() ? *notes_text : "";
    std::uint64_t final_size_bytes = size_bytes;
    std::int64_t final_mtime_epoch = mtime_epoch;
    std::int64_t final_created_epoch = 0;
    std::int64_t final_updated_epoch = now_ts;

    if (out_rec.has_value()) {
        final_rating = out_rec->rating;
        final_tags_text = out_rec->tags_text;
        final_notes_text = out_rec->notes_text;
        final_size_bytes = out_rec->size_bytes;
        final_mtime_epoch = out_rec->mtime_epoch;
        final_created_epoch = out_rec->created_epoch;
        final_updated_epoch = out_rec->updated_epoch;
    }

    json out{
        {"ok", true},
        {"path", rel_norm},
        {"meta", make_gallery_meta_api_json(
            final_rating,
            final_tags_text,
            final_notes_text,
            final_size_bytes,
            final_mtime_epoch,
            final_created_epoch,
            final_updated_epoch
        )},
        {"file", json{
            {"size_bytes", final_size_bytes},
            {"mtime_epoch", final_mtime_epoch}
        }}
    };

    reply_json(res, 200, out.dump());

    reply_json(res, 200, out.dump());
});

    // POST /api/v4/gallery/meta/get
    // Body:
    // {
    //   "path": "photos/cat.jpg"
    // }
    //
    // Response includes both old compatibility fields:
    //   rating, tags_text, notes_text
    //
    // and new standardized fields:
    //   meta.imageRating
    //   meta.keywords[]
    //   meta.description["x-default"]
srv.Post("/api/v4/gallery/meta/get", [&](const httplib::Request& req, httplib::Response& res) {
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
                          const std::string& rel_path = "") {
        pqnas::AuditEvent ev;
        ev.event = "v4.gallery_meta_get_fail";
        ev.outcome = "fail";
        ev.f["fingerprint"] = fp_hex;
        ev.f["reason"] = reason;
        ev.f["http"] = std::to_string(http);
        if (!rel_path.empty()) ev.f["path"] = pqnas::shorten(rel_path, 200);
        if (!detail.empty()) ev.f["detail"] = pqnas::shorten(detail, 180);
        add_ip_headers(ev);
        audit_append(ev);
    };

    auto audit_ok = [&](const std::string& rel_path, bool found_meta) {
        pqnas::AuditEvent ev;
        ev.event = "v4.gallery_meta_get_ok";
        ev.outcome = "ok";
        ev.f["fingerprint"] = fp_hex;
        ev.f["path"] = pqnas::shorten(rel_path, 200);
        ev.f["found_meta"] = found_meta ? "1" : "0";
        add_ip_headers(ev);
        audit_append(ev);
    };

    auto lower_ascii_local = [](std::string s) -> std::string {
        for (char& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    };

    auto file_ext_lower = [&](const std::string& name) -> std::string {
        std::string n = name;
        const auto slash = n.find_last_of("/\\");
        if (slash != std::string::npos) n = n.substr(slash + 1);
        auto lower = lower_ascii_local(n);
        const auto dot = lower.rfind('.');
        if (dot == std::string::npos || dot == 0 || dot + 1 >= lower.size()) return "";
        return lower.substr(dot + 1);
    };

    auto is_gallery_image_name = [&](const std::string& name) -> bool {
        const std::string ext = file_ext_lower(name);
        return ext == "png"  ||
               ext == "jpg"  ||
               ext == "jpeg" ||
               ext == "gif"  ||
               ext == "webp" ||
               ext == "svg"  ||
               ext == "bmp"  ||
               ext == "ico"  ||
               ext == "tif"  ||
               ext == "tiff" ||
               ext == "heic" ||
               ext == "heif" ||
               ext == "cr2"  ||
               ext == "cr3"  ||
               ext == "nef"  ||
               ext == "arw"  ||
               ext == "raf"  ||
               ext == "dng"  ||
               ext == "rw2"  ||
               ext == "orf";
    };

    // allocated storage required
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

    json in = json::parse(req.body, nullptr, false);
    if (in.is_discarded() || !in.is_object()) {
        audit_fail("bad_json", 400);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid json body"}
        }.dump());
        return;
    }

    const std::string path_in = in.value("path", "");
    if (path_in.empty()) {
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
    if (!pqnas::normalize_user_rel_path_strict(path_in, &rel_norm, &nerr)) {
        audit_fail("invalid_path", 400, nerr, path_in);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid path"}
        }.dump());
        return;
    }

    pqnas::ResolvedLogicalItem item;
    std::string rerr;
    if (!pqnas::resolve_existing_user_item(users, fp_hex, rel_norm, &item, &rerr) || !item.exists) {
        audit_fail("not_found", 404, rerr, rel_norm);
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "path not found"}
        }.dump());
        return;
    }

    if (!item.is_file) {
        audit_fail("not_file", 400, "", rel_norm);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "gallery metadata is only available for files"}
        }.dump());
        return;
    }

    if (!is_gallery_image_name(std::filesystem::path(rel_norm).filename().string())) {
        audit_fail("not_supported_image", 400, "", rel_norm);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "file is not a supported gallery image"}
        }.dump());
        return;
    }

    std::uint64_t size_bytes = 0;
    std::int64_t mtime_epoch = 0;

    {
        auto* idx = pqnas::get_file_location_index();
        if (idx) {
            std::string lerr;
            auto rec = idx->get(fp_hex, rel_norm, &lerr);
            if (!lerr.empty()) {
                audit_fail("file_location_lookup_failed", 500, lerr, rel_norm);
                reply_json(res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "file metadata lookup failed"},
                    {"detail", pqnas::shorten(lerr, 180)}
                }.dump());
                return;
            }
            if (rec.has_value()) {
                size_bytes = rec->size_bytes;
                mtime_epoch = rec->mtime_epoch;
            }
        }
    }

    if ((size_bytes == 0 || mtime_epoch == 0) && item.has_physical_anchor) {
        std::error_code ec;
        auto st = std::filesystem::symlink_status(item.abs_path, ec);
        if (!ec && std::filesystem::exists(st) && std::filesystem::is_regular_file(st)) {
            if (size_bytes == 0) {
                size_bytes = pqnas::file_size_u64_safe(item.abs_path);
            }
            if (mtime_epoch == 0) {
                auto ft = std::filesystem::last_write_time(item.abs_path, ec);
                if (!ec) {
                    using namespace std::chrono;
                    auto sctp = time_point_cast<system_clock::duration>(
                        ft - std::filesystem::file_time_type::clock::now() + system_clock::now()
                    );
                    mtime_epoch = (std::int64_t)duration_cast<seconds>(sctp.time_since_epoch()).count();
                }
            }
        }
    }

    auto* gidx = pqnas::get_gallery_meta_index();
    if (!gidx) {
        audit_fail("gallery_meta_index_missing", 500, "", rel_norm);
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "gallery metadata index missing"}
        }.dump());
        return;
    }

    std::string gerr;
    auto meta = gidx->get("user", fp_hex, rel_norm, &gerr);
    if (!gerr.empty()) {
        audit_fail("gallery_meta_lookup_failed", 500, gerr, rel_norm);
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "gallery metadata lookup failed"},
            {"detail", pqnas::shorten(gerr, 180)}
        }.dump());
        return;
    }

    const int final_rating = meta.has_value() ? meta->rating : 0;
    const std::string final_tags_text = meta.has_value() ? meta->tags_text : "";
    const std::string final_notes_text = meta.has_value() ? meta->notes_text : "";
    const std::uint64_t final_size_bytes =
        (meta.has_value() && meta->size_bytes > 0) ? meta->size_bytes : size_bytes;
    const std::int64_t final_mtime_epoch =
        (meta.has_value() && meta->mtime_epoch > 0) ? meta->mtime_epoch : mtime_epoch;
    const std::int64_t final_created_epoch = meta.has_value() ? meta->created_epoch : 0;
    const std::int64_t final_updated_epoch = meta.has_value() ? meta->updated_epoch : 0;

    json out{
        {"ok", true},
        {"path", rel_norm},
        {"meta", make_gallery_meta_api_json(
            final_rating,
            final_tags_text,
            final_notes_text,
            final_size_bytes,
            final_mtime_epoch,
            final_created_epoch,
            final_updated_epoch
        )},
        {"file", json{
            {"size_bytes", final_size_bytes},
            {"mtime_epoch", final_mtime_epoch}
        }}
    };

    audit_ok(rel_norm, meta.has_value());
    reply_json(res, 200, out.dump());
});
srv.Get("/api/v4/photogallery/stats", [&](const httplib::Request& req, httplib::Response& res) {
    std::string actor_fp, actor_role;
    if (!require_user_cookie_users_actor(req, res, COOKIE_KEY, &users, &actor_fp, &actor_role)) {
        return;
    }
    (void)actor_role;

    if (!users.load(users_path)) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "users_reload_failed"},
            {"message", "failed to reload users"}
        }.dump());
        return;
    }

    const auto uopt = users.get(actor_fp);
    if (!uopt.has_value()) {
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "user_not_found"},
            {"message", "user not found"}
        }.dump());
        return;
    }

    const auto& user = *uopt;
    if (user.storage_state != "allocated" || user.root_rel.empty()) {
        reply_json(res, 200, json{
            {"ok", true},
            {"stats", {
                {"total_photos", 0},
                {"total_bytes", 0},
                {"total_megabytes", 0.0},
                {"photos_with_exif", 0},
                {"unique_cameras", 0},
                {"unique_lenses", 0},
                {"first_taken_at", ""},
                {"last_taken_at", ""},
                {"top_cameras", json::array()},
                {"top_lenses", json::array()},
                {"iso_buckets", json::array()},
                {"aperture_buckets", json::array()},
                {"shutter_buckets", json::array()},
                {"focal_length_buckets", json::array()},
                {"by_month", json::array()}
            }}
        }.dump());
        return;
    }

    bool path_ok = true;
    const std::string rel_sub =
        req.has_param("path")
            ? normalize_rel_subpath_for_stats(req.get_param_value("path"), &path_ok)
            : std::string();

    if (!path_ok) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid path"}
        }.dump());
        return;
    }

    const std::filesystem::path user_root =
        std::filesystem::path(pqnas::data_root_dir()) / user.root_rel;

    const std::filesystem::path scan_root =
        rel_sub.empty() ? user_root : (user_root / rel_sub);

    std::error_code ec;
    if (!std::filesystem::exists(scan_root, ec) || !std::filesystem::is_directory(scan_root, ec)) {
        reply_json(res, 200, json{
            {"ok", true},
            {"stats", {
                {"total_photos", 0},
                {"total_bytes", 0},
                {"total_megabytes", 0.0},
                {"photos_with_exif", 0},
                {"unique_cameras", 0},
                {"unique_lenses", 0},
                {"first_taken_at", ""},
                {"last_taken_at", ""},
                {"top_cameras", json::array()},
                {"top_lenses", json::array()},
                {"iso_buckets", json::array()},
                {"aperture_buckets", json::array()},
                {"shutter_buckets", json::array()},
                {"focal_length_buckets", json::array()},
                {"by_month", json::array()}
            }}
        }.dump());
        return;
    }

    sqlite3* db = nullptr;
    const std::filesystem::path db_path =
        std::filesystem::path(pqnas::data_root_dir()) / "photogallery_stats.sqlite3";

    if (sqlite3_open(db_path.string().c_str(), &db) != SQLITE_OK) {
        const std::string err = db ? sqlite3_errmsg(db) : "sqlite open failed";
        if (db) sqlite3_close(db);
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "sqlite_open_failed"},
            {"message", err}
        }.dump());
        return;
    }

    std::string schema_err;
    if (!ensure_photo_stats_schema(db, &schema_err)) {
        sqlite3_close(db);
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "sqlite_schema_failed"},
            {"message", schema_err}
        }.dump());
        return;
    }

    std::uint64_t total_photos = 0;
    std::uint64_t total_bytes = 0;
    std::uint64_t photos_with_exif = 0;

    std::set<std::string> unique_cameras;
    std::set<std::string> unique_lenses;

    std::map<std::string, int> top_cameras;
    std::map<std::string, int> top_lenses;
    std::map<std::string, int> iso_buckets;
    std::map<std::string, int> aperture_buckets;
    std::map<std::string, int> shutter_buckets;
    std::map<std::string, int> focal_buckets;
    std::map<std::string, int> by_month;

    std::string first_taken_at;
    std::string last_taken_at;

    const std::int64_t indexed_now = static_cast<std::int64_t>(std::time(nullptr));

    sqlite3_exec(db, "BEGIN IMMEDIATE TRANSACTION", nullptr, nullptr, nullptr);

    for (std::filesystem::recursive_directory_iterator it(scan_root, ec), end;
         it != end && !ec;
         it.increment(ec)) {
        if (ec) break;

        std::error_code ec2;
        if (!it->is_regular_file(ec2) || ec2) continue;

        const std::filesystem::path abs = it->path();
        if (!is_supported_photo_ext(abs)) continue;

        const std::string rel =
            std::filesystem::relative(abs, user_root, ec2).generic_string();
        if (ec2 || rel.empty()) continue;

        const std::uint64_t size_bytes = file_size_u64_safe_local(abs);
        const std::int64_t mtime_epoch = file_mtime_epoch_safe_local(abs);

        ++total_photos;
        total_bytes += size_bytes;

        PhotoStatsRow row;
        const bool have_cached = load_cached_photo_stats_row(db, rel, &row);
        const bool cache_match =
            have_cached &&
            row.size_bytes == size_bytes &&
            row.mtime_epoch == mtime_epoch;

        if (!cache_match) {
            row = exif_row_from_file(rel, abs, size_bytes, mtime_epoch);
            std::string upsert_err;
            (void)upsert_photo_stats_row(db, row, indexed_now, &upsert_err);
        }

        if (row.exif_ok) ++photos_with_exif;

        const std::string camera = camera_label_from_row(row);
        const std::string lens = lens_label_from_row(row);

        if (!camera.empty()) unique_cameras.insert(camera);
        if (!lens.empty()) unique_lenses.insert(lens);

        bump_bucket(top_cameras, camera);
        bump_bucket(top_lenses, lens);
        bump_bucket(iso_buckets, iso_bucket_from_row(row));
        bump_bucket(aperture_buckets, aperture_bucket_from_row(row));
        bump_bucket(shutter_buckets, shutter_bucket_from_row(row));
        bump_bucket(focal_buckets, focal_bucket_from_row(row));
        bump_bucket(by_month, row.taken_month);

        if (!row.taken_at.empty()) {
            if (first_taken_at.empty() || row.taken_at < first_taken_at) first_taken_at = row.taken_at;
            if (last_taken_at.empty() || row.taken_at > last_taken_at) last_taken_at = row.taken_at;
        }
    }

    sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);
    sqlite3_close(db);

    json stats;
    stats["total_photos"] = total_photos;
    stats["total_bytes"] = total_bytes;
    stats["total_megabytes"] =
        static_cast<double>(total_bytes) / (1024.0 * 1024.0);

    stats["photos_with_exif"] = photos_with_exif;
    stats["unique_cameras"] = static_cast<unsigned long long>(unique_cameras.size());
    stats["unique_lenses"] = static_cast<unsigned long long>(unique_lenses.size());
    stats["first_taken_at"] = first_taken_at;
    stats["last_taken_at"] = last_taken_at;

    stats["top_cameras"] = top_bucket_array(top_cameras, 10);
    stats["top_lenses"] = top_bucket_array(top_lenses, 10);
    stats["iso_buckets"] = top_bucket_array(iso_buckets, 12);
    stats["aperture_buckets"] = top_bucket_array(aperture_buckets, 12);
    stats["shutter_buckets"] = top_bucket_array(shutter_buckets, 12);
    stats["focal_length_buckets"] = top_bucket_array(focal_buckets, 12);

    {
        std::vector<std::pair<std::string, int>> months(by_month.begin(), by_month.end());
        std::sort(months.begin(), months.end(), [](const auto& a, const auto& b) {
            return a.first < b.first;
        });

        stats["by_month"] = json::array();
        for (const auto& kv : months) {
            stats["by_month"].push_back({
                {"label", kv.first},
                {"count", kv.second}
            });
        }
    }

    res.set_header("Cache-Control", "no-store");
    reply_json(res, 200, json{
        {"ok", true},
        {"path", rel_sub},
        {"stats", stats}
    }.dump());
});
