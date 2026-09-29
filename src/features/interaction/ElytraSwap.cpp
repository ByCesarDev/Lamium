#include "features/interaction/ElytraSwap.h"
#include "features/inventory/EquipmentPlan.h"
#include "features/inventory/game/InventoryMove.h"
#include "app/Runtime.h"
#include "ui/SettingsScreen.h"
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
#include "mc/entity/components/MoveInputComponent.h"
#include "mc/input/MoveInputState.h"
#include <atomic>
#include <stdexcept>
#include <utility>

namespace lamium::interaction::elytraSwap {
namespace {
using namespace inventory;
bool installed = false;
ll::event::ListenerPtr tickListener, exitListener;
ElytraSwapState state;
ItemStack worn;      // What the chest held before the elytra went on.
int dimension = -1;
bool jumpWasDown = false, keyPressed = false;
int glideTries = 0;  // Ticks left to let vanilla start the glide after a swap.
constexpr int glideRetryTicks = 3;
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
bool empty(ItemStack const& stack) { return stack.isNull() || stack.mCount <= 0; }
bool isElytra(ItemStack const& stack) { return !empty(stack) && stack.getTypeName() == "minecraft:elytra"; }
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
    return empty(worn) ? empty(there) : !empty(there) && there.matchesItem(worn);
}
bool airborne(LocalPlayer& player) {
    return !player.isOnGround() && !player.isGliding() && !player.isFlying() && !player.isInWater() && !player.getVehicle();
}
void putOn(LocalPlayer& player) {
    if (isElytra(chest(player))) return;
    auto slot = elytraSlot(player);
    trace("put-on-slot",slot ? *slot : -1);
    if (!slot) return;
    ItemStack elytra = player.getInventory().getItem(*slot), before = chest(player);
    if (!game::movePair(player,{game::Place::Armor,1},elytra,{game::Place::Inventory,*slot},before)) { trace("put-on-busy"); return; }
    state = {*slot,0,false};
    worn = before;
    dimension = static_cast<int>(player.getDimensionId());
    // Starting the glide in the swap's own tick failed (5728561): the swap is
    // not yet seen by the glide check, so let vanilla try on the next ticks.
    if (airborne(player)) glideTries = glideRetryTicks;
}
void takeOff(LocalPlayer& player) {
    int slot = *state.returnSlot;
    ItemStack elytra = chest(player), back = player.getInventory().getItem(slot);
    if (game::movePair(player,{game::Place::Armor,1},back,{game::Place::Inventory,slot},elytra)) {
        trace("take-off",slot);
        state = {};
    }
}
bool jumpDown(LocalPlayer& player) {
    auto input = player.getEntityContext().tryGetComponent<MoveInputComponent>();
    if (!input) return false;
    auto& flags = *input->mInputState->mFlagValues;
    return flags.test(static_cast<size_t>(MoveInputState::Flag::JumpDown));
}
bool holdingFireworks(LocalPlayer& player) {
    auto const& held = player.getInventory().getItem(player.mInventory->mSelected);
    return !empty(held) && held.getTypeName() == "minecraft:firework_rocket";
}
void tick() noexcept {
    try {
        bool key = std::exchange(keyPressed,false);
        auto* player = localPlayer();
        if (!player || !enabled() || player->isCreative()) { jumpWasDown = false; glideTries = 0; return; }
        bool down = jumpDown(*player);
        bool jumped = down && !jumpWasDown;
        jumpWasDown = down;
        if (state.returnSlot && static_cast<int>(player->getDimensionId()) != dimension) { state = {}; trace("forget-dimension"); }
        if (key) {
            // The key toggles: take off an elytra this feature put on.
            if (state.returnSlot && isElytra(chest(*player)) && returnSlotHoldsChest(*player)) takeOff(*player);
            else putOn(*player);
        } else if (jumped && airborne(*player) && holdingFireworks(*player)) {
            trace("firework-jump");
            putOn(*player);
        }
        if (glideTries > 0) {
            --glideTries;
            if (player->isGliding() || !airborne(*player)) glideTries = 0;
            else if (isElytra(chest(*player))) trace("glide-try",player->tryStartGliding());
        }
        if (!state.returnSlot) return;
        ElytraInput in;
        in.enabled = true;
        in.gliding = player->isGliding();
        in.onGround = player->isOnGround();
        in.wearingElytra = isElytra(chest(*player));
        in.returnSlotHoldsChest = returnSlotHoldsChest(*player);
        in.chestWasEmpty = empty(worn);
        switch (elytraStep(state,in)) {
        case ElytraStep::TakeOff: takeOff(*player); break;
        case ElytraStep::Forget: trace("forget"); state = {}; break;
        default: break;
        }
    } catch (std::exception const& error) {
        state = {};
        static bool reported = false;
        if (!reported) { reported = true; Runtime::instance().self().getLogger().error("Auto Elytra stopped: {}",error.what()); }
    } catch (...) { state = {}; }
}
}
void press() { keyPressed = true; }
void start() {
    if (installed) return;
    try {
        auto& bus = ll::event::EventBus::getInstance();
        tickListener = bus.emplaceListener<ll::event::ClientLevelTickEvent>([](auto&) { tick(); });
        exitListener = bus.emplaceListener<ll::event::ClientExitLevelEvent>([](auto&) { state = {}; glideTries = 0; });
        if (!tickListener || !exitListener) throw std::runtime_error("Could not subscribe Auto Elytra lifecycle");
    } catch (...) { stop(); throw; }
    installed = true;
}
void stop() {
    for (auto* listener : {&tickListener,&exitListener}) if (*listener) {
        ll::event::EventBus::getInstance().removeListener(*listener); listener->reset();
    }
    state = {};
    glideTries = 0;
    installed = false;
}
}
