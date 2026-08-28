#include "cluster/internal_auth.h"
#include "cluster/node_credential_registry.h"
#include "cluster/node_registry.h"
#include "cluster/node_tls_trust_store.h"
#include "cluster/storage_handoff_ticket_client.h"
#include "cluster/storage_handoff_ticket_store.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>
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
    const fs::path base =
        fs::temp_directory_path();

    std::string pattern =
        (
            base /
            (
                "pqnas-handoff-client-" +
                std::to_string(
                    static_cast<long long>(
                        ::getpid()
                    )
                ) +
                "-XXXXXX"
            )
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

std::string fingerprint() {
    return std::string(128, 'a');
}

std::string browser_binding_hash() {
    return std::string(
        pqnas::cluster::
            kStorageHandoffBrowserBindingChars,
        'A'
    );
}

std::string ticket() {
    return std::string(
        pqnas::cluster::
            kStorageHandoffTicketChars,
        'A'
    );
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

    pqnas::cluster::NodeRegistry
        node_registry;

    std::string error;

    expect(
        node_registry.load(
            (directory / "nodes.json").string(),
            &error
        ),
        "node registry loads"
    );

    const auto add_node =
        [&](const std::string& node_id,
            const std::string& url,
            bool enabled) {
            expect(
                node_registry.upsert_configured_node(
                    node_id,
                    url,
                    enabled,
                    "2026-08-28T00:00:00Z",
                    &error
                ),
                "node added: " + node_id
            );
        };

    add_node(
        "storage-01",
        "https://storage.example:8443",
        true
    );

    add_node(
        "storage-disabled",
        "https://disabled.example:8443",
        false
    );

    add_node(
        "storage-no-credential",
        "https://nocred.example:8443",
        true
    );

    add_node(
        "storage-no-trust",
        "https://notrust.example:8443",
        true
    );

    pqnas::cluster::NodeTlsTrustStore
        tls_trust_store;

    error.clear();

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
        "primary node TLS trust persisted"
    );

    expect(
        tls_trust_store.bind_if_absent(
            "storage-no-credential",
            kTestTlsPin,
            &error
        ) ==
            pqnas::cluster::
                NodeTlsTrustStoreResult::Ok,
        "credential-less node TLS trust persisted"
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

    // Security regression: this node has bearer material but no TLS trust.
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

    pqnas::cluster::
        StorageHandoffTicketTransportResponse
            next_response;

    next_response.code =
        pqnas::cluster::
            StorageHandoffTicketTransportCode::Ok;

    next_response.http_status = 200;
    next_response.ticket = ticket();
    next_response.expires_at = 123456;

    bool transport_called = false;
    bool authorization_seen = false;
    bool tls_pin_seen = false;
    std::string seen_url;
    std::string seen_fingerprint;
    std::string seen_binding_hash;

    pqnas::cluster::
        StorageHandoffTicketClientConfig config;

    config.node_registry =
        &node_registry;

    config.credential_registry =
        &credential_registry;

    config.tls_trust_store =
        &tls_trust_store;

    config.transport =
        [&](const pqnas::cluster::
                StorageHandoffTicketTransportRequest&
                    request) {
            transport_called = true;

            tls_pin_seen =
                request.tls_spki_sha256_pin ==
                    kTestTlsPin;

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

            seen_fingerprint =
                request.fingerprint;

            seen_binding_hash =
                request.browser_binding_hash;

            return next_response;
        };

    pqnas::cluster::
        StorageHandoffTicketClient
            client(config);

    {
        const auto result =
            client.issue(
                "storage-01",
                fingerprint(),
                browser_binding_hash()
            );

        expect(
            result.code ==
                pqnas::cluster::
                    StorageHandoffTicketClientCode::Ok &&
            result.http_status == 200 &&
            result.ticket == ticket() &&
            result.expires_at == 123456 &&
            transport_called &&
            tls_pin_seen &&
            authorization_seen &&
            seen_url ==
                "https://storage.example:8443" &&
            seen_fingerprint ==
                fingerprint() &&
            seen_binding_hash ==
                browser_binding_hash(),
            "issue uses exact node URL, trust, bearer and browser binding"
        );
    }

    {
        transport_called = false;

        const auto result =
            client.issue(
                "missing-node",
                fingerprint(),
                browser_binding_hash()
            );

        expect(
            result.code ==
                pqnas::cluster::
                    StorageHandoffTicketClientCode::
                        NodeNotFound &&
            !transport_called,
            "unknown node fails before transport"
        );
    }

    {
        transport_called = false;

        const auto result =
            client.issue(
                "storage-disabled",
                fingerprint(),
                browser_binding_hash()
            );

        expect(
            result.code ==
                pqnas::cluster::
                    StorageHandoffTicketClientCode::
                        NodeDisabled &&
            !transport_called,
            "disabled node fails before transport"
        );
    }

    {
        transport_called = false;

        const auto result =
            client.issue(
                "storage-no-credential",
                fingerprint(),
                browser_binding_hash()
            );

        expect(
            result.code ==
                pqnas::cluster::
                    StorageHandoffTicketClientCode::
                        CredentialUnavailable &&
            !transport_called,
            "credential-less node fails before transport"
        );
    }

    {
        transport_called = false;

        const auto result =
            client.issue(
                "storage-no-trust",
                fingerprint(),
                browser_binding_hash()
            );

        expect(
            result.code ==
                pqnas::cluster::
                    StorageHandoffTicketClientCode::
                        TlsTrustUnavailable &&
            !transport_called,
            "TLS trust is required before bearer transport"
        );
    }

    {
        transport_called = false;

        const auto result =
            client.issue(
                "storage-01",
                std::string(127, 'a'),
                browser_binding_hash()
            );

        expect(
            result.code ==
                pqnas::cluster::
                    StorageHandoffTicketClientCode::
                        InvalidInput &&
            !transport_called,
            "invalid fingerprint rejected"
        );
    }

    {
        transport_called = false;

        const auto result =
            client.issue(
                "storage-01",
                fingerprint(),
                "invalid-binding"
            );

        expect(
            result.code ==
                pqnas::cluster::
                    StorageHandoffTicketClientCode::
                        InvalidInput &&
            !transport_called,
            "invalid browser binding rejected"
        );
    }

    {
        const struct {
            pqnas::cluster::
                StorageHandoffTicketTransportCode
                    transport_code;

            pqnas::cluster::
                StorageHandoffTicketClientCode
                    client_code;
        } mappings[] = {
            {
                pqnas::cluster::
                    StorageHandoffTicketTransportCode::
                        Rejected,
                pqnas::cluster::
                    StorageHandoffTicketClientCode::
                        Rejected
            },
            {
                pqnas::cluster::
                    StorageHandoffTicketTransportCode::
                        AuthenticationFailed,
                pqnas::cluster::
                    StorageHandoffTicketClientCode::
                        AuthenticationFailed
            },
            {
                pqnas::cluster::
                    StorageHandoffTicketTransportCode::
                        UserNotAuthorized,
                pqnas::cluster::
                    StorageHandoffTicketClientCode::
                        UserNotAuthorized
            },
            {
                pqnas::cluster::
                    StorageHandoffTicketTransportCode::
                        RateLimited,
                pqnas::cluster::
                    StorageHandoffTicketClientCode::
                        RateLimited
            },
            {
                pqnas::cluster::
                    StorageHandoffTicketTransportCode::
                        ServiceUnavailable,
                pqnas::cluster::
                    StorageHandoffTicketClientCode::
                        ServiceUnavailable
            },
            {
                pqnas::cluster::
                    StorageHandoffTicketTransportCode::
                        InvalidResponse,
                pqnas::cluster::
                    StorageHandoffTicketClientCode::
                        InvalidResponse
            },
            {
                pqnas::cluster::
                    StorageHandoffTicketTransportCode::
                        TransportFailed,
                pqnas::cluster::
                    StorageHandoffTicketClientCode::
                        TransportFailed
            }
        };

        for (const auto& mapping : mappings) {
            next_response.code =
                mapping.transport_code;

            next_response.http_status =
                503;

            // Defense-in-depth regression: error results must not propagate
            // accidental plaintext ticket material.
            next_response.ticket =
                ticket();

            next_response.expires_at =
                123456;

            const auto result =
                client.issue(
                    "storage-01",
                    fingerprint(),
                    browser_binding_hash()
                );

            expect(
                result.code ==
                    mapping.client_code &&
                result.ticket.empty() &&
                result.expires_at == 0,
                "transport error mapped without leaking ticket"
            );
        }
    }

    {
        next_response.code =
            pqnas::cluster::
                StorageHandoffTicketTransportCode::Ok;

        next_response.http_status = 200;
        next_response.ticket =
            std::string(
                pqnas::cluster::
                    kStorageHandoffTicketChars,
                'B'
            );
        next_response.expires_at = 123456;

        const auto result =
            client.issue(
                "storage-01",
                fingerprint(),
                browser_binding_hash()
            );

        expect(
            result.code ==
                pqnas::cluster::
                    StorageHandoffTicketClientCode::
                        InvalidResponse &&
            result.ticket.empty(),
            "malformed success ticket rejected by client"
        );
    }

    cleanup();

    if (failures != 0) {
        std::cerr
            << "FAIL: "
            << failures
            << " handoff ticket client test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: storage handoff ticket client tests passed\n";

    return 0;
}
