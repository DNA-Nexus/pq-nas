	srv.Get("/static/admin_badges.js", [&](const httplib::Request&, httplib::Response& res) {
    	const std::string body = slurp_file(STATIC_BADGES_JS);
    	if (body.empty()) { res.status = 404; res.set_content("missing admin_badges.js","text/plain"); return; }
    	res.set_header("Cache-Control", "no-store");
	    res.set_content(body, "application/javascript; charset=utf-8");
	});


    // -------------------------------------------------------------------------
    // Admin Statistics SQLite snapshot cache
    // -------------------------------------------------------------------------
    const std::filesystem::path admin_stats_db_path =
        std::filesystem::path(users_path).parent_path() / "admin_stats.sqlite3";

    std::mutex admin_stats_sampler_mutex;

    auto admin_stats_sql_exec_local =
        [](sqlite3* db, const char* sql, std::string* err) -> bool {
            char* msg = nullptr;
            const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &msg);
            if (rc != SQLITE_OK) {
                if (err) {
                    *err = msg ? msg : sqlite3_errmsg(db);
                }
                if (msg) sqlite3_free(msg);
                return false;
            }
            return true;
        };

    auto admin_stats_init_db_local =
        [&](sqlite3* db, std::string* err) -> bool {
            if (!admin_stats_sql_exec_local(db, "PRAGMA journal_mode=WAL;", err)) return false;
            if (!admin_stats_sql_exec_local(db, "PRAGMA busy_timeout=5000;", err)) return false;

            const char* sql = R"SQL(
CREATE TABLE IF NOT EXISTS admin_stats_snapshots (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    generated_at_epoch INTEGER NOT NULL,
    generated_at_iso TEXT NOT NULL,
    files_total_count INTEGER NOT NULL DEFAULT 0,
    files_total_bytes INTEGER NOT NULL DEFAULT 0,
    files_average_bytes INTEGER NOT NULL DEFAULT 0,
    files_largest_bytes INTEGER NOT NULL DEFAULT 0,
    users_total INTEGER NOT NULL DEFAULT 0,
    users_enabled INTEGER NOT NULL DEFAULT 0,
    users_admins INTEGER NOT NULL DEFAULT 0,
    workspaces_total INTEGER NOT NULL DEFAULT 0,
    workspaces_enabled INTEGER NOT NULL DEFAULT 0,
    workspaces_user_created INTEGER NOT NULL DEFAULT 0,
    workspaces_admin_created INTEGER NOT NULL DEFAULT 0,
    snapshot_json TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_admin_stats_snapshots_generated
ON admin_stats_snapshots(generated_at_epoch DESC, id DESC);

CREATE TABLE IF NOT EXISTS admin_stats_buckets (
    snapshot_id INTEGER NOT NULL,
    bucket_kind TEXT NOT NULL,
    bucket_key TEXT NOT NULL,
    position INTEGER NOT NULL,
    count INTEGER NOT NULL DEFAULT 0,
    bytes INTEGER NOT NULL DEFAULT 0,
    FOREIGN KEY(snapshot_id) REFERENCES admin_stats_snapshots(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_admin_stats_buckets_snapshot_kind
ON admin_stats_buckets(snapshot_id, bucket_kind, position);

CREATE TABLE IF NOT EXISTS admin_stats_monthly_rollups (
    month_key TEXT PRIMARY KEY,
    period_start_epoch INTEGER NOT NULL,
    period_end_epoch INTEGER NOT NULL,
    sample_count INTEGER NOT NULL DEFAULT 0,

    first_snapshot_epoch INTEGER NOT NULL,
    last_snapshot_epoch INTEGER NOT NULL,

    files_total_count_first INTEGER NOT NULL DEFAULT 0,
    files_total_count_last INTEGER NOT NULL DEFAULT 0,
    files_total_count_min INTEGER NOT NULL DEFAULT 0,
    files_total_count_max INTEGER NOT NULL DEFAULT 0,

    files_total_bytes_first INTEGER NOT NULL DEFAULT 0,
    files_total_bytes_last INTEGER NOT NULL DEFAULT 0,
    files_total_bytes_min INTEGER NOT NULL DEFAULT 0,
    files_total_bytes_max INTEGER NOT NULL DEFAULT 0,
    files_total_bytes_avg INTEGER NOT NULL DEFAULT 0,

    users_total_last INTEGER NOT NULL DEFAULT 0,
    users_enabled_last INTEGER NOT NULL DEFAULT 0,
    users_admins_last INTEGER NOT NULL DEFAULT 0,

    workspaces_total_last INTEGER NOT NULL DEFAULT 0,
    workspaces_enabled_last INTEGER NOT NULL DEFAULT 0,
    workspaces_user_created_last INTEGER NOT NULL DEFAULT 0,
    workspaces_admin_created_last INTEGER NOT NULL DEFAULT 0,

    last_snapshot_json TEXT NOT NULL,
    updated_at_epoch INTEGER NOT NULL DEFAULT 0
);

CREATE INDEX IF NOT EXISTS idx_admin_stats_monthly_rollups_period
ON admin_stats_monthly_rollups(period_start_epoch ASC);
)SQL";

            return admin_stats_sql_exec_local(db, sql, err);
        };

    auto admin_stats_json_u64_local =
        [](const json& j, const char* key) -> std::uint64_t {
            try {
                if (!j.is_object() || !j.contains(key)) return 0;
                const auto& v = j.at(key);
                if (v.is_number_unsigned()) return v.get<std::uint64_t>();
                if (v.is_number_integer()) {
                    const auto n = v.get<long long>();
                    return n > 0 ? static_cast<std::uint64_t>(n) : 0;
                }
            } catch (...) {
            }
            return 0;
        };

    auto admin_stats_bind_u64_local =
        [](sqlite3_stmt* st, int idx, std::uint64_t v) {
            const auto max_i64 =
                static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max());
            if (v > max_i64) v = max_i64;
            sqlite3_bind_int64(st, idx, static_cast<sqlite3_int64>(v));
        };

    auto admin_stats_save_snapshot_local =
        [&](const json& snap, std::string* err) -> bool {
            if (err) err->clear();

            sqlite3* db = nullptr;
            if (sqlite3_open(admin_stats_db_path.string().c_str(), &db) != SQLITE_OK) {
                if (err) *err = db ? sqlite3_errmsg(db) : "sqlite open failed";
                if (db) sqlite3_close(db);
                return false;
            }

            auto close_db = [&]() {
                if (db) {
                    sqlite3_close(db);
                    db = nullptr;
                }
            };

            if (!admin_stats_init_db_local(db, err)) {
                close_db();
                return false;
            }

            if (!admin_stats_sql_exec_local(db, "BEGIN IMMEDIATE;", err)) {
                close_db();
                return false;
            }

            auto rollback = [&]() {
                std::string ignored;
                admin_stats_sql_exec_local(db, "ROLLBACK;", &ignored);
            };

            const json files_j = snap.value("files", json::object());
            const json users_j = snap.value("users", json::object());
            const json workspaces_j = snap.value("workspaces", json::object());

            const char* ins_sql = R"SQL(
INSERT INTO admin_stats_snapshots (
    generated_at_epoch,
    generated_at_iso,
    files_total_count,
    files_total_bytes,
    files_average_bytes,
    files_largest_bytes,
    users_total,
    users_enabled,
    users_admins,
    workspaces_total,
    workspaces_enabled,
    workspaces_user_created,
    workspaces_admin_created,
    snapshot_json
) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14);
)SQL";

            sqlite3_stmt* st = nullptr;
            if (sqlite3_prepare_v2(db, ins_sql, -1, &st, nullptr) != SQLITE_OK) {
                if (err) *err = sqlite3_errmsg(db);
                rollback();
                close_db();
                return false;
            }

            const std::int64_t generated =
                snap.value("generated_at_epoch", static_cast<std::int64_t>(0));
            const std::string generated_iso =
                snap.value("generated_at_iso", std::string());

            sqlite3_bind_int64(st, 1, static_cast<sqlite3_int64>(generated));
            sqlite3_bind_text(st, 2, generated_iso.c_str(), -1, SQLITE_TRANSIENT);
            admin_stats_bind_u64_local(st, 3, admin_stats_json_u64_local(files_j, "total_count"));
            admin_stats_bind_u64_local(st, 4, admin_stats_json_u64_local(files_j, "total_bytes"));
            admin_stats_bind_u64_local(st, 5, admin_stats_json_u64_local(files_j, "average_bytes"));
            admin_stats_bind_u64_local(st, 6, admin_stats_json_u64_local(files_j, "largest_bytes"));
            admin_stats_bind_u64_local(st, 7, admin_stats_json_u64_local(users_j, "total"));
            admin_stats_bind_u64_local(st, 8, admin_stats_json_u64_local(users_j, "enabled"));
            admin_stats_bind_u64_local(st, 9, admin_stats_json_u64_local(users_j, "admins"));
            admin_stats_bind_u64_local(st, 10, admin_stats_json_u64_local(workspaces_j, "total"));
            admin_stats_bind_u64_local(st, 11, admin_stats_json_u64_local(workspaces_j, "enabled"));
            admin_stats_bind_u64_local(st, 12, admin_stats_json_u64_local(workspaces_j, "user_created"));
            admin_stats_bind_u64_local(st, 13, admin_stats_json_u64_local(workspaces_j, "admin_created"));

            const std::string snap_text = snap.dump();
            sqlite3_bind_text(st, 14, snap_text.c_str(), -1, SQLITE_TRANSIENT);

            if (sqlite3_step(st) != SQLITE_DONE) {
                if (err) *err = sqlite3_errmsg(db);
                sqlite3_finalize(st);
                rollback();
                close_db();
                return false;
            }
            sqlite3_finalize(st);

            const sqlite3_int64 snapshot_id = sqlite3_last_insert_rowid(db);

            const char* bucket_sql = R"SQL(
