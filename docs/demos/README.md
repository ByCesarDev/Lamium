# Design demos

Standalone HTML mockups used to agree on UI before implementation. Open them
in a browser (they need network access only for the Google Fonts stylesheet).
Text in the demos is Japanese because they were reviewed in Japanese.

| Demo | Status | Notes |
|---|---|---|
| [settings.html](settings.html) | Implemented (layout A) | Sidebar + table, switches, key caps. The native screen is the source of truth where they differ. |
| [shapes.html](shapes.html) | Implemented | Draft → create flow, dock button. "Lines + faces" was dropped after review. |
| [hud.html](hud.html) | Implemented, being reworked | HUD elements, layout editing, target card, toggle toast. BACKLOG L-02 to L-04, L-08. |
| [hud-editor.html](hud-editor.html) | Decided, being implemented | Rework after the first editor build: three editing models, three card styles, target card with icons and bars. |
| [hotkey-conflicts.html](hotkey-conflicts.html) | Implemented | Warning style for shared/overlapping bindings in every key cell and a hover tooltip listing every related binding (L-32 follow-up). |
| [light-overlay.html](light-overlay.html) | Implemented | Light overlay redesign (L-16): what to show, digit orientation and weight, spawn coloring, value and range. |
| [settings-reset.html](settings-reset.html) | Reviewed, not adopted | Resetting settings to defaults (L-46): per-row ↺, marks + right-click/Backspace, or a "changed settings" view. Per-row and per-feature resets were judged excessive; see BACKLOG L-46. |
| [settings-keymap-review.html](settings-keymap-review.html) | B implemented, game confirmed | L-52: three placements for feature toggles, commands, bindings and Durability. B was selected for implementation. |
| [debug-view.html](debug-view.html) | A adopted, implemented | L-54: three layouts compared (A Java-style split with a client/PC column right, B split with the look-at target right, C one column) with a game-standard / Java-F3 label switch. A was chosen; the shipped panel is fixed to the screen edges and is not a layout-editor element. |
| [durability-hud.html](durability-hud.html) | Decided (B default) | L-61: held-item durability HUD. B (icon + bar + number) is the default look, A and C are options; bottom left; vanilla bar colors; no flash; offhand and armor options; the elytra row while gliding. |
| [minimap.html](minimap.html) | Decided | L-60: minimap look after the step-0 discussion - terrain colors and shading, player arrow, radar dot colors, waypoint markers on the map and in the world, the death marker, cave and Nether views. Terrain is generated, not real. |
| [worldmap.html](worldmap.html) | Decided (2026-10-01), being implemented | L-60 world map: full-screen screen with bars, drag/zoom, right-click waypoint menu, progressive fill from the region cache, Nether layers, settings rows. Terrain is generated. |
| [settings-review.html](settings-review.html) | Under review (2026-10-01) | L-83: the settings tree before and after (HUD and world display categories, screen openers with their features, toggle keys for every saved switch), the keymap rules, one swatch-row color chooser, a shared waypoint editor. |
| [worldmap-review.html](worldmap-review.html) | Decided (2026-10-01): B, all recommendations | L-60 world map after first use: top bar variants measured at UI 75/100/125 %, a sidebar entry and the way back from the Waypoints screen, a waypoint side panel on the map. |
| [waypoints.html](waypoints.html) | Decided (2026-10-01) | L-60 step 5: the add prompt, the Waypoints screen (built like Shapes, death point on top) and the settings rows. Marker looks were decided in minimap.html. |

Rules for agents:

- A demo shows intent and structure, not exact pixels. Build with the tokens
  and widgets in `src/ui/Widgets.h` and the sizes in docs/DESIGN.md.
- The box at the bottom of a demo lists either open questions ("確認したいこと")
  or, once agreed, the decisions ("決定事項"). docs/DESIGN.md is authoritative.
- When a new demo is made, add it here with its status.
