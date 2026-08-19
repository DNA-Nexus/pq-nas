#include "cluster/storage_user_request_auth.h"

#include "cluster/storage_user_session_cookie.h"
#include "cluster/storage_user_session_policy.h"
#include "httplib.h"

#include <string>

namespace pqnas::cluster {
namespace {

void clear_outputs(
    std::string* fingerprint,
    std::string* role
) noexcept {
    if (fingerprint) {
        fingerprint->clear();
    }

    if (role) {
        role->clear();
    }
}

void reply_auth_error(
    httplib::Response& response,
    int status,
    const char* error
) {
    response.set_header(
        "Cache-Control",
        "no-store"
    );

    response.set_header(
        "Pragma",
        "no-cache"
    );

    response.set_header(
        "X-Content-Type-Options",
        "nosniff"
    );

    response.status = status;

    response.set_content(
        std::string(
            "{\"ok\":false,\"error\":\""
        ) +
        error +
        "\"}",
        "application/json; charset=utf-8"
    );
}

bool valid_cookie_value(
    const std::string& value
) noexcept {
    if (value.empty() ||
        value.size() >
            kStorageUserSessionCookieMaxBytes) {
        return false;
    }

    for (const unsigned char character : value) {
        // Security: reject controls, whitespace and cookie delimiters before
        // handing attacker-controlled text to cryptographic parsing.
        if (character <= 0x20 ||
            character >= 0x7f ||
            character == '"' ||
            character == ',' ||
            character == ';' ||
            character == '\\') {
            return false;
        }
    }

    return true;
}

bool extract_single_storage_session_cookie(
    const std::string& cookie_header,
    std::string* output
) {
    if (!output) {
        return false;
    }

    output->clear();

    bool found = false;
    std::size_t position = 0;

    while (position <
           cookie_header.size()) {
        while (position <
                   cookie_header.size() &&
               (cookie_header[position] == ' ' ||
                cookie_header[position] == '\t' ||
                cookie_header[position] == ';')) {
            ++position;
        }

        if (position >=
            cookie_header.size()) {
            break;
        }

        const std::size_t end =
            cookie_header.find(
                ';',
                position
            );

        const std::size_t segment_end =
            end == std::string::npos
                ? cookie_header.size()
                : end;

        const std::size_t equals =
            cookie_header.find(
                '=',
                position
            );

        // Security: reject malformed Cookie syntax rather than allowing
        // different layers to disagree about which value was authenticated.
        if (equals == std::string::npos ||
            equals >= segment_end ||
            equals == position) {
            output->clear();
            return false;
        }

        std::size_t name_end =
            equals;

        while (name_end > position &&
               (cookie_header[name_end - 1] == ' ' ||
                cookie_header[name_end - 1] == '\t')) {
            --name_end;
        }

        if (name_end == position) {
            output->clear();
            return false;
        }

        const std::string name =
            cookie_header.substr(
                position,
                name_end - position
            );

        std::size_t value_start =
            equals + 1;

        while (value_start < segment_end &&
               (cookie_header[value_start] == ' ' ||
                cookie_header[value_start] == '\t')) {
            ++value_start;
        }

        std::size_t value_end =
            segment_end;

        while (value_end > value_start &&
               (cookie_header[value_end - 1] == ' ' ||
                cookie_header[value_end - 1] == '\t')) {
            --value_end;
        }

        if (name ==
            kStorageUserRequestSessionCookieName) {
            // Security: duplicate cookie names enable cookie-tossing/parser
            // ambiguity. Never choose first or last.
            if (found) {
                output->clear();
                return false;
            }

            const std::string value =
                cookie_header.substr(
                    value_start,
                    value_end - value_start
                );

            if (!valid_cookie_value(
                    value)) {
                output->clear();
                return false;
            }

            *output = value;
            found = true;
        }

        if (end ==
            std::string::npos) {
            break;
        }

        position =
            end + 1;
    }

    return found;
}

} // namespace

bool require_storage_user_request_auth(
    const httplib::Request& request,
    httplib::Response& response,
    const StorageUserRequestAuthContext& context,
    std::string* out_fingerprint,
    std::string* out_role
) {
    clear_outputs(
        out_fingerprint,
        out_role
    );

    if (!out_fingerprint ||
        !out_role ||
        !context.authenticate) {
        reply_auth_error(
            response,
            500,
            "server_error"
        );

        return false;
    }

    // Security: Storage user routes deliberately have no mixed-auth mode.
    // Presence of Authorization must never make a Director/mobile bearer
    // credential usable on a Storage Node.
    if (request.headers.find(
            "Authorization") !=
        request.headers.end()) {
        reply_auth_error(
            response,
            401,
            "unauthorized"
        );

        return false;
    }

    const auto cookie_range =
        request.headers.equal_range(
            "Cookie"
        );

    auto cookie_iterator =
        cookie_range.first;

    if (cookie_iterator ==
        cookie_range.second) {
        reply_auth_error(
            response,
            401,
            "unauthorized"
        );

        return false;
    }

    const std::string* cookie_header =
        &cookie_iterator->second;

    ++cookie_iterator;

    // Security: reject duplicate Cookie headers rather than relying on proxy
    // or library-specific concatenation semantics.
    if (cookie_iterator !=
            cookie_range.second ||
        cookie_header->empty() ||
        cookie_header->size() >
            kStorageUserRequestMaxCookieHeaderBytes) {
        reply_auth_error(
            response,
            401,
            "unauthorized"
        );

        return false;
    }

    std::string cookie_value;

    if (!extract_single_storage_session_cookie(
            *cookie_header,
            &cookie_value)) {
        reply_auth_error(
            response,
            401,
            "unauthorized"
        );

        return false;
    }

    StorageUserSessionAuthenticatorOutput
        authentication;

    StorageUserSessionAuthenticatorResult
        result =
            StorageUserSessionAuthenticatorResult::
                InternalError;

    try {
        result =
            context.authenticate(
                cookie_value,
                &authentication
            );
    } catch (...) {
        result =
            StorageUserSessionAuthenticatorResult::
                InternalError;
    }

    // Security: erase the application-owned bearer copy as soon as the
    // authenticator no longer needs it.
    clear_storage_user_session_cookie_text(
        &cookie_value
    );

    if (result !=
        StorageUserSessionAuthenticatorResult::
            Authenticated) {
        if (result ==
                StorageUserSessionAuthenticatorResult::
                    Rejected ||
            result ==
                StorageUserSessionAuthenticatorResult::
                    InvalidInput) {
            reply_auth_error(
                response,
                401,
                "unauthorized"
            );
        } else if (
            result ==
                StorageUserSessionAuthenticatorResult::
                    InvalidConfiguration) {
            reply_auth_error(
                response,
                500,
                "server_error"
            );
        } else {
            reply_auth_error(
                response,
                503,
                "auth_unavailable"
            );
        }

        return false;
    }

    // Defense in depth: the authenticator already enforces this fixed role,
    // but this request boundary fails closed if that contract ever changes.
    if (authentication.fingerprint.empty() ||
        authentication.role !=
            kStorageUserSessionRole ||
        authentication.projection_revision == 0 ||
        authentication.expires_at <= 0) {
        reply_auth_error(
            response,
            503,
            "auth_unavailable"
        );

        return false;
    }

    *out_fingerprint =
        authentication.fingerprint;

    *out_role =
        authentication.role;

    return true;
}

} // namespace pqnas::cluster