INSERT INTO admin_stats_buckets (
    snapshot_id, bucket_kind, bucket_key, position, count, bytes
) VALUES (?1, ?2, ?3, ?4, ?5, ?6);
)SQL";

            auto insert_bucket_rows = [&](const char* kind,
                                          const json& arr,
                                          const char* key_name) -> bool {
                if (!arr.is_array()) return true;

                int pos = 0;
                for (const auto& row : arr) {
                    if (!row.is_object()) continue;

                    sqlite3_stmt* bst = nullptr;
                    if (sqlite3_prepare_v2(db, bucket_sql, -1, &bst, nullptr) != SQLITE_OK) {
                        if (err) *err = sqlite3_errmsg(db);
                        return false;
                    }

                    const std::string key = row.value(key_name, std::string());
                    sqlite3_bind_int64(bst, 1, snapshot_id);
                    sqlite3_bind_text(bst, 2, kind, -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(bst, 3, key.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int(bst, 4, pos++);
                    admin_stats_bind_u64_local(bst, 5, admin_stats_json_u64_local(row, "count"));
                    admin_stats_bind_u64_local(bst, 6, admin_stats_json_u64_local(row, "bytes"));

                    const bool ok = (sqlite3_step(bst) == SQLITE_DONE);
                    if (!ok && err) *err = sqlite3_errmsg(db);
                    sqlite3_finalize(bst);
                    if (!ok) return false;
                }

                return true;
            };

            if (!insert_bucket_rows("mime", snap.value("top_mime_types", json::array()), "mime") ||
                !insert_bucket_rows("extension", snap.value("top_extensions", json::array()), "extension") ||
                !insert_bucket_rows("scope", snap.value("files_by_scope", json::array()), "scope")) {
                rollback();
                close_db();
                return false;
            }

            if (!admin_stats_sql_exec_local(db, "COMMIT;", err)) {
                rollback();
                close_db();
                return false;
            }

            close_db();
            return true;
        };

    auto admin_stats_load_latest_snapshot_local =
        [&](json* out, std::string* err) -> bool {
            if (out) *out = json::object();
            if (err) err->clear();

            sqlite3* db = nullptr;
            if (sqlite3_open(admin_stats_db_path.string().c_str(), &db) != SQLITE_OK) {
                if (err) *err = db ? sqlite3_errmsg(db) : "sqlite open failed";
                if (db) sqlite3_close(db);
                return false;
            }

            if (!admin_stats_init_db_local(db, err)) {
                sqlite3_close(db);
                return false;
            }

            const char* sql =
                "SELECT snapshot_json FROM admin_stats_snapshots "
                "ORDER BY generated_at_epoch DESC, id DESC LIMIT 1;";

            sqlite3_stmt* st = nullptr;
            if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
                if (err) *err = sqlite3_errmsg(db);
                sqlite3_close(db);
                return false;
            }

            const int rc = sqlite3_step(st);
            if (rc != SQLITE_ROW) {
                sqlite3_finalize(st);
                sqlite3_close(db);
                if (err) *err = "no stored admin statistics snapshot";
                return false;
            }

            const unsigned char* txt = sqlite3_column_text(st, 0);
            const std::string body = txt ? reinterpret_cast<const char*>(txt) : "";

            sqlite3_finalize(st);
            sqlite3_close(db);

            json parsed = json::parse(body, nullptr, false);
            if (parsed.is_discarded() || !parsed.is_object()) {
                if (err) *err = "stored admin statistics snapshot JSON is invalid";
                return false;
            }

            parsed["snapshot_source"] = "sqlite";
            parsed["stats_cache"] = {
                {"enabled", true},
                {"db", "admin_stats.sqlite3"}
            };

            if (out) *out = std::move(parsed);
            return true;
        };

    auto admin_stats_rollup_and_prune_old_snapshots_local =
        [&](std::int64_t* rolled_up_months,
            std::int64_t* pruned_snapshots,
            std::string* err) -> bool {
            if (rolled_up_months) *rolled_up_months = 0;
            if (pruned_snapshots) *pruned_snapshots = 0;
            if (err) err->clear();

            constexpr std::int64_t kRawRetentionSeconds = 180LL * 24LL * 60LL * 60LL;
            constexpr int kKeepLatest = 2000;

            const std::int64_t now_epoch = static_cast<std::int64_t>(std::time(nullptr));
            const std::int64_t cutoff_epoch = now_epoch - kRawRetentionSeconds;

            sqlite3* db = nullptr;
            if (sqlite3_open(admin_stats_db_path.string().c_str(), &db) != SQLITE_OK) {
                if (err) *err = db ? sqlite3_errmsg(db) : "sqlite open failed";
                if (db) sqlite3_close(db);
                return false;
            }

            auto close_db = [&]() {
                if (db) {
                    sqlite3_close(db);
                    db = nullptr;
                }
            };

            if (!admin_stats_init_db_local(db, err)) {
                close_db();
                return false;
            }

            if (!admin_stats_sql_exec_local(db, "BEGIN IMMEDIATE;", err)) {
                close_db();
                return false;
            }

            auto rollback = [&]() {
                std::string ignored;
                admin_stats_sql_exec_local(db, "ROLLBACK;", &ignored);
            };

            struct MonthlyRollupLocal {
                bool seen = false;
                std::string month_key;
                std::int64_t period_start_epoch = 0;
                std::int64_t period_end_epoch = 0;
                std::int64_t sample_count = 0;
                std::int64_t first_snapshot_epoch = 0;
                std::int64_t last_snapshot_epoch = 0;

                std::uint64_t files_total_count_first = 0;
                std::uint64_t files_total_count_last = 0;
                std::uint64_t files_total_count_min = 0;
                std::uint64_t files_total_count_max = 0;

                std::uint64_t files_total_bytes_first = 0;
                std::uint64_t files_total_bytes_last = 0;
                std::uint64_t files_total_bytes_min = 0;
                std::uint64_t files_total_bytes_max = 0;
                long double files_total_bytes_sum = 0.0L;

                std::uint64_t users_total_last = 0;
                std::uint64_t users_enabled_last = 0;
                std::uint64_t users_admins_last = 0;

                std::uint64_t workspaces_total_last = 0;
                std::uint64_t workspaces_enabled_last = 0;
                std::uint64_t workspaces_user_created_last = 0;
                std::uint64_t workspaces_admin_created_last = 0;

                std::string last_snapshot_json;
            };

            auto col_i64 = [](sqlite3_stmt* st, int col) -> sqlite3_int64 {
                if (sqlite3_column_type(st, col) == SQLITE_NULL) return 0;
                return sqlite3_column_int64(st, col);
            };

            auto col_u64 = [&](sqlite3_stmt* st, int col) -> std::uint64_t {
                const sqlite3_int64 v = col_i64(st, col);
                return v > 0 ? static_cast<std::uint64_t>(v) : 0;
            };

            auto col_text = [](sqlite3_stmt* st, int col) -> std::string {
                const unsigned char* t = sqlite3_column_text(st, col);
                return t ? reinterpret_cast<const char*>(t) : std::string();
            };

            std::map<std::string, MonthlyRollupLocal> rollups;

            const char* candidate_sql = R"SQL(
SELECT
    strftime('%Y-%m', generated_at_epoch, 'unixepoch') AS month_key,
    CAST(strftime('%s', strftime('%Y-%m-01 00:00:00', generated_at_epoch, 'unixepoch')) AS INTEGER) AS period_start_epoch,
    generated_at_epoch,
    files_total_count,
    files_total_bytes,
    users_total,
    users_enabled,
    users_admins,
    workspaces_total,
    workspaces_enabled,
    workspaces_user_created,
    workspaces_admin_created,
    snapshot_json
FROM admin_stats_snapshots
WHERE CAST(strftime('%s', datetime(strftime('%Y-%m-01 00:00:00', generated_at_epoch, 'unixepoch'), '+1 month')) AS INTEGER) < ?1
  AND id NOT IN (
      SELECT id
      FROM admin_stats_snapshots
      ORDER BY generated_at_epoch DESC, id DESC
      LIMIT ?2
  )
ORDER BY generated_at_epoch ASC, id ASC;
)SQL";

            sqlite3_stmt* st = nullptr;
            if (sqlite3_prepare_v2(db, candidate_sql, -1, &st, nullptr) != SQLITE_OK) {
                if (err) *err = sqlite3_errmsg(db);
                rollback();
                close_db();
                return false;
            }

            sqlite3_bind_int64(st, 1, static_cast<sqlite3_int64>(cutoff_epoch));
            sqlite3_bind_int(st, 2, kKeepLatest);

            int rc = SQLITE_OK;
            while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
                const std::string month_key = col_text(st, 0);
                if (month_key.empty()) continue;

                MonthlyRollupLocal& r = rollups[month_key];

                const std::int64_t period_start = static_cast<std::int64_t>(col_i64(st, 1));
                const std::int64_t snap_epoch = static_cast<std::int64_t>(col_i64(st, 2));
                const std::uint64_t files_count = col_u64(st, 3);
                const std::uint64_t files_bytes = col_u64(st, 4);

                if (!r.seen) {
                    r.seen = true;
                    r.month_key = month_key;
                    r.period_start_epoch = period_start;
                    r.period_end_epoch = snap_epoch;
                    r.sample_count = 0;
                    r.first_snapshot_epoch = snap_epoch;
                    r.last_snapshot_epoch = snap_epoch;

                    r.files_total_count_first = files_count;
                    r.files_total_count_last = files_count;
                    r.files_total_count_min = files_count;
                    r.files_total_count_max = files_count;

                    r.files_total_bytes_first = files_bytes;
                    r.files_total_bytes_last = files_bytes;
                    r.files_total_bytes_min = files_bytes;
                    r.files_total_bytes_max = files_bytes;
                    r.files_total_bytes_sum = 0.0L;
                }

                r.sample_count += 1;
                r.period_end_epoch = snap_epoch;
                r.last_snapshot_epoch = snap_epoch;

                r.files_total_count_last = files_count;
                r.files_total_count_min = std::min(r.files_total_count_min, files_count);
                r.files_total_count_max = std::max(r.files_total_count_max, files_count);

                r.files_total_bytes_last = files_bytes;
                r.files_total_bytes_min = std::min(r.files_total_bytes_min, files_bytes);
                r.files_total_bytes_max = std::max(r.files_total_bytes_max, files_bytes);
                r.files_total_bytes_sum += static_cast<long double>(files_bytes);

                r.users_total_last = col_u64(st, 5);
                r.users_enabled_last = col_u64(st, 6);
                r.users_admins_last = col_u64(st, 7);

                r.workspaces_total_last = col_u64(st, 8);
                r.workspaces_enabled_last = col_u64(st, 9);
                r.workspaces_user_created_last = col_u64(st, 10);
                r.workspaces_admin_created_last = col_u64(st, 11);

                r.last_snapshot_json = col_text(st, 12);
            }

            if (rc != SQLITE_DONE) {
                if (err) *err = sqlite3_errmsg(db);
                sqlite3_finalize(st);
                rollback();
                close_db();
                return false;
            }

            sqlite3_finalize(st);

            const char* upsert_sql = R"SQL(
