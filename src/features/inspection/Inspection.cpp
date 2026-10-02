#include "features/inspection/Inspection.h"
#include "features/inspection/hover/HoverTracker.h"
#include "features/inspection/preview/HoveredPreviewCache.h"
#include "features/inspection/render/PreviewRenderer.h"
#include "features/information/SaturationHud.h"
#include "app/Runtime.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/event/render/UIRenderEvent.h"
#include "ll/api/memory/Hook.h"
#include "mc/client/gui/screens/ScreenController.h"
#include "mc/safety/RedactableString.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStackBase.h"
#include "mc/world/item/ShulkerBoxBlockItem.h"
#include "ui/Localization.h"
#include <algorithm>

namespace lamium::inspection {
namespace {
preview::HoveredPreviewCache cache;
render::PreviewRenderer renderer;
ll::event::ListenerPtr renderListener, exitListener;
bool tooltipHookInstalled = false, durabilityHookInstalled = false;
LL_TYPE_INSTANCE_HOOK(ShulkerContentsText, ll::memory::HookPriority::Normal, ShulkerBoxBlockItem,
    &ShulkerBoxBlockItem::$appendFormattedHovertext, void, ItemStackBase const& stack,
    Level& level, Bedrock::Safety::RedactableString& hovertext, bool const showCategory) {
    auto& runtime = Runtime::instance();
    auto const preferences = runtime.preferences().inspection;
    if (runtime.enabled() && preferences.containerPreviews && preferences.shulkerPreviews
        && preferences.hideShulkerContents) {
        // Keep the generic item text (name/lore/etc.); only skip the Shulker
        // specialization which appends the contained-item list.
        Item::$appendFormattedHovertext(stack, level, hovertext, showCategory);
    } else origin(stack, level, hovertext, showCategory);
}
// Remaining durability as the last line of the game's own tooltip, in its
// gray, for every damageable item (L-92: replaces Lamium's separate box).
LL_TYPE_INSTANCE_HOOK(DurabilityHovertext, ll::memory::HookPriority::Normal, ItemStackBase,
    &ItemStackBase::getFormattedHovertext, Bedrock::Safety::RedactableString, Level& level, bool showCategory) {
    auto text = origin(level, showCategory);
    try {
        auto& runtime = Runtime::instance();
        if (!runtime.enabled() || !runtime.preferences().inspection.durability) return text;
        if (isNull() || !mItem || !isDamageableItem()) return text;
        int maximum = mItem->getMaxDamage();
        if (maximum <= 0) return text;
        int remaining = maximum - std::clamp<int>(getDamageValue(), 0, maximum);
        text += "\n\u00a77" + ui::translated("durabilityValue", remaining, maximum);
    } catch (...) {
    }
    return text;
}
}
bool start() {
    try {
        tooltipHookInstalled = ShulkerContentsText::hook(true) == 0;
        if (!tooltipHookInstalled) throw std::runtime_error("Could not install Shulker contents text hook");
        durabilityHookInstalled = DurabilityHovertext::hook(true) == 0;
        if (!durabilityHookInstalled) throw std::runtime_error("Could not install durability tooltip hook");
        hover::HoverTracker::getInstance().install();
        auto& bus = ll::event::EventBus::getInstance();
        renderListener = bus.emplaceListener<ll::event::AfterUIRenderEvent>([](auto& event) {
            auto settings = Runtime::instance().preferences();
            auto* controller = event.screenView().mController.get();
            if (!controller || !controller->_isContainerScreen()) return;
            if (settings.inspection.foodValues)
                if (auto* item = hover::HoverTracker::getInstance().resolveItem(*controller))
                    information::drawFoodValues(event.uiRenderContext(), event.screenView(), *item);
            if (!settings.inspection.containerPreviews) { cache.clear(); return; }
            auto const* contents = cache.resolve(*controller);
            if (!contents) return;
            bool const shulker = contents->family == preview::ContainerPreview::Family::Shulker;
            auto const& preferences = settings.inspection;
            if (!(shulker ? preferences.shulkerPreviews : preferences.bundlePreviews)) return;
            if (contents->filledSlotCount() == 0 && contents->skippedSlotCount == 0
                && !(shulker ? preferences.emptyShulkerPreviews : preferences.emptyBundlePreviews)) return;
            renderer.render(event.screenView(), event.uiRenderContext(), *contents);
        });
        exitListener = bus.emplaceListener<ll::event::ClientExitLevelEvent>([](auto&) { cache.clear(); });
        return true;
    } catch (std::exception const& error) {
        Runtime::instance().self().getLogger().error("Item inspection initialization failed: {}", error.what());
        stop();
        return false;
    }
}
void stop() {
    auto& bus = ll::event::EventBus::getInstance();
    for (auto* listener : {&renderListener, &exitListener}) {
        if (*listener) { bus.removeListener(*listener); listener->reset(); }
    }
    cache.clear();
    hover::HoverTracker::getInstance().uninstall();
    if (durabilityHookInstalled) {
        if (DurabilityHovertext::unhook(true)) durabilityHookInstalled = false;
        else Runtime::instance().self().getLogger().error("Could not remove durability tooltip hook");
    }
    if (tooltipHookInstalled) {
        if (ShulkerContentsText::unhook(true)) tooltipHookInstalled = false;
        else Runtime::instance().self().getLogger().error("Could not remove Shulker contents text hook");
    }
}
}
