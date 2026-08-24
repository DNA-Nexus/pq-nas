#include "cluster/internal_auth.h"
#include "cluster/node_credential_registry.h"
#include "cluster/node_registry.h"
#include "cluster/node_tls_trust_store.h"
#include "cluster/storage_user_projection_audit_client.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

int failures = 0;

constexpr char kTestTlsPin[] =
    "sha256/"
    "lAhFeu/Qcc7BJ8H5hTmTCGGtG6lMlA25dclywJ/Gi2g=";

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
            "pqnas-projection-audit-client-XXXXXX"
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

    return result
        ? fs::path(result)
        : fs::path{};
}

pqnas::cluster::StorageUserProjectionAuditRequest
audit_for(
    const std::string& node_id
) {
    pqnas::cluster::StorageUserProjectionAuditRequest
        request;

    request.node_id = node_id;

    pqnas::cluster::StorageUserProjectionAuditExpected
        expected;

    expected.fingerprint =
        std::string(128, 'a');
    expected.revision = 7;
    expected.digest_hex =
        std::string(
            pqnas::cluster::
                kStorageUserProjectionDigestHexChars,
            'b'
        );

    request.expected.push_back(
        std::move(expected)
    );

    return request;
}

} // namespace

int main() {
    {
        const pqnas::cluster::
            StorageUserProjectionAuditClientResult
                default_result;

        const pqnas::cluster::
            StorageUserProjectionAuditTransportResponse
                default_transport;

        expect(
            !default_result.result.complete &&
            !default_transport.result.complete,
            "audit wrapper results fail closed by default"
        );
    }

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
            "2026-08-24T00:00:00Z",
            &error
        ),
        "enabled storage node added"
    );

    expect(
        node_registry.upsert_configured_node(
            "storage-disabled",
            "https://disabled.example:8443",
            false,
            "2026-08-24T00:00:00Z",
            &error
        ),
        "disabled storage node added"
    );

    expect(
        node_registry.upsert_configured_node(
            "storage-no-credential",
            "https://nocred.example:8443",
            true,
            "2026-08-24T00:00:00Z",
            &error
        ),
        "credential-less storage node added"
    );

    expect(
        node_registry.upsert_configured_node(
            "storage-no-trust",
            "https://notrust.example:8443",
            true,
            "2026-08-24T00:00:00Z",
            &error
        ),
        "untrusted storage node added"
    );

    pqnas::cluster::NodeTlsTrustStore
        tls_trust_store;

    expect(
        tls_trust_store.load(
            (directory / "node_tls_trust").string(),
            &error
        ),
        "TLS trust store loads"
    );

    expect(
        tls_trust_store.bind_if_absent(
            "storage-01",
            kTestTlsPin,
            &error
        ) ==
            pqnas::cluster::
                NodeTlsTrustStoreResult::Ok,
        "primary storage TLS trust persisted"
    );

    expect(
        tls_trust_store.bind_if_absent(
            "storage-no-credential",
            kTestTlsPin,
            &error
        ) ==
            pqnas::cluster::
                NodeTlsTrustStoreResult::Ok,
        "credential-less storage TLS trust persisted"
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
        "primary node credential persisted"
    );

    // Security regression: credential exists but trust does not. Audit transport
    // must never see the reusable bearer for this node.
    expect(
        credential_registry.create_if_absent(
            "storage-no-trust",
            token,
            &error
        ) ==
            pqnas::cluster::
                NodeCredentialRegistryResult::Ok,
        "untrusted node credential persisted"
    );

    pqnas::cluster::
        clear_internal_auth_token_text(
            &token
        );

    pqnas::cluster::StorageUserProjectionAuditTransportResponse
        next_response;

    next_response.code =
        pqnas::cluster::
            StorageUserProjectionAuditTransportCode::Ok;
    next_response.http_status = 200;
    next_response.result.complete = true;
    next_response.result.matched = 1;

    bool transport_called = false;
    bool authorization_seen = false;
    bool tls_pin_seen = false;
    std::string seen_url;
    std::string seen_node;

    pqnas::cluster::StorageUserProjectionAuditClientConfig
        config;

    config.node_registry = &node_registry;
    config.credential_registry =
        &credential_registry;
    config.tls_trust_store =
        &tls_trust_store;

    config.transport =
        [&](const pqnas::cluster::
                StorageUserProjectionAuditTransportRequest&
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

            tls_pin_seen =
                request.tls_spki_sha256_pin ==
                    kTestTlsPin;

            seen_url =
                request.api_base_url;
            seen_node =
                request.audit.node_id;

            return next_response;
        };

    pqnas::cluster::StorageUserProjectionAuditClient
        client(config);

    {
        const auto result =
            client.audit(
                audit_for(
                    "storage-01"
                )
            );

        expect(
            result.code ==
                pqnas::cluster::
                    StorageUserProjectionAuditClientCode::Ok &&
            result.http_status == 200 &&
            result.result.complete &&
            result.result.matched == 1 &&
            transport_called &&
            authorization_seen &&
            tls_pin_seen &&
            seen_url ==
                "https://storage.example:8443" &&
            seen_node == "storage-01",
            "audit resolves node TLS trust and bearer before transport"
        );
    }

    {
        transport_called = false;

        const auto result =
            client.audit(
                audit_for(
                    "missing-node"
                )
            );

        expect(
            result.code ==
                pqnas::cluster::
                    StorageUserProjectionAuditClientCode::
                        NodeNotFound &&
            !transport_called,
            "unknown node fails before transport"
        );
    }

    {
        transport_called = false;

        const auto result =
            client.audit(
                audit_for(
                    "storage-disabled"
                )
            );

        expect(
            result.code ==
                pqnas::cluster::
                    StorageUserProjectionAuditClientCode::
                        NodeDisabled &&
            !transport_called,
            "disabled node cannot be audited"
        );
    }

    {
        transport_called = false;

        const auto result =
            client.audit(
                audit_for(
                    "storage-no-credential"
                )
            );

        expect(
            result.code ==
                pqnas::cluster::
                    StorageUserProjectionAuditClientCode::
                        CredentialUnavailable &&
            !transport_called,
            "missing credential fails before transport"
        );
    }

    {
        transport_called = false;

        const auto result =
            client.audit(
                audit_for(
                    "storage-no-trust"
                )
            );

        expect(
            result.code ==
                pqnas::cluster::
                    StorageUserProjectionAuditClientCode::
                        TlsTrustUnavailable &&
            !transport_called,
            "missing TLS trust fails before bearer transport"
        );
    }

    {
        auto invalid =
            audit_for(
                "storage-01"
            );

        invalid.expected[0].revision = 0;
        transport_called = false;

        const auto result =
            client.audit(invalid);

        expect(
            result.code ==
                pqnas::cluster::
                    StorageUserProjectionAuditClientCode::
                        InvalidInput &&
            !transport_called,
            "invalid audit manifest is never transported"
        );
    }

    {
        auto invalid =
            audit_for(
                "storage-01"
            );

        invalid.expected.push_back(
            invalid.expected[0]
        );

        transport_called = false;

        const auto result =
            client.audit(invalid);

        expect(
            result.code ==
                pqnas::cluster::
                    StorageUserProjectionAuditClientCode::
                        InvalidInput &&
            !transport_called,
            "duplicate expected fingerprints fail closed"
        );
    }

    {
        const struct {
            pqnas::cluster::
                StorageUserProjectionAuditTransportCode
                    transport_code;

            pqnas::cluster::
                StorageUserProjectionAuditClientCode
                    client_code;
        } mappings[] = {
            {
                pqnas::cluster::
                    StorageUserProjectionAuditTransportCode::
                        Rejected,
                pqnas::cluster::
                    StorageUserProjectionAuditClientCode::
                        Rejected
            },
            {
                pqnas::cluster::
                    StorageUserProjectionAuditTransportCode::
                        AuthenticationFailed,
                pqnas::cluster::
                    StorageUserProjectionAuditClientCode::
                        AuthenticationFailed
            },
            {
                pqnas::cluster::
                    StorageUserProjectionAuditTransportCode::
                        WrongNode,
                pqnas::cluster::
                    StorageUserProjectionAuditClientCode::
                        WrongNode
            },
            {
                pqnas::cluster::
                    StorageUserProjectionAuditTransportCode::
                        RateLimited,
                pqnas::cluster::
                    StorageUserProjectionAuditClientCode::
                        RateLimited
            },
            {
                pqnas::cluster::
                    StorageUserProjectionAuditTransportCode::
                        ServiceUnavailable,
                pqnas::cluster::
                    StorageUserProjectionAuditClientCode::
                        ServiceUnavailable
            },
            {
                pqnas::cluster::
                    StorageUserProjectionAuditTransportCode::
                        InvalidResponse,
                pqnas::cluster::
                    StorageUserProjectionAuditClientCode::
                        InvalidResponse
            },
            {
                pqnas::cluster::
                    StorageUserProjectionAuditTransportCode::
                        TransportFailed,
                pqnas::cluster::
                    StorageUserProjectionAuditClientCode::
                        TransportFailed
            }
        };

        for (const auto& mapping :
             mappings) {
            next_response.code =
                mapping.transport_code;
            next_response.http_status = 503;
            next_response.result = {};

            const auto result =
                client.audit(
                    audit_for(
                        "storage-01"
                    )
                );

            expect(
                result.code ==
                    mapping.client_code &&
                !result.result.complete,
                "failed transport result maps fail-closed to audit client"
            );
        }
    }

    {
        config.transport =
            [](
                const pqnas::cluster::
                    StorageUserProjectionAuditTransportRequest&
            ) -> pqnas::cluster::
                StorageUserProjectionAuditTransportResponse {
                throw 1;
            };

        pqnas::cluster::StorageUserProjectionAuditClient
            throwing_client(config);

        const auto result =
            throwing_client.audit(
                audit_for(
                    "storage-01"
                )
            );

        expect(
            result.code ==
                pqnas::cluster::
                    StorageUserProjectionAuditClientCode::
                        TransportFailed,
            "transport exception fails closed"
        );
    }

    cleanup();

    if (failures != 0) {
        std::cerr
            << "FAIL: "
            << failures
            << " projection audit client test(s) failed\n";
        return 1;
    }

    std::cout
        << "OK: storage user projection audit client tests passed\n";
    return 0;
}
