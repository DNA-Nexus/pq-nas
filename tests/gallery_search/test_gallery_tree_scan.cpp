// Focused integration harness for the exact collector used by gallery routes.
// Uses real SQLite indexes and disk trees; EXIF extraction is deterministic here
// so these tests do not depend on an external exiftool installation.
#include "file_location_index.h"
#include "gallery_meta.h"
#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include "httplib.h"
#include <atomic>
#include <thread>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <unistd.h>

using json = nlohmann::json;
namespace fs = std::filesystem;
void require(bool ok, const std::string& message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
fs::path fixture_root;
const std::string COOKIE_KEY = "test";
std::atomic<int> refreshes{0};
std::atomic<int> cached_reads{0};
struct TestUser { std::string storage_state = "allocated"; std::uint64_t quota_bytes = 1000000; };
struct TestUsers {
    std::optional<TestUser> get(const std::string& fp) const {
        if (fp == "missing") return std::nullopt;
        TestUser user;
        if (fp == "unallocated") user.storage_state = "none";
        return user;
    }
};
struct PhotoStatsRow {
    std::uint64_t size_bytes = 0;
    std::int64_t mtime_epoch = 0, taken_epoch = 0, iso = 0;
    std::string taken_at, make, model, lens_model;
    double f_number = 0, exposure_time = 0, focal_length = 0;
    double gps_latitude = 0, gps_longitude = 0, gps_altitude = 0;
    bool exif_ok = false, gps_ok = false, gps_altitude_ok = false;
};
std::map<std::string, PhotoStatsRow> exif_rows;
namespace pqnas {
FileLocationIndex* file_index = nullptr;
GalleryMetaIndex* gallery_index = nullptr;
FileLocationIndex* get_file_location_index() { return file_index; }
GalleryMetaIndex* get_gallery_meta_index() { return gallery_index; }
std::string shorten(const std::string& s, std::size_t n) { return s.substr(0, n); }
std::string data_root_dir() { return fixture_root.string(); }
bool normalize_user_rel_path_strict(const std::string& input, std::string* out, std::string*) {
    const fs::path p(input);
    if (input.empty() || p.is_absolute()) return false;
    for (const auto& part : p) if (part == ".." || part == "." || part.empty()) return false;
    *out = p.generic_string();
    return true;
}
bool resolve_user_path_strict(const fs::path& root, const std::string& rel, fs::path* out, std::string*) {
    *out = root / rel;
    return true;
}
struct ResolvedLogicalItem { bool exists = false, is_file = false, has_physical_anchor = false; fs::path abs_path; };
bool resolve_existing_user_item(const TestUsers&, const std::string& fp, const std::string& rel,
                                ResolvedLogicalItem* out, std::string*) {
    std::string err;
    const auto rec = file_index->get(fp, rel, &err);
    out->abs_path = rec && !rec->physical_path.empty() ? fs::path(rec->physical_path) : fixture_root / "user" / rel;
    out->exists = fs::exists(out->abs_path);
    out->is_file = fs::is_regular_file(out->abs_path);
    out->has_physical_anchor = out->exists;
    return true;
}
}
bool ensure_photo_stats_schema(sqlite3*, std::string*) { return true; }
bool load_cached_photo_stats_row(sqlite3*, const std::string& rel, PhotoStatsRow* out) {
    ++cached_reads;
    const auto it = exif_rows.find(rel);
    if (it == exif_rows.end()) return false;
    *out = it->second;
    return true;
}
bool refresh_one_photo_exif_row(sqlite3*, const std::string& rel, const fs::path&,
                               std::uint64_t size, std::int64_t mtime, PhotoStatsRow* out, std::string*) {
    ++refreshes;
    *out = exif_rows[rel];
    out->size_bytes = size;
    out->mtime_epoch = mtime;
    out->exif_ok = true;
    out->model = "refreshed";
    return true;
}
std::uint64_t file_size_u64_safe_local(const fs::path& p) { return fs::file_size(p); }
std::int64_t file_mtime_epoch_safe_local(const fs::path&) { return 7; }
void put_file(const fs::path& p, const std::string& data = "x") {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << data;
}
void add_record(pqnas::FileLocationIndex& index, const std::string& fp, const std::string& rel,
                const fs::path& physical, std::uint64_t bytes = 9) {
    pqnas::FileLocationRecord rec;
    rec.fp = fp; rec.logical_rel_path = rel; rec.physical_path = physical.string();
    rec.current_pool = "landing"; rec.tier_state = "landing"; rec.size_bytes = bytes; rec.mtime_epoch = 7;
    std::string err;
    require(index.upsert_landing_file(rec, &err), "insert record: " + err);
}
void reply_json(httplib::Response& res, int status, const std::string& body) {
    res.status = status;
    res.set_content(body, "application/json");
}
bool require_user_auth_users_actor(const httplib::Request& req, httplib::Response& res,
                                   const std::string&, TestUsers*, std::string* fp, std::string* role) {
    if (!req.has_header("X-Test-Fp")) {
        reply_json(res, 401, R"({"ok":false,"error":"unauthorized"})");
        return false;
    }
    *fp = req.get_header_value("X-Test-Fp"); *role = "user";
    return true;
}
fs::path user_dir_for_fp(const TestUsers&, const std::string&) { return fixture_root / "user"; }
std::vector<json> parse_frames(const std::string& body) {
    std::vector<json> out;
    std::size_t start = 0;
    while (start < body.size()) {
        const auto end = body.find('\n', start);
        require(end != std::string::npos, "every protocol frame is newline terminated");
        out.push_back(json::parse(body.substr(start, end - start)));
        start = end + 1;
    }
    return out;
}
int main() {
    fixture_root = fs::temp_directory_path() / ("pqnas_gallery_scan_" + std::to_string(getpid()));
    fs::remove_all(fixture_root);
    fs::create_directories(fixture_root / "user");
    const auto user_dir = fixture_root / "user";
    pqnas::FileLocationIndex index(fixture_root / "locations.db");
    pqnas::GalleryMetaIndex gallery(fixture_root / "gallery.db");
    std::string err;
    require(index.open(&err) && index.init_schema(&err), "file index init: " + err);
    require(gallery.open(&err) && gallery.init_schema(&err), "gallery index init: " + err);
    pqnas::file_index = &index;
    pqnas::gallery_index = &gallery;
    TestUsers users;
#include "../../server/src/routes/gallery_tree_scan.inc"

    put_file(user_dir / "same.jpg");
    put_file(user_dir / "legacy/deep.jpg");
    put_file(user_dir / ".pqnas/hidden.jpg");
    put_file(fixture_root / "capacity/only.jpg");
    fs::create_directory_symlink(fixture_root / "capacity", user_dir / "linked");
    add_record(index, "alice", "same.jpg", user_dir / "same.jpg", 42);
    add_record(index, "alice", "virtual/nested/only.jpg", fixture_root / "capacity/only.jpg", 8);
    add_record(index, "alice", "virtual/text.txt", fixture_root / "capacity/only.jpg");
    add_record(index, "alice", "stale/gone.jpg", fixture_root / "gone.jpg");
    add_record(index, "alice", ".pqnas/indexed.jpg", fixture_root / "capacity/only.jpg");
    add_record(index, "alice", "placeholder/pending.jpg", fs::path());
    add_record(index, "bob", "private.jpg", fixture_root / "capacity/only.jpg");
    pqnas::GalleryMetaRec meta;
    meta.scope_type = "user"; meta.scope_id = "alice"; meta.logical_rel_path = "same.jpg"; meta.item_type = "file";
    meta.rating = 4; meta.tags_text = "holiday, blue"; meta.notes_text = "A caption"; meta.size_bytes = 77; meta.mtime_epoch = 11;
    require(gallery.upsert(meta, &err), "gallery metadata insert");
    PhotoStatsRow cached;
    cached.size_bytes = 77; cached.mtime_epoch = 11; cached.taken_at = "2020:01:01 12:00:00";
    cached.taken_epoch = 1000; cached.model = "camera"; cached.exif_ok = true;
    cached.gps_ok = true; cached.gps_latitude = 60.1; cached.gps_longitude = 24.9;
    exif_rows["same.jpg"] = cached;
    json items;
    std::uint64_t dirs = 0, files = 0, bytes = 0;
    bool truncated = false;
    std::string code, message;
    std::vector<json> frames;
    auto emit = [&](const json& frame) {
        frames.push_back(frame);
        if (frames.size() == 1) put_file(user_dir / "arrived_after_first_batch.jpg");
        return true;
    };
    require(collect_fast_gallery_tree("alice", user_dir, "", true, false, 0, &items,
            &dirs, &files, &bytes, &truncated, &code, &message, emit), "mixed scan: " + message);
    require(files == 5 && items.size() == 5 && dirs == 4 && bytes == 96, "merged counts and metadata byte precedence");
    require(frames.front().at("items").size() == 1, "first candidate flushes immediately");
    require(items.front().at("rel_path") == "arrived_after_first_batch.jpg", "physical file added by first emit was scanned afterward");
    require(items.back().at("rel_path") == "virtual/nested/only.jpg", "legacy JSON preserves path order");
    const auto same = std::find_if(items.begin(), items.end(), [](const json& v) { return v.at("rel_path") == "same.jpg"; });
    require(same != items.end() && same->at("size_bytes") == 77 && same->at("rating") == 4 &&
            same->at("notes_text") == "A caption" && same->at("keywords").size() == 2, "metadata roundtrip");
    require(same->at("exif").at("model") == "camera" && same->at("has_gps") == true, "cached EXIF/GPS preserved");
    require(refreshes == 0, "normal search never refreshes EXIF");
    require(collect_fast_gallery_tree("alice", user_dir, "virtual", true, false, 0, &items,
            &dirs, &files, &bytes, &truncated, &code, &message), "logical-only subtree");
    require(items.size() == 1 && dirs == 1, "scoped logical directory does not leak siblings");
    require(!collect_fast_gallery_tree("alice", user_dir, "same.jpg", true, false, 0, &items,
            &dirs, &files, &bytes, &truncated, &code, &message) && code == "not_found", "a file is not a directory root");
    require(!collect_fast_gallery_tree("alice", user_dir, "linked", true, false, 0, &items,
            &dirs, &files, &bytes, &truncated, &code, &message) && code == "bad_request", "symlink root rejected");
    require(collect_fast_gallery_tree("alice", user_dir, "", true, false, 2, &items,
            &dirs, &files, &bytes, &truncated, &code, &message) && items.size() == 2 && truncated && files == 5,
            "legacy cap is explicit and preserves full totals");
    const int reads_before = cached_reads;
    int cancels = 0;
    require(!collect_fast_gallery_tree("alice", user_dir, "", true, false, 0, nullptr,
            &dirs, &files, &bytes, &truncated, &code, &message,
            [&](const json&) { ++cancels; return false; }) && code == "cancelled", "failed writes cancel traversal");
    require(cancels == 1 && cached_reads - reads_before == 1, "cancellation stops after first image");
    // A logical directory takes precedence over a legacy file at its path.
    put_file(user_dir / "directory.jpg");
    add_record(index, "alice", "directory.jpg/child.png", fixture_root / "capacity/only.jpg");
    require(collect_fast_gallery_tree("alice", user_dir, "", true, false, 0, &items,
            &dirs, &files, &bytes, &truncated, &code, &message), "logical directory conflict scan");
    require(std::none_of(items.begin(), items.end(), [](const json& v) { return v.at("rel_path") == "directory.jpg"; }),
            "logical directory overrides legacy image path");
    require(collect_fast_gallery_tree("alice", user_dir, "", true, true, 0, &items,
            &dirs, &files, &bytes, &truncated, &code, &message) && refreshes > 0, "force refresh remains available");

    // More than the legacy 25k limit must stream fully in bounded frames.
    sqlite3* bulk = nullptr;
    require(sqlite3_open((fixture_root / "locations.db").c_str(), &bulk) == SQLITE_OK, "bulk db open");
    require(sqlite3_exec(bulk, "BEGIN", nullptr, nullptr, nullptr) == SQLITE_OK, "bulk transaction");
    sqlite3_stmt* insert = nullptr;
    require(sqlite3_prepare_v2(bulk, "INSERT INTO file_locations(fp,logical_rel_path,current_pool,physical_path,tier_state,size_bytes,mtime_epoch,created_epoch,updated_epoch,version) VALUES('many',?1,'landing','','landing',1,7,0,0,1)", -1, &insert, nullptr) == SQLITE_OK, "bulk prepare");
    for (int i = 0; i < 25129; ++i) {
        const auto rel = "bulk/p" + std::to_string(i) + ".jpg";
        sqlite3_bind_text(insert, 1, rel.c_str(), -1, SQLITE_TRANSIENT);
        require(sqlite3_step(insert) == SQLITE_DONE, "bulk insert");
        sqlite3_reset(insert);
    }
    sqlite3_finalize(insert);
    require(sqlite3_exec(bulk, "COMMIT", nullptr, nullptr, nullptr) == SQLITE_OK, "bulk commit");
    sqlite3_close(bulk);
    std::size_t streamed = 0, batches = 0;
    require(collect_fast_gallery_tree("many", user_dir, "bulk", true, false, 0, nullptr,
            &dirs, &files, &bytes, &truncated, &code, &message,
            [&](const json& frame) {
                if (frame.at("type") == "batch") {
                    require(frame.at("items").size() <= 128, "bounded stream batch");
                    streamed += frame.at("items").size(); ++batches;
                }
                return true;
            }), "large scan");
    require(streamed == 25129 && files == 25129 && bytes == 25129 && !truncated && batches > 1,
            "stream exceeds legacy cap without truncation");
    httplib::Server srv;
    // Registration locals are gone before the first HTTP request, matching the
    // server's temporary role-wiring scopes. Both helper and provider captures
    // must own their callable graph, including nested extension helpers.
    auto register_stream = [&srv, &users]() {
#include "../../server/src/routes/gallery_tree_scan.inc"
#include "../../server/src/routes/gallery_search_stream.inc"
    };
    register_stream();
    const int port = srv.bind_to_any_port("127.0.0.1");
    require(port > 0, "bind HTTP fixture");
    std::thread serving([&]() { srv.listen_after_bind(); });
    srv.wait_until_ready();
    httplib::Client client("127.0.0.1", port);
    client.set_read_timeout(10);
    const auto unauthenticated = client.Get("/api/v4/gallery/search_stream");
    require(unauthenticated && unauthenticated->status == 401 &&
            unauthenticated->get_header_value("Content-Type").find("ndjson") == std::string::npos,
            "authentication rejects before streaming headers");
    const auto denied = client.Get("/api/v4/gallery/search_stream", {{"X-Test-Fp", "unallocated"}});
    require(denied && denied->status == 403, "unallocated storage rejected");
    const auto invalid = client.Get("/api/v4/gallery/search_stream?path=../outside", {{"X-Test-Fp", "alice"}});
    require(invalid && invalid->status == 400, "invalid path rejected before streaming");
    const auto missing = client.Get("/api/v4/gallery/search_stream?path=absent", {{"X-Test-Fp", "alice"}});
    require(missing && missing->status == 200, "scan failure uses streaming protocol");
    const auto missing_frames = parse_frames(missing->body);
    require(missing_frames.back().at("type") == "error" && missing_frames.back().at("error") == "not_found" &&
            std::none_of(missing_frames.begin(), missing_frames.end(), [](const json& f) { return f.at("type") == "done"; }),
            "terminal error never reports completion");
    const int reads_before_http = cached_reads;
    bool first_batch_seen = false, before_full_scan = false;
    std::string received;
    const auto streamed_http = client.Get("/api/v4/gallery/search_stream?path=bulk", {{"X-Test-Fp", "many"}},
        [&](const char* data, std::size_t size) {
            received.append(data, size);
            if (!first_batch_seen && received.find("\"type\":\"batch\"") != std::string::npos) {
                first_batch_seen = true;
                before_full_scan = cached_reads.load() - reads_before_http < 25129;
            }
            return true;
        });
    require(streamed_http && streamed_http->status == 200 && first_batch_seen && before_full_scan,
            "HTTP client receives first image while server still scans");
    require(streamed_http->get_header_value("X-Accel-Buffering") == "no", "proxy buffering disabled");
    const auto http_frames = parse_frames(received);
    require(http_frames.back().at("type") == "done" && http_frames.back().at("stats").at("files") == 25129 &&
            http_frames.back().at("truncated") == false, "HTTP terminal completion is explicit and complete");
    const int reads_before_abort = cached_reads;
    std::string abort_body;
    const auto aborted = client.Get("/api/v4/gallery/search_stream?path=bulk", {{"X-Test-Fp", "many"}},
        [&](const char* data, std::size_t size) {
            abort_body.append(data, size);
            return abort_body.find("\"type\":\"batch\"") == std::string::npos;
        });
    require(!aborted, "HTTP client cancellation closes stream");
    srv.stop();
    serving.join();
    require(cached_reads.load() - reads_before_abort < 25129, "client disconnect stops backend before full scan");
    fs::remove_all(fixture_root);
    std::cout << "ok: gallery progressive collector integration tests passed\n";
}
