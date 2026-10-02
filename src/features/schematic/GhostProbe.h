#pragma once
// BACKLOG L-93 research: draws test blocks with three candidate ghost paths so
// their look can be compared in game. Only active with `xmake f --ghost_probe=y`.
namespace lamium::schematic::ghostProbe {
void start();
void stop();
}
