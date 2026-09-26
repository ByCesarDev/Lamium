#include "features/inventory/FakeOffhand.h"
#include "features/inventory/FakeOffhandPlan.h"
#include "features/interaction/PeriodicInput.h"
#include "features/camera/Zoom.h"
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

bool endsOnRightClick(Settings const& value) {
    auto chord = input::effectiveChord(value.bindings, input::Action::FakeOffhandUse);
    return !chord.empty() && chord.back() == input::Token{input::Device::Mouse, 2};
}
std::optional<int> chooseSlot(ClientInstance& client, HitResult const& solid, int& selected) {
    auto* player = client.getLocalPlayer();
    if (!Runtime::instance().enabled() || !player || !player->isAlive()
        || ui::ownsInput() || !gameplayScreen(client.getScreenName())
        || Zoom::instance().blocksLookInteraction(*player)) return {};
    auto* inventory = player->mInventory.get();
    if (!inventory) return {};
    selected = inventory->mSelected;
    int target = targetSlot.load();
    bool blockItem = target >= 0 && target < 9 && !player->getInventory().getItem(target).isNull()
        && player->getInventory().getItem(target).mBlock;
    bool hitBlock = solid.mType == HitResultType::Tile;
    bool interactive = hitBlock && player->getDimensionBlockSource().getBlock(solid.mBlock)
        .getBlockType().isInteractiveBlock();
    return placementSlot(inventory->mSelectedContainerId == ContainerID::Inventory, true,
        selected, target, blockItem, hitBlock, interactive, player->isSneaking());
}
// Restores the prior selection even if the vanilla build action unwinds.
struct SelectionRestore {
    PlayerInventory& inventory;
    int slot, previous;
    ~SelectionRestore() {
        if (inventory.mSelected == slot) inventory.selectSlot(previous, ContainerID::Inventory);
    }
};
LL_TYPE_INSTANCE_HOOK(BuildAction, ll::memory::HookPriority::Normal, ClientInstance,
    &ClientInstance::_tickBuildAction, void, HitResult const& solid, HitResult const& liquid, bool advanceTime) {
    if (!enabled.load() || !(rightChordHeld.load() || synthetic.load())) {
        origin(solid, liquid, advanceTime);
        return;
    }
    std::optional<int> slot;
    int selected = -1;
    PlayerInventory* inventory = nullptr;
    try {
        slot = chooseSlot(*this, solid, selected);
        if (slot) {
            inventory = getLocalPlayer()->mInventory.get();
            if (!inventory->selectSlot(*slot, ContainerID::Inventory)) slot.reset();
        }
    } catch (...) { slot.reset(); }
    if (!slot) {
        origin(solid, liquid, advanceTime);
        return;
    }
    SelectionRestore restore{*inventory, *slot, selected};
    origin(solid, liquid, advanceTime);
}
}
void configure(Settings const& value) {
    enabled.store(value.inventory.fakeOffhand);
    targetSlot.store(value.inventory.fakeOffhandSlot - 1);
}
void rightChord(bool held) { rightChordHeld.store(held); }
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
    if (BuildAction::hook(true) != 0) throw std::runtime_error("Could not install Fake Offhand hook");
    installed = true;
}
void stop() {
    release();
    rightChordHeld.store(false);
    if (installed && BuildAction::unhook(true)) installed = false;
}
}
