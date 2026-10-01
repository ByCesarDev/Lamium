#include "ui/SearchQuery.h"
#include <string>
void check(bool, char const*);
void searchQueryTests() {
    lamium::ui::SearchQuery query;
    check(query.matches("anything"), "empty search includes all settings");
    check(query.append("  ZOOM  wheel "), "append search text");
    check(query.matches("camera.wheelStep zoom Wheel step"), "case-insensitive token matching");
    check(!query.matches("zoom Magnification"), "all search words must match");
    query.clear();
    check(query.append("空のシュルカー"), "Japanese input retained");
    check(query.matches("空のシュルカーを表示: {}"), "Japanese label search");
    check(query.backspace() && query.value() == "空のシュルカ", "backspace removes a UTF-8 codepoint");
    query.clear();
    check(!query.backspace(), "backspace empty search is safe");
    check(!query.append("\n") && !query.append("\b"), "control characters do not enter query");
    check(query.append(std::string(127, 'a')), "search length below limit");
    check(!query.append("あ") && query.value().size() == 127, "limit never splits UTF-8 event");
    query.selectAll();
    check(!query.append("\n") && query.selectedAll() && query.value().size() == 127,
        "rejected input preserves selected search");
    check(query.append("ズーム") && query.value() == "ズーム" && !query.selectedAll(),
        "selected long query can be replaced with UTF-8 text");
    query.selectAll();
    check(query.backspace() && query.value().empty() && !query.selectedAll(),
        "backspace clears the selected query");
    query.selectAll(); query.clear();
    check(query.append("zoom") && query.append(" wheel") && query.value() == "zoom wheel",
        "clear resets replace mode for subsequent typing");
    // An IME rewriting "n" -> "ね" -> "ねz" -> "ねざ" -> "ねざー", then
    // converting to "ネザー": each step takes characters back with 0x08.
    lamium::ui::SearchQuery name;
    for (auto event : {"ｎ", "\bね", "ｚ", "\bざ", "ー", "\b\b\bネザー"}) name.type(event);
    check(name.value() == "ネザー", "composition updates replace what they took back");
    name.selectAll();
    check(name.type("ｎ") && name.value() == "ｎ" && name.type("\bね") && name.value() == "ね",
        "a composition replaces a selected name and keeps rewriting it");
    check(!name.type("\n") && name.value() == "ね", "other control characters are dropped");
    check(name.type("x\by") && name.value() == "ねy", "a backspace inside one event applies in order");
}
