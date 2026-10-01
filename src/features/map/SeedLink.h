#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <tuple>

namespace lamium::map {
// A link to an external seed map (BACKLOG L-82): ChunkBase's seed map opened
// at a place for this world's seed and game version. Pure.

// Bedrock versions as ChunkBase names its maps, newest first: each map
// covers versions from its own up to the next one. Versions are compared as
// (a, b, c): "26.50" is {26, 50, 0}, "1.21.120" is {1, 21, 120}.
struct SeedMapVersion {
    std::array<int, 3> from;
    std::string_view id;
};
inline constexpr auto seedMapVersions = std::to_array<SeedMapVersion>({
    {{26, 50, 0}, "bedrock_26_50"},
    {{26, 30, 0}, "bedrock_26_30"},
    {{26, 0, 0}, "bedrock_26_0"},
    {{1, 21, 120}, "bedrock_1_21_120"},
    {{1, 21, 110}, "bedrock_1_21_110"},
    {{1, 21, 90}, "bedrock_1_21_90"},
    {{1, 21, 60}, "bedrock_1_21_60"},
    {{1, 21, 50}, "bedrock_1_21_50"},
    {{1, 21, 0}, "bedrock_1_21"},
    {{1, 20, 60}, "bedrock_1_20_60"},
    {{1, 20, 0}, "bedrock_1_20"},
    {{1, 19, 0}, "bedrock_1_19"},
    {{1, 18, 0}, "bedrock_1_18"},
    {{1, 17, 0}, "bedrock_1_17"},
    {{1, 16, 0}, "bedrock_1_16"},
    {{1, 14, 0}, "bedrock_1_14"},
});
// The game reports 26.x releases as 1.26.x; ChunkBase calls them 26.x.
inline std::array<int, 3> seedMapVersionOf(int major, int minor, int patch) {
    if (major == 1 && minor >= 26) return {minor, patch, 0};
    return {major, minor, patch};
}
// The map for a version: the newest one not newer than it. A version newer
// than every known map takes the newest (an unknown id would silently open
// a Java map); an older one the oldest.
inline std::string_view seedMapPlatform(int major, int minor, int patch) {
    auto version = seedMapVersionOf(major, minor, patch);
    for (auto const& entry : seedMapVersions)
        if (std::tie(entry.from[0], entry.from[1], entry.from[2]) <= std::tie(version[0], version[1], version[2]))
            return entry.id;
    return seedMapVersions.back().id;
}
inline std::string_view seedMapDimension(int dimension) {
    return dimension == 1 ? "nether" : dimension == 2 ? "end" : "overworld";
}
// Bedrock shows seeds as signed 64-bit numbers.
inline std::string seedText(std::uint64_t seed) { return std::to_string(static_cast<std::int64_t>(seed)); }
// ChunkBase's zoom for a scale in screen pixels per block. Measured in a
// browser 2026-10-01: log2(pixels per block) = 4 * zoom - 4 (zoom 1 is one
// pixel per block, 0.5 a quarter), and it stops at 1.75 (8 pixels).
inline double seedMapZoom(double pixelsPerBlock) {
    if (!(pixelsPerBlock > 0) || !std::isfinite(pixelsPerBlock)) return 1;
    double zoom = (std::log2(pixelsPerBlock) + 4) / 4;
    return std::round(std::clamp(zoom, 0.0, 1.75) * 1000) / 1000;
}
inline std::string seedMapUrl(std::uint64_t seed, std::string_view platform, int dimension, int x, int z,
                              double zoom = 1) {
    return std::format("https://www.chunkbase.com/apps/seed-map#seed={}&platform={}&dimension={}&x={}&z={}&zoom={}",
                       seedText(seed), platform, seedMapDimension(dimension), x, z, zoom);
}
}
