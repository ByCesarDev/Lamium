#pragma once
#include "features/schematic/Structure.h"
#include <cstdint>
#include <optional>

// Where a schematic sits in the world and which part of it is shown
// (BACKLOG L-93). Positions only; block states are turned by the game's own
// transform in the render/verify glue.
namespace lamium::schematic {
struct Point {
    int x = 0, y = 0, z = 0;
    bool operator==(Point const&) const = default;
};
// X flips east-west, Z flips north-south. Applied before rotation.
enum class Mirror : std::uint8_t { None, X, Z };
struct Placement {
    Point origin;      // lower north-west corner of the placed (turned) box
    int rotation = 0;  // clockwise quarter turns seen from above, 0-3
    Mirror mirror = Mirror::None;
};

inline int quarterTurns(int rotation) { return ((rotation % 4) + 4) % 4; }
inline Size placedSize(Size size, int rotation) {
    return quarterTurns(rotation) % 2 ? Size{size.z, size.y, size.x} : size;
}
// Structure-local cell -> world cell.
inline Point toWorld(Size size, Placement const& placement, Point local) {
    int x = local.x, z = local.z;
    if (placement.mirror == Mirror::X) x = size.x - 1 - x;
    if (placement.mirror == Mirror::Z) z = size.z - 1 - z;
    // One clockwise turn takes east to south: (x, z) -> (depth-1-z, x).
    int width = size.x, depth = size.z;
    for (int i = 0; i < quarterTurns(placement.rotation); ++i) {
        int turnedX = depth - 1 - z, turnedZ = x;
        x = turnedX; z = turnedZ;
        std::swap(width, depth);
    }
    return {placement.origin.x + x, placement.origin.y + local.y, placement.origin.z + z};
}
// World cell -> structure-local cell, or nullopt outside the placed box.
inline std::optional<Point> toLocal(Size size, Placement const& placement, Point world) {
    Size placed = placedSize(size, placement.rotation);
    int x = world.x - placement.origin.x, y = world.y - placement.origin.y, z = world.z - placement.origin.z;
    if (x < 0 || y < 0 || z < 0 || x >= placed.x || y >= placed.y || z >= placed.z) return std::nullopt;
    int width = placed.x, depth = placed.z;
    for (int i = 0; i < quarterTurns(placement.rotation); ++i) {
        // Inverse of one clockwise turn: (x, z) -> (z, width-1-x).
        int localX = z, localZ = width - 1 - x;
        x = localX; z = localZ;
        std::swap(width, depth);
    }
    if (placement.mirror == Mirror::X) x = size.x - 1 - x;
    if (placement.mirror == Mirror::Z) z = size.z - 1 - z;
    return Point{x, y, z};
}

// Which layers are shown, along a world axis of the placed box.
enum class LayerAxis : std::uint8_t { UpFromBottom, DownFromTop, EastFromWest, WestFromEast, SouthFromNorth, NorthFromSouth };
enum class LayerMode : std::uint8_t { All, Only, UpTo };
struct Layers {
    LayerAxis axis = LayerAxis::UpFromBottom;
    LayerMode mode = LayerMode::All;
    int index = 0; // 0 = the first layer from the axis' start side
    bool operator==(Layers const&) const = default;
};
inline int layerCount(Size placed, LayerAxis axis) {
    switch (axis) {
    case LayerAxis::UpFromBottom: case LayerAxis::DownFromTop: return placed.y;
    case LayerAxis::EastFromWest: case LayerAxis::WestFromEast: return placed.x;
    default: return placed.z;
    }
}
// `offset` is the cell's position inside the placed box.
inline int layerOf(Size placed, LayerAxis axis, Point offset) {
    switch (axis) {
    case LayerAxis::UpFromBottom: return offset.y;
    case LayerAxis::DownFromTop: return placed.y - 1 - offset.y;
    case LayerAxis::EastFromWest: return offset.x;
    case LayerAxis::WestFromEast: return placed.x - 1 - offset.x;
    case LayerAxis::SouthFromNorth: return offset.z;
    default: return placed.z - 1 - offset.z;
    }
}
inline bool layerShown(Layers const& layers, Size placed, Point offset) {
    if (layers.mode == LayerMode::All) return true;
    int layer = layerOf(placed, layers.axis, offset);
    return layers.mode == LayerMode::Only ? layer == layers.index : layer <= layers.index;
}
}
