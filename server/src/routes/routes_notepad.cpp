#include "routes_notepad.h"

#include "httplib.h"
#include "notepad_store.h"
#include "users_registry.h"

#include <algorithm>
#include <ctime>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace pqnas {
namespace {

using json = nlohmann::json;

constexpr std::size_t kNotepadMaxRequestBytes = 96 * 1024;
constexpr std::int64_t kNotepadRateWindowSeconds = 60;
constexpr int kNotepadPostRateLimit = 90;
constexpr std::size_t kNotepadRateBucketCap = 10000;

struct NotepadMark {
    std::int64_t start = 0;
    std::int64_t end = 0;
    std::string color;
};

std::mutex g_notepad_rate_mu;
std::unordered_map<std::string, std::deque<std::int64_t>> g_notepad_rate;

std::int64_t now_epoch_fallback() {
    return static_cast<std::int64_t>(std::time(nullptr));
}

void reply_json_notepad(const NotepadRoutesDeps& deps,
                        httplib::Response& res,
                        int status,
                        const json& body) {
    if (deps.reply_json) {
        deps.reply_json(res, status, body.dump());
        return;
    }

    res.status = status;
    res.set_content(body.dump(), "application/json; charset=utf-8");
}

bool mark_color_ok(const std::string& color) {
    return color == "yellow" ||
           color == "green" ||
           color == "blue" ||
           color == "red" ||
           color == "purple";
}

json sanitize_marks_json(const json& in, const std::string& body) {
    json out = json::array();
    if (!in.is_array()) return out;

    const std::int64_t body_len = static_cast<std::int64_t>(body.size());
    if (body_len <= 0) return out;

    std::vector<NotepadMark> marks;
    marks.reserve(std::min<std::size_t>(in.size(), kNotepadMaxMarks));

    for (const auto& item : in) {
        if (marks.size() >= kNotepadMaxMarks) break;
        if (!item.is_object()) continue;
        if (!item.contains("start") || !item["start"].is_number_integer()) continue;
        if (!item.contains("end") || !item["end"].is_number_integer()) continue;
        if (!item.contains("color") || !item["color"].is_string()) continue;

        NotepadMark m;
        m.start = item["start"].get<std::int64_t>();
        m.end = item["end"].get<std::int64_t>();
        m.color = item["color"].get<std::string>();

        if (!mark_color_ok(m.color)) continue;
        if (m.start < 0 || m.end <= m.start || m.end > body_len) continue;

        marks.push_back(m);
    }

    std::sort(marks.begin(), marks.end(), [](const NotepadMark& a, const NotepadMark& b) {
        if (a.start != b.start) return a.start < b.start;
        return a.end < b.end;
    });

    std::int64_t cursor = 0;
    for (const auto& m : marks) {
        if (m.start < cursor) continue;
        cursor = m.end;
        out.push_back(json{
            {"start", m.start},
            {"end", m.end},
            {"color", m.color}
        });
    }

    return out;
}

json parse_stored_marks_json(const std::string& raw, const std::string& body) {
    try {
        if (raw.empty()) return json::array();
        return sanitize_marks_json(json::parse(raw), body);
    } catch (...) {
        return json::array();
    }
}

json note_to_json(const NotepadNoteRec& note) {
    return json{
        {"body", note.body},
        {"marks", parse_stored_marks_json(note.marks_json, note.body)},
        {"revision", note.revision},
        {"updated_at", note.updated_at_epoch},
        {"max_body_bytes", kNotepadMaxBodyBytes},
        {"max_marks", kNotepadMaxMarks}
    };
}

bool context_ok(const NotepadRoutesDeps& deps) {
    return deps.store &&
           deps.users &&
           deps.cookie_key &&
           deps.require_user_auth_users_actor &&
           deps.require_same_origin &&
           deps.reply_json &&
           deps.user_dir_for_fp;
}

std::string rate_key(const httplib::Request& req,
                     const std::string& actor_fp,
                     const std::string& action) {
    std::string ip = req.remote_addr.empty() ? "?" : req.remote_addr;
    return action + "|" + actor_fp + "|" + ip;
}

void compact_rate_map_locked(std::int64_t now_epoch) {
    const std::int64_t min_epoch = now_epoch - kNotepadRateWindowSeconds;

    for (auto it = g_notepad_rate.begin(); it != g_notepad_rate.end(); ) {
        auto& hits = it->second;
        while (!hits.empty() && hits.front() < min_epoch) {
            hits.pop_front();
        }

        if (hits.empty()) {
            it = g_notepad_rate.erase(it);
        } else {
            ++it;
        }
    }
}

bool is_rate_limited(const httplib::Request& req,
                     const std::string& actor_fp,
                     const std::string& action,
                     std::int64_t now_epoch,
                     int limit) {
    const std::string key = rate_key(req, actor_fp, action);
    const std::int64_t min_epoch = now_epoch - kNotepadRateWindowSeconds;

    std::lock_guard<std::mutex> lock(g_notepad_rate_mu);

    if (g_notepad_rate.size() >= kNotepadRateBucketCap) {
        compact_rate_map_locked(now_epoch);
    }

    auto found = g_notepad_rate.find(key);
    if (g_notepad_rate.size() >= kNotepadRateBucketCap && found == g_notepad_rate.end()) {
        return true;
    }

    auto& hits = g_notepad_rate[key];
    while (!hits.empty() && hits.front() < min_epoch) {
        hits.pop_front();
    }

    if (static_cast<int>(hits.size()) >= limit) {
        return true;
    }

    hits.push_back(now_epoch);
    return false;
}

bool require_actor(const NotepadRoutesDeps& deps,
                   const httplib::Request& req,
                   httplib::Response& res,
                   std::string* actor_fp,
                   std::string* actor_role) {
    if (!deps.require_user_auth_users_actor(
            req,
            res,
            deps.cookie_key,
            deps.users,
            actor_fp,
            actor_role)) {
        return false;
    }

    if (!actor_fp || actor_fp->empty()) {
        reply_json_notepad(deps, res, 401, {
            {"ok", false},
            {"error", "unauthorized"},
            {"message", "authentication required"}
        });
        return false;
    }

    return true;
}

std::int64_t now_epoch(const NotepadRoutesDeps& deps) {
    if (deps.now_epoch_sec) return deps.now_epoch_sec();
    return now_epoch_fallback();
}

bool resolve_actor_user_dir(const NotepadRoutesDeps& deps,
                            const std::string& actor_fp,
                            std::filesystem::path* out,
                            httplib::Response& res) {
    if (!out) {
        reply_json_notepad(deps, res, 500, {
            {"ok", false},
            {"error", "server_error"},
            {"message", "missing output path"}
        });
        return false;
    }

    try {
        *out = deps.user_dir_for_fp(actor_fp);
    } catch (...) {
        reply_json_notepad(deps, res, 500, {
            {"ok", false},
            {"error", "server_error"},
            {"message", "failed to resolve user storage"}
        });
        return false;
    }

    if (out->empty()) {
        reply_json_notepad(deps, res, 500, {
            {"ok", false},
            {"error", "server_error"},
            {"message", "user storage not resolved"}
        });
        return false;
    }

    return true;
}

} // namespace

