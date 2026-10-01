#pragma once
#include "features/map/Waypoints.h"
#include <functional>
#include <optional>
#include <string>
class IClientInstance;
namespace lamium::map::waypoints {
// The current world's waypoints (BACKLOG L-60 step 5). Loaded when a world
// is joined: a local world keeps them in its own Lamium folder, a server per
// address and port in Lamium's config folder; anything else keeps them for
// the session only. Reads return copies.
void start();
void stop();
WaypointSet current();
// Persists first and publishes only on success; false when saving failed.
bool add(Waypoint waypoint);
// Applies a change to a copy, saves it and publishes it. False when the
// change declined (returned false) or saving failed.
bool change(std::function<bool(WaypointSet&)> const& mutation);
// Called every frame from the world render: notices the local player's
// death and remembers where. Saved later from frame(), never here.
void watchDeath(IClientInstance&);
// Called every HUD frame on the client thread: saves a pending death point.
void frame(bool recordDeath);
}
