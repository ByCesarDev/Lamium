#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include "input/Binding.h"
#include "features/interaction/RestrictionMode.h"
#include "features/interaction/AutoMode.h"
#include "overlay/LightMode.h"
#include "features/information/InfoLines.h"
#include "ui/HudElement.h"
#include "features/camera/FreeCameraSpeed.h"
#include "features/map/MapView.h"

namespace lamium {
struct Settings {
    int version = 1;
    input::Bindings bindings;
    struct Interaction {
        // Auto attack / Auto use: periodic interval in client ticks and Fast
        // click rate in clicks per tick. Whole numbers kept as float for the
        // numeric option editor.
        float attackTicks = 10, useTicks = 10;
        float attackClicks = 1, useClicks = 1;
        interaction::AutoMode attackMode = interaction::AutoMode::Periodic, useMode = interaction::AutoMode::Periodic;
        // Fast click only while the button is held. Off by default: like
        // Periodic and Hold, Fast click then acts without a click.
        bool attackHeldOnly = false, useHeldOnly = false;
        // Auto Attack / Auto Use switches: session state, never saved, so a
        // new game never starts clicking by itself.
        bool autoAttack = false, autoUse = false;
        bool breaking = false;
        bool edgeGuard = false; // Stop at block edges without sneaking.
        bool toolGuard = true; // Swap or stop before a held tool breaks (L-62).
        bool toolGuardStrict = true; // A new press does not mine on with it either.
        bool elytraSwap = false; // Put on an elytra by key or a firework jump (L-70).
        float elytraReturnSeconds = 3; // Time after landing before the chestplate returns.
        bool elytraFireworkJump = true; // A jump holding fireworks puts the elytra on.
        interaction::RestrictionMode breakingMode = interaction::RestrictionMode::Plane;
        interaction::RestrictionMode placementMode = interaction::RestrictionMode::Plane;
    } interaction;
    struct Camera {
        // Activation: false = hold the key, true = press to switch. Zoom,
        // Freelook and FreeCamera have no saved on/off state (BACKLOG L-47).
        bool zoomToggle = false;
        bool freelookToggle = false;
        int freelookStartPerspective = 1; // First person, rear third, front third.
        bool freeCameraToggle = true;
        bool freeCameraWorldFixed = false;
        float freeCameraSpeed = 20.f;
        float magnification = 3.0f;
        bool showMagnification = true;
        bool operator==(Camera const&) const = default;
    } camera;
    struct Lighting {
        bool nightVision = false;
    } lighting;
    struct Inspection {
        bool containerPreviews = true;
        bool shulkerPreviews = true;
        bool emptyShulkerPreviews = true;
        bool hideShulkerContents = false;
        bool bundlePreviews = true;
        bool emptyBundlePreviews = true;
        bool durability = true;
    } inspection;
    struct Inventory {
        bool sorting = true;
        bool sortContainers = true;
        bool transfer = true;
        bool transferWheelOne = true;
        bool transferWheelStack = true;
        bool transferDragStack = true;
        bool transferDragOne = true;
        bool toolSwitch = false;
        bool handRestock = false;
        bool restockFromHotbar = true;
        bool restockOffhand = true; // Totems, fireworks and arrows (L-68).
        bool toolSwitchInventory = false; // Fetch a tool from the main inventory (L-69).
        bool fakeOffhand = false;
        int fakeOffhandSlot = 9; // Hotbar slot 1-9.
    } inventory;
    struct Interface {
        int animations = 0; // 0 follow Minecraft's Screen Animations, 1 on, 2 off
        bool toggleToasts = true;
        bool automationStatus = true;
    } ui;
    struct Hud {
        ui::HudElement info = ui::defaultHudElement(ui::HudElementId::Info);
        ui::HudElement target = ui::defaultHudElement(ui::HudElementId::Target);
        ui::HudElement status = ui::defaultHudElement(ui::HudElementId::Status);
        ui::HudElement toast = ui::defaultHudElement(ui::HudElementId::Toast);
        ui::HudElement magnification = ui::defaultHudElement(ui::HudElementId::Magnification);
        ui::HudElement durability = ui::defaultHudElement(ui::HudElementId::Durability);
        ui::HudElement minimap = ui::defaultHudElement(ui::HudElementId::Minimap);
    } hud;
    struct Map {
        bool minimap = false;
        int zoom = map::defaultZoomIndex; // Index into map::zoomSteps; saved as blocks ("range").
        float size = 20; // Map side, percent of the screen height; text follows the HUD layout scale.
        bool rotate = false; // Heading up instead of north up.
        bool round = false;
        bool coordinates = false;
        bool biome = false;
        bool compass = false;
        bool debugHide = true; // Hide while Debug View is shown.
        bool radar = true;
        bool radarPlayers = true, radarHostile = true, radarPassive = true, radarItems = false;
        bool radarInvisible = false; // Also show players and mobs that are invisible.
    } map;
    struct Overlays {
        bool chunkBorders = false;
        bool shapes = true;
        bool hitboxes = false;
        bool light = false;
        overlay::LightValue lightValue = overlay::LightValue::Block;
        float lightRange = 16; // Radius in blocks, sideways and up/down.
        overlay::LightFacing lightFacing = overlay::LightFacing::View;
        float hitboxDistance = 64.f;
    } overlays;
    struct Visuals {
        bool hideOffhand = false;
        // Master off, every effect selected: one switch turns them all on
        // (maintainer, 2026-09-30).
        bool hideEffects = false;
        bool hideWeather = true;
        bool hideParticles = true;
        bool hideBossBars = true;
        bool hideNausea = true;
        bool hideWater = true;
        bool hideLava = true;
        bool hidePowderSnow = true;
    } visuals;
    struct Information {
        bool debug = false;
        int debugLabels = 0; // 0 game standard, 1 Java F3 style
        bool debugHideHud = true;    // Hide the Info HUD while Debug is on
        bool debugHideTarget = true; // Hide the Target card while Debug is on
        bool debugShadow = true;     // Text shadow on the debug panel
        bool target = false;
        bool targetIdentifier = true;
        bool targetIcon = true;
        int targetHealth = 0; // 0 hearts, 1 bar, 2 number
        int targetArmor = 0;  // 0 icons, 1 bar, 2 number
        int targetGrowth = 0; // 0 bar, 1 number
        float targetDistance = 6; // Blocks from the viewpoint (the body, or a detached camera)
        bool targetStates = false; // Other details
        bool targetCoordinates = false;
        bool durabilityHud = false;
        int durabilityLook = 0; // 0 bar and number, 1 number, 2 bar (number below a quarter)
        bool durabilityOffhand = true;
        bool durabilityArmor = true;
        bool hud = false;
        bool coordinates = true; // Defaults match DESIGN "HUD".
        bool scaledCoordinates = false;
        bool dimension = false;
        bool biome = true;
        bool biomeId = false;     // Append the registry id to the localized name.
        bool biomeIdOnly = false; // With biomeId, show only the registry id.
        bool difficulty = false;
        bool facing = true;
        bool yaw = false;
        bool pitch = false;
        bool sprinting = false;
        bool fps = true;
        bool frameTime = false;
        bool light = false;
        bool ping = false;
        bool rotation = false;
        bool block = false;
        bool chunk = false;
        bool speed = false;
        bool horizontalSpeed = false;
        bool verticalSpeed = false;
        bool time = false;
        bool realTime = false;
        bool realTimeDate = false;
        bool weather = false;
        bool moon = false;
        std::vector<std::string> lineOrder;
    } information;

