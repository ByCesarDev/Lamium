#pragma once
#include <format>
#include <optional>
#include <string>

namespace lamium::map {
// World map teleport (2026-10-07): offered when /tp would actually run for
// this player, and only within the player's own dimension. The server's
// command list for the player decides when it has arrived: it lists /tp also
// when LeviLamina's forceEnableCheatCommands enables commands in a world
// without cheats (maintainer: usability over the cheat setting). Before it
// arrives, the client's commands flag (it follows the world's cheat setting)
// and permission decide. Lamium sends the ordinary /tp command and never
// works around permissions.
inline bool canTeleport(std::optional<bool> listed, bool commandsEnabled, int permissionLevel, int playerDimension,
                        int targetDimension) {
    if (playerDimension != targetDimension) return false;
    // GameDirectors (1) is the lowest level that may run /tp.
    return listed ? *listed : commandsEnabled && permissionLevel >= 1;
}
// Block coordinates to the block's center, standing on `y` (block -6 spans
// -6 to -5, so its center is -5.5).
inline std::string teleportCommand(int x, int y, int z) {
    return std::format("/tp @s {:.1f} {} {:.1f}", x + .5, y, z + .5);
}
}
