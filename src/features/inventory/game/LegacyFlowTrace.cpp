#include "features/inventory/game/LegacyFlowTrace.h"
#ifdef LAMIUM_RESEARCH_TRACE
#include "app/Runtime.h"
#include "features/inventory/game/ScreenTracker.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/render/UIRenderEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/PlayerInventory.h"
#include "mc/world/inventory/FillingContainer.h"
#include "mc/world/inventory/network/ItemStackNetManagerBase.h"
#include "mc/world/inventory/transaction/ComplexInventoryTransaction.h"
#include "mc/world/inventory/transaction/InventoryAction.h"
#include "mc/world/inventory/transaction/InventoryTransaction.h"
#include "mc/world/inventory/transaction/InventoryTransactionItemGroup.h"
#include "mc/world/inventory/transaction/InventoryTransactionManager.h"
#include "mc/world/phys/HitResult.h"
#include <atomic>
#include <memory>
#include <format>
#include <stdexcept>
#include <utility>

namespace lamium::inventory::game::legacyFlowTrace {
namespace {
// Bounded: one manual move is a handful of lines; a long session cannot flood.
constexpr unsigned maxLines = 400;
std::atomic<unsigned> lines{0};

LocalPlayer* localPlayer() {
    auto client = ll::service::getClientInstance();
    return client ? client->getLocalPlayer() : nullptr;
}
template <class... Args>
void log(std::format_string<Args...> format, Args&&... args) noexcept {
    try {
        if (!Runtime::instance().enabled()) return;
        if (lines.fetch_add(1) >= maxLines) return;
        Runtime::instance().self().getLogger().info(
            "research L-66 {}", std::format(format, std::forward<Args>(args)...)
        );
    } catch (...) {}
}
ItemStackNetManagerBase* netManager(LocalPlayer& player) { return player.mItemStackNetManager.get(); }
int legacyId(LocalPlayer& player) {
    auto* manager = netManager(player);
    return manager ? manager->mLegacyTransactionRequestId->mRawId : -1;
}
int allowState(LocalPlayer& player) {
    auto* manager = netManager(player);
    if (!manager) return -1;
    // Virtual call through the declared interface; the thunk itself is not
    // exported, so this is the only SDK-side way to read the flag.
    try { return manager->allowInventoryTransactionManager() ? 1 : 0; }
    catch (...) { return -1; }
}
void logState(char const* tag, LocalPlayer& player) {
    auto* manager = netManager(player);
    log("{} legacyId={} allow={} enabled={}", tag, legacyId(player), allowState(player),
        manager ? static_cast<bool>(manager->mIsEnabled) : false);
}
int actionSlot(InventoryAction const& action) { return static_cast<int>(action.mSlot); }
int actionFrom(InventoryAction const& action) { return static_cast<int>(action.mFromItem->mCount); }
int actionTo(InventoryAction const& action) { return static_cast<int>(action.mToItem->mCount); }
void logAction(char const* tag, InventoryAction const& action, bool forceBalanced = false) {
    auto const& source = action.mSource.get();
    log("{} source={} container={} slot={} from={} to={} balanced={}", tag,
        static_cast<int>(source.mType), static_cast<int>(source.mContainerId),
        actionSlot(action), actionFrom(action), actionTo(action), forceBalanced);
}
bool screenOpen() { return ScreenTracker::getInstance().current() != nullptr; }

// InventoryTransactionManager::addAction runs for both the client and the
// server player in a local world; keep the local client's inventory only.
LL_TYPE_INSTANCE_HOOK(AddAction, ll::memory::HookPriority::Normal, InventoryTransactionManager,
    &InventoryTransactionManager::addAction, void, InventoryAction const& action, bool forceBalanced) {
    try {
        auto* player = localPlayer();
        if (player && player == &mPlayer && screenOpen()) {
            logAction("addAction", action, forceBalanced);
            logState("addAction-state", *player);
        }
    } catch (...) {}
    origin(action, forceBalanced);
}
LL_TYPE_INSTANCE_HOOK(SetItem, ll::memory::HookPriority::Normal, Inventory,
    &Inventory::$setItem, void, int slot, ItemStack const& item) {
    try {
        auto* player = localPlayer();
        if (player && &player->getInventory() == this && screenOpen())
            log("setItem slot={} count={}", slot, static_cast<int>(item.mCount));
    } catch (...) {}
    origin(slot, item);
}
LL_TYPE_INSTANCE_HOOK(SetItemForceBalance, ll::memory::HookPriority::Normal, Inventory,
    &Inventory::$setItemWithForceBalance, void, int slot, ItemStack const& item, bool forceBalanced) {
    try {
        auto* player = localPlayer();
        if (player && &player->getInventory() == this && screenOpen())
            log("setItemForceBalance slot={} count={} balanced={}", slot, static_cast<int>(item.mCount), forceBalanced);
    } catch (...) {}
    origin(slot, item, forceBalanced);
}
// Middle-click block picking is vanilla's no-screen inventory -> hand path.
LL_TYPE_INSTANCE_HOOK(PickBlock, ll::memory::HookPriority::Normal, LocalPlayer,
    &LocalPlayer::pickBlock, void, HitResult const& hitResult, bool withData) {
    try {
        if (localPlayer() == this) {
            log("pickBlock withData={}", withData);
            logState("pickBlock-state", *this);
        }
    } catch (...) {}
    origin(hitResult, withData);
}
LL_TYPE_INSTANCE_HOOK(SwapSlots, ll::memory::HookPriority::Normal, FillingContainer,
    &FillingContainer::$swapSlots, void, int from, int to) {
    try {
        auto* player = localPlayer();
        if (player && static_cast<FillingContainer*>(&player->getInventory()) == this) {
            log("swapSlots from={} to={}", from, to);
            logState("swapSlots-state", *player);
        }
    } catch (...) {}
    origin(from, to);
}
LL_TYPE_INSTANCE_HOOK(SelectSlot, ll::memory::HookPriority::Normal, PlayerInventory,
    &PlayerInventory::selectSlot, bool, int slot, ContainerID containerId) {
    try {
        auto* player = localPlayer();
        if (player && player->mInventory.get() == this)
            log("selectSlot slot={} container={}", slot, static_cast<int>(containerId));
    } catch (...) {}
    return origin(slot, containerId);
}
LL_TYPE_INSTANCE_HOOK(SendComplex, ll::memory::HookPriority::Normal, LocalPlayer,
    &LocalPlayer::$sendComplexInventoryTransaction, void, std::unique_ptr<ComplexInventoryTransaction> transaction) {
    try {
        if (localPlayer() == this)
            log("sendComplex type={}", transaction ? static_cast<int>(transaction->mType) : -1);
    } catch (...) {}
    origin(std::move(transaction));
}
LL_TYPE_INSTANCE_HOOK(SendInventory, ll::memory::HookPriority::Normal, LocalPlayer,
    &LocalPlayer::$sendInventoryTransaction, void, InventoryTransaction const& transaction) {
    try {
        auto const& sources = transaction.mActions.get();
        log("sendInventoryTransaction sources={} contents={}", sources.size(), transaction.mContents.get().size());
        for (auto const& entry : sources)
            for (auto const& action : entry.second) logAction("send-action", action);
        logState("send-state", *this);
    } catch (...) {}
    origin(transaction);
}
// The begin call itself is a virtual with an unavailable thunk; its effect on
// the base request id is sampled while a container screen is open.
ll::event::ListenerPtr renderListener;
int previousLegacyId = -2;
void sample() {
    try {
        auto* player = localPlayer();
        if (!player || !screenOpen()) { previousLegacyId = -2; return; }
        int id = legacyId(*player);
        if (id != previousLegacyId) {
            if (previousLegacyId != -2) log("legacyId {} -> {}", previousLegacyId, id);
            previousLegacyId = id;
        }
    } catch (...) {}
}
struct Hook {
    int (*install)(bool);
    bool (*remove)(bool);
    bool installed = false;
};
Hook hooks[] = {
    {PickBlock::hook,          PickBlock::unhook         },
    {SwapSlots::hook,          SwapSlots::unhook         },
    {SelectSlot::hook,         SelectSlot::unhook        },
    {SendComplex::hook,        SendComplex::unhook       },
    {AddAction::hook,          AddAction::unhook         },
    {SetItem::hook,            SetItem::unhook           },
    {SetItemForceBalance::hook, SetItemForceBalance::unhook},
    {SendInventory::hook,      SendInventory::unhook     },
};
} // namespace
void start() {
    try {
        for (auto& hook : hooks)
            if (!hook.installed) {
                if (hook.install(true) != 0) throw std::runtime_error("Could not install L-66 flow trace hook");
                hook.installed = true;
            }
        if (!renderListener)
            renderListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::AfterUIRenderEvent>(
                [](auto&) { sample(); }
            );
        Runtime::instance().self().getLogger().warn(
            "L-66 legacy flow diagnostics enabled (slots, counts and state only)"
        );
    } catch (...) { stop(); throw; }
}
void stop() {
    if (renderListener) {
        ll::event::EventBus::getInstance().removeListener(renderListener);
        renderListener.reset();
    }
    for (auto it = std::rbegin(hooks); it != std::rend(hooks); ++it)
        if (it->installed && it->remove(true)) it->installed = false;
}
}
#else
namespace lamium::inventory::game::legacyFlowTrace {
void start() {}
void stop() {}
}
#endif
