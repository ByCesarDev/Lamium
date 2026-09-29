#pragma once
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdint>
#include <optional>
#include <string_view>

namespace lamium::inventory {
// Kind IDs belong to an operation. Group owned stacks using vanilla
// equivalence including components; registry names alone are not keys.
struct RestockSlot {
    int kind = -1;
    int count = 0;
    bool locked = false;
    bool empty() const { return count == 0; }
    bool valid() const { return count >= 0 && (empty() || kind >= 0); }
    bool operator==(RestockSlot const&) const = default;
};
struct RestockSnapshot {
    std::uint64_t context = 0;
    int selected = -1;
    std::array<RestockSlot,36> slots;
    bool operator==(RestockSnapshot const&) const = default;
};
inline constexpr int restockThreshold = 6;
inline bool validRestockSnapshot(RestockSnapshot const& snapshot) {
    if (!snapshot.context || snapshot.selected < 0 || snapshot.selected >= 9) return false;
    for (auto const& slot : snapshot.slots) if (!slot.valid()) return false;
    return true;
}
inline bool restockContextMatches(RestockSnapshot const& a, RestockSnapshot const& b) {
    return validRestockSnapshot(a) && validRestockSnapshot(b)
        && a.context == b.context && a.selected == b.selected;
}
inline bool onlyHandChanged(RestockSnapshot const& before, RestockSnapshot const& after) {
    if (!restockContextMatches(before,after)) return false;
    for (int i = 0; i < 36; ++i)
        if (i != before.selected && before.slots[i] != after.slots[i]) return false;
    return true;
}
// A different item is not permission to exchange it unless it is a known
// consumption remainder. Unknown/add-on transformations fail open.
inline std::string_view restockRemainder(std::string_view item) {
    if (item == "minecraft:water_bucket" || item == "minecraft:lava_bucket"
        || item == "minecraft:milk_bucket" || item == "minecraft:powder_snow_bucket") return "minecraft:bucket";
    if (item == "minecraft:mushroom_stew" || item == "minecraft:rabbit_stew"
        || item == "minecraft:beetroot_soup" || item == "minecraft:suspicious_stew") return "minecraft:bowl";
    if (item == "minecraft:potion" || item == "minecraft:honey_bottle") return "minecraft:glass_bottle";
    return {};
}
struct RestockPlan {
    int source;
    int destination;
    RestockSnapshot beforeMove;
    RestockSlot sourceAfter;
    RestockSlot destinationAfter;
    bool stillValid(RestockSnapshot const& current) const {
        return source >= 0 && source < 36 && destination >= 0 && destination < 9
            && source != destination && destination == beforeMove.selected
            && validRestockSnapshot(current) && beforeMove == current
            && !current.slots[source].locked && !current.slots[destination].locked;
    }
    RestockSnapshot predicted() const {
        auto result = beforeMove;
        result.slots[source] = sourceAfter;
        result.slots[destination] = destinationAfter;
        return result;
    }
};
// One observed consumption, one source, one move. Never invoke from an empty
// slot or a count change alone, without consumption evidence.
inline std::optional<RestockPlan> planRestock(
    RestockSnapshot const& before, RestockSnapshot const& after, bool consumed,
    int maxStack, bool hotbarSources = false, int remainderKind = -1,
    int threshold = restockThreshold
) {
    if (!consumed || !onlyHandChanged(before,after) || maxStack < 1 || maxStack > 255) return {};
    auto const& used = before.slots[before.selected];
    auto const& left = after.slots[after.selected];
    if (used.empty() || used.locked || left.locked || used.count > maxStack) return {};
    bool replacement = !left.empty() && left.kind != used.kind;
    if (replacement) {
        if (remainderKind < 0 || left.kind != remainderKind || used.count != 1 || left.count != 1) return {};
    } else {
        if (left.count != used.count - 1) return {}; // More depletion is ambiguous.
        if (!left.empty() && (left.kind != used.kind
            || left.count > std::clamp(threshold,0,maxStack - 1))) return {};
    }
    auto usable = [&](int slot) {
        auto const& candidate = after.slots[slot];
        return slot != after.selected && !candidate.empty() && !candidate.locked
            && candidate.kind == used.kind && candidate.count <= maxStack;
    };
    // Largest stack first. Main-inventory ties take the higher slot (lower
    // rows, nearest the hotbar), keeping stacks packed from the top intact.
    // Opt-in hotbar reserves come only after the main inventory; their ties
    // take the slot nearest the selection, then the higher slot.
    int source = -1;
    auto distance = [&](int slot) { return slot < 9 ? std::abs(slot - after.selected) : 0; };
    auto pick = [&](int first, int last) {
        for (int slot = first; slot < last; ++slot) {
            if (!usable(slot)) continue;
            if (source < 0 || after.slots[slot].count > after.slots[source].count
                || (after.slots[slot].count == after.slots[source].count && distance(slot) <= distance(source)))
                source = slot;
        }
    };
    pick(9,36);
    if (source < 0 && hotbarSources) pick(0,9);
    if (source < 0) return {};
    auto candidate = after.slots[source];
    if (replacement)
        return RestockPlan{source,after.selected,after,left,candidate};
    int amount = std::min(candidate.count,maxStack - left.count);
    RestockSlot destination{used.kind,left.count + amount,false};
    candidate.count -= amount;
    if (candidate.empty()) candidate = {};
    return RestockPlan{source,after.selected,after,candidate,destination};
}
}
