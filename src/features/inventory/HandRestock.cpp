#include "features/inventory/HandRestock.h"
#include "features/inventory/RestockPlan.h"
#include "features/inventory/game/RequestTracker.h"
#include "app/Runtime.h"
#include "ui/SettingsScreen.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/TargetedBedrock.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/event/world/ClientLevelTickEvent.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/MinecraftGame.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/gui/screens/models/ClientInstanceScreenModel.h"
#include "mc/world/containers/managers/controllers/HudContainerManagerController.h"
#include "mc/world/containers/managers/models/ContainerManagerModel.h"
#include "mc/world/actor/player/PlayerInventory.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/item/ItemLockHelper.h"
#include "mc/world/item/ItemLockMode.h"
#include "mc/world/item/HandSlot.h"
#include "mc/legacy/ActorRuntimeID.h"
#include <atomic>
#include <chrono>
#include "mc/world/inventory/transaction/ItemUseInventoryTransaction.h"
#ifdef LAMIUM_RESTOCK_TRACE
#include "features/inventory/RestockSpike.h"
#include "features/inventory/game/RestockTrace.h"
#include "mc/client/network/LegacyClientNetworkHandler.h"
#include "mc/network/packet/InventorySlotPacket.h"
#include "mc/network/packet/InventoryContentPacket.h"
#include "mc/world/inventory/transaction/ComplexInventoryTransaction.h"
#include "mc/world/inventory/transaction/InventoryAction.h"
#include "mc/world/inventory/transaction/InventorySource.h"
#include "mc/world/inventory/transaction/InventoryTransaction.h"
#include "mc/world/inventory/transaction/InventoryTransactionItemGroup.h"
#endif

