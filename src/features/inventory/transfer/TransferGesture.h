#pragma once
#include <span>

namespace lamium::inventory::transfer {
enum class Gesture { None, StackDrag, OneDrag, OneWheel, StackWheel };
enum class Side { Player, Storage };

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

inline int amount(Gesture gesture, int count) {
    return gesture == Gesture::OneDrag || gesture == Gesture::OneWheel ? 1 : count;
}
}
