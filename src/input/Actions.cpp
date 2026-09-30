#include "input/Actions.h"
#include "features/interaction/PermanentSneak.h"
#include "features/interaction/PeriodicInput.h"
#include "features/inventory/FakeOffhand.h"
#include "input/ToggleAction.h"
#include "settings/Options.h"
#include "features/interaction/BreakingRestriction.h"
#include "features/interaction/ElytraSwap.h"
#include "app/Runtime.h"
#include "features/camera/Zoom.h"
#include "features/inventory/Inventory.h"
#include "features/inventory/game/ScreenTracker.h"
#include "ui/SettingsScreen.h"
#include "ui/SettingsRows.h"
#include "ui/Toast.h"
#include "ui/Localization.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/input/KeyboardRemappingLayout.h"
#include "mc/client/options/IOptionRegistry.h"

namespace lamium {
namespace {
// Auto Attack / Auto Use toasts and status name the mode: "Hold", or
// "Fast click (while held)".
std::optional<std::string> autoModeText(Settings const& value, input::Action action) {
    auto attack = input::actions[static_cast<size_t>(action)].feature == "periodicAttack";
    if (!attack && input::actions[static_cast<size_t>(action)].feature != "periodicUse") return std::nullopt;
    return interaction::autoModeText(attack ? value.interaction.attackMode : value.interaction.useMode,
        attack ? value.interaction.attackHeldOnly : value.interaction.useHeldOnly);
}
std::string toggleFeatureName(input::Action action) {
    if (action == input::Action::HideWeather) return ui::translated("toast.hideWeather");
    if (action == input::Action::HideParticles) return ui::translated("toast.hideParticles");
    if (action == input::Action::HideBossBars) return ui::translated("toast.hideBossBars");
    auto id = input::actions[static_cast<size_t>(action)].feature;
    for (auto const& feature : ui::features)
        if (feature.id == id) return ui::translated(feature.name);
    return std::string(id);
}
bool toggleState(IClientInstance& client, Settings const& value, input::Action action) {
    (void)client;
    if (action == input::Action::BreakingRestriction) return value.interaction.breaking;
    if (action == input::Action::HideWeather) return value.visuals.hideWeather;
    if (action == input::Action::HideParticles) return value.visuals.hideParticles;
    if (action == input::Action::HideBossBars) return value.visuals.hideBossBars;
    if (action == input::Action::PermanentSneak) return interaction::sneak::armed();
    if (action == input::Action::PermanentSprint) return interaction::sprint::armed();
    if (action == input::Action::Zoom) return Zoom::instance().wanted(Zoom::Session::Zoom);
    if (action == input::Action::Freelook) return Zoom::instance().wanted(Zoom::Session::Freelook);
    if (action == input::Action::FreeCamera) return Zoom::instance().wanted(Zoom::Session::FreeCamera);
    auto id = input::actions[static_cast<size_t>(action)].feature;
    for (auto const& feature : ui::features) {
        if (feature.id != id || feature.toggle.empty()) continue;
        if (auto option = settings::find(feature.toggle)) {
            auto current = option->read(value);
            if (auto state = std::get_if<bool>(&current)) return *state;
        }
    }
    return false;
}
void emitToggleToast(IClientInstance& client, input::Action action, Settings const& value) {
    auto name = toggleFeatureName(action);
    if (auto mode = autoModeText(value, action)) name += ": " + *mode;
    if (ui::effectsPaused(input::actions[static_cast<size_t>(action)].feature,value))
        name += " (" + ui::translated("effectsPaused") + ")";
    ui::showToggleToast(name, toggleState(client, value, action));
}
}
bool isSessionFeature(std::string_view feature) {
    return feature == "zoom" || feature == "freelook" || feature == "freecamera"
        || feature == "permanentSneak" || feature == "permanentSprint";
}
bool sessionState(std::string_view feature) {
    if (feature == "zoom") return Zoom::instance().wanted(Zoom::Session::Zoom);
    if (feature == "freelook") return Zoom::instance().wanted(Zoom::Session::Freelook);
    if (feature == "freecamera") return Zoom::instance().wanted(Zoom::Session::FreeCamera);
    if (feature == "permanentSneak") return interaction::sneak::armed();
    if (feature == "permanentSprint") return interaction::sprint::armed();
    return false;
}
void toggleSession(IClientInstance& client, std::string_view feature) {
    if (feature == "zoom") Zoom::instance().toggleWanted(Zoom::Session::Zoom);
    else if (feature == "freelook") Zoom::instance().toggleWanted(Zoom::Session::Freelook);
    else if (feature == "freecamera") Zoom::instance().toggleWanted(Zoom::Session::FreeCamera);
    else if (feature == "permanentSneak") interaction::sneak::toggle(client);
    else if (feature == "permanentSprint") interaction::sprint::toggle(client);
}
std::string bindingChordName(IClientInstance& client, input::Chord const& chord) {
    auto layout = client.getOptions().getCurrentKeyboardRemapping();
    if (chord.empty()) return ui::translated("unbound");
    std::string result;
    for (auto token : chord) {
        if (!result.empty()) result += " + ";
        if (token.device == input::Device::Wheel) result += ui::translated(token.code > 0 ? "wheelUp" : "wheelDown");
        else if (token.device == input::Device::Mouse) result += ui::translated("mouseButton", token.code);
        else result += layout ? layout->getMappedKeyName(token.code) : std::to_string(token.code);
    }
    return result;
}
std::string actionBindingName(IClientInstance& client, input::Action action) {
    return bindingChordName(client, input::effectiveChord(Runtime::instance().preferences().bindings, action));
}
void executeAction(IClientInstance& client, input::Action action) {
    auto& runtime = Runtime::instance();
    if (!runtime.enabled() || ui::ownsInput()) return;
    // Sorting validates its container/text-input context in requestSort.
    if (action == input::Action::Sort) { inventory::requestSort(client); return; }
    if (!gameplayScreen(client.getScreenName())
        && !(action == input::Action::Transfer && inventory::game::ScreenTracker::getInstance().current())) return;
    if (action == input::Action::PermanentSneak) {
        interaction::sneak::toggle(client);
        emitToggleToast(client, action, runtime.preferences());
        return;
    }
    if (action == input::Action::PermanentSprint) {
        interaction::sprint::toggle(client);
        emitToggleToast(client, action, runtime.preferences());
        return;
    }
    if (action == input::Action::CaptureBreaking) { interaction::breaking::capture(client); return; }
    if (action == input::Action::ResetBreaking) { interaction::breaking::reset(); return; }
    if (action == input::Action::Settings) { ui::open(client); return; }
    if (action == input::Action::OpenShapes) { ui::openShapes(client); return; }
    if (action == input::Action::OpenHotkeys) { ui::openHotkeys(client); return; }
    if (action == input::Action::OpenHudLayout) { ui::openHudLayout(client); return; }
    if (action == input::Action::FakeOffhandUse) { inventory::fakeOffhand::press(client); return; }
    if (action == input::Action::ElytraSwapKey) { interaction::elytraSwap::press(); return; }
    // Toggle-style presses report the new state; held Zoom/Freelook do not.
    if (action == input::Action::Zoom) {
        Zoom::instance().press(client);
        if (runtime.preferences().camera.zoomToggle) emitToggleToast(client, action, runtime.preferences());
        return;
    }
    if (action == input::Action::Freelook) {
        Zoom::instance().pressLook(client);
        if (runtime.preferences().camera.freelookToggle) emitToggleToast(client, action, runtime.preferences());
        return;
    }
    if (action == input::Action::FreeCamera) {
        auto const& settings = runtime.preferences();
        auto chord = input::effectiveChord(settings.bindings, action);
        bool wheel = !chord.empty() && chord.back().device == input::Device::Wheel;
        if (wheel && !settings.camera.freeCameraToggle)
            Zoom::instance().toggleWanted(Zoom::Session::FreeCamera);
        else
            Zoom::instance().pressFreeCamera(client);
        if (settings.camera.freeCameraToggle || wheel) emitToggleToast(client, action, settings);
        return;
    }
    auto value = runtime.preferences();
    if (action == input::Action::FreeCameraSpeedUp || action == input::Action::FreeCameraSpeedDown) {
        if (!Zoom::instance().blocksPerspective()) return;
        auto speed = camera::adjustFlightSpeed(value.camera.freeCameraSpeed,
            action == input::Action::FreeCameraSpeedDown ? -1 : 1);
        if (speed == value.camera.freeCameraSpeed) return;
        value.camera.freeCameraSpeed = speed;
        if (!runtime.save(value)) { runtime.self().getLogger().error("Could not save FreeCamera speed"); return; }
        ui::showMessageToast(ui::translated("freeCameraSpeed", speed));
        return;
    }
    if (action == input::Action::CycleAttackMode || action == input::Action::CycleUseMode) {
        settings::find(action == input::Action::CycleAttackMode ? "interaction.attackMode" : "interaction.useMode")->adjust(value,1);
        if (!runtime.save(value)) { runtime.self().getLogger().error("Could not save auto mode"); return; }
        emitToggleToast(client, action, value);
        return;
    }
    if (action == input::Action::CycleBreakingMode) {
        settings::find("interaction.breakingMode")->adjust(value,1);
        if (!runtime.save(value)) runtime.self().getLogger().error("Could not save breaking mode");
        return;
    }
    if (input::toggleAction(value, action)) {
        if (!runtime.save(value)) {
            runtime.self().getLogger().error("Could not save action setting: {}", input::actions[static_cast<size_t>(action)].id);
            return;
        }
        // Emit only once the new state is persisted; a failed save keeps the
        // old settings, so a toast would report a change that never happened.
        emitToggleToast(client, action, value);
        if (action == input::Action::FakeOffhand && !value.inventory.fakeOffhand)
            inventory::fakeOffhand::release();
    }
}
void releaseAction(input::Action action) {
    if (action == input::Action::Zoom) Zoom::instance().release();
    if (action == input::Action::Freelook) Zoom::instance().releaseLookKey();
    if (action == input::Action::FreeCamera) Zoom::instance().releaseFreeCameraKey();
    if (action == input::Action::FakeOffhandUse) inventory::fakeOffhand::release();
}
}
