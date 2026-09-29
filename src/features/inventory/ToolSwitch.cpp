#include "features/inventory/ToolSwitch.h"
#include "features/interaction/BreakingRestriction.h"
#include "features/inventory/ToolChoice.h"
#include "features/inventory/EquipmentPlan.h"
#include "features/inventory/game/InventoryMove.h"
#include "app/Runtime.h"
#include "ui/SettingsScreen.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/actor/player/PlayerInventory.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/item/Item.h"
#include <algorithm>
#include <stdexcept>

namespace lamium::inventory::tools {
namespace {
bool installed = false;
ToolTarget target;
void selectTool(Player& player, BlockPos const& pos, bool starting) {
    if (!interaction::breaking::allows(player,pos)) return;
    auto& runtime = Runtime::instance();
    if (!runtime.enabled() || !runtime.preferences().inventory.toolSwitch || ui::ownsInput()) return;
    auto client = ll::service::getClientInstance();
    if (!client || client->getLocalPlayer() != &player || player.isCreative() || player.isSpectator()) return;
    auto* supplies = player.mInventory.get();
    if (!supplies || supplies->mSelectedContainerId != ContainerID::Inventory) return;
    int selected = supplies->mSelected;
    if (selected < 0 || selected >= 9) return;
    auto const& block = player.getDimensionBlockSource().getBlock(pos);
    bool requiresTool = block.getBlockType().mRequiresCorrectToolForDrops;
    bool fetch = starting && runtime.preferences().inventory.toolSwitchInventory;
    std::array<ToolCandidate,36> candidates;
    for (int slot=0; slot<(fetch ? 36 : 9); ++slot) {
        auto const& stack = player.getInventory().getItem(slot);
        if (stack.isNull() || !stack.mItem) continue;
        candidates[slot] = {stack.mItem->getDestroySpeed(stack,block),
            !requiresTool || stack.mItem->canDestroySpecial(block)};
        // Never fetch a tool that is about to break (L-62 would swap it back).
        if (slot >= 9 && aboutToBreak(stack.mItem->getMaxDamage(),stack.getDamageValue())) candidates[slot] = {};
    }
    std::array<ToolCandidate,9> hotbar;
    std::copy_n(candidates.begin(),9,hotbar.begin());
    if (auto slot = chooseHotbarTool(hotbar,selected)) {
        supplies->selectSlot(*slot,ContainerID::Inventory);
        return;
    }
    // L-69: only on a new press, never while a held attack moves between
    // blocks, so the move is not sent right behind the last break.
    if (!fetch) return;
    if (auto source = chooseInventoryTool(candidates,selected)) {
        auto* local = client->getLocalPlayer();
        ItemStack held = player.getInventory().getItem(selected), tool = player.getInventory().getItem(*source);
        game::movePair(*local,{game::Place::Inventory,selected},tool,{game::Place::Inventory,*source},held);
    }
}
bool clientPlayer(Player const& player) {
    auto client = ll::service::getClientInstance();
    return client && client->getLocalPlayer() == &player;
}
void choose(Player& player, BlockPos const& pos, bool starting) {
    // In a local world the integrated server's player breaks the same blocks,
    // possibly on another thread; only the client's own player is tracked.
    if (!clientPlayer(player)) return;
    try {
        bool moved = target.enter({pos.x, pos.y, pos.z});
        if (starting || moved) selectTool(player,pos,starting);
    } catch (std::exception const& error) {
        static bool reported = false;
        if (!reported) { Runtime::instance().self().getLogger().error("Tool selection failed: {}",error.what()); reported = true; }
    }
}
LL_TYPE_INSTANCE_HOOK(ToolSwitchStart, ll::memory::HookPriority::Normal, GameMode,
    &GameMode::$startDestroyBlock, bool, BlockPos const& pos, uchar face, bool& destroyed) {
    choose(mPlayer,pos,true);
    return origin(pos,face,destroyed);
}
// Holding the attack button across blocks continues breaking on the new block
// without a new start, so choose again when the position changes.
LL_TYPE_INSTANCE_HOOK(ToolSwitchContinue, ll::memory::HookPriority::Normal, GameMode,
    &GameMode::$continueDestroyBlock, bool, BlockPos const& pos, uchar face, Vec3 const& playerPos, bool& destroyed) {
    choose(mPlayer,pos,false);
    return origin(pos,face,playerPos,destroyed);
}
LL_TYPE_INSTANCE_HOOK(ToolSwitchStop, ll::memory::HookPriority::Normal, GameMode,
    &GameMode::$stopDestroyBlock, void, BlockPos const& pos) {
    if (clientPlayer(mPlayer)) target.clear();
    origin(pos);
}
struct Hook { int (*install)(bool); bool (*remove)(bool); bool installed = false; };
Hook hooks[] = {{ToolSwitchStart::hook, ToolSwitchStart::unhook}, {ToolSwitchContinue::hook, ToolSwitchContinue::unhook},
    {ToolSwitchStop::hook, ToolSwitchStop::unhook}};
}
void start() {
    if (installed) return;
    for (auto& hook : hooks) if (!hook.installed) {
        if (hook.install(true) != 0) { stop(); throw std::runtime_error("Could not install tool switch hook"); }
        hook.installed = true;
    }
    installed = true;
}
void stop() {
    for (auto it = std::rbegin(hooks); it != std::rend(hooks); ++it)
        if (it->installed && it->remove(true)) it->installed = false;
    target.clear();
    installed = false;
}
}
