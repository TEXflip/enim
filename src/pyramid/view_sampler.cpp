#include "view_sampler.hpp"

#include <algorithm>
#include <array>
#include <cstring>

#include <pipeline/sample_io.hpp>

namespace {

constexpr std::size_t kBandRows = 64;
constexpr int kMaxPreviewSamples = 16;

/// @brief Fixed pseudo-random value per (x, y, sample): each preview pass reuses the previous pass's samples.
std::uint64_t mix(std::uint64_t x, std::uint64_t y, std::uint64_t i) {
    std::uint64_t h = x * 0x9E3779B97F4A7C15ull ^ (y + 0x632BE59BD9B4E019ull) * 0xC2B2AE3D27D4EB4Full ^ i * 0x165667B19E3779F9ull;
    h ^= h >> 31;
    h *= 0xBF58476D1CE4E5B9ull;
    h ^= h >> 29;
    h *= 0x94D049BB133111EBull;
    return h ^ (h >> 32);
}

} // namespace

namespace enim {

ViewSampler::ViewSampler(const PyramidStore& store, const ImageInfo& info, std::span<const std::uint8_t> source_pixels,
                         const TextureFormat& texture, const DisplayParams& display, UploadQueue& queue,
                         std::function<void()> wakeup)
    : store(store), info(info), source_pixels(source_pixels), converter(info.layout, info.width, texture, display, true),
      texel_bytes(texture.bytes_per_pixel()), queue(queue), wakeup(std::move(wakeup)), pool(ThreadPool::default_workers()) {
    this->thread = std::jthread([this](std::stop_token stop) { this->run(stop); });
}

ViewSampler::~ViewSampler() {
    this->thread.request_stop();
    this->request_cv.notify_all();
}

void ViewSampler::post(const FrameRequest& request) {
    {
        std::lock_guard lock(this->mutex);
        this->next = request;
        this->latest.store(request.generation, std::memory_order_release);
    }
    this->request_cv.notify_all();
}

void ViewSampler::run(std::stop_token stop) {
    while (true) {
        FrameRequest request;
        {
            std::unique_lock lock(this->mutex);
            if (!this->request_cv.wait(lock, stop, [&] { return this->next.has_value(); }))
                return;
            request = *this->next;
            this->next.reset();
        }
        this->sample_frame(request, stop);
    }
}

void ViewSampler::sample_frame(const FrameRequest& request, std::stop_token stop) {
    const std::size_t width = request.plan.rect.width();
    const std::size_t height = request.plan.rect.height();
    const std::size_t row_bytes = width * this->texel_bytes;
    const std::size_t band_count = (height + kBandRows - 1) / kBandRows;
    std::atomic<bool> exact{true};

    auto stale = [&] { return stop.stop_requested() || this->latest.load(std::memory_order_acquire) != request.generation; };

    this->pool.run(band_count, [&](std::size_t band_index) {
        if (stale())
            return;
        UploadBand band;
        band.generation = request.generation;
        band.first_row = band_index * kBandRows;
        band.rows = std::min(kBandRows, height - band.first_row);
        band.pixels.resize(band.rows * row_bytes);
        std::vector<std::uint8_t> scratch(width * this->info.layout.bytes_per_pixel());
        for (std::size_t r = 0; r < band.rows; ++r)
            if (!this->sample_row(request, request.plan.rect.y0 + band.first_row + r, band.pixels.data() + r * row_bytes, scratch))
                exact.store(false, std::memory_order_relaxed);
        if (!stale() && this->queue.push(std::move(band), stop))
            this->wakeup();
    });

    if (stale())
        return;
    UploadBand end;
    end.generation = request.generation;
    end.end_of_frame = true;
    end.complete = exact.load();
    if (this->queue.push(std::move(end), stop))
        this->wakeup();
}

bool ViewSampler::sample_row(const FrameRequest& request, std::size_t y, std::uint8_t* dst, std::vector<std::uint8_t>& scratch) const {
    const int level = request.plan.level;
    const LevelRect& rect = request.plan.rect;

    if (this->store.row_ready(level, y)) {
        // one span per tile the row crosses
        for (std::size_t x = rect.x0; x < rect.x1;) {
            std::size_t tile_end = std::min((x / kTileSize + 1) * kTileSize, rect.x1);
            this->converter.convert_span(this->store.pixel(level, x, y), tile_end - x, dst + (x - rect.x0) * this->texel_bytes);
            x = tile_end;
        }
        return true;
    }

    if (level == 0 && !this->source_pixels.empty()) {
        const PixelLayout& layout = this->info.layout;
        std::size_t storage_row = layout.rows == RowOrder::BottomUp ? this->info.height - 1 - y : y;
        this->converter.convert_span(this->source_pixels.data() + storage_row * layout.stride + rect.x0 * layout.bytes_per_pixel(),
                                     rect.width(), dst);
        return true;
    }

    if (!this->coarser_row(request, y, dst, scratch))
        this->preview_row(request, y, dst, scratch);
    return false;
}

bool ViewSampler::coarser_row(const FrameRequest& request, std::size_t y, std::uint8_t* dst, std::vector<std::uint8_t>& scratch) const {
    const std::size_t bpp = this->info.layout.bytes_per_pixel();
    const LevelRect& rect = request.plan.rect;
    const PyramidGeometry& geom = this->store.geometry();
    for (int coarse = request.plan.level + 1; coarse < geom.level_count(); ++coarse) {
        int shift = coarse - request.plan.level;
        std::size_t cy = y >> shift;
        if (!this->store.row_ready(coarse, cy))
            continue;
        for (std::size_t x = rect.x0; x < rect.x1; ++x)
            std::memcpy(scratch.data() + (x - rect.x0) * bpp, this->store.pixel(coarse, x >> shift, cy), bpp);
        this->converter.convert_span(scratch.data(), rect.width(), dst);
        return true;
    }
    return false;
}

const std::uint8_t* ViewSampler::level0_pixel(std::size_t x, std::size_t y) const {
    if (!this->source_pixels.empty()) {
        const PixelLayout& layout = this->info.layout;
        std::size_t storage_row = layout.rows == RowOrder::BottomUp ? this->info.height - 1 - y : y;
        return this->source_pixels.data() + storage_row * layout.stride + x * layout.bytes_per_pixel();
    }
    return this->store.row_ready(0, y) ? this->store.pixel(0, x, y) : nullptr;
}

void ViewSampler::preview_row(const FrameRequest& request, std::size_t y, std::uint8_t* dst, std::vector<std::uint8_t>& scratch) const {
    const PixelLayout& layout = this->info.layout;
    const std::size_t bpp = layout.bytes_per_pixel();
    const std::size_t sample_bytes = sample_size(layout.sample);
    const bool swap = sample_io::needs_swap(layout);
    const int level = request.plan.level;
    const LevelRect& rect = request.plan.rect;
    const int samples = std::min(1 << std::min(request.pass, 4), kMaxPreviewSamples);

    // level-0 rows covered by this row
    const std::size_t fy0 = y << level;
    const std::size_t fh = std::min(std::size_t{1} << level, this->info.height - fy0);
    std::vector<bool> missing(rect.width(), false);
    std::array<double, kMaxChannels> sum{};

    for (std::size_t x = rect.x0; x < rect.x1; ++x) {
        const std::size_t fx0 = x << level;
        const std::size_t fw = std::min(std::size_t{1} << level, this->info.width - fx0);
        sum.fill(0.0);
        int found = 0;
        for (int i = 0; i < samples; ++i) {
            std::uint64_t h = mix(x, y, static_cast<std::uint64_t>(i));
            std::size_t sx = fx0 + (h & 0xFFFFFFFF) % fw;
            std::size_t sy = fy0 + (h >> 32) % fh;
            const std::uint8_t* p = this->level0_pixel(sx, sy);
            if (!p)
                continue;
            for (std::size_t c = 0; c < layout.channels; ++c)
                sum[c] += sample_io::read_sample(p + c * sample_bytes, layout.sample, swap);
            ++found;
        }
        std::uint8_t* out = scratch.data() + (x - rect.x0) * bpp;
        if (found == 0) {
            std::memset(out, 0, bpp);
            missing[x - rect.x0] = true;
            continue;
        }
        for (std::size_t c = 0; c < layout.channels; ++c)
            sample_io::write_native_sample(out + c * sample_bytes, layout.sample, sum[c] / found, swap);
    }

    this->converter.convert_span(scratch.data(), rect.width(), dst);
    for (std::size_t i = 0; i < missing.size(); ++i)
        if (missing[i])
            std::memset(dst + i * this->texel_bytes, 0, this->texel_bytes); // transparent
}

} // namespace enim
