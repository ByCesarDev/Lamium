#pragma once
#include "features/map/MapRegion.h"
#include "features/map/MapView.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace lamium::map {
// The world map screen's geometry (BACKLOG L-60 world map, look in
// docs/demos/worldmap.html). Pure; WorldMap.cpp draws.

// GUI units per block, from far to close.
inline constexpr std::array<double, 15> worldScales{1 / 8., 3 / 16., 1 / 4., 3 / 8., 1 / 2., 3 / 4., 1., 1.5, 2., 3., 4., 6., 8., 12., 16.};
inline constexpr int defaultWorldScale = 8;
inline constexpr int clampWorldScale(int index) { return std::clamp(index, 0, static_cast<int>(worldScales.size()) - 1); }
// The Nether's saved layers: y 0-127 in 16-block bands.
inline constexpr int netherBands = 8;

// An image of 256x256 pixels covering 256 << lod blocks: level 0 is one
// region, each level above averages four of the level below.
struct TileKey {
    int lod = 0, x = 0, z = 0;
    bool operator==(TileKey const&) const = default;
};
inline constexpr int maxLod = 5;
inline constexpr int tileBlocks(int lod) { return regionBlocks << lod; }
inline constexpr TileKey parentTile(TileKey tile) { return {tile.lod + 1, floorDiv(tile.x, 2), floorDiv(tile.z, 2)}; }
// The coarsest level whose pixel still covers a screen pixel or more, so no
// more pixels are built and uploaded than the screen can show.
inline int lodFor(double unitsPerBlock, double screenPixelsPerUnit) {
    int lod = 0;
    while (lod < maxLod && unitsPerBlock * double(1 << lod) * screenPixelsPerUnit < 1) ++lod;
    return lod;
}

struct WorldView {
    double centerX = 0, centerZ = 0;
    int zoom = defaultWorldScale;
    double width = 0, height = 0; // GUI units.
    double scale() const { return worldScales[static_cast<size_t>(clampWorldScale(zoom))]; }
    double screenX(double worldX) const { return width / 2 + (worldX - centerX) * scale(); }
    double screenY(double worldZ) const { return height / 2 + (worldZ - centerZ) * scale(); }
    double worldX(double x) const { return centerX + (x - width / 2) / scale(); }
    double worldZ(double y) const { return centerZ + (y - height / 2) / scale(); }
    // Keeps the world point under (x, y) in place.
    void zoomAt(double x, double y, int step) {
        double wx = worldX(x), wz = worldZ(y);
        zoom = clampWorldScale(zoom + step);
        centerX = wx - (x - width / 2) / scale();
        centerZ = wz - (y - height / 2) / scale();
    }
    void pan(double dx, double dy) {
        centerX = std::clamp(centerX - dx / scale(), -3.0e7, 3.0e7);
        centerZ = std::clamp(centerZ - dy / scale(), -3.0e7, 3.0e7);
    }
};
// Tiles of one level covering the view, nearest the center first.
inline std::vector<TileKey> visibleTiles(WorldView const& view, int lod) {
    double blocks = tileBlocks(lod);
    int x0 = static_cast<int>(std::floor(view.worldX(0) / blocks)), x1 = static_cast<int>(std::floor(view.worldX(view.width) / blocks));
    int z0 = static_cast<int>(std::floor(view.worldZ(0) / blocks)), z1 = static_cast<int>(std::floor(view.worldZ(view.height) / blocks));
    std::vector<TileKey> tiles;
    if (x1 - x0 > 64 || z1 - z0 > 64) return tiles;
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x) tiles.push_back({lod, x, z});
    auto distance = [&](TileKey t) {
        double dx = (t.x + .5) * blocks - view.centerX, dz = (t.z + .5) * blocks - view.centerZ;
        return dx * dx + dz * dz;
    };
    std::sort(tiles.begin(), tiles.end(), [&](TileKey a, TileKey b) { return distance(a) < distance(b); });
    return tiles;
}
// A GUI coordinate moved onto the screen's pixel grid; tiles drawn edge to
// edge then meet without seams or overlaps.
inline double snapToPixel(double units, double screenPixelsPerUnit) {
    if (!(screenPixelsPerUnit > 0)) return units;
    return std::round(units * screenPixelsPerUnit) / screenPixelsPerUnit;
}
// A round length for the scale bar at least `minUnits` long.
inline int scaleBarBlocks(double unitsPerBlock, double minUnits) {
    for (int blocks : {1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000})
        if (blocks * unitsPerBlock >= minUnits) return blocks;
    return 10000;
}
}
