#pragma once
#include "settings/Settings.h"
class MinecraftUIRenderContext;
class ScreenView;
namespace lamium::information {
// Saturation as gold outlines on the vanilla hunger bar (L-63); `view` is the HUD view.
void drawSaturation(MinecraftUIRenderContext&, ScreenView const& view, Settings::Information const&);
}
