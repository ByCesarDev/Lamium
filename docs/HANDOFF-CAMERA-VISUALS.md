# Camera and visual effects handoff (2026-09-30)

The maintainer is moving continuation to another agent because of subscription
usage. This is a checkpoint, not completion of L-42. Read AGENTS.md and
AGENTS.local.md, then BACKLOG.md's L-42, CAMERA.md, VISUAL-EFFECTS.md and
VALIDATION.md. Search VALIDATION-LOG.md for the relevant builds instead of
reading the entire history. The repository docs are authoritative.

## Authorization and workspace state

- Work directly on main per the local guide. The maintainer explicitly said
  not to push; this overrides local guidance permitting pushes after tests.
  Commits are authorized after a passing DLL build and LamiumTests.
- Implement all remaining agreed effects; their product scope is already
  decided. Do not reopen those choices merely because native research is hard.
- Do not create/message another task automatically: the maintainer requested
  a written handoff and will choose the next agent.
- No code changes are pending at this handoff. Inspect git status and git log
  for the final documentation commit. No work was pushed during this session.
- Local xmake configuration has every trace/probe option off. The deployed
  DLL is the normal, tested nausea build b239eb9, SHA-256
  `218F05A547F765E27AF78114D5E04A09EF0F10A2C9B6FAB7F1F6F2F7457E3FBA`.
  DLL/PDB/manifest source and destination hashes matched at deployment.
- `bin/Lamium-effects-trace` is an older investigation artifact from d3f0293,
  SHA-256 `675AB9C6A76CDAEDD95D15A50DDE2BFED9D2DEB39457DDFB70673FC048960E3F`.
  It does not contain the nausea hide implementation. Do not deploy it as latest.
- The user tests Minecraft; the agent cannot. Build/test success is not a
  runtime result. Do not keep requesting the same passive observations without
  a new research hypothesis and materially different capture path.

## Agreed behavior and completed work

L-26 FreeCamera speed: 5-100 blocks/s, step 5, default 20, independent bindable
Increase/Decrease speed actions. A fresh sprint press during forward input
starts a horizontal 2x boost. Releasing sprint keeps the boost until forward
input stops; W+A/W+D qualify, sideways-only/backward do not. Vertical speed
does not change. Menus/focus/session exit cancel the boost. Implemented and
confirmed; see CAMERA.md for remaining edge coverage.

L-76 FreeCamera Position reference: Player default preserves body-relative
behavior; World holds a world target independently of body motion. Switching
references mid-flight preserves the current camera position. A new session
starts at the player's current eye; explicit off restores vanilla.

L-77 rapid-motion jitter is fixed and closed. World compensation now runs from
CameraAPI::$tryGetActorInterpolatedPosition using interpolated body position
plus eye/body difference. The hook preserves the API return value and checks
client/actor ownership. After first callback reach, World skips the after-UI
tick-position compensation; a fallback remains if the callback never runs.
The user confirmed improved elytra motion, switching and release on d20fdf8.

L-78 Lamium view entry is fixed and closed. SettingsScreen's shared opener
called Zoom::reset(), which discarded FreeCamera. It now calls suspendInput(),
also used for focus loss. Toggle FreeCamera retains position/orientation while
flight input/timing/sprint pause. Settings/Hotkeys/Shapes/HUD layout, both
references, inventory/window movement and release were confirmed on d3f0293.
Hold and broader lifecycle cases remain release checks.

L-42 Hide effects currently has a saved master (default on, no key), with four
saved child switches (default off, each optionally bound). Master off restores
drawing and keeps selections. Child keys while the master is off edit only
the selection; UI/toasts explain the paused state.

- Rain/snow includes falling precipitation and rain-derived splashes; rain
  sound/weather state stay unchanged. Particles hides all particles and
  ambient precipitation layers, leaving falling rain/snow independent.
  Ordinary water splashes remain visible with only Rain/snow selected.
  RainSplash classification uses the engine's observed mapping, rejecting
  ambiguous mappings shared with water splash/wake. Runtime follow-up passed.
- Boss bars: skip Sprite/Text drawing under exact boss_health_panel and
  boss_hud_panel ancestors ending at hud_screen. At most 32 parents and
  192-character names; both hooks required. Hiding/switch behavior passed on
  d20fdf8; key/persistence/other HUD coverage was not reported separately.
- Nausea color: exact material ui_texture_and_color_blur_additive plus
  resource textures/misc/nausea in the two reference-based Mesh render
  overloads. A thread-local scalar mask scoped to the owning InGamePlayScreen
  render restricts suppression; all three hooks required. The stage-0
  textures/ui/nausea_effect status icon is different and stays visible.
  Lamium never changes vanilla Screen Distortion; this hides the green color,
  not the warp. Latest b239eb9 normal-build test passed child/master/key
  hiding/restoration and preservation of effect/icon/vanilla preference.

