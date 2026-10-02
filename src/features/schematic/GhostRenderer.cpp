#include "features/schematic/GhostRenderer.h"
#include "features/schematic/SchematicSession.h"
#include "app/Runtime.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/memory/Hook.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/ActorShaderManager.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/RenderMaterialGroup.h"
#include "mc/client/renderer/SupplementaryFieldAutoGenerationMode.h"
#include "mc/client/renderer/Tessellator.h"
#include "mc/client/renderer/block/BlockTessellator.h"
#include "mc/client/renderer/blockactor/BlockActorRenderDispatcher.h"
#include "mc/client/renderer/blockactor/MovingBlockActorRenderer.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/client/renderer/ptexture/LightTexture.h"
#include "mc/deps/core/math/Color.h"
#include "mc/deps/core_graphics/enums/PrimitiveMode.h"
#include "mc/deps/minecraft_renderer/framebuilder/dragon/RenderMetadata.h"
#include "mc/deps/minecraft_renderer/renderer/MaterialPtr.h"
#include "mc/deps/minecraft_renderer/renderer/Mesh.h"
#include "mc/deps/minecraft_renderer/renderer/TexturePtr.h"
#include "mc/deps/minecraft_renderer/resources/ClientTexture.h"
#include "mc/deps/minecraft_renderer/resources/OffscreenCaptureDescription.h"
#include "mc/deps/minecraft_renderer/resources/ServerTexture.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/renderer/Camera.h"
#include "mc/deps/renderer/MatrixStack.h"
#include "mc/util/Mirror.h"
#include "mc/util/Rotation.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/phys/HitResult.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockRenderLayer.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/BrightnessPair.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/BlockActorRendererId.h"
#include "mc/world/level/block/actor/VanillaBlockActorFactory.h"
#include "mc/world/level/block/states/VanillaBlockStateTransformUtils.h"
#include "mc/world/level/chunk/ChunkState.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/material/Material.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <map>
#include <optional>
#include <tuple>
#include <vector>

