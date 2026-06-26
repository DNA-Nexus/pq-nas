#include "routes_service_notices.h"

#include "audit_fields.h"
#include "audit_log.h"
#include "httplib.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace pqnas {
namespace {

std::int64_t now_epoch_local() {
    using namespace std::chrono;
    return static_cast<std::int64_t>(
        duration_cast<seconds>(system_clock::now().time_since_epoch()).count()
    );
}

std::int64_t route_now_epoch_local(const ServiceNoticeRoutesDeps& deps) {
    if (deps.now_epoch) return deps.now_epoch();
    return now_epoch_local();
}

void reply_json_local(const ServiceNoticeRoutesDeps& deps,
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

std::string trim_copy_local(const std::string& in) {
    std::size_t a = 0;
    while (a < in.size() && std::isspace(static_cast<unsigned char>(in[a]))) ++a;

    std::size_t b = in.size();
    while (b > a && std::isspace(static_cast<unsigned char>(in[b - 1]))) --b;

    return in.substr(a, b - a);
}

std::string read_text_file_local(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.good()) return std::string();

    return std::string(
        std::istreambuf_iterator<char>(f),
        std::istreambuf_iterator<char>()
    );
}

std::string make_notice_id_local() {
    std::random_device rd;
    const std::uint64_t a = static_cast<std::uint64_t>(rd());
    const std::uint64_t b = static_cast<std::uint64_t>(rd());

    const auto now = static_cast<std::uint64_t>(route_now_epoch_local(ServiceNoticeRoutesDeps{}));

    std::ostringstream oss;
    oss << "notice_"
        << std::hex
        << std::setw(12) << std::setfill('0') << now
        << "_"
        << std::setw(16) << std::setfill('0') << ((a << 32) ^ b);

    return oss.str();
}

bool deps_api_ok_local(const ServiceNoticeRoutesDeps& deps) {
    return deps.store &&
           deps.require_user &&
           deps.require_admin &&
           deps.require_same_origin &&
           deps.reply_json &&
           deps.audit_append;
}

bool deps_ui_ok_local(const ServiceNoticeRoutesDeps& deps) {
    return deps.require_admin &&
           !deps.static_admin_service_notices_html.empty();
}

json notices_array_json_local(const std::vector<ServiceNotice>& notices) {
    json arr = json::array();
    for (const auto& notice : notices) {
        arr.push_back(ServiceNoticesStore::notice_to_json(notice));
    }
    return arr;
}

void audit_notice_admin_local(const ServiceNoticeRoutesDeps& deps,
                              const httplib::Request& req,
                              const std::string& actor_fp,
                              const std::string& event,
                              const std::string& id,
                              const std::string& title,
                              const std::string& outcome) {
    if (!deps.audit_append) return;

    AuditEvent ev;
    ev.event = event;
    ev.outcome = outcome;
    ev.f["actor_fp"] = actor_fp;
    ev.f["id"] = pqnas::shorten(id, 100);
    ev.f["title"] = pqnas::shorten(title, 160);
    ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

    deps.audit_append(ev);
}

} // namespace

