#include "notifications/notification_routes.h"
#include "notifications/notification_settings.h"

#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>
#include <sys/wait.h>

#include <nlohmann/json.hpp>

namespace pqnas::notifications {
namespace {

using json = nlohmann::json;

void reply_json_local(const NotificationRoutesDeps& deps,
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

json parse_json_body_local(const httplib::Request& req) {
    if (req.body.empty()) return json::object();
    json j = json::parse(req.body, nullptr, false);
    return j.is_object() ? j : json::object();
}

bool require_admin_local(const NotificationRoutesDeps& deps,
                         const httplib::Request& req,
                         httplib::Response& res,
                         std::string* actor_fp_out) {
    if (!deps.users || !deps.cookie_key || !deps.require_user_auth_users_actor) {
        reply_json_local(deps, res, 500, {
            {"ok", false},
            {"error", "server_error"},
            {"message", "notification route dependencies missing"}
        });
        return false;
    }

    std::string actor_fp;
    std::string actor_role;

    if (!deps.require_user_auth_users_actor(
            req,
            res,
            deps.cookie_key,
            deps.users,
            &actor_fp,
            &actor_role)) {
        return false;
    }

    if (actor_role != "admin") {
        reply_json_local(deps, res, 403, {
            {"ok", false},
            {"error", "forbidden"},
            {"message", "admin role required"}
        });
        return false;
    }

    if (actor_fp_out) *actor_fp_out = actor_fp;
    return true;
}

std::string default_email_for_actor(const NotificationRoutesDeps& deps,
                                    const std::string& actor_fp) {
    if (!deps.users || actor_fp.empty()) return "";

    const auto u = deps.users->get(actor_fp);
    if (!u.has_value()) return "";

    return u->email;
}

json settings_response_for_actor(const NotificationRoutesDeps& deps,
                                 const std::string& actor_fp) {
    std::string err;
    const auto settings = load_notification_settings(&err);

    json out = notification_settings_public_json(
        settings,
        default_email_for_actor(deps, actor_fp));

    if (!err.empty()) {
        out["warning"] = err;
    }

    return out;
}

std::string notification_test_text(const std::string& kind) {
    if (kind == "warning") {
        return "DNA-Nexus test warning: notification delivery is working.";
    }
    return "DNA-Nexus test notification: notification delivery is working.";
}

struct ProcessResult {
    int exit_code = -1;
    bool timed_out = false;
    std::string output;
};

ProcessResult run_process_capture_local(const std::vector<std::string>& argv,
                                        std::chrono::seconds timeout,
                                        std::size_t max_output_bytes) {
    ProcessResult result;

    if (argv.empty() || argv[0].empty()) {
        result.exit_code = 127;
        return result;
    }

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        result.exit_code = 127;
        return result;
    }

    pid_t pid = fork();
    if (pid == 0) {
        // Child.
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        std::vector<char*> cargv;
        cargv.reserve(argv.size() + 1);
        for (const auto& a : argv) {
            cargv.push_back(const_cast<char*>(a.c_str()));
        }
        cargv.push_back(nullptr);

        execv(cargv[0], cargv.data());
        _exit(127);
    }

    close(pipefd[1]);

    if (pid < 0) {
        close(pipefd[0]);
        result.exit_code = 127;
        return result;
    }

    const int flags = fcntl(pipefd[0], F_GETFL, 0);
    if (flags >= 0) {
        fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);
    }

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    int status = 0;
    bool exited = false;

    while (true) {
        char buf[512];
        while (true) {
            const ssize_t n = read(pipefd[0], buf, sizeof(buf));
            if (n > 0) {
                if (result.output.size() < max_output_bytes) {
                    const std::size_t remaining = max_output_bytes - result.output.size();
                    result.output.append(buf, buf + std::min<std::size_t>(remaining, static_cast<std::size_t>(n)));
                }
                continue;
            }
            break;
        }

        const pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid) {
            exited = true;
            break;
        }

