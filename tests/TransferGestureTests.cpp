#include "features/inventory/transfer/TransferGesture.h"

extern void check(bool, const char*);

void transferGestureTests() {
    using namespace lamium::inventory::transfer;
    check(dragGesture(1, true, false) == Gesture::StackDrag, "Shift left drag transfers stacks");
    check(dragGesture(1, false, true) == Gesture::OneDrag
        && dragGesture(2, false, true) == Gesture::OneDrag,
        "Ctrl drag transfers one from left or right button");
    check(dragGesture(1, true, true) == Gesture::OneDrag
        && dragGesture(2, true, false) == Gesture::None,
        "Ctrl takes priority and Shift right drag stays vanilla");
    check(wheelSource(1, Side::Player) && wheelSource(-1, Side::Storage)
        && !wheelSource(1, Side::Storage) && !wheelSource(-1, Side::Player),
        "wheel direction chooses the source side");
    check(amount(wheelGesture(false), 64) == 1 && amount(wheelGesture(true), 64) == 64
        && amount(Gesture::StackDrag, 12) == 12 && amount(Gesture::OneDrag, 12) == 1,
        "gesture chooses one item or the current stack");
}
