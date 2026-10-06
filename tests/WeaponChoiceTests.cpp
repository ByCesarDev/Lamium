#include "features/inventory/WeaponChoice.h"
#include <limits>
void check(bool, char const*);
void weaponChoiceTests() {
    using namespace lamium::inventory;
    std::array<WeaponCandidate,9> hotbar;
    check(!chooseHotbarWeapon(hotbar,0), "a hotbar without weapons keeps the selection");
    hotbar[3] = {6,false}; hotbar[5] = {7,true};
    check(chooseHotbarWeapon(hotbar,0) == 5, "a bare hand switches to the strongest weapon");
    check(!chooseHotbarWeapon(hotbar,5), "the strongest held weapon stays");
    check(chooseHotbarWeapon(hotbar,3) == 5, "a weaker held weapon switches to a stronger one");
    hotbar[3] = {7,false};
    check(!chooseHotbarWeapon(hotbar,3), "an equal held axe stays on a tie");
    check(chooseHotbarWeapon(hotbar,0) == 5, "a sword beats an equal axe in a lower slot");
    hotbar[3].sword = true;
    check(chooseHotbarWeapon(hotbar,0) == 3, "equal swords resolve to the lower slot");
    hotbar[3] = {std::numeric_limits<float>::infinity(),false};
    hotbar[5] = {std::numeric_limits<float>::quiet_NaN(),true};
    check(!chooseHotbarWeapon(hotbar,0), "non-finite damage cannot drive selection");
    check(!chooseHotbarWeapon(hotbar,-1) && !chooseHotbarWeapon(hotbar,9), "selection outside the hotbar never changes");

    check(meleeBonus({},true,true) == 0, "no enchantments add nothing");
    check(meleeBonus({4,0,0},false,false) == 5, "Sharpness counts against anything");
    check(meleeBonus({0,5,0},true,false) == 12.5f && meleeBonus({0,5,0},false,true) == 0, "Smite counts only against undead");
    check(meleeBonus({0,0,5},false,true) == 12.5f && meleeBonus({0,0,5},true,false) == 0, "Bane counts only against arthropods");

    std::array<WeaponCandidate,36> all;
    all[12] = {7,false}; all[20] = {7,true}; all[30] = {5,true};
    check(chooseInventoryWeapon(all,0) == 20, "the strongest inventory weapon is fetched, a sword on a tie");
    all[25] = {7,true};
    check(chooseInventoryWeapon(all,0) == 25, "equal inventory swords resolve to the higher slot");
    all[4] = {2,false};
    check(chooseInventoryWeapon(all,0) == 25, "a weaker hotbar item (a shovel) does not keep a stronger sword in the inventory");
    all[4] = {7,false};
    check(!chooseInventoryWeapon(all,0), "an equal hotbar weapon prevents a fetch");
    all[4] = {9,false};
    check(!chooseInventoryWeapon(all,0), "a stronger hotbar weapon prevents a fetch");
    all[4] = {};
    check(!chooseInventoryWeapon(all,9), "a fetch needs a hotbar selection");
}
