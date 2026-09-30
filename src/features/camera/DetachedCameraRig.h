#pragma once
#include "features/camera/DetachedCameraMotion.h"
class LocalPlayer;

// The camera entities Freelook and FreeCamera detach from the player, and what
// each session changed on them, so vanilla gets them back exactly. Client
// thread only.
namespace lamium::camera::rig {
// Withholds the player update from every active camera; each one's look is
// saved the first time it is detached.
void detach(LocalPlayer&);
// Writes the saved looks back and reattaches. Without a player (level gone)
// the saved cameras are only forgotten.
void restore(LocalPlayer*);
bool detached();
// True when an active camera is the first-person (direct-look) rig.
bool firstPerson(LocalPlayer&);
// FreeCamera: takes the offset and body rendering of the last detached camera.
void takeFreeCamera(LocalPlayer&);
void restoreFreeCamera(LocalPlayer*);
// Carries the FreeCamera displacement into the taken camera's offset. False
// when the active camera is no longer the taken one; vanilla values stay.
bool writeOffset(LocalPlayer&, DetachedCameraMotion::Vector const& displacement);
}
