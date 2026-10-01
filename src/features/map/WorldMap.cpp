#include "features/map/WorldMap.h"
#include "features/map/MapImage.h"
#include "features/map/MapRadar.h"
#include "features/map/MapStore.h"
#include "features/map/Minimap.h"
#include "features/map/WaypointSession.h"
#include "features/map/WorldMapView.h"
#include "features/information/InfoHud.h"
#include "app/Runtime.h"
#include "ui/Localization.h"
#include "ui/Widgets.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/gui/GuiData.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/TextureGroup.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/deps/core/container/Blob.h"
#include "mc/deps/core/file/PathView.h"
#include "mc/deps/core/image/Image.h"
#include "mc/deps/core/resource/ResourceLocation.h"
#include "mc/deps/core_graphics/ImageBuffer.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include <chrono>
#include <cmath>
#include <format>

namespace lamium::map {
std::vector<Dot> collectDots(IClientInstance& client, double centerX, double centerZ, double reach, double playerY,
                             bool invisible);
}
namespace lamium::map::world {
namespace {
using ui::Rgb;
constexpr float rowHeight = 15, bottomHeight = 13, buttonHeight = 11;
constexpr Rgb ground{11 / 255.f, 12 / 255.f, 13 / 255.f};

double now() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}
void log(std::string const& text) {
    static int lines = 0;
    if (++lines > 20) return;
    try { Runtime::instance().self().getLogger().info("World map screen: {}", text); } catch (...) {}
}

// Runtime textures: a fixed pool of locations, never destroyed (their
// destructor is game code, which must not run while the process tears down).
constexpr int slotCount = 96;
// Uploads per frame; the first frames after opening may upload more.
constexpr int uploadsPerFrame = 6, uploadsWhileOpening = 16;
ResourceLocation const& slotLocation(int index) {
    static std::array<ResourceLocation*, slotCount + 1> all{};
    auto& location = all[static_cast<size_t>(index)];
    if (!location)
        location = new ResourceLocation(Core::PathView(index == slotCount ? std::string("lamium/worldmap/arrow")
                                                                         : std::format("lamium/worldmap/{}", index)),
                                        ResourceFileSystem::Raw);
    return *location;
}
struct Slot {
    std::optional<std::pair<MapLayer, TileKey>> tile;
    unsigned version = 0;
    bool uploaded = false;
    double used = 0;
};
std::array<Slot, slotCount> slots;
bool upload(IClientInstance& client, ResourceLocation const& location, std::vector<std::uint32_t> const& pixels, int side,
            bool replace) {
    auto group = client.getTextureGroup();
    if (!group) return false;
    mce::Image image(side, side, mce::ImageFormat::RGBA8Unorm, mce::ImageUsage::SRGB);
    image.mAlphaUsage = mce::AlphaUsage::Transparent;
    image.setRawImage(mce::Blob(reinterpret_cast<std::uint8_t const*>(pixels.data()), pixels.size() * sizeof(std::uint32_t)));
    cg::ImageBuffer buffer(std::move(image));
    if (replace) return group->updateTextureInPlace(location, std::move(buffer));
    group->uploadTexture(location, std::move(buffer));
    return true;
}
void unload(IClientInstance& client, ResourceLocation const& location) {
    try {
        if (auto group = client.getTextureGroup()) group->unloadTexture(location, false);
    } catch (...) {}
}

enum class Target { None, Map, Overworld, Nether, End, BandDown, BandUp, AutoBand, Center, Waypoints, Close, Menu };
struct Hit {
    float x, y, w, h;
    Target target;
    int item = -1;
};
enum class MenuKind { Ground, Waypoint, Death };
struct Menu {
    MenuKind kind;
    float x, y;    // Screen, where it was opened.
    int index = -1; // Waypoint: into the set.
    int worldX = 0, worldY = 0, worldZ = 0;
    bool known = false; // Ground: the height came from the saved map.
    bool armed = false; // Delete pressed once.
};
struct Marker {
    float x, y;
    MenuKind kind;
    int index;
};
struct State {
    bool open = false;
    IClientInstance* client = nullptr;
    WorldView view;
    int dimension = 0, band = 4;
    struct Drag {
        float x, y;
        double centerX, centerZ;
        bool moved = false;
    };
    std::optional<Drag> drag;
    std::optional<Menu> menu;
    std::vector<Hit> hits;
    std::vector<Marker> markers;
    glm::vec2 pointer{};
    std::string notice;
    double noticeAt = 0, openedAt = 0;
    float top = rowHeight; // The top bar, one or two rows.
    float arrowAngle = NAN;
    bool arrowUploaded = false;
} state;

struct PlayerSpot {
    double x, y, z;
    float yaw;
    int dimension;
};
std::optional<PlayerSpot> playerSpot() {
    auto* player = state.client ? state.client->getLocalPlayer() : nullptr;
    if (!player) return std::nullopt;
    auto feet = player->getFeetPos();
    if (!std::isfinite(feet.x) || !std::isfinite(feet.y) || !std::isfinite(feet.z)) return std::nullopt;
    auto rotation = player->getRotation();
    return PlayerSpot{feet.x, feet.y, feet.z, std::isfinite(rotation.z) ? rotation.z : 0.f,
                      static_cast<int>(player->getDimensionId())};
}
// The player's place seen from another dimension: the Nether is 1/8 scale.
std::optional<std::pair<double, double>> playerOn(int dimension) {
    auto spot = playerSpot();
    if (!spot) return std::nullopt;
    double k = spot->dimension == dimension ? 1 : spot->dimension == 0 && dimension == 1 ? 1 / 8. :
        spot->dimension == 1 && dimension == 0 ? 8. : 1;
    return std::pair{spot->x * k, spot->z * k};
}
void center() {
    if (auto at = playerOn(state.dimension)) {
        state.view.centerX = at->first;
        state.view.centerZ = at->second;
    }
}
bool netherAuto() { return Runtime::instance().preferences().map.worldMapNetherAuto; }
int shownBand() {
    auto spot = playerSpot();
    if (state.dimension == 1 && netherAuto() && spot && spot->dimension == 1)
        return std::clamp(floorDiv(blockFloor(spot->y), bandHeight), 0, netherBands - 1);
    return state.band;
}
MapLayer shownLayer() { return state.dimension == 1 ? MapLayer{1, shownBand()} : MapLayer{state.dimension, 0}; }
void setNetherAuto(bool on) {
    auto value = Runtime::instance().preferences();
    if (value.map.worldMapNetherAuto == on) return;
    value.map.worldMapNetherAuto = on;
    if (!Runtime::instance().save(value)) log("could not save the Nether layer setting");
}
void say(std::string text) {
    state.notice = std::move(text);
    state.noticeAt = now();
}
void showDimension(int dimension) {
    if (state.dimension == dimension) return;
    state.dimension = dimension;
    state.menu.reset();
    center();
}

void act(Target target) {
    switch (target) {
    case Target::Overworld: showDimension(0); break;
    case Target::Nether: showDimension(1); break;
    case Target::End: showDimension(2); break;
    case Target::BandDown:
    case Target::BandUp:
        state.band = std::clamp(shownBand() + (target == Target::BandUp ? 1 : -1), 0, netherBands - 1);
        setNetherAuto(false);
        break;
    case Target::AutoBand:
        state.band = shownBand();
        setNetherAuto(!netherAuto());
        break;
    case Target::Center: center(); break;
    default: break;
    }
}
std::vector<std::string> menuItems(Menu const& menu, WaypointSet const& set) {
    if (menu.kind == MenuKind::Ground) return {ui::translated("worldMap.addHere")};
    if (menu.kind == MenuKind::Death)
        return {ui::translated("waypoint.keep"), ui::translated(menu.armed ? "worldMap.deleteArmed" : "worldMap.delete")};
    bool visible = menu.index >= 0 && menu.index < static_cast<int>(set.waypoints.size())
        && set.waypoints[static_cast<size_t>(menu.index)].visible;
    return {ui::translated("worldMap.edit"), ui::translated(visible ? "worldMap.hide" : "worldMap.show"),
            ui::translated(menu.armed ? "worldMap.deleteArmed" : "worldMap.delete")};
}
Request chooseMenu(int item) {
    auto menu = *state.menu;
    auto set = waypoints::current();
    Request request;
    if (menu.kind == MenuKind::Ground) {
        state.menu.reset();
        request.kind = Request::Kind::AddWaypoint;
        auto& d = request.draft;
        d.x = menu.worldX; d.y = menu.worldY; d.z = menu.worldZ;
        d.dimension = state.dimension;
        d.color = nextColor(set.lastColor);
        d.name = defaultWaypointName(set.waypoints, [](int n) { return ui::translated("waypoint.defaultName", n); });
        return request;
    }
    if (menu.kind == MenuKind::Death) {
        if (item == 0 && set.death) {
            state.menu.reset();
            auto death = *set.death;
            Waypoint w{ui::translated("waypoint.deathName"), nextColor(set.lastColor), death.x, death.y, death.z,
                       death.dimension, true};
            bool saved = waypoints::change([&](WaypointSet& s) {
                s.waypoints.push_back(w);
                s.lastColor = w.color;
                s.death.reset();
                return true;
            });
            say(ui::translated(saved ? "waypoint.added" : "waypoint.saveError", w.name));
        } else if (item == 1) {
            if (!menu.armed) { state.menu->armed = true; return request; }
            state.menu.reset();
            if (!waypoints::change([](WaypointSet& s) { s.death.reset(); return true; }))
                say(ui::translated("waypoint.saveError"));
        }
        return request;
    }
    int index = menu.index;
    if (index < 0 || index >= static_cast<int>(set.waypoints.size())) { state.menu.reset(); return request; }
    auto name = set.waypoints[static_cast<size_t>(index)].name;
    if (item == 0) {
        state.menu.reset();
        request.kind = Request::Kind::EditWaypoint;
        request.index = index;
    } else if (item == 1) {
        state.menu.reset();
        if (!waypoints::change([&](WaypointSet& s) {
                if (index >= static_cast<int>(s.waypoints.size())) return false;
                auto& w = s.waypoints[static_cast<size_t>(index)];
                w.visible = !w.visible;
                return true;
            })) say(ui::translated("waypoint.saveError"));
    } else if (item == 2) {
        if (!menu.armed) { state.menu->armed = true; return request; }
        state.menu.reset();
        bool saved = waypoints::change([&](WaypointSet& s) {
            if (index >= static_cast<int>(s.waypoints.size())) return false;
            s.waypoints.erase(s.waypoints.begin() + index);
            return true;
        });
        say(ui::translated(saved ? "worldMap.deleted" : "waypoint.saveError", name));
    }
    return request;
}
Hit const* hitAt(float x, float y) {
    // Later entries are drawn on top.
    for (auto it = state.hits.rbegin(); it != state.hits.rend(); ++it)
        if (x >= it->x && y >= it->y && x < it->x + it->w && y < it->y + it->h) return &*it;
    return nullptr;
}
std::optional<Marker> markerAt(float x, float y) {
    std::optional<Marker> best;
    float bestDistance = 6;
    for (auto const& m : state.markers) {
        float d = std::hypot(m.x - x, m.y - y);
        if (d < bestDistance) { bestDistance = d; best = m; }
    }
    return best;
}
void openMenu(float x, float y) {
    if (auto marker = markerAt(x, y)) {
        state.menu = Menu{marker->kind, x, y, marker->index};
        return;
    }
    int wx = blockFloor(state.view.worldX(x)), wz = blockFloor(state.view.worldZ(y));
    Menu menu{MenuKind::Ground, x, y};
    menu.worldX = wx;
    menu.worldZ = wz;
    if (auto column = store::column(shownLayer(), wx, wz)) {
        menu.worldY = column->height + 1;
        menu.known = true;
    } else if (auto spot = playerSpot()) menu.worldY = blockFloor(spot->y);
    state.menu = menu;
}

// ---- Drawing ----
Rgb rgb(std::uint32_t color) { return {channel(color, 0) / 255.f, channel(color, 1) / 255.f, channel(color, 2) / 255.f}; }
void diamond(MinecraftUIRenderContext& context, float cx, float cy, int size, Rgb color, float opacity) {
    auto rows = diamondRows(size);
    float top = cy - static_cast<float>(rows.size()) / 2;
    for (size_t i = 0; i < rows.size(); ++i) {
        float half = static_cast<float>(rows[i]) + .5f;
        ui::fill(context, cx - half, top + i, 2 * half, 1, Rgb{0, 0, 0}, .85f * opacity);
    }
    auto inner = diamondRows(size - 2);
    top = cy - static_cast<float>(inner.size()) / 2;
    for (size_t i = 0; i < inner.size(); ++i) {
        float half = static_cast<float>(inner[i]) + .5f;
        ui::fill(context, cx - half, top + i, 2 * half, 1, color, opacity);
    }
}
void cross(MinecraftUIRenderContext& context, float cx, float cy) {
    for (int pass = 0; pass < 2; ++pass)
        for (int i = -2; i <= 2; ++i)
            for (int sign : {1, -1}) {
                float x = cx + i - .5f, y = cy + sign * i - .5f;
                if (pass == 0) ui::fill(context, x - 1, y - 1, 3, 3, Rgb{0, 0, 0}, .85f);
                else ui::fill(context, x, y, 1, 1, rgb(deathColor));
            }
}
void smallLabel(MinecraftUIRenderContext& context, float cx, float y, std::string const& text, Rgb color) {
    float scale = .75f, w = ui::textWidthScaled(context, text, scale);
    ui::labelScaled(context, cx - w / 2, y, w + 2, text, scale, color, ui::Align::Left, true);
}
float button(MinecraftUIRenderContext& context, float x, float y, std::string const& text, Target target, bool on,
             bool hover, int item = -1) {
    float w = ui::textWidth(context, text) + 8;
    ui::fill(context, x, y, w, buttonHeight, on ? ui::palette::accentDeep : hover ? Rgb{.23f, .23f, .24f} : ui::palette::keyFill, .9f);
    ui::frame(context, x, y, w, buttonHeight, on ? ui::palette::accent : ui::palette::keyEdge);
    ui::label(context, x, y + ui::boxTextInset(), w, text, on || hover ? ui::palette::text : ui::palette::dim, ui::Align::Center);
    state.hits.push_back({x, y, w, buttonHeight, target, item});
    return w;
}
bool hovering(float x, float y, float w, float h) {
    return state.pointer.x >= x && state.pointer.y >= y && state.pointer.x < x + w && state.pointer.y < y + h;
}

void drawTiles(MinecraftUIRenderContext& context, glm::vec2 size, double pixelsPerUnit, MapLayer layer, int& empty,
               int& shown) {
    auto& client = context.mClient;
    auto& view = state.view;
    int lod = lodFor(view.scale(), pixelsPerUnit);
    auto tiles = visibleTiles(view, lod);
    store::want(layer, tiles);
    double time = now();
    int budget = time - state.openedAt < 1 ? uploadsWhileOpening : uploadsPerFrame;
    std::vector<bool> usedNow(slotCount, false);
    auto findSlot = [&](TileKey tile) -> int {
        for (int i = 0; i < slotCount; ++i)
            if (slots[static_cast<size_t>(i)].tile && slots[static_cast<size_t>(i)].tile->first == layer
                && slots[static_cast<size_t>(i)].tile->second == tile) return i;
        return -1;
    };
    auto rectOf = [&](TileKey tile) {
        double blocks = tileBlocks(tile.lod);
        float x0 = static_cast<float>(snapToPixel(view.screenX(tile.x * blocks), pixelsPerUnit));
        float x1 = static_cast<float>(snapToPixel(view.screenX((tile.x + 1) * blocks), pixelsPerUnit));
        float y0 = static_cast<float>(snapToPixel(view.screenY(tile.z * blocks), pixelsPerUnit));
        float y1 = static_cast<float>(snapToPixel(view.screenY((tile.z + 1) * blocks), pixelsPerUnit));
        return ui::ImageRect{x0, y0, x1 - x0, y1 - y0};
    };
    auto drawSlot = [&](int index, ui::ImageRect rect, float u, float v, float span) {
        auto& slot = slots[static_cast<size_t>(index)];
        slot.used = time;
        usedNow[static_cast<size_t>(index)] = true;
        if (!ui::runtimeImage(context, slotLocation(index), rect, u, v, span, span)) slot.uploaded = false;
    };
    for (auto tile : tiles) {
        auto image = store::image(layer, tile);
        if (image && !image->pixels) { ++empty; continue; }
        int index = findSlot(tile);
        bool current = index >= 0 && slots[static_cast<size_t>(index)].uploaded
            && slots[static_cast<size_t>(index)].version == image->version;
        if (image && !current && budget > 0) {
            if (index < 0) {
                double oldest = INFINITY;
                for (int i = 0; i < slotCount; ++i) {
                    auto const& s = slots[static_cast<size_t>(i)];
                    if (usedNow[static_cast<size_t>(i)]) continue;
                    double age = s.tile ? s.used : -INFINITY;
                    if (age < oldest) { oldest = age; index = i; }
                }
            }
            if (index >= 0) {
                auto& slot = slots[static_cast<size_t>(index)];
                --budget;
                try {
                    if (upload(client, slotLocation(index), *image->pixels, regionBlocks, slot.uploaded)) {
                        slot.uploaded = true;
                        slot.tile = std::pair{layer, tile};
                        slot.version = image->version;
                    } else slot = Slot{};
                } catch (...) { slot = Slot{}; }
            }
        }
        if (index >= 0 && slots[static_cast<size_t>(index)].uploaded && slots[static_cast<size_t>(index)].tile
            && slots[static_cast<size_t>(index)].tile->second == tile) {
            drawSlot(index, rectOf(tile), 0, 0, 1);
            ++shown;
            continue;
        }
        // Not ready: a coarser image already on the GPU stands in.
        for (int up = 1; up <= 2; ++up) {
            TileKey parent{tile.lod + up, tile.x >> up, tile.z >> up};
            int at = findSlot(parent);
            if (at < 0 || !slots[static_cast<size_t>(at)].uploaded) continue;
            float span = 1.f / static_cast<float>(1 << up);
            drawSlot(at, rectOf(tile), (tile.x - (parent.x << up)) * span, (tile.z - (parent.z << up)) * span, span);
            break;
        }
    }
    (void)size;
}

void drawMarkers(MinecraftUIRenderContext& context, Settings::Map const& settings) {
    auto& view = state.view;
    state.markers.clear();
    auto spot = playerSpot();
    if (settings.radar && settings.radarPlayers && spot && spot->dimension == state.dimension) {
        for (auto const& dot : collectDots(context.mClient, spot->x, spot->z, 3.0e7, spot->y, settings.radarInvisible)) {
            if (dot.kind != DotKind::Player) continue;
            float x = static_cast<float>(view.screenX(dot.x)), y = static_cast<float>(view.screenY(dot.z));
            ui::fill(context, x - 2, y - 2, 4, 4, Rgb{0, 0, 0}, .85f);
            ui::fill(context, x - 1.5f, y - 1.5f, 3, 3, rgb(dotColor(DotKind::Player)));
            if (!dot.name.empty()) smallLabel(context, x, y + 3, dot.name, Rgb{.59f, .88f, 1.f});
        }
    }
    if (settings.waypoints) {
        auto set = waypoints::current();
        for (size_t i = 0; i < set.waypoints.size(); ++i) {
            auto const& w = set.waypoints[i];
            auto at = shownPosition(w.x, w.y, w.z, w.dimension, state.dimension, settings.waypointsCrossScale);
            if (!at) continue;
            float x = std::round(static_cast<float>(view.screenX(at->x))), y = std::round(static_cast<float>(view.screenY(at->z)));
            if (x < -20 || y < -20 || x > view.width + 20 || y > view.height + 20) continue;
            // Hidden ones stay faint here so they can be shown again.
            float opacity = w.visible ? 1.f : .35f;
            diamond(context, x, y, 9, rgb(waypointColors[static_cast<size_t>(clampColor(w.color))]), opacity);
            auto name = w.visible ? w.name : w.name + " " + ui::translated("worldMap.hidden");
            smallLabel(context, x, y + 6, name, w.visible ? ui::palette::text : ui::palette::faint);
            state.markers.push_back({x, y, MenuKind::Waypoint, static_cast<int>(i)});
        }
        if (set.death && set.death->dimension == state.dimension) {
            float x = std::round(static_cast<float>(view.screenX(set.death->x + .5)));
            float y = std::round(static_cast<float>(view.screenY(set.death->z + .5)));
            cross(context, x, y);
            if (hovering(x - 5, y - 5, 10, 10)) smallLabel(context, x, y + 5, ui::translated("waypoint.death"), ui::palette::text);
            state.markers.push_back({x, y, MenuKind::Death, -1});
        }
    }
    if (spot && spot->dimension == state.dimension) {
        // The arrow is drawn into a small texture, uploaded again when it turns.
        constexpr int side = 32;
        float angle = static_cast<float>(northUpArrowAngle(spot->yaw));
        auto& client = context.mClient;
        if (!state.arrowUploaded || !(std::abs(angle - state.arrowAngle) < .01f)) {
            std::vector<std::uint32_t> pixels(side * side, 0);
            drawArrow(pixels, side, side / 2.0, side / 2.0, angle, 22);
            try {
                if (upload(client, slotLocation(slotCount), pixels, side, state.arrowUploaded)) {
                    state.arrowUploaded = true;
                    state.arrowAngle = angle;
                } else state.arrowUploaded = false;
            } catch (...) { state.arrowUploaded = false; }
        }
        float x = static_cast<float>(view.screenX(spot->x)), y = static_cast<float>(view.screenY(spot->z));
        if (state.arrowUploaded && !ui::runtimeImage(context, slotLocation(slotCount), {x - 8, y - 8, 16, 16}))
            state.arrowUploaded = false;
    }
}

void drawBars(MinecraftUIRenderContext& context, glm::vec2 size, MapLayer layer, size_t pending) {
    auto spot = playerSpot();
    float inset = ui::boxTextInset();
    auto widthOf = [&](std::string const& text) { return ui::textWidth(context, text) + 8; };
    // Top bar: one row when everything fits, else the Nether layer moves to
    // a second row; on a very narrow screen the title goes first.
    constexpr std::array<std::pair<Target, std::string_view>, 3> dims{{
        {Target::Overworld, "dimension.overworld"}, {Target::Nether, "dimension.nether"}, {Target::End, "dimension.end"}}};
    std::array<std::string, 3> dimTexts;
    float dimsW = 8;
    for (int d = 0; d < 3; ++d) {
        dimTexts[static_cast<size_t>(d)] = (spot && spot->dimension == d ? "* " : "") + ui::translated(dims[static_cast<size_t>(d)].second);
        dimsW += widthOf(dimTexts[static_cast<size_t>(d)]) - 1;
    }
    constexpr std::array<std::pair<Target, std::string_view>, 3> rights{{
        {Target::Close, "worldMap.close"}, {Target::Waypoints, "nav.waypoints"}, {Target::Center, "worldMap.center"}}};
    float rightW = 0;
    for (auto const& [target, key] : rights) rightW += widthOf(ui::translated(key)) + 3;
    auto layerText = ui::translated("worldMap.layer", layer.band * bandHeight, layer.band * bandHeight + bandHeight - 1);
    auto myHeight = ui::translated("worldMap.myHeight");
    float layerW = state.dimension == 1 ? 10 + ui::textWidth(context, layerText) + 10 + 10 + 3 + widthOf(myHeight) + 8 : 0;
    float brandW = ui::textWidth(context, "Lamium") + 4;
    auto title = "> " + ui::translated("feature.worldMap");
    float titleW = ui::textWidth(context, title) + 8;
    bool showTitle = 4 + brandW + titleW + dimsW + rightW + 4 <= size.x;
    bool oneRow = 4 + brandW + (showTitle ? titleW : 0) + dimsW + layerW + rightW + 4 <= size.x;
    state.top = oneRow || state.dimension != 1 ? rowHeight : 2 * rowHeight;
    ui::fill(context, 0, 0, size.x, state.top, ui::palette::panel, .85f);
    ui::fill(context, 0, state.top - 1, size.x, 1, ui::palette::white, .14f);
    float x = 4, y = 2;
    ui::label(context, x, y + inset, brandW, "Lamium");
    x += brandW;
    if (showTitle) {
        ui::label(context, x, y + inset, titleW, title, ui::palette::faint);
        x += titleW;
    }
    for (int d = 0; d < 3; ++d) {
        auto const& text = dimTexts[static_cast<size_t>(d)];
        float w = widthOf(text);
        x += button(context, x, y, text, dims[static_cast<size_t>(d)].first, state.dimension == d, hovering(x, y, w, buttonHeight)) - 1;
    }
    x += 8;
    if (state.dimension == 1) {
        if (!oneRow) { x = 4; y = rowHeight + 2; }
        float w = 10;
        ui::fill(context, x, y, w, buttonHeight, ui::palette::keyFill);
        ui::frame(context, x, y, w, buttonHeight, ui::palette::keyEdge);
        ui::arrow(context, x + 3, y + 2.5f, true, hovering(x, y, w, buttonHeight) ? ui::palette::text : ui::palette::dim);
        state.hits.push_back({x, y, w, buttonHeight, Target::BandDown});
        x += w;
        float tw = ui::textWidth(context, layerText) + 10;
        ui::frame(context, x - 1, y, tw + 2, buttonHeight, ui::palette::keyEdge);
        ui::label(context, x, y + inset, tw, layerText, ui::palette::text, ui::Align::Center);
        x += tw;
        ui::fill(context, x, y, w, buttonHeight, ui::palette::keyFill);
        ui::frame(context, x, y, w, buttonHeight, ui::palette::keyEdge);
        ui::arrow(context, x + 3, y + 2.5f, false, hovering(x, y, w, buttonHeight) ? ui::palette::text : ui::palette::dim);
        state.hits.push_back({x, y, w, buttonHeight, Target::BandUp});
        x += w + 3;
        button(context, x, y, myHeight, Target::AutoBand, netherAuto(), hovering(x, y, widthOf(myHeight), buttonHeight));
    }
    float right = size.x - 4;
    for (auto const& [target, key] : rights) {
        auto text = ui::translated(key);
        float w = widthOf(text);
        right -= w;
        button(context, right, 2, text, target, false, hovering(right, 2, w, buttonHeight));
        right -= 3;
    }

    // Bottom bar: the place under the cursor on the left; the scale bar on
    // the right with the hints before it, dropped when they would meet.
    float top = size.y - bottomHeight;
    ui::fill(context, 0, top, size.x, bottomHeight, ui::palette::panel, .85f);
    ui::fill(context, 0, top, size.x, 1, ui::palette::white, .14f);
    float textY = top + 1 + inset;
    std::string where;
    if (state.pointer.y > state.top && state.pointer.y < top) {
        int wx = blockFloor(state.view.worldX(state.pointer.x)), wz = blockFloor(state.view.worldZ(state.pointer.y));
        auto column = store::column(layer, wx, wz);
        where = column ? std::format("X {}  Y {}  Z {}", wx, column->height, wz) : std::format("X {}  Z {}", wx, wz);
        std::string biome;
        if (spot && spot->dimension == state.dimension)
            if (auto* player = state.client ? state.client->getLocalPlayer() : nullptr) {
                auto& region = player->getDimensionBlockSource();
                BlockPos pos{wx, column ? column->height : blockFloor(spot->y), wz};
                if (region.getChunkAt(pos)) biome = information::biomeName(region.getBiome(pos).mHash->getString());
            }
        if (!biome.empty()) where += "  " + biome;
        else if (!column) where += "  " + ui::translated("worldMap.unrecorded");
    }
    float whereW = where.empty() ? 0 : ui::textWidth(context, where);
    ui::label(context, 4, textY, whereW + 2, where, ui::palette::dim);
    int blocks = scaleBarBlocks(state.view.scale(), 30);
    auto scaleText = ui::translated("worldMap.blocks", blocks);
    float barW = static_cast<float>(blocks * state.view.scale());
    float textW = ui::textWidth(context, scaleText);
    float barX = size.x - 4 - textW - 4 - barW;
    ui::fill(context, barX, top + 8, barW, 1, ui::palette::white);
    ui::fill(context, barX, top + 5, 1, 4, ui::palette::white);
    ui::fill(context, barX + barW - 1, top + 5, 1, 4, ui::palette::white);
    ui::label(context, size.x - 4 - textW, textY, textW + 2, scaleText, ui::palette::dim);
    float end = barX - 10, start = 4 + whereW + 10;
    auto hint = ui::translated("worldMap.hint");
    float hintW = ui::textWidthScaled(context, hint, .75f);
    if (end - hintW >= start) {
        ui::labelScaled(context, end - hintW, top + 3, hintW + 2, hint, .75f, ui::palette::faint, ui::Align::Left, false);
        end -= hintW + 10;
    }
    if (pending) {
        auto loading = ui::translated("worldMap.loading", pending);
        float w = ui::textWidth(context, loading);
        if (end - w >= start) ui::label(context, end - w, textY, w + 2, loading, ui::palette::dim);
    }
}

void drawMenu(MinecraftUIRenderContext& context, glm::vec2 size) {
    if (!state.menu) return;
    auto set = waypoints::current();
    auto items = menuItems(*state.menu, set);
    std::string head;
    auto const& m = *state.menu;
    if (m.kind == MenuKind::Ground)
        head = m.known ? std::format("{}, {}, {}", m.worldX, m.worldY, m.worldZ)
                       : std::format("{}, ?, {}  {}", m.worldX, m.worldZ, ui::translated("worldMap.unrecorded"));
    else if (m.kind == MenuKind::Death) {
        if (!set.death) { state.menu.reset(); return; }
        head = ui::translated("waypoint.death") + std::format("  {}, {}, {}", set.death->x, set.death->y, set.death->z);
    } else {
        if (m.index < 0 || m.index >= static_cast<int>(set.waypoints.size())) { state.menu.reset(); return; }
        auto const& w = set.waypoints[static_cast<size_t>(m.index)];
        head = w.name + std::format("  {}, {}, {}", w.x, w.y, w.z);
    }
    constexpr float itemHeight = 11, pad = 4;
    float width = ui::textWidthScaled(context, head, .75f) + 2 * pad;
    for (auto const& item : items) width = std::max(width, ui::textWidth(context, item) + 2 * pad);
    float height = 10 + items.size() * itemHeight + 3;
    float x = std::min(m.x, size.x - width - 2), y = std::min(m.y, size.y - bottomHeight - height - 2);
    ui::fill(context, x, y, width, height, ui::palette::panel, .94f);
    ui::frame(context, x, y, width, height, ui::palette::white, .14f);
    ui::labelScaled(context, x + pad, y + 2, width - 2 * pad, head, .75f, ui::palette::faint);
    ui::fill(context, x + 1, y + 9, width - 2, 1, ui::palette::white, .14f);
    for (size_t i = 0; i < items.size(); ++i) {
        float iy = y + 11 + i * itemHeight;
        bool danger = (m.kind == MenuKind::Waypoint && i == 2) || (m.kind == MenuKind::Death && i == 1);
        if (danger && m.armed) ui::fill(context, x + 1, iy, width - 2, itemHeight, Rgb{.54f, .18f, .16f});
        else if (hovering(x, iy, width, itemHeight)) ui::fill(context, x + 1, iy, width - 2, itemHeight, ui::palette::white, .07f);
        ui::label(context, x + pad, iy + 1, width - 2 * pad, items[i],
                  danger && !m.armed ? Rgb{1.f, .7f, .68f} : ui::palette::text);
        state.hits.push_back({x, iy, width, itemHeight, Target::Menu, static_cast<int>(i)});
    }
    // The menu swallows clicks on its padding too.
    state.hits.push_back({x, y, width, 11, Target::None});
}
}

