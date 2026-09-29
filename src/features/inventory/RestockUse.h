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
        // A release before completion is an interrupted use. Completed eating
        // sends none while use is held (BDS and local world, 6745b4b).
        if (send == RestockUseSend::Release) {
            if (!timed || !use || !completed || release) return false;
            release = true;
            return true;
        }
        // Holding use re-sends Use for the same slot while eating (BDS, ff3b5da).
        if (send == RestockUseSend::Use && timed && use && !completed) return true;
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
    bool ready() const { return use && (!timed || completed); }
};
// completeUsingItem also runs again right after the next use starts; only a
// completion after at least half the declared duration belongs to this use.
inline bool restockTimedCompletion(std::uint64_t elapsedTicks, int duration) {
    return duration > 0 && elapsedTicks * 2 >= static_cast<std::uint64_t>(duration);
}
// A timed use (food, drink) starts through a failed GameMode::useItem.
inline bool restockUseStarted(bool callbackResult, bool timed) { return callbackResult || timed; }
}
