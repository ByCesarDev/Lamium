#include "features/inventory/transfer/TransferGesture.h"
#include <array>

extern void check(bool, const char*);

void transferGestureTests() {
    using namespace lamium::inventory::transfer;
    check(enabled(Gesture::OneWheel, {true, true, true, true})
        && enabled(Gesture::StackWheel, {true, true, true, true})
        && enabled(Gesture::StackDrag, {true, true, true, true})
        && enabled(Gesture::OneDrag, {true, true, true, true}),
        "all transfer gestures can be enabled independently");
    check(!enabled(Gesture::OneWheel, {false, true, true, true})
        && !enabled(Gesture::StackWheel, {true, false, true, true})
        && !enabled(Gesture::StackDrag, {true, true, false, true})
        && !enabled(Gesture::OneDrag, {true, true, true, false})
        && !enabled(Gesture::None, {true, true, true, true}),
        "disabled gestures leave their input to vanilla");
    check(dragGesture(1, true, false) == Gesture::StackDrag, "Shift left drag transfers stacks");
    check(dragGesture(1, false, true) == Gesture::OneDrag
        && dragGesture(2, false, true) == Gesture::None,
        "Ctrl left drag transfers one while Ctrl right drag stays vanilla");
    check(dragGesture(1, true, true) == Gesture::OneDrag
        && dragGesture(2, true, false) == Gesture::None,
        "Ctrl takes priority and Shift right drag stays vanilla");
    check(wheelDestination(1) == Side::Storage && wheelDestination(-1) == Side::Player
        && otherSide(Side::Storage) == Side::Player,
        "wheel direction chooses the destination regardless of hover side");
    check(canReceive(63, 64) && !canReceive(64, 64) && !canReceive(1, 1),
        "ordinary wheel cannot bring an item into a full hovered stack");
    constexpr std::array<unsigned char, 6> matches{1, 1, 0, 1, 0, 1};
    check(chooseSource(matches, 5) == 3 && chooseSource(matches, 3) == 5
        && chooseSource(std::span<unsigned char const>{matches.data(), 1}, 0) == 0,
        "shift wheel scans high slots first and uses hovered source last");
    check(amount(wheelGesture(false), 64) == 1 && amount(wheelGesture(true), 64) == 64
        && amount(Gesture::StackDrag, 12) == 12 && amount(Gesture::OneDrag, 12) == 1,
        "gesture chooses one item or the current stack");
    check(collectionSide("container_items", 0, 36, false) == Side::Storage
        && collectionSide("inventory_items", 20, 36, false) == Side::Player
        && collectionSide("hotbar_items", 3, 36, false) == Side::Player,
        "storage screens keep storage above and the whole player inventory below");
    check(collectionSide("inventory_items", 20, 36, true) == Side::Storage
        && collectionSide("inventory_items", 4, 36, true) == Side::Player
        && collectionSide("hotbar_items", 4, 36, true) == Side::Player
        && collectionSide("inventory_items", 4, 27, true) == Side::Storage,
        "the inventory screen puts the main inventory above and the hotbar below");
    check(!collectionSide("armor_items", 0, 36, true) && !collectionSide("container_items", 0, 36, true)
        && !collectionSide("crafting_input_items", 0, 36, false),
        "armor, crafting and unknown grids never take part");
    using D = Destination;
    std::array<D, 4> slots{D{false, false, 10, 64}, D{true}, D{false, true, 64, 64}, D{false, true, 30, 64}};
    check(chooseDestination(slots) == 3, "a matching stack with room is filled before an empty slot");
    slots[3].count = 64;
    check(chooseDestination(slots) == 1, "full matching stacks fall back to the first empty slot");
    slots[1] = D{false, false, 1, 64};
    check(chooseDestination(slots) == -1, "no room leaves the item where it is");
    check(vanillaShift(Gesture::StackDrag, true, true) && !vanillaShift(Gesture::StackDrag, false, true)
        && !vanillaShift(Gesture::OneDrag, true, true) && !vanillaShift(Gesture::StackDrag, true, false),
        "only Shift on worn items in the inventory screen stays vanilla");
    check(wornItem("minecraft:diamond_chestplate", true) && wornItem("minecraft:elytra", false)
        && wornItem("minecraft:zombie_head", false) && wornItem("minecraft:carved_pumpkin", false)
        && !wornItem("minecraft:pumpkin", false) && !wornItem("minecraft:stick", false)
        && !wornItem("custom:elytra", false),
        "worn items are armor, elytra, heads and carved pumpkins");
    check(room(D{true, false, 0, 16}) == 16 && room(D{false, true, 10, 16}) == 6,
        "room follows the item's own stack limit");
}
