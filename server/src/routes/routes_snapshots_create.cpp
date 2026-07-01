#include "routes_snapshots_create.h"

#include "httplib.h"
#include "audit_log.h"
#include "audit_fields.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using json = nlohmann::json;

namespace {

void fallback_reply_json(httplib::Response& res, int status, const std::string& body) {
    res.status = status;
    res.set_content(body, "application/json; charset=utf-8");
}

void reply_json_ctx(
    const SnapshotCreateRoutesContext& c,
    httplib::Response& res,
    int status,
    const json& body
) {
    if (c.reply_json) c.reply_json(res, status, body.dump());
    else fallback_reply_json(res, status, body.dump());
}

bool context_ok(const SnapshotCreateRoutesContext& c) {
    return c.require_admin &&
           c.require_same_origin &&
           c.reply_json &&
           c.audit_append &&
           c.load_volumes &&
           c.is_btrfs_subvolume;
}

std::string trim_copy_local(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\n' || s.back() == '\r' || s.back() == '\t')) {
        s.pop_back();
    }

    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) {
        ++i;
    }

    return s.substr(i);
}

std::string lower_ascii_local(std::string s) {
    for (char& ch : s) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    return s;
}

bool starts_with_local(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

std::string utc_stamp_for_id_local() {
    using namespace std::chrono;

    auto now = system_clock::now();
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

    std::time_t tt = system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&tt, &tm);

    char buf[64];
    std::snprintf(
        buf,
        sizeof(buf),
        "%04d-%02d-%02dT%02d-%02d-%02d.%03dZ",
        tm.tm_year + 1900,
        tm.tm_mon + 1,
        tm.tm_mday,
        tm.tm_hour,
        tm.tm_min,
        tm.tm_sec,
        static_cast<int>(ms.count())
    );

    return std::string(buf);
}

bool snapshot_id_is_safe(const std::string& id) {
    for (char c : id) {
        const bool ok =
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '_' ||
            c == '-' ||
            c == '.' ||
            c == 'T' ||
            c == 'Z';

        if (!ok) return false;
    }

    return true;
}

std::string audit_header_value(
    const SnapshotCreateRoutesContext& c,
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
    const SnapshotCreateRoutesContext& c,
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

int run_btrfs_snapshot_local(
    const std::string& src,
    const std::string& dst,
    std::string* output
) {
    if (output) output->clear();

    int pipefd[2];
    if (pipe(pipefd) != 0) return 127;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return 127;
    }

    if (pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);

        close(pipefd[0]);
        close(pipefd[1]);

        execl(
            "/usr/bin/sudo",
            "sudo",
            "-n",
            "/usr/local/sbin/pqnas-btrfs-snapshot",
            "create-ro",
            src.c_str(),
            dst.c_str(),
            static_cast<char*>(nullptr)
        );

        _exit(127);
    }

    close(pipefd[1]);

    char buf[4096];
    ssize_t n;
    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
        if (output) output->append(buf, static_cast<std::size_t>(n));
    }

    close(pipefd[0]);

    int status = 0;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 128;
}

int run_restore_root_local(
    const std::vector<std::string>& args,
    std::string* output
) {
    if (output) output->clear();

    std::vector<std::string> argv_s = {
        "/usr/bin/sudo",
        "-n",
        "/usr/local/sbin/pqnas-restore-root"
    };
    argv_s.insert(argv_s.end(), args.begin(), args.end());

    int pipefd[2];
    if (pipe(pipefd) != 0) return 127;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return 127;
    }

    if (pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);

        close(pipefd[0]);
        close(pipefd[1]);

        std::vector<char*> argv;
        argv.reserve(argv_s.size() + 1);
        for (const auto& a : argv_s) {
            argv.push_back(const_cast<char*>(a.c_str()));
        }
        argv.push_back(nullptr);

        execv("/usr/bin/sudo", argv.data());
        _exit(127);
    }

    close(pipefd[1]);

    char buf[4096];
    ssize_t n;
    constexpr std::size_t kMaxOutput = 16 * 1024;
    bool truncated = false;

    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
        if (output) {
            const std::size_t have = output->size();
            if (have < kMaxOutput) {
                const std::size_t room = kMaxOutput - have;
                const std::size_t take =
                    static_cast<std::size_t>(n) < room
                        ? static_cast<std::size_t>(n)
                        : room;
                output->append(buf, take);
            }
            if (output->size() >= kMaxOutput) truncated = true;
        }
    }

    close(pipefd[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR) continue;
        return 127;
    }

    if (truncated && output) {
        output->append("\n[output truncated]\n");
    }

    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 128;
}

} // namespace

