#include "features/inventory/OffhandSwap.h"
#include "features/inventory/OffhandSwapPlan.h"
#include "features/inventory/game/InventoryMove.h"
#include "app/Runtime.h"
#include "input/Actions.h"
#include "ui/SettingsScreen.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/PlayerInventory.h"
#include "mc/world/ContainerID.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include <set>
#include <string>

namespace lamium::inventory::offhandSwap {
namespace {
bool empty(ItemStack const& stack) { return stack.isNull() || !stack.mItem || stack.mCount <= 0; }
// The item's own offhand flag (allow_off_hand). The inventory screen's
// validation is not exported for direct use.
bool fitsOffhand(ItemStack const& item) { return item.mItem->mAllowOffhand == Item::OffhandAllowed::Yes; }
// Items sent to the Fake Offhand slot, logged once per kind so the flag can be
// checked against the game's own offhand rule (L-94, not yet checked).
std::set<std::string> logged;
void note(ItemStack const& item) {
    auto name = item.getTypeName();
    if (logged.size() < 64 && logged.insert(name).second)
        Runtime::instance().self().getLogger().info("Offhand swap: {} (offhand flag {}) went to the Fake Offhand slot", name,
            static_cast<int>(item.mItem->mAllowOffhand));
}
}
void press() noexcept {
    try {
        auto& runtime = Runtime::instance();
        if (!runtime.enabled() || ui::ownsInput()) return;
        auto client = ll::service::getClientInstance();
        if (!client || !gameplayScreen(client->getScreenName())) return;
        auto* player = client->getLocalPlayer();
        // Every game mode with hands (maintainer 2026-10-06); a spectator has none.
        if (!player || player->isSpectator()) return;
        auto* supplies = player->mInventory.get();
        if (!supplies || supplies->mSelectedContainerId != ContainerID::Inventory) return;
        int selected = supplies->mSelected;
        if (selected < 0 || selected >= 9) return;
        auto prefs = runtime.preferences();
        if (!prefs.inventory.offhandSwap) return;
        game::Location hand{game::Place::Inventory, selected}, offhand{game::Place::Offhand, 0};
        ItemStack held = game::itemAt(*player, hand), second = game::itemAt(*player, offhand);
        OffhandSwapState state;
        state.selected = selected;
        state.handEmpty = empty(held);
        state.handFitsOffhand = !state.handEmpty && fitsOffhand(held);
        state.handPrefersFake = !state.handEmpty && prefs.inventory.offhandSwapFireworks
            && held.getTypeName() == "minecraft:firework_rocket";
        state.offhandEmpty = empty(second);
        if (prefs.inventory.fakeOffhand) state.fakeSlot = prefs.inventory.fakeOffhandSlot - 1;
        ItemStack fake = state.fakeSlot ? game::itemAt(*player, {game::Place::Inventory, *state.fakeSlot}) : ItemStack{};
        state.fakeSlotEmpty = empty(fake);
        switch (planOffhandSwap(state)) {
        case OffhandSwap::RealOffhand: game::movePair(*player, hand, second, offhand, held); break;
        case OffhandSwap::FakeSlot:
            if (!state.handEmpty) note(held);
            game::movePair(*player, hand, fake, {game::Place::Inventory, *state.fakeSlot}, held);
            break;
        default: break;
        }
    } catch (std::exception const& error) {
        static bool reported = false;
        if (!reported) { Runtime::instance().self().getLogger().error("Offhand swap failed: {}", error.what()); reported = true; }
    } catch (...) {}
}
}