INSERT INTO admin_stats_monthly_rollups (
    month_key,
    period_start_epoch,
    period_end_epoch,
    sample_count,
    first_snapshot_epoch,
    last_snapshot_epoch,
    files_total_count_first,
    files_total_count_last,
    files_total_count_min,
    files_total_count_max,
    files_total_bytes_first,
    files_total_bytes_last,
    files_total_bytes_min,
    files_total_bytes_max,
    files_total_bytes_avg,
    users_total_last,
    users_enabled_last,
    users_admins_last,
    workspaces_total_last,
    workspaces_enabled_last,
    workspaces_user_created_last,
    workspaces_admin_created_last,
    last_snapshot_json,
    updated_at_epoch
) VALUES (
    ?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12,
    ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, ?21, ?22, ?23, ?24
)
ON CONFLICT(month_key) DO UPDATE SET
    period_start_epoch = excluded.period_start_epoch,
    period_end_epoch = excluded.period_end_epoch,
    sample_count = excluded.sample_count,
    first_snapshot_epoch = excluded.first_snapshot_epoch,
    last_snapshot_epoch = excluded.last_snapshot_epoch,
    files_total_count_first = excluded.files_total_count_first,
    files_total_count_last = excluded.files_total_count_last,
    files_total_count_min = excluded.files_total_count_min,
    files_total_count_max = excluded.files_total_count_max,
    files_total_bytes_first = excluded.files_total_bytes_first,
    files_total_bytes_last = excluded.files_total_bytes_last,
    files_total_bytes_min = excluded.files_total_bytes_min,
    files_total_bytes_max = excluded.files_total_bytes_max,
    files_total_bytes_avg = excluded.files_total_bytes_avg,
    users_total_last = excluded.users_total_last,
    users_enabled_last = excluded.users_enabled_last,
    users_admins_last = excluded.users_admins_last,
    workspaces_total_last = excluded.workspaces_total_last,
    workspaces_enabled_last = excluded.workspaces_enabled_last,
    workspaces_user_created_last = excluded.workspaces_user_created_last,
    workspaces_admin_created_last = excluded.workspaces_admin_created_last,
    last_snapshot_json = excluded.last_snapshot_json,
    updated_at_epoch = excluded.updated_at_epoch;
)SQL";

            std::int64_t months_written = 0;

            for (const auto& kv : rollups) {
                const MonthlyRollupLocal& r = kv.second;
                if (!r.seen || r.sample_count <= 0) continue;

                sqlite3_stmt* ust = nullptr;
                if (sqlite3_prepare_v2(db, upsert_sql, -1, &ust, nullptr) != SQLITE_OK) {
                    if (err) *err = sqlite3_errmsg(db);
                    rollback();
                    close_db();
                    return false;
                }

                std::uint64_t avg_bytes = 0;
                if (r.sample_count > 0) {
                    const long double avg = r.files_total_bytes_sum / static_cast<long double>(r.sample_count);
                    const long double max_u64 = static_cast<long double>(std::numeric_limits<std::uint64_t>::max());
                    avg_bytes = avg >= max_u64 ? std::numeric_limits<std::uint64_t>::max() : static_cast<std::uint64_t>(avg);
                }

                sqlite3_bind_text(ust, 1, r.month_key.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(ust, 2, static_cast<sqlite3_int64>(r.period_start_epoch));
                sqlite3_bind_int64(ust, 3, static_cast<sqlite3_int64>(r.period_end_epoch));
                sqlite3_bind_int64(ust, 4, static_cast<sqlite3_int64>(r.sample_count));
                sqlite3_bind_int64(ust, 5, static_cast<sqlite3_int64>(r.first_snapshot_epoch));
                sqlite3_bind_int64(ust, 6, static_cast<sqlite3_int64>(r.last_snapshot_epoch));

                admin_stats_bind_u64_local(ust, 7, r.files_total_count_first);
                admin_stats_bind_u64_local(ust, 8, r.files_total_count_last);
                admin_stats_bind_u64_local(ust, 9, r.files_total_count_min);
                admin_stats_bind_u64_local(ust, 10, r.files_total_count_max);

                admin_stats_bind_u64_local(ust, 11, r.files_total_bytes_first);
                admin_stats_bind_u64_local(ust, 12, r.files_total_bytes_last);
                admin_stats_bind_u64_local(ust, 13, r.files_total_bytes_min);
                admin_stats_bind_u64_local(ust, 14, r.files_total_bytes_max);
                admin_stats_bind_u64_local(ust, 15, avg_bytes);

                admin_stats_bind_u64_local(ust, 16, r.users_total_last);
                admin_stats_bind_u64_local(ust, 17, r.users_enabled_last);
                admin_stats_bind_u64_local(ust, 18, r.users_admins_last);

                admin_stats_bind_u64_local(ust, 19, r.workspaces_total_last);
                admin_stats_bind_u64_local(ust, 20, r.workspaces_enabled_last);
                admin_stats_bind_u64_local(ust, 21, r.workspaces_user_created_last);
                admin_stats_bind_u64_local(ust, 22, r.workspaces_admin_created_last);

                sqlite3_bind_text(ust, 23, r.last_snapshot_json.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(ust, 24, static_cast<sqlite3_int64>(now_epoch));

                const bool ok = (sqlite3_step(ust) == SQLITE_DONE);
                if (!ok && err) *err = sqlite3_errmsg(db);
                sqlite3_finalize(ust);

                if (!ok) {
                    rollback();
                    close_db();
                    return false;
                }

                months_written += 1;
            }

            const char* delete_buckets_sql = R"SQL(
DELETE FROM admin_stats_buckets
WHERE snapshot_id IN (
    SELECT id
    FROM admin_stats_snapshots
    WHERE CAST(strftime('%s', datetime(strftime('%Y-%m-01 00:00:00', generated_at_epoch, 'unixepoch'), '+1 month')) AS INTEGER) < ?1
      AND id NOT IN (
          SELECT id
          FROM admin_stats_snapshots
          ORDER BY generated_at_epoch DESC, id DESC
          LIMIT ?2
      )
);
)SQL";

            const char* delete_snapshots_sql = R"SQL(
DELETE FROM admin_stats_snapshots
WHERE CAST(strftime('%s', datetime(strftime('%Y-%m-01 00:00:00', generated_at_epoch, 'unixepoch'), '+1 month')) AS INTEGER) < ?1
  AND id NOT IN (
      SELECT id
      FROM admin_stats_snapshots
      ORDER BY generated_at_epoch DESC, id DESC
      LIMIT ?2
  );
)SQL";

            auto run_delete = [&](const char* sql) -> bool {
                sqlite3_stmt* dst = nullptr;
                if (sqlite3_prepare_v2(db, sql, -1, &dst, nullptr) != SQLITE_OK) {
                    if (err) *err = sqlite3_errmsg(db);
                    return false;
                }

                sqlite3_bind_int64(dst, 1, static_cast<sqlite3_int64>(cutoff_epoch));
                sqlite3_bind_int(dst, 2, kKeepLatest);

                const bool ok = (sqlite3_step(dst) == SQLITE_DONE);
                if (!ok && err) *err = sqlite3_errmsg(db);
                sqlite3_finalize(dst);
                return ok;
            };

            if (!run_delete(delete_buckets_sql)) {
                rollback();
                close_db();
                return false;
            }

            if (!run_delete(delete_snapshots_sql)) {
                rollback();
                close_db();
                return false;
            }

            const int deleted_snapshots = sqlite3_changes(db);

            if (!admin_stats_sql_exec_local(db, "COMMIT;", err)) {
                rollback();
                close_db();
                return false;
            }

            if (rolled_up_months) *rolled_up_months = months_written;
            if (pruned_snapshots) *pruned_snapshots = static_cast<std::int64_t>(deleted_snapshots);

            close_db();
            return true;
        };



    // -------------------------------------------------------------------------
    // Admin Statistics page/API
    // -------------------------------------------------------------------------
    srv.Get("/admin/stats", [&](const httplib::Request& req, httplib::Response& res) {
        std::string actor_fp;
        if (!require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, &actor_fp)) return;

        const std::string body = slurp_file(STATIC_ADMIN_STATS_HTML);
        if (body.empty()) {
            res.status = 404;
            res.set_content("missing admin_stats.html", "text/plain");
            return;
        }
        res.set_content(body, "text/html; charset=utf-8");
    });

    srv.Get("/static/admin_stats.js", [&](const httplib::Request&, httplib::Response& res) {
        const std::string body = slurp_file(STATIC_ADMIN_STATS_JS);
        if (body.empty()) {
            res.status = 404;
            res.set_content("missing admin_stats.js", "text/plain");
            return;
        }
        res.set_content(body, "application/javascript; charset=utf-8");
    });


    // GET /api/v4/admin/stats/trends?period=24h|7d|30d|90d|all&bucket=raw|hour|day
    srv.Get("/api/v4/admin/stats/trends", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_admin_cookie_users(req, res, COOKIE_KEY, std::string{}, &users)) return;

        auto param_or = [&](const char* key, const std::string& fallback) -> std::string {
            return req.has_param(key) ? req.get_param_value(key) : fallback;
        };

        const std::string period = param_or("period", "7d");
        const std::string bucket = param_or("bucket", "hour");

        std::int64_t period_seconds = 7LL * 24LL * 3600LL;
        if (period == "24h") {
            period_seconds = 24LL * 3600LL;
        } else if (period == "7d") {
            period_seconds = 7LL * 24LL * 3600LL;
        } else if (period == "30d") {
            period_seconds = 30LL * 24LL * 3600LL;
        } else if (period == "90d") {
            period_seconds = 90LL * 24LL * 3600LL;
        } else if (period == "all") {
            period_seconds = 0;
        } else {
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "invalid period; expected 24h, 7d, 30d, 90d, or all"}
            }.dump());
            return;
        }

        std::int64_t bucket_seconds = 3600;
        if (bucket == "raw") {
            bucket_seconds = 0;
        } else if (bucket == "hour") {
            bucket_seconds = 3600;
        } else if (bucket == "day") {
            bucket_seconds = 24LL * 3600LL;
        } else {
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "invalid bucket; expected raw, hour, or day"}
            }.dump());
            return;
        }

        const std::int64_t now_epoch = static_cast<std::int64_t>(std::time(nullptr));
        const std::int64_t since_epoch = period_seconds > 0 ? (now_epoch - period_seconds) : 0;

        sqlite3* db = nullptr;
        if (sqlite3_open(admin_stats_db_path.string().c_str(), &db) != SQLITE_OK) {
            std::string msg = db ? sqlite3_errmsg(db) : "sqlite open failed";
            if (db) sqlite3_close(db);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to open admin statistics database"},
                {"detail", msg}
            }.dump());
            return;
        }

        std::string init_err;
        if (!admin_stats_init_db_local(db, &init_err)) {
            sqlite3_close(db);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to initialize admin statistics database"},
                {"detail", init_err}
            }.dump());
            return;
        }

        std::string sql;
        if (bucket == "raw") {
            sql = R"SQL(
SELECT
    generated_at_epoch AS bucket_epoch,
    id,
    generated_at_epoch,
    generated_at_iso,
    files_total_count,
    files_total_bytes,
    files_average_bytes,
    files_largest_bytes,
    users_total,
    users_enabled,
    users_admins,
    workspaces_total,
    workspaces_enabled,
    workspaces_user_created,
    workspaces_admin_created
FROM admin_stats_snapshots
WHERE (?1 <= 0 OR generated_at_epoch >= ?1)
ORDER BY generated_at_epoch ASC, id ASC
LIMIT 2000;
)SQL";
        } else {
            sql = R"SQL(
WITH latest AS (
    SELECT
        (generated_at_epoch / ?1) * ?1 AS bucket_epoch,
        MAX(id) AS snapshot_id
    FROM admin_stats_snapshots
    WHERE (?2 <= 0 OR generated_at_epoch >= ?2)
    GROUP BY bucket_epoch
)
SELECT
    latest.bucket_epoch,
    s.id,
    s.generated_at_epoch,
    s.generated_at_iso,
    s.files_total_count,
    s.files_total_bytes,
    s.files_average_bytes,
    s.files_largest_bytes,
    s.users_total,
    s.users_enabled,
    s.users_admins,
    s.workspaces_total,
    s.workspaces_enabled,
    s.workspaces_user_created,
    s.workspaces_admin_created
FROM latest
JOIN admin_stats_snapshots s ON s.id = latest.snapshot_id
ORDER BY latest.bucket_epoch ASC
LIMIT 2000;
)SQL";
        }

        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) {
            const std::string msg = sqlite3_errmsg(db);
            sqlite3_close(db);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to prepare admin statistics trend query"},
                {"detail", msg}
            }.dump());
            return;
        }

        if (bucket == "raw") {
            sqlite3_bind_int64(st, 1, since_epoch);
        } else {
            sqlite3_bind_int64(st, 1, bucket_seconds);
            sqlite3_bind_int64(st, 2, since_epoch);
        }

        json points = json::array();
        std::int64_t first_epoch = 0;
        std::int64_t last_epoch = 0;

        auto col_i64 = [&](int col) -> sqlite3_int64 {
            if (sqlite3_column_type(st, col) == SQLITE_NULL) return 0;
            return sqlite3_column_int64(st, col);
        };

        int rc = SQLITE_OK;
        while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
            const std::int64_t point_epoch = static_cast<std::int64_t>(col_i64(2));
            if (first_epoch == 0) first_epoch = point_epoch;
            last_epoch = point_epoch;

            json row = {
                {"bucket_t", static_cast<std::int64_t>(col_i64(0))},
                {"id", static_cast<std::int64_t>(col_i64(1))},
                {"t", point_epoch},
                {"iso", reinterpret_cast<const char*>(sqlite3_column_text(st, 3) ? sqlite3_column_text(st, 3) : reinterpret_cast<const unsigned char*>(""))},
                {"files_total_count", static_cast<std::int64_t>(col_i64(4))},
                {"files_total_bytes", static_cast<std::int64_t>(col_i64(5))},
                {"files_average_bytes", static_cast<std::int64_t>(col_i64(6))},
                {"files_largest_bytes", static_cast<std::int64_t>(col_i64(7))},
                {"users_total", static_cast<std::int64_t>(col_i64(8))},
                {"users_enabled", static_cast<std::int64_t>(col_i64(9))},
                {"users_admins", static_cast<std::int64_t>(col_i64(10))},
                {"workspaces_total", static_cast<std::int64_t>(col_i64(11))},
                {"workspaces_enabled", static_cast<std::int64_t>(col_i64(12))},
                {"workspaces_user_created", static_cast<std::int64_t>(col_i64(13))},
                {"workspaces_admin_created", static_cast<std::int64_t>(col_i64(14))},
                {"point_source", static_cast<std::int64_t>(col_i64(1)) < 0 ? "monthly_rollup" : "raw_snapshot"}
            };
            points.push_back(row);
        }

        if (rc != SQLITE_DONE) {
            const std::string msg = sqlite3_errmsg(db);
            sqlite3_finalize(st);
            sqlite3_close(db);
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed while reading admin statistics trend rows"},
                {"detail", msg}
            }.dump());
            return;
        }

        sqlite3_finalize(st);
        sqlite3_close(db);

        json out = {
            {"ok", true},
            {"period", period},
            {"bucket", bucket},
            {"bucket_seconds", bucket_seconds},
            {"since_epoch", since_epoch},
            {"count", points.size()},
            {"first_epoch", first_epoch},
            {"last_epoch", last_epoch},
            {"points", points}
        };

        res.set_header("Cache-Control", "no-store");
        reply_json(res, 200, out.dump());
    });


    auto admin_stats_build_live_snapshot_local =
        [&](const std::string& snapshot_source, json* out_json, std::string* err) -> bool {
            if (out_json) *out_json = json::object();
            if (err) err->clear();

        auto now_epoch_local = []() -> std::int64_t {
            return static_cast<std::int64_t>(std::time(nullptr));
        };

        auto iso_utc_from_epoch_local = [](std::int64_t epoch) -> std::string {
            std::time_t tt = static_cast<std::time_t>(epoch);
            std::tm tm{};
#if defined(_WIN32)
            gmtime_s(&tm, &tt);
#else
            gmtime_r(&tt, &tm);
#endif
            char buf[128];
            const int n = std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                                        tm.tm_year + 1900,
                                        tm.tm_mon + 1,
                                        tm.tm_mday,
                                        tm.tm_hour,
                                        tm.tm_min,
                                        tm.tm_sec);
            if (n <= 0 || static_cast<std::size_t>(n) >= sizeof(buf)) {
                return std::string();
            }
            return std::string(buf);
        };

        auto add_u64_sat_local = [](std::uint64_t a, std::uint64_t b) -> std::uint64_t {
            if (std::numeric_limits<std::uint64_t>::max() - a < b) {
                return std::numeric_limits<std::uint64_t>::max();
            }
            return a + b;
        };

        auto lower_ascii_local = [](std::string v) -> std::string {
            for (char& c : v) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            return v;
        };

        auto mime_from_extension_local = [&](std::string ext) -> std::string {
            ext = lower_ascii_local(ext);
            if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
            if (ext == ".png") return "image/png";
            if (ext == ".gif") return "image/gif";
            if (ext == ".webp") return "image/webp";
            if (ext == ".avif") return "image/avif";
            if (ext == ".svg") return "image/svg+xml";
            if (ext == ".bmp") return "image/bmp";
            if (ext == ".ico") return "image/x-icon";
            if (ext == ".heic") return "image/heic";
            if (ext == ".heif") return "image/heif";
            if (ext == ".rw2") return "image/x-panasonic-rw2";
            if (ext == ".raw") return "image/x-raw";
            if (ext == ".cr2") return "image/x-canon-cr2";
            if (ext == ".nef") return "image/x-nikon-nef";
            if (ext == ".arw") return "image/x-sony-arw";
            if (ext == ".dng") return "image/x-adobe-dng";

            if (ext == ".mp4" || ext == ".m4v") return "video/mp4";
            if (ext == ".mov") return "video/quicktime";
            if (ext == ".webm") return "video/webm";
            if (ext == ".mkv") return "video/x-matroska";
            if (ext == ".avi") return "video/x-msvideo";
            if (ext == ".3gp") return "video/3gpp";

            if (ext == ".mp3") return "audio/mpeg";
            if (ext == ".m4a") return "audio/mp4";
            if (ext == ".aac") return "audio/aac";
            if (ext == ".wav") return "audio/wav";
            if (ext == ".ogg") return "audio/ogg";
            if (ext == ".opus") return "audio/opus";
            if (ext == ".flac") return "audio/flac";
            if (ext == ".mpc") return "audio/x-musepack";

            if (ext == ".pdf") return "application/pdf";
            if (ext == ".txt" || ext == ".log") return "text/plain";
            if (ext == ".md" || ext == ".markdown") return "text/markdown";
            if (ext == ".html" || ext == ".htm") return "text/html";
            if (ext == ".css") return "text/css";
            if (ext == ".js" || ext == ".mjs") return "application/javascript";
            if (ext == ".json") return "application/json";
            if (ext == ".xml") return "application/xml";
            if (ext == ".csv") return "text/csv";

            if (ext == ".zip") return "application/zip";
            if (ext == ".deb") return "application/vnd.debian.binary-package";
            if (ext == ".tar") return "application/x-tar";
            if (ext == ".gz") return "application/gzip";
            if (ext == ".tgz") return "application/gzip";
            if (ext == ".7z") return "application/x-7z-compressed";
            if (ext == ".rar") return "application/vnd.rar";

            if (ext == ".doc") return "application/msword";
            if (ext == ".docx") return "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
            if (ext == ".xls") return "application/vnd.ms-excel";
            if (ext == ".xlsx") return "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";
            if (ext == ".ppt") return "application/vnd.ms-powerpoint";
            if (ext == ".pptx") return "application/vnd.openxmlformats-officedocument.presentationml.presentation";

            return "application/octet-stream";
        };

        struct AdminStatsBucketLocal {
            std::uint64_t count = 0;
            std::uint64_t bytes = 0;
        };

        struct AdminStatsFilesLocal {
            std::uint64_t total_count = 0;
            std::uint64_t total_bytes = 0;
            std::uint64_t largest_bytes = 0;
            std::uint64_t scanned_roots = 0;
            std::uint64_t skipped_roots = 0;
            std::map<std::string, AdminStatsBucketLocal> by_mime;
            std::map<std::string, AdminStatsBucketLocal> by_ext;
            std::map<std::string, AdminStatsBucketLocal> by_scope;
            std::vector<std::string> warnings;
        };

        auto add_bucket_local = [&](std::map<std::string, AdminStatsBucketLocal>& m,
                                    const std::string& key,
                                    std::uint64_t bytes) {
            auto& b = m[key.empty() ? "(none)" : key];
            b.count = add_u64_sat_local(b.count, 1);
            b.bytes = add_u64_sat_local(b.bytes, bytes);
        };

        AdminStatsFilesLocal fs;

        auto should_skip_admin_stats_path_local = [](const std::filesystem::path& p) -> bool {
            const std::string name = p.filename().string();

            // Keep v1 focused on live/user-visible content and skip internal bookkeeping.
            return name == ".pqnas_activity" ||
                   name == ".pqnas_upload_sessions" ||
                   name == ".pqnas_tmp" ||
                   name == ".tmp";
        };

        auto scan_root_local = [&](const std::filesystem::path& root,
                                   const std::string& scope_label) {
            std::error_code ec;
            if (!std::filesystem::exists(root, ec) || ec) {
                fs.skipped_roots = add_u64_sat_local(fs.skipped_roots, 1);
                return;
            }

            if (!std::filesystem::is_directory(root, ec) || ec) {
                fs.skipped_roots = add_u64_sat_local(fs.skipped_roots, 1);
                return;
            }

            fs.scanned_roots = add_u64_sat_local(fs.scanned_roots, 1);

            const auto opts =
                std::filesystem::directory_options::skip_permission_denied;

            std::filesystem::recursive_directory_iterator it(root, opts, ec);
            std::filesystem::recursive_directory_iterator end;

            if (ec) {
                fs.warnings.push_back("failed to start scan for " + scope_label + ": " + ec.message());
                return;
            }

            for (; it != end; it.increment(ec)) {
                if (ec) {
                    fs.warnings.push_back("scan warning for " + scope_label + ": " + ec.message());
                    ec.clear();
                    continue;
                }

                const std::filesystem::path cur = it->path();

                if (should_skip_admin_stats_path_local(cur)) {
                    if (it->is_directory(ec)) {
                        it.disable_recursion_pending();
                    }
                    ec.clear();
                    continue;
                }

                std::error_code sec;
                const auto st = it->symlink_status(sec);
                if (sec) continue;
                if (std::filesystem::is_symlink(st)) continue;
                if (!std::filesystem::is_regular_file(st)) continue;

                std::error_code sz_ec;
                const auto sz0 = std::filesystem::file_size(cur, sz_ec);
                if (sz_ec) {
                    fs.warnings.push_back("file size warning for " + scope_label + ": " + sz_ec.message());
                    continue;
                }

                const std::uint64_t sz = static_cast<std::uint64_t>(sz0);
                fs.total_count = add_u64_sat_local(fs.total_count, 1);
                fs.total_bytes = add_u64_sat_local(fs.total_bytes, sz);
                if (sz > fs.largest_bytes) fs.largest_bytes = sz;

                std::string ext = cur.extension().string();
                ext = lower_ascii_local(ext);
                if (ext.empty()) ext = "(none)";

                const std::string mime = mime_from_extension_local(ext);

                add_bucket_local(fs.by_mime, mime, sz);
                add_bucket_local(fs.by_ext, ext, sz);
                add_bucket_local(fs.by_scope, scope_label, sz);
            }
        };

        if (!users.load(users_path)) {
            if (err) *err = "failed to reload users";
            return false;
        }

        // Best-effort reload; keep serving stats even if workspace metadata reload fails.
        const bool workspaces_loaded_for_stats = workspaces.load(workspaces_path);
        if (!workspaces_loaded_for_stats) {
            fs.warnings.push_back("failed to reload workspaces metadata");
        }

        json workspaces_j = {
            {"total", 0},
            {"enabled", 0},
            {"disabled", 0},
            {"user_created", 0},
            {"admin_created", 0},
            {"enabled_members", 0},
            {"total_members", 0},
            {"quota_bytes", 0}
        };

        if (workspaces_loaded_for_stats) {
            for (const auto& kv : workspaces.snapshot()) {
                const auto& w = kv.second;

                workspaces_j["total"] = workspaces_j.value("total", 0) + 1;

                if (w.status == "enabled") {
                    workspaces_j["enabled"] = workspaces_j.value("enabled", 0) + 1;
                } else {
                    workspaces_j["disabled"] = workspaces_j.value("disabled", 0) + 1;
                }

                for (const auto& m : w.members) {
                    workspaces_j["total_members"] = workspaces_j.value("total_members", 0) + 1;
                    if (m.status == "enabled") {
                        workspaces_j["enabled_members"] = workspaces_j.value("enabled_members", 0) + 1;
                    }
                }

                // Exact source:
                // - admin-created workspace routes store kind="admin"
                // - user-created Shared Space routes store kind="personal"
                if (w.kind == "personal") {
                    workspaces_j["user_created"] = workspaces_j.value("user_created", 0) + 1;
                } else {
                    workspaces_j["admin_created"] = workspaces_j.value("admin_created", 0) + 1;
                }

                workspaces_j["quota_bytes"] =
                    add_u64_sat_local(workspaces_j.value("quota_bytes", std::uint64_t{0}),
                                      static_cast<std::uint64_t>(w.quota_bytes));
            }
        }

        json users_j = {
            {"total", 0},
            {"enabled", 0},
            {"disabled", 0},
            {"revoked", 0},
            {"admins", 0},
            {"storage_allocated", 0},
            {"quota_bytes", 0}
        };

        for (const auto& kv : users.snapshot()) {
            const auto& u = kv.second;

            users_j["total"] = users_j.value("total", 0) + 1;

            if (u.status == "enabled") users_j["enabled"] = users_j.value("enabled", 0) + 1;
            else if (u.status == "revoked") users_j["revoked"] = users_j.value("revoked", 0) + 1;
            else users_j["disabled"] = users_j.value("disabled", 0) + 1;

            if (u.role == "admin" && u.status == "enabled") {
                users_j["admins"] = users_j.value("admins", 0) + 1;
            }

            if (u.storage_state == "allocated") {
                users_j["storage_allocated"] = users_j.value("storage_allocated", 0) + 1;
                users_j["quota_bytes"] =
                    add_u64_sat_local(users_j.value("quota_bytes", std::uint64_t{0}),
                                      static_cast<std::uint64_t>(u.quota_bytes));

                scan_root_local(user_dir_for_fp(users, u.fingerprint), "user");
            }
        }

        const std::filesystem::path default_data_root = std::filesystem::path(pqnas::data_root_dir());

        for (const auto& kv : workspaces.snapshot()) {
            const auto& w = kv.second;
            if (w.status != "enabled") continue;
            if (w.root_rel.empty()) continue;

            scan_root_local(default_data_root / w.root_rel, "workspace");
        }

        auto top_array_local = [](const std::map<std::string, AdminStatsBucketLocal>& m,
                                  const std::string& name_key,
                                  std::size_t limit) -> json {
            std::vector<std::pair<std::string, AdminStatsBucketLocal>> rows(m.begin(), m.end());

            std::sort(rows.begin(), rows.end(),
                      [](const auto& a, const auto& b) {
                          if (a.second.bytes != b.second.bytes) return a.second.bytes > b.second.bytes;
                          if (a.second.count != b.second.count) return a.second.count > b.second.count;
                          return a.first < b.first;
                      });

            json out = json::array();
            std::size_t n = 0;
            for (const auto& row : rows) {
                if (n++ >= limit) break;
                out.push_back({
                    {name_key, row.first},
                    {"count", row.second.count},
                    {"bytes", row.second.bytes}
                });
            }
            return out;
        };

        const std::uint64_t average_bytes =
            fs.total_count > 0 ? (fs.total_bytes / fs.total_count) : 0;

        const std::int64_t now_ts = now_epoch_local();

        json out = {
            {"ok", true},
            {"generated_at_epoch", now_ts},
            {"generated_at_iso", iso_utc_from_epoch_local(now_ts)},
            {"server_started_at_epoch", pqnas_server_started_at_epoch_local()},
            {"server_started_at_iso", pqnas_server_started_at_iso_local()},
            {"server_uptime_seconds", pqnas_server_uptime_seconds_local()},
            {"method", "live filesystem scan"},
            {"note", "MIME types are extension-based in this v1 implementation."},
            {"users", users_j},
            {"workspaces", workspaces_j},
            {"files", {
                {"total_count", fs.total_count},
                {"total_bytes", fs.total_bytes},
                {"average_bytes", average_bytes},
                {"largest_bytes", fs.largest_bytes},
                {"scanned_roots", fs.scanned_roots},
                {"skipped_roots", fs.skipped_roots}
            }},
            {"top_mime_types", top_array_local(fs.by_mime, "mime", 10)},
            {"top_extensions", top_array_local(fs.by_ext, "extension", 10)},
            {"files_by_scope", top_array_local(fs.by_scope, "scope", 10)},
            {"warnings", fs.warnings}
        };


        out["snapshot_source"] = snapshot_source;

        if (out_json) *out_json = std::move(out);
        return true;
    };

    srv.Get("/api/v4/admin/stats/summary", [&](const httplib::Request& req, httplib::Response& res) {
        std::string actor_fp;
        if (!require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, &actor_fp)) return;

        const bool force_refresh =
            req.has_param("refresh") &&
            (req.get_param_value("refresh") == "1" ||
             req.get_param_value("refresh") == "true" ||
             req.get_param_value("refresh") == "now");

        if (!force_refresh) {
            json cached;
            std::string cache_err;
            if (admin_stats_load_latest_snapshot_local(&cached, &cache_err)) {
                cached["server_started_at_epoch"] = pqnas_server_started_at_epoch_local();
                cached["server_started_at_iso"] = pqnas_server_started_at_iso_local();
                cached["server_uptime_seconds"] = pqnas_server_uptime_seconds_local();

                reply_json(res, 200, cached.dump());
                return;
            }
            // No snapshot yet: fall through and build the first one.
        }

        json out;
        std::string build_err;

        {
            std::lock_guard<std::mutex> lock(admin_stats_sampler_mutex);

            if (!admin_stats_build_live_snapshot_local(
                    force_refresh ? std::string("live_refresh") : std::string("live_initial"),
                    &out,
                    &build_err)) {
                reply_json(res, 500, json{
                    {"ok", false},
                    {"error", "admin_stats_build_failed"},
                    {"message", build_err.empty() ? "failed to build admin statistics snapshot" : build_err}
                }.dump());
                return;
            }

            std::string save_err;
            if (!admin_stats_save_snapshot_local(out, &save_err)) {
                if (!out.contains("warnings") || !out["warnings"].is_array()) {
                    out["warnings"] = json::array();
                }
                out["warnings"].push_back("failed to save admin statistics snapshot: " + save_err);
            } else {
                out["stats_cache"] = {
                    {"enabled", true},
                    {"db", "admin_stats.sqlite3"}
                };

                std::int64_t rolled_up_months = 0;
                std::int64_t pruned_snapshots = 0;
                std::string prune_err;
                if (!admin_stats_rollup_and_prune_old_snapshots_local(&rolled_up_months, &pruned_snapshots, &prune_err)) {
                    if (!out.contains("warnings") || !out["warnings"].is_array()) {
                        out["warnings"] = json::array();
                    }
                    out["warnings"].push_back("failed to roll up/prune old admin statistics snapshots: " + prune_err);
                } else if (pruned_snapshots > 0) {
                    out["stats_cache"]["rolled_up_months"] = rolled_up_months;
                    out["stats_cache"]["pruned_snapshots"] = pruned_snapshots;
                }
            }
        }

        out["server_started_at_epoch"] = pqnas_server_started_at_epoch_local();
        out["server_started_at_iso"] = pqnas_server_started_at_iso_local();
        out["server_uptime_seconds"] = pqnas_server_uptime_seconds_local();

        reply_json(res, 200, out.dump());
    });


    std::atomic<bool> admin_stats_sampler_stop{false};
    std::thread admin_stats_sampler_thread([&]() {
        constexpr int kAdminStatsSamplerIntervalSeconds = 60 * 60;

        while (!admin_stats_sampler_stop.load()) {
            for (int i = 0;
                 i < kAdminStatsSamplerIntervalSeconds && !admin_stats_sampler_stop.load();
                 ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }

            if (admin_stats_sampler_stop.load()) break;

            try {
                json snap;
                std::string build_err;

                std::lock_guard<std::mutex> lock(admin_stats_sampler_mutex);

                if (!admin_stats_build_live_snapshot_local("scheduled_hourly", &snap, &build_err)) {
                    std::cerr << "[admin_stats] scheduled sample failed: "
                              << (build_err.empty() ? "build failed" : build_err)
                              << "\n";
                    continue;
                }

                std::string save_err;
                if (!admin_stats_save_snapshot_local(snap, &save_err)) {
                    std::cerr << "[admin_stats] scheduled sample save failed: "
                              << save_err << "\n";
                    continue;
                }

                std::int64_t rolled_up_months = 0;
                std::int64_t pruned_snapshots = 0;
                std::string prune_err;
                if (!admin_stats_rollup_and_prune_old_snapshots_local(&rolled_up_months, &pruned_snapshots, &prune_err)) {
                    std::cerr << "[admin_stats] scheduled rollup/prune failed: "
                              << prune_err << "\n";
                } else if (pruned_snapshots > 0) {
                    std::cerr << "[admin_stats] scheduled rollup/prune deleted "
                              << pruned_snapshots << " old snapshot(s)\n";
                }

                std::cerr << "[admin_stats] scheduled sample saved at "
                          << snap.value("generated_at_iso", std::string("?"))
                          << "\n";
            } catch (const std::exception& e) {
                std::cerr << "[admin_stats] scheduled sample exception: "
                          << e.what() << "\n";
            } catch (...) {
                std::cerr << "[admin_stats] scheduled sample unknown exception\n";
            }
        }
    });

    pqnas::WorkspaceFileRouteDeps ws_file_deps;
    ws_file_deps.users = &users;
    ws_file_deps.workspaces = &workspaces;
    ws_file_deps.users_path = users_path;
    ws_file_deps.workspaces_path = workspaces_path;
    ws_file_deps.cookie_key = COOKIE_KEY;
    ws_file_deps.session_cookie_verify =
        [&](const unsigned char* cookie_key,
            const std::string& cookie_value,
            std::string& out_fingerprint_b64,
            long& out_exp) -> bool {
            return session_cookie_verify(cookie_key, cookie_value, out_fingerprint_b64, out_exp);
        };
    ws_file_deps.b64_std_decode =
        [&](const std::string& b64,
            std::string& out_bytes) -> bool {
            return b64std_decode_to_bytes(b64, out_bytes);
        };
    ws_file_deps.origin = &ORIGIN;
    ws_file_deps.transport_max_upload_bytes =
        (g_transport_max_upload_bytes ? g_transport_max_upload_bytes : k_payload_max_upload_bytes);
    ws_file_deps.payload_max_upload_bytes = k_payload_max_upload_bytes;
    ws_file_deps.file_versions = &file_versions_index;
    ws_file_deps.trash_service = &trash_service;
    ws_file_deps.trash_index = &trash_index;
    ws_file_deps.locks_db_path = file_lock_deps.locks_db_path;
    ws_file_deps.reply_json =
        [](httplib::Response& res, int status, const std::string& body) {
            reply_json(res, status, body);
    };
    ws_file_deps.require_user_auth_users_actor =
        [&](const httplib::Request& req,
            httplib::Response& res,
            const unsigned char* cookie_key,
            pqnas::UsersRegistry* users_arg,
            std::string* out_fp_hex,
            std::string* out_role) -> bool {
            return require_user_auth_users_actor(
                req, res, cookie_key, users_arg, out_fp_hex, out_role);
    };
    ws_file_deps.audit_emit =
        [&](const std::string& event,
            const std::string& outcome,
            const std::map<std::string, std::string>& fields) {
            pqnas::AuditEvent ev;
            ev.event = event;
            ev.outcome = outcome;
            ev.f = fields;
            audit_append(ev);
    };
    ws_file_deps.now_epoch_sec = []() {
        return now_epoch_sec();
    };

    pqnas::register_workspace_file_routes(srv, ws_file_deps);
    pqnas::register_workspace_link_routes(srv, ws_file_deps);

