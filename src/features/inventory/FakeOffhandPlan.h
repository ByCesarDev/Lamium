#pragma once
#include <optional>
#include <string_view>

namespace lamium::inventory::fakeOffhand {
inline bool instantItem(std::string_view name) {
    return name == "minecraft:bucket" || name == "minecraft:water_bucket"
        || name == "minecraft:snowball" || name == "minecraft:egg";
}
inline std::optional<int> instantUseSlot(bool enabled, bool triggered, int selected, int target,
    bool primaryIdle, bool targetInstant, bool entityTarget,
    bool interactive, bool sneaking) {
    if (!enabled || !triggered || selected < 0 || selected >= 9 || target < 0 || target >= 9
        || selected == target || !primaryIdle || !targetInstant || entityTarget
        || (interactive && !sneaking)) return {};
    return target;
}
inline std::optional<int> placementSlot(bool enabled, bool triggered, int selected, int target,
    bool hasBlockItem, bool hitBlock, bool interactive, bool sneaking) {
    if (!enabled || !triggered || selected < 0 || selected >= 9 || target < 0 || target >= 9
        || selected == target || !hasBlockItem || !hitBlock || (interactive && !sneaking)) return {};
    return target;
}
}
