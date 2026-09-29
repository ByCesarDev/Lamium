#pragma once
// L-66 Spike A research: observe which GameMode/Player callbacks fire around
// a real consumption and whether the held slot count actually changes.
// Only callback names, hand/slot indices, counts and booleans are logged; no
// item contents or player identity.
// Only active in builds configured with `xmake f --consumption_trace=y`.
namespace lamium::inventory::game::consumptionTrace {
void start();
void stop();
}
