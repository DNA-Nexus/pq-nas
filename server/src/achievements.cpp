#include "achievements.h"

#include <ctime>
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

json stats_for(sqlite3* db, const std::string& fp, const std::string& added_at_iso, const std::string& role) {
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

    return out;
}

} // namespace

json circle_stack_public_badges(
    sqlite3* circle_db,
    const std::string& user_fp,
    const std::string& added_at_iso,
    const std::string& role) {
    if (user_fp.empty()) return json::array();
    return badges_from_stats(stats_for(circle_db, user_fp, added_at_iso, role));
}

json circle_stack_profile_json(
    sqlite3* circle_db,
    const std::string& user_fp,
    const std::string& added_at_iso,
    const std::string& role,
    bool include_private_stats) {
    json out;
    out["ok"] = true;
    out["schema"] = "pqnas.achievements.v1";
    out["user_fp"] = user_fp;

    const json stats = stats_for(circle_db, user_fp, added_at_iso, role);
    out["achievements"] = badges_from_stats(stats);

    if (include_private_stats) {
        out["stats"] = stats;
    }

    return out;
}

} // namespace pqnas::achievements
