#include "cluster/storage_user_projection_audit_https.h"

#include "cluster/cluster_identity_validation.h"
#include "cluster/internal_auth.h"
#include "cluster/node_tls_https_client.h"
#include "cluster/node_tls_identity.h"

#include "storage_metadata_validation.h"

#include "httplib.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace pqnas::cluster {
namespace {

using json = nlohmann::json;

class HeaderCredentialWiper {
public:
    explicit HeaderCredentialWiper(
        httplib::Headers& headers
    ) noexcept
        : headers_(headers) {}

    ~HeaderCredentialWiper() {
        const auto range =
            headers_.equal_range(
                "Authorization"
            );

        for (auto iterator = range.first;
             iterator != range.second;
             ++iterator) {
            clear_internal_auth_token_text(
                &iterator->second
            );
        }
    }

    HeaderCredentialWiper(
        const HeaderCredentialWiper&
    ) = delete;

    HeaderCredentialWiper& operator=(
        const HeaderCredentialWiper&
    ) = delete;

private:
    httplib::Headers& headers_;
};

StorageUserProjectionAuditTransportResponse
response(
    StorageUserProjectionAuditTransportCode code,
    int http_status = 0,
    StorageUserProjectionAuditResult result = {}
) {
    StorageUserProjectionAuditTransportResponse output;
    output.code = code;
    output.http_status = http_status;

    // Security: protocol/transport failures are never a complete verification
    // result even though the pure audit primitive defaults complete=true.
    if (code !=
        StorageUserProjectionAuditTransportCode::Ok) {
        result.complete = false;
    }

    output.result = std::move(result);
    return output;
}

std::string trim_ascii(
    const std::string& value
) {
    std::size_t first = 0;

    while (first < value.size() &&
           std::isspace(
               static_cast<unsigned char>(
                   value[first]
               ))) {
        ++first;
    }

    std::size_t last = value.size();

    while (last > first &&
           std::isspace(
               static_cast<unsigned char>(
                   value[last - 1]
               ))) {
        --last;
    }

    return value.substr(
        first,
        last - first
    );
}

bool content_type_is_json(
    std::string value
) {
    const std::size_t semicolon =
        value.find(';');

    if (semicolon !=
        std::string::npos) {
        value.resize(semicolon);
    }

    value = trim_ascii(value);

    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            return static_cast<char>(
                std::tolower(character)
            );
        }
    );

    return value ==
        "application/json";
}

bool json_u64(
    const json& value,
    std::uint64_t* output
) {
    if (!output) {
        return false;
    }

    try {
        if (value.is_number_unsigned()) {
            *output =
                value.get<std::uint64_t>();
            return true;
        }

        if (value.is_number_integer()) {
            const std::int64_t signed_value =
                value.get<std::int64_t>();

            if (signed_value < 0) {
                return false;
            }

            *output =
                static_cast<std::uint64_t>(
                    signed_value
                );
            return true;
        }
    } catch (...) {
        return false;
    }

    return false;
}

bool exact_error_body(
    const json& body,
    std::string* error_code
) {
    if (!error_code ||
        !body.is_object() ||
        body.size() != 2 ||
        body.find("ok") == body.end() ||
        body.find("error") == body.end() ||
        !body["ok"].is_boolean() ||
        body["ok"].get<bool>() ||
        !body["error"].is_string()) {
        return false;
    }

    try {
        *error_code =
            body["error"].get<std::string>();
    } catch (...) {
        error_code->clear();
        return false;
    }

    if (error_code->empty() ||
        error_code->size() > 128) {
        error_code->clear();
        return false;
    }

    for (unsigned char character :
         *error_code) {
        const bool allowed =
            (character >= 'a' &&
             character <= 'z') ||
            (character >= '0' &&
             character <= '9') ||
            character == '_';

        if (!allowed) {
            error_code->clear();
            return false;
        }
    }

    return true;
}

bool valid_authorization(
    const std::string& value
) noexcept {
    constexpr char prefix[] =
        "Bearer ";

    constexpr std::size_t prefix_size =
        sizeof(prefix) - 1;

    if (value.size() !=
            prefix_size +
                kInternalAuthTokenChars ||
        value.compare(
            0,
            prefix_size,
            prefix) != 0) {
        return false;
    }

    for (std::size_t index = prefix_size;
         index < value.size();
         ++index) {
        const unsigned char character =
            static_cast<unsigned char>(
                value[index]
            );

        const bool allowed =
            (character >= 'A' &&
             character <= 'Z') ||
            (character >= 'a' &&
             character <= 'z') ||
            (character >= '0' &&
             character <= '9') ||
            character == '-' ||
            character == '_';

        if (!allowed) {
            return false;
        }
    }

    return true;
}

