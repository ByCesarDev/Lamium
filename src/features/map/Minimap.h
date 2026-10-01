#pragma once
#include "features/map/MapCave.h"
#include "settings/Settings.h"
#include "ui/HudEditorLayout.h"
#include <optional>
class MinecraftUIRenderContext;
namespace lamium::map {
// Minimap HUD element (BACKLOG L-60). Called once per frame from the HUD
// draw, on the client thread; scans, composes and uploads within a budget.
std::optional<ui::hud_editor::Box> drawMinimap(MinecraftUIRenderContext&, float width, float height,
                                               ui::HudElement const&, Settings::Map const&, bool preview);
// The cave/surface key: forces the view not shown, or returns to automatic.
ViewForce pressViewKey();
// The hold-to-enlarge key: twice the size and twice the area at the same scale.
void setEnlarged(bool held);
void start();
void stop();
}
