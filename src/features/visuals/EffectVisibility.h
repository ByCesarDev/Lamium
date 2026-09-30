#pragma once
#include <array>
#include <string_view>

namespace lamium::visuals {
inline constexpr unsigned weatherBit = 1, particlesBit = 2, bossBarsBit = 4, nauseaBit = 8,
    pumpkinBit = 16, spyglassBit = 32, waterBit = 64, lavaBit = 128, powderSnowBit = 256;
// Effects drawn as on-screen meshes during the gameplay screen render.
inline constexpr unsigned overlayBits = nauseaBit | pumpkinBit | spyglassBit | powderSnowBit;
inline constexpr unsigned mediumBits = waterBit | lavaBit | powderSnowBit;
struct EffectSelection {
    bool weather = false, particles = false, bossBars = false, nausea = false;
    bool pumpkin = false, spyglass = false, water = false, lava = false, powderSnow = false;
};
inline constexpr unsigned effectMask(bool master, EffectSelection const& s) {
    if (!master) return 0;
    return (s.weather ? weatherBit : 0) | (s.particles ? particlesBit : 0) | (s.bossBars ? bossBarsBit : 0)
        | (s.nausea ? nauseaBit : 0) | (s.pumpkin ? pumpkinBit : 0) | (s.spyglass ? spyglassBit : 0)
        | (s.water ? waterBit : 0) | (s.lava ? lavaBit : 0) | (s.powderSnow ? powderSnowBit : 0);
}
inline constexpr unsigned effectMask(bool master, bool weather, bool particles, bool bossBars = false, bool nausea = false) {
    return effectMask(master, EffectSelection{weather, particles, bossBars, nausea});
}
// The effect a gameplay-screen mesh belongs to, from its exact vanilla
// material/resource pair. Resource paths are the vanilla texture names used
// only by these overlays (the carved pumpkin block and spyglass item use
// others); anything else is 0 and stays visible.
inline constexpr unsigned overlayMeshBit(std::string_view material, std::string_view resource) {
    if (material == "ui_texture_and_color_blur_additive" && resource == "textures/misc/nausea") return nauseaBit;
    if (material == "on_screen_effect" && resource == "textures/ui/frozen_effect") return powderSnowBit;
    if (resource == "textures/misc/pumpkinblur") return pumpkinBit;
    if (resource == "textures/ui/spyglass_scope") return spyglassBit;
    return 0;
}
inline constexpr bool hideOverlayMesh(unsigned mask, bool localScreen, std::string_view material, std::string_view resource) {
    return localScreen && (mask & overlayMeshBit(material, resource) & overlayBits);
}
inline constexpr bool hideNauseaMesh(unsigned mask, bool localScreen, std::string_view material, std::string_view resource) {
    return localScreen && (mask & nauseaBit) && overlayMeshBit(material, resource) == nauseaBit;
}
// The renderer's camera-medium flags, as fog setup reads them. Hiding a
// medium makes fog setup see the camera outside it; the flags are restored
// right after, so nothing else observes the change.
struct CameraMedium {
    bool water = false, liquid = false, lava = false, powderSnow = false;
    constexpr bool operator==(CameraMedium const&) const = default;
};
inline constexpr CameraMedium visibleMedium(CameraMedium medium, unsigned mask) {
    CameraMedium out = medium;
    if (mask & waterBit) out.water = false;
    if (mask & lavaBit) out.lava = false;
    if (mask & powderSnowBit) out.powderSnow = false;
    // "Liquid" means water or lava; keep it only while one of them stays.
    if ((medium.water && !out.water) || (medium.lava && !out.lava)) out.liquid = medium.liquid && (out.water || out.lava);
    return out;
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
