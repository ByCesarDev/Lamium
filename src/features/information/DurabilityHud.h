#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

// Held-item durability HUD (BACKLOG L-61, docs/demos/durability-hud.html).
// Pure row selection and formatting; InfoHud samples the stacks each frame.
namespace lamium::information::durability {
enum class Look { BarAndNumber, Number, Bar };
enum class Slot { MainHand, Offhand, Head, Chest, Legs, Feet };
struct Sample {
    bool damageable = false;
    int damage = 0;
    int max = 0;
    bool elytra = false;
};
// Samples in Slot order.
using Samples = std::array<Sample, 6>;
struct Row {
    Slot slot;
    int remaining;
    int max;
    bool focus = false; // The gliding elytra: first, with an accent outline.
    float ratio() const { return max > 0 ? std::clamp(static_cast<float>(remaining) / max, 0.f, 1.f) : 1.f; }
};
inline bool usable(Sample const& sample) { return sample.damageable && sample.max > 0; }
inline Row row(Slot slot, Sample const& sample, bool focus = false) {
    return {slot, sample.max - std::clamp(sample.damage, 0, sample.max), sample.max, focus};
}
inline std::vector<Row> rows(Samples const& samples, bool offhand, bool armor, bool gliding) {
    std::vector<Row> out;
    auto const& chest = samples[static_cast<size_t>(Slot::Chest)];
    // The elytra only wears while gliding, so it is shown then even with the
    // armor option off, and not repeated among the armor rows.
    bool focusElytra = gliding && chest.elytra && usable(chest);
    if (focusElytra) out.push_back(row(Slot::Chest, chest, true));
    auto add = [&](Slot slot) {
        auto const& sample = samples[static_cast<size_t>(slot)];
        if (usable(sample) && !(focusElytra && slot == Slot::Chest)) out.push_back(row(slot, sample));
    };
    add(Slot::MainHand);
    if (offhand) add(Slot::Offhand);
    if (armor) for (auto slot : {Slot::Head, Slot::Chest, Slot::Legs, Slot::Feet}) add(slot);
    return out;
}
inline bool showsBar(Look look) { return look != Look::Number; }
// Bar look shows the number only once a quarter or less remains.
inline bool showsNumber(Look look, Row const& row) { return look != Look::Bar || row.ratio() < .25f; }
inline std::string numberText(Look look, Row const& row) {
    if (look == Look::Bar) return std::to_string(row.remaining);
    return std::to_string(row.remaining) + "/" + std::to_string(row.max);
}
// Bar fill width in GUI units, rounded like the vanilla slot bar.
inline float barFill(Row const& row, float width) {
    return std::clamp(std::round(row.ratio() * width), 0.f, width);
}
}
