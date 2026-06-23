#include "routes_admin_audit_read.h"

#include "httplib.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <deque>
#include <fstream>
#include <iostream>
#include <string>

using json = nlohmann::json;

namespace {

bool context_ok(const AdminAuditReadRoutesContext& c) {
    return !c.static_audit_js.empty() &&
           !c.audit_jsonl_path.empty() &&
           !c.audit_state_path.empty() &&
           c.require_admin &&
           c.reply_json &&
           c.slurp_file &&
           c.trim_nl;
}

void reply_context_error(httplib::Response& res) {
    res.status = 500;
    res.set_header("Cache-Control", "no-store");
    res.set_content(
        "{\"ok\":false,\"error\":\"server_error\",\"message\":\"admin audit read route context incomplete\"}",
        "application/json; charset=utf-8"
    );
}

} // namespace

void register_admin_audit_read_routes(
    httplib::Server& srv,
    const AdminAuditReadRoutesContext& ctx
) {
    const AdminAuditReadRoutesContext c = ctx;

    srv.Get("/static/admin_audit.js",
        [c](const httplib::Request&, httplib::Response& res) {
            if (!context_ok(c)) {
                reply_context_error(res);
                return;
            }

            const std::string body = c.slurp_file(c.static_audit_js);
            if (body.empty()) {
                std::cerr << "[/static/admin_audit.js] ERROR: empty body. path="
                          << c.static_audit_js << std::endl;
                res.status = 404;
                res.set_content("missing admin_audit.js", "text/plain");
                return;
            }

            res.set_content(body, "application/javascript; charset=utf-8");
        }
    );

    srv.Get("/api/v4/audit/tail",
        [c](const httplib::Request& req, httplib::Response& res) {
            if (!context_ok(c)) {
                reply_context_error(res);
                return;
            }

            if (!c.require_admin(req, res)) return;

            int n = 200;
            if (req.has_param("n")) {
                try {
                    n = std::stoi(req.get_param_value("n"));
                } catch (...) {
                }
            }

            n = std::max(1, std::min(1000, n));

            std::ifstream f(c.audit_jsonl_path);
            std::deque<json> q;
            std::string line;

            if (f.good()) {
                while (std::getline(f, line)) {
                    if (line.empty()) continue;
                    try {
                        q.push_back(json::parse(line));
                        if (static_cast<int>(q.size()) > n) q.pop_front();
                    } catch (...) {
                    }
                }
            }

            json out;
            out["ok"] = true;
            out["lines"] = json::array();

            for (auto& jj : q) {
                out["lines"].push_back(jj);
            }

            c.reply_json(res, 200, out.dump());
        }
    );

    srv.Get("/api/v4/audit/verify",
        [c](const httplib::Request& req, httplib::Response& res) {
            if (!context_ok(c)) {
                reply_context_error(res);
                return;
            }

            if (!c.require_admin(req, res)) return;

            std::string state = c.trim_nl(c.slurp_file(c.audit_state_path));
            std::string last_hash;

            {
                std::ifstream f(c.audit_jsonl_path);
                std::string line;
                std::string last;

                if (f.good()) {
                    while (std::getline(f, line)) {
                        if (!line.empty()) last = line;
                    }
                }

                if (!last.empty()) {
                    try {
                        json jj = json::parse(last);
                        last_hash = jj.value("line_hash", "");
                    } catch (...) {
                    }
                }
            }

            const bool ok = (!state.empty() && !last_hash.empty() && state == last_hash);

            c.reply_json(res, 200, json{
                {"ok", ok},
                {"state", state},
                {"last_line_hash", last_hash}
            }.dump());
        }
    );
}
