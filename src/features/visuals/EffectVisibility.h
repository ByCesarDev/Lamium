#pragma once
#include <array>
#include <string_view>

namespace lamium::visuals {
inline constexpr unsigned weatherBit = 1, particlesBit = 2;
inline constexpr unsigned effectMask(bool master, bool weather, bool particles) {
    return master ? (weather ? weatherBit : 0) | (particles ? particlesBit : 0) : 0;
}
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
