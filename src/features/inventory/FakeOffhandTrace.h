#pragma once
// Bounded diagnostics for BACKLOG L-49 (held right-click build session around
// an intervening left click). Only active in `xmake f --research_trace=y`.
namespace lamium::inventory::fakeOffhand {
void startTrace();
void stopTrace();
}
