#include "storage_path_validation.h"

#include <filesystem>

namespace pqnas {
namespace {

bool path_has_prefix_components(
    const std::filesystem::path& root,
    const std::filesystem::path& candidate
) {
    const auto normalized_root =
        root.lexically_normal();

    const auto normalized_candidate =
        candidate.lexically_normal();

    auto root_it =
        normalized_root.begin();

    auto candidate_it =
        normalized_candidate.begin();

    for (;
         root_it != normalized_root.end();
         ++root_it, ++candidate_it) {
        if (candidate_it ==
                normalized_candidate.end() ||
            *root_it != *candidate_it) {
            return false;
        }
    }

    return true;
}

} // namespace

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

bool canonical_existing_path_within_root(
    const std::filesystem::path& root,
    const std::filesystem::path& candidate,
    std::filesystem::path* out_canonical,
    std::string* err
) {
    if (err) {
        err->clear();
    }

    if (out_canonical) {
        out_canonical->clear();
    }

    if (!out_canonical) {
        if (err) {
            *err = "null out_canonical";
        }

        return false;
    }

    if (root.empty() ||
        candidate.empty() ||
        !root.is_absolute() ||
        !candidate.is_absolute()) {
        if (err) {
            *err = "absolute root and candidate required";
        }

        return false;
    }

    std::error_code ec;

    const std::filesystem::path canonical_root =
        std::filesystem::canonical(
            root,
            ec
        );

    if (ec ||
        canonical_root.empty() ||
        !canonical_root.is_absolute()) {
        if (err) {
            *err = "root unavailable";
        }

        return false;
    }

    ec.clear();

    const std::filesystem::path canonical_candidate =
        std::filesystem::canonical(
            candidate,
            ec
        );

    if (ec ||
        canonical_candidate.empty() ||
        !canonical_candidate.is_absolute()) {
        if (err) {
            *err = "candidate unavailable";
        }

        return false;
    }

    // Security: canonical() resolves symlinks. Component-wise containment then
    // prevents both sibling-prefix confusion and filesystem symlink escapes.
    if (!path_has_prefix_components(
            canonical_root,
            canonical_candidate)) {
        if (err) {
            *err = "path escapes root";
        }

        return false;
    }

    *out_canonical =
        canonical_candidate;

    return true;
}

} // namespace pqnas
