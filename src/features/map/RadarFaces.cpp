#include "features/map/RadarFaces.h"
#include "app/Runtime.h"
#include "mc/client/game/IClientInstance.h"
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
        if (auto face = load(client, actor, renderer)) {
            found.push_back(*face);
            index = static_cast<int>(found.size()) - 1;
        }
    } catch (...) {}
    if (index < 0) log(std::format("no face for {}; it stays a dot", renderer));
    byRenderer.emplace(std::move(renderer), index);
    return index;
}
Face const* face(int index) {
    return index >= 0 && index < static_cast<int>(found.size()) ? &found[static_cast<size_t>(index)] : nullptr;
}
void frame() { loadsLeft = loadsPerFrame; }
void forget() {
    byRenderer.clear();
    found.clear();
}
}
