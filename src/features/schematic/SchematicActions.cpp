#include "features/schematic/SchematicActions.h"
#include "features/schematic/GhostRenderer.h"
#include "features/schematic/SchematicSession.h"
#include "ui/Localization.h"
#include "ui/Toast.h"
#include "app/Runtime.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include <cmath>
#include <limits>

namespace lamium::schematic::actions {
namespace {
using input::Action;
std::optional<Point> feet(LocalPlayer& player) {
    auto p = player.getFeetPos();
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return std::nullopt;
    return Point{static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y)), static_cast<int>(std::floor(p.z))};
}
// One block away from the player along the horizontal axis they face most;
// left and right are a quarter turn from it.
Point away(LocalPlayer& player) {
    auto v = player.getViewVector(1.f);
    if (std::abs(v.x) >= std::abs(v.z)) return {v.x > 0 ? 1 : -1, 0, 0};
    return {0, 0, v.z > 0 ? 1 : -1};
}
Point stepFor(LocalPlayer& player, Action action) {
    auto f = away(player);
    switch (action) {
    case Action::MovePlacementForward: return f;
    case Action::MovePlacementBack: return {-f.x, 0, -f.z};
    // Seen from above with +z south: left of facing east (+x) is north (-z).
    case Action::MovePlacementLeft: return {f.z, 0, -f.x};
    case Action::MovePlacementRight: return {-f.z, 0, f.x};
    case Action::MovePlacementUp: return {0, 1, 0};
    default: return {0, -1, 0};
    }
}
// Applies `apply` to the selected placement and reports `message` built from it.
template <class Apply, class Message>
void changeSelected(Apply&& apply, Message&& message) {
    std::string text;
    bool found = false;
    bool saved = session::change([&](PlacementSet& set) {
        if (set.selected < 0 || set.selected >= static_cast<int>(set.placements.size())) return false;
        found = true;
        auto& p = set.placements[static_cast<size_t>(set.selected)];
        apply(p);
        text = message(p);
        return true;
    });
    if (!found) { ui::showMessageToast(ui::translated("schematic.toast.noPlacement")); return; }
    ui::showMessageToast(saved ? text : ui::translated("schematic.saveError"));
}
int layers(SavedPlacement const& p) {
    auto structure = session::structure(p.file);
    return structure ? std::max(1, layerCount(placedSize(structure->size, p.placement.rotation), p.layers.axis)) : 1;
}
// Where the view ray enters a box, or nullopt when it misses within `reach`.
std::optional<double> enter(Vec3 const& eye, Vec3 const& dir, Point low, Size size, double reach) {
    double near = 0, far = reach;
    double o[3]{eye.x, eye.y, eye.z}, d[3]{dir.x, dir.y, dir.z};
    double lo[3]{static_cast<double>(low.x), static_cast<double>(low.y), static_cast<double>(low.z)};
    double hi[3]{lo[0] + size.x, lo[1] + size.y, lo[2] + size.z};
    for (int i = 0; i < 3; ++i) {
        if (std::abs(d[i]) < 1e-9) {
            if (o[i] < lo[i] || o[i] > hi[i]) return std::nullopt;
            continue;
        }
        double t1 = (lo[i] - o[i]) / d[i], t2 = (hi[i] - o[i]) / d[i];
        if (t1 > t2) std::swap(t1, t2);
        near = std::max(near, t1);
        far = std::min(far, t2);
        if (near > far) return std::nullopt;
    }
    return near;
}
// The placement the view ray reaches first. Ghosts are not blocks, so the
// crosshair hit cannot find them.
void selectLooked(LocalPlayer& player) {
    int dimension = static_cast<int>(player.getDimensionId());
    auto eye = player.getEyePos();
    auto dir = player.getViewVector(1.f);
    auto set = session::current();
    int best = -1;
    double bestDistance = std::numeric_limits<double>::max();
    for (int i = 0; i < static_cast<int>(set.placements.size()); ++i) {
        auto const& p = set.placements[static_cast<size_t>(i)];
        auto structure = p.dimension == dimension && p.visible ? session::structure(p.file) : nullptr;
        if (!structure) continue;
        auto distance = enter(eye, dir, p.placement.origin, placedSize(structure->size, p.placement.rotation), 256);
        if (distance && *distance < bestDistance) { best = i; bestDistance = *distance; }
    }
    if (best < 0) { ui::showMessageToast(ui::translated("schematic.toast.notLooking")); return; }
    session::change([&](PlacementSet& s) { s.selected = best; return true; });
    ui::showMessageToast(ui::translated("schematic.toast.selected", set.placements[static_cast<size_t>(best)].name));
}
// Always the mistake nearest to where the player is now.
void nearestMistake(LocalPlayer& player) {
    auto set = session::current();
    auto result = ghosts::verification();
    if (set.selected < 0) { ui::showMessageToast(ui::translated("schematic.toast.noPlacement")); return; }
    if (result->placement != set.selected || !result->complete) { ui::showMessageToast(ui::translated("schematic.toast.counting")); return; }
    auto p = player.getFeetPos();
    Mismatch const* nearest = nullptr;
    double best = std::numeric_limits<double>::max();
    for (auto const& m : result->mismatches) {
        if (m.state == CellState::Missing) continue;
        double dx = m.position.x + .5 - p.x, dy = m.position.y + .5 - p.y, dz = m.position.z + .5 - p.z;
        double d = dx * dx + dy * dy + dz * dz;
        if (d < best) { best = d; nearest = &m; }
    }
    if (!nearest) { ui::showMessageToast(ui::translated("schematic.toast.noMistakes")); return; }
    ghosts::point(nearest->position);
    char const* kind = nearest->state == CellState::Wrong ? "schematic.kind.wrong"
        : nearest->state == CellState::Extra ? "schematic.kind.extra" : "schematic.kind.state";
    ui::showMessageToast(ui::translated("schematic.toast.nearest", ui::translated(kind), static_cast<int>(std::lround(std::sqrt(best)))));
}
std::string mirrorName(Mirror mirror) {
    return ui::translated(mirror == Mirror::X ? "schematic.mirror.x" : mirror == Mirror::Z ? "schematic.mirror.z" : "schematic.mirror.none");
}
std::string layerText(SavedPlacement const& p) {
    return ui::translated("schematic.toast.layer", p.name, p.layers.index + 1, layers(p));
}
}

