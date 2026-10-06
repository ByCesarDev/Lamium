# Lamium backlog

Work the maintainer has decided to pursue. Ideas are discussed first and
enter this file only once they are to be worked on; each gets the next free
`L-` number, a kind and its open questions. A task with an open user-visible
choice is **Design**, whoever writes it down.

Each task has a **kind**, which decides who should pick it up:

| Kind | Meaning | Who |
|---|---|---|
| **Ready** | Spec is complete; mostly pure logic + tests + small glue | Any agent, including cheap models |
| **Design** | A user-visible choice is open. Output is a spec (often a web demo) that turns into Ready tasks | Maintainer + strong model |
| **Research** | Needs native reverse engineering, trace builds or runtime-driven debugging | Strong model; cheap models may only collect traces |

Ready tasks marked **(strong model)** are fully specified but visual or
cross-cutting enough that a strong model should implement them.

A large or open-ended feature (Map, placement and breaking features, later
Schematic) starts with a conversation with the maintainer about what it
should be - purpose, scope, what is left out - before any spec, spike or
mockup (decided 2026-09-28).

**Bugs** (something that ships behaves wrongly) are listed first in their own
section and are fixed before new features. Each bug still has a kind that
decides who picks it up.

Finished and closed items live in [BACKLOG-DONE.md](BACKLOG-DONE.md) with
their full history. An `L-` number referenced elsewhere that is not in this
file is there.

## Release policy (decided 2026-09-28)

Lamium is developed at the maintainer's pace, for their own use first, and
published for whoever wants it. There is no "first release" gate.

- Release when a meaningful set of changes has landed and main builds, passes
  the tests and has no known crash. Before tagging, smoke-test the release
  build (the mod loads, settings open, the changed features work) and update
  the README feature list and Known issues. A full regression is not required.
- From 0.1.4 on, releases are ordinary GitHub releases, not marked
  pre-release: the 0.x version, the README status and the Experimental badges
  say what is unfinished.
- Pushing a `v<version>` tag is public distribution. The LIP registry picks up
  every semver `v*` tag, including `-rc.N` style prerelease tags, whatever the
  GitHub pre-release flag says, and LeviLauncher offers it. A build meant only
  for testing is shared as a CI artifact or branch build without a tag, not as
  a GitHub pre-release.
- The version is set in `xmake.lua` and `tooth.json`. The asset is
  `Lamium-<version>-client-windows-x64.zip`, built by
  `scripts/New-ReleaseArchive.ps1` (never by hand); the folder inside stays
  `Lamium/`, and release notes name the asset the same way. The LIP registry
  adds each `v*` tag through a registry PR, so Bedrinth and LeviLauncher pick
  up a release without a registration step. Package rules and the release
  checklist: [DISTRIBUTION.md](DISTRIBUTION.md).
- Large features may start at any time. They land on main in steps, default
  off and with the Experimental badge, so main stays releasable while they
  grow. A step that is not usable yet stays out of the settings screen (or
  behind a build option) instead of being shown half-working.

## Current execution order

Keep this section short. It is only the ordering layer; task details and status
live in the L-items below. If this summary ever disagrees with an L-item, the
L-item wins. Every entry names what the task is, not only its number.

1. **Small and medium features**, picked by the maintainer:
   - L-90 Simplified Chinese localization: built and checked in game; waits
     for a native review of the wording.
   - Offhand follow-up: L-95 Fake Offhand beyond block placement (Research);
     L-94 and L-97 are done.
2. **Placement and breaking — L-15 restrictions and L-59 held placement
   style:** specs written after the 2026-09-28 discussion; building waits for
   the maintainer's go.
3. **Map — L-60 minimap, waypoints and world map:**
   resumed 2026-10-01. Runs in parallel with the small and medium features
   in 1; neither ranks above the other. Steps 1-5 (minimap, cave view,
   radar, waypoints with their screen) and the world map are built, checked
   in a local world and, for the minimap, radar and world map, on an
   external BDS server with a large explored area (2026-10-02). L-82
   ended as a link to an external seed map (done 2026-10-01); seed-based
   biomes and structures are a non-goal. L-83's map/settings UI review is
   done. L-89 distant players is done (2026-10-03). Open: waypoint storage
   on a server and L-86 radar-face follow-ups.
