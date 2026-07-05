
#include "routes_people.h"

#include "people_contacts.h"

#include <chrono>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace pqnas {
namespace {

using json = nlohmann::json;

void reply_json_local(const PeopleRoutesDeps& deps,
                      httplib::Response& res,
                      int code,
                      const json& body) {
    if (deps.reply_json) {
        deps.reply_json(res, code, body.dump());
        return;
    }

    res.status = code;
    res.set_content(body.dump(), "application/json; charset=utf-8");
}

bool deps_ok_local(const PeopleRoutesDeps& deps) {
    return deps.users &&
           deps.cookie_key &&
           deps.require_user_auth_users_actor &&
           deps.reply_json &&
           !deps.people_db_path.empty();
}

bool require_actor_local(const PeopleRoutesDeps& deps,
                         const httplib::Request& req,
                         httplib::Response& res,
                         std::string* actor_fp,
                         std::string* actor_role) {
    if (!deps_ok_local(deps)) {
        reply_json_local(deps, res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "people route dependencies missing"}
        });
        return false;
    }

    return deps.require_user_auth_users_actor(
        req,
        res,
        deps.cookie_key,
        deps.users,
        actor_fp,
        actor_role);
}

json contact_to_json_local(const PeopleContactRecord& c) {
    return json{
        {"id", c.id},
        {"subject_user_id", c.subject_user_id},
        {"subject_fingerprint", c.subject_fingerprint},
        {"subject_fingerprint_short", people_fingerprint_short(c.subject_fingerprint)},
        {"subject_kind", c.subject_kind},

        {"display_name", c.display_name},
        {"nickname", c.nickname},
        {"contact_type", c.contact_type},
        {"company", c.company},
        {"title", c.title},

        {"email", c.email},
        {"phone", c.phone},
        {"mobile", c.mobile},
        {"website", c.website},

        {"street", c.street},
        {"postal_code", c.postal_code},
        {"city", c.city},
        {"country", c.country},

        {"delivery_name", c.delivery_name},
        {"delivery_street", c.delivery_street},
        {"delivery_postal_code", c.delivery_postal_code},
        {"delivery_city", c.delivery_city},
        {"delivery_country", c.delivery_country},

        {"tags", c.tags},
        {"status", c.status},
        {"notes", c.notes},

        {"created_at_epoch", c.created_at_epoch},
        {"updated_at_epoch", c.updated_at_epoch}
    };
}

std::string json_string_local(const json& j, const char* key) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_string()) return {};
    return it->get<std::string>();
}

std::string people_request_ip_local(const httplib::Request& req) {
    return req.remote_addr.empty() ? "?" : req.remote_addr;
}

bool people_rate_limit_allow_local(const std::string& route,
                                  const std::string& actor_fp,
                                  const std::string& ip,
                                  int max_hits,
                                  std::chrono::seconds window) {
    using Clock = std::chrono::steady_clock;

    static std::mutex mu;
    static std::unordered_map<std::string, std::deque<Clock::time_point>> hits;

    const auto now = Clock::now();
    const auto cutoff = now - window;
    const std::string actor = actor_fp.empty() ? "?" : actor_fp;
    const std::string remote = ip.empty() ? "?" : ip;
    const std::string key = route + "\n" + actor + "\n" + remote;

    std::lock_guard<std::mutex> lock(mu);

    auto& q = hits[key];
    while (!q.empty() && q.front() < cutoff) q.pop_front();

    if (static_cast<int>(q.size()) >= max_hits) return false;

    q.push_back(now);

    if (hits.size() > 4096) {
        for (auto it = hits.begin(); it != hits.end(); ) {
            auto& bucket = it->second;
            while (!bucket.empty() && bucket.front() < cutoff) bucket.pop_front();

            if (bucket.empty()) {
                it = hits.erase(it);
            } else {
                ++it;
            }
        }
    }

    return true;
}

void reply_people_rate_limited_local(const PeopleRoutesDeps& deps, httplib::Response& res) {
    reply_json_local(deps, res, 429, json{
        {"ok", false},
        {"error", "rate_limited"},
        {"message", "too many requests"}
    });
}

bool reject_people_body_too_large_local(const PeopleRoutesDeps& deps,
                                       const httplib::Request& req,
                                       httplib::Response& res,
                                       std::size_t max_bytes) {
    if (req.body.size() <= max_bytes) return false;

    reply_json_local(deps, res, 413, json{
        {"ok", false},
        {"error", "payload_too_large"},
        {"message", "request body too large"}
    });
    return true;
}

