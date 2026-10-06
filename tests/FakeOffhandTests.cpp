#include "features/inventory/FakeOffhandPlan.h"
void check(bool, char const*);
void fakeOffhandTests() {
    using lamium::inventory::fakeOffhand::instantUseSlot;
    using lamium::inventory::fakeOffhand::instantItem;
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
