#include "features/visuals/EffectTrace.h"
#ifdef LAMIUM_EFFECTS_TRACE
#include "app/Runtime.h"
#include "input/Actions.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/gui/controls/SpriteComponent.h"
#include "mc/client/gui/controls/TextComponent.h"
#include "mc/client/gui/controls/UIControl.h"
#include "mc/client/gui/controls/renderers/MinecraftUICustomRenderer.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <unordered_set>

namespace lamium::visuals::effectTrace {
namespace {
std::mutex traceMutex;
std::unordered_set<std::string> routes;
unsigned targetLines = 0, otherLines = 0;
std::atomic<unsigned> inspections{0};
std::array<bool, 48> fogStates{};
bool spriteInstalled = false, textInstalled = false, customInstalled = false, fogInstalled = false;
bool observing() {
    auto client = ll::service::getClientInstance();
    return Runtime::instance().enabled() && client && client->getLocalPlayer() && gameplayScreen(client->getScreenName());
}
void route(char const* kind, UIControl& owner, std::string_view resource = {}) noexcept {
    try {
        if (!observing() || inspections.fetch_add(1) >= 300000) return;
        std::string path = owner.getPathedName();
        path.resize(std::min(path.size(), size_t{192}));
        std::string key = std::string(kind) + " " + path + " " + std::string(resource.substr(0,192));
        bool target = false;
        for (auto word : {"boss", "overlay", "camera", "vignette", "pumpkin", "spyglass", "frost", "powder", "nausea"})
            target = target || key.find(word) != std::string::npos;
        std::lock_guard lock{traceMutex};
        auto& lines = target ? targetLines : otherLines;
        if (lines >= (target ? 64u : 16u) || !routes.insert(key).second) return;
        ++lines;
        Runtime::instance().self().getLogger().info("research L-42 UI {}", key);
    } catch (...) {}
}
LL_TYPE_INSTANCE_HOOK(EffectSpriteTrace, ll::memory::HookPriority::Normal, SpriteComponent,
    &SpriteComponent::render, void, UIRenderContext& context) {
    try {
        ResourceLocation const& resource = mResourceLocation;
        Core::PathBuffer<std::string> const& path = resource.mPath;
        route("sprite", mOwner, path.value);
    } catch (...) {}
    origin(context);
}
LL_TYPE_INSTANCE_HOOK(EffectTextTrace, ll::memory::HookPriority::Normal, TextComponent,
    &TextComponent::$render, void, UIRenderContext& context) {
    route("text", mOwner);
    origin(context);
}
LL_TYPE_INSTANCE_HOOK(EffectCustomTrace, ll::memory::HookPriority::Normal, MinecraftUICustomRenderer,
    &MinecraftUICustomRenderer::$render, void, UIRenderContext& context,
    IClientInstance& client, UIControl& owner, int pass) {
    if (ll::service::getClientInstance() == &client) route("custom", owner);
    origin(context, client, owner, pass);
}
LL_TYPE_INSTANCE_HOOK(EffectFogTrace, ll::memory::HookPriority::Normal, LevelRendererPlayer,
    &LevelRendererPlayer::$_getFogDistanceSettingType, FogDefinition::DistanceSettingType) {
    auto type = origin();
    try {
        int index = static_cast<int>(type);
        if (ll::service::getClientInstance() != &mClientInstance || !observing() || index < 0 || index >= 6) return type;
        unsigned medium = (mCameraUnderWater ? 1u : 0u) | (mCameraUnderLava ? 2u : 0u) | (mCameraUnderPowderSnow ? 4u : 0u);
        std::lock_guard lock{traceMutex};
        auto& seen = fogStates[medium * 6 + static_cast<unsigned>(index)];
        if (!seen) {
            seen = true;
            Runtime::instance().self().getLogger().info("research L-42 fog mediumBits={} distanceType={}", medium, index);
        }
    } catch (...) {}
    return type;
}
}
void start() noexcept {
    try {
        if (!spriteInstalled) spriteInstalled = EffectSpriteTrace::hook(true) == 0;
        if (!textInstalled) textInstalled = EffectTextTrace::hook(true) == 0;
        if (!customInstalled) customInstalled = EffectCustomTrace::hook(true) == 0;
        if (!fogInstalled) fogInstalled = EffectFogTrace::hook(true) == 0;
        Runtime::instance().self().getLogger().info("research L-42 hooks sprite={} text={} custom={} fog={}",
            spriteInstalled, textInstalled, customInstalled, fogInstalled);
    } catch (...) {}
}
void stop() {
    if (fogInstalled && EffectFogTrace::unhook(true)) fogInstalled = false;
    if (customInstalled && EffectCustomTrace::unhook(true)) customInstalled = false;
    if (textInstalled && EffectTextTrace::unhook(true)) textInstalled = false;
    if (spriteInstalled && EffectSpriteTrace::unhook(true)) spriteInstalled = false;
}
}
#else
namespace lamium::visuals::effectTrace {
void start() noexcept {}
void stop() {}
}
#endif
