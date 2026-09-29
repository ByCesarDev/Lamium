#pragma once
// L-66 research: observe the vanilla legacy inventory-transaction flow from a
// manual inventory-screen move. Only slots, counts, call order and boolean
// state are logged; never item contents or player identity.
// Only active in builds configured with `xmake f --research_trace=y`.
namespace lamium::inventory::game::legacyFlowTrace {
void start();
void stop();
// Set while the L-66 predicted-move probe runs so its calls are traced even
// though no container screen is open. No-op outside research-trace builds.
void markProbe(bool active);
}