4. **Schematic — L-93 load, place, project, verify and list materials:**
   chosen 2026-10-03; the design conversation is in progress (decisions
   and open questions in the L-item). No code before the spec is agreed.
5. **Research when convenient:** L-37 FreeCamera seeing caves (wanted),
   L-79 carved pumpkin and spyglass frame draw path (cheap-model friendly
   trace/test steps), L-71 starting a glide from the mod, L-57
   client counters, L-30 Ender Dragon part hitboxes, L-33 mob growth and
   breeding timers.
6. **L-73 architecture review:** agreed 2026-09-30, in progress step by
   step (order in the L-item); step 13 goes with L-15 breaking.
7. **Before a release:** the pre-release checks below.

Ideas that are not yet chosen (for example more inventory transfer gestures,
an arrow-count HUD line, a fall-rescue elytra, Mass Craft) stay
in the maintainer's notes and enter this file once chosen.
Mass Craft ranks below Map because resource packs already cover part of it.

Task-picking rule: bugs first; otherwise work on what the execution order
names. Cheap models skip strong-model, Design and Research work. Follow the
L-item's dependencies and model/validation requirements. When a task is done,
update its status and relevant feature doc, then move it to BACKLOG-DONE.md;
do not duplicate task details into this summary.

---

## Pre-release checks

Behavior confirmed only on trace builds or only locally. Check these on the
trace-disabled release build before tagging (VALIDATION.md has the gaps per
feature):
- 0.1.6 was released on 2026-10-02 (`v0.1.6`, tag CI passed; asset SHA-256
  `3d864ca1...3946554f`): L-90 Simplified Chinese (first AI-assisted
  translation, corrections welcome), L-88 target hearts, L-75 offhand slot,
  L-63 saturation, L-64/L-92 food values and durability inside the vanilla
  tooltip. The maintainer's smoke test passed on the release-ZIP DLL
  `86f5baf0...2587883f` (`b71c9f8`). No settings migration. After tagging:
  check that the registry PR picks up `v0.1.6` and that LeviLauncher/Bedrinth
  offer it.
- 0.1.5 was released on 2026-10-02 (`v0.1.5`, tag CI passed) at the maintainer's request,
  with the map (L-60, L-85, L-87) as its main change. The release build
  `6add9b9` (DLL `0fae1c58...dc657614c`, from the release ZIP) was deployed
  for a smoke test; no separate result was reported before tagging.
  After tagging: check that the registry PR picks up `v0.1.5` and that
  LeviLauncher/Bedrinth offer it.
- 0.1.4 was released on 2026-09-30 (`v0.1.4`, tag CI passed). Its smoke test
  passed on the release build `4d25424`: the 0.1.4 version, Hand Restock
  (L-66) and offhand totems (L-68), Tool Protection (L-62), Tool Switch fetch
  (L-69), Auto Elytra (L-70) and the L-02 dedicated openers. Still open:
  Hand Restock on BDS and with real latency. The coverage gaps below carried
  past 0.1.4; recheck the relevant ones before the next release.
- FreeCamera speed controls (L-26): five-step adjustment and speed keys passed
  on `7e72244`; revised labels and forward-only sprint follow-up passed on
  `41b1ff6`. Restart persistence and detailed input/menu/focus combinations
  were not reported separately.
- FreeCamera Position reference (L-76): Player remains the default; World
  compensates body movement, with live switching preserving the camera target.
  Position retention, live switching and release passed on `43c4211`; rapid
  elytra/body movement fix (L-77), switching and release passed on `d20fdf8`.
  Lamium menus, inventory/window movement and release passed on `d3f0293`
  (L-78). Check target readouts, Hold input ownership and session cleanup;
  see CAMERA.md.
