#pragma once

#include <filesystem>
#include <string>

namespace pqnas {

struct OpaqueHelperClientResult {
    bool ok = false;
    int exit_code = -1;
    std::string output;
    std::string error;
};

class OpaqueHelperClient {
public:
    explicit OpaqueHelperClient(std::filesystem::path helper_path);

    const std::filesystem::path& helper_path() const;

    OpaqueHelperClientResult version() const;
    OpaqueHelperClientResult self_test() const;

private:
    OpaqueHelperClientResult run_allowed_command(const std::string& arg) const;

    std::filesystem::path helper_path_;
};

} // namespace pqnas
