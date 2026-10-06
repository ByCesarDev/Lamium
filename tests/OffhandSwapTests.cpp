#include "features/inventory/OffhandSwapPlan.h"
void check(bool, char const*);
void offhandSwapTests() {
    using namespace lamium::inventory;
    OffhandSwapState shield{.selected = 0, .handEmpty = false, .handFitsOffhand = true};
    check(planOffhandSwap(shield) == OffhandSwap::RealOffhand, "an item the offhand accepts goes to the real offhand");
    shield.offhandEmpty = false;
    check(planOffhandSwap(shield) == OffhandSwap::RealOffhand, "it swaps with what the offhand already holds");
    OffhandSwapState block{.selected = 0, .handEmpty = false, .handFitsOffhand = false, .fakeSlot = 8};
    check(planOffhandSwap(block) == OffhandSwap::FakeSlot, "other items go to Fake Offhand's target slot");
    block.fakeSlot.reset();
    check(planOffhandSwap(block) == OffhandSwap::None, "with Fake Offhand off they stay put");
    block.fakeSlot = 0;
    check(planOffhandSwap(block) == OffhandSwap::None, "the target slot itself has nothing to swap with");
    OffhandSwapState empty{.selected = 2, .handEmpty = true, .offhandEmpty = false, .fakeSlot = 8, .fakeSlotEmpty = false};
    check(planOffhandSwap(empty) == OffhandSwap::RealOffhand, "an empty hand takes the real offhand's item back first");
    empty.offhandEmpty = true;
    check(planOffhandSwap(empty) == OffhandSwap::FakeSlot, "then the target slot's item");
    empty.fakeSlotEmpty = true;
    check(planOffhandSwap(empty) == OffhandSwap::None, "nothing anywhere: nothing happens");
}
