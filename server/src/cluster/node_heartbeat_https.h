#pragma once

#include "node_heartbeat_worker.h"

namespace pqnas::cluster {

// Production transport for Director-to-node heartbeat requests.
//
// Security:
// - HTTPS only
// - certificate and hostname verification always enabled
// - redirects disabled
// - bounded timeouts and response size
NodeHeartbeatFetch make_https_node_heartbeat_fetch();

} // namespace pqnas::cluster
