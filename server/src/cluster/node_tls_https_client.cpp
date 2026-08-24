#include "cluster/node_tls_https_client.h"

#include "cluster/node_tls_identity.h"
#include "cluster/cluster_identity_validation.h"

#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <memory>
#include <string>

namespace pqnas::cluster {
namespace {

void set_error(
    std::string* error,
    const std::string& message
) {
    if (error) {
        *error = message;
    }
}


} // namespace

std::unique_ptr<httplib::SSLClient>
make_pinned_node_https_client(
    const std::string& host,
    int port,
    const std::string& expected_spki_sha256_pin,
    std::string* error
) {
    if (error) {
        error->clear();
    }

    std::string normalized_pin;

    if (!normalize_node_tls_spki_sha256_pin(
            expected_spki_sha256_pin,
            &normalized_pin,
            nullptr) ||
        normalized_pin !=
            expected_spki_sha256_pin) {
        // Security: there is no blank/default/unpinned client path.
        set_error(
            error,
            "node TLS SPKI pin is invalid"
        );
        return nullptr;
    }

    if (host.empty() ||
        port <= 0 ||
        port > 65535) {
        set_error(
            error,
            "node HTTPS authority is invalid"
        );
        return nullptr;
    }

    auto client =
        std::make_unique<httplib::SSLClient>(
            host,
            port
        );

    if (!client->is_valid()) {
        set_error(
            error,
            "node TLS client initialization failed"
        );
        return nullptr;
    }

    // Security: pinning strengthens rather than replaces PKI verification.
    client->enable_server_certificate_verification(
        true
    );

    client->enable_server_hostname_verification(
        true
    );

    client->set_server_certificate_verifier(
        [expected_spki_sha256_pin](
            SSL* ssl
        ) noexcept -> httplib::SSLVerifierResponse {
            if (!ssl) {
                return
                    httplib::SSLVerifierResponse::
                        CertificateRejected;
            }

            X509* certificate =
                SSL_get1_peer_certificate(
                    ssl
                );

            if (!certificate) {
                return
                    httplib::SSLVerifierResponse::
                        CertificateRejected;
            }

            const bool matches =
                node_tls_x509_matches_spki_sha256_pin(
                    certificate,
                    expected_spki_sha256_pin
                );

            X509_free(
                certificate
            );

            // IMPORTANT: never return CertificateAccepted here. A pin match
            // must still pass normal CA and hostname verification.
            return matches
                ? httplib::SSLVerifierResponse::
                      NoDecisionMade
                : httplib::SSLVerifierResponse::
                      CertificateRejected;
        }
    );

    return client;
}

bool verify_pinned_node_https_endpoint(
    const std::string& api_base_url,
    const std::string& expected_spki_sha256_pin,
    std::string* error
) {
    if (error) {
        error->clear();
    }

    ClusterHttpsAuthority authority;

    if (!parse_cluster_https_authority(
            api_base_url,
            &authority,
            error)) {
        return false;
    }

    auto client =
        make_pinned_node_https_client(
            authority.host,
            authority.port,
            expected_spki_sha256_pin,
            error
        );

    if (!client) {
        return false;
    }

    // Security: this probe proves the candidate endpoint before its identity
    // is persisted. It deliberately carries no bearer or other cluster secret.
    client->set_follow_location(false);
    client->set_keep_alive(false);
    client->set_decompress(false);

    client->set_connection_timeout(5, 0);
    client->set_read_timeout(5, 0);
    client->set_write_timeout(5, 0);

    const httplib::Result result =
        client->Head("/");

    if (!result) {
        set_error(
            error,
            "node HTTPS identity verification failed"
        );
        return false;
    }

    // Any HTTP response is sufficient here. Authentication and application
    // semantics are not being tested; the verified TLS channel is.
    return true;
}

} // namespace pqnas::cluster
