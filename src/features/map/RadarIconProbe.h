#pragma once
class IClientInstance;
namespace lamium::map {
// L-85 research: with the radar_icon_probe build option, logs what actor
// renderers offer for face icons and writes the candidates to a BMP. A no-op
// otherwise.
void probeRadarIcons(IClientInstance&);
}