bool valid_digest_hex(
    const std::string& value
) {
    if (value.size() !=
        kStorageUserProjectionDigestHexChars) {
        return false;
    }

    for (unsigned char character : value) {
        const bool valid =
            (character >= '0' && character <= '9') ||
            (character >= 'a' && character <= 'f');

        if (!valid) {
            return false;
        }
    }

    return true;
}

bool fingerprint_in_range(
    const std::string& fingerprint,
    const std::string& range_start,
    const std::string& range_end
) {
    return
        (range_start.empty() ||
         fingerprint >= range_start) &&
        (range_end.empty() ||
         fingerprint < range_end);
}

bool valid_audit_manifest(
    const StorageUserProjectionAuditRequest& request
) {
    if (!valid_cluster_node_id(
            request.node_id) ||
        (!request.range_start.empty() &&
         !pqnas::valid_storage_user_fingerprint_hex(
             request.range_start)) ||
        (!request.range_end.empty() &&
         !pqnas::valid_storage_user_fingerprint_hex(
             request.range_end)) ||
        (!request.range_start.empty() &&
         !request.range_end.empty() &&
         request.range_start >=
             request.range_end) ||
        request.expected.size() >
            kStorageUserProjectionAuditMaxExpected) {
        return false;
    }

    std::string previous_fingerprint;
    bool have_previous = false;

    for (const auto& entry :
         request.expected) {
        if (!pqnas::valid_storage_user_fingerprint_hex(
                entry.fingerprint) ||
            entry.revision == 0 ||
            !valid_digest_hex(
                entry.digest_hex) ||
            !fingerprint_in_range(
                entry.fingerprint,
                request.range_start,
                request.range_end) ||
            (have_previous &&
             entry.fingerprint <=
                 previous_fingerprint)) {
            return false;
        }

        previous_fingerprint =
            entry.fingerprint;
        have_previous = true;
    }

    return true;
}

const StorageUserProjectionAuditExpected*
find_expected(
    const StorageUserProjectionAuditRequest& request,
    const std::string& fingerprint
) {
    const auto found =
        std::lower_bound(
            request.expected.begin(),
            request.expected.end(),
            fingerprint,
            [](
                const StorageUserProjectionAuditExpected& entry,
                const std::string& value
            ) {
                return entry.fingerprint < value;
            }
        );

    if (found ==
            request.expected.end() ||
        found->fingerprint != fingerprint) {
        return nullptr;
    }

    return &*found;
}

bool parse_mismatch_code(
    const std::string& text,
    StorageUserProjectionAuditMismatchCode* code
) {
    if (!code) {
        return false;
    }

    if (text == "missing") {
        *code =
            StorageUserProjectionAuditMismatchCode::Missing;
        return true;
    }

    if (text == "stale") {
        *code =
            StorageUserProjectionAuditMismatchCode::Stale;
        return true;
    }

    if (text == "conflict") {
        *code =
            StorageUserProjectionAuditMismatchCode::Conflict;
        return true;
    }

    if (text == "newer") {
        *code =
            StorageUserProjectionAuditMismatchCode::Newer;
        return true;
    }

    if (text == "wrong_node_state") {
        *code =
            StorageUserProjectionAuditMismatchCode::WrongNodeState;
        return true;
    }

    if (text == "unexpected_authorization") {
        *code =
            StorageUserProjectionAuditMismatchCode::
                UnexpectedAuthorization;
        return true;
    }

    return false;
}

