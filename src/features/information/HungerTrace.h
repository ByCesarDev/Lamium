#pragma once
#ifdef LAMIUM_HUNGER_TRACE
class MinecraftUIRenderContext;
class ScreenView;
namespace lamium::information {
// L-63 research probe; built only with `xmake f --hunger_trace=y`.
void traceHunger(MinecraftUIRenderContext&, ScreenView const&) noexcept;
}
#endif
