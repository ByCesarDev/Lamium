#pragma once
#include "features/map/MapFaces.h"
class Actor;
class IClientInstance;
namespace lamium::map::faces {
// Mob faces for the radar (BACKLOG L-85), found once per kind of mob from
// its renderer's model ("head" part, front face) and texture. Client
// thread only.
// The face for this actor's kind, or -1 when it has none (or is still
// waiting for this frame's load budget).
int faceOf(IClientInstance&, Actor&);
Face const* face(int index);
// Each frame before looking faces up: a few new kinds load per frame.
void frame();
// A new world may bring other resource packs.
void forget();
}