bool valid_mismatch_semantics(
    const StorageUserProjectionAuditRequest& request,
    const StorageUserProjectionAuditMismatch& mismatch,
    bool* accounts_for_expected
) {
    if (!accounts_for_expected) {
        return false;
    }

    *accounts_for_expected = false;

    const auto* expected =
        find_expected(
            request,
            mismatch.fingerprint
        );

    switch (mismatch.code) {
        case StorageUserProjectionAuditMismatchCode::Missing:
            if (!expected ||
                mismatch.expected_revision !=
                    expected->revision ||
                mismatch.actual_revision != 0) {
                return false;
            }

            *accounts_for_expected = true;
            return true;

        case StorageUserProjectionAuditMismatchCode::Stale:
            if (!expected ||
                mismatch.expected_revision !=
                    expected->revision ||
                mismatch.actual_revision == 0 ||
                mismatch.actual_revision >=
                    expected->revision) {
                return false;
            }

            *accounts_for_expected = true;
            return true;

        case StorageUserProjectionAuditMismatchCode::Conflict:
            if (!expected ||
                mismatch.expected_revision !=
                    expected->revision ||
                mismatch.actual_revision !=
                    expected->revision) {
                return false;
            }

            *accounts_for_expected = true;
            return true;

        case StorageUserProjectionAuditMismatchCode::Newer:
            if (!expected ||
                mismatch.expected_revision !=
                    expected->revision ||
                mismatch.actual_revision <=
                    expected->revision) {
                return false;
            }

            *accounts_for_expected = true;
            return true;

        case StorageUserProjectionAuditMismatchCode::WrongNodeState:
            if (mismatch.actual_revision == 0) {
                return false;
            }

            if (expected) {
                if (mismatch.expected_revision !=
                    expected->revision) {
                    return false;
                }

                *accounts_for_expected = true;
                return true;
            }

            return
                mismatch.expected_revision == 0;

        case StorageUserProjectionAuditMismatchCode::
                UnexpectedAuthorization:
            return
                !expected &&
                mismatch.expected_revision == 0 &&
                mismatch.actual_revision != 0;
    }

    return false;
}

bool valid_transport_request(
    const StorageUserProjectionAuditTransportRequest& request,
    ClusterHttpsAuthority* authority
) {
    if (!authority ||
        !valid_audit_manifest(
            request.audit) ||
        !valid_authorization(
            request.authorization)) {
        return false;
    }

    std::string normalized_pin;

    if (!normalize_node_tls_spki_sha256_pin(
            request.tls_spki_sha256_pin,
            &normalized_pin,
            nullptr) ||
        normalized_pin !=
            request.tls_spki_sha256_pin) {
        return false;
    }

    std::string normalized_url;

    if (!normalize_cluster_https_base_url(
            request.api_base_url,
            &normalized_url,
            nullptr) ||
        normalized_url !=
            request.api_base_url ||
        !parse_cluster_https_authority(
            normalized_url,
            authority,
            nullptr)) {
        return false;
    }

    return true;
}

} // namespace

