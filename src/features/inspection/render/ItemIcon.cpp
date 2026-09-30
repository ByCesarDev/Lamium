#include "features/inspection/render/ItemIcon.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/actor/ItemRenderer.h"
#include "mc/client/renderer/actor/ItemRenderChunkType.h"
#include "mc/client/renderer/texture/TextureUVCoordinateSet.h"
#include "mc/world/item/ItemStack.h"
#include <optional>

namespace lamium::inspection::render {
namespace {
// ItemRenderChunkType is opaque in SDK 26.51.5. Vanilla inventory slots were
// observed drawing leather armor, tools and flat items with value 2 on
// 1.26.51.01 (effects trace, 2026-09-30); blocks used another value, so they
// stay on the previous call.
constexpr auto itemIconPass = static_cast<ItemRenderChunkType>(2);
}
void drawItemIcon(ItemRenderer& renderer, BaseActorRenderContext& context, ItemStack const& stack, float x, float y,
                  float scale, int frame, int zOrder) {
    if (stack.mBlock) {
        renderer.renderGuiItemNew(context, stack, frame, x, y, false, 1.f, 1.f, scale, zOrder);
        return;
    }
    renderer.renderGuiItemInChunk(context, itemIconPass, stack, x, y, 1.f, 1.f, scale, frame, false, zOrder,
                                  std::nullopt);
}
}
