#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
namespace lamium::inventory {
// Damage an item adds to a hit on this target (attack damage plus target
// enchantment bonuses); zero for anything that is not a weapon.
struct WeaponCandidate { float damage = 0; bool sword = false; };
// Bedrock's melee enchantment bonuses: Sharpness 1.25 per level against
// anything, Smite and Bane of Arthropods 2.5 per level against undead and
// arthropods only.
struct MeleeEnchants { int sharpness = 0, smite = 0, bane = 0; };
inline float meleeBonus(MeleeEnchants levels, bool undead, bool arthropod) {
    return 1.25f * levels.sharpness + (undead ? 2.5f * levels.smite : 0.f) + (arthropod ? 2.5f * levels.bane : 0.f);
}
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
// L-69: the strongest weapon in the main inventory (ties: a sword, then the
// higher slot), but only when it hits harder than every hotbar item; an equal
// one stays in the inventory. (It used to require a hotbar without any
// weapon, so a diamond shovel kept a diamond sword in the inventory,
// maintainer 2026-10-06.)
inline std::optional<int> chooseInventoryWeapon(std::array<WeaponCandidate,36> const& weapons, int selected) {
    if (selected < 0 || selected >= 9) return {};
    float hotbar = 0;
    for (int slot=0; slot<9; ++slot) if (isWeapon(weapons[slot])) hotbar = std::max(hotbar, weapons[slot].damage);
    std::optional<int> best;
    for (int slot=9; slot<36; ++slot) {
        auto const& weapon = weapons[slot];
        if (!isWeapon(weapon)) continue;
        if (!best || weapon.damage > weapons[*best].damage
            || (weapon.damage == weapons[*best].damage && (weapon.sword || !weapons[*best].sword))) best = slot;
    }
    if (best && weapons[*best].damage <= hotbar) return {};
    return best;
}
}
