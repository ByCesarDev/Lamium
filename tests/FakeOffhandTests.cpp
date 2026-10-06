#include "features/inventory/FakeOffhandPlan.h"
void check(bool, char const*);
void fakeOffhandTests() {
    using lamium::inventory::fakeOffhand::instantUseSlot;
    using lamium::inventory::fakeOffhand::instantItem;
    using lamium::inventory::fakeOffhand::passivePrimaryItem;
    using lamium::inventory::fakeOffhand::PrimaryUse;
    using lamium::inventory::fakeOffhand::primaryPass;
    using lamium::inventory::fakeOffhand::foodBlocked;
    check(primaryPass(PrimaryUse::Dirt,true,false) && !primaryPass(PrimaryUse::Dirt,false,false),
        "dirt passes in air but retains main-hand placement on block targets");
    check(primaryPass(PrimaryUse::GroundTool,true,false) && !primaryPass(PrimaryUse::GroundTool,false,false),
        "ground tools pass in air without guessing whether a block-target tool action fails");
    check(foodBlocked(false,false,20.f,20.f) && primaryPass(PrimaryUse::Food,false,true),
        "an ordinary food at confirmed full hunger passes to the secondary hand");
    check(!foodBlocked(false,false,19.f,20.f) && !primaryPass(PrimaryUse::Food,true,false),
        "hungry primary food retains consumption priority even in air");
    check(!foodBlocked(true,false,20.f,20.f) && !foodBlocked(false,true,20.f,20.f),
        "always-edible and creative food cannot be inferred blocked from hunger alone");
    check(!foodBlocked(false,false,{},20.f) && !foodBlocked(false,false,NAN,20.f)
        && !foodBlocked(false,false,20.f,NAN) && !foodBlocked(false,false,0.f,0.f)
        && !foodBlocked(false,false,21.f,20.f),
        "missing and invalid hunger data preserve primary behavior");
    check(!primaryPass(PrimaryUse::Unknown,true,true),
        "unknown primary actions remain vanilla even in air and at full hunger");
    for (auto name : {"minecraft:totem_of_undying", "minecraft:totem", "minecraft:stick",
        "minecraft:paper", "minecraft:diamond", "minecraft:emerald", "minecraft:iron_ingot",
        "minecraft:gold_ingot", "minecraft:copper_ingot", "minecraft:netherite_ingot",
        "minecraft:coal", "minecraft:charcoal"}) {
        check(instantUseSlot(true,true,0,8,passivePrimaryItem(name),true,false,false,false) == 8,
            "known passive primary items pass instant use to the secondary slot");
    }
    for (auto name : {"minecraft:apple", "minecraft:bow", "minecraft:water_bucket",
        "minecraft:milk_bucket", "minecraft:potion", "minecraft:diamond_axe",
        "minecraft:diamond_shovel", "minecraft:flint_and_steel", "minecraft:leather_helmet",
        "minecraft:bone", "minecraft:white_dye", "custom:totem", "custom:stick"}) {
        check(!passivePrimaryItem(name),
            "timed, target-sensitive, wearable and unknown primary uses keep vanilla priority");
    }
    using lamium::inventory::fakeOffhand::ownsInstantHold;
    check(ownsInstantHold(0,8,0,8,true),
        "a native instant hold can repeat while its primary and target slots stay owned");
    check(!ownsInstantHold(0,8,1,8,true) && !ownsInstantHold(0,8,8,8,true),
        "manual selection cancels repetition rather than restoring over the chosen slot");
    check(!ownsInstantHold(0,8,0,7,true) && !ownsInstantHold(0,8,0,8,false),
        "target changes and lost gameplay eligibility cancel the native instant hold");
    for (int invalid : {-1, 9}) {
        check(!ownsInstantHold(invalid,8,invalid,8,true)
            && !ownsInstantHold(0,invalid,0,invalid,true),
            "released and invalid slot identities cannot own a repeat session");
    }
    check(!ownsInstantHold(8,8,8,8,true),
        "an ordinary selected target never becomes a borrowed repeat session");
    check(instantItem("minecraft:water_bucket") && instantItem("minecraft:bucket")
        && instantItem("minecraft:snowball") && instantItem("minecraft:egg"),
        "known instant items do not depend on a generic maximum-use-duration value");
    check(!instantItem("minecraft:milk_bucket") && !instantItem("minecraft:bow")
        && !instantItem("minecraft:apple") && !instantItem("minecraft:potion")
        && !instantItem("custom:snowball") && !instantItem("minecraft:trident"),
        "timed and unknown items cannot enter the per-call selection scope");
    check(instantUseSlot(true,true,0,8,true,true,false,false,false) == 8,
        "an idle primary hand can borrow an instant-use item in air or on an ordinary block");
    check(!instantUseSlot(true,true,0,8,false,true,false,false,false),
        "applicable or uncertain primary-hand use keeps priority");
    check(!instantUseSlot(true,true,0,8,true,false,false,false,false),
        "empty target and timed items cannot start from a per-call borrow");
    check(!instantUseSlot(true,true,0,8,true,true,true,false,false),
        "entity interactions remain vanilla in the first instant-use step");
    check(!instantUseSlot(true,true,0,8,true,true,false,true,false)
        && instantUseSlot(true,true,0,8,true,true,false,true,true) == 8,
        "containers keep ordinary interaction unless sneaking");
    check(!instantUseSlot(false,true,0,8,true,true,false,false,false)
        && !instantUseSlot(true,false,0,8,true,true,false,false,false),
        "disabled or released activation never changes selection");
    for (int invalid : {-1, 9}) {
        check(!instantUseSlot(true,true,invalid,8,true,true,false,false,false)
            && !instantUseSlot(true,true,0,invalid,true,true,false,false,false),
            "only valid hotbar slots can be borrowed");
    }
    check(!instantUseSlot(true,true,8,8,true,true,false,false,false),
        "manually selecting the target leaves its ordinary use alone");
}
