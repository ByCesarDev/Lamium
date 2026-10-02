#pragma once
#include <string_view>

namespace lamium::ui::translations {
// One key's text in a locale kept in its own table (TranslationsZhCN.h).
struct Text { std::string_view key, text; };
}
