#include "features/inventory/WeaponSwitch.h"
#include "features/inventory/FetchSlot.h"
#include "features/inventory/WeaponChoice.h"
#include "features/inventory/EquipmentPlan.h"
#include "features/inventory/RestockUse.h"
#include "features/inventory/game/InventoryMove.h"
#include "app/Runtime.h"
#include "ui/SettingsScreen.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/gamemode/SurvivalMode.h"
#include "mc/world/actor/player/PlayerInventory.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/VanillaItemTags.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/ItemEnchants.h"
#include "mc/world/item/enchanting/EnchantmentInstance.h"
#include "mc/network/packet/MobEquipmentPacket.h"
#include "mc/network/packet/MobEquipmentPacketPayload.h"
#include <chrono>
#include <stdexcept>

namespace lamium::inventory::weapons {
namespace {
using Clock = std::chrono::steady_clock;
bool installed = false;
// Each hit changes the held weapon's durability on the server; a fetch sent
// right after it could arrive first (L-66 ordering), so it waits for a later hit.
Clock::time_point lastHit{};
bool quietSinceHit() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - lastHit).count() >= restockQuietMs;
}
// Read the way Tool Protection reads them (checked in game); the vanilla
// per-enchantment bonus calls left Smite out against zombies (2026-10-02).
MeleeEnchants meleeEnchants(ItemStack const& stack) {
    MeleeEnchants levels;
    if (!stack.isEnchanted()) return levels;
    for (auto const& e : stack.constructItemEnchantsFromUserData().getAllEnchants())
        switch (static_cast<::Enchant::Type>(e.mEnchantType)) {
        case Enchant::Type::Sharpness: levels.sharpness = e.mLevel; break;
        case Enchant::Type::Smite: levels.smite = e.mLevel; break;
        case Enchant::Type::BaneOfArthropods: levels.bane = e.mLevel; break;
        default: break;
        }
    return levels;
}
// Attack damage plus Sharpness, Smite and Bane of Arthropods against this
// target (decided 2026-10-02; nothing else counts).
WeaponCandidate rate(ItemStack const& stack, Actor const& target) {
    if (stack.isNull() || !stack.mItem) return {};
    float damage = static_cast<float>(stack.mItem->getAttackDamage());
    if (damage <= 0) return {};
    damage += meleeBonus(meleeEnchants(stack),target.hasType(ActorType::Undead),target.hasType(ActorType::Arthropod));
    return {damage, stack.mItem->hasTag(VanillaItemTags::Sword())};
}
// The client reports its selected slot from its tick, after this attack's
// transaction; the server then drops a hit made with a slot it has not seen
// (in-game check 2026-10-02). Report it now, as that tick would.
void reportSelection(LocalPlayer& player, int slot) {
    auto const& held = player.getInventory().getItem(slot);
    MobEquipmentPacket packet{MobEquipmentPacketPayload{player.getRuntimeID(),held,slot,slot,ContainerID::Inventory}};
    player.sendNetworkPacket(packet);
    player.mSentSelectedSlot = slot;
    player.mSentInventoryItem = held;
}
bool living(Actor const& target) {
    return target.hasType(ActorType::Mob) && !target.isType(ActorType::ArmorStand);
}
void choose(Player& player, Actor const& target) {
    auto& runtime = Runtime::instance();
    if (!runtime.enabled() || !runtime.snapshot()->inventory.weaponSwitch || ui::ownsInput()) return;
    auto client = ll::service::getClientInstance();
    auto* local = client ? client->getLocalPlayer() : nullptr;
    if (!local || local != &player || player.isCreative() || player.isSpectator() || !living(target)) return;
    auto* supplies = player.mInventory.get();
    if (!supplies || supplies->mSelectedContainerId != ContainerID::Inventory) return;
    int selected = supplies->mSelected;
    if (selected < 0 || selected >= 9) return;
    bool fetch = runtime.snapshot()->inventory.weaponSwitchInventory;
    std::array<WeaponCandidate,36> candidates;
    for (int slot=0; slot<(fetch ? 36 : 9); ++slot) {
        auto const& stack = player.getInventory().getItem(slot);
        candidates[slot] = rate(stack,target);
        // Never fetch a weapon that is about to break (Tool Protection would swap it back).
        if (slot >= 9 && candidates[slot].damage > 0 && aboutToBreak(stack.mItem->getMaxDamage(),stack.getDamageValue()))
            candidates[slot] = {};
    }
    // A stronger inventory weapon is fetched before a hotbar pick; while the
    // last hit is too recent for a move, the hotbar pick serves this hit.
    auto source = fetch ? chooseInventoryWeapon(candidates,selected) : std::nullopt;
    if (!source || !quietSinceHit()) {
        std::array<WeaponCandidate,9> hotbar;
        std::copy_n(candidates.begin(),9,hotbar.begin());
        if (auto slot = chooseHotbarWeapon(hotbar,selected)) {
            supplies->selectSlot(*slot,ContainerID::Inventory);
            if (supplies->mSelected == *slot) reportSelection(*local,*slot);
        }
        return;
    }
    auto prefs = runtime.preferences();
    int slot = fetchSlot(selected, prefs.inventory.weaponSwitchSlot,
        prefs.inventory.fakeOffhand ? std::optional<int>(prefs.inventory.fakeOffhandSlot - 1) : std::nullopt);
    ItemStack held = player.getInventory().getItem(slot), weapon = player.getInventory().getItem(*source);
    if (!game::movePair(*local,{game::Place::Inventory,slot},weapon,{game::Place::Inventory,*source},held)) return;
    if (slot != selected) {
        supplies->selectSlot(slot,ContainerID::Inventory);
        if (supplies->mSelected == slot) reportSelection(*local,slot);
    }
}
void attacking(Player& player, Actor const& target) {
    try {
        choose(player,target);
    } catch (std::exception const& error) {
        static bool reported = false;
        if (!reported) { Runtime::instance().self().getLogger().error("Weapon selection failed: {}",error.what()); reported = true; }
    } catch (...) {}
}
void hit(Player const& player, bool result) {
    auto client = ll::service::getClientInstance();
    if (result && client && client->getLocalPlayer() == &player) lastHit = Clock::now();
}
LL_TYPE_INSTANCE_HOOK(WeaponAttack, ll::memory::HookPriority::Normal, GameMode,
    &GameMode::$attack, bool, Actor& entity, Vec3 const& hit) {
    attacking(mPlayer,entity);
    bool result = origin(entity,hit);
    weapons::hit(mPlayer,result);
    return result;
}
// SurvivalMode overrides attack; choosing twice is harmless (the second
// call finds the best weapon already held).
LL_TYPE_INSTANCE_HOOK(WeaponSurvivalAttack, ll::memory::HookPriority::Normal, SurvivalMode,
    &SurvivalMode::$attack, bool, Actor& entity, Vec3 const& hit) {
    attacking(mPlayer,entity);
    bool result = origin(entity,hit);
    weapons::hit(mPlayer,result);
    return result;
}
struct Hook { int (*install)(bool); bool (*remove)(bool); bool installed = false; };
Hook hooks[] = {{WeaponAttack::hook, WeaponAttack::unhook}, {WeaponSurvivalAttack::hook, WeaponSurvivalAttack::unhook}};
}
void start() {
    if (installed) return;
    for (auto& hook : hooks) if (!hook.installed) {
        if (hook.install(true) != 0) { stop(); throw std::runtime_error("Could not install weapon switch hook"); }
        hook.installed = true;
    }
    installed = true;
}
void stop() {
    for (auto it = std::rbegin(hooks); it != std::rend(hooks); ++it)
        if (it->installed && it->remove(true)) it->installed = false;
    lastHit = {};
    installed = false;
}
}
