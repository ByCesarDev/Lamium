#include "features/map/PlayerLocationTrace.h"
#ifdef LAMIUM_RESEARCH_TRACE
#include "app/Runtime.h"
#include "app/TraceLog.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ClientLevelTickEvent.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/actor/player/PlayerListEntry.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/PlayerLocationReceiver.h"
#include <atomic>
#include <cmath>
#include <stdexcept>

namespace lamium::map::locationTrace {
namespace {
TraceBudget dumpBudget, updateBudget, hideBudget;
std::atomic<unsigned> updates{0}, hides{0};
ll::event::ListenerPtr tickListener;
unsigned ticks = 0;
bool hooked = false;
std::string nameOf(Level const& level, ActorUniqueID id) {
    for (auto const& [uuid, entry] : level.getPlayerList())
        if (entry.mId->rawID == id.rawID) return *entry.mName;
    return "?";
}
// Every 2 s: each entry with its listed name, next to the loaded player.
void dump() noexcept {
    try {
        auto client = ll::service::getClientInstance();
        auto* self = client ? client->getLocalPlayer() : nullptr;
        if (!self) return;
        auto& level = self->getLevel();
        auto receiver = level.getPlayerLocationReceiver();
        if (!receiver) { traceLog(dumpBudget, 400, "L-89 no receiver"); return; }
        auto const& data = *receiver->mCurrentPlayerLocationData;
        traceLog(dumpBudget, 400, "L-89 tick: entries={} updates={} hides={} listed={} self={} dim={} at ({:.1f} {:.1f} {:.1f})",
            data.keys().size(), updates.exchange(0), hides.exchange(0), level.getPlayerList().size(), self->getOrCreateUniqueID().rawID,
            static_cast<int>(self->getDimensionId()), self->getPosition().x, self->getPosition().y, self->getPosition().z);
        int shown = 0;
        for (auto const& entry : data) {
            if (++shown > 8) break;
            ActorUniqueID id = entry.first;
            std::optional<Vec3> const& pos = entry.second;
            auto* actor = level.getPlayer(id);
            std::string where = pos ? std::format("({:.2f} {:.2f} {:.2f})", pos->x, pos->y, pos->z) : std::string{"hidden"};
            std::string loaded = actor ? std::format("loaded ({:.2f} {:.2f} {:.2f}) dim={}", actor->getPosition().x,
                actor->getPosition().y, actor->getPosition().z, static_cast<int>(actor->getDimensionId())) : std::string{"not loaded"};
            traceLog(dumpBudget, 400, "L-89   id={} name={} pos={} {}", id.rawID, nameOf(level, id), where, loaded);
        }
    } catch (std::exception const& error) {
        traceLog(dumpBudget, 400, "L-89 dump failed: {}", error.what());
    } catch (...) {}
}
LL_TYPE_INSTANCE_HOOK(UpdateHook, ll::memory::HookPriority::Normal, PlayerLocationReceiver,
    &PlayerLocationReceiver::updatePlayer, void, ActorUniqueID const& id, Vec3 const& pos) {
    ++updates;
    traceLog(updateBudget, 40, "L-89 update id={} ({:.2f} {:.2f} {:.2f})", id.rawID, pos.x, pos.y, pos.z);
    origin(id, pos);
}
LL_TYPE_INSTANCE_HOOK(HideHook, ll::memory::HookPriority::Normal, PlayerLocationReceiver,
    &PlayerLocationReceiver::hidePlayer, void, ActorUniqueID const& id) {
    ++hides;
    traceLog(hideBudget, 100, "L-89 hide id={}", id.rawID);
    origin(id);
}
}
void start() {
    if (UpdateHook::hook(true) != 0 || HideHook::hook(true) != 0) { stop(); throw std::runtime_error("Could not install L-89 diagnostics"); }
    hooked = true;
    tickListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::ClientLevelTickEvent>([](auto&) {
        if (++ticks % 40 == 0) dump();
    });
    Runtime::instance().self().getLogger().warn("L-89 player location diagnostics enabled");
}
void stop() {
    if (tickListener) { ll::event::EventBus::getInstance().removeListener(tickListener); tickListener.reset(); }
    if (hooked) { UpdateHook::unhook(true); HideHook::unhook(true); hooked = false; }
}
}
#else
namespace lamium::map::locationTrace { void start() {} void stop() {} }
#endif
