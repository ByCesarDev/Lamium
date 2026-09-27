#pragma once

namespace lamium::inventory::transfer {
enum class Gesture { None, StackDrag, OneDrag, OneWheel, StackWheel };
enum class Side { Player, Storage };

inline Gesture dragGesture(int button, bool shift, bool control) {
    if (control && (button == 1 || button == 2)) return Gesture::OneDrag;
    if (shift && button == 1) return Gesture::StackDrag;
    return Gesture::None;
}

inline Gesture wheelGesture(bool shift) { return shift ? Gesture::StackWheel : Gesture::OneWheel; }

inline bool wheelSource(int direction, Side side) {
    return (direction > 0 && side == Side::Player) || (direction < 0 && side == Side::Storage);
}

inline int amount(Gesture gesture, int count) {
    return gesture == Gesture::OneDrag || gesture == Gesture::OneWheel ? 1 : count;
}
}
