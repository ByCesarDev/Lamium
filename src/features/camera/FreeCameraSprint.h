#pragma once
#include <cmath>

namespace lamium::camera {
class FreeCameraSprint {
    bool held = false, sprinting = false;
public:
    bool update(double forward, bool sprintHeld, bool gameplay = true) {
        bool pressed = sprintHeld && !held;
        held = sprintHeld;
        if (!gameplay || !std::isfinite(forward) || forward <= 0) sprinting = false;
        else if (pressed) sprinting = true;
        return sprinting;
    }
    bool active() const { return sprinting; }
    // Pausing does not turn an already-held key into another press.
    void cancel() { sprinting = false; }
    void reset() { held = sprinting = false; }
};
}
