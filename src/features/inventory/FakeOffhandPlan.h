#pragma once
#include <optional>

namespace lamium::inventory::fakeOffhand {
class SelectionSession {
    std::optional<int> original;
    int target = -1;
    bool interrupted = false;
public:
    bool active() const { return original.has_value(); }
    bool blocked() const { return interrupted; }
    bool owns(int selected) const { return active() && selected == target; }
    void begin(int selected, int selectedTarget) { original = selected; target = selectedTarget; }
    void abandon() { original.reset(); target = -1; interrupted = true; }
    std::optional<int> finish(int selected) {
        auto restore = owns(selected) ? original : std::nullopt;
        original.reset(); target = -1; interrupted = false;
        return restore;
    }
};
inline std::optional<int> placementSlot(bool enabled, bool triggered, int selected, int target,
    bool hasBlockItem, bool hitBlock, bool interactive, bool sneaking) {
    if (!enabled || !triggered || selected < 0 || selected >= 9 || target < 0 || target >= 9
        || selected == target || !hasBlockItem || !hitBlock || (interactive && !sneaking)) return {};
    return target;
}
}
