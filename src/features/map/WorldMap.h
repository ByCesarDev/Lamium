#pragma once
#include "features/map/Waypoints.h"
#include "settings/Settings.h"
#include <glm/vec2.hpp>
#include <optional>
class IClientInstance;
class MinecraftUIRenderContext;
namespace lamium::map::world {
// The world map screen (BACKLOG L-60 world map, docs/demos/worldmap.html).
// The settings screen's scene owns focus and the cursor and forwards its
// queued input here; every call runs on the client thread from its render.
struct Request {
    enum class Kind { None, Close, AddWaypoint, EditWaypoint } kind = Kind::None;
    Waypoint draft;     // AddWaypoint
    int index = -1;     // EditWaypoint: into waypoints::current()
};
void open(IClientInstance&);
// Releases the screen's textures; also on world exit.
void close();
Request press(float x, float y, bool right);
void release();
void wheel(float x, float y, int direction);
Request key(int key, bool openKey);
void render(MinecraftUIRenderContext&, glm::vec2 size, glm::vec2 pointer, Settings::Map const&);
}
