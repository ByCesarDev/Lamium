#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// Little-endian NBT as Bedrock stores it in .mcstructure files. Game-independent
// so the schematic model can be read, written and tested without Minecraft.
namespace lamium::schematic::nbt {
enum class Type : std::uint8_t {
    End, Byte, Short, Int, Long, Float, Double, ByteArray, String, List, Compound, IntArray, LongArray,
};

struct Tag;
struct Entry;
// Entries keep file order so a written structure matches what was read.
struct Compound {
    std::vector<Entry> entries;
    Tag const* find(std::string_view name) const;
    Tag& set(std::string name, Tag tag);
};
struct List {
    Type element = Type::End;
    std::vector<Tag> items;
};
struct Tag {
    std::variant<std::monostate, std::int8_t, std::int16_t, std::int32_t, std::int64_t, float, double,
        std::vector<std::int8_t>, std::string, List, Compound, std::vector<std::int32_t>, std::vector<std::int64_t>>
        value;
    Type type() const { return static_cast<Type>(value.index()); }
    template <class T> T const* as() const { return std::get_if<T>(&value); }
    template <class T> T* as() { return std::get_if<T>(&value); }
    // Reads any integer width into `out`; false for other types.
    bool integer(std::int64_t& out) const;
};
struct Entry {
    std::string name;
    Tag tag;
};
inline Tag const* Compound::find(std::string_view name) const {
    for (auto const& entry : entries) if (entry.name == name) return &entry.tag;
    return nullptr;
}
inline Tag& Compound::set(std::string name, Tag tag) {
    for (auto& entry : entries) if (entry.name == name) return entry.tag = std::move(tag);
    return entries.emplace_back(Entry{std::move(name), std::move(tag)}).tag;
}
inline bool Tag::integer(std::int64_t& out) const {
    return std::visit([&](auto const& v) {
        using V = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<V, std::int8_t> || std::is_same_v<V, std::int16_t>
                      || std::is_same_v<V, std::int32_t> || std::is_same_v<V, std::int64_t>) {
            out = v;
            return true;
        } else return false;
    }, value);
}

struct Root {
    std::string name;
    Compound compound;
};
// Throws std::runtime_error on truncated, malformed or implausibly large input.
Root read(std::span<std::uint8_t const> bytes);
std::string write(Root const& root);
// Text for display and comparison, e.g. `1b`, `"north"`, `{a:1,b:"x"}`.
std::string text(Tag const& tag);
}
