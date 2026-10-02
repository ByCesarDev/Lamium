#pragma once
#include <optional>
#include <string>
#include <string_view>

namespace lamium::inspection::tooltip {
// Food values inside the vanilla item tooltip (BACKLOG L-92). The tooltip
// text ends with a line of U+E100 food glyphs; the game draws the whole
// tooltip text in one font call, and Lamium paints the hunger-bar icons over
// the glyphs. Pure: the glyph line and where its glyphs sit in that text.
inline constexpr std::string_view glyph = "\xEE\x84\x80"; // U+E100, ":shank:"
// Vanilla glyph cell and line height in GUI units, measured on 2026-10-02.
inline constexpr float lineHeight = 10;

inline std::string glyphLine(int count) {
    std::string line;
    if (count <= 0) return line;
    line = "\n";
    for (int i = 0; i < count; ++i) line += glyph;
    return line;
}
// The glyphs start their own line, so they sit at the left edge of it.
struct GlyphRun {
    int line = 0; // Line index from the top of the text
    int count = 0;
};
// The last run of glyphs that fills the rest of the text: only Lamium's own
// line counts, never a glyph a name or lore happens to contain.
inline std::optional<GlyphRun> findGlyphRun(std::string_view text) {
    size_t end = text.size();
    int count = 0;
    while (end >= glyph.size() && text.substr(end - glyph.size(), glyph.size()) == glyph) {
        end -= glyph.size();
        ++count;
    }
    if (count == 0 || end == 0 || text[end - 1] != '\n') return std::nullopt;
    GlyphRun run;
    run.count = count;
    for (size_t i = 0; i < end; ++i)
        if (text[i] == '\n') ++run.line;
    return run;
}
}
