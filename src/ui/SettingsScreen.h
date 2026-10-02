#pragma once
#include "features/map/Waypoints.h"
class IClientInstance;
namespace lamium::ui {
void start();
void stop();
void open(IClientInstance& client);
void openShapes(IClientInstance& client);
void openHotkeys(IClientInstance& client);
void openHudLayout(IClientInstance& client);
// The waypoint add prompt over the world, for a waypoint already placed.
void openWaypointPrompt(IClientInstance& client, map::Waypoint draft);
void openWaypoints(IClientInstance& client);
// tab: -1 keeps the last one, else 0 Files, 1 Placed, 2 Check, 3 Materials.
void openSchematics(IClientInstance& client, int tab);
void openWorldMap(IClientInstance& client);
bool ownsInput();
void cancelInputCapture();
}