StorageUserProjectionAuditTransportResponse
parse_storage_user_projection_audit_https_response(
    int http_status,
    const std::string& content_type,
    const std::string& body,
    const StorageUserProjectionAuditRequest& expected_request
) {
    if (!valid_audit_manifest(
            expected_request) ||
        body.empty() ||
        body.size() >
            kStorageUserProjectionAuditHttpsMaxResponseBytes ||
        !content_type_is_json(
            content_type)) {
        return response(
            StorageUserProjectionAuditTransportCode::
                InvalidResponse,
            http_status
        );
    }

    json parsed;

    try {
        parsed =
            json::parse(body);
    } catch (...) {
        return response(
            StorageUserProjectionAuditTransportCode::
                InvalidResponse,
            http_status
        );
    }

    if (http_status == 200) {
        if (!parsed.is_object() ||
            parsed.size() != 7 ||
            parsed.find("version") == parsed.end() ||
            parsed.find("ok") == parsed.end() ||
            parsed.find("node_id") == parsed.end() ||
            parsed.find("complete") == parsed.end() ||
            parsed.find("matched") == parsed.end() ||
            parsed.find("safe_tombstones_ignored") ==
                parsed.end() ||
            parsed.find("mismatches") == parsed.end() ||
            !parsed["ok"].is_boolean() ||
            !parsed["ok"].get<bool>() ||
            !parsed["node_id"].is_string() ||
            !parsed["complete"].is_boolean() ||
            !parsed["mismatches"].is_array()) {
            return response(
                StorageUserProjectionAuditTransportCode::
                    InvalidResponse,
                http_status
            );
        }

        std::uint64_t version = 0;
        std::uint64_t matched = 0;
        std::uint64_t safe_tombstones = 0;
        std::string node_id;

        try {
            node_id =
                parsed["node_id"].get<std::string>();
        } catch (...) {
            return response(
                StorageUserProjectionAuditTransportCode::
                    InvalidResponse,
                http_status
            );
        }

        if (!json_u64(
                parsed["version"],
                &version) ||
            version != 1 ||
            node_id !=
                expected_request.node_id ||
            !json_u64(
                parsed["matched"],
                &matched) ||
            matched >
                expected_request.expected.size() ||
            !json_u64(
                parsed["safe_tombstones_ignored"],
                &safe_tombstones) ||
            matched >
                static_cast<std::uint64_t>(
                    std::numeric_limits<
                        std::size_t
                    >::max()) ||
            safe_tombstones >
                static_cast<std::uint64_t>(
                    std::numeric_limits<
                        std::size_t
                    >::max()) ||
            parsed["mismatches"].size() >
                kStorageUserProjectionAuditMaxMismatches) {
            return response(
                StorageUserProjectionAuditTransportCode::
                    InvalidResponse,
                http_status
            );
        }

        StorageUserProjectionAuditResult
            audit_result;

        audit_result.complete =
            parsed["complete"].get<bool>();

        audit_result.matched =
            static_cast<std::size_t>(
                matched
            );

        audit_result.safe_tombstones_ignored =
            static_cast<std::size_t>(
                safe_tombstones
            );

        audit_result.mismatches.reserve(
            parsed["mismatches"].size()
        );

        std::string previous_fingerprint;
        bool have_previous = false;
        std::size_t expected_mismatches = 0;

        for (const auto& item :
             parsed["mismatches"]) {
            if (!item.is_object() ||
                item.size() != 4 ||
                item.find("fingerprint") == item.end() ||
                item.find("kind") == item.end() ||
                item.find("expected_revision") ==
                    item.end() ||
                item.find("actual_revision") ==
                    item.end() ||
                !item["fingerprint"].is_string() ||
                !item["kind"].is_string()) {
                return response(
                    StorageUserProjectionAuditTransportCode::
                        InvalidResponse,
                    http_status
                );
            }

            StorageUserProjectionAuditMismatch
                mismatch;

            std::string kind;

            try {
                mismatch.fingerprint =
                    item["fingerprint"].
                        get<std::string>();

                kind =
                    item["kind"].
                        get<std::string>();
            } catch (...) {
                return response(
                    StorageUserProjectionAuditTransportCode::
                        InvalidResponse,
                    http_status
                );
            }

            if (!pqnas::valid_storage_user_fingerprint_hex(
                    mismatch.fingerprint) ||
                !fingerprint_in_range(
                    mismatch.fingerprint,
                    expected_request.range_start,
                    expected_request.range_end) ||
                (have_previous &&
                 mismatch.fingerprint <=
                     previous_fingerprint) ||
                !parse_mismatch_code(
                    kind,
                    &mismatch.code) ||
                !json_u64(
                    item["expected_revision"],
                    &mismatch.expected_revision) ||
                !json_u64(
                    item["actual_revision"],
                    &mismatch.actual_revision)) {
                return response(
                    StorageUserProjectionAuditTransportCode::
                        InvalidResponse,
                    http_status
                );
            }

            bool accounts_for_expected = false;

            if (!valid_mismatch_semantics(
                    expected_request,
                    mismatch,
                    &accounts_for_expected)) {
                return response(
                    StorageUserProjectionAuditTransportCode::
                        InvalidResponse,
                    http_status
                );
            }

            if (accounts_for_expected) {
                ++expected_mismatches;
            }

            previous_fingerprint =
                mismatch.fingerprint;
            have_previous = true;

            audit_result.mismatches.push_back(
                std::move(mismatch)
            );
        }

        if (audit_result.complete) {
            if (audit_result.matched +
                    expected_mismatches !=
                expected_request.expected.size()) {
                return response(
                    StorageUserProjectionAuditTransportCode::
                        InvalidResponse,
                    http_status
                );
            }
        } else if (
            audit_result.mismatches.size() !=
                kStorageUserProjectionAuditMaxMismatches) {
            // v1 only becomes incomplete when the bounded mismatch vector
            // reaches its hard cap. A shorter incomplete response is not proof.
            return response(
                StorageUserProjectionAuditTransportCode::
                    InvalidResponse,
                http_status
            );
        }

        return response(
            StorageUserProjectionAuditTransportCode::Ok,
            http_status,
            std::move(audit_result)
        );
    }

    std::string error_code;

    if (!exact_error_body(
            parsed,
            &error_code)) {
        return response(
            StorageUserProjectionAuditTransportCode::
                InvalidResponse,
            http_status
        );
    }

    switch (http_status) {
        case 400:
            if (error_code == "bad_request") {
                return response(
                    StorageUserProjectionAuditTransportCode::
                        Rejected,
                    http_status
                );
            }
            break;

        case 401:
            return response(
                StorageUserProjectionAuditTransportCode::
                    AuthenticationFailed,
                http_status
            );

        case 409:
            if (error_code == "wrong_node") {
                return response(
                    StorageUserProjectionAuditTransportCode::
                        WrongNode,
                    http_status
                );
            }
            break;

        case 413:
            if (error_code == "request_too_large") {
                return response(
                    StorageUserProjectionAuditTransportCode::
                        Rejected,
                    http_status
                );
            }
            break;

        case 415:
            if (error_code ==
                "unsupported_media_type") {
                return response(
                    StorageUserProjectionAuditTransportCode::
                        Rejected,
                    http_status
                );
            }
            break;

        case 429:
            if (error_code == "rate_limited") {
                return response(
                    StorageUserProjectionAuditTransportCode::
                        RateLimited,
                    http_status
                );
            }
            break;

        case 500:
            if (error_code == "server_error") {
                return response(
                    StorageUserProjectionAuditTransportCode::
                        ServiceUnavailable,
                    http_status
                );
            }
            break;

        case 503:
            if (error_code == "audit_unavailable") {
                return response(
                    StorageUserProjectionAuditTransportCode::
                        ServiceUnavailable,
                    http_status
                );
            }
            break;

        default:
            break;
    }

    return response(
        StorageUserProjectionAuditTransportCode::
            InvalidResponse,
        http_status
    );
}

