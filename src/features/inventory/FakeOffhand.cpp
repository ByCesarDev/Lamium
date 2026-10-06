#include "features/inventory/FakeOffhand.h"
#include "features/inventory/FakeOffhandPlan.h"
#include "features/interaction/PeriodicInput.h"
#include "features/camera/CameraSessions.h"
#include "app/Runtime.h"
#ifdef LAMIUM_OFFHAND_TRACE
#include "app/TraceLog.h"
#endif
#include "input/Actions.h"
#include "input/Binding.h"
#include "ui/SettingsScreen.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
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
// Only slot identities survive an instant-use hold; selection is restored
// inside each native call and vanilla owns the repeat timer.
std::atomic_int instantPrimary = -1, instantTarget = -1;
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
    if (!item.mItem || item.mBlock) return false;
    auto name = item.getTypeName();
    return name.starts_with("minecraft:")
        && (item.mItem->hasTag(VanillaItemTags::Sword()) || item.mItem->hasTag(VanillaItemTags::Pickaxe()));
}
bool endsOnRightClick(Settings const& value) {
    auto chord = input::effectiveChord(value.bindings, input::Action::FakeOffhandUse);
    return !chord.empty() && chord.back() == input::Token{input::Device::Mouse, 2};
}
std::optional<int> chooseSlot(IClientInstance& client, HitResult const& solid, int& selected, bool& instant) {
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
    bool targetInstant = !secondary.isNull() && instantItem(secondary.getTypeName());
    bool blockItem = !secondary.isNull() && secondary.mBlock && !targetInstant;
    bool hitBlock = solid.mType == HitResultType::Tile;
    bool interactive = hitBlock && player->getDimensionBlockSource().getBlock(solid.mBlock)
        .getBlockType().isInteractiveBlock();
    if (blockItem) return placementSlot(true, true, selected, target, true, hitBlock, interactive, player->isSneaking());
    // Timed use stops on a selection change (L-95 baseline). Never start it
    // from a per-call borrow or preempt an existing primary use.
    if (!player->mItemInUse->mItem->isNull()) return {};
    auto slot = instantUseSlot(true, true, selected, target,
        idlePrimary(player->getInventory().getItem(selected)), targetInstant,
        solid.mType == HitResultType::Entity, interactive, player->isSneaking());
    instant = slot.has_value();
    return slot;
}
void traceChoice(char const* stage, IClientInstance& client,
    int selected, std::optional<int> slot, bool instant, HitResult const* buildHit = nullptr) noexcept {
#ifdef LAMIUM_OFFHAND_TRACE
    try {
        static TraceBudget budget;
        auto const& hit = buildHit ? *buildHit : client.getLatestHitResult();
        auto* player = client.getLocalPlayer();
        if (!player || !player->mInventory) return;
        int actual = player->mInventory->mSelected;
        if (selected < 0) selected = actual;
        int target = targetSlot.load();
        if (selected < 0 || selected >= 9 || target < 0 || target >= 9) return;
        auto const& primary = player->getInventory().getItem(selected);
        auto const& secondary = player->getInventory().getItem(target);
        bool sword = !primary.isNull() && primary.mItem && primary.mItem->hasTag(VanillaItemTags::Sword());
        bool pickaxe = !primary.isNull() && primary.mItem && primary.mItem->hasTag(VanillaItemTags::Pickaxe());
        traceLog(budget, 128,
            "L-95 adapter stage={} enabled={} synthetic={} owned={}/{} selected={} actual={} target={} chosen={} instant={} hit={} primary={} block={} sword={} pickaxe={} idle={} secondary={} known={} using={} input={} ui={} screen={}",
            stage, enabled.load(), synthetic.load(), instantPrimary.load(), instantTarget.load(),
            selected, actual, target, slot.value_or(-1), instant, static_cast<int>(hit.mType),
            primary.isNull() ? "empty" : primary.getTypeName(), static_cast<bool>(primary.mBlock),
            sword, pickaxe, idlePrimary(primary), secondary.isNull() ? "empty" : secondary.getTypeName(),
            !secondary.isNull() && instantItem(secondary.getTypeName()), !player->mItemInUse->mItem->isNull(),
            client.isInGameInputEnabled(), ui::ownsInput(), client.getScreenName());
    } catch (...) {}
#else
    (void)stage; (void)client; (void)selected; (void)slot; (void)instant; (void)buildHit;
#endif
}
// Only a synchronous action owns this pointer. Vanilla acquires its own
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
bool beginInstant(IClientInstance& client, int selected, int slot, bool clearPrimary) {
    auto* player = client.getLocalPlayer();
    if (clearPrimary && !interaction::periodic::sendUseEdge(client, false)) {
        traceChoice("up-unavailable", client, selected, slot, true);
        return false;
    }
    if (client.getLocalPlayer() != player || !player || !player->mInventory
        || player->mInventory->mSelectedContainerId != ContainerID::Inventory
        || player->mInventory->mSelected != selected) return false;
    if (!player->mInventory->selectSlot(slot, ContainerID::Inventory)) {
        traceChoice("press-select-failed", client, selected, slot, true);
        return false;
    }
    SelectionRestore restore{*player, slot, selected, true};
    struct ReleaseUse {
        IClientInstance& client;
        bool owed = true;
        ~ReleaseUse() {
            if (owed) try { interaction::periodic::sendUseEdge(client, false); } catch (...) {}
        }
    } releaseUse{client};
    syntheticThread.store(std::this_thread::get_id());
    bool delivered = interaction::periodic::sendUseEdge(client, true);
    if (delivered) {
        instantPrimary.store(selected);
        instantTarget.store(slot);
        synthetic.store(true);
        traceChoice(clearPrimary ? "down-sent" : "native-down-sent", client, selected, slot, true);
    } else {
        traceChoice("down-unavailable", client, selected, slot, true);
    }
    releaseUse.owed = false;
    return delivered;
}
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
        int primary = instantPrimary.load();
        if (primary >= 0) {
            if (!ownsInstantHold(primary, instantTarget.load(), selected,
                targetSlot.load(), slot.has_value() && instant)) {
                traceChoice("hold-cancel", *this, selected, slot, instant, &solid);
                release();
                slot.reset();
            }
        } else if (instant) slot.reset();
        if (slot) {
            player = getLocalPlayer();
            if (!player->mInventory->selectSlot(*slot, ContainerID::Inventory)) {
                traceChoice("hold-select-failed", *this, selected, slot, instant, &solid);
                if (instant) release();
                slot.reset();
            }
        }
    } catch (...) {
        if (instantPrimary.load() >= 0) try { release(); } catch (...) {}
        slot.reset();
    }
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
LL_TYPE_INSTANCE_HOOK(ChangeDimension, ll::memory::HookPriority::Normal, LevelRendererPlayer,
    &LevelRendererPlayer::$onWillChangeDimension, void, Player& player) {
    try {
        auto client = ll::service::getClientInstance();
        if (client && client->getLocalPlayer() == &player) release();
    } catch (...) {}
    origin(player);
}
struct Hook { int (*install)(bool); bool (*remove)(bool); bool installed = false; };
Hook hooks[] = {{BuildAction::hook, BuildAction::unhook}, {ReportUse::hook, ReportUse::unhook},
    {ReportOn::hook, ReportOn::unhook}, {ChangeDimension::hook, ChangeDimension::unhook}};
}
void configure(Settings const& value) {
    enabled.store(value.inventory.fakeOffhand);
    targetSlot.store(value.inventory.fakeOffhandSlot - 1);
}
void rightChord(bool held) { rightChordHeld.store(held); }
bool rightChordActive() { return rightChordHeld.load(); }
bool nativePress(IClientInstance& client) noexcept {
    if (!enabled.load() || !rightChordHeld.load()) return false;
    // A queued press may have arrived first. Its captured handlers already
    // ran once; do not let a later physical handler try the primary item.
    if (synthetic.load()) return instantPrimary.load() >= 0;
    bool borrowed = false;
    try {
        int selected = -1;
        bool instant = false;
        auto slot = chooseSlot(client, client.getLatestHitResult(), selected, instant);
        traceChoice("native-choice", client, selected, slot, instant);
        if (!slot || !instant) return false;
        borrowed = true;
        // Replay the complete captured handler list once under selection.
        // sendUseEdge uses raw handlers, so it cannot reenter this wrapper.
        return beginInstant(client, selected, *slot, false);
    } catch (...) {
        // A callback may already have acted. Never retry it with the primary.
        return borrowed;
    }
}
void press(IClientInstance& client) {
    auto value = Runtime::instance().preferences();
    if (!value.inventory.fakeOffhand) return;
    if (synthetic.load()) {
        traceChoice("press-already-held", client, -1, {}, false);
        return;
    }
    try {
        if (!Runtime::instance().enabled() || !client.getLocalPlayer() || !client.isInGameInputEnabled()
            || ui::ownsInput() || !gameplayScreen(client.getScreenName())) return;
        int selected = -1;
        bool instant = false;
        auto slot = chooseSlot(client, client.getLatestHitResult(), selected, instant);
        traceChoice("press-choice", client, selected, slot, instant);
        if (slot && instant) {
            beginInstant(client, selected, *slot, true);
            return;
        }
    } catch (...) { return; }
    // A right-click chord lets vanilla receive the click; rightChord() marks it.
    if (endsOnRightClick(value)) return;
    syntheticThread.store(std::this_thread::get_id());
    synthetic.store(interaction::periodic::sendUseEdge(client, true));
}
void release() {
    instantPrimary.store(-1);
    instantTarget.store(-1);
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
