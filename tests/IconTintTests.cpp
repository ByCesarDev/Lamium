#include "features/inspection/render/IconTint.h"
void check(bool, char const*);
void iconTintTests() {
    using namespace lamium::inspection::render;
    check(layeredDyeIcon("minecraft:leather_helmet") && layeredDyeIcon("minecraft:leather_boots")
        && !layeredDyeIcon("minecraft:diamond_helmet") && !layeredDyeIcon("minecraft:shield"),
        "only leather icons get the two tint passes");
    int dye = static_cast<int>(0xffa06540u), white = static_cast<int>(0xffffffffu);
    auto plain = tintPass(TintPass::None, dye, white, false);
    check(plain.color == dye && plain.secondary == white && !plain.multiColor, "outside Lamium's passes the blit is unchanged");
    auto base = tintPass(TintPass::Base, dye, white, false);
    check(base.color == white && !base.multiColor, "the base pass draws the icon untinted with the plain material");
    auto top = tintPass(TintPass::Dye, dye, white, false);
    check(top.multiColor && top.secondary == dye, "the dye pass uses the multi-color material, which tints with the secondary color");
}
