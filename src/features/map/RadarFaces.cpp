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
// Front face (index 2) of the first cube of the model's "head" part, in
// texture units, with the texture size those units refer to.
struct FaceRect { float u, v, w, h, textureWidth, textureHeight; };
std::optional<FaceRect> headFace(Model const& model) {
    for (auto const* part : *model.mAllParts) {
        if (!part || part->mName->getString() != "head") continue;
        auto const& cubes = *part->mCubes;
        if (cubes.empty()) continue;
        auto const& face = (*cubes.front().mFaceData)[2];
        if (!face.mFaceValid) continue;
        auto size = *part->mTexSize;
        return FaceRect{face.mUV->x, face.mUV->y, face.mUVSize->x, face.mUVSize->y, size.x, size.y};
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
    auto rect = headFace(*model);
    if (!rect) return std::nullopt;
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
    double kx = rect->textureWidth > 0 ? width / rect->textureWidth : 1;
    double ky = rect->textureHeight > 0 ? height / rect->textureHeight : 1;
    return cropFace(storage.data(), width, height, rect->u * kx, rect->v * ky, rect->w * kx, rect->h * ky);
}
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
    // Whole screen pixels per texel and for the ring, placed on the
    // screen's pixel grid: the face keeps its texture's shape exactly.
    int texel = faceTexelPixels(std::max(face.width, face.height), size * scale);
    int ring = std::max(1, texel / 2);
    double w = face.width * texel, h = face.height * texel;
    double left = std::round(x * scale - w / 2), top = std::round(y * scale - h / 2);
    auto gui = [&](double pixels) { return static_cast<float>(pixels / scale); };
    ui::fill(context, gui(left - ring), gui(top - ring), gui(w + 2 * ring), gui(h + 2 * ring), ui::Rgb{0, 0, 0}, .9f * alpha);
    auto cell = faceAtlasCell(index);
    float span = 1.f / faceAtlasSide;
    if (!ui::runtimeImage(context, atlasLocation(), {gui(left), gui(top), gui(w), gui(h)}, cell.x * span, cell.y * span,
                          face.width * span, face.height * span, alpha)) {
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
