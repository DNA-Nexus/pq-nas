#include "cluster/internal_auth.h"
#include "cluster/node_credential_registry.h"
#include "cluster/node_registry.h"
#include "cluster/storage_user_projection_sync_client.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace {

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
            "pqnas-projection-sync-client-XXXXXX"
        ).string();

    std::vector<char> writable(
        pattern.begin(),
        pattern.end()
    );

    writable.push_back('\0');

    char* result =
        ::mkdtemp(
            writable.data()
        );

    if (!result) {
        return {};
    }

    return fs::path(result);
}

pqnas::cluster::StorageUserProjection
projection_for(
    const std::string& node_id
) {
    pqnas::cluster::StorageUserProjection projection;

    projection.fingerprint =
        std::string(128, 'a');

    projection.enabled = true;
    projection.storage_allocated = false;
    projection.home_node = node_id;
    projection.revision = 7;

    return projection;
}

} // namespace

int main() {
    const fs::path directory =
        make_test_directory();

    if (directory.empty()) {
        std::cerr
            << "FAIL: test directory creation failed\n";

        return 1;
    }

    const auto cleanup = [&]() {
        std::error_code error;
        fs::remove_all(directory, error);
    };

    pqnas::cluster::NodeRegistry node_registry;
    std::string error;

    expect(
        node_registry.load(
            (directory / "nodes.json").string(),
            &error
        ),
        "node registry loads"
    );

    expect(
        node_registry.upsert_configured_node(
            "storage-01",
            "https://storage.example:8443",
            true,
            "2026-08-17T00:00:00Z",
            &error
        ),
        "enabled storage node added"
    );

    expect(
        node_registry.upsert_configured_node(
            "storage-disabled",
            "https://disabled.example:8443",
            false,
            "2026-08-17T00:00:00Z",
            &error
        ),
        "disabled storage node added"
    );

    expect(
        node_registry.upsert_configured_node(
            "storage-no-credential",
            "https://nocred.example:8443",
            true,
            "2026-08-17T00:00:00Z",
            &error
        ),
        "credential-less storage node added"
    );

    pqnas::cluster::NodeCredentialRegistry
        credential_registry;

    expect(
        credential_registry.load(
            (directory / "node_credentials").string(),
            &error
        ),
        "credential registry loads"
    );

    std::string token;

    expect(
        pqnas::cluster::
            generate_internal_auth_credential_token(
                &token,
                &error
            ),
        "node credential generated"
    );

    expect(
        credential_registry.create_if_absent(
            "storage-01",
            token,
            &error
        ) ==
            pqnas::cluster::
                NodeCredentialRegistryResult::Ok,
        "per-node credential persisted"
    );

    pqnas::cluster::
        clear_internal_auth_token_text(
            &token
        );

    pqnas::cluster::
        StorageUserProjectionSyncTransportResponse
            next_response{
                pqnas::cluster::
                    StorageUserProjectionSyncTransportCode::
                        Applied,
                200,
                7
            };

    bool transport_called = false;
    bool authorization_seen = false;
    std::string seen_url;
    std::string seen_node;

    pqnas::cluster::
        StorageUserProjectionSyncClientConfig config;

    config.node_registry =
        &node_registry;

    config.credential_registry =
        &credential_registry;

    config.transport =
        [&](const pqnas::cluster::
                StorageUserProjectionSyncTransportRequest&
                    request) {
            transport_called = true;

            authorization_seen =
                request.authorization.rfind(
                    "Bearer ",
                    0
                ) == 0 &&
                request.authorization.size() ==
                    7 +
                    pqnas::cluster::
                        kInternalAuthTokenChars;

            seen_url =
                request.api_base_url;

            seen_node =
                request.projection.home_node;

            return next_response;
        };

    pqnas::cluster::StorageUserProjectionSyncClient
        client(config);

    {
        const auto result =
            client.sync(
                projection_for(
                    "storage-01"
                )
            );

        expect(
            result.code ==
                pqnas::cluster::
                    StorageUserProjectionSyncClientCode::
                        Applied &&
            result.http_status == 200 &&
            result.revision == 7 &&
            transport_called &&
            authorization_seen &&
            seen_url ==
                "https://storage.example:8443" &&
            seen_node == "storage-01",
            "sync uses home-node URL and its per-node Bearer credential"
        );
    }

    {
        transport_called = false;

        const auto result =
            client.sync(
                projection_for(
                    "missing-node"
                )
            );

        expect(
            result.code ==
                pqnas::cluster::
                    StorageUserProjectionSyncClientCode::
                        NodeNotFound &&
            !transport_called,
            "unknown home node fails before transport"
        );
    }

    {
        transport_called = false;

        const auto result =
            client.sync(
                projection_for(
                    "storage-disabled"
                )
            );

        expect(
            result.code ==
                pqnas::cluster::
                    StorageUserProjectionSyncClientCode::
                        NodeDisabled &&
            !transport_called,
            "disabled node cannot receive projection"
        );
    }

    {
        transport_called = false;

        const auto result =
            client.sync(
                projection_for(
                    "storage-no-credential"
                )
            );

        expect(
            result.code ==
                pqnas::cluster::
                    StorageUserProjectionSyncClientCode::
                        CredentialUnavailable &&
            !transport_called,
            "node without credential fails before transport"
        );
    }

    {
        auto invalid =
            projection_for(
                "storage-01"
            );

        invalid.revision = 0;

        transport_called = false;

        const auto result =
            client.sync(invalid);

        expect(
            result.code ==
                pqnas::cluster::
                    StorageUserProjectionSyncClientCode::
                        InvalidInput &&
            !transport_called,
            "unversioned projection is never transported"
        );
    }

    {
        const struct {
            pqnas::cluster::
                StorageUserProjectionSyncTransportCode
                    transport_code;

            pqnas::cluster::
                StorageUserProjectionSyncClientCode
                    client_code;
        } mappings[] = {
            {
                pqnas::cluster::
                    StorageUserProjectionSyncTransportCode::
                        Unchanged,
                pqnas::cluster::
                    StorageUserProjectionSyncClientCode::
                        Unchanged
            },
            {
                pqnas::cluster::
                    StorageUserProjectionSyncTransportCode::
                        AuthenticationFailed,
                pqnas::cluster::
                    StorageUserProjectionSyncClientCode::
                        AuthenticationFailed
            },
            {
                pqnas::cluster::
                    StorageUserProjectionSyncTransportCode::
                        WrongNode,
                pqnas::cluster::
                    StorageUserProjectionSyncClientCode::
                        WrongNode
            },
            {
                pqnas::cluster::
                    StorageUserProjectionSyncTransportCode::
                        Stale,
                pqnas::cluster::
                    StorageUserProjectionSyncClientCode::
                        Stale
            },
            {
                pqnas::cluster::
                    StorageUserProjectionSyncTransportCode::
                        Conflict,
                pqnas::cluster::
                    StorageUserProjectionSyncClientCode::
                        Conflict
            },
            {
                pqnas::cluster::
                    StorageUserProjectionSyncTransportCode::
                        RateLimited,
                pqnas::cluster::
                    StorageUserProjectionSyncClientCode::
                        RateLimited
            },
            {
                pqnas::cluster::
                    StorageUserProjectionSyncTransportCode::
                        ServiceUnavailable,
                pqnas::cluster::
                    StorageUserProjectionSyncClientCode::
                        ServiceUnavailable
            },
            {
                pqnas::cluster::
                    StorageUserProjectionSyncTransportCode::
                        TransportFailed,
                pqnas::cluster::
                    StorageUserProjectionSyncClientCode::
                        TransportFailed
            }
        };

        for (const auto& mapping : mappings) {
            next_response.code =
                mapping.transport_code;

            next_response.http_status =
                503;

            next_response.revision = 0;

            const auto result =
                client.sync(
                    projection_for(
                        "storage-01"
                    )
                );

            expect(
                result.code ==
                    mapping.client_code,
                "transport result maps to client result"
            );
        }
    }

    cleanup();

    if (failures != 0) {
        std::cerr
            << "FAIL: "
            << failures
            << " projection sync client test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: storage user projection sync client tests passed\n";

    return 0;
}