        if (std::chrono::steady_clock::now() >= deadline) {
            result.timed_out = true;
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Final drain after process exit/kill.
    char buf[512];
    while (true) {
        const ssize_t n = read(pipefd[0], buf, sizeof(buf));
        if (n > 0) {
            if (result.output.size() < max_output_bytes) {
                const std::size_t remaining = max_output_bytes - result.output.size();
                result.output.append(buf, buf + std::min<std::size_t>(remaining, static_cast<std::size_t>(n)));
            }
            continue;
        }
        break;
    }

    close(pipefd[0]);

    if (result.timed_out) {
        result.exit_code = 124;
    } else if (exited && WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else {
        result.exit_code = 1;
    }

    return result;
}

json send_telegram_message(const NotificationSettings& s, const std::string& text) {
    if (s.telegram_bot_token.empty() || s.telegram_chat_id.empty()) {
        return {
            {"ok", false},
            {"error", "telegram_not_configured"},
            {"message", "Telegram bot token and chat ID are required"}
        };
    }

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    httplib::SSLClient cli("api.telegram.org", 443);
    cli.set_connection_timeout(10, 0);
    cli.set_read_timeout(20, 0);
    cli.set_write_timeout(20, 0);
    cli.set_follow_location(true);

    httplib::Params params;
    params.emplace("chat_id", s.telegram_chat_id);
    params.emplace("text", text);
    params.emplace("disable_web_page_preview", "true");

    const std::string path = "/bot" + s.telegram_bot_token + "/sendMessage";
    auto res = cli.Post(path.c_str(), params);

    if (!res) {
        return {
            {"ok", false},
            {"error", "telegram_request_failed"},
            {"message", "Telegram HTTPS request failed"}
        };
    }

    if (res->status < 200 || res->status >= 300) {
        return {
            {"ok", false},
            {"error", "telegram_http_error"},
            {"status", res->status},
            {"message", "Telegram returned HTTP " + std::to_string(res->status)},
            {"body_snip", res->body.substr(0, 300)}
        };
    }

    json body = json::parse(res->body, nullptr, false);
    if (body.is_object() && body.contains("ok") && body["ok"].is_boolean() && !body["ok"].get<bool>()) {
        return {
            {"ok", false},
            {"error", "telegram_api_error"},
            {"message", body.value("description", "Telegram API returned ok=false")}
        };
    }

    return {
        {"ok", true},
        {"message", "Telegram test sent"}
    };
#else
    (void)s;
    (void)text;
    return {
        {"ok", false},
        {"error", "telegram_tls_not_available"},
        {"message", "Server was built without CPPHTTPLIB_OPENSSL_SUPPORT"}
    };
#endif
}

} // namespace

void register_notification_routes(httplib::Server& srv, const NotificationRoutesDeps& deps) {
    srv.Get("/api/v4/admin/notifications/settings",
        [deps](const httplib::Request& req, httplib::Response& res) {
            std::string actor_fp;
            if (!require_admin_local(deps, req, res, &actor_fp)) return;

            reply_json_local(deps, res, 200, settings_response_for_actor(deps, actor_fp));
        });

    srv.Post("/api/v4/admin/notifications/settings",
        [deps](const httplib::Request& req, httplib::Response& res) {
            std::string actor_fp;
            if (!require_admin_local(deps, req, res, &actor_fp)) return;

            const json body = parse_json_body_local(req);

            NotificationSettings saved;
            std::string save_err;
            if (!update_notification_settings_from_json_patch(body, &saved, &save_err)) {
                reply_json_local(deps, res, 500, {
                    {"ok", false},
                    {"error", "save_failed"},
                    {"message", save_err.empty() ? "Failed to save notification settings" : save_err}
                });
                return;
            }

            reply_json_local(
                deps,
                res,
                200,
                notification_settings_public_json(saved, default_email_for_actor(deps, actor_fp)));
        });

    srv.Post("/api/v4/admin/notifications/test-telegram",
        [deps](const httplib::Request& req, httplib::Response& res) {
            std::string actor_fp;
            if (!require_admin_local(deps, req, res, &actor_fp)) return;

            std::string load_err;
            NotificationSettings s = load_notification_settings(&load_err);

            // Allow unsaved values from the UI for testing.
            const json body = parse_json_body_local(req);
            s = notification_settings_from_json_patch(s, body);

            const std::string kind = body.value("kind", "notification");
            const json result = send_telegram_message(s, notification_test_text(kind));
            reply_json_local(deps, res, result.value("ok", false) ? 200 : 400, result);
        });

    srv.Post("/api/v4/admin/notifications/test-email",
        [deps](const httplib::Request& req, httplib::Response& res) {
            std::string actor_fp;
            if (!require_admin_local(deps, req, res, &actor_fp)) return;

            (void)req;

            const auto pr = run_process_capture_local(
                {"/usr/local/libexec/pqnas/pqnas_notify.py", "--test-email"},
                std::chrono::seconds(30),
                4096);

            if (pr.exit_code == 0 && !pr.timed_out) {
                reply_json_local(deps, res, 200, {
                    {"ok", true},
                    {"message", "Email test sent"}
                });
                return;
            }

            reply_json_local(deps, res, 500, {
                {"ok", false},
                {"error", pr.timed_out ? "email_test_timeout" : "email_test_failed"},
                {"message", pr.timed_out
                    ? "Email test timed out."
                    : "Email test failed. Run pqnas_notify.py --test-email on the server for diagnostics."}
            });
        });
}

} // namespace pqnas::notifications
