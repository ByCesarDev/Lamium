#pragma once
#include "features/map/MapTiles.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace lamium::map {
// Mob faces on the radar (BACKLOG L-85, docs/demos/radar-icons.html, look
// B). Cut at runtime from the texture the game already has; nothing from
// the game is stored. Pure; RadarFaces.cpp finds the texture and the face,
// Minimap.cpp draws them on screen pixels so every texel stays square.

// A face at its texture's own resolution (at most maxFaceSide a side).
inline constexpr int maxFaceSide = 16;
struct Face {
    int width = 0, height = 0;
    std::vector<std::uint32_t> pixels; // RGBA, row-major; alpha 0 is see-through.
};

// Renderers whose default skin is not the mob's own look (an overlay or an
// armor layer): the texture to cut the face from instead.
inline std::string_view baseTexture(std::string_view renderer) {
    if (renderer == "minecraft:villager_v2") return "textures/entity/villager2/villager";
    if (renderer == "minecraft:horse") return "textures/entity/horse2/horse_white";
    if (renderer == "minecraft:donkey") return "textures/entity/horse2/donkey";
    if (renderer == "minecraft:mule") return "textures/entity/horse2/mule";
    return {};
}

// A face from a rectangle of an RGBA image, in image pixels. A finer
// texture (a high-resolution pack) is reduced by a whole factor so the face
// fits maxFaceSide; otherwise every texel is kept. None when nothing in it
// is visible.
inline std::optional<Face> cropFace(std::uint8_t const* rgba, int width, int height, double u, double v, double w,
                                    double h) {
    if (!rgba || width <= 0 || height <= 0 || !(w >= 1) || !(h >= 1)) return std::nullopt;
    int x0 = static_cast<int>(std::lround(u)), y0 = static_cast<int>(std::lround(v));
    int fw = static_cast<int>(std::lround(w)), fh = static_cast<int>(std::lround(h));
    int step = std::max(1, (std::max(fw, fh) + maxFaceSide - 1) / maxFaceSide);
    Face face{std::max(1, fw / step), std::max(1, fh / step), {}};
    face.pixels.assign(static_cast<size_t>(face.width) * face.height, 0);
    bool any = false;
    for (int y = 0; y < face.height; ++y)
        for (int x = 0; x < face.width; ++x) {
            int sx = x0 + x * step, sy = y0 + y * step;
            if (sx < 0 || sy < 0 || sx >= width || sy >= height) continue;
            auto const* p = rgba + (static_cast<size_t>(sy) * width + sx) * 4;
            if (!p[3]) continue;
            face.pixels[static_cast<size_t>(y * face.width + x)] = packColor(p[0], p[1], p[2], p[3]);
            any = true;
        }
    if (!any) return std::nullopt;
    return face;
}

// Faces share one runtime texture: a grid of maxFaceSide cells.
inline constexpr int faceAtlasSide = 256, faceAtlasCells = faceAtlasSide / maxFaceSide;
inline constexpr int faceAtlasCapacity = faceAtlasCells * faceAtlasCells;
struct AtlasCell { int x, y; };
inline AtlasCell faceAtlasCell(int index) {
    return {(index % faceAtlasCells) * maxFaceSide, (index / faceAtlasCells) * maxFaceSide};
}
inline void writeFace(std::vector<std::uint32_t>& atlas, int index, Face const& face) {
    if (index < 0 || index >= faceAtlasCapacity) return;
    atlas.resize(static_cast<size_t>(faceAtlasSide) * faceAtlasSide, 0);
    auto cell = faceAtlasCell(index);
    for (int y = 0; y < face.height; ++y)
        for (int x = 0; x < face.width; ++x)
            atlas[static_cast<size_t>(cell.y + y) * faceAtlasSide + cell.x + x] = face.pixels[static_cast<size_t>(y * face.width + x)];
}
// Screen pixels per face texel: about `target` screen pixels for the longer
// side, never less than one, always whole so the texture's pixels stay even.
inline int faceTexelPixels(int longerSide, double target) {
    if (longerSide <= 0 || !(target > 0)) return 1;
    return std::max(1, static_cast<int>(std::lround(target / longerSide)));
}
}
