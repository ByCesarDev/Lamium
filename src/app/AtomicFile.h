#pragma once
#include <filesystem>
#include <string_view>

namespace lamium {
// Write a complete sibling temporary file, flush it, then replace the
// destination. A failure leaves the destination as it was. `what` names the
// document in error messages.
void writeFileReplacing(std::filesystem::path const& path, std::string_view text, char const* what);
}
