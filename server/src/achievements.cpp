#include "achievements.h"

#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace pqnas::achievements {
namespace {

using json = nlohmann::json;

long long count_one(sqlite3* db, const char* sql, const std::string& a) {
    if (!db || !sql || a.empty()) return 0;

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
        return 0;
    }

    sqlite3_bind_text(st, 1, a.c_str(), -1, SQLITE_TRANSIENT);

    long long out = 0;
    if (sqlite3_step(st) == SQLITE_ROW) {
        out = sqlite3_column_int64(st, 0);
    }

    sqlite3_finalize(st);
    return out;
}

long long count_two(sqlite3* db, const char* sql, const std::string& a, const std::string& b) {
    if (!db || !sql || a.empty() || b.empty()) return 0;

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
        return 0;
    }

    sqlite3_bind_text(st, 1, a.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, b.c_str(), -1, SQLITE_TRANSIENT);

    long long out = 0;
    if (sqlite3_step(st) == SQLITE_ROW) {
        out = sqlite3_column_int64(st, 0);
    }

    sqlite3_finalize(st);
    return out;
}

long long parse_iso_epoch_utc_best_effort(const std::string& raw) {
    if (raw.size() < 10) return 0;

    std::string s = raw;
    if (s.size() >= 19 && s[10] == ' ') s[10] = 'T';

    std::tm tm {};
    std::istringstream in(s.substr(0, 19));
    in >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");

    if (in.fail()) {
        std::istringstream date_only(s.substr(0, 10));
        date_only >> std::get_time(&tm, "%Y-%m-%d");
        if (date_only.fail()) return 0;
    }

#if defined(_WIN32)
    return 0;
#else
    const std::time_t t = timegm(&tm);
    return t > 0 ? static_cast<long long>(t) : 0;
#endif
}

long long account_age_days(const std::string& added_at_iso) {
    const long long created = parse_iso_epoch_utc_best_effort(added_at_iso);
    if (created <= 0) return 0;

    const long long now = static_cast<long long>(std::time(nullptr));
    if (now <= created) return 0;

    return (now - created) / 86400LL;
}

std::string trim_ascii_copy(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.erase(s.begin());
    }

    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }

    return s;
}

bool dev_force_all_achievements_for_fp(const std::string& fp) {
    if (fp.empty()) return false;

    std::ifstream in("/srv/pqnas/config/circlestack_achievements_force_all_fp");
    if (!in.good()) return false;

    std::string target;
    std::getline(in, target);
    target = trim_ascii_copy(target);

    return target == "*" || target == fp;
}

std::filesystem::path activity_db_path_for_user_root_best_effort(const std::filesystem::path& user_root) {
    if (user_root.empty()) return {};
    return user_root / ".pqnas_activity" / "activity.sqlite";
}

long long json_int64_any_key(const json& j, std::initializer_list<const char*> keys) {
    if (!j.is_object()) return 0;

    for (const char* key : keys) {
        if (!key) continue;

        auto it = j.find(key);
        if (it == j.end()) continue;

        try {
            if (it->is_number_integer() || it->is_number_unsigned()) {
                return it->get<long long>();
            }

            if (it->is_number_float()) {
                return static_cast<long long>(it->get<double>());
            }

            if (it->is_string()) {
                const std::string raw = trim_ascii_copy(it->get<std::string>());
                if (!raw.empty()) {
                    return std::stoll(raw);
                }
            }
        } catch (...) {
            return 0;
        }
    }

    return 0;
}

