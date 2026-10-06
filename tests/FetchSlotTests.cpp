#include "features/inventory/FetchSlot.h"
void check(bool, char const*);
void fetchSlotTests() {
    using lamium::inventory::fetchSlot;
    check(fetchSlot(3, 0, std::nullopt) == 3, "by default a fetch goes to the selected slot");
    check(fetchSlot(3, 1, std::nullopt) == 0 && fetchSlot(3, 9, std::nullopt) == 8, "a fixed slot 1-9 is hotbar index 0-8");
    check(fetchSlot(3, 4, std::nullopt) == 3, "a fixed slot that is selected stays selected");
    check(fetchSlot(3, 9, 8) == 3, "Fake Offhand's slot is never a fetch destination; the selected slot is used");
    check(fetchSlot(3, 9, 7) == 8, "another Fake Offhand slot does not matter");
    check(fetchSlot(3, 12, std::nullopt) == 3 && fetchSlot(3, -1, std::nullopt) == 3, "an invalid setting means the selected slot");
}
