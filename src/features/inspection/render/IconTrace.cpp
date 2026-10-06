#include "features/inspection/render/IconTrace.h"
#ifdef LAMIUM_ICON_TRACE
#include "app/Runtime.h"
#include "app/TraceLog.h"
#include "ll/api/memory/Hook.h"
#include "mc/client/gui/controls/RenderableComponent.h"
#include "mc/client/gui/controls/renderers/InventoryItemRenderer.h"
#include "mc/client/renderer/RenderMaterialGroup.h"
#include "mc/deps/minecraft_renderer/renderer/MaterialPtr.h"
#include "mc/client/renderer/actor/ItemRenderer.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/deps/core/resource/ResourceLocation.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>

namespace lamium::inspection::iconTrace {
namespace {
// Only the icons in question, so a full inventory does not flood the log.
bool watched(ItemStack const& item) {
    if (item.isNull() || !item.mItem) return false;
    auto name = item.getTypeName();
    return name.find("leather") != std::string::npos || name.find("shield") != std::string::npos
        || item.mItem->isGlint(item);
}
std::mutex seenMutex;
std::set<std::string> seen;
// Logs each distinct line once: the trace is about which routes exist, not how often.
bool firstTime(std::string const& key) {
    std::lock_guard lock{seenMutex};
    return seen.size() < 400 && seen.insert(key).second;
}
TraceBudget passBudget, chunkBudget, newBudget, blitBudget, typeBudget;
// The caller of the current icon blit; blits happen inside these calls on the render thread.
thread_local std::string current;
// Experiment (2026-10-06): one candidate fix per leather piece on Lamium's own
// calls; boots get the UI "Item" material that vanilla slots use.
thread_local bool forceMultiColor = false;
// Second round: the multi-color material keeps the undyeable layer but drew
// the dyed part white, so each piece now passes the colors differently.
enum class Colors { Same, BothDye, WhiteDye, DyeBlack, ClearDye };
thread_local Colors colorRule = Colors::Same;
std::shared_ptr<mce::RenderMaterialInfo>& info(mce::MaterialPtr& material) { return material.mRenderMaterialInfoPtr; }
std::optional<mce::MaterialPtr> uiItemMaterial;
std::string location(ResourceLocation const& value) {
    try { return const_cast<ResourceLocation&>(value).mPath->get(); } catch (...) { return "?"; }
}

LL_TYPE_INSTANCE_HOOK(SlotRenderHook, ll::memory::HookPriority::Normal, InventoryItemRenderer,
    &InventoryItemRenderer::$render, void, MinecraftUIRenderContext& context, IClientInstance& client, UIControl& owner,
    int pass) {
    ItemStack const& item = this->mItemInstance;
    bool watch = false;
    try { watch = watched(item); } catch (...) {}
    if (!watch) { origin(context, client, owner, pass); return; }
    std::string name = item.getTypeName();
    try {
        int passes = this->getNumRenderPasses();
        std::string materials;
        for (int p = 0; p < passes && p < 8; ++p)
            materials += std::format(" p{}=m{}[{} | {}]", p, static_cast<int>(this->getUIMaterialType(p)),
                location(this->getResourceLocation(0, p)), location(this->getResourceLocation(1, p)));
        auto key = std::format("slot {} pass={} of {} itemMaterial={} renderType={} texture={} enchanted={} color={}{}",
            name, pass, passes, static_cast<int>(this->mUIMaterialType), static_cast<int>(this->mItemRenderType),
            *this->mTextureName, this->mIsEnchanted, this->mCustomColor, materials);
        if (firstTime(key)) traceLog(passBudget, 200, "L-91 {}", key);
    } catch (...) {}
    auto previous = std::exchange(current, std::format("slot {} pass={}", name, pass));
    origin(context, client, owner, pass);
    current = std::move(previous);
}
LL_STATIC_HOOK(RenderTypeHook, ll::memory::HookPriority::Normal, &InventoryItemRenderer::getRenderTypeFromItem,
    ItemRenderChunkType, ItemStack const& item) {
    auto type = origin(item);
    try {
        if (watched(item)) {
            auto key = std::format("renderType {} = {}", item.getTypeName(), static_cast<int>(type));
            if (firstTime(key)) traceLog(typeBudget, 50, "L-91 {}", key);
        }
    } catch (...) {}
    return type;
}
LL_TYPE_INSTANCE_HOOK(ChunkHook, ll::memory::HookPriority::Normal, ItemRenderer, &ItemRenderer::renderGuiItemInChunk,
    void, BaseActorRenderContext& context, ItemRenderChunkType type, ItemStack const& item, float x, float y, float light,
    float alpha, float scale, int frame, bool animate, int zOrder, std::optional<TextureUVCoordinateSet> const& uv) {
    bool watch = false;
    try { watch = watched(item); } catch (...) {}
    if (!watch) { origin(context, type, item, x, y, light, alpha, scale, frame, animate, zOrder, uv); return; }
    std::string name = item.getTypeName();
    auto key = std::format("chunk {} type={} scale={:.2f} z={} uv={} from=[{}]", name, static_cast<int>(type), scale,
        zOrder, uv.has_value(), current);
    if (firstTime(key)) traceLog(chunkBudget, 200, "L-91 {}", key);
    auto previous = std::exchange(current, std::format("chunk {} type={}", name, static_cast<int>(type)));
    origin(context, type, item, x, y, light, alpha, scale, frame, animate, zOrder, uv);
    current = std::move(previous);
}
LL_TYPE_INSTANCE_HOOK(NewHook, ll::memory::HookPriority::Normal, ItemRenderer, &ItemRenderer::renderGuiItemNew, void,
    BaseActorRenderContext& context, ItemStack const& item, int frame, float x, float y, bool foil, float transparency,
    float light, float scale, int zOrder) {
    bool watch = false;
    try { watch = watched(item); } catch (...) {}
    if (!watch) { origin(context, item, frame, x, y, foil, transparency, light, scale, zOrder); return; }
    std::string name = item.getTypeName();
    auto key = std::format("new {} foil={} transparency={:.2f} scale={:.2f} z={}", name, foil, transparency, scale, zOrder);
    if (firstTime(key)) traceLog(newBudget, 100, "L-91 {}", key);
    auto previous = std::exchange(current, std::format("new {} foil={}", name, foil));
    std::shared_ptr<mce::RenderMaterialInfo> replacement;
    std::string variant = "none";
    try {
        forceMultiColor = !foil && name.starts_with("minecraft:leather_");
        if (!foil && name == "minecraft:leather_helmet") { colorRule = Colors::BothDye; variant = "multiColor color=dye secondary=dye"; }
        else if (!foil && name == "minecraft:leather_chestplate") { colorRule = Colors::WhiteDye; variant = "multiColor color=white secondary=dye"; }
        else if (!foil && name == "minecraft:leather_leggings") { colorRule = Colors::DyeBlack; variant = "multiColor color=dye secondary=black"; }
        else if (!foil && name == "minecraft:leather_boots") { colorRule = Colors::ClearDye; variant = "multiColor color=0 secondary=dye"; }
    } catch (...) { variant += " failed"; replacement.reset(); forceMultiColor = false; }
    if (variant != "none" && firstTime("variant " + name + variant)) traceLog(newBudget, 100, "L-91 experiment {} -> {}", name, variant);
    if (replacement) {
        auto savedIcon = info(this->mUIIconBlitMaterial), savedBlit = info(this->mUIBlitMaterial);
        info(this->mUIIconBlitMaterial) = replacement;
        info(this->mUIBlitMaterial) = replacement;
        origin(context, item, frame, x, y, foil, transparency, light, scale, zOrder);
        info(this->mUIIconBlitMaterial) = std::move(savedIcon);
        info(this->mUIBlitMaterial) = std::move(savedBlit);
    } else origin(context, item, frame, x, y, foil, transparency, light, scale, zOrder);
    forceMultiColor = false;
    colorRule = Colors::Same;
    current = std::move(previous);
}
LL_TYPE_INSTANCE_HOOK(BlitHook, ll::memory::HookPriority::Normal, ItemRenderer, &ItemRenderer::iconBlit, void,
    BaseActorRenderContext& context, mce::TexturePtr const& texture, float x, float y, float z,
    TextureUVCoordinateSet const& uv, float w, float h, float light, float alpha, int color, int secondaryColor,
    float xscale, float yscale, IconBlitGlint const glint, bool const multiColor) {
    if (!current.empty()) {
        try {
            auto key = std::format("blit from=[{}] glint={} multiColor={} color={:08x} secondary={:08x} uv=({:.4f},{:.4f})-({:.4f},{:.4f}) size={:.1f}x{:.1f} scale={:.2f}",
                current, static_cast<int>(glint), multiColor, static_cast<unsigned>(color),
                static_cast<unsigned>(secondaryColor), uv._u0, uv._v0, uv._u1, uv._v1, w, h, xscale);
            if (firstTime(key)) traceLog(blitBudget, 300, "L-91 {}", key);
        } catch (...) {}
    }
    int first = color, second = secondaryColor;
    switch (colorRule) {
    case Colors::BothDye: second = color; break;
    case Colors::WhiteDye: first = static_cast<int>(0xffffffffu); second = color; break;
    case Colors::DyeBlack: second = static_cast<int>(0xff000000u); break;
    case Colors::ClearDye: first = 0; second = color; break;
    default: break;
    }
    origin(context, texture, x, y, z, uv, w, h, light, alpha, first, second, xscale, yscale, glint,
        multiColor || forceMultiColor);
}
bool hooked = false;
}
void start() {
    if (SlotRenderHook::hook(true) != 0 || RenderTypeHook::hook(true) != 0 || ChunkHook::hook(true) != 0
        || NewHook::hook(true) != 0 || BlitHook::hook(true) != 0) {
        stop();
        throw std::runtime_error("Could not install L-91 icon diagnostics");
    }
    hooked = true;
    Runtime::instance().self().getLogger().warn("L-91 icon diagnostics enabled");
}
void stop() {
    SlotRenderHook::unhook(true); RenderTypeHook::unhook(true); ChunkHook::unhook(true);
    NewHook::unhook(true); BlitHook::unhook(true);
    hooked = false;
}
}
#else
namespace lamium::inspection::iconTrace { void start() {} void stop() {} }
#endif
