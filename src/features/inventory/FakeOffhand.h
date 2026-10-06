#pragma once
#include <functional>
class IClientInstance;
namespace lamium { struct Settings; }
namespace lamium::inventory::fakeOffhand {
void start();
void stop();
void configure(Settings const& value);
void press(IClientInstance& client);
// Called only from the native use-button handler instead of vanilla, which
// it runs at most once and possibly with the target slot borrowed.
void nativeDown(IClientInstance& client, std::function<void()> const& vanilla);
void release();
// Whether the activation chord is held when it ends on the right button.
void rightChord(bool held);
// The current chord state, for diagnostics only.
bool rightChordActive();
}
