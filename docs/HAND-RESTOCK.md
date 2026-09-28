# Hand Restock implementation work

Hand Restock remains experimental, Off and Unbound by default. Consumption
observation and hotbar reserve selection succeeded in local survival: after a
one-item stack is consumed, Lamium selects the first compatible reserve in
another hotbar slot. Main-inventory and offhand replenishment have not
succeeded; HUD swap/count-transfer experiments produced no captured inventory
request, and count transfer explicitly returned false. This does not establish
that main-inventory replenishment is impossible: a bounded follow-up may test
the game's ordinary server-authoritative inventory transaction path while
retaining Lamium's existing correlation and cancellation rules.
See [VALIDATION.md](VALIDATION.md) for build hashes and runtime observations.

## Intended behavior

When the selected main-hand stack is consumed, select a compatible reserve
from another hotbar slot through the proven `selectSlot` API (no stacks are
rewritten, no packets forged, no retry loop). Main-inventory replenishment is
an open issue: HUD-controller transfers through `ContainerManagerController`
(place and take both verified false 2026-09-27) have no supported path.
A future research spike may submit one ordinary inventory swap only after the
existing use/depletion correlation has identified a stable source and
destination, then wait for authoritative inventory state before declaring
success. Submission alone is not success, and a mismatch, correction, timeout
or unrelated mutation cancels without retrying. Hypothesis source: Stipuleroo
(GPL-3.0, reference only; PROVENANCE.md group 3) restocks from the main
inventory on 26.51 with an ordinary inventory transaction. Do not open its
source while writing this.
Bowls, buckets and other consumption replacements remain in the selected slot.

The maintainer also wants an offhand extension if the client exposes a safe
vanilla-backed path, especially automatically replacing a consumed Totem of
Undying from the main inventory. Treat this as a distinct observation/transfer
path until proven otherwise: offhand slot mapping, consumption timing and
controller permissions must be validated independently from the main-hand
adapter. Do not emulate success by writing the stack locally or forging an
inventory packet.

## Current consumption observation

The native adapter snapshots around main-hand GameMode use callbacks and
Player::completeUsingItem. It checks local player identity, dimension, selected
hotbar slot, gameplay input, HUD ownership and agreement between all 36 HUD
slots and player inventory. Creative and spectator players are excluded.

A successful callback closes request capture. Tracked use requests require an
Accepted response. The observed egg path instead sends a complex
ItemUseTransaction after the callback, with no item-stack request batch.
For that Untracked path, the adapter requires a matching main-hand Use/Place
transaction for the selected slot, then observes depletion for at most one
second. A submitted use is not server acknowledgement. The deadline cancels
observation; elapsed time never authorizes replenishment.

The held stack must change from one item to empty. All other inventory slots
must remain unchanged. A manual drop, unmatched transaction, selection/context
change, focus loss, world exit or inventory mismatch cancels observation.
The first unlocked compatible hotbar stack in slots 0–8 is selected using
vanilla item equivalence, including components. There is no fallback to another
reserve after unrelated inventory mutation. Main-inventory slots 9–35 are only
examined by the retained, unsupported transfer planner; they are never selected
by the shipped hotbar fallback.

## Replenishment: hotbar auto-select

After depletion the adapter selects a compatible hotbar reserve through
`PlayerInventory::selectSlot`, the same proven API Tool Switch uses, and
confirms the selection moved. No stacks are rewritten and no transfer token
is needed for the selection itself. Rejected, Untracked or TimedOut use
results still stop without retrying.

## Retired transfer approach and open issue

The HUD exposes hotbar_items with 36 slots, whose occupied entries match the
player inventory. Read access does not establish transfer capability: the
adapter reached plan-ready, but HUD `handlePlaceAmount` returned false and
generated no request (replacing the earlier unsuccessful `handleSwap`).
2026-09-27 trace: both controllers report `closed=false client=true
simulation=false`, so the simulation flag does not explain the failure;
`handleTakeAmount` also returns false under the same token. HUD-controller
transfers through `ContainerManagerController` have no supported path without
a screen, so that specific approach stays retired. Main-inventory
replenishment is BACKLOG L-66 (a bounded experiment, vanilla path first);
do not force-enable permissions, reuse a closed screen controller, rewrite
stacks locally, or treat a sent transaction as confirmation. Totem consumption
in the offhand fires no GameMode use/use-on/complete callback (passive damage
path), so offhand restock needs a separate consumption observer.

