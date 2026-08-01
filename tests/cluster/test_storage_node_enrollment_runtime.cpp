#include "cluster/storage_node_enrollment_runtime.h"

#include "cluster/internal_auth.h"
#include "cluster/node_enrollment_token.h"
#include "cluster/node_status.h"

#include <sodium.h>

#include <cerrno>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

using pqnas::cluster::ServerRole;
using pqnas::cluster::ServerRoleConfig;
using pqnas::cluster::
    StorageNodeEnrollmentRuntimeCode;
using pqnas::cluster::
    StorageNodeEnrollmentRuntimeConfig;
using pqnas::cluster::
    StorageNodeEnrollmentRuntimeConfigResult;
using pqnas::cluster::
    StorageNodeEnrollmentRuntimeDependencies;
using pqnas::cluster::
    StorageNodeEnrollmentTransportCode;
using pqnas::cluster::
    StorageNodeEnrollmentTransportRequest;
using pqnas::cluster::
    StorageNodeEnrollmentTransportResponse;

int failures = 0;

void expect(
    bool condition,
    const std::string& message
) {
    if (!condition) {
        ++failures;

        std::cerr
            << "FAIL: "
            << message
            << '\n';
    }
}

fs::path make_test_directory() {
    std::string pattern =
        (
            fs::temp_directory_path() /
            "pqnas-storage-node-enrollment-runtime-XXXXXX"
        ).string();

    std::vector<char> writable(
        pattern.begin(),
        pattern.end()
    );

    writable.push_back('\0');

    char* created =
        ::mkdtemp(
            writable.data()
        );

    return created
        ? fs::path(created)
        : fs::path{};
}

bool create_private_directory(
    const fs::path& path
) {
    std::error_code error;

    fs::create_directories(
        path,
        error
    );

    return
        !error &&
        ::chmod(
            path.c_str(),
            0700
        ) == 0;
}

bool write_private_text(
    const fs::path& path,
    const std::string& contents
) {
    const int descriptor =
        ::open(
            path.c_str(),
            O_WRONLY |
            O_CREAT |
            O_TRUNC |
            O_CLOEXEC |
            O_NOFOLLOW,
            0600
        );

    if (descriptor < 0) {
        return false;
    }

    std::size_t offset = 0;
    bool ok = true;

    while (offset <
           contents.size()) {
        const ssize_t count =
            ::write(
                descriptor,
                contents.data() + offset,
                contents.size() - offset
            );

        if (count > 0) {
            offset +=
                static_cast<std::size_t>(
                    count
                );

            continue;
        }

        if (count < 0 &&
            errno == EINTR) {
            continue;
        }

        ok = false;
        break;
    }

    if (ok) {
        ok =
            ::fsync(descriptor) == 0;
    }

    const bool closed =
        ::close(descriptor) == 0;

    return ok && closed;
}

std::string read_text(
    const fs::path& path
) {
    std::ifstream input(path);

    if (!input) {
        return {};
    }

    return std::string(
        std::istreambuf_iterator<char>(
            input
        ),
        std::istreambuf_iterator<char>()
    );
}

bool make_bootstrap_token(
    std::string* output
) {
    if (!output) {
        return false;
    }

    pqnas::cluster::
        NodeEnrollmentTokenMaterial material;

    if (pqnas::cluster::
            generate_node_enrollment_token(
                &material) !=
        pqnas::cluster::
            NodeEnrollmentTokenResult::Ok) {
        return false;
    }

    return material.take_token(
        output
    );
}

ServerRoleConfig make_role(
    const fs::path& private_directory,
    const std::string& suffix
) {
    ServerRoleConfig role;

    role.role =
        ServerRole::Node;

    role.node_id =
        "storage-runtime-" +
        suffix;

    role.director_url =
        "https://director.internal";

    role.cluster_credential_file =
        (
            private_directory /
            "cluster_node.credential"
        ).string();

    return role;
}

StorageNodeEnrollmentRuntimeConfig
make_config(
    const fs::path& token_file,
    const std::string& suffix
) {
    StorageNodeEnrollmentRuntimeConfig
        config;

    config.enabled = true;

    config.enrollment_id =
        "enrollment_runtime_" +
        suffix;

    config.expected_generation =
        3;

    config.api_base_url =
        "https://storage-runtime-" +
        suffix +
        ".internal";

    config.bootstrap_token_file =
        token_file.empty()
        ? std::string()
        : token_file.string();

    return config;
}

