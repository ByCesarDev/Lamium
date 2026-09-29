#pragma once
#include <cstdint>
namespace lamium::inventory {
enum class RestockUseSend { Place, Use, Release, Other };
// Accept one secondary Use after Place in the same client tick. A later use
// or a repeated verb must never be silently deduplicated.
struct RestockUseEvidence {
    std::uint64_t tick = 0;
    bool placement = false;
    bool timed = false;
    bool use = false;
    bool secondary = false;
    bool release = false;
    bool completed = false;
    bool secondaryCallback = false;
    bool beginSecondaryCallback(std::uint64_t now) {
        if (!placement || now != tick || secondaryCallback) return false;
        secondaryCallback = true;
        return true;
    }
    bool observe(RestockUseSend send, std::uint64_t now, bool sameSlot) {
        if (!sameSlot || send == RestockUseSend::Other) return false;
        if (send == RestockUseSend::Release) {
            if (!timed || !use || release) return false;
            release = true;
            return true;
        }
        if (now != tick) return false;
        if (!use) {
            if (send != (placement ? RestockUseSend::Place : RestockUseSend::Use)) return false;
            use = true;
            return true;
        }
        if (placement && send == RestockUseSend::Use && !secondary) {
            secondary = true;
            return true;
        }
        return false;
    }
    bool ready() const { return use && (!timed || (completed && release)); }
};
}
