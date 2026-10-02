#include "features/schematic/SchematicItems.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/ListTag.h"
#include "mc/deps/nbt/Tag.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/item/ItemStack.h"

namespace lamium::schematic::items {
namespace {
std::map<std::string, ItemStack> stacks;
}
ItemStack const* iconStack(std::string const& icon) {
    if (icon.empty()) return nullptr;
    auto found = stacks.find(icon);
    if (found == stacks.end()) {
        if (stacks.size() > 512) stacks.clear();
        ItemStack stack;
        if (auto tag = CompoundTag::fromBinaryNbt(icon)) {
            try {
                stack = ItemStack::fromTag(*tag);
                // A decoded stack counts as just picked up: no pickup squash.
                stack.mShowPickUp = false;
                stack.mWasPickedUp = false;
            } catch (...) {}
        }
        found = stacks.emplace(icon, std::move(stack)).first;
    }
    return found->second.isNull() ? nullptr : &found->second;
}
std::map<std::string, std::uint64_t> carried(LocalPlayer& player) {
    std::map<std::string, std::uint64_t> out;
    auto& inventory = player.getInventory();
    for (int slot = 0; slot < inventory.getContainerSize(); ++slot) {
        auto const& stack = inventory.getItem(slot);
        if (stack.isNull()) continue;
        out[stack.getTypeName()] += stack.mCount;
        if (!stack.getTypeName().ends_with("shulker_box")) continue;
        auto const* data = stack.mUserData.get();
        if (!data) continue;
        auto items = data->mTags.find("Items");
        if (items == data->mTags.end() || !items->second.is_array()) continue;
        for (auto const& entry : items->second.get<ListTag>()) {
            if (!entry || entry->getId() != Tag::Type::Compound) continue;
            try {
                auto inner = ItemStack::fromTag(entry->as<CompoundTag>());
                if (!inner.isNull()) out[inner.getTypeName()] += inner.mCount;
            } catch (...) {}
        }
    }
    return out;
}
}
