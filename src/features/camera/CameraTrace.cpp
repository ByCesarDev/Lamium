#include "features/camera/CameraTrace.h"
#ifdef LAMIUM_CAMERA_TRACE
#include "features/camera/Zoom.h"
#include "app/Runtime.h"
#include "app/TraceLog.h"
#include "ll/api/memory/Hook.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/deps/renderer/Camera.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <stdexcept>

namespace lamium::camera::trace {
namespace {
struct CameraTraceContext {
    mce::Camera const* setupCamera = nullptr;
    unsigned setupSerial = 0;
    bool seenSetup = false;
};
thread_local CameraTraceContext cameraTraceContext;

// Keep camera identity only for the duration of the synchronous setup callback.
struct CameraTraceScope {
    mce::Camera const* previous;
    explicit CameraTraceScope(mce::Camera const& camera)
    : previous(cameraTraceContext.setupCamera) {
        cameraTraceContext.setupCamera = &camera;
        cameraTraceContext.seenSetup = true;
        ++cameraTraceContext.setupSerial;
    }
    ~CameraTraceScope() { cameraTraceContext.setupCamera = previous; }
};

LL_TYPE_INSTANCE_HOOK(CameraDependenciesTraceHook, ll::memory::HookPriority::Normal, mce::Camera,
    &mce::Camera::updateViewMatrixDependencies, void) {
    origin();
    if (!cameraTraceContext.seenSetup) return;
    static TraceBudget budget;
    auto sample = budget.take(64);
    if (!sample || viewMatrixStack->stack->empty()) return;
    auto count = *sample;
    try {
        auto product = *viewMatrixStack->top()._m * *mInverseViewMatrix;
        float inverseError = 0;
        bool finite = true;
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                finite = finite && std::isfinite(product[column][row]);
                inverseError = std::max(inverseError, std::abs(product[column][row] - (column == row ? 1.f : 0.f)));
            }
        }
        Runtime::instance().self().getLogger().info(
            "Camera dependencies: sample={} setupSerial={} insideSetup={} sameCamera={} finite={} inverseError={} basisLengths={}/{}/{}",
            count, cameraTraceContext.setupSerial, cameraTraceContext.setupCamera != nullptr,
            cameraTraceContext.setupCamera == this, finite, inverseError,
            glm::length(*mRight), glm::length(*mUp), glm::length(*mForward));
    } catch (...) {}
}

// The trace is read-only; the separately enabled probe modifies only the fresh view.
LL_TYPE_INSTANCE_HOOK(CameraTraceHook, ll::memory::HookPriority::Normal, LevelRendererPlayer,
    &LevelRendererPlayer::setupCamera, void, mce::Camera& camera, float alpha) {
    CameraTraceScope scope{camera};
    static TraceBudget budget;
    auto taken = budget.take(3840);
    unsigned count = taken.value_or(0);
    bool sample = taken && count % 120 == 0;
    bool beforeValid = sample && !camera.viewMatrixStack->stack->empty();
    glm::mat4 before{1};
    if (beforeValid) before = *camera.viewMatrixStack->top()._m;
    origin(camera, alpha);
#if defined(LAMIUM_CAMERA_PROBE) || defined(LAMIUM_CAMERA_POSITION_PROBE)
    if (Zoom::instance().viewProbeActive() && !camera.viewMatrixStack->stack->empty()) {
        // Camera-local 20-degree yaw. Pre-multiplication rotates the view without
        // translating its eye. Always compose with this call's vanilla result.
        auto view = *camera.viewMatrixStack->top()._m;
        bool finite = true;
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 4; ++row)
                finite = finite && std::isfinite(view[column][row]);
        if (finite) {
#ifdef LAMIUM_CAMERA_PROBE
            constexpr float angle = 0.3490658504f;
            glm::mat4 rotation{1.f};
            rotation[0][0] = rotation[2][2] = std::cos(angle);
            rotation[0][2] = -std::sin(angle);
            rotation[2][0] = std::sin(angle);
            *camera.viewMatrixStack->getTop()._m = rotation * view;
#else
            // A bounded two-block camera-local displacement. Keep the original
            // world origin; test whether downstream view dependencies and
            // world-relative geometry agree before integrating free movement.
            glm::mat4 translation{1.f};
            translation[3][0] = -2.f;
            *camera.viewMatrixStack->getTop()._m = translation * view;
            static std::atomic<unsigned> positionSamples{0};
            auto sampleIndex = positionSamples.fetch_add(1, std::memory_order_relaxed);
            if (sampleIndex < 8) Runtime::instance().self().getLogger().info(
                "Camera position probe: sample={} appliedLocalRight=2", sampleIndex);
#endif
        }
    }
