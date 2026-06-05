#include "routes_drive_locate.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <map>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace pqnas {

namespace {

using json = nlohmann::json;

struct CommandResult {
    int exit_code = -1;
    std::string output;
};

void reply_json_local(const DriveLocateRoutesDeps& deps,
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

std::string trim_copy(std::string s) {
    auto is_ws = [](unsigned char c) {
        return std::isspace(c) != 0;
    };

    while (!s.empty() && is_ws(static_cast<unsigned char>(s.front()))) {
        s.erase(s.begin());
    }
    while (!s.empty() && is_ws(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
    return s;
}

bool valid_action(const std::string& action) {
    return action == "locate-on" || action == "locate-off";
}

bool valid_device_arg(const std::string& device) {
    if (device.empty() || device.size() > 512) return false;
    if (device.rfind("/dev/", 0) != 0) return false;

    for (unsigned char c : device) {
        if (c == '\0' || c == '\n' || c == '\r' || c == '\t') return false;
    }

    return true;
}

std::string audit_trunc(std::string s, std::size_t max_len = 240) {
    for (char& c : s) {
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    }
    if (s.size() > max_len) {
        s.resize(max_len);
        s += "...";
    }
    return s;
}

CommandResult run_drive_locate_wrapper(const std::string& wrapper_path,
                                       const std::string& action,
                                       const std::string& device) {
    CommandResult result;

    int pipefd[2] = {-1, -1};
    if (pipe(pipefd) != 0) {
        result.exit_code = 127;
        result.output = std::string("pipe failed: ") + std::strerror(errno);
        return result;
    }

    pid_t pid = fork();
    if (pid < 0) {
        const int saved = errno;
        close(pipefd[0]);
        close(pipefd[1]);
        result.exit_code = 127;
        result.output = std::string("fork failed: ") + std::strerror(saved);
        return result;
    }

    if (pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);

        execl("/usr/bin/sudo",
              "sudo",
              "-n",
              wrapper_path.c_str(),
              "--action",
              action.c_str(),
              "--device",
              device.c_str(),
              static_cast<char*>(nullptr));

        _exit(127);
    }

    close(pipefd[1]);

    std::array<char, 4096> buf{};
    std::string out;
    constexpr std::size_t kMaxOutput = 16384;

    while (true) {
        ssize_t n = read(pipefd[0], buf.data(), buf.size());
        if (n > 0) {
            const std::size_t room = kMaxOutput > out.size() ? kMaxOutput - out.size() : 0;
            if (room > 0) {
                out.append(buf.data(), std::min<std::size_t>(static_cast<std::size_t>(n), room));
            }
            continue;
        }
        if (n == 0) break;
        if (errno == EINTR) continue;
        out += "\nread failed: ";
        out += std::strerror(errno);
        break;
    }

    close(pipefd[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR) continue;
        result.exit_code = 127;
        result.output = out + "\nwaitpid failed: " + std::strerror(errno);
        return result;
    }

    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exit_code = 128 + WTERMSIG(status);
    } else {
        result.exit_code = 127;
    }

    result.output = out;
    return result;
}

void emit_audit(const DriveLocateRoutesDeps& deps,
                const std::string& outcome,
                const std::map<std::string, std::string>& fields) {
    if (deps.audit_emit) {
        deps.audit_emit("drive.locate", outcome, fields);
    }
}

void handle_drive_locate(const DriveLocateRoutesDeps& deps,
                         const httplib::Request& req,
                         httplib::Response& res,
                         const std::string& action) {
    if (!deps.require_admin_actor || !deps.reply_json) {
        reply_json_local(deps, res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "drive locate route dependencies missing"}
        });
        return;
    }

    std::string actor_fp;
    if (!deps.require_admin_actor(req, res, &actor_fp)) {
        return;
    }

    if (!valid_action(action)) {
        reply_json_local(deps, res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "invalid internal drive locate action"}
        });
        return;
    }

    json body;
    try {
        body = json::parse(req.body.empty() ? "{}" : req.body);
    } catch (...) {
        reply_json_local(deps, res, 400, json{
            {"ok", false},
            {"error", "bad_json"},
            {"message", "request body must be JSON"}
        });
        return;
    }

    const std::string device = trim_copy(
        body.value("device",
            body.value("by_id",
                body.value("path", std::string{}))));

    std::map<std::string, std::string> audit_fields{
        {"action", action},
        {"device", audit_trunc(device)},
        {"actor", audit_trunc(actor_fp, 128)}
    };

    if (!valid_device_arg(device)) {
        audit_fields["reason"] = "invalid_device";
        emit_audit(deps, "denied", audit_fields);

        reply_json_local(deps, res, 400, json{
            {"ok", false},
            {"error", "invalid_device"},
            {"message", "device must be a /dev/... block-device path or /dev/disk/by-id/... symlink"}
        });
        return;
    }

    const std::string wrapper = deps.wrapper_path.empty()
        ? "/usr/local/sbin/pqnas-drive-locate"
        : deps.wrapper_path;

    const CommandResult cr = run_drive_locate_wrapper(wrapper, action, device);
    audit_fields["exit_code"] = std::to_string(cr.exit_code);

    if (cr.exit_code != 0) {
        audit_fields["wrapper_output"] = audit_trunc(cr.output);
        emit_audit(deps, "error", audit_fields);

        std::string msg = trim_copy(cr.output);
        if (msg.empty()) {
            msg = "drive locate wrapper failed";
        }
        msg = audit_trunc(msg, 900);

        reply_json_local(deps, res, 500, json{
            {"ok", false},
            {"error", "locate_failed"},
            {"message", msg},
            {"detail", "drive locate wrapper failed"},
            {"exit_code", cr.exit_code},
            {"output", cr.output}
        });
        return;
    }

    emit_audit(deps, "ok", audit_fields);

    reply_json_local(deps, res, 200, json{
        {"ok", true},
        {"action", action},
        {"device", device},
        {"output", cr.output}
    });
}

} // namespace

void register_drive_locate_routes(httplib::Server& srv,
                                  const DriveLocateRoutesDeps& deps) {
    srv.Post("/api/v4/system/drives/locate/start",
             [deps](const httplib::Request& req, httplib::Response& res) {
                 handle_drive_locate(deps, req, res, "locate-on");
             });

    srv.Post("/api/v4/system/drives/locate/stop",
             [deps](const httplib::Request& req, httplib::Response& res) {
                 handle_drive_locate(deps, req, res, "locate-off");
             });
}

} // namespace pqnas
