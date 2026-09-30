#include "features/visuals/EffectTrace.h"
#ifdef LAMIUM_EFFECTS_TRACE
#include "app/Runtime.h"
#include "input/Actions.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/gui/ScreenRenderer.h"
#include "mc/client/gui/IntRectangle.h"
#include "mc/client/gui/screens/InGamePlayScreen.h"
#include "mc/client/gui/controls/renderers/HudVignetteRenderer.h"
#include "mc/client/gui/controls/SpriteComponent.h"
#include "mc/client/gui/controls/TextComponent.h"
#include "mc/client/gui/controls/UIControl.h"
#include "mc/client/gui/controls/renderers/MinecraftUICustomRenderer.h"
#include "mc/client/renderer/Tessellator.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/client/renderer/actor/ItemRenderer.h"
#include "mc/client/renderer/actor/ItemRenderChunkType.h"
#include "mc/client/renderer/texture/TextureUVCoordinateSet.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/item/ItemStack.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/deps/core/renderer/RenderMaterialInfo.h"
#include "mc/deps/core/resource/ResourceLocation.h"
#include "mc/deps/minecraft_renderer/renderer/MaterialPtr.h"
#include "mc/deps/minecraft_renderer/renderer/TexturePtr.h"
#include "mc/deps/minecraft_renderer/resources/ClientTexture.h"
#include "mc/deps/minecraft_renderer/resources/ServerTexture.h"
#include "mc/deps/minecraft_renderer/framebuilder/dragon/RenderMetadata.h"
#include "mc/deps/minecraft_renderer/renderer/Mesh.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_set>

