#include "runtime_paths.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include <unistd.h>

namespace pqnas {
    namespace {

        std::string getenv_str_local(const char* key) {
            if (!key || !*key) return {};
            const char* v = std::getenv(key);
            return v ? std::string(v) : std::string{};
        }

        std::string trim_ascii_local(std::string s) {
            auto is_ws = [](unsigned char c) {
                return std::isspace(c) != 0;
            };

            std::size_t a = 0;
            while (a < s.size() && is_ws(static_cast<unsigned char>(s[a]))) ++a;

            std::size_t b = s.size();
            while (b > a && is_ws(static_cast<unsigned char>(s[b - 1]))) --b;

            return s.substr(a, b - a);
        }

        std::filesystem::path absolute_normalized_path_or_empty_local(const std::string& raw) {
            const std::string trimmed = trim_ascii_local(raw);
            if (trimmed.empty()) return {};

            std::filesystem::path p(trimmed);
            if (!p.is_absolute()) return {};

            p = p.lexically_normal();

            // Security: never allow the storage root to collapse to filesystem
            // root. Storage-manager destructive operations use this root as an
            // allow-list boundary for pool paths.
            if (p.empty() || p == std::filesystem::path("/")) return {};

            return p;
        }

        bool dir_exists_local(const std::filesystem::path& p) {
            std::error_code ec;
            return std::filesystem::is_directory(p, ec) && !ec;
        }

        std::string exe_dir_local() {
            std::vector<char> buf(1024);

            while (true) {
                const ssize_t n = ::readlink("/proc/self/exe", buf.data(), buf.size());
                if (n < 0) return ".";

                if (static_cast<std::size_t>(n) < buf.size()) {
                    std::filesystem::path p(std::string(buf.data(), static_cast<std::size_t>(n)));
                    auto parent = p.parent_path();
                    return parent.empty() ? std::string(".") : parent.string();
                }

                buf.resize(buf.size() * 2);
            }
        }

    } // namespace

    std::string data_root_dir() {
        const std::string env = getenv_str_local("PQNAS_DATA_ROOT");
        if (!env.empty()) return env;

        const std::string srv = "/srv/pqnas/data";
        if (dir_exists_local(srv)) return srv;

        return exe_dir_local() + "/data";
    }

    std::filesystem::path data_root_path() {
        return std::filesystem::path(data_root_dir());
    }

    std::string storage_root_dir() {
        // Security: PQNAS_STORAGE_ROOT is deployment-level configuration, not
        // request data. Centralize and sanitize it here before storage-manager
        // code derives config paths or allowed pool prefixes from it.
        const std::filesystem::path env_root =
            absolute_normalized_path_or_empty_local(getenv_str_local("PQNAS_STORAGE_ROOT"));

        if (!env_root.empty()) return env_root.string();

        return "/srv/pqnas";
    }

    std::filesystem::path storage_root_path() {
        return std::filesystem::path(storage_root_dir());
    }

    std::string config_root_dir() {
        const std::string explicit_root = getenv_str_local("PQNAS_CONFIG_ROOT");
        if (!explicit_root.empty()) return explicit_root;

        // Existing deployments use PQNAS_CONFIG for the runtime config
        // directory. Keep PQNAS_CONFIG_ROOT as the clearer new name, but honor
        // PQNAS_CONFIG so OPAQUE files land beside the rest of PQ-NAS config.
        const std::string legacy_config = getenv_str_local("PQNAS_CONFIG");
        if (!legacy_config.empty()) return legacy_config;

        return "/etc/pqnas";
    }

    std::filesystem::path config_root_path() {
        return std::filesystem::path(config_root_dir());
    }

    std::filesystem::path opaque_credentials_path() {
        const std::string env = getenv_str_local("PQNAS_OPAQUE_CREDENTIALS_PATH");
        if (!env.empty()) return std::filesystem::path(env);

        return config_root_path() / "opaque_credentials.json";
    }

    std::filesystem::path opaque_server_setup_path() {
        const std::string env = getenv_str_local("PQNAS_OPAQUE_SERVER_SETUP_PATH");
        if (!env.empty()) return std::filesystem::path(env);

        return config_root_path() / "opaque_server_setup.bin";
    }

    std::filesystem::path opaque_helper_path() {
        const std::string env = getenv_str_local("PQNAS_OPAQUE_HELPER");
        if (!env.empty()) return std::filesystem::path(env);

        return std::filesystem::path("/usr/local/libexec/pqnas/pqnas_opaque_helper");
    }

    std::filesystem::path pqnas_hidden_root_for_storage_root(const std::filesystem::path& storage_root) {
        return storage_root.lexically_normal() / ".pqnas";
    }

    std::filesystem::path pqnas_trash_root_for_storage_root(const std::filesystem::path& storage_root) {
        return pqnas_hidden_root_for_storage_root(storage_root) / "trash";
    }

} // namespace pqnas