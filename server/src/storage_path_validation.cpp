#include "storage_path_validation.h"

#include <filesystem>

namespace pqnas {

bool normalize_storage_relative_path_strict(
    const std::string& rel_path,
    std::string* out_rel_norm,
    std::string* err
) {
    if (err) {
        err->clear();
    }

    if (!out_rel_norm) {
        if (err) {
            *err = "null out_rel_norm";
        }
        return false;
    }

    if (rel_path.empty()) {
        if (err) {
            *err = "empty path";
        }
        return false;
    }

    if (rel_path.find('\0') != std::string::npos) {
        if (err) {
            *err = "invalid path";
        }
        return false;
    }

    std::filesystem::path rel(rel_path);

    if (rel.is_absolute() || rel.has_root_path()) {
        if (err) {
            *err = "absolute path not allowed";
        }
        return false;
    }

    rel = rel.lexically_normal();

    if (rel.empty()) {
        if (err) {
            *err = "invalid path";
        }
        return false;
    }

    for (const auto& part : rel) {
        const std::string value = part.string();

        if (value.empty() ||
            value == "." ||
            value == "..") {
            if (err) {
                *err = "invalid path";
            }
            return false;
        }

        // Security: .pqnas is an internal namespace and must never become
        // reachable through user-controlled or control-plane storage paths.
        if (value == ".pqnas") {
            if (err) {
                *err = "reserved path";
            }
            return false;
        }
    }

    *out_rel_norm = rel.generic_string();
    return true;
}

} // namespace pqnas
