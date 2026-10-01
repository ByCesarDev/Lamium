#pragma once
#include "features/map/MapFaces.h"
#include <nlohmann/json.hpp>
#include <cctype>
#include <optional>
#include <string>
#include <vector>

namespace lamium::map {
// A player's head from their skin's own geometry (L-87): character-creator
// skins and skins with custom models do not use the classic layout, so the
// "head" bone and the bones hanging from it (the "hat" outer layer) say
// where the face is. Pure; RadarFaces.cpp passes the skin's geometry JSON.
struct SkinHead {
    std::vector<FaceBox> boxes;
    double textureWidth = 64, textureHeight = 64;
};
namespace skin_detail {
inline std::string lower(std::string text) {
    for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
inline double number(nlohmann::json const& array, size_t i) {
    return array.is_array() && i < array.size() && array[i].is_number() ? array[i].get<double>() : 0;
}
// The front (north) face of a cube in texture units, from per-face or box UV.
inline bool front(nlohmann::json const& cube, double w, double h, double d, double& u, double& v, double& uw, double& vh) {
    auto const& uv = cube.contains("uv") ? cube["uv"] : nlohmann::json();
    if (uv.is_object()) {
        if (!uv.contains("north") || !uv["north"].is_object()) return false;
        auto const& north = uv["north"];
        if (!north.contains("uv")) return false;
        u = number(north["uv"], 0); v = number(north["uv"], 1);
        uw = north.contains("uv_size") ? number(north["uv_size"], 0) : w;
        vh = north.contains("uv_size") ? number(north["uv_size"], 1) : h;
        return true;
    }
    if (!uv.is_array()) return false;
    u = number(uv, 0) + d; v = number(uv, 1) + d; uw = w; vh = h;
    return true;
}
// Worn layers that are not part of the face (armor).
inline bool skipped(std::string const& bone) {
    return bone.find("helmet") != std::string::npos || bone.find("armor") != std::string::npos;
}
inline std::optional<SkinHead> fromBones(nlohmann::json const& bones, double textureWidth, double textureHeight) {
    if (!bones.is_array()) return std::nullopt;
    struct Bone { std::string name, parent; nlohmann::json const* json; };
    std::vector<Bone> list;
    for (auto const& b : bones) {
        if (!b.is_object() || !b.contains("name") || !b["name"].is_string()) continue;
        if (b.contains("neverRender") && b["neverRender"].is_boolean() && b["neverRender"].get<bool>()) continue;
        std::string parent = b.contains("parent") && b["parent"].is_string() ? lower(b["parent"].get<std::string>()) : "";
        list.push_back({lower(b["name"].get<std::string>()), parent, &b});
    }
    auto underHead = [&](Bone const& bone) {
        std::string name = bone.name, parent = bone.parent;
        for (int depth = 0; depth < 8; ++depth) {
            if (name == "head") return true;
            if (parent.empty()) return false;
            name = parent;
            parent.clear();
            for (auto const& other : list)
                if (other.name == name) { parent = other.parent; break; }
        }
        return false;
    };
    SkinHead head{{}, textureWidth > 0 ? textureWidth : 64, textureHeight > 0 ? textureHeight : 64};
    // The head first, then its layers, each a little nearer than the last,
    // so an outer layer at the same place covers the face.
    for (int pass = 0; pass < 2; ++pass)
        for (auto const& bone : list) {
            if ((bone.name == "head") != (pass == 0) || skipped(bone.name) || !underHead(bone)) continue;
            auto const& b = *bone.json;
            if (!b.contains("cubes") || !b["cubes"].is_array()) continue;
            for (auto const& cube : b["cubes"]) {
                if (!cube.is_object() || !cube.contains("origin") || !cube.contains("size")) continue;
                double x = number(cube["origin"], 0), y = number(cube["origin"], 1), z = number(cube["origin"], 2);
                double w = number(cube["size"], 0), h = number(cube["size"], 1), d = number(cube["size"], 2);
                double u, v, uw, vh;
                if (!(w > 0) || !(h > 0) || !front(cube, w, h, d, u, v, uw, vh)) continue;
                double nearer = .001 * static_cast<double>(head.boxes.size() + 1);
                head.boxes.push_back({x, y, x + w, y + h, z - nearer, u, v, uw, vh});
            }
        }
    if (head.boxes.empty()) return std::nullopt;
    return head;
}
}
// The geometry a skin's resource patch names ({"geometry":{"default":...}}).
inline std::string patchGeometry(std::string const& resourcePatch) {
    auto root = nlohmann::json::parse(resourcePatch, nullptr, false);
    if (root.is_discarded() || !root.is_object() || !root.contains("geometry") || !root["geometry"].is_object()) return {};
    auto const& geometry = root["geometry"];
    return geometry.contains("default") && geometry["default"].is_string() ? geometry["default"].get<std::string>() : "";
}
// The head of the geometry named `wanted` (or the first with a head) in a
// skin's geometry JSON, in either the current ("minecraft:geometry") or
// the legacy ("geometry.name" keys) format.
inline std::optional<SkinHead> skinHead(std::string const& geometryJson, std::string const& wanted) {
    using namespace skin_detail;
    auto root = nlohmann::json::parse(geometryJson, nullptr, false);
    if (root.is_discarded() || !root.is_object()) return std::nullopt;
    struct Geometry { std::string id; nlohmann::json const* bones; double width, height; };
    std::vector<Geometry> found;
    auto size = [](nlohmann::json const& from, char const* key) {
        return from.contains(key) && from[key].is_number() ? from[key].get<double>() : 64.0;
    };
    if (root.contains("minecraft:geometry") && root["minecraft:geometry"].is_array())
        for (auto const& g : root["minecraft:geometry"]) {
            if (!g.is_object() || !g.contains("bones")) continue;
            auto const& d = g.contains("description") && g["description"].is_object() ? g["description"] : nlohmann::json::object();
            std::string id = d.contains("identifier") && d["identifier"].is_string() ? d["identifier"].get<std::string>() : "";
            found.push_back({id, &g["bones"], size(d, "texture_width"), size(d, "texture_height")});
        }
    for (auto const& [key, g] : root.items()) {
        if (key.rfind("geometry.", 0) != 0 || !g.is_object() || !g.contains("bones")) continue;
        found.push_back({key.substr(0, key.find(':')), &g["bones"], size(g, "texturewidth"), size(g, "textureheight")});
    }
    for (auto const& g : found)
        if (!wanted.empty() && g.id == wanted)
            if (auto head = fromBones(*g.bones, g.width, g.height)) return head;
    for (auto const& g : found)
        if (auto head = fromBones(*g.bones, g.width, g.height)) return head;
    return std::nullopt;
}
}
