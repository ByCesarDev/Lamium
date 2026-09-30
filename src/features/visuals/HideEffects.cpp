#include "features/visuals/HideEffects.h"
#include "features/visuals/EffectVisibility.h"
#include "app/Runtime.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/particle/ParticleEngine.h"
#include "mc/client/particlesystem/particle/ParticleRenderer.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/deps/minecraft_renderer/objects/ViewRenderObject.h"
#include <atomic>
#include <cmath>

namespace lamium::visuals::effects {
namespace {
std::atomic<unsigned> configured{0}, available{0};
constexpr unsigned weatherBit = 1, particlesBit = 2;
static_assert(static_cast<int>(WeatherRenderObject::PrecipitationType::Rain) == 0
    && static_cast<int>(WeatherRenderObject::PrecipitationType::Snow) == 1
    && static_cast<int>(WeatherRenderObject::PrecipitationType::Plankton) == 2
    && static_cast<int>(WeatherRenderObject::PrecipitationType::RedSpores) == 3
    && static_cast<int>(WeatherRenderObject::PrecipitationType::BlueSpores) == 4
    && static_cast<int>(WeatherRenderObject::PrecipitationType::Ash) == 5
    && static_cast<int>(WeatherRenderObject::PrecipitationType::WhiteAsh) == 6
    && static_cast<int>(WeatherRenderObject::PrecipitationType::Count) == 7);
bool weatherInstalled = false, legacyInstalled = false, dataInstalled = false;
unsigned active() noexcept {
    if (!Runtime::instance().enabled()) return 0;
    return configured.load() & available.load();
}
LL_STATIC_HOOK(LegacyParticleVisibility, ll::memory::HookPriority::Normal,
    &ParticleEngine::render, void, ScreenContext& context, ParticleLayerRenderObject const& particles) {
    if (active() & particlesBit) return;
    origin(context, particles);
}
LL_TYPE_INSTANCE_HOOK(DataParticleVisibility, ll::memory::HookPriority::Normal, ParticleRenderer,
    &ParticleRenderer::renderParticles, void, ScreenContext& context, Vec3 const& target,
    Vec3 const& camera, ParticleRenderData const& particles) {
    if (active() & particlesBit) return;
    origin(context, target, camera, particles);
}
LL_TYPE_INSTANCE_HOOK(WeatherVisibility, ll::memory::HookPriority::Normal, LevelRendererPlayer,
    &LevelRendererPlayer::createViewRenderObject, ViewRenderObject, ScreenContext& context, SubClientId id) {
    auto view = origin(context, id);
    try {
        unsigned mask = active();
        if (!mask || ll::service::getClientInstance() != &mClientInstance) return view;
        auto& weather = *view.mWeatherState;
        float* densities[] = {&weather.mDensityRain, &weather.mDensitySnow, &weather.mDensityPlankton,
            &weather.mDensityRedSpores, &weather.mDensityBlueSpores, &weather.mDensityAsh, &weather.mDensityWhiteAsh};
        auto hidden = hiddenWeatherLayers((mask & weatherBit) != 0, (mask & particlesBit) != 0);
        // Validate the whole snapshot before changing it. Never alter rain
        // updates: those also feed sound and splash emission.
        for (size_t i = 0; i < hidden.size(); ++i)
            if (!std::isfinite(*densities[i]) || *densities[i] < 0
                || !std::isfinite((*weather.mParams)[i].fAlpha)) return view;
        for (size_t i = 0; i < hidden.size(); ++i) {
            if (!hidden[i]) continue;
            *densities[i] = 0;
            (*weather.mParams)[i].fAlpha = 0;
        }
    } catch (...) {}
    return view;
}
}
void configure(Settings const& settings) {
    configured = (settings.visuals.hideWeather ? weatherBit : 0) | (settings.visuals.hideParticles ? particlesBit : 0);
}
void start() noexcept {
    try {
        if (!weatherInstalled) weatherInstalled = WeatherVisibility::hook(true) == 0;
        if (!legacyInstalled) legacyInstalled = LegacyParticleVisibility::hook(true) == 0;
        if (!dataInstalled) dataInstalled = DataParticleVisibility::hook(true) == 0;
        available = (weatherInstalled ? weatherBit : 0)
            | (weatherInstalled && legacyInstalled && dataInstalled ? particlesBit : 0);
        if (!weatherInstalled || !legacyInstalled || !dataInstalled)
            Runtime::instance().self().getLogger().warn("Some effect visibility hooks are unavailable; affected effects stay vanilla");
    } catch (...) { available = 0; }
}
void stop() {
    available = 0;
    if (dataInstalled && DataParticleVisibility::unhook(true)) dataInstalled = false;
    if (legacyInstalled && LegacyParticleVisibility::unhook(true)) legacyInstalled = false;
    if (weatherInstalled && WeatherVisibility::unhook(true)) weatherInstalled = false;
}
}
