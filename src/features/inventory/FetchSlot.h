#pragma once
#include <optional>

namespace lamium::inventory {
// L-97: the hotbar slot a fetched tool or weapon goes to. setting 0 is the
// selected slot (the original behavior); 1-9 is a fixed hotbar slot, so the
// rest of the hotbar keeps its layout. A fixed slot that is Fake Offhand's
// target (while Fake Offhand is on) falls back to the selected slot, so its
// blocks are not pushed out. The slot is then selected and stays selected.
inline int fetchSlot(int selected, int setting, std::optional<int> fakeOffhandSlot) {
    if (setting < 1 || setting > 9) return selected;
    int fixed = setting - 1;
    if (fakeOffhandSlot && *fakeOffhandSlot == fixed) return selected;
    return fixed;
}
}
