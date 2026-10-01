// L-85 research probe (xmake option radar_icon_probe, never shipped): for each
// kind of actor seen near the player, logs what its renderer offers for a face
// icon (default skin path, model parts, the head's face UVs) and writes the
// cropped candidate faces to logs/radar-faces.bmp for a visual check.
#include "features/map/RadarIconProbe.h"
#ifdef LAMIUM_RADAR_ICON_PROBE
#include "app/Runtime.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/model/geom/Cube.h"
#include "mc/client/model/geom/ModelPart.h"
#include "mc/client/model/models/Model.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/TextureGroup.h"
#include "mc/client/renderer/actor/ActorRenderDispatcher.h"
#include "mc/client/renderer/actor/DataDrivenRenderer.h"
#include "mc/deps/core/file/PathView.h"
#include "mc/deps/core/resource/ResourceLocation.h"
#include "mc/deps/core_graphics/ImageBuffer.h"
#include "mc/deps/core_graphics/ImageDescription.h"
#include "mc/deps/minecraft_renderer/renderer/TexturePtr.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/level/Level.h"
#include <cmath>
#include <cstdint>
#include <format>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace lamium::map {
namespace {
std::set<std::string> seen;
struct Face { std::string name; std::vector<std::uint32_t> pixels; }; // 8x8 RGBA
std::vector<Face> faces;
void log(std::string const& text) {
    try { Runtime::instance().self().getLogger().info("Radar icon probe: {}", text); } catch (...) {}
}
// Nearest-neighbour crop of a texture rectangle (texture pixels) to 8x8.
std::vector<std::uint32_t> crop(cg::ImageBuffer const& image, float u, float v, float w, float h) {
    std::vector<std::uint32_t> out(64, 0);
    auto const& d = *image.mImageDescription;
    auto const& blob = *image.mStorage;
    int width = static_cast<int>(d.mWidth), height = static_cast<int>(d.mHeight);
    if (width <= 0 || height <= 0 || blob.size() < static_cast<size_t>(width) * height * 4) return out;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            int sx = static_cast<int>(std::floor(u + (x + .5f) * w / 8)), sy = static_cast<int>(std::floor(v + (y + .5f) * h / 8));
            if (sx < 0 || sy < 0 || sx >= width || sy >= height) continue;
            auto const* p = blob.data() + (static_cast<size_t>(sy) * width + sx) * 4;
            out[static_cast<size_t>(y * 8 + x)] = p[0] | p[1] << 8 | p[2] << 16 | std::uint32_t(p[3]) << 24;
        }
    return out;
}
void writeBmp() {
    // Rows of faces, each 8x8 scaled x4 with a 4-pixel gap, on grey.
    constexpr int cell = 36, perRow = 6;
    int rows = (static_cast<int>(faces.size()) + perRow - 1) / perRow;
    int width = perRow * cell, height = std::max(1, rows) * cell;
    std::vector<std::uint32_t> px(static_cast<size_t>(width) * height, 0xff606060);
    for (size_t i = 0; i < faces.size(); ++i) {
        int ox = static_cast<int>(i % perRow) * cell + 2, oy = static_cast<int>(i / perRow) * cell + 2;
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x) {
                auto c = faces[i].pixels[static_cast<size_t>((y / 4) * 8 + x / 4)];
                if ((c >> 24) == 0) continue;
                px[static_cast<size_t>(oy + y) * width + ox + x] = c;
            }
    }
    try {
        auto path = Runtime::instance().self().getModDir() / "logs" / "radar-faces.bmp";
        std::ofstream out(path, std::ios::binary);
        auto put = [&](std::uint32_t v, int bytes) { for (int b = 0; b < bytes; ++b) out.put(static_cast<char>((v >> (8 * b)) & 0xff)); };
        std::uint32_t data = static_cast<std::uint32_t>(width * height * 4);
        out.put('B'); out.put('M'); put(54 + data, 4); put(0, 4); put(54, 4);
        put(40, 4); put(static_cast<std::uint32_t>(width), 4); put(static_cast<std::uint32_t>(-height), 4); put(1, 2); put(32, 2);
        put(0, 4); put(data, 4); put(2835, 4); put(2835, 4); put(0, 4); put(0, 4);
        for (auto c : px) { out.put(static_cast<char>((c >> 16) & 0xff)); out.put(static_cast<char>((c >> 8) & 0xff)); out.put(static_cast<char>(c & 0xff)); out.put(static_cast<char>(0xff)); }
    } catch (...) {}
}
void probe(IClientInstance& client, Actor& actor) {
    auto const& rendererId = actor.getActorRendererId();
    std::string key = rendererId.getString();
    if (key.empty() || !seen.insert(key).second || seen.size() > 64) return;
    std::string line = std::format("#{} renderer '{}' identifier '{}'", faces.size(), key, actor.getActorIdentifier().mFullName.get());
    auto dispatcher = client.getEntityRenderDispatcher();
    if (!dispatcher) { log(line + ": no dispatcher"); return; }
    auto renderer = dispatcher->getDataDrivenRenderer(rendererId);
    if (!renderer) { log(line + ": not data driven"); return; }
    std::string skin;
    if (auto const& location = renderer->mDefaultSkin->mResourceLocationPtr.get(); location)
        skin = location->mPath->value;
    line += std::format(" skin '{}'", skin);
    auto model = renderer->mModel.get();
    if (!model) { log(line + ": no model"); return; }
    auto const& parts = *model->mAllParts;
    line += std::format(" parts {}", parts.size());
    ModelPart const* head = nullptr;
    std::string names;
    for (auto const* part : parts) {
        if (!part) continue;
        auto const& name = part->mName->getString();
        if (names.size() < 200) names += name + " ";
        if (!head && name == "head") head = part;
    }
    line += " [" + names + "]";
    if (!head) { log(line + ": no head part"); return; }
    auto const& cubes = *head->mCubes;
    auto tex = *head->mTexSize;
    line += std::format(" texSize {}x{} cubes {}", tex.x, tex.y, cubes.size());
    if (cubes.empty()) { log(line); return; }
    auto const& cube = cubes.front();
    line += std::format(" size {} {} {}", cube.mSize->x, cube.mSize->y, cube.mSize->z);
    for (int f = 0; f < 6; ++f) {
        auto const& face = (*cube.mFaceData)[static_cast<size_t>(f)];
        line += std::format(" f{}=({},{} {}x{}{})", f, face.mUV->x, face.mUV->y, face.mUVSize->x, face.mUVSize->y,
                            face.mFaceValid ? "" : " invalid");
    }
    log(line);
    if (skin.empty()) return;
    auto group = client.getTextureGroup();
    if (!group) return;
    auto* image = group->getCachedImageOrLoadSync(ResourceLocation(Core::PathView(skin)), false);
    if (!image) { log(std::format("#{} texture did not load", faces.size())); return; }
    auto const& d = *image->mImageDescription;
    float kx = tex.x > 0 ? d.mWidth / tex.x : 1, ky = tex.y > 0 ? d.mHeight / tex.y : 1;
    auto format = d.mTextureFormat;
    log(std::format("#{} image {}x{} format {}", faces.size(), d.mWidth, d.mHeight, static_cast<unsigned>(format)));
    // Faces 2 and 3 (north and south) are the candidates for the front.
    for (int f : {2, 3}) {
        auto const& face = (*cube.mFaceData)[static_cast<size_t>(f)];
        faces.push_back({key + " f" + std::to_string(f),
                         crop(*image, face.mUV->x * kx, face.mUV->y * ky, face.mUVSize->x * kx, face.mUVSize->y * ky)});
    }
    writeBmp();
}
}
void probeRadarIcons(IClientInstance& client) {
    try {
        auto* player = client.getLocalPlayer();
        if (!player) return;
        int budget = 2;
        auto feet = player->getPosition();
        for (auto* actor : player->getLevel().getRuntimeActorList()) {
            if (!actor || actor == player || budget <= 0) continue;
            auto p = actor->getPosition();
            if (std::abs(p.x - feet.x) > 48 || std::abs(p.z - feet.z) > 48) continue;
            auto before = seen.size();
            probe(client, *actor);
            if (seen.size() != before) --budget;
        }
    } catch (std::exception const& error) {
        log(std::string("failed: ") + error.what());
    } catch (...) {
        log("failed");
    }
}
}
#else
namespace lamium::map {
void probeRadarIcons(IClientInstance&) {}
}
#endif
