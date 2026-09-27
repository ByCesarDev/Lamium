#include "features/inventory/FakeOffhandTrace.h"
#ifdef LAMIUM_RESEARCH_TRACE
#include "features/inventory/FakeOffhand.h"
#include "app/Runtime.h"
#include "ll/api/memory/Hook.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/ClientInputCallbacks.h"
#include "mc/client/input/BuildActionIntention.h"
#include "mc/world/phys/HitResult.h"
#include <Windows.h>
#include <intrin.h>
#include <atomic>
#include <chrono>
#include <format>
#include <stdexcept>

namespace lamium::inventory::fakeOffhand {
namespace {
using Clock = std::chrono::steady_clock;
// Each hook keeps its own budget so a hot hook cannot drown the others.
struct Budget { unsigned used = 0; unsigned limit = 0; };
Budget tickBudget{0, 150}, handleBudget{0, 80}, pressBudget{0, 20}, baiBudget{0, 80};
bool capturing = false;
Clock::time_point windowStart{}, lastButtonSeen{};
uintptr_t const moduleBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
uintptr_t callerOffset(void* address) { return reinterpret_cast<uintptr_t>(address) - moduleBase; }
bool anyButtonDown() {
    return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 || (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
}
// Logs from the first physical click until 800 ms after the last button
// release, then stops; at most 300 lines per capture. Timestamps in the log
// order the calls; the offsets name the vanilla call sites.
struct Capture { bool on = false; double ms = 0; };
Capture capture(Budget& budget) {
    auto now = Clock::now();
    bool down = anyButtonDown();
    if (down && !capturing) {
        capturing = true;
        windowStart = now;
        for (auto* b : {&tickBudget, &handleBudget, &pressBudget, &baiBudget}) b->used = 0;
    }
    if (down) lastButtonSeen = now;
    if (capturing && !down && now - lastButtonSeen > std::chrono::milliseconds(800)) capturing = false;
    if (!capturing || budget.used >= budget.limit) return {};
    ++budget.used;
    return {true, std::chrono::duration<double, std::milli>(now - windowStart).count()};
}
template <class... Args>
void log(Capture c, std::format_string<Args...> format, Args&&... args) noexcept {
    if (!c.on) return;
    try {
        Runtime::instance().self().getLogger().info(
            "L-49 t={:.0f}ms {}", c.ms, std::format(format, std::forward<Args>(args)...));
    } catch (...) {}
}
LL_TYPE_INSTANCE_HOOK(TickBuild, ll::memory::HookPriority::Low, ClientInstance,
    &ClientInstance::_tickBuildAction, void, HitResult const& solid, HitResult const& liquid, bool advanceTime) {
    auto c = capture(tickBudget);
    log(c, "tickbuild enter solid={} advance={} chord={} bai={}", static_cast<int>(solid.mType), advanceTime,
        rightChordActive(), getInProgressBAI().mAction);
    origin(solid, liquid, advanceTime);
    log(c, "tickbuild exit bai={}", getInProgressBAI().mAction);
}
LL_STATIC_HOOK(HandleBuild, ll::memory::HookPriority::Low, &ClientInputCallbacks::handleBuildAction, bool,
    IClientInstance& client, BuildActionIntention& bai, HitResult const& solid, HitResult const& liquid) {
    auto c = capture(handleBudget);
    log(c, "handleBuild enter bai={} solid={}", bai.mAction, static_cast<int>(solid.mType));
    auto result = origin(client, bai, solid, liquid);
    log(c, "handleBuild exit bai={} result={}", bai.mAction, result);
    return result;
}
LL_STATIC_HOOK(PressButton, ll::memory::HookPriority::Low,
    &ClientInputCallbacks::handleBuildOrAttackOrBlockSelectButtonPress, void, IClientInstance& client) {
    auto c = capture(pressBudget);
    log(c, "button press enter bai={}", client.getInProgressBAI().mAction);
    origin(client);
    log(c, "button press exit bai={} caller={:#x}", client.getInProgressBAI().mAction,
        callerOffset(_ReturnAddress()));
}
LL_TYPE_INSTANCE_HOOK(ResetBai, ll::memory::HookPriority::Low, ClientInstance,
    &ClientInstance::$resetBai, void, int flags) {
    auto c = capture(baiBudget);
    log(c, "resetBai flags={:#x} bai={} caller={:#x}", flags, getInProgressBAI().mAction,
        callerOffset(_ReturnAddress()));
    origin(flags);
}
LL_TYPE_INSTANCE_HOOK(ClearBai, ll::memory::HookPriority::Low, ClientInstance,
    &ClientInstance::$clearInProgressBAI, void) {
    auto c = capture(baiBudget);
    log(c, "clearBai bai={} caller={:#x}", getInProgressBAI().mAction, callerOffset(_ReturnAddress()));
    origin();
}
struct Hook { int (*install)(bool); bool (*remove)(bool); bool installed = false; };
Hook hooks[] = {{TickBuild::hook, TickBuild::unhook}, {PressButton::hook, PressButton::unhook},
    {ResetBai::hook, ResetBai::unhook}, {ClearBai::hook, ClearBai::unhook}};
bool handleInstalled = false;
}
void startTrace() {
    try {
        for (auto& hook : hooks) {
            if (hook.install(true) != 0) throw std::runtime_error("Could not install Fake Offhand diagnostics");
            hook.installed = true;
        }
        if (HandleBuild::hook(true) != 0) throw std::runtime_error("Could not install build diagnostics");
        handleInstalled = true;
        Runtime::instance().self().getLogger().warn(
            "Fake Offhand diagnostics enabled (L-49): click right, click left while holding right");
    } catch (...) { stopTrace(); throw; }
}
void stopTrace() {
    if (handleInstalled && HandleBuild::unhook(true)) handleInstalled = false;
    for (auto it = std::rbegin(hooks); it != std::rend(hooks); ++it)
        if (it->installed && it->remove(true)) it->installed = false;
}
}
#else
namespace lamium::inventory::fakeOffhand {
void startTrace() {}
void stopTrace() {}
}
#endif
