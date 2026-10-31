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

struct FrameRequest {
    std::uint64_t generation = 0; // newer requests cancel older ones
    FramePlan plan;
    int pass = 0;                 // preview refinement: more level-0 samples per pixel on each pass
};

/// @brief Normal mode: turns FrameRequests into frame bands on worker threads. Each frame texel is one pixel of the
/// plan's level, converted by the PixelConverter into the frame texture's format. Bands go to the UploadQueue, then an
/// end-of-frame band; bands of a request replaced by a newer one are dropped.
///
/// Rows of the level not written yet fall back to the nearest coarser level that has them, then to level 0 read
/// straight from the source (uncompressed sources), then to the preview: random samples of the level-0 pixels each
/// texel covers, averaged. Pixels with no data stay transparent.
struct ViewSampler {
    /// @param source_pixels The source's mapped pixels (raw_mappable sources), readable at any time; empty otherwise.
    ViewSampler(const PyramidStore& store, const ImageInfo& info, std::span<const std::uint8_t> source_pixels,
                const TextureFormat& texture, const DisplayParams& display, UploadQueue& queue, std::function<void()> wakeup);
    ~ViewSampler();
    ViewSampler(const ViewSampler&) = delete;
    ViewSampler& operator=(const ViewSampler&) = delete;

    /// @brief Starts producing a frame, replacing the one in progress. Main thread.
    void post(const FrameRequest& request);

  private:
    const PyramidStore& store;
    ImageInfo info;
    std::span<const std::uint8_t> source_pixels;
    PixelConverter converter;
    std::size_t texel_bytes;
    UploadQueue& queue;
    std::function<void()> wakeup;
    std::atomic<std::uint64_t> latest{0}; // generation of the newest request

    ThreadPool pool;
    std::mutex mutex;
    std::condition_variable_any request_cv;
    std::optional<FrameRequest> next;
    std::jthread thread; // last: stopped and joined before the rest goes

    void run(std::stop_token stop);
    void sample_frame(const FrameRequest& request, std::stop_token stop);
    /// @brief Converts row y of the plan's level into dst. @return False if a fallback was used.
    bool sample_row(const FrameRequest& request, std::size_t y, std::uint8_t* dst, std::vector<std::uint8_t>& scratch) const;
    bool coarser_row(const FrameRequest& request, std::size_t y, std::uint8_t* dst, std::vector<std::uint8_t>& scratch) const;
    void preview_row(const FrameRequest& request, std::size_t y, std::uint8_t* dst, std::vector<std::uint8_t>& scratch) const;
    /// @brief Level-0 pixel (x, y) if available: from the source, or from the pyramid once written.
    const std::uint8_t* level0_pixel(std::size_t x, std::size_t y) const;
};

} // namespace enim
