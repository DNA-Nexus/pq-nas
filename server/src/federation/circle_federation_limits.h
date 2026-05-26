#pragma once

#include <cstddef>

namespace pqnas::federation {

// Hard cap for a single federation event JSON envelope accepted from Nodus.
// Keeps malicious DHT values from causing large JSON parse allocations or
// unbounded SQLite growth.
inline constexpr std::size_t kMaxCircleFederationEventJsonBytes = 64 * 1024;

} // namespace pqnas::federation
