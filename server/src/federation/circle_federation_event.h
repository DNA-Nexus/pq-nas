#pragma once

#include <string>

namespace pqnas::federation {

struct CircleEventDraft {
    std::string type;
    std::string event_id;
    std::string circle_id;
    std::string origin_nas;
    std::string created_at_iso;
    std::string message;
};

std::string circle_head_key(const std::string& circle_id);
std::string circle_event_key(const std::string& circle_id, const std::string& event_id);
std::string nas_presence_key(const std::string& nas_fingerprint);

std::string make_circle_ping_event_json(const CircleEventDraft& draft);

} // namespace pqnas::federation
