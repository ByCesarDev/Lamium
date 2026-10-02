// L-92 research: paint the hunger-bar icons over plain food glyphs in the
// vanilla item tooltip. Food tooltips get a line of U+E100 glyphs; while the
// game's HoverTextRenderer draws, its text draw calls are captured and each
// glyph's box is computed with the same font. A gold frame is drawn on every
// computed box, and the numbers are logged on change, to see whether the
// boxes land on the glyphs.
#ifdef LAMIUM_TOOLTIP_TRACE
#include "app/Runtime.h"
#include "features/information/Saturation.h"
#include "ui/Widgets.h"
#include "ll/api/memory/Hook.h"
#include "mc/client/gui/CaretMeasureData.h"
#include "mc/client/gui/Font.h"
#include "mc/client/gui/TextAlignment.h"
#include "mc/client/gui/TextMeasureData.h"
#include "mc/client/gui/controls/renderers/HoverTextRenderer.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/deps/core/math/Color.h"
#include "mc/deps/input/RectangleArea.h"
#include "mc/safety/RedactableString.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStackBase.h"
#include "mc/world/item/components/IFoodItemComponent.h"
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace lamium::inspection {
namespace {
constexpr std::string_view shank = "\xEE\x84\x80"; // U+E100
struct Captured {
    Font* font;
    float x0, x1, y0, y1, size;
    int alignment;
    std::string text;
};
bool capturing = false;
void log(std::string const& line) {
    static std::string last;
    if (line == last) return;
    last = line;
    Runtime::instance().self().getLogger().info("Tooltip trace: {}", line);
}
}
LL_AUTO_TYPE_INSTANCE_HOOK(TooltipFoodGlyphs, ll::memory::HookPriority::Normal, ItemStackBase,
    &ItemStackBase::getFormattedHovertext, Bedrock::Safety::RedactableString, Level& level, bool showCategory) {
    auto text = origin(level, showCategory);
    try {
        if (isNull() || !mItem) return text;
        auto* food = mItem->getFood();
        if (!food) return text;
        auto icons = information::saturation::foodIcons(food->getNutrition(), food->getSaturationModifier());
        std::string line = "\n";
        for (size_t i = 0; i < icons.size(); ++i) line += shank;
        text += line;
    } catch (...) {
    }
    return text;
}
// Any text draw that carries the glyphs, whoever draws it.
void frameGlyphs(MinecraftUIRenderContext& context, Captured const& call) {
    auto glyph = call.text.find(shank);
    if (glyph == std::string::npos) return;
    size_t lineStart = call.text.rfind('\n', glyph);
    lineStart = lineStart == std::string::npos ? 0 : lineStart + 1;
    int lineIndex = 0, lines = 1;
    for (size_t i = 0; i < call.text.size(); ++i)
        if (call.text[i] == '\n') { if (i < glyph) ++lineIndex; ++lines; }
    float before = static_cast<float>(
        call.font->getLineLength(std::string_view(call.text).substr(lineStart, glyph - lineStart), call.size, false));
    float width = static_cast<float>(call.font->getLineLength(shank, call.size, false));
    float lineHeight = (call.y1 - call.y0) / lines;
    int count = 0;
    for (size_t at = glyph; at < call.text.size() && call.text.compare(at, shank.size(), shank) == 0; at += shank.size())
        ++count;
    for (int i = 0; i < count; ++i)
        ui::frame(context, call.x0 + before + i * width, call.y0 + lineIndex * lineHeight, width, lineHeight,
                  ui::Rgb{1.f, .78f, .2f}, 1.f);
    log(std::format("rect {:.1f},{:.1f}-{:.1f},{:.1f} size {:.2f} align {} lines {} line {} before {:.1f} glyph {:.1f} "
                    "count {} in renderer {}", call.x0, call.y0, call.x1, call.y1, call.size, call.alignment, lines,
                    lineIndex, before, width, count, capturing));
}
int renderCalls = 0;
LL_AUTO_TYPE_INSTANCE_HOOK(TooltipTextCapture, ll::memory::HookPriority::Normal, MinecraftUIRenderContext,
    &MinecraftUIRenderContext::$drawText, void, Font& font, RectangleArea const& rect, std::string&& text,
    mce::Color const& color, float alpha, ::ui::TextAlignment alignment, TextMeasureData const& textData,
    CaretMeasureData const& caretData) {
    bool glyphs = text.find(shank) != std::string::npos;
    Captured call;
    if (glyphs)
        try {
            call = {&font, rect._x0, rect._x1, rect._y0, rect._y1, textData.fontSize, static_cast<int>(alignment), text};
        } catch (...) {
            glyphs = false;
        }
    origin(font, rect, std::move(text), color, alpha, alignment, textData, caretData);
    if (glyphs)
        try {
            frameGlyphs(*this, call);
        } catch (...) {
        }
}
LL_AUTO_TYPE_INSTANCE_HOOK(TooltipRenderCapture, ll::memory::HookPriority::Normal, HoverTextRenderer,
    &HoverTextRenderer::$render, void, MinecraftUIRenderContext& context, IClientInstance& client, UIControl& owner,
    int pass) {
    if (++renderCalls % 300 == 1) log(std::format("HoverTextRenderer::render called {} times", renderCalls));
    capturing = true;
    origin(context, client, owner, pass);
    capturing = false;
}
}
#endif