void add_activity_stats_for_user_root(json& stats, const std::filesystem::path& user_root) {
    stats["shares_created_total"] = 0;
    stats["uploads_total"] = 0;
    stats["uploaded_bytes_total"] = 0;
    stats["dropzones_created_total"] = 0;
    stats["dropzone_uploads_received_total"] = 0;
    stats["trusted_devices_paired_total"] = 0;

    const auto db_path = activity_db_path_for_user_root_best_effort(user_root);
    if (db_path.empty()) return;

    std::error_code ec;
    if (!std::filesystem::exists(db_path, ec) || ec) return;

    sqlite3* db = nullptr;
    const int open_rc = sqlite3_open_v2(
        db_path.string().c_str(),
        &db,
        SQLITE_OPEN_READONLY,
        nullptr
    );

    if (open_rc != SQLITE_OK || !db) {
        if (db) sqlite3_close(db);
        return;
    }

    const char* sql =
        "SELECT event_type, COALESCE(details_json, '') "
        "FROM activity_events";

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return;
    }

    long long shares_created = 0;
    long long uploads = 0;
    long long uploaded_bytes = 0;
    long long dropzones_created = 0;
    long long dropzone_uploads = 0;
    long long trusted_devices = 0;

    while (sqlite3_step(st) == SQLITE_ROW) {
        const unsigned char* et_raw = sqlite3_column_text(st, 0);
        const unsigned char* details_raw = sqlite3_column_text(st, 1);

        const std::string event_type = et_raw
            ? reinterpret_cast<const char*>(et_raw)
            : std::string();

        const std::string details_s = details_raw
            ? reinterpret_cast<const char*>(details_raw)
            : std::string();

        json details = json::object();
        if (!details_s.empty()) {
            try {
                details = json::parse(details_s);
                if (!details.is_object()) details = json::object();
            } catch (...) {
                details = json::object();
            }
        }

        if (event_type == "share.created") {
            ++shares_created;
            continue;
        }

        if (event_type == "dropzone.created") {
            ++dropzones_created;
            continue;
        }

        if (event_type == "dropzone.uploaded") {
            ++dropzone_uploads;
            ++uploads;
            uploaded_bytes += json_int64_any_key(
                details,
                {"size_bytes", "bytes", "file_size", "upload_bytes", "uploaded_bytes", "total_bytes"}
            );
            continue;
        }

        if (event_type == "file.uploaded") {
            ++uploads;
            uploaded_bytes += json_int64_any_key(
                details,
                {"size_bytes", "bytes", "file_size", "upload_bytes", "uploaded_bytes", "total_bytes"}
            );
            continue;
        }

        if (event_type == "security.device_paired") {
            ++trusted_devices;
            continue;
        }
    }

    sqlite3_finalize(st);
    sqlite3_close(db);

    stats["shares_created_total"] = shares_created;
    stats["uploads_total"] = uploads;
    stats["uploaded_bytes_total"] = uploaded_bytes;
    stats["dropzones_created_total"] = dropzones_created;
    stats["dropzone_uploads_received_total"] = dropzone_uploads;
    stats["trusted_devices_paired_total"] = trusted_devices;
}

std::string badge_icon_key_from_id(const std::string& id) {
    if (id == "account.node_steward") return "node-steward";
    if (id == "account.established_signal") return "established-signal";
    if (id == "account.old_guard") return "old-guard";
    if (id == "account.legacy_node") return "legacy-node";

    if (id == "circlestack.first_signal") return "first-signal";
    if (id == "circlestack.signal_sender") return "signal-sender";
    if (id == "circlestack.broadcast_node") return "broadcast-node";
    if (id == "circlestack.anchor_voice") return "anchor-voice";
    if (id == "circlestack.public_voice") return "public-voice";
    if (id == "circlestack.media_runner") return "media-runner";
    if (id == "circlestack.conversation_spark") return "conversation-spark";
    if (id == "circlestack.signal_amplifier") return "signal-amplifier";
    if (id == "circlestack.crowd_spark") return "crowd-spark";
    if (id == "circlestack.thread_starter") return "thread-starter";
    if (id == "circlestack.circle_builder") return "circle-builder";

    if (id == "shares.first_share") return "first-share";
    if (id == "shares.packet_runner") return "packet-runner";
    if (id == "shares.distribution_node") return "distribution-node";

    if (id == "storage.data_seed") return "data-seed";
    if (id == "storage.vault_keeper") return "vault-keeper";
    if (id == "storage.keeper_500gb") return "keeper-500gb";
    if (id == "storage.terabyte_guardian") return "terabyte-guardian";

    if (id == "dropzone.operator") return "dropzone-operator";
    if (id == "dropzone.gatekeeper") return "gatekeeper";

    if (id == "security.trusted_device") return "trusted-device";

    return "";
}

json badge(
    const std::string& id,
    const std::string& title,
    const std::string& description,
    const std::string& icon,
    const std::string& category,
    const std::string& tier) {
    json out = {
        {"id", id},
        {"title", title},
        {"description", description},
        {"icon", icon},
        {"category", category},
        {"tier", tier},
        {"schema", "pqnas.achievements.v1"}
    };

    const std::string icon_key = badge_icon_key_from_id(id);
    if (!icon_key.empty()) {
        out["icon_key"] = icon_key;
        out["icon_asset"] = "badges/" + icon_key + ".svg";
    }

    return out;
}

