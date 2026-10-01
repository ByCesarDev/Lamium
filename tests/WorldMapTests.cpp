#include "features/map/MapRegion.h"
#include "features/map/WorldMapView.h"
#include <cmath>
void check(bool, char const*);
namespace {
using namespace lamium::map;
std::array<Column, 256> chunkOf(std::uint32_t color, std::int16_t height) {
    std::array<Column, 256> columns{};
    for (auto& c : columns) c = {color, height};
    return columns;
}
void regions() {
    check(regionOfChunk({15, -1}) == RegionKey{0, -1} && regionOfChunk({16, -16}) == RegionKey{1, -1}
              && regionOfChunk({-17, 0}) == RegionKey{-2, 0}, "chunks fall into 16x16-chunk regions");
    check(regionOfBlock(-1, 256) == RegionKey{-1, 1} && regionIndex(-1, 256) == 255,
          "negative blocks index from the region's own corner");
    RegionData region;
    std::vector<bool> recorded(regionColumns, false);
    auto grass = packColor(90, 160, 60);
    check(mergeChunk(region, {-1, 2}, chunkOf(grass, 70), &recorded), "a new chunk changes the region");
    check(!mergeChunk(region, {-1, 2}, chunkOf(grass, 70), &recorded), "the same chunk again changes nothing");
    auto half = chunkOf(packColor(10, 20, 30), 64);
    for (size_t i = 0; i < 128; ++i) half[i].color = 0;
    mergeChunk(region, {-1, 2}, half);
    std::array<Column, 256> back{};
    check(copyChunk(region, {-1, 2}, back) && back[0].color == grass && back[200].height == 64,
          "unknown columns keep what was recorded; known ones replace it");
    check(!copyChunk(region, {0, 0}, back), "a chunk never recorded is unknown");
    check(recorded[static_cast<size_t>(regionIndex(-16, 32))] && !recorded[0], "recording marks the columns it wrote");

    auto bytes = encodeRegion(region);
    auto decoded = decodeRegion(bytes);
    check(decoded && decoded->colors == region.colors && decoded->heights == region.heights, "a region survives saving");
    check(bytes.size() < 1000, "runs keep a mostly empty region small");
    check(!decodeRegion(bytes.substr(0, bytes.size() - 8)) && !decodeRegion("LMR1") && !decodeRegion("junk"),
          "a short or foreign file is not a region");
    auto wrong = bytes;
    wrong[4] = static_cast<char>(0xFF);
    wrong[5] = static_cast<char>(0xFF);
    check(!decodeRegion(wrong), "runs past the end are rejected");

    RegionData disk;
    mergeChunk(disk, {0, 0}, chunkOf(packColor(1, 2, 3), 5));
    mergeChunk(disk, {-1, 2}, chunkOf(packColor(4, 5, 6), 6));
    RegionData live;
    std::vector<bool> mask(regionColumns, false);
    mergeChunk(live, {-1, 2}, chunkOf(grass, 70), &mask);
    underlay(live, mask, disk);
    check(copyChunk(live, {0, 0}, back) && back[0].color == packColor(1, 2, 3), "the saved file fills what was not scanned");
    check(copyChunk(live, {-1, 2}, back) && back[0].color == grass, "what was scanned since wins over the file");
}
void images() {
    RegionData region;
    auto stone = packColor(100, 100, 100);
    mergeChunk(region, {0, 0}, chunkOf(stone, 64));
    region.heights[static_cast<size_t>(regionIndex(5, 5))] = 66;
    auto pixels = shadeRegion(region);
    check(pixels[0] == stone && pixels[static_cast<size_t>(regionIndex(20, 0))] == 0, "level ground keeps its color, unknown stays clear");
    check(channel(pixels[static_cast<size_t>(regionIndex(5, 5))], 0) > 100 && channel(pixels[static_cast<size_t>(regionIndex(6, 5))], 0) < 100,
          "a step up is lighter and the column after it darker");
    std::vector<std::uint32_t> parent(regionColumns, 0);
    downsampleInto(parent, pixels, 3);
    check(parent[static_cast<size_t>(128 * 256 + 128)] != 0 && parent[0] == 0 && parent[static_cast<size_t>(128 * 256 + 140)] == 0,
          "a quarter lands in its corner at half size");
    check(layerFolder({0, 0}) == "overworld" && layerFolder({1, 4}) == "nether/y64" && layerFolder({2, 0}) == "end",
          "each dimension and Nether layer has its folder");
    check(mapLayer(1, 70) == MapLayer{1, 4} && mapLayer(1, -1) == MapLayer{1, -1} && mapLayer(0, 70) == MapLayer{0, 0},
          "the Nether is layered by 16 blocks; other dimensions are not");
    check(regionFileName({-3, 7}) == "r.-3.7.lmr", "region files are named by position");
}
void view() {
    WorldView v{100, -50, defaultWorldScale, 400, 200};
    check(std::abs(v.screenX(100) - 200) < 1e-9 && std::abs(v.worldZ(100) + 50) < 1e-9, "the center is mid-screen");
    double wx = v.worldX(300), wz = v.worldZ(40);
    v.zoomAt(300, 40, 2);
    check(v.zoom == defaultWorldScale + 2 && std::abs(v.worldX(300) - wx) < 1e-9 && std::abs(v.worldZ(40) - wz) < 1e-9,
          "zooming keeps the point under the cursor");
    v.zoomAt(0, 0, 99);
    check(v.zoom == static_cast<int>(worldScales.size()) - 1, "zoom stops at the closest step");
    WorldView pan{0, 0, 6, 100, 100}; // One unit per block.
    pan.pan(10, -4);
    check(std::abs(pan.centerX + 10) < 1e-9 && std::abs(pan.centerZ - 4) < 1e-9, "dragging moves the map with the cursor");

    check(lodFor(2, 2) == 0 && lodFor(1 / 8., 2) == 2 && lodFor(1 / 8., 1) == 3 && lodFor(1 / 64., 1) == maxLod,
          "the image level follows screen pixels per block");
    check(parentTile({0, -1, 3}) == TileKey{1, -1, 1}, "a tile's parent covers it");
    WorldView wide{0, 0, defaultWorldScale, 1000, 600}; // 2 units per block: 500 x 300 blocks.
    auto tiles = visibleTiles(wide, 0);
    check(tiles.size() == 4 && tiles.front().x * 256 <= 0 && tiles.front().x * 256 + 256 >= 0, "the tiles on screen, center first");
    check(snapToPixel(10.3, 2) == 10.5 && snapToPixel(10.3, 0) == 10.3, "edges land on screen pixels");
    check(scaleBarBlocks(2, 30) == 20 && scaleBarBlocks(1 / 8., 30) == 500, "the scale bar takes a round length");
}
}
void worldMapTests() {
    regions();
    images();
    view();
}
