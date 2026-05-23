#pragma once

#include "circle_stack_routes.h"
#include "httplib.h"

#include <nlohmann/json.hpp>

#include <string>

namespace pqnas {

void register_circle_stack_memory_node_routes(
    httplib::Server& server,
    const CircleStackRoutesDeps& deps
);

void circle_stack_memory_nodes_annotate_feed_posts(
    nlohmann::json& posts,
    const std::string& viewer_fp,
    const CircleStackRoutesDeps& deps
);

nlohmann::json circle_stack_memory_nodes_admin_stats();

} // namespace pqnas