pqnas::WorkspaceExternalInviteRouteDeps ws_external_invite_deps;
ws_external_invite_deps.users = &users;
ws_external_invite_deps.workspaces = &workspaces;
ws_external_invite_deps.external_invites = &workspace_external_invites;
ws_external_invite_deps.users_path = users_path;
ws_external_invite_deps.workspaces_path = workspaces_path;
ws_external_invite_deps.external_invites_path = workspace_external_invites_path;
ws_external_invite_deps.cookie_key = COOKIE_KEY;
ws_external_invite_deps.origin = &ORIGIN;
ws_external_invite_deps.app = &APP_NAME;
ws_external_invite_deps.reply_json = [](httplib::Response& res, int status, const std::string& body) {
    reply_json(res, status, body);
};
ws_external_invite_deps.require_user_auth_users_actor =
    [&](const httplib::Request& req,
        httplib::Response& res,
        const unsigned char* cookie_key,
        pqnas::UsersRegistry* users_arg,
        std::string* out_fp_hex,
        std::string* out_role) -> bool {
        return require_user_auth_users_actor(
            req, res, cookie_key, users_arg, out_fp_hex, out_role);
    };
ws_external_invite_deps.audit_emit =
    [&](const std::string& event,
        const std::string& outcome,
        const std::map<std::string, std::string>& fields) {
        pqnas::AuditEvent ev;
        ev.event = event;
        ev.outcome = outcome;
        ev.f = fields;
        audit_append(ev);
    };
