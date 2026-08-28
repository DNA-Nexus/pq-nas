#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace pqnas::cluster {

// Parse JSON while rejecting duplicate object keys at every nesting level.
//
// Security: ambiguous security-protocol input must fail closed rather than
// relying on first-wins/last-wins behavior from one parser layer.
inline bool parse_json_reject_duplicate_keys(
    const std::string& input,
    nlohmann::json* output
) {
    if (!output) {
        return false;
    }

    bool duplicate_key = false;

    std::vector<
        std::unordered_set<std::string>
    > object_keys;

    nlohmann::json parsed_output;

    try {
        parsed_output =
            nlohmann::json::parse(
                input,
                [&](
                    int,
                    nlohmann::json::parse_event_t event,
                    nlohmann::json& parsed
                ) {
                    switch (event) {
                        case nlohmann::json::
                                parse_event_t::
                                object_start:
                            object_keys.emplace_back();
                            break;

                        case nlohmann::json::
                                parse_event_t::key:
                            if (object_keys.empty()) {
                                duplicate_key = true;
                                break;
                            }

                            try {
                                const std::string& key =
                                    parsed.get_ref<
                                        const std::string&
                                    >();

                                if (!object_keys.back().
                                        insert(key).second) {
                                    duplicate_key = true;
                                }
                            } catch (...) {
                                duplicate_key = true;
                            }

                            break;

                        case nlohmann::json::
                                parse_event_t::
                                object_end:
                            if (object_keys.empty()) {
                                duplicate_key = true;
                            } else {
                                object_keys.pop_back();
                            }

                            break;

                        default:
                            break;
                    }

                    return true;
                }
            );
    } catch (...) {
        return false;
    }

    if (duplicate_key ||
        !object_keys.empty()) {
        return false;
    }

    // Security: publish parsed protocol state only after the entire document
    // has been proven syntactically valid and unambiguous.
    *output =
        std::move(parsed_output);

    return true;
}

} // namespace pqnas::cluster
