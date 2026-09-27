# Settings and keymap roles (L-52, decided 2026-09-27)

The maintainer selected layout B in
[the comparison demo](demos/settings-keymap-review.html). A switch need not
have a key merely for symmetry. A key on a parent row must control that row's
state, or execute the command named by a row without a switch. Immediate
commands belong on their own child rows. Child settings receive keys only when
their operation is useful during play. All existing bindings, action IDs,
settings IDs, saved values and default keys are preserved by this review.

The settings screen remains the place to find each feature's switch and
options. The Hotkeys view lists every action, including unbound commands.
Future Schematic and Map features should first decide whether the parent is
an enabled feature, a command that opens a view, or a keyless group. Do not
derive the parent key from action registration order.

## Current feature audit

"Unbound" means a bindable action with no default chord; "none" means no
binding on that row. Child options without keys are summarized by purpose.
The parent row's visible switch remains unchanged except where noted.

| Section | Parent row | Parent state and key | Child settings and keyed actions |
|---|---|---|---|
| Camera | Zoom | Session switch; C | Activation, magnification, magnification display and layout |
| Camera | Freelook | Session switch; Unbound | Activation, starting perspective |
| Camera | FreeCamera | Session switch; Unbound | Activation |
| Camera | NightVision | Saved switch; J | None |
| Camera | Hide offhand | Saved switch; Unbound | None |
| Inventory | Container previews | Saved switch; none | Shulker and Bundle display options |
| Inventory | Durability | Saved switch; none | None; numeric tooltip for hovered damageable items even when previews are off |
| Inventory | Inventory sorting | Saved switch; none | **Sort now** (R), sort storage containers switch |
| Inventory | Inventory transfer | Saved switch; Unbound | Four gesture switches, no separate gesture keys |
| Inventory | Tool Switch | Saved switch; Unbound | None |
| Inventory | Hand Restock | Saved switch; Unbound | None |
| Inventory | Fake Offhand | Saved switch; Unbound | Activation (right mouse button), target slot |
| Actions | Block Restrictions | Group; none | Breaking restriction switch and key (Unbound), breaking mode and cycle key (Unbound), placement mode, capture/reset commands (Unbound); L-15 may replace these later |
| Actions | Permanent Sneak | Session switch; Unbound | None |
| Actions | Permanent Sprint | Session switch; Unbound | None |
| Actions | Edge Guard | Saved switch; Unbound | None |
| Actions | Auto Attack | Session switch; Unbound | Mode cycle and held-only toggle (both Unbound), interval and click rate |
| Actions | Auto Use | Session switch; Unbound | Mode cycle and held-only toggle (both Unbound), interval and click rate |
| HUD & overlays | Info HUD | Saved switch; Unbound | Info-line switches, order and HUD layout |
| HUD & overlays | Target | Saved switch; Unbound | Target details, range and HUD layout |
| HUD & overlays | Debug View | Saved switch; Unbound | None |
| HUD & overlays | Chunk Borders | Saved switch; Unbound | None |
| HUD & overlays | Hitboxes | Saved switch; Unbound | Distance |
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

The maintainer confirmed the layout in game on 2026-09-27. Individual
binding-editing and existing-custom-binding cases were not separately
confirmed.
