#include "features/inventory/game/TransferSession.h"
#include "features/inventory/game/RequestTracker.h"
#include "features/inventory/game/ScreenTracker.h"
#include "features/inventory/game/TextInputTracker.h"
#include "features/inspection/hover/HoverTracker.h"
#include "app/Runtime.h"
#ifdef LAMIUM_TRANSFER_TRACE
#include "app/TraceLog.h"
#endif

#include "mc/client/gui/screens/controllers/ContainerScreenController.h"
#include "mc/deps/shared_types/legacy/ContainerType.h"
#include "mc/world/containers/SlotData.h"
#include "mc/world/containers/managers/controllers/ContainerManagerController.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/VanillaItemTags.h"

#include <deque>
#include <compare>
#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace lamium::inventory::game {
namespace {
using transfer::Gesture;
using transfer::Side;
using SharedTypes::Legacy::ContainerType;

#ifdef LAMIUM_TRANSFER_TRACE
TraceBudget inputBudget, queueBudget, sendBudget;
#define TRANSFER_TRACE(budget, ...) traceLog(budget, 400, __VA_ARGS__)
#else
#define TRANSFER_TRACE(...) ((void)0)
#endif

struct Slot {
    std::string collection;
    int index = -1;
    Side side = Side::Player;
    unsigned generation = 0;
    bool vanillaShift = false; // Shift + left stays vanilla here (worn items, inventory screen).
    auto operator<=>(Slot const&) const = default;
};
struct Request { Slot slot; Gesture gesture; int wheelDirection = 0; };
struct QueuedRequest { Request request; ItemStack expected; };

std::mutex inputLock;
std::optional<Slot> hover;
std::deque<Request> pulses;
Gesture drag = Gesture::None;
// The button whose press Lamium cancelled; its release is cancelled too, even
// if the drag already ended (a released modifier), so vanilla never sees a
// lone release.
int consumedButton = -1;
std::atomic<unsigned> generation{1};
unsigned observedGeneration = 0;
unsigned stroke = 0;
unsigned observedStroke = 0;
// The slot the drag last took from. A drag moves each slot once per entry:
// staying on it does nothing more, entering it again moves what is there now
// (an item can go out and come back in one drag).
std::optional<Slot> lastDrag;
std::deque<QueuedRequest> queued;
// Guarded by inputLock: cancel() must release it even when no container
// screen is left to tick, or the shared request barrier stays busy.
std::optional<TransferToken> waiting;

void releaseWaiting() {
    std::optional<TransferToken> token;
    {
        std::scoped_lock lock(inputLock);
        token = std::exchange(waiting, std::nullopt);
    }
    if (token) cancelTransfer(*token);
}

bool ordinary(ContainerType type) {
    return type == ContainerType::Container || type == ContainerType::MinecartChest
        || type == ContainerType::ChestBoat;
}

transfer::GestureOptions gestureOptions() {
    auto const& value = Runtime::instance().preferences().inventory;
    return {value.transferWheelOne, value.transferWheelStack,
            value.transferDragStack, value.transferDragOne};
}

// The inventory screen moves between the main inventory and the hotbar in
// every game mode; creative's item catalog is not a transfer side.
bool inventoryScreen(ContainerManagerController& manager) {
    if (manager.getContainerType() != ContainerType::Inventory) return false;
    // Collection sizes are not documented (survival: 27 + 9); record each
    // layout once so a creative difference shows in the log.
    static std::pair<int, int> logged{-2, -2};
    std::pair<int, int> sizes{
        manager.hasContainerController("inventory_items") ? manager.getContainerSize("inventory_items") : -1,
        manager.hasContainerController("hotbar_items") ? manager.getContainerSize("hotbar_items") : -1};
    if (sizes != logged) {
        logged = sizes;
        Runtime::instance().self().getLogger().info("Inventory transfer: inventory screen with inventory_items {} hotbar_items {}",
            sizes.first, sizes.second);
    }
    return true;
}

int inventorySize(ContainerManagerController& manager) {
    return manager.hasContainerController("inventory_items") ? manager.getContainerSize("inventory_items") : 0;
}

std::optional<Side> sideOf(ContainerManagerController& manager, std::string const& name, int index) {
    return transfer::collectionSide(name, index, inventorySize(manager), inventoryScreen(manager));
}

// Every slot of one side, in grid order.
std::vector<Slot> sideSlots(ContainerManagerController& manager, Side side, unsigned slotGeneration) {
    std::vector<Slot> slots;
    bool const screen = inventoryScreen(manager);
    int const size = inventorySize(manager);
    auto append = [&](char const* collection) {
        if (!manager.hasContainerController(collection)) return;
        int const count = manager.getContainerSize(collection);
        for (int i = 0; i < count; ++i)
            if (transfer::collectionSide(collection, i, size, screen) == side)
                slots.push_back({collection, i, side, slotGeneration});
    };
    if (side == Side::Player || screen) {
        // A 36-slot inventory already includes the hotbar; smaller main
        // inventories need the separate hotbar collection below them.
        if (size < 36) append("hotbar_items");
        append("inventory_items");
    } else {
        for (auto name : {"container_items", "barrel_items", "shulker_box_items"}) {
            if (manager.hasContainerController(name) && manager.getContainerSize(name) > 0) {
                append(name);
                break;
            }
        }
    }
    return slots;
}

std::optional<Slot> matchingSource(ContainerManagerController& manager, Slot const& hovered,
                                   ItemStack const& reference, Side sourceSide) {
    auto candidates = sideSlots(manager, sourceSide, hovered.generation);
    int const inventorySize = game::inventorySize(manager);
    std::vector<unsigned char> matches(candidates.size());
    int hoveredIndex = -1;
    for (size_t i = 0; i < candidates.size(); ++i) {
        auto const& candidate = candidates[i];
        auto const& item = manager.getItemStack(candidate.collection, candidate.index);
        matches[i] = !item.isNull() && item.mCount > 0 && item.matchesItem(reference);
        bool const sameSlot = candidate.collection == hovered.collection && candidate.index == hovered.index;
        bool const hotbarAlias = inventorySize >= 36
            && candidate.collection == "inventory_items"
            && hovered.collection == "hotbar_items" && candidate.index == hovered.index;
        if (hovered.side == sourceSide && (sameSlot || hotbarAlias)) hoveredIndex = static_cast<int>(i);
    }
    int const index = transfer::chooseSource(std::span<unsigned char const>{matches}, hoveredIndex);
    if (index < 0) return {};
    return candidates[static_cast<size_t>(index)];
}

bool available(ContainerScreenController& controller) {
    auto& runtime = Runtime::instance();
    auto manager = controller.mContainerManagerController;
    return runtime.enabled() && runtime.preferences().inventory.transfer
        && ScreenTracker::getInstance().current().get() == &controller
        && manager && !manager->mContainersClosed
        && (ordinary(manager->getContainerType()) || inventoryScreen(*manager))
        && !controller._isCursorSelectedActive()
        && !TextInputTracker::getInstance().isEditing(ScreenTracker::getInstance().currentView());
}

std::optional<Slot> slotAt(ContainerScreenController& controller, std::string const& name, int index) {
    if (!available(controller)) return {};
    auto manager = controller.mContainerManagerController;
    auto side = sideOf(*manager, name, index);
    if (!side || !manager->hasContainerController(name)
        || index < 0 || index >= manager->getContainerSize(name)) return {};
    auto const& stack = manager->getItemStack(name, index);
    if (stack.isNull() || stack.mCount <= 0) return {};
    bool worn = inventoryScreen(*manager) && stack.mItem
        && transfer::wornItem(stack.getTypeName(),
                              stack.mItem->isHumanoidArmor() || stack.mItem->hasTag(VanillaItemTags::Armor()));
    return Slot{name, index, *side, generation.load(), worn};
}

std::optional<Slot> currentSlot(ContainerScreenController& controller) {
    auto const& current = inspection::hover::HoverTracker::getInstance().current();
    return current && current->controller == &controller
        ? slotAt(controller, current->collectionName, current->collectionIndex) : std::nullopt;
}

void stopPending() {
    releaseWaiting();
    queued.clear();
    lastDrag.reset();
}

void enqueue(ContainerScreenController& controller, Request request) {
    if (queued.size() >= 128) return;
    auto manager = controller.mContainerManagerController;
    if (!manager || !manager->hasContainerController(request.slot.collection)
        || request.slot.index < 0 || request.slot.index >= manager->getContainerSize(request.slot.collection)) return;
    auto const& stack = manager->getItemStack(request.slot.collection, request.slot.index);
    if (stack.isNull() || stack.mCount <= 0) return;
    TRANSFER_TRACE(queueBudget, "Transfer trace: enqueue {}:{} gesture={} wheel={} count={} queued={} stroke={}",
        request.slot.collection, request.slot.index, static_cast<int>(request.gesture), request.wheelDirection,
        static_cast<int>(stack.mCount), queued.size(), stroke);
    queued.push_back({std::move(request), stack});
}

void enqueueDrag(ContainerScreenController& controller, Slot const& slot, Gesture mode) {
    if (mode == Gesture::None || lastDrag == slot) return;
    lastDrag = slot;
    if (transfer::vanillaShift(mode, true, slot.vanillaShift)) return;
    enqueue(controller, {slot, mode});
}
}