namespace lamium::visuals::effectTrace {
namespace {
std::mutex traceMutex;
std::unordered_set<std::string> routes;
unsigned targetLines = 0, otherLines = 0;
std::atomic<unsigned> inspections{0};
std::atomic<std::uint64_t> uiSamples{0}, meshSamples{0};
std::array<bool, 48> fogStates{};
std::array<bool, 40> densityStates{};
std::array<std::array<bool, 3>, 8> resolvedFogStates{};
unsigned previousMedium = 8;
std::chrono::steady_clock::time_point mediumEntered{};
std::unordered_set<std::string> meshRoutes;
unsigned meshTargetLines = 0, meshOtherLines = 0;
std::atomic<unsigned> meshInspections{0};
bool spriteInstalled = false, textInstalled = false, customInstalled = false, fogInstalled = false;
bool meshInstalled = false, metadataMeshInstalled = false, densityInstalled = false, resolvedFogInstalled = false;
bool blitInstalled = false, variantBlitInstalled = false, rectBlitInstalled = false;
bool screenInstalled = false, postInstalled = false, vignetteInstalled = false;
bool spanMeshInstalled = false, tessellatorInstalled = false, chunkItemInstalled = false;
bool uiTextureInstalled = false, uiImageInstalled = false, uiFlushInstalled = false;
std::unordered_set<std::string> chunkItemRoutes;
std::atomic<unsigned> entryBits{0};
std::atomic<unsigned> screenInspections{0};
std::atomic<std::uint64_t> screenSamples{0};
std::unordered_set<std::string> screenRoutes;
unsigned screenLines = 0;
thread_local unsigned drawStage = 0;
struct DrawStage {
    unsigned previous = drawStage;
    explicit DrawStage(unsigned stage) { drawStage = stage; }
    ~DrawStage() { drawStage = previous; }
};
// Effect gate (2026-09-30 frame research): 1 while a carved pumpkin is worn,
// 2 while scoping, set from the player's own state once per gameplay-screen
// render. Routes seen with the gate closed form a baseline; with it open,
// every route not in that baseline is logged, unsampled and without the
// call budget, so a frame drawn only while worn or scoping cannot be missed.
std::atomic<unsigned> effectGate{0};
std::unordered_set<std::string> baselineRoutes, gatedRoutes;
unsigned gatedLines = 0;
void gated(std::string const& key) noexcept {
    try {
        unsigned gate = effectGate.load(std::memory_order_relaxed);
        // Equipping through the inventory opens other screens; their routes
        // are not frame candidates and would fill the line budget.
        if (gate && !observing()) return;
        std::lock_guard lock{traceMutex};
        if (!gate) {
            if (baselineRoutes.size() < 4096) baselineRoutes.insert(key);
            return;
        }
        if (gatedLines >= 128 || baselineRoutes.contains(key) || !gatedRoutes.insert(key).second) return;
        ++gatedLines;
        Runtime::instance().self().getLogger().info("research L-42 gated gate={} {}", gate, key);
    } catch (...) {}
}
unsigned currentGate() noexcept {
    try {
        auto client = ll::service::getClientInstance();
        auto* player = client ? client->getLocalPlayer() : nullptr;
        if (!player) return 0;
        auto const& head = player->getArmor(SharedTypes::Legacy::ArmorSlot::Head);
        bool pumpkin = !head.isNull() && head.mCount > 0 && head.getTypeName() == "minecraft:carved_pumpkin";
        return (pumpkin ? 1u : 0u) | (player->isScoping() ? 2u : 0u);
    } catch (...) { return 0; }
}
bool inspect(std::atomic<unsigned>& counter) {
    if (counter.load() >= 300000) return false;
    return counter.fetch_add(1) < 300000;
}
bool sample(std::atomic<std::uint64_t>& counter) {
    auto index = counter.fetch_add(1);
    // Vary the sampled slot across frames so stable HUD draw order does not
    // starve an effect, or exhaust the inspection budget before the playtest.
    return ((index ^ (index >> 5) ^ (index >> 10)) & 31u) == 0;
}
bool observing() {
    auto client = ll::service::getClientInstance();
    return Runtime::instance().enabled() && client && client->getLocalPlayer() && gameplayScreen(client->getScreenName());
}
void entry(unsigned bit, char const* name) noexcept {
    try {
        if ((entryBits.load(std::memory_order_relaxed) & bit) || !observing()) return;
        if (entryBits.fetch_or(bit,std::memory_order_relaxed) & bit) return;
        Runtime::instance().self().getLogger().info("research L-42 entry {}",name);
    } catch (...) {}
}
void route(char const* kind, UIControl& owner, std::string_view resource = {}) noexcept {
    try {
        // Gate comparison sees every UI route; the older sampled log below is unchanged.
        std::string path = owner.getPathedName();
        if (path.size() <= 512) gated(std::string("ui ") + kind + " " + path + " " + std::string(resource.substr(0,192)));
        if (inspections.load() >= 300000 || !sample(uiSamples) || !observing() || !inspect(inspections)) return;
        auto tail = path.size() > 192 ? path.substr(path.size() - 192) : std::string{};
        path.resize(std::min(path.size(), size_t{192}));
        std::string key = std::string(kind) + " " + path + " tail=" + tail + " " + std::string(resource.substr(0,192));
        bool target = false;
        for (auto word : {"boss", "overlay", "camera", "vignette", "pumpkin", "spyglass", "frost", "powder", "nausea"})
            target = target || key.find(word) != std::string::npos;
        std::lock_guard lock{traceMutex};
        auto& lines = target ? targetLines : otherLines;
        if (lines >= (target ? 64u : 16u) || !routes.insert(key).second) return;
        ++lines;
        Runtime::instance().self().getLogger().info("research L-42 UI {}", key);
    } catch (...) {}
}
LL_TYPE_INSTANCE_HOOK(EffectSpriteTrace, ll::memory::HookPriority::Normal, SpriteComponent,
    &SpriteComponent::render, void, UIRenderContext& context) {
    try {
        ResourceLocation const& resource = mResourceLocation;
        Core::PathBuffer<std::string> const& path = resource.mPath;
        route("sprite", mOwner, path.value);
    } catch (...) {}
    origin(context);
}
LL_TYPE_INSTANCE_HOOK(EffectTextTrace, ll::memory::HookPriority::Normal, TextComponent,
    &TextComponent::$render, void, UIRenderContext& context) {
    route("text", mOwner);
    origin(context);
}
LL_TYPE_INSTANCE_HOOK(EffectCustomTrace, ll::memory::HookPriority::Normal, MinecraftUICustomRenderer,
    &MinecraftUICustomRenderer::$render, void, UIRenderContext& context,
    IClientInstance& client, UIControl& owner, int pass) {
    if (ll::service::getClientInstance() == &client) route("custom", owner);
    origin(context, client, owner, pass);
}
LL_TYPE_INSTANCE_HOOK(EffectFogTrace, ll::memory::HookPriority::Normal, LevelRendererPlayer,
    &LevelRendererPlayer::$_getFogDistanceSettingType, FogDefinition::DistanceSettingType) {
    auto type = origin();
    try {
        int index = static_cast<int>(type);
        if (ll::service::getClientInstance() != &mClientInstance || !observing() || index < 0 || index >= 6) return type;
        unsigned medium = (mCameraUnderWater ? 1u : 0u) | (mCameraUnderLava ? 2u : 0u) | (mCameraUnderPowderSnow ? 4u : 0u);
        std::lock_guard lock{traceMutex};
        auto& seen = fogStates[medium * 6 + static_cast<unsigned>(index)];
        if (!seen) {
            seen = true;
            Runtime::instance().self().getLogger().info("research L-42 fog mediumBits={} distanceType={}", medium, index);
        }
    } catch (...) {}
    return type;
}
LL_TYPE_INSTANCE_HOOK(EffectDensityTrace, ll::memory::HookPriority::Normal, LevelRendererPlayer,
    &LevelRendererPlayer::$_getFogDensitySettingType, FogDefinition::DensitySettingType) {
    auto type = origin();
    try {
        int index = static_cast<int>(type);
        if (ll::service::getClientInstance() != &mClientInstance || !observing() || index < 0 || index >= 5) return type;
        unsigned medium = (mCameraUnderWater ? 1u : 0u) | (mCameraUnderLava ? 2u : 0u) | (mCameraUnderPowderSnow ? 4u : 0u);
        std::lock_guard lock{traceMutex};
        auto& seen = densityStates[medium * 5 + static_cast<unsigned>(index)];
        if (!seen) {
            seen = true;
            Runtime::instance().self().getLogger().info("research L-42 fog mediumBits={} densityType={}", medium, index);
        }
    } catch (...) {}
    return type;
}
LL_TYPE_INSTANCE_HOOK(EffectResolvedFogTrace, ll::memory::HookPriority::Normal, LevelRendererPlayer,
    &LevelRendererPlayer::$setupFog, void, ScreenContext& context, float intensity) {
    origin(context, intensity);
    try {
        if (ll::service::getClientInstance() != &mClientInstance || !observing()) return;
        unsigned medium = (mCameraUnderWater ? 1u : 0u) | (mCameraUnderLava ? 2u : 0u) | (mCameraUnderPowderSnow ? 4u : 0u);
        std::lock_guard lock{traceMutex};
        auto now = std::chrono::steady_clock::now();
        if (previousMedium != medium) { previousMedium = medium; mediumEntered = now; }
        double elapsed = std::chrono::duration<double>(now - mediumEntered).count();
        unsigned phase = elapsed >= 5 ? 2u : elapsed >= 1 ? 1u : 0u;
        if (resolvedFogStates[medium][phase]) return;
        resolvedFogStates[medium][phase] = true;
        Runtime::instance().self().getLogger().info(
            "research L-42 resolved fog mediumBits={} phase={} distance={}/{} density={} controls={}/{}",
            medium, phase, mCurrentDistanceFog->mStart, mCurrentDistanceFog->mEnd,
            mCurrentFogDensity->mMaxDensity, mFogControl->x, mFogControl->y);
    } catch (...) {}
}
using MeshTexture = std::variant<std::monostate, mce::TexturePtr, mce::ClientTexture, mce::ServerTexture>;
using MeshRender = void (mce::Mesh::*)(mce::MeshContext&, mce::MaterialPtr const&, MeshTexture const&,
    uint, uint, OffscreenCaptureDescription const&, mce::IndexBufferContainer const*) const;
using MetadataMeshRender = void (mce::Mesh::*)(mce::MeshContext&, dragon::RenderMetadata const&,
    mce::MaterialPtr const&, MeshTexture const&, uint, uint, mce::IndexBufferContainer const*) const;
void meshRoute(mce::Mesh const& mesh, mce::MaterialPtr const& material, MeshTexture const& texture, uint count,
    std::optional<size_t> textureCount = {}) noexcept {
    try {
        {
            std::string key = "mesh stage=" + std::to_string(drawStage) + " ";
            auto const& info = material.mRenderMaterialInfoPtr;
            if (info) key += info->mHashedName->getString().substr(0,192);
            key += " texture=";
            if (textureCount) key += "span[" + std::to_string(*textureCount) + "]";
            else if (auto* pointer = std::get_if<mce::TexturePtr>(&texture); pointer && pointer->mResourceLocationPtr) {
                Core::PathBuffer<std::string> const& path = pointer->mResourceLocationPtr->mPath;
                key += path.value.substr(0,192);
            } else key += "kind" + std::to_string(texture.index());
            gated(key);
        }
        if (meshInspections.load() < 300000 && sample(meshSamples) && observing() && inspect(meshInspections)) {
            std::string key = "stage=" + std::to_string(drawStage) + " ";
            auto const& info = material.mRenderMaterialInfoPtr;
            if (info) key += info->mHashedName->getString().substr(0,192);
            key += " texture=";
            if (textureCount) key += "span[" + std::to_string(*textureCount) + "]";
            else if (auto* pointer = std::get_if<mce::TexturePtr>(&texture); pointer && pointer->mResourceLocationPtr) {
                Core::PathBuffer<std::string> const& path = pointer->mResourceLocationPtr->mPath;
                key += path.value.substr(0,192);
            } else key += "kind" + std::to_string(texture.index());
            bool target = false;
            for (auto word : {"overlay", "camera", "fullscreen", "vignette", "pumpkin", "spyglass", "scope",
                              "frost", "powder", "freeze", "frozen", "on_screen", "nausea", "water", "lava", "distortion"})
                target = target || key.find(word) != std::string::npos;
            std::lock_guard lock{traceMutex};
            auto& lines = target ? meshTargetLines : meshOtherLines;
            if (lines < (target ? 48u : 8u) && meshRoutes.insert(key).second) {
                ++lines;
                Runtime::instance().self().getLogger().info("research L-42 mesh {} vertices={} drawCount={}",
                    key, mesh.mVertexCount->value_or(0), count);
            }
        }
    } catch (...) {}
}
// The internal TextureList is opaque in this SDK. Use only the declared
// reference-based variants and the complete GSL span; never guess its layout.
LL_TYPE_INSTANCE_HOOK(EffectMeshTrace, ll::memory::HookPriority::Normal, mce::Mesh,
    static_cast<MeshRender>(&mce::Mesh::renderMesh), void, mce::MeshContext& context,
    mce::MaterialPtr const& material, MeshTexture const& texture, uint startOffset, uint count,
    OffscreenCaptureDescription const& capture, mce::IndexBufferContainer const* indices) {
    meshRoute(*this, material, texture, count);
    origin(context, material, texture, startOffset, count, capture, indices);
}
LL_TYPE_INSTANCE_HOOK(EffectMetadataMeshTrace, ll::memory::HookPriority::Normal, mce::Mesh,
    static_cast<MetadataMeshRender>(&mce::Mesh::renderMesh), void, mce::MeshContext& context,
    dragon::RenderMetadata const& metadata, mce::MaterialPtr const& material, MeshTexture const& texture,
    uint startOffset, uint count, mce::IndexBufferContainer const* indices) {
    meshRoute(*this, material, texture, count);
    origin(context, metadata, material, texture, startOffset, count, indices);
}
using SpanMeshRender = void (mce::Mesh::*)(mce::MeshContext&, mce::MaterialPtr const&,
    gsl::span<mce::ClientTexture const*>, uint, uint, OffscreenCaptureDescription const&,
    mce::IndexBufferContainer const*) const;
LL_TYPE_INSTANCE_HOOK(EffectSpanMeshTrace, ll::memory::HookPriority::Normal, mce::Mesh,
    static_cast<SpanMeshRender>(&mce::Mesh::renderMesh), void, mce::MeshContext& context,
    mce::MaterialPtr const& material, gsl::span<mce::ClientTexture const*> textures, uint startOffset, uint count,
    OffscreenCaptureDescription const& capture, mce::IndexBufferContainer const* indices) {
    entry(64,"spanMesh");
    meshRoute(*this,material,MeshTexture{},count,textures.size());
    origin(context,material,textures,startOffset,count,capture,indices);
}
void screenRoute(mce::TexturePtr const* texture, mce::MaterialPtr const* material, int width, int height,
    char const* source = "blit") noexcept {
    try {
        {
            std::string key = std::string(source) + " stage=" + std::to_string(drawStage) + " ";
            if (material && material->mRenderMaterialInfoPtr)
                key += material->mRenderMaterialInfoPtr->mHashedName->getString().substr(0,192);
            key += " texture=";
            if (texture && texture->mResourceLocationPtr) {
                Core::PathBuffer<std::string> const& path = texture->mResourceLocationPtr->mPath;
                key += path.value.substr(0,192);
            } else key += texture ? "unnamed" : "none";
            gated(key + (width >= 128 && height >= 128 ? " large" : " small"));
        }
        if (screenInspections.load() >= 300000 || !sample(screenSamples) || !observing() || !inspect(screenInspections)) return;
        std::string resource, materialName;
        if (texture && texture->mResourceLocationPtr) {
            Core::PathBuffer<std::string> const& path = texture->mResourceLocationPtr->mPath;
            resource = path.value.substr(0,192);
        }
        if (material && material->mRenderMaterialInfoPtr)
            materialName = material->mRenderMaterialInfoPtr->mHashedName->getString().substr(0,192);
        bool large = width >= 128 && height >= 128;
        bool candidate = large || drawStage == 3;
        for (auto word : {"pumpkin", "spyglass", "scope", "vignette", "nausea", "frozen", "overlay", "on_screen"})
            candidate = candidate || resource.find(word) != std::string::npos || materialName.find(word) != std::string::npos;
        if (!candidate) return;
        auto key = std::string(source) + " " + std::to_string(drawStage) + " " + materialName + " " + resource
            + (large ? " large" : " small");
        std::lock_guard lock{traceMutex};
        if (screenLines >= 64 || !screenRoutes.insert(key).second) return;
        ++screenLines;
        Runtime::instance().self().getLogger().info("research L-42 screen {} size={}/{}", key, width, height);
    } catch (...) {}
}
using TextureBlit = void (ScreenRenderer::*)(ScreenContext&, mce::TexturePtr const&,
    int, int, int, int, int, int, int, int, mce::MaterialPtr const*, float, float);
using VariantBlit = void (ScreenRenderer::*)(ScreenContext&, MeshTexture const&,
    int, int, int, int, int, int, int, int, mce::MaterialPtr const*, float, float);
using RectBlit = void (ScreenRenderer::*)(ScreenContext&, mce::TexturePtr const&, IntRectangle const&, mce::MaterialPtr const*);
LL_TYPE_INSTANCE_HOOK(EffectTextureBlitTrace, ll::memory::HookPriority::Normal, ScreenRenderer,
    static_cast<TextureBlit>(&ScreenRenderer::blit), void, ScreenContext& context, mce::TexturePtr const& texture,
    int x, int y, int sx, int sy, int w, int h, int sw, int sh, mce::MaterialPtr const* material, float us, float vs) {
    entry(1,"textureBlit");
    screenRoute(&texture,material,w,h);
    origin(context,texture,x,y,sx,sy,w,h,sw,sh,material,us,vs);
}
LL_TYPE_INSTANCE_HOOK(EffectVariantBlitTrace, ll::memory::HookPriority::Normal, ScreenRenderer,
    static_cast<VariantBlit>(&ScreenRenderer::blit), void, ScreenContext& context, MeshTexture const& texture,
    int x, int y, int sx, int sy, int w, int h, int sw, int sh, mce::MaterialPtr const* material, float us, float vs) {
    entry(2,"variantBlit");
    screenRoute(std::get_if<mce::TexturePtr>(&texture),material,w,h);
    origin(context,texture,x,y,sx,sy,w,h,sw,sh,material,us,vs);
}
LL_TYPE_INSTANCE_HOOK(EffectRectBlitTrace, ll::memory::HookPriority::Normal, ScreenRenderer,
    static_cast<RectBlit>(&ScreenRenderer::blit), void, ScreenContext& context, mce::TexturePtr const& texture,
    IntRectangle const& rect, mce::MaterialPtr const* material) {
    entry(4,"rectBlit");
    screenRoute(&texture,material,rect.w,rect.h);
    origin(context,texture,rect,material);
}
LL_TYPE_INSTANCE_HOOK(EffectScreenStageTrace, ll::memory::HookPriority::Normal, InGamePlayScreen,
    &InGamePlayScreen::$render, void, ScreenContext& context, FrameRenderObject const& frame) {
    entry(8,"inGameRender");
    auto gate = currentGate();
    if (effectGate.exchange(gate, std::memory_order_relaxed) != gate) {
        try { Runtime::instance().self().getLogger().info("research L-42 gate now {}", gate); } catch (...) {}
    }
    DrawStage stage{1};
    origin(context,frame);
}
LL_TYPE_INSTANCE_HOOK(EffectPostStageTrace, ll::memory::HookPriority::Normal, InGamePlayScreen,
    &InGamePlayScreen::$_postLevelRender, void, ScreenContext& context, LevelRenderer& level) {
    entry(16,"postLevelRender");
    DrawStage stage{2};
    origin(context,level);
}
LL_TYPE_INSTANCE_HOOK(EffectVignetteTrace, ll::memory::HookPriority::Normal, HudVignetteRenderer,
    &HudVignetteRenderer::$render, void, MinecraftUIRenderContext& context, IClientInstance& client,
    UIControl& owner, int pass) {
    entry(32,"vignetteRender");
    DrawStage stage{3};
    if (ll::service::getClientInstance() == &client) route("vignette",owner);
    origin(context,client,owner,pass);
}
// UI render context: custom HUD renderers fetch textures by resource name,
// queue images and flush them with a material name. Keys join the gate
// comparison (baseline vs worn/scoping); no texture content is read.
LL_TYPE_INSTANCE_HOOK(UiTextureTrace, ll::memory::HookPriority::Normal, MinecraftUIRenderContext,
    &MinecraftUIRenderContext::$getTexture, mce::TexturePtr, ResourceLocation const& resource, bool reload) {
    try {
        Core::PathBuffer<std::string> const& path = resource.mPath;
        gated("uiTexture stage=" + std::to_string(drawStage) + " " + path.value.substr(0,192));
    } catch (...) {}
    return origin(resource, reload);
}
LL_TYPE_INSTANCE_HOOK(UiImageTrace, ll::memory::HookPriority::Normal, MinecraftUIRenderContext,
    &MinecraftUIRenderContext::$drawImage, void, mce::ClientTexture const& texture, glm::vec2 const& position,
    glm::vec2 const& size, glm::vec2 const& uv, glm::vec2 const& uvSize, bool const colorCorrected) {
    try {
        bool large = size.x >= 128 && size.y >= 128;
        gated("uiImage stage=" + std::to_string(drawStage) + (large ? " large" : " small"));
    } catch (...) {}
    origin(texture, position, size, uv, uvSize, colorCorrected);
}
LL_TYPE_INSTANCE_HOOK(UiFlushTrace, ll::memory::HookPriority::Normal, MinecraftUIRenderContext,
    &MinecraftUIRenderContext::$flushImages, void, mce::Color const& color, float alpha, HashedString const& material) {
    try { gated("uiFlush stage=" + std::to_string(drawStage) + " " + material.getString().substr(0,192)); } catch (...) {}
    origin(color, alpha, material);
}
// L-61 research: which opaque ItemRenderChunkType values vanilla slots use for
// layered icons (leather armor), so Lamium can draw the same passes.
LL_TYPE_INSTANCE_HOOK(ChunkItemTrace, ll::memory::HookPriority::Normal, ItemRenderer,
    &ItemRenderer::renderGuiItemInChunk, void, BaseActorRenderContext& context, ItemRenderChunkType type,
    ItemStack const& item, float x, float y, float light, float alpha, float scale, int frame, bool animate,
    int zOrder, std::optional<TextureUVCoordinateSet> const& uv) {
    try {
        if (!item.isNull() && item.mItem) {
            auto name = item.getTypeName();
            if (name.find("leather") != std::string::npos || name.find("diamond") != std::string::npos) {
                auto key = std::to_string(static_cast<int>(type)) + " " + name.substr(0,96);
                std::lock_guard lock{traceMutex};
                if (chunkItemRoutes.size() < 64 && chunkItemRoutes.insert(key).second)
                    Runtime::instance().self().getLogger().info("research L-61 chunk type={} scale={} z={} uv={}",
                        key, scale, zOrder, uv.has_value());
            }
        }
    } catch (...) {}
    origin(context,type,item,x,y,light,alpha,scale,frame,animate,zOrder,uv);
}
LL_TYPE_INSTANCE_HOOK(EffectTessellatorTrace, ll::memory::HookPriority::Normal, Tessellator,
    &Tessellator::triggerIntercept, void, mce::MaterialPtr const& material, mce::TexturePtr const& texture) {
    entry(128,"tessellatorIntercept");
    screenRoute(&texture,&material,0,0,"tessellator");
    origin(material,texture);
}
}
void start() noexcept {
    try {
        if (!spriteInstalled) spriteInstalled = EffectSpriteTrace::hook(true) == 0;
        if (!textInstalled) textInstalled = EffectTextTrace::hook(true) == 0;
        if (!customInstalled) customInstalled = EffectCustomTrace::hook(true) == 0;
        if (!fogInstalled) fogInstalled = EffectFogTrace::hook(true) == 0;
        if (!densityInstalled) densityInstalled = EffectDensityTrace::hook(true) == 0;
        if (!resolvedFogInstalled) resolvedFogInstalled = EffectResolvedFogTrace::hook(true) == 0;
        if (!meshInstalled) meshInstalled = EffectMeshTrace::hook(true) == 0;
        if (!metadataMeshInstalled) metadataMeshInstalled = EffectMetadataMeshTrace::hook(true) == 0;
        if (!blitInstalled) blitInstalled = EffectTextureBlitTrace::hook(true) == 0;
        if (!variantBlitInstalled) variantBlitInstalled = EffectVariantBlitTrace::hook(true) == 0;
        if (!rectBlitInstalled) rectBlitInstalled = EffectRectBlitTrace::hook(true) == 0;
        if (!screenInstalled) screenInstalled = EffectScreenStageTrace::hook(true) == 0;
        if (!postInstalled) postInstalled = EffectPostStageTrace::hook(true) == 0;
        if (!vignetteInstalled) vignetteInstalled = EffectVignetteTrace::hook(true) == 0;
        if (!spanMeshInstalled) spanMeshInstalled = EffectSpanMeshTrace::hook(true) == 0;
        if (!tessellatorInstalled) tessellatorInstalled = EffectTessellatorTrace::hook(true) == 0;
        if (!chunkItemInstalled) chunkItemInstalled = ChunkItemTrace::hook(true) == 0;
        if (!uiTextureInstalled) uiTextureInstalled = UiTextureTrace::hook(true) == 0;
        if (!uiImageInstalled) uiImageInstalled = UiImageTrace::hook(true) == 0;
        if (!uiFlushInstalled) uiFlushInstalled = UiFlushTrace::hook(true) == 0;
        Runtime::instance().self().getLogger().info("research L-42 hooks sprite={} text={} custom={} fog={} density={} resolvedFog={} mesh={} metadataMesh={}",
            spriteInstalled, textInstalled, customInstalled, fogInstalled, densityInstalled, resolvedFogInstalled,
            meshInstalled, metadataMeshInstalled);
        Runtime::instance().self().getLogger().info("research L-42 screen hooks textureBlit={} variantBlit={} rectBlit={} screen={} post={} vignette={}",
            blitInstalled,variantBlitInstalled,rectBlitInstalled,screenInstalled,postInstalled,vignetteInstalled);
        Runtime::instance().self().getLogger().info("research L-42 mesh extras span={} tessellator={} chunkItem={} uiTexture={} uiImage={} uiFlush={}",
            spanMeshInstalled,tessellatorInstalled,chunkItemInstalled,uiTextureInstalled,uiImageInstalled,uiFlushInstalled);
    } catch (...) {}
}
void stop() {
    if (uiFlushInstalled && UiFlushTrace::unhook(true)) uiFlushInstalled = false;
    if (uiImageInstalled && UiImageTrace::unhook(true)) uiImageInstalled = false;
    if (uiTextureInstalled && UiTextureTrace::unhook(true)) uiTextureInstalled = false;
    if (chunkItemInstalled && ChunkItemTrace::unhook(true)) chunkItemInstalled = false;
    if (tessellatorInstalled && EffectTessellatorTrace::unhook(true)) tessellatorInstalled = false;
    if (spanMeshInstalled && EffectSpanMeshTrace::unhook(true)) spanMeshInstalled = false;
    if (vignetteInstalled && EffectVignetteTrace::unhook(true)) vignetteInstalled = false;
    if (postInstalled && EffectPostStageTrace::unhook(true)) postInstalled = false;
    if (screenInstalled && EffectScreenStageTrace::unhook(true)) screenInstalled = false;
    if (rectBlitInstalled && EffectRectBlitTrace::unhook(true)) rectBlitInstalled = false;
    if (variantBlitInstalled && EffectVariantBlitTrace::unhook(true)) variantBlitInstalled = false;
    if (blitInstalled && EffectTextureBlitTrace::unhook(true)) blitInstalled = false;
    if (metadataMeshInstalled && EffectMetadataMeshTrace::unhook(true)) metadataMeshInstalled = false;
    if (meshInstalled && EffectMeshTrace::unhook(true)) meshInstalled = false;
    if (resolvedFogInstalled && EffectResolvedFogTrace::unhook(true)) resolvedFogInstalled = false;
    if (densityInstalled && EffectDensityTrace::unhook(true)) densityInstalled = false;
    if (fogInstalled && EffectFogTrace::unhook(true)) fogInstalled = false;
    if (customInstalled && EffectCustomTrace::unhook(true)) customInstalled = false;
    if (textInstalled && EffectTextTrace::unhook(true)) textInstalled = false;
    if (spriteInstalled && EffectSpriteTrace::unhook(true)) spriteInstalled = false;
}
}
#else
namespace lamium::visuals::effectTrace {
void start() noexcept {}
void stop() {}
}
#endif
