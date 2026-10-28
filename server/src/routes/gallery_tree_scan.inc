// Fast recursive Photo Gallery helpers/routes.
// These are intentionally lighter than /api/v4/photogallery/stats:
// - reads cached EXIF/GPS rows unless explicit refresh is requested
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

auto gallery_fast_file_ext_lower = [gallery_fast_lower_ascii](const std::string& name) -> std::string {
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

auto gallery_fast_is_image_name = [gallery_fast_file_ext_lower](const std::string& name) -> bool {
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

// The optional emitter receives bounded result batches as the scan progresses.
// A failed emit immediately stops traversal (for example when the client aborts).
// Own the helper graph: this callable is stored by routes registered inside a
// temporary role-wiring scope. Only the main-lifetime users registry is borrowed.
auto collect_fast_gallery_tree =
    [&users, gallery_fast_is_reserved_name, gallery_fast_is_image_name,
     gallery_fast_parent_rel, gallery_fast_split_keywords_text_json](const std::string& fp_hex,
        const std::filesystem::path& user_dir,
        const std::string& rel_root,
        bool include_items,
        bool refresh_stale_exif,
        std::size_t max_returned_items,
        json* items_out,
        std::uint64_t* dirs_out,
        std::uint64_t* files_out,
        std::uint64_t* bytes_out,
        bool* truncated_out,
        std::string* err_code,
        std::string* err_msg,
        const std::function<bool(const json&)>& emit = {}) -> bool {
    if (items_out) *items_out = json::array();
    if (dirs_out) *dirs_out = 0;
    if (files_out) *files_out = 0;
    if (bytes_out) *bytes_out = 0;
    if (truncated_out) *truncated_out = false;
    if (err_code) err_code->clear();
    if (err_msg) err_msg->clear();

    auto fail = [&](const std::string& code, const std::string& message) {
        if (err_code) *err_code = code;
        if (err_msg) *err_msg = message;
        return false;
    };
    auto* idx = pqnas::get_file_location_index();
    if (!idx) return fail("server_error", "metadata index missing");
    auto* gidx = include_items ? pqnas::get_gallery_meta_index() : nullptr;
    if (include_items && !gidx) return fail("server_error", "gallery metadata index missing");

    std::filesystem::path abs_root = user_dir;
    if (!rel_root.empty()) {
        std::string perr;
        if (!pqnas::resolve_user_path_strict(user_dir, rel_root, &abs_root, &perr)) abs_root.clear();
    }
    bool physical_root_ok = false;
    if (!abs_root.empty()) {
        std::error_code ec;
        const auto st = std::filesystem::symlink_status(abs_root, ec);
        if (!ec && std::filesystem::is_symlink(st)) return fail("bad_request", "symlinks not supported");
        physical_root_ok = !ec && std::filesystem::is_directory(st);
    }

    // One scoped metadata snapshot replaces a full-user query for every folder.
    // Validate each record only when visiting it, so early batches do not wait
    // for physical existence checks on the entire library.
    std::string lerr;
    const auto records = idx->list_subtree_records(fp_hex, rel_root, &lerr);
    if (!lerr.empty()) return fail("server_error", "metadata list failed: " + pqnas::shorten(lerr, 180));

    struct GalleryPhotoDbGuard {
        sqlite3* db = nullptr;
        ~GalleryPhotoDbGuard() { if (db) sqlite3_close(db); }
    } photo;
    if (include_items) {
        const auto path = std::filesystem::path(pqnas::data_root_dir()) / "photogallery_stats.sqlite3";
        if (sqlite3_open(path.string().c_str(), &photo.db) != SQLITE_OK) {
            if (photo.db) sqlite3_close(photo.db);
            photo.db = nullptr;
        } else {
            std::string schema_err;
            if (!ensure_photo_stats_schema(photo.db, &schema_err)) {
                sqlite3_close(photo.db);
                photo.db = nullptr;
            }
        }
    }

    std::set<std::string> seen_dirs;
    std::set<std::string> seen_files;
    std::uint64_t dirs = 0, files = 0, bytes = 0;
    json batch = json::array();
    auto last_emit = std::chrono::steady_clock::now();
    auto stats = [&]() { return json{{"dirs", dirs}, {"files", files}, {"file_bytes", bytes}}; };
    auto publish_counts = [&]() {
        if (dirs_out) *dirs_out = dirs;
        if (files_out) *files_out = files;
        if (bytes_out) *bytes_out = bytes;
    };
    auto flush = [&](bool progress) -> bool {
        publish_counts();
        if (!emit) return true;
        if (!batch.empty()) {
            if (!emit(json{{"type", "batch"}, {"items", batch}, {"stats", stats()}}))
                return fail("cancelled", "gallery search cancelled");
            batch = json::array();
        } else if (progress && !emit(json{{"type", "progress"}, {"stats", stats()}})) {
            return fail("cancelled", "gallery search cancelled");
        }
        last_emit = std::chrono::steady_clock::now();
        return true;
    };
    auto heartbeat = [&]() -> bool {
        return !emit || std::chrono::steady_clock::now() - last_emit < std::chrono::milliseconds(250) || flush(true);
    };
    auto reserved_path = [&](const std::string& rel) {
        for (const auto& part : std::filesystem::path(rel)) {
            if (gallery_fast_is_reserved_name(part.string())) return true;
        }
        return false;
    };
    auto add_dir = [&](const std::string& rel) {
        if (rel != rel_root && !seen_files.count(rel) && seen_dirs.insert(rel).second) ++dirs;
    };
    auto add_parents = [&](const std::string& rel) {
        std::string parent = gallery_fast_parent_rel(rel);
        while (!parent.empty() && parent != rel_root) {
            add_dir(parent);
            parent = gallery_fast_parent_rel(parent);
        }
    };
    auto visit_image = [&](FastGalleryItem item) -> bool {
        if (!seen_files.insert(item.rel_path).second) return true;
        if (seen_dirs.erase(item.rel_path)) --dirs;
        if (include_items) {
            std::string gerr;
            const auto meta = gidx->get("user", fp_hex, item.rel_path, &gerr);
            if (!gerr.empty()) return fail("server_error", "gallery metadata lookup failed: " + pqnas::shorten(gerr, 180));
            if (meta.has_value()) {
                item.rating = meta->rating;
                item.tags_text = meta->tags_text;
                item.notes_text = meta->notes_text;
                if (meta->size_bytes > 0) item.size_bytes = meta->size_bytes;
                if (meta->mtime_epoch > 0) item.mtime_unix = meta->mtime_epoch;
            }
            if (photo.db) {
                PhotoStatsRow row;
                bool have_row = load_cached_photo_stats_row(photo.db, item.rel_path, &row);
                const bool cache_match = have_row && row.size_bytes == item.size_bytes && row.mtime_epoch == item.mtime_unix;
                if (!cache_match && refresh_stale_exif) {
                    pqnas::ResolvedLogicalItem resolved;
                    std::string rerr;
                    if (pqnas::resolve_existing_user_item(users, fp_hex, item.rel_path, &resolved, &rerr) &&
                        resolved.exists && resolved.is_file && resolved.has_physical_anchor) {
                        PhotoStatsRow fresh;
                        std::string ferr;
                        have_row = refresh_one_photo_exif_row(photo.db, item.rel_path, resolved.abs_path,
                                                             item.size_bytes, item.mtime_unix, &fresh, &ferr);
                        if (have_row) row = std::move(fresh);
                    } else have_row = false;
                } else if (!cache_match) have_row = false;
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
                        item.has_gps = true;
                        item.gps_latitude = row.gps_latitude;
                        item.gps_longitude = row.gps_longitude;
                        item.gps_altitude = row.gps_altitude;
                        item.has_gps_altitude = row.gps_altitude_ok;
                    }
                }
            }
        }
        ++files;
        bytes += item.size_bytes;
        if (!include_items) return heartbeat();
        json one{
            {"type", "file"}, {"name", item.name}, {"rel_path", item.rel_path},
            {"path", item.rel_path}, {"base_path", item.base_path},
            {"size_bytes", item.size_bytes}, {"mtime_unix", item.mtime_unix},
            {"rating", item.rating}, {"tags_text", item.tags_text},
            {"keywords", gallery_fast_split_keywords_text_json(item.tags_text)},
            {"notes_text", item.notes_text}, {"description", item.notes_text},
            {"has_gps", item.has_gps}
        };
        if (!item.capture_time_text.empty()) one["capture_time_text"] = item.capture_time_text;
        if (item.capture_time_unix > 0) one["capture_time_unix"] = item.capture_time_unix;
        if (item.has_gps) {
            one["gps_latitude"] = item.gps_latitude;
            one["gps_longitude"] = item.gps_longitude;
            if (item.has_gps_altitude) {
                one["has_gps_altitude"] = true;
                one["gps_altitude"] = item.gps_altitude;
            }
        }
        one["exif"] = json{
            {"ok", item.exif_ok}, {"make", item.exif_make}, {"model", item.exif_model},
            {"lens_model", item.exif_lens_model},
            {"iso", item.exif_iso > 0 ? json(item.exif_iso) : json(nullptr)},
            {"f_number", item.exif_f_number > 0.0 ? json(item.exif_f_number) : json(nullptr)},
            {"exposure_time", item.exif_exposure_time > 0.0 ? json(item.exif_exposure_time) : json(nullptr)},
            {"focal_length", item.exif_focal_length > 0.0 ? json(item.exif_focal_length) : json(nullptr)}
        };
        if (items_out) items_out->push_back(one);
        if (emit) {
            batch.push_back(std::move(one));
            if (files == 1 || batch.size() >= 128) return flush(false);
        }
        return heartbeat();
    };

    bool metadata_root_ok = false;
    const std::string prefix = rel_root.empty() ? "" : rel_root + "/";
    for (const auto& rec : records) {
        if (!heartbeat()) return false;
        const auto& rel = rec.logical_rel_path;
        if (rel.empty() || rel == rel_root || (!prefix.empty() && rel.rfind(prefix, 0) != 0) || reserved_path(rel)) continue;
        std::string normalized, nerr;
        if (!pqnas::normalize_user_rel_path_strict(rel, &normalized, &nerr) || normalized != rel) continue;
        if (!rec.physical_path.empty()) {
            std::error_code ec;
            const auto st = std::filesystem::symlink_status(rec.physical_path, ec);
            if (ec || !std::filesystem::exists(st)) continue;
        }
        metadata_root_ok = true;
        add_parents(rel);
        const auto name = std::filesystem::path(rel).filename().string();
        if (!gallery_fast_is_image_name(name)) continue;
        FastGalleryItem item;
        item.rel_path = rel;
        item.base_path = gallery_fast_parent_rel(rel);
        item.name = name;
        item.type = "file";
        item.size_bytes = rec.size_bytes;
        item.mtime_unix = rec.mtime_epoch;
        if (!visit_image(std::move(item))) return false;
    }
    // Logical-only images must be visible before legacy filesystem enumeration.
    if (!flush(false)) return false;
    if (!physical_root_ok && !metadata_root_ok && !rel_root.empty()) return fail("not_found", "directory not found");

    if (physical_root_ok) {
        std::error_code ec;
        for (std::filesystem::recursive_directory_iterator it(abs_root,
                 std::filesystem::directory_options::skip_permission_denied, ec), end;
             it != end && !ec; it.increment(ec)) {
            if (!heartbeat()) return false;
            const auto child_path = it->path();
            const auto name = child_path.filename().string();
            std::error_code child_ec;
            const auto st = std::filesystem::symlink_status(child_path, child_ec);
            if (child_ec) continue;
            if (std::filesystem::is_symlink(st) || gallery_fast_is_reserved_name(name)) {
                if (std::filesystem::is_directory(st)) it.disable_recursion_pending();
                continue;
            }
            const auto rel = child_path.lexically_relative(user_dir).generic_string();
            if (rel.empty() || reserved_path(rel)) continue;
            if (std::filesystem::is_directory(st)) {
                add_dir(rel);
            } else if (std::filesystem::is_regular_file(st) && gallery_fast_is_image_name(name) && !seen_files.count(rel) && !seen_dirs.count(rel)) {
                FastGalleryItem item;
                item.rel_path = rel;
                item.base_path = gallery_fast_parent_rel(rel);
                item.name = name;
                item.type = "file";
                item.size_bytes = file_size_u64_safe_local(child_path);
                item.mtime_unix = file_mtime_epoch_safe_local(child_path);
                if (!visit_image(std::move(item))) return false;
            }
        }
        if (ec) return fail("server_error", "gallery filesystem scan failed");
    }
    if (!flush(false)) return false;
    if (items_out) {
        std::sort(items_out->begin(), items_out->end(), [](const json& a, const json& b) {
            return a.at("rel_path").get<std::string>() < b.at("rel_path").get<std::string>();
        });
        if (max_returned_items > 0 && items_out->size() > max_returned_items) {
            items_out->erase(items_out->begin() + static_cast<json::difference_type>(max_returned_items), items_out->end());
            if (truncated_out) *truncated_out = true;
        }
    }
    publish_counts();
    return true;
};