bool people_check_len_local(const std::string& value,
                            std::size_t max_bytes,
                            const char* field,
                            std::string* bad_field) {
    if (value.size() <= max_bytes) return true;
    if (bad_field) *bad_field = field ? field : "field";
    return false;
}


bool contacts_app_available_local(const PeopleRoutesDeps& deps) {
    // Default-open keeps legacy tests/dev wiring alive if the callback is not provided.
    // main.cpp wires this in production so uninstalling Contacts disables full Contacts APIs.
    if (!deps.contacts_app_available) return true;
    return deps.contacts_app_available();
}

bool reject_contacts_app_unavailable_local(const PeopleRoutesDeps& deps, httplib::Response& res) {
    if (contacts_app_available_local(deps)) return false;

    reply_json_local(deps, res, 403, json{
        {"ok", false},
        {"error", "app_disabled"},
        {"app", "contacts"},
        {"message", "Contacts app is not installed or enabled"}
    });
    return true;
}

bool people_contact_lengths_ok_local(const PeopleContactRecord& input, std::string* bad_field) {
    constexpr std::size_t kTiny = 64;
    constexpr std::size_t kShort = 256;
    constexpr std::size_t kMedium = 512;
    constexpr std::size_t kLong = 1024;
    constexpr std::size_t kNotes = 8192;

    return
        people_check_len_local(input.subject_user_id, kShort, "subject_user_id", bad_field) &&
        people_check_len_local(input.subject_fingerprint, 128, "subject_fingerprint", bad_field) &&
        people_check_len_local(input.subject_kind, kTiny, "subject_kind", bad_field) &&
        people_check_len_local(input.display_name, kShort, "display_name", bad_field) &&
        people_check_len_local(input.nickname, kShort, "nickname", bad_field) &&
        people_check_len_local(input.contact_type, kTiny, "contact_type", bad_field) &&
        people_check_len_local(input.company, kShort, "company", bad_field) &&
        people_check_len_local(input.title, kShort, "title", bad_field) &&
        people_check_len_local(input.email, kMedium, "email", bad_field) &&
        people_check_len_local(input.phone, kShort, "phone", bad_field) &&
        people_check_len_local(input.mobile, kShort, "mobile", bad_field) &&
        people_check_len_local(input.website, kMedium, "website", bad_field) &&
        people_check_len_local(input.street, kMedium, "street", bad_field) &&
        people_check_len_local(input.postal_code, kShort, "postal_code", bad_field) &&
        people_check_len_local(input.city, kShort, "city", bad_field) &&
        people_check_len_local(input.country, kShort, "country", bad_field) &&
        people_check_len_local(input.delivery_name, kShort, "delivery_name", bad_field) &&
        people_check_len_local(input.delivery_street, kMedium, "delivery_street", bad_field) &&
        people_check_len_local(input.delivery_postal_code, kShort, "delivery_postal_code", bad_field) &&
        people_check_len_local(input.delivery_city, kShort, "delivery_city", bad_field) &&
        people_check_len_local(input.delivery_country, kShort, "delivery_country", bad_field) &&
        people_check_len_local(input.tags, kLong, "tags", bad_field) &&
        people_check_len_local(input.status, kTiny, "status", bad_field) &&
        people_check_len_local(input.notes, kNotes, "notes", bad_field);
}

} // namespace