struct Clock {
    std::vector<std::string> values;
    std::size_t index = 0;

    std::string next() {
        if (index >=
            values.size()) {
            return {};
        }

        return values[index++];
    }
};

} // namespace

int main() {
    if (sodium_init() < 0) {
        std::cerr
            << "FAIL: sodium initialization failed\n";

        return 1;
    }

    const fs::path root =
        make_test_directory();

    if (root.empty()) {
        std::cerr
            << "FAIL: test directory creation failed\n";

        return 1;
    }

    {
        StorageNodeEnrollmentRuntimeConfig
            config;

        expect(
            pqnas::cluster::
                parse_storage_node_enrollment_runtime_config(
                    "",
                    "",
                    "",
                    "",
                    &config
                ) ==
                StorageNodeEnrollmentRuntimeConfigResult::
                    Ok &&
            !config.enabled,
            "all-empty runtime config preserves legacy mode"
        );

        expect(
            pqnas::cluster::
                parse_storage_node_enrollment_runtime_config(
                    "enrollment_runtime_config",
                    "7",
                    "https://node-config.internal/",
                    "/run/pqnas/bootstrap.token",
                    &config
                ) ==
                StorageNodeEnrollmentRuntimeConfigResult::
                    Ok &&
            config.enabled &&
            config.expected_generation == 7 &&
            config.api_base_url ==
                "https://node-config.internal",
            "valid runtime config is canonicalized"
        );

        expect(
            pqnas::cluster::
                parse_storage_node_enrollment_runtime_config(
                    "enrollment_runtime_partial",
                    "",
                    "https://node-config.internal",
                    "",
                    &config
                ) ==
                StorageNodeEnrollmentRuntimeConfigResult::
                    InvalidInput,
            "partial enrollment identity rejected"
        );

        expect(
            pqnas::cluster::
                parse_storage_node_enrollment_runtime_config(
                    "",
                    "",
                    "",
                    "/run/pqnas/bootstrap.token",
                    &config
                ) ==
                StorageNodeEnrollmentRuntimeConfigResult::
                    InvalidInput,
            "token-only runtime config rejected"
        );

        expect(
            pqnas::cluster::
                parse_storage_node_enrollment_runtime_config(
                    "enrollment_runtime_generation",
                    "18446744073709551616",
                    "https://node-config.internal",
                    "",
                    &config
                ) ==
                StorageNodeEnrollmentRuntimeConfigResult::
                    InvalidInput,
            "overflowing generation rejected"
        );

        expect(
            pqnas::cluster::
                parse_storage_node_enrollment_runtime_config(
                    "enrollment_runtime_http",
                    "1",
                    "http://node-config.internal",
                    "",
                    &config
                ) ==
                StorageNodeEnrollmentRuntimeConfigResult::
                    InvalidInput,
            "plaintext node API URL rejected"
        );

        expect(
            pqnas::cluster::
                parse_storage_node_enrollment_runtime_config(
                    "enrollment_runtime_path",
                    "1",
                    "https://node-config.internal",
                    "relative/bootstrap.token",
                    &config
                ) ==
                StorageNodeEnrollmentRuntimeConfigResult::
                    InvalidInput,
            "relative token path rejected"
        );
    }

    {
        ServerRoleConfig role;
        role.role =
            ServerRole::Standalone;

        StorageNodeEnrollmentRuntimeConfig
            disabled;

        int calls = 0;

        StorageNodeEnrollmentRuntimeDependencies
            dependencies;

        dependencies.transport =
            [&](const StorageNodeEnrollmentTransportRequest&) {
                ++calls;

                return
                    StorageNodeEnrollmentTransportResponse{
                        StorageNodeEnrollmentTransportCode::
                            TransportFailed,
                        false,
                        0
                    };
            };

        dependencies.now_iso_utc =
            []() {
                return std::string{};
            };

        const auto result =
            pqnas::cluster::
                ensure_storage_node_enrollment(
                    role,
                    disabled,
                    std::move(dependencies)
                );

        expect(
            result.ok &&
            result.code ==
                StorageNodeEnrollmentRuntimeCode::
                    Disabled &&
            calls == 0,
            "disabled runtime performs no role or network work"
        );
    }

    {
        const fs::path private_directory =
            root /
            "fresh" /
            "private";

        expect(
            create_private_directory(
                private_directory
            ),
            "create fresh private directory"
        );

        const fs::path token_file =
            private_directory /
            "bootstrap.token";

        std::string bootstrap_token;

        expect(
            make_bootstrap_token(
                &bootstrap_token
            ) &&
            write_private_text(
                token_file,
                bootstrap_token + "\n"
            ),
            "write fresh bootstrap token"
        );

        const ServerRoleConfig role =
            make_role(
                private_directory,
                "fresh"
            );

        const auto config =
            make_config(
                token_file,
                "fresh"
            );

        Clock clock{
            {
                "2026-07-31T13:00:00Z",
                "2026-07-31T13:00:01Z"
            }
        };

        int calls = 0;

        StorageNodeEnrollmentRuntimeDependencies
            dependencies;

        dependencies.now_iso_utc =
            [&]() {
                return clock.next();
            };

        dependencies.transport =
            [&](const StorageNodeEnrollmentTransportRequest&
                    request) {
                ++calls;

                expect(
                    request.authorization_token ==
                        bootstrap_token,
                    "fresh runtime uses token file value"
                );

                expect(
                    request.node_id ==
                        role.node_id &&
                    request.director_url ==
                        role.director_url &&
                    request.api_base_url ==
                        config.api_base_url &&
                    request.protocol_version ==
                        pqnas::cluster::
                            kNodeStatusProtocolVersion,
                    "fresh runtime binds exact role and protocol identity"
                );

                return
                    StorageNodeEnrollmentTransportResponse{
                        StorageNodeEnrollmentTransportCode::
                            Ok,
                        false,
                        200
                    };
            };

        const auto result =
            pqnas::cluster::
                ensure_storage_node_enrollment(
                    role,
                    config,
                    std::move(dependencies)
                );

        expect(
            result.ok &&
            result.code ==
                StorageNodeEnrollmentRuntimeCode::
                    Ok &&
            !result.recovered &&
            !result.token_cleanup_warning &&
            calls == 1,
            "fresh runtime enrollment succeeds"
        );

        expect(
            !fs::exists(token_file),
            "successful runtime removes unchanged token file"
        );

        pqnas::cluster::
            InternalAuthCredential credential;

        std::string error;

        expect(
            pqnas::cluster::
                load_internal_auth_credential_file(
                    role.cluster_credential_file,
                    &credential,
                    &error
                ) &&
            credential.configured(),
            "runtime creates normal cluster credential file"
        );

        const std::string state =
            read_text(
                private_directory /
                "storage_node_provisioning.json"
            );

        expect(
            state.find(
                "\"stage\":\"enrolled\""
            ) != std::string::npos &&
            state.find(
                bootstrap_token
            ) == std::string::npos,
            "runtime persists enrolled state without bootstrap token"
        );

        Clock restart_clock{
            {
                "2026-07-31T13:00:10Z"
            }
        };

        int restart_calls = 0;

        StorageNodeEnrollmentRuntimeDependencies
            restart_dependencies;

        restart_dependencies.now_iso_utc =
            [&]() {
                return restart_clock.next();
            };

        restart_dependencies.transport =
            [&](const StorageNodeEnrollmentTransportRequest&) {
                ++restart_calls;

                return
                    StorageNodeEnrollmentTransportResponse{
                        StorageNodeEnrollmentTransportCode::
                            TransportFailed,
                        false,
                        0
                    };
            };

        const auto restart_result =
            pqnas::cluster::
                ensure_storage_node_enrollment(
                    role,
                    config,
                    std::move(restart_dependencies)
                );

        expect(
            restart_result.ok &&
            restart_result.already_enrolled &&
            !restart_result.token_cleanup_warning &&
            restart_calls == 0 &&
            !fs::exists(token_file),
            "restart accepts removed configured token after enrollment"
        );

        pqnas::cluster::
            clear_node_enrollment_token_text(
                &bootstrap_token
            );
    }

    {
        const fs::path private_directory =
            root /
            "missing-fresh" /
            "private";

        expect(
            create_private_directory(
                private_directory
            ),
            "create missing-fresh private directory"
        );

        const fs::path token_file =
            private_directory /
            "bootstrap.token";

        const ServerRoleConfig role =
            make_role(
                private_directory,
                "missing-fresh"
            );

        const auto config =
            make_config(
                token_file,
                "missing-fresh"
            );

        int calls = 0;

        StorageNodeEnrollmentRuntimeDependencies
            dependencies;

        dependencies.now_iso_utc =
            []() {
                return "2026-07-31T13:05:00Z";
            };

        dependencies.transport =
            [&](const StorageNodeEnrollmentTransportRequest&) {
                ++calls;

                return
                    StorageNodeEnrollmentTransportResponse{
                        StorageNodeEnrollmentTransportCode::
                            Ok,
                        false,
                        200
                    };
            };

        const auto result =
            pqnas::cluster::
                ensure_storage_node_enrollment(
                    role,
                    config,
                    std::move(dependencies)
                );

        expect(
            !result.ok &&
            result.code ==
                StorageNodeEnrollmentRuntimeCode::
                    BootstrapTokenUnavailable &&
            calls == 0 &&
            !fs::exists(
                role.cluster_credential_file
            ),
            "missing configured token fails closed for fresh enrollment"
        );
    }

    {
        const fs::path private_directory =
            root /
            "recovery" /
            "private";

        expect(
            create_private_directory(
                private_directory
            ),
            "create recovery private directory"
        );

        const fs::path token_file =
            private_directory /
            "bootstrap.token";

        std::string bootstrap_token;

        expect(
            make_bootstrap_token(
                &bootstrap_token
            ) &&
            write_private_text(
                token_file,
                bootstrap_token + "\n"
            ),
            "write recovery bootstrap token"
        );

        const ServerRoleConfig role =
            make_role(
                private_directory,
                "recovery"
            );

        auto config =
            make_config(
                token_file,
                "recovery"
            );

        Clock first_clock{
            {
                "2026-07-31T13:10:00Z"
            }
        };

        StorageNodeEnrollmentRuntimeDependencies
            first_dependencies;

        first_dependencies.now_iso_utc =
            [&]() {
                return first_clock.next();
            };

        first_dependencies.transport =
            [](const StorageNodeEnrollmentTransportRequest&) {
                return
                    StorageNodeEnrollmentTransportResponse{
                        StorageNodeEnrollmentTransportCode::
                            TransportFailed,
                        false,
                        0
                    };
            };

        const auto first_result =
            pqnas::cluster::
                ensure_storage_node_enrollment(
                    role,
                    config,
                    std::move(first_dependencies)
                );

        expect(
            !first_result.ok &&
            first_result.code ==
                StorageNodeEnrollmentRuntimeCode::
                    DirectorUnavailable &&
            fs::exists(token_file),
            "failed Director call retains token file and persisted state"
        );

        std::error_code token_remove_error;

        fs::remove(
            token_file,
            token_remove_error
        );

        expect(
            !token_remove_error &&
            !fs::exists(token_file) &&
            !config.bootstrap_token_file.empty(),
            "remove recovery token while preserving configured path"
        );

        Clock recovery_clock{
            {
                "2026-07-31T13:10:10Z",
                "2026-07-31T13:10:11Z"
            }
        };

        int calls = 0;

        StorageNodeEnrollmentRuntimeDependencies
            recovery_dependencies;

        recovery_dependencies.now_iso_utc =
            [&]() {
                return recovery_clock.next();
            };

        recovery_dependencies.transport =
            [&](const StorageNodeEnrollmentTransportRequest&
                    request) {
                ++calls;

                expect(
                    request.authorization_token ==
                        request.node_credential,
                    "tokenless runtime recovery uses persisted credential"
                );

                return
                    StorageNodeEnrollmentTransportResponse{
                        StorageNodeEnrollmentTransportCode::
                            Ok,
                        true,
                        200
                    };
            };

        const auto recovery_result =
            pqnas::cluster::
                ensure_storage_node_enrollment(
                    role,
                    config,
                    std::move(recovery_dependencies)
                );

        expect(
            recovery_result.ok &&
            recovery_result.recovered &&
            calls == 1 &&
            !fs::exists(token_file),
            "configured missing token recovers after credential persistence"
        );

        pqnas::cluster::
            clear_node_enrollment_token_text(
                &bootstrap_token
            );
    }

    {
        const fs::path private_directory =
            root /
            "unsafe-token" /
            "private";

        expect(
            create_private_directory(
                private_directory
            ),
            "create unsafe-token private directory"
        );

        const fs::path token_file =
            private_directory /
            "bootstrap.token";

        std::string bootstrap_token;

        expect(
            make_bootstrap_token(
                &bootstrap_token
            ) &&
            write_private_text(
                token_file,
                bootstrap_token + "\n"
            ) &&
            ::chmod(
                token_file.c_str(),
                0644
            ) == 0,
            "write unsafe token fixture"
        );

        const ServerRoleConfig role =
            make_role(
                private_directory,
                "unsafe-token"
            );

        const auto config =
            make_config(
                token_file,
                "unsafe-token"
            );

        int calls = 0;

        StorageNodeEnrollmentRuntimeDependencies
            dependencies;

        dependencies.now_iso_utc =
            []() {
                return
                    "2026-07-31T13:20:00Z";
            };

        dependencies.transport =
            [&](const StorageNodeEnrollmentTransportRequest&) {
                ++calls;

                return
                    StorageNodeEnrollmentTransportResponse{
                        StorageNodeEnrollmentTransportCode::
                            Ok,
                        false,
                        200
                    };
            };

        const auto result =
            pqnas::cluster::
                ensure_storage_node_enrollment(
                    role,
                    config,
                    std::move(dependencies)
                );

        expect(
            !result.ok &&
            result.code ==
                StorageNodeEnrollmentRuntimeCode::
                    BootstrapTokenUnavailable &&
            calls == 0,
            "unsafe token permissions fail before network"
        );

        pqnas::cluster::
            clear_node_enrollment_token_text(
                &bootstrap_token
            );
    }

    {
        const fs::path private_directory =
            root /
            "replacement" /
            "private";

        expect(
            create_private_directory(
                private_directory
            ),
            "create replacement private directory"
        );

        const fs::path token_file =
            private_directory /
            "bootstrap.token";

        std::string bootstrap_token;

        expect(
            make_bootstrap_token(
                &bootstrap_token
            ) &&
            write_private_text(
                token_file,
                bootstrap_token + "\n"
            ),
            "write replacement bootstrap token"
        );

        const ServerRoleConfig role =
            make_role(
                private_directory,
                "replacement"
            );

        const auto config =
            make_config(
                token_file,
                "replacement"
            );

        Clock clock{
            {
                "2026-07-31T13:30:00Z",
                "2026-07-31T13:30:01Z"
            }
        };

        StorageNodeEnrollmentRuntimeDependencies
            dependencies;

        dependencies.now_iso_utc =
            [&]() {
                return clock.next();
            };

        dependencies.transport =
            [&](const StorageNodeEnrollmentTransportRequest&) {
                std::error_code remove_error;

                fs::remove(
                    token_file,
                    remove_error
                );

                expect(
                    write_private_text(
                        token_file,
                        "replacement-not-a-secret\n"
                    ),
                    "replace token file during network callback"
                );

                return
                    StorageNodeEnrollmentTransportResponse{
                        StorageNodeEnrollmentTransportCode::
                            Ok,
                        false,
                        200
                    };
            };

        const auto result =
            pqnas::cluster::
                ensure_storage_node_enrollment(
                    role,
                    config,
                    std::move(dependencies)
                );

        expect(
            result.ok &&
            result.token_cleanup_warning &&
            fs::exists(token_file) &&
            read_text(token_file) ==
                "replacement-not-a-secret\n",
            "runtime never unlinks a replaced token file"
        );

        pqnas::cluster::
            clear_node_enrollment_token_text(
                &bootstrap_token
            );
    }

    std::error_code cleanup_error;

    fs::remove_all(
        root,
        cleanup_error
    );

    if (failures != 0) {
        std::cerr
            << failures
            << " storage node enrollment runtime test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: storage node enrollment runtime tests passed\n";

    return 0;
}
