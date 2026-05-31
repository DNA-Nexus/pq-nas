#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pqnas::federation {

struct CircleFederationRemoteFeedEvent {
    std::int64_t id = 0;
    std::int64_t received_epoch = 0;
    std::int64_t created_epoch = 0;

    std::string circle_id;
    std::string event_id;
    std::string event_type;
    std::string origin_nas;

    std::string target_type;
    std::int64_t post_id = 0;
    std::int64_t reply_id = 0;
    std::string actor_fp;
    std::string reaction;

    std::string event_json;
};

struct CircleFederationRemoteFeedStats {
    std::int64_t total = 0;
    std::int64_t posts = 0;
    std::int64_t replies = 0;
    std::int64_t reaction_created = 0;
    std::int64_t reaction_removed = 0;
};

bool ensure_circle_federation_remote_feed(std::string* err);

bool store_circle_federation_remote_feed_event(
    const std::string& circle_id,
    const std::string& event_id,
    const std::string& event_type,
    const std::string& origin_nas,
    std::int64_t created_epoch,
    const std::string& target_type,
    std::int64_t post_id,
    std::int64_t reply_id,
    const std::string& actor_fp,
    const std::string& reaction,
    const std::string& event_json,
    std::string* err);

std::vector<CircleFederationRemoteFeedEvent> list_circle_federation_remote_feed(
    int limit,
    std::int64_t before_id,
    std::string* err);

// Backward-compatible helper for older call sites that just want the latest rows.
std::vector<CircleFederationRemoteFeedEvent> list_circle_federation_remote_feed(
    int limit,
    std::string* err);

CircleFederationRemoteFeedStats circle_federation_remote_feed_stats(std::string* err);

bool prune_circle_federation_remote_feed(
    int max_rows,
    std::string* err);

} // namespace pqnas::federation
