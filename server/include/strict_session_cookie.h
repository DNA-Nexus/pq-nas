#pragma once

#include "httplib.h"

#include <cstddef>
#include <string>

namespace pqnas {

enum class StrictSessionCookieResult {
    Ok,
    MissingCookieHeader,
    MissingSessionCookie,
    InvalidCookieHeader
};

inline constexpr std::size_t
    kStrictSessionCookieHeaderMaxBytes =
        16 * 1024;

inline constexpr std::size_t
    kStrictSessionCookieValueMaxBytes =
        4096;

inline bool strict_cookie_name_character(
    unsigned char character
) noexcept {
    const bool alpha =
        (character >= 'A' && character <= 'Z') ||
        (character >= 'a' && character <= 'z');

    const bool digit =
        character >= '0' && character <= '9';

    switch (character) {
        case '!':
        case '#':
        case '$':
        case '%':
        case '&':
        case 0x27: // apostrophe
        case '*':
        case '+':
        case '-':
        case '.':
        case '^':
        case '_':
        case '`':
        case '|':
        case '~':
            return true;
        default:
            return alpha || digit;
    }
}

using StrictSessionCookieValueValidator =
    bool (*)(const std::string&) noexcept;

inline bool canonical_pqnas_session_cookie_value(
    const std::string& value
) noexcept {
    if (value.empty() ||
        value.size() >
            kStrictSessionCookieValueMaxBytes) {
        return false;
    }

    std::size_t dots = 0;
    std::size_t dot_position =
        std::string::npos;

    for (std::size_t i = 0;
         i < value.size();
         ++i) {
        const unsigned char character =
            static_cast<unsigned char>(
                value[i]
            );

        const bool alpha =
            (character >= 'A' && character <= 'Z') ||
            (character >= 'a' && character <= 'z');

        const bool digit =
            character >= '0' && character <= '9';

        if (character == '.') {
            ++dots;
            dot_position = i;
            continue;
        }

        // session_cookie_mint() emits URL-safe base64 without padding.
        if (!alpha &&
            !digit &&
            character != '-' &&
            character != '_') {
            return false;
        }
    }

    return dots == 1 &&
        dot_position != 0 &&
        dot_position + 1 < value.size();
}

inline bool canonical_pqnas_share_recipient_cookie_value(
    const std::string& value
) noexcept {
    // Security: recipient sessions are minted from exactly 24 random bytes,
    // encoded as unpadded base64url.
    if (value.size() != 32) {
        return false;
    }

    for (const char raw_character : value) {
        const unsigned char character =
            static_cast<unsigned char>(raw_character);

        const bool alpha =
            (character >= 'A' && character <= 'Z') ||
            (character >= 'a' && character <= 'z');

        const bool digit =
            character >= '0' && character <= '9';

        if (!alpha &&
            !digit &&
            character != '-' &&
            character != '_') {
            return false;
        }
    }

    return true;
}

inline StrictSessionCookieResult
extract_strict_session_cookie(
    const httplib::Request& request,
    const std::string& target_name,
    std::string* output,
    StrictSessionCookieValueValidator value_validator =
        canonical_pqnas_session_cookie_value
) {
    if (!output || !value_validator) {
        return
            StrictSessionCookieResult::
                InvalidCookieHeader;
    }

    output->clear();

    // Security: callers may select the credential cookie by name, but the
    // target itself must be a canonical cookie-name token.
    if (target_name.empty()) {
        return
            StrictSessionCookieResult::
                InvalidCookieHeader;
    }

    for (const char raw_character :
         target_name) {
        if (!strict_cookie_name_character(
                static_cast<unsigned char>(
                    raw_character
                ))) {
            return
                StrictSessionCookieResult::
                    InvalidCookieHeader;
        }
    }

    const auto cookie_range =
        request.headers.equal_range(
            "Cookie"
        );

    auto cookie_iterator =
        cookie_range.first;

    if (cookie_iterator ==
        cookie_range.second) {
        return
            StrictSessionCookieResult::
                MissingCookieHeader;
    }

    const std::string* cookie_header =
        &cookie_iterator->second;

    ++cookie_iterator;

    // Security: never let proxy/library duplicate-header handling choose the
    // authenticated credential for us.
    if (cookie_iterator !=
            cookie_range.second ||
        cookie_header->empty() ||
        cookie_header->size() >
            kStrictSessionCookieHeaderMaxBytes) {
        return
            StrictSessionCookieResult::
                InvalidCookieHeader;
    }

    bool found = false;
    std::size_t position = 0;

    while (position <
           cookie_header->size()) {
        while (position <
                   cookie_header->size() &&
               ((*cookie_header)[position] == ' ' ||
                (*cookie_header)[position] == '\t')) {
            ++position;
        }

        if (position >=
            cookie_header->size()) {
            break;
        }

        // Empty cookie-pairs / repeated delimiters are malformed rather than
        // an alternate representation that another parser may interpret.
        if ((*cookie_header)[position] == ';') {
            return
                StrictSessionCookieResult::
                    InvalidCookieHeader;
        }

        const std::size_t end =
            cookie_header->find(
                ';',
                position
            );

        const std::size_t segment_end =
            end == std::string::npos
                ? cookie_header->size()
                : end;

        const std::size_t equals =
            cookie_header->find(
                '=',
                position
            );

        if (equals == std::string::npos ||
            equals >= segment_end ||
            equals == position) {
            output->clear();

            return
                StrictSessionCookieResult::
                    InvalidCookieHeader;
        }

        for (std::size_t i = position;
             i < equals;
             ++i) {
            if (!strict_cookie_name_character(
                    static_cast<unsigned char>(
                        (*cookie_header)[i]
                    ))) {
                output->clear();

                return
                    StrictSessionCookieResult::
                        InvalidCookieHeader;
            }
        }

        const std::string name =
            cookie_header->substr(
                position,
                equals - position
            );

        const std::size_t value_start =
            equals + 1;

        const std::string value =
            cookie_header->substr(
                value_start,
                segment_end - value_start
            );

        if (name == target_name) {
            // Security: duplicate target names enable cookie tossing / parser
            // ambiguity. Never select first or last.
            if (found ||
                !value_validator(value)) {
                output->clear();

                return
                    StrictSessionCookieResult::
                        InvalidCookieHeader;
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

        // RFC cookie-string does not contain a trailing empty pair.
        if (position >=
            cookie_header->size()) {
            output->clear();

            return
                StrictSessionCookieResult::
                    InvalidCookieHeader;
        }
    }

    if (!found) {
        output->clear();

        return
            StrictSessionCookieResult::
                MissingSessionCookie;
    }

    return
        StrictSessionCookieResult::
            Ok;
}


// Compatibility wrapper for the normal browser user/admin session.
//
// Security: keep all existing pqnas_session consumers on the same strict
// duplicate-header / duplicate-name parser while allowing other credential
// domains, such as pqnas_workspace_session, to use identical parsing rules.
inline StrictSessionCookieResult
extract_strict_pqnas_session_cookie(
    const httplib::Request& request,
    std::string* output
) {
    return
        extract_strict_session_cookie(
            request,
            "pqnas_session",
            output
        );
}

} // namespace pqnas
