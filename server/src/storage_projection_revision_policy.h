#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

namespace pqnas {

// Security: automatic RT-008 recovery may adopt only a bounded amount of
// newer revision knowledge from Storage. Larger divergence requires explicit
// recovery instead of letting a remote observation arbitrarily ratchet
// Director-authoritative state.
inline constexpr std::uint64_t
    kStorageProjectionMaxAutomaticRepairAdvance =
        1024;

// Automatic repair must never persist a revision that leaves no successor for
// a later legitimate Director-authoritative mutation.
inline bool storage_projection_revision_has_successor(
    std::uint64_t revision
) noexcept {
    return
        revision <
        std::numeric_limits<std::uint64_t>::max();
}

// UnexpectedAuthorization requires a strictly newer tombstone. Reject a Node
// revision whose successor would itself exhaust the Director revision space.
inline bool storage_projection_node_revision_repairable(
    std::uint64_t node_revision
) noexcept {
    const std::uint64_t maximum =
        std::numeric_limits<std::uint64_t>::max();

    return
        node_revision != 0 &&
        node_revision < maximum - 1;
}

// Compute a bounded automatic orphan-repair revision.
// Remote evidence may move Director forward only inside the explicit recovery
// window, and the resulting revision must remain advanceable.
inline bool storage_projection_automatic_repair_target(
    std::uint64_t director_revision,
    std::uint64_t node_revision,
    std::uint64_t* target_revision
) noexcept {
    if (!target_revision ||
        !storage_projection_node_revision_repairable(
            node_revision)) {
        return false;
    }

    const std::uint64_t node_successor =
        node_revision + 1;

    const std::uint64_t candidate =
        std::max(
            director_revision,
            node_successor
        );

    if (!storage_projection_revision_has_successor(
            candidate)) {
        return false;
    }

    if (candidate > director_revision &&
        candidate - director_revision >
            kStorageProjectionMaxAutomaticRepairAdvance) {
        return false;
    }

    *target_revision = candidate;
    return true;
}

// Primitive-level defense: even if a caller omits the reconciliation policy
// check, an automatic floor may not consume arbitrary Director revision space.
inline bool storage_projection_revision_floor_is_automatic_repair_safe(
    std::uint64_t expected_revision,
    std::uint64_t minimum_revision
) noexcept {
    if (minimum_revision == 0 ||
        !storage_projection_revision_has_successor(
            minimum_revision)) {
        return false;
    }

    if (minimum_revision <= expected_revision) {
        return true;
    }

    return
        minimum_revision - expected_revision <=
        kStorageProjectionMaxAutomaticRepairAdvance;
}

} // namespace pqnas
