#include "notifications/notification_settings.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <system_error>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace pqnas::notifications {
namespace {

using json = nlohmann::json;

std::string getenv_str_local(const char* key, const std::string& fallback) {
    const char* v = std::getenv(key);
    if (!v || !*v) return fallback;
    return std::string(v);
}

bool json_bool_local(const json& j, const char* key, bool fallback) {
    if (!j.contains(key)) return fallback;
    if (!j.at(key).is_boolean()) return fallback;
    return j.at(key).get<bool>();
}

std::string json_string_local(const json& j, const char* key, const std::string& fallback) {
    if (!j.contains(key)) return fallback;
    if (!j.at(key).is_string()) return fallback;
    return j.at(key).get<std::string>();
}

int json_int_local(const json& j, const char* key, int fallback) {
    if (!j.contains(key)) return fallback;
    if (j.at(key).is_number_integer()) return j.at(key).get<int>();
    if (j.at(key).is_string()) {
        try {
            return std::stoi(j.at(key).get<std::string>());
        } catch (...) {
            return fallback;
        }
    }
    return fallback;
}

std::vector<std::string> json_string_array_local(const json& j, const char* key, const std::vector<std::string>& fallback) {
    if (!j.contains(key) || !j.at(key).is_array()) return fallback;

    std::vector<std::string> out;
    for (const auto& item : j.at(key)) {
        if (!item.is_string()) continue;
        std::string v = item.get<std::string>();
        if (!v.empty()) out.push_back(v);
    }
    return out;
}

json to_private_json_local(const NotificationSettings& s) {
    return json{
        {"version", 1},
        {"info_email_enabled", s.info_email_enabled},
        {"info_telegram_enabled", s.info_telegram_enabled},
        {"warnings_email_enabled", s.warnings_email_enabled},
        {"warnings_telegram_enabled", s.warnings_telegram_enabled},
        {"weekly_summary_enabled", s.weekly_summary_enabled},
        {"extra_emails", s.extra_emails},
        {"telegram_bot_token", s.telegram_bot_token},
        {"telegram_chat_id", s.telegram_chat_id},
        {"smtp_host", s.smtp_host},
        {"smtp_port", s.smtp_port},
        {"smtp_tls", s.smtp_tls},
        {"smtp_user", s.smtp_user},
        {"smtp_password", s.smtp_password},
        {"smtp_from", s.smtp_from}
    };
}

NotificationSettings from_private_json_local(const json& j) {
    NotificationSettings s;

    s.info_email_enabled = json_bool_local(j, "info_email_enabled", s.info_email_enabled);
    s.info_telegram_enabled = json_bool_local(j, "info_telegram_enabled", s.info_telegram_enabled);
    s.warnings_email_enabled = json_bool_local(j, "warnings_email_enabled", s.warnings_email_enabled);
    s.warnings_telegram_enabled = json_bool_local(j, "warnings_telegram_enabled", s.warnings_telegram_enabled);
    s.weekly_summary_enabled = json_bool_local(j, "weekly_summary_enabled", s.weekly_summary_enabled);

    s.extra_emails = json_string_array_local(j, "extra_emails", s.extra_emails);
    s.telegram_bot_token = json_string_local(j, "telegram_bot_token", s.telegram_bot_token);
    s.telegram_chat_id = json_string_local(j, "telegram_chat_id", s.telegram_chat_id);

    s.smtp_host = json_string_local(j, "smtp_host", s.smtp_host);
    s.smtp_port = json_int_local(j, "smtp_port", s.smtp_port);
    s.smtp_tls = json_string_local(j, "smtp_tls", s.smtp_tls);
    s.smtp_user = json_string_local(j, "smtp_user", s.smtp_user);
    s.smtp_password = json_string_local(j, "smtp_password", s.smtp_password);
    s.smtp_from = json_string_local(j, "smtp_from", s.smtp_from);

    return s;
}

class FileLockLocal {
public:
    FileLockLocal(const std::filesystem::path& path, std::string* err) {
        if (err) err->clear();

        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            if (err) *err = "failed to create lock directory: " + ec.message();
            return;
        }

