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
#include <algorithm>
#include <bitset>
#include <chrono>
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
    {"minecraft:stone", 0}, {"minecraft:oak_planks", 0}, {"minecraft:glass", 0}, {"minecraft:grass_block", 0},
    {"minecraft:oak_fence", 0}, {"minecraft:oak_stairs", 0}, {"minecraft:oak_stairs", 1}, {"minecraft:oak_stairs", 2},
    {"minecraft:oak_stairs", 3}, {"minecraft:oak_stairs", 4},
};
// F6 cycles the variant so only one is on screen at a time. Round 2 showed the
// GUI path and the unfilled append draws invisible/opaque and offset, and the
// in-world path crashing in a private tessellator, so round 3 fixes the append
// mesh (corner offset, light UVs) and adds the opaque fallback look.
// Round 7: one mesh for all ghost blocks, quads sorted far to near, so the
// engine cannot reorder separate draws between frames (the round 6 flicker).
enum Variant { SortedTranslucent, SortedOutlined, VariantCount };
char const* variantName(int variant) {
    switch (variant) {
    case SortedTranslucent: return "1 one sorted mesh, translucent";
    default: return "2 one sorted mesh, tinted + outline";
    }
}
int variant = SortedTranslucent;

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
    // Like shapes, scale slightly toward the eye so coplanar faces cannot fight.
    constexpr float towardEye = .998f;
    ref.mat->_m = glm::scale(glm::translate(ref.mat->_m.get(), offset * towardEye), glm::vec3{towardEye});
    draw();
    ref.stack->_isDirty = true;
    if (ref.stack->sortOrigin->has_value() && (ref.stack->stack->size() - 1) <= ref.stack->sortOrigin->value())
        ref.stack->sortOrigin->reset();
    ref.stack->stack->pop_back();
    ref.mat = nullptr;
    ref.stack = nullptr;
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
// The appended (GUI) mesh has no light UVs, which the block shader reads.
void fillLightUVs(Tessellator& batch, std::string const& tag) {
    auto& data = batch.mMeshData.get();
    size_t vertices = data.mPositions->size();
    auto& uv1 = data.mTextureUVs[1].get();
    once(std::format("{}: uv0 {} uv1 {} uv2 {} colors {} of {}", tag, data.mTextureUVs[0].get().size(), uv1.size(),
        data.mTextureUVs[2].get().size(), data.mColors->size(), vertices));
    if (uv1.size() != vertices) uv1.assign(vertices, glm::vec2{1.f, 1.f});
}
// The appended mesh has no vertex colors either (the color override is not
// applied on this path), so write them: RGBA bytes, red lowest.
// Multiplies existing vertex colors (keeping baked face shading) or fills
// white ones first when the mesh has none.
void tintColors(Tessellator& batch, float r, float g, float b, float a) {
    auto& data = batch.mMeshData.get();
    auto& colors = data.mColors.get();
    if (colors.size() != data.mPositions->size()) colors.assign(data.mPositions->size(), 0xffffffffu);
    auto scale = [](uint value, int shift, float factor) {
        return static_cast<uint>(std::lround(std::clamp(((value >> shift) & 255) * factor, 0.f, 255.f))) << shift;
    };
    for (auto& c : colors) c = scale(c, 0, r) | scale(c, 8, g) | scale(c, 16, b) | scale(c, 24, a);
}

void drawOutlines(ScreenContext& screen, glm::vec3 offset, std::vector<Bounds> const& boxes) {
    mce::MaterialPtr material(mce::RenderMaterialGroup::common(), HashedString{"debug"});
    if (!material.mRenderMaterialInfoPtr || boxes.empty()) return;
    Tessellator lines(screen.tessellator.mBufferResourceService);
    lines.begin({}, mce::PrimitiveMode::LineList, static_cast<int>(boxes.size() * 24), false);
    lines.color(.35f, .85f, 1.f, 1.f);
    constexpr int edges[12][2] = {{0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7}};
    for (auto box : boxes) {
        glm::vec3 low = box.min - glm::vec3{.002f}, high = box.max + glm::vec3{.002f}, c[8];
        for (int i = 0; i < 8; ++i) c[i] = {i & 1 ? high.x : low.x, i & 2 ? high.y : low.y, i & 4 ? high.z : low.z};
        for (auto [a, b] : edges) { lines.vertex(c[a].x, c[a].y, c[a].z); lines.vertex(c[b].x, c[b].y, c[b].z); }
    }
    translated(screen, offset, [&] { MeshHelpers::renderMeshImmediately(screen, lines, material, OffscreenCaptureDescription{}); });
}

