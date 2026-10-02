#include "features/information/Saturation.h"
#include "settings/SettingsStore.h"
#include <array>
#include <limits>
void check(bool, char const*);
void saturationTests() {
    using namespace lamium::information::saturation;
    check(mark(0, 13.4f) == Mark::Full && mark(5, 13.4f) == Mark::Full && mark(6, 13.4f) == Mark::Half
              && mark(7, 13.4f) == Mark::None,
          "13.4 saturation marks six and a half icons from the right");
    check(mark(0, 0.9f) == Mark::None && mark(0, 1) == Mark::Half && mark(9, 20) == Mark::Full && mark(9, 99) == Mark::Full,
          "fractions are dropped and values clamp to 20");
    check(mark(0, -1) == Mark::None && mark(0, std::numeric_limits<float>::quiet_NaN()) == Mark::None,
          "no marks for unusable values");

    auto bread = afterEating({14, 0}, 5, .6f);
    check(bread.hunger == 19 && std::abs(bread.saturation - 6) < 1e-4f, "bread: 14/0 -> 19/6 as seen in game");
    auto carrot = afterEating({17, 12.4f}, 6, 1.2f);
    check(carrot.hunger == 20 && carrot.saturation == 20, "a golden carrot caps saturation at the new hunger");
    auto full = afterEating({20, 3}, 4, .3f);
    check(full.hunger == 20 && std::abs(full.saturation - 5.4f) < 1e-4f, "hunger stops at 20, saturation still grows");

    // A 3x3 plus sign: every opaque pixel touches transparency except the center.
    std::array<std::uint8_t, 36> plus{};
    for (int i : {1, 3, 4, 5, 7}) plus[i * 4 + 3] = 255;
    auto edge = outline(plus.data(), 3, 3, gold);
    check(edge.size() == 9 && edge[1] == gold && edge[3] == gold && edge[4] == 0 && edge[0] == 0,
          "the outline keeps opaque pixels next to transparency");
    std::array<std::uint8_t, 36> solid{};
    for (int i = 0; i < 9; ++i) solid[i * 4 + 3] = 255;
    auto block = outline(solid.data(), 3, 3, gold);
    check(block[4] == 0 && block[0] == gold && block[8] == gold, "the image edge counts as outside");
    auto right = outline(solid.data(), 3, 3, gold, Part::Right);
    check(right[0] == 0 && right[3] == 0 && right[1] == gold && right[2] == gold, "the half outline keeps the right half");
    auto left = outline(solid.data(), 3, 3, paleGold, Part::Left);
    check(left[0] == paleGold && left[6] == paleGold && left[1] == 0 && left[2] == 0, "a completing gain keeps the left half");
    check(outline(nullptr, 3, 3, gold).empty(), "no image, no outline");

    auto breadIcons = foodIcons(5, .6f); // +5 hunger, +6 saturation
    check(breadIcons.size() == 3 && breadIcons[0].hunger == Mark::Full && breadIcons[2].hunger == Mark::Half
              && breadIcons[2].saturation == Mark::Full, "bread: two and a half drumsticks, three outlined");
    auto carrotIcons = foodIcons(6, 1.2f); // +6 hunger, +14.4 saturation
    check(carrotIcons.size() == 7 && carrotIcons[3].hunger == Mark::None && carrotIcons[6].saturation == Mark::Full,
          "saturation beyond the hunger gain adds outlined empty icons");
    auto melon = foodIcons(2, .3f); // +2 hunger, +1.2 saturation
    check(melon.size() == 1 && melon[0].hunger == Mark::Full && melon[0].saturation == Mark::Half,
          "a small saturation gain marks half an icon");
    check(foodIcons(0, 1).empty() && foodIcons(4, std::numeric_limits<float>::quiet_NaN()).empty(), "no values, no icons");

    lamium::Settings defaults;
    check(defaults.information.saturation && defaults.information.saturationPreview && defaults.inspection.foodValues,
          "saturation and food values start on");
    auto loaded = lamium::decodeSettings(R"({"information":{"saturation":false,"saturationPreview":false}})");
    check(!loaded.information.saturation && !loaded.information.saturationPreview, "saturation options load");
}
