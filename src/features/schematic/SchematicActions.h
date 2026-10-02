#pragma once
#include "input/Binding.h"
class IClientInstance;
// Keys that act on the selected schematic placement (BACKLOG L-93). Called on
// the client thread from the action dispatch, in gameplay only.
namespace lamium::schematic::actions {
bool handles(input::Action action);
void press(IClientInstance& client, input::Action action);
}