bool TransferSession::mouseButton(int button, bool down, bool shift, bool control, bool cancelled) {
    std::scoped_lock lock(inputLock);
    if (!down) {
        TRANSFER_TRACE(inputBudget, "Transfer trace: release button={} consumed={} drag={}", button, consumedButton,
            static_cast<int>(drag));
        if (button != consumedButton) return false;
        consumedButton = -1;
        drag = Gesture::None;
        return true;
    }
    // A new press decides afresh; a release lost to focus changes never
    // swallows a later vanilla click.
    if (button == consumedButton) consumedButton = -1;
    TRANSFER_TRACE(inputBudget, "Transfer trace: press button={} shift={} control={} cancelled={} hover={}:{} drag={} stroke={}",
        button, shift, control, cancelled, hover ? hover->collection : std::string("-"), hover ? hover->index : -1,
        static_cast<int>(drag), stroke);
    if (cancelled || !hover) return false;
    auto mode = transfer::dragGesture(button, shift, control);
    if (!transfer::enabled(mode, gestureOptions())) return false;
    if (transfer::vanillaShift(mode, true, hover->vanillaShift)) return false;
    drag = mode;
    consumedButton = button;
    ++stroke;
    auto source = *hover;
    if (pulses.size() < 128) pulses.push_back({source, mode});
    return true;
}

