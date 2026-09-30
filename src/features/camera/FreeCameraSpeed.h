#pragma once
#include <algorithm>
#include <cmath>

namespace lamium::camera {
inline float normalizeFlightSpeed(float speed) {
    if (!std::isfinite(speed)) return 20.f;
    return std::clamp(std::round(speed / 5.f) * 5.f, 5.f, 100.f);
}
inline float adjustFlightSpeed(float speed, int direction) {
    return normalizeFlightSpeed(normalizeFlightSpeed(speed) + (direction < 0 ? -5.f : 5.f));
}
}
