#pragma once
#include "settings/Settings.h"
class MinecraftUIRenderContext;
class ScreenView;
class ItemStackBase;
namespace lamium::information {
// Saturation as gold outlines on the vanilla hunger bar (L-63); `view` is the HUD view.
void drawSaturation(MinecraftUIRenderContext&, ScreenView const& view, Settings::Information const&);
// A food's hunger and saturation gain beside the pointer in an inventory (L-64).
void drawFoodValues(MinecraftUIRenderContext&, ScreenView const& view, ItemStackBase const&);
}
