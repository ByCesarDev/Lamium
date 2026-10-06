#pragma once
class BaseActorRenderContext;
class ItemRenderer;
class ItemStack;

namespace lamium::inspection::render {
// Draws an item icon the way Lamium's HUDs and previews do (no glint pass),
// keeping dyed leather's undyeable layer (L-91, IconTint.h).
void renderItemIcon(ItemRenderer& renderer, BaseActorRenderContext& context, ItemStack const& stack, int frame,
                    float x, float y, float scale, int zOrder);
// Installs the icon blit hook the leather passes need; without it icons draw as before.
void startIconTint();
void stopIconTint();
}
