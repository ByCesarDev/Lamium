#include "features/information/TargetInfo.h"
#include "features/information/TargetCard.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ActorPlacerItem.h"
#include "mc/world/item/registry/ItemRegistryRef.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/block/BlockGraphics.h"
#include "mc/client/renderer/texture/TextureUVCoordinateSet.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/phys/HitResult.h"
#include "mc/world/phys/AABB.h"
#include "mc/world/phys/AABBHitResult.h"
#include "mc/world/level/ShapeType.h"
#include <cmath>
#include <mutex>
#include <unordered_map>
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/locale/I18n.h"
#include "mc/deps/nbt/CompoundTagVariant.h"
#include <algorithm>
#ifdef LAMIUM_RESEARCH_TRACE
#include "app/Runtime.h"
#include <format>
#endif

namespace lamium::information {
namespace {
// What a detached camera looks at: the nearest block or entity box along the
// ray. Only owned values and this frame's pointers leave this function.
struct Pick { HitResultType type = HitResultType::NoHit; BlockPos block; Actor* entity = nullptr; };
Pick pickAlong(LocalPlayer& player, ViewRay const& ray) {
    Vec3 from{static_cast<float>(ray.x), static_cast<float>(ray.y), static_cast<float>(ray.z)};
    Vec3 to{static_cast<float>(ray.x + ray.dx * ray.reach), static_cast<float>(ray.y + ray.dy * ray.reach),
            static_cast<float>(ray.z + ray.dz * ray.reach)};
    auto distance = [&](Vec3 const& p) {
        double dx = p.x - from.x, dy = p.y - from.y, dz = p.z - from.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    };
    auto& source = player.getDimensionBlockSource();
    Pick pick;
    double best = ray.reach;
    auto hit = source.clip(from, to, false, ShapeType::Outline, static_cast<int>(ray.reach) + 1, false, false, nullptr,
        [](BlockSource const&, Block const& block, bool) {
            // Like the game's own pick, look through water and lava.
            auto name = block.getTypeName();
            return name != "minecraft:water" && name != "minecraft:flowing_water" && name != "minecraft:lava"
                && name != "minecraft:flowing_lava";
        }, false);
    if (hit.mType == HitResultType::Tile) {
        pick = {HitResultType::Tile, hit.mBlock, nullptr};
        best = distance(hit.mPos);
    }
    AABB area{Vec3{std::min(from.x, to.x) - 1, std::min(from.y, to.y) - 1, std::min(from.z, to.z) - 1},
              Vec3{std::max(from.x, to.x) + 1, std::max(from.y, to.y) + 1, std::max(from.z, to.z) + 1}};
    for (Actor* actor : source.fetchEntities(&player, area, true, false)) {
        if (!actor || actor->mRemoved) continue;
        auto result = actor->getAABB().clip(from, to);
        if (!result.mIsHit) continue;
        double d = distance(result.mPos);
        if (d < best) { best = d; pick = {HitResultType::Entity, {}, actor}; }
    }
    return pick;
}
// Some eggs' actor id differs from the entity's own identifier, so the name
// convention cannot find them. Index every spawn-egg item by its actor id once
// per session; only strings are kept.
std::unordered_map<std::string, std::string> const& spawnEggIndex(ItemRegistryRef const& registry) {
    static std::mutex mutex;
    static std::unordered_map<std::string, std::string> index;
    static bool built = false;
    std::scoped_lock lock(mutex);
    if (built || !registry.isRegistryInitialized()) return index;
    auto const& items = registry.getNameToItemMap();
    if (items.empty()) return index;
    for (auto const& [name, weak] : items) {
        auto* item = weak.get();
        if (!item || !item->isActorPlacerItem()) continue;
        auto const* placer = static_cast<ActorPlacerItem const*>(item);
        std::string const& actor = placer->mActorID->mFullName.get();
        if (!actor.empty()) index.try_emplace(actor, name.getString());
    }
    built = true;
    return index;
}
// Spawn-egg icons come from the item registry: the exact name and the few
// renamed entities first, then the registry audit for anything else.
TargetIcon entityIcon(IClientInstance& client, std::string const& identifier) {
    if (identifier.empty()) return {};
    try {
        auto registry = client.getItemRegistry();
        for (auto const& candidate : spawnEggCandidates(identifier))
            if (registry.getItem(HashedString(candidate))) return {IconKind::Item, candidate, 0};
        auto const& index = spawnEggIndex(registry);
        if (auto found = index.find(identifier); found != index.end())
            return {IconKind::Item, found->second, 0};
        // Projectiles and carried blocks have no egg, but an item of the same
        // subject often shares a usable id (snowball, arrow, painting).
        for (auto const& candidate : entityItemCandidates(identifier))
            if (registry.getItem(HashedString(candidate))) return {IconKind::Item, candidate, 0};
    } catch (...) {} // Icon resolution must not replace the target snapshot.
    return {};
}
// Blocks without an item (portal, fire, ...) draw their own texture; the uv
// keeps one animation frame instead of the whole vertical strip.
TargetIcon blockIcon(BlockSource& source, BlockPos const& pos, Block const& block) {
    std::string pick;
    short aux = 0;
    try {
        auto item = block.asItemInstance(source, pos, true);
        if (!item.isNull() && item.mItem) {
            pick = item.mItem->mFullName->getString();
            aux = item.getAuxValue();
        }
    } catch (...) {}
    std::string texture;
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
    if (pick.empty()) {
        try {
            auto const* graphics = BlockGraphics::getForBlock(block);
            if (graphics) {
                auto const& uv = graphics->getTexture(graphics->mIconTextureIndex, 0);
                // The ui loader wants the pack-relative path; getFullPath()
                // would prepend the file system the uv set was stored in.
                Core::PathBuffer<std::string> const& rawPath = uv.sourceFileLocation.get().mPath;
                texture = rawPath.value;
                if (texture.empty()) return {};
                // The uv set carries its own end coordinates; the size ratio is
                // only a stand-in for sets that leave them unset.
                u0 = uv._u0;
                v0 = uv._v0;
                if (uv._u1 > u0 && uv._v1 > v0) {
                    u1 = uv._u1;
                    v1 = uv._v1;
                } else if (uv._sourceImageWidth > 0 && uv._sourceImageHeight > 0 && uv._texSizeW > 0
                           && uv._texSizeH > 0) {
                    u1 = std::min(1.f, u0 + static_cast<float>(uv._texSizeW) / uv._sourceImageWidth);
                    v1 = std::min(1.f, v0 + static_cast<float>(uv._texSizeH) / uv._sourceImageHeight);
                }
#ifdef LAMIUM_RESEARCH_TRACE
                // L-58: one line per block kind proves what the fallback picked.
                static std::string logged;
                if (logged != texture) {
                    logged = texture;
                    auto& location = uv.sourceFileLocation.get();
                    ResourceFileSystem fileSystem = location.mFileSystem;
                    Runtime::instance().self().getLogger().info(
                        "L-58 texture {} -> {} fs={} tex={}x{} source={}x{} raw_uv={:.4f},{:.4f}-{:.4f},{:.4f}"
                        " uv={:.4f},{:.4f}-{:.4f},{:.4f}",
                        block.getTypeName(), texture, static_cast<int>(fileSystem), uv._texSizeW, uv._texSizeH,
                        uv._sourceImageWidth, uv._sourceImageHeight, uv._u0, uv._v0, uv._u1, uv._v1, u0, v0, u1, v1);
                }
#endif
            }
        } catch (...) { return {}; }
    }
    auto icon = chooseBlockIcon(std::move(pick), aux, std::move(texture));
    icon.u0 = u0;
    icon.v0 = v0;
    icon.u1 = u1;
    icon.v1 = v1;
    return icon;
}
// A dropped stack shows the item it holds; its own identifier is
// "minecraft:item", which names no item of its own.
TargetIcon droppedIcon(Actor& entity) {
    try {
        if (!entity.hasType(ActorType::ItemEntity)) return {};
        auto& stack = static_cast<ItemActor&>(entity).item();
        if (!stack.mItem) return {};
        return {IconKind::Item, stack.mItem->mFullName->getString(), stack.getAuxValue()};
    } catch (...) { return {}; }
}
}
std::optional<TargetInfo> collectTargetInfo(IClientInstance& client, bool includeStates, std::optional<ViewRay> ray) {
    auto* player = client.getLocalPlayer();
    if (!player) return {};
    Pick pick;
    if (ray) pick = pickAlong(*player, *ray);
    else {
        auto const& latest = client.getLatestHitResult();
        pick.type = latest.mType;
        pick.block = latest.mBlock;
        if (latest.mType == HitResultType::Entity) pick.entity = latest.getEntity();
    }
    struct { HitResultType mType; BlockPos mBlock; } hit{pick.type, pick.block};
    if (hit.mType == HitResultType::Entity) {
        auto* entity = pick.entity;
        if (!entity || entity == player || entity->mRemoved
            || entity->getDimensionId() != player->getDimensionId()) return {};
        // Respect the game's filtered name, then use its localized entity type.
        // The weak hit reference is resolved only for this snapshot.
        TargetInfo result{entity->getFilteredNameTag(),entity->getTypeName()};
        result.icon = droppedIcon(*entity);
        if (result.icon.kind == IconKind::None) result.icon = entityIcon(client, result.identifier);
        if (result.name.empty()) {
            auto key = entity->getEntityLocNameString();
            result.name = getI18n().get(key,getI18n().getCurrentLanguage());
            if (result.name.empty() || result.name == key) result.name = result.identifier;
        }
        // Mob details need no hook beyond the hit reference already held.
        if (includeStates && entity->hasType(ActorType::Mob)) {
            int health = entity->getHealth(), maxHealth = entity->getMaxHealth();
            if (maxHealth > 0 && health >= 0) {
                float progress = std::clamp(static_cast<float>(health) / maxHealth, 0.f, 1.f);
                result.details.push_back({"target.health",
                    std::to_string(health) + " / " + std::to_string(maxHealth), false, progress, DetailKind::Health});
            }
            int armor = static_cast<Mob*>(entity)->getArmorValue();
            if (armor > 0)
                result.details.push_back({"target.armor", std::to_string(armor), false,
                                          std::clamp(static_cast<float>(armor) / 20.f, 0.f, 1.f), DetailKind::Armor});
            result.details.push_back({"target.age", entity->isBaby() ? "target.baby" : "target.adult", true, {}});
            if (entity->isTame()) {
                std::string owner = "target.yes";
                bool ownerIsKey = true;
                if (auto* ownerMob = entity->getOwner()) {
                    auto name = ownerMob->getFilteredNameTag();
                    if (!name.empty()) { owner = std::move(name); ownerIsKey = false; }
                }
                result.details.push_back({"target.tamed", std::move(owner), ownerIsKey, {}});
            }
        }
        return result;
    }
    if (hit.mType != HitResultType::Tile) return {};
    auto const& range = player->getDimension().mHeightRange;
    if (hit.mBlock.y < range->mMin || hit.mBlock.y >= range->mMax) return {};
    auto& source = player->getDimensionBlockSource();
    if (!source.getChunkAt(hit.mBlock)) return {};
    auto const& block = source.getBlock(hit.mBlock);
    if (block.isAir()) return {};
    TargetInfo result{block.buildDescriptionName(),block.getTypeName()};
    result.blockPosition = TargetInfo::BlockPosition{hit.mBlock.x,hit.mBlock.y,hit.mBlock.z};
    // The pick-block item (seeds for crops, the block item otherwise).
    result.icon = blockIcon(source, hit.mBlock, block);
    if (result.name.empty()) result.name = result.identifier;
    if (includeStates) {
        auto const& tags = block.mSerializationId->mTags;
        auto found = tags.find("states");
        if (found != tags.end()) {
            if (auto* states = std::get_if<CompoundTag>(&found->second.mTagStorage)) {
                for (auto const& [key,value] : states->mTags) {
                    std::optional<TargetInfo::DetailRow> detail;
                    if (auto* v = std::get_if<ByteTag>(&value.mTagStorage))
                        detail = interpretBlockState(key, StateKind::Integer, v->data, {}, result.identifier);
                    else if (auto* v = std::get_if<IntTag>(&value.mTagStorage))
                        detail = interpretBlockState(key, StateKind::Integer, v->data, {}, result.identifier);
                    else if (auto* v = std::get_if<StringTag>(&value.mTagStorage))
                        detail = interpretBlockState(key, StateKind::Text, 0, *v, result.identifier);
                    if (detail) {
                        result.details.push_back(std::move(*detail));
                        continue;
                    }
                    std::optional<std::string> text;
                    if (auto* v = std::get_if<ByteTag>(&value.mTagStorage)) text = std::to_string(v->data);
                    else if (auto* v = std::get_if<IntTag>(&value.mTagStorage)) text = std::to_string(v->data);
                    else if (auto* v = std::get_if<StringTag>(&value.mTagStorage)) text = *v;
                    if (text) result.states.push_back(key + ": " + *text);
                }
            }
        }
    }
    return result;
}
}
