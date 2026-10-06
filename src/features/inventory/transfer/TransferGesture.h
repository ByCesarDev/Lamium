#pragma once
#include <optional>
#include <span>
#include <string_view>

namespace lamium::inventory::transfer {
enum class Gesture { None, StackDrag, OneDrag, OneWheel, StackWheel };
// Storage is the upper grid and Player the lower one. The inventory screen
// has no storage: there the main inventory is the upper side and the hotbar
// the lower, so the wheel keeps its direction (2026-10-07).
enum class Side { Player, Storage };
struct GestureOptions { bool wheelOne, wheelStack, dragStack, dragOne; };

inline bool enabled(Gesture gesture, GestureOptions options) {
    switch (gesture) {
    case Gesture::OneWheel: return options.wheelOne;
    case Gesture::StackWheel: return options.wheelStack;
    case Gesture::StackDrag: return options.dragStack;
    case Gesture::OneDrag: return options.dragOne;
    default: return false;
    }
}

inline Gesture dragGesture(int button, bool shift, bool control) {
    if (control && button == 1) return Gesture::OneDrag;
    if (shift && button == 1) return Gesture::StackDrag;
    return Gesture::None;
}

inline Gesture wheelGesture(bool shift) { return shift ? Gesture::StackWheel : Gesture::OneWheel; }

inline Side wheelDestination(int direction) { return direction > 0 ? Side::Storage : Side::Player; }
inline Side otherSide(Side side) { return side == Side::Player ? Side::Storage : Side::Player; }
inline bool canReceive(int count, int maxStackSize) { return count < maxStackSize; }

// The pointer's source stack is a fallback, even if its slot index is high.
inline int chooseSource(std::span<unsigned char const> matches, int hoveredIndex = -1) {
    for (int i = static_cast<int>(matches.size()) - 1; i >= 0; --i)
        if (i != hoveredIndex && matches[static_cast<size_t>(i)]) return i;
    return hoveredIndex >= 0 && hoveredIndex < static_cast<int>(matches.size())
        && matches[static_cast<size_t>(hoveredIndex)] ? hoveredIndex : -1;
}

// A 36-slot inventory collection includes the hotbar at 0-8.
inline std::optional<Side> collectionSide(std::string_view name, int index, int inventorySize, bool inventoryScreen) {
    if (inventoryScreen) {
        if (name == "hotbar_items") return Side::Player;
        if (name == "inventory_items") return inventorySize >= 36 && index < 9 ? Side::Player : Side::Storage;
        return {};
    }
    if (name == "inventory_items" || name == "hotbar_items") return Side::Player;
    if (name == "container_items" || name == "barrel_items" || name == "shulker_box_items") return Side::Storage;
    return {};
}

// Where an inventory-screen move lands: the first matching stack with room,
// else the first empty slot. Vanilla auto-place there would equip armor.
struct Destination { bool empty = true, matches = false; int count = 0, maxStack = 64; };
inline int chooseDestination(std::span<Destination const> slots) {
    for (size_t i = 0; i < slots.size(); ++i)
        if (!slots[i].empty && slots[i].matches && slots[i].count < slots[i].maxStack) return static_cast<int>(i);
    for (size_t i = 0; i < slots.size(); ++i)
        if (slots[i].empty) return static_cast<int>(i);
    return -1;
}
inline int room(Destination const& slot) { return slot.empty ? slot.maxStack : slot.maxStack - slot.count; }

// In the inventory screen Shift + left on something worn (armor, elytra,
// heads, carved pumpkins) stays vanilla's quick move, which equips it; the
// transfer drag also passes over it (maintainer, 2026-10-07).
inline bool vanillaShift(Gesture gesture, bool inventoryScreen, bool worn) {
    return gesture == Gesture::StackDrag && inventoryScreen && worn;
}
inline bool wornItem(std::string_view name, bool armor) {
    if (armor) return true;
    constexpr std::string_view prefix = "minecraft:";
    if (!name.starts_with(prefix)) return false;
    name.remove_prefix(prefix.size());
    return name == "elytra" || name == "carved_pumpkin" || name == "turtle_helmet" || name == "skull"
        || name.ends_with("_head") || name.ends_with("_skull");
}

inline int amount(Gesture gesture, int count) {
    return gesture == Gesture::OneDrag || gesture == Gesture::OneWheel ? 1 : count;
}
}
