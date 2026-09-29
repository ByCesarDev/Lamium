#include "features/inventory/game/InventoryMove.h"
#include "app/Runtime.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/PlayerInventory.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/inventory/network/ItemStackNetManagerClient.h"
#include "mc/world/inventory/transaction/InventoryAction.h"
#include "mc/world/inventory/transaction/InventorySource.h"
#include "mc/world/inventory/transaction/InventoryTransaction.h"
#include "mc/world/inventory/transaction/InventoryTransactionManager.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include <atomic>
#include <stdexcept>

namespace lamium::inventory::game {
namespace {
bool active = false;
void trace(char const* stage, int value) noexcept {
#ifdef LAMIUM_RESTOCK_TRACE
    try {
        static std::atomic<unsigned> samples{};
        if (samples.fetch_add(1) < 2048)
            Runtime::instance().self().getLogger().info("Move: {} value={}",stage,value);
    } catch (...) {}
#else
    (void)stage; (void)value;
#endif
}
ContainerID container(Place place) {
    return place == Place::Offhand ? ContainerID::Offhand : place == Place::Armor ? ContainerID::Armor : ContainerID::Inventory;
}
void set(LocalPlayer& player, Location at, ItemStack const& item) {
    switch (at.place) {
    case Place::Inventory: player.getInventory().$setItem(at.slot,item); break;
    case Place::Offhand: player.setOffhandSlot(item); break;
    case Place::Armor: player.setArmor(static_cast<SharedTypes::Legacy::ArmorSlot>(at.slot),item); break;
    }
}
bool recorded(InventoryTransaction const& transaction, ContainerID id) {
    for (auto const& [source, actions] : transaction.mActions.get())
        if (source.mType == InventorySourceType::ContainerInventory && source.mContainerId == id && !actions.empty())
            return true;
    return false;
}
}
ItemStack const& itemAt(LocalPlayer& player, Location at) {
    switch (at.place) {
    case Place::Offhand: return player.getOffhandSlot();
    case Place::Armor: return player.getArmor(static_cast<SharedTypes::Legacy::ArmorSlot>(at.slot));
    default: return player.getInventory().getItem(at.slot);
    }
}
bool moving() { return active; }
bool movePair(LocalPlayer& player, Location a, ItemStack const& newA, Location b, ItemStack const& newB) {
    if (active) return false;
    auto& manager = player.mTransactionManager.get();
    auto* base = player.mItemStackNetManager.get();
    if (manager.mCurrentTransaction.get()) { trace("transaction-busy",0); return false; }
    if (!base || !base->mIsEnabled || !base->mIsClientSide) { trace("manager-unavailable",0); return false; }
    if (static_cast<ItemStackNetManagerClient*>(base)->mRequest) { trace("request-busy",0); return false; }
    ItemStack oldA = itemAt(player,a), oldB = itemAt(player,b);
    struct Active { Active() { active = true; } ~Active() { active = false; } } guard;
    auto scope = ItemStackNetManagerBase::_tryBeginClientLegacyTransactionRequest(&player);
    if (!base->mLegacyTransactionRequestId->mRawId) { trace("no-legacy-scope",0); return false; }
    set(player,a,newA);
    set(player,b,newB);
    // The inventory setters record their own actions (L-66). Offhand and armor
    // setters are not established to; record a missing side explicitly so the
    // transaction balances instead of leaving a half-recorded move.
    if (auto* transaction = manager.mCurrentTransaction.get()) {
        for (auto [at, from, to] : {std::tuple{a,&oldA,&newA}, std::tuple{b,&oldB,&newB}}) {
            bool own = recorded(*transaction,container(at.place));
            trace(at.place == Place::Inventory ? "recorded-inventory" : at.place == Place::Offhand ? "recorded-offhand" : "recorded-armor",own);
            if (own || at.place == Place::Inventory) continue;
            InventorySource source;
            source.mType = InventorySourceType::ContainerInventory;
            source.mContainerId = container(at.place);
            source.mFlags = InventorySource::InventorySourceFlags::NoFlag;
            manager.addAction(InventoryAction{source,static_cast<uint>(at.place == Place::Offhand ? 0 : at.slot),*from,*to},false);
        }
    } else trace("recorded-nothing-pending",0);
    if (manager.mCurrentTransaction.get()) player.updateInventoryTransactions();
    if (manager.mCurrentTransaction.get()) throw std::runtime_error("Inventory move transaction remained unbalanced");
    trace("sent",static_cast<int>(a.place) * 10 + static_cast<int>(b.place));
    return true;
}
}
