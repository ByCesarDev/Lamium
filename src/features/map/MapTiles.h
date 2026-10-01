#pragma once
#include "features/map/MapView.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <unordered_map>
#include <vector>

namespace lamium::map {
// Owned per-column map data (BACKLOG L-60): the surface color and height of
// every column the scan has seen, by chunk. No game types; the scan in
// Minimap.cpp fills it, MapImage composes from it.
struct Column {
    std::uint32_t color = 0; // RGBA bytes, little-endian (r lowest); alpha 0 = unknown.
    std::int16_t height = 0;
};
inline constexpr std::uint32_t packColor(int r, int g, int b, int a = 255) {
    auto c = [](int v) { return std::uint32_t(std::clamp(v, 0, 255)); };
    return c(r) | c(g) << 8 | c(b) << 16 | c(a) << 24;
}
inline constexpr int channel(std::uint32_t color, int index) { return int((color >> (8 * index)) & 0xFF); }

struct Tile {
    std::array<Column, 256> columns{};
    bool loaded = false;  // False: the client had no chunk there when last tried.
    double scannedAt = 0; // Seconds, the caller's clock.
    int layer = 0;        // Cave view: the height the floors were found around.
    // Colors with height shading applied, rebuilt when this chunk or its
    // north or west neighbor changes; composing reads only these.
    std::array<std::uint32_t, 256> shaded{};
    bool shadedValid = false;
};

class TileCache {
    std::unordered_map<std::uint64_t, Tile> tiles;
public:
    void clear() { tiles.clear(); }
    size_t size() const { return tiles.size(); }
    Tile const* find(ChunkKey key) const {
        auto at = tiles.find(packKey(key));
        return at == tiles.end() ? nullptr : &at->second;
    }
    Tile* find(ChunkKey key) {
        auto at = tiles.find(packKey(key));
        return at == tiles.end() ? nullptr : &at->second;
    }
    Tile& put(ChunkKey key) { return tiles[packKey(key)]; }
    // A chunk's columns changed: its shading and that of the chunks south and
    // east of it (which shade against it) must be rebuilt.
    void changed(ChunkKey key) {
        for (auto k : {key, ChunkKey{key.x + 1, key.z}, ChunkKey{key.x, key.z + 1}})
            if (auto* tile = find(k)) tile->shadedValid = false;
    }
    std::optional<Column> column(int blockX, int blockZ) const {
        auto const* tile = find(chunkOf(blockX, blockZ));
        if (!tile || !tile->loaded) return std::nullopt;
        auto const& c = tile->columns[static_cast<size_t>(columnIndex(blockX, blockZ))];
        if (!(c.color >> 24)) return std::nullopt;
        return c;
    }
    // Bound memory: forget chunks farther than `keep` chunks from the center.
    void evict(ChunkKey center, int keep) {
        std::erase_if(tiles, [&](auto const& entry) {
            int x = int(std::int32_t(entry.first >> 32)), z = int(std::int32_t(entry.first & 0xFFFFFFFFu));
            return std::abs(x - center.x) > keep || std::abs(z - center.z) > keep;
        });
    }
};

// When a chunk should be scanned again: chunks near the player change most
// (the player builds and digs there); missing chunks are retried soon, since
// the client may load them any moment.
inline double rescanAfter(int chebyshev, bool loaded) {
    if (!loaded) return 0.5;
    if (chebyshev <= 1) return 1.0;
    if (chebyshev <= 4) return 4.0;
    return 15.0;
}
// Chunks to scan now, nearest first: unseen chunks before stale ones, ring by
// ring out to `radius`. At most `limit`; the caller stops early when its time
// budget runs out. A chunk scanned around another height than `layer` (more
// than `tolerance` away) counts as unseen.
inline std::vector<ChunkKey> scanOrder(TileCache const& cache, ChunkKey center, int radius, double now, size_t limit,
                                       int layer = 0, int tolerance = 1 << 20) {
    std::vector<ChunkKey> unseen, stale;
    for (int ring = 0; ring <= radius && unseen.size() < limit; ++ring) {
        auto consider = [&](int x, int z) {
            ChunkKey key{center.x + x, center.z + z};
            auto const* tile = cache.find(key);
            if (!tile || std::abs(tile->layer - layer) > tolerance) unseen.push_back(key);
            else if (now - tile->scannedAt >= rescanAfter(ring, tile->loaded)) stale.push_back(key);
        };
        if (ring == 0) { consider(0, 0); continue; }
        for (int i = -ring; i < ring; ++i) {
            consider(i, -ring);
            consider(ring, i);
            consider(-i, ring);
            consider(-ring, -i);
        }
    }
    if (unseen.size() > limit) unseen.resize(limit);
    for (auto key : stale) {
        if (unseen.size() >= limit) break;
        unseen.push_back(key);
    }
    return unseen;
}
}
