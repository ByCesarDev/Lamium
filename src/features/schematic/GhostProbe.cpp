#include "features/schematic/GhostProbe.h"
#ifdef LAMIUM_GHOST_PROBE
#include "app/Runtime.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/ActorShaderManager.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/RenderMaterialGroup.h"
#include "mc/client/renderer/Tessellator.h"
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
#include <chrono>
#include <cmath>
#include <format>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

// Probe history (BACKLOG L-93 "Research"): rounds 1-7 settled on a private
// BlockTessellator + tessellateInWorld into one mesh sorted far to near, drawn
// with the moving-block renderer's blend material. Round 8 listed blocks without
// a mesh and showed the later passes change nothing (all record into the same
// frame). Round 9 compares materials: alpha test so empty texels write no
// depth, and an unlit blended material without depth writes.
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
    try { if (seen.size() < 600 && seen.insert(message).second) log("ghost probe: {}", message); } catch (...) {}
}

struct Sample { char const* name; unsigned short data; };
// Row 1: ordinary and special-shape blocks. Row 2: blocks drawn by block
// entity renderers in vanilla.
constexpr Sample row1[] = {
    {"minecraft:stone", 0}, {"minecraft:glass", 0}, {"minecraft:oak_stairs", 1}, {"minecraft:torch", 0},
    {"minecraft:lantern", 0}, {"minecraft:redstone_wire", 0}, {"minecraft:poppy", 0}, {"minecraft:lever", 0},
    {"minecraft:ladder", 2}, {"minecraft:rail", 0}, {"minecraft:glass_pane", 0}, {"minecraft:oak_slab", 0},
    {"minecraft:wooden_door", 0}, {"minecraft:trapdoor", 0}, {"minecraft:oak_leaves", 0}, {"minecraft:water", 0},
};
constexpr Sample row2[] = {
    {"minecraft:chest", 0}, {"minecraft:ender_chest", 0}, {"minecraft:bed", 0}, {"minecraft:standing_sign", 0},
    {"minecraft:skeleton_skull", 1}, {"minecraft:undyed_shulker_box", 0}, {"minecraft:flower_pot", 0},
    {"minecraft:campfire", 0}, {"minecraft:bell", 0}, {"minecraft:white_banner", 0}, {"minecraft:piston", 1},
    {"minecraft:end_portal_frame", 0},
};

// F6 cycles the look.
enum Look { OutlinedAlphaTest, TranslucentBlend, TranslucentNoDepth, LookCount };
char const* lookName(int look) {
    switch (look) {
    case OutlinedAlphaTest: return "1 tinted + outline, alpha-test material";
    case TranslucentBlend: return "2 translucent, blend material (round 7)";
    default: return "3 translucent, unlit without depth writes";
    }
}
int look = OutlinedAlphaTest;
std::optional<BlockPos> anchor;
bool anchorDown = false, lookDown = false;