bool handles(Action action) {
    switch (action) {
    case Action::NearestMistake: case Action::SelectLookedPlacement: case Action::NextPlacement:
    case Action::MovePlacementForward: case Action::MovePlacementBack: case Action::MovePlacementHere:
    case Action::MovePlacementLeft: case Action::MovePlacementRight: case Action::MovePlacementUp: case Action::MovePlacementDown:
    case Action::RotatePlacement: case Action::MirrorPlacement: case Action::LayerUp: case Action::LayerDown: case Action::LayerHere:
        return true;
    default: return false;
    }
}

void press(IClientInstance& client, Action action) {
    try {
        Runtime::instance().self().getLogger().info("Schematic key: {}", input::actions[static_cast<size_t>(action)].id);
    } catch (...) {}
    auto* player = client.getLocalPlayer();
    if (!player) return;
    auto moved = [](SavedPlacement const& p) {
        return ui::translated("schematic.toast.moved", p.name, p.placement.origin.x, p.placement.origin.y, p.placement.origin.z);
    };
    switch (action) {
    case Action::NearestMistake: nearestMistake(*player); return;
    case Action::SelectLookedPlacement: selectLooked(*player); return;
    case Action::NextPlacement: {
        std::string name;
        bool any = session::change([&](PlacementSet& set) {
            if (set.placements.empty()) return false;
            set.selected = (set.selected + 1) % static_cast<int>(set.placements.size());
            name = set.placements[static_cast<size_t>(set.selected)].name;
            return true;
        });
        ui::showMessageToast(any ? ui::translated("schematic.toast.selected", name) : ui::translated("schematic.toast.noPlacement"));
        return;
    }
    case Action::MovePlacementForward: case Action::MovePlacementBack: case Action::MovePlacementLeft:
    case Action::MovePlacementRight: case Action::MovePlacementUp: case Action::MovePlacementDown: {
        auto d = stepFor(*player, action);
        changeSelected([&](SavedPlacement& p) {
            p.placement.origin.x += d.x; p.placement.origin.y += d.y; p.placement.origin.z += d.z;
        }, moved);
        return;
    }
    case Action::MovePlacementHere: {
        auto at = feet(*player);
        if (!at) return;
        int dimension = static_cast<int>(player->getDimensionId());
        changeSelected([&](SavedPlacement& p) { p.placement.origin = *at; p.dimension = dimension; }, moved);
        return;
    }
    case Action::RotatePlacement:
        changeSelected([](SavedPlacement& p) { p.placement.rotation = quarterTurns(p.placement.rotation + 1); },
            [](SavedPlacement const& p) { return ui::translated("schematic.toast.rotated", p.name, p.placement.rotation * 90); });
        return;
    case Action::MirrorPlacement:
        changeSelected([](SavedPlacement& p) { p.placement.mirror = static_cast<Mirror>((static_cast<int>(p.placement.mirror) + 1) % 3); },
            [](SavedPlacement const& p) { return ui::translated("schematic.toast.mirror", p.name, mirrorName(p.placement.mirror)); });
        return;
    case Action::LayerUp: case Action::LayerDown: {
        int direction = action == Action::LayerUp ? 1 : -1;
        changeSelected([&](SavedPlacement& p) {
            if (p.layers.mode == LayerMode::All) p.layers.mode = LayerMode::Only;
            else p.layers.index = std::clamp(p.layers.index + direction, 0, layers(p) - 1);
        }, layerText);
        return;
    }
    case Action::LayerHere: {
        auto at = feet(*player);
        if (!at) return;
        changeSelected([&](SavedPlacement& p) {
            auto structure = session::structure(p.file);
            if (!structure) return;
            auto placed = placedSize(structure->size, p.placement.rotation);
            Point offset{at->x - p.placement.origin.x, at->y - p.placement.origin.y, at->z - p.placement.origin.z};
            p.layers.index = std::clamp(layerOf(placed, p.layers.axis, offset), 0, layerCount(placed, p.layers.axis) - 1);
            if (p.layers.mode == LayerMode::All) p.layers.mode = LayerMode::Only;
        }, layerText);
        return;
    }
    default: return;
    }
}
}
