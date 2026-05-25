#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pqnas::federation {

struct CircleFederationInboxEvent {
    std::int64_t id = 0;
    std::int64_t received_epoch = 0;
    std::int64_t created_epoch = 0;

    std::string status;
    std::string circle_id;
    std::string event_id;
    std::string event_type;
    std::string origin_nas;
    std::string event_key;
    std::string event_json;
    std::string last_error;
};

struct CircleFederationInboxStats {
    std::int64_t total = 0;
    std::int64_t pending = 0;
    std::int64_t applied = 0;
    std::int64_t ignored = 0;
    std::int64_t failed = 0;
};

bool ensure_circle_federation_inbox(std::string* err);

bool store_circle_federation_inbox_event(
    const std::string& circle_id,
    const std::string& event_id,
    const std::string& event_type,
    const std::string& origin_nas,
    std::int64_t created_epoch,
    const std::string& event_key,
    const std::string& event_json,
    std::string* err);

std::vector<CircleFederationInboxEvent> list_circle_federation_inbox(
    int limit,
    std::string* err);

CircleFederationInboxStats circle_federation_inbox_stats(std::string* err);

bool mark_circle_federation_inbox_applied(
    std::int64_t id,
    std::string* err);

bool mark_circle_federation_inbox_ignored(
    std::int64_t id,
    const std::string& reason,
    std::string* err);

bool mark_circle_federation_inbox_failed(
    std::int64_t id,
    const std::string& reason,
    std::string* err);

} // namespace pqnas::federation
