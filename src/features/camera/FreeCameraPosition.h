#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace lamium::camera {
class FreeCameraPosition {
public:
    using Vector = std::array<double, 3>;
private:
    std::optional<Vector> reference;
    bool worldFixed = false;
    static bool finite(Vector const& value) {
        return std::all_of(value.begin(), value.end(), [](double v) { return std::isfinite(v); });
    }
public:
    bool begin(Vector const& eye, bool fixed) {
        reset();
        if (!finite(eye)) return false;
        worldFixed = fixed;
        reference = fixed ? eye : Vector{};
        return true;
    }
    std::optional<Vector> position(Vector const& eye, Vector const& displacement) const {
        if (!reference || !finite(eye) || !finite(displacement)) return {};
        Vector target{};
        for (size_t i = 0; i < 3; ++i)
            target[i] = (*reference)[i] + displacement[i] + (worldFixed ? 0 : eye[i]);
        return finite(target) ? std::optional{target} : std::nullopt;
    }
    std::optional<Vector> offset(Vector const& eye, Vector const& displacement, bool fixed) {
        auto target = position(eye, displacement);
        if (!target) return {};
        if (worldFixed != fixed) {
            // Rebase the active session so changing the reference cannot jump
            // the camera back to its activation point.
            for (size_t i = 0; i < 3; ++i)
                (*reference)[i] = (*target)[i] - displacement[i] - (fixed ? 0 : eye[i]);
            worldFixed = fixed;
        }
        Vector result{};
        for (size_t i = 0; i < 3; ++i) result[i] = (*target)[i] - eye[i];
        return finite(result) ? std::optional{result} : std::nullopt;
    }
    void reset() { reference.reset(); }
};
}
