#include "features/map/Minimap.h"
#include "features/map/MapImage.h"
#include "features/map/MapTiles.h"
#include "features/map/MapView.h"
#include "features/information/InfoHud.h"
#include "app/Runtime.h"
#include "ui/Widgets.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/event/client/ClientJoinLevelEvent.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/client/renderer/TextureGroup.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/core/container/Blob.h"
#include "mc/deps/core/file/PathView.h"
#include "mc/deps/core/image/Image.h"
#include "mc/deps/core/math/Color.h"
#include "mc/deps/core/resource/ResourceLocation.h"
#include "mc/deps/core_graphics/ImageBuffer.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/dimension/Dimension.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <format>

namespace lamium::map {
namespace {
constexpr int pixels = 256;
// Per-frame scan budget; one chunk is the smallest step, so a frame may run
// over by one chunk's scan.
constexpr double scanBudgetSeconds = .0015;
// Terrain recomposes at most this often while moving; the arrow and settings
// changes redraw at once.
constexpr double terrainInterval = 1.0 / 30;
// Kept around the player beyond the widest zoom so zooming back is instant.
int const keepChunks = chunkRadius(zoomSteps.back(), true) + 4;

std::atomic<unsigned> worldGeneration{0};
ll::event::ListenerPtr exitListener, joinListener;

ResourceLocation const& textureLocation() {
    // Never destroyed: its destructor is game code, which must not run while
    // the process tears down after the game.
    static auto const* location = new ResourceLocation(Core::PathView("lamium/minimap"), ResourceFileSystem::Raw);
    return *location;
}
double now() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

struct Diagnostics {
    int lines = 0;
    double windowStart = 0;
    int composes = 0, uploads = 0, chunks = 0;
    double composeSeconds = 0, uploadSeconds = 0, scanSeconds = 0;
    void log(std::string const& text) {
        // Bounded: the log is for the first runs of an experimental feature.
        if (lines >= 40) return;
        ++lines;
        try { Runtime::instance().self().getLogger().info("Minimap: {}", text); } catch (...) {}
    }
    void report(double time, size_t tiles) {
        if (windowStart == 0) { windowStart = time; return; }
        if (time - windowStart < 30) return;
        log(std::format("{} composes avg {:.2f} ms, {} uploads avg {:.2f} ms, {} chunks scanned avg {:.3f} ms, {} tiles kept",
            composes, composes ? composeSeconds * 1000 / composes : 0., uploads, uploads ? uploadSeconds * 1000 / uploads : 0.,
            chunks, chunks ? scanSeconds * 1000 / chunks : 0., tiles));
        *this = Diagnostics{lines, time};
    }
};

struct State {
    TileCache cache;
    unsigned generation = ~0u;
    int dimension = -1;
    unsigned revision = 0;
    std::vector<std::uint32_t> terrain, image;
    struct Key {
        double x = NAN, z = NAN, yaw = NAN;
        int zoom = -1;
        bool round = false, rotate = false;
        unsigned revision = ~0u;
    } composed, shown;
    double composedAt = 0, evictedAt = 0;
    bool uploaded = false, failed = false, everUploaded = false;
    Diagnostics diagnostics;
};
State state;

void releaseTexture(IClientInstance& client) {
    if (!state.uploaded) return;
    state.uploaded = false;
    try {
        if (auto group = client.getTextureGroup()) group->unloadTexture(textureLocation(), false);
    } catch (...) {}
}
void forget() {
    state.cache.clear();
    state.terrain.clear();
    state.image.clear();
    state.composed = state.shown = {};
    ++state.revision;
}

std::uint32_t mapColor(BlockSource& region, BlockPos const& pos) {
    auto const& block = region.getBlock(pos);
    auto color = block.getBlockType().getMapColor(region, pos, block);
    if (!(color.a > 0)) return 0;
    auto byte = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.f, 1.f) * 255)); };
    return packColor(byte(color.r), byte(color.g), byte(color.b));
}
// One chunk's surface: the heightmap gives the top light-blocking block;
// blocks without a map color (glass, plants) are looked through.
void scanChunk(BlockSource& region, ChunkKey key, short minY, double time) {
    auto& tile = state.cache.put(key);
    tile.scannedAt = time;
    tile.loaded = false;
    BlockPos origin{key.x * 16, minY, key.z * 16};
    if (!region.getChunkAt(origin)) return;
    bool any = false;
    for (int dz = 0; dz < 16; ++dz)
        for (int dx = 0; dx < 16; ++dx) {
            int x = origin.x + dx, z = origin.z + dz;
            auto top = region.getHeightmapPos(BlockPos{x, 0, z});
            Column column{};
            for (int y = top.y - 1, steps = 0; y >= minY && steps < 16; --y, ++steps) {
                if (auto color = mapColor(region, BlockPos{x, y, z})) {
                    column = {color, static_cast<std::int16_t>(y)};
                    break;
                }
            }
            tile.columns[static_cast<size_t>(columnIndex(x, z))] = column;
            any = any || column.color;
        }
    // A chunk without any surface has not received its blocks yet.
    tile.loaded = any;
}
void scan(LocalPlayer& player, double centerX, double centerZ, int zoom, bool rotate, double time) {
    auto& region = player.getDimensionBlockSource();
    short minY = region.getMinHeight();
    auto center = chunkOf(blockFloor(centerX), blockFloor(centerZ));
    auto start = now();
    for (auto key : scanOrder(state.cache, center, chunkRadius(blocksAcross(zoom), rotate), time, 64)) {
        scanChunk(region, key, minY, time);
        ++state.revision;
        ++state.diagnostics.chunks;
        if (now() - start >= scanBudgetSeconds) break;
    }
    state.diagnostics.scanSeconds += now() - start;
    if (time - state.evictedAt > 1) {
        state.cache.evict(center, keepChunks);
        state.evictedAt = time;
    }
}

