#include "features/inventory/FakeOffhand.h"
#include "features/inventory/FakeOffhandPlan.h"
#include "features/interaction/PeriodicInput.h"
#include "features/camera/CameraSessions.h"
#include "app/Runtime.h"
#include "input/Actions.h"
#include "input/Binding.h"
#include "ui/SettingsScreen.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/actor/player/PlayerInventory.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/VanillaItemTags.h"
#include "mc/world/item/HandSlot.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/network/packet/MobEquipmentPacket.h"
#include "mc/network/packet/MobEquipmentPacketPayload.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/phys/HitResult.h"
#include <atomic>
#include <optional>
#include <stdexcept>
#include <thread>

namespace lamium::inventory::fakeOffhand {
namespace {
// Settings are cached here: the build hook runs every client tick and must not
// copy the whole Settings object.
std::atomic_bool enabled = false;
std::atomic_int targetSlot = 8;
// Set from the window procedure as the dispatcher decides it, so a right-click
// chord is already active when vanilla receives the same click.
std::atomic_bool rightChordHeld = false;
// A non-mouse trigger replays vanilla use edges; only the thread that sent
// the down edge may send the matching up edge.
std::atomic_bool synthetic = false;
std::atomic<std::thread::id> syntheticThread{};
bool installed = false;

void reportSelection(LocalPlayer& player, int slot) {
    auto const& held = player.getInventory().getItem(slot);
    MobEquipmentPacket packet{MobEquipmentPacketPayload{player.getRuntimeID(), held, slot, slot, ContainerID::Inventory}};
    player.sendNetworkPacket(packet);
    player.mSentSelectedSlot = slot;
    player.mSentInventoryItem = held;
}
bool idlePrimary(ItemStack const& item) {
    if (item.isNull()) return true;
    if (!item.mItem || item.mBlock || item.mItem->getMaxUseDuration(&item) > 0) return false;
    auto name = item.getTypeName();
    return name.starts_with("minecraft:")
        && (item.mItem->hasTag(VanillaItemTags::Sword()) || item.mItem->hasTag(VanillaItemTags::Pickaxe()));
}
bool endsOnRightClick(Settings const& value) {
    auto chord = input::effectiveChord(value.bindings, input::Action::FakeOffhandUse);
    return !chord.empty() && chord.back() == input::Token{input::Device::Mouse, 2};
}
std::optional<int> chooseSlot(ClientInstance& client, HitResult const& solid, int& selected, bool& instant) {
    auto* player = client.getLocalPlayer();
    if (!Runtime::instance().enabled() || !player || !player->isAlive() || player->isSpectator()
        || !client.isInGameInputEnabled() || ui::ownsInput() || !gameplayScreen(client.getScreenName())
        || CameraSessions::instance().blocksLookInteraction(*player)) return {};
    auto* inventory = player->mInventory.get();
    if (!inventory || inventory->mSelectedContainerId != ContainerID::Inventory) return {};
    selected = inventory->mSelected;
    int target = targetSlot.load();
    if (selected < 0 || selected >= 9 || target < 0 || target >= 9) return {};
    auto const& secondary = player->getInventory().getItem(target);
    bool blockItem = !secondary.isNull() && secondary.mBlock;
    bool hitBlock = solid.mType == HitResultType::Tile;
    bool interactive = hitBlock && player->getDimensionBlockSource().getBlock(solid.mBlock)
        .getBlockType().isInteractiveBlock();
    if (blockItem) return placementSlot(true, true, selected, target, true, hitBlock, interactive, player->isSneaking());
    // Timed use stops on a selection change (L-95 baseline). Never start it
    // from a per-call borrow or preempt an existing primary use.
    if (!player->mItemInUse->mItem->isNull()) return {};
    bool targetInstant = !secondary.isNull() && secondary.mItem
        && secondary.mItem->getMaxUseDuration(&secondary) == 0;
    auto slot = instantUseSlot(true, true, selected, target,
        idlePrimary(player->getInventory().getItem(selected)), targetInstant,
        solid.mType == HitResultType::Entity, interactive, player->isSneaking());
    instant = slot.has_value();
    return slot;
}
// Only a synchronous build call owns this pointer. Vanilla acquires its own
// item references after selection; no item argument is substituted.
struct SelectionRestore;
thread_local SelectionRestore* instantBorrow = nullptr;
struct SelectionRestore {
    LocalPlayer& player;
    int slot, previous;
    bool instant, reported = false;
    SelectionRestore* outer = instantBorrow;
    SelectionRestore(LocalPlayer& value, int target, int before, bool isInstant)
        : player(value), slot(target), previous(before), instant(isInstant) {
        if (instant) instantBorrow = this;
    }
    ~SelectionRestore() {
        if (instant) instantBorrow = outer;
        try {
            auto client = ll::service::getClientInstance();
            if (!client || client->getLocalPlayer() != &player) return;
            auto* inventory = player.mInventory.get();
            if (inventory && inventory->mSelectedContainerId == ContainerID::Inventory
                && inventory->mSelected == slot && inventory->selectSlot(previous, ContainerID::Inventory)
                && reported) reportSelection(player, previous);
        } catch (...) {}
    }
};
void reportBorrow(Player& source, HandSlot hand) noexcept {
    try {
        auto* borrow = instantBorrow;
        if (!borrow || borrow->reported || hand != HandSlot::Mainhand || &source != &borrow->player) return;
        auto* inventory = borrow->player.mInventory.get();
        if (!inventory || inventory->mSelectedContainerId != ContainerID::Inventory
            || inventory->mSelected != borrow->slot) return;
        reportSelection(borrow->player, borrow->slot);
        borrow->reported = true;
    } catch (...) {}
}
LL_TYPE_INSTANCE_HOOK(BuildAction, ll::memory::HookPriority::Normal, ClientInstance,
    &ClientInstance::_tickBuildAction, void, HitResult const& solid, HitResult const& liquid, bool advanceTime) {
    if (!enabled.load() || !(rightChordHeld.load() || synthetic.load())) {
        origin(solid, liquid, advanceTime);
        return;
    }
    std::optional<int> slot;
    int selected = -1;
    bool instant = false;
    LocalPlayer* player = nullptr;
    try {
        slot = chooseSlot(*this, solid, selected, instant);
        if (slot) {
            player = getLocalPlayer();
            if (!player->mInventory->selectSlot(*slot, ContainerID::Inventory)) slot.reset();
        }
    } catch (...) { slot.reset(); }
    if (!slot) {
        origin(solid, liquid, advanceTime);
        return;
    }
    SelectionRestore restore{*player, *slot, selected, instant};
    origin(solid, liquid, advanceTime);
}

LL_TYPE_INSTANCE_HOOK(ReportUse, ll::memory::HookPriority::High, GameMode,
    &GameMode::$useItem, bool, ItemStack& item, HandSlot hand) {
    reportBorrow(mPlayer, hand);
    return origin(item, hand);
}
LL_TYPE_INSTANCE_HOOK(ReportOn, ll::memory::HookPriority::High, GameMode,
    &GameMode::$useItemOn, InteractionResult, ItemStack& item, BlockPos const& pos, uchar face,
    Vec3 const& hit, HandSlot hand, Block const* block, bool first) {
    reportBorrow(mPlayer, hand);
    return origin(item, pos, face, hit, hand, block, first);
}
struct Hook { int (*install)(bool); bool (*remove)(bool); bool installed = false; };
Hook hooks[] = {{BuildAction::hook, BuildAction::unhook}, {ReportUse::hook, ReportUse::unhook}, {ReportOn::hook, ReportOn::unhook}};
}
void configure(Settings const& value) {
    enabled.store(value.inventory.fakeOffhand);
    targetSlot.store(value.inventory.fakeOffhandSlot - 1);
}
void rightChord(bool held) { rightChordHeld.store(held); }
bool rightChordActive() { return rightChordHeld.load(); }
void press(IClientInstance& client) {
    auto value = Runtime::instance().preferences();
    // A right-click chord lets vanilla receive the click; rightChord() marks it.
    if (!value.inventory.fakeOffhand || endsOnRightClick(value) || synthetic.load()) return;
    syntheticThread.store(std::this_thread::get_id());
    synthetic.store(interaction::periodic::sendUseEdge(client, true));
}
void release() {
    if (!synthetic.exchange(false)) return;
    // Never touch input handlers off the client thread (e.g. a shutdown
    // disable). Vanilla drops the stale hold on its next focus/input reset.
    if (syntheticThread.load() != std::this_thread::get_id()) return;
    if (auto client = ll::service::getClientInstance()) interaction::periodic::sendUseEdge(*client, false);
}
void start() {
    if (installed) return;
    try {
        for (auto& hook : hooks) if (!hook.installed) {
            if (hook.install(true) != 0) throw std::runtime_error("Could not install Fake Offhand hook");
            hook.installed = true;
        }
        installed = true;
    } catch (...) { stop(); throw; }
}
void stop() {
    release();
    rightChordHeld.store(false);
    for (auto it = std::rbegin(hooks); it != std::rend(hooks); ++it)
        if (it->installed && it->remove(true)) it->installed = false;
    installed = false;
}
}
