#pragma once
#include <format>
#include <string>

namespace lamium::map {
// World map teleport (2026-10-07): offered only where the world lets this
// player run commands, and only within the player's own dimension. Lamium
// sends the ordinary /tp command and never works around permissions. Pure.
inline bool canTeleport(bool commandsEnabled, int permissionLevel, int playerDimension, int targetDimension) {
    // GameDirectors (1) is the lowest level that may run /tp.
    return commandsEnabled && permissionLevel >= 1 && playerDimension == targetDimension;
}
// Block coordinates to the block's center, standing on `y` (block -6 spans
// -6 to -5, so its center is -5.5).
inline std::string teleportCommand(int x, int y, int z) {
    return std::format("/tp @s {:.1f} {} {:.1f}", x + .5, y, z + .5);
}
}
