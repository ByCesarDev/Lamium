#include "features/inventory/EquipmentPlan.h"
#include "features/inventory/ToolChoice.h"
#include "features/inventory/RestockPlan.h"
void check(bool, char const*);

void equipmentPlanTests() {
    using namespace lamium::inventory;
    check(aboutToBreak(1561,1560) && aboutToBreak(1561,1561) && !aboutToBreak(1561,1559) && !aboutToBreak(0,0),
          "one durability left (or none) is about to break; non-damageable items never are");

    std::vector<EnchantLevel> efficiency5{{15,5}}, efficiency4unbreaking3{{15,4},{17,3}};
    check(enchantDistance(efficiency5,efficiency5) == 0 && enchantDistance(efficiency5,efficiency4unbreaking3) == 4
          && enchantDistance(efficiency4unbreaking3,efficiency5) == 4 && enchantDistance({},efficiency5) == 5,
          "enchantment distance counts level gaps and unshared levels symmetrically");
    check(chooseReplacement({{12,4,900},{20,0,100},{30,0,100}}) == 30,
          "closest enchantments win before durability; ties take the higher slot");
    check(chooseReplacement({{12,0,100},{20,0,900}}) == 20, "more durability wins among equal enchantments");
    check(!chooseReplacement({{12,0,1},{13,0,0}}), "a replacement that is itself about to break is never chosen");
    check(guardMining(true,true,false) == GuardMining::Wait && guardMining(true,false,false) == GuardMining::Stop
          && guardMining(true,false,true) == GuardMining::Continue && guardMining(false,false,false) == GuardMining::Continue,
          "mining waits for a swap, stops without one, and a new press after a stop mines on");

    std::array<ToolCandidate,36> tools{};
    tools[20] = {8,true}; tools[30] = {8,true}; tools[10] = {4,true};
    check(chooseInventoryTool(tools,0) == 30, "the fastest inventory tool is fetched; ties take the higher slot");
    tools[5] = {2,true};
    check(!chooseInventoryTool(tools,0), "an effective hotbar tool is selected instead of fetching one");
    tools[5] = {}; tools[0] = {3,true};
    check(!chooseInventoryTool(tools,0), "an effective held tool stays");

    ElytraSwapState state;
    ElytraInput in{true,false,false,false,true,true,true};
    check(elytraStep(state,in) == ElytraStep::PutOn, "a glide attempt without an elytra puts one on");
    in.elytraAvailable = false;
    check(elytraStep(state,in) == ElytraStep::None, "no usable elytra leaves the chest alone");
    in.elytraAvailable = true; in.wearingElytra = true;
    check(elytraStep(state,in) == ElytraStep::None, "an elytra already worn is not swapped");
    state.returnSlot = 20;
    in = {true,true,false,true,true,false,false};
    check(elytraStep(state,in) == ElytraStep::None, "gliding keeps the elytra on");
    in.gliding = false; in.onGround = true;
    check(elytraStep(state,in) == ElytraStep::None && elytraStep(state,in) == ElytraStep::TakeOff,
          "the chest item returns on the second grounded tick");
    state = {20,0};
    in.returnSlotHoldsChest = false;
    check(elytraStep(state,in) == ElytraStep::Forget, "a changed return slot forgets the swap instead of guessing");
    state = {20,0}; in.returnSlotHoldsChest = true; in.wearingElytra = false;
    check(elytraStep(state,in) == ElytraStep::Forget, "taking the elytra off by hand forgets the swap");
    state = {20,0}; in.wearingElytra = true; in.enabled = false;
    check(elytraStep(state,in) == ElytraStep::Forget, "disabling the feature leaves the equipment as it is");

    RestockSnapshot before; before.context = 3; before.selected = 2;
    before.slots[offhandSlot] = {1,1}; before.slots[20] = {1,1}; before.slots[25] = {1,1}; before.slots[2] = {2,10};
    auto after = before; after.slots[offhandSlot] = {};
    auto plan = planRestock(before,after,true,1,false,-1,restockThreshold,1,offhandSlot);
    check(plan && plan->destination == offhandSlot && plan->source == 25 && plan->stillValid(after)
          && plan->predicted().slots[offhandSlot] == RestockSlot{1,1,false},
          "a consumed offhand totem is refilled in the offhand from the main inventory");
    check(!planRestock(before,after,true,1) , "an offhand change is not a main-hand consumption");
    auto changed = after; changed.slots[2].count = 9;
    check(!planRestock(before,changed,true,1,false,-1,restockThreshold,1,offhandSlot),
          "the offhand plan requires every other slot unchanged, the hand included");
    before.slots[offhandSlot] = {3,16}; after = before; after.slots[offhandSlot].count = 5; before.slots[offhandSlot].count = 6;
    before.slots[20] = after.slots[20] = {3,16}; before.slots[25] = after.slots[25] = {};
    plan = planRestock(before,after,true,16,false,-1,restockThreshold,1,offhandSlot);
    check(plan && plan->source == 20 && plan->destinationAfter.count == 16 && plan->sourceAfter.count == 5,
          "offhand fireworks and arrows are topped up like the hand");
    before.slots[20] = after.slots[20] = {}; before.slots[2] = after.slots[2] = {3,16};
    check(!planRestock(before,after,true,16,true,-1,restockThreshold,1,offhandSlot),
          "the held stack never supplies the offhand");
}