void maybe_add(json& out, bool ok, const json& b) {
    if (ok) out.push_back(b);
}

bool exec_sql(sqlite3* db, const char* sql, std::string* err) {
    if (err) *err = "";
    if (!db || !sql) {
        if (err) *err = "invalid_sqlite_exec";
        return false;
    }

    char* raw_err = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &raw_err);
    if (rc != SQLITE_OK) {
        if (err) {
            *err = raw_err ? raw_err : sqlite3_errmsg(db);
        }
        if (raw_err) sqlite3_free(raw_err);
        return false;
    }

    return true;
}

bool ensure_unlock_table(sqlite3* db, std::string* err) {
    return exec_sql(db,
        "CREATE TABLE IF NOT EXISTS user_achievement_unlocks ("
        " user_fp TEXT NOT NULL,"
        " achievement_id TEXT NOT NULL,"
        " unlocked_epoch INTEGER NOT NULL,"
        " first_seen_epoch INTEGER NOT NULL DEFAULT 0,"
        " last_seen_epoch INTEGER NOT NULL DEFAULT 0,"
        " dismissed_epoch INTEGER NOT NULL DEFAULT 0,"
        " visible INTEGER NOT NULL DEFAULT 1,"
        " pinned INTEGER NOT NULL DEFAULT 0,"
        " PRIMARY KEY(user_fp, achievement_id)"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_user_achievement_unlocks_user "
        "ON user_achievement_unlocks(user_fp);",
        err);
}

bool insert_unlock_if_missing(
    sqlite3* db,
    const std::string& user_fp,
    const std::string& achievement_id,
    long long now_epoch
) {
    sqlite3_stmt* st = nullptr;
    const char* sql =
        "INSERT OR IGNORE INTO user_achievement_unlocks "
        "(user_fp, achievement_id, unlocked_epoch, first_seen_epoch, last_seen_epoch, dismissed_epoch, visible, pinned) "
        "VALUES (?, ?, ?, 0, ?, 0, 1, 0)";

    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(st, 1, user_fp.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, achievement_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 3, now_epoch);
    sqlite3_bind_int64(st, 4, now_epoch);

    const int rc = sqlite3_step(st);
    sqlite3_finalize(st);

    return rc == SQLITE_DONE;
}

bool touch_unlock_seen(
    sqlite3* db,
    const std::string& user_fp,
    const std::string& achievement_id,
    long long now_epoch
) {
    sqlite3_stmt* st = nullptr;
    const char* sql =
        "UPDATE user_achievement_unlocks "
        "SET last_seen_epoch = ?, "
        "    first_seen_epoch = CASE WHEN first_seen_epoch = 0 THEN ? ELSE first_seen_epoch END "
        "WHERE user_fp = ? AND achievement_id = ?";

    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int64(st, 1, now_epoch);
    sqlite3_bind_int64(st, 2, now_epoch);
    sqlite3_bind_text(st, 3, user_fp.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, achievement_id.c_str(), -1, SQLITE_TRANSIENT);

    const int rc = sqlite3_step(st);
    sqlite3_finalize(st);

    return rc == SQLITE_DONE;
}

long long unlock_dismissed_epoch(
    sqlite3* db,
    const std::string& user_fp,
    const std::string& achievement_id
) {
    sqlite3_stmt* st = nullptr;
    const char* sql =
        "SELECT dismissed_epoch FROM user_achievement_unlocks "
        "WHERE user_fp = ? AND achievement_id = ?";

    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
        return 0;
    }

    sqlite3_bind_text(st, 1, user_fp.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, achievement_id.c_str(), -1, SQLITE_TRANSIENT);

    long long out = 0;
    if (sqlite3_step(st) == SQLITE_ROW) {
        out = sqlite3_column_int64(st, 0);
    }

    sqlite3_finalize(st);
    return out;
}

json sync_unlock_history(
    sqlite3* db,
    const std::string& user_fp,
    const json& current_badges
) {
    json newly_unlocked = json::array();

    if (!db || user_fp.empty() || !current_badges.is_array()) {
        return newly_unlocked;
    }

    std::string err;
    if (!ensure_unlock_table(db, &err)) {
        return newly_unlocked;
    }

    const long long now_epoch = static_cast<long long>(std::time(nullptr));

    for (const auto& badge : current_badges) {
        if (!badge.is_object()) continue;

        const std::string achievement_id = badge.value("id", "");
        if (achievement_id.empty()) continue;

        if (!insert_unlock_if_missing(db, user_fp, achievement_id, now_epoch)) {
            continue;
        }

        touch_unlock_seen(db, user_fp, achievement_id, now_epoch);

        const long long dismissed = unlock_dismissed_epoch(db, user_fp, achievement_id);
        if (dismissed == 0) {
            newly_unlocked.push_back(badge);
        }
    }

    return newly_unlocked;
}