- Hide effects first step (L-42): master/rain-splash follow-up passed on
  `41b1ff6`. Remaining coverage: restart persistence, child keys while the
  master is off, both pipelines, ambient layers, resource packs, graphics
  modes and world/dimension transitions; see VISUAL-EFFECTS.md.
- Boss bars (L-42): hiding and switch behavior passed on `d20fdf8`.
  Check the optional key separately, other HUD elements, restart persistence
  and additional resource packs/graphics modes on a trace-disabled build.
- Nausea color (L-42): child/key/master hiding/restoration and unchanged
  effect/icon/vanilla preference passed on normal build `b239eb9`. Remaining:
  restart persistence, additional packs/modes and lifecycle/owner cases.
- After tagging 0.1.4: the registry PR picks up `v0.1.4`; check that
  LeviLauncher/Bedrinth offer 0.1.4 with the icon (L-72) once it is merged.
- If possible, a server with real latency for Hand Restock.
- Distant players on the map (L-89) and opaque player markers at any
  height: checked on phone/PC-hosted worlds with a trace build; check on the
  release build and a dedicated server.
- Weapon Switch (L-67, after 0.1.6): checked locally on `7b702da`; check on a
  server (the same-hit equipment packet) and on the release build.
- Offhand swap (L-94, F): checked locally on `856d79c`/`ed288b6`/`c7bb827`
  (every game mode with hands); check on a server that the screenless swap is
  not rolled back, and on the release build.
- Fixed-slot fetch (L-97) and the stronger-weapon fetch: checked locally on
  `c7bb827`; check on a server (the same-hit selection report) and on the
  release build.

---

## Open decisions

HUD (docs/demos/hud.html), the settings key and the shape model are decided;
see DESIGN.md.

---

## Bugs

### L-73 Architecture review
Kind: Refactor (strong model). Review done 2026-09-30 on main 4d1790b
(read-only); classification and order agreed with the maintainer the same
day. No rewrite: pure logic in headers, feature docs and validation records
are sound. One commit per step; build + LamiumTests after each.
Status: steps 1-10 done 2026-09-30. In-game checks 1 and 2 passed except
Auto Attack/Use (fixed in 221edcb, rechecked the same day) and an occasional
Breaking Restriction hold that stops breaking (cause unknown; carried into B
and L-15). Step 9 concluded that no further camera split was useful: `Zoom`
was renamed `CameraSessions` (file and class), with trace/probe code and
detached-camera state already separated; the main file is 752 lines with 5
`#if`. Step 10 (084b424) was checked in game. Steps 11 and 12 were dropped
after review (see D). The only remaining step is 13 with L-15.

Fix (can cause wrong behavior)
- A. Breaking Restriction and Tool Switch read and write their
  `restartPending` flag before checking that the call is the client's own
  player. In a local world the integrated server's player runs the same
  GameMode calls (L-31), so it can consume the client's restart (a held
  attack then does not resume) or restart the server's session; possibly
  from another thread. Tool Protection already filters first. In game:
  singleplayer, hold attack across a rejected block and back; Fetch from
  inventory wait and restart while held.

Tidy (agreed)
- B. Shared mining-session control for Breaking Restriction, Tool Switch
  (L-69) and Tool Protection (L-62). Their order is implicit in hook
  priorities (Highest/High/Normal); each pause uses `stopDestroyBlock` and
  each restart re-enters the whole chain through `startDestroyBlock`, which
  Tool Protection counts as a new press and Tool Switch's stop hook sees as
  its own. Do it with, or just before, the L-15 breaking step: pure header
  and tests first, then move one feature per commit. In game: all
  combinations.
- C. Split `Zoom.cpp` (1,195 lines, 21 `#if`): trace/probe hooks and helpers
  to `CameraTrace.cpp`; camera component save/restore (detach, offset,
  body) to its own file; then decide whether Zoom (magnification, FOV,
  wheel, sensitivity) moves out. Freelook and FreeCamera share one detached
  session by design and stay together. Keep the `Zoom` facade (about 40
  call sites). In game: Zoom, Freelook, FreeCamera, F5, dimension change,
  leaving the world; also build with camera_trace and both probes.
