#include "features/inspection/render/ItemIcon.h"
#include "features/inspection/render/IconTint.h"
#include "app/Runtime.h"
#include "ll/api/memory/Hook.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/actor/ItemRenderer.h"
#include "mc/world/item/ItemStack.h"
#include <atomic>

namespace lamium::inspection::render {
namespace {
std::atomic<bool> hooked{false};
// Set only around Lamium's own calls on the render thread, so vanilla icons are untouched.
thread_local TintPass pass = TintPass::None;

LL_TYPE_INSTANCE_HOOK(TintBlitHook, ll::memory::HookPriority::Normal, ItemRenderer, &ItemRenderer::iconBlit, void,
    BaseActorRenderContext& context, mce::TexturePtr const& texture, float x, float y, float z,
    TextureUVCoordinateSet const& uv, float w, float h, float light, float alpha, int color, int secondaryColor,
    float xscale, float yscale, IconBlitGlint const glint, bool const multiColor) {
    auto colors = tintPass(pass, color, secondaryColor, multiColor);
    origin(context, texture, x, y, z, uv, w, h, light, alpha, colors.color, colors.secondary, xscale, yscale, glint,
        colors.multiColor);
}
}
void renderItemIcon(ItemRenderer& renderer, BaseActorRenderContext& context, ItemStack const& stack, int frame,
                    float x, float y, float scale, int zOrder) {
    bool layered = false;
    try { layered = hooked && layeredDyeIcon(stack.getTypeName()); } catch (...) {}
    if (!layered) {
        renderer.renderGuiItemNew(context, stack, frame, x, y, false, 1.f, 1.f, scale, zOrder);
        return;
    }
    pass = TintPass::Base;
    renderer.renderGuiItemNew(context, stack, frame, x, y, false, 1.f, 1.f, scale, zOrder);
    pass = TintPass::Dye;
    renderer.renderGuiItemNew(context, stack, frame, x, y, false, 1.f, 1.f, scale, zOrder);
    pass = TintPass::None;
}
void startIconTint() {
    // Fail open: leather keeps the old single-pass look if the hook cannot be installed.
    if (TintBlitHook::hook(true) == 0) hooked = true;
    else Runtime::instance().self().getLogger().warn("Leather icon layers unavailable: icon hook failed");
}
void stopIconTint() {
    if (hooked.exchange(false)) TintBlitHook::unhook(true);
}
}
