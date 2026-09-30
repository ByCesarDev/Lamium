#pragma once

class BaseActorRenderContext;
class ItemRenderer;
class ItemStack;

namespace lamium::inspection::render {
// Draws an item icon the way vanilla inventory slots do. `renderGuiItemNew`
// draws a single layer, so layered icons (leather armor's undyeable part)
// lose a layer; slots draw non-block items with the chunked item pass.
// Block items keep `renderGuiItemNew`, whose output already matched slots.
void drawItemIcon(ItemRenderer& renderer, BaseActorRenderContext& context, ItemStack const& stack, float x, float y,
                  float scale, int frame, int zOrder);
}
