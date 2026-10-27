/* Standalone:
g++ -std=c++17 -Wall -Wextra -Werror -Iserver/src \
  tests/file_location_index/test_file_location_scope.cpp \
  server/src/file_location_index.cpp -lsqlite3 -o /tmp/test_file_location_scope
*/
#include "file_location_index.h"

#include <sqlite3.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

struct TempDirectory {
    std::filesystem::path path;
    TempDirectory() {
        std::string pattern = (std::filesystem::temp_directory_path() /
                               "pqnas-file-index-XXXXXX").string();
        std::vector<char> writable(pattern.begin(), pattern.end());
        writable.push_back('\0');
        const char* created = ::mkdtemp(writable.data());
        require(created != nullptr, "create temporary fixture directory");
        path = created;
    }
    ~TempDirectory() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

// Observe the statements executed by the real index methods. VM-step counts
// distinguish a scoped primary-key lookup from scanning all of a user's rows
// without relying on wall-clock timing or reproducing the implementation SQL.
int select_steps = 0;
int select_statements = 0;

int trace_statement(unsigned event, void*, void* statement, void*) {
    if (event != SQLITE_TRACE_PROFILE) return 0;
    auto* stmt = static_cast<sqlite3_stmt*>(statement);
    const char* raw = sqlite3_sql(stmt);
    const std::string sql = raw ? raw : "";
    if (sql.rfind("SELECT ", 0) == 0 && sql.find("FROM file_locations") != std::string::npos) {
        select_steps += sqlite3_stmt_status(stmt, SQLITE_STMTSTATUS_VM_STEP, 0);
        ++select_statements;
    }
    return 0;
}

int observe_connection(sqlite3* db, char**, const sqlite3_api_routines*) {
    return sqlite3_trace_v2(db, SQLITE_TRACE_PROFILE, trace_statement, nullptr);
}

std::vector<std::string> paths(const std::vector<pqnas::FileLocationRecord>& rows) {
    std::vector<std::string> out;
    for (const auto& row : rows) out.push_back(row.logical_rel_path);
    return out;
}

std::set<std::string> names(const std::vector<pqnas::LogicalListItem>& rows) {
    std::set<std::string> out;
    for (const auto& row : rows) out.insert(row.name);
    return out;
}

void check_subset(pqnas::FileLocationIndex& index, const std::string& prefix,
                  const std::vector<std::string>& expected) {
    std::string err = "old error";
    const auto rows = index.list_subtree_records("alice", prefix, &err);
    require(err.empty(), "subtree lookup failed for " + prefix + ": " + err);
    require(paths(rows) == expected, "wrong exact/descendant scope for " + prefix);
    for (const auto& row : rows) require(row.fp == "alice", "another user's row leaked");
}

void run() {
    require(sqlite3_auto_extension(reinterpret_cast<void (*)()>(observe_connection)) == SQLITE_OK,
            "register SQLite statement observer");
    TempDirectory temp;
    const auto db_path = temp.path / "locations.db";
    const auto existing = temp.path / "existing.jpg";
    std::ofstream(existing) << "photo";
    pqnas::FileLocationIndex index(db_path);
    std::string err;
    require(index.open(&err), "open: " + err);
    require(index.init_schema(&err), "init schema: " + err);

    std::size_t inserted = 0;
    auto add = [&](const std::string& path, const std::string& physical = "",
                   const std::string& fp = "alice") {
        pqnas::FileLocationRecord row;
        row.fp = fp;
        row.logical_rel_path = path;
        row.current_pool = "landing";
        row.physical_path = physical;
        row.tier_state = "landing";
        row.size_bytes = 123;
        row.mtime_epoch = 456;
        row.created_epoch = 100;
        row.updated_epoch = 200;
        require(index.upsert_landing_file(row, &err), "insert " + path + ": " + err);
        if (fp == "alice") ++inserted;
    };

    add("photos"); // Exact file rows remain part of a subtree snapshot.
    add("photos/a.jpg", existing.string());
    add("photos/deep/b.jpg", existing.string());
    add("photos/missing.jpg", (temp.path / "missing.jpg").string());
    add("photos/.pqnas/private.jpg", existing.string());
    add("photosibling/leak.jpg", existing.string());
    add("Photos/case.jpg", existing.string());
    add("photos0/upper-bound.jpg", existing.string());
    add("root.jpg", existing.string());
    add(".pqnas/control.json", existing.string());
    add("other-only.jpg", existing.string(), "bob");
    add("photos/bob.jpg", existing.string(), "bob");

    for (const std::string prefix : {"100%", "a_b", "quote'", "Suomi-äö", "back\\slash"}) {
        add(prefix + "/image.jpg", existing.string());
        add(prefix + "sibling/image.jpg", existing.string());
        check_subset(index, prefix, {prefix + "/image.jpg"});
        const auto children = index.list_immediate_children("alice", prefix, &err);
        require(err.empty(), "literal immediate children: " + err);
        require(names(children) == std::set<std::string>{"image.jpg"},
                "literal immediate child scope: " + prefix);
    }
    add("100extra/wildcard.jpg", existing.string());
    add("axb/wildcard.jpg", existing.string());
    check_subset(index, "100%", {"100%/image.jpg"});
    check_subset(index, "a_b", {"a_b/image.jpg"});
    check_subset(index, "photos", {"photos", "photos/.pqnas/private.jpg", "photos/a.jpg",
                                   "photos/deep/b.jpg", "photos/missing.jpg"});
    check_subset(index, "photos/deep", {"photos/deep/b.jpg"});
    check_subset(index, "does-not-exist", {});

    const auto children = index.list_immediate_children("alice", "photos", &err);
    require(err.empty(), "immediate children: " + err);
    require(names(children) == std::set<std::string>{"a.jpg", "deep"},
            "hide missing physical anchors and reserved children; collapse descendants");
    for (const auto& child : children) {
        require(child.type == (child.name == "deep" ? "dir" : "file"), "child type retained");
        if (child.type == "file") {
            require(child.size_bytes == 123 && child.mtime_epoch == 456, "metadata retained");
        }
    }
    const auto root_children = index.list_immediate_children("alice", "", &err);
    require(err.empty(), "root immediate children: " + err);
    require(names(root_children).count("root.jpg") == 1, "root file present");
    require(names(root_children).count(".pqnas") == 0, "root private folder hidden");
    require(names(root_children).count("other-only.jpg") == 0, "user isolation in children");

    // More than the gallery's old 25k limit; unrelated rows must not be visited
    // by either scoped index method. Batch the fixture writes in one transaction.
    sqlite3* writer = nullptr;
    require(sqlite3_open(db_path.string().c_str(), &writer) == SQLITE_OK, "open fixture writer");
    require(sqlite3_exec(writer, "BEGIN", nullptr, nullptr, nullptr) == SQLITE_OK, "begin fixture batch");
    sqlite3_stmt* insert = nullptr;
    require(sqlite3_prepare_v2(writer,
        "INSERT INTO file_locations VALUES ('alice', ?1, 'landing', '', 'landing', 1, 1, 1, 1, 1)",
        -1, &insert, nullptr) == SQLITE_OK, "prepare bulk fixture");
    for (int i = 0; i < 30000; ++i) {
        const std::string path = "unrelated/" + std::to_string(i) + ".jpg";
        sqlite3_bind_text(insert, 1, path.c_str(), -1, SQLITE_TRANSIENT);
        require(sqlite3_step(insert) == SQLITE_DONE, "bulk insert");
        sqlite3_reset(insert);
        ++inserted;
    }
    sqlite3_finalize(insert);
    require(sqlite3_exec(writer, "COMMIT", nullptr, nullptr, nullptr) == SQLITE_OK, "commit fixture batch");
    sqlite3_close(writer);

    select_steps = 0;
    select_statements = 0;
    check_subset(index, "photos/deep", {"photos/deep/b.jpg"});
    require(select_statements == 1 && select_steps < 500,
            "scoped subtree must use path index, steps=" + std::to_string(select_steps));
    select_steps = 0;
    select_statements = 0;
    const auto scoped_children = index.list_immediate_children("alice", "photos/deep", &err);
    require(err.empty() && names(scoped_children) == std::set<std::string>{"b.jpg"}, "deep children");
    require(select_statements == 1 && select_steps < 500,
            "scoped children must use path index, steps=" + std::to_string(select_steps));

    const auto all = index.list_subtree_records("alice", "", &err);
    require(err.empty(), "root snapshot: " + err);
    require(all.size() == inserted && all.size() > 25000, "root snapshot must be complete and unbounded");
    const auto all_paths = paths(all);
    require(std::is_sorted(all_paths.begin(), all_paths.end()), "root snapshot path ordering");
    for (const auto& row : all) require(row.fp == "alice", "root snapshot user isolation");
    sqlite3_reset_auto_extension();
    std::cout << "ok: file location literal scopes, root completeness, metadata and indexed scans\n";
}

} // namespace

int main() {
    try {
        run();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
