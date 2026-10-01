#pragma once
#include "features/map/Waypoints.h"
#include <filesystem>
#include <string>
#include <string_view>

namespace lamium::map {
// Waypoint documents: one per local world (in the world's Lamium folder,
// beside shapes) or per server address and port (in Lamium's config folder).
std::string encodeWaypoints(WaypointSet const&);
// Tolerant of missing fields; rejects other versions and broken documents.
WaypointSet decodeWaypoints(std::string_view);
WaypointSet readWaypoints(std::filesystem::path const&);
void writeWaypoints(std::filesystem::path const&, WaypointSet const&);
}