bool upload(IClientInstance& client) {
    auto group = client.getTextureGroup();
    if (!group) return false;
    auto start = now();
    mce::Image image(pixels, pixels, mce::ImageFormat::RGBA8Unorm, mce::ImageUsage::SRGB);
    image.mAlphaUsage = mce::AlphaUsage::Transparent;
    image.setRawImage(mce::Blob(reinterpret_cast<std::uint8_t const*>(state.image.data()),
                                state.image.size() * sizeof(std::uint32_t)));
    cg::ImageBuffer buffer(std::move(image));
    if (state.uploaded) {
        if (!group->updateTextureInPlace(textureLocation(), std::move(buffer))) {
            state.uploaded = false;
            state.diagnostics.log("in-place update refused; uploading again");
            return false;
        }
    } else {
        group->uploadTexture(textureLocation(), std::move(buffer));
        state.uploaded = true;
        if (!state.everUploaded) state.diagnostics.log(std::format("texture uploaded in {:.2f} ms", (now() - start) * 1000));
        state.everUploaded = true;
    }
    state.diagnostics.uploadSeconds += now() - start;
    ++state.diagnostics.uploads;
    return true;
}

// Owned per-frame values; nothing from the game outlives the call.
struct Snapshot {
    double x, y, z;
    float yaw;
    int dimension;
    std::string biome;
};
std::optional<Snapshot> snapshot(IClientInstance& client, bool biome) {
    auto* player = client.getLocalPlayer();
    if (!player) return std::nullopt;
    auto p = player->getFeetPos();
    auto rotation = player->getRotation();
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return std::nullopt;
    Snapshot value{p.x, p.y, p.z, std::isfinite(rotation.z) ? rotation.z : 0.f,
                   static_cast<int>(player->getDimensionId()), {}};
    if (biome) {
        BlockPos pos{blockFloor(p.x), blockFloor(p.y), blockFloor(p.z)};
        auto& region = player->getDimensionBlockSource();
        if (region.getChunkAt(pos)) value.biome = information::biomeName(region.getBiome(pos).mHash->getString());
    }
    return value;
}
}

