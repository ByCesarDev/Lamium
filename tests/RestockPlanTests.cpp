#include "features/inventory/RestockPlan.h"
#include "features/inventory/RestockSpike.h"
#include <stdexcept>

void restockPlanTests() {
    using namespace lamium::inventory;
    auto check = [](bool pass) { if (!pass) throw std::runtime_error("restock planning invariant"); };
    RestockSnapshot before;
    before.context = 7;
    before.selected = 3;
    before.slots[3] = {2,1};
    before.slots[0] = {2,64}; // Other hotbar slots stay untouched.
    before.slots[9] = {1,64}; // Different components/kind.
    before.slots[10] = {2,32,true};
    before.slots[11] = {2,16};
    before.slots[12] = {2,64};
    auto after = before;
    after.slots[3] = {};
    auto plan = planRestock(before,after,true);
    check(plan && plan->source == 11 && plan->destination == 3 && plan->stillValid(after));
    check(!planRestock(before,after,false));
    auto changed = after;
    changed.context++;
    check(!planRestock(before,changed,true) && !plan->stillValid(changed));
    changed = after;
    changed.selected = 4;
    check(!planRestock(before,changed,true) && !plan->stillValid(changed));
    changed = after;
    changed.slots[3] = {3,1}; // Bowl, bucket, or a manually placed stack.
    check(!planRestock(before,changed,true) && !plan->stillValid(changed));
    changed = after;
    changed.slots[11].count--;
    check(!plan->stillValid(changed));
    check(!planRestock(before,changed,true)); // No alternate after interference.
    changed.slots[12] = {};
    check(!planRestock(before,changed,true));
    changed = after;
    changed.slots[0].count--;
    check(!planRestock(before,changed,true)); // Other hotbar mutation cancels.
    changed = after;
    changed.slots[20] = {4,1};
    check(!planRestock(before,changed,true)); // Pickup/manual move cancels.
    changed = after;
    changed.slots[3].locked = true;
    check(!planRestock(before,changed,true) && !plan->stillValid(changed));
    before.slots[3].count = 2;
    check(!planRestock(before,after,true));
    before.slots[3] = {};
    check(!planRestock(before,after,true));
    before.selected = after.selected = -1;
    check(!planRestock(before,after,true));
    before.selected = after.selected = 36;
    check(!planRestock(before,after,true));
}

void restockHotbarSelectTests() {
    using namespace lamium::inventory;
    auto check = [](bool pass) { if (!pass) throw std::runtime_error("restock hotbar-select invariant"); };
    RestockSnapshot before;
    before.context = 7;
    before.selected = 3;
    before.slots[3] = {2,1};
    before.slots[0] = {2,64}; // First compatible hotbar reserve wins.
    before.slots[5] = {2,16};
    before.slots[9] = {1,64}; // Different components/kind.
    before.slots[11] = {2,16}; // Main-inventory reserve loses to hotbar.
    auto after = before;
    after.slots[3] = {};
    auto plan = planHotbarSelect(before,after,true);
    check(plan && plan->source == 0 && plan->depleted == 3 && plan->stillValid(after));
    check(!planHotbarSelect(before,after,false));
    auto changed = after;
    changed.context++;
    check(!planHotbarSelect(before,changed,true) && !plan->stillValid(changed));
    changed = after;
    changed.selected = 4;
    check(!planHotbarSelect(before,changed,true) && !plan->stillValid(changed));
    changed = after;
    changed.slots[3] = {3,1}; // Bowl, bucket, or a manually placed stack.
    check(!planHotbarSelect(before,changed,true) && !plan->stillValid(changed));
    changed = after;
    changed.slots[0].count--;
    check(!plan->stillValid(changed));
    check(!planHotbarSelect(before,changed,true)); // No alternate after interference.
    changed = after;
    changed.slots[5] = {};
    check(!planHotbarSelect(before,changed,true));
    changed = after;
    changed.slots[20] = {4,1};
    check(!planHotbarSelect(before,changed,true)); // Pickup/manual move cancels.
    changed = after;
    changed.slots[0].locked = true;
    check(!planHotbarSelect(before,changed,true));
    // Hotbar without a compatible reserve: no plan even with one in inventory.
    auto bare = before;
    bare.slots[0] = {};
    bare.slots[5] = {};
    auto bareAfter = bare;
    bareAfter.slots[3] = {};
    check(!planHotbarSelect(bare,bareAfter,true));
    check(planRestock(bare,bareAfter,true).has_value()); // Main-inventory path still detects.
    before.slots[3].count = 2;
    check(!planHotbarSelect(before,after,true));
    before.slots[3] = {};
    check(!planHotbarSelect(before,after,true));
}