ws_external_invite_deps.now_epoch_sec = []() { return now_epoch_sec(); };
ws_external_invite_deps.now_iso_utc = []() { return pqnas::now_iso_utc(); };
ws_external_invite_deps.random_b64url = [&](int n) { return random_b64url(n); };
ws_external_invite_deps.url_encode = [&](const std::string& v) { return url_encode(v); };
ws_external_invite_deps.build_req_payload_canonical =
    [&](const std::string& sid,
        const std::string& chal,
        const std::string& nonce,
        long iat,
        long exp) {
        return build_req_payload_canonical(sid, chal, nonce, iat, exp);
    };
ws_external_invite_deps.sign_req_token =
    [&](const std::string& payload) {
        return sign_req_token(payload);
    };
ws_external_invite_deps.st_hash_b64_from_st =
    [&](const std::string& st) {
        if (v5.st_hash_b64_from_st) {
            return v5.st_hash_b64_from_st(st);
        }
        return std::string{};
    };
ws_external_invite_deps.qr_svg_from_text =
    [&](const std::string& text, int scale, int border) {
        return qr_svg_from_text(text, scale, border);
    };

pqnas::register_workspace_external_invite_routes(srv, ws_external_invite_deps);

pqnas::WorkspaceExternalSessionRouteDeps ws_external_session_deps;
ws_external_session_deps.workspaces = &workspaces;
ws_external_session_deps.external_sessions = &workspace_external_sessions;
ws_external_session_deps.workspaces_path = workspaces_path;
ws_external_session_deps.origin = &ORIGIN;
ws_external_session_deps.app = &APP_NAME;
ws_external_session_deps.cookie_key = COOKIE_KEY;
ws_external_session_deps.session_cookie_mint =
    [&](const unsigned char* cookie_key,
        const std::string& fp_b64,
        long iat,
        long exp,
        std::string& out_cookie_val) -> bool {
        return session_cookie_mint(cookie_key, fp_b64, iat, exp, out_cookie_val);
    };
