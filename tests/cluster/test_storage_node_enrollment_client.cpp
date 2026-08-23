#include "cluster/storage_node_enrollment_client.h"

#include "cluster/internal_auth.h"
#include "cluster/node_enrollment_token.h"

#include <sodium.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

using pqnas::cluster::
    StorageNodeEnrollmentClient;
using pqnas::cluster::
    StorageNodeEnrollmentClientCode;
using pqnas::cluster::
    StorageNodeEnrollmentClientConfig;
using pqnas::cluster::
    StorageNodeEnrollmentTransportCode;
using pqnas::cluster::
    StorageNodeEnrollmentTransportRequest;
using pqnas::cluster::
    StorageNodeEnrollmentTransportResponse;
using pqnas::cluster::
    StorageNodeProvisioningIntent;
using pqnas::cluster::
    StorageNodeProvisioningStage;
using pqnas::cluster::
    StorageNodeProvisioningStore;

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
            "pqnas-storage-node-enrollment-client-XXXXXX"
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

StorageNodeProvisioningIntent
make_intent(
    const std::string& suffix
) {
    StorageNodeProvisioningIntent intent;

    intent.enrollment_id =
        "enrollment_" +
        suffix;

    intent.expected_generation =
        1;

    intent.node_id =
        "storage-" +
        suffix;

    intent.director_url =
        "https://director.internal";

    intent.api_base_url =
        "https://storage-" +
        suffix +
        ".internal";

    intent.protocol_version =
        7;

    return intent;
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

bool load_store(
    const fs::path& root,
    StorageNodeProvisioningStore* store,
    fs::path* credential_file,
    std::string* error
) {
    if (!store ||
        !credential_file) {
        return false;
    }

    std::error_code create_error;

    fs::create_directories(
        root,
        create_error
    );

    if (create_error) {
        if (error) {
            *error =
                "failed to create client test root";
        }

        return false;
    }

    const fs::path private_directory =
        root /
        "private";

    const fs::path state_file =
        private_directory /
        "state.json";

    *credential_file =
        private_directory /
        "node.credential";

    return store->load(
        state_file.string(),
        credential_file->string(),
        error
    );
}

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

    std::string error;

    {
        StorageNodeProvisioningStore store;
        fs::path credential_file;

        expect(
            load_store(
                root / "fresh",
                &store,
                &credential_file,
                &error
            ),
            "load fresh client store: " +
                error
        );

        std::string bootstrap_token;

        expect(
            make_bootstrap_token(
                &bootstrap_token
            ),
            "generate fresh bootstrap token"
        );

        Clock clock{
            {
                "2026-07-31T12:00:00.123Z",
                "2026-07-31T12:00:01.456Z"
            }
        };

        int transport_calls = 0;
        std::string observed_credential;

        StorageNodeEnrollmentClientConfig
            config;

        config.tls_spki_sha256_pin =
            kTestTlsPin;

        config.store =
            &store;

        config.now_iso_utc =
            [&]() {
                return clock.next();
            };

        config.transport =
            [&](const StorageNodeEnrollmentTransportRequest&
                    request) {
                ++transport_calls;

                expect(
                    request.authorization_token ==
                        bootstrap_token,
                    "fresh request uses bootstrap token"
                );

                expect(
                    pqnas::cluster::
                        valid_internal_auth_credential_token(
                            request.node_credential
                        ),
                    "fresh request has canonical node credential"
                );


                expect(
                    request.tls_spki_sha256_pin ==
                        kTestTlsPin,
                    "fresh request carries canonical TLS SPKI pin"
                );

                observed_credential =
                    request.node_credential;

                return
                    StorageNodeEnrollmentTransportResponse{
                        StorageNodeEnrollmentTransportCode::
                            Ok,
                        false,
                        200
                    };
            };

        StorageNodeEnrollmentClient client(
            std::move(config)
        );

        const auto result =
            client.enroll(
                make_intent("fresh"),
                bootstrap_token
            );

        expect(
            result.ok &&
            !result.recovered &&
            !result.already_enrolled &&
            result.code ==
                StorageNodeEnrollmentClientCode::
                    Ok,
            "fresh client enrollment succeeds"
        );

        expect(
            transport_calls == 1,
            "fresh client performs one transport call"
        );

        const auto snapshot =
            store.snapshot();

        expect(
            snapshot.has_value() &&
            snapshot->stage ==
                StorageNodeProvisioningStage::
                    Enrolled,
            "fresh success commits local Enrolled state"
        );

        expect(
            snapshot.has_value() &&
            snapshot->prepared_at ==
                "2026-07-31T12:00:00Z" &&
            snapshot->updated_at ==
                "2026-07-31T12:00:01Z" &&
            snapshot->enrolled_at ==
                "2026-07-31T12:00:01Z",
            "fresh client canonicalizes millisecond timestamps"
        );

        pqnas::cluster::
            clear_node_enrollment_token_text(
                &bootstrap_token
            );

        pqnas::cluster::
            clear_internal_auth_token_text(
                &observed_credential
            );
    }

    {
        StorageNodeProvisioningStore store;
        fs::path credential_file;

        error.clear();

        expect(
            load_store(
                root / "fallback",
                &store,
                &credential_file,
                &error
            ),
            "load fallback client store: " +
                error
        );

        std::string bootstrap_token;

        expect(
            make_bootstrap_token(
                &bootstrap_token
            ),
            "generate fallback bootstrap token"
        );

        const StorageNodeProvisioningIntent intent =
            make_intent("fallback");

        Clock first_clock{
            {
                "2026-07-31T12:10:00Z"
            }
        };

        std::string persisted_credential;

        StorageNodeEnrollmentClientConfig
            first_config;

        first_config.tls_spki_sha256_pin =
            kTestTlsPin;

        first_config.store =
            &store;

        first_config.now_iso_utc =
            [&]() {
                return first_clock.next();
            };

        first_config.transport =
            [&](const StorageNodeEnrollmentTransportRequest&
                    request) {
                persisted_credential =
                    request.node_credential;

                return
                    StorageNodeEnrollmentTransportResponse{
                        StorageNodeEnrollmentTransportCode::
                            TransportFailed,
                        false,
                        0
                    };
            };

        StorageNodeEnrollmentClient first_client(
            std::move(first_config)
        );

        const auto first_result =
            first_client.enroll(
                intent,
                bootstrap_token
            );

        expect(
            first_result.code ==
                StorageNodeEnrollmentClientCode::
                    DirectorUnavailable &&
            !first_result.ok,
            "ambiguous first request remains locally pending"
        );

        const auto pending_snapshot =
            store.snapshot();

        expect(
            pending_snapshot.has_value() &&
            pending_snapshot->stage ==
                StorageNodeProvisioningStage::
                    CredentialPersisted,
            "failed transport preserves CredentialPersisted state"
        );

        Clock retry_clock{
            {
                "2026-07-31T12:10:10Z",
                "2026-07-31T12:10:11Z"
            }
        };

        int retry_calls = 0;

        StorageNodeEnrollmentClientConfig
            retry_config;

        retry_config.tls_spki_sha256_pin =
            kTestTlsPin;

        retry_config.store =
            &store;

        retry_config.now_iso_utc =
            [&]() {
                return retry_clock.next();
            };

        retry_config.transport =
            [&](const StorageNodeEnrollmentTransportRequest&
                    request) {
                ++retry_calls;

                expect(
                    request.node_credential ==
                        persisted_credential,
                    "fallback retry preserves persisted credential"
                );

                if (retry_calls == 1) {
                    expect(
                        request.authorization_token ==
                            bootstrap_token,
                        "fallback first retries bootstrap token"
                    );

                    return
                        StorageNodeEnrollmentTransportResponse{
                            StorageNodeEnrollmentTransportCode::
                                AuthenticationFailed,
                            false,
                            401
                        };
                }

                expect(
                    request.authorization_token ==
                        persisted_credential,
                    "fallback second attempt uses persisted credential"
                );

                return
                    StorageNodeEnrollmentTransportResponse{
                        StorageNodeEnrollmentTransportCode::
                            Ok,
                        true,
                        200
                    };
            };

        StorageNodeEnrollmentClient retry_client(
            std::move(retry_config)
        );

        const auto retry_result =
            retry_client.enroll(
                intent,
                bootstrap_token
            );

        expect(
            retry_result.ok &&
            retry_result.recovered &&
            retry_calls == 2,
            "401 triggers exactly one credential recovery attempt"
        );

        int enrolled_transport_calls = 0;

        Clock enrolled_clock{
            {
                "2026-07-31T12:10:20Z"
            }
        };

        StorageNodeEnrollmentClientConfig
            enrolled_config;

        enrolled_config.tls_spki_sha256_pin =
            kTestTlsPin;

        enrolled_config.store =
            &store;

        enrolled_config.now_iso_utc =
            [&]() {
                return enrolled_clock.next();
            };

        enrolled_config.transport =
            [&](const StorageNodeEnrollmentTransportRequest&) {
                ++enrolled_transport_calls;

                return
                    StorageNodeEnrollmentTransportResponse{
                        StorageNodeEnrollmentTransportCode::
                            TransportFailed,
                        false,
                        0
                    };
            };

        StorageNodeEnrollmentClient enrolled_client(
            std::move(enrolled_config)
        );

        const auto enrolled_result =
            enrolled_client.enroll(
                intent,
                ""
            );

        expect(
            enrolled_result.ok &&
            enrolled_result.already_enrolled &&
            enrolled_transport_calls == 0,
            "already enrolled state performs no network call"
        );

        pqnas::cluster::
            clear_node_enrollment_token_text(
                &bootstrap_token
            );

        pqnas::cluster::
            clear_internal_auth_token_text(
                &persisted_credential
            );
    }

    {
        StorageNodeProvisioningStore store;
        fs::path credential_file;

        error.clear();

        expect(
            load_store(
                root / "credential-only",
                &store,
                &credential_file,
                &error
            ),
            "load credential-only client store: " +
                error
        );

        std::string bootstrap_token;

        expect(
            make_bootstrap_token(
                &bootstrap_token
            ),
            "generate credential-only bootstrap token"
        );

        const StorageNodeProvisioningIntent intent =
            make_intent("credential-only");

        Clock first_clock{
            {
                "2026-07-31T12:20:00Z"
            }
        };

        std::string persisted_credential;

        StorageNodeEnrollmentClientConfig
            first_config;

        first_config.tls_spki_sha256_pin =
            kTestTlsPin;

        first_config.store =
            &store;

        first_config.now_iso_utc =
            [&]() {
                return first_clock.next();
            };

        first_config.transport =
            [&](const StorageNodeEnrollmentTransportRequest&
                    request) {
                persisted_credential =
                    request.node_credential;

                return
                    StorageNodeEnrollmentTransportResponse{
                        StorageNodeEnrollmentTransportCode::
                            TransportFailed,
                        false,
                        0
                    };
            };

        StorageNodeEnrollmentClient first_client(
            std::move(first_config)
        );

        (void)first_client.enroll(
            intent,
            bootstrap_token
        );

        Clock retry_clock{
            {
                "2026-07-31T12:20:10Z",
                "2026-07-31T12:20:11Z"
            }
        };

        int calls = 0;

        StorageNodeEnrollmentClientConfig
            retry_config;

        retry_config.tls_spki_sha256_pin =
            kTestTlsPin;

        retry_config.store =
            &store;

        retry_config.now_iso_utc =
            [&]() {
                return retry_clock.next();
            };

        retry_config.transport =
            [&](const StorageNodeEnrollmentTransportRequest&
                    request) {
                ++calls;

                expect(
                    request.authorization_token ==
                        persisted_credential &&
                    request.node_credential ==
                        persisted_credential,
                    "empty bootstrap retry uses only persisted credential"
                );

                return
                    StorageNodeEnrollmentTransportResponse{
                        StorageNodeEnrollmentTransportCode::
                            Ok,
                        true,
                        200
                    };
            };

        StorageNodeEnrollmentClient retry_client(
            std::move(retry_config)
        );

        const auto result =
            retry_client.enroll(
                intent,
                ""
            );

        expect(
            result.ok &&
            result.recovered &&
            calls == 1,
            "credential-only finalized recovery succeeds"
        );

        pqnas::cluster::
            clear_node_enrollment_token_text(
                &bootstrap_token
            );

        pqnas::cluster::
            clear_internal_auth_token_text(
                &persisted_credential
            );
    }

    {
        StorageNodeProvisioningStore store;
        fs::path credential_file;

        error.clear();

        expect(
            load_store(
                root / "no-fallback",
                &store,
                &credential_file,
                &error
            ),
            "load no-fallback client store: " +
                error
        );

        std::string bootstrap_token;

        expect(
            make_bootstrap_token(
                &bootstrap_token
            ),
            "generate no-fallback bootstrap token"
        );

        Clock clock{
            {
                "2026-07-31T12:30:00Z"
            }
        };

        int calls = 0;

        StorageNodeEnrollmentClientConfig
            config;

        config.tls_spki_sha256_pin =
            kTestTlsPin;

        config.store =
            &store;

        config.now_iso_utc =
            [&]() {
                return clock.next();
            };

        config.transport =
            [&](const StorageNodeEnrollmentTransportRequest&) {
                ++calls;

                return
                    StorageNodeEnrollmentTransportResponse{
                        StorageNodeEnrollmentTransportCode::
                            Conflict,
                        false,
                        409
                    };
            };

        StorageNodeEnrollmentClient client(
            std::move(config)
        );

        const auto result =
            client.enroll(
                make_intent("no-fallback"),
                bootstrap_token
            );

        expect(
            result.code ==
                StorageNodeEnrollmentClientCode::
                    DirectorConflict &&
            !result.ok &&
            calls == 1,
            "non-authentication failure never triggers credential fallback"
        );

        const auto snapshot =
            store.snapshot();

        expect(
            snapshot.has_value() &&
            snapshot->stage ==
                StorageNodeProvisioningStage::
                    CredentialPersisted,
            "Director conflict never marks local state enrolled"
        );

        pqnas::cluster::
            clear_node_enrollment_token_text(
                &bootstrap_token
            );
    }

    {
        StorageNodeProvisioningStore store;
        fs::path credential_file;

        error.clear();

        expect(
            load_store(
                root / "local-commit-failure",
                &store,
                &credential_file,
                &error
            ),
            "load local commit failure store: " +
                error
        );

        std::string bootstrap_token;

        expect(
            make_bootstrap_token(
                &bootstrap_token
            ),
            "generate local failure bootstrap token"
        );

        Clock clock{
            {
                "2026-07-31T12:40:00Z",
                "2026-07-31T12:40:01Z"
            }
        };

        StorageNodeEnrollmentClientConfig
            config;

        config.tls_spki_sha256_pin =
            kTestTlsPin;

        config.store =
            &store;

        config.now_iso_utc =
            [&]() {
                return clock.next();
            };

        config.transport =
            [&](const StorageNodeEnrollmentTransportRequest&) {
                std::error_code remove_error;

                fs::remove(
                    credential_file,
                    remove_error
                );

                return
                    StorageNodeEnrollmentTransportResponse{
                        StorageNodeEnrollmentTransportCode::
                            Ok,
                        false,
                        200
                    };
            };

        StorageNodeEnrollmentClient client(
            std::move(config)
        );

        const auto result =
            client.enroll(
                make_intent("local-commit-failure"),
                bootstrap_token
            );

        expect(
            result.code ==
                StorageNodeEnrollmentClientCode::
                    LocalStateUnavailable &&
            !result.ok,
            "Director success without local credential durability fails closed"
        );

        const auto snapshot =
            store.snapshot();

        expect(
            snapshot.has_value() &&
            snapshot->stage ==
                StorageNodeProvisioningStage::
                    CredentialPersisted,
            "failed local completion does not install Enrolled state"
        );

        pqnas::cluster::
            clear_node_enrollment_token_text(
                &bootstrap_token
            );
    }

    {
        StorageNodeProvisioningStore store;
        fs::path credential_file;

        error.clear();

        expect(
            load_store(
                root / "invalid",
                &store,
                &credential_file,
                &error
            ),
            "load invalid-input store: " +
                error
        );

        Clock clock{
            {
                "2026-07-31T12:50:00Z"
            }
        };

        int calls = 0;

        StorageNodeEnrollmentClientConfig
            config;

        config.tls_spki_sha256_pin =
            kTestTlsPin;

        config.store =
            &store;

        config.now_iso_utc =
            [&]() {
                return clock.next();
            };

        config.transport =
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

        StorageNodeEnrollmentClient client(
            std::move(config)
        );

        const auto result =
            client.enroll(
                make_intent("invalid"),
                ""
            );

        expect(
            result.code ==
                StorageNodeEnrollmentClientCode::
                    InvalidInput &&
            calls == 0 &&
            !store.snapshot().has_value(),
            "brand-new enrollment requires bootstrap token"
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
            << " storage node enrollment client test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: storage node enrollment client tests passed\n";

    return 0;
}