json stats_for(
    sqlite3* db,
    const std::filesystem::path& user_root,
    const std::string& fp,
    const std::string& added_at_iso,
    const std::string& role
) {
    json stats = json::object();

    stats["account_age_days"] = account_age_days(added_at_iso);
    stats["role"] = role;

    stats["posts_total"] = count_one(
        db,
        "SELECT COUNT(*) FROM posts WHERE owner_fp = ?",
        fp);

    stats["public_posts_total"] = count_one(
        db,
        "SELECT COUNT(*) FROM posts WHERE owner_fp = ? AND visibility = 'public'",
        fp);

    stats["media_posts_total"] = count_one(
        db,
        "SELECT COUNT(*) FROM posts WHERE owner_fp = ? AND media_path IS NOT NULL AND media_path <> ''",
        fp);

    stats["replies_total"] = count_one(
        db,
        "SELECT COUNT(*) FROM post_replies WHERE actor_fp = ?",
        fp);

    const long long post_reactions_given = count_one(
        db,
        "SELECT COUNT(*) FROM post_reactions WHERE actor_fp = ?",
        fp);

    const long long reply_reactions_given = count_one(
        db,
        "SELECT COUNT(*) FROM reply_reactions WHERE actor_fp = ?",
        fp);

    stats["reactions_given_total"] = post_reactions_given + reply_reactions_given;

    stats["post_reactions_received_total"] = count_two(
        db,
        "SELECT COUNT(*) FROM post_reactions "
        "WHERE post_id IN (SELECT id FROM posts WHERE owner_fp = ?) "
        "AND actor_fp <> ?",
        fp,
        fp);

    stats["replies_received_total"] = count_two(
        db,
        "SELECT COUNT(*) FROM post_replies "
        "WHERE post_id IN (SELECT id FROM posts WHERE owner_fp = ?) "
        "AND actor_fp <> ?",
        fp,
        fp);

    stats["circle_edges_total"] = count_two(
        db,
        "SELECT COUNT(*) FROM circle_edges WHERE user_a_fp = ? OR user_b_fp = ?",
        fp,
        fp);

    add_activity_stats_for_user_root(stats, user_root);

    if (dev_force_all_achievements_for_fp(fp)) {
        stats["account_age_days"] = 1001;
        stats["posts_total"] = 1000;
        stats["public_posts_total"] = 100;
        stats["media_posts_total"] = 100;
        stats["replies_total"] = 100;
        stats["reactions_given_total"] = 100;
        stats["post_reactions_received_total"] = 100;
        stats["replies_received_total"] = 100;
        stats["circle_edges_total"] = 10;

        stats["shares_created_total"] = 1000;
        stats["uploads_total"] = 1000;
        stats["uploaded_bytes_total"] = 1099511627776LL;
        stats["dropzones_created_total"] = 1;
        stats["dropzone_uploads_received_total"] = 100;
        stats["trusted_devices_paired_total"] = 1;
    }

    return stats;
}

