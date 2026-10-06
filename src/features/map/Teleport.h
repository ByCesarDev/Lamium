#pragma once
#include <format>
#include <string>

namespace lamium::map {
// World map teleport (2026-10-07): offered only in a world with cheats on
// where this player may run commands, and only within the player's own
// dimension. Commands alone are not enough: LeviLamina's
// forceEnableCheatCommands enables them in worlds whose cheats are off.
// Lamium sends the ordinary /tp command and never works around permissions.
inline bool canTeleport(bool cheatsEnabled, bool commandsEnabled, int permissionLevel, int playerDimension,
                        int targetDimension) {
    // GameDirectors (1) is the lowest level that may run /tp.
    return cheatsEnabled && commandsEnabled && permissionLevel >= 1 && playerDimension == targetDimension;
}
// Block coordinates to the block's center, standing on `y` (block -6 spans
// -6 to -5, so its center is -5.5).
inline std::string teleportCommand(int x, int y, int z) {
    return std::format("/tp @s {:.1f} {} {:.1f}", x + .5, y, z + .5);
}
}
