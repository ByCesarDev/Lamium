#pragma once
#include <filesystem>
#include <optional>
#include <string_view>

namespace lamium::overlay {
// Use the game's current world-storage root, never a guessed profile directory.
// A local sidecar naturally separates profiles and travels with a copied world.
// `fileName` is the document inside the world's Lamium folder.
inline std::optional<std::filesystem::path> localWorldFile(
    std::filesystem::path const& worlds, std::string_view levelId, std::string_view fileName) {
    if (!worlds.is_absolute() || levelId.empty() || levelId.size() > 128
        || levelId == "." || levelId == ".." || levelId.back() == '.' || levelId.back() == ' ')
        return {};
    for (unsigned char c : levelId)
        if (c < 32 || c == 127 || std::string_view("/\\:*?\"<>|").find(c) != std::string_view::npos) return {};
    auto root = std::filesystem::canonical(worlds);
    auto world = root / std::filesystem::u8path(levelId);
    if (!std::filesystem::is_directory(world) || !std::filesystem::is_regular_file(world / "level.dat")) return {};
    world = std::filesystem::canonical(world);
    if (!std::filesystem::equivalent(world.parent_path(),root)) return {};
    auto folder = world / "lamium";
    if (std::filesystem::exists(folder)
        && (!std::filesystem::is_directory(folder)
            || !std::filesystem::equivalent(std::filesystem::canonical(folder).parent_path(),world)))
        throw std::runtime_error("Invalid Lamium world data directory");
    return folder / std::filesystem::path(fileName);
}
inline std::optional<std::filesystem::path> localShapePath(
    std::filesystem::path const& worlds, std::string_view levelId) {
    return localWorldFile(worlds, levelId, "shapes.json");
}
}
