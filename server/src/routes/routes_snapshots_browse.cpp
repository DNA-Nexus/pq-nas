#include "routes_snapshots_browse.h"

#include "httplib.h"
#include "audit_log.h"
#include "audit_fields.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <pwd.h>
#include <string>
#include <system_error>
#include <unistd.h>
#include <vector>

using json = nlohmann::json;

namespace {

void fallback_reply_json(httplib::Response& res, int status, const std::string& body) {
    res.status = status;
    res.set_content(body, "application/json; charset=utf-8");
}

void reply_json_ctx(
    const SnapshotBrowseRoutesContext& c,
    httplib::Response& res,
    int status,
    const json& body
) {
    if (c.reply_json) c.reply_json(res, status, body.dump());
    else fallback_reply_json(res, status, body.dump());
}

bool context_ok(const SnapshotBrowseRoutesContext& c) {
    return c.require_admin &&
           c.reply_json &&
           c.audit_append &&
           c.load_volumes &&
           c.is_btrfs_subvolume &&
           c.is_path_under &&
           c.btrfs_subvolume_show;
}

std::string lower_ascii_local(std::string s) {
    for (char& ch : s) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    return s;
}

std::string audit_header_value(
    const SnapshotBrowseRoutesContext& c,
    const std::string& value,
    std::size_t maxlen
) {
    if (c.audit_safe_header_value) return c.audit_safe_header_value(value, maxlen);
    return pqnas::shorten(value, maxlen);
}

std::string audit_ua(const httplib::Request& req) {
    auto it = req.headers.find("User-Agent");
    return pqnas::shorten(it == req.headers.end() ? "" : it->second);
}

void add_forwarded_audit_fields(
    const SnapshotBrowseRoutesContext& c,
    const httplib::Request& req,
    pqnas::AuditEvent* ev
) {
    if (!ev) return;

    auto it_cf = req.headers.find("CF-Connecting-IP");
    if (it_cf != req.headers.end()) {
        ev->f["cf_ip"] = audit_header_value(c, it_cf->second, 120);
    }

    auto it_xff = req.headers.find("X-Forwarded-For");
    if (it_xff != req.headers.end()) {
        ev->f["xff"] = audit_header_value(c, it_xff->second, 120);
    }

    ev->f["ua"] = audit_ua(req);
}

} // namespace

