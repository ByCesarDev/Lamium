#pragma once
#include "features/schematic/Placement.h"
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

// Comparing a placed schematic with what the client sees, and counting what
// is left to place (BACKLOG L-93). Blocks are identified by name and by the
// state key from PaletteBlock::key(); reading them from the world is glue.
namespace lamium::schematic {
enum class CellState : std::uint8_t {
    Ignored,  // structure void, or an extra block while extras are ignored
    Unknown,  // chunk not loaded; never counted as placed or wrong
    Correct,
    Missing,  // nothing (air) where a block belongs
    Wrong,    // a different block
    State,    // the right block with other states (facing, half...)
    Extra,    // a block where the schematic has air
};
struct WorldBlock {
    std::string_view name;
    std::string_view key;
    bool air() const { return name == "minecraft:air"; }
};
// `expected` is null for structure void. `world` is null when not loaded.
inline CellState classify(PaletteBlock const* expected, std::string_view expectedKey,
                          std::optional<WorldBlock> world, bool countExtras) {
    if (!expected) return CellState::Ignored;
    if (!world) return CellState::Unknown;
    if (expected->isAir()) {
        if (world->air()) return CellState::Correct;
        return countExtras ? CellState::Extra : CellState::Ignored;
    }
    if (world->air()) return CellState::Missing;
    if (world->name != expected->name) return CellState::Wrong;
    return world->key == expectedKey ? CellState::Correct : CellState::State;
}

struct Tally {
    std::uint64_t correct = 0, missing = 0, wrong = 0, state = 0, extra = 0, unknown = 0;
    // Blocks the schematic asks for (air excluded): what "correct / total" counts.
    std::uint64_t total() const { return correct + missing + wrong + state + unknown; }
    std::uint64_t mistakes() const { return wrong + state + extra; }
    void add(CellState state, bool expectsBlock) {
        switch (state) {
        case CellState::Correct: if (expectsBlock) ++correct; break;
        case CellState::Missing: ++missing; break;
        case CellState::Wrong: ++wrong; break;
        case CellState::State: ++this->state; break;
        case CellState::Extra: ++extra; break;
        case CellState::Unknown: if (expectsBlock) ++unknown; break;
        case CellState::Ignored: break;
        }
    }
};

// Per palette block name: how many the schematic needs and how many are placed.
struct Material {
    std::uint64_t needed = 0, placed = 0;
    std::uint64_t remaining() const { return needed - placed; }
};
using Materials = std::map<std::string, Material>;
inline void addMaterial(Materials& materials, PaletteBlock const& expected, CellState state) {
    if (expected.isAir()) return;
    auto& material = materials[expected.name];
    ++material.needed;
    if (state == CellState::Correct) ++material.placed;
}
}
