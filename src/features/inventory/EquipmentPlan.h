#pragma once
#include <cstdlib>
#include <optional>
#include <vector>

namespace lamium::inventory {
// L-62: a held item (or worn elytra) with this much durability left or less is
// about to break; the elytra stops working there instead of breaking.
inline constexpr int guardRemaining = 1;
inline bool aboutToBreak(int maxDamage, int damage) {
    return maxDamage > 0 && maxDamage - damage <= guardRemaining;
}
struct EnchantLevel {
    int type = 0, level = 0;
};
// How far two enchantment sets are apart: level differences of shared kinds
// plus the full level of kinds only one side has.
inline int enchantDistance(std::vector<EnchantLevel> const& a, std::vector<EnchantLevel> const& b) {
    int distance = 0;
    for (auto const& x : a) {
        int other = 0;
        for (auto const& y : b) if (y.type == x.type) other = y.level;
        distance += std::abs(x.level - other);
    }
    for (auto const& y : b) {
        bool shared = false;
        for (auto const& x : a) shared = shared || x.type == y.type;
        if (!shared) distance += y.level;
    }
    return distance;
}
struct ReplacementCandidate {
    int slot = -1;
    int enchantDistance = 0;
    int remaining = 0;
};
// Among the same item: closest enchantments, then most durability left, then
// the higher slot (lower rows). Never another item that is about to break.
inline std::optional<int> chooseReplacement(std::vector<ReplacementCandidate> const& candidates) {
    std::optional<ReplacementCandidate> best;
    for (auto const& c : candidates) {
        if (c.slot < 0 || c.remaining <= guardRemaining) continue;
        if (!best || c.enchantDistance < best->enchantDistance
            || (c.enchantDistance == best->enchantDistance && (c.remaining > best->remaining
                || (c.remaining == best->remaining && c.slot > best->slot))))
            best = c;
    }
    return best ? std::optional<int>{best->slot} : std::nullopt;
}
enum class GuardMining { Continue, Wait, Stop };
// Mining with a held tool about to break: wait for the swap when a replacement
// exists, otherwise stop. A new press after a stop mines on (overridden).
inline GuardMining guardMining(bool breaking, bool replacement, bool overridden) {
    if (!breaking || overridden) return GuardMining::Continue;
    return replacement ? GuardMining::Wait : GuardMining::Stop;
}

// L-70: put on an elytra when gliding starts, take it off after landing.
struct ElytraSwapState {
    std::optional<int> returnSlot; // Inventory slot now holding the chest item.
    int groundTicks = 0;
};
enum class ElytraStep { None, PutOn, TakeOff, Forget };
struct ElytraInput {
    bool enabled = false;
    bool gliding = false;
    bool onGround = false;
    bool wearingElytra = false;
    bool returnSlotHoldsChest = false; // The remembered slot still holds what was worn (or is empty).
    bool elytraAvailable = false;      // A usable elytra in the main inventory.
    bool glideAttempt = false;
};
// Landing is confirmed on the second grounded tick so a bounce does not undo it.
inline constexpr int elytraLandingTicks = 2;
inline ElytraStep elytraStep(ElytraSwapState& state, ElytraInput const& in) {
    if (in.glideAttempt) {
        if (!in.enabled || in.wearingElytra || !in.elytraAvailable || in.gliding) return ElytraStep::None;
        return ElytraStep::PutOn;
    }
    if (!state.returnSlot) return ElytraStep::None;
    if (!in.enabled || !in.wearingElytra || !in.returnSlotHoldsChest) return ElytraStep::Forget;
    if (!in.onGround || in.gliding) { state.groundTicks = 0; return ElytraStep::None; }
    return ++state.groundTicks >= elytraLandingTicks ? ElytraStep::TakeOff : ElytraStep::None;
}
}