// Reorders whole quads far to near from `eye`, in every per-vertex stream.
template <class T>
void permute(std::vector<T>& values, std::vector<size_t> const& order, size_t vertices) {
    if (values.size() != vertices) return;
    std::vector<T> sorted;
    sorted.reserve(vertices);
    for (size_t quad : order) for (size_t k = 0; k < 4; ++k) sorted.push_back(values[quad * 4 + k]);
    values.swap(sorted);
}
void sortQuads(Tessellator& batch, glm::vec3 eye) {
    auto& data = batch.mMeshData.get();
    auto& positions = data.mPositions.get();
    size_t vertices = positions.size();
    if (vertices % 4 || !data.mIndices->empty()) {
        once(std::format("cannot sort: {} vertices, {} indices", vertices, data.mIndices->size()));
        return;
    }
    size_t quads = vertices / 4;
    std::vector<float> distance(quads);
    for (size_t q = 0; q < quads; ++q) {
        glm::vec3 center = (positions[q*4] + positions[q*4+1] + positions[q*4+2] + positions[q*4+3]) * .25f;
        glm::vec3 d = center - eye;
        distance[q] = glm::dot(d, d);
    }
    std::vector<size_t> order(quads);
    for (size_t q = 0; q < quads; ++q) order[q] = q;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return distance[a] > distance[b]; });
    permute(positions, order, vertices);
    permute(data.mNormals.get(), order, vertices);
    permute(data.mTangents.get(), order, vertices);
    permute(data.mColors.get(), order, vertices);
    permute(data.mBoneId0s.get(), order, vertices);
    for (int i = 0; i < 3; ++i) permute(data.mTextureUVs[i].get(), order, vertices);
    permute(data.mPBRTextureIndices.get(), order, vertices);
    permute(data.mMERS.get(), order, vertices);
    permute(data.mGeoType.get(), order, vertices);
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
    mce::MaterialPtr const& material =
        movingRenderer->mBlockMaterials[static_cast<int>(BlockRenderLayer::RenderlayerBlend)].get();
    if (!material.mRenderMaterialInfoPtr) { once("no material"); return; }

    // A private tessellator keeps our changes out of vanilla's caches. Round 2
    // crashed in in-world tessellation on one that had not appended a block
    // yet that frame; priming it avoided that in rounds 5-6.
    auto own = std::make_unique<BlockTessellator>(&region);
    if (auto stone = Block::tryGetFromRegistry(HashedString{"minecraft:stone"}, 0)) {
        Tessellator primer(screen.tessellator.mBufferResourceService);
        primer.begin({}, mce::PrimitiveMode::QuadList, 64, false);
        own->appendTessellatedBlock(primer, *stone);
    }

    struct Item { Sample sample; BlockPos pos; };
    std::vector<Item> items;
    for (int i = 0; i < static_cast<int>(std::size(samples)); ++i)
        items.push_back({samples[i], BlockPos{anchor->x + i * 2, anchor->y, anchor->z}});
    int cluster = static_cast<int>(std::size(samples)) * 2;
    for (int x = 0; x < 2; ++x) for (int y = 0; y < 2; ++y) for (int z = 0; z < 2; ++z)
        items.push_back({samples[0], BlockPos{anchor->x + cluster + x, anchor->y + y, anchor->z + z}});
    // Glass in front of planks, the pair that flickered in round 6.
    items.push_back({samples[2], BlockPos{anchor->x, anchor->y, anchor->z + 2}});
    items.push_back({samples[1], BlockPos{anchor->x, anchor->y, anchor->z + 3}});

    bool outlined = variant == SortedOutlined;
    Tessellator batch(screen.tessellator.mBufferResourceService);
    batch.begin({}, mce::PrimitiveMode::QuadList, 2048, false);
    std::vector<Bounds> boxes;
    for (auto const& [sample, pos] : items) {
        std::string tag = std::format("{} {}:{}", variantName(variant), sample.name, sample.data);
        try {
            auto found = Block::tryGetFromRegistry(HashedString{sample.name}, sample.data);
            if (!found) { once(tag + ": block not found"); continue; }
            size_t before = batch.mMeshData->mPositions->size();
            own->tessellateInWorld(batch, *found, pos, false);
            auto const& positions = batch.mMeshData->mPositions.get();
            if (positions.size() == before) { once(tag + ": no vertices"); continue; }
            Bounds box;
            for (size_t v = before; v < positions.size(); ++v) {
                box.min = glm::min(box.min, positions[v]);
                box.max = glm::max(box.max, positions[v]);
            }
            boxes.push_back(box);
        } catch (std::exception const& error) {
            once(tag + ": failed: " + error.what());
        } catch (...) {
            once(tag + ": failed");
        }
    }
    if (!batch.mCount) return;
    glm::vec3 const eye{static_cast<float>(camera.x), static_cast<float>(camera.y), static_cast<float>(camera.z)};
    fillLightUVs(batch, "combined");
    if (outlined) tintColors(batch, .62f, .85f, 1.f, 1.f);
    else tintColors(batch, 1.f, 1.f, 1.f, .5f);
    sortQuads(batch, eye);
    once(std::format("combined mesh: {} vertices, {} blocks", static_cast<uint>(batch.mCount), boxes.size()));
    if (outlined) drawOutlines(screen, -eye, boxes);
    translated(screen, -eye, [&] {
        setupLight(screen, client, region);
        MeshHelpers::renderMeshImmediately(screen, batch, material, atlas, OffscreenCaptureDescription{});
    });
}

LL_TYPE_INSTANCE_HOOK(GhostProbeHook, ll::memory::HookPriority::Low, LevelRendererPlayer,
    &LevelRendererPlayer::$renderEntityEffects, void, BaseActorRenderContext& context) {
    origin(context);
    // Counts calls per 5 s to see whether this pass runs more than once a frame.
    static unsigned calls = 0;
    static auto since = std::chrono::steady_clock::now();
    ++calls;
    if (anchor && std::chrono::steady_clock::now() - since > std::chrono::seconds(5)) {
        log("ghost probe: {} render passes in 5 s", calls);
        calls = 0; since = std::chrono::steady_clock::now();
    }
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
