#include "features/inventory/RestockPlan.h"
#include "features/inventory/RestockUse.h"
void check(bool, char const*);

void restockPlanTests() {
    using namespace lamium::inventory;
    RestockSnapshot before;
    before.context = 7;
    before.selected = 3;
    before.slots[3] = {2,7};
    before.slots[0] = {2,64};
    before.slots[9] = {1,64};
    before.slots[10] = {2,32,true};
    before.slots[11] = {2,32};
    before.slots[12] = {2,64};
    auto after = before;
    after.slots[3].count = 6;
    auto plan = planRestock(before,after,true,64);
    check(plan && plan->source == 11 && plan->destination == 3 && plan->stillValid(after),
          "main inventory supplies the same selected slot, skipping locks and different components");
    auto predicted = plan->predicted();
    check(predicted.selected == 3 && predicted.slots[3].count == 38 && predicted.slots[11].empty()
          && predicted.slots[0].count == 64, "partial refill consumes one source and preserves other hotbar stacks");
    before.slots[11].count = after.slots[11].count = 64;
    plan = planRestock(before,after,true,64,true);
    check(plan && plan->source == 11 && plan->sourceAfter.count == 6 && plan->destinationAfter.count == 64,
          "main inventory wins even with hotbar sources on; refill leaves the excess at source");
    check(!planRestock(before,after,false,64), "count changes without use evidence never authorize restock");
    for (int slot = 0; slot < 36; ++slot) {
        auto changed = after;
        changed.slots[slot] = {8,1};
        check(!plan->stillValid(changed), "every slot is revalidated before sending, including unrelated slots");
        check(!planRestock(before,changed,true,64), "unrelated mutations cannot silently choose another reserve");
    }
    auto changed = after;
    changed.context++;
    check(!plan->stillValid(changed) && !planRestock(before,changed,true,64), "world generation invalidates a plan");
    changed = after; changed.selected = 4;
    check(!plan->stillValid(changed) && !planRestock(before,changed,true,64), "manual selection changes invalidate a plan");
    changed = after; changed.slots[3].locked = true;
    check(!planRestock(before,changed,true,64), "locked destinations are never changed");
    changed = after; changed.slots[3].count = 5;
    check(!planRestock(before,changed,true,64), "multiple ambiguous decrements are not one consumption");
    check(!planRestock(before,after,true,0) && !planRestock(before,after,true,256), "invalid stack limits fail open");
    check(!planRestock(before,before,true,64), "unchanged or failed use cannot refill");
    for (int slot = 9; slot < 36; ++slot) before.slots[slot] = after.slots[slot] = {};
    check(!planRestock(before,after,true,64), "hotbar reserves are excluded by default");
    plan = planRestock(before,after,true,64,true);
    check(plan && plan->source == 0 && plan->destination == 3, "opt-in hotbar reserve moves without selecting it");
    before.slots[0] = after.slots[0] = {};
    check(!planRestock(before,after,true,64,true), "no reserve leaves the hand alone");

    for (int maxStack : {1,16,64}) {
        before = {}; before.context = 8; before.selected = 0;
        before.slots[0] = {1,1}; before.slots[9] = {1,maxStack};
        after = before; after.slots[0] = {};
        plan = planRestock(before,after,true,maxStack);
        check(plan && plan->destinationAfter.count == maxStack && plan->sourceAfter.empty(),
              "depletion moves the reserve for single, sixteen and sixty-four stacks");
        if (maxStack == 1) continue;
        before.slots[0].count = 7; after = before; after.slots[0].count = 6;
        plan = planRestock(before,after,true,maxStack);
        check(plan && plan->destinationAfter.count == maxStack && plan->sourceAfter.count == 6,
              "partial refills respect each item's maximum stack size");
        check(!planRestock(before,after,true,maxStack,false,-1,0), "internal threshold zero disables partial refill only");
        before.slots[0].count = 8; after.slots[0].count = 7;
        check(!planRestock(before,after,true,maxStack), "seven left is above the provisional six-item threshold");
    }
    before = {}; before.context = 9; before.selected = 2;
    before.slots[2] = {1,1}; before.slots[14] = {1,1};
    after = before; after.slots[2] = {2,1};
    check(!planRestock(before,after,true,1), "unrecognized replacement is not an empty slot");
    plan = planRestock(before,after,true,1,false,2);
    check(plan && plan->sourceAfter == RestockSlot{2,1} && plan->destinationAfter == RestockSlot{1,1},
          "known remainder exchanges with the reserve even without an empty inventory slot");
    for (int slot = 0; slot < 36; ++slot)
        if (slot != 2 && slot != 14) before.slots[slot] = after.slots[slot] = {7,64};
    check(planRestock(before,after,true,1,false,2).has_value(), "a full inventory still permits a two-slot exchange");
    before.slots[14].count = after.slots[14].count = 16;
    plan = planRestock(before,after,true,16,false,2);
    check(plan && plan->destinationAfter.count == 16 && plan->sourceAfter.count == 1,
          "a single final bottle can be exchanged with a whole compatible reserve");
    before.slots[2].count = 2;
    check(!planRestock(before,after,true,16,false,2), "replacement before the last consumed item is ambiguous");
    check(restockRemainder("minecraft:water_bucket") == "minecraft:bucket"
          && restockRemainder("minecraft:mushroom_stew") == "minecraft:bowl"
          && restockRemainder("minecraft:potion") == "minecraft:glass_bottle"
          && restockRemainder("addon:mushroom_stew").empty()
          && restockRemainder("minecraft:diamond_pickaxe").empty(),
          "only explicitly supported remainder transformations qualify");

    RestockUseEvidence evidence{42,true};
    check(evidence.observe(RestockUseSend::Place,42,true) && evidence.ready(), "placement has one matching use send");
    check(evidence.observe(RestockUseSend::Use,42,true), "one same-tick secondary Use belongs to placement");
    check(!evidence.observe(RestockUseSend::Use,42,true), "a third send cannot be hidden as a duplicate");
    evidence = {42,true}; evidence.observe(RestockUseSend::Place,42,true);
    check(!evidence.observe(RestockUseSend::Use,43,true), "a later use cannot be deduplicated into an old placement");
    check(!evidence.observe(RestockUseSend::Place,42,true), "a second placement is a new action");
    evidence = {42,false};
    check(!evidence.observe(RestockUseSend::Use,42,false) && !evidence.ready(), "offhand and other slots do not match");
    evidence.timed = true;
    check(evidence.observe(RestockUseSend::Use,42,true) && !evidence.ready(), "starting food does not imply consumption");
    check(evidence.observe(RestockUseSend::Use,46,true) && !evidence.ready(),
          "a repeated Use while eating belongs to the same timed use");
    check(!evidence.observe(RestockUseSend::Use,46,false), "a Use for another slot while eating is not absorbed");
    check(evidence.observe(RestockUseSend::Release,75,true) && !evidence.ready(), "early release alone cannot refill food");
    evidence.completed = true;
    check(evidence.ready(), "timed consumption requires start, release and completion");
    check(!evidence.observe(RestockUseSend::Other,75,true), "unrelated transactions cancel correlation");

    evidence = {42,true};
    evidence.observe(RestockUseSend::Place,42,true);
    evidence.observe(RestockUseSend::Use,42,true);
    check(evidence.beginSecondaryCallback(42),
          "the secondary send may precede its callback without discarding placement");
    check(!evidence.beginSecondaryCallback(42), "a second secondary callback is not deduplicated");
    evidence = {42,true};
    evidence.observe(RestockUseSend::Place,42,true);
    check(evidence.beginSecondaryCallback(42) && evidence.observe(RestockUseSend::Use,42,true),
          "the secondary callback may also precede its send");
    evidence = {42,true};
    check(!evidence.beginSecondaryCallback(43), "later-tick callbacks do not reuse an old placement");
    evidence = {42,false};
    check(!evidence.beginSecondaryCallback(42), "ordinary uses have no placement secondary callback");

    check(!restockSettled(false,0) && !restockSettled(false,restockSettleMs - 1),
          "a move waits for the server to run the use");
    check(restockSettled(true,0), "a server update showing the consumption allows the move at once");
    check(restockSettled(false,restockSettleMs), "without a server update the move waits the settle delay");
    check(restockUseStarted(false,true) && restockUseStarted(true,false) && !restockUseStarted(false,false),
          "starting food fails the use callback yet is tracked; other failed uses are not");
}
