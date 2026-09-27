#include "features/inventory/transfer/TransferGesture.h"
#include <array>

extern void check(bool, const char*);

void transferGestureTests() {
    using namespace lamium::inventory::transfer;
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
}
