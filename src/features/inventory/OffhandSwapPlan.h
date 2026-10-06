#pragma once
#include <optional>

namespace lamium::inventory {
// L-94: one key swaps the held item with the "second hand". An item the real
// offhand accepts goes to the real offhand; any other item goes to Fake
// Offhand's target hotbar slot while Fake Offhand is on. An empty hand takes
// the real offhand's item back first, then the target slot's. Nothing to do
// is silent (decided with the maintainer 2026-10-06).
enum class OffhandSwap { None, RealOffhand, FakeSlot };
struct OffhandSwapState {
    int selected = 0;               // Hotbar slot 0-8.
    bool handEmpty = true;
    bool handFitsOffhand = false;   // The real offhand accepts the held item.
    bool handPrefersFake = false;   // Fake Offhand can use it (fireworks, when chosen).
    bool offhandEmpty = true;
    std::optional<int> fakeSlot;    // Fake Offhand's target slot 0-8 while it is on.
    bool fakeSlotEmpty = true;
};
inline OffhandSwap planOffhandSwap(OffhandSwapState const& state) {
    bool fake = state.fakeSlot && *state.fakeSlot != state.selected;
    if (!state.handEmpty) {
        if (fake && state.handPrefersFake) return OffhandSwap::FakeSlot;
        if (state.handFitsOffhand) return OffhandSwap::RealOffhand;
        return fake ? OffhandSwap::FakeSlot : OffhandSwap::None;
    }
    if (!state.offhandEmpty) return OffhandSwap::RealOffhand;
    if (fake && !state.fakeSlotEmpty) return OffhandSwap::FakeSlot;
    return OffhandSwap::None;
}
}