namespace lamium::inventory::restock {
namespace {
using game::ResponseBarrier;
constexpr char collection[] = "hotbar_items";
std::weak_ptr<HudContainerManagerController> hud;
struct Operation {
    std::weak_ptr<HudContainerManagerController> controller;
    std::uint64_t playerId;
    int dimension;
    RestockSnapshot before;
    std::vector<ItemStack> kinds;
    std::optional<RestockPlan> plan;
    std::optional<HotbarSelectPlan> select;
    std::optional<game::TransferToken> token;
    bool useFinished = false;
    bool useSent = false;
    std::chrono::steady_clock::time_point useDeadline;
#ifdef LAMIUM_RESTOCK_TRACE
    // L-66 spike: one client-built NormalTransaction for an inventory-only
    // reserve, then authoritative-state observation until the deadline.
    SpikeStage spike = SpikeStage::Fresh;
    RestockSnapshot spikeBefore;
    std::chrono::steady_clock::time_point spikeDeadline;
    // Legacy uses execute in the server tick while inventory transactions run
    // on receipt, so the move must wait for the server's own slot update.
    bool serverDepleted = false;
#endif
};
#ifdef LAMIUM_RESTOCK_TRACE
// Set only around our own send so the complex-send observer can tell the
// spike's NormalTransaction apart from an unrelated mutation.
bool spikeSendInFlight = false;
#endif
std::shared_ptr<Operation> pending;
ll::event::ListenerPtr tickListener, exitListener;
// Opt-in diagnostics explain silent early exits without changing use/transfer
// behavior. Only fixed stage labels and numeric state are emitted.
void trace(char const* stage, int value = 0) noexcept {
#ifdef LAMIUM_RESTOCK_TRACE
    try {
        if (!Runtime::instance().preferences().inventory.handRestock) return;
        static std::atomic<unsigned> samples{};
        if (samples.fetch_add(1) >= 512) return;
        Runtime::instance().self().getLogger().info("Restock use trace: {} value={}",stage,value);
    } catch (...) {}
#else
    (void)stage; (void)value;
#endif
}
void cancel() {
    auto old = std::move(pending);
    if (old && old->token) game::cancelTransfer(*old->token);
}
void failure() noexcept {
    cancel();
    static bool reported = false;
    if (!reported) {
        reported = true;
        try { Runtime::instance().self().getLogger().error("Hand Restock stopped after an inventory error"); }
        catch (...) {}
    }
}
LocalPlayer* eligible() {
    auto& runtime = Runtime::instance();
    auto client = ll::service::getClientInstance();
    if (!runtime.enabled() || !runtime.preferences().inventory.handRestock || ui::ownsInput()
        || !client || !client->isInGameInputEnabled()) return nullptr;
    auto* player = client->getLocalPlayer();
    if (!player || !player->isAlive() || player->isCreative() || player->isSpectator()
        || player->isSleeping() || !player->hasRuntimeID() || !player->mInventory) return nullptr;
    auto& inventory = *player->mInventory;
    if (inventory.mSelectedContainerId != ContainerID::Inventory || inventory.mSelected < 0
        || inventory.mSelected >= 9) return nullptr;
    return player;
}
bool owned(HudContainerManagerController& controller, Player& player) {
    auto model = controller.mContainerManagerModel.lock();
    return model && &model->mPlayer == &player && !controller.mContainersClosed
        && controller.hasContainerController(collection) && controller.getContainerSize(collection) == 36;
}
RestockSnapshot snapshot(Operation& op, HudContainerManagerController& controller, LocalPlayer& player) {
    RestockSnapshot result;
    result.context = 1; // Operation identity is separately checked by runtime ID, dimension and weak controller.
    result.selected = player.mInventory->mSelected;
    for (int slot = 0; slot < 36; ++slot) {
        auto const& stack = controller.getItemStack(collection,slot);
        auto const& inventoryStack = player.getInventory().getItem(slot);
        bool empty = stack.isNull() || stack.mCount <= 0;
        bool inventoryEmpty = inventoryStack.isNull() || inventoryStack.mCount <= 0;
        if (empty != inventoryEmpty || (!empty && (stack.mCount != inventoryStack.mCount
            || !stack.matchesItem(inventoryStack)))) throw std::runtime_error("HUD inventory mismatch");
        if (empty) continue;
        int kind = 0;
        for (; kind < static_cast<int>(op.kinds.size()); ++kind)
            if (stack.matchesItem(op.kinds[kind])) break;
        if (kind == static_cast<int>(op.kinds.size())) op.kinds.emplace_back(stack);
        result.slots[slot] = {kind,stack.mCount,ItemLockHelper::getItemLockMode(stack) == ItemLockMode::LockInSlot};
    }
    return result;
}
std::shared_ptr<Operation> beginUse(Player& actor, HandSlot hand) noexcept {
    try {
        if (pending || hand != HandSlot::Mainhand) { trace("nested-or-offhand"); return {}; }
        auto* player = eligible();
        auto controller = hud.lock();
        if (!player) { trace("ineligible"); return {}; }
        if (player != &actor) { trace("non-local-actor"); return {}; }
        if (!controller) { trace("hud-expired"); return {}; }
        if (!owned(*controller,*player)) { trace("hud-not-owned-or-closed"); return {}; }
        auto op = std::make_shared<Operation>();
        op->controller = controller;
        op->playerId = player->getRuntimeID().rawID;
        op->dimension = static_cast<int>(player->getDimensionId());
        op->before = snapshot(*op,*controller,*player);
        auto const& held = op->before.slots[op->before.selected];
        if (held.count != 1 || held.locked) { trace("held-count-or-lock",held.count); return {}; }
        op->token = game::beginTransfer(*controller);
        if (!op->token) { trace("capture-unavailable"); return {}; }
        pending = op;
        trace("capture-started");
#ifdef LAMIUM_RESTOCK_TRACE
        game::restockTrace::inspectUseController(*controller);
#endif
        return op;
    } catch (...) { failure(); return {}; }
}
bool current(Operation const& op, LocalPlayer& player, HudContainerManagerController& controller) {
    return op.playerId == player.getRuntimeID().rawID
        && op.dimension == static_cast<int>(player.getDimensionId())
        && op.before.selected == player.mInventory->mSelected
        && hud.lock() == op.controller.lock() && owned(controller,player);
}
void finishUse(std::shared_ptr<Operation> const& op, bool success) noexcept {
    if (!op || pending != op) return;
    try {
        auto* player = eligible();
        auto controller = op->controller.lock();
        if (!success || !player || !controller || !current(*op,*player,*controller)) {
            trace("finish-invalid",success); cancel(); return;
        }
        game::endTransfer(*op->token);
        // The local use callback can return before inventory depletion arrives.
        // Legacy consumption can instead send a complex transaction after this
        // callback. Keep the ownership token while observing that separate path.
        op->useFinished = true;
        op->useDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        trace("use-finished");
    } catch (...) { failure(); }
}
#ifdef LAMIUM_RESTOCK_TRACE
// Build one ordinary NormalTransaction: take the whole reserve stack out of
// its main-inventory slot and place it into the empty selected hotbar slot.
// Only SDK types are used; the transaction is handed to the vanilla
// LocalPlayer send path, which owns serialization and sending.
bool sendClientMove(LocalPlayer& player, RestockPlan const& plan) {
    auto const& stack = player.getInventory().getItem(plan.source);
    if (stack.isNull() || stack.mCount <= 0) return false;
    ItemStack empty;
    InventorySource source{InventorySourceType::ContainerInventory, ContainerID::Inventory,
                           InventorySource::InventorySourceFlags::NoFlag};
    InventoryTransaction transaction;
    transaction.addAction(InventoryAction(source,static_cast<uint>(plan.source),stack,empty));
    transaction.addAction(InventoryAction(source,static_cast<uint>(plan.destination),empty,stack));
    transaction.forceBalanceTransaction();
    spikeSendInFlight = true;
    try { player.$sendInventoryTransaction(transaction); }
    catch (...) { spikeSendInFlight = false; throw; }
    spikeSendInFlight = false;
    return true;
}
// L-66 research spike, trace builds only. Sends at most one client-built
// transaction per depletion and then waits for authoritative inventory state;
// success is declared from that state only. A mismatch, correction, timeout
// or unrelated change cancels open without retrying.
void spikeTick(std::shared_ptr<Operation> const& op, LocalPlayer& player,
               HudContainerManagerController& controller) noexcept {
    try {
        if (!op->plan || op->spike == SpikeStage::Done || op->select) { cancel(); return; }
        auto& plan = *op->plan;
        auto now = snapshot(*op,controller,player);
        if (pending != op) return;
        if (op->spike == SpikeStage::Fresh) {
            // A legacy use runs in the server tick while inventory transactions
            // run on receipt, so a move sent now can be executed before the
            // use and consume from the moved stack. Wait for the server's own
            // slot update of the depleted hand first.
            if (op->useSent && !op->serverDepleted) {
                if (std::chrono::steady_clock::now() >= op->spikeDeadline) {
                    trace("spike-no-server-depletion");
                    cancel();
                }
                return;
            }
            if (!plan.stillValid(now)) { trace("spike-stale"); cancel(); return; }
            op->spikeBefore = now;
            bool sent = false;
            try { sent = sendClientMove(player,plan); }
            catch (...) { failure(); return; }
            if (pending != op) return;
            trace("spike-send",plan.expectedSource.count);
            if (!sent) { trace("spike-refused"); cancel(); return; }
            op->spike = SpikeStage::Sent;
            op->spikeDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            return;
        }
        if (spikeMoveApplied(op->spikeBefore,now,plan)) {
            Runtime::instance().self().getLogger().info(
                "Hand Restock L-66 client transaction moved inventory reserve");
            trace("spike-moved",now.slots[now.selected].count);
            op->spike = SpikeStage::Done;
            cancel();
            return;
        }
        if (!spikeUnchanged(op->spikeBefore,now)) {
            // Server updates may arrive slot by slot; only states still on the
            // way to the one planned move stay open.
            if (spikeProgressing(op->spikeBefore,now,plan)) return;
            trace("spike-mismatch");
            Runtime::instance().self().getLogger().info("Hand Restock spike moved nothing");
            op->spike = SpikeStage::Done;
            cancel();
            return;
        }
        if (std::chrono::steady_clock::now() >= op->spikeDeadline) {
            trace("spike-timeout");
            Runtime::instance().self().getLogger().info("Hand Restock spike moved nothing");
            op->spike = SpikeStage::Done;
            cancel();
        }
    } catch (...) { failure(); }
}
#endif
void tick() noexcept {
    if (!pending) return;
    try {
        auto op = pending;
        auto* player = eligible();
        auto controller = op->controller.lock();
        if (!player || !controller || !current(*op,*player,*controller)) {
            trace("tick-context-changed"); cancel(); return;
        }
        if (!op->useFinished) return; // A synchronous vanilla use is still on the stack.
#ifdef LAMIUM_RESTOCK_TRACE
        if (!op->token) {
            // The spike released the use token when it armed and owns the
            // observation from here.
            if (spikeEligible(op->select,op->plan)) { spikeTick(op,*player,*controller); return; }
            cancel(); return;
        }
#endif
        auto result = game::transferResult(*op->token);
        if (result == ResponseBarrier::Result::Waiting) return;
        bool legacyUse = result == ResponseBarrier::Result::Untracked && op->useSent;
        if (result != ResponseBarrier::Result::Accepted && !legacyUse) {
            Runtime::instance().self().getLogger().info("Hand Restock stopped: inventory response {}",static_cast<int>(result));
            cancel(); return;
        }
        auto now = snapshot(*op,*controller,*player);
        if (legacyUse && std::chrono::steady_clock::now() >= op->useDeadline) { cancel(); return; }
        if (legacyUse && !now.slots[now.selected].empty()) {
            // Only an unchanged inventory may wait for delayed depletion. The
            // deadline cancels observation; elapsed time never proves success.
            if (now.slots != op->before.slots || std::chrono::steady_clock::now() >= op->useDeadline) cancel();
            return;
        }
        if (!op->select && !op->plan) {
            trace(legacyUse ? "observed-use-count" : "accepted-use-count",now.slots[now.selected].count);
            op->select = planHotbarSelect(op->before,now,true);
            op->plan = planRestock(op->before,now,true);
            trace(op->select || op->plan ? "plan-ready" : "no-depletion-plan");
            if (!op->select && !op->plan) { cancel(); return; }
#ifdef LAMIUM_RESTOCK_TRACE
            if (spikeEligible(op->select,op->plan)) {
                // The use phase is over; free the barrier for the move spike.
                if (op->token) game::cancelTransfer(*op->token);
                op->token.reset();
                op->spikeDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
                trace("spike-armed",op->plan->source);
            }
#endif
        }
        if (op->select) {
            // HUD-controller transfers are unsupported (L-17 open issue), so
            // move the selection to the hotbar reserve through selectSlot,
            // the same proven API Tool Switch uses. No stacks are rewritten.
            if (!op->select->stillValid(now)) { cancel(); return; }
            bool moved = false;
            try { moved = player->mInventory->selectSlot(op->select->source,ContainerID::Inventory); }
            catch (...) { failure(); return; }
            if (pending != op) return; // Vanilla may synchronously leave the world.
            bool confirmed = moved && player->mInventory->mSelected == op->select->source;
            Runtime::instance().self().getLogger().info("Hand Restock {}",
                confirmed ? "selected hotbar reserve" : "stopped: selection refused");
            trace("hotbar-select-submitted",confirmed);
            cancel(); return;
        }
#ifdef LAMIUM_RESTOCK_TRACE
        // Trace builds try the L-66 client-built transaction for an
        // inventory-only reserve.
        if (spikeEligible(op->select,op->plan)) { spikeTick(op,*player,*controller); return; }
#endif
        // A main-inventory reserve exists but has no supported transfer path.
        Runtime::instance().self().getLogger().info("Hand Restock stopped: inventory reserve has no transfer path");
        cancel(); return;
    } catch (...) { failure(); }
}
LL_TYPE_INSTANCE_HOOK(CaptureHud, ll::memory::HookPriority::Normal, ClientInstanceScreenModel,
    &ClientInstanceScreenModel::createHudContainerManagerController, std::shared_ptr<HudContainerManagerController>) {
    auto result = origin();
    cancel();
    hud = result;
    trace("hud-captured",bool(result));
    return result;
}
LL_TYPE_INSTANCE_HOOK(Use, ll::memory::HookPriority::Normal, GameMode,
    &GameMode::$useItem, bool, ItemStack& item, HandSlot hand) {
    trace("use-item", static_cast<int>(hand));
    auto op = beginUse(mPlayer,hand);
    try { bool result = origin(item,hand); finishUse(op,result); return result; }
    catch (...) { if (pending == op) cancel(); throw; }
}
LL_TYPE_INSTANCE_HOOK(UseOn, ll::memory::HookPriority::Normal, GameMode,
    &GameMode::$useItemOn, InteractionResult, ItemStack& item, BlockPos const& pos, uchar face,
    Vec3 const& hit, HandSlot hand, Block const* target, bool first) {
    trace("use-item-on", static_cast<int>(hand));
    auto op = beginUse(mPlayer,hand);
    try { auto result = origin(item,pos,face,hit,hand,target,first); finishUse(op,result.mSuccess); return result; }
    catch (...) { if (pending == op) cancel(); throw; }
}
LL_TYPE_INSTANCE_HOOK(CompleteUse, ll::memory::HookPriority::Normal, Player,
    &Player::completeUsingItem, void) {
    trace("complete-use");
    auto op = beginUse(*this,HandSlot::Mainhand);
    try { origin(); finishUse(op,true); }
    catch (...) { if (pending == op) cancel(); throw; }
}
LL_TYPE_INSTANCE_HOOK(FocusLost, ll::memory::HookPriority::Normal, MinecraftGame,
    &MinecraftGame::$onAppFocusLost, void) { cancel(); origin(); }
// A legacy use transaction need not appear in the item-stack request batch.
// Observe this boundary without interpreting a send as server acceptance.
LL_TYPE_INSTANCE_HOOK(ComplexSend, ll::memory::HookPriority::Normal, LocalPlayer,
    &LocalPlayer::$sendComplexInventoryTransaction, void,
    std::unique_ptr<ComplexInventoryTransaction> transaction) {
    std::shared_ptr<Operation> observed;
    try { if (eligible() == this) {
        trace("complex-transaction-send-type",transaction ? static_cast<int>(transaction->mType) : -1);
        trace("complex-transaction-during-use",bool(pending && !pending->useFinished));
        auto op = pending;
        if (op) {
#ifdef LAMIUM_RESTOCK_TRACE
            // The spike's own NormalTransaction is not an unrelated mutation.
            bool ownMove = spikeSendInFlight;
#else
            bool ownMove = false;
#endif
            bool matches = false;
            if (transaction && transaction->mType == ComplexInventoryTransaction::Type::ItemUseTransaction) {
                auto const& use = static_cast<ItemUseInventoryTransaction const&>(*transaction);
                matches = use.mHand == HandSlot::Mainhand && use.mSlot == op->before.selected
                    && (use.mActionType == ItemUseInventoryTransaction::ActionType::Use
                        || use.mActionType == ItemUseInventoryTransaction::ActionType::Place);
            }
            if (matches && !op->useSent) observed = op;
            else if (!ownMove) cancel();
        }
    }} catch (...) { failure(); }
    try { origin(std::move(transaction)); }
    catch (...) { if (observed && pending == observed) cancel(); throw; }
    if (observed && pending == observed) observed->useSent = true;
}
LL_TYPE_INSTANCE_HOOK(Drop, ll::memory::HookPriority::Normal, Player,
    &Player::$drop, bool, ItemStack const& item, bool const randomly) {
    try { if (static_cast<Player*>(eligible()) == static_cast<Player*>(this)) cancel(); } catch (...) { failure(); }
    return origin(item,randomly);
}
#ifdef LAMIUM_RESTOCK_TRACE
// Observe the legacy inventory path without treating an arbitrary server update
// as acknowledgement of a use. Do not retain packet data or alter pending work.
// A server update that empties the selected slot is the only signal that a
// legacy use has actually executed server-side; the local hand was already
// cleared by client prediction before that.
void traceInventoryUpdate(char const* stage) noexcept {
    try {
        auto* player = eligible();
        if (!player) return;
        auto const& held = player->getInventory().getItem(player->mInventory->mSelected);
        bool empty = held.isNull() || held.mCount <= 0;
        trace(stage,empty ? 0 : static_cast<int>(held.mCount));
        auto op = pending;
        if (empty && op && op->useSent && !op->serverDepleted) {
            op->serverDepleted = true;
            trace("legacy-server-depleted");
        }
    } catch (...) {}
}
LL_TYPE_INSTANCE_HOOK(SlotUpdateTrace, ll::memory::HookPriority::Normal, LegacyClientNetworkHandler,
    &LegacyClientNetworkHandler::$handle, void,
    NetworkIdentifier const& source, InventorySlotPacket const& packet) {
    origin(source,packet);
    traceInventoryUpdate("legacy-slot-applied-held-count");
}
LL_TYPE_INSTANCE_HOOK(ContentUpdateTrace, ll::memory::HookPriority::Normal, LegacyClientNetworkHandler,
    &LegacyClientNetworkHandler::$handle, void,
    NetworkIdentifier const& source, InventoryContentPacket const& packet) {
    origin(source,packet);
    traceInventoryUpdate("legacy-content-applied-held-count");
}
#endif
struct Hook { int (*install)(bool); bool (*remove)(bool); bool installed = false; };
Hook hooks[] = {{CaptureHud::hook,CaptureHud::unhook},{Use::hook,Use::unhook},
    {UseOn::hook,UseOn::unhook},{CompleteUse::hook,CompleteUse::unhook},{FocusLost::hook,FocusLost::unhook},
    {ComplexSend::hook,ComplexSend::unhook},{Drop::hook,Drop::unhook},
#ifdef LAMIUM_RESTOCK_TRACE
    {SlotUpdateTrace::hook,SlotUpdateTrace::unhook},{ContentUpdateTrace::hook,ContentUpdateTrace::unhook},
#endif
};
}
void start() {
    try {
        for (auto& hook : hooks) if (!hook.installed) {
            if (hook.install(true) != 0) throw std::runtime_error("Could not install Hand Restock hook");
            hook.installed = true;
        }
        auto& bus = ll::event::EventBus::getInstance();
        if (!tickListener) tickListener = bus.emplaceListener<ll::event::ClientLevelTickEvent>([](auto&) { tick(); });
        if (!exitListener) exitListener = bus.emplaceListener<ll::event::ClientExitLevelEvent>([](auto&) { cancel(); hud.reset(); });
        if (!tickListener || !exitListener) throw std::runtime_error("Could not subscribe Hand Restock lifecycle");
    } catch (...) { stop(); throw; }
}
void stop() {
    cancel(); hud.reset();
    for (auto* listener : {&tickListener,&exitListener}) if (*listener) {
        ll::event::EventBus::getInstance().removeListener(*listener); listener->reset();
    }
    for (auto it = std::rbegin(hooks); it != std::rend(hooks); ++it)
        if (it->installed && it->remove(true)) it->installed = false;
}
}
