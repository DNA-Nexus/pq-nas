#include "cluster/server_role.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>

namespace pqnas::cluster {
namespace {

std::string trim_ascii(const std::string& value) {
    std::size_t begin = 0;
    while (begin < value.size() &&
           std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }

    return value.substr(begin, end - begin);
}

std::string lower_ascii(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(
            std::tolower(static_cast<unsigned char>(ch))
        );
    }
    return value;
}

std::string env_value(const char* name) {
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
}

bool valid_node_id(const std::string& value) {
    if (value.empty() || value.size() > 64) return false;

    const auto valid_first = [](char ch) {
        return std::isalnum(static_cast<unsigned char>(ch)) != 0;
    };

    const auto valid_rest = [&](char ch) {
        return valid_first(ch) || ch == '-' || ch == '_' || ch == '.';
    };

    if (!valid_first(value.front())) return false;

    for (char ch : value) {
        if (!valid_rest(ch)) return false;
    }

    return true;
}

bool normalize_director_url(
    const std::string& raw,
    std::string* normalized,
    std::string* err
) {
    std::string value = trim_ascii(raw);

    // Security: internal control-plane traffic must use TLS. Keeping the URL
    // to a base authority also prevents credentials, queries, and fragments
    // from being accidentally accepted or written to startup logs.
    constexpr const char* scheme = "https://";
    constexpr std::size_t scheme_len = 8;

    if (value.rfind(scheme, 0) != 0) {
        if (err) *err = "PQNAS_DIRECTOR_URL must begin with https://";
        return false;
    }

    while (value.size() > scheme_len && value.back() == '/') {
        value.pop_back();
    }

    const std::string authority = value.substr(scheme_len);

    if (authority.empty()) {
        if (err) *err = "PQNAS_DIRECTOR_URL is missing a host";
        return false;
    }

    for (char ch : authority) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        if (std::isspace(byte) || byte < 0x21 || byte > 0x7e) {
            if (err) *err = "PQNAS_DIRECTOR_URL contains invalid characters";
            return false;
        }
    }

    if (authority.find('/') != std::string::npos ||
        authority.find('@') != std::string::npos ||
        authority.find('?') != std::string::npos ||
        authority.find('#') != std::string::npos) {
        if (err) {
            *err =
                "PQNAS_DIRECTOR_URL must be a base HTTPS authority "
                "without path, credentials, query, or fragment";
        }
        return false;
    }

    *normalized = value;
    return true;
}

bool normalize_cluster_credential_file(
    const std::string& raw,
    std::string* normalized,
    std::string* err
) {
    if (!normalized) {
        if (err) *err = "missing credential path output";
        return false;
    }

    const std::string value = trim_ascii(raw);

    if (value.empty()) {
        if (err) {
            *err =
                "director and node roles require "
                "PQNAS_CLUSTER_CREDENTIAL_FILE";
        }
        return false;
    }

    if (value.size() > 4096) {
        if (err) {
            *err = "PQNAS_CLUSTER_CREDENTIAL_FILE is too long";
        }
        return false;
    }

    if (value.find('\0') != std::string::npos) {
        if (err) {
            *err =
                "PQNAS_CLUSTER_CREDENTIAL_FILE contains invalid characters";
        }
        return false;
    }

    const std::filesystem::path path(value);

    // Security: relative and traversal paths could make credential loading
    // depend on the service working directory or an attacker-controlled path.
    if (!path.is_absolute() || !path.has_root_path()) {
        if (err) {
            *err =
                "PQNAS_CLUSTER_CREDENTIAL_FILE must be an absolute path";
        }
        return false;
    }

    for (const auto& component : path) {
        const std::string part = component.string();

        if (part == "." || part == "..") {
            if (err) {
                *err =
                    "PQNAS_CLUSTER_CREDENTIAL_FILE must not contain "
                    "dot or traversal components";
            }
            return false;
        }
    }

    const std::filesystem::path clean = path.lexically_normal();

    if (clean.empty() ||
        clean == clean.root_path() ||
        clean.filename().empty()) {
        if (err) {
            *err =
                "PQNAS_CLUSTER_CREDENTIAL_FILE must name a file";
        }
        return false;
    }

    *normalized = clean.string();
    return true;
}

} // namespace

const char* server_role_name(ServerRole role) {
    switch (role) {
        case ServerRole::Standalone:
            return "standalone";
        case ServerRole::Director:
            return "director";
        case ServerRole::Node:
            return "node";
    }

    return "unknown";
}

bool parse_server_role_config(
    const std::string& raw_role,
    const std::string& raw_node_id,
    const std::string& raw_director_url,
    const std::string& raw_cluster_credential_file,
    ServerRoleConfig* out,
    std::string* err
) {
    if (err) err->clear();

    if (!out) {
        if (err) *err = "null output configuration";
        return false;
    }

    *out = ServerRoleConfig{};

    std::string role = lower_ascii(trim_ascii(raw_role));
    const std::string node_id = trim_ascii(raw_node_id);
    const std::string director_url = trim_ascii(raw_director_url);
    const std::string credential_file =
        trim_ascii(raw_cluster_credential_file);

    if (role.empty()) role = "standalone";

    if (role != "standalone" &&
        role != "director" &&
        role != "node") {
        if (err) {
            *err =
                "invalid PQNAS_SERVER_ROLE; expected "
                "standalone, director, or node";
        }
        return false;
    }

    if (role == "standalone") {
        if (!node_id.empty() ||
            !director_url.empty() ||
            !credential_file.empty()) {
            if (err) {
                *err =
                    "standalone role must not define PQNAS_NODE_ID, "
                    "PQNAS_DIRECTOR_URL, or "
                    "PQNAS_CLUSTER_CREDENTIAL_FILE";
            }
            return false;
        }

        out->role = ServerRole::Standalone;
        return true;
    }

    std::string normalized_credential_file;

    if (!normalize_cluster_credential_file(
            credential_file,
            &normalized_credential_file,
            err)) {
        return false;
    }

    if (role == "director") {
        if (!node_id.empty() || !director_url.empty()) {
            if (err) {
                *err =
                    "director role must not define "
                    "PQNAS_NODE_ID or PQNAS_DIRECTOR_URL";
            }
            return false;
        }

        out->role = ServerRole::Director;
        out->cluster_credential_file =
            normalized_credential_file;
        return true;
    }

    // Security: constrain node IDs before using them in registry keys,
    // audit fields, URLs, logs, or filesystem-related metadata.
    if (!valid_node_id(node_id)) {
        if (err) {
            *err =
                "PQNAS_NODE_ID must be 1-64 characters, begin with an "
                "alphanumeric character, and contain only A-Z, a-z, 0-9, "
                "dot, underscore, or hyphen";
        }
        return false;
    }

    if (director_url.empty()) {
        if (err) *err = "node role requires PQNAS_DIRECTOR_URL";
        return false;
    }

    std::string normalized_url;

    if (!normalize_director_url(
            director_url,
            &normalized_url,
            err)) {
        return false;
    }

    out->role = ServerRole::Node;
    out->node_id = node_id;
    out->director_url = normalized_url;
    out->cluster_credential_file =
        normalized_credential_file;
    return true;
}

bool load_server_role_config_from_env(
    ServerRoleConfig* out,
    std::string* err
) {
    return parse_server_role_config(
        env_value("PQNAS_SERVER_ROLE"),
        env_value("PQNAS_NODE_ID"),
        env_value("PQNAS_DIRECTOR_URL"),
        env_value("PQNAS_CLUSTER_CREDENTIAL_FILE"),
        out,
        err
    );
}

} // namespace pqnas::cluster
