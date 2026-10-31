#include "pyramid_builder.hpp"

#include <cstring>

#include <pipeline/sample_io.hpp>

namespace {

constexpr auto kWakeInterval = std::chrono::milliseconds(100);

} // namespace

namespace enim {

PyramidBuilderSink::PyramidBuilderSink(PyramidStore& store, const ImageInfo& info, std::function<void()> wakeup)
    : store(store), info(info), wakeup(std::move(wakeup)), bpp(info.layout.bytes_per_pixel()),
      swap(sample_io::needs_swap(info.layout)), last_wake(std::chrono::steady_clock::now()) {
    const PyramidGeometry& geom = store.geometry();
    this->pending.resize(geom.levels.size());
    this->scratch.resize(geom.levels.size());
    for (std::size_t l = 0; l < geom.levels.size(); ++l) {
        this->pending[l].pixels.resize(geom.levels[l].width * this->bpp);
        this->scratch[l].resize(geom.levels[l].width * this->bpp);
    }
}

void PyramidBuilderSink::on_rows(std::size_t y0, std::size_t count, const std::uint8_t* data, std::size_t stride) {
    for (std::size_t i = 0; i < count; ++i) {
        std::size_t storage_row = y0 + i;
        std::size_t y = this->info.layout.rows == RowOrder::BottomUp ? this->info.height - 1 - storage_row : storage_row;
        this->add_row(0, y, data + i * stride);
    }

    auto now = std::chrono::steady_clock::now();
    if (now - this->last_wake >= kWakeInterval) {
        this->last_wake = now;
        this->wakeup();
    }
}

void PyramidBuilderSink::add_row(int level, std::size_t y, const std::uint8_t* row) {
    this->store.write_row(level, y, row);

    const PyramidGeometry& geom = this->store.geometry();
    if (level + 1 >= geom.level_count())
        return;

    const PyramidLevel& l = geom.levels[level];
    PendingRow& waiting = this->pending[level];
    std::uint8_t* out = this->scratch[level + 1].data();
    std::size_t partner = y ^ 1;
    if (partner >= l.height) {
        this->downsample(level, row, nullptr, out); // last row of an odd height
    } else if (waiting.valid && waiting.y == partner) {
        this->downsample(level, waiting.pixels.data(), row, out);
        waiting.valid = false;
    } else {
        std::memcpy(waiting.pixels.data(), row, l.width * this->bpp);
        waiting.y = y;
        waiting.valid = true;
        return;
    }
    this->add_row(level + 1, y / 2, out);
}

void PyramidBuilderSink::downsample(int level, const std::uint8_t* a, const std::uint8_t* b, std::uint8_t* out) const {
    const PyramidGeometry& geom = this->store.geometry();
    const std::size_t src_width = geom.levels[level].width;
    const std::size_t dst_width = geom.levels[level + 1].width;
    const std::size_t bpp = this->bpp;
    const SampleType sample = geom.layout.sample;
    const std::size_t sample_bytes = sample_size(sample);
    const std::size_t channels = geom.layout.channels;

    if (sample == SampleType::U8) {
        for (std::size_t x = 0; x < dst_width; ++x) {
            std::size_t x0 = 2 * x, x1 = std::min(2 * x + 1, src_width - 1);
            unsigned n = (x1 != x0 ? 2u : 1u) * (b ? 2u : 1u);
            for (std::size_t c = 0; c < channels; ++c) {
                unsigned sum = a[x0 * bpp + c] + (x1 != x0 ? a[x1 * bpp + c] : 0u);
                if (b)
                    sum += b[x0 * bpp + c] + (x1 != x0 ? b[x1 * bpp + c] : 0u);
                out[x * bpp + c] = static_cast<std::uint8_t>((sum + n / 2) / n);
            }
        }
        return;
    }

    for (std::size_t x = 0; x < dst_width; ++x) {
        std::size_t x0 = 2 * x, x1 = std::min(2 * x + 1, src_width - 1);
        for (std::size_t c = 0; c < channels; ++c) {
            std::size_t off0 = x0 * bpp + c * sample_bytes, off1 = x1 * bpp + c * sample_bytes;
            double sum = sample_io::read_sample(a + off0, sample, this->swap);
            int n = 1;
            if (x1 != x0) {
                sum += sample_io::read_sample(a + off1, sample, this->swap);
                ++n;
            }
            if (b) {
                sum += sample_io::read_sample(b + off0, sample, this->swap);
                ++n;
                if (x1 != x0) {
                    sum += sample_io::read_sample(b + off1, sample, this->swap);
                    ++n;
                }
            }
            sample_io::write_native_sample(out + x * bpp + c * sample_bytes, sample, sum / n, this->swap);
        }
    }
}

void PyramidBuilderSink::on_done(std::expected<void, enim::Error> result) {
    if (result) {
        if (auto finished = this->store.finish(); !finished)
            result = std::unexpected(finished.error());
    }
    this->done = std::move(result);
    this->finished_flag.store(true, std::memory_order_release);
    this->wakeup();
}

} // namespace enim