bool TransferSession::wheel(int direction, bool shift, bool cancelled) {
    std::scoped_lock lock(inputLock);
    TRANSFER_TRACE(inputBudget, "Transfer trace: wheel direction={} shift={} cancelled={} hover={}:{}",
        direction, shift, cancelled, hover ? hover->collection : std::string("-"), hover ? hover->index : -1);
    if (cancelled || !hover) return false;
    auto mode = transfer::wheelGesture(shift);
    if (!transfer::enabled(mode, gestureOptions())) return false;
    if (pulses.size() < 128) pulses.push_back({*hover, mode, direction});
    return true;
}

void TransferSession::modifierReleased() {
    std::scoped_lock lock(inputLock);
    TRANSFER_TRACE(inputBudget, "Transfer trace: modifier released drag={}", static_cast<int>(drag));
    drag = Gesture::None;
}

void TransferSession::cancel() {
    {
        std::scoped_lock lock(inputLock);
        TRANSFER_TRACE(inputBudget, "Transfer trace: cancel drag={} pulses={} generation={}", static_cast<int>(drag),
            pulses.size(), generation.load());
        drag = Gesture::None;
        hover.reset();
        pulses.clear();
        ++generation;
        ++stroke;
    }
    // No tick may follow (the screen closed), so release the barrier now.
    // cancelTransfer locks the request tracker, so the window procedure may
    // call this too.
    releaseWaiting();
}

