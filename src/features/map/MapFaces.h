#pragma once
#include "features/map/MapImage.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace lamium::map {
// Mob faces on the radar (BACKLOG L-85, docs/demos/radar-icons.html, look
// B). Cut at runtime from the texture the game already has; nothing from
// the game is stored. Pure; RadarFaces.cpp finds the texture and the face.

// 8x8 RGBA pixels; alpha 0 is see-through.
using Face = std::array<std::uint32_t, 64>;

// Renderers whose default skin is not the mob's own look (an overlay or an
// armor layer): the texture to cut the face from instead.
inline std::string_view baseTexture(std::string_view renderer) {
    if (renderer == "minecraft:villager_v2") return "textures/entity/villager2/villager";
    if (renderer == "minecraft:horse") return "textures/entity/horse2/horse_white";
    if (renderer == "minecraft:donkey") return "textures/entity/horse2/donkey";
    if (renderer == "minecraft:mule") return "textures/entity/horse2/mule";
    return {};
}

// A face from a rectangle of an RGBA image (texture pixels, any size),
// scaled to 8x8 keeping its proportions: the longer side fills the square,
// the shorter one is centered. None when nothing in it is visible.
inline std::optional<Face> cropFace(std::uint8_t const* rgba, int width, int height, double u, double v, double w,
                                    double h) {
    if (!rgba || width <= 0 || height <= 0 || !(w > 0) || !(h > 0)) return std::nullopt;
    Face face{};
    double side = std::max(w, h), offsetX = (side - w) / 2, offsetY = (side - h) / 2;
    bool any = false;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            double fx = (x + .5) * side / 8 - offsetX, fy = (y + .5) * side / 8 - offsetY;
            if (fx < 0 || fy < 0 || fx >= w || fy >= h) continue;
            int sx = static_cast<int>(std::floor(u + fx)), sy = static_cast<int>(std::floor(v + fy));
            if (sx < 0 || sy < 0 || sx >= width || sy >= height) continue;
            auto const* p = rgba + (static_cast<size_t>(sy) * width + sx) * 4;
            if (!p[3]) continue;
            face[static_cast<size_t>(y * 8 + x)] = packColor(p[0], p[1], p[2], p[3]);
            any = true;
        }
    if (!any) return std::nullopt;
    return face;
}

// A face `size` pixels square inside a black ring `ring` pixels wide,
// centered on (cx, cy) and placed on whole pixels.
inline void drawFace(std::vector<std::uint32_t>& pixels, int n, double cx, double cy, double size, double ring,
                     Face const& face, float alpha) {
    int side = std::max(2, static_cast<int>(std::lround(size)));
    int edge = std::max(1, static_cast<int>(std::lround(ring)));
    int x0 = static_cast<int>(std::lround(cx - side / 2.0)), y0 = static_cast<int>(std::lround(cy - side / 2.0));
    for (int y = y0 - edge; y < y0 + side + edge; ++y)
        for (int x = x0 - edge; x < x0 + side + edge; ++x) {
            if (x < 0 || y < 0 || x >= n || y >= n) continue;
            auto& pixel = pixels[static_cast<size_t>(y) * n + x];
            bool inside = x >= x0 && y >= y0 && x < x0 + side && y < y0 + side;
            if (!inside) { pixel = over(pixel, 0, 0, 0, .9f * alpha); continue; }
            auto c = face[static_cast<size_t>(((y - y0) * 8 / side) * 8 + (x - x0) * 8 / side)];
            // See-through parts of a face show the ring's black behind them.
            pixel = over(pixel, 0, 0, 0, .9f * alpha);
            if (channel(c, 3)) pixel = over(pixel, channel(c, 0), channel(c, 1), channel(c, 2), channel(c, 3) / 255.f * alpha);
        }
}
}
