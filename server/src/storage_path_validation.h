#pragma once

#include <string>

namespace pqnas {

// Canonical lexical validation/normalization for relative storage paths.
//
// Security:
// - rejects empty/NUL-containing paths
// - rejects absolute/rooted paths
// - keeps the path inside a relative namespace
// - rejects reserved .pqnas components
//
// This helper intentionally preserves the existing File API normalization
// semantics. Trust-boundary callers that require canonical input must also
// compare the returned normalized value with the original input.
bool normalize_storage_relative_path_strict(
    const std::string& rel_path,
    std::string* out_rel_norm,
    std::string* err
);

} // namespace pqnas
