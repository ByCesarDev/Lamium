#include "features/schematic/SchematicActions.h"
#include "features/schematic/GhostRenderer.h"
#include "features/schematic/SchematicSession.h"
#include "ui/Localization.h"
#include "ui/Toast.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/phys/HitResult.h"
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
// One block along the strongest axis of the view: up or down when looking
// steeply, else east-west or north-south.
Point step(LocalPlayer& player, int sign) {
    auto v = player.getViewVector(1.f);
    float ax = std::abs(v.x), ay = std::abs(v.y), az = std::abs(v.z);
    if (ay > ax && ay > az) return {0, v.y > 0 ? sign : -sign, 0};
    if (ax >= az) return {v.x > 0 ? sign : -sign, 0, 0};
    return {0, 0, v.z > 0 ? sign : -sign};
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
void selectLooked(IClientInstance& client, LocalPlayer& player) {
    auto const& hit = client.getLatestHitResult();
    if (hit.mType != HitResultType::Tile) { ui::showMessageToast(ui::translated("schematic.toast.notLooking")); return; }
    Point at{hit.mBlock.x, hit.mBlock.y, hit.mBlock.z};
    int dimension = static_cast<int>(player.getDimensionId());
    auto set = session::current();
    int best = -1;
    std::uint64_t bestVolume = std::numeric_limits<std::uint64_t>::max();
    for (int i = 0; i < static_cast<int>(set.placements.size()); ++i) {
        auto const& p = set.placements[static_cast<size_t>(i)];
        auto structure = p.dimension == dimension ? session::structure(p.file) : nullptr;
        if (!structure || !toLocal(structure->size, p.placement, at)) continue;
        // Nested placements: the smallest one around the cell wins.
        if (structure->cells() < bestVolume) { best = i; bestVolume = structure->cells(); }
    }
    if (best < 0) { ui::showMessageToast(ui::translated("schematic.toast.notLooking")); return; }
    session::change([&](PlacementSet& s) { s.selected = best; return true; });
    ui::showMessageToast(ui::translated("schematic.toast.selected", set.placements[static_cast<size_t>(best)].name));
}
// Pressing again moves to the next mistake of the same result.
std::shared_ptr<Verification const> lastResult;
size_t cursor = 0;
void nearestMistake(LocalPlayer& player) {
    auto set = session::current();
    auto result = ghosts::verification();
    if (set.selected < 0) { ui::showMessageToast(ui::translated("schematic.toast.noPlacement")); return; }
    if (result->placement != set.selected || !result->complete) { ui::showMessageToast(ui::translated("schematic.toast.counting")); return; }
    std::vector<Mismatch const*> mistakes;
    for (auto const& m : result->mismatches) if (m.state != CellState::Missing) mistakes.push_back(&m);
    if (mistakes.empty()) { ui::showMessageToast(ui::translated("schematic.toast.noMistakes")); return; }
    cursor = result == lastResult ? (cursor + 1) % mistakes.size() : 0;
    lastResult = result;
    auto const& m = *mistakes[cursor];
    ghosts::point(m.position);
    auto p = player.getFeetPos();
    double dx = m.position.x + .5 - p.x, dy = m.position.y + .5 - p.y, dz = m.position.z + .5 - p.z;
    char const* kind = m.state == CellState::Wrong ? "schematic.kind.wrong" : m.state == CellState::Extra ? "schematic.kind.extra"
        : "schematic.kind.state";
    ui::showMessageToast(ui::translated("schematic.toast.mistake", ui::translated(kind),
        static_cast<int>(std::lround(std::sqrt(dx * dx + dy * dy + dz * dz)))));
}
std::string mirrorName(Mirror mirror) {
    return ui::translated(mirror == Mirror::X ? "schematic.mirror.x" : mirror == Mirror::Z ? "schematic.mirror.z" : "schematic.mirror.none");
}
}

bool handles(Action action) {
    switch (action) {
    case Action::NearestMistake: case Action::SelectLookedPlacement: case Action::NextPlacement:
    case Action::MovePlacementForward: case Action::MovePlacementBack: case Action::MovePlacementHere:
    case Action::RotatePlacement: case Action::MirrorPlacement: case Action::LayerUp: case Action::LayerDown:
        return true;
    default: return false;
    }
}

void press(IClientInstance& client, Action action) {
    auto* player = client.getLocalPlayer();
    if (!player) return;
    auto moved = [](SavedPlacement const& p) {
        return ui::translated("schematic.toast.moved", p.name, p.placement.origin.x, p.placement.origin.y, p.placement.origin.z);
    };
    switch (action) {
    case Action::NearestMistake: nearestMistake(*player); return;
    case Action::SelectLookedPlacement: selectLooked(client, *player); return;
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
    case Action::MovePlacementForward: case Action::MovePlacementBack: {
        auto d = step(*player, action == Action::MovePlacementForward ? 1 : -1);
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
        }, [](SavedPlacement const& p) { return ui::translated("schematic.toast.layer", p.name, p.layers.index + 1, layers(p)); });
        return;
    }
    default: return;
    }
}
}
