# Visual effect visibility (L-42)

The maintainer confirmed the scope on 2026-09-30: a keyless "Hide effects"
group under Camera & view, with independent switches and optional keys.
All switches default off and have no default key. Only drawing changes;
weather, sound, equipment, status effects and boss state stay vanilla.

## First implementation step, 2026-09-30

The group currently exposes Rain and snow, and Particles, with the
Experimental badge. Both children carry their own toggle binding. Settings
persist independently; the toast names the individual effect being hidden.

- Rain and snow: after `LevelRendererPlayer::createViewRenderObject`, change
  only the owned `WeatherRenderObject` snapshot for the local client's view.
  Zero rain/snow density and alpha. Keep vanilla `tickRain`/`doRainUpdate`
  intact, including their sound and splash work. Rain splashes remain visible
  unless Particles is also on. Sky darkening stays vanilla.
- Particles: skip `ParticleEngine::render` (legacy layers) and
  `ParticleRenderer::renderParticles` (data-driven particles). Also zero the
  five ambient precipitation layers (plankton, spores and ash) in the weather
  snapshot; keep rain/snow controlled by their own switch. Particle creation
  and ticking stay vanilla, so this is a visibility feature, not a simulation
  or performance reduction. Split-screen per-view particle isolation is not
  established by these global render entry points.
- Hook availability gates each effect. Particles requires both particle
  render hooks and the weather hook, so an incomplete set leaves particles
  vanilla. Validate all seven snapshot densities and alphas before modifying
  anything. Configuration is atomic; render callbacks never copy the full
  settings or retain game pointers. The only weather loop has seven entries.

The release DLL and LamiumTests build and pass. Tests cover all four
weather/particle visibility combinations, independent toggles, the keyless
group and child keys, translations and settings round trip. There is no
Minecraft result yet. Runtime checks must cover both particle pipelines,
rain and snow, Nether ambient layers, immediate restoration, resource packs,
graphics modes, focus/world/dimension transitions and sound preservation.

## Remaining research

Boss bars, the pumpkin overlay, the spyglass frame, the nausea color effect
and water/lava/powder-snow immersion fog/view overlays are not implemented
and are not shown as settings yet. The agreed scope is unchanged.

- `WeatherRenderer` and `PlayerRenderView` are opaque in SDK 26.51.5. Do not
  invent private render methods or offsets.
- The SDK has generic SpriteComponent, TextComponent and custom UI renderer
  entry points, but no confirmed per-effect native entry for the four HUD/view
  effects. Read-only runtime routes must establish what draws each effect
  before filtering it. The public [official HUD sample](https://github.com/Mojang/bedrock-samples/blob/main/resource_pack/ui/hud_screen.json)
  was inspected as platform documentation; it does not establish the current
  game's per-effect native path. No resource-pack source or assets are copied.
- `LevelRendererPlayer::$_getFogDistanceSettingType` exposes Air, Weather,
  Water, Lava, LavaResist and PowderSnow; the renderer also has density and
  volumetric coefficient paths and camera-medium flags. A declaration alone
  does not prove which graphics mode consumes which path, or remove the
  corresponding full-screen view overlay. Do not expose a partially working
  immersion switch by substituting Air before this is checked.

## Bounded read-only observation

`effects_trace` is an opt-in xmake option. An ordinary build contains no UI
or fog trace hooks. Both trace-enabled and trace-disabled DLL builds were
checked; no runtime trace has been collected yet. To collect observations, build with
`xmake f --effects_trace=y` then `xmake build Lamium`; deployment still
requires the maintainer to request a trace build and close Minecraft.

The trace logs only UI control routes, sprite resource paths and fog enum/
camera-medium bits. It never logs displayed text, player identity or world
coordinates, and does not suppress drawing or change fog. In gameplay it
inspects at most 300,000 UI callbacks per process and stores at most 80 unique
routes (64 effect candidates plus 16 other routes), with each route/path
bounded to 192 characters. At most 48 distinct fog combinations are recorded.
These observations identify candidate paths; they do not prove an effect works.

In a local test world, encounter a boss bar, equip a carved pumpkin, use a
spyglass, trigger nausea, and enter water, lava and powder snow. Search the log
for `research L-42`; repeat relevant cases in each graphics mode. Before
ordinary builds, reset with `xmake f --effects_trace=n` and rebuild.
