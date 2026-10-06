#pragma once
#include <algorithm>

namespace lamium::ui {
// Line elements (Info, Status, Debug View): row height and the per-line
// background (L-98, docs/demos/hud-density.html). The background hugs each
// line's text with one unit at each side, and rows touch, as on Java's debug
// screen.
inline constexpr float lineGlyphHeight = 8; // Label text height at 100 %, GUI units.
inline constexpr float lineSidePadding = 1;
struct LineBox { float x, y, width, height; };
inline LineBox lineBox(float textLeft, float rowTop, float textWidth, float rowHeight, float zoom) {
    float pad = lineSidePadding * zoom;
    return {textLeft - pad, rowTop, std::max(0.0f, textWidth) + 2 * pad, rowHeight};
}
// The text's top in a row: centered when each line has its own background, so
// the band frames it evenly; at the row top otherwise (the original layout).
inline float lineTextTop(float rowTop, float rowHeight, float zoom, bool band) {
    return band ? rowTop + std::max(0.0f, (rowHeight - lineGlyphHeight * zoom) / 2) : rowTop;
}
}