bool pressed(int key, bool& down) {
    bool now = (GetAsyncKeyState(key) & 0x8000) != 0;
    bool edge = now && !down;
    down = now;
    return edge;
}
void pollKeys(LocalPlayer& player) {
    if (pressed(VK_F7, anchorDown)) {
        if (anchor) { anchor.reset(); log("ghost probe: cleared"); }
        else {
            Vec3 feet = player.getFeetPos(), view = player.getViewVector(1.f);
            anchor = BlockPos{static_cast<int>(std::floor(feet.x + view.x * 3)), static_cast<int>(std::floor(feet.y)),
                static_cast<int>(std::floor(feet.z + view.z * 3))};
            log("ghost probe: anchored at {},{},{}; row 1 along +X, row 2 three blocks along +Z; look {}",
                anchor->x, anchor->y, anchor->z, lookName(look));
        }
    }
    if (pressed(VK_F6, lookDown)) { look = (look + 1) % LookCount; log("ghost probe: look {}", lookName(look)); }
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
struct Outline { Bounds box; float r, g, b; };
void drawOutlines(ScreenContext& screen, glm::vec3 offset, std::vector<Outline> const& outlines) {
    mce::MaterialPtr material(mce::RenderMaterialGroup::common(), HashedString{"debug"});
    if (!material.mRenderMaterialInfoPtr || outlines.empty()) return;
    Tessellator lines(screen.tessellator.mBufferResourceService);
    lines.begin({}, mce::PrimitiveMode::LineList, static_cast<int>(outlines.size() * 24), false);
    constexpr int edges[12][2] = {{0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7}};
    for (auto const& [box, r, g, b] : outlines) {
        lines.color(r, g, b, 1.f);
        glm::vec3 low = box.min - glm::vec3{.002f}, high = box.max + glm::vec3{.002f}, c[8];
        for (int i = 0; i < 8; ++i) c[i] = {i & 1 ? high.x : low.x, i & 2 ? high.y : low.y, i & 4 ? high.z : low.z};
        for (auto [a, z] : edges) { lines.vertex(c[a].x, c[a].y, c[a].z); lines.vertex(c[z].x, c[z].y, c[z].z); }
    }
    translated(screen, offset, [&] { MeshHelpers::renderMeshImmediately(screen, lines, material, OffscreenCaptureDescription{}); });
}

// The in-world mesh may lack light UVs when a block has none; the shader reads them.
void fillLightUVs(Tessellator& batch) {
    auto& data = batch.mMeshData.get();
    auto& uv1 = data.mTextureUVs[1].get();
    if (uv1.size() != data.mPositions->size()) uv1.assign(data.mPositions->size(), glm::vec2{1.f, 1.f});
}
// Multiplies existing vertex colors (keeping baked face shading), filling white first if missing.
void tintColors(Tessellator& batch, float r, float g, float b, float a) {
    auto& data = batch.mMeshData.get();
    auto& colors = data.mColors.get();
    if (colors.size() != data.mPositions->size()) colors.assign(data.mPositions->size(), 0xffffffffu);
    auto scale = [](uint value, int shift, float factor) {
        return static_cast<uint>(std::lround(std::clamp(((value >> shift) & 255) * factor, 0.f, 255.f))) << shift;
    };
    for (auto& c : colors) c = scale(c, 0, r) | scale(c, 8, g) | scale(c, 16, b) | scale(c, 24, a);
}
template <class T>
void permute(std::vector<T>& values, std::vector<size_t> const& order, size_t vertices) {
    if (values.size() != vertices) return;
    std::vector<T> sorted;
    sorted.reserve(vertices);
    for (size_t quad : order) for (size_t k = 0; k < 4; ++k) sorted.push_back(values[quad * 4 + k]);
    values.swap(sorted);
}
// Reorders whole quads far to near from `eye`, in every per-vertex stream.
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
        glm::vec3 d = (positions[q*4] + positions[q*4+1] + positions[q*4+2] + positions[q*4+3]) * .25f - eye;
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

void draw(ScreenContext& screen, IClientInstance& client, Vec3 const& camera) {
    auto* player = client.getLocalPlayer();
    if (!player || !anchor) return;
    auto& region = player->getDimensionBlockSource();
    auto& dispatcher = client.getBlockEntityRenderDispatcher();
    auto* movingRenderer =
        static_cast<MovingBlockActorRenderer*>(dispatcher.mRenderers.get()[BlockActorRendererId::MovingBlock].get());
    if (!movingRenderer) { once("no moving block renderer"); return; }
    mce::TexturePtr const& atlas = movingRenderer->mAtlasTexture.get();
    bool outlined = look == OutlinedAlphaTest;
    mce::MaterialPtr beacon(mce::RenderMaterialGroup::switchable(), HashedString{"beacon_beam_transparent"});
    if (!beacon.mRenderMaterialInfoPtr) beacon = mce::MaterialPtr(mce::RenderMaterialGroup::common(), HashedString{"beacon_beam_transparent"});
    mce::MaterialPtr const& material = look == TranslucentNoDepth ? beacon
        : movingRenderer->mBlockMaterials[static_cast<int>(
            outlined ? BlockRenderLayer::RenderlayerAlphatest : BlockRenderLayer::RenderlayerBlend)].get();
    if (!material.mRenderMaterialInfoPtr) { once(std::format("{}: no material", lookName(look))); return; }

    // Private tessellator, primed with one appended block (round 2 crashed without).
    auto own = std::make_unique<BlockTessellator>(&region);
    if (auto stone = Block::tryGetFromRegistry(HashedString{"minecraft:stone"}, 0)) {
        Tessellator primer(screen.tessellator.mBufferResourceService);
        primer.begin({}, mce::PrimitiveMode::QuadList, 64, false);
        own->appendTessellatedBlock(primer, *stone);
    }

    struct Item { Sample sample; BlockPos pos; };
    std::vector<Item> items;
    for (int i = 0; i < static_cast<int>(std::size(row1)); ++i)
        items.push_back({row1[i], BlockPos{anchor->x + i * 2, anchor->y, anchor->z}});
    for (int i = 0; i < static_cast<int>(std::size(row2)); ++i)
        items.push_back({row2[i], BlockPos{anchor->x + i * 2, anchor->y, anchor->z + 3}});

    Tessellator batch(screen.tessellator.mBufferResourceService);
    batch.begin({}, mce::PrimitiveMode::QuadList, 4096, false);
    std::vector<Outline> outlines;
    for (auto const& [sample, pos] : items) {
        std::string tag = std::format("{}:{}", sample.name, sample.data);
        Bounds cell{{static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(pos.z)},
                    {pos.x + 1.f, pos.y + 1.f, pos.z + 1.f}};
        try {
            auto found = Block::tryGetFromRegistry(HashedString{sample.name}, sample.data);
            if (!found) { once(tag + ": block not found"); outlines.push_back({cell, 1.f, .2f, .2f}); continue; }
            size_t before = batch.mMeshData->mPositions->size();
            own->tessellateInWorld(batch, *found, pos, false);
            auto const& positions = batch.mMeshData->mPositions.get();
            size_t added = positions.size() - before;
            once(std::format("{}: {} vertices", tag, added));
            // No mesh: an orange full-block outline marks it.
            if (!added) { outlines.push_back({cell, 1.f, .55f, .1f}); continue; }
            Bounds box;
            for (size_t v = before; v < positions.size(); ++v) {
                box.min = glm::min(box.min, positions[v]);
                box.max = glm::max(box.max, positions[v]);
            }
            if (outlined) outlines.push_back({box, .35f, .85f, 1.f});
        } catch (std::exception const& error) {
            once(tag + ": failed: " + error.what());
        } catch (...) {
            once(tag + ": failed");
        }
    }
    glm::vec3 const eye{static_cast<float>(camera.x), static_cast<float>(camera.y), static_cast<float>(camera.z)};
    drawOutlines(screen, -eye, outlines);
    if (!batch.mCount) return;
    fillLightUVs(batch);
    // The unlit material reads normals, which in-world meshes may not carry.
    if (auto& normals = batch.mMeshData->mNormals.get(); normals.size() != batch.mMeshData->mPositions->size()) {
        once(std::format("filling normals ({} of {})", normals.size(), batch.mMeshData->mPositions->size()));
        normals.assign(batch.mMeshData->mPositions->size(), glm::vec4{0.f, 1.f, 0.f, 0.f});
    }
    if (outlined) tintColors(batch, .62f, .85f, 1.f, 1.f);
    else tintColors(batch, 1.f, 1.f, 1.f, .5f);
    sortQuads(batch, eye);
    translated(screen, -eye, [&] {
        setupLight(screen, client, region);
        MeshHelpers::renderMeshImmediately(screen, batch, material, atlas, OffscreenCaptureDescription{});
    });
}

LL_TYPE_INSTANCE_HOOK(EffectsPassHook, ll::memory::HookPriority::Low, LevelRendererPlayer,
    &LevelRendererPlayer::$renderEntityEffects, void, BaseActorRenderContext& context) {
    origin(context);
    try {
        IClientInstance& client = context.mClientInstance;
        if (auto* player = client.getLocalPlayer()) pollKeys(*player);
        if (context.mImpl) draw(context.mScreenContext, client, context.mImpl->mCameraPosition);
    } catch (...) { once("effects pass threw"); }
}
bool effects = false;
}
void start() {
    effects = EffectsPassHook::hook(true) == 0;
    if (!effects) throw std::runtime_error("Could not install the ghost probe");
    Runtime::instance().self().getLogger().warn("Ghost probe enabled: F7 anchors test blocks, F6 cycles the look");
}
void stop() {
    if (effects && EffectsPassHook::unhook(true)) effects = false;
    anchor.reset();
}
}
#else
namespace lamium::schematic::ghostProbe { void start() {} void stop() {} }
#endif