StorageUserProjectionAuditTransport
make_https_storage_user_projection_audit_transport() {
    return [](
        const StorageUserProjectionAuditTransportRequest&
            request
    ) -> StorageUserProjectionAuditTransportResponse {
        ClusterHttpsAuthority authority;

        if (!valid_transport_request(
                request,
                &authority)) {
            return response(
                StorageUserProjectionAuditTransportCode::
                    InvalidResponse
            );
        }

        json expected =
            json::array();

        for (const auto& entry :
             request.audit.expected) {
            expected.push_back(
                json{
                    {
                        "fingerprint",
                        entry.fingerprint
                    },
                    {
                        "revision",
                        entry.revision
                    },
                    {
                        "digest",
                        entry.digest_hex
                    }
                }
            );
        }

        json request_json{
            {"version", 1},
            {"node_id", request.audit.node_id},
            {"range_start", request.audit.range_start},
            {"range_end", request.audit.range_end},
            {"expected", std::move(expected)}
        };

        std::string request_body;

        try {
            request_body =
                request_json.dump();
        } catch (...) {
            return response(
                StorageUserProjectionAuditTransportCode::
                    InvalidResponse
            );
        }

        if (request_body.empty() ||
            request_body.size() >
                kStorageUserProjectionAuditHttpsMaxRequestBytes) {
            return response(
                StorageUserProjectionAuditTransportCode::
                    InvalidResponse
            );
        }

        httplib::Headers headers{
            {
                "Authorization",
                request.authorization
            },
            {
                "Accept",
                "application/json"
            },
            {
                "User-Agent",
                "DNA-Nexus-Director-Projection-Audit/1"
            }
        };

        HeaderCredentialWiper header_wiper(
            headers
        );

        auto client =
            make_pinned_node_https_client(
                authority.host,
                authority.port,
                request.tls_spki_sha256_pin,
                nullptr
            );

        if (!client) {
            return response(
                StorageUserProjectionAuditTransportCode::
                    TransportFailed
            );
        }

        // Security: never forward a reusable bearer through redirects and keep
        // every audit response/body processing path explicitly bounded.
        client->set_follow_location(false);
        client->set_keep_alive(false);
        client->set_decompress(false);

        client->set_connection_timeout(5, 0);
        client->set_read_timeout(8, 0);
        client->set_write_timeout(5, 0);

        std::string response_body;
        response_body.reserve(4096);

        bool too_large = false;

        const httplib::Result result =
            client->Post(
                "/internal/storage/user-projection/audit",
                headers,
                request_body,
                "application/json",
                [&](
                    const char* data,
                    std::size_t length
                ) -> bool {
                    if (length >
                        kStorageUserProjectionAuditHttpsMaxResponseBytes -
                            response_body.size()) {
                        too_large = true;
                        return false;
                    }

                    response_body.append(
                        data,
                        length
                    );

                    return true;
                }
            );

        if (too_large) {
            return response(
                StorageUserProjectionAuditTransportCode::
                    InvalidResponse
            );
        }

        if (!result) {
            return response(
                StorageUserProjectionAuditTransportCode::
                    TransportFailed
            );
        }

        return
            parse_storage_user_projection_audit_https_response(
                result->status,
                result->get_header_value(
                    "Content-Type"
                ),
                response_body,
                request.audit
            );
    };
}

} // namespace pqnas::cluster
