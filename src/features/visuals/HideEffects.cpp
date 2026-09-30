#include "features/visuals/HideEffects.h"
#include "features/visuals/EffectVisibility.h"
#include "app/Runtime.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/gui/controls/SpriteComponent.h"
#include "mc/client/gui/controls/TextComponent.h"
#include "mc/client/gui/controls/UIControl.h"
#include "mc/client/particle/ParticleEngine.h"
#include "mc/client/particle/Particle.h"
#include "mc/client/particlesystem/particle/ParticleEmitterActual.h"
#include "mc/client/particlesystem/particle/ParticleRenderer.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/deps/minecraft_renderer/objects/ViewRenderObject.h"
#include <atomic>
#include <cmath>
#include <memory>
#include <string>

namespace lamium::visuals::effects {
namespace {
std::atomic<unsigned> configured{0}, available{0};
std::atomic<std::shared_ptr<std::string const>> rainEffectName;
static_assert(static_cast<int>(WeatherRenderObject::PrecipitationType::Rain) == 0
    && static_cast<int>(WeatherRenderObject::PrecipitationType::Snow) == 1
    && static_cast<int>(WeatherRenderObject::PrecipitationType::Plankton) == 2
    && static_cast<int>(WeatherRenderObject::PrecipitationType::RedSpores) == 3
    && static_cast<int>(WeatherRenderObject::PrecipitationType::BlueSpores) == 4
    && static_cast<int>(WeatherRenderObject::PrecipitationType::Ash) == 5
    && static_cast<int>(WeatherRenderObject::PrecipitationType::WhiteAsh) == 6
    && static_cast<int>(WeatherRenderObject::PrecipitationType::Count) == 7);
bool weatherInstalled = false, legacyInstalled = false, dataInstalled = false;
bool rainLegacyInstalled = false, rainMappingInstalled = false, rainDataInstalled = false;
bool bossSpriteInstalled = false, bossTextInstalled = false;
unsigned active() noexcept {
    if (!Runtime::instance().enabled()) return 0;
    return configured.load() & available.load();
}
bool bossControl(UIControl& owner) noexcept {
    try {
        BossBarRoute route;
        UIControl* control = &owner;
        std::shared_ptr<UIControl> parent;
        for (unsigned depth = 0; depth < 32 && control; ++depth) {
            std::string const& name = *control->mName;
            if (name.size() > 192) return false;
            if (route.visit(name)) return true;
            if (name == "hud_screen") return false;
            parent = control->mParent.lock();
            control = parent.get();
        }
    } catch (...) {}
    return false;
}
LL_TYPE_INSTANCE_HOOK(BossBarSpriteVisibility, ll::memory::HookPriority::Normal, SpriteComponent,
    &SpriteComponent::render, void, UIRenderContext& context) {
    if ((active() & bossBarsBit) && bossControl(mOwner)) return;
    origin(context);
}
LL_TYPE_INSTANCE_HOOK(BossBarTextVisibility, ll::memory::HookPriority::Normal, TextComponent,
    &TextComponent::$render, void, UIRenderContext& context) {
    if ((active() & bossBarsBit) && bossControl(mOwner)) return;
    origin(context);
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
LL_TYPE_INSTANCE_HOOK(RainParticleVisibility, ll::memory::HookPriority::Normal, Particle,
    &Particle::$tessellate, void, ParticleRenderContext const& context) {
    if (hideParticle(active(),mType == ParticleType::RainSplash)) return;
    origin(context);
}
LL_TYPE_INSTANCE_HOOK(RainEffectMapping, ll::memory::HookPriority::Normal, ParticleEngine,
    &ParticleEngine::_emitParticleNew, void, ParticleSystemEngine& engine, ParticleType type,
    Vec3 const& pos, Vec3 const& direction, int data) {
    if (type == ParticleType::RainSplash) {
        try {
            // Learn the current pack's rain identifier from the game's own
            // mapping, rather than guessing names or suppressing WaterSplash.
            auto found = mNewParticleSystemJsonLookup->find(type);
            auto previous = rainEffectName.load();
            bool sharedWithWater = false;
            if (found != mNewParticleSystemJsonLookup->end()) {
                for (auto other : {ParticleType::WaterSplash, ParticleType::WaterSplashManual, ParticleType::WaterWake}) {
                    auto water = mNewParticleSystemJsonLookup->find(other);
                    if (water != mNewParticleSystemJsonLookup->end()
                        && water->second.getString() == found->second.getString()) sharedWithWater = true;
                }
            }
            if (found == mNewParticleSystemJsonLookup->end() || found->second.empty()
                || found->second.getString().size() > 192 || sharedWithWater) rainEffectName.store(nullptr);
            else if (!previous || *previous != found->second.getString())
                rainEffectName.store(std::make_shared<std::string const>(found->second.getString()));
        } catch (...) { rainEffectName.store(nullptr); }
    }
    origin(engine,type,pos,direction,data);
}
LL_TYPE_INSTANCE_HOOK(RainEmitterVisibility, ll::memory::HookPriority::Normal, ParticleSystem::ParticleEmitterActual,
    &ParticleSystem::ParticleEmitterActual::$extractForRendering, void, ParticleRenderData& particles, float alpha) {
    if (active() & weatherBit) {
        try {
            auto rain = rainEffectName.load();
            if (rain && rainEffectMatches(*rain,mEffectName->getString())) return;
        } catch (...) {}
    }
    origin(particles,alpha);
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
    configured = effectMask(settings.visuals.hideEffects,settings.visuals.hideWeather,
        settings.visuals.hideParticles,settings.visuals.hideBossBars);
}
void start() noexcept {
    try {
        if (!weatherInstalled) weatherInstalled = WeatherVisibility::hook(true) == 0;
        if (!legacyInstalled) legacyInstalled = LegacyParticleVisibility::hook(true) == 0;
        if (!dataInstalled) dataInstalled = DataParticleVisibility::hook(true) == 0;
        if (!rainLegacyInstalled) rainLegacyInstalled = RainParticleVisibility::hook(true) == 0;
        if (!rainMappingInstalled) rainMappingInstalled = RainEffectMapping::hook(true) == 0;
        if (!rainDataInstalled) rainDataInstalled = RainEmitterVisibility::hook(true) == 0;
        if (!bossSpriteInstalled) bossSpriteInstalled = BossBarSpriteVisibility::hook(true) == 0;
        if (!bossTextInstalled) bossTextInstalled = BossBarTextVisibility::hook(true) == 0;
        bool weatherReady = weatherInstalled && rainLegacyInstalled && rainMappingInstalled && rainDataInstalled;
        available = (weatherReady ? weatherBit : 0)
            | (weatherInstalled && legacyInstalled && dataInstalled ? particlesBit : 0)
            | (bossSpriteInstalled && bossTextInstalled ? bossBarsBit : 0);
        if (!weatherReady || !legacyInstalled || !dataInstalled || !bossSpriteInstalled || !bossTextInstalled)
            Runtime::instance().self().getLogger().warn("Some effect visibility hooks are unavailable; affected effects stay vanilla");
    } catch (...) { available = 0; }
}
void stop() {
    available = 0;
    rainEffectName.store(nullptr);
    if (bossTextInstalled && BossBarTextVisibility::unhook(true)) bossTextInstalled = false;
    if (bossSpriteInstalled && BossBarSpriteVisibility::unhook(true)) bossSpriteInstalled = false;
    if (rainDataInstalled && RainEmitterVisibility::unhook(true)) rainDataInstalled = false;
    if (rainMappingInstalled && RainEffectMapping::unhook(true)) rainMappingInstalled = false;
    if (rainLegacyInstalled && RainParticleVisibility::unhook(true)) rainLegacyInstalled = false;
    if (dataInstalled && DataParticleVisibility::unhook(true)) dataInstalled = false;
    if (legacyInstalled && LegacyParticleVisibility::unhook(true)) legacyInstalled = false;
    if (weatherInstalled && WeatherVisibility::unhook(true)) weatherInstalled = false;
}
}
