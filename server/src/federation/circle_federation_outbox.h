#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pqnas::federation {

struct CircleFederationOutboxEvent {
    std::int64_t id = 0;
    std::int64_t created_epoch = 0;
    std::int64_t updated_epoch = 0;
    std::int64_t next_attempt_epoch = 0;
    int attempts = 0;

    std::string status;
    std::string event_type;
    std::string circle_id;
    std::string event_id;
    std::string event_key;
    std::string head_key;
    std::string event_json;
    std::string last_error;
};

struct CircleFederationOutboxStats {
    std::int64_t total = 0;
    std::int64_t pending = 0;
    std::int64_t publishing = 0;
    std::int64_t done = 0;
    std::int64_t failed = 0;
    std::int64_t retry_wait = 0;
};

bool ensure_circle_federation_outbox(std::string* err);

bool enqueue_circle_federation_event(
    const std::string& event_type,
    const std::string& circle_id,
    const std::string& event_id,
    const std::string& event_key,
    const std::string& head_key,
    const std::string& event_json,
    std::string* err);

std::vector<CircleFederationOutboxEvent> list_circle_federation_outbox(
    int limit,
    std::string* err);

CircleFederationOutboxStats circle_federation_outbox_stats(std::string* err);

std::vector<CircleFederationOutboxEvent> claim_circle_federation_outbox_pending(
    int limit,
    int lease_seconds,
    std::string* err);

bool mark_circle_federation_outbox_done(
    std::int64_t id,
    std::string* err);

bool mark_circle_federation_outbox_retry(
    std::int64_t id,
    const std::string& last_error,
    int retry_delay_seconds,
    std::string* err);

bool mark_circle_federation_outbox_failed(
    std::int64_t id,
    const std::string& last_error,
    std::string* err);

int recover_stale_circle_federation_outbox_leases(std::string* err);

} // namespace pqnas::federation
