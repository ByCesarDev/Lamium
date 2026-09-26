#pragma once
class IClientInstance;
namespace lamium::inventory::fakeOffhand {
void start();
void stop();
void press(IClientInstance& client);
void release();
void rawRightButton(bool down);
}
