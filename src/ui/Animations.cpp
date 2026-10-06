#include "ui/Animations.h"
#include "app/Runtime.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/options/IOptionRegistry.h"

namespace lamium::ui {
bool animationsOn(IClientInstance& client) {
    auto mode = Runtime::instance().preferences().ui.animations;
    if (mode == 1) return true;
    if (mode == 2) return false;
    try { return client.getOptions().getScreenAnimations(); } catch (...) { return true; }
}
}
