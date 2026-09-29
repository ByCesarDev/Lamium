#pragma once
namespace lamium::interaction::toolGuard {
// Before a held tool (or worn elytra) breaks, swap in the same item from the
// inventory; without one, stop held mining (BACKLOG L-62).
void start();
void stop();
}