void register_snapshot_create_routes(
    httplib::Server& srv,
    const SnapshotCreateRoutesContext& ctx
) {
    const SnapshotCreateRoutesContext c = ctx;

    srv.Post("/api/v4/snapshots/create",
        [c](const httplib::Request& req, httplib::Response& res) {
            auto reply = [&](int status, const json& body) {
                reply_json_ctx(c, res, status, body);
            };

            if (!context_ok(c)) {
                reply(500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "snapshot create route context incomplete"}
                });
                return;
            }

            std::string actor_fp;
            if (!c.require_admin(req, res, &actor_fp)) return;
            if (!c.require_same_origin(req, res)) return;

            auto audit_fail = [&](const std::string& reason, int http, const std::string& detail = "") {
                pqnas::AuditEvent ev;
                ev.event = "snapshots.create";
                ev.outcome = "fail";
                ev.f["actor_fp"] = actor_fp;
                ev.f["reason"] = reason;
                ev.f["http"] = std::to_string(http);
                if (!detail.empty()) ev.f["detail"] = pqnas::shorten(detail, 180);
                ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
                add_forwarded_audit_fields(c, req, &ev);
                c.audit_append(ev);
            };

            auto audit_ok = [&](const std::string& vol, const std::string& id, const std::string& path) {
                pqnas::AuditEvent ev;
                ev.event = "snapshots.create";
                ev.outcome = "ok";
                ev.f["actor_fp"] = actor_fp;
                ev.f["volume"] = vol;
                ev.f["id"] = id;
                ev.f["path"] = pqnas::shorten(path, 140);
                ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
                add_forwarded_audit_fields(c, req, &ev);
                c.audit_append(ev);
            };

            json body;
            try {
                body = json::parse(req.body.empty() ? "{}" : req.body);
            } catch (...) {
                audit_fail("bad_json", 400);
                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid json"}});
                return;
            }

            std::string vol = trim_copy_local(body.value("volume", ""));
            std::string id = trim_copy_local(body.value("id", ""));

            if (vol.empty()) {
                audit_fail("missing_volume", 400);
                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "missing volume"}});
                return;
            }

            std::string backend;
            std::string err;
            std::vector<SnapshotCreateVolume> vols;
            if (!c.load_volumes(&backend, &vols, &err)) {
                audit_fail("settings_load_failed", 500, err);
                reply(500, json{{"ok", false}, {"error", "server_error"}, {"message", "failed to load snapshot settings"}});
                return;
            }

            auto it = std::find_if(vols.begin(), vols.end(), [&](const SnapshotCreateVolume& v) {
                return v.name == vol;
            });
            if (it == vols.end()) {
                audit_fail("unknown_volume", 404, vol);
                reply(404, json{{"ok", false}, {"error", "not_found"}, {"message", "unknown volume"}});
                return;
            }

            if (!it->enabled) {
                audit_fail("snapshots_disabled", 409, vol);
                reply(409, json{
                    {"ok", false},
                    {"error", "disabled"},
                    {"message", "snapshots are disabled for this volume"}
                });
                return;
            }

            const std::string source_subvolume = it->source_subvolume;
            const std::string snap_root = it->snap_root;

            if (!starts_with_local(source_subvolume, "/srv/pqnas/")) {
                audit_fail("live_path_not_allowed", 400, source_subvolume);
                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "live_path not allowed"}});
                return;
            }

            const std::string snap_root_norm =
                std::filesystem::path(snap_root).lexically_normal().string();

            const std::string source_norm =
                std::filesystem::path(source_subvolume).lexically_normal().string();

            const std::string pool_local_snap_root =
                (std::filesystem::path(source_norm) / ".snapshots").lexically_normal().string();

            const bool legacy_global_snap_root =
                starts_with_local(snap_root_norm, "/srv/pqnas/.snapshots/");

            const bool pool_local_snap_root_ok =
                snap_root_norm == pool_local_snap_root ||
                starts_with_local(snap_root_norm, pool_local_snap_root + "/");

            if (!legacy_global_snap_root && !pool_local_snap_root_ok) {
                audit_fail("snap_root_not_allowed", 400, snap_root);
                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "snap_root not allowed"}});
                return;
            }

            if (id.empty()) {
                id = "MANUAL_" + utc_stamp_for_id_local();
            }

            if (!snapshot_id_is_safe(id)) {
                audit_fail("invalid_id", 400, id);
                reply(400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "invalid id (allowed: A-Z a-z 0-9 _ - . T Z)"}
                });
                return;
            }

            std::error_code ec;
            std::filesystem::create_directories(snap_root_norm, ec);

            if (ec && pool_local_snap_root_ok) {
                std::string helper_out;
                // Security: root creates only validated .snapshots roots through pqnas-restore-root.
                const int rc = run_restore_root_local(
                    {"subvolume-create-snapshot-root", snap_root_norm},
                    &helper_out
                );
                if (rc == 0) {
                    ec.clear();
                } else {
                    std::cerr << "[snapshots] snap_root auto-create failed root="
                              << snap_root_norm << " rc=" << rc
                              << " detail=" << pqnas::shorten(helper_out, 240) << "\\n";
                }
            }

            if (ec) {
                audit_fail("mkdir_failed", 500, snap_root_norm);
                reply(500, json{{"ok", false}, {"error", "server_error"}, {"message", "snapshot root mkdir failed"}});
                return;
            }

            std::error_code ec_root;
            if (!std::filesystem::exists(snap_root_norm, ec_root) ||
                !std::filesystem::is_directory(snap_root_norm, ec_root)) {
                audit_fail("snap_root_missing_after_create", 500, snap_root_norm);
                reply(500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "snapshot root missing after create"}
                });
                return;
            }

            const std::filesystem::path dst = std::filesystem::path(snap_root_norm) / id;
            if (std::filesystem::exists(dst, ec) && !ec) {
                audit_fail("already_exists", 409, dst.string());
                reply(409, json{
                    {"ok", false},
                    {"error", "already_exists"},
                    {"message", "snapshot id already exists"}
                });
                return;
            }

            std::string out;
            int rc = run_btrfs_snapshot_local(source_subvolume, dst.string(), &out);

            if (rc != 0) {
                const std::string dlow = lower_ascii_local(out);
                if (dlow.find("a password is required") != std::string::npos ||
                    dlow.find("not in the sudoers") != std::string::npos ||
                    dlow.find("no tty present") != std::string::npos) {
                    audit_fail("no_privs", 403, out);
                    reply(403, json{
                        {"ok", false},
                        {"error", "no_privs"},
                        {"message", "sudo not permitted for pqnas-btrfs-snapshot helper; install snapshot helper sudoers rule"},
                        {"detail", pqnas::shorten(out, 200)}
                    });
                    return;
                }

                audit_fail("snapshot_create_failed", 500, out);
                reply(500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "snapshot create failed"},
                    {"detail", pqnas::shorten(out, 200)}
                });
                return;
            }

            std::string probe_detail;
            bool is_sub = c.is_btrfs_subvolume(dst.string(), &probe_detail);

            audit_ok(vol, id, dst.string());
            reply(200, json{
                {"ok", true},
                {"volume", vol},
                {"id", id},
                {"path", dst.string()},
                {"is_btrfs_subvolume", is_sub},
                {"probe_detail", pqnas::shorten(probe_detail, 180)}
            });
        }
    );
}
