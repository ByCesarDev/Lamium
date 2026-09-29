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
    Clock::time_point deadline = Clock::now() + std::chrono::seconds(1);
};
std::shared_ptr<Operation> pending;
ll::event::ListenerPtr tickListener, exitListener;
void trace(char const* stage, int value = 0) noexcept {
#ifdef LAMIUM_RESTOCK_TRACE
    try {
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
void cancel() {
    auto old = std::move(pending);
    if (old) releaseToken(*old);
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
std::shared_ptr<Operation> beginUse(Player& actor, HandSlot hand, bool placement) noexcept {
    try {
        if (applying || hand != HandSlot::Mainhand || (pending && pending->callbackActive)) return {};
        auto* player = eligible();
        auto controller = hud.lock();
        if (!player || player != &actor || !controller || !owned(*controller,*player)) return {};
        // Repeated use callbacks while eating are not new consumptions. The
        // completion callback and matching release close that operation.
        if (pending && pending->evidence.timed && !pending->evidence.completed) return {};
        cancel();
        auto const& held = player->getInventory().getItem(player->mInventory->mSelected);
        if (held.isNull() || held.mCount <= 0 || !held.mItem || held.mItem->getMaxDamage() > 0) return {};
        auto op = std::make_shared<Operation>();
        op->controller = controller;
        op->playerId = player->getRuntimeID().rawID;
        op->dimension = static_cast<int>(player->getDimensionId());
        op->before = snapshot(*op,*controller,*player);
        if (op->before.slots[op->before.selected].locked) return {};
        op->maxStack = held.getMaxStackSize();
        op->remainder = restockRemainder(held.getTypeName());
        op->hotbarSources = Runtime::instance().preferences().inventory.restockFromHotbar;
        op->evidence.tick = tickSerial;
        op->evidence.placement = placement;
        op->token = game::beginTransfer(*controller);
        if (!op->token) return {};
        pending = op;
        return op;
    } catch (...) { failure(); return {}; }
}
void finishUse(std::shared_ptr<Operation> const& op, bool success) noexcept {
    if (!op || pending != op) return;
    try {
        auto* player = eligible();
        auto controller = op->controller.lock();
        if (!success || !player || !controller || !current(*op,*player,*controller)) { cancel(); return; }
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
    if (manager.mCurrentTransaction.get() || !base || !base->mIsEnabled || !base->mIsClientSide) return false;
    auto& net = *static_cast<ItemStackNetManagerClient*>(base);
    if (net.mRequest) return false;
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
    if (!base->mLegacyTransactionRequestId->mRawId) return false;
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
        if (!player || !controller || !current(*op,*player,*controller)) { cancel(); return; }
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
            if (now != op->before) cancel();
            return;
        }
        if (!op->token) { cancel(); return; }
        auto response = game::transferResult(*op->token);
        if (response == ResponseBarrier::Result::Waiting) return;
        bool accepted = response == ResponseBarrier::Result::Accepted;
        bool legacy = response == ResponseBarrier::Result::Untracked && op->evidence.ready();
        if (!accepted && !legacy) { cancel(); return; }
        if (now == op->before) return; // Delayed depletion; time never authorizes a move.
        int remainderKind = -1;
        auto const& left = now.slots[now.selected];
        if (!left.empty() && !op->remainder.empty()
            && op->kinds[left.kind].getTypeName() == op->remainder) remainderKind = left.kind;
        auto plan = planRestock(op->before,now,true,op->maxStack,op->hotbarSources,remainderKind);
        if (!plan) { cancel(); return; }
        releaseToken(*op);
        op->token = game::beginTransfer(*controller);
        if (!op->token || !plan->stillValid(snapshot(*op,*controller,*player))) { cancel(); return; }
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
    bool secondary = previous && !previous->callbackActive && previous->evidence.placement
        && previous->evidence.tick == tickSerial && !previous->evidence.secondary
        && hand == HandSlot::Mainhand;
    std::optional<RestockSnapshot> beforeSecondary;
    if (secondary) try {
        auto* player = eligible(); auto controller = previous->controller.lock();
        if (player != &mPlayer || !controller || !current(*previous,*player,*controller)) secondary = false;
        else beforeSecondary = snapshot(*previous,*controller,*player);
    } catch (...) { failure(); secondary = false; }
    if (secondary) {
        bool result;
        try { result = origin(item,hand); }
        catch (...) { cancel(); throw; }
        try {
            auto* player = eligible(); auto controller = previous->controller.lock();
            if (pending == previous && (result || !player || !controller
                || !current(*previous,*player,*controller)
                || snapshot(*previous,*controller,*player) != *beforeSecondary)) cancel();
        } catch (...) { failure(); }
        return result;
    }
    auto op = beginUse(mPlayer,hand,false);
    try { bool result = origin(item,hand); finishUse(op,result); return result; }
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
        if (pending && pending->callbackActive && static_cast<Player*>(eligible()) == static_cast<Player*>(this)) {
            pending->evidence.timed = true;
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
        auto* player = eligible();
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
            if (op->predicted || !op->evidence.observe(send,tickSerial,sameSlot)) cancel();
            else observed = op;
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
void inventoryUpdated() noexcept {
    try {
        if (!pending) return;
        if (pending->predicted || applying) { trace("server-update-after-move"); cancel(); return; }
        auto* player = eligible(); auto controller = pending->controller.lock();
        if (!player || !controller || !current(*pending,*player,*controller)) { cancel(); return; }
        // A matching post-use server snapshot can be observed by the planner.
        // An unrelated change never opens another source or a retry.
        auto now = snapshot(*pending,*controller,*player);
        if (!onlyHandChanged(pending->before,now)) cancel();
    } catch (...) { failure(); }
}
LL_TYPE_INSTANCE_HOOK(SlotUpdate, ll::memory::HookPriority::Normal, LegacyClientNetworkHandler,
    &LegacyClientNetworkHandler::$handle, void,
    NetworkIdentifier const& source, InventorySlotPacket const& packet) {
    origin(source,packet);
    if (packet.mInventoryId == ContainerID::Inventory) inventoryUpdated();
}
LL_TYPE_INSTANCE_HOOK(ContentUpdate, ll::memory::HookPriority::Normal, LegacyClientNetworkHandler,
    &LegacyClientNetworkHandler::$handle, void,
    NetworkIdentifier const& source, InventoryContentPacket const& packet) {
    origin(source,packet);
    if (packet.mInventoryId == ContainerID::Inventory) inventoryUpdated();
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
