#pragma once

namespace pqnas::federation {

// Starts the optional Circle Stack federation outbox worker once.
// The worker is disabled unless PQNAS_CIRCLE_FEDERATION_WORKER=1.
void start_circle_federation_outbox_worker_once();

} // namespace pqnas::federation
