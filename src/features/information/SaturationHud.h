#pragma once
#include "features/information/Saturation.h"
#include "settings/Settings.h"
#include "ui/Widgets.h"
#include <vector>
class MinecraftUIRenderContext;
class ScreenView;
namespace lamium::information {
// Saturation as gold outlines on the vanilla hunger bar (L-63); `view` is the HUD view.
void drawSaturation(MinecraftUIRenderContext&, ScreenView const& view, Settings::Information const&);
// A food's hunger and saturation gain as hunger-bar icons, icons[i] into
// rects[i] (L-64; painted over the tooltip's food glyphs, L-92).
void drawFoodIcons(MinecraftUIRenderContext&, std::vector<saturation::FoodIcon> const& icons,
                   std::vector<ui::ImageRect> const& rects);
}
