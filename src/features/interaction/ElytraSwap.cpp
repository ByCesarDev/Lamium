#include "features/interaction/ElytraSwap.h"
#include "features/inventory/EquipmentPlan.h"
#include "features/inventory/game/InventoryMove.h"
#include "app/Runtime.h"
#include "ui/SettingsScreen.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/TargetedBedrock.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/event/world/ClientLevelTickEvent.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/PlayerInventory.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include <atomic>
#include <stdexcept>

namespace lamium::interaction::elytraSwap {
namespace {
using namespace inventory;
bool installed = false;
ll::event::ListenerPtr tickListener, exitListener;
ElytraSwapState state;
ItemStack worn;      // What the chest held before the elytra went on.
int dimension = -1;
void trace(char const* stage, int value = 0) noexcept {
#ifdef LAMIUM_RESTOCK_TRACE
    try {
        static std::atomic<unsigned> samples{};
        if (samples.fetch_add(1) < 1024)
            Runtime::instance().self().getLogger().info("ElytraSwap: {} value={}",stage,value);
    } catch (...) {}
#else
    (void)stage; (void)value;
#endif
}
bool enabled() {
    auto& runtime = Runtime::instance();
    return runtime.enabled() && runtime.preferences().interaction.elytraSwap;
}
LocalPlayer* localPlayer() {
    auto client = ll::service::getClientInstance();
    auto* player = client ? client->getLocalPlayer() : nullptr;
    if (!player || !player->isAlive() || player->isSpectator() || !player->mInventory || ui::ownsInput()
        || !client->isInGameInputEnabled()) return nullptr;
    return player;
}
bool isElytra(ItemStack const& stack) {
    return !stack.isNull() && stack.mCount > 0 && stack.getTypeName() == "minecraft:elytra";
}
ItemStack const& chest(LocalPlayer& player) { return player.getArmor(SharedTypes::Legacy::ArmorSlot::Torso); }
// The elytra with the most durability left, main inventory first, never the
// held slot or one about to stop working.
std::optional<int> elytraSlot(LocalPlayer& player) {
    std::vector<ReplacementCandidate> main, hotbar;
    for (int slot = 0; slot < 36; ++slot) {
        auto const& stack = player.getInventory().getItem(slot);
        if (slot == player.mInventory->mSelected || !isElytra(stack) || !stack.mItem) continue;
        (slot < 9 ? hotbar : main).push_back({slot,0,stack.mItem->getMaxDamage() - stack.getDamageValue()});
    }
    if (auto slot = chooseReplacement(main)) return slot;
    return chooseReplacement(hotbar);
}
bool returnSlotHoldsChest(LocalPlayer& player) {
    auto const& there = player.getInventory().getItem(*state.returnSlot);
    bool empty = there.isNull() || there.mCount <= 0;
    bool wasEmpty = worn.isNull() || worn.mCount <= 0;
    return wasEmpty ? empty : !empty && there.matchesItem(worn);
}
void putOn(LocalPlayer& player, int slot) {
    ItemStack elytra = player.getInventory().getItem(slot), before = chest(player);
    if (!game::movePair(player,{game::Place::Armor,1},elytra,{game::Place::Inventory,slot},before)) { trace("put-on-busy"); return; }
    state = {slot,0};
    worn = before;
    dimension = static_cast<int>(player.getDimensionId());
    trace("put-on",slot);
}
void tick() noexcept {
    try {
        if (!state.returnSlot) return;
        auto* player = localPlayer();
        if (!player) return;
        if (static_cast<int>(player->getDimensionId()) != dimension) { state = {}; trace("forget-dimension"); return; }
        ElytraInput in;
        in.enabled = enabled();
        in.gliding = player->isGliding();
        in.onGround = player->isOnGround();
        in.wearingElytra = isElytra(chest(*player));
        in.returnSlotHoldsChest = returnSlotHoldsChest(*player);
        switch (elytraStep(state,in)) {
        case ElytraStep::TakeOff: {
            int slot = *state.returnSlot;
            ItemStack elytra = chest(*player), back = player->getInventory().getItem(slot);
            if (game::movePair(*player,{game::Place::Armor,1},back,{game::Place::Inventory,slot},elytra)) {
                trace("take-off",slot);
                state = {};
            }
            break;
        }
        case ElytraStep::Forget: trace("forget"); state = {}; break;
        default: break;
        }
    } catch (std::exception const& error) {
        state = {};
        static bool reported = false;
        if (!reported) { reported = true; Runtime::instance().self().getLogger().error("Auto Elytra stopped: {}",error.what()); }
    } catch (...) { state = {}; }
}
// Vanilla tries to start gliding when jump is pressed in mid-air. Putting the
// elytra on first lets that same press start the glide.
LL_TYPE_INSTANCE_HOOK(GlideAttempt, ll::memory::HookPriority::Normal, Player, &Player::tryStartGliding, bool) {
    try {
        auto* player = localPlayer();
        if (player && static_cast<Player*>(player) == this && enabled() && !player->isCreative()) {
            trace("glide-attempt",player->isGliding());
            ElytraInput in;
            in.enabled = true;
            in.gliding = player->isGliding();
            in.wearingElytra = isElytra(chest(*player));
            auto slot = in.wearingElytra ? std::nullopt : elytraSlot(*player);
            in.elytraAvailable = slot.has_value();
            in.glideAttempt = true;
            if (!state.returnSlot && elytraStep(state,in) == ElytraStep::PutOn) putOn(*player,*slot);
        }
    } catch (...) { state = {}; }
    return origin();
}
struct Hook { int (*install)(bool); bool (*remove)(bool); bool installed = false; };
Hook hooks[] = {{GlideAttempt::hook,GlideAttempt::unhook}};
}
void start() {
    if (installed) return;
    try {
        for (auto& hook : hooks) if (!hook.installed) {
            if (hook.install(true) != 0) throw std::runtime_error("Could not install Auto Elytra hook");
            hook.installed = true;
        }
        auto& bus = ll::event::EventBus::getInstance();
        tickListener = bus.emplaceListener<ll::event::ClientLevelTickEvent>([](auto&) { tick(); });
        exitListener = bus.emplaceListener<ll::event::ClientExitLevelEvent>([](auto&) { state = {}; });
        if (!tickListener || !exitListener) throw std::runtime_error("Could not subscribe Auto Elytra lifecycle");
    } catch (...) { stop(); throw; }
    installed = true;
}
void stop() {
    for (auto* listener : {&tickListener,&exitListener}) if (*listener) {
        ll::event::EventBus::getInstance().removeListener(*listener); listener->reset();
    }
    for (auto it = std::rbegin(hooks); it != std::rend(hooks); ++it)
        if (it->installed && it->remove(true)) it->installed = false;
    state = {};
    installed = false;
}
}
