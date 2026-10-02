#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace lamium::information::saturation {
// Saturation on the vanilla hunger bar (BACKLOG L-63, docs/demos/saturation.html).
// Pure: which icons get the gold outline, what eating would give, and the
// outline image cut from the game's own drumstick.
enum class Mark { None, Half, Full };
inline constexpr int icons = 10;
inline constexpr float maximum = 20;

// Icon i counts from the right like the game's bar and holds units 2i+1 and
// 2i+2. Fractions are dropped: 13.4 saturation marks six and a half icons.
inline Mark mark(int icon, float value) {
    if (!std::isfinite(value)) return Mark::None;
    int units = static_cast<int>(std::floor(std::clamp(value, 0.f, maximum)));
    return units >= 2 * icon + 2 ? Mark::Full : units == 2 * icon + 1 ? Mark::Half : Mark::None;
}

struct Levels { float hunger = 0, saturation = 0; };
// Eating adds the nutrition to hunger and nutrition x modifier x 2 to
// saturation; hunger stops at 20 and saturation at the new hunger.
inline Levels afterEating(Levels now, int nutrition, float modifier) {
    if (!std::isfinite(now.hunger) || !std::isfinite(now.saturation) || !std::isfinite(modifier)) return now;
    Levels next;
    next.hunger = std::clamp(now.hunger + std::max(nutrition, 0), 0.f, maximum);
    next.saturation = std::clamp(now.saturation + std::max(nutrition, 0) * std::max(modifier, 0.f) * 2, 0.f, next.hunger);
    return next;
}

// A food's values in the inventory (L-64), in the hunger bar's own terms:
// icons right to left, hunger as drumsticks, saturation as gold outlines.
// Raw gains, not capped by the player's state; enough icons for the larger.
struct FoodIcon { Mark hunger, saturation; };
inline std::vector<FoodIcon> foodIcons(int nutrition, float modifier) {
    std::vector<FoodIcon> result;
    if (nutrition <= 0 || !std::isfinite(modifier)) return result;
    float gain = nutrition * std::max(modifier, 0.f) * 2;
    int units = std::max(nutrition, static_cast<int>(std::floor(std::min(gain, maximum))));
    int count = std::min((units + 1) / 2, icons);
    for (int i = 0; i < count; ++i) result.push_back({mark(i, static_cast<float>(nutrition)), mark(i, gain)});
    return result;
}

// Which part of the outline: a half mark is the right half, like the game's
// half drumstick; a gain that completes a half-marked icon is the left half.
enum class Part { Whole, Right, Left };
// The icon's own outline in `color` (RGBA bytes packed little-endian, as
// uploaded): opaque pixels that touch a transparent one or the image edge.
// Everything else is transparent.
inline std::vector<std::uint32_t> outline(std::uint8_t const* rgba, int width, int height, std::uint32_t color,
                                          Part part = Part::Whole) {
    std::vector<std::uint32_t> out;
    if (!rgba || width <= 0 || height <= 0) return out;
    out.assign(static_cast<size_t>(width) * height, 0);
    auto opaque = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < width && y < height && rgba[(static_cast<size_t>(y) * width + x) * 4 + 3] >= 128;
    };
    for (int y = 0; y < height; ++y)
        for (int x = part == Part::Right ? width / 2 : 0; x < (part == Part::Left ? width / 2 : width); ++x)
            if (opaque(x, y) && (!opaque(x - 1, y) || !opaque(x + 1, y) || !opaque(x, y - 1) || !opaque(x, y + 1)))
                out[static_cast<size_t>(y) * width + x] = color;
    return out;
}
// Saturation you have: #f2c23a. What a held food would add: pale gold #fff1b8,
// opaque too, because a thin translucent line disappears into the drumstick.
inline constexpr std::uint32_t gold = 0xff3ac2f2, paleGold = 0xffb8f1ff;
}
