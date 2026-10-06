#include "app/Versions.h"

#include "ll/api/Versions.h"

#ifndef LAMIUM_VERSION
#define LAMIUM_VERSION "dev"
#endif

namespace lamium {
std::string lamiumVersion() { return LAMIUM_VERSION; }
std::string runningGameVersion() {
    try { return ll::getGameVersion().to_string(); } catch (...) { return "?"; }
}
std::string runningLoaderVersion() {
    try { return ll::getLoaderVersion().to_string(); } catch (...) { return "?"; }
}
std::string runningVersionLine() {
    return versionLine(LAMIUM_VERSION, runningGameVersion(), runningLoaderVersion());
}
}
