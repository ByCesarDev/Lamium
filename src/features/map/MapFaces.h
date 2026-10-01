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
// B). Built at runtime from the texture the game already has; nothing from
// the game is stored. Pure; RadarFaces.cpp reads the model and texture,
// Minimap.cpp draws the faces on screen pixels.

// A face at its texture's own resolution (at most maxFaceSide a side).
inline constexpr int maxFaceSide = 16;
struct Face {
    int width = 0, height = 0;
    std::vector<std::uint32_t> pixels; // RGBA, row-major; alpha 0 is see-through.
};

// Renderers whose default skin is not the mob's own look (an overlay or an
// armor layer): the texture to build the face from instead.
inline std::string_view baseTexture(std::string_view renderer) {
    if (renderer == "minecraft:villager_v2") return "textures/entity/villager2/villager";
    if (renderer == "minecraft:horse") return "textures/entity/horse2/horse_white";
    if (renderer == "minecraft:donkey") return "textures/entity/horse2/donkey";
    if (renderer == "minecraft:mule") return "textures/entity/horse2/mule";
    return {};
}
// Parts drawn over a head that are not the face itself (worn layers).
inline bool faceLayer(std::string_view part) {
    for (auto skip : {"hat", "helmet", "brim", "armor", "jacket"})
        if (part.find(skip) != std::string_view::npos) return false;
    return true;
}

// One cube's front: where it covers the head (model units, y up), how near
// it is (smaller z is nearer, the mob facing -z), and its front face in the
// texture (texture units).
struct FaceBox {
    double x0, y0, x1, y1, z;
    double u, v, w, h;
};
// The head seen from the front: every box's front face painted far to
// near, at `texelsPerUnit` image pixels per model unit. A finer texture (a
// high-resolution pack) is reduced by a whole factor to fit maxFaceSide.
// None when nothing is visible.
inline std::optional<Face> composeFace(std::uint8_t const* rgba, int width, int height, double texelsPerUnit,
                                       std::vector<FaceBox> boxes) {
    if (!rgba || width <= 0 || height <= 0 || !(texelsPerUnit > 0) || boxes.empty()) return std::nullopt;
    double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
    for (auto const& b : boxes) {
        minX = std::min(minX, b.x0); maxX = std::max(maxX, b.x1);
        minY = std::min(minY, b.y0); maxY = std::max(maxY, b.y1);
    }
    int fullW = static_cast<int>(std::lround((maxX - minX) * texelsPerUnit));
    int fullH = static_cast<int>(std::lround((maxY - minY) * texelsPerUnit));
    if (fullW <= 0 || fullH <= 0 || fullW > 512 || fullH > 512) return std::nullopt;
    std::vector<std::uint32_t> full(static_cast<size_t>(fullW) * fullH, 0);
    std::stable_sort(boxes.begin(), boxes.end(), [](FaceBox const& a, FaceBox const& b) { return a.z > b.z; });
    for (auto const& b : boxes) {
        // A mob faces north (-z): seen from the front, +x is on the viewer's
        // left, as the front texture is laid out.
        int px0 = static_cast<int>(std::lround((maxX - b.x1) * texelsPerUnit));
        int px1 = static_cast<int>(std::lround((maxX - b.x0) * texelsPerUnit));
        int py0 = static_cast<int>(std::lround((maxY - b.y1) * texelsPerUnit));
        int py1 = static_cast<int>(std::lround((maxY - b.y0) * texelsPerUnit));
        if (px1 <= px0 || py1 <= py0) continue;
        for (int y = py0; y < py1; ++y)
            for (int x = px0; x < px1; ++x) {
                double fx = (x - px0 + .5) / (px1 - px0), fy = (y - py0 + .5) / (py1 - py0);
                int sx = static_cast<int>(std::floor((b.u + fx * b.w) * texelsPerUnit));
                int sy = static_cast<int>(std::floor((b.v + fy * b.h) * texelsPerUnit));
                if (sx < 0 || sy < 0 || sx >= width || sy >= height) continue;
                auto const* p = rgba + (static_cast<size_t>(sy) * width + sx) * 4;
                if (!p[3]) continue;
                full[static_cast<size_t>(y) * fullW + x] = packColor(p[0], p[1], p[2], p[3]);
            }
    }
    // A high-resolution face drops to one texel per model unit when that
    // fits, else to the smallest whole factor that does.
    int step = std::max(1, (std::max(fullW, fullH) + maxFaceSide - 1) / maxFaceSide);
    if (int unit = static_cast<int>(std::lround(texelsPerUnit)); unit > step && std::max(fullW, fullH) / unit <= maxFaceSide)
        step = unit;
    Face face{std::max(1, fullW / step), std::max(1, fullH / step), {}};
    face.pixels.assign(static_cast<size_t>(face.width) * face.height, 0);
    bool any = false;
    for (int y = 0; y < face.height; ++y)
        for (int x = 0; x < face.width; ++x) {
            auto c = full[static_cast<size_t>(y * step) * fullW + x * step];
            face.pixels[static_cast<size_t>(y * face.width + x)] = c;
            any = any || (c >> 24);
        }
    if (!any) return std::nullopt;
    return face;
}

// Faces share one runtime texture: a grid of cells, each a face with a
// one-texel black outline around its visible pixels (look B). The outline
// is part of the image, so it moves with the face exactly.
inline constexpr int faceCellSide = maxFaceSide + 2;
inline constexpr int faceAtlasSide = 256, faceAtlasCells = faceAtlasSide / faceCellSide;
inline constexpr int faceAtlasCapacity = faceAtlasCells * faceAtlasCells;
inline constexpr std::uint32_t faceOutline = packColor(0, 0, 0, 230);
struct AtlasCell { int x, y; };
inline AtlasCell faceAtlasCell(int index) {
    return {(index % faceAtlasCells) * faceCellSide, (index / faceAtlasCells) * faceCellSide};
}
// The face's outlined image size inside its cell.
inline int outlinedWidth(Face const& face) { return face.width + 2; }
inline int outlinedHeight(Face const& face) { return face.height + 2; }
inline void writeFace(std::vector<std::uint32_t>& atlas, int index, Face const& face) {
    if (index < 0 || index >= faceAtlasCapacity) return;
    atlas.resize(static_cast<size_t>(faceAtlasSide) * faceAtlasSide, 0);
    auto cell = faceAtlasCell(index);
    auto opaque = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < face.width && y < face.height
            && (face.pixels[static_cast<size_t>(y * face.width + x)] >> 24);
    };
    for (int y = -1; y <= face.height; ++y)
        for (int x = -1; x <= face.width; ++x) {
            std::uint32_t c = 0;
            if (opaque(x, y)) c = face.pixels[static_cast<size_t>(y * face.width + x)];
            else
                for (int dy = -1; dy <= 1 && !c; ++dy)
                    for (int dx = -1; dx <= 1 && !c; ++dx)
                        if (opaque(x + dx, y + dy)) c = faceOutline;
            atlas[static_cast<size_t>(cell.y + y + 1) * faceAtlasSide + cell.x + x + 1] = c;
        }
}
// Screen pixels per face texel: about `target` screen pixels for the longer
// side, never less than one, always whole so the texture's pixels stay even.
inline int faceTexelPixels(int longerSide, double target) {
    if (longerSide <= 0 || !(target > 0)) return 1;
    return std::max(1, static_cast<int>(std::lround(target / longerSide)));
}
}
