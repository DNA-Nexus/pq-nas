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
    std::string logo_bright = "/static/img/logo/nexuslogo_text.svg";
    std::string favicon = "/static/favicon.ico";

    std::string primary_color;
    std::string accent_color;
    std::string support_url;
    std::string presentation_url = "/static/nexus-presentation/index.html";
    bool show_presentation_link = true;
};

std::filesystem::path branding_config_path();
BrandingConfig load_branding_config();
nlohmann::json branding_config_public_json(const BrandingConfig& cfg);

} // namespace pqnas