Useful code commits: c3c5bdb (World reference), 1e18b64 (interpolation fix),
1d28754 (Lamium view suspension), d20fdf8 (boss bars), b239eb9 (nausea).
Earlier speed/sprint and weather/master/splashes are in 653077c, 1af8009,
7e72244 and 41b1ff6. Validation commits describe exactly what the user saw.

## Remaining five L-42 effects

1. Carved-pumpkin overlay.
2. Spyglass frame, keeping magnification.
3. Water immersion fog/view effects.
4. Lava immersion fog/view effects, including fire resistance.
5. Powder-snow immersion fog/view effects, including freezing overlay.

Each needs an independent saved, default-off, unbound child under the existing
master. Hide drawing only; do not change equipment, status effects, sound,
gameplay medium or camera zoom. Keep L-42 open. None is currently exposed in
settings. Do not implement only immersion fog and advertise the full agreed
fog/view-overlay switch before both routes are established.

### Runtime evidence already collected

Minecraft 1.26.51.01, LeviLamina Client SDK 26.51.5, Windows x64. User reported
Fancy graphics. Active global metadata identified Déesse UI Pack 1.3.9;
world-specific pack stacks were not fully established. Only pack identity
metadata was inspected; do not read/copy third-party pack implementation.

The user repeatedly displayed pumpkin and spyglass frames, so missing trace
candidates are not evidence of absent drawing. For the early nausea test they
saw warp; d3f0293 correctly tested Screen Distortion zero and visible green.

Medium selectors: water distance/density type 2, lava 3, fire-resistant lava 4,
powder snow distance 5/density 1. Resolved Fancy distance endpoints: water
about 14.6841 after one second and 29.5375 after five, lava 0.64, powder snow 2;
recorded density is zero. The phase budget shares lava's medium bit with
fire-resistant lava, so their resolved values are not independently captured.
Air/weather endpoint was 416/512. These values identify paths, not a proven
complete immersion-removal implementation.

Frozen overlay: material on_screen_effect, resource textures/ui/frozen_effect,
stage 1. No production filter uses it yet. A generic on_screen_effect filter
would hide unrelated effects; require per-effect ownership/resource evidence.

### Trace coverage and limits

effects_trace is opt-in. EffectTrace.cpp observes UI Sprite/Text/Custom routes,
two reference-based Mesh overloads, the complete GSL multi-texture span,
ScreenRenderer's three reference-based blit overloads, gameplay/post/vignette
render stages, Tessellator::triggerIntercept and fog selector/resolved setup.
It preserves original calls and does not force tessellator interception.
Budgets, sampling, identifier limits and entry-reach records are documented in
VISUAL-EFFECTS.md. No vertex-buffer reads or retained game pointers are used.

On d3f0293 all sixteen hooks installed, but entry records reached only
inGameRender, spanMesh, rectBlit and variantBlit. No entry for textureBlit,
postLevelRender, vignetteRender or tessellatorIntercept. Span observations
included debug and holo_hand_pointer with span[0], not either missing frame.
Nausea was identified; pumpkin/spyglass were not. Installed is not reached,
and sampled absence is not proof. The user need not repeat these identical
frame observations without a new capture strategy.

### SDK boundaries and next investigation

SDK headers are read-only primary references. FullScreenEffectRenderer,
OnCameraEffectRenderer, PlayerRenderView, WeatherRenderer, FancyFrameRenderer/
Resources, FullscreenEffectDescription, FullScreenOverlayObject and
InsideBlockEffect are empty/opaque in this SDK. Do not invent fields or offsets.
Mesh::_renderMesh takes an opaque by-value brstd::static_vector TextureList;
hooking it with a guessed layout changes the ABI. The GSL span is complete and
was safely forwarded without reading texture entries, but its trace did not
identify the frames. Client/server texture variants may have no resource name.

Next: find a documented, typed backend contract for the missing frame and
immersion view effects rather than repeating the same generic hooks. Record
any new native hypothesis before requesting another runtime probe. Preserve
vanilla return values/state and fail open when ownership or the version-sensitive
contract is unavailable. If no callable safe contract can be found, write the
specific missing API/evidence and limitation instead of shipping a speculative
filter. There is no pending user product decision; the blocker is native evidence.

## Implementation and validation checklist

Pure behavior belongs in headers/tests; native glue in cpp. Settings changes
must update Settings/Options/Store, rows and EN/JP translations. Append actions
without reordering saved ids. Follow PROVENANCE.md and never open reference-only
mod source while implementing. Machine-specific paths come from AGENTS.local.md,
not this tracked document.

Run xmake build Lamium, LamiumTests build/run, and LamiumNativeTests when SDK
adapters change. Deploy only for user testing and with Minecraft closed; copy
DLL/PDB/manifest and compare both hashes. Reset all trace/probe options after
research builds; never accidentally deploy the old saved trace artifact.
End gameplay changes with a short Japanese checklist, source commit and full
DLL SHA-256. Record results at the top of VALIDATION-LOG.md and update
VALIDATION.md; preserve historical entries. Keep remaining release checks in
BACKLOG.md. No push unless the maintainer explicitly changes that instruction.