ws_external_session_deps.b64_std =
    [&](const unsigned char* data, size_t len) {
        return pqnas::b64_std(data, len);
    };
ws_external_session_deps.reply_json = [](httplib::Response& res, int status, const std::string& body) {
    reply_json(res, status, body);
};
ws_external_session_deps.audit_emit =
    [&](const std::string& event,
        const std::string& outcome,
        const std::map<std::string, std::string>& fields) {
        pqnas::AuditEvent ev;
        ev.event = event;
        ev.outcome = outcome;
        ev.f = fields;
        audit_append(ev);
    };
ws_external_session_deps.now_epoch_sec = []() { return now_epoch_sec(); };
ws_external_session_deps.now_iso_utc = []() { return pqnas::now_iso_utc(); };
ws_external_session_deps.random_b64url = [&](int n) { return random_b64url(n); };
ws_external_session_deps.url_encode = [&](const std::string& v) { return url_encode(v); };
ws_external_session_deps.build_req_payload_canonical =
    [&](const std::string& sid,
        const std::string& chal,
        const std::string& nonce,
        long iat,
        long exp) {
        return build_req_payload_canonical(sid, chal, nonce, iat, exp);
    };
ws_external_session_deps.sign_req_token =
    [&](const std::string& payload) {
        return sign_req_token(payload);
    };
