#pragma once
#include "features/map/MapColors.h"
#include "features/map/MapTiles.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>

namespace lamium::map {
// Cave view (BACKLOG L-60 step 3): under a ceiling the map shows the floors
// around the player's height instead of the top of the world. Pure.

enum class ViewMode { Surface, Cave };
// Covered: a block above the head. Sky light keeps open ravines and tree
// shade on the surface; the gap between the two thresholds stops flicker at
// cave mouths.
inline ViewMode chooseView(ViewMode current, bool nether, bool covered, int skyLight) {
    if (nether) return ViewMode::Cave;
    if (!covered) return ViewMode::Surface;
    if (current == ViewMode::Cave) return skyLight < 10 ? ViewMode::Cave : ViewMode::Surface;
    return skyLight <= 6 ? ViewMode::Cave : ViewMode::Surface;
}
// A forced view from the key; Auto follows chooseView.
enum class ViewForce { Auto, Cave, Surface };
inline ViewMode applyForce(ViewForce force, ViewMode automatic) {
    if (force == ViewForce::Cave) return ViewMode::Cave;
    if (force == ViewForce::Surface) return ViewMode::Surface;
    return automatic;
}
// The key forces the view that is not shown; pressed again it returns to
// the automatic choice.
inline ViewForce pressForce(ViewForce force, ViewMode shown) {
    if (force != ViewForce::Auto) return ViewForce::Auto;
    return shown == ViewMode::Cave ? ViewForce::Surface : ViewForce::Cave;
}

// Blocks examined per column, from above the head down.
inline constexpr int caveAbove = 2, caveBelow = 24;
inline constexpr std::uint32_t caveWall = packColor(34, 34, 38), caveDeep = packColor(18, 18, 22);
// A column's solid blocks from y = top downward (index 0 is top). Rock at
// the player's feet and head is a wall. Otherwise the floor is the first
// solid block below open space; open all the way down is a drop.
struct CaveHit {
    enum class Kind { Wall, Floor, Drop } kind;
    int y;
};
inline CaveHit caveFloor(std::span<bool const> solid, int top, int playerY) {
    auto solidAt = [&](int y) {
        int i = top - y;
        return i >= 0 && i < static_cast<int>(solid.size()) && solid[static_cast<size_t>(i)];
    };
    if (solidAt(playerY) && solidAt(playerY + 1)) return {CaveHit::Kind::Wall, top};
    bool open = false;
    for (size_t i = 0; i < solid.size(); ++i) {
        if (!solid[i]) { open = true; continue; }
        if (open) return {CaveHit::Kind::Floor, top - static_cast<int>(i)};
    }
    if (!open) return {CaveHit::Kind::Wall, top};
    return {CaveHit::Kind::Drop, top - static_cast<int>(solid.size())};
}
// Floors near the player's height are bright, lower or higher ones dimmer;
// the block under the feet is at full brightness.
inline float caveBrightness(int y, int playerY) {
    return 1.f - .6f * std::min(1.f, std::abs(y - (playerY - 1)) / float(caveBelow));
}
// The column as the map shows it, given the floor block's own color.
inline Column caveColumn(CaveHit hit, int playerY, std::uint32_t floorColor) {
    if (hit.kind == CaveHit::Kind::Wall) return {caveWall, static_cast<std::int16_t>(hit.y)};
    if (hit.kind == CaveHit::Kind::Drop) return {caveDeep, static_cast<std::int16_t>(hit.y)};
    float k = caveBrightness(hit.y, playerY);
    return {tinted(floorColor ? floorColor : caveWall, k, k, k), static_cast<std::int16_t>(hit.y)};
}
}