json badges_from_stats(const json& stats) {
    json out = json::array();

    const long long account_days = stats.value("account_age_days", 0LL);
    const long long posts = stats.value("posts_total", 0LL);
    const long long public_posts = stats.value("public_posts_total", 0LL);
    const long long media_posts = stats.value("media_posts_total", 0LL);
    const long long replies = stats.value("replies_total", 0LL);
    const long long reactions_given = stats.value("reactions_given_total", 0LL);
    const long long reactions_received = stats.value("post_reactions_received_total", 0LL);
    const long long replies_received = stats.value("replies_received_total", 0LL);
    const long long circle_edges = stats.value("circle_edges_total", 0LL);
    const std::string role = stats.value("role", "");

    maybe_add(out, role == "admin",
        badge("account.node_steward", "Node Steward", "Admin or steward of this DNA-Nexus node.", "🛡️", "account", "special"));

    maybe_add(out, account_days >= 100,
        badge("account.established_signal", "Established Signal", "Account has existed for at least 100 days.", "⏳", "account", "bronze"));
    maybe_add(out, account_days >= 500,
        badge("account.old_guard", "Old Guard", "Account has existed for at least 500 days.", "🏛️", "account", "gold"));
    maybe_add(out, account_days >= 1000,
        badge("account.legacy_node", "Legacy Node", "Account has existed for at least 1000 days.", "💎", "account", "legendary"));

    maybe_add(out, posts >= 1,
        badge("circlestack.first_signal", "First Signal", "Created the first Circle Stack post.", "📡", "circlestack", "bronze"));
    maybe_add(out, posts >= 100,
        badge("circlestack.signal_sender", "Signal Sender", "Created at least 100 Circle Stack posts.", "📣", "circlestack", "silver"));
    maybe_add(out, posts >= 500,
        badge("circlestack.broadcast_node", "Broadcast Node", "Created at least 500 Circle Stack posts.", "📻", "circlestack", "gold"));
    maybe_add(out, posts >= 1000,
        badge("circlestack.anchor_voice", "Anchor Voice", "Created at least 1000 Circle Stack posts.", "🛰️", "circlestack", "legendary"));

    maybe_add(out, public_posts >= 100,
        badge("circlestack.public_voice", "Public Voice", "Created at least 100 public Circle Stack posts.", "🌍", "circlestack", "silver"));
    maybe_add(out, media_posts >= 100,
        badge("circlestack.media_runner", "Media Runner", "Created at least 100 media posts.", "🖼️", "circlestack", "silver"));
    maybe_add(out, replies >= 100,
        badge("circlestack.conversation_spark", "Conversation Spark", "Wrote at least 100 replies.", "💬", "social", "silver"));
    maybe_add(out, reactions_given >= 100,
        badge("circlestack.signal_amplifier", "Signal Amplifier", "Reacted at least 100 times.", "⚡", "social", "silver"));
    maybe_add(out, reactions_received >= 100,
        badge("circlestack.crowd_spark", "Crowd Spark", "Received at least 100 reactions on posts.", "🔥", "social", "gold"));
    maybe_add(out, replies_received >= 100,
        badge("circlestack.thread_starter", "Thread Starter", "Received at least 100 replies on posts.", "🧵", "social", "gold"));
    maybe_add(out, circle_edges >= 10,
        badge("circlestack.circle_builder", "Circle Builder", "Connected with at least 10 Circle members.", "🫂", "social", "silver"));

    const long long shares_created = stats.value("shares_created_total", 0LL);
    const long long uploaded_bytes = stats.value("uploaded_bytes_total", 0LL);
    const long long dropzones_created = stats.value("dropzones_created_total", 0LL);
    const long long dropzone_uploads = stats.value("dropzone_uploads_received_total", 0LL);
    const long long trusted_devices = stats.value("trusted_devices_paired_total", 0LL);

    maybe_add(out, shares_created >= 1,
        badge("shares.first_share", "First Share", "Created the first share link.", "🔗", "sharing", "bronze"));
    maybe_add(out, shares_created >= 100,
        badge("shares.packet_runner", "Packet Runner", "Created at least 100 share links.", "📦", "sharing", "silver"));
    maybe_add(out, shares_created >= 1000,
        badge("shares.distribution_node", "Distribution Node", "Created at least 1000 share links.", "🛰️", "sharing", "legendary"));

    maybe_add(out, uploaded_bytes >= 10LL * 1024LL * 1024LL * 1024LL,
        badge("storage.data_seed", "Data Seed", "Uploaded at least 10 GB.", "🌱", "storage", "bronze"));
    maybe_add(out, uploaded_bytes >= 100LL * 1024LL * 1024LL * 1024LL,
        badge("storage.vault_keeper", "Vault Keeper", "Uploaded at least 100 GB.", "🧰", "storage", "silver"));
    maybe_add(out, uploaded_bytes >= 500LL * 1024LL * 1024LL * 1024LL,
        badge("storage.keeper_500gb", "500GB Keeper", "Uploaded at least 500 GB.", "💾", "storage", "gold"));
    maybe_add(out, uploaded_bytes >= 1024LL * 1024LL * 1024LL * 1024LL,
        badge("storage.terabyte_guardian", "Terabyte Guardian", "Uploaded at least 1 TB.", "🛡️", "storage", "legendary"));

    maybe_add(out, dropzones_created >= 1,
        badge("dropzone.operator", "Drop Zone Operator", "Created the first Drop Zone.", "📥", "dropzone", "bronze"));
    maybe_add(out, dropzone_uploads >= 100,
        badge("dropzone.gatekeeper", "Gatekeeper", "Received at least 100 Drop Zone uploads.", "🚪", "dropzone", "gold"));

    maybe_add(out, trusted_devices >= 1,
        badge("security.trusted_device", "Trusted Device", "Paired the first trusted device.", "🔐", "security", "bronze"));

    return out;
}

} // namespace

