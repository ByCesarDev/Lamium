#pragma once
class ItemStack;
class LocalPlayer;
namespace lamium::inventory::game {
// A player-owned slot a screenless move can change: the 36 inventory slots,
// the offhand, or an armor slot (0 head, 1 chest, 2 legs, 3 feet).
enum class Place { Inventory, Offhand, Armor };
struct Location { Place place; int slot; };
ItemStack const& itemAt(LocalPlayer& player, Location at);
// Predicts two slots through the client's own setters under a legacy
// request scope and flushes the resulting balanced transaction (L-66). A
// side the setter did not record (offhand/armor) is added as its own action.
// Returns false, changing nothing, when another transaction or request is
// in progress. Throws if the transaction stays unbalanced after mutation.
bool movePair(LocalPlayer& player, Location a, ItemStack const& newA, Location b, ItemStack const& newB);
// True while movePair is changing slots, so observers ignore its own effects.
bool moving();
}