- D. `SettingsScreen.cpp` (1,878 lines). Closed after step 10 (maintainer,
  2026-09-30): the Shapes view and the input listeners use 20+ screen-wide
  variables and the Shapes list also renders inside the table, so a file
  split would only move text behind a header of shared variables. Split it
  when the screen grows again, after grouping its state first. Original
  plan: The pure parts are already out
  (SettingsTable, SettingsNavigation, ShapesLayout, ShapeEditor, NumberInput,
  SearchQuery); what remains is about 80 file-scope variables under one
  mutex. First, Enter/Esc/Tab while editing a number or a shape name saves
  settings and shapes from inside the key event; defer that to the frame.
  Then move the Shapes view and the input listeners to their own files. In
  game: search, number entry, key binding, shape editing, HUD layout.
- E. Runtime feature table: one ordered list of start/stop, stopped in
  reverse. Correction (in-game check 2026-09-30): periodic input and the
  automation trace must start in `load()`; they capture the button handlers
  the client registers between load and enable. Moving them into enable()
  (5e877e5) stopped Auto Attack/Use; 221edcb restores the load() start.
- F. One budgeted trace helper instead of the four `trace(stage, value)`
  copies (ElytraSwap, ToolGuard, HandRestock, InventoryMove) and Zoom's own
  budget loops. The trace-only files (with stubs) already follow the rule;
  keep `#ifdef` for all research traces.
- G. SettingsStore fallbacks: 96 literal defaults repeat `Settings.h` (none
  differ today; camera already uses the struct value). Use the struct value
  everywhere and test that each empty section decodes to `Settings{}`. No
  schema framework.
- H. HideOffhand removes all three hooks on stop even when not installed;
  give each an installed flag. No general HookSet (HideEffects needs
  per-hook fail-open).

Not now
- Moving the totem watch out of `HandRestock.cpp` (about 50 lines sharing
  Restock state, validated in game).
- Test registration: every suite and test function is called today.
- `Runtime::preferences()` copies, `settings::find()` linear scan, JSON write
  per change: profile first.
- Runtime log levels, renaming `Zoom`, test layers (BDS, computer-use): a
  separate Research item if wanted.

Order: 1 A; 2 test that empty sections decode to defaults; 3 G; 4 F; 5 H;
6 E; 7 C trace/probe; 8 C camera state (in-game check); 9 decide on the Zoom
split; 10 D deferred save; 11 D Shapes view; 12 D input listeners (in-game
check); 13 B with L-15 (in-game check). In-game check 1 follows step 1.

---

## Design

### L-95 Fake Offhand beyond block placement
Kind: Research (strong model). Resumed by the maintainer 2026-10-06.
Status: broad item-use scope chosen 2026-10-06; vanilla use baseline and
existing-placement regression checked on `c673fad`. First instant-use
candidate `621a7b8` failed in every tested empty-hand/sword/pickaxe combination;
placement remained usable. Revised known-item eligibility on `c4d6258`
reached water placement/collection but repeated them during a hold; snowballs
still did nothing (build calls never reached air use). The queued activation
delivered one ordinary use-button pair with a scoped selection. On
`c522b22`, the maintainer confirmed snowballs, water placement/collection,
block placement and chest interaction. Single-use-per-hold is a temporary
adapter limitation, not the desired behavior: held activation must repeat
according to ordinary item-use cadence and cooldowns. Native selected-bucket
traces show repeated replacement transitions roughly 200-250 ms apart.
The maintainer also confirmed native held snowballs in air and on a block;
traces place the repeat air-use calls inside build processing at roughly
200-250 ms intervals. The adapter now retains the ordinary native hold and
borrows/restores selection within each build call, leaving repeat timing to
vanilla. On `e7ce3f1`, the maintainer confirmed empty-primary snowball/bucket
repetition and release, manual-selection cancellation, placement and chest
interaction. Buckets also worked with swords/pickaxes, but snowballs failed
with those primaries. `2f7878c` traces establish that eligibility, selection
and edge replay succeed, but the physical primary use runs first and borrowed
snowball use never follows. The adapter now intercepts the native use-button
handler before that first primary attempt, invokes the captured handler list
once under borrowed selection and suppresses the duplicate queued/physical
press. The revised input ordering awaits runtime checks. Manual selection, target
changes or lost eligibility cancel held ownership; broader cancellation
checks remain open.
Timed use, entities, general primary-hand priority and
passive effects remain open.
Technical findings, scope and the runtime research plan:
[FAKE-OFFHAND.md](FAKE-OFFHAND.md).

