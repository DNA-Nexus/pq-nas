#pragma once

#include <string>

namespace pqnas {

// Security: make the currently visible file contents and its directory entry
// durable before a security-sensitive operation reports success.
bool fsync_file_and_parent_directory(
    const std::string& path,
    std::string* error
);

} // namespace pqnas
