#include "branding_config.h"

#include "runtime_paths.h"

#include <fstream>

namespace pqnas {
namespace {

std::string json_string_or(const nlohmann::json& j, const char* key, const std::string& fallback) {
    if (!j.is_object() || !j.contains(key) || !j.at(key).is_string()) {
        return fallback;
    }

    std::string value = j.at(key).get<std::string>();

    // Keep the first public version conservative: avoid control characters in
    // user-visible branding values loaded from config.
    for (char& c : value) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 0x20 || uc == 0x7f) {
            c = ' ';
        }
    }

    return value;
}

bool json_bool_or(const nlohmann::json& j, const char* key, bool fallback) {
    if (!j.is_object() || !j.contains(key) || !j.at(key).is_boolean()) {
        return fallback;
    }
    return j.at(key).get<bool>();
}

} // namespace

std::filesystem::path branding_config_path() {
    return config_root_path() / "branding.json";
}

BrandingConfig load_branding_config() {
    BrandingConfig cfg;

    const auto path = branding_config_path();
    std::ifstream in(path);
    if (!in) {
        return cfg;
    }

    nlohmann::json j;
    try {
        in >> j;
    } catch (...) {
        return cfg;
    }

    if (!j.is_object()) {
        return cfg;
    }

    cfg.enabled = json_bool_or(j, "enabled", cfg.enabled);
    cfg.product_name = json_string_or(j, "product_name", cfg.product_name);
    cfg.product_short_name = json_string_or(j, "product_short_name", cfg.product_short_name);
    cfg.company_name = json_string_or(j, "company_name", cfg.company_name);
    cfg.copyright = json_string_or(j, "copyright", cfg.copyright);
    cfg.hide_upstream_brand = json_bool_or(j, "hide_upstream_brand", cfg.hide_upstream_brand);

    cfg.logo_dark = json_string_or(j, "logo_dark", cfg.logo_dark);
    cfg.logo_bright = json_string_or(j, "logo_bright", cfg.logo_bright);
    cfg.logo_wordmark = json_string_or(j, "logo_wordmark", cfg.logo_wordmark);
    cfg.favicon = json_string_or(j, "favicon", cfg.favicon);

    cfg.primary_color = json_string_or(j, "primary_color", cfg.primary_color);
    cfg.accent_color = json_string_or(j, "accent_color", cfg.accent_color);
    cfg.support_url = json_string_or(j, "support_url", cfg.support_url);

    cfg.mobile_display_name = json_string_or(j, "mobile_display_name", cfg.mobile_display_name);
    cfg.mobile_short_name = json_string_or(j, "mobile_short_name", cfg.mobile_short_name);
    cfg.mobile_logo_url = json_string_or(j, "mobile_logo_url", cfg.mobile_logo_url);
    cfg.mobile_primary_color = json_string_or(j, "mobile_primary_color", cfg.mobile_primary_color);
    cfg.mobile_accent_color = json_string_or(j, "mobile_accent_color", cfg.mobile_accent_color);

    cfg.presentation_url = json_string_or(j, "presentation_url", cfg.presentation_url);
    cfg.show_presentation_link = json_bool_or(j, "show_presentation_link", cfg.show_presentation_link);

    if (cfg.product_name.empty()) cfg.product_name = "DNA-Nexus NAS";
    if (cfg.product_short_name.empty()) cfg.product_short_name = "DNA-Nexus";
    if (cfg.company_name.empty()) cfg.company_name = "CPUNK";
    if (cfg.logo_dark.empty()) cfg.logo_dark = "/static/img/logo/Nexus_logo_dark.png";
    if (cfg.logo_bright.empty()) cfg.logo_bright = "/static/img/logo/Nexus_logo_bright.png";
    if (cfg.logo_wordmark.empty()) cfg.logo_wordmark = "/static/img/logo/nexuslogo_text.svg";

    if (cfg.mobile_display_name.empty()) cfg.mobile_display_name = cfg.product_name;
    if (cfg.mobile_short_name.empty()) cfg.mobile_short_name = cfg.product_short_name;
    if (cfg.mobile_logo_url.empty()) cfg.mobile_logo_url = cfg.logo_wordmark;
    if (cfg.mobile_primary_color.empty()) cfg.mobile_primary_color = cfg.primary_color;
    if (cfg.mobile_accent_color.empty()) cfg.mobile_accent_color = cfg.accent_color;

    return cfg;
}

nlohmann::json branding_config_public_json(const BrandingConfig& cfg) {
    return nlohmann::json{
        {"ok", true},
        {"enabled", cfg.enabled},
        {"product_name", cfg.product_name},
        {"product_short_name", cfg.product_short_name},
        {"company_name", cfg.company_name},
        {"copyright", cfg.copyright},
        {"hide_upstream_brand", cfg.hide_upstream_brand},
        {"logo_dark", cfg.logo_dark},
        {"logo_bright", cfg.logo_bright},
        {"logo_wordmark", cfg.logo_wordmark},
        {"favicon", cfg.favicon},
        {"primary_color", cfg.primary_color},
        {"accent_color", cfg.accent_color},
        {"support_url", cfg.support_url},
        {"presentation_url", cfg.presentation_url},
        {"show_presentation_link", cfg.show_presentation_link},
        {"mobile", nlohmann::json{
            {"display_name", cfg.mobile_display_name},
            {"short_name", cfg.mobile_short_name},
            {"logo_url", cfg.mobile_logo_url},
            {"primary_color", cfg.mobile_primary_color},
            {"accent_color", cfg.mobile_accent_color}
        }}
    };
}

} // namespace pqnas
