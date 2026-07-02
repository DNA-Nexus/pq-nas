#include "routes_snapshots_restore.h"

#include "httplib.h"
#include "audit_log.h"
#include "audit_fields.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <csignal>
#include <chrono>
#include <cerrno>
#include <array>
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
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

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
           c.require_same_origin &&
           c.reply_json &&
           c.audit_append &&
           c.load_volumes &&
           c.is_path_under &&
           c.is_btrfs_subvolume &&
           c.realpath_str &&
           c.now_iso_utc &&
           c.rand_hex_32 &&
           c.random_b64url;
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

bool restore_job_id_is_safe(const std::string& id) {
    // Security: job IDs are used for run-dir filenames and systemd instance names.
    // Accept only server-generated RJOB_<base64url> values.
    if (id.size() != 29) return false;
    if (id.rfind("RJOB_", 0) != 0) return false;

    for (std::size_t i = 5; i < id.size(); ++i) {
        const char c = id[i];
        const bool ok =
            (c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '_' ||
            c == '-';
        if (!ok) return false;
    }

    return true;
}

std::string restore_unit_name_for_display(const std::string& job_id) {
    return "pqnas-restore@" + job_id + ".service";
}

struct RestoreRootResult {
    int exit_code = 127;
    std::string output;
};

RestoreRootResult run_restore_root_argv(
    const std::vector<std::string>& args,
    int timeout_ms = 10000,
    std::size_t max_bytes = 16 * 1024
) {
    RestoreRootResult result;

    std::vector<std::string> argv_s = {
        "/usr/bin/sudo",
        "-n",
        "/usr/local/sbin/pqnas-restore-root"
    };
    argv_s.insert(argv_s.end(), args.begin(), args.end());

    int pipefd[2] = {-1, -1};
    if (::pipe(pipefd) != 0) {
        result.output = std::string("pipe failed: ") + std::strerror(errno);
        return result;
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        const int saved = errno;
        ::close(pipefd[0]);
        ::close(pipefd[1]);
        result.output = std::string("fork failed: ") + std::strerror(saved);
        return result;
    }

    if (pid == 0) {
        ::close(pipefd[0]);
        ::dup2(pipefd[1], STDOUT_FILENO);
        ::dup2(pipefd[1], STDERR_FILENO);
        ::close(pipefd[1]);

        std::vector<char*> argv;
        argv.reserve(argv_s.size() + 1);
        for (const auto& a : argv_s) {
            argv.push_back(const_cast<char*>(a.c_str()));
        }
        argv.push_back(nullptr);

        ::execv("/usr/bin/sudo", argv.data());
        _exit(127);
    }

    ::close(pipefd[1]);

    const int flags = ::fcntl(pipefd[0], F_GETFL, 0);
    if (flags >= 0) {
        (void)::fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);
    }

    bool truncated = false;
    auto append_bytes = [&](const char* buf, ssize_t n) {
        if (n <= 0) return;
        const std::size_t have = result.output.size();
        if (have < max_bytes) {
            const std::size_t room = max_bytes - have;
            const std::size_t take = std::min<std::size_t>(
                static_cast<std::size_t>(n),
                room
            );
            result.output.append(buf, take);
        }
        if (static_cast<std::size_t>(n) > 0 && result.output.size() >= max_bytes) {
            truncated = true;
        }
    };

    auto drain = [&]() {
        std::array<char, 4096> buf{};
        for (;;) {
            const ssize_t n = ::read(pipefd[0], buf.data(), buf.size());
            if (n > 0) {
                append_bytes(buf.data(), n);
                continue;
            }
            if (n == 0) return;
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            return;
        }
    };

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    int status = 0;
    for (;;) {
        drain();

        const pid_t w = ::waitpid(pid, &status, WNOHANG);
        if (w == pid) break;

        if (w < 0 && errno != EINTR) {
            result.output += "\nwaitpid failed: ";
            result.output += std::strerror(errno);
            result.exit_code = 127;
            ::close(pipefd[0]);
            return result;
        }

        if (std::chrono::steady_clock::now() >= deadline) {
            // Security: cap privileged helper runtime so restore polling cannot hang a worker.
            (void)::kill(pid, SIGKILL);
            (void)::waitpid(pid, &status, 0);
            drain();
            ::close(pipefd[0]);
            if (truncated) result.output += "\n[output truncated]\n";
            result.output += "\ncommand timed out";
            result.exit_code = 124;
            return result;
        }

        ::usleep(10 * 1000);
    }

    drain();
    ::close(pipefd[0]);

    if (truncated) result.output += "\n[output truncated]\n";

    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exit_code = 128 + WTERMSIG(status);
    } else {
        result.exit_code = 127;
    }

    return result;
}

bool run_restore_root_ctx(
    const std::vector<std::string>& args,
    std::string* out,
    int* rc_out
) {
    const RestoreRootResult r = run_restore_root_argv(args);
    if (out) *out = r.output;
    if (rc_out) *rc_out = r.exit_code;
    return r.exit_code == 0;
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
    // Security: availability probe goes through the guarded restore helper,
    // not through a direct systemctl sudo wildcard.
    const RestoreRootResult r = run_restore_root_argv(
        {"systemctl-status-pqnas"},
        5000,
        4096
    );
    return r.exit_code == 0;
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
            if (!c.require_same_origin(req, res)) return;

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
            if (!restore_job_id_is_safe(job_id)) {
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

            std::string out_show;
            int rc_show = 0;

            // Security: helper builds pqnas-restore@<job_id>.service after validating
            // the server-generated RJOB_ id; no caller-controlled unit reaches systemctl.
            if (run_restore_root_ctx({"systemctl-show-restore", job_id}, &out_show, &rc_show)) {
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
                    {"unit", restore_unit_name_for_display(job_id)},
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
            if (!c.require_same_origin(req, res)) return;

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
            const std::string unit = restore_unit_name_for_display(job_id);
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

            std::string out_start;
            int rc_start = 0;

            // Security: start only the validated restore unit derived by the helper.
            if (!run_restore_root_ctx({"systemctl-start-restore", job_id}, &out_start, &rc_start)) {
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
                    {"unit", restore_unit_name_for_display(job_id)},
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
