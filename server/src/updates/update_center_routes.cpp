#include "updates/update_center_routes.h"

#include <nlohmann/json.hpp>

#include <string>
#include <system_error>
#include <filesystem>

namespace pqnas::updates {

using json = nlohmann::json;

namespace {

std::string update_lower(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

bool update_ends_with(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::filesystem::path updates_root_dir(const UpdateCenterRoutesDeps& deps) {
    const std::string env = deps.getenv_str ? deps.getenv_str("PQNAS_UPDATES_ROOT") : "";
    if (!env.empty()) return std::filesystem::path(env);
    return std::filesystem::path("/var/lib/pqnas/updates");
}

std::filesystem::path update_incoming_dir(const UpdateCenterRoutesDeps& deps) {
    return updates_root_dir(deps) / "incoming";
}

} // namespace


void register_update_center_routes(httplib::Server& srv, const UpdateCenterRoutesDeps& deps) {
    srv.Get("/admin/updates", [deps](const httplib::Request& req, httplib::Response& res) {
        if (!deps.require_admin(req, res)) return;

        std::string body;
        if (!deps.read_file_to_string(deps.static_admin_updates_html, body)) {
            res.status = 404;
            res.body = "Missing static file: " + deps.static_admin_updates_html;
            return;
        }

        res.set_header("Cache-Control", "no-store");
        res.set_content(body, "text/html; charset=utf-8");
    });

    srv.Get("/api/v4/admin/updates/status", [deps](const httplib::Request& req, httplib::Response& res) {
        if (!deps.require_admin(req, res)) return;

        std::error_code ec;
        const std::filesystem::path incoming = update_incoming_dir(deps);
        std::filesystem::create_directories(incoming, ec);
        if (ec) {
            deps.reply_json(res, 500, json{
                {"ok", false},
                {"error", "create_incoming_failed"},
                {"message", ec.message()}
            }.dump(2));
            return;
        }

        json files = json::array();

        std::error_code it_ec;
        for (const auto& ent : std::filesystem::directory_iterator(incoming, it_ec)) {
            if (it_ec) break;

            std::error_code st_ec;
            if (!ent.is_regular_file(st_ec) || st_ec) continue;

            const std::string name = ent.path().filename().string();
            const std::string low = update_lower(name);

            if (update_ends_with(low, ".part") || update_ends_with(low, ".json")) {
                continue;
            }

            std::uintmax_t sz = ent.file_size(st_ec);
            if (st_ec) sz = 0;

            files.push_back(json{
                {"name", name},
                {"size", sz}
            });
        }

        deps.reply_json(res, 200, json{
            {"ok", true},
            {"incoming_count", files.size()},
            {"incoming", files}
        }.dump(2));
    });
}

} // namespace pqnas::updates
