#include "features/inventory/HandRestock.h"
#include "features/inventory/RestockPlan.h"
#include "features/inventory/RestockUse.h"
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
#include "mc/client/network/LegacyClientNetworkHandler.h"
#include "mc/network/packet/InventorySlotPacket.h"
#include "mc/network/packet/InventoryContentPacket.h"
#include "mc/world/containers/managers/controllers/HudContainerManagerController.h"
#include "mc/world/containers/managers/models/ContainerManagerModel.h"
#include "mc/world/actor/player/PlayerInventory.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemLockHelper.h"
#include "mc/world/item/ItemLockMode.h"
#include "mc/world/item/HandSlot.h"
#include "mc/legacy/ActorRuntimeID.h"
#include "mc/world/inventory/network/ItemStackNetManagerClient.h"
#include "mc/world/inventory/transaction/ItemUseInventoryTransaction.h"
#include "mc/world/inventory/transaction/ItemReleaseInventoryTransaction.h"
#include <atomic>
#include <chrono>

namespace lamium::inventory::restock {
namespace {
using game::ResponseBarrier;
using Clock = std::chrono::steady_clock;
constexpr char collection[] = "hotbar_items";
std::weak_ptr<HudContainerManagerController> hud;
std::uint64_t generation = 1, tickSerial = 0;
bool applying = false, faulted = false;
struct Operation {
    std::weak_ptr<HudContainerManagerController> controller;
    std::uint64_t playerId;
    int dimension;
    RestockSnapshot before;
    std::vector<ItemStack> kinds;
    std::optional<game::TransferToken> token;
    RestockUseEvidence evidence;
    bool callbackActive = true;
    bool predicted = false;
    bool hotbarSources = false;
    int maxStack = 0;
    std::string remainder;
    RestockSnapshot expected;
    bool serverConfirmed = false;
    int duration = 0;
    int uses = 1;
    Clock::time_point deadline = Clock::now() + std::chrono::seconds(1);
};
std::shared_ptr<Operation> pending;
ll::event::ListenerPtr tickListener, exitListener;
void trace(char const* stage, int value = 0) noexcept {
#ifdef LAMIUM_RESTOCK_TRACE
    try {
        if (!Runtime::instance().preferences().inventory.handRestock) return;
        static std::atomic<unsigned> samples{};
        if (samples.fetch_add(1) < 512)
            Runtime::instance().self().getLogger().info("Restock: {} value={}",stage,value);
    } catch (...) {}
#else
    (void)stage; (void)value;
#endif
}
void releaseToken(Operation& op) {
    if (op.token) game::cancelTransfer(*op.token);
    op.token.reset();
}
void cancel(char const* reason = "cancel") {
    auto old = std::move(pending);
    if (old) { trace(reason); releaseToken(*old); }
}
void reset() { cancel(); ++generation; faulted = false; }
void failure() noexcept {
    cancel();
    // A setter may have partially executed. Never roll back or attempt another
    // move in this context after an exception; vanilla owns recovery.
    if (!faulted) {
        faulted = true;
        try { Runtime::instance().self().getLogger().error("Hand Restock stopped after an inventory error"); }
        catch (...) {}
    }
}
LocalPlayer* eligible() {
    auto& runtime = Runtime::instance();
    auto client = ll::service::getClientInstance();
    if (faulted || !runtime.enabled() || !runtime.preferences().inventory.handRestock || ui::ownsInput()
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
    result.context = generation;
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
bool current(Operation const& op, LocalPlayer& player, HudContainerManagerController& controller) {
    return op.before.context == generation && op.playerId == player.getRuntimeID().rawID
        && op.dimension == static_cast<int>(player.getDimensionId())
        && op.before.selected == player.mInventory->mSelected
        && op.hotbarSources == Runtime::instance().preferences().inventory.restockFromHotbar
        && hud.lock() == op.controller.lock() && owned(controller,player);
}
bool settling(std::shared_ptr<Operation> const& op) {
    return op && !op->callbackActive && !op->predicted && op->evidence.ready();
}
std::shared_ptr<Operation> beginUse(Player& actor, HandSlot hand, bool placement) noexcept {
    try {
        trace(placement ? "begin-place" : "begin-use",static_cast<int>(hand));
        if (applying || hand != HandSlot::Mainhand || (pending && pending->callbackActive)) {
            trace("begin-nested-or-offhand"); return {};
        }
        auto* player = eligible();
        auto controller = hud.lock();
        if (!player) { trace("begin-ineligible"); return {}; }
        if (player != &actor) { trace("begin-other-player"); return {}; }
        if (!controller) { trace("begin-no-hud"); return {}; }
        if (!owned(*controller,*player)) { trace("begin-hud-not-owned"); return {}; }
        // Repeated use callbacks while eating are not new consumptions. The
        // completion callback and matching release close that operation.
        if (pending && pending->evidence.timed && !pending->evidence.completed) {
            trace("begin-timed-pending"); return {};
        }
        if (settling(pending)) {
            auto op = pending;
            auto const& held = player->getInventory().getItem(player->mInventory->mSelected);
            // Holding use restarts eating before the server update arrives.
            // Starting to eat changes nothing, so the waiting refill proceeds;
            // the new use itself is not tracked.
            if (!held.isNull() && held.mItem && held.mItem->getMaxUseDuration(&held) > 0) {
                trace("begin-during-settle"); return {};
            }
            // Holding use throws or places again before the server confirms
            // the last use. Continue the same operation: the refill waits
            // until the server shows every tracked use (BDS eggs, 8939fd5).
            auto now = snapshot(*op,*controller,*player);
            auto const& used = op->before.slots[op->before.selected];
            auto const& left = now.slots[now.selected];
            if (current(*op,*player,*controller) && onlyHandChanged(op->before,now) && !left.empty()
                && left.kind == used.kind && left.count == used.count - op->uses && op->uses < 64) {
                ++op->uses;
                op->serverConfirmed = false;
                op->evidence = {tickSerial,placement};
                op->callbackActive = true;
                releaseToken(*op);
                op->token = game::beginTransfer(*controller);
                if (!op->token) { cancel("begin-token-unavailable"); return {}; }
                trace("begin-continued",op->uses);
                return op;
            }
        }
        cancel("begin-replaced-observation");
        auto const& held = player->getInventory().getItem(player->mInventory->mSelected);
        if (held.isNull() || held.mCount <= 0 || !held.mItem) { trace("begin-empty"); return {}; }
        trace("held-count",held.mCount);
        trace("held-max-damage",held.mItem->getMaxDamage());
        if (held.mItem->getMaxDamage() > 0) return {};
        auto op = std::make_shared<Operation>();
        op->controller = controller;
        op->playerId = player->getRuntimeID().rawID;
        op->dimension = static_cast<int>(player->getDimensionId());
        op->before = snapshot(*op,*controller,*player);
        if (op->before.slots[op->before.selected].locked) { trace("begin-locked"); return {}; }
        op->maxStack = held.getMaxStackSize();
        op->remainder = restockRemainder(held.getTypeName());
        op->hotbarSources = Runtime::instance().preferences().inventory.restockFromHotbar;
        op->evidence.tick = tickSerial;
        op->evidence.placement = placement;
        op->token = game::beginTransfer(*controller);
        if (!op->token) { trace("begin-token-unavailable"); return {}; }
        pending = op;
        trace("begin-captured",op->before.selected);
        return op;
    } catch (...) { failure(); return {}; }
}
void finishUse(std::shared_ptr<Operation> const& op, bool success) noexcept {
    if (!op || pending != op) return;
    try {
        auto* player = eligible();
        auto controller = op->controller.lock();
        trace("finish-success",success);
        if (!success || !player || !controller || !current(*op,*player,*controller)) { cancel("finish-invalid"); return; }
        if (op->token) game::endTransfer(*op->token);
        op->callbackActive = false;
        if (op->evidence.timed && !op->evidence.completed) {
            releaseToken(*op); // No request-manager pointer may survive eating.
        } else {
            op->deadline = Clock::now() + std::chrono::seconds(1);
        }
    } catch (...) { failure(); }
}

// The setters record the balanced action pair themselves. Flush only an
// actually pending transaction, never by observing our own send hook. No raw
// packet or hand-edited legacy metadata is involved.
bool applyMove(LocalPlayer& player, RestockPlan const& plan) {
    auto& inventory = player.getInventory();
    auto& manager = player.mTransactionManager.get();
    auto* base = player.mItemStackNetManager.get();
    if (manager.mCurrentTransaction.get()) { trace("move-transaction-busy"); return false; }
    if (!base || !base->mIsEnabled || !base->mIsClientSide) { trace("move-manager-unavailable"); return false; }
    auto& net = *static_cast<ItemStackNetManagerClient*>(base);
    if (net.mRequest) { trace("move-request-busy"); return false; }
    ItemStack source = inventory.getItem(plan.source);
    ItemStack destination = inventory.getItem(plan.destination);
    bool destinationEmpty = destination.isNull() || destination.mCount <= 0;
    bool exchange = !destinationEmpty && !source.matchesItem(destination);
    if (exchange) {
        std::swap(source,destination);
    } else {
        int amount = plan.destinationAfter.count - plan.beforeMove.slots[plan.destination].count;
        if (destinationEmpty) {
            destination = source;
            destination.remove(static_cast<int>(source.mCount) - amount);
        } else destination.add(amount);
        source.remove(amount);
        if (source.mCount <= 0) source = ItemStack{};
    }
    struct Applying { Applying() { applying = true; } ~Applying() { applying = false; } } guard;
    auto scope = ItemStackNetManagerBase::_tryBeginClientLegacyTransactionRequest(&player);
    if (!base->mLegacyTransactionRequestId->mRawId) { trace("move-no-legacy-scope"); return false; }
    inventory.$setItem(plan.source,source);
    inventory.$setItem(plan.destination,destination);
    if (manager.mCurrentTransaction.get()) player.updateInventoryTransactions();
    if (manager.mCurrentTransaction.get()) throw std::runtime_error("Restock transaction remained unbalanced");
    return true;
}
void tick() noexcept {
    ++tickSerial;
    if (!pending) return;
    try {
        auto op = pending;
        auto* player = eligible();
        auto controller = op->controller.lock();
        if (!player || !controller || !current(*op,*player,*controller)) { cancel("tick-context"); return; }
        if (op->callbackActive) return;
        if (Clock::now() >= op->deadline) { trace("observation-expired"); cancel(); return; }
        auto now = snapshot(*op,*controller,*player);
        if (op->predicted) {
            // This is a local consistency check, NOT server acknowledgement.
            // Corrections run through vanilla and cancel observation below.
            trace(now == op->expected ? "prediction-stable" : "prediction-changed");
            cancel(); return;
        }
        if (op->evidence.timed && !op->evidence.completed) {
            if (now != op->before) cancel("timed-inventory-changed");
            return;
        }
        if (!op->token) { cancel(); return; }
        auto response = game::transferResult(*op->token);
        if (response == ResponseBarrier::Result::Waiting) return;
        bool accepted = response == ResponseBarrier::Result::Accepted;
        bool legacy = response == ResponseBarrier::Result::Untracked && op->evidence.ready();
        if (!accepted && !legacy) {
            trace("use-response",static_cast<int>(response));
            trace("use-evidence",int(op->evidence.use) | (int(op->evidence.release) << 1)
                | (int(op->evidence.completed) << 2) | (int(op->evidence.timed) << 3));
            cancel("use-not-correlated"); return;
        }
        if (now == op->before) return; // Delayed depletion; time never authorizes a move.
        // The server runs a legacy use in its tick but a transfer on receipt; a
        // move sent before its update showing the consumption was rejected on
        // BDS (a41f0f2). Local worlds send that update too (6745b4b). Without
        // it the deadline cancels; elapsed time never authorizes a move.
        if (!op->serverConfirmed) return;
        int remainderKind = -1;
        auto const& left = now.slots[now.selected];
        if (!left.empty() && !op->remainder.empty()
            && op->kinds[left.kind].getTypeName() == op->remainder) remainderKind = left.kind;
        auto plan = planRestock(op->before,now,true,op->maxStack,op->hotbarSources,remainderKind,
            restockThreshold,op->uses);
        if (!plan) {
            trace("plan-held-before",op->before.slots[op->before.selected].count);
            trace("plan-held-after",left.count);
            trace("plan-max-stack",op->maxStack);
            cancel("no-plan"); return;
        }
        trace("plan-source",plan->source);
        releaseToken(*op);
        op->token = game::beginTransfer(*controller);
        if (!op->token || !plan->stillValid(snapshot(*op,*controller,*player))) { cancel("move-token-or-stale"); return; }
        if (!applyMove(*player,*plan)) { trace("move-refused"); cancel(); return; }
        if (pending != op) return;
        game::endTransfer(*op->token);
        op->expected = plan->predicted();
        if (snapshot(*op,*controller,*player) != op->expected) { failure(); return; }
        op->predicted = true;
        op->deadline = Clock::now() + std::chrono::seconds(1);
        trace("move-predicted",plan->destinationAfter.count);
    } catch (...) { failure(); }
}
LL_TYPE_INSTANCE_HOOK(CaptureHud, ll::memory::HookPriority::Normal, ClientInstanceScreenModel,
    &ClientInstanceScreenModel::createHudContainerManagerController, std::shared_ptr<HudContainerManagerController>) {
    auto result = origin(); reset(); hud = result; return result;
}
LL_TYPE_INSTANCE_HOOK(Use, ll::memory::HookPriority::Normal, GameMode,
    &GameMode::$useItem, bool, ItemStack& item, HandSlot hand) {
    // The secondary callback after placement must fail without another count
    // change. A successful/new use cancels the old observation instead.
    auto previous = pending;
    bool secondary = previous && !previous->callbackActive && hand == HandSlot::Mainhand
        && previous->evidence.beginSecondaryCallback(tickSerial);
    std::optional<RestockSnapshot> beforeSecondary;
    if (secondary) try {
        auto* player = eligible(); auto controller = previous->controller.lock();
        if (player != &mPlayer || !controller || !current(*previous,*player,*controller)) secondary = false;
        else beforeSecondary = snapshot(*previous,*controller,*player);
    } catch (...) { failure(); secondary = false; }
    if (secondary) {
        trace("secondary-callback");
        bool result;
        try { result = origin(item,hand); }
        catch (...) { cancel(); throw; }
        try {
            auto* player = eligible(); auto controller = previous->controller.lock();
            if (pending == previous && (result || !player || !controller
                || !current(*previous,*player,*controller)
                || snapshot(*previous,*controller,*player) != *beforeSecondary)) cancel("secondary-callback-changed");
        } catch (...) { failure(); }
        return result;
    }
    auto op = beginUse(mPlayer,hand,false);
    try { bool result = origin(item,hand); finishUse(op,restockUseStarted(result,op && op->evidence.timed)); return result; }
    catch (...) { if (op && pending == op) cancel(); throw; }
}
LL_TYPE_INSTANCE_HOOK(UseOn, ll::memory::HookPriority::Normal, GameMode,
    &GameMode::$useItemOn, InteractionResult, ItemStack& item, BlockPos const& pos, uchar face,
    Vec3 const& hit, HandSlot hand, Block const* target, bool first) {
    auto op = beginUse(mPlayer,hand,true);
    try { auto result = origin(item,pos,face,hit,hand,target,first); finishUse(op,result.mSuccess); return result; }
    catch (...) { if (op && pending == op) cancel(); throw; }
}
LL_TYPE_INSTANCE_HOOK(StartUse, ll::memory::HookPriority::Normal, Player,
    &Player::startUsingItem, void, ItemStack const& item, int duration) {
    try {
        trace("start-timed",pending ? int(pending->callbackActive) : -1);
        if (pending && pending->callbackActive && static_cast<Player*>(eligible()) == static_cast<Player*>(this)) {
            pending->evidence.timed = true;
            pending->duration = duration;
            pending->deadline = Clock::now() + std::chrono::milliseconds(std::clamp(duration,1,1200) * 50 + 1000);
        }
    } catch (...) { failure(); }
    origin(item,duration);
}
LL_TYPE_INSTANCE_HOOK(CompleteUse, ll::memory::HookPriority::Normal, Player,
    &Player::completeUsingItem, void) {
    std::shared_ptr<Operation> op;
    try {
        auto candidate = pending;
        trace("complete-timed",candidate ? int(candidate->evidence.timed) : -1);
        auto* player = eligible();
        if (candidate && candidate->evidence.timed && !candidate->evidence.completed
            && !restockTimedCompletion(tickSerial - candidate->evidence.tick,candidate->duration)) {
            trace("complete-too-early",static_cast<int>(tickSerial - candidate->evidence.tick));
            candidate.reset();
        }
        if (candidate && candidate->evidence.timed && !candidate->evidence.completed && static_cast<Player*>(player) == static_cast<Player*>(this)) {
            auto controller = candidate->controller.lock();
            if (controller && current(*candidate,*player,*controller)
                && snapshot(*candidate,*controller,*player) == candidate->before) {
                candidate->token = game::beginTransfer(*controller);
                if (candidate->token) {
                    op = candidate;
                    op->callbackActive = true;
                    op->evidence.completed = true;
                } else cancel();
            } else cancel();
        }
    } catch (...) { failure(); }
    try { origin(); finishUse(op,true); }
    catch (...) { if (op && pending == op) cancel(); throw; }
}
LL_TYPE_INSTANCE_HOOK(FocusLost, ll::memory::HookPriority::Normal, MinecraftGame,
    &MinecraftGame::$onAppFocusLost, void) { cancel(); ++generation; origin(); }
LL_TYPE_INSTANCE_HOOK(ComplexSend, ll::memory::HookPriority::Normal, LocalPlayer,
    &LocalPlayer::$sendComplexInventoryTransaction, void,
    std::unique_ptr<ComplexInventoryTransaction> transaction) {
    std::shared_ptr<Operation> observed;
    try {
        if (!applying && pending && static_cast<Player*>(eligible()) == static_cast<Player*>(this)) {
            auto op = pending;
            trace("send-type",transaction ? static_cast<int>(transaction->mType) : -1);
            RestockUseSend send = RestockUseSend::Other;
            bool sameSlot = false;
            if (transaction && transaction->mType == ComplexInventoryTransaction::Type::ItemUseTransaction) {
                auto const& use = static_cast<ItemUseInventoryTransaction const&>(*transaction);
                sameSlot = use.mHand == HandSlot::Mainhand && use.mSlot == op->before.selected;
                if (use.mActionType == ItemUseInventoryTransaction::ActionType::Place) send = RestockUseSend::Place;
                if (use.mActionType == ItemUseInventoryTransaction::ActionType::Use) send = RestockUseSend::Use;
            } else if (transaction && transaction->mType == ComplexInventoryTransaction::Type::ItemReleaseTransaction) {
                auto const& release = static_cast<ItemReleaseInventoryTransaction const&>(*transaction);
                sameSlot = release.mSlot == op->before.selected;
                send = RestockUseSend::Release;
            }
            trace("send-verb",static_cast<int>(send));
            trace("send-tick-distance",static_cast<int>(tickSerial - op->evidence.tick));
            if (settling(op)) trace("send-during-settle"); // Snapshot checks own later changes.
            else if (op->predicted || !op->evidence.observe(send,tickSerial,sameSlot)) cancel("send-not-correlated");
            else { observed = op; trace("send-correlated"); }
        }
    } catch (...) { failure(); }
    try { origin(std::move(transaction)); }
    catch (...) { if (observed && pending == observed) cancel(); throw; }
}
LL_TYPE_INSTANCE_HOOK(Drop, ll::memory::HookPriority::Normal, Player,
    &Player::$drop, bool, ItemStack const& item, bool const randomly) {
    try { if (static_cast<Player*>(eligible()) == static_cast<Player*>(this)) cancel(); } catch (...) { failure(); }
    return origin(item,randomly);
}
void inventoryUpdated(std::optional<int> slot) noexcept {
    try {
        if (!pending) return;
        if (pending->predicted || applying) { trace("server-update-after-move"); cancel(); return; }
        auto* player = eligible(); auto controller = pending->controller.lock();
        if (!player || !controller || !current(*pending,*player,*controller)) { cancel(); return; }
        // A matching post-use server snapshot can be observed by the planner.
        // An unrelated change never opens another source or a retry.
        auto now = snapshot(*pending,*controller,*player);
        if (!onlyHandChanged(pending->before,now)) { cancel("server-unrelated-change"); return; }
        // Only a server state that covers the held slot and already shows the
        // consumption means the server ran the use before our move.
        auto const& evidence = pending->evidence;
        bool covers = !slot || *slot == now.selected;
        // With continued uses, only a server state showing all of them counts.
        auto const& used = pending->before.slots[pending->before.selected];
        auto const& left = now.slots[now.selected];
        bool all = pending->uses == 1 || (left.empty() ? used.count == pending->uses
            : left.kind == used.kind && left.count == used.count - pending->uses);
        if (covers && all && now != pending->before && evidence.use && (!evidence.timed || evidence.completed)) {
            pending->serverConfirmed = true;
            trace("server-confirmed",now.slots[now.selected].count);
        } else trace("server-update-before-consumption",slot ? *slot : -1);
    } catch (...) { failure(); }
}
LL_TYPE_INSTANCE_HOOK(SlotUpdate, ll::memory::HookPriority::Normal, LegacyClientNetworkHandler,
    &LegacyClientNetworkHandler::$handle, void,
    NetworkIdentifier const& source, InventorySlotPacket const& packet) {
    origin(source,packet);
    if (packet.mInventoryId == ContainerID::Inventory) inventoryUpdated(static_cast<int>(packet.mSlot));
}
LL_TYPE_INSTANCE_HOOK(ContentUpdate, ll::memory::HookPriority::Normal, LegacyClientNetworkHandler,
    &LegacyClientNetworkHandler::$handle, void,
    NetworkIdentifier const& source, InventoryContentPacket const& packet) {
    origin(source,packet);
    if (packet.mInventoryId == ContainerID::Inventory) inventoryUpdated(std::nullopt);
}
struct Hook { int (*install)(bool); bool (*remove)(bool); bool installed = false; };
Hook hooks[] = {{CaptureHud::hook,CaptureHud::unhook},{Use::hook,Use::unhook},
    {UseOn::hook,UseOn::unhook},{StartUse::hook,StartUse::unhook},{CompleteUse::hook,CompleteUse::unhook},
    {FocusLost::hook,FocusLost::unhook},{ComplexSend::hook,ComplexSend::unhook},{Drop::hook,Drop::unhook},
    {SlotUpdate::hook,SlotUpdate::unhook},{ContentUpdate::hook,ContentUpdate::unhook}};
}
void start() {
    try {
        for (auto& hook : hooks) if (!hook.installed) {
            if (hook.install(true) != 0) throw std::runtime_error("Could not install Hand Restock hook");
            hook.installed = true;
        }
        auto& bus = ll::event::EventBus::getInstance();
        if (!tickListener) tickListener = bus.emplaceListener<ll::event::ClientLevelTickEvent>([](auto&) { tick(); });
        if (!exitListener) exitListener = bus.emplaceListener<ll::event::ClientExitLevelEvent>([](auto&) { reset(); hud.reset(); });
        if (!tickListener || !exitListener) throw std::runtime_error("Could not subscribe Hand Restock lifecycle");
    } catch (...) { stop(); throw; }
}
void stop() {
    reset(); hud.reset();
    for (auto* listener : {&tickListener,&exitListener}) if (*listener) {
        ll::event::EventBus::getInstance().removeListener(*listener); listener->reset();
    }
    for (auto it = std::rbegin(hooks); it != std::rend(hooks); ++it)
        if (it->installed && it->remove(true)) it->installed = false;
}
}