void register_people_routes(httplib::Server& srv, const PeopleRoutesDeps& deps) {
    srv.Get("/api/v4/people/local-users", [deps](const httplib::Request& req, httplib::Response& res) {
        std::string actor_fp;
        std::string actor_role;
        if (!require_actor_local(deps, req, res, &actor_fp, &actor_role)) return;

        actor_fp = people_canonical_fingerprint(actor_fp);
        if (!people_valid_fingerprint(actor_fp)) {
            reply_json_local(deps, res, 403, json{
                {"ok", false},
                {"error", "forbidden"},
                {"message", "invalid authenticated fingerprint"}
            });
            return;
        }

        if (reject_contacts_app_unavailable_local(deps, res)) return;

        json arr = json::array();
        const auto snapshot = deps.users->snapshot();

        for (const auto& kv : snapshot) {
            const UserRec& u = kv.second;

            if (u.status != "enabled") continue;

            std::string fp = !u.fingerprint.empty() ? u.fingerprint : kv.first;
            fp = people_canonical_fingerprint(fp);

            if (!people_valid_fingerprint(fp)) continue;
            if (fp == actor_fp) continue;

            std::string display_name = u.name;
            if (display_name.empty()) display_name = people_fingerprint_short(fp);

            arr.push_back(json{
                {"fingerprint", fp},
                {"fingerprint_short", people_fingerprint_short(fp)},
                {"display_name", display_name},
                {"subject_kind", "local_user"}
            });
        }

        reply_json_local(deps, res, 200, json{
            {"ok", true},
            {"candidates", arr},
            {"count", arr.size()}
        });
    });

    srv.Get("/api/v4/people/list", [deps](const httplib::Request& req, httplib::Response& res) {
        std::string actor_fp;
        std::string actor_role;
        if (!require_actor_local(deps, req, res, &actor_fp, &actor_role)) return;

        actor_fp = people_canonical_fingerprint(actor_fp);
        if (!people_valid_fingerprint(actor_fp)) {
            reply_json_local(deps, res, 403, json{
                {"ok", false},
                {"error", "forbidden"},
                {"message", "invalid authenticated fingerprint"}
            });
            return;
        }

        if (reject_contacts_app_unavailable_local(deps, res)) return;

        PeopleContactsStore store(deps.people_db_path);
        std::vector<PeopleContactRecord> contacts;
        std::string err;
        if (!store.list_for_owner(actor_fp, &contacts, &err)) {
            reply_json_local(deps, res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to list people"}
            });
            return;
        }

        json arr = json::array();
        for (const auto& c : contacts) arr.push_back(contact_to_json_local(c));

        reply_json_local(deps, res, 200, json{
            {"ok", true},
            {"contacts", arr},
            {"count", arr.size()}
        });
    });

    srv.Get("/api/v4/people/resolve", [deps](const httplib::Request& req, httplib::Response& res) {
        std::string actor_fp;
        std::string actor_role;
        if (!require_actor_local(deps, req, res, &actor_fp, &actor_role)) return;

        actor_fp = people_canonical_fingerprint(actor_fp);
        const std::string subject_fp = people_canonical_fingerprint(
            req.has_param("fingerprint") ? req.get_param_value("fingerprint") : "");

        if (!people_valid_fingerprint(actor_fp) || !people_valid_fingerprint(subject_fp)) {
            reply_json_local(deps, res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "invalid fingerprint"}
            });
            return;
        }

        PeopleContactsStore store(deps.people_db_path);
        std::optional<PeopleContactRecord> found;
        std::string err;
        if (!store.find_for_owner(actor_fp, subject_fp, &found, &err)) {
            reply_json_local(deps, res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to resolve person"}
            });
            return;
        }

        if (found.has_value()) {
            reply_json_local(deps, res, 200, json{
                {"ok", true},
                {"resolved", true},
                {"source", "people"},
                {"person", contact_to_json_local(*found)}
            });
            return;
        }

        reply_json_local(deps, res, 200, json{
            {"ok", true},
            {"resolved", false},
            {"source", "fingerprint"},
            {"person", {
                {"subject_fingerprint", subject_fp},
                {"subject_fingerprint_short", people_fingerprint_short(subject_fp)},
                {"subject_kind", "fingerprint"},
                {"display_name", people_fingerprint_short(subject_fp)}
            }}
        });
    });

    srv.Post("/api/v4/people/upsert", [deps](const httplib::Request& req, httplib::Response& res) {
        std::string actor_fp;
        std::string actor_role;
        if (!require_actor_local(deps, req, res, &actor_fp, &actor_role)) return;

        actor_fp = people_canonical_fingerprint(actor_fp);

        if (!people_valid_fingerprint(actor_fp)) {
            reply_json_local(deps, res, 403, json{
                {"ok", false},
                {"error", "forbidden"},
                {"message", "invalid authenticated fingerprint"}
            });
            return;
        }

        if (reject_contacts_app_unavailable_local(deps, res)) return;

        if (!people_rate_limit_allow_local(
                "people.upsert",
                actor_fp,
                people_request_ip_local(req),
                30,
                std::chrono::minutes(1))) {
            reply_people_rate_limited_local(deps, res);
            return;
        }

        if (reject_people_body_too_large_local(deps, req, res, 64 * 1024)) return;

        json body;
        try {
            body = json::parse(req.body.empty() ? "{}" : req.body);
        } catch (...) {
            reply_json_local(deps, res, 400, json{
                {"ok", false},
                {"error", "bad_json"},
                {"message", "invalid JSON body"}
            });
            return;
        }

        PeopleContactRecord input;
        input.subject_user_id = json_string_local(body, "subject_user_id");
        input.subject_fingerprint = json_string_local(body, "subject_fingerprint");
        if (input.subject_fingerprint.empty()) {
            input.subject_fingerprint = json_string_local(body, "fingerprint");
        }

        input.subject_kind = json_string_local(body, "subject_kind");

        input.display_name = json_string_local(body, "display_name");
        input.nickname = json_string_local(body, "nickname");
        input.contact_type = json_string_local(body, "contact_type");
        input.company = json_string_local(body, "company");
        input.title = json_string_local(body, "title");

        input.email = json_string_local(body, "email");
        input.phone = json_string_local(body, "phone");
        input.mobile = json_string_local(body, "mobile");
        input.website = json_string_local(body, "website");

        input.street = json_string_local(body, "street");
        input.postal_code = json_string_local(body, "postal_code");
        input.city = json_string_local(body, "city");
        input.country = json_string_local(body, "country");

        input.delivery_name = json_string_local(body, "delivery_name");
        input.delivery_street = json_string_local(body, "delivery_street");
        input.delivery_postal_code = json_string_local(body, "delivery_postal_code");
        input.delivery_city = json_string_local(body, "delivery_city");
        input.delivery_country = json_string_local(body, "delivery_country");

        input.tags = json_string_local(body, "tags");
        input.status = json_string_local(body, "status");
        input.notes = json_string_local(body, "notes");

        input.subject_fingerprint = people_canonical_fingerprint(input.subject_fingerprint);

        std::string bad_field;
        if (!people_contact_lengths_ok_local(input, &bad_field)) {
            reply_json_local(deps, res, 400, json{
                {"ok", false},
                {"error", "field_too_large"},
                {"message", "contact field is too large"},
                {"field", bad_field}
            });
            return;
        }

        if (!people_valid_fingerprint(actor_fp) || !people_valid_fingerprint(input.subject_fingerprint)) {
            reply_json_local(deps, res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "invalid fingerprint"}
            });
            return;
        }

        PeopleContactsStore store(deps.people_db_path);
        PeopleContactRecord saved;
        std::string err;
        if (!store.upsert_for_owner(actor_fp, input, &saved, &err)) {
            reply_json_local(deps, res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to save person"}
            });
            return;
        }

        reply_json_local(deps, res, 200, json{
            {"ok", true},
            {"contact", contact_to_json_local(saved)}
        });
    });

    srv.Post("/api/v4/people/delete", [deps](const httplib::Request& req, httplib::Response& res) {
        std::string actor_fp;
        std::string actor_role;
        if (!require_actor_local(deps, req, res, &actor_fp, &actor_role)) return;

        actor_fp = people_canonical_fingerprint(actor_fp);

        if (!people_valid_fingerprint(actor_fp)) {
            reply_json_local(deps, res, 403, json{
                {"ok", false},
                {"error", "forbidden"},
                {"message", "invalid authenticated fingerprint"}
            });
            return;
        }

        if (reject_contacts_app_unavailable_local(deps, res)) return;

        if (!people_rate_limit_allow_local(
                "people.delete",
                actor_fp,
                people_request_ip_local(req),
                60,
                std::chrono::minutes(1))) {
            reply_people_rate_limited_local(deps, res);
            return;
        }

        if (reject_people_body_too_large_local(deps, req, res, 16 * 1024)) return;

        json body;
        try {
            body = json::parse(req.body.empty() ? "{}" : req.body);
        } catch (...) {
            reply_json_local(deps, res, 400, json{
                {"ok", false},
                {"error", "bad_json"},
                {"message", "invalid JSON body"}
            });
            return;
        }

        std::string subject_fp = json_string_local(body, "subject_fingerprint");
        if (subject_fp.empty()) subject_fp = json_string_local(body, "fingerprint");
        subject_fp = people_canonical_fingerprint(subject_fp);

        if (!people_valid_fingerprint(actor_fp) || !people_valid_fingerprint(subject_fp)) {
            reply_json_local(deps, res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "invalid fingerprint"}
            });
            return;
        }

        PeopleContactsStore store(deps.people_db_path);
        bool deleted = false;
        std::string err;
        if (!store.delete_for_owner(actor_fp, subject_fp, &deleted, &err)) {
            reply_json_local(deps, res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "failed to delete person"}
            });
            return;
        }

        reply_json_local(deps, res, 200, json{
            {"ok", true},
            {"deleted", deleted}
        });
    });
}

} // namespace pqnas