namespace lamium::schematic::ghosts {
namespace {
using Clock = std::chrono::steady_clock;
constexpr int sectionSize = 16;
constexpr int sectionBudget = 3;      // Sections rebuilt per frame.
// Re-check the world this often: quickly near the camera, where blocks are
// being placed, slowly elsewhere.
constexpr std::chrono::milliseconds refreshNear{250}, refreshFar{2000};
constexpr double nearDistance = 24;
constexpr std::chrono::milliseconds lookedDelay{100};
constexpr double drawDistance = 192;  // Sections farther than this are not built or drawn.
constexpr float towardEye = .998f;    // Like shapes: stay in front of coplanar terrain faces.

struct Outline { glm::vec3 min, max; float r, g, b; };
struct EntityCell { BlockPos pos; Block const* block; };
struct Section {
    glm::vec3 origin{};
    std::optional<mce::Mesh> faces, lines, marks;
    std::uint32_t faceVertices = 0, lineVertices = 0, markVertices = 0;
    std::vector<EntityCell> entities;
    Clock::time_point built{};
    std::optional<Clock::time_point> due; // An early rebuild after a looked-at block changed.
    bool complete = false; // false while some chunk was not loaded
};
using SectionKey = std::tuple<int, int, int, int>; // placement, section x, y, z
// Game blocks for a placement's palette, turned by the game's own transform.
struct Resolved {
    Structure const* structure = nullptr;
    int rotation = 0;
    Mirror mirror = Mirror::None;
    std::vector<Block const*> blocks;
};

std::map<SectionKey, Section> sections;
std::vector<Resolved> resolved;
// Created once per cell; a null result is remembered too.
std::map<std::tuple<int, int, int>, std::optional<std::shared_ptr<BlockActor>>> actors;
std::vector<std::pair<BlockPos, Block const*>> watched; // Recently looked-at cells and what was there.
std::uint64_t builtRevision = 0;
int builtDimension = -1;
std::atomic<bool> releaseRequested{false};
ll::event::ListenerPtr exitListener;
bool installed = false;

void log(std::string const& text) {
    try { Runtime::instance().self().getLogger().info("Schematic ghosts: {}", text); } catch (...) {}
}
void release() {
    sections.clear();
    resolved.clear();
    actors.clear();
    watched.clear();
    builtRevision = 0;
}

::Rotation gameRotation(int quarterTurns) {
    switch (quarterTurns) {
    case 1: return ::Rotation::Clockwise90;
    case 2: return ::Rotation::Clockwise180;
    case 3: return ::Rotation::CounterClockwise90;
    default: return ::Rotation::None;
    }
}
::Mirror gameMirror(Mirror mirror) {
    return mirror == Mirror::X ? ::Mirror::X : mirror == Mirror::Z ? ::Mirror::Z : ::Mirror::None;
}
// The palette entry as the NBT the game's block registry reads.
Block const* lookup(PaletteBlock const& entry) {
    nbt::Root root;
    root.compound.set("name", {entry.name});
    root.compound.set("states", {entry.states});
    root.compound.set("version", {entry.version});
    auto tag = CompoundTag::fromBinaryNbt(nbt::write(root));
    if (!tag) return nullptr;
    auto block = Block::tryGetFromRegistry(*tag);
    return block ? &*block : nullptr;
}
Resolved resolve(Structure const& structure, SavedPlacement const& placement) {
    Resolved out{&structure, placement.placement.rotation, placement.placement.mirror, {}};
    out.blocks.reserve(structure.palette.size());
    unsigned missing = 0;
    for (auto const& entry : structure.palette) {
        Block const* block = entry.isAir() ? nullptr : lookup(entry);
        if (!block && !entry.isAir()) ++missing;
        if (block && (out.rotation || out.mirror != Mirror::None))
            if (auto const* turned = VanillaBlockStateTransformUtils::transformBlock(*block, gameRotation(out.rotation), gameMirror(out.mirror)))
                block = turned;
        out.blocks.push_back(block);
    }
    if (missing) log(std::format("{}: {} palette entries are not known blocks", placement.file, missing));
    return out;
}

// Light UVs and colors: the in-world mesh carries both, but fill when absent.
void finishColors(Tessellator& batch, float r, float g, float b) {
    auto& data = batch.mMeshData.get();
    size_t vertices = data.mPositions->size();
    auto& uv1 = data.mTextureUVs[1].get();
    if (uv1.size() != vertices) uv1.assign(vertices, glm::vec2{1.f, 1.f});
    auto& colors = data.mColors.get();
    if (colors.size() != vertices) colors.assign(vertices, 0xffffffffu);
    auto scale = [](std::uint32_t value, int shift, float factor) {
        return static_cast<std::uint32_t>(std::lround(std::clamp(((value >> shift) & 255) * factor, 0.f, 255.f))) << shift;
    };
    for (auto& c : colors) c = scale(c, 0, r) | scale(c, 8, g) | scale(c, 16, b) | (c & 0xff000000u);
}

void buildSection(ScreenContext& screen, BlockSource& region, BlockTessellator& own, session::Shown const& shown,
                  Resolved const& blocks, SectionKey key, Section& out) {
    auto const& structure = *shown.structure;
    auto const& placement = shown.placement;
    Size placed = placedSize(structure.size, placement.placement.rotation);
    Point const& origin = placement.placement.origin;
    auto [index, sx, sy, sz] = key;
    Point low{sx * sectionSize, sy * sectionSize, sz * sectionSize};
    // Reset in place: meshes cannot be copied or assigned.
    out.faces.reset(); out.lines.reset(); out.marks.reset();
    out.faceVertices = out.lineVertices = out.markVertices = 0;
    out.entities.clear();
    out.due.reset();
    out.origin = {static_cast<float>(low.x), static_cast<float>(low.y), static_cast<float>(low.z)};
    out.built = Clock::now();
    out.complete = true;

    Tessellator batch(screen.tessellator.mBufferResourceService);
    batch.begin({}, mce::PrimitiveMode::QuadList, 4096, false);
    // Mistakes also get tinted faces just outside the real block, so they
    // stay visible next to the vanilla selection outline.
    std::vector<Outline> outlines, marks;
    auto cellBox = [](BlockPos p) { return std::pair{glm::vec3(p.x, p.y, p.z), glm::vec3(p.x + 1, p.y + 1, p.z + 1)}; };
    for (int x = std::max(low.x, origin.x); x < std::min(low.x + sectionSize, origin.x + placed.x); ++x)
        for (int y = std::max(low.y, origin.y); y < std::min(low.y + sectionSize, origin.y + placed.y); ++y)
            for (int z = std::max(low.z, origin.z); z < std::min(low.z + sectionSize, origin.z + placed.z); ++z) {
                Point offset{x - origin.x, y - origin.y, z - origin.z};
                if (!layerShown(placement.layers, placed, offset)) continue;
                auto local = toLocal(structure.size, placement.placement, {x, y, z});
                if (!local) continue;
                auto paletteIndex = structure.blocks[static_cast<size_t>(structure.cell(local->x, local->y, local->z))];
                if (paletteIndex == voidCell) continue;
                Block const* expected = blocks.blocks[static_cast<size_t>(paletteIndex)];
                bool expectsAir = structure.palette[static_cast<size_t>(paletteIndex)].isAir();
                BlockPos pos{x, y, z};
                auto* chunk = region.getChunkAt(pos);
                // While a chunk arrives the client shows placeholder blocks:
                // neither counts as built until it is loaded.
                if (!chunk || chunk->mLoadState->load() < ChunkState::Loaded) { out.complete = false; continue; }
                Block const& actual = region.getBlock(pos);
                if (actual.getMaterial().mType == SharedTypes::v1_26_20::MaterialType::ClientRequestPlaceholder) {
                    out.complete = false;
                    continue;
                }
                auto [boxLow, boxHigh] = cellBox(pos);
                if (expectsAir) {
                    // An extra block: red outline (the real block hides any ghost).
                    if (placement.countExtras && !actual.isAir()) {
                        outlines.push_back({boxLow, boxHigh, 1.f, .25f, .2f});
                        marks.push_back({boxLow, boxHigh, 1.f, .25f, .2f});
                    }
                    continue;
                }
                if (!expected) { outlines.push_back({boxLow, boxHigh, 1.f, .55f, .1f}); continue; } // unknown block name
                if (&actual == expected) continue; // placed correctly
                if (!actual.isAir()) {
                    bool sameType = &actual.getBlockType() == &expected->getBlockType();
                    // Something else is there: red, or yellow when only the state differs.
                    Outline mark = sameType ? Outline{boxLow, boxHigh, 1.f, .8f, .2f} : Outline{boxLow, boxHigh, 1.f, .25f, .2f};
                    outlines.push_back(mark);
                    marks.push_back(mark);
                    continue;
                }
                size_t before = batch.mMeshData->mPositions->size();
                own.tessellateInWorld(batch, *expected, pos, false);
                auto& positions = batch.mMeshData->mPositions.get();
                if (positions.size() == before) {
                    // No block mesh: block entities draw through their renderer.
                    out.entities.push_back({pos, expected});
                    outlines.push_back({boxLow, boxHigh, .35f, .85f, 1.f});
                    continue;
                }
                glm::vec3 shapeLow{1e9f}, shapeHigh{-1e9f};
                for (size_t v = before; v < positions.size(); ++v) {
                    shapeLow = glm::min(shapeLow, positions[v]);
                    shapeHigh = glm::max(shapeHigh, positions[v]);
                }
                outlines.push_back({shapeLow, shapeHigh, .35f, .85f, 1.f});
            }

    if (batch.mCount) {
        // Section-relative vertices keep float precision far from the origin.
        for (auto& p : batch.mMeshData->mPositions.get()) p -= out.origin;
        finishColors(batch, .62f, .85f, 1.f);
        out.faceVertices = batch.mCount;
        out.faces.emplace(batch.end(Tessellator::UploadMode::Buffered, "Lamium schematic ghosts", SupplementaryFieldAutoGenerationMode{}));
    }
    if (!marks.empty()) {
        Tessellator quads(screen.tessellator.mBufferResourceService);
        quads.begin({}, mce::PrimitiveMode::QuadList, static_cast<int>(marks.size() * 48), false);
        for (auto const& m : marks) {
            quads.color(m.r, m.g, m.b, .3f);
            glm::vec3 a = m.min - glm::vec3{.01f} - out.origin, b = m.max + glm::vec3{.01f} - out.origin;
            glm::vec3 c[8];
            for (int i = 0; i < 8; ++i) c[i] = {i & 1 ? b.x : a.x, i & 2 ? b.y : a.y, i & 4 ? b.z : a.z};
            constexpr int sides[6][4] = {{0,2,6,4},{1,5,7,3},{0,4,5,1},{2,3,7,6},{0,1,3,2},{4,6,7,5}};
            for (auto const& side : sides) {
                for (int k = 0; k < 4; ++k) quads.vertex(c[side[k]].x, c[side[k]].y, c[side[k]].z);
                for (int k = 3; k >= 0; --k) quads.vertex(c[side[k]].x, c[side[k]].y, c[side[k]].z);
            }
        }
        out.markVertices = static_cast<std::uint32_t>(marks.size() * 48);
        out.marks.emplace(quads.end(Tessellator::UploadMode::Buffered, "Lamium schematic mistakes", SupplementaryFieldAutoGenerationMode{}));
    }
    if (!outlines.empty()) {
        Tessellator lines(screen.tessellator.mBufferResourceService);
        lines.begin({}, mce::PrimitiveMode::LineList, static_cast<int>(outlines.size() * 24), false);
        constexpr int edges[12][2] = {{0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7}};
        for (auto const& o : outlines) {
            lines.color(o.r, o.g, o.b, 1.f);
            glm::vec3 a = o.min - glm::vec3{.002f} - out.origin, b = o.max + glm::vec3{.002f} - out.origin, c[8];
            for (int i = 0; i < 8; ++i) c[i] = {i & 1 ? b.x : a.x, i & 2 ? b.y : a.y, i & 4 ? b.z : a.z};
            for (auto [i, j] : edges) { lines.vertex(c[i].x, c[i].y, c[i].z); lines.vertex(c[j].x, c[j].y, c[j].z); }
        }
        out.lineVertices = static_cast<std::uint32_t>(outlines.size() * 24);
        out.lines.emplace(lines.end(Tessellator::UploadMode::Buffered, "Lamium schematic outlines", SupplementaryFieldAutoGenerationMode{}));
    }
}

template <class Draw>
void translated(ScreenContext& screen, glm::vec3 offset, Draw&& draw) {
    auto ref = screen.camera.worldMatrixStack->push(false);
    ref.stack->_isDirty = true;
    ref.mat->_m = glm::scale(glm::translate(ref.mat->_m.get(), offset * towardEye), glm::vec3{towardEye});
    draw();
    // Pop manually, as the world overlay does for this stack.
    ref.stack->_isDirty = true;
    if (ref.stack->sortOrigin->has_value() && (ref.stack->stack->size() - 1) <= ref.stack->sortOrigin->value())
        ref.stack->sortOrigin->reset();
    ref.stack->stack->pop_back();
    ref.mat = nullptr;
    ref.stack = nullptr;
}

void drawPlacements(BaseActorRenderContext& context, IClientInstance& client, LocalPlayer& player) {
    auto snapshot = session::snapshot();
    int dimension = static_cast<int>(player.getDimensionId());
    if (snapshot.revision != builtRevision || dimension != builtDimension) {
        release();
        builtRevision = snapshot.revision;
        builtDimension = dimension;
        for (auto const& shown : snapshot.placements)
            resolved.push_back(shown.structure ? resolve(*shown.structure, shown.placement) : Resolved{});
    }
    ScreenContext& screen = context.mScreenContext;
    Vec3 const camera = context.mImpl->mCameraPosition;
    auto& region = player.getDimensionBlockSource();

    // Sections near the camera, for visible placements in this dimension.
    struct Wanted { SectionKey key; double distance; };
    std::vector<Wanted> wanted;
    for (int i = 0; i < static_cast<int>(snapshot.placements.size()); ++i) {
        auto const& shown = snapshot.placements[static_cast<size_t>(i)];
        if (!shown.structure || !shown.placement.visible || shown.placement.dimension != dimension) continue;
        Size placed = placedSize(shown.structure->size, shown.placement.placement.rotation);
        Point const& o = shown.placement.placement.origin;
        auto section = [](int v) { return static_cast<int>(std::floor(v / static_cast<double>(sectionSize))); };
        for (int sx = section(o.x); sx <= section(o.x + placed.x - 1); ++sx)
            for (int sy = section(o.y); sy <= section(o.y + placed.y - 1); ++sy)
                for (int sz = section(o.z); sz <= section(o.z + placed.z - 1); ++sz) {
                    double cx = (sx + .5) * sectionSize - camera.x, cy = (sy + .5) * sectionSize - camera.y,
                           cz = (sz + .5) * sectionSize - camera.z;
                    double distance = std::sqrt(cx * cx + cy * cy + cz * cz);
                    if (distance <= drawDistance) wanted.push_back({{i, sx, sy, sz}, distance});
                }
    }
    std::sort(wanted.begin(), wanted.end(), [](auto const& a, auto const& b) { return a.distance < b.distance; });
    std::erase_if(sections, [&](auto const& entry) {
        return std::none_of(wanted.begin(), wanted.end(), [&](auto const& w) { return w.key == entry.first; });
    });

    // The block in the crosshair and the cell against its face are where a
    // block is broken or placed next: when either changes, rebuild its
    // section soon instead of waiting for the periodic refresh. Not at once:
    // the block exists a few frames before its terrain mesh is drawn, and
    // dropping the ghost then left an empty cell for a moment.
    std::vector<BlockPos> looked;
    if (auto const& hit = client.getLatestHitResult(); hit.mType == HitResultType::Tile) {
        static constexpr int offsets[6][3] = {{0,-1,0},{0,1,0},{0,0,-1},{0,0,1},{-1,0,0},{1,0,0}};
        BlockPos at = hit.mBlock;
        looked.push_back(at);
        if (hit.mFacing < 6) looked.push_back(BlockPos{at.x + offsets[hit.mFacing][0], at.y + offsets[hit.mFacing][1],
                                                         at.z + offsets[hit.mFacing][2]});
    }
    for (auto const& [pos, seen] : watched) {
        if (&region.getBlock(pos) == seen) continue;
        auto section = [](int v) { return static_cast<int>(std::floor(v / static_cast<double>(sectionSize))); };
        for (auto& [key, built] : sections)
            if (std::get<1>(key) == section(pos.x) && std::get<2>(key) == section(pos.y) && std::get<3>(key) == section(pos.z))
                if (!built.due) built.due = Clock::now() + lookedDelay;
    }
    // Keep the previous positions one more frame: placing moves the crosshair.
    std::vector<std::pair<BlockPos, Block const*>> next;
    for (auto const& pos : looked) next.push_back({pos, &region.getBlock(pos)});
    for (auto const& [pos, seen] : watched)
        if (next.size() < 6 && std::none_of(next.begin(), next.end(), [&](auto const& n) { return n.first == pos; }))
            next.push_back({pos, &region.getBlock(pos)});
    watched = std::move(next);

    // Rebuild the nearest missing or stale sections within the budget.
    int budget = sectionBudget;
    std::unique_ptr<BlockTessellator> own;
    auto now = Clock::now();
    for (auto const& w : wanted) {
        if (!budget) break;
        auto found = sections.find(w.key);
        auto refreshAfter = w.distance <= nearDistance ? std::chrono::duration_cast<Clock::duration>(refreshNear)
            : std::chrono::duration_cast<Clock::duration>(refreshFar);
        bool stale = found == sections.end() || !found->second.complete || now - found->second.built > refreshAfter
            || (found->second.due && now >= *found->second.due)
            || (found->second.faces && !found->second.faces->isValid()) || (found->second.lines && !found->second.lines->isValid())
            || (found->second.marks && !found->second.marks->isValid());
        if (!stale) continue;
        if (!own) {
            // A private tessellator, primed with one appended block: in-world
            // tessellation on a fresh one crashed in the probe.
            own = std::make_unique<BlockTessellator>(&region);
            if (auto stone = Block::tryGetFromRegistry(HashedString{"minecraft:stone"})) {
                Tessellator primer(screen.tessellator.mBufferResourceService);
                primer.begin({}, mce::PrimitiveMode::QuadList, 64, false);
                own->appendTessellatedBlock(primer, *stone);
            }
        }
        auto const index = static_cast<size_t>(std::get<0>(w.key));
        buildSection(screen, region, *own, snapshot.placements[index], resolved[index], w.key, sections[w.key]);
        --budget;
    }

    // Draw: alpha-tested ghost faces (empty texels let water and glass show
    // through), then outlines, then block-entity models.
    auto& dispatcher = client.getBlockEntityRenderDispatcher();
    auto* moving = static_cast<MovingBlockActorRenderer*>(dispatcher.mRenderers.get()[BlockActorRendererId::MovingBlock].get());
    if (!moving) return;
    mce::TexturePtr const& atlas = moving->mAtlasTexture.get();
    mce::MaterialPtr const& faces = moving->mBlockMaterials[static_cast<int>(BlockRenderLayer::RenderlayerAlphatest)].get();
    mce::MaterialPtr lineMaterial(mce::RenderMaterialGroup::common(), HashedString{"debug"});
    // Vertex-colored and blended, as shape faces use in Fancy graphics.
    mce::MaterialPtr markMaterial(mce::RenderMaterialGroup::switchable(), HashedString{"holo_hand_pointer"});
    auto* lightTexture = client.getLightTexture();
    std::variant<std::monostate, mce::TexturePtr, mce::ClientTexture, mce::ServerTexture> texture{atlas};
    for (auto& [key, section] : sections) {
        glm::vec3 offset{static_cast<float>(section.origin.x - camera.x), static_cast<float>(section.origin.y - camera.y),
                         static_cast<float>(section.origin.z - camera.z)};
        translated(screen, offset, [&] {
            if (section.faces && faces.mRenderMaterialInfoPtr && lightTexture) {
                BrightnessPair full;
                full.sky->mValue = 15;
                full.block->mValue = 15;
                ActorShaderManager::setupShaderParameters(screen, region, full, glm::vec4{1, 1, 1, 1}, 1.f, true,
                    *lightTexture, Vec2{1, 1}, Vec4{0, 0, 1, 1});
                section.faces->renderMesh(screen, faces, texture, 0, section.faceVertices, OffscreenCaptureDescription{}, nullptr);
            }
            if (section.marks && markMaterial.mRenderMaterialInfoPtr)
                section.marks->renderMesh(screen, markMaterial, gsl::span<mce::ClientTexture const*>{}, 0, section.markVertices,
                    OffscreenCaptureDescription{}, nullptr);
            if (section.lines && lineMaterial.mRenderMaterialInfoPtr)
                section.lines->renderMesh(screen, lineMaterial, gsl::span<mce::ClientTexture const*>{}, 0, section.lineVertices,
                    OffscreenCaptureDescription{}, nullptr);
        });
        for (auto const& [pos, block] : section.entities) {
            auto& actor = actors[{pos.x, pos.y, pos.z}];
            if (!actor) actor = VanillaBlockActorFactory::createBlockActor(pos, block->getBlockType());
            auto* component = *actor ? (*actor)->_getRenderComponent() : nullptr;
            if (!component) continue;
            Vec3 renderPos{static_cast<float>(pos.x - camera.x), static_cast<float>(pos.y - camera.y), static_cast<float>(pos.z - camera.z)};
            mce::MaterialPtr none(mce::RenderMaterialGroup::common(), HashedString{"lamium_no_forced_material"});
            dispatcher.render(context, region, *component, *block, renderPos, pos, false, none, nullptr, 0, std::nullopt);
        }
    }
}

LL_TYPE_INSTANCE_HOOK(GhostPass, ll::memory::HookPriority::Normal, LevelRendererPlayer,
    &LevelRendererPlayer::$renderEntityEffects, void, BaseActorRenderContext& context) {
    origin(context);
    auto& runtime = Runtime::instance();
    if (releaseRequested.exchange(false)) release();
    if (!runtime.enabled() || !runtime.preferences().schematic.enabled || !context.mImpl) {
        if (!sections.empty() || !resolved.empty()) release();
        return;
    }
    IClientInstance& client = context.mClientInstance;
    auto* player = client.getLocalPlayer();
    if (!player) return;
    try {
        drawPlacements(context, client, *player);
    } catch (std::exception const& error) {
        static bool reported = false;
        if (!std::exchange(reported, true)) log(std::string("drawing failed: ") + error.what());
    }
}
}

void start() {
    if (installed) return;
    installed = GhostPass::hook(true) == 0;
    if (!installed) throw std::runtime_error("Could not install the schematic ghost pass");
    exitListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::ClientExitLevelEvent>(
        [](auto&) { releaseRequested = true; });
}
void stop() {
    if (exitListener) {
        ll::event::EventBus::getInstance().removeListener(exitListener);
        exitListener.reset();
    }
    if (installed && GhostPass::unhook(true)) installed = false;
    releaseRequested = true;
}
}
