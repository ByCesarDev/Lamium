#pragma once
#include "features/map/MapFaces.h"
#include <nlohmann/json.hpp>
#include <algorithm>
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
    bool animatedFace = false; // From the animated face geometry: its texture is the skin's animated face image.
};
// The geometries a skin's resource patch names ({"geometry":{"default":...,
// "animated_face":...}}). Character-creator skins keep the head only in
// the animated face geometry.
struct SkinPatch {
    std::string geometry, animatedFace;
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
// The front (normal 0, 0, -1) faces of a poly mesh with normalized UVs, v
// counted from the bottom as the character creator writes them.
inline void polyFront(nlohmann::json const& mesh, SkinHead& head) {
    if (!mesh.is_object() || !mesh.contains("normalized_uvs") || !mesh["normalized_uvs"].is_boolean()
        || !mesh["normalized_uvs"].get<bool>())
        return;
    for (auto key : {"positions", "uvs", "normals", "polys"})
        if (!mesh.contains(key) || !mesh[key].is_array()) return;
    auto const& positions = mesh["positions"];
    auto const& uvs = mesh["uvs"];
    auto const& normals = mesh["normals"];
    for (auto const& poly : mesh["polys"]) {
        if (!poly.is_array() || poly.size() < 3) continue;
        double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9, z = 0, u0 = 1e9, v0 = 1e9, u1 = -1e9, v1 = -1e9;
        bool front = true;
        for (auto const& corner : poly) {
            if (!corner.is_array() || corner.size() < 3 || !corner[0].is_number_unsigned() || !corner[1].is_number_unsigned()
                || !corner[2].is_number_unsigned()) { front = false; break; }
            size_t p = corner[0].get<size_t>(), n = corner[1].get<size_t>(), t = corner[2].get<size_t>();
            if (p >= positions.size() || n >= normals.size() || t >= uvs.size()) { front = false; break; }
            if (!(number(normals[n], 2) < -.99)) { front = false; break; }
            double x = number(positions[p], 0), y = number(positions[p], 1);
            x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
            z = number(positions[p], 2);
            double u = number(uvs[t], 0), v = number(uvs[t], 1);
            u0 = std::min(u0, u); u1 = std::max(u1, u); v0 = std::min(v0, v); v1 = std::max(v1, v);
        }
        if (!front || !(x1 > x0) || !(y1 > y0) || !(u1 > u0) || !(v1 > v0)) continue;
        double nearer = .001 * static_cast<double>(head.boxes.size() + 1);
        head.boxes.push_back({x0, y0, x1, y1, z - nearer, u0 * head.textureWidth, (1 - v1) * head.textureHeight,
                              (u1 - u0) * head.textureWidth, (v1 - v0) * head.textureHeight});
    }
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
            if (b.contains("poly_mesh")) polyFront(b["poly_mesh"], head);
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
    // A mesh's outer layer is half a unit larger all round (cubes give it
    // as "inflate", left out): laid over the face at the face's size.
    auto const& face = head.boxes.front();
    for (auto& b : head.boxes) {
        bool around = b.x0 <= face.x0 && b.x1 >= face.x1 && b.y0 <= face.y0 && b.y1 >= face.y1;
        if (around && b.x1 - b.x0 <= face.x1 - face.x0 + 1.01 && b.y1 - b.y0 <= face.y1 - face.y0 + 1.01) {
            b.x0 = face.x0; b.x1 = face.x1; b.y0 = face.y0; b.y1 = face.y1;
        }
    }
    return head;
}
}
inline SkinPatch patchGeometry(std::string const& resourcePatch) {
    auto root = nlohmann::json::parse(resourcePatch, nullptr, false);
    if (root.is_discarded() || !root.is_object() || !root.contains("geometry") || !root["geometry"].is_object()) return {};
    auto const& geometry = root["geometry"];
    auto text = [&](char const* key) {
        return geometry.contains(key) && geometry[key].is_string() ? geometry[key].get<std::string>() : std::string{};
    };
    return {text("default"), text("animated_face")};
}
// The head in a skin's geometry JSON, in either the current
// ("minecraft:geometry") or the legacy ("geometry.name" keys) format: from
// the patch's geometry, else its animated face geometry, else the first
// geometry with a head.
inline std::optional<SkinHead> skinHead(std::string const& geometryJson, SkinPatch const& patch) {
    using namespace skin_detail;
    auto root = nlohmann::json::parse(geometryJson, nullptr, false);
    // The geometry may be kept as JSON text inside a string.
    if (root.is_string()) root = nlohmann::json::parse(root.get<std::string>(), nullptr, false);
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
    auto from = [&](Geometry const& g) {
        auto head = fromBones(*g.bones, g.width, g.height);
        if (head) head->animatedFace = !patch.animatedFace.empty() && g.id == patch.animatedFace;
        return head;
    };
    for (auto const* wanted : {&patch.geometry, &patch.animatedFace})
        for (auto const& g : found)
            if (!wanted->empty() && g.id == *wanted)
                if (auto head = from(g)) return head;
    for (auto const& g : found)
        if (auto head = from(g)) return head;
    return std::nullopt;
}
}
