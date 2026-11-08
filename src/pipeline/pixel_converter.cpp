#include "pixel_converter.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <pipeline/sample_io.hpp>

namespace {

using enim::SampleType;
using enim::sample_io::float_to_half;
using enim::sample_io::read_sample;

/// @brief Writes a normalized value t (0..1 maps to the display range) in the texture's sample type.
void write_sample(std::uint8_t* p, SampleType type, float t) {
    switch (type) {
    case SampleType::U8:
        *p = static_cast<std::uint8_t>(std::lround(std::clamp(t, 0.0f, 1.0f) * 255.0f));
        break;
    case SampleType::U16: {
        auto v = static_cast<std::uint16_t>(std::lround(std::clamp(t, 0.0f, 1.0f) * 65535.0f));
        std::memcpy(p, &v, sizeof(v));
        break;
    }
    case SampleType::F16: {
        std::uint16_t v = float_to_half(t);
        std::memcpy(p, &v, sizeof(v));
        break;
    }
    default:
        std::memcpy(p, &t, sizeof(t));
        break; // F32: float data is not clamped (HDR)
    }
}

} // namespace

namespace enim {

PixelConverter::PixelConverter(const PixelLayout& src, std::size_t width, const TextureFormat& dst, const DisplayParams& display, bool premultiply)
    : src(src), width(width), dst(dst), texel(texel_channels(dst, display)), range(display.range), swap_bytes(sample_io::needs_swap(src)) {
    this->fast_u8 = src.sample == SampleType::U8 && dst.sample == SampleType::U8 && display.range == default_range(SampleType::U8);
    // a constant alpha is 1: premultiplying would change nothing
    const int last = static_cast<int>(dst.channels) - 1;
    if (premultiply && (dst.channels == 2 || dst.channels == 4) && this->texel.source[last] >= 0)
        this->alpha = last;
}

void PixelConverter::convert_rows(const std::uint8_t* src, std::size_t src_stride, std::size_t count, std::uint8_t* dst,
                                  std::ptrdiff_t dst_stride) const {
    for (std::size_t y = 0; y < count; ++y) {
        const std::uint8_t* src_row = src + y * src_stride;
        std::uint8_t* dst_row = dst + static_cast<std::ptrdiff_t>(y) * dst_stride;
        this->convert_span(src_row, this->width, dst_row);
    }
}

void PixelConverter::convert_span(const std::uint8_t* src, std::size_t count, std::uint8_t* dst) const {
    if (this->fast_u8)
        this->convert_span_u8(src, count, dst);
    else
        this->convert_span_any(src, count, dst);
}

void PixelConverter::convert_span_u8(const std::uint8_t* src, std::size_t count, std::uint8_t* dst) const {
    const std::size_t src_bpp = this->src.channels; // bytes per pixel for the source row (U8 only, fast path)
    const std::size_t channels = this->dst.channels;
    std::array<std::uint8_t, 4> constants{};
    for (std::size_t k = 0; k < channels; ++k)
        constants[k] = static_cast<std::uint8_t>(std::lround(std::clamp(this->texel.constant[k], 0.0f, 1.0f) * 255.0f));

    for (std::size_t x = 0; x < count; ++x, src += src_bpp, dst += channels) {
        for (std::size_t k = 0; k < channels; ++k) {
            std::int8_t c = this->texel.source[k];
            dst[k] = c >= 0 ? src[c] : constants[k];
        }
        if (this->alpha >= 0) {
            const unsigned a = dst[this->alpha];
            for (int k = 0; k < this->alpha; ++k)
                dst[k] = static_cast<std::uint8_t>((dst[k] * a + 127) / 255);
        }
    }
}

void PixelConverter::convert_span_any(const std::uint8_t* src, std::size_t count, std::uint8_t* dst) const {
    const std::size_t sample_bytes = sample_size(this->src.sample);
    const std::size_t src_bpp = this->src.bytes_per_pixel();
    const std::size_t channels = this->dst.channels;
    const std::size_t dst_sample_bytes = sample_size(this->dst.sample);
    const float scale = 1.0f / (this->range.y - this->range.x);

    std::array<float, 4> t{};
    for (std::size_t x = 0; x < count; ++x, src += src_bpp) {
        for (std::size_t k = 0; k < channels; ++k) {
            std::int8_t c = this->texel.source[k];
            t[k] =
                c >= 0 ? (read_sample(src + c * sample_bytes, this->src.sample, this->swap_bytes) - this->range.x) * scale : this->texel.constant[k];
        }
        if (this->alpha >= 0) {
            const float a = std::clamp(t[this->alpha], 0.0f, 1.0f);
            for (int k = 0; k < this->alpha; ++k)
                t[k] *= a;
        }
        for (std::size_t k = 0; k < channels; ++k, dst += dst_sample_bytes)
            write_sample(dst, this->dst.sample, t[k]);
    }
}

} // namespace enim
