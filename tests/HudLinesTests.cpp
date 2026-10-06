#include "ui/HudLines.h"
void check(bool, char const*);
void hudLinesTests() {
    using namespace lamium::ui;
    auto box = lineBox(10, 20, 50, 9, 1);
    check(box.x == 9 && box.width == 52 && box.y == 20 && box.height == 9,
        "a line background is the text plus one unit each side, one row high");
    auto next = lineBox(10, 29, 30, 9, 1);
    check(next.y == box.y + box.height, "line backgrounds of consecutive rows touch");
    auto scaled = lineBox(10, 20, 50, 18, 2);
    check(scaled.x == 8 && scaled.width == 54, "the side padding follows the element scale");
    check(lineTextTop(20, 14, 1, false) == 20, "without a per-line background text keeps the row top");
    check(lineTextTop(20, 14, 1, true) == 23 && lineTextTop(20, 9, 1, true) == 20.5f,
        "with a per-line background text is centered in the band");
    check(lineTextTop(20, 6, 1, true) == 20, "a row lower than the text never moves it up");
}
