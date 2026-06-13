#pragma once

#include <filesystem>
#include <string>
#include <vector>

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
    OpaqueHelperClientResult server_setup_check(const std::filesystem::path& setup_path) const;
    OpaqueHelperClientResult register_start(const std::filesystem::path& setup_path,
                                           const std::string& credential_id,
                                           const std::string& registration_request_b64) const;
    OpaqueHelperClientResult register_finish(const std::string& registration_upload_b64) const;

private:
    OpaqueHelperClientResult run_allowed_command(const std::vector<std::string>& args) const;

    std::filesystem::path helper_path_;
};

} // namespace pqnas
