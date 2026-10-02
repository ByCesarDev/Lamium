#include "features/schematic/GhostProbe.h"
#ifdef LAMIUM_GHOST_PROBE
#include "app/Runtime.h"
#include "ll/api/memory/Hook.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/ActorShaderManager.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/RenderMaterialGroup.h"
#include "mc/client/renderer/Tessellator.h"
#include "mc/client/renderer/block/BlockGraphics.h"
#include "mc/client/renderer/block/BlockTessellator.h"
#include "mc/client/renderer/blockactor/BlockActorRenderDispatcher.h"
#include "mc/client/renderer/blockactor/MovingBlockActorRenderer.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/client/renderer/ptexture/LightTexture.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/common/client/renderer/helpers/MeshHelpers.h"
#include "mc/deps/core/math/Color.h"
#include "mc/deps/core_graphics/enums/PrimitiveMode.h"
#include "mc/deps/minecraft_renderer/renderer/MaterialPtr.h"
#include "mc/deps/minecraft_renderer/renderer/Mesh.h"
#include "mc/deps/minecraft_renderer/renderer/TexturePtr.h"
#include "mc/deps/minecraft_renderer/resources/ClientTexture.h"
#include "mc/deps/minecraft_renderer/resources/ServerTexture.h"
#include "mc/deps/minecraft_renderer/resources/OffscreenCaptureDescription.h"
#include "mc/deps/renderer/Camera.h"
#include "mc/deps/renderer/MatrixStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockRenderLayer.h"
#include "mc/world/level/block/BrightnessPair.h"
#include "mc/world/level/block/actor/BlockActorRendererId.h"
#include <glm/gtc/matrix_transform.hpp>
#include <Windows.h>
#pragma comment(lib, "user32.lib") // GetAsyncKeyState, probe builds only
#include <bitset>
#include <cmath>
#include <format>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace lamium::schematic::ghostProbe {
namespace {
template <class... Args>
void log(std::format_string<Args...> format, Args&&... args) noexcept {
    try { Runtime::instance().self().getLogger().info("{}", std::format(format, std::forward<Args>(args)...)); }
    catch (...) {}
}
// Each message is logged once so a frame loop cannot flood the log.
void once(std::string const& message) noexcept {
    static std::set<std::string> seen;
    try { if (seen.size() < 400 && seen.insert(message).second) log("ghost probe: {}", message); } catch (...) {}
}

struct Sample { char const* name; unsigned short data; };
constexpr Sample samples[] = {
    {"minecraft:stone", 0}, {"minecraft:oak_planks", 0}, {"minecraft:glass", 0}, {"minecraft:oak_stairs", 1},
    {"minecraft:grass_block", 0}, {"minecraft:oak_fence", 0}, {"minecraft:torch", 0}, {"minecraft:chest", 0},
};
// F6 cycles the variant so only one is on screen at a time. Round 2 showed the
// GUI path and the unfilled append draws invisible/opaque and offset, and the
// in-world path crashing in a private tessellator, so round 3 fixes the append
// mesh (corner offset, light UVs) and adds the opaque fallback look.
enum Variant { Translucent, TranslucentRendererMaterial, OpaqueOutlined, VariantCount };
char const* variantName(int variant) {
    switch (variant) {
    case Translucent: return "1 translucent: append fixed + moving_block_blend";
    case TranslucentRendererMaterial: return "2 translucent: append fixed + renderer blend material";
    default: return "3 opaque fallback: tinted + cyan outline";
    }
}
int variant = Translucent;

std::optional<BlockPos> anchor;
bool keyDown = false, cycleKeyDown = false;

void pollKeys(LocalPlayer& player) {
    bool now = (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
    if (now && !keyDown) {
        if (anchor) { anchor.reset(); log("ghost probe: cleared"); }
        else {
            Vec3 feet = player.getFeetPos(), view = player.getViewVector(1.f);
            anchor = BlockPos{static_cast<int>(std::floor(feet.x + view.x * 3)), static_cast<int>(std::floor(feet.y)),
                static_cast<int>(std::floor(feet.z + view.z * 3))};
            log("ghost probe: anchored at {},{},{}; samples every 2 blocks along +X, then a 2x2x2 stone cluster; "
                "variant {}", anchor->x, anchor->y, anchor->z, variantName(variant));
        }
    }
    keyDown = now;
    bool cycle = (GetAsyncKeyState(VK_F6) & 0x8000) != 0;
    if (cycle && !cycleKeyDown) { variant = (variant + 1) % VariantCount; log("ghost probe: variant {}", variantName(variant)); }
    cycleKeyDown = cycle;
}

// Translate the world matrix to `offset` (relative to the camera) while `draw` runs.
template <class Draw>
void translated(ScreenContext& screen, glm::vec3 offset, Draw&& draw) {
    auto ref = screen.camera.worldMatrixStack->push(false);
    ref.stack->_isDirty = true;
    ref.mat->_m = glm::translate(ref.mat->_m.get(), offset);
    draw();
    ref.stack->_isDirty = true;
    if (ref.stack->sortOrigin->has_value() && (ref.stack->stack->size() - 1) <= ref.stack->sortOrigin->value())
        ref.stack->sortOrigin->reset();
    ref.stack->stack->pop_back();
    ref.mat = nullptr;
    ref.stack = nullptr;
}

mce::MaterialPtr blendMaterial() {
    mce::MaterialPtr common(mce::RenderMaterialGroup::common(), HashedString{"moving_block_blend"});
    if (common.mRenderMaterialInfoPtr) { once("moving_block_blend resolved in common"); return common; }
    mce::MaterialPtr switchable(mce::RenderMaterialGroup::switchable(), HashedString{"moving_block_blend"});
    once(switchable.mRenderMaterialInfoPtr ? "moving_block_blend resolved in switchable" : "moving_block_blend not found");
    return switchable;
}

void setupLight(ScreenContext& screen, IClientInstance& client, BlockSource& region) {
    auto* texture = client.getLightTexture();
    if (!texture) { once("no light texture"); return; }
    BrightnessPair full;
    full.sky->mValue = 15;
    full.block->mValue = 15;
    ActorShaderManager::setupShaderParameters(screen, region, full, glm::vec4{1, 1, 1, 1}, 1.f, true, *texture,
        Vec2{1, 1}, Vec4{0, 0, 1, 1});
}

struct Bounds { glm::vec3 min{1e9f}, max{-1e9f}; };
Bounds bounds(Tessellator& batch) {
    Bounds result;
    for (auto const& p : batch.mMeshData->mPositions.get()) { result.min = glm::min(result.min, p); result.max = glm::max(result.max, p); }
    return result;
}
// The appended (GUI) mesh has no light UVs, which the block shader reads.
void fillLightUVs(Tessellator& batch, std::string const& tag) {
    auto& data = batch.mMeshData.get();
    size_t vertices = data.mPositions->size();
    auto& uv1 = data.mTextureUVs[1].get();
    once(std::format("{}: uv0 {} uv1 {} uv2 {} colors {} of {}", tag, data.mTextureUVs[0].get().size(), uv1.size(),
        data.mTextureUVs[2].get().size(), data.mColors->size(), vertices));
    if (uv1.size() != vertices) uv1.assign(vertices, glm::vec2{1.f, 1.f});
}

void drawOutline(ScreenContext& screen, glm::vec3 offset) {
    mce::MaterialPtr material(mce::RenderMaterialGroup::common(), HashedString{"debug"});
    if (!material.mRenderMaterialInfoPtr) return;
    Tessellator lines(screen.tessellator.mBufferResourceService);
    lines.begin({}, mce::PrimitiveMode::LineList, 24, false);
    lines.color(.35f, .85f, 1.f, 1.f);
    constexpr float lo = -.002f, hi = 1.002f;
    glm::vec3 c[8];
    for (int i = 0; i < 8; ++i) c[i] = {i & 1 ? hi : lo, i & 2 ? hi : lo, i & 4 ? hi : lo};
    constexpr int edges[12][2] = {{0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7}};
    for (auto [a, b] : edges) { lines.vertex(c[a].x, c[a].y, c[a].z); lines.vertex(c[b].x, c[b].y, c[b].z); }
    translated(screen, offset, [&] { MeshHelpers::renderMeshImmediately(screen, lines, material, OffscreenCaptureDescription{}); });
}

void draw(BaseActorRenderContext& context) {
    IClientInstance& client = context.mClientInstance;
    auto* player = client.getLocalPlayer();
    if (!player || !context.mImpl) return;
    pollKeys(*player);
    if (!anchor) return;
    ScreenContext& screen = context.mScreenContext;
    Vec3 const camera = context.mImpl->mCameraPosition;
    auto& region = player->getDimensionBlockSource();

    auto& dispatcher = client.getBlockEntityRenderDispatcher();
    auto& moving = dispatcher.mRenderers.get()[BlockActorRendererId::MovingBlock];
    auto* movingRenderer = static_cast<MovingBlockActorRenderer*>(moving.get());
    if (!movingRenderer) { once("no moving block renderer"); return; }
    mce::TexturePtr const& atlas = movingRenderer->mAtlasTexture.get();
    mce::MaterialPtr const& rendererBlend =
        movingRenderer->mBlockMaterials[static_cast<int>(BlockRenderLayer::RenderlayerBlend)].get();
    mce::MaterialPtr named = blendMaterial();

    // A private tessellator keeps our color override out of vanilla's caches.
    auto own = std::make_unique<BlockTessellator>(&region);
    own->mColorOverride = variant == OpaqueOutlined ? mce::Color{.72f, .9f, 1.f, 1.f} : mce::Color{1.f, 1.f, 1.f, .5f};

    struct Item { Sample sample; BlockPos pos; };
    std::vector<Item> items;
    for (int i = 0; i < static_cast<int>(std::size(samples)); ++i)
        items.push_back({samples[i], BlockPos{anchor->x + i * 2, anchor->y, anchor->z}});
    int cluster = static_cast<int>(std::size(samples)) * 2;
    for (int x = 0; x < 2; ++x) for (int y = 0; y < 2; ++y) for (int z = 0; z < 2; ++z)
        items.push_back({samples[0], BlockPos{anchor->x + cluster + x, anchor->y + y, anchor->z + z}});

    // The appended mesh is centered on the origin; shift by the stone's lower corner.
    std::optional<glm::vec3> shift;
    for (auto const& [sample, pos] : items) {
        std::string tag = std::format("{} {}", variantName(variant), sample.name);
        try {
            auto found = Block::tryGetFromRegistry(HashedString{sample.name}, sample.data);
            if (!found) { once(tag + ": block not found"); continue; }
            Block const& block = *found;
            glm::vec3 cell{static_cast<float>(pos.x - camera.x), static_cast<float>(pos.y - camera.y),
                static_cast<float>(pos.z - camera.z)};
            if (variant == OpaqueOutlined) drawOutline(screen, cell);
            mce::MaterialPtr const& material = variant == Translucent ? named : rendererBlend;
            if (!material.mRenderMaterialInfoPtr) { once(tag + ": no material"); continue; }
            Tessellator batch(screen.tessellator.mBufferResourceService);
            batch.begin({}, mce::PrimitiveMode::QuadList, 256, false);
            own->appendTessellatedBlock(batch, block);
            uint count = batch.mCount;
            if (!count) { once(tag + ": no vertices"); continue; }
            auto box = bounds(batch);
            once(std::format("{}: {} vertices, bounds {:.2f},{:.2f},{:.2f} .. {:.2f},{:.2f},{:.2f}", tag, count,
                box.min.x, box.min.y, box.min.z, box.max.x, box.max.y, box.max.z));
            if (!shift) shift = -box.min;
            fillLightUVs(batch, tag);
            translated(screen, cell + *shift, [&] {
                setupLight(screen, client, region);
                MeshHelpers::renderMeshImmediately(screen, batch, material, atlas, OffscreenCaptureDescription{});
            });
        } catch (std::exception const& error) {
            once(tag + ": failed: " + error.what());
        } catch (...) {
            once(tag + ": failed");
        }
    }
}

LL_TYPE_INSTANCE_HOOK(GhostProbeHook, ll::memory::HookPriority::Low, LevelRendererPlayer,
    &LevelRendererPlayer::$renderEntityEffects, void, BaseActorRenderContext& context) {
    origin(context);
    try { draw(context); } catch (...) { once("draw threw"); }
}
bool installed = false;
}
void start() {
    installed = GhostProbeHook::hook(true) == 0;
    if (!installed) throw std::runtime_error("Could not install the ghost probe");
    Runtime::instance().self().getLogger().warn("Ghost probe enabled: F7 anchors test blocks, F6 cycles the render variant");
}
void stop() {
    if (installed && GhostProbeHook::unhook(true)) installed = false;
    anchor.reset();
}
}
#else
namespace lamium::schematic::ghostProbe { void start() {} void stop() {} }
#endif
