#pragma once
// L-66 research: observe the vanilla legacy inventory-transaction flow from a
// manual inventory-screen move. Only slots, counts, call order and boolean
// state are logged; never item contents or player identity.
// Only active in builds configured with `xmake f --research_trace=y`.
namespace lamium::inventory::game::legacyFlowTrace {
void start();
void stop();
}