void register_notepad_routes(httplib::Server& srv, const NotepadRoutesDeps& deps) {
    const NotepadRoutesDeps c = deps;

    srv.Get("/api/v4/notepad",
        [c](const httplib::Request& req, httplib::Response& res) {
            if (!context_ok(c)) {
                reply_json_notepad(c, res, 500, {
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "notepad route context incomplete"}
                });
                return;
            }

            std::string actor_fp;
            std::string actor_role;
            if (!require_actor(c, req, res, &actor_fp, &actor_role)) return;

            std::filesystem::path user_dir;
            if (!resolve_actor_user_dir(c, actor_fp, &user_dir, res)) return;

            std::string err;
            auto note = c.store->get_note(actor_fp, user_dir, &err);
            if (!err.empty()) {
                reply_json_notepad(c, res, 500, {
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "notepad load failed"}
                });
                return;
            }

            if (!note) {
                reply_json_notepad(c, res, 200, {
                    {"ok", true},
                    {"body", ""},
                    {"marks", json::array()},
                    {"revision", 0},
                    {"updated_at", 0},
                    {"max_body_bytes", kNotepadMaxBodyBytes},
                    {"max_marks", kNotepadMaxMarks}
                });
                return;
            }

            json out = note_to_json(*note);
            out["ok"] = true;
            reply_json_notepad(c, res, 200, out);
        }
    );

    srv.Post("/api/v4/notepad",
        [c](const httplib::Request& req, httplib::Response& res) {
            if (!context_ok(c)) {
                reply_json_notepad(c, res, 500, {
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "notepad route context incomplete"}
                });
                return;
            }

            std::string actor_fp;
            std::string actor_role;
            if (!require_actor(c, req, res, &actor_fp, &actor_role)) return;

            if (!c.require_same_origin(req, res)) return;

            const std::int64_t now = now_epoch(c);
            if (is_rate_limited(req, actor_fp, "notepad_save", now, kNotepadPostRateLimit)) {
                reply_json_notepad(c, res, 429, {
                    {"ok", false},
                    {"error", "rate_limited"},
                    {"message", "too many notepad saves"}
                });
                return;
            }

            if (req.body.size() > kNotepadMaxRequestBytes) {
                reply_json_notepad(c, res, 413, {
                    {"ok", false},
                    {"error", "too_large"},
                    {"message", "notepad payload too large"}
                });
                return;
            }

            json j;
            try {
                j = json::parse(req.body.empty() ? "{}" : req.body);
            } catch (...) {
                reply_json_notepad(c, res, 400, {
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "invalid json"}
                });
                return;
            }

            if (!j.is_object() || !j.contains("body") || !j["body"].is_string()) {
                reply_json_notepad(c, res, 400, {
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "body must be a string"}
                });
                return;
            }

            if (!j.contains("revision") || !j["revision"].is_number_integer()) {
                reply_json_notepad(c, res, 400, {
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "revision must be an integer"}
                });
                return;
            }

            if (j.contains("marks") && !j["marks"].is_array()) {
                reply_json_notepad(c, res, 400, {
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "marks must be an array"}
                });
                return;
            }

            const std::string body = j["body"].get<std::string>();
            if (body.size() > kNotepadMaxBodyBytes) {
                reply_json_notepad(c, res, 413, {
                    {"ok", false},
                    {"error", "too_large"},
                    {"message", "notepad body too large"},
                    {"max_body_bytes", kNotepadMaxBodyBytes}
                });
                return;
            }

            const json marks = sanitize_marks_json(j.value("marks", json::array()), body);
            const std::string marks_json = marks.dump();
            if (marks_json.size() > kNotepadMaxMarksJsonBytes) {
                reply_json_notepad(c, res, 413, {
                    {"ok", false},
                    {"error", "too_large"},
                    {"message", "notepad marks too large"},
                    {"max_marks", kNotepadMaxMarks}
                });
                return;
            }

            const std::int64_t revision = j["revision"].get<std::int64_t>();

            std::filesystem::path user_dir;
            if (!resolve_actor_user_dir(c, actor_fp, &user_dir, res)) return;

            NotepadNoteRec saved;
            bool revision_mismatch = false;
            std::string err;

            const bool ok = c.store->save_note(
                actor_fp,
                user_dir,
                body,
                marks_json,
                revision,
                now,
                &saved,
                &revision_mismatch,
                &err
            );

            if (!ok && revision_mismatch) {
                json current = note_to_json(saved);
                reply_json_notepad(c, res, 409, {
                    {"ok", false},
                    {"error", "revision_mismatch"},
                    {"message", "notepad changed elsewhere"},
                    {"current", current}
                });
                return;
            }

            if (!ok) {
                reply_json_notepad(c, res, 500, {
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "notepad save failed"}
                });
                return;
            }

            json out = note_to_json(saved);
            out["ok"] = true;
            reply_json_notepad(c, res, 200, out);
        }
    );
}

} // namespace pqnas
