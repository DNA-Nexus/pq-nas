#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace pqnas::notifications {

struct NotificationSettings {
    bool info_email_enabled = true;
    bool info_telegram_enabled = false;
    bool warnings_email_enabled = true;
    bool warnings_telegram_enabled = false;
    bool weekly_summary_enabled = true;

    std::vector<std::string> extra_emails;

    // Secret: never return this raw value to browser.
    std::string telegram_bot_token;
    std::string telegram_chat_id;
};

std::filesystem::path notification_settings_path();

NotificationSettings load_notification_settings(std::string* err = nullptr);
bool save_notification_settings(const NotificationSettings& s, std::string* err = nullptr);

NotificationSettings notification_settings_from_json_patch(
    const NotificationSettings& current,
    const nlohmann::json& patch);

nlohmann::json notification_settings_public_json(
    const NotificationSettings& s,
    const std::string& default_email);

std::string mask_secret_for_admin(const std::string& secret);

} // namespace pqnas::notifications