void open(IClientInstance& client) {
    state.client = &client;
    state.open = true;
    state.drag.reset();
    state.menu.reset();
    state.hits.clear();
    state.markers.clear();
    state.notice.clear();
    state.openedAt = now();
    if (auto spot = playerSpot()) {
        state.dimension = spot->dimension;
        if (spot->dimension == 1) state.band = std::clamp(floorDiv(blockFloor(spot->y), bandHeight), 0, netherBands - 1);
    }
    center();
}
void close() {
    if (state.client) {
        for (int i = 0; i < slotCount; ++i)
            if (slots[static_cast<size_t>(i)].uploaded) unload(*state.client, slotLocation(i));
        if (state.arrowUploaded) unload(*state.client, slotLocation(slotCount));
    }
    slots = {};
    state.arrowUploaded = false;
    state.open = false;
    state.client = nullptr;
    state.drag.reset();
    state.menu.reset();
}
Request press(float x, float y, bool right) {
    Request request;
    auto const* hit = hitAt(x, y);
    if (hit && hit->target == Target::Menu && state.menu) {
        if (right) return request;
        return chooseMenu(hit->item);
    }
    bool menuWasOpen = state.menu.has_value();
    if (!(hit && hit->target == Target::None)) state.menu.reset();
    bool onMap = y > state.top && y < state.view.height - bottomHeight && !hit;
    if (right) {
        if (onMap) openMenu(x, y);
        return request;
    }
    if (hit) {
        switch (hit->target) {
        case Target::Close: request.kind = Request::Kind::Close; return request;
        case Target::Waypoints: request.kind = Request::Kind::EditWaypoint; return request;
        default: act(hit->target); return request;
        }
    }
    if (onMap && !menuWasOpen) state.drag = State::Drag{x, y, state.view.centerX, state.view.centerZ};
    return request;
}
void release() { state.drag.reset(); }
void wheel(float x, float y, int direction) {
    state.menu.reset();
    state.view.zoomAt(x, y, direction);
}
Request key(int key, bool openKey) {
    Request request;
    if (key == 0x1b) {
        if (state.menu) state.menu.reset();
        else request.kind = Request::Kind::Close;
    } else if (key == 0x20) {
        center();
    } else if (openKey) {
        request.kind = Request::Kind::Close;
    }
    return request;
}
void render(MinecraftUIRenderContext& context, glm::vec2 size, glm::vec2 pointer, Settings::Map const& settings) {
    auto& client = context.mClient;
    if (!state.open) open(client);
    state.client = &client;
    // Recording goes on while the map is open: the HUD that drives it is hidden.
    map::record(client, settings);
    state.pointer = pointer;
    auto& view = state.view;
    view.width = size.x;
    view.height = size.y;
    if (state.drag) {
        double dx = pointer.x - state.drag->x, dy = pointer.y - state.drag->y;
        if (std::abs(dx) + std::abs(dy) > 2) state.drag->moved = true;
        if (state.drag->moved) {
            view.centerX = state.drag->centerX;
            view.centerZ = state.drag->centerZ;
            view.pan(dx, dy);
        }
    }
    double inverse = client.getGuiData()->mInvGuiScale;
    double pixelsPerUnit = std::isfinite(inverse) && inverse > 0 ? 1 / inverse : 1;
    auto layer = shownLayer();
    state.hits.clear();
    ui::fill(context, 0, 0, size.x, size.y, ground);
    int empty = 0, shown = 0;
    drawTiles(context, size, pixelsPerUnit, layer, empty, shown);
    size_t pending = store::pending();
    drawMarkers(context, settings);
    // Text is batched: flush each layer so the bars and the menu cover it.
    context.flushText(0, std::nullopt);
    if (!shown && !pending && empty) {
        auto text = ui::translated("worldMap.empty");
        float w = ui::textWidth(context, text) + 12;
        ui::fill(context, (size.x - w) / 2, size.y / 2 - 7, w, 14, ui::palette::panel, .8f);
        ui::label(context, (size.x - w) / 2, size.y / 2 - 4, w, text, ui::palette::dim, ui::Align::Center);
    }
    drawBars(context, size, layer, pending);
    context.flushText(0, std::nullopt);
    drawMenu(context, size);
    if (!state.notice.empty() && now() - state.noticeAt < 2.6) {
        float w = ui::textWidth(context, state.notice) + 12, y = size.y - bottomHeight - 16;
        ui::fill(context, (size.x - w) / 2, y, w, 12, ui::palette::panel, .85f);
        ui::label(context, (size.x - w) / 2, y + 2, w, state.notice, ui::palette::text, ui::Align::Center);
    }
    context.flushText(0, std::nullopt);
}
}
