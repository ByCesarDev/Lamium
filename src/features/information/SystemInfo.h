#pragma once
#include <optional>
#include <string>

namespace lamium::information {
// PC information for the Debug View (BACKLOG L-54). Read-only, local, cached;
// an empty value means Windows did not provide it and must not be guessed.
std::optional<std::string> systemMemoryText();  // "1.8 GB (peak 2.4 GB)", refreshed on a short interval
std::optional<std::string> systemCpuText();     // "AMD Ryzen 7 5800X (16 threads)", read once
std::optional<std::string> systemGpuText();     // "NVIDIA GeForce RTX 3070", read once
std::optional<std::string> systemDisplayText(); // "2560 × 1440", read once
std::optional<std::string> systemOsText();      // "Windows 11 (26100)", read once
}
