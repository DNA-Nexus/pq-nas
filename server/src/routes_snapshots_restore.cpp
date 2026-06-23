#include "routes_snapshots_restore.h"

#include "httplib.h"
#include "audit_log.h"
#include "audit_fields.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

using json = nlohmann::json;

namespace {

struct RestorePlan {
    std::string volume;
    std::string snapshot_id;
    std::string snapshot_path;
    std::string source_subvolume;
    std::string mode;
    std::string confirm_phrase;
    std::string created_iso;
    std::time_t created_epoch = 0;
};

std::mutex g_restore_mu;
std::unordered_map<std::string, RestorePlan> g_restore_by_id;

bool context_ok(const SnapshotRestoreRoutesContext& c) {
    return c.require_admin &&
           c.reply_json &&
           c.audit_append &&
           c.load_volumes &&
           c.is_path_under &&
           c.is_btrfs_subvolume &&
           c.realpath_str &&
           c.now_iso_utc &&
           c.rand_hex_32 &&
           c.random_b64url &&
           c.popen_capture;
}

void fallback_reply_json(httplib::Response& res, int status, const std::string& body) {
    res.status = status;
    res.set_content(body, "application/json; charset=utf-8");
}

void reply_json_ctx(
    const SnapshotRestoreRoutesContext& c,
    httplib::Response& res,
    int status,
    const json& body
) {
    if (c.reply_json) c.reply_json(res, status, body.dump());
    else fallback_reply_json(res, status, body.dump());
}

std::string sh_quote(const std::string& s) {
    std::string q = s;
    std::size_t pos = 0;
    while ((pos = q.find("'", pos)) != std::string::npos) {
        q.replace(pos, 1, "'\\''");
        pos += 4;
    }
    return "'" + q + "'";
}

bool run_cmd_ctx(
    const SnapshotRestoreRoutesContext& c,
    const std::string& cmd,
    std::string* out,
    int* rc_out
) {
    int rc = 0;
    std::string o;
    c.popen_capture(cmd + " 2>&1", &o, &rc);
    if (out) *out = o;
    if (rc_out) *rc_out = rc;
    return rc == 0;
}

std::string file_read_all(const std::filesystem::path& p) {
    std::ifstream f(p);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void restore_cache_gc_best_effort() {
    const std::time_t now = std::time(nullptr);

    std::lock_guard<std::mutex> lk(g_restore_mu);
    for (auto it = g_restore_by_id.begin(); it != g_restore_by_id.end();) {
        if (it->second.created_epoch > 0 && now - it->second.created_epoch > 600) {
            it = g_restore_by_id.erase(it);
        } else {
            ++it;
        }
    }
}

bool systemd_unit_available() {
    int rc1 = std::system("command -v systemctl >/dev/null 2>&1");
    if (rc1 != 0) return false;

    int rc2 = std::system("sudo -n /usr/bin/systemctl status pqnas.service >/dev/null 2>&1");
    return rc2 == 0;
}

} // namespace

void register_snapshot_restore_routes(
    httplib::Server& srv,
    const SnapshotRestoreRoutesContext& ctx
) {
    const SnapshotRestoreRoutesContext c = ctx;

    srv.Post("/api/v4/snapshots/restore/prepare",
        [c](const httplib::Request& req, httplib::Response& res) {
            auto reply = [&](int status, const json& body) {
                reply_json_ctx(c, res, status, body);
            };

            if (!context_ok(c)) {
                reply(500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "snapshot restore route context incomplete"}
                });
                return;
            }

            std::string actor_fp;
            if (!c.require_admin(req, res, &actor_fp)) return;

            json j;
            try {
                j = json::parse(req.body);
            } catch (...) {
                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid json"}});
                return;
            }

            const std::string vol = j.value("volume", "");
            const std::string id = j.value("id", "");
            const std::string mode = j.value("mode", "swap");
            const bool force_stop = j.value("force_stop", false);

            if (vol.empty() || id.empty()) {
                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "missing volume or id"}});
                return;
            }
            if (mode != "swap") {
                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "unsupported mode"}});
                return;
            }
            if (!force_stop) {
                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "force_stop must be true in v1"}});
                return;
            }

            std::string backend;
            std::string err;
            std::vector<SnapshotRestoreVolume> vols;
            if (!c.load_volumes(&backend, &vols, &err)) {
                reply(500, json{{"ok", false}, {"error", "server_error"}, {"message", "failed to load snapshot settings"}});
                return;
            }

            auto it = std::find_if(vols.begin(), vols.end(), [&](const SnapshotRestoreVolume& v) {
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

            if (!c.is_btrfs_subvolume(snap_path)) {
                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "snapshot is not a btrfs subvolume"}});
                return;
            }

            const std::string confirm_phrase = "RESTORE " + vol + " " + id;

            restore_cache_gc_best_effort();
            const std::string confirm_id = "RSTR_" + c.rand_hex_32();

            RestorePlan plan;
            plan.volume = vol;
            plan.snapshot_id = id;
            plan.snapshot_path = snap_path;
            plan.source_subvolume = it->source_subvolume;
            plan.mode = "swap";
            plan.confirm_phrase = confirm_phrase;
            plan.created_iso = c.now_iso_utc();
            plan.created_epoch = std::time(nullptr);

            {
                std::lock_guard<std::mutex> lk(g_restore_mu);
                g_restore_by_id[confirm_id] = plan;
            }

            {
                pqnas::AuditEvent ev;
                ev.event = "snapshots.restore_prepare";
                ev.outcome = "ok";
                ev.f["actor_fp"] = actor_fp;
                ev.f["volume"] = vol;
                ev.f["id"] = id;
                ev.f["mode"] = mode;
                ev.f["confirm_id"] = confirm_id;
                ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
                c.audit_append(ev);
            }

            const bool can_service = systemd_unit_available();

            json steps = json::array();
            if (can_service) steps.push_back("stop pqnas.service");
            else steps.push_back("STOP PQ-NAS manually (dev mode: running via ./start.sh)");

            steps.push_back("rename source_subvolume -> source_subvolume.pre_restore.<ts>");
            steps.push_back("btrfs subvolume snapshot <snapshot> <source_subvolume>");

            if (can_service) steps.push_back("start pqnas.service");
            else steps.push_back("START PQ-NAS manually (dev mode)");

            json warnings = json::array({
                "Restoring replaces the live volume content",
                "Service downtime required"
            });
            if (!can_service) {
                warnings.push_back("Dev mode: pqnas.service not detected; you must stop/start PQ-NAS yourself");
            }

            reply(200, json{
                {"ok", true},
                {"confirm_id", confirm_id},
                {"expires_in_sec", 120},
                {"plan", json{
                    {"volume", vol},
                    {"source_subvolume", it->source_subvolume},
                    {"snapshot_path", snap_path},
                    {"mode", mode},
                    {"steps", steps},
                    {"warnings", warnings}
                }}
            });
        }
    );

    srv.Get("/api/v4/snapshots/restore/status",
        [c](const httplib::Request& req, httplib::Response& res) {
            auto reply = [&](int status, const json& body) {
                reply_json_ctx(c, res, status, body);
            };

            if (!context_ok(c)) {
                reply(500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "snapshot restore route context incomplete"}
                });
                return;
            }

            std::string actor_fp;
            if (!c.require_admin(req, res, &actor_fp)) return;
            (void)actor_fp;

            const std::string job_id = req.has_param("job_id") ? req.get_param_value("job_id") : "";
            if (job_id.empty()) {
                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "missing job_id"}});
                return;
            }
            if (job_id.rfind("RJOB_", 0) != 0) {
                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid job_id"}});
                return;
            }

            const std::filesystem::path run_dir = "/run/pqnas/restore";
            const std::filesystem::path job_path = run_dir / (job_id + ".json");
            const std::filesystem::path result_path = run_dir / (job_id + ".result.json");

            std::error_code ec;

            if (std::filesystem::exists(result_path, ec) && !ec) {
                const std::string body = file_read_all(result_path);
                if (body.empty()) {
                    reply(500, json{{"ok", false}, {"error", "server_error"}, {"message", "result file unreadable"}});
                    return;
                }

                try {
                    json jr = json::parse(body);
                    if (jr.value("job_id", "") != job_id) {
                        reply(500, json{{"ok", false}, {"error", "server_error"}, {"message", "result job_id mismatch"}});
                        return;
                    }

                    reply(200, json{
                        {"ok", true},
                        {"job_id", job_id},
                        {"status", "done"},
                        {"result", jr}
                    });
                    return;
                } catch (...) {
                    reply(500, json{{"ok", false}, {"error", "server_error"}, {"message", "result file contains invalid json"}});
                    return;
                }
            }

            const std::string unit = "pqnas-restore@" + job_id + ".service";
            std::string out_show;
            int rc_show = 0;

            const std::string cmd_show =
                "sudo -n /usr/bin/systemctl show " + sh_quote(unit) +
                " -p ActiveState -p SubState -p Result -p ExecMainStatus -p ExecMainCode";

            if (run_cmd_ctx(c, cmd_show, &out_show, &rc_show)) {
                auto kv = [&](const std::string& key) -> std::string {
                    std::istringstream iss(out_show);
                    std::string line;
                    while (std::getline(iss, line)) {
                        if (line.rfind(key + "=", 0) == 0) return line.substr(key.size() + 1);
                    }
                    return "";
                };

                const std::string active = kv("ActiveState");
                const std::string sub = kv("SubState");
                const std::string result = kv("Result");
                const std::string code = kv("ExecMainCode");
                const std::string status2 = kv("ExecMainStatus");

                std::string derived = "running";
                if (active == "failed") derived = "failed";
                else if (active == "active" && sub == "running") derived = "running";
                else if (active == "inactive" && sub == "dead") derived = "queued";
                else if (active == "inactive" && sub == "exited") derived = "exited";
                else derived = active.empty() ? "unknown" : active;

                reply(200, json{
                    {"ok", true},
                    {"job_id", job_id},
                    {"status", derived},
                    {"unit", unit},
                    {"systemd", {
                        {"ActiveState", active},
                        {"SubState", sub},
                        {"Result", result},
                        {"ExecMainCode", code},
                        {"ExecMainStatus", status2}
                    }},
                    {"hint", "result not written yet"}
                });
                return;
            }

            ec.clear();
            if (std::filesystem::exists(job_path, ec) && !ec) {
                reply(200, json{
                    {"ok", true},
                    {"job_id", job_id},
                    {"status", "queued"},
                    {"hint", "result not written yet (and systemd status unavailable)"}
                });
                return;
            }

            reply(404, json{{"ok", false}, {"error", "not_found"}, {"message", "unknown job_id"}});
        }
    );

    srv.Post("/api/v4/snapshots/restore/confirm",
        [c](const httplib::Request& req, httplib::Response& res) {
            auto reply = [&](int status, const json& body) {
                reply_json_ctx(c, res, status, body);
            };

            if (!context_ok(c)) {
                reply(500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "snapshot restore route context incomplete"}
                });
                return;
            }

            std::string actor_fp;
            if (!c.require_admin(req, res, &actor_fp)) return;

            json j;
            try {
                j = json::parse(req.body);
            } catch (...) {
                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid json"}});
                return;
            }

            const std::string confirm_id = j.value("confirm_id", "");
            const std::string confirm_text = j.value("confirm_text", "");

            if (confirm_id.empty() || confirm_text.empty()) {
                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "missing confirm_id or confirm_text"}});
                return;
            }

            RestorePlan plan;
            {
                std::lock_guard<std::mutex> lk(g_restore_mu);
                auto it = g_restore_by_id.find(confirm_id);
                if (it == g_restore_by_id.end()) {
                    reply(404, json{{"ok", false}, {"error", "not_found"}, {"message", "unknown confirm_id"}});
                    return;
                }

                plan = it->second;
            }

            if (confirm_text != plan.confirm_phrase) {
                pqnas::AuditEvent ev;
                ev.event = "snapshots.restore_confirm";
                ev.outcome = "fail";
                ev.f["actor_fp"] = actor_fp;
                ev.f["confirm_id"] = confirm_id;
                ev.f["volume"] = plan.volume;
                ev.f["id"] = plan.snapshot_id;
                ev.f["reason"] = "confirm_text_mismatch";
                ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
                c.audit_append(ev);

                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "confirmation text mismatch"}});
                return;
            }

            {
                std::lock_guard<std::mutex> lk(g_restore_mu);
                g_restore_by_id.erase(confirm_id);
            }

            const std::string snap_root_real =
                c.realpath_str(std::filesystem::path(plan.snapshot_path).parent_path().string());

            if (!c.is_path_under(plan.snapshot_path, snap_root_real)) {
                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "snapshot path invalid"}});
                return;
            }

            if (!c.is_btrfs_subvolume(plan.snapshot_path)) {
                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "snapshot no longer valid"}});
                return;
            }

            const std::string job_id = "RJOB_" + c.random_b64url(18);
            const std::string created_utc = c.now_iso_utc();

            const std::filesystem::path run_dir = "/run/pqnas/restore";
            {
                std::error_code ec;
                std::filesystem::create_directories(run_dir, ec);
                if (ec) {
                    reply(500, json{
                        {"ok", false},
                        {"error", "server_error"},
                        {"message", "failed to create /run/pqnas/restore"},
                        {"detail", pqnas::shorten(ec.message(), 200)}
                    });
                    return;
                }
            }

            const std::filesystem::path job_path = run_dir / (job_id + ".json");
            const std::filesystem::path tmp_path = run_dir / (job_id + ".tmp." + c.random_b64url(10));

            json job = {
                {"job_id", job_id},
                {"created_utc", created_utc},
                {"api_version", 4},
                {"service_name", "pqnas.service"},
                {"volume", {
                    {"name", plan.volume},
                    {"live_path", plan.source_subvolume},
                    {"snap_path", plan.snapshot_path}
                }},
                {"snapshot_id", plan.snapshot_id},
                {"request", {
                    {"mode", "swap"},
                    {"confirm_id", confirm_id},
                    {"actor_fp", actor_fp},
                    {"ip", req.remote_addr.empty() ? "?" : req.remote_addr}
                }}
            };

            const std::string job_text = job.dump(2) + "\n";

            {
                std::ofstream out(tmp_path, std::ios::binary | std::ios::out | std::ios::trunc);
                if (!out.good()) {
                    std::error_code ec2;
                    std::filesystem::remove(tmp_path, ec2);

                    reply(500, json{
                        {"ok", false},
                        {"error", "server_error"},
                        {"message", "failed to create temp job file"},
                        {"path", tmp_path.string()}
                    });
                    return;
                }

                out.write(job_text.data(), static_cast<std::streamsize>(job_text.size()));
                if (!out.good()) {
                    std::error_code ec2;
                    std::filesystem::remove(tmp_path, ec2);

                    reply(500, json{
                        {"ok", false},
                        {"error", "server_error"},
                        {"message", "failed to write temp job file"},
                        {"path", tmp_path.string()}
                    });
                    return;
                }
            }

            {
                std::error_code ec;
                if (std::filesystem::exists(job_path, ec) && !ec) {
                    ec.clear();
                    std::filesystem::remove(job_path, ec);
                    if (ec) {
                        std::error_code ec2;
                        std::filesystem::remove(tmp_path, ec2);

                        reply(500, json{
                            {"ok", false},
                            {"error", "server_error"},
                            {"message", "failed to overwrite existing job file"},
                            {"path", job_path.string()},
                            {"detail", pqnas::shorten(ec.message(), 200)}
                        });
                        return;
                    }
                }

                ec.clear();
                std::filesystem::rename(tmp_path, job_path, ec);
                if (ec) {
                    std::error_code ec2;
                    std::filesystem::remove(tmp_path, ec2);

                    reply(500, json{
                        {"ok", false},
                        {"error", "server_error"},
                        {"message", "failed to finalize job file"},
                        {"path", job_path.string()},
                        {"detail", pqnas::shorten(ec.message(), 200)}
                    });
                    return;
                }

                std::error_code ec_perm;
                std::filesystem::permissions(
                    job_path,
                    std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                    std::filesystem::perm_options::replace,
                    ec_perm
                );
            }

            const std::string unit = "pqnas-restore@" + job_id + ".service";
            const std::string cmd_start_restore =
                "sudo -n /usr/bin/systemctl start " + sh_quote(unit);

            std::string out_start;
            int rc_start = 0;
            if (!run_cmd_ctx(c, cmd_start_restore, &out_start, &rc_start)) {
                pqnas::AuditEvent ev;
                ev.event = "snapshots.restore_job_start";
                ev.outcome = "fail";
                ev.f["actor_fp"] = actor_fp;
                ev.f["confirm_id"] = confirm_id;
                ev.f["job_id"] = job_id;
                ev.f["volume"] = plan.volume;
                ev.f["id"] = plan.snapshot_id;
                ev.f["reason"] = "systemctl_start_failed";
                ev.f["rc"] = std::to_string(rc_start);
                ev.f["out"] = pqnas::shorten(out_start, 300);
                ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
                c.audit_append(ev);

                reply(500, json{
                    {"ok", false},
                    {"error", "restore_start_failed"},
                    {"message", "failed to start restore unit"},
                    {"job_id", job_id},
                    {"unit", unit},
                    {"rc", rc_start},
                    {"out", pqnas::shorten(out_start, 400)}
                });
                return;
            }

            {
                pqnas::AuditEvent ev;
                ev.event = "snapshots.restore_job_start";
                ev.outcome = "ok";
                ev.f["actor_fp"] = actor_fp;
                ev.f["confirm_id"] = confirm_id;
                ev.f["job_id"] = job_id;
                ev.f["volume"] = plan.volume;
                ev.f["id"] = plan.snapshot_id;
                ev.f["job_path"] = pqnas::shorten(job_path.string(), 220);
                ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
                c.audit_append(ev);
            }

            reply(200, json{
                {"ok", true},
                {"job_id", job_id},
                {"volume", plan.volume},
                {"id", plan.snapshot_id}
            });
        }
    );
}
