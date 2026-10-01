# Validation status

What is known to work in game, where, and what has not been checked. One row
per feature, overwritten when a new result arrives. The evidence (builds,
hashes, traces, what the maintainer saw) is in
[VALIDATION-LOG.md](VALIDATION-LOG.md); search it by L-number instead of
reading it whole.

Baseline: Minecraft 1.26.51.01, LeviLamina Client 26.51.5, Windows x64.
"Local" is a single-player world, "BDS" a same-machine dedicated server
1.26.51.1. Results before the 26.51.5 update (commit 4a5b975) were re-checked
only briefly after it.

## Recording a result

Append an entry to the top of VALIDATION-LOG.md (date, commit, DLL SHA-256,
trace options, environment, what was done and seen, what was not covered),
then update the matching row here. Keep this file short: no hashes or traces,
only the date, environment and the open gaps. "Builds and tests pass" is not a
game result.

## Camera and view

| Feature | Last confirmed | Not yet checked |
|---|---|---|
| Zoom (L-38, L-45, L-47, L-80) | 2026-09-26, local; 2026-09-30: wheel level kept across a FreeCamera speed key and new magnification applied on `e5ee44a`; 2x floor for setting and wheel on `1cdb481`; detached FreeCamera/Freelook share its turn sensitivity on `30f0c4a` | Controllers |
| Freelook (L-39, L-48) | 2026-09-27, local; elytra flight 2026-09-23; detached Zoom sensitivity on `30f0c4a` | Multiplayer head view, riding, dimension change, controller (L-19) |
| FreeCamera, experimental (L-18, L-27, L-47) | 2026-09-26, local; 2026-09-30: five-step speed/keys on `7e72244`, sprint follow-up positive on `41b1ff6`; world position retention on `43c4211`, elytra fix/live switching/release on `d20fdf8`; Lamium views/paused flight, inventory/window movement/release on `d3f0293`; detached Zoom sensitivity on `30f0c4a` | Hold input ownership, targeting/cleanup, restart persistence, detailed input/menu/focus combinations, multiplayer, controllers; underground caves are a known limit (L-37) |
| Night Vision | 2026-09-22, local | Underwater, Nether, End |
| Hide Offhand, shield included (L-14) | 2026-09-27, local | |
| Hide effects, experimental (L-42) | 2026-09-30: independent hiding/restoration and rain sound on `7e72244`; master/rain-splash follow-up positive on `41b1ff6`; boss hiding/switches and settled fog/frozen routes on `d20fdf8`; nausea child/master/key hiding/restoration and effect/icon/preference preservation on normal `b239eb9`; underwater, lava and powder snow (fog and frost) hiding/restoration on `87f11cd` (packs removed) | Boss key and other HUD elements not reported separately; persistence, ambient layers, additional packs/modes, normal-build boss/weather checks, lifecycle/split-screen; lava with vs without Fire Resistance; pumpkin and spyglass frames parked (L-79, switches removed) |

## Inventory

| Feature | Last confirmed | Not yet checked |
|---|---|---|
| Container previews, durability readout (L-35) | 2026-09-26, local | |
| Sorting | 2026-09-22, local (inventory, large chest) | Screen closed mid-sort, latency, more container kinds |
| Inventory transfer gestures (L-41) | 2026-09-27, local (overall) | Individual edge cases, multiplayer |
| Tool Switch, hotbar (L-31) | 2026-09-25, local | |
| Tool Switch, fetch from inventory (L-69) | 2026-09-30, local; light BDS pass | Trace-disabled build, latency |
| Hand Restock (L-66) | 2026-09-30, local and BDS (trace builds): blocks, food, eggs, stew and water bucket, held use, largest-first and hotbar sources | Trace-disabled build, latency, screens/focus/dimension change during observation, 16-stack throwables other than eggs |
| Offhand totems (L-68) | 2026-09-30, local; light BDS pass | Trace-disabled build |
| Fake Offhand (L-49) | 2026-09-30, local (build 221edcb) | Multiplayer slot sync |

## Interaction

| Feature | Last confirmed | Not yet checked |
|---|---|---|
| Breaking Restriction, resume after a forbidden block (L-36) | 2026-09-30, local (mostly; see L-73 entry) | A held attack occasionally stops breaking, cause unknown; redesign L-15 pending |
| Permanent Sneak, Permanent Sprint (L-43) | 2026-09-26, local | |
| Edge Guard (L-40) | 2026-09-26, local | Servers |
| Auto Attack / Auto Use (L-34) | 2026-09-30, local (build 221edcb) | Several clicks per update landing on servers |
| Tool Protection (L-62) | 2026-09-30, local; light BDS pass: swap from inventory and hotbar, stop toast, strict child | Trace-disabled build, Unbreaking/Mending ordering, elytra replacement in flight |
| Auto Elytra, experimental (L-70) | 2026-09-30, local; light BDS pass: key, firework jump, delayed chestplate, hand-worn elytra | Trace-disabled build; no automatic glide (L-71) |

## Information and overlays

| Feature | Last confirmed | Not yet checked |
|---|---|---|
| Info HUD lines (L-04, L-05, L-56) | 2026-09-25, local | |
| Info HUD wave 1 (L-53) | 2026-09-30, local (follow-up: embedded biome names, angle labels, display formats) | |
| Target card (L-08, L-55, L-58) | 2026-09-28, local | |
| Debug View and F3 keys (L-54, L-52) | 2026-09-28, local | |
| Chunk Borders (L-10), Hitboxes (L-11, L-51) | 2026-09-27, local | Exact border shades side by side |
| Light Level Overlay (L-16) | 2026-09-26, local | |
| Shapes (L-13) | 2026-09-25, local; 2026-09-30: ten distinct type glyphs on `87f11cd`; revised glyphs and list glyphs on the `f7d49cf` trace build | Graphics modes, resource packs, performance |
| Minimap, experimental (L-60 steps 1-3) | 2026-10-01, local on `84195f0`: runtime texture, scanning, zoom, options, layout, dimension/world changes, Debug View hiding, resize/packs, clean exit; on `73729d3`: Overworld colors, lines and compass | On `0bca475`: FreeCamera following and Nether colors; on `6685cb2`: calmer cave view, stand-in blocks no longer earthy | On `42c8e10`: size setting, hold-to-enlarge, End void look. Nether holes, finer range/size steps and shimmer at wide zoom reworked after it (unchecked); multiplayer |
| Durability HUD, default off (L-61) | 2026-09-30: held-only display, three looks, offhand/armor order, gliding elytra row and layout editor on `87f11cd` (packs removed) | Leather armor's undyeable layer was missing (also in container previews); chunked icon pass in `e987861` unchecked. Elytra row removed (confirmed on the `f7d49cf` trace build); flight time parked |

## Settings, UI and distribution

| Feature | Last confirmed | Not yet checked |
|---|---|---|
| Settings screen, search, layout B (L-50, L-52, L-81) | 2026-09-27, local; 2026-09-30: edits finish with the frame, range warning cleared on moving (`1cdb481`) | Individual binding edits |
| Dedicated Hotkeys/Shapes/HUD openers (L-02 follow-up) | Source and tests only (2026-09-28) | In game |
| Toggle toasts; message toast | 2026-09-30, local | |
| Managed install and update keeping settings (L-65) | 2026-09-28, LeviLauncher test instance | |
