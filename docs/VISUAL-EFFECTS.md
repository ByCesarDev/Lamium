# Visual effect visibility (L-42)

The maintainer revised the scope after testing on 2026-09-30: "Hide effects"
under Camera & view has a saved master switch without a key, plus independent
child switches and optional keys. The master defaults on and children off;
older files preserve their existing selections. Only drawing changes;
weather, sound, equipment, status effects and boss state stay vanilla.

## First implementation step, 2026-09-30

The group currently exposes Rain and snow, and Particles, with the
Experimental badge. Both children carry their own toggle binding. Settings
persist independently; the toast names the individual effect being hidden.
Master off restores normal drawing without changing selections. Child switches
and keys still edit selections while paused, but never enable the master.
The UI dims child rows and labels them "Main switch off"; their toasts and
help explain that the selected effect is paused.

- Rain and snow: after `LevelRendererPlayer::createViewRenderObject`, change
  only the owned `WeatherRenderObject` snapshot for the local client's view.
  Zero rain/snow density and alpha. Keep vanilla `tickRain`/`doRainUpdate`
  intact, including their sound and splash work. Sky darkening stays vanilla.
  Rain splash drawing is also hidden: skip `Particle::$tessellate` only for
  `ParticleType::RainSplash` in the legacy pipeline. For data-driven particles,
  observe `_emitParticleNew` without changing it, and read the RainSplash
  entry in the engine's own `mNewParticleSystemJsonLookup`. Store at most one
  owned identifier of 192 characters, replacing it only when that entry
  changes; no game pointer is retained. Skip
  `ParticleEmitterActual::$extractForRendering` only when its effect name
  exactly matches that observed identifier. Missing/oversized mappings leave
  emitter drawing vanilla. Three constant-time lookups also reject identifiers
  shared with WaterSplash, WaterSplashManual or WaterWake, leaving such
  ambiguous pack mappings visible. No substring match, guessed identifier, scan of
  live particles, or alteration of emission/ticking is used. WaterSplash and
  other particles stay visible unless Particles is on. The follow-up playtest
  was positive, but no trace establishes individual native callback coverage.
- Particles: skip `ParticleEngine::render` (legacy layers) and
  `ParticleRenderer::renderParticles` (data-driven particles). Also zero the
  five ambient precipitation layers (plankton, spores and ash) in the weather
  snapshot; keep rain/snow controlled by their own switch. Particle creation
  and ticking stay vanilla, so this is a visibility feature, not a simulation
  or performance reduction. Split-screen per-view particle isolation is not
  established by these global render entry points.
- Hook availability gates each effect. Particles requires both particle
  render hooks and the weather hook, so an incomplete set leaves particles
  vanilla. Rain and snow additionally requires the three rain classification/
  extraction hooks. Validate all seven snapshot densities and alphas before modifying
  anything. Configuration is atomic; render callbacks never copy the full
  settings or retain game pointers. The only weather loop has seven entries.

The maintainer confirmed independent rain/snow and particle hiding, restoration
and preserved rain sound on `7e72244`. That build left rain splashes controlled
only by Particles. On `41b1ff6` they checked the revised build in game and
reported no problems; the supplied checklist included the master and rain/
ordinary-water splash distinction, without individual case results. Pure
tests cover all eight master/weather/particle
combinations, exact bounded rain identifiers, independent toggles, the master
and child keys, translations and settings round trip. Additional runtime
checks include both particle pipelines, ordinary water splashes with only
Rain and snow enabled, Nether ambient layers, immediate restoration, resource
packs, graphics modes, focus/world/dimension transitions and sound preservation.

With the master on, Rain and snow hides falling precipitation and rain
splashes. Particles hides rain splashes and every other particle, but leaves
falling precipitation alone. Either child hides rain splashes; master off
shows everything regardless of saved selections.

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