void TransferSession::slotHovered(ContainerScreenController& controller, std::string const& collection, int index) {
    auto slot = slotAt(controller, collection, index);
    std::scoped_lock lock(inputLock);
    hover = slot;
    if (drag != Gesture::None && pulses.size() < 128) pulses.push_back({hover.value_or(Slot{}), drag});
}

void TransferSession::slotUnhovered(ContainerScreenController&, std::string const& collection, int index) {
    std::scoped_lock lock(inputLock);
    if (hover && hover->collection == collection && hover->index == index) hover.reset();
    if (drag != Gesture::None && pulses.size() < 128) pulses.push_back({Slot{}, drag});
}

void TransferSession::tick(ContainerScreenController& controller) {
    auto slot = currentSlot(controller);
    std::deque<Request> incoming;
    Gesture mode;
    unsigned revision;
    unsigned currentStroke;
    {
        std::scoped_lock lock(inputLock);
        if (slot && slot->generation != generation.load()) slot.reset();
        hover = slot;
        incoming.swap(pulses);
        mode = drag;
        revision = generation.load();
        currentStroke = stroke;
    }
    if (observedGeneration != revision) {
        TRANSFER_TRACE(queueBudget, "Transfer trace: generation {} -> {} drops queued={}", observedGeneration, revision,
            queued.size());
        stopPending();
        observedGeneration = revision;
    }
    if (observedStroke != currentStroke) { lastDrag.reset(); observedStroke = currentStroke; }
    if (!available(controller)) {
        if (!queued.empty() || !incoming.empty())
            TRANSFER_TRACE(queueBudget, "Transfer trace: unavailable drops queued={}", queued.size());
        stopPending();
        return;
    }
    for (auto const& request : incoming) {
        if (request.slot.index < 0) { lastDrag.reset(); continue; } // The pointer left a slot.
        if (request.slot.generation != revision) continue;
        if (!transfer::enabled(request.gesture, gestureOptions())) continue;
        if (request.gesture == Gesture::OneWheel || request.gesture == Gesture::StackWheel) {
            if (!slot || request.slot.collection != slot->collection || request.slot.index != slot->index) continue;
            enqueue(controller, request);
        } else enqueueDrag(controller, request.slot, request.gesture);
    }
    if (slot) enqueueDrag(controller, *slot, mode);
    else lastDrag.reset();
    std::optional<TransferToken> pending;
    {
        std::scoped_lock lock(inputLock);
        pending = waiting;
    }
    if (pending) {
        auto result = transferResult(*pending);
        if (result == ResponseBarrier::Result::Waiting) return;
        TRANSFER_TRACE(sendBudget, "Transfer trace: response {} queued={}", static_cast<int>(result), queued.size());
        releaseWaiting();
        if (result != ResponseBarrier::Result::Accepted) {
            Runtime::instance().self().getLogger().warn("Inventory transfer stopped: response {}", static_cast<int>(result));
            queued.clear();
            return;
        }
    }
    if (queued.empty()) return;
    auto entry = std::move(queued.front());
    queued.pop_front();
    auto const& request = entry.request;
    if (!transfer::enabled(request.gesture, gestureOptions())) return;
    auto manager = controller.mContainerManagerController;
    if (!manager || !manager->hasContainerController(request.slot.collection)
        || request.slot.index < 0 || request.slot.index >= manager->getContainerSize(request.slot.collection)) return;
    auto const& hovered = manager->getItemStack(request.slot.collection, request.slot.index);
    if (hovered.isNull() || hovered.mCount <= 0) return;
    bool const wheel = request.gesture == Gesture::OneWheel || request.gesture == Gesture::StackWheel;
    if (!hovered.matchesItem(entry.expected) || (!wheel && hovered.mCount != entry.expected.mCount)) {
        Runtime::instance().self().getLogger().info("Inventory transfer stopped: source slot changed ({}:{} now {}, expected {})",
            request.slot.collection, request.slot.index, static_cast<int>(hovered.mCount),
            static_cast<int>(entry.expected.mCount));
        queued.clear();
        return;
    }
    std::optional<Slot> source;
    std::optional<Slot> destination;
    if (wheel) {
        auto targetSide = transfer::wheelDestination(request.wheelDirection);
        auto sourceSide = transfer::otherSide(targetSide);
        bool const stackWheel = request.gesture == Gesture::StackWheel;
        if (!stackWheel && request.slot.side == targetSide) {
            if (!transfer::canReceive(hovered.mCount, hovered.getMaxStackSize())) return;
            destination = request.slot;
        }
        source = !stackWheel && request.slot.side == sourceSide ? std::optional<Slot>(request.slot)
            : matchingSource(*manager, request.slot, hovered, sourceSide);
    } else source = request.slot;
    if (!source) {
        TRANSFER_TRACE(sendBudget, "Transfer trace: no source for {}:{}", request.slot.collection, request.slot.index);
        return;
    }
    auto const& stack = manager->getItemStack(source->collection, source->index);
    if (stack.isNull() || stack.mCount <= 0 || !stack.matchesItem(hovered)) return;
    int moved = 1;
    std::optional<QueuedRequest> rest;
    if (!destination && inventoryScreen(*manager)) {
        auto targets = sideSlots(*manager, transfer::otherSide(source->side), source->generation);
        std::vector<transfer::Destination> room;
        for (auto const& target : targets) {
            auto const& item = manager->getItemStack(target.collection, target.index);
            bool const empty = item.isNull() || item.mCount <= 0;
            room.push_back({empty, !empty && item.matchesItem(stack), empty ? 0 : item.mCount, stack.getMaxStackSize()});
        }
        int const at = transfer::chooseDestination(std::span<transfer::Destination const>{room});
        if (at < 0) {
            TRANSFER_TRACE(sendBudget, "Transfer trace: no destination for {}:{}", source->collection, source->index);
            return;
        }
        destination = targets[static_cast<size_t>(at)];
        moved = std::min(transfer::amount(request.gesture, stack.mCount), transfer::room(room[static_cast<size_t>(at)]));
        // The rest of a stack follows once vanilla accepts this part.
        if (moved < stack.mCount && transfer::amount(request.gesture, stack.mCount) > 1) {
            rest = entry;
            if (!wheel) rest->expected.mCount = static_cast<uchar>(stack.mCount - moved);
        }
    }
    if (!destination && !controller.tryGetAutoPlaceOrder(source->collection)) return;
    auto token = beginTransfer(*manager);
    if (!token) {
        TRANSFER_TRACE(sendBudget, "Transfer trace: barrier busy, retry {}:{}", source->collection, source->index);
        queued.push_front(std::move(entry));
        return;
    }
    TRANSFER_TRACE(sendBudget, "Transfer trace: send {}:{} -> {} amount={} count={} gesture={} rest={}",
        source->collection, source->index,
        destination ? destination->collection + ":" + std::to_string(destination->index) : std::string("auto"),
        destination ? moved : transfer::amount(request.gesture, stack.mCount), static_cast<int>(stack.mCount),
        static_cast<int>(request.gesture), rest.has_value());
    bool submitted = true;
    try {
        if (destination) {
            SlotData const src(source->collection, source->index);
            SlotData const dst(destination->collection, destination->index);
            submitted = manager->handlePlaceAmount(src, moved, dst);
        } else controller._handleAutoPlace(transfer::amount(request.gesture, stack.mCount),
                                         source->collection, source->index);
    } catch (...) {
        cancelTransfer(*token);
        throw;
    }
    if (!submitted) {
        cancelTransfer(*token);
        queued.clear();
        Runtime::instance().self().getLogger().info("Inventory transfer stopped: vanilla refused a destination");
        return;
    }
    endTransfer(*token);
    if (rest) queued.push_front(std::move(*rest));
    bool stale;
    {
        std::scoped_lock lock(inputLock);
        stale = generation.load() != revision;
        if (!stale) waiting = token;
    }
    // Cancelled while vanilla ran (it may close the screen synchronously).
    if (stale) cancelTransfer(*token);
}
}
