#include "features/inventory/game/TransferSession.h"
#include "features/inventory/game/RequestTracker.h"
#include "features/inventory/game/ScreenTracker.h"
#include "features/inventory/game/TextInputTracker.h"
#include "features/inspection/hover/HoverTracker.h"
#include "app/Runtime.h"

#include "mc/client/gui/screens/controllers/ContainerScreenController.h"
#include "mc/deps/shared_types/legacy/ContainerType.h"
#include "mc/world/containers/managers/controllers/ContainerManagerController.h"
#include "mc/world/item/ItemStack.h"

#include <deque>
#include <compare>
#include <atomic>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>

namespace lamium::inventory::game {
namespace {
using transfer::Gesture;
using transfer::Side;
using SharedTypes::Legacy::ContainerType;

struct Slot {
    std::string collection;
    int index = -1;
    Side side = Side::Player;
    unsigned generation = 0;
    auto operator<=>(Slot const&) const = default;
};
struct Request { Slot slot; Gesture gesture; };
struct QueuedRequest { Request request; ItemStack expected; };

std::mutex inputLock;
std::optional<Slot> hover;
std::deque<Request> pulses;
Gesture drag = Gesture::None;
std::atomic<unsigned> generation{1};
unsigned observedGeneration = 0;
unsigned stroke = 0;
unsigned observedStroke = 0;
std::set<Slot> visited;
std::deque<QueuedRequest> queued;
std::optional<TransferToken> waiting;

bool ordinary(ContainerType type) {
    return type == ContainerType::Container || type == ContainerType::MinecartChest
        || type == ContainerType::ChestBoat;
}

std::optional<Side> sideOf(std::string const& name) {
    if (name == "inventory_items" || name == "hotbar_items") return Side::Player;
    if (name == "container_items" || name == "barrel_items" || name == "shulker_box_items") return Side::Storage;
    return {};
}

bool available(ContainerScreenController& controller) {
    auto& runtime = Runtime::instance();
    auto manager = controller.mContainerManagerController;
    return runtime.enabled() && runtime.preferences().inventory.transfer
        && ScreenTracker::getInstance().current().get() == &controller
        && manager && !manager->mContainersClosed && ordinary(manager->getContainerType())
        && !controller._isCursorSelectedActive()
        && !TextInputTracker::getInstance().isEditing(ScreenTracker::getInstance().currentView());
}

std::optional<Slot> slotAt(ContainerScreenController& controller, std::string const& name, int index) {
    if (!available(controller)) return {};
    auto side = sideOf(name);
    auto manager = controller.mContainerManagerController;
    if (!side || !manager->hasContainerController(name)
        || index < 0 || index >= manager->getContainerSize(name)
        || !controller.tryGetAutoPlaceOrder(name)) return {};
    auto const& stack = manager->getItemStack(name, index);
    if (stack.isNull() || stack.mCount <= 0) return {};
    return Slot{name, index, *side, generation.load()};
}

std::optional<Slot> currentSlot(ContainerScreenController& controller) {
    auto const& current = inspection::hover::HoverTracker::getInstance().current();
    return current && current->controller == &controller
        ? slotAt(controller, current->collectionName, current->collectionIndex) : std::nullopt;
}

void stopPending() {
    if (waiting) cancelTransfer(*waiting);
    waiting.reset();
    queued.clear();
    visited.clear();
}

void enqueue(ContainerScreenController& controller, Request request) {
    if (queued.size() >= 128) return;
    auto manager = controller.mContainerManagerController;
    if (!manager || !manager->hasContainerController(request.slot.collection)
        || request.slot.index < 0 || request.slot.index >= manager->getContainerSize(request.slot.collection)) return;
    auto const& stack = manager->getItemStack(request.slot.collection, request.slot.index);
    if (!stack.isNull() && stack.mCount > 0) queued.push_back({std::move(request), stack});
}

void enqueueDrag(ContainerScreenController& controller, Slot const& slot, Gesture mode) {
    if (mode != Gesture::None && visited.insert(slot).second) enqueue(controller, {slot, mode});
}
}

bool TransferSession::mouseButton(int button, bool down, bool shift, bool control, bool cancelled) {
    std::scoped_lock lock(inputLock);
    if (!down) {
        if (drag == Gesture::None) return false;
        drag = Gesture::None;
        return true;
    }
    if (cancelled || !hover) return false;
    auto mode = transfer::dragGesture(button, shift, control);
    if (mode == Gesture::None) return false;
    drag = mode;
    ++stroke;
    auto source = *hover;
    if (pulses.size() < 128) pulses.push_back({source, mode});
    return true;
}

bool TransferSession::wheel(int direction, bool shift, bool cancelled) {
    std::scoped_lock lock(inputLock);
    if (cancelled || !hover || !transfer::wheelSource(direction, hover->side)) return false;
    if (pulses.size() < 128) pulses.push_back({*hover, transfer::wheelGesture(shift)});
    return true;
}

void TransferSession::modifierReleased() {
    std::scoped_lock lock(inputLock);
    drag = Gesture::None;
}

void TransferSession::cancel() {
    {
        std::scoped_lock lock(inputLock);
        drag = Gesture::None;
        hover.reset();
        pulses.clear();
        ++generation;
        ++stroke;
    }
    // cancel() is also called by the window procedure. Release the response
    // token on the client thread when tick next runs.
}

void TransferSession::slotHovered(ContainerScreenController& controller, std::string const& collection, int index) {
    auto slot = slotAt(controller, collection, index);
    std::scoped_lock lock(inputLock);
    hover = slot;
    if (hover && drag != Gesture::None && pulses.size() < 128) pulses.push_back({*hover, drag});
}

void TransferSession::slotUnhovered(ContainerScreenController&, std::string const& collection, int index) {
    std::scoped_lock lock(inputLock);
    if (hover && hover->collection == collection && hover->index == index) hover.reset();
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
        stopPending();
        observedGeneration = revision;
    }
    if (observedStroke != currentStroke) { visited.clear(); observedStroke = currentStroke; }
    if (!available(controller)) { stopPending(); return; }
    for (auto const& request : incoming) {
        if (request.slot.generation != revision) continue;
        if (request.gesture == Gesture::OneWheel || request.gesture == Gesture::StackWheel) {
            if (!slot || request.slot.collection != slot->collection || request.slot.index != slot->index) continue;
            enqueue(controller, request);
        } else enqueueDrag(controller, request.slot, request.gesture);
    }
    if (slot) enqueueDrag(controller, *slot, mode);
    if (waiting) {
        auto result = transferResult(*waiting);
        if (result == ResponseBarrier::Result::Waiting) return;
        cancelTransfer(*waiting);
        waiting.reset();
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
    auto manager = controller.mContainerManagerController;
    if (!manager || !manager->hasContainerController(request.slot.collection)
        || request.slot.index < 0 || request.slot.index >= manager->getContainerSize(request.slot.collection)
        || !controller.tryGetAutoPlaceOrder(request.slot.collection)) return;
    auto const& stack = manager->getItemStack(request.slot.collection, request.slot.index);
    if (stack.isNull() || stack.mCount <= 0) return;
    bool const wheel = request.gesture == Gesture::OneWheel || request.gesture == Gesture::StackWheel;
    if (!stack.matchesItem(entry.expected) || (!wheel && stack.mCount != entry.expected.mCount)) {
        Runtime::instance().self().getLogger().info("Inventory transfer stopped: source slot changed");
        queued.clear();
        return;
    }
    auto token = beginTransfer(*manager);
    if (!token) { queued.push_front(std::move(entry)); return; }
    try {
        controller._handleAutoPlace(transfer::amount(request.gesture, stack.mCount),
                                    request.slot.collection, request.slot.index);
    } catch (...) {
        cancelTransfer(*token);
        throw;
    }
    endTransfer(*token);
    waiting = token;
}
}