    void normalize() {
        for (auto* ticks : {&interaction.attackTicks, &interaction.useTicks}) {
            if (!std::isfinite(*ticks)) *ticks = 10;
            *ticks = std::clamp(std::round(*ticks), 1.f, 1200.f);
        }
        for (auto* clicks : {&interaction.attackClicks, &interaction.useClicks}) {
            if (!std::isfinite(*clicks)) *clicks = 1;
            *clicks = std::clamp(std::round(*clicks), 1.f, 10.f);
        }
        auto normalizeMode = [](auto& mode) { if (static_cast<unsigned>(mode) >= 4) mode = lamium::interaction::RestrictionMode::Plane; };
        normalizeMode(interaction.breakingMode);
        for (auto* mode : {&interaction.attackMode, &interaction.useMode})
            if (static_cast<unsigned>(*mode) >= lamium::interaction::autoModeNames.size()) *mode = lamium::interaction::AutoMode::Periodic;
        information.targetHealth = std::clamp(information.targetHealth, 0, 2);
        information.targetArmor = std::clamp(information.targetArmor, 0, 2);
        information.debugLabels = std::clamp(information.debugLabels, 0, 1);
        information.durabilityLook = std::clamp(information.durabilityLook, 0, 2);
        ui.animations = std::clamp(ui.animations, 0, 2);
        information.targetGrowth = std::clamp(information.targetGrowth, 0, 1);
        if (!std::isfinite(information.targetDistance)) information.targetDistance = 6;
        information.targetDistance = std::clamp(std::round(information.targetDistance), 2.f, 64.f);
        normalizeMode(interaction.placementMode);
        if (inventory.fakeOffhandSlot < 1 || inventory.fakeOffhandSlot > 9) inventory.fakeOffhandSlot = 9;
        information.lineOrder = information::mergeLineOrder(information.lineOrder);
        if (static_cast<unsigned>(overlays.lightValue) >= overlay::lightValueNames.size()) overlays.lightValue = overlay::LightValue::Block;
        if (static_cast<unsigned>(overlays.lightFacing) >= overlay::lightFacingNames.size()) overlays.lightFacing = overlay::LightFacing::View;
        if (!std::isfinite(overlays.lightRange)) overlays.lightRange = 16;
        overlays.lightRange = std::clamp(std::round(overlays.lightRange), 4.f, 64.f);
        if (!std::isfinite(overlays.hitboxDistance)) overlays.hitboxDistance = 64.f;
        overlays.hitboxDistance = std::clamp(overlays.hitboxDistance, 8.f, 128.f);
        if (!std::isfinite(interaction.elytraReturnSeconds)) interaction.elytraReturnSeconds = 3;
        interaction.elytraReturnSeconds = std::clamp(std::round(interaction.elytraReturnSeconds * 2) / 2, 0.f, 10.f);
        auto normalizeElement = [](ui::HudElement& element, ui::HudElement defaultValue) {
            if (!std::isfinite(element.dx)) element.dx = defaultValue.dx;
            if (!std::isfinite(element.dy)) element.dy = defaultValue.dy;
            element.dx = std::clamp(element.dx, -512.f, 512.f);
            element.dy = std::clamp(element.dy, -512.f, 512.f);
            if (!std::isfinite(element.scale)) element.scale = defaultValue.scale;
            element.scale = std::clamp(element.scale, 75.f, 150.f);
            if (static_cast<unsigned>(element.anchor) > 8) element.anchor = defaultValue.anchor;
            if (static_cast<unsigned>(element.background) > 1) element.background = defaultValue.background;
        };
        normalizeElement(hud.info, ui::defaultHudElement(ui::HudElementId::Info));
        normalizeElement(hud.target, ui::defaultHudElement(ui::HudElementId::Target));
        normalizeElement(hud.status, ui::defaultHudElement(ui::HudElementId::Status));
        normalizeElement(hud.toast, ui::defaultHudElement(ui::HudElementId::Toast));
        normalizeElement(hud.magnification, ui::defaultHudElement(ui::HudElementId::Magnification));
        normalizeElement(hud.durability, ui::defaultHudElement(ui::HudElementId::Durability));
        normalizeElement(hud.minimap, ui::defaultHudElement(ui::HudElementId::Minimap));
        map.zoom = map::clampZoomIndex(map.zoom);
        if (!std::isfinite(map.size)) map.size = 20;
        map.size = std::clamp(std::round(map.size), 10.f, 50.f);
        camera.freeCameraSpeed = camera::normalizeFlightSpeed(camera.freeCameraSpeed);
        if (!std::isfinite(camera.magnification)) camera.magnification = 3.0f;
        camera.magnification = std::clamp(camera.magnification, 2.0f, 50.0f);
        camera.freelookStartPerspective = std::clamp(camera.freelookStartPerspective, 0, 2);
    }
};
}
