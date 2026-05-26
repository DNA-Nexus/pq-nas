#pragma once

#include <cstddef>

namespace pqnas::federation {

// Hard cap for a single federation event JSON envelope accepted from Nodus.
// Keeps malicious DHT values from causing large JSON parse allocations or
// unbounded SQLite growth.
inline constexpr std::size_t kMaxCircleFederationEventJsonBytes = 64 * 1024;

// Hard caps for federation SQLite tables populated from Nodus.
// These prevent a poisoned DHT/recent:index from filling disk indefinitely.
inline constexpr int kMaxCircleFederationInboxRows = 10000;
inline constexpr int kMaxCircleFederationRemoteFeedRows = 10000;

} // namespace pqnas::federation
