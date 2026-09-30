#pragma once
#include "app/Runtime.h"
#include <atomic>
#include <optional>
#include <utility>

namespace lamium {
// Diagnostic builds only. Each call site owns a budget so one noisy stage
// cannot use up the samples of another, and a trace build cannot flood the log.
class TraceBudget {
    std::atomic<unsigned> used{0};
public:
    // The sample index while the budget lasts.
    std::optional<unsigned> take(unsigned limit) noexcept {
        auto count = used.load(std::memory_order_relaxed);
        while (count < limit && !used.compare_exchange_weak(count, count + 1, std::memory_order_relaxed)) {}
        if (count >= limit) return {};
        return count;
    }
};
// Diagnostics must never interrupt the hooked call.
template <class... Args>
void traceLog(TraceBudget& budget, unsigned limit, fmt::format_string<Args...> format, Args&&... args) noexcept {
    if (!budget.take(limit)) return;
    try { Runtime::instance().self().getLogger().info(format, std::forward<Args>(args)...); } catch (...) {}
}
}
