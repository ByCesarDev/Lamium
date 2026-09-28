#pragma once
#include "features/inventory/RestockPlan.h"

namespace lamium::inventory {
// L-66 bounded research spike for an inventory-only reserve (no hotbar
// reserve, so the hotbar-select plan is empty and the restock plan found a
// main-inventory source). One client-built NormalTransaction is sent through
// the vanilla LocalPlayer send path; success is declared only from the
// authoritative inventory snapshot afterwards. No retries: any mismatch,
// correction, timeout or unrelated change ends the operation.
enum class SpikeStage { Fresh, Sent, Done };

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

// A failed or not-yet-applied attempt left the inventory untouched, so the
// operation can still be waiting for a delayed authoritative update.
inline bool spikeUnchanged(RestockSnapshot const& beforeMove, RestockSnapshot const& afterMove) {
    return beforeMove.context == afterMove.context && beforeMove.selected == afterMove.selected
        && beforeMove.slots == afterMove.slots;
}

// An intermediate state still consistent with the one planned whole-stack
// move: only the two planned slots differ from the attempt baseline, the
// source may only have emptied and the destination may only have received the
// exact expected stack. Server updates can arrive slot by slot.
inline bool spikeProgressing(
    RestockSnapshot const& beforeMove, RestockSnapshot const& afterMove, RestockPlan const& plan
) {
    if (afterMove.context != beforeMove.context || afterMove.selected != beforeMove.selected) return false;
    for (int slot = 0; slot < 36; ++slot) {
        if (slot == plan.source || slot == plan.destination) continue;
        if (beforeMove.slots[slot] != afterMove.slots[slot]) return false;
    }
    auto const& source = afterMove.slots[plan.source];
    auto const& destination = afterMove.slots[plan.destination];
    if (!source.valid() || !destination.valid() || source.locked || destination.locked) return false;
    bool sourceIntact = source == plan.expectedSource;
    bool sourceEmpty = source.empty();
    bool destinationEmpty = destination.empty();
    bool destinationFull = destination == plan.expectedSource;
    if (!sourceIntact && !sourceEmpty) return false;
    if (!destinationEmpty && !destinationFull) return false;
    // Never accept a duplicated or partially transferred stack.
    if (sourceEmpty && destinationEmpty) return !beforeMove.slots[plan.source].empty();
    return sourceEmpty != destinationEmpty;
}
}
