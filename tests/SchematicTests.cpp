#include "features/schematic/Structure.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
void check(bool, char const*);
namespace {
using namespace lamium::schematic;
std::span<std::uint8_t const> span(std::string const& bytes) {
    return {reinterpret_cast<std::uint8_t const*>(bytes.data()), bytes.size()};
}
bool throws(std::string const& bytes) {
    try { parseStructure(span(bytes)); } catch (std::runtime_error const&) { return true; }
    return false;
}

Structure sample() {
    Structure s;
    s.size = {2, 3, 4};
    s.worldOrigin = {100, 64, -20};
    nbt::Compound stairs;
    stairs.set("weirdo_direction", {std::int32_t{1}});
    stairs.set("upside_down_bit", {std::int8_t{0}});
    s.palette = {{"minecraft:air", {}, 18168865}, {"minecraft:stone", {}, 18168865},
                 {"minecraft:oak_stairs", stairs, 18168865}, {"minecraft:chest", {}, 18168865},
                 {"minecraft:water", {}, 18168865}};
    s.blocks.assign(s.cells(), 0);
    s.blocks[s.cell(0, 0, 0)] = 1;
    s.blocks[s.cell(1, 2, 3)] = 2;
    s.blocks[s.cell(0, 1, 2)] = 3;
    s.blocks[s.cell(1, 0, 1)] = voidCell;
    s.liquids.assign(s.cells(), voidCell);
    s.liquids[s.cell(1, 2, 3)] = 4;
    nbt::Compound chest;
    chest.set("id", {std::string{"Chest"}});
    chest.set("Items", {nbt::List{nbt::Type::Compound, {}}});
    s.blockEntities[s.cell(0, 1, 2)] = chest;
    nbt::Compound stand;
    stand.set("identifier", {std::string{"minecraft:armor_stand"}});
    nbt::List pos{nbt::Type::Float, {{101.5f}, {64.f}, {-17.5f}}};
    stand.set("Pos", {pos});
    s.entities.push_back({"minecraft:armor_stand", 1.5, 0, 2.5, stand});
    return s;
}

void nbtBasics() {
    nbt::Root root{"", {}};
    root.compound.set("b", {std::int8_t{-3}});
    root.compound.set("s", {std::string{"hé"}});
    root.compound.set("l", {nbt::List{nbt::Type::Short, {{std::int16_t{7}}, {std::int16_t{-8}}}}});
    root.compound.set("a", {std::vector<std::int32_t>{1, 2, 3}});
    auto bytes = nbt::write(root);
    check(bytes[0] == 10 && bytes[1] == 0 && bytes[2] == 0, "the root is an unnamed compound");
    auto back = nbt::read(span(bytes));
    check(nbt::text({back.compound}) == R"({b:-3b,s:"hé",l:[7s,-8s],a:[1,2,3]})",
          "NBT round-trips little-endian with entry order kept");
    std::int64_t value = 0;
    check(back.compound.find("b")->integer(value) && value == -3 && !back.compound.find("s")->integer(value),
          "integers of any width read as integers; strings do not");
    bool truncated = false;
    try { nbt::read(span(bytes.substr(0, bytes.size() - 3))); } catch (std::runtime_error const&) { truncated = true; }
    check(truncated, "truncated NBT is rejected");
    std::string huge = bytes.substr(0, 3) + std::string("\x0b\x01\x00\x61\xff\xff\xff\x7f", 8);
    bool tooLong = false;
    try { nbt::read(span(huge)); } catch (std::runtime_error const&) { tooLong = true; }
    check(tooLong, "an array length past the end is rejected before allocating");
}

void structureRoundTrip() {
    auto original = sample();
    check(original.cell(0, 0, 1) == 1 && original.cell(0, 1, 0) == 4 && original.cell(1, 0, 0) == 12,
          "cells run z fastest, then y, then x");
    auto at = original.position(original.cell(1, 2, 3));
    check(at[0] == 1 && at[1] == 2 && at[2] == 3, "a cell index maps back to its position");
    auto parsed = parseStructure(span(writeStructure(original)));
    check(parsed.size == original.size && parsed.worldOrigin == original.worldOrigin, "size and origin survive");
    check(parsed.blocks == original.blocks && parsed.liquids == original.liquids, "both block layers survive");
    check(parsed.palette.size() == 5 && parsed.palette[2].key() == "minecraft:oak_stairs[upside_down_bit=0b,weirdo_direction=1]",
          "palette keys sort states by name");
    check(parsed.palette[0].isAir() && !parsed.palette[1].isAir(), "air is recognized by name");
    check(parsed.blockEntities.size() == 1 && parsed.blockEntities.contains(original.cell(0, 1, 2))
          && *parsed.blockEntities.at(original.cell(0, 1, 2)).find("id")->as<std::string>() == "Chest",
          "block entity data is keyed by cell");
    check(parsed.entities.size() == 1 && parsed.entities[0].identifier == "minecraft:armor_stand"
          && parsed.entities[0].x == 1.5 && parsed.entities[0].y == 0 && parsed.entities[0].z == 2.5,
          "entity positions become relative to the structure corner");

    auto dry = original;
    dry.liquids.clear();
    check(parseStructure(span(writeStructure(dry))).liquids.empty(), "an all-void second layer reads as empty");

    // Older exports store each layer as a list of ints.
    nbt::Root legacy = nbt::read(span(writeStructure(dry)));
    auto& body = *legacy.compound.entries[2].tag.as<nbt::Compound>();
    nbt::List layers{nbt::Type::List, {}};
    nbt::List ints{nbt::Type::Int, {}};
    for (auto index : dry.blocks) ints.items.push_back({index});
    layers.items.push_back({ints});
    body.set("block_indices", {layers});
    check(parseStructure(span(nbt::write(legacy))).blocks == dry.blocks, "list-of-int layers read too");
}

void structureRejects() {
    check(throws("") && throws("\x0a\x00\x00\x00"), "empty input and an empty compound are rejected");
    auto bad = sample();
    bad.blocks[0] = 9;
    bool caught = false;
    try { parseStructure(span(writeStructure(bad))); } catch (std::runtime_error const&) { caught = true; }
    check(caught, "a block index outside the palette is rejected");
    auto mismatched = sample();
    mismatched.blocks.pop_back();
    caught = false;
    try { writeStructure(mismatched); } catch (std::runtime_error const&) { caught = true; }
    check(caught, "writing a layer that does not match the size is refused");
}

// Optional: LAMIUM_SAMPLE_STRUCTURES names a folder of real exports to parse.
void sampleFiles() {
    char* folder = nullptr;
    size_t length = 0;
    if (_dupenv_s(&folder, &length, "LAMIUM_SAMPLE_STRUCTURES") || !folder) return;
    std::unique_ptr<char, decltype(&std::free)> owned(folder, &std::free);
    for (auto const& entry : std::filesystem::directory_iterator(folder)) {
        if (entry.path().extension() != ".mcstructure") continue;
        std::ifstream file(entry.path(), std::ios::binary);
        std::string bytes{std::istreambuf_iterator<char>(file), {}};
        auto parsed = parseStructure(span(bytes));
        check(parsed.blocks.size() == parsed.cells() && !parsed.palette.empty(), "a real export parses");
        auto again = parseStructure(span(writeStructure(parsed)));
        check(again.blocks == parsed.blocks && again.palette.size() == parsed.palette.size()
              && again.blockEntities.size() == parsed.blockEntities.size() && again.entities.size() == parsed.entities.size(),
              "a real export survives writing back");
    }
}
}
void schematicTests() {
    nbtBasics();
    structureRoundTrip();
    structureRejects();
    sampleFiles();
}
