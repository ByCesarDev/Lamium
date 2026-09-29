# Hand Restock

L-66 production implementation on codex/l66-hand-restock. Experimental,
Off and Unbound by default. Build and pure tests do not establish in-game
behavior. Historical probes and their exact build hashes are preserved in
[VALIDATION.md](VALIDATION.md); this integration needs a new playtest.
Product scope is authoritative in [BACKLOG.md](BACKLOG.md), L-66.

## Behavior

- Refill the selected main-hand slot after an observed consumption. Keep the
  selected index fixed; there is no hotbar-selection fallback.
- Top up when the same item falls to six or fewer items (provisional internal
  threshold, capped below the item's maximum). Depletion uses the same planner.
  One source per consumption, moving only what fits. A 16-stack respects its
  own maximum; a maximum-one item cannot be topped up before depletion.
- Choose the first unlocked compatible main-inventory stack (slots 9-35).
  The saved child setting "Restock from hotbar" is off by default; when on,
  slots 0-8 are fallback sources after main inventory, excluding the selected
  slot. Compatibility uses vanilla matching, including components.
- Exchange a recognized remainder with a compatible reserve: return the held
  empty bucket, bowl or bottle to the source slot and put the reserve in hand.
  No spare slot is needed and no other remainder stacks are consolidated.
  Initial recognized pairs: water/lava/milk/powder-snow bucket -> bucket;
  mushroom/rabbit/beetroot/suspicious stew -> bowl; potion/honey bottle ->
  glass bottle. Only a last single item becoming one remainder qualifies.
  Unknown/add-on transformations and changes to other slots fail open.
- No reserve means no move. Ordinary drops, manual moves, unrelated inventory
  changes, context/selection changes and failed uses must not trigger refill.
- No per-refill toast. No extra key binding or public threshold control.
- Tool-break replacement and all damageable held items are excluded. Offhand
  and passive main-hand/offhand totem consumption need separate future work.

## Consumption and transfer

RestockPlan.h owns the pure two-snapshot planner and revalidates all 36 slots
before moving. RestockUse.h owns send correlation. The native adapter retains
owned item copies, a weak HUD controller, player runtime ID, dimension and a
context generation, never a player pointer between frames. HUD and player
inventory must agree before a move.

GameMode use/use-on callbacks capture a baseline. Timed food/drink use is
observed through startUsingItem and completeUsingItem; the legacy path requires
both its start-use and release sends plus completion. Request-backed uses
require an Accepted response. Callback success or a send alone never proves
consumption: the held snapshot must also decrease by exactly one, or turn
into its recognized remainder, with every other slot unchanged.

A placement may emit Place and one secondary Use in the same client tick.
The secondary GameMode callback is preserved only if it fails without another
inventory change. Repeated verbs, a later-tick send, a different hand/slot,
another successful callback or another mutation cancels the old observation.
New uses get new baselines; they do not reuse an old transfer plan. Ambiguous
multiple uses before observation may skip replenishment instead of guessing.

After use observation, the adapter obtains a fresh transfer token, resnapshots,
and checks the client's transaction and request managers are available. Two
Inventory setters apply the predicted state under the SDK's client legacy
request scope. They record the transaction themselves; no duplicate addAction,
manual packet send, probe-send flag, custom legacy slot list, or auto-opened
inventory screen remains. A pending manager transaction is flushed once; an
already-completed manager is not flushed again. Actual source/destination
ItemStacks are copied so their metadata is retained.

The adapter checks the resulting client snapshot immediately and on the next
tick. These checks are explicitly prediction checks, not authoritative success.
Legacy prediction has no established per-operation server acknowledgement;
absence of a correction is not confirmation. Vanilla applies server updates;
updates during prediction cancel observation, and no old snapshot is written
back. There is no retry of an interrupted, rejected or ambiguous operation.
Exceptions after mutation stop restocking in that context until reset.

The spike's unconditional 250 ms wait is not retained: it originated in the
packet-only experiment and was not an acknowledgement mechanism. Production
moves are scheduled on the next client tick after correlated consumption is
visible. This scheduling differs from the successful bounded probe and needs
explicit local-world and BDS validation, especially continuous placement,
refilling at zero, latency, and use during refill. It is not yet established
that client prediction plus legacy scope orders every consumption/transfer
correctly on the server. The feature remains default off and Experimental.

Observation deadlines only cancel: one second after an ordinary/completed use;
timed use allows its declared duration (bounded to 60 seconds) plus one second.
They never authorize a transfer. Screens/input loss, focus loss, disable,
selection changes, dimension/world changes and manual drops invalidate pending
work. Changes to the source-policy setting also invalidate it.

## Verification

Pure tests cover threshold/depletion/remainder plans, 1/16/64-stack limits,
component-kind mismatch, locked items, source priority, opt-in hotbar sources,
full inventories, all-slot revalidation, context changes, unrelated mutations,
placement dedup and timed-use evidence. Settings tests cover old-file defaults,
disk round trips, translations and the child row.

In-game acceptance checklist (new normal build, local world then BDS):
1. Enable Hand Restock; place continuously from seven blocks with a 32-stack
   reserve. At six remaining expect 38 in the same selected slot. Repeat with
   a 64-stack reserve: expect 64 in hand and six at source.
2. Repeat with food and 16-stack throwables, including a final single item.
   Food must refill only after completion, never after interrupted eating.
3. Verify main inventory wins over hotbar reserves; hotbar-only reserves stay
   put by default and supply the selected slot only when the child option is on.
4. Consume stew/potion/milk and pour water: a matching reserve replaces the
   remainder, which occupies the old reserve slot. With no reserve, leave the
   remainder in hand. Try a full inventory and existing remainder stacks.
5. Drop items, manually rearrange them, switch slots, open screens, lose focus,
   disable the feature, change dimension and leave the world during observation:
   no delayed or unrelated transfer, and vanilla actions remain usable.
6. Continue using at the refill moment, test latency/corrections, move source,
   destination and unrelated stacks in the GUI, then re-join. Expected server
   agreement, no duplication/loss/ghosts/rollback/lock, and one refill per use.
   A quiet log alone does not meet these criteria.

Optional restock_trace logs bounded fixed labels and numeric values; it does
not enable another transfer path. partial_restock_trace and RestockSpike were
removed. The separate read-only consumption/legacy-flow research traces remain
opt-in and are off in normal builds. No current production runtime result has
yet been added to VALIDATION.md.
