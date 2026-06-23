#include "routes_drive_locate.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <map>
#include <string>
#include <vector>
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

CommandResult run_wrapper_with_args(const std::string& wrapper_path,
                                    const std::vector<std::string>& args) {
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

        std::vector<char*> argv;
        argv.push_back(const_cast<char*>("/usr/bin/sudo"));
        argv.push_back(const_cast<char*>("-n"));
        argv.push_back(const_cast<char*>(wrapper_path.c_str()));
        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        execv("/usr/bin/sudo", argv.data());
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


bool valid_idrac_host_arg(const std::string& v) {
    if (v.empty() || v.size() > 255) return false;
    for (unsigned char c : v) {
        if (!(std::isalnum(c) || c == '.' || c == '-' || c == '_' || c == ':')) return false;
    }
    return true;
}

bool valid_idrac_user_arg(const std::string& v) {
    if (v.empty() || v.size() > 128) return false;
    for (unsigned char c : v) {
        if (!(std::isalnum(c) || c == '.' || c == '-' || c == '_')) return false;
    }
    return true;
}

bool valid_idrac_port_arg(const std::string& v) {
    if (v.empty() || v.size() > 5) return false;
    for (unsigned char c : v) {
        if (!std::isdigit(c)) return false;
    }
    try {
        const int n = std::stoi(v);
        return n >= 1 && n <= 65535;
    } catch (...) {
        return false;
    }
}

void handle_idrac_backend_action(const DriveLocateRoutesDeps& deps,
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

    const std::string wrapper = deps.wrapper_path.empty()
        ? "/usr/local/sbin/pqnas-drive-locate"
        : deps.wrapper_path;

    std::vector<std::string> args{"--action", action};

    if (action == "idrac-save-config") {
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

        const bool enabled = body.value("enabled", false);
        const std::string host = trim_copy(body.value("host", std::string{}));
        const std::string port = trim_copy(body.value("port", std::string{"22"}));
        const std::string user = trim_copy(body.value("user", std::string{}));

        if (!valid_idrac_host_arg(host)) {
            reply_json_local(deps, res, 400, json{{"ok", false}, {"error", "invalid_host"}, {"message", "invalid iDRAC host/IP"}});
            return;
        }
        if (!valid_idrac_port_arg(port)) {
            reply_json_local(deps, res, 400, json{{"ok", false}, {"error", "invalid_port"}, {"message", "invalid iDRAC SSH port"}});
            return;
        }
        if (!valid_idrac_user_arg(user)) {
            reply_json_local(deps, res, 400, json{{"ok", false}, {"error", "invalid_user"}, {"message", "invalid iDRAC username"}});
            return;
        }

        args.insert(args.end(), {
            "--enabled", enabled ? "1" : "0",
            "--host", host,
            "--port", port,
            "--user", user
        });
    }

    const CommandResult cr = run_wrapper_with_args(wrapper, args);

    std::map<std::string, std::string> audit_fields{
        {"action", action},
        {"actor", audit_trunc(actor_fp, 128)},
        {"exit_code", std::to_string(cr.exit_code)}
    };

    if (cr.exit_code != 0) {
        audit_fields["wrapper_output"] = audit_trunc(cr.output);
        emit_audit(deps, "error", audit_fields);

        std::string msg = trim_copy(cr.output);
        if (msg.empty()) msg = "iDRAC backend operation failed";
        msg = audit_trunc(msg, 900);

        reply_json_local(deps, res, 500, json{
            {"ok", false},
            {"error", "idrac_backend_failed"},
            {"message", msg},
            {"output", cr.output},
            {"exit_code", cr.exit_code}
        });
        return;
    }

    emit_audit(deps, "ok", audit_fields);

    reply_json_local(deps, res, 200, json{
        {"ok", true},
        {"action", action},
        {"output", cr.output}
    });
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

    const CommandResult cr = run_wrapper_with_args(wrapper, {
        "--action", action,
        "--device", device
    });
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


    srv.Get("/api/v4/system/drives/locate/idrac/config",
            [deps](const httplib::Request& req, httplib::Response& res) {
                handle_idrac_backend_action(deps, req, res, "idrac-status");
            });

    srv.Post("/api/v4/system/drives/locate/idrac/save",
             [deps](const httplib::Request& req, httplib::Response& res) {
                 handle_idrac_backend_action(deps, req, res, "idrac-save-config");
             });

    srv.Post("/api/v4/system/drives/locate/idrac/generate-key",
             [deps](const httplib::Request& req, httplib::Response& res) {
                 handle_idrac_backend_action(deps, req, res, "idrac-generate-key");
             });

    srv.Get("/api/v4/system/drives/locate/idrac/public-key",
            [deps](const httplib::Request& req, httplib::Response& res) {
                handle_idrac_backend_action(deps, req, res, "idrac-public-key");
            });

    srv.Post("/api/v4/system/drives/locate/idrac/test-connection",
             [deps](const httplib::Request& req, httplib::Response& res) {
                 handle_idrac_backend_action(deps, req, res, "idrac-test-connection");
             });

    srv.Post("/api/v4/system/drives/locate/idrac/test-inventory",
             [deps](const httplib::Request& req, httplib::Response& res) {
                 handle_idrac_backend_action(deps, req, res, "idrac-test-inventory");
             });
}

} // namespace pqnas
