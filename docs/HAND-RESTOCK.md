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

## L-66 negative spike result

The 2026-09-29 trace repeated two calls on the already-retired HUD
controller path: `handlePlaceAmount`, then `handleSwap`. Both returned false
synchronously and created no request. The second call was described during
the spike as a client-built transaction, but it was another controller verb;
no client-built transaction was sent. The redundant spike code has been
removed. Its build hash, setup and observed log lines remain in
[VALIDATION.md](VALIDATION.md).

This result rules out only those HUD-controller calls. It does not rule out a
different no-screen vanilla API or a separately constructed ordinary
inventory transaction. Absence of a linkable `ItemStackRequestScope` export
is an SDK observation, not proof that every client-backed path is impossible.
Automatically opening and closing the inventory screen is not an acceptable
substitute for seamless hand restock unless the maintainer explicitly chooses
that user-visible behavior.

## L-66 client-built transaction spike result

2026-09-29 follow-up on the ordinary-inventory-transaction hypothesis
(branch `spike/l66-client-inventory-transaction`). The transaction is
buildable with SDK headers only: two balanced `InventoryAction`s on
`ContainerID::Inventory` in a `ComplexInventoryTransaction`, handed to
`LocalPlayer::sendInventoryTransaction`. In-game on the tested local
single-player / integrated-server world the server executed the move (the
stacks appeared in the hotbar after world re-entry), but the live client never
applied it and the inventory screen refused further item moves until
re-entry. An earlier revision that sent immediately also raced the queued
legacy use: the server executed the move before the use, which then consumed
from the moved stack. See [VALIDATION.md](VALIDATION.md) for hashes and trace
lines.

A packet-only move was not usable on that tested world; the inventory
authority model was not directly confirmed. The remaining candidates
(client-side local application like a vanilla legacy caller, the unexported
client request scope, or a dedicated server) change product behavior or need
unavailable exports and are a maintainer decision.

### Vanilla flow observation (2026-09-29)

A research trace of a manual inventory-screen move (`LegacyFlowTrace`, see
[VALIDATION.md](VALIDATION.md)) shows the vanilla client applies the change
locally (`Inventory::$setItem` and the nested `$setItemWithForceBalance`) and
records it (`InventoryTransactionManager::addAction`) before submitting it
through the item-stack request path. `LocalPlayer::$sendInventoryTransaction`
is never called, `allowInventoryTransactionManager()` returns false and the
legacy request id never changes. The submission half of that specific flow is
the SDK `MCNAPI` surface (`ItemStackNetManagerClient` /
`ItemStackRequestScope`); following that flow with exported APIs alone was
not possible.

The middle-click block pick, vanilla's no-screen inventory -> hand case, was
traced next. `pickBlock` and `selectSlot` are the only client calls; the
observed behavior is consistent with server-mediated handling (the outgoing
pick request was not traced directly) and the client applied a legacy
full-inventory content update. No client-side transaction, request or slot
swap was involved. `pickBlock` is exported, but its input is the looked-at
block, not a chosen stack.

### Predicted-move probe (2026-09-29)

A bounded follow-up made the client prediction and the server transaction one
operation: `Inventory::$setItem` applies the move locally and the setter
itself records it through the client's own `InventoryTransactionManager`,
which sends the legacy transaction. A legacy request scope
(`_tryBeginClientLegacyTransactionRequest(Player*)`) around the setters
populates a legacy request id and one set-item group on the packet. Two
refinement runs on the tested local single-player / integrated-server world
passed the maintainer's in-game criteria (immediate and re-entered state,
usable stack, inventory gestures, no duplication/loss/ghost) with exactly one
send and one action pair.

A dedicated-server check (BDS 1.26.51.1, separate server process, probe build
`0168ECF5...`) repeated the same bounded probe twice: both runs sent exactly
one transaction with one action pair and a non-zero legacy request id, and no
correction followed. The maintainer confirmed immediate usability, GUI
operation, matching state after re-joining, and no duplication, loss, ghost,
rollback or inventory lock. The packet's `LegacySetItemSlots` carry only the
destination slot; the emptied source slot is not registered, so broadcast
coverage for other clients stays unverified. Other containers and the offhand
remain unverified. See [VALIDATION.md](VALIDATION.md) for the trace evidence.
The probe stays trace-build-only and is not integrated into the feature, which
keeps its current hotbar-select behavior.

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
