#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <io/pixel_layout.hpp>

namespace enim {

constexpr std::size_t kTileSize = 256;
constexpr std::uint32_t kPyramidVersion = 1;
constexpr std::array<char, 8> kPyramidMagic{'E', 'N', 'I', 'M', 'P', 'Y', 'R', '\0'};
constexpr std::size_t kPyramidHeaderBytes = 4096; // the tiles start on a page boundary

/// @brief Header at the start of a .pyr file. Fixed-width fields, native byte order.
struct PyramidHeader {
    std::array<char, 8> magic = kPyramidMagic;
    std::uint32_t version = kPyramidVersion;
    std::uint32_t complete = 0; // set last, once every level is written and flushed
    std::uint64_t width = 0;
    std::uint64_t height = 0;
    std::uint8_t sample = 0; // SampleType
    std::uint8_t channels = 0;
    std::uint8_t big_endian = 0;
    std::uint8_t reserved = 0;
    std::uint32_t tile_size = kTileSize;
    std::array<std::uint8_t, kMaxChannels> roles{}; // ChannelRole
    std::uint32_t level_count = 0;
    std::uint32_t reserved2 = 0;
    std::uint64_t file_size = 0;
};
static_assert(sizeof(PyramidHeader) <= kPyramidHeaderBytes);

struct PyramidLevel {
    std::size_t width = 0, height = 0;   // px
    std::size_t tiles_x = 0, tiles_y = 0;
    std::uint64_t offset = 0;            // file offset of tile (0, 0); tiles are stored row by row
};

/// @brief Where every level and tile is in a .pyr file. Levels 0..N, each half the previous one (rounded up), down to 1x1.
/// Tiles are kTileSize x kTileSize, tightly packed (edge tiles padded), top-down, in the source's channel order,
/// sample type and byte order: the levels have the source's PixelLayout with stride = kTileSize * bytes per pixel.
struct PyramidGeometry {
    std::size_t width = 0, height = 0;
    PixelLayout layout;      // stride = one tile row
    std::size_t bpp = 0;     // bytes per pixel
    std::size_t tile_bytes = 0;
    std::vector<PyramidLevel> levels;
    std::uint64_t file_size = 0;

    static PyramidGeometry make(std::size_t width, std::size_t height, const PixelLayout& source_layout) {
        PyramidGeometry g;
        g.width = width;
        g.height = height;
        g.layout = source_layout;
        g.layout.rows = RowOrder::TopDown;
        g.bpp = source_layout.bytes_per_pixel();
        g.layout.stride = kTileSize * g.bpp;
        g.tile_bytes = kTileSize * kTileSize * g.bpp;

        std::uint64_t offset = kPyramidHeaderBytes;
        std::size_t w = width, h = height;
        while (true) {
            PyramidLevel level;
            level.width = w;
            level.height = h;
            level.tiles_x = (w + kTileSize - 1) / kTileSize;
            level.tiles_y = (h + kTileSize - 1) / kTileSize;
            level.offset = offset;
            g.levels.push_back(level);
            offset += static_cast<std::uint64_t>(level.tiles_x) * level.tiles_y * g.tile_bytes;
            if (w <= 1 && h <= 1)
                break;
            w = (w + 1) / 2;
            h = (h + 1) / 2;
        }
        g.file_size = offset;
        return g;
    }

    int level_count() const { return static_cast<int>(this->levels.size()); }

    /// @brief File offset of pixel (x, y) of a level.
    std::uint64_t pixel_offset(int level, std::size_t x, std::size_t y) const {
        const PyramidLevel& l = this->levels[level];
        std::size_t tile = (y / kTileSize) * l.tiles_x + x / kTileSize;
        return l.offset + static_cast<std::uint64_t>(tile) * this->tile_bytes + (y % kTileSize) * this->layout.stride + (x % kTileSize) * this->bpp;
    }

    PyramidHeader header() const {
        PyramidHeader h;
        h.width = this->width;
        h.height = this->height;
        h.sample = static_cast<std::uint8_t>(this->layout.sample);
        h.channels = this->layout.channels;
        h.big_endian = this->layout.big_endian ? 1 : 0;
        for (std::size_t c = 0; c < kMaxChannels; ++c)
            h.roles[c] = static_cast<std::uint8_t>(this->layout.roles[c]);
        h.level_count = static_cast<std::uint32_t>(this->levels.size());
        h.file_size = this->file_size;
        return h;
    }
};

} // namespace enim
