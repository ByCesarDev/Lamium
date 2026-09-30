#pragma once

namespace lamium { struct Settings; }
namespace lamium::visuals::effects {
void configure(Settings const&);
void start() noexcept;
void stop();
}
