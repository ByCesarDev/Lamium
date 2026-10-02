// L-92 research: can readouts live in the vanilla item tooltip? Appends test
// lines to every item's hover text: whether every item passes through
// ItemStackBase::getFormattedHovertext, whether color codes tint the food
// glyph (U+E100 ":shank:"), and how a half is best written.
#ifdef LAMIUM_TOOLTIP_TRACE
#include "app/Runtime.h"
#include "ll/api/memory/Hook.h"
#include "mc/safety/RedactableString.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStackBase.h"
#include <algorithm>
#include <format>
#include <string>

namespace lamium::inspection {
namespace {
constexpr char const* shank = "\xEE\x84\x80"; // U+E100
std::string repeat(int count) {
    std::string out;
    for (int i = 0; i < count; ++i) out += shank;
    return out;
}
}
LL_AUTO_TYPE_INSTANCE_HOOK(TooltipTrace, ll::memory::HookPriority::Normal, ItemStackBase,
    &ItemStackBase::getFormattedHovertext, Bedrock::Safety::RedactableString, Level& level, bool showCategory) {
    auto text = origin(level, showCategory);
    try {
        if (isNull() || !mItem) return text;
        std::string lines = "\n§7[probe] plain " + repeat(3) + " §6gold " + repeat(3) + " §eyellow "
            + repeat(2) + "§r half " + repeat(2) + "½";
        lines += "\n§7[probe] counts " + std::string(shank) + " 2.5  §6" + shank + " 6§r";
        if (isDamageableItem()) {
            int maximum = mItem->getMaxDamage();
            int remaining = maximum - std::clamp<int>(getDamageValue(), 0, maximum);
            lines += std::format("\n§7[probe] durability {} / {}", remaining, maximum);
        }
        text += lines;
        static int logged = 0;
        if (++logged <= 5)
            Runtime::instance().self().getLogger().info("Tooltip trace: appended to {}", getTypeName());
    } catch (...) {
    }
    return text;
}
}
#endif