## L-66 research spike (trace builds only)

With `restock_trace` the depleted operation arms a bounded spike when the
reserve is inventory-only (no hotbar reserve, so the proven `selectSlot`
fallback cannot run). At most two sends happen per depletion, A first:

- Path A (`spike-A-*`): the vanilla HUD move for an empty destination,
  `handlePlaceAmount(SlotData("hotbar_items", source), count,
  SlotData("hotbar_items", destination))`, tracked through the existing
  response barrier like Sort transfers.
- Path B (`spike-B-*`): only when A moved nothing and the inventory is
  byte-identical to the pre-A snapshot, the vanilla HUD swap for the same
  slots (`handleSwap`). A client-built transaction was the candidate 1b,
  but `ItemStackRequestScope::addRequestAction` and its destructor have no
  linkable SDK export, so it is not buildable here; both spike paths stay
  on the vanilla controller virtuals that Sort transfers already use.

Labels are fixed strings with numeric values only (no item contents,
request IDs or identities): `spike-armed`, `spike-A-return`,
`spike-A-result`, `spike-A-success`, `spike-A-mismatch`, `spike-B-scope`,
`spike-B-return`, `spike-B-result`, `spike-B-success`, `spike-B-mismatch`,
`spike-stale`, plus `Hand Restock L-66A/B moved inventory reserve` on
success. Success needs an Accepted response **and** a resnapshot showing
the whole reserve stack in the selected slot, an empty source and no other
change; anything else cancels without retrying and fails open. Normal
builds keep the old `no transfer path` stop.

Test setup (local world first): Hand Restock on, one single-item consumable
selected (for example one egg), exactly one compatible stack in the main
inventory (slots 9-35), no compatible hotbar reserve. Reading the log:
`L-66A moved` means the vanilla path works; `A-*` refusal followed by
`L-66B moved` means only the client-built path works; `moved nothing`
means neither does.

Result 2026-09-29 (trace DLL `2337CE...8908F5`): one egg selected, a
main-inventory reserve, throw. `spike-armed 10`, then `spike-A-return 0`
with an empty request batch, then `spike-B-return 0` / `spike-B-refused` /
`spike moved nothing`. Both vanilla HUD verbs refuse synchronously and
create no request, so the HUD controller issues no move either way. A
later throw with a hotbar reserve selected it (`selected hotbar reserve`),
confirming the feature was on.

## Diagnostics and validation

The opt-in restock_trace build records bounded fixed labels and numeric values:
use stages and complex sends, legacy slot/content updates after vanilla applies
them, capture boundaries, new-request counts and response counts. It logs no
item contents, request IDs, player identities or world identifiers. Legacy
updates and response counts are observations, not correlated use acceptance.
It also logs the HUD controller's transfer context at use time and the
container screen controller's context when a screen opens (closed, client-side
and simulation flags, `Restock transfer context`), to compare the failing HUD
path with the working screen path. Use callbacks now record the hand value,
so offhand (totem) consumption timing can be mapped separately.

Local egg tests establish callback-before-depletion ordering, complex send
ordering and successful depletion planning. Hotbar reserve selection was
verified in game on 2026-09-27 (DLL `1f1f7816`); an inventory-only reserve
correctly stopped with no transfer path. Block/food/firework behavior,
manual-drop cancellation, server rejection/correction, multiplayer and
disconnect handling remain unverified.

Planner tests cover unchanged compatible reserves, interference, replacement
items, locking and context/selection validity. Response ownership tests cover
contention, stale cancellation, unrelated responses and restart. Existing Sort
runtime checks establish its screen-based transfer path, not the HUD path.
Compilation and these tests cannot substitute for native validation.
