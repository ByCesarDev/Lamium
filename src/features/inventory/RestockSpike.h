#pragma once
#include "features/inventory/RestockPlan.h"

namespace lamium::inventory {
// L-66 bounded research spike for an inventory-only reserve (no hotbar
// reserve, so HotbarSelectPlan is empty and RestockPlan found a main-
// inventory source). Path A is the vanilla HUD handlePlaceAmount call;
// path B is one client-built Swap through the vanilla request scope.
// At most one attempt per path per depletion, A first; B runs only when A
// moved nothing. No retries: any mismatch, correction, timeout or unrelated
// change ends the operation and fails open to vanilla.
enum class SpikeStage { Fresh, TriedA, Done };

// The spike only runs when depletion left no hotbar reserve behind.
inline bool spikeEligible(
    std::optional<HotbarSelectPlan> const& hotbar, std::optional<RestockPlan> const& plan
) {
    return !hotbar && plan.has_value();
}

// Exactly one whole-stack move happened: the destination now holds the
// expected source stack, the source is empty, everything else is unchanged.
inline bool spikeMoveApplied(
    RestockSnapshot const& beforeMove, RestockSnapshot const& afterMove, RestockPlan const& plan
) {
    if (!plan.stillValid(beforeMove)) return false;
    if (afterMove.context != beforeMove.context || afterMove.selected != beforeMove.selected) return false;
    for (auto const& slot : afterMove.slots) if (!slot.valid()) return false;
    if (!afterMove.slots[plan.source].empty()) return false;
    if (!(afterMove.slots[plan.destination] == plan.expectedSource)) return false;
    for (int slot = 0; slot < 36; ++slot) {
        if (slot == plan.source || slot == plan.destination) continue;
        if (beforeMove.slots[slot] != afterMove.slots[slot]) return false;
    }
    return true;
}

// A failed attempt left the inventory untouched, so the other path is safe.
inline bool spikeUnchanged(RestockSnapshot const& beforeMove, RestockSnapshot const& afterMove) {
    if (!beforeMove.context || beforeMove.context != afterMove.context) return false;
    if (beforeMove.selected != afterMove.selected) return false;
    for (int slot = 0; slot < 36; ++slot)
        if (beforeMove.slots[slot] != afterMove.slots[slot]) return false;
    return true;
}

inline SpikeStage spikeAfterA(bool moved) { return moved ? SpikeStage::Done : SpikeStage::TriedA; }
}