Goal: support as much secondary-hand use as the client-only platform permits,
including instant use, timed use and entity interactions, with
ordinary vanilla item-use semantics and restoration of the prior selection.
This remains temporary main-hand selection; it does not move an item into
the real offhand. Keep the existing switch, target slot and activation binding.

Scope (maintainer direction, 2026-10-06):
- Blocks, food/drinks, throwables, buckets, tools, fireworks, bows,
  crossbows, tridents and entity-directed uses such as feeding or shearing.
- Research held-item effects too: blocking, maps, ammunition preference,
  totems and equipment/enchantment effects. A target hotbar slot is not a
  real equipped hand; document unsupported effects individually rather than
  silently treating ordinary item use as complete support.
- Target behavior: normal target interaction and applicable selected-hand
  use take priority; the secondary item is used when the first hand passes.
  A failed or unavailable path must not cause a second mutation. Existing
  L-49 placement behavior stays during the diagnostic step; changes to its
  priority are implemented and checked as part of the extension.
- Restore after instant actions and after timed use ends. An explicit manual
  selection takes ownership and is never overwritten by delayed restoration.
  A visible selected-slot change during timed use is a feasibility question,
  not permission to permanently leave the secondary item selected.

Open research: whether vanilla can retain timed use of the secondary slot
while the primary slot is selected, including ordinary attacks; whether
selection/equipment reporting is accepted by servers; which passive effects
can be supported without a server mod or invented authoritative state.

Steps:
1. Trace ordinary instant use, timed food use, charged use and entity use
   with Fake Offhand off: start, progress, completion/release, selected and
   use slots, and reported selection. `offhand_trace` enables only this
   research, with per-stage budgets. Trace builds require a request before
   deployment.
2. Implement the smallest adapter supported by those observations. Keep
   block placement's per-call restoration; do not extend selection across
   frames for every category. Put ownership/restoration decisions in pure
   logic with tests; cancel on menu/focus/world/dimension changes and disable.
3. Check actual effects and inventory state locally, then on a server;
   a callback result or submitted transaction is not completion evidence.

### L-59 Held placement style: vanilla, Java-like or fast
Kind: Design done (discussion with the maintainer, 2026-09-28); Research
first, then Ready **(strong model)**. Started as "keep placing across a left
click" after L-49 closed as vanilla parity.
Status: planning. Nothing is built, and no step starts until the maintainer
says so.
What it is for: building bridges and long straight lines quickly by holding
the use button with a block. Block placement only; eating, bows and buckets
keep vanilla behavior.

#### Spec (decided 2026-09-28)
- One setting, "Held placement", under Actions: Vanilla (default), Java-like,
  Fast. Default Vanilla because players expecting Bedrock would be
  surprised.
- Vanilla: Bedrock's own held build session, unchanged.
- Java-like: while the use button is held with a block, place on whatever
  face the crosshair targets at Java's fixed interval (every 4 ticks):
  no direction lock and no placing into air; nothing is placed when the
  crosshair is not on a face. An intervening left click does not
  end it (the original L-59 request): placing resumes while right is still
  held.
- Fast: while right is held, place on each new block face the crosshair
  crosses, capped per tick (a setting with a modest default). The help text
  warns that some servers may treat it as unfair. Also survives a left
  click.
