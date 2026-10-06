#pragma once
class IClientInstance;
namespace lamium { struct Settings; }
namespace lamium::inventory::fakeOffhand {
void start();
void stop();
void configure(Settings const& value);
void press(IClientInstance& client);
// Called only from the native use-button handler, before vanilla reads its item.
bool nativePress(IClientInstance& client) noexcept;
void release();
// Whether the activation chord is held when it ends on the right button.
void rightChord(bool held);
// The current chord state, for diagnostics only.
bool rightChordActive();
}