ws_external_session_deps.st_hash_b64_from_st =
    [&](const std::string& st) {
        if (v5.st_hash_b64_from_st) {
            return v5.st_hash_b64_from_st(st);
        }
        return std::string{};
    };
ws_external_session_deps.qr_svg_from_text =
    [&](const std::string& text, int scale, int border) {
        return qr_svg_from_text(text, scale, border);
    };

pqnas::register_workspace_external_session_routes(srv, ws_external_session_deps);


	    // ---- Admin users overview routes ----
    {
        AdminUsersOverviewRoutesContext admin_users_overview_ctx;
        admin_users_overview_ctx.users = &users;
        admin_users_overview_ctx.users_path = users_path;

        admin_users_overview_ctx.require_admin_auth =
            [&](const httplib::Request& req, httplib::Response& res, std::string* actor_fp) {
                return require_admin_auth_users_actor(req, res, COOKIE_KEY, users_path, &users, actor_fp);
            };

        admin_users_overview_ctx.reply_json =
            [](httplib::Response& res, int status, const std::string& body) {
                reply_json(res, status, body);
            };

        admin_users_overview_ctx.user_dir_for_fp =
            [&](const std::string& fp) {
                return user_dir_for_fp(users, fp);
            };

        admin_users_overview_ctx.dir_size_bytes_best_effort =
            [](const std::filesystem::path& path) -> std::uint64_t {
                return static_cast<std::uint64_t>(dir_size_bytes_best_effort(path));
            };

        register_admin_users_overview_routes(srv, admin_users_overview_ctx);
    }


    // ---- Admin approvals UI routes ----
    {
        AdminApprovalsUiRoutesContext admin_approvals_ui_ctx;
        admin_approvals_ui_ctx.approvals_html_path = STATIC_APPROVALS_HTML;
        admin_approvals_ui_ctx.approvals_js_path = STATIC_APPROVALS_JS;

        admin_approvals_ui_ctx.require_admin =
            [&](const httplib::Request& req, httplib::Response& res, std::string* actor_fp) {
                return require_admin_cookie_users_actor(req, res, COOKIE_KEY, users_path, &users, actor_fp);
            };

        admin_approvals_ui_ctx.slurp_file =
            [&](const std::string& path) {
                return slurp_file(path);
            };

        register_admin_approvals_ui_routes(srv, admin_approvals_ui_ctx);
    }


	// Generic static handler: /static/<anything>
	// Must come AFTER specific /static/*.js handlers so those remain unchanged.