- Both non-vanilla styles respect the placement restriction (L-15) when it
  is on, which turns Fast into "paint a flat floor".
- Interactive blocks (chests, doors, buttons), ordinary attacks, Fake
  Offhand and custom activation chords keep their current behavior.

#### Steps
External behavior research suggests the following hypotheses, but they are
not treated as facts until Lamium traces current Bedrock: while the button is
held, Bedrock may lock the build direction set by the first placement; blocks
hovered outside that direction may be ignored; the session may place into air
in front of the last block while bridging; and it may place as soon as a new
position is valid instead of on a fixed interval. The Java-like mode defined
above places on the currently targeted face every 4 ticks and does nothing
when no valid face is targeted. Hypothesis sources (Java mods describing
Bedrock, behavior only): modrinth.com/mod/pro-placer and
github.com/squeeglii/BridgingMod/issues/13.

1. Research: record Bedrock's held build session with the L-49 trace
   (`research_trace`, `FakeOffhandTrace.cpp`) and confirm or correct each
   point above: the direction lock (what sets it, which positions it
   accepts), placing into air along it, the timing, and what ends the
   session. Lamium already hooks `GameMode::buildBlock` and
   `SurvivalMode::buildBlock` for detached-camera interaction; test whether a
   discrete ordinary build action is sufficient for one server-authoritative
   placement, and map any required start/stop lifecycle. Do not synthesize a
   custom packet merely to imitate held input. Report before building.
2. Java-like style (Ready after step 1), with tests for the interval and the
   left-click rule.
3. Fast style with the per-tick cap; then combine both with L-15's placement
   restriction once that exists.

### L-15 Breaking and placement restrictions
Kind: Design done (discussion with the maintainer, 2026-09-28); breaking is
then Ready **(strong model)**, placement needs Research first. Replaces the
current Breaking Restriction (capture/reset keys) and the unimplemented
placement mode.
Status: planning. Nothing is built, and no step starts until the maintainer
says so.
What it is for: leveling ground and digging tunnels without breaking past a
chosen level or face, and laying floors, walls and roofs flat without
placing outside them. Placing with a chosen facing is not wanted for now.

#### Spec (decided 2026-09-28)
Anchor and lifetime
- The anchor is the block where the button press starts (breaking: the first
  block mined; placement: the first block placed). The restriction holds
  while that button stays held and ends on release. The capture and reset
  keys go away.
- Rejected blocks never end the physical hold: when the crosshair comes back
  to an allowed block, breaking or placing continues (L-36 already does this
  for breaking).
Breaking modes
- Layer: only blocks at the first block's Y.
- Height band: from the feet level up N blocks (default 2, a setting); this
  mode is anchored at the feet, not the first block. For 2-high tunnels and
  wide leveling.
- Plane: the plane of the first block's mined face.
- Line and column: straight on from the first block, or vertical through it.
Placement modes (chosen separately from breaking, so leveling and laying a
floor can run at once)
- Layer (same Y as the first placed block), plane (a wall: the vertical plane
  through it, oriented by the first clicked face), line, column.
Controls and feedback
- Breaking and placement each have a switch with a key and a "next mode"
  key (no default keys decided yet); while on, the Status element shows the
  mode.
- While the button is held, the allowed region shows as faint faces like
  Shapes, skipping the targeted block so the vanilla outline stays visible.
  A rejected block is silent: no sound, no toast.
- Later, after the basic modes work: shape-linked modes (inside a shape, on
  its surface).
Settings and ids
- The Actions category keeps one "Block Restrictions" group. The old capture
  and reset actions stay in `enum Action` (ids are saved) but are no longer
  shown or dispatched; the old breaking mode setting maps to the new list.
  Say so in the settings migration notes before changing the store.

#### Steps
1. Breaking (Ready, strong model): press-anchored lifetime, the four modes
   plus height band, the allowed-region faces, Status line, removal of the
   capture/reset flow. Pure region predicates already exist
   (RESTRICTIONS.md); extend them and their tests. In game: every mode in
   survival and creative, held-button target changes, Tool Switch together,
   world exit and dimension change.
