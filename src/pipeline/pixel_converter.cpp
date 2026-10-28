#include "pixel_converter.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace {

using enim::SampleType;

float half_to_float(std::uint16_t h) {
    std::uint32_t sign = static_cast<std::uint32_t>(h & 0x8000) << 16;
    std::uint32_t exponent = (h >> 10) & 0x1F;
    std::uint32_t mantissa = h & 0x3FF;
    if (exponent == 0) // zero or subnormal: mantissa * 2^-24
        return (sign ? -1.0f : 1.0f) * std::ldexp(static_cast<float>(mantissa), -24);
    if (exponent == 31) // inf or nan
        return std::bit_cast<float>(sign | 0x7F800000 | (mantissa << 13));
    return std::bit_cast<float>(sign | ((exponent + 112) << 23) | (mantissa << 13));
}

std::uint16_t float_to_half(float f) {
    std::uint32_t bits = std::bit_cast<std::uint32_t>(f);
    std::uint16_t sign = static_cast<std::uint16_t>((bits >> 16) & 0x8000);
    std::int32_t exponent = static_cast<std::int32_t>((bits >> 23) & 0xFF) - 127 + 15;
    std::uint32_t mantissa = bits & 0x7FFFFF;
    if (((bits >> 23) & 0xFF) == 0xFF) // inf or nan
        return sign | 0x7C00 | (mantissa ? 0x200 : 0);
    if (exponent >= 31) // overflow: inf
        return sign | 0x7C00;
    if (exponent <= 0) { // subnormal or zero
        if (exponent < -10)
            return sign;
        mantissa |= 0x800000;
        std::uint32_t shift = static_cast<std::uint32_t>(14 - exponent);
        std::uint32_t rounded = (mantissa + (1u << (shift - 1))) >> shift;
        return sign | static_cast<std::uint16_t>(rounded);
    }
    std::uint32_t rounded = mantissa + 0x1000; // round to nearest
    if (rounded & 0x800000) {
        rounded = 0;
        ++exponent;
        if (exponent >= 31)
            return sign | 0x7C00;
    }
    return sign | static_cast<std::uint16_t>(exponent << 10) | static_cast<std::uint16_t>(rounded >> 13);
}

template <typename T>
T load(const std::uint8_t* p, bool swap) {
    T value;
    std::memcpy(&value, p, sizeof(T));
    return swap ? std::byteswap(value) : value;
}

float read_sample(const std::uint8_t* p, SampleType type, bool swap) {
    switch (type) {
    case SampleType::U8: return static_cast<float>(*p);
    case SampleType::I8: return static_cast<float>(static_cast<std::int8_t>(*p));
    case SampleType::U16: return static_cast<float>(load<std::uint16_t>(p, swap));
    case SampleType::I16: return static_cast<float>(static_cast<std::int16_t>(load<std::uint16_t>(p, swap)));
    case SampleType::U32: return static_cast<float>(load<std::uint32_t>(p, swap));
    case SampleType::I32: return static_cast<float>(static_cast<std::int32_t>(load<std::uint32_t>(p, swap)));
    case SampleType::F16: return half_to_float(load<std::uint16_t>(p, swap));
    case SampleType::F32: return std::bit_cast<float>(load<std::uint32_t>(p, swap));
    }
    return 0.0f;
}

/// @brief Writes a normalized value t (0..1 maps to the display range) in the texture's sample type.
void write_sample(std::uint8_t* p, SampleType type, float t) {
    switch (type) {
    case SampleType::U8: *p = static_cast<std::uint8_t>(std::lround(std::clamp(t, 0.0f, 1.0f) * 255.0f)); break;
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
    default: std::memcpy(p, &t, sizeof(t)); break; // F32: float data is not clamped (HDR)
    }
}

} // namespace

namespace enim {

PixelConverter::PixelConverter(const PixelLayout& src, std::size_t width, const TextureFormat& dst, const DisplayParams& display)
    : src(src), width(width), dst(dst), texel(texel_channels(dst, display)), range(display.range),
      swap_bytes(sample_size(src.sample) > 1 && src.big_endian != (std::endian::native == std::endian::big)) {
    this->fast_u8 = src.sample == SampleType::U8 && dst.sample == SampleType::U8 && display.range == default_range(SampleType::U8);
}

void PixelConverter::convert_rows(const std::uint8_t* src, std::size_t src_stride, std::size_t count, std::uint8_t* dst,
                                  std::ptrdiff_t dst_stride) const {
    for (std::size_t y = 0; y < count; ++y) {
        const std::uint8_t* src_row = src + y * src_stride;
        std::uint8_t* dst_row = dst + static_cast<std::ptrdiff_t>(y) * dst_stride;
        if (this->fast_u8)
            this->convert_row_u8(src_row, dst_row);
        else
            this->convert_row(src_row, dst_row);
    }
}

void PixelConverter::convert_row_u8(const std::uint8_t* src, std::uint8_t* dst) const {
    const std::size_t src_bpp = this->src.channels;
    const std::size_t channels = this->dst.channels;
    std::array<std::uint8_t, 4> constants{};
    for (std::size_t k = 0; k < channels; ++k)
        constants[k] = static_cast<std::uint8_t>(std::lround(std::clamp(this->texel.constant[k], 0.0f, 1.0f) * 255.0f));

    for (std::size_t x = 0; x < this->width; ++x, src += src_bpp, dst += channels)
        for (std::size_t k = 0; k < channels; ++k) {
            std::int8_t c = this->texel.source[k];
            dst[k] = c >= 0 ? src[c] : constants[k];
        }
}

void PixelConverter::convert_row(const std::uint8_t* src, std::uint8_t* dst) const {
    const std::size_t sample_bytes = sample_size(this->src.sample);
    const std::size_t src_bpp = this->src.bytes_per_pixel();
    const std::size_t channels = this->dst.channels;
    const std::size_t dst_sample_bytes = sample_size(this->dst.sample);
    const float scale = 1.0f / (this->range.y - this->range.x);

    for (std::size_t x = 0; x < this->width; ++x, src += src_bpp)
        for (std::size_t k = 0; k < channels; ++k, dst += dst_sample_bytes) {
            std::int8_t c = this->texel.source[k];
            float t = c >= 0 ? (read_sample(src + c * sample_bytes, this->src.sample, this->swap_bytes) - this->range.x) * scale
                             : this->texel.constant[k];
            write_sample(dst, this->dst.sample, t);
        }
}

} // namespace enim