json circle_stack_public_badges(
    sqlite3* circle_db,
    const std::string& user_fp,
    const std::string& added_at_iso,
    const std::string& role) {
    return circle_stack_public_badges(circle_db, std::filesystem::path(), user_fp, added_at_iso, role);
}

json circle_stack_public_badges(
    sqlite3* circle_db,
    const std::filesystem::path& user_root,
    const std::string& user_fp,
    const std::string& added_at_iso,
    const std::string& role) {
    if (user_fp.empty()) return json::array();
    return badges_from_stats(stats_for(circle_db, user_root, user_fp, added_at_iso, role));
}

json circle_stack_profile_json(
    sqlite3* circle_db,
    const std::string& user_fp,
    const std::string& added_at_iso,
    const std::string& role,
    bool include_private_stats) {
    return circle_stack_profile_json(
        circle_db,
        std::filesystem::path(),
        user_fp,
        added_at_iso,
        role,
        include_private_stats
    );
}

json circle_stack_profile_json(
    sqlite3* circle_db,
    const std::filesystem::path& user_root,
    const std::string& user_fp,
    const std::string& added_at_iso,
    const std::string& role,
    bool include_private_stats) {
    json out;
    out["ok"] = true;
    out["schema"] = "pqnas.achievements.v1";
    out["user_fp"] = user_fp;

    const json stats = stats_for(circle_db, user_root, user_fp, added_at_iso, role);
    const json achievements = badges_from_stats(stats);

    out["achievements"] = achievements;
    out["newly_unlocked"] = include_private_stats
        ? sync_unlock_history(circle_db, user_fp, achievements)
        : json::array();

    if (include_private_stats) {
        out["stats"] = stats;
    }

    return out;
}

bool mark_achievement_dismissed(
    sqlite3* circle_db,
    const std::string& user_fp,
    const std::string& achievement_id,
    std::string* err
) {
    if (err) *err = "";

    if (!circle_db) {
        if (err) *err = "circle_db_unavailable";
        return false;
    }

    if (user_fp.empty() || achievement_id.empty()) {
        if (err) *err = "missing_user_or_achievement";
        return false;
    }

    if (!ensure_unlock_table(circle_db, err)) {
        return false;
    }

    const long long now_epoch = static_cast<long long>(std::time(nullptr));

    sqlite3_stmt* st = nullptr;
    const char* sql =
        "INSERT INTO user_achievement_unlocks "
        "(user_fp, achievement_id, unlocked_epoch, first_seen_epoch, last_seen_epoch, dismissed_epoch, visible, pinned) "
        "VALUES (?, ?, ?, ?, ?, ?, 1, 0) "
        "ON CONFLICT(user_fp, achievement_id) DO UPDATE SET "
        " first_seen_epoch = CASE "
        "   WHEN user_achievement_unlocks.first_seen_epoch = 0 THEN excluded.first_seen_epoch "
        "   ELSE user_achievement_unlocks.first_seen_epoch END,"
        " last_seen_epoch = excluded.last_seen_epoch,"
        " dismissed_epoch = CASE "
        "   WHEN user_achievement_unlocks.dismissed_epoch = 0 THEN excluded.dismissed_epoch "
        "   ELSE user_achievement_unlocks.dismissed_epoch END";

    if (sqlite3_prepare_v2(circle_db, sql, -1, &st, nullptr) != SQLITE_OK) {
        if (err) *err = sqlite3_errmsg(circle_db);
        return false;
    }

    sqlite3_bind_text(st, 1, user_fp.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, achievement_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 3, now_epoch);
    sqlite3_bind_int64(st, 4, now_epoch);
    sqlite3_bind_int64(st, 5, now_epoch);
    sqlite3_bind_int64(st, 6, now_epoch);

    const int rc = sqlite3_step(st);
    sqlite3_finalize(st);

    if (rc != SQLITE_DONE) {
        if (err) *err = sqlite3_errmsg(circle_db);
        return false;
    }

    return true;
}

} // namespace pqnas::achievements
