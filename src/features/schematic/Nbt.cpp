#include "features/schematic/Nbt.h"
#include <algorithm>
#include <bit>
#include <cstring>
#include <format>
#include <stdexcept>

namespace lamium::schematic::nbt {
namespace {
constexpr int maxDepth = 512;

struct Reader {
    std::span<std::uint8_t const> bytes;
    size_t at = 0;
    [[noreturn]] void fail(char const* what) const {
        throw std::runtime_error(std::format("NBT: {} at byte {}", what, at));
    }
    void need(size_t count) const { if (count > bytes.size() - at) fail("unexpected end"); }
    template <class T> T number() {
        need(sizeof(T));
        T value;
        std::memcpy(&value, bytes.data() + at, sizeof(T));
        at += sizeof(T);
        if constexpr (std::endian::native == std::endian::big) fail("big-endian hosts are not supported");
        return value;
    }
    // A length is never more than the bytes left could hold, so a corrupt
    // count cannot make us allocate gigabytes.
    size_t length(size_t elementSize) {
        auto count = number<std::int32_t>();
        if (count < 0) fail("negative length");
        if (static_cast<size_t>(count) > (bytes.size() - at) / std::max<size_t>(elementSize, 1)) fail("length past the end");
        return static_cast<size_t>(count);
    }
    std::string string() {
        auto count = number<std::uint16_t>();
        need(count);
        std::string value(reinterpret_cast<char const*>(bytes.data() + at), count);
        at += count;
        return value;
    }
    template <class T> std::vector<T> array() {
        auto count = length(sizeof(T));
        std::vector<T> values(count);
        for (auto& value : values) value = number<T>();
        return values;
    }
    Tag payload(Type type, int depth) {
        if (depth > maxDepth) fail("nesting too deep");
        switch (type) {
        case Type::Byte: return {number<std::int8_t>()};
        case Type::Short: return {number<std::int16_t>()};
        case Type::Int: return {number<std::int32_t>()};
        case Type::Long: return {number<std::int64_t>()};
        case Type::Float: return {number<float>()};
        case Type::Double: return {number<double>()};
        case Type::ByteArray: return {array<std::int8_t>()};
        case Type::String: return {string()};
        case Type::IntArray: return {array<std::int32_t>()};
        case Type::LongArray: return {array<std::int64_t>()};
        case Type::List: {
            List list;
            list.element = static_cast<Type>(number<std::uint8_t>());
            if (list.element > Type::LongArray) fail("unknown list type");
            auto count = length(1);
            if (count && list.element == Type::End) fail("non-empty list of End");
            list.items.reserve(count);
            for (size_t i = 0; i < count; ++i) list.items.push_back(payload(list.element, depth + 1));
            return {std::move(list)};
        }
        case Type::Compound: {
            Compound compound;
            for (;;) {
                auto child = static_cast<Type>(number<std::uint8_t>());
                if (child == Type::End) return {std::move(compound)};
                if (child > Type::LongArray) fail("unknown tag type");
                auto name = string();
                compound.entries.push_back({std::move(name), payload(child, depth + 1)});
            }
        }
        default: fail("unknown tag type");
        }
    }
};

struct Writer {
    std::string out;
    template <class T> void number(T value) {
        char raw[sizeof(T)];
        std::memcpy(raw, &value, sizeof(T));
        out.append(raw, sizeof(T));
    }
    void string(std::string const& value) {
        if (value.size() > 0xffff) throw std::runtime_error("NBT: string longer than 65535 bytes");
        number(static_cast<std::uint16_t>(value.size()));
        out += value;
    }
    template <class T> void array(std::vector<T> const& values) {
        number(static_cast<std::int32_t>(values.size()));
        for (auto value : values) number(value);
    }
    void payload(Tag const& tag) {
        std::visit([&](auto const& v) {
            using V = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<V, std::monostate>) throw std::runtime_error("NBT: empty tag");
            else if constexpr (std::is_arithmetic_v<V>) number(v);
            else if constexpr (std::is_same_v<V, std::string>) string(v);
            else if constexpr (std::is_same_v<V, List>) {
                number(static_cast<std::uint8_t>(v.element));
                number(static_cast<std::int32_t>(v.items.size()));
                for (auto const& item : v.items) {
                    if (item.type() != v.element) throw std::runtime_error("NBT: list item of the wrong type");
                    payload(item);
                }
            } else if constexpr (std::is_same_v<V, Compound>) {
                for (auto const& entry : v.entries) named(entry.name, entry.tag);
                number(static_cast<std::uint8_t>(Type::End));
            } else array(v);
        }, tag.value);
    }
    void named(std::string const& name, Tag const& tag) {
        number(static_cast<std::uint8_t>(tag.type()));
        string(name);
        payload(tag);
    }
};

std::string quoted(std::string const& value) {
    std::string out = "\"";
    for (char c : value) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + '"';
}
}

Root read(std::span<std::uint8_t const> bytes) {
    Reader reader{bytes};
    if (static_cast<Type>(reader.number<std::uint8_t>()) != Type::Compound) reader.fail("root is not a compound");
    Root root;
    root.name = reader.string();
    root.compound = std::move(*reader.payload(Type::Compound, 0).as<Compound>());
    return root;
}

std::string write(Root const& root) {
    Writer writer;
    writer.named(root.name, Tag{root.compound});
    return std::move(writer.out);
}

std::string text(Tag const& tag) {
    return std::visit([](auto const& v) -> std::string {
        using V = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<V, std::monostate>) return "";
        else if constexpr (std::is_same_v<V, std::int8_t>) return std::format("{}b", v);
        else if constexpr (std::is_same_v<V, std::int16_t>) return std::format("{}s", v);
        else if constexpr (std::is_same_v<V, std::int32_t>) return std::format("{}", v);
        else if constexpr (std::is_same_v<V, std::int64_t>) return std::format("{}L", v);
        else if constexpr (std::is_same_v<V, float>) return std::format("{}f", v);
        else if constexpr (std::is_same_v<V, double>) return std::format("{}d", v);
        else if constexpr (std::is_same_v<V, std::string>) return quoted(v);
        else if constexpr (std::is_same_v<V, List>) {
            std::string out = "[";
            for (size_t i = 0; i < v.items.size(); ++i) out += (i ? "," : "") + text(v.items[i]);
            return out + "]";
        } else if constexpr (std::is_same_v<V, Compound>) {
            std::string out = "{";
            for (size_t i = 0; i < v.entries.size(); ++i)
                out += (i ? "," : "") + v.entries[i].name + ":" + text(v.entries[i].tag);
            return out + "}";
        } else {
            std::string out = "[";
            for (size_t i = 0; i < v.size(); ++i) out += std::format("{}{}", i ? "," : "", v[i]);
            return out + "]";
        }
    }, tag.value);
}
}
