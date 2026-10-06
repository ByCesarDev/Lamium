# Settings and keymap roles (L-52, decided 2026-09-27; rules revised L-83)

## Rules (decided 2026-10-01, L-83, option X)

These rules govern every feature, current and future. They refine the L-52
layout below; where they differ, these win.

| Row | Examples | Key on the parent row | Keys on child rows |
|---|---|---|---|
| 1. Feature with a saved switch | Night Vision, Minimap, Sorting, Waypoints | Toggles that switch. **Always provided**, unbound by default | The feature's commands (row 4) |
| 2. Session feature (no saved state) | Zoom, Freelook, FreeCamera, permanent sneak | Starts/stops it (hold or toggle, per its Activation) | Commands such as speed |
| 3. Named command without a switch | Settings screen, Cave view | Runs that command | none |
| 4. Command inside a feature | Sort now, Add here, open its screen, cycle a mode, hold-to-do | never on the parent | one row per command |
| 5. Child switch | Hide rain and snow, Breaking restriction, Show in the world | none | on the same row, only when switching during play is useful |
| 6. Heading only | Map text, Block restrictions | none | none |

- A key that opens a screen belongs to the feature that owns the screen
  (row 4). Screens owned by no feature (Hotkeys, HUD layout) stay under
  General > Settings screen. The sidebar's pinned items open every screen
  with the mouse.
- Default keys: none, except where a convention exists or the action is
  used constantly (L settings, C zoom, R sort, F3 / F3+G / F3+B, M world
  map, right button fake offhand, F swap with offhand (2026-10-06, Java's
  key; Bedrock has none)). Record the date when one is added.
- A toggle key shows the toggle toast, as all toggles do now.
- New action ids are appended (never reordered); moving an action to
  another row changes only presentation, and existing bindings stay.

The maintainer selected layout B in
[the comparison demo](demos/settings-keymap-review.html). A switch need not
have a key merely for symmetry. A key on a parent row must control that row's
state, or execute the command named by a row without a switch. Immediate
commands belong on their own child rows. Child settings receive keys only when
their operation is useful during play. All existing bindings, action IDs,
settings IDs and saved values were preserved by the layout review. The later
default-key decision below changes four defaults without rewriting overrides.

The settings screen remains the place to find each feature's switch and
options. The Hotkeys view lists every action, including unbound commands.
Future Schematic and Map features should first decide whether the parent is
an enabled feature, a command that opens a view, or a keyless group. Do not
derive the parent key from action registration order.

## Current feature audit

Superseded in part by the rules above (2026-10-01): container previews,
durability numbers, sorting, hide effects, durability HUD, automation
status, radar, waypoints and world map gained unbound toggle keys on their
parent rows; "Add a waypoint here" and "Open the world map" (M) are child
rows; "Open the Shapes screen" sits under Shape drawing; automation status
moved to Info & overlays. The table below records the 2026-09-27 state.

"Unbound" means a bindable action with no default chord; "none" means no
binding on that row. Child options without keys are summarized by purpose.
The parent row's visible switch remains unchanged except where noted.

| Section | Parent row | Parent state and key | Child settings and keyed actions |
|---|---|---|---|
| Camera | Zoom | Session switch; C | Activation, magnification, magnification display and layout |
| Camera | Freelook | Session switch; Unbound | Activation, starting perspective |
| Camera | FreeCamera | Session switch; Unbound | Activation |
| Camera | NightVision | Saved switch; Unbound | None |
| Camera | Hide offhand | Saved switch; Unbound | None |
| Inventory | Container previews | Saved switch; none | Shulker and Bundle display options |
| Inventory | Durability | Saved switch; none | None; numeric tooltip for hovered damageable items even when previews are off |
| Inventory | Inventory sorting | Saved switch; none | **Sort now** (R), sort storage containers switch |
| Inventory | Inventory transfer | Saved switch; Unbound | Four gesture switches, no separate gesture keys |
| Inventory | Tool Switch | Saved switch; Unbound | None |
| Inventory | Weapon Switch | Saved switch; Unbound | Fetch from inventory (off) |
| Inventory | Hand Restock | Saved switch; Unbound | Restock from hotbar (off) |
| Inventory | Fake Offhand | Saved switch; Unbound | Activation (right mouse button), target slot |
| Inventory | Swap with offhand (L-94) | Saved switch, default on; Unbound | **Swap with offhand** (F) |
| Actions | Block Restrictions | Group; none | Breaking restriction switch and key (Unbound), breaking mode and cycle key (Unbound), placement mode, capture/reset commands (Unbound); L-15 may replace these later |
| Actions | Permanent Sneak | Session switch; Unbound | None |
| Actions | Permanent Sprint | Session switch; Unbound | None |
| Actions | Edge Guard | Saved switch; Unbound | None |
| Actions | Auto Attack | Session switch; Unbound | Mode cycle and held-only toggle (both Unbound), interval and click rate |
| Actions | Auto Use | Session switch; Unbound | Mode cycle and held-only toggle (both Unbound), interval and click rate |
| HUD & overlays | Info HUD | Saved switch; Unbound | Info-line switches, order and HUD layout |
| HUD & overlays | Target | Saved switch; Unbound | Target details, range and HUD layout |
| HUD & overlays | Debug View | Saved switch; F3 | None |
| HUD & overlays | Chunk Borders | Saved switch; F3+G | None |
| HUD & overlays | Hitboxes | Saved switch; F3+B | Distance |
| HUD & overlays | Light Overlay | Saved switch; Unbound | Value, range and facing |
| HUD & overlays | Shapes | Saved switch; Unbound | Shapes view opener remains under General, as previously decided |
| General | Automation status | Saved switch; none | HUD layout |
| General | Settings screen | Named opener command; L | Toggle toasts, animations, Open Hotkeys, Open Shapes and Open HUD layout (all Unbound), toast layout |

## Changes in this review

- The Sort binding stays on `R` and keeps executing a sort, but appears on the
  first child row under the keyless Inventory sorting parent. The parent
  switch still enables or disables sorting.
- The Breaking Restriction binding appears next to the Breaking restriction
  child switch, rather than on the Block Restrictions group heading. The
  current breaking behavior is unchanged; L-15 owns its redesign.
- All other parent and child bindings stay on their existing rows. No new
  bindings or settings fields are introduced.

## Default-key decision (2026-09-27)

The maintainer chose Java-style debug defaults: F3 for Debug View, F3+B for
Hitboxes and F3+G for Chunk Borders. NightVision loses its arbitrary J default;
N is Minecraft Bedrock's notifications key. Other defaults stay as they were.
F3 alone activates on release because it leads longer chords; completing B or
G suppresses that F3 action. Explicit user bindings, including an empty chord,
take precedence over the changed defaults. No saved binding or action ID is
rewritten. These defaults still need in-game validation.

The maintainer confirmed the layout in game on 2026-09-27. Individual
binding-editing and existing-custom-binding cases were not separately
confirmed.
