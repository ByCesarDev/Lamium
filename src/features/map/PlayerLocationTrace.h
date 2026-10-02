#pragma once
// L-89 research: logs vanilla's player-location state (the Locator Bar's)
// next to the loaded players. Only active with `xmake f --research_trace=y`.
namespace lamium::map::locationTrace {
void start();
void stop();
}