std::optional<ui::hud_editor::Box> drawMinimap(MinecraftUIRenderContext& context, float width, float height,
                                               ui::HudElement const& element, Settings::Map const& settings,
                                               bool preview) {
    auto& client = context.mClient;
    if (!preview && !settings.minimap) {
        if (state.uploaded || state.cache.size()) {
            releaseTexture(client);
            forget();
        }
        return std::nullopt;
    }
    if (state.failed) return std::nullopt;
    auto view = snapshot(client, settings.biome);
    if (!view) return std::nullopt;
    try {
        auto time = now();
        unsigned generation = worldGeneration.load();
        if (generation != state.generation || view->dimension != state.dimension) {
            forget();
            state.generation = generation;
            state.dimension = view->dimension;
        }
        int zoom = clampZoomIndex(settings.zoom);
        if (auto* player = client.getLocalPlayer()) scan(*player, view->x, view->z, zoom, settings.rotate, time);

        State::Key key{view->x, view->z, settings.rotate ? view->yaw : 0.f, zoom, settings.round, settings.rotate,
                       state.revision};
        auto const& last = state.composed;
        bool layoutChanged = key.zoom != last.zoom || key.round != last.round || key.rotate != last.rotate;
        double blocks = blocksAcross(zoom), perPixel = blocks / pixels;
        bool moved = !(std::abs(key.x - last.x) < perPixel / 4 && std::abs(key.z - last.z) < perPixel / 4)
            || (key.rotate && !(std::abs(key.yaw - last.yaw) < .25));
        bool dataChanged = key.revision != last.revision;
        auto transform = settings.rotate ? ViewTransform::headingUp(view->yaw) : ViewTransform::northUp();
        float zoomScale = std::clamp(std::isfinite(element.scale) ? element.scale : 100.f, 75.f, 150.f) / 100;
        float size = std::round(height / 5 * zoomScale);
        if (layoutChanged || ((moved || dataChanged) && time - state.composedAt >= terrainInterval)) {
            auto start = now();
            composeTerrain(state.cache, Frame{pixels, view->x, view->z, blocks, transform, settings.round}, state.terrain);
            state.diagnostics.composeSeconds += now() - start;
            ++state.diagnostics.composes;
            state.composed = key;
            state.composedAt = time;
        }
        double arrowAngle = settings.rotate ? 0 : northUpArrowAngle(view->yaw);
        State::Key shownKey = state.composed;
        shownKey.yaw = arrowAngle;
        auto const& shown = state.shown;
        bool redraw = !state.uploaded || shownKey.revision != shown.revision || shownKey.x != shown.x
            || shownKey.z != shown.z || shownKey.zoom != shown.zoom || shownKey.round != shown.round
            || shownKey.rotate != shown.rotate || !(std::abs(shownKey.yaw - shown.yaw) < .004);
        if (redraw && !state.terrain.empty()) {
            state.image = state.terrain;
            drawArrow(state.image, pixels, pixels / 2.0, pixels / 2.0, arrowAngle, pixels * 16.0 / 216);
            drawFrame(state.image, pixels, settings.round, pixels / std::max(16.f, size));
            if (upload(client)) state.shown = shownKey;
        }
        state.diagnostics.report(time, state.cache.size());

        // Layout: the map, then the optional lines under it.
        std::vector<std::string> lines;
        if (settings.coordinates)
            lines.push_back(std::format("{}, {}, {}", blockFloor(view->x), blockFloor(view->y), blockFloor(view->z)));
        if (settings.biome && !view->biome.empty()) lines.push_back(view->biome);
        float textScale = zoomScale, lineHeight = 12 * textScale;
        float linesWidth = 0;
        for (auto const& line : lines) linesWidth = std::max(linesWidth, ui::textWidthScaled(context, line, textScale));
        bool card = element.background == ui::ElementBackground::Card;
        float pad = card ? 3 : 0;
        float contentW = std::max(size, linesWidth), contentH = size + (lines.empty() ? 0 : 2 + lines.size() * lineHeight);
        float boxW = contentW + 2 * pad, boxH = contentH + 2 * pad;
        auto placement = ui::placeElement(width, height, boxW, boxH, element);
        if (card) ui::card(context, placement.x, placement.y, boxW, boxH);
        auto factors = ui::anchorFactors(element.anchor);
        float mapX = placement.x + pad + (contentW - size) * factors.x, mapY = placement.y + pad;
        if (state.uploaded && !ui::runtimeImage(context, textureLocation(), {mapX, mapY, size, size})) {
            // Resource reloads drop runtime textures; upload again next frame.
            state.uploaded = false;
            state.diagnostics.log("texture missing at draw; uploading again");
        }
        if (settings.compass) {
            float inset = 6 * textScale;
            auto points = compassPoints(transform, pixels, inset * pixels / size, settings.round);
            constexpr std::array<std::string_view, 4> letters{"N", "E", "S", "W"};
            for (size_t i = 0; i < 4; ++i) {
                float cx = mapX + static_cast<float>(points[i].x) * size / pixels;
                float cy = mapY + static_cast<float>(points[i].y) * size / pixels;
                ui::labelScaled(context, cx - 10, cy - 5 * textScale, 20, std::string(letters[i]), textScale,
                                i == 0 ? ui::Rgb{1.f, .54f, .48f} : ui::palette::text, ui::Align::Center, true);
            }
        }
        auto align = factors.x == 0 ? ui::Align::Left : factors.x == 1 ? ui::Align::Right : ui::Align::Center;
        for (size_t i = 0; i < lines.size(); ++i)
            ui::labelScaled(context, placement.x + pad, mapY + size + 2 + i * lineHeight, contentW, lines[i], textScale,
                            ui::palette::text, align, element.shadow);
        context.flushText(0, std::nullopt);
        return ui::hud_editor::Box{placement.x, placement.y, boxW, boxH};
    } catch (std::exception const& error) {
        // Fail open: no minimap for the rest of the session, the game unaffected.
        state.failed = true;
        state.diagnostics.log(std::format("disabled after an error: {}", error.what()));
    } catch (...) {
        state.failed = true;
        state.diagnostics.log("disabled after an unknown error");
    }
    return std::nullopt;
}

void start() {
    auto& bus = ll::event::EventBus::getInstance();
    exitListener = bus.emplaceListener<ll::event::ClientExitLevelEvent>([](auto&) { ++worldGeneration; });
    joinListener = bus.emplaceListener<ll::event::ClientJoinLevelEvent>([](auto&) { ++worldGeneration; });
    if (!exitListener || !joinListener) {
        stop();
        throw std::runtime_error("Could not subscribe minimap world changes");
    }
}
void stop() {
    for (auto* listener : {&exitListener, &joinListener})
        if (*listener) {
            ll::event::EventBus::getInstance().removeListener(*listener);
            listener->reset();
        }
}
}
