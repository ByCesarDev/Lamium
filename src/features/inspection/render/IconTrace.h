#pragma once
// L-91 research: how vanilla inventory slots draw leather armor and enchanted
// shields (render passes, UI materials, textures, icon blits) next to
// Lamium's own icon calls. Only active with `xmake f --icon_trace=y`.
namespace lamium::inspection::iconTrace {
void start();
void stop();
}