void register_service_notice_routes(httplib::Server& srv,
                                    const ServiceNoticeRoutesDeps& deps) {
    srv.Get("/admin/service-notices",
        [deps](const httplib::Request& req, httplib::Response& res) {
            if (!deps_ui_ok_local(deps)) {
                res.status = 500;
                res.set_content(
                    "service notices UI route context incomplete",
                    "text/plain; charset=utf-8"
                );
                return;
            }

            std::string actor_fp;
            if (!deps.require_admin(req, res, &actor_fp)) return;

            const std::string body = read_text_file_local(deps.static_admin_service_notices_html);
            if (body.empty()) {
                res.status = 404;
                res.set_content("missing admin_service_notices.html", "text/plain; charset=utf-8");
                return;
            }

            res.set_header("Cache-Control", "no-store");
            res.set_content(body, "text/html; charset=utf-8");
        }
    );

    srv.Get("/api/v4/service-notices/active",
        [deps](const httplib::Request& req, httplib::Response& res) {
            if (!deps_api_ok_local(deps)) {
                reply_json_local(deps, res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "service notice route context incomplete"}
                });
                return;
            }

            std::string fp_hex;
            std::string role;
            if (!deps.require_user(req, res, &fp_hex, &role)) return;

            std::vector<ServiceNotice> notices;
            std::string err;
            if (!deps.store->list_active(route_now_epoch_local(deps), 3, &notices, &err)) {
                reply_json_local(deps, res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", err.empty() ? "failed to load service notices" : err}
                });
                return;
            }

            reply_json_local(deps, res, 200, json{
                {"ok", true},
                {"notices", notices_array_json_local(notices)}
            });
        }
    );

    srv.Get("/api/v4/admin/service-notices/list",
        [deps](const httplib::Request& req, httplib::Response& res) {
            if (!deps_api_ok_local(deps)) {
                reply_json_local(deps, res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "service notice route context incomplete"}
                });
                return;
            }

            std::string actor_fp;
            if (!deps.require_admin(req, res, &actor_fp)) return;

            std::vector<ServiceNotice> notices;
            std::string err;
            if (!deps.store->list_all(&notices, &err)) {
                reply_json_local(deps, res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", err.empty() ? "failed to load service notices" : err}
                });
                return;
            }

            reply_json_local(deps, res, 200, json{
                {"ok", true},
                {"notices", notices_array_json_local(notices)}
            });
        }
    );

    srv.Post("/api/v4/admin/service-notices/save",
        [deps](const httplib::Request& req, httplib::Response& res) {
            if (!deps_api_ok_local(deps)) {
                reply_json_local(deps, res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "service notice route context incomplete"}
                });
                return;
            }

            std::string actor_fp;
            if (!deps.require_admin(req, res, &actor_fp)) return;
            if (!deps.require_same_origin(req, res)) return;

            json body = json::parse(req.body.empty() ? "{}" : req.body, nullptr, false);
            if (body.is_discarded() || !body.is_object()) {
                reply_json_local(deps, res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "invalid json"}
                });
                return;
            }

            ServiceNotice notice = ServiceNoticesStore::notice_from_json(body);
            const bool created = trim_copy_local(notice.id).empty();
            if (created) notice.id = make_notice_id_local();

            const std::int64_t now = route_now_epoch_local(deps);
            if (notice.created_at <= 0) notice.created_at = now;
            notice.updated_at = now;

            std::string nerr;
            if (!ServiceNoticesStore::normalize_for_save(&notice, &nerr)) {
                reply_json_local(deps, res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", nerr.empty() ? "invalid service notice" : nerr}
                });
                return;
            }

            std::string err;
            if (!deps.store->upsert(notice, &err)) {
                reply_json_local(deps, res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", err.empty() ? "failed to save service notice" : err}
                });
                return;
            }

            audit_notice_admin_local(
                deps,
                req,
                actor_fp,
                created ? "admin.service_notices.create" : "admin.service_notices.update",
                notice.id,
                notice.title,
                "ok"
            );

            if (deps.record_activity) {
                deps.record_activity(
                    req,
                    actor_fp,
                    notice,
                    created ? "created" : "updated"
                );
            }

            reply_json_local(deps, res, 200, json{
                {"ok", true},
                {"notice", ServiceNoticesStore::notice_to_json(notice)}
            });
        }
    );

    srv.Post("/api/v4/admin/service-notices/delete",
        [deps](const httplib::Request& req, httplib::Response& res) {
            if (!deps_api_ok_local(deps)) {
                reply_json_local(deps, res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "service notice route context incomplete"}
                });
                return;
            }

            std::string actor_fp;
            if (!deps.require_admin(req, res, &actor_fp)) return;
            if (!deps.require_same_origin(req, res)) return;

            json body = json::parse(req.body.empty() ? "{}" : req.body, nullptr, false);
            if (body.is_discarded() || !body.is_object()) {
                reply_json_local(deps, res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "invalid json"}
                });
                return;
            }

            const std::string id = trim_copy_local(body.value("id", ""));
            if (id.empty()) {
                reply_json_local(deps, res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "missing id"}
                });
                return;
            }

            ServiceNotice notice_before_delete;
            notice_before_delete.id = id;

            {
                std::vector<ServiceNotice> all_notices;
                std::string list_err;
                if (deps.store->list_all(&all_notices, &list_err)) {
                    for (const auto& candidate : all_notices) {
                        if (candidate.id == id) {
                            notice_before_delete = candidate;
                            break;
                        }
                    }
                }
            }

            bool removed = false;
            std::string err;
            if (!deps.store->erase(id, &removed, &err)) {
                reply_json_local(deps, res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", err.empty() ? "failed to delete service notice" : err}
                });
                return;
            }

            audit_notice_admin_local(
                deps,
                req,
                actor_fp,
                "admin.service_notices.delete",
                id,
                "",
                removed ? "ok" : "noop"
            );

            if (removed && deps.record_activity) {
                deps.record_activity(
                    req,
                    actor_fp,
                    notice_before_delete,
                    "deleted"
                );
            }

            reply_json_local(deps, res, 200, json{
                {"ok", true},
                {"removed", removed}
            });
        }
    );
}

} // namespace pqnas
