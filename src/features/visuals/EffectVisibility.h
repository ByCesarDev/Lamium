#pragma once
#include <array>
#include <string_view>

namespace lamium::visuals {
inline constexpr unsigned weatherBit = 1, particlesBit = 2, bossBarsBit = 4;
inline constexpr unsigned effectMask(bool master, bool weather, bool particles, bool bossBars = false) {
    return master ? (weather ? weatherBit : 0) | (particles ? particlesBit : 0) | (bossBars ? bossBarsBit : 0) : 0;
}
class BossBarRoute {
    bool healthPanel = false, hudPanel = false;
public:
    bool visit(std::string_view name) {
        healthPanel = healthPanel || name == "boss_health_panel";
        hudPanel = hudPanel || name == "boss_hud_panel";
        return name == "hud_screen" && healthPanel && hudPanel;
    }
};
inline constexpr bool hideParticle(unsigned mask, bool rainSplash) {
    return (mask & particlesBit) || (rainSplash && (mask & weatherBit));
}
inline constexpr bool rainEffectMatches(std::string_view learned, std::string_view candidate) {
    return !learned.empty() && learned.size() <= 192 && learned == candidate;
}
inline constexpr std::array<bool, 7> hiddenWeatherLayers(bool weather, bool particles) {
    return {weather, weather, particles, particles, particles, particles, particles};
}
}
