#include "features/map/RadarFaces.h"
#include "app/Runtime.h"
#include "ui/Widgets.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/gui/GuiData.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/deps/core/container/Blob.h"
#include "mc/deps/core/image/Image.h"
#include "mc/client/model/geom/Cube.h"
#include "mc/client/model/geom/ModelPart.h"
#include "mc/client/model/models/Model.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/client/renderer/TextureGroup.h"
#include "mc/client/renderer/actor/ActorRenderDispatcher.h"
#include "mc/client/renderer/actor/DataDrivenRenderer.h"
#include "mc/deps/core/file/PathView.h"
#include "mc/deps/core/resource/ResourceLocation.h"
#include "mc/deps/core_graphics/ImageBuffer.h"
#include "mc/deps/core_graphics/ImageDescription.h"
#include "mc/deps/core_graphics/enums/TextureFormat.h"
#include "mc/deps/minecraft_renderer/renderer/TexturePtr.h"
#include "mc/world/actor/Actor.h"
#include <format>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace lamium::map::faces {
namespace {
std::unordered_map<std::string, int> byRenderer; // -1: this kind has no face.
std::vector<Face> found;
// Every face in one runtime texture, uploaded again when one is added.
std::vector<std::uint32_t> atlas;
bool atlasDirty = false, atlasUploaded = false;
ResourceLocation const& atlasLocation() {
    // Never destroyed: its destructor is game code, which must not run while
    // the process tears down after the game.
    static auto const* location = new ResourceLocation(Core::PathView("lamium/radar-faces"), ResourceFileSystem::Raw);
    return *location;
}
bool upload(IClientInstance& client) {
    auto group = client.getTextureGroup();
    if (!group) return false;
    mce::Image image(faceAtlasSide, faceAtlasSide, mce::ImageFormat::RGBA8Unorm, mce::ImageUsage::SRGB);
    image.mAlphaUsage = mce::AlphaUsage::Transparent;
    image.setRawImage(mce::Blob(reinterpret_cast<std::uint8_t const*>(atlas.data()), atlas.size() * sizeof(std::uint32_t)));
    cg::ImageBuffer buffer(std::move(image));
    if (atlasUploaded) return group->updateTextureInPlace(atlasLocation(), std::move(buffer));
    group->uploadTexture(atlasLocation(), std::move(buffer));
    return true;
}
// New kinds looked up per frame; each may load a texture.
constexpr int loadsPerFrame = 2;
int loadsLeft = 0;
int logged = 0;
void log(std::string const& text) {
    if (++logged > 12) return;
    try { Runtime::instance().self().getLogger().info("Radar faces: {}", text); } catch (...) {}
}
// The head seen from the front: the front face (index 2) of every cube of
// the model's "head" part and of the parts hanging from it (nose, ears,
// muzzle), worn layers left out. Cube origins are model coordinates, y up
// (checked in game 2026-10-02: reading them as part-relative and y down
// turned the pig's face over and scattered villagers' noses).
struct HeadBoxes {
    std::vector<FaceBox> boxes;
    float textureWidth = 0, textureHeight = 0;
};
void addPart(ModelPart const& part, HeadBoxes& out, int depth) {
    if (depth > 4 || !faceLayer(part.mName->getString())) return;
    for (auto const& cube : *part.mCubes) {
        auto const& face = (*cube.mFaceData)[2];
        auto origin = *cube.mOrigin, size = *cube.mSize;
        if (!face.mFaceValid || !(size.x > 0) || !(size.y > 0)) continue;
        out.boxes.push_back({origin.x, origin.y, origin.x + size.x, origin.y + size.y, origin.z, face.mUV->x, face.mUV->y,
                             face.mUVSize->x, face.mUVSize->y});
    }
    for (auto const* child : *part.mChildren)
        if (child) addPart(*child, out, depth + 1);
}
std::optional<HeadBoxes> headBoxes(Model const& model) {
    for (auto const* part : *model.mAllParts) {
        if (!part || part->mName->getString() != "head") continue;
        HeadBoxes head;
        auto size = *part->mTexSize;
        head.textureWidth = size.x;
        head.textureHeight = size.y;
        addPart(*part, head, 0);
        if (!head.boxes.empty()) return head;
    }
    return std::nullopt;
}
std::optional<Face> load(IClientInstance& client, Actor& actor, std::string const& renderer) {
    auto dispatcher = client.getEntityRenderDispatcher();
    if (!dispatcher) return std::nullopt;
    auto dataDriven = dispatcher->getDataDrivenRenderer(actor.getActorRendererId());
    if (!dataDriven) return std::nullopt;
    auto model = dataDriven->mModel.get();
    if (!model) return std::nullopt;
    auto head = headBoxes(*model);
    if (!head) return std::nullopt;
    std::string texture(baseTexture(renderer));
    if (texture.empty())
        if (auto const& location = dataDriven->mDefaultSkin->mResourceLocationPtr.get(); location)
            texture = location->mPath->value;
    if (texture.empty()) return std::nullopt;
    auto group = client.getTextureGroup();
    if (!group) return std::nullopt;
    auto* image = group->getCachedImageOrLoadSync(ResourceLocation(Core::PathView(texture)), false);
    if (!image) return std::nullopt;
    auto const& description = *image->mImageDescription;
    auto format = description.mTextureFormat;
    int width = static_cast<int>(description.mWidth), height = static_cast<int>(description.mHeight);
    auto const& storage = *image->mStorage;
    bool usable = (format == mce::TextureFormat::R8g8b8a8Unorm || format == mce::TextureFormat::R8g8b8a8UnormSrgb)
        && width > 0 && height > 0 && storage.size() >= static_cast<size_t>(width) * height * 4;
    if (!usable) return std::nullopt;
    // Textures may be finer than the model's texture units.
    double texels = head->textureWidth > 0 ? width / head->textureWidth : 1;
    return composeFace(storage.data(), width, height, texels, std::move(head->boxes));
}
#ifdef LAMIUM_RADAR_ICON_PROBE
// Research builds only: every face built so far, outlined and x4, to
// logs/radar-faces.bmp, in the order logged with "face #N".
void dumpFaces() {
    constexpr int zoom = 4, cell = faceCellSide * zoom + 4, perRow = 8;
    int count = static_cast<int>(found.size()), rows = (count + perRow - 1) / perRow;
    int width = perRow * cell, height = std::max(1, rows) * cell;
    std::vector<std::uint32_t> px(static_cast<size_t>(width) * height, packColor(96, 96, 96));
    for (int i = 0; i < count; ++i) {
        auto at = faceAtlasCell(i);
        int ox = (i % perRow) * cell + 2, oy = (i / perRow) * cell + 2;
        for (int y = 0; y < faceCellSide * zoom; ++y)
            for (int x = 0; x < faceCellSide * zoom; ++x) {
                auto c = atlas[static_cast<size_t>(at.y + y / zoom) * faceAtlasSide + at.x + x / zoom];
                if (c >> 24) px[static_cast<size_t>(oy + y) * width + ox + x] = c;
            }
    }
    try {
        std::ofstream out(Runtime::instance().self().getModDir() / "logs" / "radar-faces.bmp", std::ios::binary);
        auto put = [&](std::uint32_t v, int bytes) { for (int b = 0; b < bytes; ++b) out.put(static_cast<char>((v >> (8 * b)) & 0xff)); };
        std::uint32_t data = static_cast<std::uint32_t>(width * height * 4);
        out.put('B'); out.put('M'); put(54 + data, 4); put(0, 4); put(54, 4);
        put(40, 4); put(static_cast<std::uint32_t>(width), 4); put(static_cast<std::uint32_t>(-height), 4); put(1, 2); put(32, 2);
        put(0, 4); put(data, 4); put(2835, 4); put(2835, 4); put(0, 4); put(0, 4);
        for (auto c : px) { out.put(static_cast<char>(channel(c, 2))); out.put(static_cast<char>(channel(c, 1))); out.put(static_cast<char>(channel(c, 0))); out.put(static_cast<char>(0xff)); }
    } catch (...) {}
}
#endif
}
int faceOf(IClientInstance& client, Actor& actor) {
    std::string renderer = actor.getActorRendererId().getString();
    if (renderer.empty()) return -1;
    if (auto at = byRenderer.find(renderer); at != byRenderer.end()) return at->second;
    if (loadsLeft <= 0) return -1;
    --loadsLeft;
    int index = -1;
    try {
        if (static_cast<int>(found.size()) < faceAtlasCapacity)
            if (auto face = load(client, actor, renderer)) {
                found.push_back(std::move(*face));
                index = static_cast<int>(found.size()) - 1;
                writeFace(atlas, index, found.back());
                atlasDirty = true;
#ifdef LAMIUM_RADAR_ICON_PROBE
                Runtime::instance().self().getLogger().info("Radar faces: face #{} is {} ({}x{})", index, renderer,
                                                            found.back().width, found.back().height);
                dumpFaces();
#endif
            }
    } catch (...) {}
    if (index < 0) log(std::format("no face for {}; it stays a dot", renderer));
    byRenderer.emplace(std::move(renderer), index);
    return index;
}
void frame() { loadsLeft = loadsPerFrame; }
bool draw(MinecraftUIRenderContext& context, int index, float x, float y, float size, float alpha) {
    if (index < 0 || index >= static_cast<int>(found.size())) return false;
    auto& client = context.mClient;
    if (atlasDirty || !atlasUploaded) {
        try { atlasUploaded = upload(client); } catch (...) { atlasUploaded = false; }
        atlasDirty = false;
        if (!atlasUploaded) return false;
    }
    auto const& face = found[static_cast<size_t>(index)];
    double inverse = client.getGuiData()->mInvGuiScale;
    double scale = std::isfinite(inverse) && inverse > 0 ? 1 / inverse : 1; // Screen pixels per GUI unit.
    // Whole screen pixels per texel, placed on the screen's pixel grid: the
    // face keeps its texture's shape. The outline is part of the image.
    int texel = faceTexelPixels(std::max(face.width, face.height), size * scale);
    int cols = outlinedWidth(face), rows = outlinedHeight(face);
    double w = cols * texel, h = rows * texel;
    double left = std::round(x * scale - w / 2), top = std::round(y * scale - h / 2);
    auto gui = [&](double pixels) { return static_cast<float>(pixels / scale); };
    auto cell = faceAtlasCell(index);
    float span = 1.f / faceAtlasSide;
    if (!ui::runtimeImage(context, atlasLocation(), {gui(left), gui(top), gui(w), gui(h)}, cell.x * span, cell.y * span,
                          cols * span, rows * span, alpha)) {
        // Resource reloads drop runtime textures; upload again next time.
        atlasUploaded = false;
        return false;
    }
    return true;
}
void forget(IClientInstance* client) {
    byRenderer.clear();
    found.clear();
    atlas.clear();
    if (atlasUploaded && client)
        try {
            if (auto group = client->getTextureGroup()) group->unloadTexture(atlasLocation(), false);
        } catch (...) {}
    atlasUploaded = false;
    atlasDirty = false;
}
}
