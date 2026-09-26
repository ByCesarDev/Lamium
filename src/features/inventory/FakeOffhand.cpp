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
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/world/actor/player/PlayerInventory.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/phys/HitResult.h"
#include <atomic>
#include <stdexcept>

namespace lamium::inventory::fakeOffhand {
namespace {
std::atomic_bool rightDown = false;
bool triggered = false;
bool synthetic = false;
bool installed = false;
bool dimensionInstalled = false;
SelectionSession selection;

void restore(LocalPlayer* player) {
    auto* inventory = player ? player->mInventory.get() : nullptr;
    auto slot = selection.finish(inventory ? inventory->mSelected : -1);
    if (slot && inventory && inventory->mSelectedContainerId == ContainerID::Inventory)
        inventory->selectSlot(*slot, ContainerID::Inventory);
}

bool nativeTrigger(Settings const& value) {
    return input::effectiveChord(value.bindings, input::Action::FakeOffhandUse)
        == input::defaultChord(input::Action::FakeOffhandUse);
}
LL_TYPE_INSTANCE_HOOK(BuildAction, ll::memory::HookPriority::Normal, ClientInstance,
    &ClientInstance::_tickBuildAction, void, HitResult const& solid, HitResult const& liquid, bool advanceTime) {
    auto& runtime = Runtime::instance();
    auto value = runtime.preferences();
    auto* player = getLocalPlayer();
    if (!runtime.enabled() || !value.inventory.fakeOffhand || !player || !player->isAlive()
        || ui::ownsInput() || !gameplayScreen(getScreenName())
        || Zoom::instance().blocksLookInteraction(*player)) {
        restore(player);
        origin(solid, liquid, advanceTime);
        return;
    }
    auto* inventory = player->mInventory.get();
    int selected = inventory ? inventory->mSelected : -1;
    if (selection.active()) {
        if (!selection.owns(selected)) selection.abandon();
        origin(solid, liquid, advanceTime);
        return;
    }
    if (selection.blocked()) { origin(solid, liquid, advanceTime); return; }
    int target = static_cast<int>(value.inventory.fakeOffhandSlot) - 1;
    bool active = nativeTrigger(value) ? rightDown.load() : triggered;
    bool blockItem = inventory && target >= 0 && target < 9 && !player->getInventory().getItem(target).isNull()
        && player->getInventory().getItem(target).mBlock;
    bool hitBlock = solid.mType == HitResultType::Tile;
    bool interactive = hitBlock && player->getDimensionBlockSource().getBlock(solid.mBlock)
        .getBlockType().isInteractiveBlock();
    auto slot = placementSlot(inventory && inventory->mSelectedContainerId == ContainerID::Inventory,
        active, selected, target, blockItem, hitBlock, interactive, player->isSneaking());
    if (!slot || !inventory->selectSlot(*slot, ContainerID::Inventory)) {
        origin(solid, liquid, advanceTime);
        return;
    }
    selection.begin(selected, *slot);
    try { origin(solid, liquid, advanceTime); }
    catch (...) { restore(player); throw; }
}
LL_TYPE_INSTANCE_HOOK(DimensionChange, ll::memory::HookPriority::Normal, LevelRendererPlayer,
    &LevelRendererPlayer::$onWillChangeDimension, void, Player& player) {
    release();
    origin(player);
}
}
void rawRightButton(bool down) { rightDown.store(down); }
void press(IClientInstance& client) {
    auto value = Runtime::instance().preferences();
    if (!value.inventory.fakeOffhand || nativeTrigger(value) || triggered) return;
    triggered = true;
    auto chord = input::effectiveChord(value.bindings, input::Action::FakeOffhandUse);
    if (!chord.empty() && chord.back() == input::Token{input::Device::Mouse, 2}) return;
    synthetic = interaction::periodic::sendUseEdge(client, true);
    if (!synthetic) triggered = false;
}
void release() {
    if (synthetic)
        if (auto client = ll::service::getClientInstance()) interaction::periodic::sendUseEdge(*client, false);
    auto client = ll::service::getClientInstance();
    restore(client ? client->getLocalPlayer() : nullptr);
    synthetic = triggered = false;
}
void start() {
    if (installed) return;
    if (BuildAction::hook(true) != 0) throw std::runtime_error("Could not install Fake Offhand hook");
    installed = true;
    if (DimensionChange::hook(true) != 0) { stop(); throw std::runtime_error("Could not install Fake Offhand dimension hook"); }
    dimensionInstalled = true;
}
void stop() {
    release();
    rightDown.store(false);
    if (dimensionInstalled && DimensionChange::unhook(true)) dimensionInstalled = false;
    if (installed && BuildAction::unhook(true)) installed = false;
}
}
