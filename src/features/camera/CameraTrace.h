#pragma once
#include "features/camera/DetachedCameraMotion.h"

// Camera diagnostics (xmake camera_trace and the camera probes). Without them
// every call here is empty, so the camera code calls it without #ifdef.
namespace lamium::camera::trace {
enum class LookStage { Begin, Turn, Render };
#ifdef LAMIUM_CAMERA_TRACE
void start();
void stop();
void freeCamera(unsigned reason, double x, double y, double z) noexcept;
void writer(bool same, bool orbit, DetachedCameraMotion::Vector const& displacement) noexcept;
void look(LookStage, float pitch, float yaw) noexcept;
void headTurned(float before, float after) noexcept;
#else
inline void start() {}
inline void stop() {}
inline void freeCamera(unsigned, double, double, double) noexcept {}
inline void writer(bool, bool, DetachedCameraMotion::Vector const&) noexcept {}
inline void look(LookStage, float, float) noexcept {}
inline void headTurned(float, float) noexcept {}
#endif
}
