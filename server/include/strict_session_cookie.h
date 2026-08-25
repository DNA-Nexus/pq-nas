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

inline StrictSessionCookieResult
extract_strict_pqnas_session_cookie(
    const httplib::Request& request,
    std::string* output
) {
    if (!output) {
        return
            StrictSessionCookieResult::
                InvalidCookieHeader;
    }

    output->clear();

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

        if (name == "pqnas_session") {
            // Security: duplicate target names enable cookie tossing / parser
            // ambiguity. Never select first or last.
            if (found ||
                !canonical_pqnas_session_cookie_value(
                    value)) {
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

} // namespace pqnas
