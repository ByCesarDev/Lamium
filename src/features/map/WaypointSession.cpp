#include "features/map/WaypointSession.h"
#include "features/map/WaypointStore.h"
#include "overlay/LocalShapePath.h"
#include "app/Runtime.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/event/client/ClientJoinLevelEvent.h"
#include "ll/api/event/client/ClientStartJoinLevelEvent.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/game/IMinecraftGame.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/core/utility/FilePathManager.h"
#include "mc/network/GameConnectionInfo.h"
#include "mc/world/level/Level.h"
#include <atomic>
#include <cmath>
#include <mutex>

namespace lamium::map::waypoints {
namespace {
std::mutex mutex;
WaypointSet set;
std::optional<std::filesystem::path> destination;
bool loadFailed = false; // Never overwrite a file that could not be read.
std::optional<DeathPoint> pendingDeath;
DeathWatch deathWatch;
std::atomic<bool> joiningLocal{false};
ll::event::ListenerPtr startJoinListener, joinListener, exitListener;

void log(std::string const& text) {
    try { Runtime::instance().self().getLogger().info("Waypoints: {}", text); } catch (...) {}
}
std::optional<std::filesystem::path> resolve(ll::event::ClientJoinLevelEvent& event) {
    auto& client = event.self();
    if (joiningLocal) {
        auto paths = client.getMinecraftGame_DEPRECATED().getFilePathManager();
        return overlay::localWorldFile(std::filesystem::u8path(paths->mWorlds->value),
                                       event.player().getLevel().getLevelId(), "waypoints.json");
    }
    auto connection = client.getGameConnectionInfo();
    if (!connection) return std::nullopt;
    std::string host = connection->mUnresolvedUrl->empty() ? *connection->mHostIpAddress : *connection->mUnresolvedUrl;
    auto file = serverFileName(host, connection->mPort);
    if (file.empty()) return std::nullopt;
    return Runtime::instance().self().getConfigDir() / "waypoints" / std::filesystem::path(file);
}
void join(ll::event::ClientJoinLevelEvent& event) noexcept {
    try {
        if (event.self().getLocalPlayer() != &event.player()) return;
        std::lock_guard lock(mutex);
        set = {};
        destination.reset();
        loadFailed = false;
        pendingDeath.reset();
        deathWatch.reset();
        try {
            destination = resolve(event);
            if (destination && std::filesystem::exists(*destination)) set = readWaypoints(*destination);
        } catch (std::exception const& error) {
            loadFailed = true;
            log(std::string("could not load: ") + error.what());
        }
    } catch (...) {}
}
void leave() {
    std::lock_guard lock(mutex);
    set = {};
    destination.reset();
    loadFailed = false;
    pendingDeath.reset();
    deathWatch.reset();
}
// Saves a candidate, then publishes it; the caller holds the lock.
bool commit(WaypointSet candidate) {
    if (loadFailed) return false;
    if (destination) {
        try { writeWaypoints(*destination, candidate); }
        catch (std::exception const& error) { log(std::string("could not save: ") + error.what()); return false; }
    }
    set = std::move(candidate);
    return true;
}
}
WaypointSet current() {
    std::lock_guard lock(mutex);
    return set;
}
bool add(Waypoint waypoint) {
    std::lock_guard lock(mutex);
    if (set.waypoints.size() >= maxWaypoints) return false;
    auto candidate = set;
    waypoint.color = clampColor(waypoint.color);
    if (waypoint.name.size() > maxNameBytes) waypoint.name.resize(maxNameBytes);
    candidate.lastColor = waypoint.color;
    candidate.waypoints.push_back(std::move(waypoint));
    return commit(std::move(candidate));
}
void watchDeath(IClientInstance& client) {
    auto* player = client.getLocalPlayer();
    if (!player) return;
    bool alive = player->isAlive();
    std::lock_guard lock(mutex);
    if (!deathWatch.update(alive)) return;
    auto feet = player->getFeetPos();
    if (!std::isfinite(feet.x) || !std::isfinite(feet.y) || !std::isfinite(feet.z)) return;
    pendingDeath = DeathPoint{static_cast<int>(std::floor(feet.x)), static_cast<int>(std::floor(feet.y)),
                              static_cast<int>(std::floor(feet.z)), static_cast<int>(player->getDimensionId())};
}
void frame(bool recordDeath) {
    std::lock_guard lock(mutex);
    if (!pendingDeath) return;
    auto death = *std::exchange(pendingDeath, std::nullopt);
    if (!recordDeath) return;
    auto candidate = set;
    candidate.death = death;
    commit(std::move(candidate));
}
void start() {
    auto& bus = ll::event::EventBus::getInstance();
    startJoinListener = bus.emplaceListener<ll::event::ClientStartJoinLevelEvent>(
        [](auto& event) { leave(); joiningLocal = event.isJoiningLocalServer(); });
    joinListener = bus.emplaceListener<ll::event::ClientJoinLevelEvent>(join);
    exitListener = bus.emplaceListener<ll::event::ClientExitLevelEvent>([](auto&) { leave(); });
    if (!startJoinListener || !joinListener || !exitListener) {
        stop();
        throw std::runtime_error("Could not subscribe waypoint world changes");
    }
}
void stop() {
    for (auto* listener : {&startJoinListener, &joinListener, &exitListener})
        if (*listener) {
            ll::event::EventBus::getInstance().removeListener(*listener);
            listener->reset();
        }
    leave();
}
}