2. Placement gate (Research): Lamium already has cancellable
   `GameMode::buildBlock` / `SurvivalMode::buildBlock` hooks in the
   detached-camera interaction guard. Determine whether that boundary can
   reject the whole placement before mutation and, separately, how to derive
   the actual destination cell/state using vanilla placement semantics rather
   than assuming clicked-block + face is always correct. Trace ordinary
   blocks, replaceable vegetation, slabs/snow, doors/beds, signs, redstone,
   waterlogged/merge cases and edge placements without touching container use
   or buckets. Prefer a vanilla placement-prediction API when available.
   RESTRICTIONS.md lists the existing trace candidates. Stop and report if no
   safe path exists.
3. Placement modes (Ready once step 2 finds a path): the four modes, anchor
   on the first placed block, faces and Status line.

### L-90 Simplified Chinese localization
Kind: Design decided, then implementation. Chosen by the maintainer
2026-10-02.
Status: built 2026-10-02 (agent-drafted text for all keys, `TranslationsZhCN.h`
with a build-time order check, docs/TRANSLATING.md). Checked in game on
`ca25c2c` (fit, baseline and behavior fine; no Latin raise needed). Open: a
native review of the wording, invited from FeixiangTMC as a PR. Hold the release that first
ships it until the review or a "draft, corrections welcome" note is decided.
Add Simplified Chinese (`zh_CN`) as Lamium's third official UI locale.
English and Japanese remain supported; Traditional Chinese is not claimed
until there is actual demand and a separately reviewed translation.
Scope:
- Translate user-facing Settings text, feature descriptions, editor/prompt
  text and toasts. A full translated README is not required for this item.
- Replace the fixed two-language `Entry { key, english, japanese }` shape
  with a translation representation that can add another locale without
  duplicating lookup logic at call sites.
- Match the game locale to `zh_CN`; unsupported locales still fall back to
  English.
- Keep every locale complete. Tests must fail when a shipped translation key
  is missing in English, Japanese or Simplified Chinese.
- The first Chinese wording may be prepared by an agent, but Minecraft/mod
  terminology corrections from native users are explicitly welcome. Add a
  short contribution note when the locale ships.
Do not generate Traditional Chinese by mechanical conversion and present it as
official support.

---

## Later / parked

- L-19 Freelook in multiplayer, riding, dimension change, controller: runtime
  checks only, no code expected.
- L-25 Detached-camera interaction options (parked, after L-18; agreed
  2026-09-24, implement only once FreeCamera proves viable). While detached,
  attack/use are fully blocked and clicks still swing the arm. Desired shape:
  per-feature detail settings, FreeCamera x {attack, use/place/interact,
  break} and Freelook x {attack, use/place/interact, break} (6 toggles,
  all default off). Aim follows the body, never the detached view; state
  this in the help text. Movement freeze (FreeCamera) and movement keep
  (Freelook) stay non-optional. Swing suppression (no arm swing while
  detached) is a separate Research item: find the swing trigger first.
- L-29 Hide the hotbar while detached (parked, after L-18). Requested
  2026-09-24: an option to hide the hotbar while FreeCamera is active
  (looking-only flight needs no hotbar). Find the vanilla hotbar render entry
  first; Freelook is out of scope unless trivially shared.
- L-86 Radar faces follow-up (after L-85; noted 2026-10-02): mobs without
  a "head" part (silverfish, tadpole) could use the whole model seen from
  the front; rotated head bones (camel, hoglin) need the bone rotation in
  the front view; check the remaining mob kinds (only about 25 of 80+ were
  seen); decide whether faces become the default once they hold up.
- L-21 Shape color picker or more colors: only if the four colors prove
  insufficient.
- Not started, not yet triaged: Mass Craft. It needs a Design pass before
  it becomes a task (Schematic became L-93). (Fast Attack/Use became L-34;
  Scroll Transfer became L-41.)
