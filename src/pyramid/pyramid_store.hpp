#pragma once

#include <atomic>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>

#include <mio.hpp>

#include <errors.hpp>
#include <io/pixel_layout.hpp>
#include <pyramid/pyramid_format.hpp>

namespace enim {

/// @brief A memory-mapped .pyr file, and which of its rows are written. The OS page cache is the tile cache.
/// Thread-safe: one writer (the builder) and any number of readers (the view sampler). A reader sees a row's pixels
/// once row_ready() returns true for it.
struct PyramidStore {
    /// @brief Opens the pyramid at path if it is complete and matches the image, else creates it empty (any old file
    /// is replaced). Checks the free disk space first.
    static std::expected<std::unique_ptr<PyramidStore>, enim::Error> open_or_create(const std::filesystem::path& path,
                                                                                 const ImageInfo& info);

    const PyramidGeometry& geometry() const { return this->geom; }
    const std::filesystem::path& path() const { return this->file_path; }

    /// @brief True once every level is written and flushed (or the file was already complete when opened).
    bool complete() const { return this->is_complete.load(std::memory_order_acquire); }

    /// @brief True if row y (top-down) of the level is written. Each level's rows are written in the source's row
    /// order, so the written rows are a prefix (top-down sources) or a suffix (bottom-up sources) of the level.
    bool row_ready(int level, std::size_t y) const {
        std::size_t written = this->rows_written[level].load(std::memory_order_acquire);
        return this->bottom_up ? y >= this->geom.levels[level].height - written : y < written;
    }

    /// @brief Changes each time rows are written: the view sampler can produce a better frame.
    std::uint64_t progress() const { return this->progress_count.load(std::memory_order_acquire); }

    const std::uint8_t* pixel(int level, std::size_t x, std::size_t y) const {
        return this->base() + this->geom.pixel_offset(level, x, y);
    }

    /// @brief Writes row y of a level (width px, tightly packed, native layout) into its tiles and marks it ready.
    /// Builder thread only, each row once, in the source's row order.
    void write_row(int level, std::size_t y, const std::uint8_t* row);

    /// @brief Flushes the mapping and marks the file complete. Builder thread.
    std::expected<void, enim::Error> finish();

  private:
    std::filesystem::path file_path;
    PyramidGeometry geom;
    mio::mmap_sink map;
    bool bottom_up = false;                                 // rows arrive bottom row first
    std::unique_ptr<std::atomic<std::size_t>[]> rows_written; // per level
    std::atomic<std::uint64_t> progress_count{0};
    std::atomic<bool> is_complete{false};

    std::uint8_t* base() const { return reinterpret_cast<std::uint8_t*>(const_cast<char*>(this->map.data())); }
    void mark_all_ready();
};

} // namespace enim
