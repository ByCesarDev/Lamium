# Fake Offhand

L-49 implements block placement. L-95 researches additional item use.
Decisions and open choices are in [BACKLOG.md](BACKLOG.md); confirmed runtime
coverage is in [VALIDATION.md](VALIDATION.md).

## Extension scope (chosen 2026-10-06)

The maintainer chose broad secondary-hand capability, rather than one item
category. Keep one switch, one target slot and the existing activation binding.
Implement support in validated steps without narrowing the overall goal.

| Capability | Examples | Research boundary |
|---|---|---|
| Placement | Blocks, torches, seeds | Existing placement plus target-sensitive use |
| Instant use | Buckets, bottles, fire starters, tools, throwables, fireworks, fishing rod | Air/block use, replacement stacks and cooldowns |
| Timed consumption | Food, potions, milk, stews | Start/progress/finish/cancel; correct source slot and returned container |
| Charged/continuous use | Bow, crossbow, trident, spyglass, brush | Charge/release, retained use slot, durability and cancellation |
| Entity use | Feed, tame, shear, milk, lead, name tag, saddle | Target interaction vs selected-hand use; item source and result |
| Holding effects | Shield, map, arrows, totem, Mending and equipment modifiers | Real equipped-hand state; cannot infer support from a use callback |

Target priority is ordinary target interaction and applicable primary-hand
use, then secondary use when the primary hand passes. Distinguish a pass
from failure and mutation; never retry a second use simply because a bool
result is false. Bedrock result types and target-sensitive ordering need
observation before implementing the fallback. L-49 remains unchanged during
this diagnostic step.

Instant uses restore selection in the call. Timed uses must complete or cancel
against their actual source slot and restore afterward, without overwriting
a manual selection. Investigate retaining the primary selection while using
the secondary slot before adopting a visible selection for the whole hold.
An ordinary attack must never accidentally use the borrowed item. Simultaneous
primary-hand attack and secondary use is a separate lifecycle check.

Passive effects remain in scope for feasibility research, not promised parity:
an item in a hotbar slot is not server-authoritatively held in an offhand.
Existing real-offhand support (L-68, L-75, L-94) remains available. Do not
implement client-only health, protection or enchantment effects as if the
server accepted them. Record unsupported cases and the reason per capability.

Native Bedrock differences require their own checks; for example, shielding
uses sneak rather than ordinary use (official platform behavior reference:
[Taking Inventory: Shield](https://www.minecraft.net/en-us/article/taking-inventory--shield)).
No external mod implementation is used.

## Existing contract

The feature borrows a configured hotbar slot, default slot 9, while its
activation binding is held. The default binding is right click. It performs
no inventory transfer and does not use the real offhand.

`FakeOffhand.cpp` hooks `ClientInstance::_tickBuildAction`. Its pure predicate
in `FakeOffhandPlan.h` requires a block item and a block hit. Interactive
blocks preserve ordinary interaction unless sneaking. Air, entities and
non-block target items keep the selected hand. Selection is restored inside
each build call, including unwinding, only if the selected slot is still the
borrowed slot. Other bindings replay the captured vanilla use edges.

## L-95 static review (2026-10-06)

Inspected the installed LeviLamina Client SDK 26.51.5 headers and Lamium's
own adapters; no reference-only mod source was used. These declarations
identify research boundaries, not confirmed runtime ordering:

- `GameMode::useItem` and `useItemOn` cover item use in air and on blocks;
  `interact` is a separate entity boundary. `releaseUsingItem` is available.
- `useItem` and `interact` return bool; `InteractionResult` exposes only
  success and swing bits. There is no declared pass/fail distinction there.
  A safe fallback needs additional evidence about the vanilla route and
  whether an earlier callback changed state.
- `Player::startUsingItem`, `completeUsingItem`, `releaseUsingItem` and
  `stopUsingItem` expose the timed-use lifecycle.
- `Player::mItemInUse` contains an owned item and `PlayerInventorySlotData`
  slot record. Observe those values to establish which slot vanilla follows
  after temporary selection is restored; never retain a player pointer.
- `LocalPlayer::mSentSelectedSlot` tracks reported equipment selection.
  Weapon Switch already reports a new selection before attacking. Additional
  use must establish its own reporting order; copying the attack solution
  does not prove food or charged-item synchronization.
- Hand Restock already observes ordinary and timed uses. Its timed-completion
  guard documents a completion callback immediately after a new use begins.
  Trace both features together; do not interpret every completion callback as
  consumption or attempt to restock from an unconfirmed result.

Removing only the block-item and block-hit guards would allow additional
calls but would leave their timed-use, release and server-selection contracts
unverified. L-49 also records that retaining selection throughout every hold
interfered with main-hand use and was reverted. An extension must distinguish
instant, placement and timed-use ownership.

## Research and verification

`offhand_trace` adds read-only hooks for build ticks, base and survival
air/block/entity use, and player start/complete/stop/release. It logs selected
slot, last reported slot, selected/use/target item identities and counts,
the use slot/container, callback results, duration arguments and timestamps.
Slot numbers in these diagnostics are zero-based. Results are observations,
not authoritative confirmation. Build ticks log only state changes; each
stage group has a 256-line lifetime budget. Restart for another category if
the budget is exhausted. The older L-49 and L-66 diagnostics are unchanged.

First baseline session, with Hand Restock and Auto Use off:
1. Fake Offhand off: select food, eat once, then begin eating and release
   early. Repeat with a bow, charging and firing once; select a bucket and
   place/collect water; feed an animal with an applicable item.
2. During food/bow use, manually select another slot and try an attack.
   Observe whether vanilla stops use and which item actually acts.
3. Fake Offhand on, target slot containing blocks: place on an ordinary
   block, open a chest normally, sneak-place against it. Then put food in
   the target slot while holding a tool: additional use is still unsupported
   in this read-only build, so this records the current adapter's baseline.

Build diagnostics with `xmake f --offhand_trace=y -y`, then build Lamium and
LamiumTests. Deploy only for a requested trace test; name the commit, DLL
hash and enabled options. After compile verification, reset with
`xmake f --offhand_trace=n -y` and rebuild the ordinary DLL before committing.
Include physical right click and a non-mouse activation in later adapter
checks. Keep every trace option off for ordinary builds.

For each supported category, the maintainer checks:
1. Actual effect, duration and source stack with the target slot different
   from the selected slot; release before completion and after completion.
2. Interactive blocks, air and entities according to the agreed priority;
   ordinary block placement still follows L-49.
3. Manual slot changes, intervening attacks, an empty/depleted target,
   Hand Restock and Auto Use overlap.
4. Disable, menus, focus loss, death, dimension change and world exit:
   no stale hold or restoration over a later manual selection.
5. Rejoin and server testing: counts, replacement containers and durability
   persist, with no rollback or duplicated effect.

No additional item category or runtime behavior is confirmed by this review.