void register_snapshot_browse_routes(
    httplib::Server& srv,
    const SnapshotBrowseRoutesContext& ctx
) {
    const SnapshotBrowseRoutesContext c = ctx;

    srv.Get("/api/v4/snapshots/volumes",
        [c](const httplib::Request& req, httplib::Response& res) {
            auto reply = [&](int status, const json& body) {
                reply_json_ctx(c, res, status, body);
            };

            if (!context_ok(c)) {
                reply(500, json{{"ok", false}, {"error", "server_error"}, {"message", "snapshot browse route context incomplete"}});
                return;
            }

            std::string actor_fp;
            if (!c.require_admin(req, res, &actor_fp)) return;

            auto audit_emit = [&](const std::string& outcome, const std::string& reason, int http, const std::string& detail = "") {
                pqnas::AuditEvent ev;
                ev.event = "snapshots.volumes";
                ev.outcome = outcome;
                ev.f["actor_fp"] = actor_fp;
                ev.f["http"] = std::to_string(http);
                if (!reason.empty()) ev.f["reason"] = reason;
                if (!detail.empty()) ev.f["detail"] = pqnas::shorten(detail, 180);
                ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
                add_forwarded_audit_fields(c, req, &ev);
                c.audit_append(ev);
            };

            std::string backend;
            std::string err;
            std::vector<SnapshotBrowseVolume> vols;
            if (!c.load_volumes(&backend, &vols, &err)) {
                audit_emit("fail", "settings_load_failed", 500, err);
                reply(500, json{{"ok", false}, {"error", "server_error"}, {"message", "failed to load snapshot settings"}});
                return;
            }

            json out_vols = json::array();
            for (const auto& v : vols) {
                out_vols.push_back(json{
                    {"name", v.name},
                    {"source_subvolume", v.source_subvolume},
                    {"snap_root", v.snap_root},
                    {"enabled", v.enabled}
                });
            }

            std::string runtime_user;
            if (struct passwd* pw = getpwuid(geteuid()); pw && pw->pw_name) {
                runtime_user = pw->pw_name;
            }

            audit_emit("ok", "", 200);
            reply(200, json{
                {"ok", true},
                {"backend", backend},
                {"volumes", out_vols},
                {"runtime_user", runtime_user}
            });
        }
    );

    srv.Get("/api/v4/snapshots/list",
        [c](const httplib::Request& req, httplib::Response& res) {
            auto reply = [&](int status, const json& body) {
                reply_json_ctx(c, res, status, body);
            };

            if (!context_ok(c)) {
                reply(500, json{{"ok", false}, {"error", "server_error"}, {"message", "snapshot browse route context incomplete"}});
                return;
            }

            std::string actor_fp;
            if (!c.require_admin(req, res, &actor_fp)) return;

            auto audit_fail = [&](const std::string& reason, int http, const std::string& detail = "") {
                pqnas::AuditEvent ev;
                ev.event = "snapshots.list";
                ev.outcome = "fail";
                ev.f["actor_fp"] = actor_fp;
                ev.f["reason"] = reason;
                ev.f["http"] = std::to_string(http);
                if (!detail.empty()) ev.f["detail"] = pqnas::shorten(detail, 180);
                ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
                add_forwarded_audit_fields(c, req, &ev);
                c.audit_append(ev);
            };

            auto audit_ok = [&](const std::string& vol, std::size_t n) {
                pqnas::AuditEvent ev;
                ev.event = "snapshots.list";
                ev.outcome = "ok";
                ev.f["actor_fp"] = actor_fp;
                ev.f["volume"] = vol;
                ev.f["count"] = std::to_string(static_cast<unsigned long long>(n));
                ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
                add_forwarded_audit_fields(c, req, &ev);
                c.audit_append(ev);
            };

            std::string vol = req.has_param("volume") ? req.get_param_value("volume") : "";
            if (vol.empty()) {
                audit_fail("missing_volume", 400);
                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "missing volume"}});
                return;
            }

            std::string backend;
            std::string err;
            std::vector<SnapshotBrowseVolume> vols;
            if (!c.load_volumes(&backend, &vols, &err)) {
                audit_fail("settings_load_failed", 500, err);
                reply(500, json{{"ok", false}, {"error", "server_error"}, {"message", "failed to load snapshot settings"}});
                return;
            }

            auto it = std::find_if(vols.begin(), vols.end(), [&](const SnapshotBrowseVolume& v) {
                return v.name == vol;
            });
            if (it == vols.end()) {
                audit_fail("unknown_volume", 404, vol);
                reply(404, json{{"ok", false}, {"error", "not_found"}, {"message", "unknown volume"}});
                return;
            }

            const std::string snap_root = it->snap_root;
            std::error_code ec;
            if (!std::filesystem::exists(snap_root, ec) || ec) {
                audit_fail("snap_root_missing", 404, snap_root);
                reply(404, json{{"ok", false}, {"error", "not_found"}, {"message", "snap_root not found"}});
                return;
            }

            struct Item {
                std::string id;
                std::string path;
                std::uint64_t mtime_unix{0};
                bool is_subvol{false};
                std::string probe;
                std::string probe_detail;
            };

            std::vector<Item> items;

            for (auto& de : std::filesystem::directory_iterator(snap_root, ec)) {
                if (ec) break;
                if (!de.is_directory(ec)) continue;

                const auto p = de.path();
                const std::string id = p.filename().string();
                const std::string abs = p.string();

                std::uint64_t mt = 0;
                std::error_code ec2;
                auto ftime = std::filesystem::last_write_time(p, ec2);
                if (!ec2) {
                    auto sctp = std::chrono::time_point_cast<std::chrono::seconds>(ftime);
                    mt = static_cast<std::uint64_t>(sctp.time_since_epoch().count());
                }

                std::string detail;
                bool is_sub = c.is_btrfs_subvolume(abs, &detail);
                std::string probe = "ok";

                const std::string dlow = lower_ascii_local(detail);
                if (!is_sub) {
                    if (dlow.find("sudo:") != std::string::npos ||
                        dlow.find("a password is required") != std::string::npos ||
                        dlow.find("no tty present") != std::string::npos ||
                        dlow.find("not in the sudoers file") != std::string::npos ||
                        dlow.find("operation not permitted") != std::string::npos) {
                        probe = "no_privs";
                    } else {
                        probe = "ok";
                    }
                }

                items.push_back(Item{id, abs, mt, is_sub, probe, detail});
            }

            std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
                if (a.mtime_unix != b.mtime_unix) return a.mtime_unix > b.mtime_unix;
                return a.id > b.id;
            });

            json snaps = json::array();
            for (const auto& s : items) {
                snaps.push_back(json{
                    {"id", s.id},
                    {"path", s.path},
                    {"created_utc", ""},
                    {"readonly", false},
                    {"is_btrfs_subvolume", s.is_subvol},
                    {"probe", s.probe},
                    {"probe_detail", pqnas::shorten(s.probe_detail, 180)}
                });
            }

            audit_ok(vol, snaps.size());
            reply(200, json{
                {"ok", true},
                {"volume", vol},
                {"snap_root", snap_root},
                {"snapshots", snaps}
            });
        }
    );

    srv.Get("/api/v4/snapshots/info",
        [c](const httplib::Request& req, httplib::Response& res) {
            auto reply = [&](int status, const json& body) {
                reply_json_ctx(c, res, status, body);
            };

            if (!context_ok(c)) {
                reply(500, json{{"ok", false}, {"error", "server_error"}, {"message", "snapshot browse route context incomplete"}});
                return;
            }

            std::string actor_fp;
            if (!c.require_admin(req, res, &actor_fp)) return;
            (void)actor_fp;

            std::string vol = req.has_param("volume") ? req.get_param_value("volume") : "";
            std::string id = req.has_param("id") ? req.get_param_value("id") : "";
            if (vol.empty() || id.empty()) {
                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "missing volume or id"}});
                return;
            }

            std::string backend;
            std::string err;
            std::vector<SnapshotBrowseVolume> vols;
            if (!c.load_volumes(&backend, &vols, &err)) {
                reply(500, json{{"ok", false}, {"error", "server_error"}, {"message", "failed to load snapshot settings"}});
                return;
            }

            auto it = std::find_if(vols.begin(), vols.end(), [&](const SnapshotBrowseVolume& v) {
                return v.name == vol;
            });
            if (it == vols.end()) {
                reply(404, json{{"ok", false}, {"error", "not_found"}, {"message", "unknown volume"}});
                return;
            }

            const std::string snap_root = it->snap_root;
            const std::string snap_path = (std::filesystem::path(snap_root) / id).string();

            if (!c.is_path_under(snap_path, snap_root)) {
                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid snapshot id"}});
                return;
            }

            std::error_code ec;
            if (!std::filesystem::exists(snap_path, ec) || ec) {
                reply(404, json{{"ok", false}, {"error", "not_found"}, {"message", "snapshot not found"}});
                return;
            }

            std::string out;
            int rc = 0;
            c.btrfs_subvolume_show(snap_path, &out, &rc);

            const bool show_ok = (rc == 0);

            reply(200, json{
                {"ok", true},
                {"volume", vol},
                {"id", id},
                {"snapshot_path", snap_path},
                {"btrfs_show_ok", show_ok},
                {"btrfs_show_rc", rc},
                {"btrfs_show", pqnas::shorten(out, 2000)},
                {"hint", show_ok ? "" : "btrfs details require sudo/root (configure sudoers for pqnas)"}
            });
        }
    );
}
