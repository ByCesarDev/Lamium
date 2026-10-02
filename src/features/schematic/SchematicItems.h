#pragma once
#include <cstdint>
#include <map>
#include <string>
class ItemStack;
class LocalPlayer;
// Item helpers shared by the Schematics screen and HUD (BACKLOG L-93).
// Client thread only.
namespace lamium::schematic::items {
// An item stack for an icon stored as binary NBT, cached; null when unknown.
// Pickup squash is off so the renderer draws it still.
ItemStack const* iconStack(std::string const& icon);
// Drops the cached stacks. Called on world exit: a stack points at the
// world's items, which the game rebuilds (for example after a language
// change), and drawing an old one crashed.
void forget();
// Items carried: the inventory and the contents of shulker boxes in it.
std::map<std::string, std::uint64_t> carried(LocalPlayer& player);
}