#endif
    if (!sample || camera.viewMatrixStack->stack->empty()) return;
    try {
        auto const& view = *camera.viewMatrixStack->top()._m;
        auto product = view * *camera.mInverseViewMatrix;
        float inverseError = 0, change = 0;
        bool finite = true;
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                finite = finite && std::isfinite(view[column][row]) && std::isfinite(product[column][row]);
                inverseError = std::max(inverseError, std::abs(product[column][row] - (column == row ? 1.f : 0.f)));
                if (beforeValid) change = std::max(change, std::abs(view[column][row] - before[column][row]));
            }
        }
        Runtime::instance().self().getLogger().info(
            "Camera trace: sample={} setupSerial={} alpha={} before={} finite={} viewChange={} inverseError={} basisLengths={}/{}/{}",
            count / 120, cameraTraceContext.setupSerial, alpha, beforeValid, finite, change, inverseError,
            glm::length(*camera.mRight), glm::length(*camera.mUp), glm::length(*camera.mForward));
    } catch (...) {
        // Diagnostics must not interrupt rendering or expose native text/paths.
    }
}
struct Hook { int (*install)(bool); bool (*remove)(bool); bool installed = false; };
Hook hooks[] = {{CameraDependenciesTraceHook::hook, CameraDependenciesTraceHook::unhook},
    {CameraTraceHook::hook, CameraTraceHook::unhook}};
}
void freeCamera(unsigned reason, double x, double y, double z) noexcept {
    // Bounded per-reason budget: distinguishes a hook that never fires (no
    // samples at all) from missing input, failed advance, or an
    // applied-but-invisible transform. Reasons: 0 no-session, 1 no-input,
    // 2 advance-fail, 3 applied with the displacement.
    static TraceBudget budgets[4];
    if (reason >= 4) return;
    auto sample = budgets[reason].take(4);
    if (!sample) return;
    try {
        static constexpr char const* names[] = {"no-session", "no-input", "advance-fail", "applied"};
        Runtime::instance().self().getLogger().info(
            "FreeCamera trace: what={} sample={} dx={} dy={} dz={}", names[reason], *sample, x, y, z);
    } catch (...) {}
}
void writer(bool same, bool orbit, DetachedCameraMotion::Vector const& displacement) noexcept {
    // Bounded: tells morph (same entity, changed shape) from swap (entity
    // replaced by the perspective switch) in a single session.
    static TraceBudget budget;
    auto sample = budget.take(6);
    if (!sample) return;
    try {
        Runtime::instance().self().getLogger().info(
            "FreeCamera writer: sample={} sameEntity={} orbit={} dx={} dy={} dz={}",
            *sample, same, orbit, displacement[0], displacement[1], displacement[2]);
    } catch (...) {}
}
void look(LookStage stage, float pitch, float yaw) noexcept {
    // Independent budgets: startup render sampling must not consume input evidence.
    static TraceBudget budgets[3];
    auto index = static_cast<unsigned>(stage);
    auto sample = budgets[index].take(32);
    if (!sample) return;
    try {
        constexpr char const* names[] = {"begin", "turn-native-delta", "camera-rotation"};
        Runtime::instance().self().getLogger().info(
            "Freelook trace: stage={} sample={} pitch={} yaw={}", names[index], *sample, pitch, yaw);
    } catch (...) {}
}

void headTurned(float before, float after) noexcept {
    static TraceBudget budget;
    traceLog(budget, 16, "Freelook source: head-turned-by-look before={} after={}", before, after);
}
void start() {
    for (auto& hook : hooks) if (!hook.installed) {
        if (hook.install(true) != 0) { stop(); throw std::runtime_error("Could not install camera trace hook"); }
        hook.installed = true;
    }
}
void stop() {
    for (auto it = std::rbegin(hooks); it != std::rend(hooks); ++it)
        if (it->installed && it->remove(true)) it->installed = false;
}
}
#endif
