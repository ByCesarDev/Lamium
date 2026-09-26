#pragma once
class IClientInstance;
namespace lamium { struct Settings; }
namespace lamium::inventory::fakeOffhand {
void start();
void stop();
void configure(Settings const& value);
void press(IClientInstance& client);
void release();
// Whether the activation chord is held when it ends on the right button.
void rightChord(bool held);
}