void restockPartialTests() {
    using namespace lamium::inventory;
    auto check = [](bool pass) { if (!pass) throw std::runtime_error("partial restock invariant"); };
    constexpr int maxStack = 64;
    constexpr int threshold = 8;
    RestockSnapshot before;
    before.context = 9;
    before.selected = 3;
    before.slots[3] = {2,7};   // held stack of 7, one is about to be used
    before.slots[11] = {2,32};
    auto after = before;
    after.slots[3].count = 6;  // one consumed, still above zero
    auto plan = planPartialRestock(before,after,true,threshold,maxStack);
    check(plan && plan->source == 11 && plan->destination == 3 && plan->move == 32);
    check(plan->stillValid(after));
    // Destination 6 / source 64: move 58, destination reaches max, source keeps 6.
    RestockSnapshot big = before;
    big.slots[11].count = 64;
    auto bigAfter = big;
    bigAfter.slots[3].count = 6;
    auto plan2 = planPartialRestock(big,bigAfter,true,threshold,maxStack);
    check(plan2 && plan2->move == 58 && plan2->expectedSource.count == 64);
    check(plan2->stillValid(bigAfter));
    auto moved = after;
    moved.slots[3].count = 38;
    moved.slots[11] = {};
    check(partialMoveApplied(after,moved,*plan));
    auto moved2 = bigAfter;
    moved2.slots[3].count = 64;
    moved2.slots[11].count = 6;
    check(partialMoveApplied(bigAfter,moved2,*plan2));
    auto shortMove = after;
    shortMove.slots[3].count = 20; // not the planned amount
    shortMove.slots[11].count = 18;
    check(!partialMoveApplied(after,shortMove,*plan));
    auto noisy = moved;
    noisy.slots[20] = {4,1};
    check(!partialMoveApplied(after,noisy,*plan));
    auto stale = moved;
    stale.context++;
    check(!partialMoveApplied(after,stale,*plan));
    check(!planPartialRestock(before,after,false,threshold,maxStack)); // use failed
    check(!planPartialRestock(before,before,true,threshold,maxStack)); // no decrease
    auto replacement = after;
    replacement.slots[3] = {5,6}; // different kind (bowl/bottle)
    check(!planPartialRestock(before,replacement,true,threshold,maxStack));
    auto above = after;
    above.slots[3].count = 9; // still above the threshold
    check(!planPartialRestock(before,above,true,threshold,maxStack));
    auto depleted = before;
    depleted.slots[3].count = 0; // whole-stack depletion is the other plan
    check(!planPartialRestock(before,depleted,true,threshold,maxStack));
    auto manual = after;
    manual.slots[20] = {4,1}; // unrelated mutation
    check(!planPartialRestock(before,manual,true,threshold,maxStack));
    auto locked = after;
    locked.slots[11].locked = true;
    check(!planPartialRestock(before,locked,true,threshold,maxStack));
    auto sourceChanged = after;
    sourceChanged.slots[11].count = 31; // source must be unchanged by the use
    check(!planPartialRestock(before,sourceChanged,true,threshold,maxStack));
    auto noSource = after;
    noSource.slots[11] = {};
    check(!planPartialRestock(before,noSource,true,threshold,maxStack));
    auto noRoom = after;
    noRoom.slots[3].count = maxStack;
    check(!planPartialRestock(before,noRoom,true,threshold,maxStack));
}

void restockSpikeTests() {
    using namespace lamium::inventory;
    auto check = [](bool pass) { if (!pass) throw std::runtime_error("restock spike invariant"); };
    RestockSnapshot depleted;
    depleted.context = 7;
    depleted.selected = 3;
    depleted.slots[11] = {2,16};
    RestockPlan plan{11,3,{2,16},7};
    check(spikeEligible(std::nullopt,std::optional<RestockPlan>{plan}));
    HotbarSelectPlan hotbar{0,3,{2,64},7};
    check(!spikeEligible(hotbar,std::optional<RestockPlan>{plan}));
    check(!spikeEligible(std::nullopt,std::nullopt));
    auto moved = depleted;
    moved.slots[3] = {2,16};
    moved.slots[11] = {};
    check(spikeMoveApplied(depleted,moved,plan));
    check(spikeUnchanged(depleted,depleted));
    check(!spikeUnchanged(depleted,moved));
    check(!spikeMoveApplied(depleted,depleted,plan)); // Nothing moved yet.
    auto partial = moved;
    partial.slots[11] = {2,1}; // Source left behind: not a whole-stack move.
    check(!spikeMoveApplied(depleted,partial,plan));
    auto wrong = depleted;
    wrong.slots[3] = {2,15};
    wrong.slots[11] = {};
    check(!spikeMoveApplied(depleted,wrong,plan));
    auto noisy = moved;
    noisy.slots[20] = {4,1}; // Unrelated mutation breaks correlation.
    check(!spikeMoveApplied(depleted,noisy,plan));
    check(!spikeProgressing(depleted,noisy,plan));
    auto stale = moved;
    stale.context++;
    check(!spikeMoveApplied(depleted,stale,plan));
    check(!spikeUnchanged(depleted,stale));
    check(!spikeProgressing(depleted,stale,plan));
    auto occupied = depleted;
    occupied.slots[3] = {5,1}; // Destination was not empty at the attempt.
    check(!spikeMoveApplied(occupied,moved,plan));
    check(!plan.stillValid(occupied));
    // Slot-by-slot authoritative updates stay open while on the way.
    auto emptied = depleted;
    emptied.slots[11] = {};
    check(spikeProgressing(depleted,emptied,plan));
    check(spikeProgressing(emptied,moved,plan));
    check(spikeProgressing(depleted,moved,plan)); // Completion is checked first.
    auto duplicated = depleted;
    duplicated.slots[3] = {2,16};
    check(!spikeProgressing(depleted,duplicated,plan));
    auto both = moved;
    both.slots[11] = {2,1}; // Partial source with full destination: impossible.
    check(!spikeProgressing(depleted,both,plan));
    auto relocked = depleted;
    relocked.slots[11].locked = true;
    check(!spikeProgressing(depleted,relocked,plan));
}
