#include "federation/circle_federation_event.h"

#include <sstream>
#include <stdexcept>

namespace pqnas::federation {
namespace {

void require_non_empty(const std::string& value, const char* field) {
    if (value.empty()) {
        throw std::invalid_argument(std::string(field) + " is empty");
    }
}

std::string json_escape(const std::string& value) {
    std::ostringstream out;

    for (unsigned char c : value) {
        switch (c) {
            case '"':  out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20) {
                    const char* hex = "0123456789abcdef";
                    out << "\\u00" << hex[(c >> 4) & 0x0f] << hex[c & 0x0f];
                } else {
                    out << static_cast<char>(c);
                }
        }
    }

    return out.str();
}

} // namespace

std::string circle_head_key(const std::string& circle_id) {
    require_non_empty(circle_id, "circle_id");
    return "pqnas:circlestack:circle:" + circle_id + ":head";
}

std::string circle_event_key(const std::string& circle_id, const std::string& event_id) {
    require_non_empty(circle_id, "circle_id");
    require_non_empty(event_id, "event_id");
    return "pqnas:circlestack:circle:" + circle_id + ":event:" + event_id;
}

std::string nas_presence_key(const std::string& nas_fingerprint) {
    require_non_empty(nas_fingerprint, "nas_fingerprint");
    return "pqnas:circlestack:nas:" + nas_fingerprint + ":presence";
}

std::string make_circle_ping_event_json(const CircleEventDraft& draft) {
    require_non_empty(draft.type, "type");
    require_non_empty(draft.event_id, "event_id");
    require_non_empty(draft.circle_id, "circle_id");
    require_non_empty(draft.origin_nas, "origin_nas");
    require_non_empty(draft.created_at_iso, "created_at_iso");

    std::ostringstream out;
    out << "{"
        << "\"type\":\"" << json_escape(draft.type) << "\","
        << "\"event_id\":\"" << json_escape(draft.event_id) << "\","
        << "\"circle_id\":\"" << json_escape(draft.circle_id) << "\","
        << "\"origin_nas\":\"" << json_escape(draft.origin_nas) << "\","
        << "\"created_at\":\"" << json_escape(draft.created_at_iso) << "\","
        << "\"payload\":{"
        << "\"message\":\"" << json_escape(draft.message) << "\""
        << "}"
        << "}";

    return out.str();
}

} // namespace pqnas::federation
