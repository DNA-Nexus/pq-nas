#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>

namespace pqnas {

struct BrandingConfig {
    bool enabled = false;
    std::string product_name = "DNA-Nexus NAS";
    std::string product_short_name = "DNA-Nexus";
    std::string company_name = "CPUNK";
    std::string copyright = "© CPUNK 2026 · DNA-Nexus";
    bool hide_upstream_brand = false;

    std::string logo_dark = "/static/img/logo/Nexus_logo_dark.png";
    std::string logo_bright = "/static/img/logo/Nexus_logo_bright.png";
    std::string logo_wordmark = "/static/img/logo/nexuslogo_text.svg";
    std::string favicon = "/static/favicon.ico";

    std::string primary_color;
    std::string accent_color;
    std::string support_url;

    // Public mobile UI branding only. Do not put secrets, tokens, or private
    // deployment data in branding.json; Android APK/runtime UI data is visible
    // to the user and should be treated as untrusted display metadata.
    std::string mobile_display_name;
    std::string mobile_short_name;
    std::string mobile_logo_url;
    std::string mobile_primary_color;
    std::string mobile_accent_color;

    std::string presentation_url = "/static/nexus-presentation/index.html";
    bool show_presentation_link = true;
};

std::filesystem::path branding_config_path();
BrandingConfig load_branding_config();
nlohmann::json branding_config_public_json(const BrandingConfig& cfg);

} // namespace pqnas