        fd_ = open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        if (fd_ < 0) {
            if (err) {
                *err = "failed to open notification settings lock: ";
                *err += std::strerror(errno);
            }
            return;
        }

        struct flock fl {};
        fl.l_type = F_WRLCK;
        fl.l_whence = SEEK_SET;

        if (fcntl(fd_, F_SETLKW, &fl) != 0) {
            if (err) {
                *err = "failed to acquire notification settings lock: ";
                *err += std::strerror(errno);
            }
            close(fd_);
            fd_ = -1;
            return;
        }

        locked_ = true;
    }

    FileLockLocal(const FileLockLocal&) = delete;
    FileLockLocal& operator=(const FileLockLocal&) = delete;

    ~FileLockLocal() {
        if (fd_ >= 0) {
            if (locked_) {
                struct flock fl {};
                fl.l_type = F_UNLCK;
                fl.l_whence = SEEK_SET;
                (void)fcntl(fd_, F_SETLK, &fl);
            }
            close(fd_);
        }
    }

    bool ok() const {
        return locked_;
    }

private:
    int fd_ = -1;
    bool locked_ = false;
};

std::filesystem::path notification_settings_lock_path_local(const std::filesystem::path& path) {
    return std::filesystem::path(path.string() + ".lock");
}

NotificationSettings load_notification_settings_unlocked_local(
    const std::filesystem::path& path,
    std::string* err) {
    if (err) err->clear();

    NotificationSettings defaults;

    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return defaults;
    }

    std::ifstream in(path);
    if (!in) {
        if (err) *err = "failed to open notifications settings";
        return defaults;
    }

    json j = json::parse(in, nullptr, false);
    if (!j.is_object()) {
        if (err) *err = "invalid notifications settings json";
        return defaults;
    }

    return from_private_json_local(j);
}

bool save_notification_settings_unlocked_local(
    const std::filesystem::path& path,
    const NotificationSettings& s,
    std::string* err) {
    if (err) err->clear();

    const auto dir = path.parent_path();

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        if (err) *err = "failed to create config directory: " + ec.message();
        return false;
    }

    const auto tmp = path.string() + ".tmp";

    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) {
            if (err) *err = "failed to write temporary notifications settings";
            return false;
        }
        out << to_private_json_local(s).dump(2) << "\n";
    }

    chmod(tmp.c_str(), 0600);

    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp);
        if (err) *err = "failed to replace notifications settings: " + ec.message();
        return false;
    }

    chmod(path.c_str(), 0600);
    return true;
}

} // namespace

std::filesystem::path notification_settings_path() {
    // Security: notification settings contain secret-bearing fields such as
    // Telegram bot tokens and SMTP passwords. Do not allow a per-file
    // environment override to redirect this store; keep it under the trusted
    // server config root with a fixed filename.
    std::string cfg = getenv_str_local("PQNAS_CONFIG_DIR", "");
    if (cfg.empty()) {
        cfg = getenv_str_local("PQNAS_CONFIG", "/etc/pqnas");
    }

    return std::filesystem::path(cfg) / "notifications.json";
}

NotificationSettings load_notification_settings(std::string* err) {
    if (err) err->clear();

    const auto path = notification_settings_path();

    FileLockLocal lock(notification_settings_lock_path_local(path), err);
    if (!lock.ok()) {
        return NotificationSettings{};
    }

    return load_notification_settings_unlocked_local(path, err);
}

bool save_notification_settings(const NotificationSettings& s, std::string* err) {
    if (err) err->clear();

    const auto path = notification_settings_path();

    FileLockLocal lock(notification_settings_lock_path_local(path), err);
    if (!lock.ok()) {
        return false;
    }

    return save_notification_settings_unlocked_local(path, s, err);
}

