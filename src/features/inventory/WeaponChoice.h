#pragma once
#include <array>
#include <cmath>
#include <optional>
namespace lamium::inventory {
// Damage an item adds to a hit on this target (attack damage plus target
// enchantment bonuses); zero for anything that is not a weapon.
struct WeaponCandidate { float damage = 0; bool sword = false; };
inline bool isWeapon(WeaponCandidate weapon) { return std::isfinite(weapon.damage) && weapon.damage > 0; }
// L-67: the hotbar weapon that hits hardest. The held item stays on a tie; among
// equal others a sword wins (an axe loses 2 durability per hit), then the
// lower slot.
inline std::optional<int> chooseHotbarWeapon(std::array<WeaponCandidate,9> const& weapons, int selected) {
    if (selected < 0 || selected >= 9) return {};
    float held = isWeapon(weapons[selected]) ? weapons[selected].damage : 0;
    std::optional<int> best;
    for (int slot=0; slot<9; ++slot) {
        auto const& weapon = weapons[slot];
        if (slot == selected || !isWeapon(weapon) || weapon.damage <= held) continue;
        if (!best || weapon.damage > weapons[*best].damage
            || (weapon.damage == weapons[*best].damage && weapon.sword && !weapons[*best].sword)) best = slot;
    }
    return best;
}
// L-69: only when no hotbar slot holds a weapon, the strongest one in the
// main inventory (ties: a sword, then the higher slot).
inline std::optional<int> chooseInventoryWeapon(std::array<WeaponCandidate,36> const& weapons, int selected) {
    if (selected < 0 || selected >= 9) return {};
    for (int slot=0; slot<9; ++slot) if (isWeapon(weapons[slot])) return {};
    std::optional<int> best;
    for (int slot=9; slot<36; ++slot) {
        auto const& weapon = weapons[slot];
        if (!isWeapon(weapon)) continue;
        if (!best || weapon.damage > weapons[*best].damage
            || (weapon.damage == weapons[*best].damage && (weapon.sword || !weapons[*best].sword))) best = slot;
    }
    return best;
}
}
