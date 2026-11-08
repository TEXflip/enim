#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <thread>
#include <vector>

#include <core/thread_pool.hpp>
#include <io/pixel_layout.hpp>
#include <pipeline/pixel_converter.hpp>
#include <pipeline/upload_queue.hpp>
#include <pyramid/pyramid_store.hpp>
#include <pyramid/tile_selection.hpp>

namespace enim {

struct TileJob {
    std::size_t tx = 0, ty = 0;
    int pass = 0; // preview refinement: more level-0 samples per pixel on each pass
};

struct TileRequest {
    std::uint64_t generation = 0; // newer requests cancel the tiles of older ones not started yet
    int level = 0;
    std::vector<TileJob> tiles;   // sampled in this order
};

/// @brief A sampled tile, converted to the ring texture's layout, waiting to be uploaded.
struct UploadTile {
    int level = 0;
    TileJob job;
    std::size_t width = 0, height = 0; // tile_pixels(): edge tiles are smaller than kTileSize
    bool complete = true;              // no fallback was used: the tile doesn't need refining
    std::vector<std::uint8_t> pixels;  // tightly packed rows
};

using TileQueue = UploadQueue<UploadTile>;

/// @brief Normal mode: turns TileRequests into UploadTiles on worker threads. Each texel is one
/// pixel of the level, converted by the PixelConverter into the ring texture's format. When a newer request arrives,
/// the tiles of the older one not started yet are skipped: the renderer sends them again if it still needs them.
///
/// Rows of the level not written yet fall back to the nearest coarser level that has them, then to level 0 read
/// straight from the source (uncompressed sources), then to the preview: random samples of the level-0 pixels each
/// texel covers, averaged. Pixels with no data stay transparent.
struct ViewSampler {
    /// @param source_pixels The source's mapped pixels (raw_mappable sources), readable at any time; empty otherwise.
    ViewSampler(const PyramidStore& store, const ImageInfo& info, std::span<const std::uint8_t> source_pixels,
                const TextureFormat& texture, const DisplayParams& display, TileQueue& queue, std::function<void()> wakeup);
    ~ViewSampler();
    ViewSampler(const ViewSampler&) = delete;
    ViewSampler& operator=(const ViewSampler&) = delete;

    /// @brief Starts producing tiles, replacing the request in progress. Main thread.
    void post(TileRequest request);

  private:
    const PyramidStore& store;
    ImageInfo info;
    std::span<const std::uint8_t> source_pixels;
    PixelConverter converter;
    std::size_t texel_bytes;
    TileQueue& queue;
    std::function<void()> wakeup;
    std::atomic<std::uint64_t> latest{0}; // generation of the newest request

    ThreadPool pool;
    std::mutex mutex;
    std::condition_variable_any request_cv;
    std::optional<TileRequest> next;
    std::jthread thread; // last: stopped and joined before the rest goes

    void run(std::stop_token stop);
    void sample_tiles(const TileRequest& request, std::stop_token stop);
    /// @brief Converts the pixels [rect.x0, rect.x1) of row y of the level into dst. @return False if a fallback was used.
    bool sample_row(int level, const LevelRect& rect, int pass, std::size_t y, std::uint8_t* dst,
                    std::vector<std::uint8_t>& scratch) const;
    bool coarser_row(int level, const LevelRect& rect, std::size_t y, std::uint8_t* dst, std::vector<std::uint8_t>& scratch) const;
    void preview_row(int level, const LevelRect& rect, int pass, std::size_t y, std::uint8_t* dst,
                     std::vector<std::uint8_t>& scratch) const;
    /// @brief Level-0 pixel (x, y) if available: from the source, or from the pyramid once written.
    const std::uint8_t* level0_pixel(std::size_t x, std::size_t y) const;
};

} // namespace enim