NotificationSettings notification_settings_from_json_patch(
    const NotificationSettings& current,
    const nlohmann::json& patch) {
    NotificationSettings s = current;

    s.info_email_enabled = json_bool_local(patch, "info_email_enabled", s.info_email_enabled);
    s.info_telegram_enabled = json_bool_local(patch, "info_telegram_enabled", s.info_telegram_enabled);
    s.warnings_email_enabled = json_bool_local(patch, "warnings_email_enabled", s.warnings_email_enabled);
    s.warnings_telegram_enabled = json_bool_local(patch, "warnings_telegram_enabled", s.warnings_telegram_enabled);
    s.weekly_summary_enabled = json_bool_local(patch, "weekly_summary_enabled", s.weekly_summary_enabled);

    s.extra_emails = json_string_array_local(patch, "extra_emails", s.extra_emails);
    s.telegram_chat_id = json_string_local(patch, "telegram_chat_id", s.telegram_chat_id);

    // Secret update policy:
    // - missing/empty token preserves existing token
    // - telegram_bot_token_clear=true clears it
    // - non-empty telegram_bot_token replaces it
    if (json_bool_local(patch, "telegram_bot_token_clear", false)) {
        s.telegram_bot_token.clear();
    } else if (patch.contains("telegram_bot_token") && patch.at("telegram_bot_token").is_string()) {
        const std::string v = patch.at("telegram_bot_token").get<std::string>();
        if (!v.empty()) s.telegram_bot_token = v;
    }

    s.smtp_host = json_string_local(patch, "smtp_host", s.smtp_host);
    s.smtp_port = json_int_local(patch, "smtp_port", s.smtp_port);
    s.smtp_tls = json_string_local(patch, "smtp_tls", s.smtp_tls);
    s.smtp_user = json_string_local(patch, "smtp_user", s.smtp_user);
    s.smtp_from = json_string_local(patch, "smtp_from", s.smtp_from);

    // Secret update policy:
    // - missing/empty password preserves existing password
    // - smtp_password_clear=true clears it
    // - non-empty smtp_password replaces it
    if (json_bool_local(patch, "smtp_password_clear", false)) {
        s.smtp_password.clear();
    } else if (patch.contains("smtp_password") && patch.at("smtp_password").is_string()) {
        const std::string v = patch.at("smtp_password").get<std::string>();
        if (!v.empty()) s.smtp_password = v;
    }

    return s;
}

bool update_notification_settings_from_json_patch(
    const nlohmann::json& patch,
    NotificationSettings* saved,
    std::string* err) {
    if (err) err->clear();

    const auto path = notification_settings_path();

    FileLockLocal lock(notification_settings_lock_path_local(path), err);
    if (!lock.ok()) {
        return false;
    }

    std::string load_err;
    const auto current = load_notification_settings_unlocked_local(path, &load_err);
    if (!load_err.empty() && err) {
        *err = load_err;
        return false;
    }

    const auto next = notification_settings_from_json_patch(current, patch);

    if (!save_notification_settings_unlocked_local(path, next, err)) {
        return false;
    }

    if (saved) {
        *saved = next;
    }

    return true;
}

std::string mask_secret_for_admin(const std::string& secret) {
    if (secret.empty()) return "";
    if (secret.size() <= 10) return "••••";
    return secret.substr(0, 6) + "…" + secret.substr(secret.size() - 4);
}

nlohmann::json notification_settings_public_json(
    const NotificationSettings& s,
    const std::string& default_email) {
    return nlohmann::json{
        {"ok", true},
        {"default_email", default_email},
        {"settings", {
            {"info_email_enabled", s.info_email_enabled},
            {"info_telegram_enabled", s.info_telegram_enabled},
            {"warnings_email_enabled", s.warnings_email_enabled},
            {"warnings_telegram_enabled", s.warnings_telegram_enabled},
            {"weekly_summary_enabled", s.weekly_summary_enabled},
            {"extra_emails", s.extra_emails},
            {"telegram_chat_id", s.telegram_chat_id},
            {"telegram_bot_token_present", !s.telegram_bot_token.empty()},
            {"telegram_bot_token_masked", mask_secret_for_admin(s.telegram_bot_token)},
            {"smtp_host", s.smtp_host},
            {"smtp_port", s.smtp_port},
            {"smtp_tls", s.smtp_tls},
            {"smtp_user", s.smtp_user},
            {"smtp_from", s.smtp_from},
            {"smtp_password_present", !s.smtp_password.empty()},
            {"smtp_password_masked", mask_secret_for_admin(s.smtp_password)}
        }}
    };
}

} // namespace pqnas::notifications
