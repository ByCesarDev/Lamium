#pragma once
#include <cstdint>
#include <string_view>

namespace lamium::inspection::render {
// L-91: Lamium's direct icon calls tint a dyeable leather icon whole, losing
// its undyeable layer. The game's multi-color icon material tints only the
// dyeable pixels, using the secondary color, and leaves the undyeable ones
// transparent (trace and experiments, 2026-10-06). So these icons are drawn
// twice: untinted with the plain material, then the dye on top.
inline bool layeredDyeIcon(std::string_view typeName) { return typeName.starts_with("minecraft:leather_"); }

enum class TintPass { None, Base, Dye };
struct BlitColors {
    int color;
    int secondary;
    bool multiColor;
};
inline BlitColors tintPass(TintPass pass, int color, int secondary, bool multiColor) {
    switch (pass) {
    case TintPass::Base: return {static_cast<int>(0xffffffffu), secondary, false};
    case TintPass::Dye: return {color, color, true};
    default: return {color, secondary, multiColor};
    }
}
}
