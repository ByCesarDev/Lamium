#pragma once
#include <optional>
#include <cmath>
#include <string_view>

namespace lamium::inventory::fakeOffhand {
enum class PrimaryUse { Idle, Dirt, GroundTool, Food, Unknown };
inline bool foodBlocked(bool alwaysEat, bool creative, std::optional<float> hunger, float maximum) {
    return !alwaysEat && !creative && hunger && std::isfinite(*hunger)
        && std::isfinite(maximum) && maximum > 0 && *hunger == maximum;
}
inline bool primaryPass(PrimaryUse use, bool air, bool cannotEat) {
    return use == PrimaryUse::Idle || (air && (use == PrimaryUse::Dirt || use == PrimaryUse::GroundTool))
        || (use == PrimaryUse::Food && cannotEat);
}
inline bool passivePrimaryItem(std::string_view name) {
    return name == "minecraft:totem_of_undying" || name == "minecraft:totem"
        || name == "minecraft:stick" || name == "minecraft:paper"
        || name == "minecraft:diamond" || name == "minecraft:emerald"
        || name == "minecraft:iron_ingot" || name == "minecraft:gold_ingot"
        || name == "minecraft:copper_ingot" || name == "minecraft:netherite_ingot"
        || name == "minecraft:coal" || name == "minecraft:charcoal";
}
inline bool instantItem(std::string_view name) {
    return name == "minecraft:bucket" || name == "minecraft:water_bucket"
        || name == "minecraft:snowball" || name == "minecraft:egg";
}
inline bool ownsInstantHold(int primary, int target, int selected, int configured, bool eligible) {
    return eligible && primary >= 0 && primary < 9 && target >= 0 && target < 9
        && primary != target && selected == primary && configured == target;
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
